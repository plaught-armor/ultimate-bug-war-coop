// Crash and side guards: the remote-PC UpdateBindings guard (card 003), the SetPlayer guard (card 004), the
// client SetupInputComponent GameMode-null and mech input-context guards (card 005), the client gate for
// server-only encounter and spawn logic (card 006), the remote-PC idle check guard (card 007) and the
// SpawnSoundAttachedToComponentWithAIAwareness null-component guard (card 052).

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

UpdateBindingsFn g_orig_update_bindings = nullptr;
uint32_t g_update_bindings_skips = 0; // game thread only (UpdateBindings runs there); no lock

void detour_update_bindings(void* local_player, const void* settings) {
    if (local_player != nullptr) {
        g_orig_update_bindings(local_player, settings);
        return;
    }
    if (note_skip(g_update_bindings_skips, "UpdateBindings")) {
        const auto* ret = static_cast<const uint8_t*>(_ReturnAddress());
        log::line("guard: UpdateBindings(LocalPlayer=null, settings=%p) skipped, caller exe+0x%llX",
                  settings, static_cast<unsigned long long>(ret - g_exe_base));
    }
}

SetPlayerFn g_orig_st_set_player = nullptr;
SetPlayerFn g_pc_set_player = nullptr;          // APlayerController::SetPlayer (Super), called
StaticClassFn g_local_player_class = nullptr;   // ULocalPlayer::StaticClass(), called
uint32_t g_set_player_skips = 0; // game thread only (SetPlayer runs there); no lock

void detour_st_set_player(void* self, void* player) {
    void* const slot = player; // FObjectPtr::IsA reads the object through a pointer slot
    if (player != nullptr && g_object_ptr_is_a(&slot, g_local_player_class())) {
        g_orig_st_set_player(self, player);
        return;
    }
    g_pc_set_player(self, player);
    if (note_skip(g_set_player_skips, "ASTPlayerController::SetPlayer")) {
        log::line("guard: ASTPlayerController::SetPlayer(pc=%p, player=%p) not a ULocalPlayer: Super only, "
                  "input-mapping tail skipped",
                  self, player);
    }
}

using RequestRebuildFn = void (*)(void* iface, const uint8_t* options, uint8_t rebuild_type);
GetGameModeFn g_orig_get_game_mode = nullptr;
uint8_t* g_setup_input_component = nullptr; // ASTPlayerController::SetupInputComponent
LoadSynchronousFn g_load_synchronous = nullptr;
StaticClassFn g_mappable_config_class = nullptr;
MarkAsReachableFn g_mark_as_reachable = nullptr;
ReceivedGameModeClassFn g_received_game_mode_class = nullptr; // folded empty stub
// Set once in ubw::init from SetupInputComponent's own code, before any hook is enabled.
const void* g_setup_input_get_game_mode_ret = nullptr; // SetupInputComponent+0x5C
const volatile uint8_t* g_gc_barrier_flag = nullptr;   // the byte SetupInputComponent tests

constexpr uint32_t kSetupInputCallGetGameMode = 0x57; // e8 rel32 -> UGameplayStatics::GetGameMode
constexpr uint32_t kSetupInputBarrierFlagLoad = 0x5C; // 0f b6 0d disp32: movzx ecx, byte [rip+d]
constexpr size_t kWorldGameState = 0x1B0;             // UWorld::GameState
constexpr size_t kGameStateGameModeClass = 0x2A8;     // AGameStateBase::GameModeClass
constexpr size_t kPcStartingMappings = 0x838;         // ASTPlayerController::StartingPlayerInputMappings
constexpr size_t kPcReceivedGameModeClassSlot = 0xEC0 / sizeof(void*);
constexpr size_t kStartingMappingStride = 0x38; // TSet element: key @0, TSoftObjectPtr @8

constexpr size_t kIfaceRequestRebuild = 0xD8 / sizeof(void*);
constexpr uint8_t kOptionsNotifyUserSettings = 0x4;  // what SetupInputComponent passes to Add
constexpr uint8_t kOptionsRebuild = 0x3;             // IgnoreAllPressedKeysUntilRelease | ForceImmediately
constexpr uint8_t kRebuildTypeRebuild = 1;           // EInputMappingRebuildType::Rebuild
constexpr int32_t kMaxStartingMappings = 256;

struct FakeGameMode {
    uint8_t unused[kObjectClass];
    const void* game_mode_class; // read by SetupInputComponent+0x63 as GameMode->ClassPrivate
};
static_assert(sizeof(FakeGameMode) == kObjectClass + sizeof(void*), "key must sit at +0x10");
// Game thread only. Written by detour_get_game_mode and read by the caller that it returns to,
// before any other code runs; the stale class pointer left in it is never read again.
FakeGameMode g_fake_game_mode{};
bool g_starting_mappings_pending = false; // game thread only
uint32_t g_client_input_logs = 0;         // game thread only

// The replicated GameModeClass, or null when the client has no GameState (or class) yet.
const void* replicated_game_mode_class(const void* pc) {
    const auto* const world = static_cast<const uint8_t*>(g_actor_get_world(pc));
    if (world == nullptr) {
        return nullptr;
    }
    const auto* const game_state = *reinterpret_cast<const uint8_t* const*>(world + kWorldGameState);
    if (game_state == nullptr) {
        return nullptr;
    }
    return *reinterpret_cast<const void* const*>(game_state + kGameStateGameModeClass);
}

// True if `p` lies in a readable, non-executable section of the exe image (.rdata vtables).
bool in_exe_rdata(const void* p) {
    const IMAGE_NT_HEADERS64* const nt = nt_headers(g_exe_base);
    const auto* const q = static_cast<const uint8_t*>(p);
    if (nt == nullptr || q < g_exe_base) {
        return false;
    }
    const auto rva = static_cast<uint64_t>(q - g_exe_base);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const IMAGE_SECTION_HEADER& s = sec[i];
        if (rva >= s.VirtualAddress && rva + sizeof(void*) <= s.VirtualAddress + s.Misc.VirtualSize) {
            return (s.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0 && (s.Characteristics & IMAGE_SCN_MEM_READ) != 0;
        }
    }
    return false;
}

void detour_received_game_mode_class(void* pc, const void* const* game_mode_class);

// Points this PC's vtable slot for ReceivedGameModeClass at the deferred-apply detour. The slot
// is in the exe's .rdata (shared by every object of that class); idempotent.
bool arm_received_game_mode_class(const void* pc) {
    void** const slot = *static_cast<void** const*>(pc) + kPcReceivedGameModeClassSlot;
    void* const detour = reinterpret_cast<void*>(&detour_received_game_mode_class);
    if (!in_exe_rdata(slot)) {
        log::line("ERROR client-input: PC %p vtable slot %p is not in the exe's .rdata", pc,
                  static_cast<void*>(slot));
        return false;
    }
    if (*slot == detour) {
        return true;
    }
    if (*slot != reinterpret_cast<void*>(g_received_game_mode_class)) {
        log::line("ERROR client-input: PC vtable slot 0xEC0 = %p, expected the empty stub %p", *slot,
                  reinterpret_cast<void*>(g_received_game_mode_class));
        return false;
    }
    DWORD old_protect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_protect)) {
        log::line("ERROR client-input: VirtualProtect(%p, RW) failed, err=%lu", static_cast<void*>(slot),
                  GetLastError());
        return false;
    }
    *slot = detour; // aligned 8-byte store; the slot is only read on the game thread
    DWORD unused = 0;
    if (!VirtualProtect(slot, sizeof(void*), old_protect, &unused)) {
        log::line("ERROR client-input: VirtualProtect(%p, restore 0x%lx) failed, err=%lu", static_cast<void*>(slot),
                  old_protect, GetLastError());
    }
    log::line("client-input: vtable slot %p (ReceivedGameModeClass) now defers starting input mappings",
              static_cast<void*>(slot));
    return true;
}

// Supplies SetupInputComponent's lookup key on a client (GameMode null). Returns the fake block.
void* client_setup_input_key(const void* pc) {
    const void* key = replicated_game_mode_class(pc);
    if (key != nullptr) {
        g_starting_mappings_pending = false;
    } else {
        key = *reinterpret_cast<const void* const*>(static_cast<const uint8_t*>(pc) + kObjectClass);
        g_starting_mappings_pending = arm_received_game_mode_class(pc);
    }
    if (note_skip(g_client_input_logs, "client-input")) {
        log::line("client-input: SetupInputComponent(pc=%p) on a client: key %s %p%s", pc,
                  g_starting_mappings_pending ? "PC class (miss)" : "GameModeClass", key,
                  g_starting_mappings_pending ? ", contexts deferred to ReceivedGameModeClass" : "");
    }
    g_fake_game_mode.game_mode_class = key;
    return &g_fake_game_mode;
}

void* detour_get_game_mode(const void* world_context) {
    void* const game_mode = g_orig_get_game_mode(world_context);
    if (game_mode != nullptr || _ReturnAddress() != g_setup_input_get_game_mode_ret) {
        return game_mode;
    }
    return client_setup_input_key(world_context);
}

// The StartingPlayerInputMappings element whose key is `mode_class`, or null.
const uint8_t* find_starting_mapping(const void* pc, const void* mode_class) {
    SparseView v{};
    if (!sparse_view(static_cast<const uint8_t*>(pc) + kPcStartingMappings, kMaxStartingMappings, v)) {
        return nullptr;
    }
    for (int32_t i = 0; i < v.count; ++i) {
        const uint8_t* const e = v.data + static_cast<size_t>(i) * kStartingMappingStride;
        if (sparse_allocated(v, i) && *reinterpret_cast<const void* const*>(e) == mode_class) {
            return e;
        }
    }
    return nullptr;
}

int32_t add_config_contexts(void* iface, const void* soft_config) {
    SparseView v{};
    if (!load_config_contexts(soft_config, "starting input config", v)) {
        return -1;
    }
    const auto add = reinterpret_cast<AddMappingContextFn>((*static_cast<void* const* const*>(iface))[kIfaceAddMappingContext]);
    const uint8_t options = kOptionsNotifyUserSettings;
    int32_t added = 0;
    for (int32_t i = 0; i < v.count; ++i) {
        if (!sparse_allocated(v, i)) {
            continue;
        }
        int32_t priority = 0;
        const void* const context = config_context(v, i, &priority);
        add(iface, context, priority, &options);
        ++added;
    }
    return added;
}

// Deferred half of the client fix: the contexts SetupInputComponent could not add, then the
// same RequestRebuildControlMappings the original issues after them.
void apply_starting_mappings(void* pc, void* local_player, const void* mode_class) {
    auto* const subsystem = static_cast<uint8_t*>(g_get_enhanced_input(local_player));
    if (subsystem == nullptr) {
        log::line("ERROR client-input: pc=%p has no EnhancedInput subsystem; starting mappings not applied", pc);
        return;
    }
    void* const iface = subsystem + kSubsystemInputInterface;
    const uint8_t* const entry = find_starting_mapping(pc, mode_class);
    const int32_t added = entry != nullptr ? add_config_contexts(iface, entry + 0x8) : 0;
    const auto rebuild = reinterpret_cast<RequestRebuildFn>((*static_cast<void* const* const*>(iface))[kIfaceRequestRebuild]);
    const uint8_t options = kOptionsRebuild;
    rebuild(iface, &options, kRebuildTypeRebuild);
    log::line("client-input: ReceivedGameModeClass(pc=%p, class=%p): %s, %d context(s) added, rebuild requested", pc,
              mode_class, entry != nullptr ? "mapping found" : "no mapping for this class", added);
}

void clear_pending_starting_mappings() {
    g_starting_mappings_pending = false;
}

// Patched into the PC vtable (slot 0xEC0) by arm_received_game_mode_class.
void detour_received_game_mode_class(void* pc, const void* const* game_mode_class) {
    g_received_game_mode_class(pc, game_mode_class); // the empty original, for fidelity
    if (!g_starting_mappings_pending || game_mode_class == nullptr || *game_mode_class == nullptr) {
        return;
    }
    if (g_orig_get_game_mode(pc) != nullptr) {
        return; // authority: SetupInputComponent's own lookup already ran with the real GameMode
    }
    void* const local_player = g_get_local_player(pc);
    if (local_player == nullptr) {
        return;
    }
    g_starting_mappings_pending = false;
    apply_starting_mappings(pc, local_player, *game_mode_class);
}

MechInputFn g_orig_add_mech_input = nullptr;
MechInputFn g_orig_remove_mech_input = nullptr;
uint32_t g_mech_input_skips = 0; // game thread only

bool mech_has_local_input(const void* mech) {
    const void* const pc = g_weak_get(static_cast<const uint8_t*>(mech) + kMechController);
    if (pc == nullptr) {
        return false;
    }
    const void* const local_player = g_get_local_player(pc);
    return local_player != nullptr && g_get_enhanced_input(local_player) != nullptr;
}

void guard_mech_input(MechInputFn original, void* mech, const char* what) {
    if (mech_has_local_input(mech)) {
        original(mech);
        return;
    }
    if (note_skip(g_mech_input_skips, "ASTPlayerMechBase input context")) {
        log::line("guard: ASTPlayerMechBase::%s(mech=%p) skipped: controller not local", what, mech);
    }
}

void detour_add_mech_input(void* mech) {
    guard_mech_input(g_orig_add_mech_input, mech, "AddMechInputContext");
}

void detour_remove_mech_input(void* mech) {
    guard_mech_input(g_orig_remove_mech_input, mech, "RemoveMechInputContext");
}

EncounterFn g_orig_start_encounter = nullptr;
EncounterFn g_orig_activate_current_wave = nullptr;
PerformSpawnFn g_orig_perform_spawn = nullptr;
QueueSpawnFn g_orig_queue_spawn = nullptr;
uint32_t g_encounter_skips = 0; // game thread only (Blueprint VM, actor tick)
uint32_t g_spawn_skips = 0;     // game thread only (spawner tick, Blueprint VM)

// FSTSpawnResult as ASTSpawnerBase::PerformSpawn's prologue writes it: FString message at +0x00,
// params+0x10 copied to +0x10, TArray at +0x20 (freed by TickQueuedSpawnRequests), flag bytes
// from +0x30; the early-out path then sets +0x32 = 1. The prologue stores through +0x3B.
constexpr size_t kSpawnResultWritten = 0x3C;
constexpr size_t kSpawnResultSource = 0x10;
constexpr size_t kSpawnResultFailed = 0x32;
constexpr size_t kFStringBytes = 0x10; // TArray<TCHAR>: data pointer, num, max

void gate_encounter(EncounterFn original, void* manager, const char* what) {
    if (!is_net_client(manager)) {
        original(manager);
        return;
    }
    if (note_skip(g_encounter_skips, "client encounter")) {
        log::line("guard: ASTEncounterManagerBase::%s(%p) skipped on client", what, manager);
    }
}

// Card 245 (b190): host/standalone StartEncounter rows, so a level chain's encounter (C4M2 EM_47 after the
// FlameThrower_C_0 pickup) shows in the log with its caller.
constexpr uint32_t kC245EncounterLines = 60;
uint32_t g_c245_encounter_lines = 0; // game thread

void detour_start_encounter(void* manager) {
    if (!is_net_client(manager) && note_event_cap(g_c245_encounter_lines, kC245EncounterLines, "card245 encounter")) {
        log::line("card245: %s t=%llu StartEncounter on %s (%s), caller exe+0x%llX", side(),
                  static_cast<unsigned long long>(GetTickCount64()), object_name(manager).text,
                  class_name(manager).text, static_cast<unsigned long long>(exe_rva(_ReturnAddress())));
    }
    gate_encounter(g_orig_start_encounter, manager, "StartEncounter");
}

void detour_activate_current_wave(void* manager) {
    gate_encounter(g_orig_activate_current_wave, manager, "ActivateCurrentWave");
}

void* detour_perform_spawn(void* spawner, void* result, const void* params) {
    if (!is_net_client(spawner)) {
        return g_orig_perform_spawn(spawner, result, params);
    }
    auto* const out = static_cast<uint8_t*>(result);
    memset(out, 0, kSpawnResultWritten);
    memcpy(out + kSpawnResultSource, static_cast<const uint8_t*>(params) + kSpawnResultSource, sizeof(void*));
    out[kSpawnResultFailed] = 1;
    if (note_skip(g_spawn_skips, "client spawn")) {
        log::line("guard: ASTSpawnerBase::PerformSpawn(%p) skipped on client", spawner);
    }
    return result;
}

void* detour_queue_spawn(void* spawner, void* result, const void* params, float delay) {
    if (!is_net_client(spawner)) {
        return g_orig_queue_spawn(spawner, result, params, delay);
    }
    memset(result, 0, kFStringBytes); // empty FString: no buffer, nothing for the caller to free
    if (note_skip(g_spawn_skips, "client spawn")) {
        log::line("guard: ASTSpawnerBase::QueueSpawnRequest(%p) skipped on client", spawner);
    }
    return result;
}

IsPressedFn g_orig_is_pressed = nullptr;
uint8_t* g_st_pc_tick = nullptr;                // ASTPlayerController::Tick, not hooked
const void* g_tick_is_pressed_ret = nullptr;    // Tick+0x122; set once in ubw::init
uint32_t g_is_pressed_skips = 0;                // game thread only (actor tick); no lock

constexpr uint32_t kTickLoadPlayerInput = 0xEB;   // 48 8b 8b 20 04 00 00: mov rcx, [rbx+0x420]
constexpr uint32_t kTickCallIsPressed = 0x11D;    // e8 rel32 -> UPlayerInput::IsPressed
constexpr uint32_t kTickIsPressedReturn = 0x122;  // 84 c0: test al, al

bool detour_is_pressed(const void* player_input, void* key) {
    if (player_input != nullptr || _ReturnAddress() != g_tick_is_pressed_ret) {
        return g_orig_is_pressed(player_input, key);
    }
    g_fkey_destructor(key);
    if (note_skip(g_is_pressed_skips, "remote-PC IsPressed")) {
        log::line("guard: UPlayerInput::IsPressed(this=null) from ASTPlayerController::Tick: returned false");
    }
    return false;
}

// Validates Tick's PlayerInput load, its IsPressed call and the test after it, and records the
// return address. Runs in ubw::init before any hook.
bool resolve_tick_is_pressed_site() {
    static constexpr uint8_t kLoad[] = {0x48, 0x8B, 0x8B, 0x20, 0x04, 0x00, 0x00};
    const uint8_t* const load = g_st_pc_tick + kTickLoadPlayerInput;
    const uint8_t* const call = g_st_pc_tick + kTickCallIsPressed;
    const uint8_t* const ret = g_st_pc_tick + kTickIsPressedReturn;
    int32_t rel = 0;
    memcpy(&rel, call + 1, sizeof(rel));
    const uint8_t* const want = g_exe_base + offsets::UPlayerInput_IsPressed.rva;
    if (memcmp(load, kLoad, sizeof(kLoad)) != 0 || call[0] != 0xE8 || ret + rel != want || ret[0] != 0x84 ||
        ret[1] != 0xC0) {
        log::line("FATAL hooks: ASTPlayerController::Tick+0xEB/+0x11D is not "
                  "`mov rcx, [rbx+0x420]` ... `call IsPressed; test al, al`");
        return false;
    }
    g_tick_is_pressed_ret = ret;
    log::line("hooks: ASTPlayerController::Tick IsPressed return site %p", static_cast<const void*>(ret));
    return true;
}

// Validates SetupInputComponent's GetGameMode call site and the barrier-flag load after it,
// and records the return address and flag address. Runs in ubw::init before any hook.
bool resolve_setup_input_sites() {
    const uint8_t* const call = g_setup_input_component + kSetupInputCallGetGameMode;
    const uint8_t* const load = g_setup_input_component + kSetupInputBarrierFlagLoad;
    int32_t rel = 0;
    memcpy(&rel, call + 1, sizeof(rel));
    const uint8_t* const callee = load + rel;
    const uint8_t* const want = g_exe_base + offsets::UGameplayStatics_GetGameMode.rva;
    if (call[0] != 0xE8 || callee != want || load[0] != 0x0F || load[1] != 0xB6 || load[2] != 0x0D) {
        log::line("FATAL hooks: SetupInputComponent+0x57/+0x5C is not `call GetGameMode; movzx ecx, byte [rip+d]`");
        return false;
    }
    int32_t disp = 0;
    memcpy(&disp, load + 3, sizeof(disp));
    g_setup_input_get_game_mode_ret = load;
    g_gc_barrier_flag = load + 7 + disp;
    log::line("hooks: SetupInputComponent GetGameMode return site %p, GC barrier flag %p",
              static_cast<const void*>(load), static_cast<const volatile void*>(g_gc_barrier_flag));
    return true;
}

SpawnSoundAttachedFn g_orig_spawn_sound_attached = nullptr;
uint32_t g_spawn_sound_skips = 0; // game thread only

void* detour_spawn_sound_attached(void* component, uint64_t attach_point, uint64_t tag, Vec3 location,
                                  bool stop_when_destroyed, float volume, float pitch, float start_time,
                                  void* attenuation, void* concurrency, bool auto_destroy) {
    if (component != nullptr) {
        return g_orig_spawn_sound_attached(component, attach_point, tag, location, stop_when_destroyed, volume, pitch,
                                           start_time, attenuation, concurrency, auto_destroy);
    }
    if (note_skip(g_spawn_sound_skips, "SpawnSoundAttachedToComponentWithAIAwareness")) {
        const auto* ret = static_cast<const uint8_t*>(_ReturnAddress());
        log::line("guard: %s SpawnSoundAttachedToComponentWithAIAwareness(component=null) skipped, returned null, "
                  "caller exe+0x%llX",
                  side(), static_cast<unsigned long long>(ret - g_exe_base));
    }
    return nullptr;
}

SkelAnimStartFn g_orig_skel_anim_start = nullptr;
uint32_t g_skel_anim_start_skips = 0; // game thread only

void detour_skel_anim_start(void* animator, uint64_t key, float rate) {
    constexpr size_t kMesh = 0xF8; // USkeletalMeshComponent* the animator plays on
    void* mesh = nullptr;
    memcpy(&mesh, static_cast<const uint8_t*>(animator) + kMesh, sizeof(mesh));
    if (mesh != nullptr) {
        g_orig_skel_anim_start(animator, key, rate);
        return;
    }
    if (note_skip(g_skel_anim_start_skips, "UADSkeletalAnimatorComponent::StartAnimationSequence")) {
        const auto* ret = static_cast<const uint8_t*>(_ReturnAddress());
        log::line("guard: %s UADSkeletalAnimatorComponent::StartAnimationSequence(mesh=null) skipped, caller exe+0x%llX",
                  side(), static_cast<unsigned long long>(ret - g_exe_base));
    }
}

} // namespace ubw::hooks
