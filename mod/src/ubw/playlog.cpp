// Card 155: the play-build logs. Every line here is on in the play build and goes through pl_allow, a per-class
// lines-per-second limit with a global cap; what it drops is counted per class and given in the summary. The budget
// is under 30 MB per log per hour (the 2026-11-08 playtest: host 24.0 MB/h, client 15.5 MB/h with the old lines).
//
// Lines (all "playlog: <side> t=<GetTickCount64> ..."):
//   clock    host and client clock offset and round trip, at the client's first Pong and every 60 s (ChanOp::Clock)
//   rtt      the card 026 ping round trips of the last 60 s: count, min, avg, max
//   hit      host: damage to a player pawn (enemies.cpp, card 040's Process detour, player branch)
//   shot     one line per burst (all launches of one shooter in one tick): side c (client own), h (host own) or
//            s (the host's server copy of a client); the client's seq is a real field (card 157 sends it)
//   input    IMC add and remove, SetInputMode, cursor (calls and direct writes), viewport widgets, key presses
//   weapon   equipped weapon changes; ability activations (one per class per pawn per 2 s); ADS with FOV
//   grenade  aim at the own grenade activation; the host's throw (grenades.cpp)
//   door     the card 095 lines through the limiter; the own pawn blocked by a door shown open
//   audio    sounds of a remote player's pawn or weapon (one per sound per 2 s)
//   summary  every 300 s, at map end and at process detach
//
// Hot paths (sound, impact and input hooks) test a pointer before any name or string work: see pl_remote,
// detour_pl_impact and pl_execute.

#include "ubw/hooks_internal.h"
#include "ubw/init.h"

namespace ubw::hooks {

AudioPlayInternalFn g_orig_pl_play_internal = nullptr;
CmcHandleImpactFn g_orig_pl_impact = nullptr;
AddToViewportFn g_orig_pl_add_to_viewport = nullptr;
RemoveFromParentFn g_orig_pl_remove_from_parent = nullptr;
RemoveMappingContextFn g_orig_pl_remove_mapping_context = nullptr;

namespace {

constexpr size_t kPlClasses = static_cast<size_t>(PlClass::Count);
constexpr size_t kPlCounts = static_cast<size_t>(PlCount::Count);
// Lines per second per class.
constexpr uint32_t kPlRate[] = {4, 2, 10, 25, 2, 5, 5, 5, 5, 5, 3, 3, 5, 1, 5, 2};
constexpr const char* kPlClassText[] = {"clock", "rtt", "hit", "shot", "launch", "input", "cursor", "widget",
                                        "weapon", "ability", "ads", "grenade", "door", "door-block", "audio", "fx"};
constexpr const char* kPlCountText[] = {"shots own", "shots copy", "player hits", "downs", "deaths",
                                        "loads", "joins", "rejoins", "mismatches"};
static_assert(std::size(kPlRate) == kPlClasses && std::size(kPlClassText) == kPlClasses);
static_assert(std::size(kPlCountText) == kPlCounts);
constexpr uint32_t kPlGlobalRate = 40;      // lines per second, all classes
constexpr ULONGLONG kPlSummaryMs = 300000;
constexpr ULONGLONG kPlClockMs = 60000;     // clock and rtt
constexpr uint32_t kPlRttRing = 5;          // card 159 reopen: round trips the lowest is taken from (card 340) (c165e: one 2056 ms sample)
constexpr ULONGLONG kPlPawnMs = 500;
constexpr ULONGLONG kPlAdsFovMs = 500;
constexpr ULONGLONG kPlOnceMs = 2000;
constexpr ULONGLONG kPlRemoteMs = 3000;     // a client's remote pawn not seen by card 028 for this long is dropped
constexpr uint32_t kPlOnce = 64;
constexpr uint32_t kPlWidgets = 32;
constexpr uint32_t kPlActions = 48; // the named actions only (pl_action_tabled): one asset each, about 25
constexpr size_t kPlName = 48;
constexpr size_t kPovFov = 0x40;            // FCameraCacheEntry: Timestamp, pad, POV (+0x10): Location, Rotation, FOV +0x30
constexpr size_t kHitResultActor = 0xB8;    // FHitResult::HitObjectHandle (weak), card 040's kC40HitActor
constexpr size_t kInstanceTriggerEvent = 0x12; // FInputActionInstance::TriggerEvent (input_diag.cpp card 016)
constexpr uint8_t kTriggerTriggered = 1;    // ETriggerEvent
constexpr uint8_t kTriggerStarted = 2;
constexpr uint8_t kTriggerCanceled = 8;
constexpr uint8_t kTriggerCompleted = 16;
// A tap reaches the bindings as Triggered, never Started: UEnhancedPlayerInput::ConvertInternalTriggerEvent maps
// StartedAndTriggered to Triggered. So a press is Started, or Triggered after this long without an execute of that
// action (a held action executes every frame; several bindings of one action execute in the same frame).
constexpr ULONGLONG kPlPressGapMs = 100;
constexpr uint8_t kDoorOpen = 1;            // EMoveType: Closed 0, Open 1, Opening 2, Closing 3

enum class PlKey : uint8_t { Unseen, Quiet, Press, Weapon, Ads, Grenade };
struct PlAction {
    const void* action;
    PlKey kind;
    ULONGLONG last_ms; // the last execute of this action (any trigger event)
    char name[kPlName];
};
struct PlOnce {
    uint64_t key;
    ULONGLONG until;
};
struct PlBurst {
    bool open;
    char side;          // c, h or s
    uint64_t tick;
    ULONGLONG t;
    uint32_t seq;
    uint32_t k;
    int ads;            // -1 unknown (server copy)
    long long rl;       // ms since the pawn's last reload activation, -1 none
    Vec3 at;
    Vec3 rot;
    char weapon[kPlName];
};
struct PlRemote {
    C31Weak pawn; // card 183: weak, null once the pawn is destroyed
    ULONGLONG seen_ms;
};

struct PlState {
    ULONGLONG sec_ms;
    uint32_t used[kPlClasses];
    uint32_t used_all;
    uint64_t lines[kPlClasses];
    uint64_t dropped[kPlClasses];
    uint64_t count[kPlCounts];
    uint64_t tick;
    ULONGLONG start_ms, pawns_ms, clock_ms, summary_ms, ads_fov_ms;
    // Card 183: weak pointers, resolved at use (pl_pc, pl_pawn, c31_weak_get). A pawn, PC or weapon destroyed inside
    // the map (a client's Logout, a respawn in place, a mech's destruction, a weapon drop) resolves null at once.
    C31Weak pc[kC17Slots];
    C31Weak pawn[kC17Slots];
    bool mi[kC17Slots];     // ASTPlayerCharacterMI: the MI weapon inventory (pl_weapons)
    bool player[kC17Slots]; // card 198: any ASTPlayerCharacter (the MI or the bug pawn): side and movement
    C31Weak weapon[kC17Slots];
    ULONGLONG reload_ms[kC17Slots];
    PlBurst burst[kC17Slots];
    PlRemote remote[kC17Slots];
    const void* own_cmc;
    uint32_t seq;            // the own shots, both sides; the client's goes on the wire in card 157
    int ads;
    unsigned cursor;
    bool cursor_known;
    const void* widget[kPlWidgets];
    uint32_t widget_n;
    PlAction action[kPlActions];
    uint32_t action_n;
    uint32_t action_full;
    uint32_t action_null; // card 160: executes whose instance had no SourceAction
    PlOnce once[kPlOnce];
    uint32_t clock_n;
    uint32_t rtt_n, rtt_min, rtt_max;
    uint32_t rtt_last; // card 159: the last card 026 round trip (ms), 0 before the first
    uint32_t rtt_ring[kPlRttRing]; // card 159 reopen: the last kPlRttRing round trips, kept across map loads
    uint32_t rtt_ring_n;           // samples written (the ring holds min(n, kPlRttRing))
    uint64_t rtt_sum;
    int32_t move_at, cache_at, door_at, sound_at, atten_at, volume_at, pitch_at; // reflected offsets, -1 unresolved, -2 absent
    int32_t sound_atten_at; // card 162: USoundBase::AttenuationSettings
};

PlState g_pl = [] {
    PlState s{};
    s.ads = 0;
    s.move_at = s.cache_at = s.door_at = s.sound_at = s.atten_at = s.volume_at = s.pitch_at = s.sound_atten_at = -1;
    return s;
}();

// "client" in a joined session; "host" once the local player has a player pawn; "menu" before that (the front end,
// where neither role exists yet: a joining process is standalone until its LoadMap of the server's map).
const char* pl_side() {
    return g_client_session ? "client" : g_pl.player[0] ? "host" : "menu";
}

unsigned long long pl_now() {
    return static_cast<unsigned long long>(GetTickCount64());
}

// The reflected offset of `name` on `object`'s class, cached in `at` (-2 when absent, logged once).
int32_t pl_offset(int32_t& at, const void* object, const char* name) {
    if (at == -1 && object != nullptr) {
        const uint8_t* const prop = prop_find_prop(read_ptr(object, kObjectClass), name);
        at = prop != nullptr ? read_i32(prop, kPropertyOffset) : -2;
        log::line("%splaylog: %s reflected %s on %s: %s +0x%X", at < 0 ? "WARN " : "", pl_side(), name,
                  class_name(object).text, at < 0 ? "absent" : "at", at < 0 ? 0 : at);
    }
    return at;
}

// Card 418: a reflected float or pointer on a component, read at its cached offset (1 or null when absent).
float pl_component_f32(const void* comp, int32_t at) {
    return at >= 0 ? read_f32(comp, static_cast<size_t>(at)) : 1.0f;
}

const void* pl_component_ptr(const void* comp, int32_t at) {
    return at >= 0 ? read_ptr(comp, static_cast<size_t>(at)) : nullptr;
}

// Card 183: slot s's PC and pawn, null when there is none or it was destroyed since the last pl_pawns.
void* pl_pc(int32_t s) {
    return c31_weak_get(g_pl.pc[s]);
}

const void* pl_pawn(int32_t s) {
    return c31_weak_get(g_pl.pawn[s]);
}

// Slot of `actor` among the PC pawns, or -1. Pointer compares only.
int32_t pl_slot(const void* actor) {
    for (int32_t s = 0; s < kC17Slots && actor != nullptr; ++s) {
        if (pl_pawn(s) == actor) {
            return s;
        }
    }
    return -1;
}

// True when `actor` is a remote player's pawn (host: a server copy; client: a pawn card 028 drew). Pointer compares.
bool pl_remote(const void* actor) {
    for (const PlRemote& r : g_pl.remote) {
        if (actor != nullptr && c31_weak_get(r.pawn) == actor) {
            return true;
        }
    }
    return false;
}

void pl_remote_add(const void* pawn, ULONGLONG now) {
    int32_t free_at = -1;
    for (int32_t i = 0; i < kC17Slots; ++i) {
        const void* const at = c31_weak_get(g_pl.remote[i].pawn);
        if (at == pawn) {
            g_pl.remote[i].seen_ms = now;
            return;
        }
        free_at = free_at < 0 && at == nullptr ? i : free_at; // a destroyed pawn's slot is free (card 183)
    }
    if (free_at >= 0) {
        c31_weak_set(g_pl.remote[free_at].pawn, const_cast<void*>(pawn));
        g_pl.remote[free_at].seen_ms = now;
    }
}

void pl_burst_flush(PlBurst& b) {
    b.open = false;
    if (!pl_allow(PlClass::Shot)) {
        return;
    }
    log::line("playlog: %s t=%llu shot %c seq=%u w=%s k=%u ads=%d rl=%lld at=%.0f,%.0f,%.0f p=%.2f y=%.2f",
              pl_side(), static_cast<unsigned long long>(b.t), b.side, b.seq, b.weapon, b.k, b.ads, b.rl, b.at.x,
              b.at.y, b.at.z, b.rot.x, b.rot.y);
}

// Every kPlPawnMs: the PC pawns (host: every PC; client: its own), their movement component and the remote list.
void pl_pawns(void* world, ULONGLONG now) {
    g_pl.pawns_ms = now;
    for (int32_t s = 0; s < kC17Slots; ++s) {
        void* const pc = g_client_session && s > 0 ? nullptr : g_get_player_controller(world, s);
        const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
        c31_weak_set(g_pl.pc[s], pc);
        c31_weak_set(g_pl.pawn[s], const_cast<void*>(pawn));
        g_pl.mi[s] = pawn != nullptr && is_a(pawn, g_player_mi_class());
        g_pl.player[s] = pawn != nullptr && is_a(pawn, g_player_character_class());
        if (!g_client_session && pc != nullptr && pawn != nullptr && c26_is_remote_pc(pc)) {
            pl_remote_add(pawn, now);
        }
    }
    for (PlRemote& r : g_pl.remote) {
        r = r.pawn.serial != 0 && now - r.seen_ms > kPlRemoteMs ? PlRemote{} : r;
    }
    const void* const own = pl_pawn(0);
    // Only a player pawn: the front-end pawn has no CharacterMovement, and its absent row would stick for the process.
    const int32_t move = g_pl.player[0] ? pl_offset(g_pl.move_at, own, "CharacterMovement") : -2;
    g_pl.own_cmc = move >= 0 ? read_ptr(own, static_cast<size_t>(move)) : nullptr;
}

float pl_fov() {
    const void* const pc = pl_pc(0);
    const void* const camera = pc != nullptr ? read_ptr(pc, kPcCameraManager) : nullptr;
    const int32_t cache = camera != nullptr ? pl_offset(g_pl.cache_at, camera, "CameraCachePrivate") : -2;
    return cache >= 0 ? read_f32(camera, static_cast<size_t>(cache) + kPovFov) : -1.0f;
}

// Per tick: the equipped weapon (MI) or form (bug, card 208) of each player pawn; a change is one weapon line, with
// the weapon's definition class (bug forms all print as STBugForm).
void pl_weapons() {
    for (int32_t s = 0; s < kC17Slots; ++s) {
        const void* const pawn = g_pl.player[s] ? pl_pawn(s) : nullptr;
        void* const inventory = pawn != nullptr ? const_cast<void*>(c24_inventory(pawn)) : nullptr;
        void* const weapon = inventory != nullptr ? g_get_equipped_weapon(inventory) : nullptr;
        const void* const was = c31_weak_get(g_pl.weapon[s]);
        if (weapon == was) {
            continue;
        }
        c31_weak_set(g_pl.weapon[s], weapon);
        if (weapon != nullptr && pl_allow(PlClass::Weapon)) {
            log::line("playlog: %s t=%llu weapon slot %d %s: %s -> %s (%s)", pl_side(), pl_now(), s,
                      kSprintSideText[sprint_side(pawn)], object_name(was).text, object_name(weapon).text,
                      object_name(read_ptr(weapon, kWeaponDefinitionClass)).text);
        }
    }
}

// Per tick: the local PC's bShowMouseCursor, so a direct write (no SetShowMouseCursor call) is logged too.
void pl_cursor_poll() {
    const void* const pc = pl_pc(0);
    if (pc == nullptr) {
        return;
    }
    const unsigned bit = cursor_bit(pc);
    if (g_pl.cursor_known && bit != g_pl.cursor && pl_allow(PlClass::Cursor)) {
        log::line("playlog: %s t=%llu cursor %u -> %u (poll)", pl_side(), pl_now(), g_pl.cursor, bit);
    }
    g_pl.cursor = bit;
    g_pl.cursor_known = true;
}

void pl_clock_send(void* pc) {
    char text[64];
    snprintf(text, sizeof(text), "Q %u %llu", ++g_pl.clock_n, pl_now());
    wchar_t wire[kChanTextFrame];
    chan_send_client(pc, chan_text_frame(ChanOp::Clock, text, wire));
}

// Host, every 60 s: a clock query to each remote PC that has sent its first Pong.
void pl_clock_step() {
    for (int32_t s = 1; s < kC17Slots && !g_client_session; ++s) {
        void* const pc = pl_pc(s);
        const C50Copy* const c = pc != nullptr && c26_is_remote_pc(pc) ? c50_copy(object_index(pc), false) : nullptr;
        if (c != nullptr && c->ponged) {
            pl_clock_send(pc);
        }
    }
}

void pl_rtt_step() {
    if (g_pl.rtt_n != 0 && pl_allow(PlClass::Rtt)) {
        log::line("playlog: %s t=%llu rtt %u pings: min %u avg %llu max %u ms", pl_side(), pl_now(), g_pl.rtt_n,
                  g_pl.rtt_min, static_cast<unsigned long long>(g_pl.rtt_sum / g_pl.rtt_n), g_pl.rtt_max);
    }
    g_pl.rtt_n = g_pl.rtt_max = 0;
    g_pl.rtt_min = UINT32_MAX;
    g_pl.rtt_sum = 0;
}

// The summary text: counters, lines and suppressed lines per class (nonzero only). Under 480 chars (crash_line).
void pl_summary_text(const char* why, char* out, size_t cap) {
    const uint64_t* const c = g_pl.count;
    int n = snprintf(out, cap,
                     "playlog: %s t=%llu summary (%s) up %llu s: shots own %llu copy %llu, player hits %llu, downs "
                     "%llu, deaths %llu, loads %llu, joins %llu, rejoins %llu, mismatches %llu; input actions %u tabled, %u untabled, "
                     "%u no-action executes; suppressed",
                     pl_side(), pl_now(), why, (pl_now() - g_pl.start_ms) / 1000, c[0], c[1], c[2], c[3], c[4], c[5],
                     c[6], c[7], c[8], g_pl.action_n, g_pl.action_full, g_pl.action_null);
    for (size_t i = 0; i < kPlClasses && n > 0 && static_cast<size_t>(n) < cap; ++i) {
        if (g_pl.dropped[i] != 0) {
            n += snprintf(out + n, cap - static_cast<size_t>(n), " %s %llu", kPlClassText[i],
                          static_cast<unsigned long long>(g_pl.dropped[i]));
        }
    }
}

void pl_summary(const char* why) {
    char text[480];
    pl_summary_text(why, text, sizeof(text));
    log::line("%s", text);
}

} // namespace

void pl_ads_set(int ads) {
    if (ads == g_pl.ads) {
        return;
    }
    g_pl.ads = ads;
    g_pl.ads_fov_ms = GetTickCount64() + kPlAdsFovMs;
    if (pl_allow(PlClass::Ads)) {
        log::line("playlog: %s t=%llu ads %d fov %.1f", pl_side(), pl_now(), ads, static_cast<double>(pl_fov()));
    }
}

namespace {

// The kind of an input action, from its name once (the table keeps the pointer).
PlKey pl_action_kind(const char* name) {
    if (strncmp(name, "IA_SwitchWeapon", 15) == 0 || strcmp(name, "IA_SwitchTech") == 0) {
        return PlKey::Weapon;
    }
    if (strcmp(name, "IA_ADS") == 0) {
        return PlKey::Ads;
    }
    if (strcmp(name, "IA_UseItem") == 0 || strncmp(name, "IA_TacticalSupport", 18) == 0) {
        return PlKey::Grenade;
    }
    const bool press = strcmp(name, "IA_Reload") == 0 || strcmp(name, "IA_Interact") == 0 ||
                       strncmp(name, "IA_Skill", 8) == 0 || strcmp(name, "IA_Jump") == 0 ||
                       strcmp(name, "IA_Crouch") == 0 || strcmp(name, "IA_HumanMelee") == 0 ||
                       strcmp(name, "IA_Pause") == 0 || strcmp(name, "IA_Inventory") == 0 ||
                       strcmp(name, "IA_Objectives") == 0 || strcmp(name, "IA_ExitVehicle") == 0;
    return press ? PlKey::Press : PlKey::Quiet;
}

// The quiet actions that run every frame in game: tabled so the pointer compare ends them. Every other quiet action
// (menu and UI actions) is never tabled, so they cannot fill the table; each of their executes reads the name again
// (event rate, not per frame) and counts as untabled.
bool pl_quiet_tabled(const char* name) {
    static constexpr const char* kQuiet[] = {"IA_Move", "IA_MoveUp", "IA_Look", "IA_Sprint", "IA_PrimaryFire"};
    for (const char* q : kQuiet) {
        if (strcmp(name, q) == 0) {
            return true;
        }
    }
    return false;
}

PlAction* pl_action(const void* action) {
    for (uint32_t i = 0; i < g_pl.action_n; ++i) {
        if (g_pl.action[i].action == action) {
            return &g_pl.action[i];
        }
    }
    const ObjectName name = object_name(action);
    const PlKey kind = pl_action_kind(name.text);
    if ((kind == PlKey::Quiet && !pl_quiet_tabled(name.text)) || g_pl.action_n >= kPlActions) {
        ++g_pl.action_full; // untabled: a menu or UI action, or the table is full (a bug: it holds the named set)
        return nullptr;
    }
    PlAction& a = g_pl.action[g_pl.action_n++];
    a.action = action;
    snprintf(a.name, sizeof(a.name), "%s", name.text);
    a.kind = kind;
    a.last_ms = 0;
    log::line("playlog: %s t=%llu input action tabled: %s (kind %d)", pl_side(), pl_now(), a.name,
              static_cast<int>(kind)); // card 160: which pawn actions reach a binding Execute
    return &a;
}

} // namespace

bool pl_allow(PlClass c) {
    const size_t i = static_cast<size_t>(c);
    const ULONGLONG now = GetTickCount64();
    if (now - g_pl.sec_ms >= 1000) {
        g_pl.sec_ms = now;
        memset(g_pl.used, 0, sizeof(g_pl.used));
        g_pl.used_all = 0;
    }
    if (g_pl.used[i] >= kPlRate[i] || g_pl.used_all >= kPlGlobalRate) {
        ++g_pl.dropped[i];
        return false;
    }
    ++g_pl.used[i];
    ++g_pl.used_all;
    ++g_pl.lines[i];
    return true;
}

bool pl_once(uint64_t key, ULONGLONG now, ULONGLONG ms) {
    uint32_t pick = 0;
    for (uint32_t i = 0; i < kPlOnce; ++i) {
        PlOnce& o = g_pl.once[i];
        if (o.key == key) {
            if (now < o.until) {
                return false;
            }
            o.until = now + ms;
            return true;
        }
        pick = o.until < g_pl.once[pick].until ? i : pick;
    }
    g_pl.once[pick] = PlOnce{key, now + ms};
    return true;
}

void pl_count(PlCount c, uint32_t n) {
    g_pl.count[static_cast<size_t>(c)] += n;
}

int32_t pl_player_slot(const void* actor) {
    return pl_slot(actor);
}

uint32_t pl_shot_seq() {
    return g_pl.seq;
}

uint32_t pl_rtt_last() {
    return g_pl.rtt_last;
}

// The lowest of the last kPlRttRing round trips, 0 before one (card 340). A load hitch only adds to a round trip, so
// the lowest is the path's own time. A median was not enough: the ring is kept across map loads and held 2 hitch
// samples of 3 (1643, 30, 1106: the join load and the travel load), so both middles read a hitch.
uint32_t pl_rtt_floor() {
    const uint32_t n = g_pl.rtt_ring_n < kPlRttRing ? g_pl.rtt_ring_n : kPlRttRing;
    uint32_t low = n == 0 ? 0 : g_pl.rtt_ring[0];
    for (uint32_t i = 1; i < n; ++i) {
        low = g_pl.rtt_ring[i] < low ? g_pl.rtt_ring[i] : low;
    }
    return low;
}

void pl_rtt(uint32_t ms) {
    g_pl.rtt_last = ms;
    g_pl.rtt_ring[g_pl.rtt_ring_n % kPlRttRing] = ms;
    ++g_pl.rtt_ring_n;
    g_pl.rtt_min = g_pl.rtt_n == 0 || ms < g_pl.rtt_min ? ms : g_pl.rtt_min;
    g_pl.rtt_max = ms > g_pl.rtt_max ? ms : g_pl.rtt_max;
    g_pl.rtt_sum += ms;
    ++g_pl.rtt_n;
}

void pl_remote_seen(const void* pawn) {
    if (g_client_session && pawn != nullptr) {
        pl_remote_add(pawn, GetTickCount64());
    }
}

void pl_step(void* world, ULONGLONG now) {
    ++g_pl.tick;
    g_pl.start_ms = g_pl.start_ms == 0 ? now : g_pl.start_ms;
    for (PlBurst& b : g_pl.burst) {
        if (b.open && b.tick != g_pl.tick) {
            pl_burst_flush(b);
        }
    }
    if (now - g_pl.pawns_ms >= kPlPawnMs) {
        pl_pawns(world, now);
    }
    pl_weapons();
    pl_cursor_poll();
    if (g_pl.ads_fov_ms != 0 && now >= g_pl.ads_fov_ms) {
        g_pl.ads_fov_ms = 0;
        if (pl_allow(PlClass::Ads)) {
            log::line("playlog: %s t=%llu ads %d fov %.1f (+%llu ms)", pl_side(), pl_now(), g_pl.ads,
                      static_cast<double>(pl_fov()), static_cast<unsigned long long>(kPlAdsFovMs));
        }
    }
    if (now - g_pl.clock_ms >= kPlClockMs) {
        g_pl.clock_ms = now;
        pl_clock_step();
        pl_rtt_step();
    }
    if (g_pl.summary_ms == 0 || now - g_pl.summary_ms >= kPlSummaryMs) {
        if (g_pl.summary_ms != 0) {
            pl_summary("300 s");
        }
        g_pl.summary_ms = now;
    }
}

// A launch: true when the shooter (or its owner) is a player pawn, so the caller logs no per-launch line. Launches of
// one shooter in one tick are one burst (pellets spawn in one call stack); the burst line is written at the next tick.
bool pl_shot(const void* shooter, const Vec3& at, const Vec3& rot, bool copying) {
    int32_t s = pl_slot(shooter);
    const void* const owner = s < 0 && shooter != nullptr ? weapon_pawn(shooter) : nullptr;
    s = s < 0 ? pl_slot(owner) : s;
    if (s < 0 && g_client_session && (pl_remote(shooter) || pl_remote(owner))) {
        pl_count(PlCount::ShotCopy); // the client's copy of a remote player's launch: counted, the host logs the shot
        return true;
    }
    if (s < 0) {
        return false;
    }
    PlBurst& b = g_pl.burst[s];
    if (b.open && b.tick == g_pl.tick) {
        ++b.k;
        return true;
    }
    if (b.open) {
        pl_burst_flush(b);
    }
    const bool own = s == 0 && !copying;
    pl_count(own ? PlCount::ShotOwn : PlCount::ShotCopy);
    if (g_client_session && !own) {
        return true; // the host's shots on the client: counted, the host logs them
    }
    const ULONGLONG now = GetTickCount64();
    b = PlBurst{true, own ? (g_client_session ? 'c' : 'h') : 's', g_pl.tick, now, own ? ++g_pl.seq : 0u, 1,
                own ? g_pl.ads : -1, g_pl.reload_ms[s] != 0 ? static_cast<long long>(now - g_pl.reload_ms[s]) : -1,
                at, rot, {}};
    const void* const weapon = shooter == pl_pawn(s) ? c31_weak_get(g_pl.weapon[s]) : shooter;
    snprintf(b.weapon, sizeof(b.weapon), "%s", object_name(weapon).text);
    return true;
}

// Card 286: the client's own grenade launch is released right after its task's Activate (the host's copy is shown),
// in the same tick as its pl_shot: it leaves the own pawn's open burst and the own shot count. False when no own burst
// of this tick is open (nothing to take back).
bool pl_shot_undo() {
    PlBurst& b = g_pl.burst[0];
    if (!b.open || b.tick != g_pl.tick || b.seq == 0) {
        return false;
    }
    uint64_t& own = g_pl.count[static_cast<size_t>(PlCount::ShotOwn)];
    own -= own > 0 ? 1u : 0u;
    if (b.k > 1) {
        --b.k;
    } else {
        b.open = false; // never flushed: no shot row
    }
    return true;
}

// After InternalTryActivateAbility: one ability line per class per pawn per 2 s on player ASCs; a reload activation
// starts the pawn's reload clock; the own grenade activation logs the aim.
void pl_activation(const void* asc, const void* ability, bool ok, bool server_request) {
    c276_reload_activation(asc, ability, ok, server_request); // card 276: the client's reload source
    c437_respool_activation(asc, ability, ok, server_request); // card 437 part C: before card 296 reads the press
    c296_activation(asc, ability, ok, server_request);        // card 296: the client's spool rows
    const void* const owner = asc != nullptr ? read_ptr(asc, kComponentOwner) : nullptr;
    int32_t s = pl_slot(owner);
    s = s < 0 && owner != nullptr ? pl_slot(weapon_pawn(owner)) : s;
    if (s < 0 || ability == nullptr) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    const void* const cls = read_ptr(ability, kObjectClass);
    const uint64_t key = reinterpret_cast<uintptr_t>(cls) ^ (static_cast<uint64_t>(s) << 60) ^ (server_request ? 1u : 0u);
    if (!ok || !pl_once(key, now, kPlOnceMs)) {
        return;
    }
    const ObjectName name = class_name(ability);
    g_pl.reload_ms[s] = strstr(name.text, "Reload") != nullptr ? now : g_pl.reload_ms[s];
    if (pl_allow(PlClass::Ability)) {
        log::line("playlog: %s t=%llu ability slot %d %s%s: %s", pl_side(), pl_now(), s,
                  kSprintSideText[sprint_side(pl_pawn(s))], server_request ? " (client request)" : "", name.text);
    }
    const void* const pc = s == 0 ? pl_pc(0) : nullptr;
    if (pc != nullptr && !server_request && strstr(name.text, "Grenade") != nullptr && pl_allow(PlClass::Grenade)) {
        const Vec3 rot = read_vec3(pc, kControllerControlRotation);
        const void* const camera = read_ptr(pc, kPcCameraManager);
        const int32_t cache = camera != nullptr ? pl_offset(g_pl.cache_at, camera, "CameraCachePrivate") : -2;
        const Vec3 cam = cache >= 0 ? read_vec3(camera, static_cast<size_t>(cache) + 0x10) : Vec3{};
        log::line("playlog: %s t=%llu grenade aim %s: control P %.2f Y %.2f, camera (%.0f, %.0f, %.0f)", pl_side(),
                  pl_now(), name.text, rot.x, rot.y, cam.x, cam.y, cam.z);
    }
}

// EI Execute (every bound input event): the action pointer against the table first; names only on a new action.
void pl_execute(const void* instance) {
    const void* const action = instance != nullptr ? read_ptr(instance, kInstanceSourceAction) : nullptr;
    g_pl.action_null += action == nullptr ? 1u : 0u;
    PlAction* const a = action != nullptr ? pl_action(action) : nullptr;
    if (a == nullptr || a->kind == PlKey::Quiet) {
        return;
    }
    const uint8_t event = static_cast<const uint8_t*>(instance)[kInstanceTriggerEvent];
    const ULONGLONG now = GetTickCount64();
    const bool press = event == kTriggerStarted || (event == kTriggerTriggered && now - a->last_ms > kPlPressGapMs);
    a->last_ms = now;
    if (a->kind == PlKey::Ads) {
        if (press || event == kTriggerCompleted || event == kTriggerCanceled) {
            pl_ads_set(press ? 1 : 0);
        }
        return;
    }
    if (!press) {
        return;
    }
    if (strcmp(a->name, "IA_Crouch") == 0) { // card 482: every press, never rate-limited, with its key's source
        const C482Press& k = g_c482_press;
        log::line("card482: %s t=%llu IA_Crouch press (event %u): last key press %s at t=%llu (%llu ms before), "
                  "source %s, driver cmd %u", pl_side(), now, event, k.key[0] != '\0' ? k.key : "(none)", k.t,
                  now - k.t, k.injected ? "driver" : "not a driver command", k.driver_seq);
    }
    const PlClass c = a->kind == PlKey::Weapon ? PlClass::Weapon : (a->kind == PlKey::Grenade ? PlClass::Grenade : PlClass::Input);
    if (pl_allow(c)) {
        log::line("playlog: %s t=%llu press %s", pl_side(), pl_now(), a->name);
    }
}

// A sound: logged when its owner (or the owner's Owner, a weapon's pawn) is a remote player's pawn.
void pl_sound(const void* owner, const void* sound, const Vec3& at, const void* attenuation, int spatial, float volume) {
    const bool remote = pl_remote(owner) || (owner != nullptr && pl_remote(weapon_pawn(owner)));
    if (!remote) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (!pl_once(reinterpret_cast<uintptr_t>(sound) ^ (reinterpret_cast<uintptr_t>(owner) << 1), now, kPlOnceMs) ||
        !pl_allow(PlClass::Audio)) {
        return;
    }
    const void* const own_pawn = pl_pawn(0);
    const Vec3 own = own_pawn != nullptr ? actor_location(own_pawn) : Vec3{};
    const double dx = at.x - own.x, dy = at.y - own.y, dz = at.z - own.z;
    char vol[32] = "";
    if (volume >= 0.0f) {
        snprintf(vol, sizeof(vol), ", volume %.2f", static_cast<double>(volume));
    }
    log::line("playlog: %s t=%llu audio %s from %s at (%.0f, %.0f, %.0f) %.0f uu, %s, attenuation %s%s", pl_side(),
              pl_now(), object_name(sound).text, object_name(owner).text, at.x, at.y, at.z,
              sqrt(dx * dx + dy * dy + dz * dz), spatial < 0 ? "2D/3D ?" : (spatial != 0 ? "3D" : "2D"),
              object_name(attenuation).text, vol);
}

// Card 162: true when a sound at `at` from `owner` is a remote player's. The owner (or its Owner, a weapon's pawn) is a
// remote pawn; or no player owns it (null, or an actor that is no player pawn's) and the nearest player pawn to `at`,
// own or remote, is a remote one within `max_uu` uu. `d` gets that remote pawn's distance (-1 when the owner decided).
bool pl_remote_source(const void* owner, const Vec3& at, double max_uu, double& d) {
    d = -1.0;
    const void* const up = owner != nullptr ? weapon_pawn(owner) : nullptr;
    if (pl_remote(owner) || pl_remote(up)) {
        return true;
    }
    if ((owner != nullptr && pl_slot(owner) >= 0) || (up != nullptr && pl_slot(up) >= 0)) {
        return false; // a local player's own sound
    }
    double best = -1.0;
    for (const PlRemote& r : g_pl.remote) {
        const void* const pawn = c31_weak_get(r.pawn);
        if (pawn != nullptr) {
            const Vec3 p = actor_location(pawn);
            const double e = sqrt((at.x - p.x) * (at.x - p.x) + (at.y - p.y) * (at.y - p.y) + (at.z - p.z) * (at.z - p.z));
            best = best < 0.0 || e < best ? e : best;
        }
    }
    double own = -1.0;
    const void* const own_pawn = pl_pawn(0);
    if (own_pawn != nullptr) {
        const Vec3 p = actor_location(own_pawn);
        own = sqrt((at.x - p.x) * (at.x - p.x) + (at.y - p.y) * (at.y - p.y) + (at.z - p.z) * (at.z - p.z));
    }
    d = best;
    return best >= 0.0 && best <= max_uu && (own < 0.0 || best < own);
}

double pl_listener_uu(const Vec3& at) {
    const void* const own_pawn = pl_pawn(0);
    if (own_pawn == nullptr) {
        return -1.0;
    }
    const Vec3 p = actor_location(own_pawn);
    return sqrt((at.x - p.x) * (at.x - p.x) + (at.y - p.y) * (at.y - p.y) + (at.z - p.z) * (at.z - p.z));
}

// Card 162, client: a relayed card 136 sound row. Every relayed row is a host-side sound, so it logs with no owner test
// (a world-keyed row carries no pawn). Its place against the listener and the nearest remote pawn, and the attenuation
// it plays with: the row's override, else the sound's own AttenuationSettings ("none" means it plays unattenuated).
void pl_sound_row(const void* owner, const void* sound, const Vec3& at, const void* override_atten, bool attached) {
    const ULONGLONG now = GetTickCount64();
    if (sound == nullptr || !pl_once((reinterpret_cast<uintptr_t>(sound) ^ (reinterpret_cast<uintptr_t>(owner) << 1)) + 0x162, now,
                                     kPlOnceMs) || !pl_allow(PlClass::Audio)) {
        return;
    }
    const int32_t sa = pl_offset(g_pl.sound_atten_at, sound, "AttenuationSettings");
    const void* const own_atten = sa >= 0 ? read_ptr(sound, static_cast<size_t>(sa)) : nullptr;
    const void* const own_pawn = pl_pawn(0);
    const Vec3 me = own_pawn != nullptr ? actor_location(own_pawn) : Vec3{};
    double near_remote = -1.0;
    for (const PlRemote& r : g_pl.remote) {
        const void* const pawn = c31_weak_get(r.pawn);
        if (pawn == nullptr) {
            continue;
        }
        const Vec3 p = actor_location(pawn);
        const double d = sqrt((at.x - p.x) * (at.x - p.x) + (at.y - p.y) * (at.y - p.y) + (at.z - p.z) * (at.z - p.z));
        near_remote = near_remote < 0.0 || d < near_remote ? d : near_remote;
    }
    const double dx = at.x - me.x, dy = at.y - me.y, dz = at.z - me.z;
    log::line("playlog: client t=%llu audio row %s from %s%s at (%.0f, %.0f, %.0f): %.0f uu from the listener, %.0f uu "
              "from the nearest remote pawn; owner %s; attenuation override %s, sound's own %s", pl_now(), object_name(sound).text,
              owner != nullptr ? object_name(owner).text : "a world row", attached ? " (attached)" : "", at.x, at.y,
              at.z, sqrt(dx * dx + dy * dy + dz * dz), near_remote, owner != nullptr ? "resolved" : "world fallback",
              object_name(override_atten).text,
              sa >= 0 ? object_name(own_atten).text : "?");
}

void pl_clock_client(void* pc, const WireString* s) {
    char text[128];
    if (chan_text(s, text, sizeof(text)) <= 0) {
        return;
    }
    unsigned n = 0;
    unsigned long long t0 = 0, tc = 0, rtt = 0;
    long long off = 0;
    if (text[0] == 'Q' && sscanf_s(text + 1, "%u %llu", &n, &t0) == 2) {
        char reply[96];
        snprintf(reply, sizeof(reply), "A %u %llu %llu", n, t0, pl_now());
        wchar_t wire[kChanTextFrame];
        chan_send_host(pc, chan_text_frame(ChanOp::Clock, reply, wire));
        return;
    }
    if (text[0] == 'R' && sscanf_s(text + 1, "%u %llu %llu %llu %lld", &n, &t0, &tc, &rtt, &off) == 5 &&
        pl_allow(PlClass::Clock)) {
        log::line("playlog: client t=%llu clock %u: host t %llu client t %llu offset %lld ms rtt %llu ms", pl_now(), n,
                  t0, tc, off, rtt);
    }
}

void pl_clock_host(void* pc, const WireString* s) {
    char text[128];
    unsigned n = 0;
    unsigned long long t0 = 0, tc = 0;
    if (chan_text(s, text, sizeof(text)) <= 0 || text[0] != 'A' || sscanf_s(text + 1, "%u %llu %llu", &n, &t0, &tc) != 3) {
        return;
    }
    const unsigned long long now = pl_now();
    const unsigned long long rtt = now >= t0 ? now - t0 : 0;
    const long long off = static_cast<long long>(tc) - static_cast<long long>(t0 + rtt / 2);
    if (pl_allow(PlClass::Clock)) {
        log::line("playlog: host t=%llu clock %u (%s): host t %llu client t %llu offset %lld ms rtt %llu ms", now, n,
                  object_name(pc).text, t0, tc, off, rtt);
    }
    char reply[128];
    snprintf(reply, sizeof(reply), "R %u %llu %llu %llu %lld", n, t0, tc, rtt, off);
    wchar_t wire[kChanTextFrame];
    chan_send_client(pc, chan_text_frame(ChanOp::Clock, reply, wire));
}

void pl_first_pong(void* pc) {
    pl_count(PlCount::Join);
    pl_clock_send(pc);
}

// Card 182: at LoadMap enter (after sync_map_end, whose "bugform" row is card 208's map end). The pawn, controller,
// weapon and remote pointers name the old world, which LoadMap frees before c98_arm_map runs pl_map; a sound played
// during the new world's actor init would read them. No tick runs inside LoadMap, so pl_pawns refills them on the new
// map's first tick (pawns_ms 0).
void pl_map_end() {
    memset(g_pl.pawn, 0, sizeof(g_pl.pawn));
    memset(g_pl.pc, 0, sizeof(g_pl.pc));
    memset(g_pl.mi, 0, sizeof(g_pl.mi));
    memset(g_pl.player, 0, sizeof(g_pl.player));
    memset(g_pl.weapon, 0, sizeof(g_pl.weapon));
    memset(g_pl.remote, 0, sizeof(g_pl.remote));
    g_pl.own_cmc = nullptr;
    g_pl.pawns_ms = 0;
}

void pl_map() {
    for (PlBurst& b : g_pl.burst) {
        if (b.open) {
            pl_burst_flush(b);
        }
    }
    if (g_pl.start_ms != 0) {
        pl_summary("map end");
    }
    pl_count(PlCount::Load);
    pl_map_end();
    c304_map_end(); // card 304
    c314_map_end(); // card 314
    memset(g_pl.reload_ms, 0, sizeof(g_pl.reload_ms));
    g_pl.widget_n = 0;
    g_pl.ads = 0;
    g_pl.ads_fov_ms = 0;
    g_pl.pawns_ms = 0;
}

// Detach (process exit): crash_line, which never waits for the log lock.
void pl_session_end() {
    char text[480];
    pl_summary_text("session end", text, sizeof(text));
    log::crash_line("%s", text);
}

// Card 176: on the host, a server copy's 2D play is muted first (c176_mute_copy_2d); card 314: another machine's
// player's unspatialized play gets the fallback attenuation and a 2D one is muted (both sides). The original always
// runs.
void detour_pl_play_internal(void* comp, const void* data, void* sound_override) {
    const void* const owner = comp != nullptr ? read_ptr(comp, kComponentOwner) : nullptr;
    const int32_t snd = comp != nullptr ? pl_offset(g_pl.sound_at, comp, "Sound") : -1;
    const void* const sound = sound_override != nullptr ? sound_override
                              : (snd >= 0 ? read_ptr(comp, static_cast<size_t>(snd)) : nullptr);
    const int32_t vol_at = comp != nullptr ? pl_offset(g_pl.volume_at, comp, "VolumeMultiplier") : -1;
    const int32_t vol = !g_client_session ? vol_at : -1;
    c314_restore_volume(comp, vol_at); // card 314 review: an earlier mute of this component is undone first
    const bool muted = c176_mute_copy_2d(comp, owner, sound, vol);
    c314_component_play(comp, owner, sound, comp != nullptr ? pl_offset(g_pl.atten_at, comp, "AttenuationSettings") : -1,
                        vol_at, muted);
    if (pl_remote(owner) || (owner != nullptr && pl_remote(weapon_pawn(owner)))) {
        const int32_t att = pl_offset(g_pl.atten_at, comp, "AttenuationSettings");
        static constexpr const char* kSpatial[] = {"bAllowSpatialization", nullptr};
        pl_sound(owner, sound, read_vec3(comp, kComponentTranslation),
                 att >= 0 ? read_ptr(comp, static_cast<size_t>(att)) : nullptr, c29_bool(comp, kSpatial),
                 vol >= 0 ? read_f32(comp, static_cast<size_t>(vol)) : -1.0f);
    }
    uint32_t id = 0;
    if (comp != nullptr && c418_play_enter(comp, sound, pl_component_f32(comp, vol_at),
                                           pl_component_f32(comp, pl_offset(g_pl.pitch_at, comp, "PitchMultiplier")),
                                           pl_component_ptr(comp, pl_offset(g_pl.atten_at, comp, "AttenuationSettings")),
                                           &id)) {
        if (!c136_blocked()) { // card 418: a host-sent Blueprint SpawnSoundAtLocation is not played on the client
            g_orig_pl_play_internal(comp, data, sound_override);
        }
        c136_fx_leave(C136Point::SpawnSound, id, nullptr);
        return;
    }
    g_orig_pl_play_internal(comp, data, sound_override);
}

// The own pawn's movement hit an ASTDoor whose DoorState is Open: the client sees it open and is blocked.
void detour_pl_impact(void* cmc, const void* hit, const Vec3* delta) {
    g_orig_pl_impact(cmc, hit, delta);
    if (cmc != g_pl.own_cmc || cmc == nullptr || hit == nullptr) {
        return;
    }
    const void* const actor = g_weak_get(static_cast<const uint8_t*>(hit) + kHitResultActor);
    if (actor == nullptr || !is_a(actor, g_c95_door_class())) {
        return;
    }
    const int32_t at = pl_offset(g_pl.door_at, actor, "DoorState");
    const uint8_t state = at >= 0 ? static_cast<const uint8_t*>(actor)[at] : 0xFF;
    if (state != kDoorOpen || !pl_once(reinterpret_cast<uintptr_t>(actor), GetTickCount64(), kPlOnceMs) ||
        !pl_allow(PlClass::DoorBlock)) {
        return;
    }
    const Vec3 door = actor_location(actor);
    const void* const own_pawn = pl_pawn(0);
    const Vec3 own = own_pawn != nullptr ? actor_location(own_pawn) : Vec3{};
    log::line("playlog: %s t=%llu door block: own pawn at (%.0f, %.0f, %.0f) blocked by %s at (%.0f, %.0f, %.0f) "
              "shown open (DoorState %u)", pl_side(), pl_now(), own.x, own.y, own.z, object_name(actor).text, door.x,
              door.y, door.z, state);
}

void detour_pl_add_to_viewport(void* widget, int32_t z) {
    g_orig_pl_add_to_viewport(widget, z);
    bool held = false;
    for (uint32_t i = 0; i < g_pl.widget_n; ++i) {
        held = held || g_pl.widget[i] == widget;
    }
    if (!held && g_pl.widget_n < kPlWidgets) {
        g_pl.widget[g_pl.widget_n++] = widget;
    }
    if (pl_allow(PlClass::Widget)) {
        log::line("playlog: %s t=%llu widget add %s (%s) z %d, %u in viewport", pl_side(), pl_now(),
                  class_name(widget).text, object_name(widget).text, z, g_pl.widget_n);
    }
}

// Every widget's removal: only those in the viewport table (pointer compares) are logged.
void detour_pl_remove_from_parent(void* widget) {
    int32_t found = -1;
    for (uint32_t i = 0; i < g_pl.widget_n; ++i) {
        found = g_pl.widget[i] == widget ? static_cast<int32_t>(i) : found;
    }
    if (found >= 0) {
        g_pl.widget[found] = g_pl.widget[--g_pl.widget_n];
        if (pl_allow(PlClass::Widget)) {
            log::line("playlog: %s t=%llu widget remove %s (%s), %u in viewport", pl_side(), pl_now(),
                      class_name(widget).text, object_name(widget).text, g_pl.widget_n);
        }
    }
    g_orig_pl_remove_from_parent(widget);
}

void detour_pl_remove_mapping_context(void* iface, const void* context, const uint8_t* options) {
    g_orig_pl_remove_mapping_context(iface, context, options);
    if (pl_allow(PlClass::Input)) {
        log::line("playlog: %s t=%llu imc remove %s, caller exe+0x%llX", pl_side(), pl_now(), object_name(context).text,
                  exe_rva(_ReturnAddress()));
    }
}

} // namespace ubw::hooks

namespace ubw {
void playlog_session_end() {
    hooks::pl_session_end();
}
} // namespace ubw
