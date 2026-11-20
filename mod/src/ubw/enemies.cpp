// Enemies: the enemy replication audit (card 023), enemy health (card 037), deaths (card 038), gibs and
// corpse collision (card 039), the x1.5 and x0.1 hits (card 040) and bug attack animations (card 041),
// all on the client.

#include <intrin.h>
#include <wchar.h>

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

void c38_death_edge(void* actor, float last, float now, bool corpse);
void c132_dying_line(const void* npc);
void c131_client_revive(void* actor, float last, float now);
void c318_step(ULONGLONG now);
void c38_prune(const C37Seen* seen, uint32_t n);
void c38_state_line(const void* actor);
void c38_shot_step(void* engine, ULONGLONG now);
bool c39_death_edge(void* actor, float now, bool corpse);
void c39_step(void* engine, ULONGLONG now);
void c39_gib_actor(void* actor, uint32_t cell, const char* route);
uint32_t c42_send_gib(void* world, const void* owner, uint32_t cell); // card 042: Gib by NetGUID, chunks on the client
bool c42_owe_gib(const void* owner, uint32_t cell); // card 269: a Gib with no NetGUID yet waits for one
void c42_arm_map();
void c39_send_gib(const void* owner);

// ---- Card 023: enemy replication audit (UBWCOOP_ENEMYAUDIT=1, log only) ----
// Both sides, from map load: every ASTEnemyBase in the world's levels (card 025's level walk), scanned once a second.
// An enemy's first sighting, its disappearance and any change of its health values are logged as events; every 5 s
// a row per enemy (capped) gives class, name, pointer, roles, bReplicates (bit 3 of +0x5B), byte +0x5A, location,
// CurrentHealth/MaxHealth (+0xAC0/+0xAC4), the health set's Health and the controller. Enemy pointers are compared
// as identities only (with their FName) and never dereferenced after the scan that found them.
// Test drivers on top (never game behaviour): while an enemy is within kC23ShotRange of this side's pawn, the
// camera faces the nearest one and a "shot" is taken every kC23ShotGapMs; on the client the camera also faces it on
// every scan while the scripted input runs, so its fire hits. UBWCOOP_SPAWN_BUG=1 (host only): once per map,
// kC23SpawnDelayMs after the host lands, one BP_AI_WarriorBug through UAIBlueprintHelperLibrary::SpawnAIFromClass,
// kC23SpawnAhead in front of the host; once the client's relayed fire has pressed twice (or kC23KillFallbackMs after
// the spawn), the host kills it with card 025's InstaKillTarget and shoots its window at kC23KillShotMs.
StaticClassFn g_enemy_class = nullptr;  // ASTEnemyBase
bool g_c23_audit = false; // UBWCOOP_ENEMYAUDIT; written in ubw::init before any hook
bool g_c23_spawn = false; // UBWCOOP_SPAWN_BUG; written in ubw::init before any hook

constexpr size_t kActorRepFlagsLow = 0x5A;  // APawn ctor sets 0x50 here (bReplicateMovement among them)
constexpr size_t kNpcMaxHealth = 0xAC4;     // ASTNonPlayableCharacterBase::MaxHealth (float)

constexpr uint32_t kC23TickLines = 64;
constexpr uint32_t kC23EventLines = 600;
constexpr uint32_t kC23Ticks = 120;    // 10 min of 5 s rows per map
constexpr ULONGLONG kC23ScanMs = 1000;
constexpr ULONGLONG kC40ScanMs = 100;   // card 040 (UBWCOOP_C40_DAMAGE_TRACE): the scan, so its aim follows a moving bug
constexpr ULONGLONG kC23TickMs = 5000;
constexpr double kC23ShotRange = 6000.0;
constexpr ULONGLONG kC23ShotGapMs = 10000;
constexpr ULONGLONG kC23ClientShotLagMs = 1000; // card 037: client shots 1 s after the host's
constexpr uint32_t kC23Shots = 20;
constexpr ULONGLONG kC23SpawnDelayMs = 18000; // the client's scripted fire presses come about 25 s after the host lands
constexpr double kC23SpawnAhead = 1500.0;

constexpr double kC23SpawnLift[kC23SpawnTries] = {100.0, 300.0, 100.0};
constexpr bool kC23SpawnNoFail[kC23SpawnTries] = {false, false, true};
constexpr uint32_t kC23KillPresses = 2;     // relayed client fire presses before the kill
constexpr ULONGLONG kC23KillAfterRelayMs = 5000;
constexpr ULONGLONG kC23KillFallbackMs = 180000;
constexpr ULONGLONG kC23KillShotMs[] = {500, 1500, 4000, 10000};
constexpr uint32_t kC23KillShots = sizeof(kC23KillShotMs) / sizeof(kC23KillShotMs[0]);
constexpr wchar_t kC23BugClass[] = L"/Game/Blueprints/Enemies/Arachnids/WarriorBug/BP_AI_WarriorBug.BP_AI_WarriorBug_C";

C23State g_c23{}; // game thread; reset per map by c23_arm_map

float c23_set_health(const void* actor) {
    void* component = nullptr;
    return c21_health(static_cast<const uint8_t*>(actor), &component);
}

// One full audit row for an enemy found by this scan.
void c23_row(const char* what, const C23Enemy& n) {
    const auto* const a = static_cast<const uint8_t*>(n.actor);
    const void* const controller = read_ptr(a, kPawnController);
    log::line("card023: %s %s %s %s %p LocalRole %u RemoteRole %u bReplicates %u +0x5A 0x%02X at (%.0f, %.0f, %.0f) "
              "dist %.0f CurrentHealth %.1f MaxHealth %.1f Health %.1f controller %p %s",
              side(), what, class_name(a).text, object_name(a).text, n.actor, a[kActorLocalRole],
              a[kActorRemoteRole], (a[kActorRepFlags] & kRepBit) != 0 ? 1 : 0, a[kActorRepFlagsLow], n.loc.x, n.loc.y,
              n.loc.z, n.dist, read_f32(a, kNpcCurrentHealth), read_f32(a, kNpcMaxHealth), c23_set_health(a),
              controller, controller != nullptr ? class_name(controller).text : "-");
}

int32_t c23_seen_index(const void* actor, uint64_t name) {
    for (uint32_t i = 0; i < g_c23.seen_count; ++i) {
        if (g_c23.seen[i].actor == actor && g_c23.seen[i].name == name) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

// Appear and health-change events for this scan's enemies.
void c23_note_found(const C23List& list) {
    for (uint32_t i = 0; i < list.count; ++i) {
        const C23Enemy& n = list.e[i];
        const uint64_t name = read_u64(n.actor, kObjectName);
        const float set = c23_set_health(n.actor);
        const float cur = read_f32(n.actor, kNpcCurrentHealth);
        const int32_t k = c23_seen_index(n.actor, name);
        if (k < 0 && g_c23.seen_count < kC23Known) {
            g_c23.seen[g_c23.seen_count++] = C23Seen{n.actor, name, set, cur};
            if (note_event_cap(g_c23.event_lines, kC23EventLines, "card023 enemy events")) {
                c23_row("APPEAR", n);
            }
        } else if (k < 0 && !g_c23.untracked_logged) {
            g_c23.untracked_logged = true;
            log::line("WARN card023: %s more than %u enemies -- further ones are not tracked for events", side(),
                      kC23Known);
        } else if (k >= 0 && (g_c23.seen[k].set_health != set || g_c23.seen[k].cur_health != cur)) {
            if (note_event_cap(g_c23.event_lines, kC23EventLines, "card023 enemy events")) {
                log::line("card023: %s HEALTH %s %p Health %.1f -> %.1f CurrentHealth %.1f -> %.1f", side(),
                          object_name(n.actor).text, n.actor, g_c23.seen[k].set_health, set, g_c23.seen[k].cur_health,
                          cur);
            }
            g_c23.seen[k].set_health = set;
            g_c23.seen[k].cur_health = cur;
        }
    }
}

bool c23_listed(const C23List& list, const C23Seen& s) {
    for (uint32_t i = 0; i < list.count; ++i) {
        if (list.e[i].actor == s.actor && read_u64(list.e[i].actor, kObjectName) == s.name) {
            return true;
        }
    }
    return false;
}

// Gone events: tracked enemies this scan no longer lists. Only the stored values are logged (no dereference).
void c23_note_gone(const C23List& list) {
    if (list.count < list.total) {
        return; // a capped list cannot prove an enemy gone
    }
    uint32_t i = 0;
    for (uint32_t guard = 0; i < g_c23.seen_count && guard < kC23Known; ++guard) {
        const C23Seen s = g_c23.seen[i];
        if (c23_listed(list, s)) {
            ++i;
            continue;
        }
        if (note_event_cap(g_c23.event_lines, kC23EventLines, "card023 enemy events")) {
            log::line("card023: %s GONE %p (last Health %.1f CurrentHealth %.1f)", side(), s.actor, s.set_health,
                      s.cur_health);
        }
        g_c23.seen[i] = g_c23.seen[--g_c23.seen_count];
    }
}

void c23_log_tick(const C23List& list) {
    log::line("card023: %s tick %u: %u enemies (ASTEnemyBase) in the world, %u listed", side(), g_c23.ticks,
              list.total, list.count < kC23TickLines ? list.count : kC23TickLines);
    for (uint32_t i = 0; i < list.count && i < kC23TickLines; ++i) {
        c23_row("row", list.e[i]);
    }
}

// AI troopers are ASTEnemyBase too (no shared bug base class); the drivers below aim only at the others.
bool c23_trooper(const void* actor) {
    return strstr(class_name(actor).text, "Trooper") != nullptr;
}

// Index of the nearest non-trooper enemy within kC23ShotRange of this side's pawn, or -1.
int32_t c23_nearest(const C23List& list) {
    int32_t best = -1;
    for (uint32_t i = 0; i < list.count; ++i) {
        const double d = list.e[i].dist;
        if (d >= 0.0 && d < kC23ShotRange && !c23_trooper(list.e[i].actor) && (best < 0 || d < list.e[best].dist)) {
            best = static_cast<int32_t>(i);
        }
    }
    return best;
}

// Sets the controller's ControlRotation from `eye` straight at `at` (yaw and pitch), so a shot along it hits.
void c23_face(void* pc, const Vec3& eye, const Vec3& at) {
    const double dx = at.x - eye.x;
    const double dy = at.y - eye.y;
    const double rot[3] = {atan2(at.z - eye.z, sqrt(dx * dx + dy * dy)) * kRadToDeg, atan2(dy, dx) * kRadToDeg, 0.0};
    g_orig_set_control_rotation(pc, rot);
}

// The next shot time: a multiple of kC23ShotGapMs on the shared tick clock, plus kC23ClientShotLagMs on the client,
// so each client shot pairs with the host shot taken 1 s before it (card 037).
ULONGLONG c23_next_shot(ULONGLONG now) {
    const ULONGLONG lag = g_client_session ? kC23ClientShotLagMs : 0;
    return ((now - lag) / kC23ShotGapMs + 1) * kC23ShotGapMs + lag;
}

Vec3 c40_aim_at(const Vec3& at, ULONGLONG now); // card 040

// Faces the nearest enemy on every scan while the client's script runs; a facing shot every kC23ShotGapMs.
void c23_view_step(const C23List& list, void* pc, const void* pawn, ULONGLONG now) {
    const int32_t i = c23_nearest(list);
    if (i < 0) {
        return;
    }
    const C23Enemy& n = list.e[i];
    if (g_c23.next_shot_ms == 0) {
        g_c23.next_shot_ms = c23_next_shot(now);
    }
    const bool shot_due = g_c23.shots < kC23Shots && now >= g_c23.next_shot_ms;
    if (shot_due || (g_client_session && (autoinput::running() || g_c40_trace))) {
        c23_face(pc, actor_location(pawn), c40_aim_at(n.loc, now)); // card 040: three heights on the client
    }
    if (!shot_due) {
        return;
    }
    request_shot(pc, false);
    ++g_c23.shots;
    g_c23.next_shot_ms = c23_next_shot(now);
    log::line("card023: %s shot %u requested facing %s %p at (%.0f, %.0f, %.0f) dist %.0f", side(), g_c23.shots,
              object_name(n.actor).text, n.actor, n.loc.x, n.loc.y, n.loc.z, n.dist);
}

// Card 057 test: the bug spawn yaw. The first of kC57SpawnYaws yaws, from `yaw` round in equal steps, whose line
// from the server copy's eye to the spawn point is clear under BlockAll (so both screens can see the bug; some walls
// do not block PlayerProjectile, card057u), else the first clear under PlayerProjectile (the client's fire still
// reaches it), else `yaw`.
constexpr uint32_t kC57SpawnYaws = 8;
constexpr double kC57EyeLift = 60.0;
constexpr double kC57ClearSlack = 50.0;
uint64_t g_c57_block_all = 0; // FName BlockAll, cached

// Index of the first clear yaw under `profile`, or kC57SpawnYaws when none is.
uint32_t c57_first_clear(const void* copy, const Vec3& eye, double yaw, uint64_t profile) {
    for (uint32_t i = 0; i < kC57SpawnYaws; ++i) {
        const double y = yaw + 2.0 * 3.14159265358979 * i / kC57SpawnYaws;
        const Vec3 end{eye.x + cos(y) * kC23SpawnAhead, eye.y + sin(y) * kC23SpawnAhead, eye.z};
        const Vec3 hit = trace_profile(copy, eye, end, profile);
        const double dx = hit.x - end.x;
        const double dy = hit.y - end.y;
        if (dx * dx + dy * dy < kC57ClearSlack * kC57ClearSlack) {
            return i;
        }
    }
    return kC57SpawnYaws;
}

double c57_clear_yaw(const void* copy, const Vec3& at, double yaw) {
    if (g_line_trace_by_profile == nullptr || g_query_params_ctor == nullptr || g_actor_get_world(copy) == nullptr) {
        log::line("WARN card057: no line trace for the spawn check: the view yaw is used");
        return yaw;
    }
    const Vec3 eye{at.x, at.y, at.z + kC57EyeLift};
    const char* how = "BlockAll";
    uint32_t i = c57_first_clear(copy, eye, yaw, fname_cached(g_c57_block_all, L"BlockAll"));
    if (i == kC57SpawnYaws) {
        how = "PlayerProjectile only";
        i = c57_first_clear(copy, eye, yaw, fname_cached(g_c24_aim.profile, L"PlayerProjectile"));
    }
    if (i == kC57SpawnYaws) {
        log::line("WARN card057: no clear bug spawn direction from the server copy: the view yaw is used");
        return yaw;
    }
    log::line("card057: host bug spawn direction %u of %u is clear from the server copy (%s)", i + 1, kC57SpawnYaws,
              how);
    return yaw + 2.0 * 3.14159265358979 * i / kC57SpawnYaws;
}

// Card 057 test: moves the host's own pawn up to kC57HostBack behind the server copy (stopping short of a wall),
// facing the bug, so the host's screen shows the client's sprite firing and the bug dying.
constexpr double kC57HostBack = 400.0;
constexpr double kC57WallGap = 60.0;
void c57_place_host(void* pc, const void* own, const void* copy, const Vec3& bug) {
    const Vec3 at = actor_location(copy);
    const double dx = at.x - bug.x;
    const double dy = at.y - bug.y;
    const double len = sqrt(dx * dx + dy * dy);
    if (len < 1.0) {
        log::line("WARN card057: bug on the server copy: host pawn not moved");
        return;
    }
    const Vec3 eye{at.x, at.y, at.z + kC57EyeLift};
    const Vec3 end{at.x + dx / len * kC57HostBack, at.y + dy / len * kC57HostBack, eye.z};
    const Vec3 hit = c24_trace(copy, eye, end);
    const double clear = sqrt((hit.x - at.x) * (hit.x - at.x) + (hit.y - at.y) * (hit.y - at.y));
    const double back = fmax(0.0, clear - kC57WallGap);
    const Vec3 to{at.x + dx / len * back, at.y + dy / len * back, at.z};
    const bool moved = g_set_actor_location(const_cast<void*>(own), &to, false, nullptr, kTeleportPhysics);
    const Vec3 now = actor_location(own);
    c25_face(pc, now, bug);
    log::line("%scard057: host test moves its own pawn %.0f behind the server copy, facing the bug: SetActorLocation "
              "returned %d, host now (%.0f, %.0f, %.0f)",
              moved ? "" : "WARN ", back, moved ? 1 : 0, now.x, now.y, now.z);
}

// Card 125 test: `spawn bug <x> <y> <z> <yaw> [<kind>]` on the host, one bug there facing yaw (degrees), always
// spawned: BP_AI_WarriorBug, or with a kind (card 132: ArcherBug) /Game/Blueprints/Enemies/Arachnids/<kind>/
// BP_AI_<kind>.BP_AI_<kind>_C. Null when spawned, else why not.
const char* c125_spawn_bug(void* world, const Vec3& at, double yaw, const char* kind) {
    if (g_uclass_class == nullptr || g_spawn_ai_from_class == nullptr || g_static_load_object == nullptr) {
        return "SpawnAIFromClass or StaticLoadObject unresolved";
    }
    wchar_t path[192];
    if (kind != nullptr && kind[0] != '\0') {
        swprintf(path, sizeof(path) / sizeof(path[0]), L"/Game/Blueprints/Enemies/Arachnids/%hs/BP_AI_%hs.BP_AI_%hs_C",
                 kind, kind, kind);
    } else {
        swprintf(path, sizeof(path) / sizeof(path[0]), L"%ls", kC23BugClass);
    }
    void* const cls = g_static_load_object(g_uclass_class(), nullptr, path, nullptr, kC29LoadFlags, nullptr, true,
                                           nullptr);
    if (cls == nullptr) {
        log::line("WARN card125: host StaticLoadObject(UClass, %ls) returned null", path);
        return "bug class not found";
    }
    const Vec3 rot{0.0, yaw, 0.0};
    void* const bug = cls != nullptr ? g_spawn_ai_from_class(world, &cls, nullptr, &at, &rot, true, nullptr) : nullptr;
    log::line("%scard125: host spawned %s at (%.0f, %.0f, %.0f) yaw %.0f", bug != nullptr ? "" : "WARN ",
              bug != nullptr ? object_name(bug).text : "nothing", at.x, at.y, at.z, yaw);
    return bug != nullptr ? nullptr : "spawn failed";
}

// Host, UBWCOOP_SPAWN_BUG: one spawn attempt per scan, up to kC23SpawnTries (the last one AlwaysSpawn).
void c23_spawn(void* world, void* pc, const void* pawn, ULONGLONG now) {
    const uint32_t t = g_c23.spawn_tries++;
    void* const cls = g_static_load_object(g_uclass_class(), nullptr, kC23BugClass, nullptr, kC29LoadFlags, nullptr,
                                           true, nullptr);
    if (cls == nullptr) {
        g_c23.spawn_tries = kC23SpawnTries;
        log::line("WARN card023: host StaticLoadObject(UClass, %ls) returned null -- no spawn", kC23BugClass);
        return;
    }
    // Card 057: under UBWCOOP_C50_TEST the bug goes in front of the client's server copy, for the client's fire.
    const void* const copy = g_c50_test ? c29_server_copy(pawn) : nullptr;
    const void* const anchor_pc = copy != nullptr ? read_ptr(copy, kPawnController) : pc;
    const Vec3 at = actor_location(copy != nullptr && anchor_pc != nullptr ? copy : pawn);
    const double view_yaw = read_vec3(anchor_pc != nullptr ? anchor_pc : pc, kControllerControlRotation).y / kRadToDeg;
    const double yaw = copy != nullptr && anchor_pc != nullptr ? c57_clear_yaw(copy, at, view_yaw) : view_yaw;
    const Vec3 loc{at.x + cos(yaw) * kC23SpawnAhead, at.y + sin(yaw) * kC23SpawnAhead, at.z + kC23SpawnLift[t]};
    const Vec3 rot{0.0, yaw * kRadToDeg + 180.0, 0.0}; // FRotator pitch, yaw, roll: facing the host
    void* const bug = g_spawn_ai_from_class(world, &cls, nullptr, &loc, &rot, kC23SpawnNoFail[t], nullptr);
    log::line("%scard023: host spawn try %u: SpawnAIFromClass(%s, (%.0f, %.0f, %.0f), no_collision_fail %d) returned %p "
              "%s; host at (%.0f, %.0f, %.0f)",
              bug != nullptr ? "" : "WARN ", t + 1, class_name(cls).text, loc.x, loc.y, loc.z,
              kC23SpawnNoFail[t] ? 1 : 0, bug, bug != nullptr ? object_name(bug).text : "-", at.x, at.y, at.z);
    if (bug != nullptr) {
        g_c23.spawn_ms = now;
        g_c23.spawned_name = read_u64(bug, kObjectName);
        g_c23.spawn_tries = kC23SpawnTries;
        if (copy != nullptr && anchor_pc != nullptr) {
            c57_place_host(pc, pawn, copy, loc);
        }
    }
}

// Host: the spawned bug if listed, else the nearest enemy to the host; -1 for none.
int32_t c23_kill_target(const C23List& list) {
    int32_t best = -1;
    for (uint32_t i = 0; i < list.count; ++i) {
        if (read_u64(list.e[i].actor, kObjectName) == g_c23.spawned_name) {
            return static_cast<int32_t>(i);
        }
        const bool other = list.e[i].dist >= 0.0 && !c23_trooper(list.e[i].actor);
        if (other && (best < 0 || list.e[i].dist < list.e[best].dist)) {
            best = static_cast<int32_t>(i);
        }
    }
    return best;
}

void c23_kill(const C23List& list, ULONGLONG now) {
    g_c23.kill_ms = now;
    const int32_t i = c23_kill_target(list);
    if (i < 0) {
        log::line("WARN card023: host kill due but no enemy is listed -- no kill");
        return;
    }
    const C23Enemy& n = list.e[i];
    g_c23.kill_loc = n.loc;
    c23_row("KILL", n);
    g_insta_kill(n.actor, g_c39_overkill); // card 039: UBWCOOP_C39_OVERKILL makes the test kill an overkill
    c23_row("after InstaKillTarget", n);
}

// Host: the kill once the relayed client fire pressed kC23KillPresses times (or kC23KillFallbackMs after the
// spawn), then the kill shots facing where the bug died.
void c23_kill_step(const C23List& list, void* pc, const void* pawn, ULONGLONG now) {
    if (g_c23.relay_ms == 0 && g_c24.relayed_presses >= kC23KillPresses) {
        g_c23.relay_ms = now;
        log::line("card023: host relayed client fire presses reached %u -- kill in %llu ms", g_c24.relayed_presses,
                  static_cast<unsigned long long>(kC23KillAfterRelayMs));
    }
    const bool by_relay = g_c23.relay_ms != 0 && now >= g_c23.relay_ms + kC23KillAfterRelayMs;
    const bool by_time = g_c23.spawn_ms != 0 && now >= g_c23.spawn_ms + kC23KillFallbackMs;
    if (g_c23.kill_ms == 0 && (by_relay || by_time)) {
        c23_kill(list, now);
    }
    if (g_c23.kill_ms == 0 || g_c23.kill_shots >= kC23KillShots ||
        now < g_c23.kill_ms + kC23KillShotMs[g_c23.kill_shots]) {
        return;
    }
    c23_face(pc, actor_location(pawn), g_c23.kill_loc);
    request_shot(pc, false);
    ++g_c23.kill_shots;
    log::line("card023: host kill shot %u requested (+%llu ms)", g_c23.kill_shots,
              static_cast<unsigned long long>(now - g_c23.kill_ms));
}

// Host, at the spawn: InstaKills every listed AI trooper, so only the relayed client fire can damage the bug.
void c23_clear_troopers(const C23List& list) {
    uint32_t killed = 0;
    for (uint32_t i = 0; i < list.count; ++i) {
        if (c23_trooper(list.e[i].actor) && c23_set_health(list.e[i].actor) > 0.0f) {
            g_insta_kill(list.e[i].actor, false);
            ++killed;
        }
    }
    log::line("card023: host cleared %u live AI troopers (InstaKillTarget) of %u listed enemies before the spawn",
              killed, list.count);
}

void c37_host_burst(const C23List& list, void* pc, const void* pawn, ULONGLONG now); // card 037
void c40_host_step(const C23List& list, void* pc, const void* pawn, ULONGLONG now); // card 040
void c40_client_step(const C23List& list, const void* pawn, ULONGLONG now);         // card 040

// Host spawn driver: kC23SpawnDelayMs after the first on-foot scan.
void c23_host_step(const C23List& list, void* world, void* pc, const void* pawn, ULONGLONG now) {
    if (g_c23.landed_ms == 0) {
        g_c23.landed_ms = now;
        log::line("card023: host on foot at (%.0f, %.0f, %.0f); %u enemies in the world", actor_location(pawn).x,
                  actor_location(pawn).y, actor_location(pawn).z, list.total);
    }
    if (g_c23_spawn && g_c23.spawn_tries < kC23SpawnTries && now >= g_c23.landed_ms + kC23SpawnDelayMs) {
        if (g_c23.spawn_tries == 0) {
            c23_clear_troopers(list);
        }
        c23_spawn(world, pc, pawn, now);
    }
    c37_host_burst(list, pc, pawn, now);
    if (!g_c40_trace) {
        c23_kill_step(list, pc, pawn, now); // card 040 lets each bug die to the players' fire
    }
    c40_host_step(list, pc, pawn, now);
}

void c23_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    if (!g_c23.armed || now < g_c23.next_scan_ms) {
        return;
    }
    g_c23.next_scan_ms = now + (g_c40_trace ? kC40ScanMs : kC23ScanMs); // card 040: the aim follows the bug
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    if (pc == nullptr) {
        return;
    }
    const void* const pawn = c23_own_pawn(pc);
    static C23List list; // game thread only; too large for the stack
    c23_collect(world, pawn, g_enemy_class(), list);
    c23_note_found(list);
    c23_note_gone(list);
    if (pawn != nullptr) {
        c23_view_step(list, pc, pawn, now);
        c40_client_step(list, pawn, now);
    }
    if (pawn != nullptr && !g_client_session) {
        c23_host_step(list, world, pc, pawn, now);
    }
    if (now < g_c23.next_tick_ms || g_c23.ticks >= kC23Ticks) {
        return;
    }
    g_c23.next_tick_ms = now + kC23TickMs;
    ++g_c23.ticks;
    c23_log_tick(list);
}

StaticClassFn g_st_character_class = nullptr; // ASTCharacter
PostEffectFn g_orig_post_effect = nullptr;
bool g_c37_off = false; // UBWCOOP_NO_ENEMY_HEALTH; written in ubw::init before any hook
bool g_c132_god = false; // UBWCOOP_GOD (card 132 step 3a)
bool g_c132_god_host_only = false; // UBWCOOP_GOD_HOST_ONLY (card 132 hb53 note 3)

constexpr ULONGLONG kC37DumpMs = 5000;
constexpr ULONGLONG kC37BurstDelayMs = 3000;
constexpr ULONGLONG kC37BurstHoldMs = 300; // about 3 Morita shots (40 each of the bug's 400), so the client can still hit

constexpr uint32_t kC37AdoptLines = 64;
constexpr uint32_t kC37NotifyLines = 200;
constexpr uint32_t kC37SkipLines = 32;
constexpr uint32_t kC37DumpLines = 300;

constexpr uint32_t kC37DamageLines = 200;
constexpr int32_t kC37DumpSets = 4;       // health sets printed per dump line
constexpr size_t kHealthSetMaxHealth = 0x40;
constexpr size_t kHealthSetDamage = 0x70; // meta attribute, server only
constexpr size_t kModDataSpec = 0x0;      // FGameplayEffectModCallbackData: const FGameplayEffectSpec&
constexpr size_t kModDataEvaluated = 0x8; // FGameplayModifierEvaluatedData&
constexpr size_t kSpecDef = 0x0;          // FGameplayEffectSpec::Def (UGameplayEffect)
constexpr size_t kEvaluatedOp = 0x38;     // after FGameplayAttribute (0x38, AttributeName FString +0)
constexpr size_t kEvaluatedMagnitude = 0x3C;

C37State g_c37{}; // game thread; reset per map by c37_arm_map

bool c37_enemy(const void* actor) {
    return g_enemy_class != nullptr && is_a(actor, g_enemy_class()) && !c23_trooper(actor);
}

// One line: the component's health set and every health set in its ASC's SpawnedAttributes, with Health.
void c37_dump(const void* actor, const void* component, const char* why) {
    const void* const asc = read_ptr(component, kHealthComponentAsc);
    const void* const own = read_ptr(component, kHealthComponentSet);
    if (asc == nullptr || own == nullptr || !note_event_cap(g_c37.dump_lines, kC37DumpLines, "card037 dump")) {
        return;
    }
    const void* const* const items = static_cast<const void* const*>(read_ptr(asc, kAscSpawnedAttributes));
    const int32_t num = read_i32(asc, kAscSpawnedAttributesNum);
    const void* const cls = read_ptr(own, kObjectClass);
    const void* sets[kC37DumpSets] = {};
    float health[kC37DumpSets] = {};
    int32_t found = 0;
    for (int32_t i = 0; items != nullptr && i < num && i < kC21MaxSpawned; ++i) {
        if (items[i] != nullptr && read_ptr(items[i], kObjectClass) == cls) {
            if (found < kC37DumpSets) {
                sets[found] = items[i];
                health[found] = read_f32(items[i], kHealthSetHealth + kAttrDataCurrent);
            }
            ++found;
        }
    }
    log::line("card037: %s sets %s %s %s (%s): component %p set %p Health %.1f/%.1f MaxHealth %.1f; ASC %p "
              "ReplicationMode %u SpawnedAttributes %d, health sets %d: %p %.1f, %p %.1f, %p %.1f, %p %.1f",
              side(), why, class_name(actor).text, object_name(actor).text, kSprintSideText[sprint_side(actor)],
              component, own, read_f32(own, kHealthSetHealth + kAttrDataBase),
              read_f32(own, kHealthSetHealth + kAttrDataCurrent), read_f32(own, kHealthSetMaxHealth + kAttrDataCurrent),
              asc, static_cast<const uint8_t*>(asc)[kAscReplicationMode], num, found, sets[0], health[0], sets[1],
              health[1], sets[2], health[2], sets[3], health[3]);
}

// Client: adopt the replicated set; true when the component was repointed this call. *own_health gets the local
// set's Health (the value the component held until now) when it was repointed.
bool c37_adopt(const void* actor, uint8_t* component, float* own_health) {
    const void* own = nullptr;
    const void* const other = c21_adopt_set(component, &own);
    if (other == nullptr) {
        return false;
    }
    *own_health = read_f32(own, kHealthSetHealth + kAttrDataCurrent);
    ++g_c37.adopts;
    if (note_event_cap(g_c37.adopt_lines, kC37AdoptLines, "card037 adopt")) {
        log::line("card037: %s adopt %s %s (%s): component %p now uses replicated set %p (Health %.1f) instead of "
                  "local set %p (Health %.1f)",
                  side(), class_name(actor).text, object_name(actor).text, kSprintSideText[sprint_side(actor)],
                  component, other, read_f32(other, kHealthSetHealth + kAttrDataCurrent), own,
                  read_f32(own, kHealthSetHealth + kAttrDataCurrent));
    }
    return true;
}

// Client: the game's own handler for a Health change replication wrote silently; the revive edge is only logged.
void c37_notify(const void* actor, void* component, float last, float now) {
    if (last <= 0.0f && now > 0.0f) {
        ++g_c37.revive_skips;
        if (note_event_cap(g_c37.skip_lines, kC37SkipLines, "card037 revive skip")) {
            log::line("card037: %s skip %s %s Health %.1f -> %.1f: revive branch, HealthAttributeChanged not called",
                      side(), class_name(actor).text, object_name(actor).text, last, now);
        }
        return;
    }
    uint8_t data[kAttributeChangeBytes] = {};
    memcpy(data + kAttributeChangeNew, &now, sizeof(now));
    memcpy(data + kAttributeChangeOld, &last, sizeof(last));
    ++g_c37.notifies;
    if (note_event_cap(g_c37.notify_lines, kC37NotifyLines, "card037 notify")) {
        log::line("card037: %s notify %s %s Health %.1f -> %.1f, HealthAttributeChanged called", side(),
                  class_name(actor).text, object_name(actor).text, last, now);
    }
    g_c21_in_notify = true;
    g_health_attribute_changed(component, data);
    g_c21_in_notify = false;
}

// Last scan's Health of this pawn, or -1 when it was not tracked.
// Health goes negative on overkill (a dead bug reached -196.0), so "not seen yet" needs a value Health never takes.
constexpr float kC37Unseen = -3.0e38f;

// Card 146: a pawn scanned at least this long before its replicated set is adopted with Health <= 0 died in view
// (a one-shot kill: the host's set first replicated with the death). The client saw it alive, so its death plays as
// a live death; only a pawn adopted dead within this time of its first scan is a corpse arrival (join, relevancy).
constexpr ULONGLONG kC37SeenLiveMs = 1000;

// This pawn's entry in the last scan, or null when it was not tracked.
// `hint`: this pawn's place in the scan being built. The scan keeps its order between level walks, so the entry is
// at that place unless a pawn before it left; without the hint every pawn searched the whole table each frame.
const C37Seen* c37_last_seen(const void* actor, uint64_t name, uint32_t hint) {
    if (hint < g_c37.seen_count && g_c37.seen[hint].actor == actor && g_c37.seen[hint].name == name) {
        return &g_c37.seen[hint];
    }
    for (uint32_t i = 0; i < g_c37.seen_count; ++i) {
        if (g_c37.seen[i].actor == actor && g_c37.seen[i].name == name) {
            return &g_c37.seen[i];
        }
    }
    return nullptr;
}

// One pawn of this scan: dump, adoption, notify. Returns its Health for the next scan (kC37Unseen without a health set).
// seen_ms: how long the pawn has been scanned (0 on its first scan).
float c37_pawn(void* actor, float last, ULONGLONG seen_ms, bool dump_due) {
    auto* const component = static_cast<uint8_t*>(c21_character_ptr(actor, kCharacterHealthComponent));
    if (component == nullptr || read_ptr(component, kHealthComponentSet) == nullptr) {
        return kC37Unseen;
    }
    const bool first = last == kC37Unseen;
    if (g_c37.audit && (first || (dump_due && c37_enemy(actor)))) {
        c37_dump(actor, component, first ? "first" : "tick");
    }
    float own_health = 0.0f;
    const bool adopted = g_c37.armed && c37_adopt(actor, component, &own_health);
    if (adopted && g_c37.audit) {
        c37_dump(actor, component, "adopted");
    }
    const float now = read_f32(read_ptr(component, kHealthComponentSet), kHealthSetHealth + kAttrDataCurrent);
    if (g_c37.armed && !first && !adopted && now != last) {
        c37_notify(actor, component, last, now);
        if (last > 0.0f && now <= 0.0f) {
            c38_death_edge(actor, last, now, false);
        } else if (last <= 0.0f && now > 0.0f) {
            c131_client_revive(actor, last, now); // card 131: a pooled or revived NPC leaves the client's Dying
        }
    } else if (g_c37.armed && adopted && own_health > 0.0f && now <= 0.0f && !first && seen_ms >= kC37SeenLiveMs) {
        // Card 146 (playtest 2026-11-08 findings 5 and 10): a pawn the client has shown alive for seen_ms whose set
        // first replicated with its death (a one-shot kill in view). A live death, not a corpse arrival: the host's
        // ragdoll is still running, so its first pose row comes seconds later (settled or kPoseForceMs).
        c37_notify(actor, component, own_health, now);
        c38_death_edge(actor, own_health, now, false);
        log::line("card146: client %s %s adopted dead after %llu ms in view (Health %.1f -> %.1f): live death",
                  class_name(actor).text, object_name(actor).text, static_cast<unsigned long long>(seen_ms),
                  own_health, now);
    } else if (g_c37.armed && adopted && own_health > 0.0f && now <= 0.0f) {
        // Card 132: the replicated set already holds the host's death (late join, relevancy). The adoption is the
        // client's only sight of it, so the game's own handler runs for the local Health -> replicated Health change
        // (it sets the health component's IsDead, which OnSignificanceChanged reads before swapping a corpse to its
        // static mesh), then the corpse visuals. Before this, an adopted corpse skipped both and stayed alive here.
        c37_notify(actor, component, own_health, now);
        c38_death_edge(actor, now, now, true);
    } else if (g_c37.armed && first && !adopted && now <= 0.0f) {
        // card 131: a corpse the client first sees dead (late join, relevancy). Card 132: the replication wrote the
        // Health silently, so the game's handler runs from MaxHealth for IsDead, as for an adopted corpse above.
        const float max_health = c43_max_health(component);
        if (max_health > 0.0f) {
            c37_notify(actor, component, max_health, now);
        }
        c38_death_edge(actor, now, now, true);
    }
    return now;
}

static_assert(kC37Max >= kC23Max, "every collected pawn must fit the seen table");

// Card 367: the level walk behind the scan (every actor of the map, 30.7k on C2M1) ran every frame: 1164.3 ms in 2118
// calls on the client (b50r2). It now runs every kC37CollectMs; the frames between read the same pawns through weak
// refs, so a Health change is still seen in its frame and only a pawn's first sighting can be up to one period late.
constexpr ULONGLONG kC37CollectMs = 100;
struct C37Pawns {
    C31Weak weak[kC37Max];
    uint32_t count;
    const void* world;       // identity only: the world the refs were collected in
    ULONGLONG next_ms;       // when the next level walk is due
};
C37Pawns g_c37_pawns{};

// This frame's pawn `i` of the scan: from the fresh level walk, else the weak ref (null once the pawn is gone).
void* c37_scan_pawn(const C23List& list, bool fresh, uint32_t i) {
    return fresh ? list.e[i].actor : c31_weak_get(g_c37_pawns.weak[i]);
}

// Card 528 (diagnostic, UBWCOOP_C132_FX=1): a fresh walk while the seen table is empty (the first after a map arm)
// that finds pawns. Shows which world the walk read and where its first pawn lives, to tell the left map's corpses
// (c263L313: 94 re-seen as arrivals 800 ms after the arm) from the new map's pawns. Before g_c37_pawns is overwritten.
void c528_walk_row(const void* world, const C23List& list) {
    constexpr uint32_t kC528Rows = 8; // per process
    static uint32_t rows = 0;         // game thread only
    if (!g_c132_fx_log || g_c37.seen_count != 0 || list.count == 0 || rows >= kC528Rows) {
        return;
    }
    ++rows;
    const void* const pawn = list.e[0].actor;
    const void* const level = read_ptr(pawn, kObjectOuter);
    const void* const level_world = level != nullptr ? read_ptr(level, kObjectOuter) : nullptr;
    log::line("card528: %s t=%llu fresh walk with an empty seen table: world %p (last walk's %p), %u pawns; first %s "
              "level %p in world %p%s",
              side(), static_cast<unsigned long long>(GetTickCount64()), world, g_c37_pawns.world, list.count,
              object_name(pawn).text, level, level_world, level_world == world ? "" : " (not this world)");
}

// Card 528 fix (c263L316: 119 kept NPCs at the seamless travel, no card434 spawn row after the arm): the left map's
// NPCs that seamless travel carried into Untitled and the destination world are not this map's pawns. The host
// destroyed them; their channel close removes them here. The scan skips them, so none is a first sighting (a corpse
// arrival with HandleDying on an actor being torn down). Counted per process for the map-end proof row.
uint32_t g_c528_dropped = 0; // game thread only; per process
void c528_drop_kept(C23List& list) {
    constexpr uint32_t kC528DropRows = 8; // per process
    static uint32_t rows = 0;             // game thread only
    uint32_t kept = 0;
    for (uint32_t i = 0; i < list.count; ++i) {
        if (c392_kept_npc(list.e[i].actor)) {
            continue;
        }
        list.e[kept++] = list.e[i];
    }
    const uint32_t dropped = list.count - kept;
    list.count = kept;
    g_c528_dropped += dropped;
    if (dropped != 0 && g_c37.seen_count == 0 && rows < kC528DropRows) {
        ++rows;
        log::line("card528: %s t=%llu walk skipped %u NPCs carried by seamless travel (card 392), %u pawns left, "
                  "%u skipped this process",
                  side(), static_cast<unsigned long long>(GetTickCount64()), dropped, kept, g_c528_dropped);
    }
}

void c37_scan(void* world, bool dump_due) {
    static C23List list;          // game thread only; too large for the stack
    static C37Seen next[kC37Max]; // game thread only
    const ULONGLONG scan_ms = GetTickCount64();
    // Without the weak-ref functions every frame walks the levels, as before card 367.
    const bool fresh = world != g_c37_pawns.world || scan_ms >= g_c37_pawns.next_ms || g_weak_get == nullptr ||
                       g_weak_assign == nullptr;
    if (fresh) {
        c23_collect(world, nullptr, g_st_character_class(), list);
        c528_drop_kept(list);
        c528_walk_row(world, list);
        g_c37_pawns.world = world;
        g_c37_pawns.next_ms = scan_ms + kC37CollectMs;
        g_c37_pawns.count = list.count;
        for (uint32_t i = 0; i < list.count; ++i) {
            c31_weak_set(g_c37_pawns.weak[i], list.e[i].actor);
        }
    }
    uint32_t n = 0;
    // The dead set can only lose an entry when a tracked dead pawn left or stopped being dead; on every other frame
    // the prune (dead entries x seen pawns) would keep all of it.
    bool prune = fresh;
    for (uint32_t i = 0; i < g_c37_pawns.count; ++i) {
        void* const actor = c37_scan_pawn(list, fresh, i);
        if (actor == nullptr) {
            prune = true;
            continue; // gone since the last level walk
        }
        if (sprint_side(actor) == kSideClientOwn) {
            continue; // card 021 owns the client-own pawn
        }
        const uint64_t name = read_u64(actor, kObjectName);
        const C37Seen* const was = c37_last_seen(actor, name, n);
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG since = was != nullptr ? was->since : now;
        const float health = c37_pawn(actor, was != nullptr ? was->health : kC37Unseen, now - since, dump_due);
        const bool was_dead = was != nullptr && was->health <= 0.0f && was->health != kC37Unseen;
        prune = prune || (was_dead && !(health <= 0.0f && health != kC37Unseen));
        next[n++] = C37Seen{actor, name, health, since};
        c38_state_line(actor);
    }
    prune = prune || n != g_c37.seen_count;
    memcpy(g_c37.seen, next, n * sizeof(C37Seen));
    g_c37.seen_count = n;
    if (prune) {
        c38_prune(next, n); // card 130 item 15
    }
    if (list.total > list.count && !g_c37.untracked_logged) {
        g_c37.untracked_logged = true;
        log::line("WARN card037: %s %u characters in the world, only %u tracked", side(), list.total, list.count);
    }
}

void c37_host_release(void* engine, ULONGLONG now);
void c40_host_release(void* engine, ULONGLONG now); // card 040

void c37_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    c39_step(engine, now);
    if (!g_client_session) {
        c37_host_release(engine, now);
        c40_host_release(engine, now);
        return;
    }
    c38_shot_step(engine, now);
    c318_step(now); // card 318: the read-back 2 s after each revive
    if (!(g_c37.armed || g_c37.audit)) {
        return;
    }
    void* const world = g_get_game_world(engine);
    if (world == nullptr) {
        return;
    }
    const bool dump_due = g_c37.audit && now >= g_c37.next_dump_ms;
    if (dump_due) {
        g_c37.next_dump_ms = now + kC37DumpMs;
    }
    c37_scan(world, dump_due);
}

// Host, every frame while the burst is held: Fire 0 on the host's own weapon kC37BurstHoldMs after the press.
// Host: Fire 0 on the host's own weapon, ending a card 037 or card 040 burst held for held_ms.
void host_burst_release(void* engine, const char* card, ULONGLONG held_ms) {
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    const void* weapon = nullptr;
    void* const asc = pawn != nullptr ? c24_weapon_asc(pawn, &weapon) : nullptr;
    const uint64_t tag = fname_cached(g_c24.fire_tag, L"InputTag.Fire");
    if (asc != nullptr) {
        g_orig_asc_tag_released(asc, &tag);
    }
    log::line("%s%s: host burst release (Fire 0) after %llu ms on %s, weapon ASC %p", asc != nullptr ? "" : "WARN ",
              card, static_cast<unsigned long long>(held_ms), object_name(weapon).text, asc);
}

void c37_host_release(void* engine, ULONGLONG now) {
    if (!g_c37.burst_held || now < g_c37.burst_ms + kC37BurstHoldMs) {
        return;
    }
    g_c37.burst_held = false;
    host_burst_release(engine, "card037", now - g_c37.burst_ms);
}

// Host, UBWCOOP_SPAWN_BUG: kC37BurstDelayMs after the spawn, the host's own weapon fires at the bug (released by
// c37_host_release), so host shots can be compared with relayed client shots.
void c37_host_burst(const C23List& list, void* pc, const void* pawn, ULONGLONG now) {
    const void* weapon = nullptr;
    void* const asc = c24_weapon_asc(pawn, &weapon);
    const uint64_t tag = fname_cached(g_c24.fire_tag, L"InputTag.Fire");
    if (!g_c37.audit || g_c37.burst_ms != 0 || g_c23.spawn_ms == 0 || now < g_c23.spawn_ms + kC37BurstDelayMs) {
        return;
    }
    g_c37.burst_ms = now;
    if (g_c50_test) { // card 057: only the client's relayed fire may hit the bug
        log::line("card037: host burst skipped under UBWCOOP_C50_TEST (card 057 client fire test)");
        return;
    }
    const int32_t i = c23_kill_target(list);
    if (i < 0 || asc == nullptr || tag == 0) {
        log::line("WARN card037: host burst skipped: target %d, weapon %s, weapon ASC %p", i, object_name(weapon).text,
                  asc);
        return;
    }
    c23_face(pc, actor_location(pawn), list.e[i].loc);
    g_orig_asc_tag_pressed(asc, &tag);
    g_c37.burst_held = true;
    log::line("card037: host burst press (Fire 1) on %s at %s %p dist %.0f, Health %.1f", object_name(weapon).text,
              object_name(list.e[i].actor).text, list.e[i].actor, list.e[i].dist, c23_set_health(list.e[i].actor));
}

// Host with the audit: one line per damage effect applied to a non-trooper enemy's health set.
void c37_damage_line(const void* owner, const void* data, float before, float after, float damage) {
    if (!note_event_cap(g_c37.damage_lines, kC37DamageLines, "card037 damage")) {
        return;
    }
    const void* const spec = read_ptr(data, kModDataSpec);
    const void* const def = spec != nullptr ? read_ptr(spec, kSpecDef) : nullptr;
    const auto* const ev = static_cast<const uint8_t*>(read_ptr(data, kModDataEvaluated));
    const auto* const attr = reinterpret_cast<const FStringRaw*>(ev);
    const bool named = ev != nullptr && attr->data != nullptr && attr->num > 1 && attr->num <= kMaxLoggedFStringNum;
    log::line("card037: host damage %s %s Health %.1f -> %.1f (Damage meta at entry %.1f); effect %s, attribute "
              "%.*ls op %d magnitude %.2f; firing: host burst %d, client relays held %d",
              class_name(owner).text, object_name(owner).text, before, after, damage,
              def != nullptr ? class_name(def).text : "-", named ? attr->num - 1 : 1, named ? attr->data : L"-",
              ev != nullptr ? ev[kEvaluatedOp] : -1, ev != nullptr ? read_f32(ev, kEvaluatedMagnitude) : 0.0f,
              g_c37.burst_held ? 1 : 0, g_c24.relays.count);
}

// Host, audit only: Health around the component's post-effect handler for non-trooper enemies (log only).
void detour_post_effect(void* component, const void* data) {
    const void* const owner = g_c37.audit && !g_client_session ? read_ptr(component, kComponentOwner) : nullptr;
    if (owner == nullptr || !c37_enemy(owner)) {
        g_orig_post_effect(component, data);
        return;
    }
    const void* const set = read_ptr(component, kHealthComponentSet);
    const float before = set != nullptr ? read_f32(set, kHealthSetHealth + kAttrDataCurrent) : -1.0f;
    const float damage = set != nullptr ? read_f32(set, kHealthSetDamage + kAttrDataCurrent) : -1.0f;
    g_orig_post_effect(component, data);
    const void* const set_after = read_ptr(component, kHealthComponentSet);
    const float after = set_after != nullptr ? read_f32(set_after, kHealthSetHealth + kAttrDataCurrent) : -1.0f;
    c37_damage_line(owner, data, before, after, damage);
}

using HitResultFn = const void* (*)(const void* context);
DamageProcessFn g_orig_c40_process = nullptr;
DamageStageFn g_orig_c40_base_modify = nullptr;
DamageStageFn g_orig_c40_ai_modify = nullptr;
DamageStageFn g_orig_c40_split = nullptr;
DamageStageFn g_orig_c40_armour = nullptr;
DamageStageFn g_orig_c40_health = nullptr;
AttrOrDefaultFn g_orig_c40_attr = nullptr;
DamageByTagsFn g_orig_c40_by_tags = nullptr;
ModifiedDamageFn g_orig_c40_modified = nullptr;
WithinVolumeFn g_orig_c40_within = nullptr;
TryTriggerFn g_orig_c40_try_trigger = nullptr;

constexpr uint32_t kC40Hits = 160;           // traced hits per map
constexpr uint32_t kC40Tags = 6;             // tag names logged per container
constexpr int32_t kC40AttrChars = 64;        // attribute name characters logged
constexpr size_t kC40ParamsAsc = 0x0;        // FADDamageProcessParams: target UAbilitySystemComponent*
constexpr size_t kC40ParamsSet = 0x8;        // target health attribute set
constexpr size_t kC40ParamsModData = 0x10;   // const FGameplayEffectModCallbackData*
constexpr size_t kC40ParamsDamage = 0x3C;    // float damage after the modify stages
// SplitDamage writes both portions. ApplyDamageToArmour 0x04A3B110 subtracts [params+0x44] from Armour, and
// ApplyDamageToHealth 0x04A3B190 subtracts [params+0x40] from Health (the playlog hit line read them the other way
// round: "(armour 0.00, health 16.00)" next to "Armour 100.0 -> 84.0").
constexpr size_t kC40ParamsHealth = 0x40;    // float health portion
constexpr size_t kC40ParamsArmour = 0x44;    // float armour portion
constexpr size_t kC40SpecContext = 0x280;    // FGameplayEffectSpec::EffectContext (FGameplayEffectContext*)
constexpr size_t kC40CtxHitResultSlot = 0x70; // FGameplayEffectContext vtable: GetHitResult
constexpr size_t kC40CtxInstigator = 0x8;    // TWeakObjectPtr<AActor> Instigator
constexpr size_t kC40CtxCauser = 0x10;       // TWeakObjectPtr<AActor> EffectCauser
constexpr size_t kC40HitImpactPoint = 0x28;  // FHitResult::ImpactPoint (3 doubles)
constexpr size_t kC40HitTraceStart = 0x70;   // FHitResult::TraceStart (3 doubles)
constexpr size_t kC40HitActor = 0xB8;        // FHitResult::HitObjectHandle, TWeakObjectPtr<AActor> first
constexpr size_t kC40HitBoneName = 0xE0;     // FHitResult::BoneName, after Component (kHitComponent)
constexpr size_t kC40VolumeCooldown = 0x560; // USTHitVolumeComponentBase: float cooldown (StartCooldown argument)
constexpr size_t kC40VolumeTimer = 0x568;    // FTimerHandle; non-zero = cooling down (base ShouldTrigger)
constexpr size_t kC40TagArray = 0x0;         // FGameplayTagContainer::GameplayTags (TArray<FGameplayTag>)
constexpr size_t kC40TagNum = 0x8;
constexpr ULONGLONG kC40PressDelayMs = kC37BurstDelayMs + kC37BurstHoldMs + 500; // after card 037's short burst
constexpr ULONGLONG kC40BurstCapMs = 10000;   // host fire on one bug: released at its death or this cap
constexpr ULONGLONG kC40RespawnGapMs = 3000;  // after a bug's death, before card 023 spawns the next
constexpr uint32_t kC40ExtraBugs = 6;         // bugs asked for after card 023's first, per map
constexpr uint32_t kC40ShotsWanted = 30;      // traced projectile hits wanted per origin
constexpr ULONGLONG kC40AimStepMs = 150;     // a bug dies in about 800 ms of fire, so every spot gets shots
constexpr uint32_t kC40AimSpots = 5;
constexpr double kC40AimLift[kC40AimSpots] = {0.0, 80.0, -70.0, 140.0, -120.0}; // cm above the actor location

struct C40State {
    const void* params;    // the traced Process call's params while it runs, else null; never kept across calls
    const uint8_t* hit;    // its FHitResult while the AI ModifyDamage runs, else null
    const void* host_pawn; // identity only (compared, never read); written each card 023 scan
    uint32_t hits;         // traced hits this map
    uint32_t untraced;     // enemy hits past kC40Hits
    bool in_base;          // inside the traced hit's base ModifyDamage
    bool shot;             // the traced hit's EffectCauser is a projectile
    uint32_t host_shots;   // traced projectile hits by origin, this map
    uint32_t relayed_shots;
    uint32_t extra_bugs;   // host: bugs asked of card 023 after its first
    ULONGLONG asked_spawn; // host: card 023's spawn time when the next bug was asked for; 0 = none pending
    ULONGLONG dead_ms;     // host: when the current bug was first seen dead or gone; 0 = alive
    ULONGLONG burst_ms;    // host: the press on the current bug
    ULONGLONG burst_spawn; // host: card 023's spawn time of the bug the burst was pressed on; 0 = none yet
    uint32_t spot;         // host: the last logged aim spot
    bool burst_held;
    bool release_due;      // host: the per-frame step releases the burst
    bool done_logged;
    bool client_held;      // client: own fire held on a live enemy
};
C40State g_c40{}; // game thread only; reset per map by c40_arm_map

void c40_arm_map() {
    if (g_c40.hits != 0) {
        log::line("card040: map totals: %u hits traced, %u enemy hits past the cap", g_c40.hits, g_c40.untraced);
    }
    g_c40 = C40State{};
}

const void* c40_context(const void* params) {
    const void* const data = read_ptr(params, kC40ParamsModData);
    const void* const spec = data != nullptr ? read_ptr(data, kModDataSpec) : nullptr;
    return spec != nullptr ? read_ptr(spec, kC40SpecContext) : nullptr;
}

// The traced Process's target: the owner of the ASC at params +0.
const void* c40_target(const void* params) {
    const void* const asc = read_ptr(params, kC40ParamsAsc);
    return asc != nullptr ? read_ptr(asc, kComponentOwner) : nullptr;
}

float c40_set_health(const void* params) {
    const void* const set = read_ptr(params, kC40ParamsSet);
    return set != nullptr ? read_f32(set, kHealthSetHealth + kAttrDataCurrent) : -1.0f;
}

// Card 155: the set's Armour (UADAttributeSetHealth::Armour, reflected once; -1 when absent or no set).
float c40_set_armour(const void* params) {
    static int32_t at = -1;
    const void* const set = read_ptr(params, kC40ParamsSet);
    if (set == nullptr) {
        return -1.0f;
    }
    if (at == -1) {
        const uint8_t* const prop = prop_find_prop(read_ptr(set, kObjectClass), "Armour");
        at = prop != nullptr ? read_i32(prop, kPropertyOffset) : -2;
        log::line("%scard155: Armour on %s: %s +0x%X", at < 0 ? "WARN " : "", class_name(set).text,
                  at < 0 ? "absent" : "at", at < 0 ? 0 : at);
    }
    return at >= 0 ? read_f32(set, static_cast<size_t>(at) + kAttrDataCurrent) : -1.0f;
}

void c40_process_begin(const void* params) {
    const void* const target = c40_target(params);
    const auto* const ctx = static_cast<const uint8_t*>(c40_context(params));
    const void* const inst = ctx != nullptr ? g_weak_get(ctx + kC40CtxInstigator) : nullptr;
    const void* const causer = ctx != nullptr ? g_weak_get(ctx + kC40CtxCauser) : nullptr;
    const bool host = inst != nullptr && inst == g_c40.host_pawn;
    const bool relayed = !host && inst != nullptr && is_a(inst, g_player_mi_class());
    const char* const who = host ? "host" : (relayed ? "relayed" : (inst == nullptr ? "unknown" : "npc"));
    g_c40.shot = causer != nullptr && strstr(class_name(causer).text, "Projectile") != nullptr;
    g_c40.host_shots += g_c40.shot && host ? 1u : 0u;
    g_c40.relayed_shots += g_c40.shot && relayed ? 1u : 0u;
    char origin[24] = {};
    snprintf(origin, sizeof(origin), "%s%s", who, g_c40.shot ? "" : "-other");
    const Vec3 at = actor_location(target);
    log::line("card040: hit %u begin: %s %s at (%.1f, %.1f, %.1f) Health %.1f, damage %.2f; origin %s (instigator %s "
              "%s %p, causer %s %s); host bursts held %d/%d, client relays held %d",
              g_c40.hits, class_name(target).text, object_name(target).text, at.x, at.y, at.z, c40_set_health(params),
              read_f32(params, kC40ParamsDamage), origin, class_name(inst).text, object_name(inst).text, inst,
              class_name(causer).text, object_name(causer).text, g_c37.burst_held ? 1 : 0, g_c40.burst_held ? 1 : 0,
              g_c24.relays.count);
}

// Card 155: host damage to a player pawn, one line per Process (pl_allow Hit): the damage split, the set's Health before
// and after, the instigator and causer, and the damage effect's class. On in the play build; card 040's gate is not used.
void c40_player_hit(void* handler, void* params, int32_t slot) {
    const float before = c40_set_health(params);
    const float armour_before = c40_set_armour(params);
    g_orig_c40_process(handler, params);
    pl_count(PlCount::Hit);
    if (!pl_allow(PlClass::Hit)) {
        return;
    }
    const auto* const ctx = static_cast<const uint8_t*>(c40_context(params));
    const void* const inst = ctx != nullptr ? g_weak_get(ctx + kC40CtxInstigator) : nullptr;
    const void* const causer = ctx != nullptr ? g_weak_get(ctx + kC40CtxCauser) : nullptr;
    const void* const data = read_ptr(params, kC40ParamsModData);
    const void* const spec = data != nullptr ? read_ptr(data, kModDataSpec) : nullptr;
    const void* const def = spec != nullptr ? read_ptr(spec, 0) : nullptr;
    const void* const target = c40_target(params);
    log::line("playlog: host t=%llu hit slot %d %s: damage %.2f (armour %.2f, health %.2f), Armour %.1f -> %.1f, Health %.1f -> %.1f; "
              "instigator %s %s (%s), causer %s %s; effect %s",
              static_cast<unsigned long long>(GetTickCount64()), slot, kSprintSideText[sprint_side(target)],
              read_f32(params, kC40ParamsDamage), read_f32(params, kC40ParamsArmour), read_f32(params, kC40ParamsHealth),
              armour_before, c40_set_armour(params), before, c40_set_health(params), class_name(inst).text, object_name(inst).text,
              inst != nullptr ? kSprintSideText[sprint_side(inst)] : "none", class_name(causer).text,
              object_name(causer).text, class_name(def).text);
    // b16 (card 336 trace): ASTFallZonesBase::OnBeginOverlap has no movement-mode or falling gate (overlapper must be
    // an ASTPlayerCharacter with Player.IsPlayer, else InstaKillTarget); this row records the pawn's state at the hit.
    if (causer != nullptr && strcmp(class_name(causer).text, "BP_FallZones_C") == 0) {
        const auto* const cmc = static_cast<const uint8_t*>(read_ptr(target, kCharacterMovement));
        const Vec3 at = actor_location(target);
        log::line("playlog: host t=%llu fall zone hit slot %d: mode %d, z velocity %.0f, at (%.0f, %.0f, %.0f), zone %s",
                  static_cast<unsigned long long>(GetTickCount64()), slot, cmc != nullptr ? cmc[kCmcMovementMode] : -1,
                  cmc != nullptr ? read_vec3(cmc, kMoveVelocity).z : 0.0, at.x, at.y, at.z, object_name(causer).text);
    }
}

void detour_c40_process(void* handler, void* params) {
    const int32_t slot = g_client_session ? -1 : pl_player_slot(c40_target(params));
    if (slot >= 0) {
        c40_player_hit(handler, params, slot);
        return;
    }
    const bool enemy = g_gate_c40 && g_c40_trace && !g_client_session && g_c40.params == nullptr && c40_target(params) != nullptr &&
                       c37_enemy(c40_target(params));
    if (!enemy || g_c40.hits >= kC40Hits) {
        g_c40.untraced += enemy ? 1u : 0u;
        g_orig_c40_process(handler, params);
        return;
    }
    ++g_c40.hits;
    c40_process_begin(params);
    g_c40.params = params;
    g_orig_c40_process(handler, params);
    g_c40.params = nullptr;
    log::line("card040: hit %u end: damage %.2f, armour %.2f, health %.2f; Health %.1f", g_c40.hits,
              read_f32(params, kC40ParamsDamage), read_f32(params, kC40ParamsArmour),
              read_f32(params, kC40ParamsHealth), c40_set_health(params));
}

// One damage stage of the traced hit: params +0x3C/+0x40/+0x44 around the original.
void c40_stage(const char* what, DamageStageFn orig, const void* handler, void* params) {
    if (params != g_c40.params) {
        orig(handler, params);
        return;
    }
    const float d = read_f32(params, kC40ParamsDamage);
    const float a = read_f32(params, kC40ParamsArmour);
    const float h = read_f32(params, kC40ParamsHealth);
    orig(handler, params);
    log::line("card040: hit %u   %s: damage %.2f -> %.2f, armour %.2f -> %.2f, health %.2f -> %.2f", g_c40.hits, what, d,
              read_f32(params, kC40ParamsDamage), a, read_f32(params, kC40ParamsArmour), h,
              read_f32(params, kC40ParamsHealth));
}

void detour_c40_base_modify(const void* handler, void* params) {
    g_c40.in_base = params == g_c40.params;
    c40_stage("base ModifyDamage", g_orig_c40_base_modify, handler, params);
    g_c40.in_base = false;
}

void c40_hit_line(const uint8_t* h) {
    if (h == nullptr) {
        log::line("card040: hit %u   no FHitResult in the effect context", g_c40.hits);
        return;
    }
    const Vec3 impact = read_vec3(h, kC40HitImpactPoint);
    const Vec3 start = read_vec3(h, kC40HitTraceStart);
    const double dx = impact.x - start.x;
    const double dy = impact.y - start.y;
    const double dz = impact.z - start.z;
    const void* const actor = g_weak_get(h + kC40HitActor);
    const void* const comp = g_weak_get(h + kHitComponent);
    log::line("card040: hit %u   FHitResult %p: ImpactPoint (%.1f, %.1f, %.1f), TraceStart (%.1f, %.1f, %.1f), "
              "distance %.1f, bBlockingHit %d, actor %s %p, component %s %s, BoneName %s (0x%llx)",
              g_c40.hits, h, impact.x, impact.y, impact.z, start.x, start.y, start.z, sqrt(dx * dx + dy * dy + dz * dz),
              (h[kHitFlags] & kHitBlocking) != 0 ? 1 : 0, object_name(actor).text, actor, class_name(comp).text,
              object_name(comp).text, g_c40.shot ? fname_text(h + kC40HitBoneName).text : "(not read: not a shot)",
              static_cast<unsigned long long>(read_u64(h, kC40HitBoneName)));
}

void detour_c40_ai_modify(const void* handler, void* params) {
    if (params != g_c40.params) {
        g_orig_c40_ai_modify(handler, params);
        return;
    }
    const void* const ctx = c40_context(params);
    const auto fn = ctx != nullptr ? reinterpret_cast<HitResultFn>(read_ptr(read_ptr(ctx, 0), kC40CtxHitResultSlot))
                                   : nullptr;
    g_c40.hit = fn != nullptr ? static_cast<const uint8_t*>(fn(ctx)) : nullptr;
    c40_hit_line(g_c40.hit);
    c40_stage("AI ModifyDamage", g_orig_c40_ai_modify, handler, params);
    g_c40.hit = nullptr;
}

void detour_c40_split(const void* handler, void* params) {
    c40_stage("SplitDamage", g_orig_c40_split, handler, params);
}

void detour_c40_armour(const void* handler, void* params) {
    c40_stage("ApplyDamageToArmour", g_orig_c40_armour, handler, params);
}

void detour_c40_health(const void* handler, void* params) {
    c40_stage("ApplyDamageToHealth", g_orig_c40_health, handler, params);
}

// The callee destroys the by-value FGameplayAttribute, so its name is copied before the call.
float detour_c40_attr(void* asc, void* attribute, float default_value) {
    if (!g_c40.in_base) {
        return g_orig_c40_attr(asc, attribute, default_value);
    }
    const auto* const name = static_cast<const FStringRaw*>(attribute);
    wchar_t text[kC40AttrChars] = {L'-'};
    int32_t n = 1;
    if (name->data != nullptr && name->num > 1 && name->num <= kMaxLoggedFStringNum) {
        n = name->num - 1 < kC40AttrChars ? name->num - 1 : kC40AttrChars;
        memcpy(text, name->data, static_cast<size_t>(n) * sizeof(wchar_t));
    }
    const float v = g_orig_c40_attr(asc, attribute, default_value);
    log::line("card040: hit %u     attribute %.*ls on %s = %.4f (default %.4f)", g_c40.hits, n, text,
              object_name(asc).text, v, default_value);
    return v;
}

// Up to kC40Tags tag names of one FGameplayTagContainer.
void c40_tags_line(const char* what, const void* container) {
    const void* const data = container != nullptr ? read_ptr(container, kC40TagArray) : nullptr;
    const int32_t num = container != nullptr ? read_i32(container, kC40TagNum) : -1;
    ObjectName names[kC40Tags] = {};
    for (uint32_t i = 0; i < kC40Tags; ++i) {
        const bool have = data != nullptr && static_cast<int32_t>(i) < num;
        names[i] = have ? fname_text(static_cast<const uint8_t*>(data) + i * sizeof(uint64_t)) : ObjectName{};
    }
    log::line("card040: hit %u       %s (%d tags): %s %s %s %s %s %s", g_c40.hits, what, num, names[0].text,
              names[1].text, names[2].text, names[3].text, names[4].text, names[5].text);
}

float detour_c40_by_tags(const void* subsystem, float damage, const void* tags_a, const void* tags_b) {
    const float out = g_orig_c40_by_tags(subsystem, damage, tags_a, tags_b);
    if (g_c40.params != nullptr) {
        log::line("card040: hit %u     GetModifiedDamageByTags %.2f -> %.2f", g_c40.hits, damage, out);
        c40_tags_line("first tags", tags_a);
        c40_tags_line("second tags", tags_b);
    }
    return out;
}

float detour_c40_modified(const void* subsystem, const void* set, float damage, const void* tags_a,
                          const void* tags_b) {
    const float out = g_orig_c40_modified(subsystem, set, damage, tags_a, tags_b);
    if (g_c40.params != nullptr) {
        log::line("card040: hit %u     GetModifiedDamage(set %p) %.2f -> %.2f", g_c40.hits, set, damage, out);
        c40_tags_line("first tags", tags_a);
        c40_tags_line("second tags", tags_b);
    }
    return out;
}

// Both points per volume: the game tests TraceStart first and may skip ImpactPoint, so a TraceStart miss also
// evaluates ImpactPoint here (IsWithinVolume is a const box test).
bool detour_c40_within(const void* volume, const double* point) {
    const bool in = g_orig_c40_within(volume, point);
    const uint8_t* const h = g_c40.hit;
    if (g_c40.params == nullptr || h == nullptr) {
        return in;
    }
    const auto* const p = reinterpret_cast<const uint8_t*>(point);
    const char* const which = p == h + kC40HitTraceStart ? "TraceStart" : (p == h + kC40HitImpactPoint ? "ImpactPoint" : "other");
    const bool impact = p == h + kC40HitTraceStart && !in &&
                        g_orig_c40_within(volume, reinterpret_cast<const double*>(h + kC40HitImpactPoint));
    log::line("card040: hit %u     IsWithinVolume %s %s: %s %d%s; cooldown %.2f s, timer 0x%llx (%s)", g_c40.hits,
              class_name(volume).text, object_name(volume).text, which, in ? 1 : 0,
              p == h + kC40HitTraceStart && !in ? (impact ? ", ImpactPoint 1 (mod check)" : ", ImpactPoint 0 (mod check)")
                                                : "",
              read_f32(volume, kC40VolumeCooldown), static_cast<unsigned long long>(read_u64(volume, kC40VolumeTimer)),
              read_u64(volume, kC40VolumeTimer) == 0 ? "ready" : "cooling down");
    return in;
}

void detour_c40_try_trigger(void* volume, void* params) {
    if (params != g_c40.params) {
        g_orig_c40_try_trigger(volume, params);
        return;
    }
    const float d = read_f32(params, kC40ParamsDamage);
    const uint64_t timer = read_u64(volume, kC40VolumeTimer);
    g_orig_c40_try_trigger(volume, params);
    log::line("card040: hit %u     TryTriggerVolume %s %s: damage %.2f -> %.2f, timer 0x%llx -> 0x%llx (%s before)",
              g_c40.hits, class_name(volume).text, object_name(volume).text, d, read_f32(params, kC40ParamsDamage),
              static_cast<unsigned long long>(timer), static_cast<unsigned long long>(read_u64(volume, kC40VolumeTimer)),
              timer == 0 ? "ready" : "cooling down");
}

// Client (card 040): the scripted fire aims at kC40AimLift heights in turn, one per kC40AimStepMs.
Vec3 c40_aim_at(const Vec3& at, ULONGLONG now) {
    if (!g_c40_trace || !g_client_session) {
        return at;
    }
    return Vec3{at.x, at.y, at.z + kC40AimLift[(now / kC40AimStepMs) % kC40AimSpots]};
}

// Host: card 023's latest spawned bug in the list while its Health is above 0, else -1.
int32_t c40_live_bug(const C23List& list) {
    for (uint32_t i = 0; i < list.count; ++i) {
        if (read_u64(list.e[i].actor, kObjectName) == g_c23.spawned_name) {
            return c23_set_health(list.e[i].actor) > 0.0f ? static_cast<int32_t>(i) : -1;
        }
    }
    return -1;
}

// Host: card 023's bug is dead or gone. The burst is released; kC40RespawnGapMs later card 023 is asked for the
// next bug, until both origins have kC40ShotsWanted traced shots or kC40ExtraBugs were asked for.
void c40_host_next_bug(ULONGLONG now) {
    g_c40.release_due = g_c40.burst_held;
    if (g_c40.asked_spawn != 0 && g_c23.spawn_ms == g_c40.asked_spawn) {
        return; // card 023 has not spawned the asked bug yet
    }
    if (g_c40.dead_ms == 0) {
        g_c40.dead_ms = now;
        log::line("card040: host bug dead or gone; traced shots so far: host %u, relayed %u", g_c40.host_shots,
                  g_c40.relayed_shots);
    }
    const bool enough = g_c40.host_shots >= kC40ShotsWanted && g_c40.relayed_shots >= kC40ShotsWanted;
    if (now < g_c40.dead_ms + kC40RespawnGapMs || g_c40.done_logged) {
        return;
    }
    if (enough || g_c40.extra_bugs >= kC40ExtraBugs) {
        g_c40.done_logged = true;
        log::line("card040: host stops spawning: traced shots host %u, relayed %u, extra bugs %u", g_c40.host_shots,
                  g_c40.relayed_shots, g_c40.extra_bugs);
        return;
    }
    g_c40.asked_spawn = g_c23.spawn_ms;
    g_c40.dead_ms = 0;
    ++g_c40.extra_bugs;
    g_c23.spawn_tries = 0; // card 023 spawns the next bug from the next scan
    log::line("card040: host asks card 023 for bug %u", g_c40.extra_bugs + 1);
}

// Host: Fire 1 on the host's own weapon, once per bug.
void c40_host_press(const C23Enemy& n, const void* pawn, ULONGLONG now) {
    g_c40.burst_spawn = g_c23.spawn_ms;
    g_c40.burst_ms = now;
    g_c40.spot = kC40AimSpots; // the first aim line is logged
    const void* weapon = nullptr;
    void* const asc = c24_weapon_asc(pawn, &weapon);
    const uint64_t tag = fname_cached(g_c24.fire_tag, L"InputTag.Fire");
    if (asc == nullptr || tag == 0) {
        log::line("WARN card040: host burst skipped: weapon %s, weapon ASC %p", object_name(weapon).text, asc);
        return;
    }
    g_orig_asc_tag_pressed(asc, &tag);
    g_c40.burst_held = true;
    log::line("card040: host burst press (Fire 1) on %s at %s %p dist %.0f, Health %.1f; held until it dies or %llu ms",
              object_name(weapon).text, object_name(n.actor).text, n.actor, n.dist, c23_set_health(n.actor),
              static_cast<unsigned long long>(kC40BurstCapMs));
}

// Host, each scan on a live bug: the press, then the aim at kC40AimLift heights in turn while the burst is held.
void c40_host_fire(const C23Enemy& n, void* pc, const void* pawn, ULONGLONG now) {
    g_c40.dead_ms = 0;
    if (now < g_c23.spawn_ms + kC40PressDelayMs || g_c37.burst_held) {
        return;
    }
    if (g_c40.burst_spawn != g_c23.spawn_ms) {
        c40_host_press(n, pawn, now);
    }
    if (!g_c40.burst_held) {
        return;
    }
    if (now >= g_c40.burst_ms + kC40BurstCapMs) {
        g_c40.release_due = true;
        return;
    }
    const uint32_t spot = static_cast<uint32_t>((now - g_c40.burst_ms) / kC40AimStepMs) % kC40AimSpots;
    const Vec3 at{n.loc.x, n.loc.y, n.loc.z + kC40AimLift[spot]};
    c23_face(pc, actor_location(pawn), at);
    if (spot != g_c40.spot) {
        g_c40.spot = spot;
        log::line("card040: host aims at %s %p lift %.0f (dist %.0f, Health %.1f)", object_name(n.actor).text, n.actor,
                  kC40AimLift[spot], n.dist, c23_set_health(n.actor));
    }
}

// Host, each card 023 scan: hold fire on card 023's live bug, then ask for the next one after it dies.
void c40_host_step(const C23List& list, void* pc, const void* pawn, ULONGLONG now) {
    g_c40.host_pawn = pawn;
    if (!g_c40_trace || !g_c23_spawn || g_c23.spawn_ms == 0) {
        return;
    }
    const int32_t i = c40_live_bug(list);
    if (i < 0) {
        c40_host_next_bug(now);
        return;
    }
    c40_host_fire(list.e[i], pc, pawn, now);
}

// Host, every frame: releases the burst once the scan step marks it due.
void c40_host_release(void* engine, ULONGLONG now) {
    if (!g_c40.burst_held || !g_c40.release_due) {
        return;
    }
    g_c40.burst_held = false;
    g_c40.release_due = false;
    host_burst_release(engine, "card040", now - g_c40.burst_ms);
}

// Client, each card 023 scan: holds its own fire while the nearest enemy is alive. The press and release go
// through the AbilityInputTagPressed/Released detours, so card 026 relays them to the host like a mouse press;
// c23_view_step faces the enemy.
void c40_client_step(const C23List& list, const void* pawn, ULONGLONG now) {
    if (!g_c40_trace || !g_client_session) {
        return;
    }
    const int32_t i = c23_nearest(list);
    const bool want = i >= 0 && c23_set_health(list.e[i].actor) > 0.0f;
    if (want == g_c40.client_held) {
        return;
    }
    const void* weapon = nullptr;
    void* const asc = c24_weapon_asc(pawn, &weapon);
    const uint64_t tag = fname_cached(g_c24.fire_tag, L"InputTag.Fire");
    if (asc == nullptr || tag == 0) {
        log::line("WARN card040: client fire skipped: weapon %s, weapon ASC %p", object_name(weapon).text, asc);
        return;
    }
    g_c40.client_held = want;
    if (want) {
        detour_asc_tag_pressed(asc, &tag);
    } else {
        detour_asc_tag_released(asc, &tag);
    }
    log::line("card040: client fire %d on %s (t=%llu)%s%s", want ? 1 : 0, object_name(weapon).text,
              static_cast<unsigned long long>(now), want ? " at " : "", want ? object_name(list.e[i].actor).text : "");
}

StaticClassFn g_bug_anim_class = nullptr; // USTBugAnimPhysComponent
StaticClassFn g_gore_class = nullptr;     // USTGoreComponent
BugAnimDeathFn g_orig_bug_anim_death = nullptr;
GoreDeathFn g_orig_gore_death = nullptr;
NpcVoidFn g_orig_try_play_death = nullptr;
NpcVoidFn g_orig_enemy_start_dying = nullptr;
EnemyKilledFn g_orig_enemy_killed = nullptr;
NpcVoidFn g_orig_release_tickets = nullptr;
DeregisterAiFn g_orig_deregister_ai = nullptr;
NpcVoidFn g_orig_corpse_ticket = nullptr;

constexpr size_t kBugAnimState = 0x430;    // int32 state; OnDeath writes 0x10 when outside 0x10-0x1F
constexpr size_t kBugAnimDeathFlag = 0x428; // byte; the tick sets 1 on state 0x10, then state 0x11
constexpr size_t kBugAnimTickNeedA = 0x360; // the tick returns at once while this is null
constexpr size_t kBugAnimTickNeedB = 0x198; // ... or this (a mesh component, ragdoll calls at state 0x12)
constexpr size_t kC38EventBytes = 0x100;    // zeroed FADDamageEvent stand-in; the callee never reads it

constexpr uint32_t kC38EdgeLines = 64;
constexpr uint32_t kC38HostLines = 64;
constexpr uint32_t kC38LogicLines = 32;
constexpr ULONGLONG kC38ShotMs[] = {1850, 2850, 4850, 10850}; // card 039: 1 s after the host kill shots
constexpr uint32_t kC38Shots = sizeof(kC38ShotMs) / sizeof(kC38ShotMs[0]);

constexpr const char* kC38LogicText[kC38Logic] = {"HandleDeathEvent", "NPC StartDying", "ASTEnemyBase::StartDying",
                                                  "EnemyKilledByGameplayTag", "ReleaseAllTickets", "DeregisterAI",
                                                  "TryTakeCorpseTicket", "StartOverkillState",
                                                  "ReleaseCorpseTicket", "HandleDying",
                                                  "OverkillDamageEffectReceived"};

C38State g_c38{}; // game thread; reset per map by c38_arm_map

void c38_logic(C38Logic which, const void* actor) {
    ++g_c38.logic[which];
    if (note_event_cap(g_c38.logic_lines, kC38LogicLines, "card038 game-logic calls")) {
        log::line("%scard038: %s game-logic %s on %s %s (call %u)%s", g_client_session && !g_c38.in_dying ? "WARN " : "",
                  side(), kC38LogicText[which], class_name(actor).text, object_name(actor).text, g_c38.logic[which],
                  g_c38.in_dying ? " (inside the client's card 131 HandleDying)" : "");
    }
}

void c38_log_counts(const char* when) {
    const uint32_t* const n = g_c38.logic;
    log::line("card038: %s %s: game-logic calls HandleDeathEvent %u, NPC StartDying %u, ASTEnemyBase::StartDying %u, "
              "EnemyKilledByGameplayTag %u, ReleaseAllTickets %u, DeregisterAI %u, TryTakeCorpseTicket %u, "
              "StartOverkillState %u, ReleaseCorpseTicket %u, HandleDying %u, OverkillDamageEffectReceived %u; mod "
              "visual calls BugAnimPhys OnDeath %u, TryPlayDeathAnimation %u, Dying state and HandleDying %u (client copies' local Falling held at the host's mode %u times, drifted copies set back to the host's z %u times, not set back (encroaching) %u times); card 131 corpses "
              "first seen dead %u, revives undone %u, barks blocked %u; dead tracked %u, peak %u of %u, pruned %u",
              side(), when, n[0], n[1], n[2], n[3], n[4], n[5], n[6], n[7], n[8], n[9], n[10], g_c38.calls_bug,
              g_c38.calls_npc, g_c38.calls_dying, g_c38.held, g_c38.drift, g_c38.drift_skipped, g_c38.calls_corpse, g_c38.calls_revive, g_c38.barks_blocked, g_c38.dead_count, g_c38.dead_peak, kC38Max, g_c38.pruned);
}

// True when the client's death edge already ran for `actor`.
bool c38_is_marked(const void* actor) {
    const uint64_t name = read_u64(actor, kObjectName);
    for (uint32_t i = 0; i < g_c38.dead_count; ++i) {
        if (g_c38.dead[i].actor == actor && g_c38.dead[i].name == name) {
            return true;
        }
    }
    return false;
}

// Client, after each card 037 scan: keep only the dead entries whose actor is still in the scan with Health <= 0.
// A destroyed corpse or a pooled bug back to life leaves the set, so a later death of that actor gets its visual too.
void c38_prune(const C37Seen* seen, uint32_t n) {
    uint32_t kept = 0;
    for (uint32_t i = 0; i < g_c38.dead_count; ++i) {
        bool dead = false;
        for (uint32_t j = 0; j < n && !dead; ++j) {
            dead = seen[j].actor == g_c38.dead[i].actor && seen[j].name == g_c38.dead[i].name &&
                   seen[j].health <= 0.0f && seen[j].health != kC37Unseen;
        }
        if (dead) {
            g_c38.dead[kept++] = g_c38.dead[i];
        } else if (g_c38.pruned++ == 0) { // the actor may be gone: its FName only, not the object
            log::line("card038: client first dead-set prune: %s freed (destroyed or alive again), %u tracked",
                      fname_text(&g_c38.dead[i].name).text, g_c38.dead_count - 1);
        }
    }
    g_c38.dead_count = kept;
}

// True when `actor` is newly marked dead; false when it already was or the set is full (then no call is made).
bool c38_mark_dead(const void* actor) {
    const uint64_t name = read_u64(actor, kObjectName);
    for (uint32_t i = 0; i < g_c38.dead_count; ++i) {
        if (g_c38.dead[i].actor == actor && g_c38.dead[i].name == name) {
            return false;
        }
    }
    if (g_c38.dead_count >= kC38Max) {
        if (!g_c38.full_logged) {
            g_c38.full_logged = true;
            log::line("WARN card038: client dead set full (%u) -- further deaths get no visual", kC38Max);
        }
        return false;
    }
    g_c38.dead[g_c38.dead_count++] = C38Dead{actor, name};
    if (g_c38.dead_count > g_c38.dead_peak) {
        g_c38.dead_peak = g_c38.dead_count; // card 130: logged as it rises, so a run ended by quit still shows it
        log::line("card038: client dead-set peak %u of %u (%s)", g_c38.dead_peak, kC38Max, class_name(actor).text);
    }
    return true;
}

// Card 131, client: the NPC copy enters the host's Dying state (see C38State's comment). False, with a WARN, when
// either function is unresolved.
bool c131_client_dying(void* actor) {
    if (g_c131_set_base_state == nullptr || g_orig_handle_dying == nullptr) {
        log::line("WARN card131: client death of %s %s: SetCurrentBaseState or HandleDying unresolved, no Dying state",
                  class_name(actor).text, object_name(actor).text);
        return false;
    }
    g_c131_set_base_state(actor, kNpcStateDying);
    g_c38.in_dying = true;
    c132_dying_line(actor);
    g_orig_handle_dying(actor);
    g_c38.in_dying = false;
    ++g_c38.calls_dying;
    return true;
}

// Card 131: the Dying FGameplayTag HandleDying adds, read through its own load at kHandleDyingTagLoad (the tag is a
// module-local global, not a PDB public). Null, with one WARN, when the bytes there are not mov rcx, [rip+disp32].
const uint64_t* c131_dying_tag() {
    static const uint64_t* tag = nullptr;
    static bool tried = false;
    if (tried) {
        return tag;
    }
    tried = true;
    const uint8_t* const ins = g_exe_base + offsets::ASTNonPlayableCharacterBase_HandleDying.rva + kHandleDyingTagLoad;
    if (ins[0] != 0x48 || ins[1] != 0x8B || ins[2] != 0x0D) {
        log::line("WARN card131: HandleDying +0x%zX is %02X %02X %02X, not mov rcx, [rip+disp32]: no Dying tag, a revive "
                  "keeps it", kHandleDyingTagLoad, ins[0], ins[1], ins[2]);
        return nullptr;
    }
    int32_t disp = 0;
    memcpy(&disp, ins + 3, sizeof(disp));
    tag = reinterpret_cast<const uint64_t*>(ins + 7 + disp);
    return tag;
}

// Card 131: the Dying tag off the NPC's tag counts, as the host's revive does. True when a count was removed.
bool c131_remove_dying_tag(void* actor) {
    void* const comp = read_ptr(actor, kNpcTagComponent);
    const uint64_t* const tag = c131_dying_tag();
    if (comp == nullptr || tag == nullptr || g_c131_update_tag_map == nullptr) {
        return false;
    }
    uint8_t* const container = static_cast<uint8_t*>(comp) + kTagCountContainer;
    if (!g_c131_update_tag_map(container, tag, -1)) {
        return false;
    }
    void* const* const vtable = *static_cast<void* const* const*>(comp);
    reinterpret_cast<TagChangedFn>(vtable[kTagChangedVfunc / sizeof(void*)])(comp, tag, 0);
    return true;
}

// Card 318: what the client mirrors of the host's revive (HandleHealthValueChangedEvent, RVA 0x05193030, steps 4-6)
// beyond the tag and the capsule. Not mirrored, host-only: vfunc +0x790 APawn::IsPawnControlled and +0x850
// SpawnDefaultController (an AI controller on a copy), the +0x690 USTSaveComponent bHighPriority byte (+0x17A, save
// bookkeeping), and the next-tick ForceAwareness timer (AI awareness).
constexpr uint8_t kNpcStateDead = 4;          // ESTBaseNPCState::Dead: the game's own Dying -> Dead when the flipbook ends
constexpr size_t kNpcCoreSm = 0xA10;          // SM_Instance_Core (USTAggregatedSMInstance); StartStateMachine_Core restarts it
constexpr size_t kNpcUpdateTicksVfunc = 0xA78; // ConditionallyUpdateTicks(bool); the revive passes false
constexpr ULONGLONG kC318CheckMs = 2000;      // the read-back after a revive
constexpr double kC318MovedUu = 50.0;         // "moved": more than this since the revive
constexpr uint32_t kC318Pending = 32;
constexpr uint32_t kC318Lines = 60;           // per map
using NpcTicksFn = void (*)(void* npc, bool force);
NpcVoidFn g_c318_stop_dying_sm = nullptr;  // StopStateMachine_Dying (SM_Instance_Dying, NPC +0xA20)
NpcVoidFn g_c318_start_core_sm = nullptr;  // StartStateMachine_Core
NpcVoidFn g_c318_on_revived = nullptr;     // BP_OnRevived, raw address (the card 023 detour passes through when on)
struct C318Wait {
    C31Weak actor;
    uint64_t guid;
    ULONGLONG due;
    Vec3 at;       // the copy's location at the revive
    Vec3 host_at;  // its ReplicatedMovement.Location at the revive (the host's location)
    bool host_ok;
};
struct C318 {
    C318Wait waits[kC318Pending];
    uint32_t n;
    uint32_t revives, left_dead, on_death, not_moved, died_again, no_entry, not_dying, wait_full, lines;
};
C318 g_c318{}; // client, game thread

// Card 318, client: the host's revive steps 5 and 6 on a copy just set to Aware. The core machine is restarted only
// when the copy has one (never created on a client). Returns what ran as bits: 1 dying stop, 2 core start,
// 4 BP_OnRevived, 8 ConditionallyUpdateTicks.
uint32_t c318_revive_steps(void* actor) {
    uint32_t ran = 0;
    if (g_c318_stop_dying_sm != nullptr) {
        g_c318_stop_dying_sm(actor);
        ran |= 1u;
    }
    if (g_c318_start_core_sm != nullptr && read_ptr(actor, kNpcCoreSm) != nullptr) {
        g_c318_start_core_sm(actor);
        ran |= 2u;
    }
    if (g_c318_on_revived != nullptr) {
        g_c318_on_revived(actor);
        ran |= 4u;
    }
    const auto ticks = reinterpret_cast<NpcTicksFn>((*static_cast<void* const* const*>(actor))[kNpcUpdateTicksVfunc / 8]);
    ticks(actor, false);
    return ran | 8u;
}

void c318_wait_add(void* actor) {
    if (g_c318.n >= kC318Pending) {
        ++g_c318.wait_full;
        return;
    }
    C318Wait& w = g_c318.waits[g_c318.n++];
    c31_weak_set(w.actor, actor);
    w.guid = c41_guid(actor);
    w.due = GetTickCount64() + kC318CheckMs;
    w.at = actor_location(actor);
    w.host_ok = c132_rep_location(actor, &w.host_at);
}

// Card 318, client (c37_step): the row 2 s after each revive. A copy whose health set is at 0 again died again inside
// the window (counted, no row). The animator's current sequence is logged but is no WARN: after a revive it stays on
// the finished Death while the drawn flipbook is upright (batch 3 run 11 shot).
void c318_step(ULONGLONG now) {
    for (uint32_t i = g_c318.n; i-- > 0;) {
        C318Wait& w = g_c318.waits[i];
        void* const actor = now >= w.due ? c31_weak_get(w.actor) : nullptr;
        if (now < w.due) {
            continue;
        }
        void* component = nullptr;
        const float health = actor != nullptr ? c21_health(static_cast<const uint8_t*>(actor), &component) : 0.0f;
        const bool has_set = component != nullptr && read_ptr(component, kHealthComponentSet) != nullptr;
        if (actor != nullptr && has_set && health <= 0.0f) {
            ++g_c318.died_again;
        } else if (actor != nullptr) {
            const uint8_t state = static_cast<const uint8_t*>(actor)[kNpcBaseState];
            char seq[32];
            int32_t frame = -1;
            int32_t frames = -1;
            const int death = c318_sprite_now(actor, seq, sizeof(seq), &frame, &frames);
            Vec3 host{};
            const double moved = vec_dist(actor_location(actor), w.at);
            const double host_moved = w.host_ok && c132_rep_location(actor, &host) ? vec_dist(host, w.host_at) : -1.0;
            const bool dead = state == kNpcStateDying || state == kNpcStateDead;
            const bool still = moved <= kC318MovedUu && host_moved > kC318MovedUu;
            g_c318.left_dead += dead ? 1u : 0u;
            g_c318.on_death += death == 1 ? 1u : 0u;
            g_c318.not_moved += still ? 1u : 0u;
            if (note_event_cap(g_c318.lines, kC318Lines, "card318 revive")) {
                log::line("%scard318: client revived %s guid %llu: base state %u, sprite sequence %s frame %d of %d, "
                          "moved %.0f since the revive, host moved %.0f",
                          dead || still ? "WARN " : "", class_name(actor).text,
                          static_cast<unsigned long long>(w.guid), state, seq, frame, frames, moved, host_moved);
            }
        }
        w = g_c318.waits[--g_c318.n];
    }
}

void c318_map_end() {
    if (g_c318.revives + g_c318.no_entry + g_c318.not_dying != 0) {
        log::line("card318: client revives %u, left Dead %u, still on a death sequence after 2 s %u, not moved while "
                  "the host moved %u, died again in the window %u; early returns: no death entry %u, entry not Dying %u; checks not queued %u",
                  g_c318.revives, g_c318.left_dead, g_c318.on_death, g_c318.not_moved, g_c318.died_again, g_c318.no_entry,
                  g_c318.not_dying, g_c318.wait_full);
    }
    g_c318 = C318{};
}

// Card 131, client: Health <= 0 -> > 0 on an NPC whose copy the client put in Dying (a pooled bug reused, a card 130
// item 14 trooper revive). Undone as the host's HandleHealthValueChangedEvent does it: base state Aware, the Dying tag
// removed; and the capsule card 039 set to NoCollision gets its collision back. The death anim is not replayed
// backwards: ReceiveBaseStateChanged(Dying -> Aware) is the Blueprint's own way out, as on the host.
// Card 318: from Dead as well as Dying (the game moves a copy to Dead when its flipbook ends, before most revives),
// in the host's order, with the host's steps 5 and 6 (c318_revive_steps); the sprite death row and arrival wait go
// at once; a row 2 s later reads the result back.
void c131_client_revive(void* actor, float last, float now) {
    const uint64_t name = read_u64(actor, kObjectName);
    const C38Dead* entry = nullptr;
    for (uint32_t i = 0; i < g_c38.dead_count && entry == nullptr; ++i) {
        entry = g_c38.dead[i].actor == actor && g_c38.dead[i].name == name ? &g_c38.dead[i] : nullptr;
    }
    if (entry == nullptr || !entry->dying) {
        g_c318.no_entry += entry == nullptr ? 1u : 0u;
        g_c318.not_dying += entry != nullptr ? 1u : 0u;
        return; // no client death visual ran, or a gib (no Dying state): nothing to undo
    }
    const uint8_t state = static_cast<const uint8_t*>(actor)[kNpcBaseState];
    const bool tag = c131_remove_dying_tag(actor);
    const bool set = (state == kNpcStateDying || state == kNpcStateDead) && g_c131_set_base_state != nullptr;
    if (set) {
        g_c131_set_base_state(actor, kNpcStateAware);
    }
    const uint32_t steps = set ? c318_revive_steps(actor) : 0u;
    const uint32_t sprite = c318_sprite_revive(actor);
    void* const capsule = read_ptr(actor, kActorRootComponent);
    const bool collide = capsule != nullptr && entry->capsule_before != kC28NoCollision &&
                         g_collision_enabled(capsule) == kC28NoCollision;
    if (collide) {
        g_orig_set_collision_enabled(capsule, entry->capsule_before);
    }
    ++g_c38.calls_revive;
    ++g_c318.revives;
    c318_wait_add(actor);
    log::line("%scard131: client revive %s %s Health %.1f -> %.1f: base state %u -> %u%s, Dying tag %s, capsule "
              "collision %s, card318 steps 0x%x, sprite rows dropped %u (%u revives)",
              set && !tag ? "WARN " : "", class_name(actor).text, object_name(actor).text, last, now, state,
              static_cast<const uint8_t*>(actor)[kNpcBaseState], set ? "" : " (not Dying or Dead: left as is)",
              tag ? "removed" : "not removed", collide ? "restored" : "unchanged", steps, sprite, g_c38.calls_revive);
}

// Card 137, client: the death visual c38_death_edge skips for a corpse, played only when no host pose placed it
// (c132_pose_arrival gave up waiting).
void c38_corpse_death_play(void* actor) {
    void* const anim = find_component(actor, g_bug_anim_class);
    alignas(16) uint8_t event[kC38EventBytes] = {};
    if (anim != nullptr) {
        g_orig_bug_anim_death(anim, event);
        ++g_c38.calls_bug;
    } else {
        g_orig_try_play_death(actor);
        ++g_c38.calls_npc;
    }
}

// Client: the death visuals for one NPC whose Health just crossed to <= 0, or (corpse, card 131) one first seen with
// Health <= 0: the same visuals once per actor, without the death burst and the bark (the corpse was already there).
void c38_death_edge(void* actor, float last, float now, bool corpse) {
    if (g_npc_class == nullptr || !is_a(actor, g_npc_class()) || !c38_mark_dead(actor)) {
        return;
    }
    void* const anim = find_component(actor, g_bug_anim_class);
    const Vec3 at = actor_location(actor);
    const int32_t before = anim != nullptr ? read_i32(anim, kBugAnimState) : -1;
    alignas(16) uint8_t event[kC38EventBytes] = {};
    C38Dead* const entry = &g_c38.dead[g_c38.dead_count - 1]; // c38_mark_dead just appended it
    void* const capsule = read_ptr(actor, kActorRootComponent);
    entry->capsule_before = capsule != nullptr ? g_collision_enabled(capsule) : kC28NoCollision;
    const bool gibbed = c39_death_edge(actor, now, corpse);
    const uint8_t state_before = static_cast<const uint8_t*>(actor)[kNpcBaseState];
    g_c38.corpse = corpse;
    const bool dying = !gibbed && c131_client_dying(actor);
    g_c38.corpse = false;
    entry->dying = dying;
    g_c38.calls_corpse += corpse ? 1u : 0u;
    if (gibbed) {
        // card 039: the gore replaces the collapse, as on the host
    } else if (corpse) {
        // card 137 (hb70): a corpse is not killed again; it is held hidden until the host's pose places it
        c132_pose_arrival(actor);
    } else if (anim != nullptr) {
        c291_death_edge(actor, false); // card 291: the edge time only (the held ragdoll row's ms after the edge)
        g_orig_bug_anim_death(anim, event);
        ++g_c38.calls_bug;
    } else {
        if (!c291_death_edge(actor, true)) { // card 291: the host's death sequence when its row came first
            g_orig_try_play_death(actor);
        }
        ++g_c38.calls_npc;
    }
    if (corpse) {
        log::line("card131: client corpse first seen dead %s %s Health %.1f at (%.0f, %.0f, %.0f): %s, base state %u -> "
                  "%u%s, no death burst, barks blocked %u so far; client FedScore %d",
                  class_name(actor).text, object_name(actor).text, now, at.x, at.y, at.z,
                  gibbed ? "card 039 gib" : "card137 pose arrival, no death play",
                  state_before, static_cast<const uint8_t*>(actor)[kNpcBaseState], dying ? ", HandleDying called" : "",
                  g_c38.barks_blocked, c131_score(actor));
        return; // no kill shots and no edge line for a corpse
    }
    if (!c23_trooper(actor) && g_c38.edge_ms == 0) {
        g_c38.edge_ms = GetTickCount64();
        g_c38.shot_bug = C38Dead{actor, read_u64(actor, kObjectName)};
        g_c38.shot_loc = at;
    }
    if (note_event_cap(g_c38.edge_lines, kC38EdgeLines, "card038 client death edges")) {
        log::line("card038: client death edge %s %s Health %.1f -> %.1f at (%.0f, %.0f, %.0f): %s; anim state "
                  "0x%x -> 0x%x (gore component %s; card 039 calls Gore OnDeath for the death burst); card 131 base state "
                  "%u -> %u%s; client FedScore %d",
                  class_name(actor).text, object_name(actor).text, last, now, at.x, at.y, at.z,
                  gibbed ? "card 039 gib" : (anim != nullptr ? "BugAnimPhys OnDeath" : "TryPlayDeathAnimation"), before,
                  anim != nullptr ? read_i32(anim, kBugAnimState) : -1,
                  find_component(actor, g_gore_class) != nullptr ? "present" : "absent", state_before,
                  static_cast<const uint8_t*>(actor)[kNpcBaseState], dying ? ", HandleDying called" : "", c131_score(actor));
    }
}

// Client, from card 037's scan: the shot bug's anim state after each kill shot (re-fetched, never kept).
void c38_state_line(const void* actor) {
    if (!g_c38.state_due || actor != g_c38.shot_bug.actor || read_u64(actor, kObjectName) != g_c38.shot_bug.name) {
        return;
    }
    g_c38.state_due = false;
    const void* const anim = find_component(actor, g_bug_anim_class);
    const uint8_t* const a = static_cast<const uint8_t*>(anim);
    const Vec3 at = actor_location(actor);
    log::line("card038: client bug %s +%llu ms after the edge: anim %p state 0x%x, death flag %u, tick needs +0x360 %p "
              "+0x198 %p; at (%.0f, %.0f, %.0f)",
              object_name(actor).text, static_cast<unsigned long long>(GetTickCount64() - g_c38.edge_ms), anim,
              a != nullptr ? read_i32(a, kBugAnimState) : -1, a != nullptr ? a[kBugAnimDeathFlag] : 0u,
              a != nullptr ? read_ptr(a, kBugAnimTickNeedA) : nullptr, a != nullptr ? read_ptr(a, kBugAnimTickNeedB) : nullptr,
              at.x, at.y, at.z);
}

// Client with the audit, every frame: the kill shots kC38ShotMs after the first bug death edge.
void c38_shot_step(void* engine, ULONGLONG now) {
    if (!g_c37.audit || g_c38.edge_ms == 0 || g_c38.shots >= kC38Shots ||
        now < g_c38.edge_ms + kC38ShotMs[g_c38.shots]) {
        return;
    }
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    const void* const pawn = pc != nullptr ? c23_own_pawn(pc) : nullptr;
    ++g_c38.shots;
    g_c38.state_due = true;
    if (pawn == nullptr) {
        log::line("WARN card038: client kill shot %u skipped: no pawn on foot", g_c38.shots);
        return;
    }
    c23_face(pc, actor_location(pawn), g_c38.shot_loc);
    request_shot(pc, false);
    log::line("card038: client kill shot %u requested (+%llu ms after the edge)", g_c38.shots,
              static_cast<unsigned long long>(now - g_c38.edge_ms));
    if (g_c38.shots == kC38Shots) {
        c38_log_counts("after the last kill shot");
    }
}

// Host with the audit: one line per visual death native, with the time since the NPC's HandleDeathEvent.
void c38_host_line(const char* what, const void* actor, const char* extra) {
    if (!g_c37.audit || g_client_session || !note_event_cap(g_c38.host_lines, kC38HostLines, "card038 host natives")) {
        return;
    }
    log::line("card038: host %s %s %s, +%lld ms after HandleDeathEvent%s", what, class_name(actor).text,
              object_name(actor).text,
              g_c38.death_ms != 0 ? static_cast<long long>(GetTickCount64() - g_c38.death_ms) : -1LL, extra);
}

void detour_bug_anim_death(void* component, const void* damage_event) {
    const int32_t before = read_i32(component, kBugAnimState);
    g_orig_bug_anim_death(component, damage_event);
    char extra[64];
    snprintf(extra, sizeof(extra), "; state 0x%x -> 0x%x", before, read_i32(component, kBugAnimState));
    c38_host_line("BugAnimPhys OnDeath", read_ptr(component, kComponentOwner), extra);
}

void detour_gore_death(void* component, const Vec3* location) {
    const void* const owner = read_ptr(component, kComponentOwner);
    char extra[128];
    const Vec3 at = owner != nullptr ? actor_location(owner) : Vec3{};
    snprintf(extra, sizeof(extra), "; vector (%.0f, %.0f, %.0f), actor at (%.0f, %.0f, %.0f)", location->x,
             location->y, location->z, at.x, at.y, at.z);
    c38_host_line("Gore OnDeath", owner, extra);
    g_orig_gore_death(component, location); // its SpawnGoreResponse carries the host's burst row (detour_spawn_gore)
}

void detour_try_play_death(void* npc) {
    c38_host_line("TryPlayDeathAnimation", npc, "");
    g_orig_try_play_death(npc);
    c177_sprite_death_host(npc); // card 177: a sprite NPC's death row (after the original: reads the name it played)
    c269_death_note(npc);        // card 269: a 3D NPC's death name, for a rest row if it reaches Dead with no ragdoll
}

// Card 269: ASTNonPlayableCharacterBase::ReceiveBaseStateChanged (not virtual; every state setter calls it after the
// state byte is written). The host's Dead state with no pose row opens a rest row (pose_c132.cpp).
BaseStateChangedFn g_orig_c269_state = nullptr;

void detour_c269_state(void* npc, uint8_t old_state, uint8_t new_state) {
    g_orig_c269_state(npc, old_state, new_state);
    c269_state_host(npc, new_state);
}

void detour_enemy_start_dying(void* npc) {
    c38_logic(kC38EnemyStartDying, npc);
    g_orig_enemy_start_dying(npc);
}

void detour_enemy_killed(void* meta_game, uint64_t tag) {
    c38_logic(kC38EnemyKilled, meta_game);
    g_orig_enemy_killed(meta_game, tag);
}

void detour_release_tickets(void* npc) {
    c38_logic(kC38ReleaseTickets, npc);
    g_orig_release_tickets(npc);
}

void detour_deregister_ai(void* awareness, void* npc) {
    c38_logic(kC38DeregisterAi, npc);
    g_orig_deregister_ai(awareness, npc);
}

void detour_corpse_ticket(void* npc) {
    c38_logic(kC38CorpseTicket, npc);
    g_orig_corpse_ticket(npc);
}

NpcVoidFn g_orig_start_overkill = nullptr;
SpawnGoreFn g_orig_spawn_gore = nullptr;
NpcVoidFn g_orig_try_play_overkill = nullptr;
NpcVoidFn g_orig_release_corpse = nullptr;
NpcVoidFn g_orig_handle_dying = nullptr;
NpcSetStateFn g_c131_set_base_state = nullptr; // card 131
FedScoreSubsystemFn g_c131_score_subsystem = nullptr; // card 131
FedScoreFn g_c131_fed_score = nullptr;               // card 131

int32_t c131_score(void* actor) {
    if (actor == nullptr || g_c131_score_subsystem == nullptr || g_c131_fed_score == nullptr) {
        return -1;
    }
    void* const world = g_actor_get_world(actor);
    const void* const sub = world != nullptr ? g_c131_score_subsystem(world) : nullptr;
    return sub != nullptr ? g_c131_fed_score(sub) : -1;
}
TagMapFn g_c131_update_tag_map = nullptr;       // card 131
BarkFn g_orig_npc_play_bark = nullptr;          // card 131
BarkFn g_orig_bark_near_player = nullptr;       // card 131

// Card 131: both barks pass through, except inside the client's HandleDying for a corpse first seen dead.
bool detour_npc_play_bark(void* npc, void* bark) {
    if (g_c38.corpse) {
        ++g_c38.barks_blocked;
        return false;
    }
    return g_orig_npc_play_bark(npc, bark);
}

bool detour_bark_near_player(void* context, void* bark) {
    if (g_c38.corpse) {
        ++g_c38.barks_blocked;
        return false;
    }
    return g_orig_bark_near_player(context, bark);
}

// Card 451 (host, always on). UADBarkSubsystem::RequestBark refuses a bark whose speaker is farther than the event
// data's range (class default object +0x34, a float) from the first player controller's view point, so a client
// next to an NPC far from the host got no bark. The sound itself reaches the client by card 136 (the subsystem's
// PlaySound calls SpawnSoundAttached). When a remote player's pawn is inside the range, the range is widened for
// the one call and restored after it; the host hears that bark by its own attenuation, as any far sound.
constexpr size_t kC451ClassDefault = 0x110; // UClass::ClassDefaultObject (RequestBark+0x56)
constexpr size_t kC451Range = 0x34;        // UADBarkEventData range, units (RequestBark+0x18B)
constexpr float kC451Wide = 1.0e18f;       // its square is still a finite float
constexpr uint32_t kC451Rows = 8;          // rows per process
C451BarkFn g_orig_c451_request_bark = nullptr;
uint32_t g_c451_widened = 0;

// Whether a remote player's pawn is within `range` of the speaker.
static bool c451_remote_in_range(void* speaker, float range) {
    const Vec3 at = c74_actor_loc(speaker);
    for (int32_t s = 0; s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(speaker, s);
        const void* const pawn = pc != nullptr && c26_is_remote_pc(pc) ? read_ptr(pc, kControllerPawn) : nullptr;
        if (pawn == nullptr) {
            continue;
        }
        const Vec3 p = c74_actor_loc(pawn);
        const double dx = p.x - at.x;
        const double dy = p.y - at.y;
        const double dz = p.z - at.z;
        if (dx * dx + dy * dy + dz * dz <= static_cast<double>(range) * static_cast<double>(range)) {
            return true;
        }
    }
    return false;
}

// Card 496 (both sides): UADBarkSubsystem::ProcessNextBarkIndex (0x04C5BC20) keys the map at subsystem+0x70 by the
// bark event class, and that map is no UPROPERTY: GC neither keeps its keys nor clears them. On the client nothing
// else holds a bark class (the host's server-only AI does), so one unloaded (c247b242b: BP_BarkType_sawbughopper_C,
// card451 "class 0" drop row), and the next lookup ran IsChildOf on the freed key (exe+0x4C59B53, client crash).
// Each bark class is rooted the first time RequestBark sees it; barks are a few dozen fixed assets.
constexpr int32_t kC496Max = 128;
struct C496Table {
    const void* rooted[kC496Max];
    int32_t count;
    bool full_logged;
    uint32_t gone_lines; // not-rooted rows for a class with no live entry (it is not tabled, so it is tried again)
};
static C496Table g_c496{}; // game thread

static void c496_root(const void* cls) {
    for (int32_t i = 0; i < g_c496.count && i < kC496Max; ++i) {
        if (g_c496.rooted[i] == cls) {
            return;
        }
    }
    if (g_c496.count >= kC496Max) {
        if (!g_c496.full_logged) {
            g_c496.full_logged = true;
            log::line("WARN card496: %s t=%llu bark class %s not rooted: the table of %d is full", side(),
                      static_cast<unsigned long long>(GetTickCount64()), object_name(cls).text, kC496Max);
        }
        return;
    }
    int32_t flags = 0;
    if (!object_add_root(cls, &flags)) {
        if (note_event_cap(g_c496.gone_lines, 8, "card496 not rooted")) {
            log::line("WARN card496: %s t=%llu bark class %s not rooted: no live object entry, or SetRootFlags unresolved", side(),
                      static_cast<unsigned long long>(GetTickCount64()), object_name(cls).text);
        }
        return;
    }
    g_c496.rooted[g_c496.count++] = cls;
    log::line("card496: %s t=%llu rooted bark class %s (%d of %d), item flags 0x%08X", side(),
              static_cast<unsigned long long>(GetTickCount64()), object_name(cls).text, g_c496.count, kC496Max,
              static_cast<uint32_t>(flags));
}

bool detour_c451_request_bark(void* subsystem, void* speaker, void* const* bark, void* const* voice) {
    if (bark != nullptr && *bark != nullptr) {
        c496_root(*bark); // card 496: before the original adds the class to its map, both sides
    }
    void* const cls = !g_client_session && speaker != nullptr && bark != nullptr ? *bark : nullptr;
    auto* const cdo = cls != nullptr ? static_cast<uint8_t*>(read_ptr(cls, kC451ClassDefault)) : nullptr;
    if (cdo == nullptr) {
        return g_orig_c451_request_bark(subsystem, speaker, bark, voice);
    }
    float* const range = reinterpret_cast<float*>(cdo + kC451Range);
    const float own = *range;
    if (!(own > 0.0f) || own >= kC451Wide || !c451_remote_in_range(speaker, own)) {
        return g_orig_c451_request_bark(subsystem, speaker, bark, voice);
    }
    *range = kC451Wide;
    const bool played = g_orig_c451_request_bark(subsystem, speaker, bark, voice);
    *range = own;
    if (g_c451_widened++ < kC451Rows) {
        log::line("card451: host bark %s from %s: a client is within its range %.0f, host range check passed over; "
                  "played %d",
                  object_name(cls).text, object_name(speaker).text, static_cast<double>(own), played ? 1 : 0);
    }
    return played;
}
NpcVoidFn g_orig_update_colliders = nullptr;
OverkillEffectFn g_orig_overkill_effect = nullptr;
GoreOverkillFn g_orig_gore_overkill = nullptr;
NpcVoidFn g_enable_corpse_box = nullptr;
GoreLocationFn g_gore_location = nullptr;
KinematicBonesFn g_kinematic_bones = nullptr;
SetRelativeLocationFn g_set_relative_location = nullptr; // card 524
BodySimulatingFn g_body_simulating = nullptr;
SkeletalComFn g_skeletal_com = nullptr;
SetSimulateFn g_orig_c132_set_simulate = nullptr;
ShowStaticFn g_orig_c132_show_static = nullptr;
ExecThunkFn g_orig_c132_exec_set_simulate = nullptr;
ExecThunkFn g_orig_c132_exec_set_hidden = nullptr;
SetHiddenInGameFn g_orig_c132_set_hidden = nullptr;
NpcBoolFn g_is_npc_2d = nullptr;
HealthFloatFn g_overkill_threshold = nullptr;
CollisionEnabledFn g_collision_enabled = nullptr;
ProfileNameFn g_collision_profile = nullptr;
CollisionTypeFn g_collision_type = nullptr;

NpcEndPlayFn g_orig_npc_end_play = nullptr;
bool g_c39_off = false;      // UBWCOOP_NO_CLIENT_GORE

constexpr size_t kNpcCorpseBox = 0xA70;      // UBoxComponent* (EnableCorpseBoxCollider)
constexpr size_t kNpcGore = 0xA90;           // USTGoreComponent* (StartOverkillState)
constexpr size_t kCharacterMesh = 0x328;     // ACharacter::Mesh
constexpr size_t kHealthDead = 0x148;        // UADHealthComponent byte, RefreshState: Health <= 0
constexpr size_t kHealthOverkilled = 0x149;  // byte, RefreshState: -|OverkillThreshold| >= Health (IsOverkilled)
constexpr size_t kGoreOffset = 0x00;         // FVector added to the location (SpawnGoreResponse)
constexpr size_t kGoreChunks = 0x18;         // TArray, stride 0xC8, spawned through the GameMode pool
constexpr size_t kGoreNiagara = 0x28;        // TArray, stride 0x28: +0x00 UNiagaraSystem*, +0x08 offset, +0x20 chance
constexpr size_t kGoreSounds = 0x58;         // TArray
constexpr size_t kGameModeSpawnManager = 0x388; // SpawnGoreResponse: Niagara and chunks need it non-null
constexpr size_t kCompVisibleByte = 0x1A0;   // bit 0x20 bVisible (USceneComponent::IsVisible)
constexpr uint8_t kCompVisibleBit = 0x20;
constexpr size_t kCompHiddenByte = 0x1A1;    // bit 0x08 bHiddenInGame
constexpr uint8_t kCompHiddenBit = 0x08;
constexpr size_t kBodyResponses = 0x3E0;     // byte per channel (GetCollisionResponseToChannel)
constexpr uint32_t kC39Channels = 8;         // ECC_WorldStatic .. ECC_Destructible, then game channels 0-1
constexpr ULONGLONG kC39ProbeMs[] = {0, 1000, 5000};
constexpr uint32_t kC39Probes = sizeof(kC39ProbeMs) / sizeof(kC39ProbeMs[0]);
constexpr uint32_t kC39ProbeMax = 32;
constexpr uint32_t kC39ProbeLines = 96;
constexpr uint32_t kC39WhereLines = 64; // client gore location vs actor and mesh, one line per mod gore call
constexpr uint32_t kC39TraceLines = 512;
constexpr uint32_t kC39EndPlayLines = 64;
constexpr uint32_t kC39GibLines = 32;
constexpr uint32_t kC39Gibbed = 16;
constexpr double kC39CellUu = 256.0;         // Gib payload: location cell size per axis
constexpr uint32_t kC39CellBits = 7;         // bits per axis; cells wrap every 128 (32768 uu)
constexpr uint32_t kC39CellMask = (1u << kC39CellBits) - 1;
constexpr ULONGLONG kC39GibShotMs = 300;     // client: a shot this long after the Gib signal (the burst is short)
constexpr uint32_t kC39CellSlack = 1;        // client match: within one cell per axis (replication lag)
constexpr uint32_t kC39ColliderLines = 32;
constexpr uint32_t kC39EdgeLines = 64;

struct C39Probe {
    const void* actor; // identity only, re-found in a live scan
    uint64_t name;
    ULONGLONG death_ms;
    uint32_t done; // probes logged
};
struct C39State {
    C39Probe probe[kC39ProbeMax];
    uint32_t probe_count;
    uint32_t probe_lines, trace_lines, collider_lines, edge_lines, end_lines, gib_lines, where_lines;
    uint32_t static_lines, static_blocked; // both roles: static-mesh swaps refused for dead NPCs
    uint32_t ragdoll_lines, ragdolls, ragdolls_stale, hidden_lines, sim_off_lines; // both roles: StartRagdoll calls, those with stale bodies
    uint32_t gibs, boxes, bursts, capsules, sent, received, unmatched;
    uint32_t hit_responses; // client: gore responses from replayed host hits
    C38Dead gibbed[kC39Gibbed]; // client: NPCs already gibbed (signal or edge); host: NPCs already signalled
    uint32_t gibbed_count;
    ULONGLONG gib_shot_ms; // client: when the early gib shot is due (0 = none)
    Vec3 gib_loc;
    bool in_mod_call; // the client's own OnOverkill is running
    bool in_hit_call; // the client's replay of a host hit (Gore OnDamage) is running
    bool full_logged;
    uint32_t c357_skips, c357_lines; // client: card 357 StartOverkillState calls skipped, rows logged
};
C39State g_c39{}; // game thread; reset per map by c39_arm_map

void c39_line(const char* what, const void* actor, const char* extra) {
    const bool colliders = strcmp(what, "UpdateColliders") == 0; // virtual, called often: its own cap
    if (!g_c37.audit || !(colliders ? note_event_cap(g_c39.collider_lines, kC39ColliderLines, "card039 UpdateColliders")
                                    : note_event_cap(g_c39.trace_lines, kC39TraceLines, "card039 trace"))) {
        return;
    }
    const long long ms = g_c38.death_ms != 0 ? static_cast<long long>(GetTickCount64() - g_c38.death_ms) : -1LL;
    log::line("card039: %s %s %s %s, +%lld ms after HandleDeathEvent%s%s", side(), what, class_name(actor).text,
              object_name(actor).text, g_client_session ? -1LL : ms, extra, g_c39.in_mod_call ? " (mod call)" : "");
}

// "Health h threshold t dead d overkilled o" from the NPC's health component, for the trace and the edge lines.
void c39_health_text(const void* actor, char* out, size_t size) {
    const void* const hc = c21_character_ptr(actor, kCharacterHealthComponent);
    const void* const set = hc != nullptr ? read_ptr(hc, kHealthComponentSet) : nullptr;
    if (set == nullptr) {
        snprintf(out, size, "; no health set");
        return;
    }
    const uint8_t* const h = static_cast<const uint8_t*>(hc);
    snprintf(out, size, "; Health %.1f OverkillThreshold %.1f dead %u overkilled %u state %u",
             read_f32(set, kHealthSetHealth + kAttrDataCurrent), g_overkill_threshold(hc), h[kHealthDead],
             h[kHealthOverkilled], static_cast<const uint8_t*>(actor)[kNpcBaseState]);
}

constexpr uint32_t kC357Lines = 64;

// Card 357: on a client the host's Gib signal is the only overkill writer. The game's StartOverkillState (reached from
// the client's own HealthAttributeChanged once a corpse's replicated Health crosses -OverkillThreshold) would set the
// state byte to 5, call ReleaseCorpseTicket (its only caller), spawn the overkill gore a second time and SetLifeSpan(0.1)
// on the replicated corpse. Skipped unless the mod itself is in the call. True when the call was skipped.
bool c357_skip_overkill(const void* npc) {
    if (!g_client_session || g_c39.in_mod_call) {
        return false;
    }
    ++g_c39.c357_skips;
    if (note_event_cap(g_c39.c357_lines, kC357Lines, "card357 skip")) {
        const auto* const hc = static_cast<const uint8_t*>(c21_character_ptr(npc, kCharacterHealthComponent));
        const void* const set = hc != nullptr ? read_ptr(hc, kHealthComponentSet) : nullptr;
        log::line("card357: client StartOverkillState skipped on %s %s guid 0x%llx (skip %u): Health %.1f "
                  "OverkillThreshold %.1f dead %u overkilled %u state %u; the host's Gib signal brings the gore",
                  class_name(npc).text, object_name(npc).text, static_cast<unsigned long long>(c41_guid(npc)),
                  g_c39.c357_skips, set != nullptr ? read_f32(set, kHealthSetHealth + kAttrDataCurrent) : 0.0f,
                  hc != nullptr ? g_overkill_threshold(hc) : 0.0f, hc != nullptr ? hc[kHealthDead] : 0u,
                  hc != nullptr ? hc[kHealthOverkilled] : 0u, static_cast<const uint8_t*>(npc)[kNpcBaseState]);
    }
    return true;
}

void detour_start_overkill(void* npc) {
    if (c357_skip_overkill(npc)) {
        return;
    }
    char extra[192];
    c39_health_text(npc, extra, sizeof(extra));
    const uint8_t* const mode = static_cast<const uint8_t*>(g_orig_get_game_mode(npc));
    const size_t used = strlen(extra);
    snprintf(extra + used, sizeof(extra) - used, "; GameMode %p spawn manager +0x388 %p", mode,
             mode != nullptr ? read_ptr(mode, kGameModeSpawnManager) : nullptr);
    c39_line("StartOverkillState", npc, extra);
    c38_logic(kC38StartOverkill, npc);
    g_orig_start_overkill(npc);
}

void c42_host_gore(const void* mode, const void* data);                                    // card 042
void c42_client_chunks(const void* owner, const void* data, const Vec3& at, const Vec3& rot); // card 042
void detour_spawn_gore(void* context, const void* data, const Vec3* location, const Vec3* direction,
                       const Vec3* rotation, float* out) {
    const Vec3 at = *location; // SpawnGoreResponse adds the response offset to *location in place
    const Vec3 rot = *rotation;
    const bool gore = g_gore_class != nullptr && context != nullptr && is_a(context, g_gore_class());
    const void* const owner = gore ? read_ptr(context, kComponentOwner) : context;
    const void* const mode = g_orig_get_game_mode(context);
    if (g_c37.audit) { // card 330: c39_line drops the row without the audit, so its text is built only with it
        char extra[256];
        snprintf(extra, sizeof(extra),
                 "; context %s, at (%.0f, %.0f, %.0f) direction (%.2f, %.2f, %.2f) rotation (%.0f, %.0f, %.0f), "
                 "chunks %d niagara %d sounds %d, GameMode %p spawn manager %p", class_name(context).text,
                 location->x, location->y, location->z, direction->x, direction->y, direction->z, rotation->x,
                 rotation->y, rotation->z, read_i32(data, kGoreChunks + kArrayNum),
                 read_i32(data, kGoreNiagara + kArrayNum), read_i32(data, kGoreSounds + kArrayNum), mode,
                 mode != nullptr ? read_ptr(mode, kGameModeSpawnManager) : nullptr);
        c39_line("SpawnGoreResponse", owner, extra);
    }
    if (!g_client_session && mode != nullptr) {
        c42_host_gore(mode, data);
    }
    g_orig_spawn_gore(context, data, location, direction, rotation, out);
    if (g_client_session && (g_c39.in_mod_call || g_c39.in_hit_call)) {
        c42_client_chunks(owner, data, at, rot); // the chunks the original's GameMode gate skipped
    }
    // Card 136 review 2: the response's Niagara (hit, death and overkill) is the host's, sent by the effects manager
    // with the gore component's owner; the client's own call above gets only its chunks.
    g_c39.hit_responses += g_client_session && g_c39.in_hit_call ? 1u : 0u;
}

namespace {

constexpr uint32_t kC132GoreLines = 40; // gore trace lines per side per process; then one per kC132GoreEvery calls
constexpr uint32_t kC132GoreEvery = 50;

struct C132Gore {
    uint32_t decals;   // SpawnGoreDecals calls
    uint32_t attached; // SpawnDecalAttached calls
    uint32_t at;       // SpawnDecalAtLocation calls
    uint32_t nulls;    // decal spawns that returned null
    // SpawnDecalAttached (exe+0x5138D60) null causes, read before the call: no material, no component, a component
    // with bReceivesDecals clear (+0x26C bit 0x40, the native early out); the rest come from the pool spawn
    // (exe+0x5120C60: manager disabled or cap 0; a full pool releases active[0] and does not return null).
    uint32_t null_material, null_component, null_no_decals, null_other;
    uint32_t lines;
};
C132Gore g_c132_gore{};
constexpr size_t kPrimitiveDecalFlags = 0x26C; // UPrimitiveComponent bitfield byte holding bReceivesDecals
constexpr uint8_t kPrimitiveReceivesDecals = 0x40;

// Counts a null SpawnDecalAttached under its cause.
void c132_count_attached_null(const void* material, const void* component) {
    if (material == nullptr) {
        ++g_c132_gore.null_material;
    } else if (component == nullptr) {
        ++g_c132_gore.null_component;
    } else if ((static_cast<const uint8_t*>(component)[kPrimitiveDecalFlags] & kPrimitiveReceivesDecals) == 0) {
        ++g_c132_gore.null_no_decals;
    } else {
        ++g_c132_gore.null_other;
    }
}

bool c132_gore_line(uint32_t count) {
    if (g_c132_gore.lines < kC132GoreLines) {
        ++g_c132_gore.lines;
        return true;
    }
    return count % kC132GoreEvery == 0;
}

} // namespace

SpawnGoreDecalsFn g_orig_spawn_gore_decals = nullptr;
SpawnDecalAttachedFn g_orig_spawn_decal_attached = nullptr;
SpawnDecalAtLocationFn g_orig_spawn_decal_at = nullptr;

void detour_spawn_gore_decals(void* context, const void* data, const Vec3* location, const Vec3* direction) {
    const uint32_t before = g_c132_gore.attached + g_c132_gore.at;
    g_orig_spawn_gore_decals(context, data, location, direction);
    const uint32_t n = ++g_c132_gore.decals;
    if (c132_gore_line(n)) {
        log::line("card132: %s gore trace SpawnGoreDecals %u: context %s at (%.0f, %.0f, %.0f), %u decal spawns "
                  "inside, hit call %d mod call %d", side(), n, class_name(context).text, location->x, location->y,
                  location->z, g_c132_gore.attached + g_c132_gore.at - before, g_c39.in_hit_call ? 1 : 0,
                  g_c39.in_mod_call ? 1 : 0);
    }
}

void* detour_spawn_decal_attached(void* manager, void* material, const Vec3* size, void* component, uint64_t socket,
                                  const Vec3* location, const Vec3* rotation, uint8_t attach, float life) {
    if (c132_decal_suppress(material)) {
        return nullptr; // client: the host's Decal rows bring its gore decals
    }
    c132_decal_before(manager);
    C136Call c{C136Point::DecalAttached};
    c.asset = material;
    c.attach = component;
    c.socket = socket;
    c.at = location != nullptr ? *location : Vec3{};
    c.rot = rotation != nullptr ? *rotation : Vec3{};
    c.scale = size != nullptr ? *size : Vec3{1.0, 1.0, 1.0}; // card 136: the decal size rides in scale
    c.look[0] = life;
    c.flags = static_cast<uint8_t>((attach & kC136FlagLocationMask) << kC136FlagLocationShift);
    const uint32_t id = c136_fx_enter(c);
    void* const decal = c136_blocked() ? nullptr // card 136 step 3: the host sends it
                                       : g_orig_spawn_decal_attached(manager, material, size, component, socket,
                                                                     location, rotation, attach, life);
    c136_fx_leave(c.point, id, decal);
    c132_decal_spawned(manager, decal, material, size, component, socket, location, rotation, attach, life,
                       _ReturnAddress());
    const uint32_t n = ++g_c132_gore.attached;
    if (decal == nullptr) {
        ++g_c132_gore.nulls;
        c132_count_attached_null(material, component);
    }
    if (c132_gore_line(n)) {
        const C132Gore& g = g_c132_gore;
        log::line("card132: %s gore trace SpawnDecalAttached %u: %s on %s of %s, at (%.0f, %.0f, %.0f) attach %u "
                  "size (%.0f, %.0f, %.0f) life %.1f -> %s (%u null so far; attached nulls: material %u, component "
                  "%u, no decals %u, other %u)", side(), n, object_name(material).text, object_name(component).text,
                  component != nullptr ? class_name(read_ptr(component, kComponentOwner)).text : "-", location->x,
                  location->y, location->z, attach, size->x, size->y, size->z, life,
                  decal != nullptr ? "decal" : "null", g.nulls, g.null_material, g.null_component, g.null_no_decals,
                  g.null_other);
    }
    return decal;
}

void* detour_spawn_decal_at(void* manager, const void* context, void* material, const Vec3* size,
                            const Vec3* location, const Vec3* rotation, float life) {
    if (c132_decal_suppress(material)) {
        return nullptr; // client: the host's Decal rows bring its gore decals
    }
    c132_decal_before(manager);
    C136Call c{C136Point::DecalAt};
    c.asset = material;
    c.context = context;
    c.at = location != nullptr ? *location : Vec3{};
    c.rot = rotation != nullptr ? *rotation : Vec3{};
    c.scale = size != nullptr ? *size : Vec3{1.0, 1.0, 1.0}; // card 136: the decal size rides in scale
    c.look[0] = life;
    const uint32_t id = c136_fx_enter(c);
    void* const decal = c136_blocked() ? nullptr // card 136 step 3: the host sends it
                                       : g_orig_spawn_decal_at(manager, context, material, size, location, rotation,
                                                               life);
    c136_fx_leave(c.point, id, decal);
    c132_decal_spawned(manager, decal, material, size, nullptr, 0, location, rotation, 255, life, _ReturnAddress());
    const uint32_t n = ++g_c132_gore.at;
    g_c132_gore.nulls += decal == nullptr ? 1u : 0u;
    if (c132_gore_line(n)) {
        log::line("card132: %s gore trace SpawnDecalAtLocation %u: %s by %s at (%.0f, %.0f, %.0f) size (%.0f, %.0f, "
                  "%.0f) life %.1f -> %s (%u null so far)", side(), n, object_name(material).text,
                  class_name(context).text, location->x, location->y, location->z, size->x, size->y, size->z, life,
                  decal != nullptr ? "decal" : "null", g_c132_gore.nulls);
    }
    return decal;
}

void detour_try_play_overkill(void* npc) {
    c39_line("TryPlayOverkillAnimation", npc, "");
    g_orig_try_play_overkill(npc);
}

void detour_release_corpse(void* npc) {
    c39_line("ReleaseCorpseTicket", npc, "");
    c38_logic(kC38ReleaseCorpse, npc);
    g_orig_release_corpse(npc);
}

void detour_overkill_effect(void* health, const void* callback, const void* damage_event) {
    const void* const owner = read_ptr(health, kComponentOwner);
    // Range targets and other damageables own a health component too; only an NPC has the character layout that
    // c39_health_text reads (+0x688 on a target is a double, not a pointer: session D fault at the shooting range).
    if (owner == nullptr || g_npc_class == nullptr || !is_a(owner, g_npc_class())) {
        g_orig_overkill_effect(health, callback, damage_event);
        return;
    }
    char extra[192];
    c39_health_text(owner, extra, sizeof(extra));
    c39_line("OverkillDamageEffectReceived (before)", owner, extra);
    c38_logic(kC38OverkillEffect, owner);
    c39_send_gib(owner);
    g_orig_overkill_effect(health, callback, damage_event);
}

// "class at (x, y, z) collision c profile p responses ddddddddd visible v hidden h" for one primitive component.
void c39_comp_text(const void* comp, char* out, size_t size) {
    if (comp == nullptr) {
        snprintf(out, size, "none");
        return;
    }
    uint64_t profile = 0;
    g_collision_profile(comp, &profile);
    char resp[kC39Channels + 1] = {};
    const uint8_t* const c = static_cast<const uint8_t*>(comp);
    for (uint32_t i = 0; i < kC39Channels; ++i) {
        resp[i] = static_cast<char>('0' + (c[kBodyResponses + i] & 0x7));
    }
    const Vec3 at = read_vec3(comp, kComponentTranslation);
    snprintf(out, size, "%s at (%.0f, %.0f, %.0f) collision %u profile %s responses %s visible %u hidden %u",
             class_name(comp).text, at.x, at.y, at.z, g_collision_enabled(comp), fname_text(&profile).text, resp,
             (c[kCompVisibleByte] & kCompVisibleBit) != 0 ? 1u : 0u,
             (c[kCompHiddenByte] & kCompHiddenBit) != 0 ? 1u : 0u);
}

void c39_probe_line(const void* actor, const char* when) {
    if (!note_event_cap(g_c39.probe_lines, kC39ProbeLines, "card039 collision probes")) {
        return;
    }
    char box[224], capsule[224], mesh[224], health[192];
    c39_comp_text(read_ptr(actor, kNpcCorpseBox), box, sizeof(box));
    c39_comp_text(read_ptr(actor, kActorRootComponent), capsule, sizeof(capsule));
    c39_comp_text(read_ptr(actor, kCharacterMesh), mesh, sizeof(mesh));
    c39_health_text(actor, health, sizeof(health));
    const uint8_t* const a = static_cast<const uint8_t*>(actor);
    log::line("card039: %s probe %s %s %s: IsNPC2D %d actor collision %u life span %.1f%s; box %s; capsule %s; "
              "mesh %s", side(), when, class_name(actor).text, object_name(actor).text, g_is_npc_2d(actor) ? 1 : 0,
              a[kActorCollisionFlags] & kActorEnableCollisionBit, g_life_span(actor), health, box, capsule, mesh);
}

// Card 132: the WarriorBug Blueprint's OnIsDying_Event (HandleDying broadcasts OnIsDying) starts its ragdoll at once
// on the client but never on the host (hb18). Its ubergraph calls IsFalling, so each side logs the NPC's movement
// mode and mesh state as HandleDying starts.
constexpr uint32_t kC132DyingLines = 48;
void c132_dying_line(const void* npc) {
    if (!note_event_cap(g_c38.dying_lines, kC132DyingLines, "card132 HandleDying lines")) {
        return;
    }
    const auto* const cmc = static_cast<const uint8_t*>(read_ptr(npc, kCharacterMovement));
    const auto* const mesh = static_cast<const uint8_t*>(read_ptr(npc, kCharacterMesh));
    const Vec3 at = actor_location(npc);
    log::line("card132: %s HandleDying %s at (%.0f, %.0f, %.0f): base state %u, movement mode %u, mesh hidden %d",
              g_client_session ? "client" : "host", object_name(npc).text, at.x, at.y, at.z,
              static_cast<const uint8_t*>(npc)[kNpcBaseState],
              cmc != nullptr ? cmc[kCmcMovementMode] : 0xFFu,
              mesh != nullptr ? ((mesh[kCompHiddenByte] & kCompHiddenBit) != 0 ? 1 : 0) : -1);
}

// Card 132, client: an NPC copy's CharacterMovement mode follows the host's. A simulated proxy gets the host's mode
// from ACharacter::ReplicatedMovementMode (PostNetReceive applies it only when that value changes), but its own
// SimulateMovement finds no floor under a NavWalking or Walking capsule placed at the host's location (NavWalking
// sets the capsule to ignore WorldStatic and WorldDynamic) and sets Falling, which then stays: in hb22 every client
// copy read Falling for seconds while its host NPC was Walking or NavWalking. At death the WarriorBug Blueprint's
// OnIsDying_Event starts the ragdoll at once when IsFalling is true, so those copies ragdolled before the host's.
// The local switch to Falling is replaced with the host's mode; a host NPC that really falls replicates Falling,
// which passes through.
SetMovementModeFn g_orig_set_movement_mode = nullptr;
constexpr size_t kCharReplicatedMoveMode = 0x452; // ACharacter::ReplicatedMovementMode (ACharacter::PostNetReceive)
constexpr size_t kVtUnpackMoveMode = 0x650;   // UCharacterMovementComponent::UnpackNetworkMovementMode
constexpr uint32_t kC132HeldLogs = 12;        // logged mode holds; the rest are counted
using UnpackMoveModeFn = void (*)(const void* cmc, uint8_t packed, uint8_t* mode, uint8_t* custom, uint8_t* ground);

// The host's movement mode for a client NPC copy, or kMoveFalling when the local switch stands (not an NPC simulated
// proxy, or the host's NPC falls too).
uint8_t c132_host_mode(const uint8_t* cmc) {
    const auto* const owner = static_cast<const uint8_t*>(read_ptr(cmc, kCmcCharacterOwner));
    if (owner == nullptr || owner[kActorLocalRole] != kRoleSimulatedProxy || g_npc_class == nullptr ||
        !is_a(owner, g_npc_class())) {
        return kMoveFalling;
    }
    uint8_t mode = kMoveFalling;
    uint8_t custom = 0;
    uint8_t ground = 0;
    const auto* const vt = static_cast<void* const*>(read_ptr(cmc, 0));
    reinterpret_cast<UnpackMoveModeFn>(vt[kVtUnpackMoveMode / sizeof(void*)])(cmc, owner[kCharReplicatedMoveMode],
                                                                              &mode, &custom, &ground);
    return mode == kMoveWalking || mode == kMoveNavWalking ? mode : kMoveFalling;
}

void detour_set_movement_mode(void* cmc, uint8_t mode, uint8_t custom_mode) {
    if (!g_client_session || mode != kMoveFalling) {
        g_orig_set_movement_mode(cmc, mode, custom_mode);
        return;
    }
    const uint8_t host = c132_host_mode(static_cast<const uint8_t*>(cmc));
    if (host != kMoveFalling && g_c38.held++ < kC132HeldLogs) {
        const void* const owner = read_ptr(cmc, kCmcCharacterOwner);
        log::line("card132: client %s local switch to Falling held at the host's mode %u (now %u)",
                  object_name(owner).text, host, static_cast<const uint8_t*>(cmc)[kCmcMovementMode]);
    }
    g_orig_set_movement_mode(cmc, host != kMoveFalling ? host : mode, custom_mode);
}

// Card 132, client: a held NPC copy stays at the host's height. The hold above keeps a copy in Walking or NavWalking
// where its own SimulateMovement would have set Falling, so nothing pulls it down: in hb25 the copy of 0x16c rose
// about 540 uu/s to 1316 uu above its host NPC, which stood still and sent no update, pushed up by a client-side
// ragdoll under it. After SimulateMovement, a copy in Walking or NavWalking whose host mode is Walking or NavWalking
// and whose z is more than kC132DriftZ from ReplicatedMovement.Location (the host's last replicated location) is set
// back to that z, whatever pushed it. kC132DriftZ: in hb25 a grounded copy's z stayed within 138 uu of its host NPC's
// at the same instant (1483 samples without 0x16c, 99th percentile 21 uu), and that gap includes the network delay
// the replicated location does not have.
SimulateMovementFn g_orig_simulate_movement = nullptr;
constexpr double kC132DriftZ = 200.0;     // uu
constexpr uint32_t kC132DriftLogs = 16;   // logged corrections; the rest are counted
constexpr size_t kRepMovementUnset = 0;   // C132Drift offsets not resolved yet

struct C132Drift {
    size_t rep_location;   // ACharacter offset of ReplicatedMovement.Location (FVector, doubles)
    size_t movement_base;  // ACharacter offset of BasedMovement.MovementBase (UPrimitiveComponent*)
    bool failed;           // reflection did not find them: the clamp stays off
};
C132Drift g_c132d = {};

// The struct property `name` of `ustruct`: its offset in `ustruct` and its UScriptStruct, or false.
bool c132_struct_field(const void* ustruct, const char* name, size_t* offset, const void** script_struct) {
    const uint8_t* const prop = prop_find_prop(ustruct, name);
    if (prop == nullptr) {
        return false;
    }
    *offset = static_cast<size_t>(read_i32(prop, kPropertyOffset));
    *script_struct = read_ptr(prop, kStructPropStruct);
    return *script_struct != nullptr;
}

bool c132_drift_offsets(const void* character) {
    if (g_c132d.rep_location != kRepMovementUnset || g_c132d.failed) {
        return !g_c132d.failed;
    }
    const void* const cls = read_ptr(character, kObjectClass);
    size_t rep = 0;
    size_t based = 0;
    const void* rep_struct = nullptr;
    const void* based_struct = nullptr;
    const uint8_t* loc = nullptr;
    const uint8_t* base = nullptr;
    if (c132_struct_field(cls, "ReplicatedMovement", &rep, &rep_struct) &&
        c132_struct_field(cls, "BasedMovement", &based, &based_struct)) {
        loc = prop_find_prop(rep_struct, "Location");
        base = prop_find_prop(based_struct, "MovementBase");
    }
    g_c132d.failed = loc == nullptr || base == nullptr;
    if (!g_c132d.failed) {
        g_c132d.rep_location = rep + static_cast<size_t>(read_i32(loc, kPropertyOffset));
        g_c132d.movement_base = based + static_cast<size_t>(read_i32(base, kPropertyOffset));
    }
    log::line("%scard132: %s drift clamp %s: ReplicatedMovement.Location at +%zx, BasedMovement.MovementBase at "
              "+%zx, z tolerance %.0f uu", g_c132d.failed ? "WARN " : "", side(), g_c132d.failed ? "off" : "on",
              g_c132d.rep_location, g_c132d.movement_base, kC132DriftZ);
    return !g_c132d.failed;
}

// Card 132: the character's ReplicatedMovement.Location (the host's last replicated location on a client copy).
bool c132_rep_location(const void* character, Vec3* out) {
    if (character == nullptr || !c132_drift_offsets(character)) {
        return false;
    }
    memcpy(out, static_cast<const uint8_t*>(character) + g_c132d.rep_location, sizeof(*out));
    return true;
}

// Card 328 step 1: when each held NPC copy's ReplicatedMovement.Location last changed, so a drift row can tell a stale
// location from a fresh one. Direct-mapped by the copy's object index: a copy whose slot another copy took since its
// last tick reads age "?" (as does a copy's first tick in the table). Game thread only.
constexpr uint32_t kC328AgeSlots = 1024;
constexpr double kC328FloorDown = 5000.0; // uu: the floor trace's reach below the actor's location
struct C328Age {
    const void* actor[kC328AgeSlots];
    Vec3 rep[kC328AgeSlots];
    ULONGLONG changed_ms[kC328AgeSlots];
};
C328Age g_c328_age = {};

// Notes the copy's replicated location; the ms since it last changed, or -1 when unknown.
int64_t c328_rep_age(const void* owner, const Vec3& rep, ULONGLONG now) {
    const uint32_t slot = static_cast<uint32_t>(object_index(owner)) % kC328AgeSlots;
    const bool same = g_c328_age.actor[slot] == owner;
    if (same && memcmp(&g_c328_age.rep[slot], &rep, sizeof(rep)) == 0) {
        return static_cast<int64_t>(now - g_c328_age.changed_ms[slot]);
    }
    g_c328_age.actor[slot] = owner;
    g_c328_age.rep[slot] = rep;
    g_c328_age.changed_ms[slot] = now;
    return same ? 0 : -1;
}

// Card 328 step 2: one visibility trace straight down from `from` to kC328FloorDown below `at`, ignoring `actor`.
struct C328Floor {
    double z;          // the blocking hit's z (valid when hit)
    const void* actor; // the blocking component's owner (null: none or freed)
    const void* comp;  // the blocking component
    bool hit;          // a blocking hit within the reach
    bool inside;       // the trace started inside the blocking body (FHitResult bStartPenetrating)
};

C328Floor c328_floor(const void* actor, const Vec3& from, const Vec3& at) {
    C328Floor f{};
    if (g_line_trace_by_channel == nullptr || g_query_params_ctor == nullptr || g_actor_get_world(actor) == nullptr) {
        return f;
    }
    const Vec3 end{at.x, at.y, at.z - kC328FloorDown};
    const Vec3 hit = trace_channel(actor, from, end, kEccVisibility, &f.actor, &f.comp, &f.inside);
    f.z = hit.z;
    f.hit = hit.z != end.z;
    return f;
}

// Card 328: the z of the first visibility hit straight down from `at`, ignoring `actor`; false with no hit within
// kC328FloorDown or no trace.
bool c328_floor_z(const void* actor, const Vec3& at, double* z) {
    const C328Floor f = c328_floor(actor, at, at);
    *z = f.z;
    return f.hit;
}

// Card 328 step 2: "<z> on <comp> of <actor>[, start inside]", or "none" with no blocking hit.
void c328_floor_text(const C328Floor& f, char* out, size_t cap) {
    if (!f.hit) {
        snprintf(out, cap, "none");
        return;
    }
    snprintf(out, cap, "%.0f on %s of %s%s", f.z, f.comp != nullptr ? object_name(f.comp).text : "-",
             f.actor != nullptr ? object_name(f.actor).text : "-", f.inside ? ", start inside" : "");
}

// Card 328 step 2: the root capsule's CapsuleHalfHeight (unscaled, reflected), -1 when unresolved.
double c328_half_height(const void* owner) {
    const void* const root = read_ptr(owner, kActorRootComponent);
    const void* const cls = root != nullptr ? read_ptr(root, kObjectClass) : nullptr;
    const uint8_t* const h = cls != nullptr ? prop_find_prop(cls, "CapsuleHalfHeight") : nullptr;
    return h != nullptr ? read_f32(root, static_cast<size_t>(read_i32(h, kPropertyOffset))) : -1.0;
}

// Card 328 step 2: UCharacterMovementComponent CurrentFloor.HitResult (FFindFloorResult) offset, reflected once on the
// first drift row; 0 = not found.
size_t g_c328_floor_hit = 0;
bool g_c328_floor_read = false;
size_t g_c328_floor = 0;                    // card 328 step 3: CurrentFloor itself
const void* g_c328_floor_struct = nullptr; // card 328 step 3: FFindFloorResult; null = not found

// The component of the CMC's CurrentFloor hit (null: none, freed or the offset unresolved).
const void* c328_current_floor_comp(const void* cmc) {
    if (!g_c328_floor_read) {
        g_c328_floor_read = true;
        size_t floor = 0;
        const void* floor_struct = nullptr;
        const uint8_t* const hit =
            c132_struct_field(read_ptr(cmc, kObjectClass), "CurrentFloor", &floor, &floor_struct)
                ? prop_find_prop(floor_struct, "HitResult") : nullptr;
        g_c328_floor_hit = hit != nullptr ? floor + static_cast<size_t>(read_i32(hit, kPropertyOffset)) : 0;
        g_c328_floor = floor;
        g_c328_floor_struct = hit != nullptr ? floor_struct : nullptr;
        log::line("%scard328: %s CharacterMovement CurrentFloor.HitResult at +%zx", hit != nullptr ? "" : "WARN ",
                  side(), g_c328_floor_hit);
    }
    if (g_c328_floor_hit == 0) {
        return nullptr;
    }
    C31Weak w{};
    memcpy(&w, static_cast<const uint8_t*>(cmc) + g_c328_floor_hit + hit_layout().comp, sizeof(w));
    return c31_weak_get(w);
}

// Card 328, host: a client's DigestQuery "Z<guid>" (the client clamped that NPC copy): the NPC's z, movement mode,
// movement base and floor z, logged for the client row with the same guid.
void c328_host_row(void* pc, uint64_t guid) {
    const void* const npc = c41_object_from_guid(pc, guid);
    if (npc == nullptr || g_npc_class == nullptr || !is_a(npc, g_npc_class())) {
        log::line("WARN card328: host guid %llx asked by the client is not a live NPC",
                  static_cast<unsigned long long>(guid));
        return;
    }
    const auto* const cmc = static_cast<const uint8_t*>(read_ptr(npc, kCharacterMovement));
    const Vec3 at = actor_location(npc);
    const void* const base = c132_drift_offsets(npc) ? read_ptr(npc, g_c132d.movement_base) : nullptr;
    double floor = 0.0;
    const bool hit = c328_floor_z(npc, at, &floor);
    char floor_text[24];
    snprintf(floor_text, sizeof(floor_text), hit ? "%.0f" : "none", floor);
    log::line("card328: host t=%llu guid %llx %s at (%.0f, %.0f, %.0f) mode %d, movement base %s of %s, floor z %s",
              static_cast<unsigned long long>(GetTickCount64()), static_cast<unsigned long long>(guid),
              object_name(npc).text, at.x, at.y, at.z, cmc != nullptr ? cmc[kCmcMovementMode] : -1,
              base != nullptr ? object_name(base).text : "none",
              base != nullptr ? object_name(read_ptr(base, kObjectOuter)).text : "-", floor_text);
}

// Card 328, client: one drift row (card 132's, extended), then the host's row for the same guid asked for.
void c328_drift_row(const void* owner, const Vec3& at, const Vec3& rep, uint8_t mode, const Vec3& vel, int64_t age,
                    const char* outcome) {
    const void* const base = read_ptr(owner, g_c132d.movement_base);
    const uint64_t guid = c41_guid(owner);
    // Card 328 step 2: the floor trace's body, the capsule, the CMC's own floor, and the trace again from 50 uu above
    // the copy's top (outside any body the copy's centre sits in).
    char floor_text[160];
    c328_floor_text(c328_floor(owner, at, at), floor_text, sizeof(floor_text));
    const double half = c328_half_height(owner);
    const Vec3 above{at.x, at.y, at.z + (half > 0.0 ? half : 0.0) + 50.0};
    char above_text[160];
    c328_floor_text(c328_floor(owner, above, at), above_text, sizeof(above_text));
    const void* const cmc = read_ptr(owner, kCharacterMovement);
    const void* const cur = cmc != nullptr ? c328_current_floor_comp(cmc) : nullptr;
    const void* const cur_actor = cur != nullptr ? read_ptr(cur, kComponentOwner) : nullptr;
    char age_text[24];
    snprintf(age_text, sizeof(age_text), age >= 0 ? "%lld ms" : "?", static_cast<long long>(age));
    log::line("card132: client t=%llu %s guid %llx drifted %.0f uu in z from the host's last location (%.0f, %.0f, "
              "%.0f), xy %.0f uu, in mode %u (replicated mode byte %02x, location age %s), velocity (%.0f, %.0f, "
              "%.0f), movement base %s of %s, floor %s; capsule half height %.0f, CurrentFloor %s of %s; from 50 uu "
              "above the top: floor %s: %s z %.0f",
              static_cast<unsigned long long>(GetTickCount64()), object_name(owner).text,
              static_cast<unsigned long long>(guid), at.z - rep.z, rep.x, rep.y, rep.z,
              hypot(at.x - rep.x, at.y - rep.y), mode, static_cast<const uint8_t*>(owner)[kCharReplicatedMoveMode],
              age_text, vel.x, vel.y, vel.z,
              base != nullptr ? object_name(base).text : "none",
              base != nullptr ? object_name(read_ptr(base, kObjectOuter)).text : "-", floor_text, half,
              cur != nullptr ? object_name(cur).text : "none", cur_actor != nullptr ? object_name(cur_actor).text : "-",
              above_text, outcome, rep.z);
    void* const world = g_actor_get_world(owner);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    char text[32];
    snprintf(text, sizeof(text), "Z%llx", static_cast<unsigned long long>(guid));
    static wchar_t wire[kChanTextFrame];
    const WireString w = chan_text_frame(ChanOp::DigestQuery, text, wire);
    if (guid == 0 || pc == nullptr || w.num <= 0 || !chan_send_host(pc, w)) {
        log::line("WARN card328: client guid %llx host row not asked (no guid, PC or send)",
                  static_cast<unsigned long long>(guid));
    }
}

// Card 328 step 3 (diagnostic): after a set-back, kC328TraceFrames rows of that copy, one per SimulateMovement (after
// the movement step): its z, the CMC's CurrentFloor flags and distances, the mesh's active montage and whether that
// montage moves the root, and the movement mode. One copy at a time, kC328TraceMax traces per map.
constexpr uint32_t kC328TraceFrames = 30;
constexpr uint32_t kC328TraceMax = 4;

// A reflected plain bool field of `ustruct` read at `base`: 1, 0, or -1 when absent or not a plain bool.
int c328_bool(const void* base, const void* ustruct, const char* name) {
    const uint8_t* const prop = ustruct != nullptr ? prop_find_prop(ustruct, name) : nullptr;
    if (prop == nullptr || !prop_bool_ok(prop)) {
        return -1;
    }
    const uint8_t* const at =
        static_cast<const uint8_t*>(base) + read_i32(prop, kPropertyOffset) + prop[kBoolByteOffset];
    return (*at & prop[kBoolFieldMask]) != 0 ? 1 : 0;
}

// A reflected float field of `ustruct` read at `base`; -99999 when absent.
double c328_float(const void* base, const void* ustruct, const char* name) {
    const uint8_t* const prop = ustruct != nullptr ? prop_find_prop(ustruct, name) : nullptr;
    return prop != nullptr ? read_f32(base, static_cast<size_t>(read_i32(prop, kPropertyOffset))) : -99999.0;
}

void c328_trace_row(const void* owner, const uint8_t* cmc) {
    const uint32_t frame = kC328TraceFrames - g_c38.trace_left + 1;
    --g_c38.trace_left;
    const void* const cur = c328_current_floor_comp(cmc); // also reflects CurrentFloor on first use
    const uint8_t* const floor = cmc + g_c328_floor;
    const void* const fs = g_c328_floor_struct;
    const void* const mesh = read_ptr(owner, kCharacterMesh);
    const void* const anim = mesh != nullptr && g_c169_anim_instance != nullptr ? g_c169_anim_instance(mesh) : nullptr;
    const void* const montage =
        anim != nullptr && g_c169_active_montage != nullptr ? g_c169_active_montage(anim) : nullptr;
    const int root =
        montage != nullptr ? c328_bool(montage, read_ptr(montage, kObjectClass), "bEnableRootMotionTranslation") : -1;
    log::line("card328: client t=%llu trace frame %u/%u guid %llx %s z %.1f mode %u, CurrentFloor %s blocking %d "
              "walkable %d line trace %d FloorDist %.1f LineDist %.1f, montage %s (root motion translation %d)",
              static_cast<unsigned long long>(GetTickCount64()), frame, kC328TraceFrames,
              static_cast<unsigned long long>(c41_guid(owner)), object_name(owner).text, actor_location(owner).z,
              cmc[kCmcMovementMode], cur != nullptr ? object_name(cur).text : "none",
              fs != nullptr ? c328_bool(floor, fs, "bBlockingHit") : -1,
              fs != nullptr ? c328_bool(floor, fs, "bWalkableFloor") : -1,
              fs != nullptr ? c328_bool(floor, fs, "bLineTrace") : -1, c328_float(floor, fs, "FloorDist"),
              c328_float(floor, fs, "LineDist"), montage != nullptr ? object_name(montage).text : "none", root);
}

void c132_drift_clamp(void* cmc) {
    auto* const c = static_cast<uint8_t*>(cmc);
    if (g_c38.trace_left != 0 && read_ptr(cmc, kCmcCharacterOwner) == g_c38.trace_owner) {
        c328_trace_row(g_c38.trace_owner, c); // card 328 step 3: every mode, before the clamp's own tests
    }
    const uint8_t mode = c[kCmcMovementMode];
    if ((mode != kMoveWalking && mode != kMoveNavWalking) || c132_host_mode(c) == kMoveFalling) {
        return; // also not an NPC simulated proxy
    }
    void* const owner = read_ptr(cmc, kCmcCharacterOwner);
    if (!c132_drift_offsets(owner)) {
        return;
    }
    Vec3 rep = {};
    memcpy(&rep, static_cast<const uint8_t*>(owner) + g_c132d.rep_location, sizeof(rep));
    const int64_t age = c328_rep_age(owner, rep, GetTickCount64());
    Vec3 at = actor_location(owner);
    const double dz = at.z - rep.z;
    if (dz <= kC132DriftZ && dz >= -kC132DriftZ) {
        return;
    }
    Vec3 vel = {};
    memcpy(&vel, c + kMoveVelocity, sizeof(vel));
    // Card 328 fix (b196): the engine's own encroachment test of the copy at the set-back place
    // (EncroachingBlockingGeometry with an adjustment). A place inside another body is not written: in c245b194d the
    // copy set back there was pushed out about 160 uu up within 2 frames (adjustment z +199.8), which read as a climb.
    // The copy stays where it is until the host's next location; a free place keeps the set-back.
    void* const world = g_actor_get_world(owner);
    const Vec3 rot{};
    Vec3 probe{at.x, at.y, rep.z};
    Vec3 adjust{};
    const int encroach = world != nullptr && g_c46_encroaching != nullptr
                             ? (g_c46_encroaching(world, owner, &probe, &rot, &adjust) ? 1 : 0)
                             : -1;
    char outcome[96];
    snprintf(outcome, sizeof(outcome), "%s (encroaching %d, adjustment (%.1f, %.1f, %.1f)) at",
             encroach == 1 ? "skipped (encroaching)" : "set back", encroach, adjust.x, adjust.y, adjust.z);
    if (g_c38.drift + g_c38.drift_skipped < kC132DriftLogs) {
        c328_drift_row(owner, at, rep, mode, vel, age, outcome);
    }
    if (encroach == 1) {
        ++g_c38.drift_skipped;
        return;
    }
    ++g_c38.drift;
    at.z = rep.z;
    vel.z = 0.0;
    memcpy(c + kMoveVelocity, &vel, sizeof(vel));
    g_set_actor_location(owner, &at, false, nullptr, kTeleportPhysics);
    if (g_c38.trace_left == 0 && g_c38.traces < kC328TraceMax) {
        ++g_c38.traces;
        g_c38.trace_owner = owner;
        g_c38.trace_left = kC328TraceFrames;
        log::line("card328: client t=%llu trace %u of %u armed on guid %llx %s after the set-back to z %.0f",
                  static_cast<unsigned long long>(GetTickCount64()), g_c38.traces, kC328TraceMax,
                  static_cast<unsigned long long>(c41_guid(owner)), object_name(owner).text, at.z);
    }
}

void detour_simulate_movement(void* cmc, float delta) {
    g_orig_simulate_movement(cmc, delta);
    if (g_client_session) {
        c132_drift_clamp(cmc);
    }
}

void detour_handle_dying(void* npc) {
    c39_line("HandleDying", npc, "");
    c38_logic(kC38HandleDying, npc);
    c132_dying_line(npc);
    g_orig_handle_dying(npc);
    if (g_c37.audit) {
        c39_probe_line(npc, "after HandleDying");
    }
}

void detour_update_colliders(void* npc) {
    g_orig_update_colliders(npc);
    char extra[256];
    snprintf(extra, sizeof(extra), " (after; box collision %u)",
             read_ptr(npc, kNpcCorpseBox) != nullptr ? g_collision_enabled(read_ptr(npc, kNpcCorpseBox)) : 255u);
    c39_line("UpdateColliders", npc, extra);
}

// Both sides with the audit: probe this NPC kC39ProbeMs after now.
void c39_track(const void* actor) {
    if (!g_c37.audit) {
        return;
    }
    uint32_t slot = g_c39.probe_count;
    for (uint32_t i = 0; i < g_c39.probe_count; ++i) {
        if (g_c39.probe[i].done >= kC39Probes) {
            slot = i; // finished or gone: reuse
            break;
        }
    }
    if (slot >= kC39ProbeMax) {
        if (!g_c39.full_logged) {
            g_c39.full_logged = true;
            log::line("WARN card039: %s probe table full (%u) -- further deaths not probed", side(), kC39ProbeMax);
        }
        return;
    }
    g_c39.probe[slot] = C39Probe{actor, read_u64(actor, kObjectName), GetTickCount64(), 0};
    g_c39.probe_count = slot == g_c39.probe_count ? slot + 1 : g_c39.probe_count;
}

// The live NPC matching probe p, or null (then the actor is gone).
const void* c39_find(const C23List& list, const C39Probe& p) {
    for (uint32_t i = 0; i < list.count; ++i) {
        if (list.e[i].actor == p.actor && read_u64(list.e[i].actor, kObjectName) == p.name) {
            return list.e[i].actor;
        }
    }
    return nullptr;
}

bool c39_due(ULONGLONG now) {
    for (uint32_t i = 0; i < g_c39.probe_count; ++i) {
        const C39Probe& p = g_c39.probe[i];
        if (p.done < kC39Probes && now >= p.death_ms + kC39ProbeMs[p.done]) {
            return true;
        }
    }
    return false;
}

// Client with the audit: one shot kC39GibShotMs after the Gib signal, facing the gibbed bug's last location.
void c39_gib_shot(void* engine, ULONGLONG now) {
    if (g_c39.gib_shot_ms == 0 || now < g_c39.gib_shot_ms) {
        return;
    }
    g_c39.gib_shot_ms = 0;
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    const void* const pawn = pc != nullptr ? c23_own_pawn(pc) : nullptr;
    if (pawn == nullptr) {
        log::line("WARN card039: client gib shot skipped: no pawn on foot");
        return;
    }
    c23_face(pc, actor_location(pawn), g_c39.gib_loc);
    request_shot(pc, false);
    log::line("card039: client gib shot requested (+%llu ms after the Gib signal)",
              static_cast<unsigned long long>(kC39GibShotMs));
}

// Both sides, every frame: the due collision probes, each actor re-found in a live scan first.
void c39_step(void* engine, ULONGLONG now) {
    c39_gib_shot(engine, now);
    if (!g_c37.audit || g_npc_class == nullptr || !c39_due(now)) {
        return;
    }
    void* const world = g_get_game_world(engine);
    if (world == nullptr) {
        return;
    }
    static C23List list; // game thread only; too large for the stack
    c23_collect(world, nullptr, g_npc_class(), list);
    for (uint32_t i = 0; i < g_c39.probe_count; ++i) {
        C39Probe& p = g_c39.probe[i];
        if (p.done >= kC39Probes || now < p.death_ms + kC39ProbeMs[p.done]) {
            continue;
        }
        const void* const actor = c39_find(list, p);
        char when[48];
        snprintf(when, sizeof(when), "+%llu ms", static_cast<unsigned long long>(now - p.death_ms));
        if (actor == nullptr) {
            log::line("card039: %s probe %s: NPC gone (destroyed)", side(), when);
            p.done = kC39Probes;
            continue;
        }
        c39_probe_line(actor, when);
        ++p.done;
    }
}

// The NPC's gore component, or null (logged).
// +0xA90 is the NPC class's field. Any other owner (a placed BP_WarriorBug_Corpses_Destructible_01_C pile, whose own
// USTGoreComponent is the host's burst context: hb47, 4 WARNs from reading +0xA90 on it) is searched by class.
void* c39_gore(const void* actor) {
    const bool npc = g_npc_class != nullptr && is_a(actor, g_npc_class());
    void* const gore = npc ? read_ptr(actor, kNpcGore)
                           : (g_gore_class != nullptr ? find_component(actor, g_gore_class) : nullptr);
    if (gore == nullptr || g_gore_class == nullptr || !is_a(gore, g_gore_class())) {
        log::line("WARN card039: client %s has no gore component (%s) -- no gore", object_name(actor).text,
                  npc ? "NPC +0xA90" : "by class");
        return nullptr;
    }
    return gore;
}

// USkinnedMeshComponent::ComponentSpaceTransformsArray[2] (TArray<FTransform>, 0x10 each) and
// CurrentReadComponentTransforms, the index of the array that holds the current pose (USkinnedMeshComponent::
// GetBoneTransform reads both); USkeletalMeshComponent::Bodies (TArray<FBodyInstance*>, UpdateKinematicBonesToAnim
// reads it).
constexpr size_t kSkinnedSpaceTransforms = 0x5F0;
constexpr size_t kSkinnedSpaceRead = 0x638;
constexpr size_t kSkeletalBodies = 0xB80;
constexpr int32_t kSkeletalBodiesMax = 256;       // a bug's physics asset has a few dozen bodies
constexpr int32_t kDisallowKinematicDeferral = 1; // EAllowKinematicDeferral::DisallowDeferral

// True when any of the mesh's physics bodies simulates (a ragdoll). Null or out-of-range body arrays count as false.
bool c39_mesh_simulating(const void* mesh) {
    const void* const* const bodies = static_cast<const void* const*>(read_ptr(mesh, kSkeletalBodies));
    const int32_t count = read_i32(mesh, kSkeletalBodies + 0x8);
    if (bodies == nullptr || count <= 0 || count > kSkeletalBodiesMax) {
        return false;
    }
    for (int32_t i = 0; i < count; ++i) {
        if (bodies[i] != nullptr && g_body_simulating(bodies[i])) {
            return true;
        }
    }
    return false;
}

// Move a mesh's kinematic physics bodies to its current pose. A mesh that was never rendered keeps its bodies where
// they were last updated (often the spawn point) while the component follows the actor. Bug meshes set
// KinematicBonesUpdateType (+0xA6C) to SkipAllBones, and UpdateKinematicBonesToAnim then returns at once unless a body
// simulates (exe 0x144019C66 to 0x144019C84), so the type is set to SkipSimulatingBones for this one call and put
// back. No body simulates here (that is checked first), so SkipSimulatingBones updates every body.
// UpdateKinematicBonesToAnim with TeleportPhysics also teleports simulating bodies, so a ragdoll is left alone. It
// returns early itself without a physics state, scene, physics asset, skeletal mesh or bodies. Returns what it did,
// for the log lines.
constexpr size_t kKinematicBonesUpdateType = 0xA6C; // TEnumAsByte<EKinematicBonesUpdateToPhysics::Type>
constexpr uint8_t kSkipSimulatingBones = 0;
constexpr uint8_t kSkipAllBones = 1;

const char* c39_sync_mesh(void* mesh) {
    if (mesh == nullptr || g_kinematic_bones == nullptr || g_body_simulating == nullptr) {
        return "no mesh";
    }
    if (c39_mesh_simulating(mesh)) {
        return "ragdoll left";
    }
    const int32_t read = read_i32(mesh, kSkinnedSpaceRead);
    if (read != 0 && read != 1) {
        log::line("WARN card039: mesh %s pose index %d is not 0 or 1; bodies left as they are",
                  object_name(mesh).text, read);
        return "bad pose index";
    }
    const uint8_t* const pose = static_cast<const uint8_t*>(mesh) + kSkinnedSpaceTransforms + 0x10 * read;
    if (read_i32(pose, 0x8) <= 0) {
        return "no pose";
    }
    uint8_t* const update_type = static_cast<uint8_t*>(mesh) + kKinematicBonesUpdateType;
    const uint8_t was = *update_type;
    if (was == kSkipAllBones) {
        *update_type = kSkipSimulatingBones;
    }
    g_kinematic_bones(mesh, pose, kTeleportPhysics, false, kDisallowKinematicDeferral);
    *update_type = was;
    return was == kSkipAllBones ? "bodies synced (SkipAllBones lifted)" : "bodies synced";
}

// Client: the NPC mesh's bodies to its current pose before the gore reads them. GetGoreLocation is
// Mesh->GetSkeletalCenterOfMass, the average of the body positions, so stale bodies put the gore at the spawn point.
const char* c39_sync_bodies(void* actor) {
    return c39_sync_mesh(read_ptr(actor, kCharacterMesh));
}

// Both roles: a ragdoll starts from wherever its mesh's bodies are. A mesh its own game never rendered has bodies at
// a stale pose (see c39_sync_mesh), so the ragdoll started far from the capsule and physics flung or dropped it.
// USTBugAnimPhysComponent starts its ragdoll with Mesh->SetSimulatePhysics(true) (vtable +0x6D8), inlined into
// TickComponent at exe 0x1451E2C77 (state 0x12) and in StartRagdoll (exe 0x1451DF280), so the hook is on
// USkeletalMeshComponent::SetSimulatePhysics itself. Before simulation turns on, the bodies are moved to the current
// pose. c39_sync_mesh leaves a mesh that already simulates alone, so a repeated call changes nothing. The line gives
// the bodies' centre of mass to mesh distance before and after.
constexpr uint32_t kC132RagdollLines = 512; // card 485: 64 ended at 614273220 in c244b206b, before 0x450's death
constexpr double kC132StaleBodies = 500.0; // uu from centre of mass to mesh origin; a bug is under 300 uu tall

// The Blueprint frames running a hooked exec thunk (execSetSimulatePhysics, execSetHiddenInGame), during that call
// only: the function (FFrame::Node, read before the thunk steps the frame), the frame that called it (for an
// ubergraph, the event stub), the one above that and its caller (the caller's own event stub), so the log lines can
// name them.
struct C132Bp {
    const void* node = nullptr;
    const void* event = nullptr;
    const void* caller = nullptr;
    const void* caller_event = nullptr;
};
C132Bp g_c132_bp;

const void* c132_frame_up(const void* frame) {
    return frame != nullptr ? read_ptr(frame, kFramePrevious) : nullptr;
}

const void* c132_frame_node(const void* frame) {
    return frame != nullptr ? read_ptr(frame, kFrameNode) : nullptr;
}

void c132_exec(ExecThunkFn original, void* context, void* frame, void* result) {
    const C132Bp outer = g_c132_bp;
    const void* const up = c132_frame_up(frame);
    const void* const up2 = c132_frame_up(up);
    g_c132_bp = C132Bp{c132_frame_node(frame), c132_frame_node(up), c132_frame_node(up2),
                       c132_frame_node(c132_frame_up(up2))};
    original(context, frame, result);
    g_c132_bp = outer;
}

// "Outer.Function from Event from Caller from CallerEvent" for g_c132_bp, or "native" outside a hooked thunk.
void c132_bp_text(char* out, size_t size) {
    if (g_c132_bp.node == nullptr) {
        snprintf(out, size, "native");
        return;
    }
    snprintf(out, size, "%s.%s from %s from %s from %s", object_name(read_ptr(g_c132_bp.node, kObjectOuter)).text,
             object_name(g_c132_bp.node).text,
             g_c132_bp.event != nullptr ? object_name(g_c132_bp.event).text : "no script caller",
             g_c132_bp.caller != nullptr ? object_name(g_c132_bp.caller).text : "no script caller",
             g_c132_bp.caller_event != nullptr ? object_name(g_c132_bp.caller_event).text : "no script caller");
}

void detour_c132_exec_set_simulate(void* context, void* frame, void* result) {
    c132_exec(g_orig_c132_exec_set_simulate, context, frame, result);
}

void detour_c132_exec_set_hidden(void* context, void* frame, void* result) {
    c132_exec(g_orig_c132_exec_set_hidden, context, frame, result);
}

// Both sides: a change of an NPC mesh's hidden-in-game flag, with its caller. A client corpse whose mesh was hidden
// when its ragdoll started sinks through the floor (hb14), so this names what hides it.
constexpr uint32_t kC132HiddenLines = 64;
void detour_c132_set_hidden(void* comp, bool hidden, uint8_t propagation) {
    const void* const owner = comp != nullptr ? read_ptr(comp, kComponentOwner) : nullptr;
    const bool npc_mesh = owner != nullptr && read_ptr(owner, kCharacterMesh) == comp;
    const auto* const c = static_cast<const uint8_t*>(comp);
    const bool was = npc_mesh && (c[kCompHiddenByte] & kCompHiddenBit) != 0;
    g_orig_c132_set_hidden(comp, hidden, propagation);
    if (npc_mesh && was && !hidden) {
        c207_shown_corpse(const_cast<void*>(owner), comp); // card 207
    }
    if (!npc_mesh || was == hidden ||
        !note_event_cap(g_c39.hidden_lines, kC132HiddenLines, "card132 mesh hidden lines")) {
        return;
    }
    char bp[320];
    c132_bp_text(bp, sizeof(bp));
    const Vec3 at = actor_location(owner);
    log::line("card132: %s mesh hidden %d for %s at (%.0f, %.0f, %.0f), simulating %d; caller exe+0x%llX, "
              "blueprint %s", g_client_session ? "client" : "host", hidden ? 1 : 0, object_name(owner).text, at.x,
              at.y, at.z, c39_mesh_simulating(comp) ? 1 : 0, exe_rva(_ReturnAddress()), bp);
}

// Height of the first BlockAll hit straight down from the actor's root, within kC132FloorReach; -1 when none or
// the trace is unresolved (UBW floors sit far above z 0).
constexpr double kC132FloorReach = 3000.0;
double c132_floor_below(const void* owner) {
    if (owner == nullptr || g_line_trace_by_profile == nullptr || g_query_params_ctor == nullptr ||
        g_actor_get_world(owner) == nullptr) {
        return -1.0;
    }
    const Vec3 root = actor_location(owner);
    const Vec3 end{root.x, root.y, root.z - kC132FloorReach};
    const Vec3 hit = trace_profile(owner, root, end, fname_cached(g_c57_block_all, L"BlockAll"));
    return hit.z > end.z + 1.0 ? hit.z : -1.0;
}

// Card 132: who turns an NPC mesh's simulation off. A corpse that stops simulating stops showing its ragdoll pose,
// so the pose journal needs to know which code does it (capped lines, caller and Blueprint frame).
constexpr uint32_t kC132SimOffLines = 64;
void c132_note_simulate_off(void* mesh, void* caller) {
    const void* const owner = mesh != nullptr ? read_ptr(mesh, kComponentOwner) : nullptr;
    if (owner == nullptr || read_ptr(owner, kCharacterMesh) != mesh ||
        !note_event_cap(g_c39.sim_off_lines, kC132SimOffLines, "card132 simulate off lines")) {
        return;
    }
    char bp[320];
    c132_bp_text(bp, sizeof(bp));
    const Vec3 at = actor_location(owner);
    log::line("card132: %s simulate off for %s at (%.0f, %.0f, %.0f), simulating %d; caller exe+0x%llX, blueprint %s",
              g_client_session ? "client" : "host", object_name(owner).text, at.x, at.y, at.z,
              c39_mesh_simulating(mesh) ? 1 : 0, exe_rva(caller), bp);
}

// hb51 senior point 3: the body calls other than SetSimulatePhysics, on an NPC's character mesh only. What a call
// found and left: body 0 simulating, body count and array (TermArticulated/InitArticulated replace it).
uint64_t c41_guid(const void* actor); // below (card 041)
MeshBoolFn g_orig_c132_all_bodies = nullptr;
MeshBelowFn g_orig_c132_bodies_below = nullptr;
MeshVoidFn g_orig_c132_reset_bodies = nullptr;
MeshVoidFn g_orig_c132_term_articulated = nullptr;
MeshSceneFn g_orig_c132_init_articulated = nullptr;
constexpr uint32_t kC132BodyCallLines = 96;
uint32_t g_c132_body_call_lines = 0; // game thread only

struct C132BodyState {
    const void* bodies;
    int32_t count;
    int simulating; // body 0: 1, 0; -1 when there is no body 0
};

C132BodyState c132_body_state(const void* mesh) {
    const auto* const bodies = static_cast<const void* const*>(read_ptr(mesh, kSkeletalBodies));
    const int32_t count = read_i32(mesh, kSkeletalBodies + 0x8);
    const bool any = bodies != nullptr && count > 0 && count <= kSkeletalBodiesMax && bodies[0] != nullptr;
    return C132BodyState{bodies, count, any ? (g_body_simulating(bodies[0]) ? 1 : 0) : -1};
}

// The NPC whose character mesh this is, or null (a player, a prop, a weapon mesh).
const void* c132_body_owner(const void* mesh) {
    const void* const owner = mesh != nullptr ? read_ptr(mesh, kComponentOwner) : nullptr;
    return owner != nullptr && read_ptr(owner, kCharacterMesh) == mesh && c41_guid(owner) != 0 ? owner : nullptr;
}

void c132_note_body_call(const char* fn, const char* arg, const void* owner, const C132BodyState& before,
                         const C132BodyState& after, void* caller) {
    if (!note_event_cap(g_c132_body_call_lines, kC132BodyCallLines, "card132 body call lines")) {
        return;
    }
    float health = -1.0f;
    const int dead = c25_dead(owner, &health);
    char bp[320];
    c132_bp_text(bp, sizeof(bp));
    const Vec3 at = actor_location(owner);
    log::line("card132: %s %s(%s) on %s guid 0x%llx at (%.0f, %.0f, %.0f) t=%llu, IsDead %d health %.1f: body 0 "
              "simulating %d -> %d, bodies %d at %p -> %d at %p; caller exe+0x%llX, blueprint %s",
              side(), fn, arg, object_name(owner).text, static_cast<unsigned long long>(c41_guid(owner)), at.x, at.y,
              at.z, static_cast<unsigned long long>(GetTickCount64()), dead, health, before.simulating,
              after.simulating, before.count, before.bodies, after.count, after.bodies, exe_rva(caller), bp);
}

void detour_c132_all_bodies(void* mesh, bool simulate) {
    const void* const owner = g_body_simulating != nullptr ? c132_body_owner(mesh) : nullptr;
    const C132BodyState before = owner != nullptr ? c132_body_state(mesh) : C132BodyState{};
    // Card 152 r3: the game's own freeze (caller in the exe; c152-r2: exe+0x3B05087, blueprint native) of a client
    // copy whose stream is live is blocked before the call; the host's freeze reaches the copy as its frozen final.
    // Card 207 step 3: its own start (1) on a copy the host's rest row owns is blocked the same way.
    const auto rva = static_cast<uintptr_t>(exe_rva(_ReturnAddress()));
    if (g_client_session && rva != 0 &&
        (simulate ? c207_pose_game_unfreeze(mesh, rva) || c152_pose_game_start(mesh, rva)
                  : c152_pose_game_freeze(mesh, rva))) {
        return;
    }
    g_orig_c132_all_bodies(mesh, simulate);
    if (owner != nullptr) {
        c132_pose_sim_change(mesh, simulate, before.simulating); // card 137 Pose:1; card 339 the before state
        c132_note_body_call("SetAllBodiesSimulatePhysics", simulate ? "1" : "0", owner, before, c132_body_state(mesh),
                            _ReturnAddress());
    }
}

void detour_c132_bodies_below(void* mesh, const void* bone, bool simulate, bool self) {
    const void* const owner = g_body_simulating != nullptr ? c132_body_owner(mesh) : nullptr;
    const C132BodyState before = owner != nullptr ? c132_body_state(mesh) : C132BodyState{};
    g_orig_c132_bodies_below(mesh, bone, simulate, self);
    if (owner != nullptr) {
        char arg[96];
        snprintf(arg, sizeof(arg), "%s, %d, %d", bone != nullptr ? fname_text(bone).text : "?", simulate ? 1 : 0,
                 self ? 1 : 0);
        c132_note_body_call("SetAllBodiesBelowSimulatePhysics", arg, owner, before, c132_body_state(mesh),
                            _ReturnAddress());
    }
}

void detour_c132_reset_bodies(void* mesh) {
    const void* const owner = g_body_simulating != nullptr ? c132_body_owner(mesh) : nullptr;
    const C132BodyState before = owner != nullptr ? c132_body_state(mesh) : C132BodyState{};
    g_orig_c132_reset_bodies(mesh);
    if (owner != nullptr) {
        c132_note_body_call("ResetAllBodiesSimulatePhysics", "", owner, before, c132_body_state(mesh), _ReturnAddress());
    }
}

void detour_c132_term_articulated(void* mesh) {
    const void* const owner = g_body_simulating != nullptr ? c132_body_owner(mesh) : nullptr;
    const C132BodyState before = owner != nullptr ? c132_body_state(mesh) : C132BodyState{};
    g_orig_c132_term_articulated(mesh);
    if (owner != nullptr) {
        c132_note_body_call("TermArticulated", "", owner, before, c132_body_state(mesh), _ReturnAddress());
    }
}

void detour_c132_init_articulated(void* mesh, void* scene) {
    const void* const owner = g_body_simulating != nullptr ? c132_body_owner(mesh) : nullptr;
    const C132BodyState before = owner != nullptr ? c132_body_state(mesh) : C132BodyState{};
    g_orig_c132_init_articulated(mesh, scene);
    if (owner != nullptr) {
        c132_note_body_call("InitArticulated", "", owner, before, c132_body_state(mesh), _ReturnAddress());
    }
}

// Card 132 note 3: USTSkeletalAnimatorComponent swaps a distant NPC's skeletal mesh for a static VAT mesh. The swap
// (SetSkeletalMeshEnabled false) sets the skeletal mesh's collision to NoCollision, and the swap back sets QueryOnly,
// so a corpse that swaps loses its ragdoll profile (QueryAndPhysics) for good. The game's only show=true caller for an
// NPC is ASTNonPlayableCharacterBase::OnSignificanceChanged (exe+0x519DE50), which asks for the static mesh only
// while significance level +0x2a is 0 and !IsDead() (vfunc 0x9e0, exe+0x50F0D40: HealthComponent +0x148, set by
// RefreshState). On the host a dead NPC therefore never swaps. On the client a copy re-created at a rejoin has not
// refreshed its health yet, so its local IsDead is 0 while the host's NPC is dead, and the swap ran (hb41 0xdc, 0x10e,
// 0x118: collision 3 -> 0 at exe+0x5103075, 1 ms before the death replay). The client evaluates the same predicate
// with the host's truth: a show request for a copy whose death edge ran (c38_is_marked) or whose health attribute
// is already <= 0 is refused. The host and every show=false call run unchanged. Every call is logged (capped).
constexpr size_t kAnimatorSkeletalMesh = 0xF8; // USTSkeletalAnimatorComponent: the skeletal mesh it shows or hides
constexpr size_t kAnimatorShowingStatic = 0x158; // bShowingStaticMesh
uint64_t c41_guid(const void* actor); // below (card 041)
constexpr uint32_t kC132StaticLines = 48;

// Movesync field 15 bit 3: the owners whose animator now shows the static mesh (SetShowingStaticMesh_Internal ran with
// show set and was not refused). Kept here because no USTSkeletalAnimatorComponent StaticClass is resolved to read
// +0x158 from the probe; owners are compared by address only, never dereferenced. Cleared at each LoadMap enter.
constexpr uint32_t kC132StaticOwners = 512;
const void* g_c132_static_owner[kC132StaticOwners] = {}; // game thread only
uint32_t g_c132_static_count = 0;

void c132_note_static(const void* owner, bool showing) {
    uint32_t at = g_c132_static_count;
    for (uint32_t i = 0; i < g_c132_static_count; ++i) {
        at = g_c132_static_owner[i] == owner ? i : at;
    }
    if (showing && at == g_c132_static_count && g_c132_static_count < kC132StaticOwners) {
        g_c132_static_owner[g_c132_static_count++] = owner;
    } else if (!showing && at < g_c132_static_count) {
        g_c132_static_owner[at] = g_c132_static_owner[--g_c132_static_count];
    }
}

void c132_static_map_end() {
    g_c132_static_count = 0;
}

bool c132_showing_static(const void* owner) {
    for (uint32_t i = 0; i < g_c132_static_count; ++i) {
        if (g_c132_static_owner[i] == owner) {
            return true;
        }
    }
    return false;
}

void detour_c132_show_static(void* animator, bool show, bool immediate) {
    void* const mesh = read_ptr(animator, kAnimatorSkeletalMesh);
    void* const owner = read_ptr(animator, kComponentOwner);
    float health = -1.0f;
    const int dead_flag = owner != nullptr ? c25_dead(owner, &health) : -1;
    const bool marked = g_client_session && owner != nullptr && c38_is_marked(owner);
    const bool host_dead = marked || (health <= 0.0f && health != -1.0f);
    const bool refuse = g_client_session && show && host_dead;
    const unsigned before = g_collision_enabled != nullptr && mesh != nullptr ? g_collision_enabled(mesh) : 255u;
    const unsigned was_static = static_cast<const uint8_t*>(animator)[kAnimatorShowingStatic];
    if (!refuse) {
        g_orig_c132_show_static(animator, show, immediate);
        if (owner != nullptr) {
            c132_note_static(owner, show);
        }
    } else {
        ++g_c39.static_blocked;
    }
    if (!note_event_cap(g_c39.static_lines, kC132StaticLines, "card132 static-mesh calls")) {
        return;
    }
    const unsigned after = g_collision_enabled != nullptr && mesh != nullptr ? g_collision_enabled(mesh) : 255u;
    log::line("card132: %s static mesh %s guid 0x%llx %s show %d immediate %d caller exe+0x%llx t=%llu: IsDead %d health "
              "%.1f death edge %d -> %s; showing static %u -> %u, mesh collision %u -> %u (refusals %u)",
              side(), owner != nullptr ? object_name(owner).text : "?",
              static_cast<unsigned long long>(owner != nullptr ? c41_guid(owner) : 0), refuse ? "REFUSED" : "ran",
              show ? 1 : 0, immediate ? 1 : 0, exe_rva(_ReturnAddress()),
              static_cast<unsigned long long>(GetTickCount64()), dead_flag, health, marked ? 1 : 0,
              host_dead ? "host dead" : "host alive", was_static, static_cast<const uint8_t*>(animator)[kAnimatorShowingStatic],
              before, after, g_c39.static_blocked);
}

void detour_c132_set_simulate(void* mesh, bool simulate) {
    if (!simulate) {
        c132_note_simulate_off(mesh, _ReturnAddress());
    }
    if (!simulate || g_skeletal_com == nullptr) {
        g_orig_c132_set_simulate(mesh, simulate);
        return;
    }
    const Vec3 at = read_vec3(mesh, kComponentTranslation);
    // The mesh's collision as this call finds it (after anything our death path did) and its object type; the
    // after-call collision is logged too, so a second call on a mesh our code changed shows here.
    char found[256];
    c39_comp_text(mesh, found, sizeof(found));
    const unsigned type_before = g_collision_type != nullptr ? g_collision_type(mesh) : 255u;
    Vec3 before{};
    g_skeletal_com(mesh, &before);
    // Card 146: the game's own start (not the pose apply's, whose caller is in this DLL) on a copy holding the host's
    // pose is blocked before anything moves its bodies.
    if (g_client_session && exe_rva(_ReturnAddress()) != 0) {
        const void* const posed = read_ptr(mesh, kComponentOwner);
        if (c132_pose_game_ragdoll(posed != nullptr ? c41_guid(posed) : 0,
                                   static_cast<uintptr_t>(exe_rva(_ReturnAddress())))) {
            return;
        }
    }
    const char* const what = c39_sync_mesh(mesh);
    Vec3 after{};
    g_skeletal_com(mesh, &after);
    g_orig_c132_set_simulate(mesh, simulate);
    c132_pose_track(mesh); // card 132 pose journal (host only)
    const double stale = vec_dist(before, at);
    ++g_c39.ragdolls;
    g_c39.ragdolls_stale += stale > kC132StaleBodies ? 1u : 0u;
    if (note_event_cap(g_c39.ragdoll_lines, kC132RagdollLines, "card132 ragdoll start lines")) {
        const void* const owner = read_ptr(mesh, kComponentOwner);
        char left[256];
        c39_comp_text(mesh, left, sizeof(left));
        char bp[320];
        c132_bp_text(bp, sizeof(bp));
        log::line("card132: %s ragdoll collision %s: before the call %s object type %u; after it %s object type %u",
                  g_client_session ? "client" : "host", owner != nullptr ? object_name(owner).text : "?", found,
                  type_before, left, g_collision_type != nullptr ? g_collision_type(mesh) : 255u);
        const auto* const m = static_cast<const uint8_t*>(mesh);
        // The floor under the capsule (BlockAll, straight down): a client corpse that sinks with no floor hit here
        // has no floor in its physics scene; one with a floor hit has bodies that ignore it.
        const double floor = c132_floor_below(owner);
        log::line("card132: %s ragdoll start %s guid 0x%llx: mesh at (%.0f, %.0f, %.0f); bodies centre %.0f uu from it before, "
                  "%.0f after; %s; floor %s%.0f; mesh collision %u; caller exe+0x%llX, blueprint %s; %u of %u had stale "
                  "bodies",
                  g_client_session ? "client" : "host", owner != nullptr ? object_name(owner).text : "?",
                  static_cast<unsigned long long>(owner != nullptr ? c41_guid(owner) : 0), at.x, at.y, at.z, stale, vec_dist(after, at), what, floor > -1.0 ? "z " : "none within uu ",
                  floor > -1.0 ? floor : kC132FloorReach, m[kBodyCollisionEnabled], exe_rva(_ReturnAddress()),
                  bp, g_c39.ragdolls_stale, g_c39.ragdolls);
    }
}

// Client: where GetGoreLocation put a mod gore call, next to the actor's root and mesh component locations, so a
// stale gore location shows which transform it came from.
void c39_where_line(const void* actor, const Vec3& before, const Vec3& gore, const char* what, const char* bodies) {
    if (!note_event_cap(g_c39.where_lines, kC39WhereLines, "card039 gore location lines")) {
        return;
    }
    const Vec3 root = actor_location(actor);
    void* const mesh = read_ptr(actor, kCharacterMesh);
    const Vec3 m = mesh != nullptr ? read_vec3(mesh, kComponentTranslation) : Vec3{};
    const double dx = before.x - gore.x;
    const double dy = before.y - gore.y;
    const double dz = before.z - gore.z;
    log::line("card039: client %s gore for %s at (%.0f, %.0f, %.0f); actor at (%.0f, %.0f, %.0f); mesh at (%.0f, %.0f, "
              "%.0f); %s, gore location moved %.0f uu", what, object_name(actor).text, gore.x, gore.y, gore.z, root.x,
              root.y, root.z, m.x, m.y, m.z, bodies, sqrt(dx * dx + dy * dy + dz * dz));
}

// Client: the death burst the host's StartDyingState spawns on every death (response +0x1B0), through Gore OnDeath
// (the same SpawnGoreResponse build): its chunks. Its Niagara is the host's (card 136).
bool c39_client_burst(void* actor) {
    void* const gore = c39_gore(actor);
    if (gore == nullptr) {
        return false;
    }
    Vec3 location{};
    Vec3 before{};
    g_gore_location(actor, &before); // only for the location line: where the gore would have gone
    const char* const bodies = c39_sync_bodies(actor);
    g_gore_location(actor, &location);
    c39_where_line(actor, before, location, "death", bodies);
    g_c39.in_mod_call = true;
    g_orig_gore_death(gore, &location);
    g_c39.in_mod_call = false; // the Niagara is the host's, sent by the card 136 effects manager
    ++g_c39.bursts;
    return true;
}

// Client: the overkill gore for one NPC (Gore OnOverkill; the response +0x230 Niagara is the host's). False without a gore
// component; the NPC joins the gibbed set either way so it is not tried twice.
bool c39_client_gib(void* actor) {
    if (g_c39.gibbed_count < kC39Gibbed) {
        g_c39.gibbed[g_c39.gibbed_count++] = C38Dead{actor, read_u64(actor, kObjectName)};
    }
    void* const gore = c39_gore(actor);
    if (gore == nullptr) {
        return false;
    }
    Vec3 location{};
    Vec3 before{};
    g_gore_location(actor, &before); // only for the location line: where the gore would have gone
    const char* const bodies = c39_sync_bodies(actor);
    g_gore_location(actor, &location);
    c39_where_line(actor, before, location, "overkill", bodies);
    g_c39.in_mod_call = true;
    g_orig_gore_overkill(gore, &location);
    g_c39.in_mod_call = false; // the Niagara is the host's, sent by the card 136 effects manager
    ++g_c39.gibs;
    return true;
}

bool c39_was_gibbed(const void* actor) {
    const uint64_t name = read_u64(actor, kObjectName);
    for (uint32_t i = 0; i < g_c39.gibbed_count; ++i) {
        if (g_c39.gibbed[i].actor == actor && g_c39.gibbed[i].name == name) {
            return true;
        }
    }
    return false;
}

constexpr size_t kNpcAnimator = 0xA30; // USTSkeletalAnimatorComponent* (AnimatorComponent script interface)

// Card 269, client, at a corpse arrival: the animator shows the static mesh (+0x158) or its deferred swap back is
// still counting frames (+0x15C: SetShowingStaticMesh_Internal(false, false) keeps the skeletal mesh hidden and
// USTSkeletalAnimatorComponent::TickComponent, exe+0x5107F40, shows it when the count reaches 0). A copy that arrives
// dead never finished that count (c423b130a: 26 arrivals "(mesh already hidden)", bodies never drawn), so the swap
// back runs here with immediate set: skeletal mesh shown, static mesh hidden. Returns true when it ran.
constexpr size_t kAnimatorSwapFrames = 0x15C; // int32: frames until the deferred swap back completes
constexpr uint32_t kC269SwapLines = 40;
uint32_t g_c269_swap_lines = 0; // per process; game thread

// Card 479 (b187): SetShowingStaticMesh_Internal(false, true) calls SetHiddenInGame on the animator's static mesh
// component (+0x128) with no check, and the game's own swap completion (TickComponent, count at +0x15C reaching 0)
// reads it raw too (SetStaticMeshEnabled, exe+0x5103470). Run c245a crashed the client there on a dangling pointer.
constexpr size_t kAnimatorStaticMesh = 0x128;  // UStaticMeshComponent*
constexpr size_t kActorDestroyBits = 0x5D;     // AActor bitfield byte (execIsActorBeingDestroyed, exe+0x37FE510)
constexpr uint8_t kActorBeingDestroyed = 0x02; // bActorIsBeingDestroyed
constexpr uint32_t kC479Lines = 24;
uint32_t g_c479_lines = 0; // per process; game thread

// Card 479: why the animator's static mesh component may not be called through; null when it is a live
// StaticMeshComponent. `buf` holds the class reason.
const char* c479_static_dead(const void* mesh, char* buf, size_t cap) {
    if (mesh == nullptr) {
        return "null";
    }
    if (!object_present(mesh)) {
        return "freed";
    }
    if (!object_listed(mesh)) {
        return "unreachable or garbage";
    }
    const ObjectName cls = class_name(mesh);
    if (strstr(cls.text, "StaticMeshComponent") == nullptr) {
        snprintf(buf, cap, "class %s", cls.text);
        return buf;
    }
    return nullptr;
}

bool c269_swap_back(void* npc) {
    void* const animator = npc != nullptr ? read_ptr(npc, kNpcAnimator) : nullptr;
    if (animator == nullptr || g_orig_c132_show_static == nullptr) {
        return false;
    }
    // Card 486 (c244b212a): the animator at NPC+0xA30 can be a freed component whose memory was reused; its stale
    // class pointer still named SkeletalAnimator and +0x128 held 0x31F00000001. Only a listed, live UObject is read.
    if (!object_listed(animator)) {
        if (note_event_cap(g_c479_lines, kC479Lines, "card479 swap back skipped")) {
            log::line("WARN card486: client corpse arrival %s guid 0x%llx: animator %p not live (freed, unreachable or "
                      "garbage), swap back skipped",
                      object_name(npc).text, static_cast<unsigned long long>(c41_guid(npc)), animator);
        }
        return false;
    }
    // Card 486 (b214 review): a listed object may be another live UObject that reused the freed slot. Only a
    // USTSkeletalAnimatorComponent is read. +0xA30 is ASTNonPlayableCharacterBase::AnimatorComponent (PropertyClass
    // ADAnimatorBaseComponent, card 023) and c38_death_edge passes only that class here, so every caller's NPC has the
    // field; a sprite NPC's UADSpriteAnimatorComponent there is refused by this test as well (no static mesh swap).
    static void* const skeletal = find_class(L"/Script/Aloft.STSkeletalAnimatorComponent");
    if (skeletal == nullptr || !is_a(animator, skeletal)) {
        const bool sprite = skeletal != nullptr && strstr(class_name(animator).text, "SpriteAnimator") != nullptr;
        if (!sprite && note_event_cap(g_c479_lines, kC479Lines, "card479 swap back skipped")) {
            log::line("WARN card486: client corpse arrival %s guid 0x%llx: animator %p is %s, not a "
                      "STSkeletalAnimatorComponent%s, swap back skipped",
                      object_name(npc).text, static_cast<unsigned long long>(c41_guid(npc)), animator,
                      class_name(animator).text, skeletal == nullptr ? " (class not found)" : "");
        }
        return false;
    }
    const unsigned was_static = static_cast<const uint8_t*>(animator)[kAnimatorShowingStatic];
    int32_t frames = 0;
    memcpy(&frames, static_cast<const uint8_t*>(animator) + kAnimatorSwapFrames, sizeof(frames));
    if (was_static == 0 && frames <= 0) {
        return false;
    }
    const uint64_t guid = c41_guid(npc);
    const void* const mesh = read_ptr(animator, kAnimatorStaticMesh);
    const bool destroying = (static_cast<const uint8_t*>(npc)[kActorDestroyBits] & kActorBeingDestroyed) != 0;
    char why_buf[96];
    const char* const why = destroying ? "actor being destroyed" : c479_static_dead(mesh, why_buf, sizeof(why_buf));
    const int32_t none = 0;
    if (why != nullptr) { // skipped; a zero count keeps the game's own tick off the same pointer
        if (frames > 0) {
            memcpy(static_cast<uint8_t*>(animator) + kAnimatorSwapFrames, &none, sizeof(none));
        }
        if (note_event_cap(g_c479_lines, kC479Lines, "card479 swap back skipped")) {
            log::line("WARN card479: client corpse arrival %s guid 0x%llx: static mesh component %p not live (%s), "
                      "swap back skipped; showing static %u, %d frames left, actor being destroyed %d",
                      object_name(npc).text, static_cast<unsigned long long>(guid), mesh, why, was_static, frames,
                      destroying ? 1 : 0);
        }
        return false;
    }
    if (note_event_cap(g_c269_swap_lines, kC269SwapLines, "card269 swap back")) { // before the call (card 479)
        log::line("card269: client corpse arrival %s guid 0x%llx: static mesh swap back run at once (showing static %u, "
                  "%d frames of the deferred swap left)",
                  object_name(npc).text, static_cast<unsigned long long>(guid), was_static, frames);
    }
    g_orig_c132_show_static(animator, false, true);
    memcpy(static_cast<uint8_t*>(animator) + kAnimatorSwapFrames, &none, sizeof(none));
    c132_note_static(npc, false);
    return true;
}

// Client: the capsule state the host reaches by its first probe after death (NoCollision), when it still collides.
bool c39_client_capsule(void* actor) {
    void* const capsule = read_ptr(actor, kActorRootComponent);
    if (capsule == nullptr || g_collision_enabled(capsule) == kC28NoCollision) {
        return false;
    }
    g_orig_set_collision_enabled(capsule, kC28NoCollision);
    ++g_c39.capsules;
    return true;
}

// Client: the corpse box the host's HandleDying enables (EnableCorpseBoxCollider, 3D NPCs only), when it is off.
bool c39_client_box(void* actor) {
    const void* const box = read_ptr(actor, kNpcCorpseBox);
    if (box == nullptr || g_is_npc_2d(actor) || g_collision_enabled(box) != 0) {
        return false;
    }
    g_enable_corpse_box(actor);
    ++g_c39.boxes;
    return true;
}

// Client, from card 038's death edge: probes, the death burst, the gib when overkilled (unless the host's Gib signal
// already did it), the corpse box and the capsule. True when the gib replaced the collapse.
bool c39_death_edge(void* actor, float now, bool corpse) {
    c39_track(actor);
    const void* const hc = c21_character_ptr(actor, kCharacterHealthComponent);
    const float threshold = hc != nullptr ? g_overkill_threshold(hc) : 0.0f;
    const bool overkilled = threshold != 0.0f && -fabsf(threshold) >= now;
    const bool signalled = c39_was_gibbed(actor);
    const bool burst = !corpse && !g_c39_off && !signalled && c39_client_burst(actor);
    const bool gibbed = overkilled && !signalled && !g_c39_off && c39_client_gib(actor);
    const bool box = !g_c39_off && c39_client_box(actor);
    const bool capsule = !g_c39_off && c39_client_capsule(actor);
    if (note_event_cap(g_c39.edge_lines, kC39EdgeLines, "card039 client edges")) {
        char health[192];
        c39_health_text(actor, health, sizeof(health));
        log::line("card039: client edge %s %s%s, IsNPC2D %d: overkilled %d (%s)%s%s%s%s", class_name(actor).text,
                  object_name(actor).text, health, g_is_npc_2d(actor) ? 1 : 0, overkilled ? 1 : 0,
                  gibbed ? "Gore OnOverkill" : (g_c39_off ? "client gore off" : "no gib"),
                  burst ? "; death burst (Gore OnDeath)" : "",
                  signalled ? "; already gibbed by the host's Gib signal" : "",
                  box ? "; EnableCorpseBoxCollider" : "", capsule ? "; capsule set to NoCollision" : "");
    }
    return gibbed || signalled;
}

// The Gib payload: the location's cell, kC39CellBits per axis (x high, y low), wrapping.
uint32_t c39_cell(const Vec3& at) {
    const uint32_t x = static_cast<uint32_t>(static_cast<int64_t>(floor(at.x / kC39CellUu))) & kC39CellMask;
    const uint32_t y = static_cast<uint32_t>(static_cast<int64_t>(floor(at.y / kC39CellUu))) & kC39CellMask;
    return x << kC39CellBits | y;
}

// Cells apart on one axis, with the wrap.
uint32_t c39_axis_gap(uint32_t a, uint32_t b) {
    const uint32_t d = (a - b) & kC39CellMask;
    return d < (kC39CellMask + 1) - d ? d : (kC39CellMask + 1) - d;
}

// Host, from OverkillDamageEffectReceived (before the original): tell every remote PC which NPC is overkilled. The
// actor is destroyed within a second, before its Health replicates, so the client's death edge never sees it.
void c39_send_gib(const void* owner) {
    if (g_client_session || owner == nullptr || g_npc_class == nullptr || !is_a(owner, g_npc_class()) ||
        c39_was_gibbed(owner)) {
        return; // once per NPC: a repeat would gib the nearest other bug on the client
    }
    if (g_c39.gibbed_count < kC39Gibbed) {
        g_c39.gibbed[g_c39.gibbed_count++] = C38Dead{owner, read_u64(owner, kObjectName)};
    }
    void* const world = g_actor_get_world(owner);
    if (world == nullptr) {
        log::line("WARN card039: host overkilled %s has no world -- no Gib signal", object_name(owner).text);
        return;
    }
    const Vec3 at = actor_location(owner);
    const uint32_t cell = c39_cell(at);
    const uint32_t sent = c42_owe_gib(owner, cell) ? 0u : c42_send_gib(world, owner, cell); // card 269: owed
    g_c39.sent += sent;
    if (note_event_cap(g_c39.gib_lines, kC39GibLines, "card039 Gib signals")) {
        log::line("card039: host Gib signal %s %s at (%.0f, %.0f, %.0f) cell %u (x %u y %u) sent to %u PCs",
                  class_name(owner).text, object_name(owner).text, at.x, at.y, at.z, cell, cell >> kC39CellBits,
                  cell & kC39CellMask, sent);
    }
}

// The live NPC nearest the Gib cell (within kC39CellSlack per axis) that is not yet gibbed, or null.
void* c39_match_cell(const C23List& list, uint32_t cell) {
    void* best = nullptr;
    uint32_t best_gap = 2 * kC39CellSlack + 1;
    for (uint32_t i = 0; i < list.count; ++i) {
        void* const actor = list.e[i].actor;
        const uint32_t c = c39_cell(list.e[i].loc);
        const uint32_t gx = c39_axis_gap(c >> kC39CellBits, cell >> kC39CellBits);
        const uint32_t gy = c39_axis_gap(c & kC39CellMask, cell & kC39CellMask);
        if (gx > kC39CellSlack || gy > kC39CellSlack || gx + gy >= best_gap || c39_was_gibbed(actor)) {
            continue;
        }
        best = actor;
        best_gap = gx + gy;
    }
    return best;
}

// Client: gibs the NPC a Gib signal named (card 042 resolves it by NetGUID, or by cell as the fallback; `route` says
// which). The death burst too when the death edge has not run (Health still above 0 here). Null actor: log only.
void c39_gib_actor(void* actor, uint32_t cell, const char* route) {
    g_c39.unmatched += actor == nullptr ? 1 : 0;
    if (actor != nullptr && g_c37.audit && g_c39.gibs == 0) {
        g_c39.gib_shot_ms = GetTickCount64() + kC39GibShotMs; // the first gib of the map only
        g_c39.gib_loc = actor_location(actor);
    }
    if (actor != nullptr && g_c38.edge_ms == 0) {
        // card 038's kill shots: an overkilled bug is destroyed before its death edge, so the signal starts them
        g_c38.edge_ms = GetTickCount64();
        g_c38.shot_bug = C38Dead{actor, read_u64(actor, kObjectName)};
        g_c38.shot_loc = actor_location(actor);
    }
    const bool dead = actor != nullptr && c38_is_marked(actor);
    const bool burst = actor != nullptr && !dead && !g_c39_off && c39_client_burst(actor);
    const bool gibbed = actor != nullptr && !g_c39_off && c39_client_gib(actor);
    if (!note_event_cap(g_c39.gib_lines, kC39GibLines, "card039 Gib signals")) {
        return;
    }
    char health[192] = "";
    if (actor != nullptr) {
        c39_health_text(actor, health, sizeof(health));
    }
    const Vec3 at = actor != nullptr ? actor_location(actor) : Vec3{};
    log::line("card039: client Gib signal %s cell %u (x %u y %u): %s %s at (%.0f, %.0f, %.0f)%s: %s%s", route, cell,
              cell >> kC39CellBits, cell & kC39CellMask, actor != nullptr ? class_name(actor).text : "no NPC",
              actor != nullptr ? object_name(actor).text : "in range", at.x, at.y, at.z, health,
              gibbed ? "Gore OnOverkill" : (g_c39_off ? "client gore off" : "no gib"),
              burst ? "; death burst (Gore OnDeath)" : (dead ? "; death edge already ran" : ""));
}

// Client: brackets the replay of one host hit (hit_c132.cpp) so SpawnGoreResponse fills in its chunks.
void c39_hit_call(bool on) {
    g_c39.in_hit_call = on;
}

// Card 200: at LoadMap enter (ubw_load_map_enter): C39Probe and the gib list keep old-world actor pointers.
void c39_map_end() {
    if (g_c39.probe_count + g_c39.gibs + g_c39.sent + g_c39.received != 0) {
        log::line("card039: %s map totals: probe slots %u, Gib signals sent %u received %u unmatched %u, client gibs "
                  "%u death bursts %u, hit responses %u, corpse boxes enabled %u, capsules cleared %u", side(),
                  g_c39.probe_count, g_c39.sent, g_c39.received, g_c39.unmatched, g_c39.gibs, g_c39.bursts,
                  g_c39.hit_responses, g_c39.boxes, g_c39.capsules);
    }
    g_c39 = C39State{};
}

// First tick after LoadMap. Card 200: c42 stays here, not at LoadMap enter: c42_release_pools runs later in
// ubw_load_map_enter (client_diag_load_map_enter) and needs the held pools in g_c42c, and no c42 row is written
// inside LoadMap.
void c39_arm_map() {
    c42_arm_map();
}

// Both sides, log only: when each NPC actor ends play (the host's destroy timing and the client's net destroy).
void detour_npc_end_play(void* npc, uint32_t reason) {
    if (g_c37.audit && note_event_cap(g_c39.end_lines, kC39EndPlayLines, "card039 NPC EndPlay")) {
        char health[192];
        c39_health_text(npc, health, sizeof(health));
        log::line("card039: %s NPC EndPlay %s %s reason %u%s, %lld ms after the last HandleDeathEvent", side(),
                  class_name(npc).text, object_name(npc).text, reason & 0xFFu, health,
                  g_c38.death_ms != 0 ? static_cast<long long>(GetTickCount64() - g_c38.death_ms) : -1LL);
    }
    g_orig_npc_end_play(npc, reason);
}

void detour_gore_overkill(void* component, const Vec3* location) {
    char extra[96];
    snprintf(extra, sizeof(extra), "; at (%.0f, %.0f, %.0f)", location->x, location->y, location->z);
    c39_line("Gore OnOverkill", read_ptr(component, kComponentOwner), extra);
    g_orig_gore_overkill(component, location); // its SpawnGoreResponse carries the host's burst row
}

// ---- Card 041: bug attack animations on the client ----
// ASTNonPlayableCharacterBase::TryPlayAnimationSequence (0x051B0F30): the sequence player is the interface pointer at
// NPC+0xA38. vtbl+0x18 = ready (nothing plays when false); vtbl+0x48(player, FName* out) returns the current
// sequence's FName (no restart when force is false and it equals the request); vtbl+0x8(player, FName,
// TArray* frame_callbacks, TArray* end_callbacks, float) plays. Each callback array holds one 16-byte entry
// {FWeakObjectPtr NPC, FName ProcessFrameAction / AnimationEnded}; those two only broadcast the multicast delegates at
// NPC+0x950 and +0x960 (TArray of 16-byte FScriptDelegate {FWeakObjectPtr, FName}).
constexpr size_t kNpcSeqPlayer = 0xA38;
constexpr size_t kNpcFrameDelegate = 0x950;
constexpr size_t kNpcEndDelegate = 0x960;
constexpr size_t kSeqVtHasKey = 0x18; // HasAnimationSequenceForKey(FName) (UADSprite/UADSkeletalAnimatorComponent)
constexpr size_t kSeqVtCurrent = 0x48;
constexpr size_t kSeqVtPlay = 0x8; // UADAnimatorBaseComponent::StartAnimationSequenceCallback(FName, frame, end, rate)
constexpr size_t kDelegateBytes = 16;
constexpr size_t kDelegateName = 8;
constexpr int32_t kC41Listeners = 8;      // listeners logged per delegate
constexpr int32_t kC41ListenerSane = 256; // a larger Num is not read
constexpr uint32_t kC41TraceLines = 300;
constexpr uint32_t kC41ListenerLines = 80;
constexpr uint32_t kC41CallbackLines = 40; // per callback kind

constexpr const char* kC41CbName[kC41Cbs] = {"NPC ProcessFrameAction", "NPC AnimationEnded",
                                             "USTAttackComponent OnAnimationEnd",
                                             "USTAttackActionPlayAnimation OnAnimationEnd"};

using SeqHasKeyFn = bool (*)(void* player, uint64_t name);
using SeqCurrentFn = const uint64_t* (*)(void* player, uint64_t* out);

TryPlaySeqFn g_orig_c41_try_play = nullptr;
TryPlayTimedFn g_orig_c41_try_timed = nullptr;
NpcCallbackFn g_orig_c41_frame = nullptr;
NpcCallbackFn g_orig_c41_ended = nullptr;
AnimEndFn g_orig_c41_attack_end = nullptr;
AnimEndFn g_orig_c41_action_end = nullptr;
GetNetGuidFn g_get_net_guid = nullptr;           // UPackageMapClient::GetNetGUIDFromObject
GetObjectFromGuidFn g_object_from_guid = nullptr; // UPackageMapClient::GetObjectFromNetGUID
bool g_c41_trace = false; // UBWCOOP_C41_ANIM_TRACE; written in ubw::init before any hook

C41State g_c41{};

// The package map of the first PC with a NetConnection (host: a remote client's; client: the server's), or null.
void* c41_package_map(const void* world) {
    for (int s = 0; s < kC17Slots; ++s) {
        const void* const pc = g_get_player_controller(world, s);
        const void* const conn = pc != nullptr ? read_ptr(pc, kPcNetConnection) : nullptr;
        if (conn != nullptr) {
            return read_ptr(conn, kConnPackageMap);
        }
    }
    return nullptr;
}

// The actor's NetGUID value (the host assigns it; the client learns it), or 0 when there is none.
uint64_t c41_guid(const void* actor) {
    const void* const world = g_actor_get_world(actor);
    const void* const map = world != nullptr ? c41_package_map(world) : nullptr;
    if (map == nullptr || g_get_net_guid == nullptr) {
        return 0;
    }
    uint64_t out = 0;
    const uint64_t* const guid = g_get_net_guid(map, &out, actor);
    return guid != nullptr ? *guid : 0;
}

constexpr uint32_t kC132CorpseCollisionLines = 24;

void c132_corpse_collision(const void* owner, const void* component, unsigned before, unsigned after,
                           unsigned long long caller) {
    if (before == after || owner == nullptr || net_mode(owner) != kNetModeClient ||
        !is_a(owner, g_character_class()) || read_ptr(owner, kCharacterMesh) != component || !c38_is_marked(owner) ||
        !note_event_cap(g_c38.corpse_collision_lines, kC132CorpseCollisionLines, "card132 corpse collision")) {
        return;
    }
    const Vec3 at = actor_location(owner);
    log::line("card132: client corpse mesh collision %s guid 0x%llx at (%.0f, %.0f, %.0f): %u -> %u, caller exe+0x%llX",
              class_name(owner).text, static_cast<unsigned long long>(c41_guid(owner)), at.x, at.y, at.z, before,
              after, caller);
}

C41Seq* c41_seq(uint64_t name) {
    for (uint32_t i = 0; i < g_c41.seq_count; ++i) {
        if (g_c41.seq[i].name == name) {
            return &g_c41.seq[i];
        }
    }
    if (g_c41.seq_count >= kC41Seqs) {
        ++g_c41.seq_dropped;
        return nullptr;
    }
    C41Seq* const s = &g_c41.seq[g_c41.seq_count++];
    *s = C41Seq{};
    s->name = name;
    return s;
}

// Signature of one delegate's listener set: the count, each weak object index and each function FName.
uint32_t c41_listener_sig(const void* npc, size_t off) {
    const uint8_t* const arr = static_cast<const uint8_t*>(npc) + off;
    const uint8_t* const data = static_cast<const uint8_t*>(read_ptr(arr, kArrayData));
    const int32_t num = array_num(arr, 0);
    uint32_t sig = static_cast<uint32_t>(num) * 0x9E3779B1u;
    if (data == nullptr || num <= 0 || num > kC41ListenerSane) {
        return sig;
    }
    for (int32_t i = 0; i < num; ++i) {
        const uint8_t* const d = data + static_cast<size_t>(i) * kDelegateBytes;
        sig = sig * 31u + static_cast<uint32_t>(read_i32(d, 0));
        sig = sig * 31u + static_cast<uint32_t>(read_u64(d, kDelegateName));
    }
    return sig;
}

void c41_listener_line(const void* npc, const char* which, size_t off) {
    const uint8_t* const arr = static_cast<const uint8_t*>(npc) + off;
    const uint8_t* const data = static_cast<const uint8_t*>(read_ptr(arr, kArrayData));
    const int32_t num = array_num(arr, 0);
    const int32_t shown = (data == nullptr || num <= 0) ? 0 : (num < kC41Listeners ? num : kC41Listeners);
    char text[640] = "";
    size_t used = 0;
    for (int32_t i = 0; i < shown && used < sizeof(text); ++i) {
        const uint8_t* const d = data + static_cast<size_t>(i) * kDelegateBytes;
        const void* const obj = g_weak_get(d);
        const int w = snprintf(text + used, sizeof(text) - used, " [%s %s . %s]", class_name(obj).text,
                               object_name(obj).text, fname_text(d + kDelegateName).text);
        used += w > 0 ? static_cast<size_t>(w) : sizeof(text);
    }
    log::line("card041: %s %s %s %s delegate +0x%zx: %d listeners%s", side(), class_name(npc).text,
              object_name(npc).text, which, off, num, text);
}

// The NPC's listener sets, logged when first seen and whenever they change (capped).
void c41_note_npc(const void* npc) {
    const uint32_t sig = c41_listener_sig(npc, kNpcFrameDelegate) ^ (c41_listener_sig(npc, kNpcEndDelegate) * 7u);
    const uint64_t name = read_u64(npc, kObjectName);
    C41Npc* slot = nullptr;
    for (uint32_t i = 0; i < g_c41.npc_count && slot == nullptr; ++i) {
        slot = (g_c41.npc[i].npc.actor == npc && g_c41.npc[i].npc.name == name) ? &g_c41.npc[i] : nullptr;
    }
    if (slot == nullptr && g_c41.npc_count < kC41Npcs) {
        slot = &g_c41.npc[g_c41.npc_count++];
        *slot = C41Npc{C38Dead{npc, name}, ~sig};
    }
    if (slot == nullptr || slot->listeners == sig) {
        return;
    }
    slot->listeners = sig;
    if (!note_event_cap(g_c41.listener_lines, kC41ListenerLines, "card041 listener sets")) {
        return;
    }
    const void* const player = read_ptr(npc, kNpcSeqPlayer);
    log::line("card041: %s NPC %s %s %p guid 0x%llx: player %p vtbl 0x%llx; listeners:", side(), class_name(npc).text,
              object_name(npc).text, npc, static_cast<unsigned long long>(c41_guid(npc)), player,
              player != nullptr ? exe_rva(read_ptr(player, 0)) : 0ull);
    c41_listener_line(npc, "ProcessFrameAction", kNpcFrameDelegate);
    c41_listener_line(npc, "AnimationEnded", kNpcEndDelegate);
}

// One TryPlay call (force -1 = the time-augmented variant, `time` < 0 = the plain one): counts, and a capped line
// for each call that plays (not a repeat of the current sequence).
void c41_trace(void* npc, uint64_t name, int32_t force, float time, unsigned long long ret) {
    void* const player = read_ptr(npc, kNpcSeqPlayer);
    const auto ready = player != nullptr ? reinterpret_cast<SeqHasKeyFn>(read_ptr(read_ptr(player, 0), kSeqVtHasKey))
                                         : nullptr;
    const bool is_ready = ready != nullptr && ready(player, name);
    const auto current = reinterpret_cast<SeqCurrentFn>(is_ready ? read_ptr(read_ptr(player, 0), kSeqVtCurrent)
                                                                  : nullptr);
    uint64_t out = 0;
    const uint64_t* const cur = current != nullptr ? current(player, &out) : nullptr;
    const uint64_t now_name = cur != nullptr ? *cur : 0;
    const bool same = force == 0 && now_name == name;
    C41Seq* const s = c41_seq(name);
    if (s != nullptr) {
        s->calls += force >= 0 ? 1 : 0;
        s->timed += force < 0 ? 1 : 0;
        s->forced += force == 1 ? 1 : 0;
        s->same += same ? 1 : 0;
    }
    c41_note_npc(npc);
    if (same || !note_event_cap(g_c41.lines, kC41TraceLines, "card041 anim trace")) {
        return;
    }
    log::line("card041: %s %s %s %p seq %s force %d time %.2f ready %d current %s ret 0x%llx player %s guid 0x%llx",
              side(), class_name(npc).text, object_name(npc).text, npc, fname_text(&name).text, force,
              static_cast<double>(time), is_ready ? 1 : 0, cur != nullptr ? fname_text(cur).text : "-", ret,
              player != nullptr ? "set" : "null", static_cast<unsigned long long>(c41_guid(npc)));
}

// Card 041 relay (always on): the host's attack sequence plays go to each client as ChanOp::Anim {NetGUID bits 0-13,
// NetGUID bits 14-27, table index * 2 + 1 when the NPC is named by card 039's location cell instead}. The client
// plays the sequence visual only: the player's vtbl+0x8 with empty callback arrays, so neither ProcessFrameAction
// nor AnimationEnded is bound and no delegate listener (the enemy FSM on AnimationEnded) runs.
// Card 132 item 4b: the AnimNotifies on these sequences (muzzle flashes, attack and pain sounds) run on the client's
// copy only when the copy plays the sequence; hb54 shows the relayed WarriorBug Attack firing its notifies there. hb54
// host-only forced plays: Attack01/Attack02 (ArcherBug and the other bugs), Flinch, Taunt, Spawn, OverKill, and the
// troopers' GrenadeThrow and Reload. Idle and Death the client plays itself; Walk and Run are not forced.
constexpr const wchar_t* kC41AttackSeq[] = {
    L"Attack", // card 041 run b: the only sequence the host plays that the client never does on its own
    L"Attack01", L"Attack02", L"Flinch", L"Taunt", L"Spawn", L"OverKill", L"GrenadeThrow", L"Reload",
};
constexpr uint32_t kC41AttackSeqs = sizeof(kC41AttackSeq) / sizeof(kC41AttackSeq[0]);
constexpr uint32_t kC41GuidBits = 14; // per argument: every argument stays below kChanArgMax
constexpr uint64_t kC41GuidMask = (1ull << kC41GuidBits) - 1;
constexpr uint64_t kC41GuidMax = 1ull << (2 * kC41GuidBits);
constexpr float kC41PlayRate = 1.0f; // the rate TryPlayAnimationSequence passes (0x1404EB4C .rdata)
constexpr uint32_t kC41RelayLines = 120;
constexpr uint32_t kC41Shots = 3; // screenshot pairs: the host at its first sends, the client at its first plays

// frame_callbacks / end_callbacks: const FADAnimationDelegate& (16 bytes, all zero = unbound).
using SeqPlayFn = void (*)(void* player, uint64_t name, void* frame_callbacks, void* end_callbacks, float rate);
struct C41EmptyArray {
    void* data;
    int32_t num;
    int32_t max;
};
struct C41Relay {
    uint64_t fname[kC41AttackSeqs]; // FName values, built on first use (not game pointers)
    uint32_t sent;
    uint32_t sent_cell; // sends that named the NPC by cell (no NetGUID)
    uint32_t received;
    uint32_t played;
    uint32_t unmatched;
    uint32_t not_ready;
    uint32_t no_key; // host: sequence the NPC's player does not have, no row (card 436)
    uint32_t lines;
    uint32_t shots;
};
C41Relay g_c41r{};

int32_t c41_attack_index(uint64_t name) {
    for (uint32_t i = 0; i < kC41AttackSeqs; ++i) {
        if (fname_cached(g_c41r.fname[i], kC41AttackSeq[i]) == name) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

// The sequence the NPC's player is on now, or 0 when it has no player.
uint64_t c41_current(const void* npc) {
    void* const player = read_ptr(npc, kNpcSeqPlayer);
    const auto current = reinterpret_cast<SeqCurrentFn>(player != nullptr ? read_ptr(read_ptr(player, 0), kSeqVtCurrent)
                                                                          : nullptr);
    uint64_t out = 0;
    const uint64_t* const cur = current != nullptr ? current(player, &out) : nullptr;
    return cur != nullptr ? *cur : 0;
}

void c434_owner_row(const void* owner, const char* why, double min_far, bool fx); // world_c98.cpp

void c41_send_row(const void* npc, void* world, int32_t index);

// Card 436: Anim rows held for a NetGUID. A replicated NPC has no NetGUID until its first replication, which is after
// the spawn frame, so its Spawn sequence went by cell before the client had the actor: c399b132a (six maps) 42 of 53
// Spawn rows read "no NPC matched" on the client and the other 11 matched whatever NPC stood in that cell. The host
// now keeps such a row and sends it by guid on the first frame the NPC has one. An NPC still without one after
// kC41HoldMs is not relevant to any client (beyond the net cull distance), and its row is not sent. An NPC the game
// does not replicate (RemoteRole 0) and a full list keep the send by cell.
constexpr uint32_t kC41Held = 128; // b131 FED_C2M1: 51 spawns in one second
constexpr ULONGLONG kC41HoldMs = 2000;
constexpr uint32_t kC41HeldLines = 40; // per map
struct C41Held {
    C31Weak npc;
    uint32_t index;
    ULONGLONG at_ms;
};
struct C41HeldTally {
    uint32_t held;
    uint32_t sent;     // by guid, after the wait
    uint32_t expired;  // no NetGUID after kC41HoldMs: not sent
    uint32_t gone;     // the NPC was destroyed while held
    uint32_t full;     // list full: sent by cell at once
    ULONGLONG worst_ms; // longest wait of a sent row
    uint32_t lines;
};
C41Held g_c41_held[kC41Held] = {}; // host, game thread
uint32_t g_c41_held_n = 0;
C41HeldTally g_c41_held_tally{};

// True when the row was kept for c41_held_step (the caller sends nothing now).
bool c41_hold(const void* npc, uint32_t index) {
    const bool replicated = static_cast<const uint8_t*>(npc)[kActorRemoteRole] != 0;
    if (c41_guid(npc) != 0 || !replicated) {
        return false;
    }
    if (g_c41_held_n == kC41Held) {
        ++g_c41_held_tally.full;
        return false;
    }
    C41Held& h = g_c41_held[g_c41_held_n++];
    h = C41Held{{}, index, GetTickCount64()};
    c31_weak_set(h.npc, const_cast<void*>(npc));
    ++g_c41_held_tally.held;
    return true;
}

void c41_held_end(void* npc, const C41Held& h, ULONGLONG now, bool ready) {
    C41HeldTally& t = g_c41_held_tally;
    void* const world = npc != nullptr && ready ? g_actor_get_world(npc) : nullptr;
    const ULONGLONG waited = now - h.at_ms;
    t.gone += npc == nullptr ? 1 : 0;
    t.expired += npc != nullptr && !ready ? 1 : 0;
    if (world != nullptr) {
        ++t.sent;
        t.worst_ms = waited > t.worst_ms ? waited : t.worst_ms;
        c41_send_row(npc, world, static_cast<int32_t>(h.index));
    }
    if (note_event_cap(t.lines, kC41HeldLines, "card436 held Anim")) {
        log::line("card436: host Anim seq %ls for %s held %llu ms: %s", kC41AttackSeq[h.index],
                  npc != nullptr ? class_name(npc).text : "(destroyed)", static_cast<unsigned long long>(waited),
                  world != nullptr ? "sent by guid" : (npc == nullptr ? "NPC gone, not sent"
                                                                     : "still no NetGUID, not sent"));
    }
}

// Host, every frame: sends each held row whose NPC now has a NetGUID; drops the destroyed and the expired.
void c41_held_step(ULONGLONG now) {
    uint32_t keep = 0;
    for (uint32_t i = 0; i < g_c41_held_n; ++i) {
        const C41Held h = g_c41_held[i];
        void* const npc = c31_weak_get(h.npc);
        const bool ready = npc != nullptr && c41_guid(npc) != 0;
        if (npc != nullptr && !ready && now - h.at_ms < kC41HoldMs) {
            g_c41_held[keep++] = h;
            continue;
        }
        c41_held_end(npc, h, now, ready);
    }
    g_c41_held_n = keep;
}

// Card 436: true when the NPC's sequence player has `name`. The game calls Play(Spawn) on classes with no Spawn
// sequence (troopers, HopperBug, turrets: c399b141a, 27 of 50 Spawn rows); the host plays nothing there and the
// client's copy of the same class data cannot play the row either.
bool c41_has_key(const void* npc, uint64_t name) {
    void* const player = read_ptr(npc, kNpcSeqPlayer);
    const auto has = player != nullptr ? reinterpret_cast<SeqHasKeyFn>(read_ptr(read_ptr(player, 0), kSeqVtHasKey))
                                       : nullptr;
    return has != nullptr && has(player, name);
}

// Host, before the original runs: one Anim frame to each remote PC when `name` is an attack sequence that will play
// (forced, or not already the current one) and the NPC's player has it.
void c41_send_attack(const void* npc, uint64_t name, bool force) {
    const int32_t index = c41_attack_index(name);
    const bool wanted = index >= 0 && (force || c41_current(npc) != name);
    const bool plays = wanted && c41_has_key(npc, name);
    g_c41r.no_key += wanted && !plays && !g_client_session ? 1 : 0;
    void* const world = plays ? g_actor_get_world(npc) : nullptr;
    if (world == nullptr || c41_hold(npc, static_cast<uint32_t>(index))) {
        return;
    }
    c41_send_row(npc, world, index);
}

// Host: one Anim frame for attack sequence `index` on npc to each remote PC, by NetGUID or (none usable) by cell.
void c41_send_row(const void* npc, void* world, int32_t index) {
    const uint64_t guid = c41_guid(npc);
    const bool by_cell = guid == 0 || guid >= kC41GuidMax;
    const uint32_t cell = c39_cell(actor_location(npc));
    const ChanArgs args{{by_cell ? cell : static_cast<uint32_t>(guid & kC41GuidMask),
                         by_cell ? 0u : static_cast<uint32_t>((guid >> kC41GuidBits) & kC41GuidMask),
                         static_cast<uint32_t>(index) * 2u + (by_cell ? 1u : 0u)}};
    uint32_t sent = 0;
    uint32_t no_channel = 0;
    for (int s = 0; s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        if (!by_cell && pc != nullptr && sprint_side(pc) == kSideServerCopy && !c434_has_channel(pc, npc)) {
            ++no_channel; // card 434: this client has no copy of the NPC (not relevant to it)
            continue;
        }
        sent += chan_to_client_args(pc, ChanOp::Anim, args) ? 1 : 0;
    }
    static uint32_t skipped = 0; // host, game thread; session total
    skipped += no_channel;
    if (no_channel != 0 && (skipped <= 8 || skipped % 64 == 0)) {
        log::line("card434: host Anim for %s guid 0x%llx not sent to %u PCs with no actor channel for it (not "
                  "relevant to them); %u rows skipped this session",
                  class_name(npc).text, static_cast<unsigned long long>(guid), no_channel, skipped);
    }
    g_c41r.sent += sent != 0 ? 1 : 0;
    g_c41r.sent_cell += sent != 0 && by_cell ? 1 : 0;
    if (sent != 0) {
        c434_owner_row(npc, "Anim sent for a far NPC", 5000.0, false); // card 434 step 2 (diagnostic)
    }
    if (sent != 0 && g_c41r.shots < kC41Shots && g_c41_trace) {
        ++g_c41r.shots;
        request_shot(g_get_player_controller(world, 0), false);
    }
    if (sent != 0 && note_event_cap(g_c41r.lines, kC41RelayLines, "card041 Anim relay")) {
        const void* const player = read_ptr(npc, kNpcSeqPlayer);
        log::line("card041: host Anim %s %s %s guid 0x%llx%s seq %ls (index %d) sent to %u PCs; player %p vtbl 0x%llx",
                  class_name(npc).text, object_name(npc).text, by_cell ? "by cell" : "by guid",
                  static_cast<unsigned long long>(guid), by_cell ? " (none usable)" : "", kC41AttackSeq[index], index,
                  sent, player, player != nullptr ? exe_rva(read_ptr(player, 0)) : 0ull);
    }
}

// Card 209: a NetGUID as two channel values of kC41GuidBits each. False (lo, hi untouched) when the GUID is 0 or
// needs more than the two values hold; the caller logs and sends nothing.
bool c41_guid_args(uint64_t guid, uint32_t& lo, uint32_t& hi) {
    if (guid == 0 || guid >= kC41GuidMax) {
        return false;
    }
    lo = static_cast<uint32_t>(guid & kC41GuidMask);
    hi = static_cast<uint32_t>((guid >> kC41GuidBits) & kC41GuidMask);
    return true;
}

uint64_t c41_args_guid(uint32_t lo, uint32_t hi) {
    return (static_cast<uint64_t>(lo) & kC41GuidMask) | ((static_cast<uint64_t>(hi) & kC41GuidMask) << kC41GuidBits);
}

// Client: the object a NetGUID names through the server connection's package map, or null (not mapped yet).
void* c41_object_from_guid(void* pc, uint64_t guid) {
    const void* const conn = pc != nullptr ? read_ptr(pc, kPcNetConnection) : nullptr;
    void* const map = conn != nullptr ? read_ptr(conn, kConnPackageMap) : nullptr;
    return map != nullptr && g_object_from_guid != nullptr && guid != 0 ? g_object_from_guid(map, &guid, false)
                                                                        : nullptr;
}

// Card 209, client: the player pawn a NetGUID names (the own pawn included), or null while it does not resolve.
void* c41_resolve_player(void* pc, uint64_t guid) {
    void* const object = g_player_character_class != nullptr ? c41_object_from_guid(pc, guid) : nullptr;
    return object != nullptr && is_a(object, g_player_character_class()) ? object : nullptr;
}

// Client: the NPC an Anim frame names, or null.
void* c41_resolve(void* pc, const ChanArgs& args, bool by_cell) {
    void* const world = g_actor_get_world(pc);
    if (world == nullptr || g_npc_class == nullptr) {
        return nullptr;
    }
    if (by_cell) {
        static C23List list; // game thread only; too large for the stack
        c23_collect(world, nullptr, g_npc_class(), list);
        return c39_match_cell(list, args.v[0]);
    }
    void* const object = c41_object_from_guid(pc, c41_args_guid(args.v[0], args.v[1]));
    return is_a(object, g_npc_class()) ? object : nullptr;
}

// Client: the NPC a NetGUID names, or null (card 132 hit relay; a GUID past the Anim frame's two values is unmapped).
void* c41_resolve_guid(void* pc, uint64_t guid) {
    if (guid == 0 || guid >= kC41GuidMax) {
        return nullptr;
    }
    const ChanArgs args{{static_cast<uint32_t>(guid & kC41GuidMask),
                         static_cast<uint32_t>((guid >> kC41GuidBits) & kC41GuidMask), 0}};
    return c41_resolve(pc, args, false);
}

// Card 132 movesync field 15 (root-motion gate probe). ACharacter::OnRep_ReplicatedMovement (exe+0x39b8ec0) skips
// the location update while Mesh->IsPlayingNetworkedRootMotionMontage() (exe+0x3ae8440: AnimScriptInstance +0x30
// RootMotionMode == 3 and a root-motion montage instance at +0x328 whose +0x1c4 disable count is < 1). hb43 disproved
// this as the 0x1dc freeze cause: WarriorBug copies run RootMotionMode 1 natively, bits 0 and 1 stayed 0 for every
// guid, and the client Montage_PlayInternal detour that forced mode 1 never fired; it was removed.
constexpr size_t kMeshAnimInstance = 0x8f8;      // USkeletalMeshComponent::AnimScriptInstance
constexpr size_t kAnimRootMotionMode = 0x30;     // UAnimInstance::RootMotionMode (ERootMotionMode, uint8)
constexpr size_t kAnimRootMotionMontage = 0x328; // UAnimInstance root-motion FAnimMontageInstance*
constexpr size_t kMontageRootMotionOff = 0x1c4;  // FAnimMontageInstance: root-motion disable count (int32)
constexpr uint8_t kRootMotionFromMontagesOnly = 3;
constexpr size_t kMeshHiddenByte = 0x1A1;
constexpr uint8_t kMeshHiddenBit = 0x08;

// Movesync field 15. Bit 0: the anim instance takes montage root motion (mode 3); bit 1: the mesh plays a networked
// root-motion montage now, the gate that stops ReplicatedMovement (exe+0x3ae8440); bit 2: the Mesh is bHiddenInGame
// (+0x1A1 bit 0x08), so its location is not what the player sees (a sprite-rendered trooper, a bug in static mode).
uint32_t c132_root_motion_state(const void* npc) {
    const auto* const mesh = static_cast<const uint8_t*>(npc != nullptr ? read_ptr(npc, kCharacterMesh) : nullptr);
    const auto* const anim = static_cast<const uint8_t*>(mesh != nullptr ? read_ptr(mesh, kMeshAnimInstance) : nullptr);
    const uint32_t hidden = mesh != nullptr && (mesh[kMeshHiddenByte] & kMeshHiddenBit) != 0 ? 4u : 0u;
    if (anim == nullptr || anim[kAnimRootMotionMode] != kRootMotionFromMontagesOnly) {
        return hidden;
    }
    const void* const montage = read_ptr(anim, kAnimRootMotionMontage);
    const bool gating = montage != nullptr && read_i32(montage, kMontageRootMotionOff) < 1;
    return hidden | 1u | (gating ? 2u : 0u);
}

// Movesync field 15 bits 4 and 5, which component the player sees (card 132 senior note 2 after hb44: a skeletal mesh
// pinned while the capsule walks away is only a visible bug if that mesh is drawn). Bit 4: the Mesh is not bVisible
// (+0x1A0 bit 0x20). Bit 5: the NPC has a USTSpriteRenderComponent that is bVisible and not bHiddenInGame (a
// sprite-rendered trooper); fields 16-18 then give that renderer's world location (the capsule's when it has none).
uint32_t c132_drawn_state(const void* npc, const void* mesh) {
    const auto* const m = static_cast<const uint8_t*>(mesh);
    const uint32_t mesh_off = m != nullptr && (m[kCompVisibleByte] & kCompVisibleBit) == 0 ? 16u : 0u;
    const auto* const sprite = static_cast<const uint8_t*>(
        g_sprite_render_class != nullptr ? find_component(npc, g_sprite_render_class) : nullptr);
    const bool sprite_drawn = sprite != nullptr && (sprite[kCompVisibleByte] & kCompVisibleBit) != 0 &&
                              (sprite[kCompHiddenByte] & kCompHiddenBit) == 0;
    return mesh_off | (sprite_drawn ? 32u : 0u);
}

// Where the player sees the NPC, for every mod check that compares positions (movesync fields 6-8, the client hit
// distance): the sprite renderer's location when that renderer is drawn (state bit 32; the skeletal mesh component of
// a sprite-drawn NPC is stale, hb46 and hb47: 2387 off-capsule samples, every one sprite-drawn at the capsule), else
// the ragdoll's body 0 when its bodies simulate (the component stays where the corpse died), else the mesh component,
// else the capsule. `source` (may be null) gets 1 sprite, 2 ragdoll body, 3 mesh, 0 capsule.
Vec3 c132_drawn_location(const void* npc, uint32_t* source) {
    const void* const mesh = npc != nullptr ? read_ptr(npc, kCharacterMesh) : nullptr;
    const void* const sprite =
        npc != nullptr && g_sprite_render_class != nullptr ? find_component(npc, g_sprite_render_class) : nullptr;
    uint32_t from = 0;
    Vec3 at = npc != nullptr ? actor_location(npc) : Vec3{};
    if (sprite != nullptr && (c132_drawn_state(npc, mesh) & 32u) != 0) {
        at = read_vec3(sprite, kComponentTranslation);
        from = 1;
    } else if (c132_ragdoll_root(mesh, at)) {
        from = 2;
    } else if (mesh != nullptr) {
        at = read_vec3(mesh, kComponentTranslation);
        from = 3;
    }
    if (source != nullptr) {
        *source = from;
    }
    return at;
}

// Client: plays `name` on the NPC's sequence player with no callbacks. False when the player is absent or lacks `name`.
bool c41_play_visual(void* npc, uint64_t name) {
    void* const player = read_ptr(npc, kNpcSeqPlayer);
    const auto ready = player != nullptr ? reinterpret_cast<SeqHasKeyFn>(read_ptr(read_ptr(player, 0), kSeqVtHasKey))
                                         : nullptr;
    if (ready == nullptr || !ready(player, name)) {
        return false;
    }
    const auto play = reinterpret_cast<SeqPlayFn>(read_ptr(read_ptr(player, 0), kSeqVtPlay));
    C41EmptyArray frame{};
    C41EmptyArray end{};
    play(player, name, &frame, &end, kC41PlayRate);
    if (frame.data != nullptr || end.data != nullptr) {
        log::line("WARN card041: client visual play left callback arrays %p %p (expected empty)", frame.data, end.data);
    }
    return true;
}

// --- Card 434 step 4 (diagnostic, client) ---
// The host has an actor channel open for NPCs whose Anim rows the client cannot place for 20 s and more (c399b135a,
// C5M2: 11 guids, `channel 1 relevant 1`). These rows say which of three things the client did with each of them:
// never processed a spawn bunch, processed one that made no actor, or made the actor and closed its channel.
constexpr uint32_t kC434Spawns = 512; // NPC spawn bunches remembered a map (ring)
constexpr uint32_t kC434SpawnRows = 64;
constexpr uint32_t kC434FailRows = 16;
constexpr uint32_t kC434CloseRows = 64;
constexpr uint32_t kC434MissRows = 48;
// Card 528 (diagnostic): spawn and close rows in the first kC434ArmWindowMs after each client map arm bypass the
// per-map caps (own cap kC434ArmRows), so the left map's corpses re-seen in the new world can be joined to their spawn
// bunch and channel close by object name and guid.
constexpr ULONGLONG kC434ArmWindowMs = 5000;
constexpr uint32_t kC434ArmRows = 192;

struct C434Spawn {
    const void* channel; // null once the channel closed
    uint64_t guid;
    ULONGLONG spawn_ms;
    ULONGLONG close_ms; // 0 while the channel is open
    uint8_t reason;     // EChannelCloseReason at the close
    bool for_destroy;
};

struct C434Client {
    C434Spawn e[kC434Spawns];
    uint32_t next;
    uint32_t spawned;
    uint32_t no_actor;
    uint32_t closed;
    uint32_t spawn_lines;
    uint32_t fail_lines;
    uint32_t close_lines;
    uint32_t miss_lines;
    ULONGLONG arm_ms;   // card 528: the last client map arm, 0 before the first
    uint32_t arm_lines; // card 528: rows logged inside the arm window
};

C434Client g_c434c{};

// Card 528: a spawn or close row is logged inside the arm window (own cap), else under its per-map cap.
bool c434_row_allowed(uint32_t& lines, uint32_t cap, const char* tag, ULONGLONG now) {
    if (g_c434c.arm_ms != 0 && now - g_c434c.arm_ms < kC434ArmWindowMs && g_c434c.arm_lines < kC434ArmRows) {
        ++g_c434c.arm_lines;
        return true;
    }
    return note_event_cap(lines, cap, tag);
}
C434NewActorFn g_orig_c434_new_actor = nullptr;
C434CleanUpFn g_orig_c434_cleanup = nullptr;
C434SerializeObjectFn g_orig_c434_serialize_object = nullptr;

// Card 434 (diagnostic, b139): the SerializeObject calls inside one client spawn bunch (actor, then the archetype
// and level for a dynamic guid), so a bunch without an actor says which read failed: archetype null, the class is
// not loaded; the actor call null with a nonzero guid and nothing after it, an archive error or a broken guid; all
// good, the spawn itself failed. Set around the SerializeNewActor original only (game thread).
constexpr int32_t kC434Calls = 4;

struct C434Call {
    const void* cls;
    const void* object;
    uint64_t guid;
    bool result;
    bool has_guid;
};

struct C434Bunch {
    C434Call c[kC434Calls];
    const void* map; // the UPackageMapClient of the bunch
    int32_t n;
    int32_t more; // calls past kC434Calls
    bool active;
};

C434Bunch g_c434b{};

bool detour_c434_serialize_object(void* map, void* ar, void* cls, void** object, uint64_t* out_guid) {
    const bool ok = g_orig_c434_serialize_object(map, ar, cls, object, out_guid);
    if (!g_c434b.active) {
        return ok;
    }
    if (g_c434b.n >= kC434Calls) {
        ++g_c434b.more;
        return ok;
    }
    g_c434b.c[g_c434b.n++] = C434Call{cls, object != nullptr ? *object : nullptr,
                                      out_guid != nullptr ? *out_guid : 0, ok, out_guid != nullptr};
    return ok;
}

C434CacheObjectFn g_c434_cache_object = nullptr;
constexpr int32_t kC434OuterDepth = 3;

// Card 434 (diagnostic): the client's guid cache entry of `guid` and of its outers (the archetype, then its
// package), one row each. No entry: the host sent the guid without its path (it holds the export as acked).
// An entry with a null object: the path did not load here.
void c434_cache_rows(ULONGLONG now, const void* map, uint64_t guid) {
    const void* const cache = map != nullptr ? read_ptr(map, kMapGuidCache) : nullptr;
    if (cache == nullptr || g_c434_cache_object == nullptr) {
        log::line("card434: client t=%llu guid cache not read (cache %p)", static_cast<unsigned long long>(now), cache);
        return;
    }
    for (int32_t level = 0; level < kC434OuterDepth && guid != 0; ++level) {
        const uint8_t* const e = g_c434_cache_object(cache, &guid);
        if (e == nullptr) {
            log::line("card434: client t=%llu guid cache 0x%llx (level %d): no entry",
                      static_cast<unsigned long long>(now), static_cast<unsigned long long>(guid),
                      static_cast<int>(level));
            return;
        }
        C31Weak weak{};
        uint64_t outer = 0;
        uint32_t sum = 0;
        memcpy(&weak, e, sizeof(weak));
        memcpy(&outer, e + 0x8, sizeof(outer));
        memcpy(&sum, e + 0x18, sizeof(sum));
        const uint8_t bits = e[0x28];
        log::line("card434: client t=%llu guid cache 0x%llx (level %d): path %s, outer 0x%llx, checksum 0x%x, "
                  "object index %d serial %d, no-load %d, ignore-missing %d, pending %d, broken %d",
                  static_cast<unsigned long long>(now), static_cast<unsigned long long>(guid),
                  static_cast<int>(level), fname_text(e + 0x10).text, static_cast<unsigned long long>(outer),
                  static_cast<unsigned>(sum), static_cast<int>(weak.index), static_cast<int>(weak.serial),
                  static_cast<int>(bits & 1), static_cast<int>((bits >> 1) & 1), static_cast<int>((bits >> 2) & 1),
                  static_cast<int>((bits >> 3) & 1));
        guid = outer;
    }
}

// The remembered calls of the bunch that made no actor, one row.
void c434_calls_row(ULONGLONG now) {
    char text[768];
    size_t used = 0;
    for (int32_t i = 0; i < g_c434b.n; ++i) {
        const C434Call& r = g_c434b.c[i];
        char guid[24] = "none";
        if (r.has_guid) {
            snprintf(guid, sizeof(guid), "0x%llx", static_cast<unsigned long long>(r.guid));
        }
        const int n = snprintf(text + used, sizeof(text) - used, "%s %d: cls %s, result %d, guid %s, object %s %s",
                               i == 0 ? "" : ";", static_cast<int>(i + 1),
                               r.cls != nullptr ? object_name(r.cls).text : "null", r.result ? 1 : 0, guid,
                               r.object != nullptr ? class_name(r.object).text : "null",
                               r.object != nullptr ? object_name(r.object).text : "");
        used += n > 0 && static_cast<size_t>(n) < sizeof(text) - used ? static_cast<size_t>(n) : 0;
    }
    log::line("card434: client t=%llu the bunch's SerializeObject calls (%d, %d more):%s",
              static_cast<unsigned long long>(now), static_cast<int>(g_c434b.n), static_cast<int>(g_c434b.more),
              used > 0 ? text : " none");
    for (int32_t i = 0; i < g_c434b.n; ++i) {
        const C434Call& r = g_c434b.c[i];
        if (r.has_guid && r.guid != 0 && r.object == nullptr) {
            c434_cache_rows(now, g_c434b.map, r.guid);
        }
    }
}

bool detour_c434_new_actor(void* map, void* ar, void* channel, void** actor) {
    g_c434b = C434Bunch{};
    g_c434b.map = map;
    g_c434b.active = g_client_session;
    const bool made = g_orig_c434_new_actor(map, ar, channel, actor);
    g_c434b.active = false;
    if (!g_client_session) {
        return made;
    }
    void* const a = actor != nullptr ? *actor : nullptr;
    const ULONGLONG now = GetTickCount64();
    if (a == nullptr) {
        ++g_c434c.no_actor;
        if (note_event_cap(g_c434c.fail_lines, kC434FailRows, "card434 spawn bunch without an actor")) {
            log::line("card434: client t=%llu a spawn bunch made no actor (channel %p, result %d)",
                      static_cast<unsigned long long>(now), channel, made ? 1 : 0);
            c434_calls_row(now);
        }
        return made;
    }
    if (!is_a(a, g_npc_class())) {
        return made;
    }
    uint64_t out = 0;
    const uint64_t* const guid = g_get_net_guid != nullptr ? g_get_net_guid(map, &out, a) : nullptr;
    C434Spawn& e = g_c434c.e[g_c434c.next];
    g_c434c.next = (g_c434c.next + 1) % kC434Spawns;
    e = C434Spawn{channel, guid != nullptr ? *guid : 0, now, 0, 0, false};
    ++g_c434c.spawned;
    if (c434_row_allowed(g_c434c.spawn_lines, kC434SpawnRows, "card434 NPC spawn bunch", now)) {
        const Vec3 at = actor_location(a);
        log::line("card434: client t=%llu spawn bunch gave %s %s guid 0x%llx at (%.0f, %.0f, %.0f), new actor %d",
                  static_cast<unsigned long long>(now), class_name(a).text, object_name(a).text,
                  static_cast<unsigned long long>(e.guid), at.x, at.y, at.z, made ? 1 : 0);
    }
    return made;
}

// Client: the channel of a remembered NPC spawn closes (reason: 0 destroyed, 1 dormancy, 2 level unloaded,
// 3 relevancy, 4 tear off).
void c434_note_close(const void* channel, bool for_destroy, uint8_t reason) {
    for (uint32_t i = 0; i < kC434Spawns; ++i) {
        C434Spawn& e = g_c434c.e[i];
        if (e.channel != channel) {
            continue;
        }
        e.channel = nullptr;
        e.close_ms = GetTickCount64();
        e.reason = reason;
        e.for_destroy = for_destroy;
        ++g_c434c.closed;
        if (c434_row_allowed(g_c434c.close_lines, kC434CloseRows, "card434 NPC channel close", e.close_ms)) {
            log::line("card434: client t=%llu channel of guid 0x%llx closed %llu ms after its spawn, reason %u "
                      "(0 destroyed, 1 dormancy, 2 level unloaded, 3 relevancy, 4 tear off), for destroy %d",
                      static_cast<unsigned long long>(e.close_ms), static_cast<unsigned long long>(e.guid),
                      static_cast<unsigned long long>(e.close_ms - e.spawn_ms), reason, for_destroy ? 1 : 0);
        }
        return;
    }
}

bool detour_c434_cleanup(void* channel, bool for_destroy, uint8_t reason) {
    if (g_client_session && channel != nullptr && g_c434c.spawned != 0) {
        c434_note_close(channel, for_destroy, reason);
    }
    return g_orig_c434_cleanup(channel, for_destroy, reason);
}

// Client: an Anim row by guid found no NPC. Says what the guid maps to now and what the spawn table holds for it.
void c434_miss(void* pc, uint64_t guid) {
    if (guid == 0 || !note_event_cap(g_c434c.miss_lines, kC434MissRows, "card434 Anim miss")) {
        return;
    }
    const C434Spawn* seen = nullptr;
    for (uint32_t i = 0; i < kC434Spawns; ++i) {
        const C434Spawn& e = g_c434c.e[i];
        if (e.guid == guid && e.spawn_ms != 0 && (seen == nullptr || e.spawn_ms >= seen->spawn_ms)) {
            seen = &e;
        }
    }
    const ULONGLONG now = GetTickCount64();
    char spawn[160];
    if (seen == nullptr) {
        snprintf(spawn, sizeof(spawn), "no spawn bunch seen for it (%u NPC spawn bunches this map, table %u)",
                 g_c434c.spawned, kC434Spawns);
    } else if (seen->close_ms == 0) {
        snprintf(spawn, sizeof(spawn), "spawned %llu ms ago, channel open",
                 static_cast<unsigned long long>(now - seen->spawn_ms));
    } else {
        snprintf(spawn, sizeof(spawn), "spawned %llu ms ago, channel closed %llu ms ago, reason %u, for destroy %d",
                 static_cast<unsigned long long>(now - seen->spawn_ms),
                 static_cast<unsigned long long>(now - seen->close_ms), seen->reason, seen->for_destroy ? 1 : 0);
    }
    const void* const object = c41_object_from_guid(pc, guid);
    log::line("card434: client t=%llu Anim miss guid 0x%llx: object %s; %s", static_cast<unsigned long long>(now),
              static_cast<unsigned long long>(guid), object != nullptr ? class_name(object).text : "none", spawn);
}

void c41_receive_anim(void* pc, const ChanArgs& args) {
    ++g_c41r.received;
    const uint32_t index = args.v[2] >> 1;
    const bool by_cell = (args.v[2] & 1u) != 0;
    void* const npc = index < kC41AttackSeqs ? c41_resolve(pc, args, by_cell) : nullptr;
    const uint64_t name = index < kC41AttackSeqs ? fname_cached(g_c41r.fname[index], kC41AttackSeq[index]) : 0;
    const bool played = npc != nullptr && name != 0 && c41_play_visual(npc, name);
    g_c41r.unmatched += npc == nullptr ? 1 : 0;
    if (npc == nullptr && !by_cell && index < kC41AttackSeqs) {
        c434_miss(pc, c41_args_guid(args.v[0], args.v[1])); // card 434 step 4
    }
    g_c41r.not_ready += npc != nullptr && !played ? 1 : 0;
    g_c41r.played += played ? 1 : 0;
    if (played && g_c41r.shots < kC41Shots && g_c41_trace) {
        ++g_c41r.shots;
        request_shot(pc, false);
    }
    if (!note_event_cap(g_c41r.lines, kC41RelayLines, "card041 Anim relay")) {
        return;
    }
    const void* const player = npc != nullptr ? read_ptr(npc, kNpcSeqPlayer) : nullptr;
    log::line("card041: client Anim %s %u/%u index %u (%ls): %s %s %s (player %p vtbl 0x%llx current %llx); callbacks "
              "so far frame %u ended %u attack end %u", by_cell ? "cell" : "guid", args.v[0], args.v[1], index,
              index < kC41AttackSeqs ? kC41AttackSeq[index] : L"bad index", npc != nullptr ? class_name(npc).text : "no NPC",
              npc != nullptr ? object_name(npc).text : "matched", played ? "played visual only" : "not played", player,
              player != nullptr ? exe_rva(read_ptr(player, 0)) : 0ull,
              static_cast<unsigned long long>(npc != nullptr ? c41_current(npc) : 0), g_c41.cb[kC41Frame],
              g_c41.cb[kC41Ended], g_c41.cb[kC41AttackEnd]);
    if (played && g_c41r.played <= kC41Shots) {
        c41_listener_line(npc, "ProcessFrameAction", kNpcFrameDelegate);
        c41_listener_line(npc, "AnimationEnded", kNpcEndDelegate);
    }
}

void c41_relay_totals(const char* when) {
    log::line("card041: %s Anim %s: sent %u (by cell %u), received %u, played %u, unmatched %u, not ready %u, no such "
              "sequence (host, no row) %u", side(), when, g_c41r.sent, g_c41r.sent_cell, g_c41r.received,
              g_c41r.played, g_c41r.unmatched, g_c41r.not_ready, g_c41r.no_key);
    const C41HeldTally& t = g_c41_held_tally;
    if (t.held + t.full != 0) {
        log::line("card436: host Anim %s: held for a NetGUID %u, sent by guid %u (longest wait %llu ms), not sent after "
                  "%llu ms without one %u, NPC gone %u, still held %u, list full (sent by cell) %u",
                  when, t.held, t.sent, static_cast<unsigned long long>(t.worst_ms),
                  static_cast<unsigned long long>(kC41HoldMs), t.expired, t.gone, g_c41_held_n, t.full);
    }
}

void c41_relay_arm_map() {
    if (g_c41r.sent + g_c41r.received + g_c41r.no_key != 0) {
        c41_relay_totals("map totals");
    }
    if (g_c434c.spawned + g_c434c.no_actor != 0) {
        log::line("card434: client NPC spawn bunches (map totals): %u with an actor, %u without, %u channels "
                  "closed", g_c434c.spawned, g_c434c.no_actor, g_c434c.closed);
    }
    g_c434c = C434Client{};
    g_c434c.arm_ms = GetTickCount64(); // card 528: the arm window starts here
    const C41Relay keep = g_c41r;
    g_c41r = C41Relay{};
    g_c41_held_n = 0; // card 436: a held row does not cross a map
    g_c41_held_tally = C41HeldTally{};
    memcpy(g_c41r.fname, keep.fname, sizeof(g_c41r.fname)); // FName values outlive the map
}

void detour_c41_try_play(void* npc, uint64_t name, bool force) {
    if (npc != nullptr) {
        c41_send_attack(npc, name, force);
    }
    if (g_c41_trace && npc != nullptr) {
        c41_trace(npc, name, force ? 1 : 0, -1.0f, exe_rva(_ReturnAddress()));
    }
    g_orig_c41_try_play(npc, name, force);
}

void detour_c41_try_timed(void* npc, uint64_t name, float time) {
    if (g_c41_trace && npc != nullptr) {
        c41_trace(npc, name, -1, time, exe_rva(_ReturnAddress()));
    }
    g_orig_c41_try_timed(npc, name, time);
}

void c41_cb_line(C41Cb cb, const void* self, uint32_t reason, unsigned long long ret) {
    ++g_c41.cb[cb];
    if (!note_event_cap(g_c41.cb_lines[cb], kC41CallbackLines, kC41CbName[cb])) {
        return;
    }
    log::line("card041: %s %s on %s %s reason %u ret 0x%llx", side(), kC41CbName[cb], class_name(self).text,
              object_name(self).text, reason, ret);
}

void detour_c41_frame(void* npc) {
    if (g_c41_trace) {
        c41_cb_line(kC41Frame, npc, 0, exe_rva(_ReturnAddress()));
    }
    g_orig_c41_frame(npc);
}

void detour_c41_ended(void* npc) {
    if (g_c41_trace) {
        c41_cb_line(kC41Ended, npc, 0, exe_rva(_ReturnAddress()));
    }
    g_orig_c41_ended(npc);
}

void detour_c41_attack_end(void* component, uint8_t reason) {
    if (g_c41_trace) {
        c41_cb_line(kC41AttackEnd, component, reason, exe_rva(_ReturnAddress()));
    }
    g_orig_c41_attack_end(component, reason);
}

void detour_c41_action_end(void* action, uint8_t reason) {
    if (g_c41_trace) {
        c41_cb_line(kC41ActionEnd, action, reason, exe_rva(_ReturnAddress()));
    }
    g_orig_c41_action_end(action, reason);
}

void c41_dump(const char* when, const void* world) {
    const void* const map = world != nullptr ? c41_package_map(world) : nullptr;
    log::line("card041: %s %s: %u NPCs, %u sequences (%u dropped); callbacks frame %u ended %u attack end %u action "
              "end %u; package map %p %s", side(), when, g_c41.npc_count, g_c41.seq_count, g_c41.seq_dropped,
              g_c41.cb[kC41Frame], g_c41.cb[kC41Ended], g_c41.cb[kC41AttackEnd], g_c41.cb[kC41ActionEnd], map,
              class_name(map).text);
    for (uint32_t i = 0; i < g_c41.seq_count; ++i) {
        const C41Seq& s = g_c41.seq[i];
        log::line("card041: %s   seq %s: calls %u timed %u forced %u same-as-current %u", side(),
                  fname_text(&s.name).text, s.calls, s.timed, s.forced, s.same);
    }
}

// Diagnostic (playtest: bugs do not animate on the client): with the card 041 trace on, both sides log the nearest
// NPCs' animator state at each dump. USTSkeletalAnimatorComponent::TickComponent (0x05107F40) runs its static mesh
// animation path only when the byte at +0x158 is set; +0x15C counts frames down to the skeletal switch; the static
// mesh path advances the float at +0x14C up to the duration at +0x150. The animator object is NPC+0xA30 (kNpcAnimator).
constexpr size_t kAnimStaticMode = 0x158;
constexpr size_t kAnimSwitchCount = 0x15C;
constexpr size_t kAnimStaticTime = 0x14C;
constexpr size_t kAnimStaticLength = 0x150;
constexpr size_t kAnimSkelMesh = 0xF8;
constexpr uint32_t kAnimProbeNpcs = 4;
struct AnimProbeSeen {
    const void* npc;
    Vec3 loc;
    float time;
};
AnimProbeSeen g_anim_probe[kAnimProbeNpcs] = {};
constexpr ULONGLONG kAnimProbeMs = 3000;
ULONGLONG g_anim_probe_next_ms = 0;

void anim_probe(void* world) {
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    const void* const pawn = pc != nullptr ? c23_own_pawn(pc) : nullptr;
    if (pawn == nullptr || g_npc_class == nullptr) {
        return;
    }
    static C23List list; // game thread only; too large for the stack
    c23_collect(world, pawn, g_npc_class(), list);
    AnimProbeSeen next[kAnimProbeNpcs] = {};
    uint32_t shown = 0;
    for (uint32_t pick = 0; pick < kAnimProbeNpcs; ++pick) {
        int32_t best = -1;
        for (uint32_t i = 0; i < list.count; ++i) {
            bool taken = false;
            for (uint32_t k = 0; k < shown; ++k) {
                taken = taken || next[k].npc == list.e[i].actor;
            }
            const void* const animator = read_ptr(list.e[i].actor, kNpcAnimator);
            taken = taken || !object_listed(animator) || strstr(class_name(animator).text, "SkeletalAnimator") == nullptr;
            if (!taken && (best < 0 || list.e[i].dist < list.e[best].dist)) {
                best = static_cast<int32_t>(i);
            }
        }
        if (best < 0) {
            break;
        }
        const C23Enemy& e = list.e[best];
        const auto* const anim = static_cast<const uint8_t*>(read_ptr(e.actor, kNpcAnimator));
        const float time = anim != nullptr ? read_f32(anim, kAnimStaticTime) : -1.0f;
        const uint64_t key = c41_current(e.actor);
        const AnimProbeSeen* prev = nullptr;
        for (uint32_t k = 0; k < kAnimProbeNpcs && prev == nullptr; ++k) {
            prev = g_anim_probe[k].npc == e.actor ? &g_anim_probe[k] : nullptr;
        }
        log::line("animprobe: %s %s %s %.0f uu: animator %s %p static %d switch %d time %.2f/%.2f (was %.2f) mesh %p "
                  "key %s moved %.0f uu",
                  side(), class_name(e.actor).text, object_name(e.actor).text, e.dist, class_name(anim).text, anim,
                  anim != nullptr ? anim[kAnimStaticMode] : -1, anim != nullptr ? read_i32(anim, kAnimSwitchCount) : -1,
                  static_cast<double>(time), anim != nullptr ? static_cast<double>(read_f32(anim, kAnimStaticLength)) : -1.0,
                  prev != nullptr ? static_cast<double>(prev->time) : -1.0,
                  anim != nullptr ? read_ptr(anim, kAnimSkelMesh) : nullptr, fname_text(&key).text,
                  prev != nullptr ? vec_dist(prev->loc, e.loc) : -1.0);
        next[shown++] = AnimProbeSeen{e.actor, e.loc, time};
    }
    memcpy(g_anim_probe, next, sizeof(next));
}

void c64_set_yaw(void* pc, double yaw, double pitch); // testrun.cpp

// Card 132 note 2 (hb45 review, side-on after hb47): a sprite-drawn NPC (state bit 32) whose skeletal mesh component
// sits more than kOffShotUu from its capsule. The logs put the sprite at the capsule; the shots show whether the stale
// skeletal mesh is drawn as well. When the `offshot` command arms it, the host waits for one that stands still
// (kOffShotStillMs), then for the capsule and then the mesh: warps its pawn kOffShotNear uu side-on of the point
// (perpendicular to the capsule-to-mesh line, so the other point is not behind it), aims at it and takes a named shot,
// phases kOffShotGapMs apart (kOffShotSettleMs after each warp). hb47's pair, taken from wherever the pawn stood, had
// both on one line of sight (57 and 56 degrees); hb50's one side-on point was 4449 uu out, where a trooper is a few
// pixels at 640x360.
constexpr double kOffShotUu = 300.0;
constexpr double kOffShotNear = 500.0;
constexpr double kOffShotStillUu = 50.0;       // hb48 0x64 ran at ~750 uu/s and was gone by the shot: only an NPC
constexpr ULONGLONG kOffShotStillMs = 1000;    // that moved under kOffShotStillUu over kOffShotStillMs is shot
constexpr ULONGLONG kOffShotForgetMs = 3000;   // a candidate not probed this long is replaced
constexpr double kOffShotLift = 150.0; // uu over the higher of the two points
constexpr ULONGLONG kOffShotGapMs = 150;
constexpr ULONGLONG kOffShotSettleMs = 1000;
constexpr uint32_t kOffShotDone = 7; // phases 1..6: warp by the capsule, aim, shoot, warp by the mesh, aim, shoot
struct OffShot {
    uint64_t guid;
    Vec3 capsule;
    Vec3 mesh;
    uint32_t phase; // 0 idle
    ULONGLONG at;
};
struct OffCandidate {
    uint64_t guid; // 0 none
    Vec3 loc;      // its capsule when it became the candidate
    ULONGLONG since, seen;
};
OffShot g_off_shot{};
OffCandidate g_off_cand{};
bool g_off_shot_armed = false; // set by the `offshot` command, cleared when the shots are queued
const void* g_off_shot_world = nullptr;

// Host, from move_probe: queue the shots for this NPC when it is sprite-drawn with its mesh off the capsule, and none
// has been queued on this map.
void c132_off_shot_pick(const void* world, const C23Enemy& e, const Vec3& mesh, uint32_t drawn, ULONGLONG now) {
    if (world != g_off_shot_world) {
        g_off_shot_world = world;
        g_off_shot = OffShot{};
        g_off_cand = OffCandidate{};
        g_off_shot_armed = false;
    }
    if (g_client_session || g_off_shot.phase != 0 || !g_off_shot_armed || (drawn & 32u) == 0 ||
        c23_set_health(e.actor) <= 0.0f || vec_dist(mesh, e.loc) <= kOffShotUu) {
        return;
    }
    const uint64_t guid = c41_guid(e.actor);
    const bool mine = g_off_cand.guid == guid;
    if (!mine && g_off_cand.guid != 0 && now - g_off_cand.seen < kOffShotForgetMs) {
        return; // another candidate is still being watched
    }
    if (!mine || vec_dist(e.loc, g_off_cand.loc) > kOffShotStillUu) {
        g_off_cand = OffCandidate{guid, e.loc, now, now}; // new, or it moved: the still time starts again
        return;
    }
    g_off_cand.seen = now;
    if (now - g_off_cand.since < kOffShotStillMs) {
        return;
    }
    g_off_shot_armed = false;
    g_off_shot = OffShot{guid, e.loc, mesh, 1, now};
    log::line("card132: host off-capsule shots for sprite-drawn %s guid 0x%llx (still %llu ms): capsule (%.0f, %.0f, "
              "%.0f), mesh (%.0f, %.0f, %.0f), %.0f uu apart", class_name(e.actor).text,
              static_cast<unsigned long long>(g_off_shot.guid), static_cast<unsigned long long>(now - g_off_cand.since),
              e.loc.x, e.loc.y, e.loc.z, mesh.x, mesh.y, mesh.z, vec_dist(mesh, e.loc));
}

const char* c132_off_shot_arm() {
    g_off_shot_armed = true;
    g_off_cand = OffCandidate{};
    return nullptr;
}

// Phases 1 and 4: the host pawn kOffShotNear uu side-on of `target`. False when the move failed.
bool c132_off_shot_warp(const void* pawn, const Vec3& target) {
    const Vec3& c = g_off_shot.capsule;
    const Vec3& m = g_off_shot.mesh;
    const double dx = m.x - c.x;
    const double dy = m.y - c.y;
    const double flat = sqrt(dx * dx + dy * dy);
    const double px = flat > 1.0 ? -dy / flat : 1.0; // unit perpendicular in the ground plane
    const double py = flat > 1.0 ? dx / flat : 0.0;
    const Vec3 to{target.x + px * kOffShotNear, target.y + py * kOffShotNear, target.z + kOffShotLift};
    const bool moved = g_set_actor_location(const_cast<void*>(pawn), &to, false, nullptr, kTeleportPhysics);
    log::line("card132: host off-capsule side-on warp for guid 0x%llx to (%.0f, %.0f, %.0f), %.0f uu from (%.0f, %.0f, "
              "%.0f); SetActorLocation %d", static_cast<unsigned long long>(g_off_shot.guid), to.x, to.y, to.z,
              kOffShotNear, target.x, target.y, target.z, moved ? 1 : 0);
    return moved;
}

// Host, every engine tick: one phase of the queued shots once its time comes.
void c132_off_shot_step(void* world, ULONGLONG now) {
    if (g_off_shot.phase == 0 || now < g_off_shot.at || world != g_off_shot_world) {
        return;
    }
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    const void* const pawn = pc != nullptr ? c23_own_pawn(pc) : nullptr;
    const uint32_t phase = g_off_shot.phase;
    const bool at_mesh = phase >= 4;
    const Vec3 target = at_mesh ? g_off_shot.mesh : g_off_shot.capsule;
    if (pawn == nullptr || ((phase == 1 || phase == 4) && !c132_off_shot_warp(pawn, target))) {
        g_off_shot.phase = 0;
        return;
    }
    if (phase == 2 || phase == 5) {
        ViewPoint view{};
        if (!c24_view_point(pawn, view)) {
            view.loc = actor_location(pawn);
        }
        const double flat = sqrt((target.x - view.loc.x) * (target.x - view.loc.x) +
                                 (target.y - view.loc.y) * (target.y - view.loc.y));
        c64_set_yaw(pc, atan2(target.y - view.loc.y, target.x - view.loc.x) * kRadToDeg,
                    atan2(target.z - view.loc.z, flat) * kRadToDeg);
    } else if (phase == 3 || phase == 6) {
        // Named (<tag>-offshot-capsule.png, <tag>-offshot-mesh.png): a numbered shot cannot be told apart afterwards.
        const char* const why = c63_shot(at_mesh ? "offshot-mesh" : "offshot-capsule");
        log::line("card132: host off-capsule shot %s for guid 0x%llx at (%.0f, %.0f, %.0f)%s%s", at_mesh ? "mesh" : "capsule",
                  static_cast<unsigned long long>(g_off_shot.guid), target.x, target.y, target.z, why != nullptr ? ": " : "",
                  why != nullptr ? why : "");
    }
    g_off_shot.phase = phase + 1 < kOffShotDone ? phase + 1 : 0;
    g_off_shot.at = now + (phase == 1 || phase == 4 ? kOffShotSettleMs : kOffShotGapMs);
}

// Card 132 movement probe (with the anim trace): every kMoveProbeMs, each side logs every NPC as guid, location,
// +0x5A rep flags, health, drawn location (c132_drawn_location; state bit 64 when read from the ragdoll body), CharacterMovement mode and
// capsule collision enabled, kMoveProbeRow NPCs per line,
// the first line of each sample led by the side's own pawn location. Host and client lines join on guid offline
// (tools/movesync.py) to show which client copies stop following the host and from when.
constexpr ULONGLONG kMoveProbeMs = 1000;
constexpr uint32_t kMoveProbeRow = 7;       // NPCs per line (each at most ~130 chars of the 1024-char line)
constexpr uint32_t kMoveProbeWindows = 300; // per map
ULONGLONG g_move_probe_next_ms = 0;
uint32_t g_move_probe_windows = 0;
const void* g_move_probe_world = nullptr;

void move_probe(void* world, ULONGLONG now) {
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    const void* const pawn = pc != nullptr ? c23_own_pawn(pc) : nullptr;
    if (world != g_move_probe_world) {
        g_move_probe_world = world;
        g_move_probe_windows = 0;
    }
    if (pawn == nullptr || g_npc_class == nullptr || g_move_probe_windows >= kMoveProbeWindows) {
        return;
    }
    ++g_move_probe_windows;
    static C23List list; // game thread only; too large for the stack
    c23_collect(world, pawn, g_npc_class(), list);
    // Card 132: the card's SetMovementMode hook covers an NPC only when its CharacterMovement class keeps the engine's
    // SetMovementMode in the vtable slot every caller uses.
    const void* const cmc0 = list.count > 0 ? read_ptr(list.e[0].actor, kCharacterMovement) : nullptr;
    if (g_move_probe_windows == 1 && cmc0 != nullptr) {
        const auto* const vt = static_cast<void* const*>(read_ptr(cmc0, 0));
        const auto slot = reinterpret_cast<uintptr_t>(vt[kVtSetMovementMode / sizeof(void*)]);
        log::line("card132: %s NPC CharacterMovement SetMovementMode slot exe+%llx, hooked exe+%x", side(),
                  static_cast<unsigned long long>(slot - reinterpret_cast<uintptr_t>(g_exe_base)),
                  offsets::UCharacterMovementComponent_SetMovementMode.rva);
    }
    char text[kMoveProbeRow * 140];
    const Vec3 own = actor_location(pawn);
    // A leading "pawn x y z" row (4 fields, which movesync.py's NPC parser skips) gives this side's own pawn location.
    const int lead = snprintf(text, sizeof(text), " pawn %.0f %.0f %.0f;", own.x, own.y, own.z);
    size_t used = lead > 0 && static_cast<size_t>(lead) < sizeof(text) ? static_cast<size_t>(lead) : 0;
    uint32_t in_row = 0;
    for (uint32_t i = 0; i < list.count; ++i) {
        const C23Enemy& e = list.e[i];
        const auto* const a = static_cast<const uint8_t*>(e.actor);
        const void* const mesh = read_ptr(e.actor, kCharacterMesh);
        const Vec3 m = mesh != nullptr ? read_vec3(mesh, kComponentTranslation) : e.loc;
        const auto* const cmc = static_cast<const uint8_t*>(read_ptr(e.actor, kCharacterMovement));
        void* const capsule = read_ptr(e.actor, kActorRootComponent);
        // Card 132: ReplicatedMovement.Location last (on a client copy, the host's last replicated location), so a
        // stale copy shows whether movement stopped arriving or arrived and was not applied.
        Vec3 rep = e.loc;
        c132_rep_location(e.actor, &rep);
        const uint32_t drawn = c132_drawn_state(e.actor, mesh);
        c132_off_shot_pick(world, e, m, drawn, now);
        uint32_t source = 0;
        const Vec3 seen = c132_drawn_location(e.actor, &source);
        const void* const sprite =
            g_sprite_render_class != nullptr ? find_component(e.actor, g_sprite_render_class) : nullptr;
        const Vec3 sp = sprite != nullptr ? read_vec3(sprite, kComponentTranslation) : e.loc;
        const int n = snprintf(text + used, sizeof(text) - used,
                               " %llx %.0f %.0f %.0f %02X %.0f %.0f %.0f %.0f %u %d %.0f %.0f %.0f %u %.0f %.0f %.0f;",
                               static_cast<unsigned long long>(c41_guid(e.actor)), e.loc.x, e.loc.y, e.loc.z,
                               a[kActorRepFlagsLow], c23_set_health(e.actor), seen.x, seen.y, seen.z,
                               cmc != nullptr ? cmc[kCmcMovementMode] : 0xFFu,
                               capsule != nullptr ? static_cast<int>(g_collision_enabled(capsule)) : -1, rep.x, rep.y,
                               rep.z, c132_root_motion_state(e.actor) | (c132_showing_static(e.actor) ? 8u : 0u) | drawn |
                                   (source == 2 ? 64u : 0u),
                               sp.x, sp.y, sp.z);
        used += n > 0 && static_cast<size_t>(n) < sizeof(text) - used ? static_cast<size_t>(n) : 0;
        ++in_row;
        if (in_row == kMoveProbeRow || i + 1 == list.count) {
            log::line("movesync: %s t=%llu n=%u:%s", side(), static_cast<unsigned long long>(now), list.count, text);
            used = 0;
            in_row = 0;
            text[0] = '\0';
        }
    }
}

void c41_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    c41_held_step(now); // card 436 (the list is empty on a client)
    if (g_c41_trace && now >= g_anim_probe_next_ms) {
        g_anim_probe_next_ms = now + kAnimProbeMs;
        anim_probe(g_get_game_world(engine));
    }
    if (g_c41_trace) {
        c132_off_shot_step(g_get_game_world(engine), now);
    }
    if (g_c41_trace && now >= g_move_probe_next_ms) {
        g_move_probe_next_ms = now + kMoveProbeMs;
        move_probe(g_get_game_world(engine), now);
    }
    if (!g_c41_trace || now < g_c41.next_dump_ms) {
        return;
    }
    g_c41.next_dump_ms = now + kC41DumpMs;
    c41_dump("trace totals", g_get_game_world(engine));
    c41_relay_totals("trace totals");
}

// Card 132: the client's per-frame significance update. On the host ASTGameplayGameMode::Tick runs it; a client has
// no game mode, so its significance manager kept every registered object at its first significance. Each NPC's
// OnSignificanceChanged then left the skeletal animator in static-mesh mode (SetShowingStaticMesh, called at
// exe+0x519E0A0): arachnids did not animate and their wound shader (blood) never showed. Every other
// OnSignificanceChanged class (gore parts, damageables, pickups, attack system enemies, dropships) shared the gap.
const void* g_significance_world = nullptr; // the world the once-per-map line was written for

constexpr size_t kPcSignificanceNum = 0xA70; // ASTPlayerController significance location count (AddSignificanceLocation)

// Card 132 senior note 3 (hb44): the view UpdateSignificanceManager (exe+0x51408C0) takes for the local PC. Its main
// view is the FTransform at *(PlayerCameraManager + 0x1B8) + 0x1E0 (identity, so the world origin, when that pointer is
// null); the PC's extra significance locations are kept only when they lie farther than a threshold from the
// camera manager's vfunc 0x840 location. Logged every kSignificanceViewMs on both sides with the own pawn's location,
// so a client view left at the spawn point, the pre-rejoin pawn or the origin shows as a distance.
constexpr size_t kCameraSignificanceView = 0x1B8;  // object holding the view transform
constexpr size_t kSignificanceViewTranslation = 0x1E0 + 0x20; // FTransform: rotation quat 0x00, translation 0x20
constexpr ULONGLONG kSignificanceViewMs = 10000;
constexpr uint32_t kSignificanceViewLines = 60; // per map
struct SignificanceView {
    const void* world;
    ULONGLONG next_ms;
    uint32_t lines;
};
SignificanceView g_significance_view{};

void c132_log_significance_view(const void* world, void* pc) {
    const ULONGLONG now = GetTickCount64();
    if (world != g_significance_view.world) {
        g_significance_view = SignificanceView{world, 0, 0};
    }
    if (now < g_significance_view.next_ms || g_significance_view.lines >= kSignificanceViewLines) {
        return;
    }
    g_significance_view.next_ms = now + kSignificanceViewMs;
    ++g_significance_view.lines;
    const void* const camera = read_ptr(pc, kPcCameraManager);
    const void* const holder = camera != nullptr ? read_ptr(camera, kCameraSignificanceView) : nullptr;
    const Vec3 view = holder != nullptr ? read_vec3(holder, kSignificanceViewTranslation) : Vec3{0, 0, 0};
    const void* const pawn = c23_own_pawn(pc);
    const Vec3 at = pawn != nullptr ? actor_location(pawn) : Vec3{0, 0, 0};
    const double dx = view.x - at.x;
    const double dy = view.y - at.y;
    const double dz = view.z - at.z;
    log::line("card132: %s significance view: holder %s (%s) at (%.0f, %.0f, %.0f), own pawn (%.0f, %.0f, %.0f), %.0f uu "
              "apart, %d extra location(s)",
              side(), object_name(holder).text, holder != nullptr ? class_name(holder).text : "null -> identity",
              view.x, view.y, view.z, at.x, at.y, at.z, sqrt(dx * dx + dy * dy + dz * dz),
              read_i32(pc, kPcSignificanceNum));
}

void client_significance_step(void* engine) {
    void* const world = g_client_session && engine != nullptr ? g_get_game_world(engine) : nullptr;
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    if (pc == nullptr || g_update_significance == nullptr) {
        return;
    }
    if (world != g_significance_world) {
        g_significance_world = world;
        void* const manager = g_significance_get != nullptr ? g_significance_get(world) : nullptr;
        log::line("card132: client significance: manager %p, updated each frame with local PC %s as the game mode",
                  manager, object_name(pc).text);
    }
    // c423b141a client crash: after the rejoin LoadMap the local PC is for a moment the engine's plain
    // PlayerController, smaller than an ASTPlayerController. The view line read its significance location count
    // (+0xA70) past the object's end into an unmapped page (ACCESS_VIOLATION at ubwcoop+0x594CB). Every c423
    // client log since b130 has this PC twice; earlier runs read mapped heap. UpdateSignificanceManager reads the
    // same ASTPlayerController fields, so neither runs until the game's own PC is there.
    if (!is_a(pc, g_st_pc_class())) {
        return;
    }
    c132_log_significance_view(world, pc);
    g_update_significance(pc);
}
const void* g_host_significance_world = nullptr;

CombatTrackerTickFn g_orig_c321_combat_tick = nullptr; // card 321
namespace {
constexpr size_t kGameModeCombatLevel = 0x460; // ASTGameplayGameMode: combat level byte (0/1/2)
uint8_t g_c321_level = 0;                      // the last level logged (the game starts at 0)
// b226 card 321 client music: USTMusicManager's TickMusicModeManager reads the level from the game mode, which
// the client lacks (GetSTGameplayGameMode null: the tick returns before the mode check), so the client's music
// never leaves level 0. The host sends its level on change (and in join replay); the client's tick post-hook
// does what the host's tick does in auto mode: write the level and call SetAndPlayMusic.
constexpr size_t kMusicMode = 0x5c;  // USTMusicManager: music mode byte (1 = auto, combat level driven)
constexpr size_t kMusicLevel = 0x5f; // USTMusicManager: the level the music last played for
constexpr size_t kMusicAutoFlag = 0x60; // USTMusicManager: the bool the host's tick passes to SetToAuto
constexpr uint8_t kMusicModeNone = 0;   // the mode before SetToAuto
constexpr uint8_t kMusicModeAuto = 1;
// b228 (c245b227a: one client music row, then none; a TriggerMusic left the mode off auto): the host's tick runs its
// mode 4 and timed-mode branches after the same game-mode check, so the client never returned to auto. These are
// the fields those branches read (TickMusicModeManager 0x051083E0).
constexpr uint8_t kMusicModeWatch = 4;     // mode 4: back to auto once the watched sound stops
constexpr size_t kMusicWatched = 0x70;     // mode 4: the watched object (null: back to auto)
constexpr size_t kMusicWatchedIface = 0x240; // its interface; vtable +0xA8 returns true while it plays
constexpr size_t kMusicWatchedPlaying = 0xA8;
constexpr size_t kMusicTrigger = 0x5e;     // the trigger type the mode came from
constexpr size_t kMusicTimed = 0x61;       // timed modes: the timer runs
constexpr size_t kMusicPaused = 0x88;      // timed modes: the timer is held
constexpr size_t kMusicTimer = 0x64;       // timed modes: seconds left
constexpr uint8_t kMusicTriggerChain = 7;  // a timed trigger 7 ends in TriggerMusic(8, 0, false), not auto
constexpr uint8_t kMusicTriggerNext = 8;
constexpr uint32_t kC321MaxLevel = 2;
uint8_t g_c321_host_level = 0; // client: the host's level, last received
} // namespace
MusicTickFn g_orig_c321_music_tick = nullptr; // card 321
MusicPlayFn g_c321_set_and_play = nullptr;    // card 321
MusicAutoFn g_c321_set_to_auto = nullptr;     // card 321 b227
MusicTriggerFn g_c321_trigger_music = nullptr; // card 321 b228

// Host: the current level to one client PC (join replay, or a change).
void c321_level_send(void* pc) {
    if (g_client_session || pc == nullptr || !c26_is_remote_pc(pc)) {
        return;
    }
    const bool ok = chan_to_client(pc, ChanOp::CombatLevel, g_c321_level);
    log::line("%scard321: host sent combat level %u to %s", ok ? "" : "WARN ", static_cast<unsigned>(g_c321_level),
              object_name(pc).text);
}

// Client: the host's level, applied by the next music tick.
void c321_level_receive(void*, uint32_t level) {
    if (!g_client_session) {
        return;
    }
    if (level > kC321MaxLevel) {
        log::line("WARN card321: client host combat level %u out of range, ignored", level);
        return;
    }
    if (g_c321_host_level != level) {
        log::line("card321: client t=%llu host combat level %u -> %u", static_cast<unsigned long long>(GetTickCount64()),
                  static_cast<unsigned>(g_c321_host_level), level);
    }
    g_c321_host_level = static_cast<uint8_t>(level);
}

namespace {
// Client: the host tick's non-auto branches, as the host runs them (the client's tick returned before them).
void c321_client_modes(uint8_t* m, float dt) {
    if (m[kMusicMode] == kMusicModeWatch) {
        uint8_t* const watched = static_cast<uint8_t*>(read_ptr(m, kMusicWatched));
        using PlayingFn = bool (*)(void* iface);
        void* const iface = watched != nullptr ? watched + kMusicWatchedIface : nullptr;
        const bool playing = iface != nullptr && reinterpret_cast<PlayingFn>(
                                                     (*reinterpret_cast<void* const* const*>(iface))[kMusicWatchedPlaying / sizeof(void*)])(iface);
        if (!playing) {
            log::line("card321: client t=%llu music mode 4 -> auto", static_cast<unsigned long long>(GetTickCount64()));
            g_c321_set_to_auto(m, true);
        }
        return;
    }
    if (m[kMusicMode] == kMusicModeAuto || m[kMusicTimed] == 0 || m[kMusicPaused] != 0) {
        return;
    }
    float timer = 0.0F;
    memcpy(&timer, m + kMusicTimer, sizeof(timer));
    timer -= dt;
    memcpy(m + kMusicTimer, &timer, sizeof(timer));
    if (timer > 0.0F) {
        return;
    }
    log::line("card321: client t=%llu music mode %u (trigger %u) timer done", static_cast<unsigned long long>(GetTickCount64()),
              static_cast<unsigned>(m[kMusicMode]), static_cast<unsigned>(m[kMusicTrigger]));
    if (m[kMusicTrigger] == kMusicTriggerChain) {
        m[kMusicTimed] = 0;
        memset(m + kMusicTimer, 0, sizeof(float));
        g_c321_trigger_music(m, kMusicTriggerNext, 0.0F, false);
        return;
    }
    g_c321_set_to_auto(m, m[kMusicAutoFlag] != 0);
}
} // namespace

void detour_c321_music_tick(void* music, float dt) {
    g_orig_c321_music_tick(music, dt);
    if (!g_client_session || music == nullptr || g_c321_set_and_play == nullptr || g_c321_set_to_auto == nullptr ||
        g_c321_trigger_music == nullptr) {
        return;
    }
    uint8_t* const m = static_cast<uint8_t*>(music);
    // b227 (c245b226a: 0 client music rows): the client's mode stays 0, because the host's tick calls SetToAuto in
    // mode 0 only after the game-mode check the client fails. SetToAuto is client-safe (null game mode: level 0,
    // mode 1, then SetAndPlayMusic), so the client does the same here.
    if (m[kMusicMode] == kMusicModeNone) {
        log::line("card321: client t=%llu music mode 0 -> auto", static_cast<unsigned long long>(GetTickCount64()));
        g_c321_set_to_auto(music, m[kMusicAutoFlag] != 0);
    }
    c321_client_modes(m, dt);
    if (m[kMusicMode] != kMusicModeAuto || m[kMusicLevel] == g_c321_host_level) {
        return;
    }
    log::line("card321: client t=%llu music combat level %u -> %u", static_cast<unsigned long long>(GetTickCount64()),
              static_cast<unsigned>(m[kMusicLevel]), static_cast<unsigned>(g_c321_host_level));
    m[kMusicLevel] = g_c321_host_level;
    g_c321_set_and_play(music);
}

void detour_c321_combat_tick(void* game_mode, float dt) {
    g_orig_c321_combat_tick(game_mode, dt);
    if (game_mode == nullptr) {
        return;
    }
    const uint8_t level = *(static_cast<const uint8_t*>(game_mode) + kGameModeCombatLevel);
    if (level == g_c321_level) {
        return;
    }
    log::line("card321: %s t=%llu combat level %u -> %u", side(), static_cast<unsigned long long>(GetTickCount64()),
              static_cast<unsigned>(g_c321_level), static_cast<unsigned>(level));
    g_c321_level = level;
    void* const world = g_client_session ? nullptr : g_actor_get_world(game_mode);
    for (int32_t i = 1; world != nullptr && i < kC17Slots; ++i) {
        c321_level_send(g_get_player_controller(world, i));
    }
}

void detour_c132_update_significance(void* game_mode) {
    void* const world = !g_client_session && game_mode != nullptr ? g_actor_get_world(game_mode) : nullptr;
    void* const host_pc = world != nullptr ? g_get_player_controller(world, 0) : nullptr;
    if (host_pc == nullptr || g_add_significance_location == nullptr || !is_a(host_pc, g_st_pc_class())) {
        g_update_significance(game_mode); // +0xA70 is an ASTPlayerController field (c132s: the client's crash)
        return;
    }
    c132_log_significance_view(world, host_pc);
    const int32_t held = read_i32(host_pc, kPcSignificanceNum);
    int added = 0;
    int full = 0;
    for (int s = 1; s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        const void* const pawn = pc != nullptr && c26_is_remote_pc(pc) ? read_ptr(pc, kPcAcknowledgedPawn) : nullptr;
        if (pawn == nullptr) {
            continue;
        }
        const Vec3 at = actor_location(pawn);
        const bool ok = g_add_significance_location(host_pc, &at);
        added += ok ? 1 : 0;
        full += ok ? 0 : 1;
    }
    g_update_significance(game_mode);
    // The added views last for this call only: the count goes back, and the game's own entries stay as they were.
    *reinterpret_cast<int32_t*>(static_cast<char*>(host_pc) + kPcSignificanceNum) = held;
    if (added != 0 && world != g_host_significance_world) {
        g_host_significance_world = world;
        log::line("card132: host significance: %d remote pawn view(s) added to %d game view location(s), %d not "
                  "added (array full)", added, held, full);
    }
}

NpcDamageFn g_orig_c132_npc_damage = nullptr;
constexpr uint32_t kC132DamageLines = 400;
uint32_t g_c132_damage_lines = 0;

void detour_c132_npc_damage(void* npc, const void* damage_event) {
    if (g_c132_damage_lines < kC132DamageLines) {
        ++g_c132_damage_lines;
        log::line("card132: %s t=%llu npc damage event %s %p (caller exe+0x%llX)", side(),
                  static_cast<unsigned long long>(GetTickCount64()), object_name(npc).text, npc,
                  exe_rva(_ReturnAddress()));
    }
    g_orig_c132_npc_damage(npc, damage_event);
}

constexpr const char* kC42KindName[kC42Kinds] = {"StickySprite",    "StickyStaticMesh",    "StickySkeletalMesh",
                                                 "NonStickySprite", "NonStickyStaticMesh", "NonStickySkeletalMesh"};
constexpr uint32_t kC42GibLines = 48;
constexpr uint32_t kC42ChunkLines = 64;
constexpr uint32_t kC42ResponseLines = 256;
constexpr ULONGLONG kC42TrackMs = 100;      // chunk track sample period
constexpr ULONGLONG kC42TrackMaxMs = 60000; // a chunk still there after this is no longer followed
constexpr double kC42TrackStill = 1.0;      // uu per sample: a chunk that moved less has stopped
constexpr uint32_t kC42HostLines = 48;
constexpr int32_t kC42PartMax = 16;      // parts read per gore response
constexpr int32_t kC42SpawnBudget = 32;  // chunk spawn attempts per gore response
constexpr uint32_t kC42Builds = 3;       // pooler builds per kind per map; a rebuild leaks the collected pool's actors
constexpr uint32_t kC42SuperDepth = 8;
constexpr int32_t kC42MapMax = 16;       // GameMode pool map elements logged
constexpr uint32_t kC42NearCells = 2;    // a GUID match further than this from the sent cell counts as wrong
constexpr size_t kGmPoolers = 0x348;     // ASTGameplayGameMode: six pooler pointers (DeferredBeginPlay)
constexpr size_t kGmPoolMap = 0x3A0;     // TMap<UClass* data class, pooler>: element stride kGmPoolMapStride
constexpr size_t kGmPoolMapStride = 0x18; // key +0x00, value +0x08
constexpr size_t kVtPoolConfig = 0x2B0;  // pooler: config object; +0x10 points at the int32 pool count
constexpr size_t kVtPoolSpawn = 0x2B8;   // pooler: spawn (data class, location, rotation, float, null) -> part
constexpr size_t kVtPartLaunch = 0x750;  // gore part: launch (A, B, C)
constexpr size_t kPartStride = 0xC8;     // FSTGoreResponsePart
constexpr size_t kPartData = 0x00;       // UClass* data class
constexpr size_t kPartOffset = 0x08;     // FVector added to the response location
constexpr size_t kPartMin = 0x20;        // int32 parts always spawned
constexpr size_t kPartExtra = 0x24;      // int32 more parts, each rolled against kPartChance
constexpr size_t kPartChance = 0x28;     // int32 percent
constexpr size_t kPartA = 0x30;          // FVector low, high at +0x18 (each launch vector is a per-axis lerp)
constexpr size_t kPartC = 0x60;          // plus the response rotation
constexpr size_t kPartB = 0x90;
constexpr size_t kPartRange = 0x18;
constexpr size_t kPartFloat = 0xC0;      // float low, high at +4: the spawn call's float

using C42PoolSpawnFn = void* (*)(void* pooler, void* const* data_cls, const Vec3* loc, const Vec3* rot, float f,
                                 void* extra);
using C42LaunchFn = void (*)(void* part, const Vec3* a, const Vec3* b, const Vec3* c);

using C42ConfigFn = void* (*)(void* pooler);
StaticClassFn g_c42_pooler_cls[kC42Kinds] = {};
StaticClassFn g_c42_part_cls[kC42Kinds] = {};
StaticClassFn g_c42_data_cls[kC42Kinds] = {};
C42InitFn g_pooler_init = nullptr; // USTActorPoolerMultiplatformBase::Initialise

struct C42Gib {
    uint32_t sent_guid, sent_cell;                 // host: Gib signals by route
    uint32_t recv_guid, recv_cell, guid_miss;      // client: signals by route; GUIDs that resolved to no NPC
    uint32_t wrong, cell_differs, repeat, fallback; // client: match checks
    uint32_t lines;
};
struct C42Chunks {
    C31Weak pooler[kC42Kinds]; // client pools, outer = the PC
    void* held[kC42Kinds];     // card 140: the pool in the game instance's ReferencedObjects, or null
    C31Weak gi;                // the game instance that holds them
    uint32_t builds[kC42Kinds];
    uint32_t pool_lost;        // card 140: a held pool freed anyway (should stay 0)
    uint32_t responses, parts, attempts, spawned, null_spawn, no_kind, no_pooler, lines, host_lines;
    uint32_t rng;
    bool host_logged;
    // Client: the last chunk of the latest response, followed until it is gone (for the hit-chunk shot).
    void* last_chunk;       // set by c42_spawn_one, read at once by c42_client_chunks
    C31Weak track;
    ULONGLONG track_ms, track_next;
    Vec3 track_at;
    bool track_settled;
    uint32_t responses_logged;
};
C42Gib g_c42g{};
C42Chunks g_c42c{}; // game thread; both reset per map by c42_arm_map

uint32_t c42_rand() { // xorshift32, 15 bits like rand()
    uint32_t x = g_c42c.rng != 0 ? g_c42c.rng : 0x2545F491u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_c42c.rng = x;
    return x & 0x7FFFu;
}

double c42_unit() {
    return static_cast<double>(c42_rand()) / 32768.0;
}

// Per axis: low + t * (high - low), each axis its own t (the game's three rand() calls).
Vec3 c42_lerp3(const uint8_t* low) {
    const Vec3 a = read_vec3(low, 0);
    const Vec3 b = read_vec3(low, kPartRange);
    return Vec3{a.x + c42_unit() * (b.x - a.x), a.y + c42_unit() * (b.y - a.y), a.z + c42_unit() * (b.z - a.z)};
}

// Host: one Gib frame to each remote PC, naming the NPC by `guid` when it is usable, else by cell. `cls` and `name`
// label the row (the actor may be gone: card 269's owed gib falls back to the cell).
uint32_t c42_send_route(void* world, uint64_t guid, uint32_t cell, const char* cls, const char* name) {
    const bool by_cell = guid == 0 || guid >= kC41GuidMax;
    const ChanArgs args{{by_cell ? 0u : static_cast<uint32_t>(guid & kC41GuidMask),
                         by_cell ? 0u : static_cast<uint32_t>((guid >> kC41GuidBits) & kC41GuidMask), cell}};
    uint32_t sent = 0;
    for (int s = 0; s < kC17Slots; ++s) {
        sent += chan_to_client_args(g_get_player_controller(world, s), ChanOp::Gib, args) ? 1 : 0;
    }
    g_c42g.sent_guid += sent != 0 && !by_cell ? 1 : 0;
    g_c42g.sent_cell += sent != 0 && by_cell ? 1 : 0;
    if (sent != 0 && note_event_cap(g_c42g.lines, kC42GibLines, "card042 Gib routes")) {
        log::line("card042: host Gib %s %s %s guid 0x%llx cell %u sent to %u PCs", cls, name,
                  by_cell ? "by cell" : "by guid", static_cast<unsigned long long>(guid), cell, sent);
    }
    return sent;
}

// Host: one Gib frame to each remote PC, naming `owner` by NetGUID when it has a usable one.
uint32_t c42_send_gib(void* world, const void* owner, uint32_t cell) {
    return c42_send_route(world, c41_guid(owner), cell, class_name(owner).text, object_name(owner).text);
}

// ---- Card 269: a Gib for an NPC with no NetGUID yet is owed (the sprite rows' pattern) ----
// c41_guid is 0 when the overkilled actor has no NetGUID in the first PC's package map (4 of 9 chariot Gib rows in
// card 243 run 9); by cell the client may gib a neighbour. The Gib waits here and goes by guid once the actor has one;
// when the actor goes first, or after kC42OwedMaxMs, it goes by cell as before.
constexpr int32_t kC42Owed = 32;
constexpr ULONGLONG kC42OwedMaxMs = 2000; // the overkilled actor is destroyed within about 1 s
constexpr uint32_t kC42OwedLines = 40;    // per map
struct C42OwedGib {
    C31Weak actor;
    uint32_t cell;
    ULONGLONG at;
    ObjectName cls, name; // the actor's, for the row after it is gone
};
struct C42Owed {
    C42OwedGib e[kC42Owed];
    int32_t n, peak;
    uint32_t added, by_guid, by_cell, full, lines;
};
C42Owed g_c42o{}; // game thread

// Host, from c39_send_gib: true when the Gib is owed (no NetGUID yet, a free entry); false: send it now.
bool c42_owe_gib(const void* owner, uint32_t cell) {
    if (c41_guid(owner) != 0) {
        return false;
    }
    if (g_c42o.n >= kC42Owed) {
        ++g_c42o.full;
        log::line("WARN card269: host Gib %s owed list full (%d), sent by cell now", object_name(owner).text, kC42Owed);
        return false;
    }
    C42OwedGib& e = g_c42o.e[g_c42o.n++];
    e = C42OwedGib{};
    c31_weak_set(e.actor, const_cast<void*>(owner));
    e.cell = cell;
    e.at = GetTickCount64();
    e.cls = class_name(owner);
    e.name = object_name(owner);
    ++g_c42o.added;
    g_c42o.peak = g_c42o.n > g_c42o.peak ? g_c42o.n : g_c42o.peak;
    if (note_event_cap(g_c42o.lines, kC42OwedLines, "card269 owed Gib")) {
        log::line("card269: host Gib %s %s owed: no NetGUID yet, cell %u", e.cls.text, e.name.text, cell);
    }
    return true;
}

// Host, per tick while a Gib is owed: by guid once the actor has one; by cell when it is gone or waited too long.
void c42_owed_step(void* engine, ULONGLONG now) {
    void* const world = g_get_game_world(engine);
    for (int32_t i = g_c42o.n - 1; world != nullptr && i >= 0; --i) {
        C42OwedGib& e = g_c42o.e[i];
        void* const actor = c31_weak_get(e.actor);
        const uint64_t guid = actor != nullptr ? c41_guid(actor) : 0;
        const bool late = now - e.at >= kC42OwedMaxMs;
        if (guid == 0 && actor != nullptr && !late) {
            continue;
        }
        const uint32_t sent = c42_send_route(world, guid, e.cell, e.cls.text, e.name.text);
        g_c42o.by_guid += guid != 0 ? 1u : 0u;
        g_c42o.by_cell += guid != 0 ? 0u : 1u;
        if (note_event_cap(g_c42o.lines, kC42OwedLines, "card269 owed Gib")) {
            log::line("card269: host owed Gib %s %s sent %s guid 0x%llx cell %u to %u PCs, %llu ms after the overkill%s",
                      e.cls.text, e.name.text, guid != 0 ? "by guid" : "by cell", static_cast<unsigned long long>(guid),
                      e.cell, sent, static_cast<unsigned long long>(now - e.at),
                      guid != 0 ? "" : actor == nullptr ? " (the actor went first)" : " (no NetGUID in time)");
        }
        g_c42o.e[i] = g_c42o.e[--g_c42o.n];
    }
}

bool c42_near_cell(const void* actor, uint32_t cell) {
    const uint32_t c = c39_cell(actor_location(actor));
    return c39_axis_gap(c >> kC39CellBits, cell >> kC39CellBits) <= kC42NearCells &&
           c39_axis_gap(c & kC39CellMask, cell & kC39CellMask) <= kC42NearCells;
}

void c42_gib_shots(void* pc, const void* actor); // the test's client screenshots

// Client: the host's Gib frame. The GUID names the NPC; the cell route is the fallback and, when both resolve, the
// cross-check (a GUID match far from the sent cell is wrong; a different cell pick is what the GUID fixed).
void c42_receive_gib(void* pc, const ChanArgs& args) {
    ++g_c39.received;
    const uint32_t cell = args.v[2];
    const bool by_guid = (args.v[0] | args.v[1]) != 0;
    void* const guid_npc = by_guid ? c41_resolve(pc, args, false) : nullptr;
    void* const cell_npc = c41_resolve(pc, ChanArgs{{cell, 0, 0}}, true);
    const bool repeat = guid_npc != nullptr && c39_was_gibbed(guid_npc);
    g_c42g.recv_guid += by_guid ? 1 : 0;
    g_c42g.recv_cell += by_guid ? 0 : 1;
    g_c42g.guid_miss += by_guid && guid_npc == nullptr ? 1 : 0;
    g_c42g.wrong += guid_npc != nullptr && !c42_near_cell(guid_npc, cell) ? 1 : 0;
    g_c42g.cell_differs += guid_npc != nullptr && cell_npc != guid_npc ? 1 : 0;
    g_c42g.repeat += repeat ? 1 : 0;
    g_c42g.fallback += guid_npc == nullptr && cell_npc != nullptr ? 1 : 0;
    if (note_event_cap(g_c42g.lines, kC42GibLines, "card042 Gib routes")) {
        log::line("card042: client Gib guid 0x%llx cell %u: guid route %s %s, cell route %s %s%s%s",
                  static_cast<unsigned long long>(args.v[0] | (static_cast<uint64_t>(args.v[1]) << kC41GuidBits)),
                  cell, guid_npc != nullptr ? class_name(guid_npc).text : (by_guid ? "unmapped" : "none sent"),
                  guid_npc != nullptr ? object_name(guid_npc).text : "", cell_npc != nullptr ? class_name(cell_npc).text
                                                                                           : "no NPC",
                  cell_npc != nullptr ? object_name(cell_npc).text : "",
                  guid_npc != nullptr && cell_npc != guid_npc ? " (routes differ)" : "",
                  repeat ? " -- already gibbed, skipped" : "");
    }
    if (repeat) {
        return;
    }
    void* const actor = guid_npc != nullptr ? guid_npc : cell_npc;
    if (g_c42_test && actor != nullptr) {
        c42_gib_shots(pc, actor);
    }
    c39_gib_actor(actor, cell, guid_npc != nullptr ? "by guid" : (by_guid ? "by cell (guid unmapped)" : "by cell"));
}

// The pool kind of a gore part data class: it or one of its supers is one of the six native data classes; else -1.
int32_t c42_kind(const void* cls) {
    const void* c = cls;
    for (uint32_t depth = 0; depth < kC42SuperDepth && c != nullptr; ++depth) {
        for (uint32_t k = 0; k < kC42Kinds; ++k) {
            if (g_c42_data_cls[k] != nullptr && c == g_c42_data_cls[k]()) {
                return static_cast<int32_t>(k);
            }
        }
        c = read_ptr(c, kStructSuper);
    }
    return -1;
}

uint32_t c42_replicates(const void* cls) {
    const void* const cdo = cls != nullptr ? read_ptr(cls, kClassDefaultObject) : nullptr;
    return cdo != nullptr && (static_cast<const uint8_t*>(cdo)[kActorRepFlags] & kRepBit) != 0 ? 1u : 0u;
}

// Client: kind k's pool, built on first use (outer and owner = the PC). Card 140 (c140h): nothing rooted it, so GC
// collected it, the weak pointer read null and the pool was rebuilt; past kC42Builds the host's gore parts failed to
// spawn (36 in one soak). So a built pool is held in the game instance's ReferencedObjects until the next LoadMap
// (c42_release_pools). The weak pointer stays the guard: a held pool freed anyway is counted in pool_lost.
void* c42_pooler(uint32_t k, void* pc) {
    void* const live = g_weak_get(&g_c42c.pooler[k]);
    if (live != nullptr) {
        return live;
    }
    if (g_c42c.held[k] != nullptr) {
        ++g_c42c.pool_lost;
        g_c42c.held[k] = nullptr;
        log::line("WARN card140: client gore pool %s freed while held (%u so far)", kC42KindName[k], g_c42c.pool_lost);
    }
    if (g_c42c.builds[k] >= kC42Builds || g_pooler_init == nullptr || g_c42_pooler_cls[k] == nullptr ||
        g_c42_part_cls[k] == nullptr) {
        return nullptr;
    }
    ++g_c42c.builds[k];
    void* const pooler = c31_construct(g_c42_pooler_cls[k](), pc);
    if (pooler == nullptr) {
        return nullptr;
    }
    const auto config = reinterpret_cast<C42ConfigFn>(read_ptr(read_ptr(pooler, 0), kVtPoolConfig));
    const void* const cfg = config(pooler);
    const auto* const want = cfg != nullptr ? static_cast<const int32_t*>(read_ptr(cfg, 0x10)) : nullptr;
    const int32_t asked = want != nullptr ? *want : 0;
    const int32_t count = asked < 1 ? 1 : (asked > kC42PoolMax ? kC42PoolMax : asked);
    void* const part_cls = g_c42_part_cls[k]();
    g_pooler_init(pooler, part_cls, count, pc);
    g_weak_assign(&g_c42c.pooler[k], &pooler);
    void* const world = g_actor_get_world(pc);
    void* const gi = world != nullptr ? read_ptr(world, kWorldOwningGameInstance) : nullptr;
    if (gi != nullptr && g_gi_register_ref != nullptr) {
        g_gi_register_ref(gi, pooler);
        c31_weak_set(g_c42c.gi, gi);
        g_c42c.held[k] = pooler;
    } else {
        log::line("WARN card140: client gore pool %s not held: no game instance", kC42KindName[k]);
    }
    log::line("card042: client pool %s build %u: %s %p, %d actors (config %d), owner %s; part class %s bReplicates %u",
              kC42KindName[k], g_c42c.builds[k], class_name(pooler).text, pooler, count, asked, class_name(pc).text,
              object_name(part_cls).text, c42_replicates(part_cls));
    return pooler;
}

// Card 137 step 6b: client. One host part, exactly: the data class's pool kind, spawned at the host's location with the
// host's float and launched with the host's A/B/C (gib_c137.cpp). Null when there is no kind, no pool or no spawn.
void* c42_spawn_exact(void* pc, void* data_cls, const Vec3& loc, float f, const Vec3& a, const Vec3& b,
                      const Vec3& c) {
    const int32_t k = c42_kind(data_cls);
    void* const pooler = k >= 0 ? c42_pooler(static_cast<uint32_t>(k), pc) : nullptr;
    if (pooler == nullptr) {
        return nullptr;
    }
    const auto spawn = reinterpret_cast<C42PoolSpawnFn>(read_ptr(read_ptr(pooler, 0), kVtPoolSpawn));
    const Vec3 zero{};
    void* const actor = spawn(pooler, &data_cls, &loc, &zero, f, nullptr);
    if (actor != nullptr) {
        reinterpret_cast<C42LaunchFn>(read_ptr(read_ptr(actor, 0), kVtPartLaunch))(actor, &a, &b, &c);
    }
    return actor;
}

// Client: one chunk from the pool, then its launch (the host body's two virtual calls, same arguments).
bool c42_spawn_one(void* pooler, void* data_cls, const uint8_t* part, const Vec3& loc, const Vec3& rot) {
    ++g_c42c.attempts;
    const auto spawn = reinterpret_cast<C42PoolSpawnFn>(read_ptr(read_ptr(pooler, 0), kVtPoolSpawn));
    const Vec3 zero{};
    const float low = read_f32(part, kPartFloat);
    const float f = low + static_cast<float>(c42_unit()) * (read_f32(part, kPartFloat + 4) - low);
    void* const actor = spawn(pooler, &data_cls, &loc, &zero, f, nullptr);
    if (actor == nullptr) {
        ++g_c42c.null_spawn;
        return false;
    }
    const Vec3 a = c42_lerp3(part + kPartA);
    const Vec3 b = c42_lerp3(part + kPartB);
    const Vec3 c0 = c42_lerp3(part + kPartC);
    const Vec3 c{c0.x + rot.x, c0.y + rot.y, c0.z + rot.z};
    reinterpret_cast<C42LaunchFn>(read_ptr(read_ptr(actor, 0), kVtPartLaunch))(actor, &a, &b, &c);
    ++g_c42c.spawned;
    g_c42c.last_chunk = actor;
    if (note_event_cap(g_c42c.lines, kC42ChunkLines, "card042 client chunks")) {
        const Vec3 now = actor_location(actor);
        log::line("card042: client chunk %s %s at (%.0f, %.0f, %.0f) data %s float %.2f, launch A (%.0f, %.0f, %.0f) "
                  "B (%.0f, %.0f, %.0f) C (%.0f, %.0f, %.0f)", class_name(actor).text, object_name(actor).text, now.x,
                  now.y, now.z, object_name(data_cls).text, f, a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z);
    }
    return true;
}

bool c42_finite(const Vec3& v) {
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

// Client: one FSTGoreResponsePart: Min always, then Extra more each rolled against Chance (multiplier 1). Returns the
// spawn attempts made (at most `budget`).
int32_t c42_part(void* pc, const uint8_t* part, const Vec3& base, const Vec3& rot, int32_t budget) {
    ++g_c42c.parts;
    void* const data_cls = read_ptr(part, kPartData);
    const int32_t k = c42_kind(data_cls);
    void* const pooler = k >= 0 ? c42_pooler(static_cast<uint32_t>(k), pc) : nullptr;
    if (pooler == nullptr) {
        g_c42c.no_kind += k < 0 ? 1 : 0;
        g_c42c.no_pooler += k >= 0 ? 1 : 0;
        if (note_event_cap(g_c42c.lines, kC42ChunkLines, "card042 client chunks")) {
            log::line("WARN card042: client gore part data %s (super %s): %s -- no chunks", object_name(data_cls).text,
                      object_name(data_cls != nullptr ? read_ptr(data_cls, kStructSuper) : nullptr).text,
                      k < 0 ? "no pool kind" : "no pool");
        }
        return 0;
    }
    const int32_t min = read_i32(part, kPartMin);
    const int32_t total = min + read_i32(part, kPartExtra);
    const int32_t n = total < 0 ? 0 : (total > budget ? budget : total);
    const int32_t chance = read_i32(part, kPartChance);
    const Vec3 offset = read_vec3(part, kPartOffset);
    const Vec3 loc = c42_finite(offset) ? Vec3{base.x + offset.x, base.y + offset.y, base.z + offset.z} : base;
    for (int32_t i = 0; i < n; ++i) {
        const int32_t roll = static_cast<int32_t>(static_cast<double>(c42_rand()) * 0.0030519);
        if (i >= min && (roll > 99 ? 100 : roll + 1) > chance) {
            continue;
        }
        c42_spawn_one(pooler, data_cls, part, loc, rot);
    }
    return n;
}

// Client, after the mod's own Gore OnOverkill / OnDeath ran SpawnGoreResponse (which stopped at its GameMode gate):
// the response's chunks. `at` and `rot` are the location and rotation SpawnGoreResponse was called with.
void c42_client_chunks(const void* owner, const void* data, const Vec3& at, const Vec3& rot) {
    if (g_client_session) {
        ++g_c42c.responses; // card 137 step 6b: the host's gib rows bring the parts; the client rolls none
        return;
    }
    const auto* const parts = static_cast<const uint8_t*>(read_ptr(data, kGoreChunks));
    const int32_t num = read_i32(data, kGoreChunks + kArrayNum);
    void* const world = owner != nullptr ? g_actor_get_world(owner) : nullptr;
    void* const pc = world != nullptr ? g_get_player_controller(world, 0) : nullptr;
    if (parts == nullptr || num <= 0 || pc == nullptr) {
        return;
    }
    ++g_c42c.responses;
    const Vec3 offset = read_vec3(data, kGoreOffset);
    const Vec3 base{at.x + offset.x, at.y + offset.y, at.z + offset.z};
    const uint32_t spawned = g_c42c.spawned;
    int32_t budget = kC42SpawnBudget;
    for (int32_t i = 0; i < num && i < kC42PartMax && budget > 0; ++i) {
        budget -= c42_part(pc, parts + static_cast<size_t>(i) * kPartStride, base, rot, budget);
    }
    const bool any = g_c42c.spawned != spawned && g_c42c.last_chunk != nullptr;
    if (any) {
        c31_weak_set(g_c42c.track, g_c42c.last_chunk);
        g_c42c.track_ms = GetTickCount64();
        g_c42c.track_next = g_c42c.track_ms + kC42TrackMs;
        g_c42c.track_at = actor_location(g_c42c.last_chunk);
        g_c42c.track_settled = false;
    }
    // Own cap: the chunk lines run out early in a fight, and the hit-chunk shot waits for this line (hb33 to hb36).
    if (note_event_cap(g_c42c.responses_logged, kC42ResponseLines, "card042 client gore responses")) {
        log::line("card042: client gore response for %s: %d parts, %u chunks spawned at (%.0f, %.0f, %.0f); "
                  "tracked chunk %s at (%.0f, %.0f, %.0f)", object_name(owner).text, num, g_c42c.spawned - spawned,
                  base.x, base.y, base.z, any ? object_name(g_c42c.last_chunk).text : "none", g_c42c.track_at.x,
                  g_c42c.track_at.y, g_c42c.track_at.z);
    }
    g_c42c.last_chunk = nullptr;
}

// Client, per tick: the tracked chunk's first stop ("settled at", where it lies) and its end ("gone": the actor is
// gone or its root hidden, the pool took it back), with the time since its spawn.
void c42_track_tick(ULONGLONG now) {
    if (g_c42c.track_ms == 0 || now < g_c42c.track_next) {
        return;
    }
    g_c42c.track_next = now + kC42TrackMs;
    void* const actor = c31_weak_get(g_c42c.track);
    const auto* const root = actor != nullptr ? static_cast<const uint8_t*>(read_ptr(actor, kActorRootComponent))
                                              : nullptr;
    const unsigned long long age = now - g_c42c.track_ms;
    if (root == nullptr || (root[kCompHiddenByte] & kCompHiddenBit) != 0 || age > kC42TrackMaxMs) {
        log::line("card042: client chunk track %s after %llu ms at (%.0f, %.0f, %.0f)",
                  age > kC42TrackMaxMs ? "still there" : "gone", age, g_c42c.track_at.x, g_c42c.track_at.y,
                  g_c42c.track_at.z);
        g_c42c.track_ms = 0;
        return;
    }
    const Vec3 at = actor_location(actor);
    if (!g_c42c.track_settled && vec_dist(at, g_c42c.track_at) < kC42TrackStill) {
        g_c42c.track_settled = true;
        log::line("card042: client chunk track %s settled at (%.0f, %.0f, %.0f) after %llu ms",
                  object_name(actor).text, at.x, at.y, at.z, age);
    }
    g_c42c.track_at = at;
}

// Host, once per map: the GameMode's six pool slots and its data class -> pool map.
void c42_host_pools(const void* mode) {
    for (uint32_t k = 0; k < kC42Kinds; ++k) {
        const void* const pool = read_ptr(mode, kGmPoolers + 8 * k);
        const void* const part = g_c42_part_cls[k] != nullptr ? g_c42_part_cls[k]() : nullptr;
        log::line("card042: host GameMode pool slot %u (+0x%zx): %s %p (expected %s); part class %s bReplicates %u", k,
                  kGmPoolers + 8 * k, class_name(pool).text, pool,
                  g_c42_pooler_cls[k] != nullptr ? object_name(g_c42_pooler_cls[k]()).text : "?",
                  object_name(part).text, c42_replicates(part));
    }
    const auto* const elems = static_cast<const uint8_t*>(read_ptr(mode, kGmPoolMap));
    const int32_t num = read_i32(mode, kGmPoolMap + kArrayNum);
    log::line("card042: host GameMode +0x3A0 map: %d elements at %p", num, elems);
    for (int32_t i = 0; elems != nullptr && i < num && i < kC42MapMax; ++i) {
        const uint8_t* const e = elems + static_cast<size_t>(i) * kGmPoolMapStride;
        const void* const key = read_ptr(e, 0);
        const int32_t k = c42_kind(key);
        const bool known = k >= 0 && key == g_c42_data_cls[k]();
        const void* const value = read_ptr(e, 8);
        log::line("card042: host   [%d] key %s %p -> %s %p", i, known ? object_name(key).text : "(not a known data class)",
                  key, known ? class_name(value).text : "-", value);
    }
}

// Host, each SpawnGoreResponse: the pool log once per map, then a line per chunk part (data class, map key, kind).
void c42_host_gore(const void* mode, const void* data) {
    if (!g_c42c.host_logged) {
        g_c42c.host_logged = true;
        c42_host_pools(mode);
    }
    const auto* const parts = static_cast<const uint8_t*>(read_ptr(data, kGoreChunks));
    const int32_t num = read_i32(data, kGoreChunks + kArrayNum);
    for (int32_t i = 0; parts != nullptr && i < num && i < kC42PartMax; ++i) {
        const uint8_t* const part = parts + static_cast<size_t>(i) * kPartStride;
        const void* const cls = read_ptr(part, kPartData);
        if (!note_event_cap(g_c42c.host_lines, kC42HostLines, "card042 host gore parts")) {
            return;
        }
        log::line("card042: host gore part %d/%d data %s key (super) %s kind %d min %d extra %d chance %d", i, num,
                  object_name(cls).text, object_name(cls != nullptr ? read_ptr(cls, kStructSuper) : nullptr).text,
                  c42_kind(cls), read_i32(part, kPartMin), read_i32(part, kPartExtra), read_i32(part, kPartChance));
    }
}

void c42_totals(const char* when) {
    uint32_t builds = 0;
    for (uint32_t k = 0; k < kC42Kinds; ++k) {
        builds += g_c42c.builds[k];
    }
    log::line("card042: %s %s: Gib sent by guid %u by cell %u; received by guid %u by cell %u, guid unmapped %u, "
              "cell fallback %u, wrong %u, routes differ %u, repeats %u; chunks: responses %u parts %u attempts %u "
              "spawned %u null %u no kind %u no pool %u, pool builds %u, held pools lost %u", side(), when, g_c42g.sent_guid,
              g_c42g.sent_cell, g_c42g.recv_guid, g_c42g.recv_cell, g_c42g.guid_miss, g_c42g.fallback, g_c42g.wrong,
              g_c42g.cell_differs, g_c42g.repeat, g_c42c.responses, g_c42c.parts, g_c42c.attempts, g_c42c.spawned,
              g_c42c.null_spawn, g_c42c.no_kind, g_c42c.no_pooler, builds, g_c42c.pool_lost);
}

// UBWCOOP_C42_TEST. Host timeline from kC42StartMs after it is first on foot (kC42StageMs offsets): kill the live AI
// troopers; move to the nearest corpse; walk over it; put the client's pawn at the same start; Ping the client to
// walk; spawn two WarriorBugs close together; overkill them kC42SecondKillMs apart; Ping done.
constexpr ULONGLONG kC42StartMs = 20000;
constexpr ULONGLONG kC42StageMs[] = {0, 1500, 2500, 5500, 6000, 10000, 13000, 13400, 18000};
constexpr uint32_t kC42Stages = sizeof(kC42StageMs) / sizeof(kC42StageMs[0]);
constexpr ULONGLONG kC42WalkMs = 2500;      // W held this long
constexpr ULONGLONG kC42WalkRowMs = 100;    // a position row this often
constexpr ULONGLONG kC42WalkSettleMs = 300; // no blocked verdict before this (walk start-up)
constexpr double kC42NearUu = 150.0;        // "at the corpse": the summary's slowest speed is taken within this
constexpr double kC42BugAhead = 900.0;
constexpr double kC42BugSide = 100.0;       // the two bugs 200 uu apart: within one cell of each other
constexpr double kC42ClientSide = 120.0;    // the client walks this far to the side of the host's start
constexpr uint32_t kC42Shots = 4;
constexpr ULONGLONG kC42ShotMs[2] = {250, 900}; // client shots after each of the first two Gib frames
constexpr uint32_t kC42WalkLines = 80;

struct C42Walk {
    Vec3 target, start, last;
    ULONGLONG start_ms, next_ms;
    double max_speed, min_near;
    uint32_t rows, blocked;
    bool active;
};
struct C42Test {
    C42Walk walk;
    Vec3 corpse;
    uint64_t corpse_name;
    uint64_t bug[2]; // spawned bug FNames (re-found by name each use)
    ULONGLONG foot_ms;
    ULONGLONG shot_ms[kC42Shots];
    uint32_t stage, shots_set, walk_lines;
    bool done;
};
C42Test g_c42t{}; // game thread; one run per session

double c42_flat(const Vec3& a, const Vec3& b) {
    return sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

void c42_walk_start(void* pc, const void* pawn, const Vec3& target, ULONGLONG now) {
    C42Walk& w = g_c42t.walk;
    w = C42Walk{target, actor_location(pawn), actor_location(pawn), now, now + kC42WalkRowMs, 0.0, 1e9, 0, 0, true};
    c25_face(pc, w.start, target);
    const bool pressed = autoinput::key_event(pc, L"W", true);
    log::line("card042: %s walk start at (%.0f, %.0f, %.0f) toward corpse (%.0f, %.0f, %.0f) %.0f uu away; W %s", side(),
              w.start.x, w.start.y, w.start.z, target.x, target.y, target.z, c42_flat(w.start, target),
              pressed ? "held" : "NOT sent");
}

void c42_walk_end(void* pc, const Vec3& at) {
    C42Walk& w = g_c42t.walk;
    w.active = false;
    autoinput::key_event(pc, L"W", false);
    const double sx = w.target.x - w.start.x;
    const double sy = w.target.y - w.start.y;
    const bool passed = (at.x - w.target.x) * sx + (at.y - w.target.y) * sy > 0.0;
    log::line("card042: %s walk summary: %s the corpse; travelled %.0f uu, max speed %.0f, slowest within %.0f uu of "
              "the corpse %.0f, blocked rows %u of %u; end (%.0f, %.0f, %.0f) %.0f from it", side(),
              passed ? "PASSED" : "did NOT pass", c42_flat(at, w.start), w.max_speed, kC42NearUu,
              w.min_near < 1e8 ? w.min_near : -1.0, w.blocked, w.rows, at.x, at.y, at.z, c42_flat(at, w.target));
}

// Each tick while walking: a row every kC42WalkRowMs (position, distance, speed, blocked = under half the best speed).
void c42_walk_tick(void* pc, const void* pawn, ULONGLONG now) {
    C42Walk& w = g_c42t.walk;
    if (!w.active || now < w.next_ms) {
        return;
    }
    const Vec3 at = actor_location(pawn);
    const double speed = c42_flat(at, w.last) * 1000.0 / static_cast<double>(kC42WalkRowMs + now - w.next_ms);
    const double dist = c42_flat(at, w.target);
    const bool settled = now >= w.start_ms + kC42WalkSettleMs;
    const bool blocked = settled && speed < 0.5 * w.max_speed;
    w.max_speed = speed > w.max_speed ? speed : w.max_speed;
    w.min_near = settled && dist < kC42NearUu && speed < w.min_near ? speed : w.min_near;
    w.blocked += blocked ? 1 : 0;
    ++w.rows;
    w.last = at;
    w.next_ms = now + kC42WalkRowMs;
    if (note_event_cap(g_c42t.walk_lines, kC42WalkLines, "card042 walk rows")) {
        log::line("card042: %s walk +%llu ms at (%.0f, %.0f, %.0f) corpse %.0f uu, speed %.0f%s", side(),
                  static_cast<unsigned long long>(now - w.start_ms), at.x, at.y, at.z, dist, speed,
                  blocked ? " BLOCKED" : "");
    }
    if (now >= w.start_ms + kC42WalkMs) {
        c42_walk_end(pc, at);
    }
}

// The listed enemy with this FName, or null.
void* c42_find(const C23List& list, uint64_t name) {
    for (uint32_t i = 0; i < list.count; ++i) {
        if (name != 0 && read_u64(list.e[i].actor, kObjectName) == name) {
            return list.e[i].actor;
        }
    }
    return nullptr;
}

const C23List& c42_enemies(const void* world, const void* pawn) {
    static C23List list; // game thread only; too large for the stack
    c23_collect(world, pawn, g_enemy_class(), list);
    return list;
}

// Index of the nearest AI trooper (alive or dead as asked), or -1.
int32_t c42_nearest_trooper(const C23List& list, bool dead) {
    int32_t best = -1;
    for (uint32_t i = 0; i < list.count; ++i) {
        const C23Enemy& e = list.e[i];
        const bool match = c23_trooper(e.actor) && (c23_set_health(e.actor) <= 0.0f) == dead && e.dist >= 0.0;
        if (match && (best < 0 || e.dist < list.e[best].dist)) {
            best = static_cast<int32_t>(i);
        }
    }
    return best;
}

void c42_ping_clients(void* world, uint32_t what) {
    for (int s = 0; s < kC17Slots; ++s) {
        chan_to_client(g_get_player_controller(world, s), ChanOp::Ping, what);
    }
}

// Stage 0: the nearest live AI trooper is the corpse to walk over; every live trooper dies (card 023's clear).
void c42_stage_kill(void* world, void*, const void* pawn) {
    const C23List& list = c42_enemies(world, pawn);
    const int32_t i = c42_nearest_trooper(list, false);
    if (i < 0) {
        log::line("WARN card042: host found no live AI trooper among %u enemies -- no corpse walk", list.count);
        return;
    }
    g_c42t.corpse = list.e[i].loc;
    g_c42t.corpse_name = read_u64(list.e[i].actor, kObjectName);
    log::line("card042: host corpse target %s %s at (%.0f, %.0f, %.0f) %.0f uu away", class_name(list.e[i].actor).text,
              object_name(list.e[i].actor).text, list.e[i].loc.x, list.e[i].loc.y, list.e[i].loc.z, list.e[i].dist);
    c23_clear_troopers(list);
}

// Stage 1: the host moves next to the corpse where it lies now.
void c42_stage_move(void* world, void* pc, const void* pawn) {
    const C23List& list = c42_enemies(world, pawn);
    const void* corpse = c42_find(list, g_c42t.corpse_name);
    const int32_t i = corpse == nullptr ? c42_nearest_trooper(list, true) : -1;
    if (i >= 0) { // the first pick's body is gone (some trooper kinds are destroyed on death): nearest one that stays
        corpse = list.e[i].actor;
        g_c42t.corpse_name = read_u64(corpse, kObjectName);
        log::line("card042: host first corpse is gone; new target %s %s", class_name(corpse).text,
                  object_name(corpse).text);
    }
    if (corpse != nullptr) {
        g_c42t.corpse = actor_location(corpse);
    }
    log::line("card042: host corpse %s at (%.0f, %.0f, %.0f), Health %.1f", corpse != nullptr ? "found" : "NOT found",
              g_c42t.corpse.x, g_c42t.corpse.y, g_c42t.corpse.z, corpse != nullptr ? c23_set_health(corpse) : 0.0f);
    c25_move(const_cast<void*>(pawn), pc, g_c42t.corpse);
}

void c42_stage_walk(void*, void* pc, const void* pawn) {
    c42_walk_start(pc, pawn, g_c42t.corpse, GetTickCount64());
}

// Stage 3: the client's pawn to the host's walk start, kC42ClientSide to the side (the server move corrects it).
void c42_stage_place(void* world, void*, const void*) {
    void* const pc1 = g_get_player_controller(world, 1);
    void* const pawn1 = pc1 != nullptr ? read_ptr(pc1, kControllerPawn) : nullptr;
    const Vec3 s = g_c42t.walk.start;
    const double h = c42_flat(s, g_c42t.corpse);
    const double ux = h > 1.0 ? (g_c42t.corpse.x - s.x) / h : 1.0;
    const double uy = h > 1.0 ? (g_c42t.corpse.y - s.y) / h : 0.0;
    const Vec3 to{s.x - uy * kC42ClientSide, s.y + ux * kC42ClientSide, s.z};
    const bool moved = pawn1 != nullptr && g_set_actor_location(pawn1, &to, false, nullptr, kTeleportPhysics);
    log::line("%scard042: host placed the client pawn %s at (%.0f, %.0f, %.0f): %d", moved ? "" : "WARN ",
              object_name(pawn1).text, to.x, to.y, to.z, moved ? 1 : 0);
}

void c42_stage_ping_walk(void* world, void*, const void*) {
    c42_ping_clients(world, kC42WalkPing);
    log::line("card042: host sent the client walk ping");
}

// Stage 5: two WarriorBugs kC42BugAhead in front of the host, kC42BugSide either side of its view line.
void c42_stage_spawn(void* world, void* pc, const void* pawn) {
    void* const cls = g_static_load_object(g_uclass_class(), nullptr, kC23BugClass, nullptr, kC29LoadFlags, nullptr,
                                           true, nullptr);
    const Vec3 at = actor_location(pawn);
    const double yaw = read_vec3(pc, kControllerControlRotation).y / kRadToDeg;
    for (uint32_t b = 0; b < 2 && cls != nullptr; ++b) {
        const double side = b == 0 ? -kC42BugSide : kC42BugSide;
        const Vec3 loc{at.x + cos(yaw) * kC42BugAhead - sin(yaw) * side,
                       at.y + sin(yaw) * kC42BugAhead + cos(yaw) * side, at.z + 100.0};
        const Vec3 rot{0.0, yaw * kRadToDeg + 180.0, 0.0};
        void* const bug = g_spawn_ai_from_class(world, &cls, nullptr, &loc, &rot, true, nullptr);
        g_c42t.bug[b] = bug != nullptr ? read_u64(bug, kObjectName) : 0;
        log::line("%scard042: host spawned bug %u %s at (%.0f, %.0f, %.0f) cell %u", bug != nullptr ? "" : "WARN ", b,
                  bug != nullptr ? object_name(bug).text : "(null)", loc.x, loc.y, loc.z, c39_cell(loc));
    }
}

void c42_overkill(void* world, const void* pawn, uint32_t b) {
    void* const bug = c42_find(c42_enemies(world, pawn), g_c42t.bug[b]);
    const Vec3 at = bug != nullptr ? actor_location(bug) : Vec3{};
    log::line("%scard042: host overkill bug %u %s at (%.0f, %.0f, %.0f) cell %u guid 0x%llx", bug != nullptr ? "" : "WARN ",
              b, bug != nullptr ? object_name(bug).text : "not found", at.x, at.y, at.z, c39_cell(at),
              static_cast<unsigned long long>(bug != nullptr ? c41_guid(bug) : 0));
    if (bug != nullptr) {
        g_insta_kill(bug, true);
    }
}

void c42_stage_kill0(void* world, void*, const void* pawn) {
    c42_overkill(world, pawn, 0);
}

void c42_stage_kill1(void* world, void*, const void* pawn) {
    c42_overkill(world, pawn, 1);
}

void c42_stage_done(void* world, void*, const void*) {
    c42_totals("test totals");
    c42_ping_clients(world, kC42DonePing);
    g_c42t.done = true;
    log::line("card042: test done");
}

using C42StageFn = void (*)(void* world, void* pc, const void* pawn);
constexpr C42StageFn kC42Stage[kC42Stages] = {c42_stage_kill,  c42_stage_move,  c42_stage_walk,
                                              c42_stage_place, c42_stage_ping_walk, c42_stage_spawn,
                                              c42_stage_kill0, c42_stage_kill1, c42_stage_done};

// Client: the host's walk or done ping.
void c42_ping(void* pc, uint32_t arg) {
    void* const world = g_actor_get_world(pc);
    const void* const pawn = c23_own_pawn(pc);
    if (arg == kC42DonePing) {
        c42_totals("test totals");
        g_c42t.done = true;
        log::line("card042: test done");
        return;
    }
    if (world == nullptr || pawn == nullptr) {
        log::line("WARN card042: client walk ping while not on foot -- no walk");
        return;
    }
    const C23List& list = c42_enemies(world, pawn);
    const int32_t i = c42_nearest_trooper(list, true);
    if (i < 0) {
        log::line("WARN card042: client walk ping: no trooper corpse among %u enemies -- no walk", list.count);
        return;
    }
    const C23Enemy& e = list.e[i];
    log::line("card042: client corpse %s %s at (%.0f, %.0f, %.0f) Health %.1f", class_name(e.actor).text,
              object_name(e.actor).text, e.loc.x, e.loc.y, e.loc.z, c23_set_health(e.actor));
    c42_walk_start(pc, pawn, e.loc, GetTickCount64());
}

// Client: faces the gibbed NPC and schedules two shots, for the first two Gib frames.
void c42_gib_shots(void* pc, const void* actor) {
    const void* const pawn = c23_own_pawn(pc);
    if (g_c42t.shots_set >= kC42Shots / 2 || pawn == nullptr) {
        return;
    }
    c25_face(pc, actor_location(pawn), actor_location(actor));
    const ULONGLONG now = GetTickCount64();
    for (uint32_t j = 0; j < 2; ++j) {
        g_c42t.shot_ms[g_c42t.shots_set * 2 + j] = now + kC42ShotMs[j];
    }
    ++g_c42t.shots_set;
}

void c42_shot_tick(void* pc, ULONGLONG now) {
    for (uint32_t i = 0; i < kC42Shots; ++i) {
        if (g_c42t.shot_ms[i] != 0 && now >= g_c42t.shot_ms[i]) {
            g_c42t.shot_ms[i] = 0;
            request_shot(pc, false);
            log::line("card042: client shot %u requested (chunks %u spawned so far)", i, g_c42c.spawned);
        }
    }
}

void c42_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    if (g_client_session) {
        c42_track_tick(now);
    }
    c137_gib_tick(now); // card 137 step 6b
    c532_tick(now);     // card 532: NpcTarget flush (host), pending rows (client)
    c531_tick(now);     // card 531: GetSaveId cache totals
    c535_tick(now);     // card 535: FindFloor skip totals
    c536_tick(now);     // card 536: FindComponentByClass cache totals
    c538_tick(now);     // card 538: DoesTargetMeetConditions repeat counts
    c539_tick(now);     // card 539: client NPC proxy sweep counts
    c534_tick(now);     // card 534: world query cost rows (diag)
    c545_tick(now);     // card 545: Blueprint Kismet query rows (diag)
    c548_tick(now);     // card 548: player primitive UpdateOverlapsImpl rows (diag)
    c544_tick(engine);  // card 544: dynamic tree padding, once
    if (!g_client_session && g_c42o.n != 0) {
        c42_owed_step(engine, now); // card 269
    }
    if (!g_c42_test) {
        return;
    }
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    const void* const pawn = pc != nullptr ? c23_own_pawn(pc) : nullptr;
    if (pawn == nullptr) {
        return;
    }
    c42_walk_tick(pc, pawn, now);
    if (g_client_session) {
        c42_shot_tick(pc, now);
        return;
    }
    if (g_c42t.foot_ms == 0) {
        g_c42t.foot_ms = now;
        log::line("card042: host on foot; test starts in %llu ms", static_cast<unsigned long long>(kC42StartMs));
    }
    for (uint32_t n = 0; n < kC42Stages && g_c42t.stage < kC42Stages; ++n) {
        if (now < g_c42t.foot_ms + kC42StartMs + kC42StageMs[g_c42t.stage]) {
            return;
        }
        kC42Stage[g_c42t.stage++](world, pc, pawn);
    }
}

// Card 140: client, at LoadMap enter (before the engine collects the old world). The held pools go out of the game
// instance's ReferencedObjects, so they and their outer PC do not keep the old world alive.
void c42_release_pools() {
    void* const gi = c31_weak_get(g_c42c.gi);
    uint32_t released = 0;
    for (uint32_t k = 0; k < kC42Kinds; ++k) {
        if (g_c42c.held[k] != nullptr && gi != nullptr && g_gi_unregister_ref != nullptr) {
            g_gi_unregister_ref(gi, g_c42c.held[k]);
            ++released;
        }
        g_c42c.held[k] = nullptr;
    }
    if (released != 0 || g_c42c.pool_lost != 0) {
        log::line("card140: client gore pools released at LoadMap: %u, %u freed while held this map", released,
                  g_c42c.pool_lost);
    }
}

void c42_arm_map() {
    if (g_c42g.sent_guid + g_c42g.sent_cell + g_c42g.recv_guid + g_c42g.recv_cell + g_c42c.responses != 0) {
        c42_totals("map totals");
    }
    if (g_c42o.added + g_c42o.full != 0) {
        log::line("card269: host owed Gib map totals: owed %u, sent by guid %u, by cell %u, still owed %d, list full "
                  "%u, peak %d of %d", g_c42o.added, g_c42o.by_guid, g_c42o.by_cell, g_c42o.n, g_c42o.full, g_c42o.peak,
                  kC42Owed);
    }
    g_c42o = C42Owed{};
    g_c42g = C42Gib{};
    g_c42c = C42Chunks{}; // the pools' weak pointers go with it; the pools (outer = the old PC) are left to GC
    g_c42t.walk.active = false;
}

} // namespace ubw::hooks
