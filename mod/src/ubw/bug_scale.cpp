// Card 111: more bugs, and more bugs attacking at once, with more players (host only; the client spawns no enemies).
// The game already scales both by difficulty; the mod multiplies the result by kC111Scale[players - 1]:
//
// - Wave size: USTEncounterWaveBase::FactorDifficultyIntoWaveBudget (0x0511E8A0) returns
//   (int)(budget * difficulty factor); the budget is the pre-made wave data's +0x28. Its two callers are
//   ASTEncounterManagerBase::InitialiseWave (0x05129407, the procedural list from data +0x60 is generated against
//   it) and USTEncounterWaveBase::SetupReinforcements (0x05137BCB). Explicit enemy lists (data +0x70), bosses and
//   spawners outside encounters do not go through it and are unchanged.
// - Attackers at once: ASTTicketManager keeps one container per ESTTicketType (1 Melee, 2 WeakProjectile,
//   3 DeadlyProjectile, 4 AreaAttack) in a TMap at manager +0x2C8 (24-byte pairs: type byte, container pointer at
//   +8). A container's limit is at +0x90 and the points in use at +0x8C; RequestTicket (0x050FF6D0) grants while
//   used + cost <= limit. CreateTicketContainer (0x050E2750, four calls from BeginPlay) and TryRefreshTicketContainer
//   (0x0510A320, four calls from RefreshTicketLimits) take the limit as their int argument; both are scaled.
//
// The count is the PlayerControllers present when a wave is set up or the limits are written. When the count
// changes mid-map the step calls RefreshTicketLimits once, so the limits follow; waves already set up keep their size.
// With one player the factor is 1.0 and every value passes through unchanged.
//
// Card 122: the player preference roll. UADTargetComponent::SelectPrimaryTarget (0x051A5940) rolls
// rand() * 100/32767 against AttackPlayerOverOtherTargetsPercentage (component +0xC4, BeginPlay copies it from the
// NPC data +0x138; WarriorBug 10) and on a hit adds AttackPlayerOverOtherTargetsThreatValue (+0xC8, 50) to the threat
// of "the player" (card 103: the enemy's own player), which pulls bugs off NPC troopers and onto players. Around each
// call the chance is scaled by the factor (at most 100) and put back after, so it follows the count live and the
// threat bonus, attack rules and cooldowns stay as they are.

#include "ubw/hooks_internal.h"

#include <math.h>
#include <string.h>

namespace ubw::hooks {

C111FactorFn g_orig_c111_factor = nullptr;
C111CreateFn g_orig_c111_create = nullptr;
C111TryRefreshFn g_orig_c111_try_refresh = nullptr;
C111RequestFn g_orig_c111_request = nullptr;
C111RefreshFn g_c111_refresh = nullptr;
C112SetProceduralFn g_c112_set_procedural = nullptr;
C112ActivateFn g_orig_c112_activate = nullptr;

namespace {

constexpr float kC111Scale[kC43Max] = {1.0f, 1.6f, 2.1f, 2.6f}; // by player count: 1, 2, then +0.5 each
constexpr int32_t kC111Types = 5;              // ESTTicketType None..AreaAttack
constexpr size_t kC111ManagerMap = 0x2C8;      // ASTTicketManager: TMap<ESTTicketType, container*>
constexpr int32_t kC111MapCap = 8;             // the map holds four containers
constexpr size_t kC111PairBytes = 24;          // TSetElement<TPair<uint8, ptr>>: key, pad, value at +8, hash
constexpr size_t kC111PairValue = 0x8;
constexpr size_t kC111HolderMap = 0x28;        // container: TMap<enemy, cost>
constexpr size_t kC111HolderNum = kC111HolderMap + 0x8;
constexpr size_t kC111HolderFree = kC111HolderMap + 0x34;
constexpr size_t kC111Used = 0x8C;
constexpr size_t kC111Limit = 0x90;
constexpr uint32_t kC111Lines = 240;
constexpr const char* kC111TypeNames[kC111Types] = {"None", "Melee", "WeakProjectile", "DeadlyProjectile",
                                                    "AreaAttack"};

// Card 112: waves that InitialiseWave set up and that have not started. The budget InitialiseWave passes is consumed
// at once: GenerateProceduralSpawnList (0x05120120) empties the wave's procedural spawn list (TArray of
// FSTProceduralEnemy at wave +0x2B8, 16-byte entries: class, cost int at +8, weight float at +0xC) and fills it to
// the budget from the data's procedural enemies (data +0x60). SetProceduralEnemies (0x051378B0) is that same call
// with a new budget, so a rescale regenerates the list. ActivateWave (0x05115980) starts spawning and sets byte
// +0x3A9 (ClearWave resets it); a wave leaves the table when it activates. Wave objects are outered to a Package,
// not the level, so each entry keeps the world it was set up in.
constexpr int32_t kC112Waves = 64;               // c111c: 23 waves on FED_C2M1
constexpr unsigned long long kC112InitialiseRet = 0x0512940C; // return address of the InitialiseWave budget call
constexpr size_t kC112Spawns = 0x2B8;            // TArray<FSTProceduralEnemy>
constexpr size_t kC112SpawnBytes = 16;
constexpr size_t kC112SpawnCost = 0x8;
constexpr size_t kC112DataProcedural = 0x60;     // pre-made wave data: TArray<FSTProceduralEnemy>
constexpr size_t kC112Activated = 0x3A9;
constexpr int32_t kC112MaxSpawns = 512;

struct C112Wave {
    C31Weak wave;
    C31Weak data;
    uintptr_t world;
    int32_t base;      // budget after difficulty, before the player factor
    uint32_t players;  // count the current list was generated for
};

struct C112Table {
    uintptr_t world;   // game world the step last saw; a change clears the table
    int32_t count;
    bool full_logged;
    C112Wave w[kC112Waves];
};
C112Table g_c112{};

struct C111State {
    C31Weak manager;           // the ticket manager whose limits were last written; state resets when it changes
    uint32_t players;          // count the step last saw (0 = none yet for this manager)
    int32_t base[kC111Types];  // limit before scaling, as the game passed it
    int32_t peak_used[kC111Types];
    int32_t peak_holders[kC111Types];
    uint32_t lines;
};
C111State g_c111{};

bool c111_line() {
    return g_c111.lines < kC111Lines && ++g_c111.lines != 0;
}

const char* c111_type(uint8_t type) {
    return type < kC111Types ? kC111TypeNames[type] : "?";
}

// The UWorld in an object's outer chain (wave -> encounter manager -> level -> world; manager -> level -> world).
// GetPlayerController needs it: a wave object has no GetWorld, so it would find no controller.
const void* c111_world(const void* object) {
    constexpr int32_t kMaxOuters = 8;
    const void* o = object;
    for (int32_t i = 0; i < kMaxOuters && o != nullptr; ++i) {
        if (strcmp(class_name(o).text, "World") == 0) {
            return o;
        }
        o = read_ptr(o, kObjectOuter);
    }
    return nullptr;
}

// Card 113: the co-op session's player count, host only. A map change or restart drops every client to the menu and
// card 045's reconnect brings it back (card 052: 7.0-9.1 s after the drop; card 045: 6.4 s), while the new map's
// start waves are set up at load. The count is the most players seen; it holds for kC113GraceMs after each new world
// and then follows the players present, so a player who does not come back, or leaves mid-map, drops out.
constexpr ULONGLONG kC113GraceMs = 45000; // reconnect about 10 s after the drop, plus the 20 s connect timeout and margin
struct C113Session {
    uintptr_t world;     // world the grace was armed for
    ULONGLONG grace_end; // GetTickCount64 time the hold ends
    uint32_t players;    // session count, 0 before the first listen world
};
C113Session g_c113{};

uint32_t c111_present(const void* object) {
    const void* const world = c111_world(object);
    const uint32_t n = world != nullptr ? c43_roster(world).players : 0;
    return n < 1 ? 1 : (n > kC43Max ? kC43Max : n);
}

uint32_t c111_players(const void* object) {
    const uint32_t present = c111_present(object);
    return present > g_c113.players ? present : g_c113.players;
}

// Host, every tick in a listen world: raise the session count to the players present, arm the grace on a new world,
// and after the grace let the count follow the players present.
void c113_step(void* world, ULONGLONG now) {
    const uint32_t present = c111_present(world);
    if (reinterpret_cast<uintptr_t>(world) != g_c113.world) {
        g_c113.world = reinterpret_cast<uintptr_t>(world);
        g_c113.grace_end = now + kC113GraceMs;
        if (c111_line()) {
            log::line("card113: new world, session players %u held for %llu ms (present %u)", g_c113.players,
                      static_cast<unsigned long long>(kC113GraceMs), present);
        }
    }
    if (present > g_c113.players || (present < g_c113.players && now >= g_c113.grace_end)) {
        if (c111_line()) {
            log::line("card113: session players %u -> %u (present %u, %s)", g_c113.players, present, present,
                      present > g_c113.players ? "join" : "left or not back after the grace");
        }
        g_c113.players = present;
    }
}

float c111_factor(uint32_t players) {
    return kC111Scale[players - 1];
}

// ceil(limit * factor), at least limit + 1 with 2+ players; 0 stays 0 (a type the difficulty turns off).
int32_t c111_scale_limit(int32_t limit, uint32_t players) {
    if (players <= 1 || limit <= 0) {
        return limit;
    }
    const int32_t scaled = static_cast<int32_t>(ceilf(static_cast<float>(limit) * c111_factor(players)));
    return scaled > limit ? scaled : limit + 1;
}

int32_t c111_scale_budget(int32_t base, uint32_t players) {
    return players <= 1 ? base : static_cast<int32_t>(lroundf(static_cast<float>(base) * c111_factor(players)));
}

// A wave's outer chain does not reach the world (Package outer), so its count and world come from the ticket manager,
// an actor of the same game world.
const void* c111_wave_ctx(const void* wave) {
    const void* const manager = c31_weak_get(g_c111.manager);
    return c111_world(wave) != nullptr || manager == nullptr ? wave : manager;
}

struct C112Spawns {
    int32_t entries;
    int32_t cost;
};

C112Spawns c112_spawns(const void* wave) {
    C112Spawns r{read_i32(wave, kC112Spawns + 0x8), 0};
    const uint8_t* const data = static_cast<const uint8_t*>(read_ptr(wave, kC112Spawns));
    for (int32_t i = 0; data != nullptr && i < r.entries && i < kC112MaxSpawns; ++i) {
        r.cost += read_i32(data, static_cast<size_t>(i) * kC112SpawnBytes + kC112SpawnCost);
    }
    return r;
}

void c112_record(const void* wave, void* const* data, int32_t base, uint32_t players) {
    if (g_c112.count >= kC112Waves) {
        if (!g_c112.full_logged && c111_line()) {
            log::line("card112: wave table full (%d); %s keeps its setup size", kC112Waves, object_name(wave).text);
        }
        g_c112.full_logged = true;
        return;
    }
    C112Wave& e = g_c112.w[g_c112.count++];
    e = C112Wave{};
    c31_weak_set(e.wave, const_cast<void*>(wave));
    c31_weak_set(e.data, data != nullptr ? *data : nullptr);
    e.world = reinterpret_cast<uintptr_t>(c111_world(c111_wave_ctx(wave)));
    e.base = base;
    e.players = players;
}

void c112_drop(int32_t i) {
    g_c112.w[i] = g_c112.w[--g_c112.count];
}

// Every recorded wave of `world` that has not started is regenerated for `players`; dead or started waves leave.
void c112_rescale(const void* world, uint32_t players) {
    int32_t rescaled = 0;
    for (int32_t i = g_c112.count - 1; i >= 0; --i) {
        C112Wave& e = g_c112.w[i];
        void* const wave = c31_weak_get(e.wave);
        const uint8_t* const data = static_cast<const uint8_t*>(c31_weak_get(e.data));
        if (wave == nullptr || data == nullptr || e.world != reinterpret_cast<uintptr_t>(world) ||
            static_cast<const uint8_t*>(wave)[kC112Activated] != 0) {
            c112_drop(i);
            continue;
        }
        if (e.players == players) {
            continue;
        }
        const C112Spawns before = c112_spawns(wave);
        const int32_t budget = c111_scale_budget(e.base, players);
        g_c112_set_procedural(wave, budget, data + kC112DataProcedural);
        const C112Spawns after = c112_spawns(wave);
        if (c111_line()) {
            log::line("card112: rescale %s (data %s): budget %d -> %d (base %d, %u -> %u players), spawn list %d entries "
                      "cost %d -> %d entries cost %d",
                      object_name(wave).text, object_name(data).text, c111_scale_budget(e.base, e.players), budget,
                      e.base, e.players, players, before.entries, before.cost, after.entries, after.cost);
        }
        e.players = players;
        ++rescaled;
    }
    if (c111_line()) {
        log::line("card112: %d waves rescaled for %u players, %d not started in the table", rescaled, players,
                  g_c112.count);
    }
}

const uint8_t* c111_container(const void* manager, uint8_t type) {
    SparseView v{};
    if (manager == nullptr || !sparse_view(static_cast<const uint8_t*>(manager) + kC111ManagerMap, kC111MapCap, v)) {
        return nullptr;
    }
    for (int32_t i = 0; i < v.count && i < kC111MapCap; ++i) {
        const uint8_t* const pair = v.data + static_cast<size_t>(i) * kC111PairBytes;
        if (sparse_allocated(v, i) && pair[0] == type) {
            return static_cast<const uint8_t*>(read_ptr(pair, kC111PairValue));
        }
    }
    return nullptr;
}

void c111_note_limit(void* manager, const char* how, uint8_t type, int32_t base, int32_t scaled, uint32_t players) {
    if (c31_weak_get(g_c111.manager) != manager) {
        const uint32_t lines = g_c111.lines;
        g_c111 = C111State{};
        g_c111.lines = lines;
        g_c111.players = players;
        c31_weak_set(g_c111.manager, manager);
    }
    if (type < kC111Types) {
        g_c111.base[type] = base;
    }
    if (c111_line()) {
        log::line("card111: ticket limit %s %s: base %d, scaled %d (%u players, factor %.2f)", how, c111_type(type),
                  base, scaled, players, c111_factor(players));
    }
}

void c111_note_peak(const void* manager, uint8_t type) {
    const uint8_t* const c = type < kC111Types ? c111_container(manager, type) : nullptr;
    if (c == nullptr) {
        return;
    }
    const int32_t used = read_i32(c, kC111Used);
    const int32_t holders = read_i32(c, kC111HolderNum) - read_i32(c, kC111HolderFree);
    if (used <= g_c111.peak_used[type] && holders <= g_c111.peak_holders[type]) {
        return;
    }
    g_c111.peak_used[type] = used > g_c111.peak_used[type] ? used : g_c111.peak_used[type];
    g_c111.peak_holders[type] = holders > g_c111.peak_holders[type] ? holders : g_c111.peak_holders[type];
    if (c111_line()) {
        log::line("card111: ticket peak %s: %d enemies hold %d points, limit %d (base %d)", c111_type(type), holders,
                  used, read_i32(c, kC111Limit), g_c111.base[type]);
    }
}

} // namespace

// Enemies holding `type` tickets on the current manager now, or -1 (card 122 window peak).
int32_t c111_holders(uint8_t type) {
    const uint8_t* const c = type < kC111Types ? c111_container(c31_weak_get(g_c111.manager), type) : nullptr;
    return c != nullptr ? read_i32(c, kC111HolderNum) - read_i32(c, kC111HolderFree) : -1;
}

namespace {

constexpr size_t kC122PrefPct = 0xC4;    // UADTargetComponent::AttackPlayerOverOtherTargetsPercentage (float)
constexpr size_t kC122PrefThreat = 0xC8; // AttackPlayerOverOtherTargetsThreatValue (float, not scaled)
constexpr float kC122PrefMax = 100.0f;
constexpr int32_t kC122PrefLogged = 32;  // (class, player count) pairs logged per map
struct C122Pref {
    uintptr_t world; // a new world clears the logged pairs
    const void* cls[kC122PrefLogged];
    uint32_t players[kC122PrefLogged];
    int32_t count;
};
C122Pref g_c122_pref{};

// Once per (enemy class, player count) per map: the base and scaled chance.
void c122_pref_log(const void* component, const void* world, uint32_t players, float base, float scaled) {
    if (reinterpret_cast<uintptr_t>(world) != g_c122_pref.world) {
        g_c122_pref = C122Pref{reinterpret_cast<uintptr_t>(world), {}, {}, 0};
    }
    const void* const owner = read_ptr(component, kComponentOwner);
    const void* const cls = owner != nullptr ? read_ptr(owner, kObjectClass) : nullptr;
    for (int32_t i = 0; i < g_c122_pref.count; ++i) {
        if (g_c122_pref.cls[i] == cls && g_c122_pref.players[i] == players) {
            return;
        }
    }
    if (g_c122_pref.count >= kC122PrefLogged) {
        return;
    }
    g_c122_pref.cls[g_c122_pref.count] = cls;
    g_c122_pref.players[g_c122_pref.count++] = players;
    if (c111_line()) {
        log::line("card122: %s player preference: AttackPlayerOverOtherTargetsPercentage base %.1f -> %.1f "
                  "(players %u, factor %.2f), threat bonus %.1f unchanged",
                  owner != nullptr ? class_name(owner).text : "?", base, scaled, players, c111_factor(players),
                  read_f32(component, kC122PrefThreat));
    }
}

} // namespace

// Host, before SelectPrimaryTarget: writes the scaled chance into the component and returns the base to put back
// (c122_pref_restore), or -1 when nothing was written (no world, an NPC trooper, chance off, or one player).
float c122_pref_scale(void* component) {
    const void* const world = component != nullptr ? c111_world(component) : nullptr;
    const void* const owner = world != nullptr ? read_ptr(component, kComponentOwner) : nullptr;
    // Bugs only: NPC troopers (class name holds "Trooper", as cards 025 and 122 tell them apart) keep their chance.
    if (owner == nullptr || strstr(class_name(owner).text, "Trooper") != nullptr) {
        return -1.0f;
    }
    const uint32_t players = c111_players(component);
    const float base = read_f32(component, kC122PrefPct);
    const float scaled = players > 1 && base > 0.0f ? fminf(base * c111_factor(players), kC122PrefMax) : base;
    c122_pref_log(component, world, players, base, scaled);
    if (scaled == base) {
        return -1.0f;
    }
    *reinterpret_cast<float*>(static_cast<uint8_t*>(component) + kC122PrefPct) = scaled;
    return base;
}

void c122_pref_restore(void* component, float base) {
    if (base >= 0.0f) {
        *reinterpret_cast<float*>(static_cast<uint8_t*>(component) + kC122PrefPct) = base;
    }
}

int32_t detour_c111_factor(const void* wave, void* const* data, int32_t budget) {
    c114_wave(data != nullptr ? *data : nullptr); // card 114: the file's values on this wave data CDO first
    const int32_t result = g_orig_c111_factor(wave, data, budget);
    if (g_client_session) {
        return result;
    }
    const uint32_t players = c111_players(c111_wave_ctx(wave));
    const int32_t scaled = c111_scale_budget(result, players);
    if (exe_rva(_ReturnAddress()) == kC112InitialiseRet) {
        c112_record(wave, data, result, players);
    }
    if (c111_line()) {
        log::line("card111: wave budget %s (outer %s, data %s): base %d, difficulty %d, scaled %d (%u players, caller exe+0x%llX)",
                  object_name(wave).text, class_name(read_ptr(wave, kObjectOuter)).text, data != nullptr ? object_name(*data).text : "-", budget, result, scaled,
                  players, exe_rva(_ReturnAddress()));
    }
    return scaled;
}

void detour_c111_create(void* manager, uint8_t type, int32_t limit, float rate) {
    if (g_client_session) {
        g_orig_c111_create(manager, type, limit, rate);
        return;
    }
    const uint32_t players = c111_players(manager);
    const int32_t scaled = c111_scale_limit(limit, players);
    c111_note_limit(manager, "create", type, limit, scaled, players);
    g_orig_c111_create(manager, type, scaled, rate);
}

bool detour_c111_try_refresh(void* manager, uint8_t type, int32_t limit, float rate) {
    if (g_client_session) {
        return g_orig_c111_try_refresh(manager, type, limit, rate);
    }
    const uint32_t players = c111_players(manager);
    const int32_t scaled = c111_scale_limit(limit, players);
    c111_note_limit(manager, "refresh", type, limit, scaled, players);
    return g_orig_c111_try_refresh(manager, type, scaled, rate);
}

bool detour_c111_request(void* manager, void* enemy, uint8_t type, int32_t cost, bool steal) {
    const bool ok = g_orig_c111_request(manager, enemy, type, cost, steal);
    if (ok && !g_client_session) {
        c111_note_peak(manager, type);
    }
    return ok;
}

// Log only: a recorded wave's spawn list as it starts spawning (the card 112 spawn check); it leaves the table.
void detour_c112_activate(void* wave) {
    for (int32_t i = 0; !g_client_session && i < g_c112.count; ++i) {
        if (c31_weak_get(g_c112.w[i].wave) != wave) {
            continue;
        }
        const C112Spawns s = c112_spawns(wave);
        if (c111_line()) {
            log::line("card112: activate %s: spawn list %d entries cost %d (budget %d, %u players)",
                      object_name(wave).text, s.entries, s.cost,
                      c111_scale_budget(g_c112.w[i].base, g_c112.w[i].players), g_c112.w[i].players);
        }
        c112_drop(i);
        break;
    }
    g_orig_c112_activate(wave);
}

// Host, every tick: when the player count changes in a world whose ticket manager is known, refresh the limits once.
void c111_step(void* engine) {
    void* const world = g_client_session || g_c111_refresh == nullptr ? nullptr : g_get_game_world(engine);
    // Card 349: any PC in the world will do (net mode only); not the local PC.
    void* const pc = world != nullptr ? g_world_first_pc(world) : nullptr;
    if (pc != nullptr && net_mode(pc) == kNetModeStandalone && g_c113.players != 0) {
        // The co-op session is over (menu or a solo map): solo waves must not keep the session's count.
        if (c111_line()) {
            log::line("card113: session ended (standalone world), session players %u -> 0", g_c113.players);
        }
        g_c113 = C113Session{};
    }
    if (pc == nullptr || net_mode(pc) == kNetModeStandalone) {
        return;
    }
    if (reinterpret_cast<uintptr_t>(world) != g_c112.world) {
        // Waves of the new world may already be in the table (set up at load, before this first tick); keep those.
        for (int32_t i = g_c112.count - 1; i >= 0; --i) {
            if (g_c112.w[i].world != reinterpret_cast<uintptr_t>(world)) {
                c112_drop(i);
            }
        }
        g_c112.full_logged = false;
        g_c112.world = reinterpret_cast<uintptr_t>(world);
    }
    c113_step(world, GetTickCount64());
    void* const manager = c31_weak_get(g_c111.manager);
    const uint32_t players = manager != nullptr ? c111_players(manager) : 0;
    if (manager == nullptr || players == g_c111.players) {
        return;
    }
    const uint32_t was = g_c111.players;
    g_c111.players = players;
    if (c111_line()) {
        log::line("card111: players %u -> %u, refreshing ticket limits", was, players);
    }
    c112_rescale(world, players);
    g_c111_refresh(manager);
}

} // namespace ubw::hooks
