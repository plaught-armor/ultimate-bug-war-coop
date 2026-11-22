// Card 121: a client in a co-op session runs no save engine save or load (see hooks_internal.h).
// The client never loads a checkpoint of its own (card 048: its pawn's save id never matches; card 045 restarts and
// card 113 travel rejoin from the host), so a client save had no reader, and its pre-save callbacks walked state the
// client never filled in: card 119's bare copies, then a player pawn's USTSquadLeaderComponent with a null squad
// member (OnPreSave_Implementation+0x6d, GetNPCGuid on null). Skipping at PerformSave covers every saveable class.

#include <intrin.h>

#include <cstring>

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

C121PerformSaveFn g_orig_c121_perform_save = nullptr;
C121LoadFn g_orig_c121_load = nullptr;
C137LoadSlotFn g_orig_c137_load_slot = nullptr;

namespace {

constexpr uint32_t kC121Lines = 40;           // skip lines per map
constexpr unsigned long long kC121SaveCheckPointCall = 0x52186DF; // Save_CheckPoint's PerformSave call (return address)
constexpr size_t kPawnSquadLeader = 0x868;    // ASTPlayerCharacter::GetSquadLeaderComponent
// USTSquadLeaderComponent: live squad TArray<ASTNonPlayableCharacterBase*> (GetSquadMembers, CheckForSquadRemovals).
// OnPreSave_Implementation reads it as +0x40 from its ISTSavableComponentInterface `this` (component + 0xB8).
constexpr size_t kSquadMembers = 0xF8;
constexpr int32_t kMaxSquad = 64;
uint32_t g_c121_lines = 0;
bool g_c121_squads_logged = false;

// PerformSave's completion TDelegate<void(bool, const FSTSaveGameMetadata&, USTCampaignSlotsSaveGame*)>: the bound
// instance pointer at +0 and the allocation size at +8 (zero when unbound). FSaveTaskThread::Run (exe+0x521501A)
// executes it this way at the end of a real save: size != 0 and instance != null, then IsSafeToExecute (vtable +0x30),
// then Execute (vtable +0x58) with (instance, success, metadata, slots).
constexpr size_t kDelegateInstance = 0x0;
constexpr size_t kDelegateSize = 0x8;
constexpr size_t kInstanceIsSafe = 0x30;
constexpr size_t kInstanceExecute = 0x58;
using DelegateIsSafeFn = bool (*)(const void* instance);
using DelegateExecuteFn = void (*)(void* instance, bool success, const void* metadata, void* slots);

// Completes a skipped client save the way a failed save completes: the caller's delegate runs with success false.
// Every PerformSave caller's lambda (Save_CheckPoint, Save_Manual, Save_Quicksave) returns at once on false; on true
// it would record the save in the campaign slot tracker, which needs the slots object a skipped save never makes.
const char* c121_complete_skipped_save(void* delegate, const void* metadata) {
    if (delegate == nullptr || read_i32(delegate, kDelegateSize) == 0) {
        return "unbound";
    }
    void* const instance = read_ptr(delegate, kDelegateInstance);
    if (instance == nullptr) {
        return "unbound";
    }
    void* const* const vtable = static_cast<void* const*>(read_ptr(instance, 0));
    const auto is_safe = reinterpret_cast<DelegateIsSafeFn>(vtable[kInstanceIsSafe / sizeof(void*)]);
    if (!is_safe(instance)) {
        return "not safe to execute";
    }
    const auto execute = reinterpret_cast<DelegateExecuteFn>(vtable[kInstanceExecute / sizeof(void*)]);
    execute(instance, false, metadata, nullptr);
    return "delegate executed (success 0)";
}

// Diagnostic, once per map at the first skipped save: each player pawn's squad, with its null members. These are
// the members the skipped OnPreSave would have passed to GetNPCGuid.
void c121_squad_lines(const void* world) {
    const void* const cls = g_player_character_class(); // ASTPlayerCharacter owns the component
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const void* const level = levels[l];
        const auto* const actors = level != nullptr ? static_cast<void* const*>(read_ptr(level, kLevelActors)) : nullptr;
        const int32_t num = level != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; actors != nullptr && a < num && a < kMaxLevelActors; ++a) {
            const void* const pawn = actors[a];
            if (pawn == nullptr || !is_a(pawn, cls)) {
                continue;
            }
            const void* const squad = read_ptr(pawn, kPawnSquadLeader);
            const auto* const members = squad != nullptr ? static_cast<void* const*>(read_ptr(squad, kSquadMembers)) : nullptr;
            const int32_t n = squad != nullptr ? read_i32(squad, kSquadMembers + kArrayNum) : 0;
            int32_t nulls = 0;
            for (int32_t i = 0; members != nullptr && i < n && i < kMaxSquad; ++i) {
                nulls += members[i] == nullptr ? 1 : 0;
            }
            log::line("card121: client   squad of %s (%s): component %p, %d members, %d null", object_name(pawn).text, kSprintSideText[sprint_side(pawn)], squad, n, nulls);
        }
    }
}

} // namespace

void detour_c121_perform_save(void* world, void* metadata, void* delegate, bool flag) {
    if (!g_client_session) {
        if (note_event_cap(g_c121_lines, kC121Lines, "card121 save")) {
            log::line("card121: %s t=%llu save engine save runs (PerformSave, flag %d, caller exe+0x%llX)", side(),
                      static_cast<unsigned long long>(GetTickCount64()), flag ? 1 : 0, exe_rva(_ReturnAddress()));
        }
        if (exe_rva(_ReturnAddress()) == kC121SaveCheckPointCall && world != nullptr) {
            c46_note_checkpoint(world); // card 146: the rejoin and all-down kill floor spot
        }
        // Card 382: saved player pawn rows hold the PC list index (card 375). A save taken after a seamless travel
        // while the client's PC still waits for its swap has the list as [client, host]; loaded later with the list
        // as [host, client] (or [host] alone) the rows pair with the wrong PC. Every save is taken host first.
        const int host_slot = world != nullptr ? c375_host_pc_first(world) : 0;
        if (host_slot > 0) {
            log::line("card382: host t=%llu save: host PC moved from list slot %d to slot 0 before the save",
                      static_cast<unsigned long long>(GetTickCount64()), host_slot);
        }
        g_orig_c121_perform_save(world, metadata, delegate, flag);
        return;
    }
    const char* const completion = c121_complete_skipped_save(delegate, metadata);
    if (note_event_cap(g_c121_lines, kC121Lines, "card121 save skip")) {
        log::line("card121: client t=%llu save engine save skipped (PerformSave, flag %d, caller exe+0x%llX): %s",
                  static_cast<unsigned long long>(GetTickCount64()), flag ? 1 : 0, exe_rva(_ReturnAddress()),
                  completion);
    }
    if (!g_c121_squads_logged && world != nullptr) {
        g_c121_squads_logged = true;
        c121_squad_lines(world);
    }
}

bool g_c121_in_place_load = false;

void detour_c121_load(void* context, void* save_data, void* metadata, bool* file_io_success, bool load_pawns,
                      bool destroy_actors) {
    if (!g_client_session) {
        // Card 132 point 1: the host's in-place loads (quickload, LoadLatestSave) are the continued-save case.
        log::line("card121: %s t=%llu save engine load runs (Load, context %s, load pawns %d, destroy actors %d, caller "
                  "exe+0x%llX)", side(), static_cast<unsigned long long>(GetTickCount64()), object_name(context).text,
                  load_pawns ? 1 : 0, destroy_actors ? 1 : 0, exe_rva(_ReturnAddress()));
        if (load_pawns) {
            c294_host_load(strstr(object_name(context).text, "DeathScreen") != nullptr ? "death-screen load"
                                                                                       : "in-place load");
        }
        g_c121_in_place_load = load_pawns;
        g_orig_c121_load(context, save_data, metadata, file_io_success, load_pawns, destroy_actors);
        g_c121_in_place_load = false;
        log::line("card121: %s t=%llu save engine load done (file io success %d)", side(),
                  static_cast<unsigned long long>(GetTickCount64()),
                  file_io_success != nullptr && *file_io_success ? 1 : 0);
        if (load_pawns) {
            c132_decal_respawn_totals(); // card 309 / b12 item 5: every host in-place load, down state or not
        }
        return;
    }
    if (file_io_success != nullptr) {
        *file_io_success = false; // LoadLatestSave returns it; Load_Quicksave passes it on
    }
    if (note_event_cap(g_c121_lines, kC121Lines, "card121 load skip")) {
        log::line("card121: client t=%llu save engine load skipped (Load, context %s, caller exe+0x%llX)",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(context).text,
                  exe_rva(_ReturnAddress()));
    }
}

// Card 137 step 3: the reconcile's per-row lines, capped per load (the totals line always logs).
bool c137_load_line(C137LoadCounts& n) {
    constexpr uint32_t kC137LoadLines = 40;
    return note_event_cap(n.lines, kC137LoadLines, "card137 load reconcile");
}

// Card 137 step 3: host. ASTSaveSystemEngine::LoadFromSlot covers both load routes (the in-map restore, as the travel
// checkpoint and quickload do, and the frontend Continue route, which loads after OpenLevel's map load, so after the
// journals were armed). It loads every saved actor synchronously, so at its return the actors hold the save's state
// and each host journal owner rebuilds its rows from them (two-way: rows the save undid go, kills the save holds that
// no OnDeath ever journalled are added).
void detour_c137_load_slot(void* engine, void* slot_name, bool a, bool b, const void* data) {
    g_orig_c137_load_slot(engine, slot_name, a, b, data);
    void* const world = !g_client_session && engine != nullptr ? g_actor_get_world(engine) : nullptr;
    if (world == nullptr) {
        return;
    }
    C137LoadCounts n{};
    sync_after_load(world, n); // card 326: each registered owner's after-load pass (world, then door)
    log::line("card137: host t=%llu journal reconcile after LoadFromSlot (caller exe+0x%llX): c98 K dropped %u added %u, "
              "H fixed %u, S dropped %u added %u; c145 E fixed %u added %u; c95 doors fixed %u added %u; c95 collision fixed %u, removed %u "
              "(rows %u -> %u); %d actors",
              static_cast<unsigned long long>(GetTickCount64()), exe_rva(_ReturnAddress()), n.k_dropped, n.k_added,
              n.h_fixed, n.s_dropped, n.s_added, n.e_fixed, n.e_added, n.door_fixed, n.door_added, n.c95c_fixed, n.c95c_removed,
              n.c95c_before, n.c95c_after, n.actors);
    c143_after_load(world); // card 143: each client gets its copy's restored state and rejoins
}

void c121_arm_map() {
    g_c121_lines = 0;
    g_c121_squads_logged = false;
}

// --- Card 294: a host in-place load is a restart the client follows as one transition ---
// The load runs inside a widget click with no engine at hand; c290_host_travel needs it to find the world. The engine
// is the one game engine for the process, noted at every engine tick, so the travel row goes from the load hook
// itself, before the load runs (b8: the row sent a tick later queued behind the load's own burst of frames).
namespace {
void* g_c294_engine = nullptr; // host: the engine of the last tick; game thread
} // namespace

void c294_host_load(const char* reason) {
    if (g_c294_engine == nullptr) {
        log::line("WARN card294: host t=%llu %s before any engine tick: no travel row",
                  static_cast<unsigned long long>(GetTickCount64()), reason);
        return;
    }
    c290_host_travel(g_c294_engine, reason);
}

void c294_host_step(void* engine) {
    g_c294_engine = engine;
}

} // namespace ubw::hooks
