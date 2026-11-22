// Card 549 (performance mod family, card 527): overlap events off on the player Camera children nobody reads.
//
// Card 548 (c263L369) names the player Camera children whose UpdateOverlapsImpl makes the Camera self time of card
// 546. Each costly child is a primitive with bGenerateOverlapEvents set, so every camera move runs an overlap query for
// it against every nearby body. No native code reads their overlaps: USTPlayerDeterAllyCollider has no overlap handler,
// it is a nav-relevant box (INavRelevantInterface) that makes allies path around the player's front, and nav relevance
// uses bounds, not overlaps.
//
// Once a second, for each player character (the card 368 cached list), this walks the Camera AttachChildren (two
// levels) and clears bGenerateOverlapEvents on the components named in kC549Names. UPrimitiveComponent's
// UpdateOverlapsImpl then skips the query and ends the overlaps the component held. The Super call still visits the
// component's own children. Each cleared component is logged once.
// Off switch: UBWCOOP_C549_OFF=1.
//
// Open risk check (pre-playtest, 2026-11-20): a Blueprint on the player class could bind OnComponentBeginOverlap of
// these components (a BndEvt__ function). Once per process, the first player's class chain is walked, and each
// function whose name has BndEvt, Overlap or Ally in it is logged, then a count line. No such row on a collider name
// means no Blueprint reads their overlaps. Runs whether or not the lever is on.

#include "ubw/hooks_internal.h"

#include <string.h>

namespace ubw::hooks {

bool g_c549_on = true; // written in ubw::init before any hook (UBWCOOP_C549_OFF=1 turns it off)

namespace {

constexpr ULONGLONG kC549StepMs = 1000;
constexpr int32_t kC549MaxChildren = 32;
constexpr uint32_t kC549MaxLogs = 32;
constexpr const char* kC549Names[] = {"AllyNavCollider", "Ally Nav Collider Left"};
constexpr int32_t kC549MaxClasses = 16;
constexpr int32_t kC549MaxFns = 2048;
constexpr const char* kC549FnWords[] = {"BndEvt", "Overlap", "Ally"};

struct C549State {
    ULONGLONG next_ms;
    uint32_t cleared; // components whose flag this card cleared, all maps
    uint32_t logs;
    bool fns_listed;
};
C549State g_c549{};

bool c549_target(const void* comp) {
    const auto name = object_name(comp);
    for (const char* const n : kC549Names) {
        if (strcmp(name.text, n) == 0) {
            return true;
        }
    }
    return false;
}

// Clears the reflected bool `bGenerateOverlapEvents` of `comp` when set. True when this call cleared it.
bool c549_clear(void* comp) {
    const uint8_t* const prop = prop_find_prop(read_ptr(comp, kObjectClass), "bGenerateOverlapEvents");
    if (prop == nullptr) {
        log::line("WARN card549: bGenerateOverlapEvents not found on %s", class_name(comp).text);
        return false;
    }
    uint8_t* const at = static_cast<uint8_t*>(comp) + read_i32(prop, kPropertyOffset) + prop[kBoolByteOffset];
    const uint8_t mask = prop[kBoolFieldMask];
    if ((*at & mask) == 0) {
        return false;
    }
    *at = static_cast<uint8_t>(*at & ~mask);
    return true;
}

void c549_visit(void* comp, const void* pawn) {
    if (comp == nullptr || !c549_target(comp) || !c549_clear(comp)) {
        return;
    }
    ++g_c549.cleared;
    if (g_c549.logs < kC549MaxLogs) {
        ++g_c549.logs;
        log::line("card549: %s overlap events off on %s (%s) of %s, %u cleared", g_client_session ? "client" : "host",
                  object_name(comp).text, class_name(comp).text, object_name(pawn).text, g_c549.cleared);
    }
}

bool c549_fn_word(const char* name) {
    for (const char* const w : kC549FnWords) {
        if (strstr(name, w) != nullptr) {
            return true;
        }
    }
    return false;
}

// Once: the functions of the player's class chain that could take the colliders' overlap events (see the header).
void c549_list_fns(const void* pawn) {
    if (g_c549.fns_listed) {
        return;
    }
    g_c549.fns_listed = true;
    const char* const side = g_client_session ? "client" : "host";
    int32_t classes = 0;
    int32_t fns = 0;
    int32_t hits = 0;
    const void* cls = read_ptr(pawn, kObjectClass);
    for (; classes < kC549MaxClasses && cls != nullptr; ++classes, cls = read_ptr(cls, kStructSuper)) {
        const void* fn = read_ptr(cls, kStructChildren);
        for (; fns < kC549MaxFns && fn != nullptr; ++fns, fn = read_ptr(fn, kUFieldNext)) {
            const auto name = object_name(fn);
            if (c549_fn_word(name.text)) {
                ++hits;
                log::line("card549: %s player class fn %s::%s (%s)", side, object_name(cls).text, name.text,
                          class_name(fn).text);
            }
        }
    }
    log::line("card549: %s player class chain of %s: %d classes, %d functions walked, %d named BndEvt/Overlap/Ally%s",
              side, object_name(pawn).text, classes, fns, hits, fns >= kC549MaxFns ? " (cap hit)" : "");
}

// The Camera's attached children and their children.
void c549_pawn(const void* pawn) {
    void* const camera = read_ptr(pawn, kCharacterCamera);
    const uint8_t* const prop = camera != nullptr ? prop_find_prop(read_ptr(camera, kObjectClass), "AttachChildren")
                                                  : nullptr;
    if (prop == nullptr) {
        return;
    }
    const size_t off = static_cast<size_t>(read_i32(prop, kPropertyOffset));
    void* const* const kids = static_cast<void* const*>(read_ptr(camera, off));
    const int32_t num = read_i32(camera, off + kArrayNum);
    for (int32_t i = 0; kids != nullptr && i < num && i < kC549MaxChildren; ++i) {
        void* const child = kids[i];
        if (child == nullptr) {
            continue;
        }
        c549_visit(child, pawn);
        void* const* const sub = static_cast<void* const*>(read_ptr(child, off));
        const int32_t sub_num = read_i32(child, off + kArrayNum);
        for (int32_t k = 0; sub != nullptr && k < sub_num && k < kC549MaxChildren; ++k) {
            c549_visit(sub[k], pawn);
        }
    }
}

} // namespace

// Game thread, per frame (c42_step).
void c549_tick(void* engine, ULONGLONG now) {
    if (now < g_c549.next_ms || (!g_c549_on && g_c549.fns_listed)) {
        return;
    }
    g_c549.next_ms = now + kC549StepMs;
    const void* const world = g_get_game_world(engine);
    if (world == nullptr || g_player_character_class == nullptr) {
        return;
    }
    static void* players[kC23Max]; // game thread only
    const uint32_t count = c368_actors(g_c368_players, world, g_player_character_class(), players);
    for (uint32_t i = 0; i < count; ++i) {
        if (players[i] == nullptr) {
            continue;
        }
        c549_list_fns(players[i]);
        if (g_c549_on) {
            c549_pawn(players[i]);
        }
    }
}

} // namespace ubw::hooks
