// Card 326: one table owns every sync state mechanism's map end (the only place its state tables are cleared), map
// arm (reports and per-map counters; never a state table), join replay (host: current state to one PC at its first
// Pong) and in-place load reconcile (host: rebuild from the world after LoadFromSlot). A new state mechanism is added
// here as one row, and nowhere else (docs/SYNC_AUDIT.md section 10).
//
// Card 332: the host's world teardown state lives here too. It is set at map end (before LoadMap tears the old world
// down) and at quit, and cleared at the map arm. While it is set, no frame goes to a client (c139_client_hold), the
// objective journal does not record calls made on the world being left, and card 310's script gate reads the world
// being left from here.

#include "ubw/hooks_internal.h"
#include "ubw/init.h"

namespace ubw::hooks {

namespace {

struct SyncMech {
    const char* name;
    void (*map_end)(void* old_world);                     // clears this mechanism's state tables (old world's rows)
    void (*map_arm)();                                    // reports, counters, class re-resolve; never a state table
    void (*join_replay)(void* pc);                        // host: current state to one PC; nullptr: reason in comment
    void (*after_load)(const void* world, C137LoadCounts& n); // host, in-place load; nullptr = clients rejoin
    int32_t (*live)();                                    // rows held now; nullptr = a flag, not a table
};

// Map end wrappers where the mechanism's own function takes no world or is a report with a label.
void effects_map_end(void*) {
    c136_report("map end"); // card 136: the totals, then the live set and counts start over
}
void grenades_map_end(void*) {
    c132_rounds_map_end();
}
void player_map_end(void*) {
    c45_arm_map(); // card 044 one-shots, with the 043 state row before the clear below
    c43_map_end(); // card 043: life entries and holds
    c75_map_end(); // card 075: mech pilot slots and the client mech identity
}
void bugform_map_end(void*) {
    c208_map_end();
}
void objective_map_end(void*) {
    c49_map_end();
}
void world_map_end(void*) {
    c98_world_map_end();
    c467_map_end(); // card 467: held 'T' rows (a world relay kind)
}
void intro_map_end(void*) {
    c135_map_end();
}
void door_map_end(void*) {
    c95_map_end();
}
void dropship_map_end(void*) {
    c132_dropship_map_end();
}
void levelend_map_end(void*) {
    c260_map_end();
}
void ext_map_end(void*) {
    c132_ext_map_end();
}
void pose_map_end(void*) {
    c132_pose_map_end();
}
void decal_map_end(void*) {
    c132_decal_map_end();
}
void gib_map_end(void*) {
    c137_gib_map_end();
}
void holds_map_end(void*) {
    c133_map_end();
}

void effects_map_arm() {
    c136_arm_map();
}

void pause_join(void* pc) {
    c96_lift_on_join(pc); // card 120: a host pause already in force would freeze this client
}
void player_join(void* pc) {
    c43_join_replay(pc); // card 327: every other player's life state through the Down frame
    c75_join_replay(pc); // card 323: the mech seats held now (row only; see the function)
}

// Card 137 step 3: one pass over the level actors per reconcile; each owner applies its rule to each actor.
template <typename F>
int32_t sync_level_actors(const void* world, F&& each) {
    int32_t seen = 0;
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const void* const level = levels[l];
        const auto* const actors = level != nullptr ? static_cast<void* const*>(read_ptr(level, kLevelActors))
                                                    : nullptr;
        const int32_t num = level != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        for (int32_t i = 0; actors != nullptr && i < num && i < kMaxLevelActors; ++i) {
            if (actors[i] != nullptr) {
                ++seen;
                each(actors[i]);
            }
        }
    }
    return seen;
}
void world_after_load(const void* world, C137LoadCounts& n) {
    n.actors = sync_level_actors(world, [&n](const void* a) {
        c98_load_actor(a, n);
        c145_load_actor(a, n); // card 145 (was card 056's name journal)
    });
}
void door_after_load(const void* world, C137LoadCounts& n) {
    sync_level_actors(world, [&n](const void* a) { c95_load_actor(a, n); });
    c95_load_collisions(world, n); // card 147
}

// The join replay keeps the pre-registry order exactly (objective first: it was c49_first_pong, before
// the old c50_join_replay); map end runs in the same table order. Every mechanism's map end touches only its own
// tables.
// Card 323 (b257): event journals before state journals. "world events" (the world journal's S, K, F, G, C rows and
// the B rates they play at,
// world_c98.cpp c98_event_kind) runs right after the objective replay, so its scripts' side effects come before
// "door" (door and collision state), "world" (the state rows), "ext" and the rest, and the final state lands last
// (c245b256a: an 'F' closure replayed after CollisionOp set BlockingVolume_14..17 back to 1 on the client).
// Card 491 (b260): "resupply" (each pawn's ResupplyLevel, levels only) goes before "objective", so the score the
// objective replay adds on the client does not step a pawn copy the host already stepped. Its state is cleared by
// "score"'s map end (c475_map_end).
constexpr SyncMech kSyncMechs[] = {
    {"resupply", nullptr, nullptr, c491_resupply_join_replay, nullptr, nullptr}, // no table: levels are read live
    {"objective", objective_map_end, c49_arm_map, c49_join_replay, nullptr, c49_live},
    {"world events", nullptr, nullptr, c98_replay_events, nullptr, nullptr}, // rows counted by "world"
    {"door", door_map_end, nullptr, c95_replay, door_after_load, c95_live},
    {"world", world_map_end, nullptr, c98_replay, world_after_load, c98_live},
    {"levelend", levelend_map_end, nullptr, c260_join_replay, nullptr, nullptr}, // a flag: the map's EndLevel
    {"ext", ext_map_end, nullptr, c132_ext_replay, nullptr, c132_ext_live},
    {"pose", pose_map_end, nullptr, c132_pose_replay, nullptr, c132_pose_live},
    {"decal", decal_map_end, nullptr, c132_decal_replay, nullptr, c132_decal_live},
    {"dropship", dropship_map_end, c132_dropship_arm_map, c132_dropship_replay, nullptr, c132_dropship_live},
    {"effects", effects_map_end, effects_map_arm, c136_join_replay, nullptr, c136_live},
    {"pause", nullptr, nullptr, pause_join, nullptr, nullptr}, // no table: the pause is read live at the join
    {"bugform", bugform_map_end, nullptr, c208_join_replay, nullptr, c208_live},
    {"intro", intro_map_end, nullptr, c135_join_replay, nullptr, nullptr}, // a flag: the host left its screen
    {"script", c310_map_end, c310_arm_map, c310_join_replay, nullptr, c310_live},
    {"player", player_map_end, c43_arm_map, player_join, nullptr, c43_live},
    {"gib", gib_map_end, nullptr, c137_gib_join_replay, nullptr, c137_gib_live},
    {"grenades", grenades_map_end, nullptr, c74_join_replay, nullptr, c74_live},
    {"holds", holds_map_end, nullptr, c133_join_replay, nullptr, c133_live},
    // Card 475: last, so every replay above that adds score on the client (objective completes) has run first.
    {"score", c475_map_end, nullptr, c475_join_replay, nullptr, nullptr}, // no table: the history is read live
};
constexpr int32_t kSyncMechCount = static_cast<int32_t>(sizeof(kSyncMechs) / sizeof(kSyncMechs[0]));

// One "name n, " list of every mechanism's live rows; "-" for a flag. Returns the total.
int32_t sync_live_list(char* out, size_t cap) {
    int32_t total = 0;
    size_t used = 0;
    out[0] = '\0';
    for (int32_t i = 0; i < kSyncMechCount && used < cap; ++i) {
        const SyncMech& m = kSyncMechs[i];
        const int32_t n = m.live != nullptr ? m.live() : 0;
        total += n;
        const int w = m.live != nullptr ? snprintf(out + used, cap - used, "%s%s %d", i > 0 ? ", " : "", m.name, n)
                                        : snprintf(out + used, cap - used, "%s%s -", i > 0 ? ", " : "", m.name);
        used += w > 0 ? static_cast<size_t>(w) : cap;
    }
    return total;
}

// --- Card 332: the teardown state ---
struct C332Teardown {
    bool on;
    C31Weak world;       // the world being left (weak: LoadMap frees it)
    const char* why;     // literal: "map end" or "quit"
    ULONGLONG since;
    uint32_t refused;                    // host frames not sent while on
    uint32_t by_op[kChanOpCount];
    uint32_t journal_skipped;            // card 049 calls on the world being left, not journalled
    uint32_t foreign;                    // card 332 item 3: prefixed frames on a PC not this side's own, this map
    bool foreign_warned;
};
C332Teardown g_c332{}; // game thread

void c332_teardown_begin(void* world, const char* why) {
    if (g_c332.on) {
        return; // quit during a map end keeps the first world
    }
    const uint32_t foreign = g_c332.foreign;
    const bool warned = g_c332.foreign_warned;
    g_c332 = C332Teardown{};
    g_c332.foreign = foreign; // the per-map drop count runs to the arm
    g_c332.foreign_warned = warned;
    g_c332.on = true;
    c31_weak_set(g_c332.world, world);
    g_c332.why = why;
    g_c332.since = GetTickCount64();
}

void c332_report() {
    char ops[256];
    size_t used = 0;
    ops[0] = '\0';
    for (size_t o = 0; o < kChanOpCount && used < sizeof(ops); ++o) {
        if (g_c332.by_op[o] == 0) {
            continue;
        }
        const int w = snprintf(ops + used, sizeof(ops) - used, "%s%s %u", used > 0 ? ", " : "",
                               c132_op_name(static_cast<uint8_t>(o)), g_c332.by_op[o]);
        used += w > 0 ? static_cast<size_t>(w) : sizeof(ops);
    }
    log::line("card332: %s t=%llu teardown (%s, %llu ms): %u frame(s) not sent, by op %s; %u objective call(s) not "
              "journalled",
              side(), static_cast<unsigned long long>(GetTickCount64()), g_c332.why,
              static_cast<unsigned long long>(GetTickCount64() - g_c332.since), g_c332.refused,
              used > 0 ? ops : "none", g_c332.journal_skipped);
}

} // namespace

void c332_teardown_end() {
    if (g_c332.on) {
        c332_report();
    }
    if (g_c332.foreign != 0) {
        log::line("card332: %s map: %u prefixed frame(s) dropped on a PC not this side's own",
                  side(), g_c332.foreign);
    }
    g_c332 = C332Teardown{};
}

void c332_detach() {
    if (g_c332.on) {
        c332_report();
    }
}

const char* c332_hold(wchar_t op) {
    if (!g_c332.on) {
        return nullptr;
    }
    ++g_c332.refused;
    const auto o = static_cast<uint32_t>(op);
    g_c332.by_op[o < kChanOpCount ? o : 0] += o < kChanOpCount ? 1u : 0u;
    return "world teardown (card 332)";
}

bool c332_on() {
    return g_c332.on;
}

bool c332_leaving(const void* world) {
    return g_c332.on && world != nullptr && c31_weak_get(g_c332.world) == world;
}

// Any UObject: its outer chain reaches the world being left (an actor's is its level, then the world). Bounded: an
// outer chain deeper than kC332Outers is not the world's.
bool c332_in_leaving_world(const void* object) {
    constexpr int32_t kC332Outers = 8;
    const void* const world = g_c332.on ? c31_weak_get(g_c332.world) : nullptr;
    const void* o = object;
    for (int32_t i = 0; world != nullptr && o != nullptr && i < kC332Outers; ++i) {
        if (o == world) {
            return true;
        }
        o = read_ptr(o, kObjectOuter);
    }
    return false;
}

void c332_journal_skipped() {
    ++g_c332.journal_skipped;
}

namespace {
uint32_t g_c348_old_pc_frames = 0; // card 348, game thread: host frames dropped on a PC whose client is not loaded

// Card 348, host: a frame from a remote PC whose client has not loaded this world (the old PC a seamless travel
// keeps until ServerNotifyLoadedWorld) is old-map traffic: dropped. A hard load sets the loaded world at join.
bool c348_host_frame_ok(const void* pc) {
    if (g_pc_client_loaded == nullptr || pc == nullptr || g_pc_client_loaded(pc)) {
        return true;
    }
    ++g_c348_old_pc_frames;
    return false;
}

// Card 332, client: a frame on a PC not this side's own is dropped.
bool c332_client_frame_ok(const void* pc, wchar_t op) {
    if (sprint_side(pc) == kSideClientOwn) {
        return true;
    }
    ++g_c332.foreign;
    if (!g_c332.foreign_warned) {
        g_c332.foreign_warned = true;
        log::line("WARN card332: %s t=%llu prefixed frame %s on %s, not this side's own PC (side %u): dropped, the "
                  "receive sequence untouched (one row per map)",
                  side(), static_cast<unsigned long long>(GetTickCount64()), c132_op_name(static_cast<uint8_t>(op)),
                  object_name(pc).text, sprint_side(pc));
    }
    return false;
}
} // namespace

bool sync_frame_ok(const void* pc, wchar_t op) {
    return g_client_session ? c332_client_frame_ok(pc, op) : c348_host_frame_ok(pc);
}

void sync_map_end(void* old_world) {
    c332_teardown_begin(old_world, "map end");
    char list[512];
    const int32_t total = sync_live_list(list, sizeof(list));
    log::line("sync: %s t=%llu map end: %s (%d rows)", side(), static_cast<unsigned long long>(GetTickCount64()),
              list, total);
    for (const SyncMech& m : kSyncMechs) {
        if (m.map_end != nullptr) {
            m.map_end(old_world);
        }
    }
}

void sync_map_arm() {
    c332_teardown_end();
    if (!g_client_session) {
        log::line("card348: host t=%llu map: %u stale Pong(s) not taken as first, %u frame(s) dropped on a PC whose "
                  "client had not loaded the world",
                  static_cast<unsigned long long>(GetTickCount64()), c348_stale_pongs_take(), g_c348_old_pc_frames);
        g_c348_old_pc_frames = 0;
        c348_ping_arm();
    }
    for (const SyncMech& m : kSyncMechs) {
        if (m.map_arm == nullptr) {
            continue;
        }
        const int32_t before = m.live != nullptr ? m.live() : 0;
        m.map_arm();
        const int32_t after = m.live != nullptr ? m.live() : 0;
        if (after < before) {
            log::line("WARN sync: %s t=%llu %s lost %d rows at map arm", side(),
                      static_cast<unsigned long long>(GetTickCount64()), m.name, before - after);
        }
    }
    // The clears run at map end, so rows written between map end and this arm survive: this row shows them.
    char list[512];
    const int32_t total = sync_live_list(list, sizeof(list));
    log::line("sync: %s t=%llu map arm: %s (%d rows)", side(), static_cast<unsigned long long>(GetTickCount64()),
              list, total);
}

// The load failed: no arm follows, so the teardown state must not outlive it (every send would be refused).
void sync_map_failed() {
    c332_teardown_end();
}

void sync_join_replay(void* pc) {
    char list[512];
    const int32_t total = sync_live_list(list, sizeof(list));
    for (const SyncMech& m : kSyncMechs) {
        const int32_t n = m.live != nullptr ? m.live() : 0;
        if (m.join_replay != nullptr) {
            m.join_replay(pc);
        } else if (n > 0) {
            log::line("WARN sync: %s holds %d rows and has no join replay", m.name, n);
        }
    }
    log::line("sync: host t=%llu join replay to %s: %s (%d rows)", static_cast<unsigned long long>(GetTickCount64()),
              object_name(pc).text, list, total);
}

void sync_after_load(const void* world, C137LoadCounts& n) {
    for (const SyncMech& m : kSyncMechs) {
        if (m.after_load != nullptr) {
            m.after_load(world, n);
        }
    }
}

// Test quit (testrun.cpp) and any engine exit hook: no frame goes out from here on; the quit reports keep their label.
void sync_quit(void* world) {
    c332_teardown_begin(world, "quit");
    c136_report("quit");
    c137_gib_dump("quit");
    c159_dropship_report("quit");
    // Cards 308 and 436: the totals rows of the map the session ends on (a run that quits on its first map has no
    // LoadMap enter to print them).
    c98_world_totals("quit totals");
    c308_journal_totals("quit");
    c41_relay_totals("quit totals");
    c304_map_end(); // card 304: the client audio tally of a map the run quits inside
    c408_map_end(); // card 477: the card408 and card419 totals of a map the run quits inside
    c161_map_end(); // card 161: the door totals of a map the run quits inside
}

} // namespace ubw::hooks

namespace ubw {
void sync_detach() {
    hooks::c332_detach();
}
} // namespace ubw
