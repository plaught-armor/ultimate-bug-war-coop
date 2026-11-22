// Card 593 step 4 (performance mod family, host diag): which navmesh each AI agent uses, and who resolves nav data.
//
// DefaultEngine.ini lists five supported agents (Default, Small, Medium, Large, Gigantic), each with its own
// RecastNavMesh, and every dirty batch rebuilds all five. A navmesh no agent ever resolves to is a rebuild to cut.
//
// Row 1, per AI class at its first possession (ASTNonPlayableCharacterBase::PossessedBy, after the original): the
// capsule radius and half height, the CharacterMovement NavAgentProps (radius, height, step, preferred nav data), and
// the nav data ASTNonPlayableCharacterBase::GetNavData returns.
// Row 2, the whole-run counter: every UNavigationSystemV1::GetNavDataForProps(const FNavAgentProperties&) const
// result, by (nav data, caller RVA), on the host game thread. The non-const and the location overloads share a 9-byte
// thunk (0x0356F530) that tail-jumps to vtable +0x358, this body, so their callers show here by return address. This
// catches non-pawn users too (EQS, ability nav queries, the mod's own goto). One `card593: host nav resolves` row per
// nav data rides on the card577 row cadence; the table is never reset.
// FNavAgentProperties: AgentRadius at +4, AgentHeight at +8 (the resolve body's cvttss2si reads, 2026-11-21).
#include "ubw/hooks_internal.h"

#include <atomic>
#include <intrin.h>
#include <string.h>

namespace ubw::hooks {

C593ResolveFn g_orig_c593_resolve = nullptr;
C593PossessFn g_orig_c593_possess = nullptr;

namespace {

constexpr uint32_t kC593Pairs = 48;   // (nav data, caller) pairs; more are counted as overflow
constexpr uint32_t kC593AiClasses = 32;
constexpr uint32_t kC593NavShown = 8; // distinct nav data per row set
constexpr size_t kC593PropRadius = 0x4;
constexpr size_t kC593PropHeight = 0x8;

struct C593Pair {
    const void* nav;
    uintptr_t caller;
    float radius, height; // the first query's agent
    uint32_t n;
    ObjectName name; // taken at the first tally: the table outlives map travel, so `nav` is compared, never read
};
struct C593Resolves {
    C593Pair p[kC593Pairs];
    uint32_t n, total, overflow;
};
C593Resolves g_c593r{};                     // game thread only
std::atomic<uint32_t> g_c593r_off_thread{0}; // resolves on another thread, or before the game thread is known
std::atomic<DWORD> g_c593_game_thread{0};    // set by the possession hook (game thread)
bool g_c593_probe = false;                   // the possession row's own GetNavData call: not counted

const void* g_c593_seen[kC593AiClasses] = {};
uint32_t g_c593_seen_n = 0;

uintptr_t c593_rva(const void* p) {
    return reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(ue::hooktable::g_exe_base);
}

void c593_tally_resolve(const void* nav, const uint8_t* props, uintptr_t caller) {
    ++g_c593r.total;
    for (uint32_t i = 0; i < g_c593r.n; ++i) {
        if (g_c593r.p[i].nav == nav && g_c593r.p[i].caller == caller) {
            ++g_c593r.p[i].n;
            return;
        }
    }
    if (g_c593r.n >= kC593Pairs) {
        ++g_c593r.overflow;
        return;
    }
    const float radius = props != nullptr ? read_f32(props, kC593PropRadius) : -1.0f;
    const float height = props != nullptr ? read_f32(props, kC593PropHeight) : -1.0f;
    C593Pair& p = g_c593r.p[g_c593r.n++];
    p = C593Pair{nav, caller, radius, height, 1, {}};
    if (nav != nullptr) {
        p.name = object_name(nav);
    } else {
        memcpy(p.name.text, "(null)", sizeof("(null)"));
    }
    log::line("card593: %s t=%llu first resolve to %s from caller 0x%llX (agent radius %.0f, height %.0f)", side(),
              static_cast<unsigned long long>(GetTickCount64()), p.name.text, static_cast<unsigned long long>(caller),
              radius, height);
}

// A float property of `obj` by name, -1 when absent or not a FloatProperty.
float c593_float(const void* obj, const void* ustruct, const char* name) {
    const uint8_t* const prop = ustruct != nullptr ? prop_find_prop(ustruct, name) : nullptr;
    if (prop == nullptr || !prop_kind_is(prop, "FloatProperty")) {
        return -1.0f;
    }
    return read_f32(obj, static_cast<size_t>(read_i32(prop, kPropertyOffset)));
}

struct C593Agent {
    float radius, height, step;
    ObjectName pref[3]; // PreferredNavData FNames at +0x8, +0x10, +0x18 (see c593_agent_props)
    bool found;
};

C593Agent c593_agent_props(const void* movement) {
    C593Agent a{-1.0f, -1.0f, -1.0f, {}, false};
    const uint8_t* const prop =
        movement != nullptr ? prop_find_prop(read_ptr(movement, kObjectClass), "NavAgentProps") : nullptr;
    const void* const st = prop != nullptr ? read_ptr(prop, kStructPropStruct) : nullptr;
    if (st == nullptr) {
        return a;
    }
    const auto* const base = static_cast<const uint8_t*>(movement) + read_i32(prop, kPropertyOffset);
    a.found = true;
    a.radius = c593_float(base, st, "AgentRadius");
    a.height = c593_float(base, st, "AgentHeight");
    a.step = c593_float(base, st, "AgentStepHeight");
    const uint8_t* const pref = prop_find_prop(st, "PreferredNavData");
    if (pref != nullptr) {
        // TSoftClassPtr: FWeakObjectPtr (8 bytes), then the FSoftObjectPath (package, asset FNames) at +0x8, or at
        // +0x10 when the engine keeps an int32 tag between them. Three FNames cover both layouts; unset reads None.
        const uint8_t* const path = base + read_i32(pref, kPropertyOffset);
        a.pref[0] = fname_text(path + 0x8);
        a.pref[1] = fname_text(path + 0x10);
        a.pref[2] = fname_text(path + 0x18);
    }
    return a;
}

void c593_class_row(void* pawn) {
    const void* const cls = read_ptr(pawn, kObjectClass);
    for (uint32_t i = 0; i < g_c593_seen_n; ++i) {
        if (g_c593_seen[i] == cls) {
            return;
        }
    }
    if (g_c593_seen_n >= kC593AiClasses) {
        return;
    }
    g_c593_seen[g_c593_seen_n++] = cls;
    const uint8_t* field = nullptr;
    const void* const capsule = prop_object_field(pawn, "CapsuleComponent", &field);
    const void* const movement = prop_object_field(pawn, "CharacterMovement", &field);
    const void* const cap_cls = capsule != nullptr ? read_ptr(capsule, kObjectClass) : nullptr;
    const C593Agent a = c593_agent_props(movement);
    const auto get_nav = reinterpret_cast<C593GetNavDataFn>(reinterpret_cast<uintptr_t>(ue::hooktable::g_exe_base) +
                                                            offsets::ASTNonPlayableCharacterBase_GetNavData.rva);
    g_c593_probe = true;
    const void* const nav = get_nav(pawn);
    g_c593_probe = false;
    log::line("card593: %s t=%llu ai class %s: capsule radius %.1f half height %.1f; NavAgentProps %s radius %.1f "
              "height %.1f step %.1f preferred names %s/%s/%s; GetNavData %s",
              side(), static_cast<unsigned long long>(GetTickCount64()), object_name(cls).text,
              capsule != nullptr ? c593_float(capsule, cap_cls, "CapsuleRadius") : -1.0f,
              capsule != nullptr ? c593_float(capsule, cap_cls, "CapsuleHalfHeight") : -1.0f,
              a.found ? "found" : "missing", a.radius, a.height, a.step, a.pref[0].text, a.pref[1].text,
              a.pref[2].text, nav != nullptr ? object_name(nav).text : "(null)");
}

} // namespace

const void* detour_c593_resolve(const void* navsys, const void* props) {
    const void* const nav = g_orig_c593_resolve(navsys, props);
    if (g_client_session) {
        return nav;
    }
    c593_hold_use(nav); // step 5: any resolve, any thread, marks its navmesh used (the possession probe too)
    if (g_c593_probe) {
        return nav;
    }
    const DWORD gt = g_c593_game_thread.load(std::memory_order_relaxed);
    if (gt == 0 || GetCurrentThreadId() != gt) {
        g_c593r_off_thread.fetch_add(1, std::memory_order_relaxed);
        return nav;
    }
    c593_tally_resolve(nav, static_cast<const uint8_t*>(props), c593_rva(_ReturnAddress()));
    return nav;
}

void detour_c593_possess(void* pawn, void* controller) {
    g_orig_c593_possess(pawn, controller);
    g_c593_game_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    if (!g_client_session && pawn != nullptr) {
        c593_class_row(pawn);
    }
}

// One row per distinct nav data: its whole-run resolve count and callers. Host, from the card577 row (game thread).
void c593_resolve_rows() {
    const void* navs[kC593NavShown] = {};
    const char* names[kC593NavShown] = {};
    uint32_t nn = 0;
    for (uint32_t i = 0; i < g_c593r.n && nn < kC593NavShown; ++i) {
        bool known = false;
        for (uint32_t k = 0; k < nn; ++k) {
            known = known || navs[k] == g_c593r.p[i].nav;
        }
        navs[nn] = g_c593r.p[i].nav;
        names[nn] = g_c593r.p[i].name.text;
        nn += known ? 0u : 1u;
    }
    log::line("card593: %s nav resolves so far: %u on the game thread (%u pairs, %u overflow), %u off it or before "
              "the first AI possession",
              side(), g_c593r.total, g_c593r.n, g_c593r.overflow, g_c593r_off_thread.load(std::memory_order_relaxed));
    for (uint32_t k = 0; k < nn; ++k) {
        char text[768];
        int len = 0;
        uint32_t sum = 0;
        for (uint32_t i = 0; i < g_c593r.n; ++i) {
            const C593Pair& p = g_c593r.p[i];
            if (p.nav != navs[k]) {
                continue;
            }
            sum += p.n;
            const int w = snprintf(text + len, sizeof(text) - static_cast<size_t>(len), "%s0x%llX x%u (r %.0f)",
                                   len == 0 ? "" : ", ", static_cast<unsigned long long>(p.caller), p.n, p.radius);
            len = w > 0 && len + w < static_cast<int>(sizeof(text)) ? len + w : len;
        }
        text[len] = '\0';
        log::line("card593: %s nav resolves to %s: %u, callers %s", side(), names[k], sum, text);
    }
}

} // namespace ubw::hooks
