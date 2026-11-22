// Card 103: enemies perceive, target and attack both players.
// Gate (host, always): an index-0 GetPlayerController call whose context belongs to an enemy returns the controller of
// the enemy's player: its primary target (SetPrimaryTarget, recorded per enemy) when that is a player pawn, else the
// nearest player pawn. Slot 0 (the host) keeps the original result. Card 099's detour calls c103_enemy_pc.
// Measure (UBWCOOP_C103_LOG=1, host only, log only):
// - UADTargetComponent::SelectPrimaryTarget: per enemy at most every kC103RowMs, the threat of the host pawn and of the
//   server copy in its target list (FindTargetData; FADTargetData threat at +0x50, absent = not listed).
// - UADTargetComponent::SetPrimaryTarget: each change of an enemy's primary target (host pawn, server copy, other).
// - index-0 GetPlayerController calls whose context belongs to an enemy, counted per caller and dumped at map change.

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

bool g_c103_log = false; // UBWCOOP_C103_LOG; written in ubw::init before any hook
C103SelectFn g_orig_c103_select = nullptr;
C103SetFn g_orig_c103_set = nullptr;
C103FindFn g_c103_find = nullptr;

namespace {

constexpr size_t kC103Threat = 0x50;      // FADTargetData::Threat (ModifyTargetThreat adds here)
constexpr uint32_t kC103Slots = 96;       // enemies tracked per map
constexpr uint32_t kC103Callers = 48;     // distinct enemy-context player 0 callers per map
constexpr uint32_t kC103Lines = 900;      // log lines per map
constexpr ULONGLONG kC103RowMs = 2000;    // threat row gap per enemy
constexpr int32_t kC103Hops = 6;

struct C103Enemy {
    const void* component;
    const void* primary; // last primary target seen by SetPrimaryTarget (identity only)
    ULONGLONG row_at;
    uint32_t switches;
};
struct C103Caller {
    unsigned long long rva;
    uint32_t count;
    char cls[64];
};
struct C103State {
    C103Enemy e[kC103Slots];
    uint32_t used;
    C103Caller c[kC103Callers];
    uint32_t callers;
    uint32_t total;
    uint32_t lines;
};
C103State g_c103{};

// Each enemy's last primary target, by identity only (never dereferenced). A ring: a new enemy past kC103Slots
// replaces the oldest row. Game thread only (SetPrimaryTarget and the gate's game-thread check).
struct C103Pick {
    const void* enemy;
    const void* primary;
    int32_t chosen; // card 108: the gate's last chosen roster slot + 1 (0: none yet), for the change log
};
struct C103Picks {
    C103Pick row[kC103Slots];
    uint32_t next;
    uint32_t wraps; // new enemies that replaced a live row (the oldest) this map; logged at map arm
};
C103Picks g_c103_picks{};

void c103_pick_note(const void* enemy, const void* primary) {
    for (C103Pick& p : g_c103_picks.row) {
        if (p.enemy == enemy) {
            p.primary = primary;
            return;
        }
    }
    g_c103_picks.wraps += g_c103_picks.row[g_c103_picks.next].enemy != nullptr ? 1u : 0u;
    g_c103_picks.row[g_c103_picks.next] = C103Pick{enemy, primary, 0};
    g_c103_picks.next = (g_c103_picks.next + 1) % kC103Slots;
}

} // namespace

// The primary target card 103 last picked for `enemy`, or nullptr (card 122 reads it).
const void* c103_pick_of(const void* enemy) {
    for (const C103Pick& p : g_c103_picks.row) {
        if (p.enemy == enemy) {
            return p.primary;
        }
    }
    return nullptr;
}

namespace {

bool c103_line() {
    return note_event_cap(g_c103.lines, kC103Lines, "card103 log");
}

C103Enemy* c103_slot(const void* component) {
    for (uint32_t i = 0; i < g_c103.used; ++i) {
        if (g_c103.e[i].component == component) {
            return &g_c103.e[i];
        }
    }
    if (g_c103.used == kC103Slots) {
        return nullptr;
    }
    C103Enemy& e = g_c103.e[g_c103.used++];
    e = C103Enemy{component, nullptr, 0, 0};
    return &e;
}

const char* c103_who(const void* actor) {
    if (actor == nullptr) {
        return "none";
    }
    const uint32_t s = sprint_side(actor);
    return s == kSideServerCopy ? "server copy" : s == kSideHostOwn ? "host pawn" : "other";
}

float c103_threat(const void* component, const void* pawn) {
    const void* const data = pawn != nullptr ? g_c103_find(component, pawn) : nullptr;
    return data != nullptr ? read_f32(data, kC103Threat) : -1.0f;
}

// The enemy a context belongs to: the context itself, its outer chain, or its Owner chain.
const void* c103_enemy_of(const void* ctx) {
    const void* const actor_class = g_actor_class();
    const void* o = ctx;
    for (int32_t hop = 0; hop < kC103Hops && o != nullptr; ++hop) {
        if (is_a(o, g_enemy_class())) {
            return o;
        }
        if (!is_a(o, actor_class)) {
            o = read_ptr(o, kObjectOuter);
            continue;
        }
        o = read_ptr(o, kActorOwner);
    }
    return nullptr;
}

const void* c108_target(const void* component, const void* actor);

} // namespace

void detour_c103_select(void* component) {
    const float base = g_client_session ? -1.0f : c122_pref_scale(component); // card 122: always installed (host)
    g_orig_c103_select(component);
    c122_pref_restore(component, base);
    c532_host_note(component); // card 532: host target to clients (returns at once on the client)
    if (!g_c103_log) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    C103Enemy* const e = g_client_session ? nullptr : c103_slot(component);
    if (e == nullptr || now - e->row_at < kC103RowMs) {
        return;
    }
    e->row_at = now;
    const C43Roster r = c43_roster(component);
    const void* const owner = read_ptr(component, kComponentOwner);
    if (c103_line()) {
        log::line("card103: threat %s %p: host pawn %.1f, server copy %.1f (-1 = not listed), primary %s %s, "
                  "switches %u", class_name(owner).text, owner, c103_threat(component, r.p[0].pawn),
                  c103_threat(component, c44_other_pawn(component)), c103_who(e->primary),
                  class_name(e->primary).text, e->switches);
    }
}

// Card 572 step 8: UADTargetComponent's primary target GUID (16 bytes), the key GetPrimaryTarget (exe+0x518F310)
// hashes and finds in the target table (`add rcx, 0x190; mov edx, 0x10; call CityHash64`).
constexpr size_t kC572PrimaryKey = 0x190;

bool detour_c103_set(void* component, void* actor) {
    if (g_client_session) {
        return g_orig_c103_set(component, actor);
    }
    actor = const_cast<void*>(c108_target(component, actor));
    uint8_t key_before[16]; // card 572 step 8: the primary target key a memo pick reads
    memcpy(key_before, static_cast<const uint8_t*>(component) + kC572PrimaryKey, sizeof(key_before));
    const bool ok = g_orig_c103_set(component, actor);
    if (memcmp(key_before, static_cast<const uint8_t*>(component) + kC572PrimaryKey, sizeof(key_before)) != 0) {
        c572_input_bump(kC572InputPrimary);
    }
    c103_pick_note(read_ptr(component, kComponentOwner), actor);
    C103Enemy* const e = g_c103_log ? c103_slot(component) : nullptr;
    if (e == nullptr || e->primary == actor) {
        return ok;
    }
    e->primary = actor;
    ++e->switches;
    const void* const owner = read_ptr(component, kComponentOwner);
    if (c103_line()) {
        log::line("card103: primary %s %p -> %s %s %p (ok %d, switch %u, caller exe+0x%llX)", class_name(owner).text,
                  owner, c103_who(actor), class_name(actor).text, actor, ok ? 1 : 0, e->switches,
                  exe_rva(_ReturnAddress()));
    }
    return ok;
}

namespace {

void c103_note_player0(const void* ctx, unsigned long long rva) {
    ++g_c103.total;
    for (uint32_t i = 0; i < g_c103.callers; ++i) {
        if (g_c103.c[i].rva == rva) {
            ++g_c103.c[i].count;
            return;
        }
    }
    if (g_c103.callers == kC103Callers) {
        return;
    }
    C103Caller& c = g_c103.c[g_c103.callers++];
    c.rva = rva;
    c.count = 1;
    snprintf(c.cls, sizeof(c.cls), "%s", class_name(ctx).text);
    if (c103_line()) {
        log::line("card103: enemy-context player 0: caller exe+0x%llX, context %s (%s)", rva, c.cls,
                  object_name(ctx).text);
    }
}

// Card 400: one row per redirect away from a pawn whose client is not in yet, for the first kC400RedirectLines.
constexpr uint32_t kC400RedirectLines = 12;
uint32_t g_c400_redirects = 0;
void c400_note_redirect(const void* enemy, int32_t from, int32_t to) {
    ++g_c400_redirects;
    if (g_c400_redirects <= kC400RedirectLines) {
        log::line("card400: host t=%llu enemy %s %p primary target slot %d (client not confirmed) -> slot %d (%u "
                  "redirects)", static_cast<unsigned long long>(GetTickCount64()), class_name(enemy).text, enemy, from,
                  to, g_c400_redirects);
    }
}

// Card 108: the slot's player is Downed or Out (card 043's own states; the game's AI does not know them).
bool c108_down(const C43Player& p) {
    const int32_t i = p.pawn != nullptr ? c43_find(p.id) : -1;
    return i >= 0 && c43_down_state(g_c43.e[i].state);
}

// Card 400: the slot's pawn is hidden until its client confirms; never an enemy target.
bool c400_out(const C43Player& p) {
    return p.pawn != nullptr && c400_shielded(p.pawn);
}

// Card 108: true when at least one player in the roster is standing (has a pawn and is not Downed or Out). Card 400:
// a pawn whose client is not in yet does not count.
bool c108_any_up(const C43Roster& r) {
    for (int32_t s = 0; s < kC43Max; ++s) {
        if (r.p[s].pawn != nullptr && !c108_down(r.p[s]) && !c400_out(r.p[s])) {
            return true;
        }
    }
    return false;
}

// The roster slot of the enemy's player: the primary target if it is a player pawn, else the nearest player pawn.
// Card 108: while any player stands, a Downed or Out player is skipped both as the primary target and in the nearest
// search; when every player is down, both rules take any player as before. Card 400: a pawn whose client is not in
// yet is always skipped (-1 when no other player has a pawn).
// Card 572 step 4: each slot's Downed and Out state is read once (it was read twice per slot, through c108_any_up and
// then again in each search).
int32_t c103_player_slot(const C43Roster& r, const void* enemy) {
    bool live[kC43Max] = {}; // a pawn that is not Out
    bool up[kC43Max] = {};   // and not Downed
    bool any_up = false;
    for (int32_t s = 0; s < kC43Max; ++s) {
        live[s] = r.p[s].pawn != nullptr && !c400_out(r.p[s]);
        up[s] = live[s] && !c108_down(r.p[s]);
        any_up = any_up || up[s];
    }
    const bool* const ok = any_up ? up : live; // while any player stands, a Downed one is skipped
    const void* const primary = c103_pick_of(enemy);
    for (int32_t s = 0; s < kC43Max && primary != nullptr; ++s) {
        if (r.p[s].pawn == primary && ok[s]) {
            return s;
        }
    }
    const Vec3 at = actor_location(enemy);
    int32_t best = -1;
    double best_d = 0.0;
    for (int32_t s = 0; s < kC43Max; ++s) {
        if (!ok[s]) {
            continue;
        }
        const double d = vec_dist(actor_location(r.p[s].pawn), at);
        if (best < 0 || d < best_d) {
            best = s;
            best_d = d;
        }
    }
    return best;
}

// Card 108: the primary target the enemy gets. A Downed or Out player pawn becomes the nearest standing player pawn
// while one stands; any other actor, or everyone down, passes unchanged. Host only (the caller checks).
const void* c108_target(const void* component, const void* actor) {
    const C43Roster r = c43_roster(component);
    int32_t from = -1;
    for (int32_t s = 0; s < kC43Max && actor != nullptr; ++s) {
        if (r.p[s].pawn == actor) {
            from = s;
        }
    }
    const bool out = from >= 0 && c400_out(r.p[from]);
    if (from < 0 || (!out && (!c108_down(r.p[from]) || !c108_any_up(r)))) {
        return actor;
    }
    const void* const enemy = read_ptr(component, kComponentOwner);
    const int32_t to = c103_player_slot(r, enemy);
    if (out) { // card 400: never the unconfirmed pawn; no other player pawn: no primary target
        c400_note_redirect(enemy, from, to);
        return to >= 0 ? r.p[to].pawn : nullptr;
    }
    if (to < 0 || to == from) {
        return actor;
    }
    if (g_c103_log && c103_line()) {
        log::line("card108: redirect %s %p primary slot %d (down) -> slot %d", class_name(enemy).text, enemy, from,
                  to);
    }
    return r.p[to].pawn;
}

// Card 108 (UBWCOOP_C103_LOG): one line per change of an enemy's chosen slot, for enemies with a pick row.
void c108_note_slot(const void* enemy, const C43Roster& r, int32_t s) {
    for (C103Pick& p : g_c103_picks.row) {
        if (p.enemy != enemy || p.chosen == s + 1) {
            continue;
        }
        const int32_t was = p.chosen - 1;
        p.chosen = s + 1;
        if (c103_line()) {
            log::line("card108: enemy %s %p slot %d -> %d (slot 0 %s, slot 1 %s)", class_name(enemy).text, enemy,
                      was, s, r.p[0].pawn == nullptr ? "none" : c108_down(r.p[0]) ? "down" : "up",
                      r.p[1].pawn == nullptr ? "none" : c108_down(r.p[1]) ? "down" : "up");
        }
        return;
    }
}

} // namespace

void* c103_enemy_pc(const void* ctx, unsigned long long rva) {
    if (g_client_session) { // the game thread check is the caller's (c99_answer)
        return nullptr;
    }
    const void* const enemy = c103_enemy_of(ctx);
    if (enemy == nullptr) {
        return nullptr;
    }
    if (g_c103_log) {
        c103_note_player0(ctx, rva);
    }
    const C43Roster r = c43_roster(enemy);
    const int32_t s = c103_player_slot(r, enemy);
    if (g_c103_log) {
        c108_note_slot(enemy, r, s);
    }
    return s > 0 ? r.p[s].pc : nullptr; // slot 0 is the host: the original result stands
}

void c103_arm_map() {
    const uint32_t wraps = g_c103_picks.wraps;
    g_c103_picks = C103Picks{};
    if (!g_c103_log) {
        if (wraps > 0) { // the table peak rule: a full pick ring is reported in the play build too
            log::line("WARN card103: map: pick ring wraps %u (ring of %u enemies, oldest replaced)", wraps,
                      kC103Slots);
        }
        return;
    }
    log::line("card103: map: %u enemy-context player 0 calls, %u callers, %u enemies tracked, ring wraps %u",
              g_c103.total, g_c103.callers, g_c103.used, wraps);
    for (uint32_t i = 0; i < g_c103.callers; ++i) {
        log::line("card103:   caller exe+0x%llX, context %s, %u calls", g_c103.c[i].rva, g_c103.c[i].cls,
                  g_c103.c[i].count);
    }
    for (uint32_t i = 0; i < g_c103.used; ++i) {
        const C103Enemy& e = g_c103.e[i];
        log::line("card103:   enemy component %p: %u primary switches", e.component, e.switches);
    }
    g_c103 = C103State{};
}

} // namespace ubw::hooks
