#pragma once

// The one internal header of the ubw::hooks files (ubw/hooks_ubw.cpp and the files split out of the old
// hooks.cpp): the shared includes and using-directives, and only what crosses a file boundary: extern declarations of
// globals defined in one file and used in another, declarations of functions called across files, and
// the constants, structs and type aliases used by more than one file.

#include "autoinput.h"
#include "cmdfile.h"
#include "ue/config.h"
#include "ubw/flags.h"
#include "lobby.h"
#include "upnp.h"
#include "ue/log.h"
#include "ue/reflect.h"
#include "ue/actor.h"
#include "ue/gas.h"
#include "ue/input.h"
#include "ue/widget.h"
#include "ue/hooktable.h"
#include "ue/net.h"
#include "ue/chan.h"
#include "ubw/chan_ops.h"
#include "offsets.generated.h"
#include <windows.h>
#include <MinHook.h>
#include <array>
#include <intrin.h>
#include <iterator>
#include <math.h>
#include <span>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

namespace ubw::hooks {
namespace log = ue::log;
using ue::log::note_event;
using ue::log::note_event_cap;
using ue::log::note_skip;
using namespace ue::reflect;
using namespace ue::actor;
using namespace ue::gas;
using namespace ue::input;
using namespace ue::widget;
using namespace ue::hooktable;
using namespace ue::net;
using namespace ue::chan;
using namespace ubw::chan;
extern ue::config::Travel g_travel;
extern bool g_travel_armed;
void clear_pending_starting_mappings();
void client_diag_load_map_enter(const void* pending);
void client_diag_load_map_return(bool ok, const void* pending);
void client_snapshot_step(void* engine);
extern bool g_snapshot_armed;
void rotation_report_step(void* engine);
extern uint32_t g_rot_rows_left;
extern bool g_input_window_armed;
void note_input_window_key(const char* key, float amount);
void own_pawn_collision_step(void* engine);
extern bool g_pawn_watch_armed;
void own_rows_step(void* engine);
extern uint32_t g_own_rows_left;
void card020_row_step(void* engine);
void start_card020_rows();
extern uint32_t g_c20_rows_left;
void c21_grant_step(void* engine);
void c21_parity_step(void* engine);
extern uint32_t g_c21_camera_logs;
void c21_note_activation(void* asc, int32_t handle, bool server_request, void* ability, bool ok);
void c439_note_end(const void* ability, int32_t handle, bool cancelled); // card 439 step 2 (diagnostic)
void c24_log_rpc(const char* what, const void* asc, int32_t handle, int32_t extra);
void c24_log_local_activation(const void* asc, int32_t handle, const void* ability, bool ok);
void c26_own_fire(const void* asc, const void* tag, bool pressed);
bool c26_firing(const void* pawn);
void c24_step(void* engine);
bool net_flush_pc(void* pc); // fire.cpp: FlushNet on pc's connection now; false when it is not Open
void c25_step(void* engine);
void c293_step(void* engine); // audit_npc.cpp: card 293 minimap marker diagnostic, 1 Hz, both sides
void c293_map_end();
void c319_step(void* engine); // audit_npc.cpp: card 319 client minimap scan diagnostic, every 5 s, both sides
void c319_map_end();
void c319_shot_row(void* world, const char* shot); // audit_npc.cpp: b232, the minimap gates at a scenario shot
void c494_weapon_row(void* pc, const char* why); // weapon_c494.cpp: card 494, the drawn weapon (both sides)
void c494_after_throw();                         // card 494: own grenade OnDoAction, check 2 s later
void c494_client_step(void* pc);                 // card 494: each client frame
void c495_door_opened(const char* door);         // card 495: client door 0 -> open, in-hand rows now and +100 ms
void c321_step(void* engine); // player0_c321.cpp: card 321 host begin-play count row, once per map
void c321_map_end();
void c336_step(void* engine); // objective_c284.cpp: card 336 fall zone rows, once per map, both sides
void c336_map_end();
extern StaticClassFn g_c321_fall_zone_class; // ASTFallZonesBase
extern StaticClassFn g_c321_streaming_class; // AADLevelStreamingVolume
using C321DoorPathFn = void (*)(void* door, const void* from, const void* to, bool closing);
using C321PrimOverlapFn = bool (*)(const void* component, const void* actor);
extern C321DoorPathFn g_orig_c321_door_path;
extern C321PrimOverlapFn g_c321_prim_overlapping; // UPrimitiveComponent::IsOverlappingActor
void detour_c321_door_path(void* door, const void* from, const void* to, bool closing);
void c321_pilot_check(const void* mech); // host, once per EnterMech: GetPlayerCharacter, pawn, pilot body per slot
bool c321_player_body(const void* pawn); // trooper or mech: what GetPlayerCharacter answers for a player
// fallzone_c338.cpp: card 338, one fall zone timer for two players
using C338OverlapFn = void (*)(void* zone, void* comp, void* other, void* other_comp, int32_t body, bool sweep,
                               const void* hit);
using C338WeakFn = void (*)(void* zone, uint64_t pawn); // TWeakObjectPtr<ASTPlayerCharacter> by value
extern C338OverlapFn g_orig_c338_overlap;
extern C338WeakFn g_orig_c338_reposition;
extern C338WeakFn g_orig_c338_end;
void detour_c338_overlap(void* zone, void* comp, void* other, void* other_comp, int32_t body, bool sweep,
                         const void* hit);
void detour_c338_reposition(void* zone, uint64_t pawn);
void detour_c338_end(void* zone, uint64_t pawn);
void c338_step(); // host: the 5 s row after each reposition
// render_c337.cpp: card 337, client world rendering off from its LoadMap return until its title screen is up
extern StaticClassFn g_c337_statics_class; // UGameplayStatics
void c337_uncover(const char* why);
void c337_load_map_enter(void* engine);
void c337_load_map_return(bool ok, const void* pending);
void c337_step();
void c397_receive(void* pc, const WireString* s);     // client: ChanOp::TravelIds (render_c337.cpp)
void c397_host_travel(void* world, const wchar_t* url); // host: TravelIds to every remote PC (intro_c135.cpp)
bool c397_set_ids(void* world, int32_t chapter, int32_t level); // client: the IDs onto its game instance
void c310_collision_row(const void* actor, const char* stage); // testrun.cpp: c310 range collision row
void c310_collision_map_end();
using GetHudFn = void* (*)(const void* world_context);     // USTCommonUtilityLibrary::GetSTHUD
using ScanRangeFn = float (*)(void* pc);                    // ASTPlayerController::FindLargestMinimapScanningRange
using ObjectTypeChannelFn = int32_t (*)(int32_t object_type); // UEngineTypes::ConvertToCollisionChannel(EObjectTypeQuery)
using ResponseToChannelFn = int32_t (*)(const void* component, int32_t channel); // GetCollisionResponseToChannel
extern GetHudFn g_c319_get_hud;
extern ScanRangeFn g_c319_scan_range;
extern StaticClassFn g_c319_hud_gameplay_class;
extern ObjectTypeChannelFn g_c319_type_channel;
extern ResponseToChannelFn g_c319_response;
using C319MarkerFn = void (*)(void* widget, int32_t index); // USTBugProximityAreaWidget::OnNewMarker / OnRemoveMarker
extern C319MarkerFn g_orig_c319_new_marker;
extern C319MarkerFn g_orig_c319_remove_marker;
extern StaticClassFn g_c319_area_class; // USTBugProximityAreaWidget
extern StaticClassFn g_minimap_class;   // USTMinimapComponent (card 319 b251)
extern StaticClassFn g_c319_icon_class; // USTBugProximityIconWidget
void detour_c319_new_marker(void* widget, int32_t index);    // card 319 reopen (b249): counted, both sides (diag gate)
void detour_c319_remove_marker(void* widget, int32_t index);
void c25_arm_map();
void c27_step(void* engine);
void c69_reset_map();
void c73_reset_map();
void c27_arm_map();
void c29_step(void* engine);
void c29_arm_map();
void c29_sets();
void c29_note_activation(const void* asc, const void* ability, bool ok, bool server_request);
void c22_step(void* engine);
void c28_step(void* engine);
void c28_arm_map();
void c45_pending_failed(const char* kind, const char* name); // card 130 item 15c
// ---- Card 130 (items 5, 9, 15a): receiver-side proof that a mod-spawned Niagara effect is shown (fx_watch.cpp) ----
using NiagaraBoolFn = bool (*)(const void* component);
using NiagaraStateFn = uint8_t (*)(const void* component); // ENiagaraExecutionState: 0 Active .. 3 Complete
extern NiagaraBoolFn g_niagara_is_complete;
extern NiagaraStateFn g_niagara_requested_state;
constexpr uint32_t kFxWatchMax = 64;           // effects checked at once; a grenade burst or airstrike spawns a few
constexpr ULONGLONG kFxWatchFirstMs = 100;     // first check: running, active, visible, not hidden
constexpr ULONGLONG kFxWatchLateMs = 1000;     // second check: visible (complete is the normal end of a burst)
constexpr size_t kFxWatchWhatChars = 48;
struct C31Weak { // FWeakObjectPtr: {ObjectIndex, ObjectSerialNumber}; serial 0 = null
    int32_t index;
    int32_t serial;
};
struct FxWatch {
    C31Weak comp;     // weak ref (card 319 run 1: a raw pointer read after the component was freed crashed the host)
    ULONGLONG born_ms;
    uint32_t checks;  // 0, 1; the row leaves after 2
    char what[kFxWatchWhatChars];
};
void fx_watch_add(const void* comp, const char* what);
void fx_watch_step(void* engine);
void fx_watch_map(const char* why);
void pawn_slots_respawn(); // card 130 item 15: card 022/028/033 pawn slots at a respawn in place
void c23_step(void* engine);
void c23_arm_map();
void c37_step(void* engine);
void c37_arm_map();
void c37_note_npc(const char* what, const void* npc, float old_health, float new_health, bool values);
void c38_arm_map();
void c38_note_npc(bool death_event, const void* npc);
void c39_arm_map();
void c39_map_end(); // card 200: at LoadMap enter
void c42_release_pools(); // card 140: client gore pools out of the game instance's ReferencedObjects (enemies.cpp)
void c40_arm_map();
void c41_step(void* engine);
void c41_arm_map();
extern bool g_c40_trace;
void c39_track(const void* actor);
void c42_step(void* engine);
extern bool g_c42_test;
void c43_step(void* engine);
void c43_arm_map();
extern bool g_c43_test;
extern uint32_t g_c43_start_ms;
extern bool g_c45_test;
extern bool g_c46_test;
extern bool g_c50_test;
extern bool g_c54_test;
extern bool g_c65_log;
extern uint32_t g_c65_logs;
extern uint32_t g_c68_logs;
extern bool g_c73_log;
extern bool g_c74_log;
extern bool g_c76_log;
extern bool g_c132_fx_log; // UBWCOOP_C132_FX: card 132 step 2e effect audit (host against client)
extern bool g_c136_log;    // UBWCOOP_C136_LOG: card 136 host effect event and skip lines (totals always)
extern int g_c79_down;
extern uint32_t g_c65_activate_logs;
extern uint32_t g_c65_shunt_logs;
constexpr uint32_t kC65ShuntLogs = 8;
const uint8_t* c21_spec_for(const void* asc, int32_t handle);
extern bool g_c63_on;
extern bool g_c67_pe;
extern bool g_c74_pe;
void c74_note_pe(const void* object, const void* function);
void c67_note_pe(void* object, const void* function);
void c67_note_pause(void* requester, bool pause);
void c67_reset_map();
void c67_ui_remember(void* stack);
void c67_ui_forget();
bool c67_ui_close(void* world, void* stack);
void c67_step(void* engine, ULONGLONG now);
extern bool g_c60_mk1_only;
extern uint64_t g_c62_instance_weak; // FWeakObjectPtr to the last host switch ability instance
bool c62_before_try(const void* asc, int32_t handle);
void c62_after_try(const void* asc, int32_t handle, bool ok);
void c62_note_cancel(const void* ability, int32_t handle, const void* actor_info, bool replicate);
void c50_step(void* world, void* pc, ULONGLONG now);
void c79_mech_death(const void* mech);
void c76_tag(const void* asc, const void* tag, bool pressed);
void c76_try(const void* asc, int32_t handle, const void* ability, bool ok, bool server);
void c76_fire(const void* ability, const void* avatar, const double* transform, const void* task);
void c76_mark_task(const void* shooter, const void* task);
void c76_spawn(const void* init_data, const double* transform, const void* proj);
void c76_damage(const void* results, const void* instigator, const void* causer);
void c76_fx(const char* what, const void* system, const Vec3* at);
void c50_arm_map();
void c120_arm_map(); // card 120: the client's Pong gate starts over (chan_ops.cpp)
void c120_client_step(void* pc); // card 157 batch: a Ping held by the gate is answered once ready (chan_ops.cpp)
ULONGLONG c120_pong_open_ms(); // card 146: the first Pong of this map load, 0 while held (chan_ops.cpp)
// Card 128, client: GetTickCount64 of the last host Ping (the host pings every second); 0 before the first.
// Continuous client-to-host senders hold while it is stale, so a stalled host's reliable buffer does not overflow.
// Card 026: both sides ping every this often (the client the host, the host every remote PC).
inline constexpr ULONGLONG kC26PingMs = 5000;
extern ULONGLONG g_c128_host_ping_ms;
void c50_after_load_asc(const void* pawn);
// Card 143 (restore_c143.cpp): a host LoadFromSlot in place with clients in the session. The host sends each client
// its server copy's restored state (Reload) and the client rejoins; after the reconnect the client sends it back
// (Restore) and the host applies it to the new copy once its starting inventory is in.
void c143_after_load(void* world);
void c143_after_load_asc(void* pawn);
void c198_health_change(const void* pawn, float old_health, float new_health); // host: card 198 read-back count
void c143_host_assign(const void* pawn);
void c143_host_step(void* world, ULONGLONG now);
void c143_client_ping(void* pc);
void c143_receive_reload(void* pc, const ue::chan::WireString* s);
void c143_receive_restore(void* pc, const ue::chan::WireString* s);
bool c143_apply_weapons(void* inventory, const char* text, ULONGLONG state_ms);
// Card 440 (restore_c143.cpp), host: a server copy's weapon list and ammo map kept on each card 327 row change;
// a new copy controller takes the kept texts of a gone one as a held Restore with no Health.
void c440_keep(void* pc, const char* weapons, const char* ammo, const char* charge);
void c440_arm_map();
void c448_logout(const void* pc, const void* pawn); // card 448: Health and Armour into the kept entry
// Card 446, host: each relayed item count of a server copy is kept (c73_relay); the controller that takes a gone
// one's kept inventory gets those counts set on its new pawn (c446_set_count) once its Restore hold is gone.
void c446_note_item(void* pc, const void* pawn, void* definition, int32_t count);
void c446_set_count(void* inventory, void* definition, int32_t want);
bool c72_seed(void* pc, void* pawn);
void c72_mirror_apply(void* pc, const char* text, const char* what);
bool c72_take_set(void* pc, ULONGLONG now);
void c50_chapter_frame();
void c50_relay_use(void* pawn, bool pressed);
void c46_step(void* world, void* pc, ULONGLONG now);
void c46_arm_map();
void c45_load_map_return(bool ok, const void* pending);
void c45_step(void* engine);
void c45_client_screen(void* screen);
void c45_arm_map();
void c45_respawn_check(void* pc, ULONGLONG now);
void c49_arm_map();
void c49_map_end(); // card 199, LoadMap enter: the journal and synced PCs start over
void c308_journal_totals(const char* when); // card 308: host objective journal totals row (map end, quit)
void c48_arm_map();
bool c43_refuse_tag(void* asc, const void* tag);
void c73_drop_stale(void* asc, const void* tag);
void c43_interact_edge(void* interactor, bool pressed);
bool c43_is_down(const void* pawn);
const char* c43_life_text(const void* pawn);
void c43_receive_use(void* pawn, uint32_t arg);
void c43_logout(const void* pc, const void* pawn); // card 508: pc is the leaving one
void c44_context(const void* context, int32_t priority);
void c44_host_fire(const void* pawn, uint32_t arg);
extern bool g_c44_pe;
extern uint32_t g_c44_fires[4];
extern bool g_c39_overkill;
void c30_step(void* engine);
void c31_step(void* engine);
void c31_note_mode(const void* pc, const void* mode);
void c31_note_ui_only(const void* object);
bool c52_swallow_key(const char* key);
void c52_load_map_return(bool ok, const void* pending);
void c30_arm_map();
void c32_step(void* engine);
void c32_arm_map();
extern config::SpawnMech g_c32_mode;
void c30_note_input(const char* what, const void* owner, const void* tag);
void c24_aim_begin(const void* owner);
void c24_aim_end();
extern config::GrantWeapon g_grant_weapon;
extern bool g_c21_grant_armed;
void dropship_watch_step(void* engine);
extern bool g_dropship_watch_armed;
extern bool g_auto_dropship;
extern bool g_auto_dropship_armed;
extern bool g_skip_flight;
void auto_dropship_step(void* engine);
void card017_step(void* engine);
void card018_client_step(void* engine); // client, every tick: the own pawn's attribute re-init (card 298)
void c490_step(void* engine);           // diag: Stamina and Abilities.NoStamina rows per player pawn (card 490 step 1)
void reset_c490_map();
void c490_hold_map_end();               // card 490: the map's stamina refill count (sprint.cpp)
void c490_hold_step(void* engine);      // card 490: Stamina held full while the host option is on, every tick
void c490_send(void* pc);               // host: the stamina option to one client PC (join replay)
void c490_set_host(const void* world_ctx, bool on, const char* why); // host: set it, send to every client
void c490_receive(void* pc, uint32_t on); // client: ChanOp::Stamina
void c459_step(void* engine);           // both sides at 1 Hz: host MechFuel sends, client pending write; diag rows (card 459)
void reset_card017_map(bool arm);
extern bool g_c17_armed;
extern const char* g_last_hook;
const char* session_side();
// --- Remote-PlayerController crash guard (card 003) -------------------------------------------
// UpdateBindings(LocalPlayer, Settings) does `lea rdi,[rcx+0x138]` and walks that subsystem
// collection without a null check. On a listen server, a remote client's PlayerController has
// no LocalPlayer, so ASTPlayerController::GetAndApplyGameUserSettings passes null and the host
// faults reading 0x148. Input bindings are local-only state, so skipping is the correct no-op.
// The controller is not an argument here; the log names the caller by RVA instead.
using UpdateBindingsFn = void (*)(void* local_player, const void* settings);
extern UpdateBindingsFn g_orig_update_bindings;
void detour_update_bindings(void* local_player, const void* settings);
// --- ASTPlayerController::SetPlayer guard (card 004) -----------------------------------------
// ASTPlayerController::SetPlayer = Super (APlayerController::SetPlayer) followed by a tail that
// registers the controller's PlayerMappableInputConfigs with the EnhancedInput user settings:
// GetLocalPlayer() -> GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() -> GetUserSettings().
// The tail null-checks GetLocalPlayer() only around GetSubsystem and then calls through the
// subsystem unconditionally, so for a remote client's PC on the listen server (Player is a
// UNetConnection, GetLocalPlayer() is null) the host faults reading 0x30.
// The tail is local-only input state. For a non-ULocalPlayer InPlayer the detour runs Super and
// skips the tail; it never calls the original then, so Super runs exactly once either way.
// Super stores Player = InPlayer unconditionally, so testing InPlayer up front with the same
// FObjectPtr::IsA(ULocalPlayer) that GetLocalPlayer() uses decides exactly what the original's
// post-Super GetLocalPlayer() would. The skipped tail allocates nothing that outlives it (its
// one FMemory allocation is a per-iteration FSoftObjectPath copy freed inside the loop).
using SetPlayerFn = void (*)(void* self, void* player);
extern SetPlayerFn g_orig_st_set_player;
extern SetPlayerFn g_pc_set_player;
extern StaticClassFn g_local_player_class;
void detour_st_set_player(void* self, void* player);
// --- Client GameMode-null guard for ASTPlayerController::SetupInputComponent (card 005) -------
// SetupInputComponent: Super, then (local player and EnhancedInput subsystem present) looks up
// StartingPlayerInputMappings (TMap<TSubclassOf<AGameModeBase>, TSoftObjectPtr<
// UPlayerMappableInputConfig>> at this+0x838) by GetGameMode(this)->GetClass() and, on a hit,
// AddMappingContext()s every context of that config; then RequestRebuildControlMappings, then
// the action bindings (SetupInputForConfig) and GetAndApplyGameUserSettings. GetGameMode is
// null on every client, so +0x63 faults reading 0x10.
// Fix: GetGameMode is hooked, and its result is changed only when it is null AND it returns to
// SetupInputComponent+0x5C (validated at install). Then the detour returns a static block whose
// +0x10 (the only field the caller reads) holds the key: the replicated
// AGameStateBase::GameModeClass, which AGameModeBase::InitGameState sets to the host's
// GameMode->GetClass(). The original then runs unmodified, so Super runs exactly once. If the
// GameState has not replicated yet (the PC's channel can open first), the key is the PC's own
// class, which is never an AGameModeBase subclass, so the lookup misses (contexts skipped,
// rebuild + action bindings still run) and the contexts are added later, once, when the
// GameState's OnRep_GameModeClass calls APlayerController::ReceivedGameModeClass on this PC.
// That virtual is an empty folded stub, so its PC vtable slot is patched (not the shared code).
// Every other GetGameMode caller, and the host, sees the original result.
using GetGameModeFn = void* (*)(const void* world_context);
using LoadSynchronousFn = void* (*)(const void* soft_ptr);
using MarkAsReachableFn = void (*)(const void* object);
using ReceivedGameModeClassFn = void (*)(void* pc, const void* const* game_mode_class);
using AddMappingContextFn = void (*)(void* iface, const void* context, int32_t priority, const uint8_t* options);
using RemoveMappingContextFn = void (*)(void* iface, const void* context, const uint8_t* options);
extern GetGameModeFn g_orig_get_game_mode;
extern uint8_t* g_setup_input_component;
extern LoadSynchronousFn g_load_synchronous;
extern StaticClassFn g_mappable_config_class;
extern MarkAsReachableFn g_mark_as_reachable;
extern ReceivedGameModeClassFn g_received_game_mode_class;
extern const volatile uint8_t* g_gc_barrier_flag;
constexpr size_t kConfigContexts = 0x58;        // UPlayerMappableInputConfig::Contexts TMap
constexpr size_t kConfigContextStride = 0x18;   // TSet element: context @0, int32 priority @8
constexpr size_t kIfaceAddMappingContext = 0x98 / sizeof(void*);
constexpr int32_t kMaxConfigContexts = 256;
void* detour_get_game_mode(const void* world_context);
bool load_config_contexts(const void* soft_config, const char* what, SparseView& v);
const void* config_context(const SparseView& v, int32_t i, int32_t* priority);
// --- ASTPlayerMechBase::Add/RemoveMechInputContext guards (card 005) --------------------------
// Both: PC = Controller weak ptr (this+0xE30).Get(); subsystem = PC->GetLocalPlayer() ?
// GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : null; then LoadSynchronous(this+0xE60)
// (the mech's UPlayerMappableInputConfig) and Add/RemoveMappingContext for each of its contexts
// through the subsystem with no null check. That is local input state only. The guard repeats
// the same pure getters and skips the original when the PC is null (GetLocalPlayer(null) itself
// faults) or there is no subsystem; the skipped original would only have loaded the config
// asset (GC-owned, nothing to free) and called the subsystem.
using MechInputFn = void (*)(void* mech);
extern MechInputFn g_orig_add_mech_input;
extern MechInputFn g_orig_remove_mech_input;
constexpr size_t kMechController = 0xE30; // TWeakObjectPtr<APlayerController>
void detour_add_mech_input(void* mech);
void detour_remove_mech_input(void* mech);
// --- Client gate for server-only encounter and spawn logic (card 006) ----------------------------
// A level Blueprint latent action calls ASTEncounterManagerBase::StartEncounter on every instance.
// On a network client that runs the wave and the spawner, and PerformSpawnAtPoint ends in
// UNavigationSystemV1::ProjectPointToNavigation(this=null): a client has no navigation system
// (the game ships none; card 064 creates one only for the test runner, UBWCOOP_CMD=1).
// Enemies must come from the server. The gate asks AActor::InternalGetNetMode (pure: world,
// net driver, world-context URL) and skips the original only on NM_Client; standalone, listen
// and dedicated servers call it unchanged. AActor::Role is not usable: actors the game never
// marked as replicated report ROLE_Authority on clients too.
//
// Every gated function is skipped at entry, before it allocates or binds anything, and none
// takes a by-value argument, so a skip frees nothing. The two struct returns are filled the way
// the originals fill them (see the card 006 report for the layout evidence).
using EncounterFn = void (*)(void* manager);
using PerformSpawnFn = void* (*)(void* spawner, void* result, const void* params);
using QueueSpawnFn = void* (*)(void* spawner, void* result, const void* params, float delay);
extern EncounterFn g_orig_start_encounter;
extern EncounterFn g_orig_activate_current_wave;
extern PerformSpawnFn g_orig_perform_spawn;
extern QueueSpawnFn g_orig_queue_spawn;
bool is_net_client(const void* actor);
void detour_start_encounter(void* manager);
// Card 503 (diag, waves_c503.cpp): host rows per encounter wave event; c503_clear_report after a `clear ... kill`
// that left 0 lists each still-open wave's alive NPCs.
using C503WaveNpcFn = void (*)(void* manager, void* wave, void* npc);
using C503WaveFn = void (*)(void* manager, void* wave);
using C503CancelFn = void (*)(void* wave, const void* result);
using C503WaveIdFn = int32_t (*)(void* manager);
extern C503WaveNpcFn g_orig_c503_spawned;
extern C503WaveNpcFn g_orig_c503_killed;
extern C503WaveFn g_orig_c503_spawn_complete;
extern C503WaveFn g_orig_c503_wave_end;
extern EncounterFn g_orig_c503_next_wave;
extern EncounterFn g_orig_c503_encounter_ended;
extern C503CancelFn g_orig_c503_cancelled;
extern C503WaveIdFn g_c503_wave_id;
void detour_c503_spawned(void* manager, void* wave, void* npc);
void detour_c503_killed(void* manager, void* wave, void* npc);
void detour_c503_spawn_complete(void* manager, void* wave);
void detour_c503_wave_end(void* manager, void* wave);
void detour_c503_next_wave(void* manager);
void detour_c503_encounter_ended(void* manager);
void detour_c503_cancelled(void* wave, const void* result);
void c503_clear_report(uint32_t seq, const void* pawn);
void detour_activate_current_wave(void* manager);
void* detour_perform_spawn(void* spawner, void* result, const void* params);
void* detour_queue_spawn(void* spawner, void* result, const void* params, float delay);
// --- Remote-PC idle check guard for ASTPlayerController::Tick (card 007) ----------------------
// ASTPlayerController::Tick runs an idle check: while the idle latch (byte +0x7E0) is clear it
// adds DeltaSeconds to the idle timer (+0x7E4); once that reaches the threshold (+0x7E8, from the
// pawn's TimeBeforeIdle, default 5 s) and RotationInput (+0x528/+0x530/+0x538) is zero mod 360,
// it calls PlayerInput->IsPressed(EKeys::AnyKey) and sets the latch when that returns false.
// PlayerInput (+0x420) is created only by InitInputSystem, which a remote client's PC on the
// listen server never runs, so after 5 s the host faults reading 0x308 inside IsPressed.
// Only Tick and the HandleInput* handlers / OnPossess touch the latch and timer, and only Tick
// reads them, so the check has no consumer beyond Tick itself.
// Fix: IsPressed is hooked, and only a call with this == null that returns to Tick+0x122
// (validated at install) is changed: it returns false ("no key down" is the truth for a PC with
// no local input), so Tick sets the latch once and stops checking until the next OnPossess.
// IsPressed takes the FKey by value and destroys it (releases the shared details pointer at
// +0x10, which Tick added a reference to), so the skip runs ~FKey on it to match.
using IsPressedFn = bool (*)(const void* player_input, void* key);
extern IsPressedFn g_orig_is_pressed;
extern uint8_t* g_st_pc_tick;
bool detour_is_pressed(const void* player_input, void* key);
bool resolve_tick_is_pressed_site();
bool resolve_setup_input_sites();
// --- Client input receiver and client input diagnostics (card 008) ---------------------------
// Every ASTPlayerController::HandleInput* action except Look goes through the pawn's
// USTInputReceiverInterface, cached in the PC at +0x7C0: HandleInputMove does
// `mov r8, [rcx+0x7C0]; test r8, r8; je <ret>; ...; call [[r8]+0xC0]`. The only writer of
// +0x7C0 and of the player-character pointer at +0x7D8 is ASTPlayerController::OnPossess
// (+0x3F / +0x71), and UE runs OnPossess only with authority (AController::Possess). A network
// client's own PC gets its pawn by replication instead (OnRep_Pawn / ClientRestart, both ending
// in APlayerController::SetPawn), so on the client the receiver stays null and every action
// handler returns at its first test. HandleInputLook calls AddYawInput / AddPitchInput directly
// (vtable +0xD30 / +0xD28) and only uses +0x7C0 for aim assist, which is why looking works.
// Fix: after APlayerController::SetPawn on a network client's local ASTPlayerController, store
// what OnPossess would have stored for the pawn now in AController::Pawn: the receiver is
// GetInterfaceAddress(Pawn, USTInputReceiverInterface) (null without a pawn, as OnPossess+0x161
// does), and +0x7D8 = Pawn when it is an ASTPlayerCharacter (otherwise left as is, as OnPossess
// leaves it). Before a non-null receiver is replaced by a different one, ReleaseActiveInputs
// runs, as ASTPlayerController::OnUnPossess does. Host, standalone and the host's remote-client
// PCs are untouched (their net mode is not NM_Client). Not mirrored (open, see the card 008
// report): OnPossess's minimap origin, idle threshold and ability-system tag counts.
// The other detours below only log, client only, capped at kMaxLoggedEvents each.
using SetPawnFn = void (*)(void* pc, void* pawn);
using ControllerFn = void (*)(void* controller);
using SetInputModeFn = void (*)(void* pc, const void* mode);
using SetBoolFn = void (*)(void* controller, bool value);
extern SetPawnFn g_orig_pc_set_pawn;
extern ControllerFn g_orig_on_rep_pawn;
extern SetPawnFn g_orig_acknowledge_possession;
extern SetPawnFn g_orig_client_restart;
extern SetInputModeFn g_orig_set_input_mode;
extern SetBoolFn g_orig_set_show_mouse_cursor;
extern SetBoolFn g_orig_set_ignore_move_input;
extern AddMappingContextFn g_orig_add_mapping_context;
extern StaticClassFn g_input_receiver_class;
extern StaticClassFn g_player_character_class;
extern StaticClassFn g_character_class;
extern StaticClassFn g_player_controller_class;
extern StaticClassFn g_st_pc_class;
extern ControllerFn g_release_active_inputs;
using SetOriginActorFn = void (*)(void* minimap, void* actor);
extern SetOriginActorFn g_set_minimap_origin;
extern WorldGetFn g_world_first_pc;
using PcClientLoadedFn = bool (*)(const void* pc); // card 348: APlayerController::HasClientLoadedCurrentWorld
extern PcClientLoadedFn g_pc_client_loaded;
// Card 009. FUIInputConfig is 6 bytes, read off ApplyUIInputConfig (0x04C0B000): [0]
// bIgnoreMoveInput, [1] bIgnoreLookInput, [2] ECommonInputMode, [3] EMouseCaptureMode, [4]
// EMouseLockMode, [5] bHideCursorDuringViewportCapture. The router keeps the last applied one as
// TOptional<FUIInputConfig> ActiveInputConfig at +0x31 (6 bytes) with the IsSet byte at +0x37;
// ApplyUIInputConfig+0x3D..+0x77 compares the new config with it bytewise and returns early when
// equal and !bForceRefresh.
using ApplyUIInputConfigFn = void (*)(void* router, const uint8_t* config, bool force_refresh);
extern ApplyUIInputConfigFn g_orig_apply_ui_input_config;
extern bool g_client_game_only;
extern bool g_game_only_pending;
// Offsets, each read off a function of this build (see the card 008 report for the listings).
constexpr size_t kPcInputReceiver = 0x7C0;      // ASTPlayerController::OnPossess+0x3F store
constexpr size_t kPcPlayerCharacter = 0x7D8;    // ASTPlayerController::OnPossess+0x71 store
constexpr size_t kPcMinimap = 0x7B0;            // USTMinimapComponent* (OnPossess+0x233, GetProximityComponent)
constexpr size_t kMinimapOrigin = 0x108;        // USTMinimapComponent::OriginActor (SetOriginActor+0x26 store)
// FInputModeGameOnly: vtable at +0, bConsumeCaptureMouseDown byte at +8; the ctor (0x0402AC20)
// writes exactly those two. Its dtor (0x0402D2A0) only resets the vtable pointer: nothing to free.
constexpr size_t kInputModeGameOnlyBytes = 16;
constexpr ULONGLONG kSnapshotDelayMs = 10000;
// FInputMode* vtables: PDB publics ??_7FInputMode<X>@@6B@ (section 2 .rdata, VA 0x700C000).
struct InputModeName {
    uint32_t vtable_rva;
    const char* name;
};
constexpr InputModeName kInputModes[] = {
    {0x07B26340u, "GameOnly"}, {0x07B26328u, "GameAndUI"}, {0x07B26310u, "UIOnly"}};
extern bool g_client_session;
extern ULONGLONG g_snapshot_start_ms;
extern uint32_t g_context_logs;
uint32_t object_name_into(const void* object, char* out, uint32_t cap);
// The same for an FName (card 136 run 4): its entry's text in out (cut to cap - 1), returning the full length.
uint32_t fname_into(const void* fname, char* out, uint32_t cap);
bool is_local_pc(const void* pc);
void* own_pc(const void* world); // the world's local PC (not GetFirstPlayerController: card 346), or null
void detour_pc_set_pawn(void* pc, void* pawn);
void detour_on_rep_pawn(void* controller);
void client_force_game_only(void* pc);
void detour_acknowledge_possession(void* pc, void* pawn);
void detour_client_restart(void* pc, void* pawn);
// The engine's ClientRestart with the possession log row; what the hook runs, without the card 135 test lever.
void client_restart_call(void* pc, void* pawn);
void detour_set_input_mode(void* pc, const void* mode);
void detour_set_show_mouse_cursor(void* pc, bool show);
void detour_set_ignore_move_input(void* controller, bool ignore);
void detour_add_mapping_context(void* iface, const void* context, int32_t priority, const uint8_t* options);
void detour_apply_ui_input_config(void* router, const uint8_t* config, bool force_refresh);
// --- Client look and movement diagnostics (card 010) -----------------------------------------
// Log only; every original runs unchanged with the arguments it was given. Active only while
// g_client_session (the last LoadMap joined a server), so the host and standalone pay one load
// and one branch per call. Every counter and global below is game thread only: input, actor
// tick, RPC dispatch and UGameEngine::Tick all run there.
using HandleInputLookFn = void (*)(void* pc, const void* value);
using InputAxisFn = bool (*)(void* viewport_client, const void* args);
using SetControlRotationFn = void (*)(void* controller, const double* rotation);
using ClientSetRotationFn = void (*)(void* controller, const double* rotation, bool reset_camera);
using ClientAdjustPositionFn = void (*)(void* cmc, float timestamp, const double* new_loc, const double* new_vel,
                                        void* new_base, uint64_t base_bone, bool has_base, bool base_relative,
                                        uint8_t movement_mode, const void* new_rotation);
using ReplicateMoveFn = void (*)(void* cmc, float delta_seconds, const double* acceleration);
extern HandleInputLookFn g_orig_handle_input_look;
extern InputAxisFn g_orig_input_axis;
extern SetControlRotationFn g_orig_set_control_rotation;
extern ClientSetRotationFn g_orig_client_set_rotation;
extern ClientAdjustPositionFn g_orig_client_adjust_position;
extern ReplicateMoveFn g_orig_replicate_move;
// Offsets, each read off a function of this build (see the card 010 report for the listings).
constexpr size_t kControllerStateName = 0x2E0;       // FName StateName (AController::IsInState)
constexpr size_t kPcPlayer = 0x348;                  // APlayerController::Player (PlayerTick)
constexpr size_t kPcAcknowledgedPawn = 0x350;        // APlayerController::AcknowledgedPawn (PlayerTick)
constexpr size_t kPcCameraManager = 0x360;           // PlayerCameraManager (APlayerController::UpdateRotation)
constexpr size_t kPlayerPlayerController = 0x30;     // UPlayer::PlayerController (PlayerTick)
constexpr uint32_t kRotRows = 28; // card 016: the script runs about 25 s
constexpr ULONGLONG kRotRowIntervalMs = 1000;
// Event counts since the last rotation row (reset by each row).
struct DiagCounts {
    uint32_t look;
    uint32_t axis;
    uint32_t set_rotation_frame;
    uint32_t set_rotation_other;
    uint32_t replicate_move;
    uint32_t adjust;
    // Card 012: UEnhancedPlayerInput::InputKey calls (all keys, then by mouse key) and IA_Move.
    uint32_t keys;
    uint32_t mouse_x;
    uint32_t mouse_y;
    uint32_t mouse_2d;
    uint32_t move;
    // Card 013: MouseX/MouseY InputKey calls with a nonzero AmountDepressed, and the summed |amount|.
    uint32_t mouse_x_nonzero;
    uint32_t mouse_y_nonzero;
    double mouse_x_abs;
    double mouse_y_abs;
};
extern DiagCounts g_counts;
// Card 011: the client's last ReplicateMoveToServer arguments (values, not pointers).
struct ClientMove {
    Vec3 accel;
    float delta;
};
extern ClientMove g_client_move;
extern uint32_t g_rot_row;
extern ULONGLONG g_rot_next_ms;
void resolve_update_rotation_site();
void detour_handle_input_look(void* pc, const void* value);
bool detour_input_axis(void* viewport_client, const void* args);
void detour_set_control_rotation(void* controller, const double* rotation);
void detour_client_set_rotation(void* controller, const double* rotation, bool reset_camera);
// --- Card 012: the client's own local moves ----------------------------------------------------
// UCharacterMovementComponent::PerformMovement (vtable slot 0xA30, called by ReplicateMoveToServer)
// on the client's autonomous-proxy pawn: how far each call moves UpdatedComponent, and "pins": a
// move starting somewhere other than where the previous one ended. Values only, no pointers kept;
// game thread only. Row fields reset each rot row; the end location and adjust flag at LoadMap.
constexpr size_t kComponentMobility = 0x1A3;     // USceneComponent::Mobility (PerformMovement +0xC7: == 2 or no move)
constexpr double kPinTolerance = 0.1;            // cm
constexpr double kStillTolerance = 0.001;        // cm: a call that moved less counts as "did not move"
struct MoveDiag {
    uint32_t calls;            // row
    uint32_t still;            // row: calls that moved UpdatedComponent < kStillTolerance
    double displacement;       // row: summed |end - start|
    uint32_t pins_after_adjust; // row: pins with a ClientAdjustPosition since the previous move
    uint32_t pins_other;       // row
    Vec3 pin_from;             // last "other" pin: previous move's end
    Vec3 pin_to;               //                   this move's start
    unsigned long long caller_first; // row: caller of the first call, and how many calls came from it
    uint32_t calls_first;
    unsigned long long caller_other; // row: last caller that was not caller_first
    uint32_t calls_other;
    int updated_is_root;       // last call: UpdatedComponent == pawn RootComponent (-1 = no call yet)
    unsigned mobility;         // last call: UpdatedComponent Mobility
    bool have_end;             // map: last_end valid
    bool adjust_seen;          // map: ClientAdjustPosition since the last move ended
    Vec3 last_end;
};
extern MoveDiag g_move_diag;
void reset_move_diag_row();
void reset_move_diag_map();
void detour_client_adjust_position(void* cmc, float timestamp, const double* new_loc, const double* new_vel,
                                   void* new_base, uint64_t base_bone, bool has_base, bool base_relative,
                                   uint8_t movement_mode, const void* new_rotation);
void detour_replicate_move(void* cmc, float delta_seconds, const double* acceleration);
using PerformMovementFn = void (*)(void* cmc, float delta_seconds);
using PlayerInputKeyFn = bool (*)(void* player_input, const void* args);
extern PerformMovementFn g_orig_perform_movement;
extern PlayerInputKeyFn g_orig_player_input_key;
extern HandleInputLookFn g_orig_handle_input_move;
void note_local_move(const void* owner, const uint8_t* updated, const Vec3& start, unsigned long long caller);
// --- Card 013: the movement chain below PerformMovement, collision writers, IA_Look evaluation --
// Log only, game thread only. The hooks below PerformMovement do work only while
// detour_perform_movement is inside the original call for the watched pawn: g_move_cmc is set
// for that call's extent and cleared when it returns, so no game pointer outlives the call.
constexpr size_t kHitComponent = 0xD8;                // TWeakObjectPtr<UPrimitiveComponent> (MoveAlongFloor+0x39B, Hit at rbp-0x50)
constexpr size_t kDropshipPlayerInside = 0x749;       // ASTPlayerTransportDropship: EnterDropship+0xD stores 1
constexpr uint32_t kTrackedModifiers = 4;
constexpr uint32_t kTrackedActions = 12;             // card 014: per-row action table; later actions go to "other"
using PhysicsStepFn = void (*)(void* cmc, float delta_seconds, int32_t iterations);
using MoveComponentFn = bool (*)(void* mc, const double* delta, const void* rotation, bool sweep, void* hit,
                                 uint8_t teleport);
using SetCollisionFn = void (*)(void* actor, bool enable);
using SetCollisionEnabledFn = void (*)(void* component, uint8_t type);
// Card 237: UPrimitiveComponent::SetCollisionProfileName(FName, bool bUpdateOverlaps); the 8-byte FName goes by value.
using SetCollisionProfileFn = void (*)(void* component, uint64_t profile, bool update_overlaps);
using DropshipFn = void (*)(void* dropship);
using ProcessMappingFn = void (*)(void* player_input, const void* const* action, float delta_seconds, bool paused,
                                  const void* raw_value, uint8_t key_event, const void* modifiers, const void* triggers,
                                  bool always_tick);
using ApplyModifiersFn = void* (*)(const void* player_input, void* ret, const void* modifiers, const void* value,
                                   float delta_seconds);
using ModifyRawFn = void* (*)(const void* modifier, void* ret, const void* player_input, const void* value,
                              float delta_seconds);
using EvaluateTriggersFn = uint8_t (*)(void* tracker, const void* player_input, const void* triggers,
                                       const void* value, float delta_seconds);
extern PhysicsStepFn g_orig_start_new_physics;
extern PhysicsStepFn g_orig_phys_walking;
extern MoveComponentFn g_orig_safe_move;
extern MoveComponentFn g_orig_move_impl;
extern SetCollisionFn g_orig_set_actor_collision;
extern ExecThunkFn g_orig_exec_set_actor_collision;
extern SetCollisionEnabledFn g_orig_set_collision_enabled;
extern SetCollisionProfileFn g_orig_set_collision_profile; // card 237
extern DropshipFn g_orig_enter_dropship;
extern DropshipFn g_orig_descend_completed;
extern ProcessMappingFn g_orig_process_mapping;
extern ApplyModifiersFn g_orig_apply_modifiers;
extern ModifyRawFn g_orig_modify_raw;
extern EvaluateTriggersFn g_orig_evaluate_triggers;
// Card 014
using PawnFn = void (*)(void* pawn);
using PossessedByFn = void (*)(void* pawn, void* controller);
using BindingExecuteFn = void (*)(const void* binding, const void* instance);
extern PawnFn g_orig_post_init;
extern PossessedByFn g_orig_possessed_by;
extern BindingExecuteFn g_orig_execute_value;
extern BindingExecuteFn g_orig_execute_void;
extern BindingExecuteFn g_orig_execute_dynamic;
extern autoinput::FKeyCtorFn g_fkey_ctor;
extern autoinput::KeyArgsCtorFn g_key_args_ctor;
extern autoinput::RequestExitFn g_request_exit;
struct MoveChain {
    uint32_t new_physics;   // row: StartNewPhysics calls inside the watched PerformMovement
    uint32_t walking;       // row: PhysWalking calls
    uint32_t safe_moves;    // row: SafeMoveUpdatedComponent calls
    uint32_t safe_false;
    uint32_t moves;         // row: MoveUpdatedComponentImpl calls (SafeMove's inner call included)
    uint32_t move_false;
    uint32_t blocking;      // row: SafeMove hits with bBlockingHit
    uint32_t penetrating;   // row: SafeMove hits with bStartPenetrating
    float dt_min;
    float dt_max;
    double delta_sum;       // row: summed requested |Delta| of SafeMove calls
    double delta_max;
    uint8_t actor_collision; // last PerformMovement entry
    uint8_t body_collision;
    uint8_t floor_flags;
    uint8_t mode;
    int controller;          // CharacterOwner->Controller != null (-1 = no call yet)
    ObjectName hit_component; // last blocking hit
    ObjectName hit_owner;
    bool snap_pending;       // the next watched PerformMovement records before/after values
    bool snap_done;
    Vec3 v_before, v_after, a_before, a_after;
};
extern MoveChain g_chain;
extern const void* g_move_cmc;
void reset_move_chain_row();
void detour_start_new_physics(void* cmc, float delta_seconds, int32_t iterations);
void detour_phys_walking(void* cmc, float delta_seconds, int32_t iterations);
bool detour_safe_move(void* mc, const double* delta, const void* rotation, bool sweep, void* hit, uint8_t teleport);
bool detour_move_impl(void* mc, const double* delta, const void* rotation, bool sweep, void* hit, uint8_t teleport);
bool watched_pawn(const uint8_t* owner);
void note_move_entry(const uint8_t* cmc, const uint8_t* owner, const uint8_t* updated);
void note_move_exit(const uint8_t* cmc);
void log_move_chain(const char* tag, uint32_t k);
extern uint32_t g_collision_logs;
extern uint32_t g_pawn_collision_logs;
const char* side_text();
void detour_set_actor_collision(void* actor, bool enable);
void detour_exec_set_actor_collision(void* context, void* frame, void* result);
void detour_set_collision_enabled(void* component, uint8_t type);
void detour_set_collision_profile(void* component, uint64_t profile, bool update_overlaps); // card 237
extern uint32_t g_dropship_enter_exit_logs;
void detour_enter_dropship(void* dropship);
void detour_descend_completed(void* dropship);
constexpr size_t kDropshipState = 0x69B;            // ESTDropshipState; 0 = IsPerformingActions
using StartActionFn = void (*)(void* dropship, uint8_t startup, uint8_t nav_direction, uint8_t target_point,
                               void* spline);
using SetStateFn = void (*)(void* dropship, uint8_t state);
using SkipFlightFn = void (*)(void* dropship, bool to_landing);
extern StartActionFn g_orig_start_action;
extern SetStateFn g_orig_set_state;
extern SkipFlightFn g_orig_skip_flight;
extern SkipFlightFn g_call_skip_flight; // kCallsUbw: resolved with the diag gate off (card 090)
extern DropshipFn g_orig_begin_descent;
extern StaticClassFn g_transport_dropship_class;
extern StaticClassFn g_troops_dropship_class; // ASTProceduralDropshipTroops (card 132 state relay)
extern ConstBoolFn g_actor_tick_enabled;
using ConstIntFn = int32_t (*)(const void* self);
extern ConstIntFn g_world_num_pcs;
extern uint32_t g_dropship_logs;
struct DropshipWatch {
    ULONGLONG start_ms; // host LoadMap return
    ULONGLONG next_ms;
    uint32_t lines;
    uint32_t ticks;            // transport dropship Tick calls since the previous line
    uint32_t tick_behaviours;  // TickBehaviours calls on a transport dropship since the previous line
    uint32_t spline_travels;   // TickSplineTravel calls on a transport dropship since the previous line
    float last_delta;          // delta_seconds of the last transport dropship Tick
    bool seen; // the PC's pawn was a transport dropship at least once this map
};
extern DropshipWatch g_dropship_watch;
using DropshipTickFn = void (*)(void* dropship, float delta_seconds);
extern DropshipTickFn g_orig_transport_tick;
extern DropshipTickFn g_orig_tick_behaviours;
extern DropshipTickFn g_orig_tick_spline_travel;
void detour_transport_tick(void* dropship, float delta_seconds);
void detour_tick_behaviours(void* dropship, float delta_seconds);
void detour_tick_spline_travel(void* dropship, float delta_seconds);
void detour_start_action(void* dropship, uint8_t startup, uint8_t nav_direction, uint8_t target_point, void* spline);
void detour_set_state(void* dropship, uint8_t state);
void detour_skip_flight(void* dropship, bool to_landing);
void detour_begin_descent(void* dropship);
// USTGameInstance pause requests (card 015). `this` is the game instance's pause-interface
// subobject: RequestGamePause_Implementation+0x103 adjusts it by -0x1C0 to the UGameInstance.
// Requester arrays (TArray<UObject*>): +0x260 every request, +0x270 requests with the bool set.
// USTGameInstance::UpdatePause 0x0510CDE0 pauses the world (UGameplayStatics::SetGamePaused)
// while the first array's count (+0x268 here, GI+0x428) is non-zero.
constexpr size_t kPauseRequesters = 0x260;
extern uint32_t g_pause_logs;
using RequestPauseFn = void (*)(void* self, void* requester, bool flag);
using RequestUnpauseFn = void (*)(void* self, void* requester);
extern RequestPauseFn g_orig_request_pause;
extern RequestUnpauseFn g_orig_request_unpause;
void detour_request_pause(void* self, void* requester, bool flag);
void detour_request_unpause(void* self, void* requester);
// Card 260: USTGameInstance::EndLevel (0x50E40C0), the only caller of ShowLevelEndScreen. The host relays it to every
// client (ChanOp::EndLevel); a client runs it once per map, from its own script or from the relay (objectives.cpp).
using EndLevelFn = void (*)(void* game_instance);
extern EndLevelFn g_orig_end_level;
void detour_end_level(void* game_instance);
void c260_receive(void* pc, const ChanArgs& args); // client: ChanOp::EndLevel
void c260_map_end();                                // both sides, at LoadMap enter (ubw_load_map_enter)
// Card 345b: the level-end Next (WB_TrooperLevelEnd). The client's own Next is not run while it is in the host's
// session; the host's Next is relayed (ChanOp::EndLevel, value 1) and runs the client's own Next handler.
extern bool g_c376_pe;                        // card 376: a client's WB_Stack_C holds the pause in the host's session
void c376_note_pause(void* requester, bool paused); // restart.cpp, from the pause and unpause detours
bool c376_pe(void* object, void* function);   // restart.cpp, detour_process_event while g_c376_pe; true: not run
extern bool g_c345b_pe;                       // armed from EndLevel to the map end, both sides
bool c345b_pe(void* object, void* function); // true: the call is not run (detour_process_event)
void c345b_step(void* pc);                    // both sides, each tick (c50_step)
const uint8_t* c121_ui_find(const char* prefix, void** widget, int32_t* num, int32_t* index); // testrun.cpp
// Card 096: UGameplayStatics::SetGamePaused, which USTGameInstance::UpdatePause (and the pause menu) calls. On a listen
// server it pauses the whole world, so every client freezes. The host skips a pause while a client is connected
// (ponged); unpauses and every other case run the original.
using SetGamePausedFn = bool (*)(const void* world_context, bool paused);
extern SetGamePausedFn g_orig_c96_set_game_paused;
bool detour_c96_set_game_paused(const void* world_context, bool paused);
// Card 120: host, from sync_join_replay (card 326 "pause" row). Lifts a pause already in force when the client joins
// (see pause_c96.cpp).
void c96_lift_on_join(const void* pc);
// Card 320 (intro_c135.cpp), host: the title-screen hold. While it is on, card 096 lets the pause through and refuses
// the unpause, and card 120 does not lift it.
bool c320_holding();
void c364_expect_clients(int32_t n); // host, at a travel: clients a seamless travel carries to the next map (0: none)
void c320_receive_done(void* pc, uint32_t); // host: ChanOp::IntroDone from the client that owns pc
void c320_logout(const void* pc);           // host, from detour_logout
bool c320_unconfirmed(const void* pc);      // card 400, host: sent IntroStart on this map, no IntroDone yet
// --- UBWCOOP_AUTODROPSHIP (card 015) ---
// Gate: after FED_C1M1 loads, WB_Stack_C calls IADPausableObjectInterface::RequestGamePause
// (flag 1) for the mission-brief movie and the "PRESS ANY KEY TO CONTINUE" screen; while it is
// held, USTGameInstance::UpdatePause keeps the world paused, the transport dropship never
// ticks and never leaves the spline start. The screen's any-key check is BP-only
// (USTUserWidget::IsValidAsAnyKey is reached only through its exec thunk) and needs Slate
// keyboard focus: Enter injected through FSlateApplication::OnKeyDown/OnKeyUp (card015h) was
// handled elsewhere and never released the pause. The action is therefore the call WB_Stack_C
// itself makes when the screen closes: USTGameInstance::RequestGameUnpause_Implementation with
// the held requester, read fresh from the game instance's requester array each time. Repeated
// (capped) only while the world stays paused, in case the stack requests again.
// Card 032: maps that start on foot (FED_C4M1) hold the same WB_Stack_C pause for their mission
// brief, so the gate also runs with an on-foot pawn. Then it stays armed for the whole map: each
// later WB_Stack_C pause (a mid-mission "press to continue" screen, or a pause menu) is released
// the same way 3 s after it starts. Test switch only.
// Card 067: each action first closes the stack through ProcessEvent (c67_ui_close: skip the brief video, close the
// any-key screen), so host screenshots show the game; the bare unpause stays as the fallback.
constexpr size_t kWorldOwningGameInstance = 0x228; // AActor::GetGameInstance 0x037E59F0+0xE
constexpr size_t kGameInstancePauseIface = 0x1C0;  // RequestGamePause_Implementation+0x103
struct AutoDropship {
    ULONGLONG wait_end_ms; // 0 = no paused dropship tick seen yet
    ULONGLONG next_ms;     // 0 = still waiting for the client's PlayerController
    uint32_t actions;
};
extern AutoDropship g_auto;
// --- IA_Look / IA_Move evaluation (local player input only: PlayerInput exists only there) ---
struct ModifierEval {
    ObjectName cls;
    uint32_t in_nonzero;
    uint32_t out_nonzero;
    double in_max;
    double out_max;
};
struct ActionEval {
    uint32_t mappings;       // ProcessActionMappingEvent calls for this action
    uint32_t raw_nonzero;
    uint32_t modified_nonzero;
    uint32_t state[3];       // mapping EvaluateTriggers: None, Ongoing, Triggered
    double raw_max;
    double modified_max;
    uint32_t modifiers;      // mapping modifier count seen (max over the row)
    ModifierEval mod[kTrackedModifiers];
};
// Card 014: every action evaluated on the local player input, keyed by the UInputAction's FName
// value (ComparisonIndex, Number), which is stable for the process: kept as a value, not a pointer.
struct ActionStat {
    uint64_t name;
    ObjectName text;
    ActionEval eval;
    uint32_t handled; // binding Execute calls whose instance's SourceAction is this action
    // Card 016: the action-level EvaluateTriggers call in EvaluateInputDelegates (exe+0x446C0DB).
    uint32_t act_evals;
    uint32_t act_state[3];     // its return: None, Ongoing, Triggered (before the min with the mapping state)
    uint32_t last_state[3];    // the instance's LastTriggerState at entry: the previous frame's final state
    uint32_t no_trigger_hit;   // tracker NoTriggerState == Triggered at entry (|value|^2 >= 1e-8)
    uint32_t mapping_applied;  // bMappingTriggerApplied set at entry (the result is min'd with the mapping state)
    uint32_t paused;           // evaluations with bGamePaused (the final state is None unless bTriggerWhenPaused)
};
struct ActionTable {
    uint32_t count;
    ActionStat entry[kTrackedActions];
    ActionStat other; // actions past kTrackedActions, summed
};
extern ActionTable g_actions;
void reset_actions();
void detour_execute_value(const void* binding, const void* instance);
void detour_execute_void(const void* binding, const void* instance);
void detour_execute_dynamic(const void* binding, const void* instance);
void detour_process_mapping(void* player_input, const void* const* action, float delta_seconds, bool paused,
                            const void* raw_value, uint8_t key_event, const void* modifiers, const void* triggers,
                            bool always_tick);
void* detour_apply_modifiers(const void* player_input, void* ret, const void* modifiers, const void* value,
                             float delta_seconds);
// --- Card 016: action-level modifiers (UInputAction::Modifiers, run after the mappings merge) ---
// IA_Look carries USTInputModifierTimeDilationLookScale (ModifyRaw_Implementation 0x0512E260: scales
// by YawPitchInputScaleWhileTimeDilation +0x28/+0x30 only while time is dilated) and
// USTInputGASPlayerAttributeModifier (0x0512DF10: multiplies the value by the GAS attribute at +0x28,
// read through the ability system component the player input resolves; unchanged when not found).
// Logged per modifier object: the first calls whose input is nonzero, capped per map.
constexpr uint32_t kActionModSlots = 8;
struct ActionModLog {
    const void* modifier[kActionModSlots]; // identity only, compared, never dereferenced across frames
    uint32_t lines[kActionModSlots];
};
extern ActionModLog g_action_mods;
void* detour_modify_raw(const void* modifier, void* ret, const void* player_input, const void* value,
                        float delta_seconds);
constexpr uint32_t kDumpedActions = 16;
using EvaluateInputDelegatesFn = void (*)(void* player_input, const void* components, float delta_seconds,
                                          bool paused, const void* key_states);
extern EvaluateInputDelegatesFn g_orig_evaluate_delegates;
struct DumpedActions {
    uint32_t count;
    uint64_t name[kDumpedActions];
};
extern DumpedActions g_dumped;
void resolve_action_eval_site();
void detour_evaluate_delegates(void* player_input, const void* components, float delta_seconds, bool paused,
                               const void* key_states);
uint8_t detour_evaluate_triggers(void* tracker, const void* player_input, const void* triggers, const void* value,
                                 float delta_seconds);
void log_actions(const char* tag, uint32_t k);
void log_card013_row(const char* tag, uint32_t k);
// --- Card 016: jump and sprint call chain (log only, capped per map) ----------------------
// IA_Jump/IA_Sprint bind to ASTPlayerController::HandleInputTag_Pressed/_Released (GAS input tags):
// the PC needs its input receiver (+0x7C0) and a flag bit in +0x618, then the receiver (the
// character's ISTInputReceiverInterface) hands the tag to its ability system component. The jump
// ability ends in ASTPlayerCharacterMI::Jump -> ACharacter::Jump (bPressedJump = bit 3 of +0x450)
// and ACharacter::CanJump. USTSprintAbility::HandleSprintChange applies a set-by-caller gameplay
// effect (handle stored at ability+0x3B8). Every original runs unchanged with its own arguments.
using InputTagFn = void (*)(void* self, uint64_t tag); // FGameplayTag (one FName) passed by value
using AscInputTagFn = void (*)(void* asc, const void* tag);
using TryActivateFn = bool (*)(void* asc, int32_t handle, bool allow_remote);
using InternalTryActivateFn = bool (*)(void* asc, int32_t handle, const void* prediction_key, void** out_ability,
                                       void* on_ended, const void* event_data);
using ServerTryActivateFn = void (*)(void* asc, int32_t handle, bool input_pressed, const void* prediction_key,
                                     const void* event_data);
using ClientActivateFailedFn = void (*)(void* asc, int32_t handle, int16_t prediction_key);
using ClientActivateSucceedFn = void (*)(void* asc, int32_t handle, const void* prediction_key);
using CharacterFn = void (*)(void* character);
using CharacterBoolFn = bool (*)(const void* character);
using CheckJumpInputFn = void (*)(void* character, float delta_seconds);
using SprintChangeFn = void (*)(void* ability, float value);
extern InputTagFn g_orig_pc_tag_pressed;
extern InputTagFn g_orig_pc_tag_released;
extern InputTagFn g_orig_char_tag_pressed;
extern AscInputTagFn g_orig_asc_tag_pressed;
extern TryActivateFn g_orig_try_activate;
extern InternalTryActivateFn g_orig_internal_try_activate;
extern ServerTryActivateFn g_orig_server_try_activate;
extern ClientActivateFailedFn g_orig_client_activate_failed;
extern ClientActivateSucceedFn g_orig_client_activate_succeed;
extern CharacterFn g_orig_character_jump;
extern CharacterFn g_orig_mi_jump;
extern CharacterBoolFn g_orig_can_jump;
extern CharacterBoolFn g_orig_can_jump_internal;
extern CheckJumpInputFn g_orig_check_jump_input;
extern SprintChangeFn g_orig_sprint_change;
constexpr size_t kSprintEffectHandle = 0x3B8;  // HandleSprintChange+0x1F5 stores the applied effect's handle
constexpr uint32_t kTagEdgeSlots = 8;
struct TagEdge {
    uint64_t tag[kTagEdgeSlots];
    ULONGLONG last_ms[kTagEdgeSlots];
};
struct JumpTrace {
    uint32_t lines;              // all card 016 jump/sprint lines but CheckJumpInput's, this map
    uint32_t check_lines;
    ULONGLONG tag_window_until;  // ability activations are logged until then (after a local input tag)
    bool in_server_try;          // inside InternalServerTryActivateAbility (host: a client's request)
    bool inner_tag_lines;        // inside a logged PC HandleInputTag call: its ASC/character calls log too
    TagEdge pressed;             // repeat filter for HandleInputTag_Pressed
    TagEdge released;            // repeat filter for HandleInputTag_Released
};
extern JumpTrace g_jump;
void reset_card016_map();
const char* side();
void detour_pc_tag_pressed(void* pc, uint64_t tag);
void detour_pc_tag_released(void* pc, uint64_t tag);
void detour_char_tag_pressed(void* receiver, uint64_t tag);
// UADAbilitySystemComponent::AbilityInputTagPressed (0x04A0EA00) looks the tag up in a
// TMap<FGameplayTag, TArray<FGameplayAbilitySpecHandle>> at +0x1260 (elements 0x20 apart: tag at +0,
// handles TArray at +0x8/+0x10) and returns at once when the map is empty (Num +0x1268 == free +0x1294)
// or the tag is missing. Linear scan, bounded; free slots are not told apart (log only).
constexpr size_t kAscInputTagMap = 0x1260;
constexpr size_t kAscInputTagMapNum = 0x1268;
constexpr size_t kAscInputTagMapFree = 0x1294;
constexpr size_t kAscInputTagMapStride = 0x20;
constexpr int32_t kAscInputTagMapMax = 256;
struct TagMapProbe {
    int32_t num;
    int32_t free;
    int32_t handles; // -1: the tag is not in the map
};
void c21_log_tag_pressed(const void* asc, const void* tag, const TagMapProbe& m);
bool c21_is_axis_tag(const char* name);
void detour_asc_tag_pressed(void* asc, const void* tag);
bool detour_try_activate(void* asc, int32_t handle, bool allow_remote);
extern bool g_c21_inner_seen;
bool detour_internal_try_activate(void* asc, int32_t handle, const void* prediction_key, void** out_ability,
                                  void* on_ended, const void* event_data);
void detour_server_try_activate(void* asc, int32_t handle, bool input_pressed, const void* prediction_key,
                                const void* event_data);
void detour_client_activate_failed(void* asc, int32_t handle, int16_t prediction_key);
void detour_client_activate_succeed(void* asc, int32_t handle, const void* prediction_key);
void detour_mi_jump(void* character);
void detour_character_jump(void* character);
bool detour_can_jump(const void* character);
bool detour_can_jump_internal(const void* character);
void detour_check_jump_input(void* character, float delta_seconds);
// --- Card 019: GA_Sprint counters per pawn side (state; the hooks are further down) -----------
// Side of the pawn a sprint event belongs to, from its roles: the host's own pawn (Authority, remote
// SimulatedProxy), the server copy of a client's pawn (Authority, remote AutonomousProxy) and a
// client's own pawn (AutonomousProxy).
enum SprintSide : uint32_t { kSideHostOwn, kSideServerCopy, kSideClientOwn, kSideOther, kSprintSides };
const char* const kSprintSideText[kSprintSides] = {"host-own", "server-copy", "client-own", "other"};
constexpr uint32_t kOwnRows = 24; // 12 s at kOwnRowIntervalMs: the host script runs 6.5 s after its delay
constexpr ULONGLONG kOwnRowIntervalMs = 500;
struct SprintCounts {
    uint32_t activates;
    uint32_t ends;
    uint32_t cancelled_ends; // EndAbility with bWasCancelled
    uint32_t cancels;        // UGameplayAbility::CancelAbility on a USTSprintAbility
    uint32_t changes;        // HandleSprintChange
    uint32_t walk_changes;   // USTAttributeSet_Movement::PostAttributeChange(WalkSpeed)
    float last_value;        // last HandleSprintChange value
    float last_walk;         // last WalkSpeed new value
};
struct SprintTrace {
    uint32_t lines[kSprintSides];      // logged sprint events, per side, this map
    uint32_t bp_lines[kSprintSides];   // logged blueprint native calls inside GA_Sprint activation
    bool props_logged[kSprintSides];   // the movement set's property row, once per side per map
    SprintCounts row[kSprintSides];    // since the side's last row
    uint32_t own_row;
    ULONGLONG own_next_ms;
};
extern SprintTrace g_sprint;
void reset_card020_map();
uint32_t sprint_side(const void* actor);
void log_sprint_row(const char* tag, uint32_t k, uint32_t side);
void start_own_rows();
extern uint32_t g_player_pawn_logs;
struct PawnWatch {
    uint32_t ticks;      // UGameEngine::Tick calls since the client map load
    uint64_t pawn_name;  // FName value of the own pawn at the previous Tick
    int32_t bit;         // bActorEnableCollision at the previous Tick, -1 before the first pawn
    uint32_t change_logs;
    uint32_t fix_logs;
    uint32_t reenables_map;
    uint32_t reenables_row;
    uint32_t hold_logs;  // card 015: dropship hold start/end lines
    bool held;           // card 015: the own pawn was held by a dropship at the previous Tick
};
constexpr PawnWatch kPawnWatchReset = {0, 0, -1, 0, 0, 0, 0, 0, false};
extern PawnWatch g_pawn_watch;
void detour_post_init(void* pawn);
void detour_possessed_by(void* pawn, void* controller);
const uint8_t* holding_dropship(const uint8_t* pawn);
void detour_perform_movement(void* cmc, float delta_seconds);
bool detour_player_input_key(void* player_input, const void* args);
// Card 482: the last non-axis key press the local PlayerInput saw, so an IA_Crouch press row names its source.
// injected: the mod's driver sent it (autoinput); driver_seq: the running driver command then, 0 when none.
struct C482Press {
    char key[32];
    ULONGLONG t;
    bool injected;
    uint32_t driver_seq;
};
extern C482Press g_c482_press;
void detour_handle_input_move(void* pc, const void* value);
void log_move_diag(uint32_t k);
void log_key_counts(const char* tag, uint32_t k);
void log_cmc(const char* tag, uint32_t k, const uint8_t* cmc, const void* owner);
// --- Card 011: mapping-context contents, host-side server move rows --------------------------
// Log only, game thread only, like card 010 above.
using ServerCheckClientErrorFn = bool (*)(void* cmc, float timestamp, float delta, const double* accel,
                                          const double* client_loc, const double* relative_client_loc, void* base,
                                          uint64_t base_bone, uint8_t mode);
using ServerMovePerformFn = void (*)(void* cmc, const void* move_data);
extern ServerCheckClientErrorFn g_orig_server_check_client_error;
extern ServerMovePerformFn g_orig_server_move_perform;
extern uint32_t g_mapping_lines;
bool text_contains(const char* text, const char* needle);
// Counts since the last host row (reset by each row).
struct ServerDiag {
    uint32_t moves;
    uint32_t checks;
    uint32_t corrections;
    Vec3 last_error; // server UpdatedComponent location - client location, at the last correction
    Vec3 last_accel; // the client acceleration of that move
};
extern ServerDiag g_srv;
extern uint32_t g_host_rows_left;
extern uint32_t g_host_row;
extern ULONGLONG g_host_next_ms;
void open_input_window();
bool detour_server_check_client_error(void* cmc, float timestamp, float delta, const double* accel,
                                      const double* client_loc, const double* relative_client_loc, void* base,
                                      uint64_t base_bone, uint8_t mode);
void host_row_step(const uint8_t* cmc);
// --- Card 019: the client's move input on the server copy of its pawn -------------------------
// GA_Sprint_C's IsMovingForward reads PawnMovementComponent.GetLastInputVector (APawn::
// LastControlInputVector). On the server copy of a remote pawn nothing adds movement input, so it
// is zero, the blueprint calls K2_EndAbility, and ClientEndAbility ends the client's predicted
// sprint every frame. Before each server tick of that pawn's CMC, the client's input (recovered from
// the acceleration of its last ServerMove, which the CMC keeps between moves) goes to the pawn
// through the engine's own input call, APawn::Internal_AddMovementInput (what
// UPawnMovementComponent::AddInputVector calls). UCharacterMovementComponent::TickComponent
// consumes it at once (ConsumeInputVector, vtable +0x608) into LastControlInputVector and does not
// move a remote pawn with it, so movement stays driven by the ServerMove alone. Once per tick, not
// once per move: a tick without a move would otherwise leave LastControlInputVector zero.
using CmcTickFn = void (*)(void* cmc, float delta, int32_t tick_type, void* tick_function);
extern CmcTickFn g_orig_cmc_tick;
constexpr size_t kPawnControlInputVector = 0x2F0; // APawn::ControlInputVector (Internal_AddMovementInput+0x22)
void detour_cmc_tick(void* cmc, float delta, int32_t tick_type, void* tick_function);
void detour_server_move_perform(void* cmc, const void* move_data);
double component_yaw(const void* component);
Vec3 component_rotator(const void* component); // card 206: ComponentToWorld rotation as pitch, yaw, roll
// Card 012: UEnhancedPlayerInput::AppliedInputContexts, a TMap<TObjectPtr<const UInputMappingContext>,
// int32 priority> at +0x538: TSet elements of 24 bytes, key at +0, priority at +8
// (IEnhancedInputSubsystemInterface::HasMappingContext +0x2B and +0xCE..+0xED). The interface's
// vtable slot 2 (+0x10) returns that PlayerInput (same function, +0x1A).
constexpr size_t kPlayerInputAppliedContexts = 0x538;
constexpr size_t kIfaceGetPlayerInput = 0x10 / sizeof(void*);
constexpr size_t kAppliedContextBytes = 24;
constexpr int32_t kMaxAppliedContexts = 64;
using GetPlayerInputFn = void* (*)(const void* iface);
// --- Card 017: the client pawn's GAS state (abilities, attribute sets, input-tag map) -----------
// Log only. Offsets from the exe (RVAs exe-relative):
//   UAbilitySystemComponent: ActivatableAbilities.Items +0x508 / Num +0x510 (FindAbilitySpecFromHandle,
//   0xF0 per spec, handle at spec+0xC, Ability at spec+0x10); SpawnedAttributes +0x10A8 / Num +0x10B0
//   (GetSpawnedAttributes); ReplicationMode byte +0x158 (SetReplicationMode); OwnerActor +0x3E0
//   (SetOwnerActor); AvatarActor +0x3E8 (SetAvatarActor_Direct); +0x7C0 != 0 = not authoritative
//   (IsOwnerActorAuthoritative); ability scope lock count +0x560 and the pending bind queue Num +0x12B8
//   (BindInputTagToAbilitySpec).
//   USTAttributeSet_PlayerCamera: CameraLookInputMultiplier FGameplayAttributeData at +0x40, BaseValue
//   +0x48, CurrentValue +0x4C (NewProp offset 64; the constructor zeroes it; no CPF_Net, no RepNotify).
//   AActor::Owner +0x158 (AActor::GetNetConnection); APlayerController::NetConnection +0x520
//   (APlayerController::GetNetConnection).
constexpr size_t kAscReplicationMode = 0x158;
constexpr size_t kAscPendingBindNum = 0x12B8;
constexpr size_t kPcNetConnection = 0x520;
// Card 434 step 3 (diagnostic). UActorChannel* UNetConnection::FindActorChannelRef(const TWeakObjectPtr<AActor>&)
// (pdb public). Virtual slots read from the APawn vtable (??_7APawn@@6BAActor@@@): +0x528 holds
// APawn::IsNetRelevantFor (exe+0x3F67330; the slot is declared on AActor, so every actor has it), +0x750 holds
// APawn::GetMovementBase (declared on APawn: call it on pawns only; ACharacter overrides it).
using C434FindChannelFn = void* (*)(void* conn, const C31Weak* actor);
using C434RelevantFn = bool (*)(const void* actor, const void* real_viewer, const void* view_target, const Vec3* src);
using C434BaseFn = void* (*)(const void* pawn);
extern C434FindChannelFn g_c434_find_channel;
bool c434_has_channel(const void* pc, const void* actor); // host: false only when pc's connection has no channel for actor
// Card 434 step 4 (diagnostic, client): bool UPackageMapClient::SerializeNewActor(FArchive&, UActorChannel*,
// AActor*&) and bool UActorChannel::CleanUp(bool bForDestroy, EChannelCloseReason) (pdb publics).
using C434NewActorFn = bool (*)(void* map, void* ar, void* channel, void** actor);
using C434CleanUpFn = bool (*)(void* channel, bool for_destroy, uint8_t reason);
extern C434NewActorFn g_orig_c434_new_actor;
extern C434CleanUpFn g_orig_c434_cleanup;
bool detour_c434_new_actor(void* map, void* ar, void* channel, void** actor);
bool detour_c434_cleanup(void* channel, bool for_destroy, uint8_t reason);
using C434SerializeObjectFn = bool (*)(void* map, void* ar, void* cls, void** object, uint64_t* out_guid);
extern C434SerializeObjectFn g_orig_c434_serialize_object;
// Card 434 (diagnostic). FNetGUIDCache::GetCacheObject (pdb public): the cache entry of a guid, or nullptr.
// FNetGuidCacheObject (read in FNetGUIDCache::IsGUIDBroken exe+0x3EABBA0 and IsGUIDPending exe+0x3EABC70):
// +0 Object (weak), +8 OuterGUID, +0x10 PathName, +0x18 NetworkChecksum, +0x28 bits NoLoad 1,
// IgnoreWhenMissing 2, Pending 4, Broken 8. UPackageMapClient::GuidCache is at +0x310
// (UPackageMapClient::Initialize exe+0x3EA9CD0).
using C434CacheObjectFn = const uint8_t* (*)(const void* cache, const uint64_t* guid);
extern C434CacheObjectFn g_c434_cache_object;
constexpr size_t kMapGuidCache = 0x310;
bool detour_c434_serialize_object(void* map, void* ar, void* cls, void** object, uint64_t* out_guid);
constexpr size_t kActorVtIsNetRelevantFor = 0x528;
constexpr size_t kPawnVtGetMovementBase = 0x750;
constexpr size_t kConnPackageMap = 0x68; // UNetConnection::PackageMap (stored by UNetConnection::InitBase 0x03E6E440)
constexpr int32_t kC17SetsShown = 8;
constexpr int kC17Slots = 4;                 // PlayerControllers looked at per tick (host: own + remote)
using GiveAbilityFn = void* (*)(void* asc, void* out_handle, const void* spec);
using BindInputTagFn = void (*)(void* asc, const void* tag, const void* handle);
// TSubclassOf<> by value is passed by hidden pointer to the caller's copy (call sites: lea rdx, [rsp+..]).
using InitFeaturesFn = void (*)(void* actor, void* const* definition, void* context);
using AddAttributeSetFn = void (*)(void* asc, void* const* init_data);
using PostAttributeChangeFn = void (*)(void* set, const void* attribute, float old_value, float new_value);
extern GiveAbilityFn g_orig_give_ability;
extern BindInputTagFn g_orig_bind_input_tag;
extern InitFeaturesFn g_orig_init_features;
extern AddAttributeSetFn g_orig_add_attribute_set;
extern PostAttributeChangeFn g_orig_camera_post_change;
extern StaticClassFn g_player_mi_class;
extern StaticClassFn g_camera_set_class;
extern StaticClassFn g_movement_set_class;
struct C17Slot {
    uint64_t pawn_name; // FName of the on-foot pawn last seen in this slot (no pointer kept)
    ULONGLONG land_ms;  // 0 until the landing sample
    uint8_t done;       // bit 0 possession, bit 1 landing, bit 2 landing + 10 s
};
struct C17State {
    C17Slot slots[kC17Slots];
    ULONGLONG start_ms;
    uint32_t give_logs;
    uint32_t bind_logs;
    uint32_t init_logs;
    uint32_t attr_logs;
    uint32_t change_logs;
    uint32_t row_logs;
};
extern C17State g_c17;
constexpr uint32_t kC18MaxBoundTags = 64;
// Card 018 per-map state (reset with card 017's). Only FNames are kept across calls, no pointers.
struct C18State {
    uint64_t fix_done_asc;    // player_asc_key of the ASC the camera re-init ran (or was refused) on
    uint32_t audit_logs;
    uint32_t ui_only_logs;
    uint32_t fix_logs;
    uint64_t bind_asc;                     // player_asc_key of the ASC whose binds bound_tags lists
    uint64_t bound_tags[kC18MaxBoundTags]; // tag FNames bound on bind_asc this map, paired with bound_handles
    int32_t bound_handles[kC18MaxBoundTags]; // spec handle each tag was bound to (each tag, handle pair once)
    uint32_t bound_tags_num;
    bool bind_pending; // OnRep_ActivateAbilities ran before the pawn was LocalRole 2
    uint32_t bind_logs;
};
extern C18State g_c18;
void* detour_give_ability(void* asc, void* out_handle, const void* spec);
void detour_bind_input_tag(void* asc, const void* tag, const void* handle);
void detour_init_features(void* actor, void* const* definition, void* context);
void detour_add_attribute_set(void* asc, void* const* init_data);
ObjectName attribute_name(const void* attribute);
void detour_camera_post_change(void* set, const void* attribute, float old_value, float new_value);
// --- Card 018: client camera attribute re-init and the UIOnly caller --------------------------
// Offsets from the exe (RVAs exe-relative):
//   ASTPlayerCharacter definition class (TSubclassOf<UADActorDefinition>) +0x698: the caller of
//   InitFeaturesFromDefinition (exe+0x50FC576) passes a copy of it. UClass::ClassDefaultObject +0x110
//   (InitFeaturesFromDefinition +0x10E). UADActorDefinition CDO: attribute init data
//   TArray<TSubclassOf<UADAttributeSetInitData>> +0x48 / Num +0x50. UADAttributeSetInitData CDO: the
//   attribute set class +0x28 (AddAttributeSetFromData).
//   UStruct::ChildProperties +0x50; FField::Next +0x18, NamePrivate +0x20; FProperty::PropertyFlags
//   +0x38, Offset_Internal +0x44 (checked at run time: CameraLookInputMultiplier must read 0x40).
//   FGameplayAttributeData: BaseValue +0x8, CurrentValue +0xC.
//   FFrame::Object +0x18.
constexpr size_t kPawnDefinition = 0x698;
constexpr size_t kDefinitionAttrInit = 0x48;
constexpr size_t kDefinitionAttrInitNum = 0x50;
constexpr size_t kInitDataSetClass = 0x28;
constexpr int kC18MaxProps = 16;      // the camera set declares 6
constexpr int32_t kC18MaxInitData = 16;
using OnRepSpawnedAttributesFn = void (*)(void* asc, const void* previous);
extern OnRepSpawnedAttributesFn g_orig_onrep_spawned_attributes;
extern ExecThunkFn g_orig_exec_ui_only;
void* camera_init_data(const void* pawn, const void* set_class);
// --- Card 019: client movement attribute re-init ---------------------------------------------
// USTAttributeSet_Movement has the camera set's problem: no attribute has CPF_Net, the replicated
// subobject that OnRep_SpawnedAttributes puts in place holds 0 in every attribute, and nothing on the
// client sets them. Once the server's GE_Sprint replicates in, the client's aggregator recomputes
// WalkSpeed from base 0 and USTAttributeSet_Movement::PostAttributeChange sets MaxWalkSpeed 0, which
// stays after the sprint ends. The same fix as card 018: re-run the game's own
// AddAttributeSetFromData with the movement set's init data from the pawn definition.
constexpr int32_t kWalkSpeedOffset = 0x30; // USTAttributeSet_Movement::WalkSpeed (the property walk checks it)
struct MovementLayout {
    int32_t props;
    int32_t net_props;
    bool walk_offset_ok; // WalkSpeed found at kWalkSpeedOffset
};
MovementLayout movement_layout(const void* set_class);
const void* find_movement_set(const void* asc);
void card019_movement_fix(void* asc, const uint8_t* pawn);
void detour_onrep_spawned_attributes(void* asc, const void* previous);
using OnRepActivateAbilitiesFn = void (*)(void* asc);
extern OnRepActivateAbilitiesFn g_orig_onrep_activate_abilities;
void detour_onrep_activate_abilities(void* asc);
void detour_exec_ui_only(void* context, void* frame, void* result);
// --- Card 019: GA_Sprint lifecycle, its gameplay effect and the speed path ------------------
// Offsets from the exe (RVAs exe-relative), signatures checked against the disassembly:
//   USTSprintAbility::ActivateAbility (0x05114C80): rcx this, edx handle, r8 ActorInfo*, r9 points
//     to the caller's FGameplayAbilityActivationInfo (by value > 8 bytes: movups [r13] at +0x4D),
//     EventData* on the stack.
//   USTSprintAbility::EndAbility (0x0511CEB0): rcx, edx, r8, r9 ActivationInfo* as above, then
//     bool bReplicateEndAbility (stack +0x28) and bool bWasCancelled (stack +0x30); it calls
//     UGameplayAbility::EndAbility, then stores -1 to the effect handle at +0x3B8.
//   UGameplayAbility::CancelAbility (0x0492A1B0): rcx, edx, r8, r9 ActivationInfo* (movups [r12]),
//     bool bReplicateCancelAbility. USTSprintAbility does not override it.
//   USTSprintAbility::HandleSprintChange (0x051272C0): GE class at +0x3B0; SetSetByCallerMagnitude
//     (tag at exe+0x943A8C8, value) at +0x1A1 and (tag at this+0x3C0, 1.0) at +0x1B8; applies it
//     through the ASC and stores the 8-byte handle at +0x3B8 (+0x1F5).
//   USTAttributeSet_Movement::PostAttributeChange (0x051D7BB0): compares the attribute with
//     WalkSpeed, GetOwningActor (the set's outer), and if it is an ACharacter writes the new value
//     to its CMC MaxWalkSpeed: the attribute-to-speed path.
//   UGameplayAbility::CurrentActorInfo +0x328; FGameplayAbilityActorInfo::AvatarActor (weak) +0x10.
using SprintActivateFn = void (*)(void* ability, int32_t handle, const void* actor_info, const void* activation_info,
                                  const void* event_data);
using SprintEndFn = void (*)(void* ability, int32_t handle, const void* actor_info, const void* activation_info,
                             bool replicate, bool was_cancelled);
using CancelAbilityFn = void (*)(void* ability, int32_t handle, const void* actor_info, const void* activation_info,
                                 bool replicate);
extern SprintActivateFn g_orig_sprint_activate;
extern SprintEndFn g_orig_sprint_end;
extern CancelAbilityFn g_orig_cancel_ability;
extern PostAttributeChangeFn g_orig_movement_post_change;
extern StaticClassFn g_sprint_ability_class;
constexpr uint32_t kStackFrames = 4;      // callers logged per event
// Inside GA_Sprint ActivateAbility on the game thread: UFunction::Invoke logs the blueprint's
// native calls (which call decides the immediate K2_EndAbility). Cleared when activation returns.
using InvokeFn = void (*)(void* function, void* object, void* frame, void* result);
extern InvokeFn g_orig_invoke;
constexpr uint32_t kMaxStackFrames = 16; // ceiling for a deeper caller_chain (card 048)
void caller_chain(char* out, size_t cap, uint32_t depth = kStackFrames);
void detour_sprint_activate(void* ability, int32_t handle, const void* actor_info, const void* activation_info,
                            const void* event_data);
void detour_sprint_end(void* ability, int32_t handle, const void* actor_info, const void* activation_info,
                       bool replicate, bool was_cancelled);
void detour_cancel_ability(void* ability, int32_t handle, const void* actor_info, const void* activation_info,
                           bool replicate);
void detour_sprint_change(void* ability, float value);
void detour_invoke(void* function, void* object, void* frame, void* result);
void detour_movement_post_change(void* set, const void* attribute, float old_value, float new_value);
constexpr size_t kCharacterCamera = 0x818;            // ASTPlayerCharacter::CameraComponent
constexpr size_t kCharacterInteractor = 0x858;        // ASTPlayerCharacter::InteractorComponent
constexpr size_t kMiWeaponInventory = 0xA40;          // ASTPlayerCharacterMI weapon inventory
constexpr size_t kBugFormsInventory = 0xA30;          // ASTPlayerCharacterBug::FormsInventory (USTBugFormInventory, a
                                                      // UADWeaponInventory; card 196)
constexpr size_t kTurretWeaponInventory = 0x790;      // ASTMountableTurret.WeaponInventory (card 125)
constexpr size_t kInteractorTarget = 0x100;           // IADInteractableInterface* (UADInteractorComponent::Interact)
constexpr size_t kIfaceGetObject = 0x20 / sizeof(void*); // interface vtable slot returning the UObject
constexpr int32_t kAttributeDataBytes = 0x10;         // FGameplayAttributeData: vtable, Base, Current
constexpr uint32_t kC20Rows = 200;                    // 100 s: on the host, its script then the client's; on the client, its 62 s delay and script
constexpr int kC20MaxAttrs = 96;
constexpr int32_t kC20MaxSpecs = 32;
constexpr int32_t kC20MaxTags = 48;
constexpr int32_t kC20MaxEffects = 32;
constexpr int kC20Sides = 2;            // [0] the local own pawn, [1] the server copy (host only)
using GetBaseAimRotationFn = double* (*)(const void* pawn, double* out_rotator);
using GetEyesViewPointFn = void (*)(const void* pawn, double* out_location, double* out_rotator);
using GetEquippedWeaponFn = void* (*)(const void* inventory);
using GetWeaponCountFn = int32_t (*)(const void* inventory);
using GetAmmoCountFn = int32_t (*)(const void* weapon);
using GetEquippedItemFn = const void* (*)(const void* inventory);
using GetStackCountFn = int32_t (*)(const void* spec);
using IfaceGetObjectFn = void* (*)(const void* iface);
using ZeroAdjustFn = void (*)(const void* weapon, double* transform);
using GetMuzzleTransformFn = double* (*)(double* out_transform, const void* weapon); // card 316
using FireProjectileFn = void* (*)(void* ability, const double* transform, void* init_data_class);
using InteractFn = void (*)(void* interactor);
extern GetBaseAimRotationFn g_get_base_aim;
extern GetEyesViewPointFn g_get_eyes_view_point;
extern GetEquippedWeaponFn g_get_equipped_weapon;
extern GetWeaponCountFn g_get_weapon_count;
extern GetAmmoCountFn g_get_ammo_count;
extern GetEquippedItemFn g_get_equipped_item;
extern GetStackCountFn g_get_stack_count;
extern ZeroAdjustFn g_orig_zero_adjust;
extern GetMuzzleTransformFn g_get_muzzle_transform; // card 316 (kCallsUbw)
extern FireProjectileFn g_orig_fire_projectile;
extern InteractFn g_orig_interact;
// What one side's last GAS row showed; a different pawn FName starts it over with a full listing.
struct C20Gas {
    uint64_t pawn_name;
    bool seen;
    int attrs;
    float base[kC20MaxAttrs];
    float current[kC20MaxAttrs];
    int32_t specs;
    uint8_t spec_active[kC20MaxSpecs];
    uint8_t spec_flags[kC20MaxSpecs];
    int32_t tags;
    uint64_t tag[kC20MaxTags];
    int32_t effects;
    int32_t effect_handle[kC20MaxEffects];
    int32_t effect_stacks[kC20MaxEffects];
    uint32_t lines;
};
struct C20State {
    uint32_t row;
    ULONGLONG next_ms;
    C20Gas gas[kC20Sides];
    uint32_t zero_lines[kSprintSides];
    uint32_t fire_lines[kSprintSides];
    uint32_t interact_lines[kSprintSides];
};
extern C20State g_c20;
extern uint32_t g_c21_grant_lines;
struct C21Grant {
    ULONGLONG next_ms;
    ULONGLONG damage_at_ms; // host: the step 5 test hit on the server copy; 0 until that copy holds the weapon
    ULONGLONG next_set_dump_ms; // card 021 step 5 diagnosis: next health-set dump (kC21SetDumpMs apart)
    uint32_t done;
    uint32_t side_grants[kSprintSides]; // grants made this map per pawn side; the first one equips
    const void* granted[kSprintSides];  // the weapon the side's last equipping grant put in its hands
    bool held_granted[kSprintSides];    // that weapon was in the hands at the last grant step (1 s apart)
    ULONGLONG missing_since[kSprintSides]; // a regrant waits one grant step after the weapon went missing
    bool damaged;
};
extern float g_c21_health_seen[kSprintSides];
extern C21Grant g_c21_grant;
Vec3 camera_location(const void* pawn);
const void* iface_object(const void* iface);
const void* interact_target(const void* interactor);
void detour_zero_adjust(const void* weapon, double* transform);
void* detour_fire_projectile(void* ability, const double* transform, void* init_data_class);
// Card 024/181: the pawn that fires a weapon or pawn ability (the avatar, or its weapon's Owner pawn); the avatar too.
const void* ability_shooter(const void* ability, const void** avatar_out);
// Card 181 (round_c181.cpp): USTWeaponFireAbility::Fire, the round report (client) and the round gate (host copy).
using WeaponFireFn = void (*)(void* ability);
extern WeaponFireFn g_orig_weapon_fire;
void detour_weapon_fire(void* ability);
void c181_receive_round(void* pc, uint32_t weapon_key); // host: ChanOp::Round (c437_weapon_key of the client's weapon)
void c439_fire_begin();                  // client: the own Fire starts; an Equip sent inside it is held
void c439_fire_end();                    // client: the own Fire's Round is out; the held Equip goes now
void c439_before_equip(const void* pawn); // host: a copy about to equip fires one unspent reported round first
void c181_clear(int32_t pawn_id, const char* who, const char* why); // host: primary press, release or pawn gone
uint32_t c181_credits(int32_t pawn_id); // host: reported rounds not yet fired by the copy (c276_repress)
void c437_note(const void* pawn, uint32_t fired); // host: a held primary relay ended because the copy's weapon changed
                                                   // (fired: the press's rounds, its own plus the ones it carried)
void c437_drop(int32_t pawn_id);  // host: the client pressed or released primary fire
// Card 081: a Downed, Out or Dead player's interactions are refused (own presses, relayed Use, the focus trace).
constexpr uint32_t kC81Lines = 40; // per kind, per process
struct C81State {
    uint32_t press_lines, relay_lines, trace_lines, mech_lines;
    uint32_t presses, relays, mech_skips;
    bool trace_hidden; // this machine's own interactor trace is returning nothing
};
extern C81State g_c81;
using TraceInteractableFn = void* (*)(void* interactor);
extern TraceInteractableFn g_orig_trace_interactable;
bool c81_refuse_press(const void* interactor, const void* owner);
void* detour_trace_interactable(void* interactor);
// Card 130 item 11: the revive prompt. A player pawn is not an IADInteractableInterface, so the game's interact prompt
// (WB_Pickup_Prompt) never shows for a downed teammate. Each machine keeps one local, unreplicated
// ASTLimitedInteractionActor per map with its prompt set to kC130ReviveText; the card 081 trace returns its interface
// while c43_revivable_near finds a teammate. A press on it starts only the card 043 hold (detour_interact).
// The classes are card 054's g_c54_limited_class and card 050's g_c50_interactable_class.
const void* c43_revivable_near(const void* own);
void* c130_revive_iface(const void* own);
bool c130_is_revive_target(const void* target);
void detour_interact(void* interactor);
void detour_release_interaction(void* interactor);
using GiveByClassFn = bool (*)(void* inventory, void* const* definition_class, int32_t slot, bool auto_equip);
using GiveByInstanceFn = bool (*)(void* inventory, void* weapon, int32_t slot, bool auto_equip);
using AddWeaponFn = bool (*)(void* actor, void* definition, bool equip);
using LoadInventoryFn = void* (*)(void* inventory, void* out_array);
using ExecFn = void (*)(void* context, void* frame, void* result);
extern GiveByClassFn g_orig_give_by_class;
extern GiveByInstanceFn g_orig_give_by_instance;
// GiveWeaponByInstance itself (a call row). Callers outside detour_give_by_instance use this one: the hook row is
// diag-gated, so g_orig_give_by_instance is null in a play build (b16, the card 312 crash).
extern GiveByInstanceFn g_give_by_instance;
extern AddWeaponFn g_orig_add_weapon;
extern LoadInventoryFn g_orig_load_inventory;
extern ExecFn g_orig_exec_add_weapon;
extern ExecFn g_orig_exec_add_weapon_by_tag;
extern ExecFn g_orig_exec_give_by_class;
extern ExecFn g_orig_exec_give_by_instance;
bool detour_give_by_class(void* inventory, void* const* definition_class, int32_t slot, bool auto_equip);
bool detour_give_by_instance(void* inventory, void* weapon, int32_t slot, bool auto_equip);
bool detour_add_weapon(void* actor, void* definition, bool equip);
void* detour_load_inventory(void* inventory, void* out_array);
void detour_exec_add_weapon(void* context, void* frame, void* result);
void detour_exec_add_weapon_by_tag(void* context, void* frame, void* result);
void detour_exec_give_by_class(void* context, void* frame, void* result);
void detour_exec_give_by_instance(void* context, void* frame, void* result);
// Card 267: level script weapon changes on any player pawn (host's own or a server copy), mirrored to every other
// player pawn (card 310 fix item 1).
// Clear (card 463): a drop inside the level script's RemovePlayersWeapons; the mirror empties each other pawn's list.
enum class C267Call : uint8_t { None, Add, Give, Drop, Clear };
extern C267Call g_c267_call;
// Card 463: client. Non-null while the client's own FED_C1M1 level script runs one of its weapon edit functions
// (RemovePlayersWeapons, GiveUnarmedWeapon, GiveMorita, GiveLaser): the native weapon edit is skipped, the host's
// card 267 mirror and the card 050 list carry it. The UFunction of that frame.
extern const void* g_c463_skip_node;
bool c463_client_skip(const char* native, const void* what);
bool c267_level_script_frame(const void* frame);
bool c267_source(const void* pawn);
int32_t c267_other_players(const void* source, void** out, int32_t cap);
extern ExecFn g_orig_exec_drop_current_weapon;
extern ExecFn g_orig_exec_drop_weapon;
extern ExecFn g_orig_exec_drop_weapon_by_tag;
void detour_exec_drop_current_weapon(void* context, void* frame, void* result);
void detour_exec_drop_weapon(void* context, void* frame, void* result);
void detour_exec_drop_weapon_by_tag(void* context, void* frame, void* result);
void c267_mirror_drop(const void* source, const void* weapon_class, bool arg2);
// Card 278: host, each tick (death.cpp c50_step, after c143_host_step): the map's script loadout rows replayed on
// each new MI pawn of a client server copy.
void c278_host_step(void* world);
// Card 310: a level script's per-player write on any player pawn reaches every player pawn and each client that
// reads it (ubw/script_c310.cpp). The exec thunks set the kind while a host level script frame runs; the impl
// detours mirror the write through their trampolines with the kind cleared.
enum class C310Kind : uint8_t { None, Faction, Squad, Charge, Item, Ammo, TagAdd, TagRemove };
extern C310Kind g_c310_kind;
using C310TagSetFn = void (*)(void* component, const void* tag);                  // SetFactionTag(const FGameplayTag&)
using C310BoolFn = void (*)(void* component, bool on);                            // SetEnabled, SetCanTacticalSupportBeCharged
using C310CountUpdateFn = bool (*)(void* container, const void* tag, int32_t delta); // FGameplayTagCountContainer
using C310AscUpdateFn = void (*)(void* asc, const void* container, int32_t delta);  // UAbilitySystemComponent
using C310TagCountFn = int32_t (*)(const void* asc, uint64_t tag);                // FGameplayTag by value
extern ExecFn g_orig_c310_exec_faction, g_orig_c310_exec_squad, g_orig_c310_exec_charge, g_orig_c310_exec_item,
    g_orig_c310_exec_ammo, g_orig_c310_exec_tag_add, g_orig_c310_exec_tag_remove;
extern C310TagSetFn g_orig_c310_set_faction;
extern C310BoolFn g_orig_c310_set_squad, g_orig_c310_set_charge;
extern C310CountUpdateFn g_orig_c310_count_update;
extern C310AscUpdateFn g_orig_c310_asc_update;
extern C310TagCountFn g_c310_tag_count;
void detour_c310_exec_faction(void* context, void* frame, void* result);
void detour_c310_exec_squad(void* context, void* frame, void* result);
void detour_c310_exec_charge(void* context, void* frame, void* result);
void detour_c310_exec_item(void* context, void* frame, void* result);
void detour_c310_exec_ammo(void* context, void* frame, void* result);
void detour_c310_exec_tag_add(void* context, void* frame, void* result);
void detour_c310_exec_tag_remove(void* context, void* frame, void* result);
void detour_c310_set_faction(void* comp, const void* tag);
void detour_c310_set_squad(void* comp, bool on);
void detour_c310_set_charge(void* comp, bool on);
int32_t detour_c310_add_ammo(void* actor, uint64_t tag, int32_t count);
bool detour_c310_count_update(void* container, const void* tag, int32_t delta);
void detour_c310_asc_update(void* asc, const void* container, int32_t delta);
void c310_mirror_tags(const void* world, bool add);
// From detour_c73_remove, after the source's removal: the same removal on every other player pawn.
void c310_note_item_remove(void* inventory, void* const* definition, int32_t amount, bool flag);
void c310_host_step(void* world, ULONGLONG now); // death.cpp c50_step, after c278_host_step
void c310_client_step(void* pc);                 // death.cpp c50_step, client
void c310_arm_map();                             // sprint.cpp c50_arm_map
void c310_map_end(void* old_world);              // hooks_ubw.cpp ubw_load_map_enter, before LoadMap
void c310_receive(void* pc, const WireString* s); // ChanOp::ScriptWrite
void c310_join_replay(void* pc);                 // host: sync_join_replay "script" row (card 139)
// ASTPlayerCharacter::AssignStartingInventories reads the level data selected by the game instance's
// chapter (+0x410) and level (+0x414) indices (AssignStartingInventories+0x54, +0x5B).
constexpr size_t kGameInstanceChapter = 0x410;
constexpr size_t kGameInstanceLevel = 0x414;
constexpr size_t kSparseNumFree = 0x34;  // TSparseArray: Data 0x10, AllocationFlags 0x20, FirstFree, NumFree
using AssignStartingFn = void (*)(void* pawn, void* slots, void* weapons, void* equip, void* items);
using GetGameInstanceFn = void* (*)(const void* context);
// Card 375: USTGameInstance::LoadComplete (exe+0x50F2970). With the deferred-load flag (byte +0x309, set by
// StoreDeferredLoadParams) it runs ApplyDeferredLoadParams(params at +0x310) and clears the flag; it reads only `this`.
constexpr size_t kGameInstanceDeferredLoad = 0x309;
using GameInstanceLoadCompleteFn = void (*)(void* game_instance, float load_seconds, const FStringRaw* map_name);
extern GameInstanceLoadCompleteFn g_c375_load_complete;
extern AssignStartingFn g_orig_assign_starting;
extern GetGameInstanceFn g_get_st_game_instance;
// Weapon definition DataTable (GetWeaponDefinitionFromTag+0x1C..+0x6E): the engine subsystem holds it
// as a weak pointer at +0x38; UDataTable::GetRowMap is vtable +0x2D0 (FindRow+0x5A). RowMap elements
// are 0x18 bytes: FName key, row pointer, hash link. A row holds its tag at +0x8 and a soft class
// path at +0x18 (asset name FName at +0x20).
constexpr size_t kDataTableSubsystemWeapons = 0x38;
constexpr size_t kDataTableGetRowMap = 0x2D0;
constexpr size_t kWeaponRowTag = 0x8;
constexpr int32_t kC21RowsListed = 48;
using GetRowMapFn = const uint8_t* (*)(const void* table);
const void* c21_weapon_table(const void* game_instance);
const uint8_t* c21_row_map(const void* table);
void detour_assign_starting(void* pawn, void* slots, void* weapons, void* equip, void* items);
using WeaponDefFromTagFn = void* (*)(const void* tag);
using WeaponIndexFromTagFn = int32_t (*)(const void* inventory, const void* tag);
extern WeaponDefFromTagFn g_weapon_def_from_tag;
extern WeaponIndexFromTagFn g_weapon_index_from_tag;
// Card 137 step 5: a granted weapon's starting ammo. FGameplayTag (one FName, 8 bytes) goes by value in a register.
using WeaponFromClassFn = void* (*)(const void* inventory, void* const* definition_class); // TSubclassOf by pointer (the exe)
using AmmoUsesMagazineFn = bool (*)(const void* weapon, const uint64_t* tag, uint8_t behaviour);
using SetEquippedAmmoFn = void (*)(const void* weapon, uint64_t tag, int32_t count, uint8_t behaviour);
using AddAmmunitionFn = int32_t (*)(void* actor, uint64_t tag, int32_t count);
extern WeaponFromClassFn g_weapon_from_class;
// Card 137: a weapon definition class's starting ammunition to the pawn (weapons.cpp); card 243 `ammo fill` too.
void c21_grant_ammo(void* pawn, void* inventory, void* cls);
extern AmmoUsesMagazineFn g_ammo_uses_magazine;
extern SetEquippedAmmoFn g_set_equipped_ammo;
extern AddAmmunitionFn g_add_ammunition;
extern AddAmmunitionFn g_orig_c310_add_ammo; // card 310: the AddAmmunition hook (script_c310.cpp)
// USceneComponent::SetWorldRotation(FRotator, bool bSweep, FHitResult*, ETeleportType): the FRotator (3
// doubles) goes by hidden pointer in rdx (disassembly 0x03AF7C80 reads [rdx], [rdx+0x10]).
using SetWorldRotationFn = void (*)(void* component, const double* rotator, bool sweep, void* hit, uint8_t teleport);
extern SetWorldRotationFn g_set_world_rotation;
const uint8_t* c21_row_tag(const void* table, const char* row);
constexpr int kC21MaxSetProps = 24; // the set declares 11
constexpr size_t kCharacterHealthComponent = 0x688; // ASTCharacter::HealthComponent
constexpr size_t kHealthComponentSet = 0x130;       // UADHealthComponent::HealthAttributeSet
constexpr size_t kHealthComponentAsc = 0x128;       // UADHealthComponent::AbilitySystemComponent
constexpr size_t kHealthSetHealth = 0x30;
constexpr size_t kAttributeChangeBytes = 0x48;      // FOnAttributeChangeData; the handler reads +0x38/+0x3C only
using HealthChangedFn = void (*)(void* component, const void* change_data);
extern StaticClassFn g_health_set_class;
extern HealthChangedFn g_health_attribute_changed;
extern HealthChangedFn g_armour_attribute_changed; // card 170: UADHealthComponent::ArmourAttributeChanged, same shape
extern bool g_c21_in_notify;
extern ULONGLONG g_c21_notify_ms;
bool c21_is_net_attribute(const char* name);
// An ASTCharacter pointer field (kCharacterHealthComponent, kCharacterAsc) of `actor`, or nullptr when `actor` is not
// an ASTCharacter: those offsets are past the end of a smaller actor (hb42: a placed destructible crashed the host).
void* c21_character_ptr(const void* actor, size_t offset);
// Card 132: forget the owners recorded as showing the static mesh (movesync field 15 bit 3); called at LoadMap enter.
void c132_static_map_end();
void c132_rounds_map_end(); // grenades.cpp: empties the round and grenade tables at map load
void c306_step();      // vehicles.cpp, after the engine Tick: destroys this frame's inert own spawns (card 306)
void c306_map_end();   // vehicles.cpp, LoadMap enter: card 306 tally
void c75_map_end();         // vehicles.cpp: empties the mech pilot slots and the client mech identity at map load
float c21_health(const uint8_t* pawn, void** component_out);
constexpr int32_t kC21MaxSpawned = 16;
const void* c21_spawned_health_set(const void* asc, const void* own, int32_t* found);
const void* c21_adopt_set(uint8_t* component, const void** own_out);
bool c21_first_seen(int32_t key);
// ---- Card 024: client fire relay ----
// Step 1 (log only): which server-side ability RPC bodies run for the server copy's pawn ASC, per call with
// handle, ability class, ActiveCount and InputPressed after the call (the log prefix carries the time). The
// press/release bodies take the handle in edx; the end/cancel bodies take their structs by hidden pointer, passed
// through untouched. Client: every local activation, so bursts can be matched shot for shot.
constexpr uint32_t kC24RpcLines = 240;   // per map, host: covers two 2 s bursts at about 600 ms per shot
constexpr uint32_t kC24LocalLines = 120; // per map, client
// Card 214: a run of failed local activations of one handle (a held press the game refuses, e.g. a grounded Hopper
// Swoop) logs only these entries of the run; a success or another handle starts a new run.
constexpr uint32_t kC24FailRunLogs[] = {1, 10, 100};
// Card 212: card 148 ground probes that fell back from empty colliding-only bounds, first lines per side.
constexpr uint32_t kGibBoundsLines = 20;
using SpecHandleRpcFn = void (*)(void* asc, int32_t handle);
using EndAbilityRpcFn = void (*)(void* asc, int32_t handle, const void* activation_info, const void* prediction_key);
using CancelAbilityRpcFn = void (*)(void* asc, int32_t handle, const void* activation_info);
extern SpecHandleRpcFn g_orig_server_input_pressed;
extern SpecHandleRpcFn g_orig_server_input_released;
extern EndAbilityRpcFn g_orig_server_end_ability;
extern CancelAbilityRpcFn g_orig_server_cancel_ability;
void detour_server_input_pressed(void* asc, int32_t handle);
void detour_server_input_released(void* asc, int32_t handle);
void detour_server_end_ability(void* asc, int32_t handle, const void* activation_info, const void* prediction_key);
void detour_server_cancel_ability(void* asc, int32_t handle, const void* activation_info);
void c41_receive_anim(void* pc, const ChanArgs& args);
void c57_receive_equip(void* pc, const WireString* s); // card 312: text, "<client slot>[ <class path>]"
void c141_receive_switch(void* pc, uint32_t packed);
void c141_step(void* engine); // card 141: host, the underslung result check
void c42_receive_gib(void* pc, const ChanArgs& args);
void c43_receive_down(void* pc, const ChanArgs& args);
void c46_receive_warp(void* pc, const ChanArgs& args);
void c49_receive(void* pc, const WireString* s, const ChanArgs& args);
void c70_receive(const WireString* s);
void c70_map_end(); // b13: the client's pending names frame does not outlive its map
void c50_receive(void* pc, const WireString* s);
void c50_peer_receive(void* pc, const WireString* s); // card 050 b205: another player's weapon list
void c50_receive_use_name(void* pc, const WireString* s);
void c65_receive_activate(void* pc, const WireString* s);
void c71_receive(void* pc, const WireString* s);
void c73_receive(void* pc, const WireString* s);
void c74_receive(void* pc, const WireString* s);
void c74_receive_explode(void* pc, const WireString* s);
// Card 157: client hit claims (claim_c157.cpp).
void c157_launch(void* proj, const void* shooter, const void* init); // both sides, an own launch (c132_proj_launch)
const char* c281_discard_arm(uint32_t n, uint32_t seq); // card 281 (b187), test only, host: `discard <n>`
void c157_forget(const void* proj, const char* why); // both sides, back in the pool or not a player round
bool c157_on_hit(void* self, void* hit_comp, void* other, void* other_comp, const Vec3* impulse, const void* hit);
void c157_receive_claim(void* pc, const WireString* s); // host
void c157_step(void* engine); // per tick: host claims; client own vitals line
void c157_map_end(); // both sides, map load
extern ExecThunkFn g_orig_c157_overlap;
void detour_c157_overlap(void* context, void* frame, void* result); // both sides, a round's overlap (run 4)
// `self` of ASTCharacter::OnDamageCaused / OnDeathCaused is its IADDamageCauserInterface subobject, not the pawn:
// the pawn is self - kCauserIface (card 166; card 157's diagnostic read it as a UObject and crashed in c163-r1m).
constexpr size_t kCauserIface = 0x658;
using C157DamageCausedFn = void (*)(void* self, const void* event);
extern C157DamageCausedFn g_orig_c157_damage_caused;
void detour_c157_damage_caused(void* self, const void* event); // diagnostic: logs the event's head on both sides
// Card 166 (marker_c166.cpp): a server copy's OnDamageCaused / OnDeathCaused relayed as ChanOp::Marker to its client.
// `self` of both is the IADDamageCauserInterface subobject (character + 0x658).
extern C157DamageCausedFn g_orig_c166_death_caused;
void detour_c166_death_caused(void* self, const void* event);
void c166_host_damage_caused(void* self, const void* event); // host, from detour_c157_damage_caused
void c166_receive(void* pc, const WireString* s);             // client: ChanOp::Marker
void c455_receive(void* pc, const WireString* s);             // client: ChanOp::HurtSound (player_c99.cpp)
void c459_receive(void* pc, const WireString* s);             // client: ChanOp::MechFuel (attributes.cpp)
void c475_receive(void* pc, const WireString* s);             // client: ChanOp::Score (score_c475.cpp)
void c475_join_replay(void* pc);                              // host: score rows to one PC (score_c475.cpp)
void c491_pawn_rows(const void* sub, const char* what); // card 491 diag: each bound pawn's resupply state (UBWCOOP_C99_LOG)
// Card 491 b260 (score_c475.cpp): the host pays every ResupplyUpgraded; clients drop their own and apply "U" rows.
void c491_resupply_arm(const void* sub);                  // both: ResupplyLevel's offset, once per map
bool c491_resupply_drop(const void* sub, uint64_t tag);   // client: true for a native ResupplyUpgraded (dropped)
void c491_resupply_paid(const void* sub, uint64_t tag);   // host: after a ResupplyUpgraded, "U" to every client
extern bool g_c491_inner_text; // AddFedScore(tag)'s own inner AddFedScoreText is next: the detour passes it (b262)
void c491_resupply_join_replay(void* pc);                 // host: each bound pawn's level to one PC
bool c491_pe(void* object, const void* function);         // client, ProcessEvent: true refuses a pawn's FedScoreGained (b262)
void c491_level_watch(const void* sub);                   // client, every tick: a ResupplyLevel change no U row made (b262)
void c475_map_end(void* old_world);                           // both: drops a replay cut by the map change
int32_t c475_digest_total(const void* world);                 // the digest's Score: value, -1 when none
void c475_step(void* engine);                                 // card 323: host total per change, client apply
void c323_host_send(const void* world); // card 323/529, host: a changed total as "T" (also ahead of each digest B)
void c459_after_reinit(const void* pawn);                    // client: card 020 re-init of the mech set (attributes.cpp)
void c166_arm_map();                                          // per map, from c98_arm_map
// Card 168 (marker_c166.cpp): UUserWidget::PlayAnimation. The handle struct returns through `ret` (RDX).
using C168PlayAnimationFn = void* (*)(void* self, void* ret, void* animation, float start, int32_t loops, uint8_t mode,
                                      float speed, bool restore);
extern C168PlayAnimationFn g_orig_c168_play_animation;
void* detour_c168_play_animation(void* self, void* ret, void* animation, float start, int32_t loops, uint8_t mode,
                                 float speed, bool restore); // both sides: logs WB_Hit_Marker's marker animations
Vec3 c74_actor_loc(const void* actor); // grenades.cpp
void c72_receive_set(void* pc, const WireString* s);
void c72_receive_mirror(void* pc, const WireString* s);
void c75_receive_enter(void* pc, const WireString* s);
void c75_receive_leave(void* pc, const WireString* s);
void c123_receive_mount(void* pc, const WireString* s);
void c123_receive_leave(void* pc, const WireString* s);
void c49_logout(const void* pc);
bool c50_first_pong(void* pc); // host: true at a copy's first Pong this map (marks it ponged), then sync_join_replay
// Card 208 (weapons.cpp): the bug form switch relay, host to client (ChanOp::BugForm).
void c208_step(void* world, void* pc, ULONGLONG now); // both sides, from c50_step
void c208_join_replay(void* pc);                       // host, from sync_join_replay
void c208_receive(void* pc, const ChanArgs& args);     // client: ChanOp::BugForm
void c208_apply(void* pc, const void* pawn, uint64_t guid, int32_t index); // client: one resolved BugForm row
void c46_warp_apply(void* pc, void* target, uint64_t guid, uint32_t warp);                // client: one resolved Warp row
void c208_map_end();                                   // both sides, at LoadMap enter (pl_map_end)
void c50_logout(const void* pc);
void c49_snapshot(void* subsystem);
constexpr size_t kWeaponAsc = 0x2C0; // AADWeapon::GetWeaponASC
constexpr size_t kWeaponOwnerInventory = 0x2D8; // AADWeapon: the owning inventory (InitialiseWeaponItem sets it for
                                                // MI guns and bug forms alike; vanilla reads it here)
constexpr int32_t kC24Relays = 4;
constexpr uint32_t kC26PingSlots = 8; // send times kept for the last pings, by counter
constexpr uint32_t kC42WalkPing = 0x7F01; // card 042 test pings (host to client)
constexpr uint32_t kC42DonePing = 0x7F02;
void c42_ping(void* pc, uint32_t arg);
constexpr uint32_t kC26ShotPing = 3;  // UBWCOOP_C28_SHOTS: the client shoots its window when this host ping arrives
using LogoutFn = void (*)(void* game_mode, void* exiting);
using GetWeaponBySlotFn = void* (*)(const void* inventory, uint8_t slot);
extern LogoutFn g_orig_logout;
extern AscInputTagFn g_orig_asc_tag_released;
extern GetWeaponBySlotFn g_get_weapon_by_slot;
// Card 076: what a Fire frame presses. Its argument is (kind << 1) | pressed, so Fire 0 and Fire 1 keep their meaning.
constexpr int32_t kFirePrimary = 0;   // InputTag.Fire: soldier and mech primary
constexpr int32_t kFireMechAlt = 1;   // InputTag.MechSecondaryAbility: mech only
constexpr int32_t kFireAds = 2;       // card 160: InputTag.ADS, a client's soldier weapon (IronSights)
constexpr int32_t kFireReload = 3;    // card 274: InputTag.Reload, a soldier weapon's reload (both sides)
constexpr int32_t kFireKinds = 4;
constexpr uint32_t kFireOnBit = 1u;
constexpr uint32_t kFireKindMask = 0x7Fu; // (arg >> 1) & mask is the kind; bits 8+ carry the card 160 ADS settings
// Card 160: an ADS edge also carries the client's own IronSights inputs (the server copy reads them, not the host's).
constexpr uint32_t kFireAdsToggleKbm = 1u << 8;  // USTGameUserSettings::IsToggleADSKBMEnabled
constexpr uint32_t kFireAdsToggleCtrl = 1u << 9; // USTGameUserSettings::IsToggleADSControllerEnabled
constexpr uint32_t kFireAdsGamepad = 1u << 10;   // UADInputDetectionSubsystem::CurrentInputGamepad
constexpr uint32_t kFireAdsKnown = 1u << 11;     // the client read its settings (else the host keeps its own values)
// Card 175: a client's primary Fire 0 carries its own rounds for that press (client-own FireProjectile calls), in
// bits 8-14: a primary edge has no ADS bits there, and a fixed channel argument is sent modulo ue::chan::kChanArgMax
// (0x7FFF), so higher bits would wrap (c175-r1: "client sent Fire 188416", the host received 24581, an ADS press).
constexpr uint32_t kFireRoundsShift = 8;
constexpr uint32_t kFireRoundsMax = 0x7Fu; // bits 8-14; more rounds are sent as the maximum
static_assert(((kFireRoundsMax << kFireRoundsShift) | (static_cast<uint32_t>(kFireKinds - 1) << 1) | kFireOnBit) <
              ue::chan::kChanArgMax);
static_assert((kFireAdsKnown | kFireAdsGamepad | kFireAdsToggleCtrl | kFireAdsToggleKbm |
               (static_cast<uint32_t>(kFireKinds - 1) << 1) | kFireOnBit) < ue::chan::kChanArgMax);
struct C24Relay {
    int32_t pawn_id;      // UObject InternalIndex of the server-copy pawn
    int32_t weapon_id;    // InternalIndex of the weapon that was pressed
    int32_t kind;         // kFirePrimary, kFireMechAlt, kFireAds or kFireReload
    uint32_t fired;       // card 175: the copy's rounds since this press (kFirePrimary)
    uint32_t wanted;      // card 175: the client's rounds for this press, from its Fire 0
    ULONGLONG release_ms; // card 175: 0 = held by the press; else the release waits for `wanted` until this time
    ULONGLONG press_ms;   // card 276: when the relay pressed (the re-press tries count from here)
    uint32_t represses;   // card 276: re-presses of this press so far (at most kC276Represses)
    int32_t press_mag;    // card 276 diag: the copy's primary magazine at the press (-1: not read)
    ULONGLONG repress_ms; // card 181 run a: the last re-press try or reload skip (0: none yet); the next try waits
                          // kC276RepressMs from the later of this and press_ms
    uint32_t carried;     // card 437 part D: the client's rounds of this key press fired before a weapon change,
                          // on the presses it ended; the client's release counts them, the copy's `fired` does not
};
struct C24Relays {
    C24Relay item[kC24Relays]; // only held relays are present
    int32_t count;
};
struct C26Own {
    int32_t pawn_id; // the own pawn whose fire is held; only meaningful while held
    bool held;
    bool refused; // card 352: a press refused under WeaponSystem.Blocked, still held (one row per refusal)
};
struct C26Ping {
    uint32_t last_n; // the last ping counter sent
    ULONGLONG next_ms;
    ULONGLONG sent_ms[kC26PingSlots];
    uint32_t remotes; // host: remote PCs at the last step (card 154: a new one is pinged at once)
};
struct C24State {
    uint64_t fire_tag; // FName values (not game pointers); 0 = not built yet
    uint64_t mech_alt_tag; // card 076
    uint64_t ads_tag;      // card 160
    uint64_t reload_tag;   // card 274
    uint64_t blocked_tag;  // card 352: WeaponSystem.Blocked
    uint32_t c352_lines;   // card 352: refused press rows
    C24Relays relays;
    C26Own own[kFireKinds]; // by fire kind
    C26Ping ping;
    bool host_fire; // client: the host's last Fire frame was 1 (drives the host sprite's Attack01)
    uint32_t relay_lines;
    uint32_t edge_lines;
    uint32_t ping_lines;
    uint32_t relayed_presses;
    uint32_t press_rounds; // card 175, client: client-own FireProjectile count at its last primary press
    uint32_t hold_lines;   // card 175, host: hold lines
    // Card 276, host: each pawn's last primary release on its copy (pawn InternalIndex, time), for the press line.
    int32_t c276_release_pawn[kC24Relays];
    ULONGLONG c276_release_ms[kC24Relays];
    int32_t c276_release_next;
    uint32_t c276_press_lines, c276_repress_lines;
    uint32_t c296_hold_lines; // card 296, host: re-presses held off during a spool
    uint32_t c276_end_lines; // card 276 diag: host press end rows
    // Card 276 diag: client reload sends, host reload presses received, and their rows.
    uint32_t c276_reload_sends, c276_reload_relays, c276_reload_lines;
    // Card 178, client: a primary release waiting for the end of its frame (c178_defer_release).
    bool release_due;
    bool release_flushing;     // c178_flush_release is sending it: not deferred again
    int32_t release_pc_id;     // InternalIndex of the own PC it is for
    uint32_t release_fired_at; // client-own FireProjectile count when AbilityInputTagReleased ran
    const char* release_why;   // static text
    uint32_t release_lines;
    // Card 180, client: the own connection flushed after a release sent from c24_step (c180_flush_net).
    uint32_t flushes;
    uint32_t flush_lines;
    uint32_t flush_skip_lines;
    bool flush_slot_logged;
};
extern C24State g_c24;
uint32_t c24_fire_arg(int32_t kind, bool on);
void c26_edge_line(const char* fmt_what, uint32_t value, const char* why);
const void* c26_own_fire_pawn(const void* asc, const void* tag, int32_t* kind);
void c196_note_bug_press(const void* asc, const void* tag); // card 196: first form-ASC presses, capped
void c26_send_fire(const void* world_context, void* own_pc, int32_t kind, bool on, const char* why);
// Card 276, client (pl_activation): the own soldier weapon's reload ability activated: Fire kind 3 to the host.
void c276_end_line(const C24Relay& r, const void* pawn, const char* why); // card 276 diag (fire.cpp)
void c276_reload_activation(const void* asc, const void* ability, bool ok, bool server_request);
// Card 296 diagnostic, client (fire.cpp): the own weapon's spool-up end and its fire activation.
int32_t c276_spec_active(const void* asc, const char* part);
void c296_input(bool pressed);
void c296_activation(const void* asc, const void* ability, bool ok, bool server_request);
void c437_respool_activation(const void* asc, const void* ability, bool ok, bool server_request); // card 437 part C
uint32_t c175_press_rounds(bool on);
void c296_tick();
void c104_side_counts(uint32_t side, uint32_t* actions, uint32_t* ends); // anim_c104.cpp: task callbacks per side
bool c178_defer_release(void* own_pc, int32_t kind, bool on, const char* why);
void detour_asc_tag_released(void* asc, const void* tag);
// Card 196: the offset of a player pawn's weapon-class inventory: kMiWeaponInventory on an ASTPlayerCharacterMI,
// kBugFormsInventory on an ASTPlayerCharacterBug (its bug forms are AADWeapon actors), 0 on any other actor.
size_t c196_inventory_at(const void* pawn);
const void* c24_inventory(const void* pawn);
void* c24_weapon_asc(const void* pawn, const void** weapon_out);
void* c73_spec_instance(const uint8_t* spec); // grenades.cpp: a spec's primary ability instance, or null
int32_t c352_blocked(const void* pawn, int32_t kind); // card 352
const char* c24_kind_text(int32_t kind, bool press);
// Card 196: the pawn an actor belongs to. A weapon whose owning inventory (+0x2D8) has a player character as its
// component owner gives that pawn (a bug form has no AActor::Owner); any other actor gives its AActor::Owner.
const void* weapon_pawn(const void* actor);
int32_t c24_find_relay(int32_t pawn_id, int32_t kind);
void c24_release(int32_t i, const void* pawn, const char* why);
void c24_release_pawn(const void* pawn, const char* why);
void c24_relay(const void* pawn, int32_t kind, bool fire, uint32_t rounds);
void c175_copy_round(const void* pawn); // host: a server-copy FireProjectile, counted on its held primary relay
void c26_pong(const void* from_pc, uint32_t n);
bool c26_is_remote_pc(const void* pc); // chan_ops.cpp: a PC this host sends to (a connected client's)
void detour_logout(void* game_mode, void* exiting);
constexpr int32_t kMechContextPriority = 3;
constexpr int32_t kC76Contexts = 4;
struct C76Imc {
    int32_t mech_id; // InternalIndex of the mech the contexts are for; -1 = none
    const void* context[kC76Contexts];
    int32_t count;
};
extern C76Imc g_c76_imc;
void c76_imc_add(void* iface, const void* mech);
void c76_imc_remove(void* iface);
void c76_client_imc(void* world);
using GetPlayerViewPointFn = void (*)(const void* pc, Vec3* loc, Vec3* rot);
using GetTargetLocationFn = Vec3* (*)(const void* iface, Vec3* out);
using DeprojectFn = bool (*)(const void* pc, float x, float y, Vec3* loc, Vec3* dir);
extern GetPlayerViewPointFn g_player_view_point;
extern GetTargetLocationFn g_orig_target_location;
extern DeprojectFn g_orig_deproject;
constexpr size_t kParamsSize = 0x80;        // FCollisionQueryParams as the original's frame holds it
constexpr size_t kParamsHeapA = 0x48;       // TArray heap pointers the original frees after the trace
constexpr size_t kParamsHeapB = 0x68;
struct C24Aim {
    const void* pawn;     // the zero-adjust owner while inside detour_zero_adjust, else null (game thread only)
    uint64_t profile;     // FName "PlayerProjectile", cached
    uint32_t target_lines;
    uint32_t deproject_lines;
    uint32_t fallback_lines;
};
extern C24Aim g_c24_aim;
bool c24_view_point(const void* pawn, ViewPoint& out);
// actor_out (card 264): the owner of the blocking hit's component (FHitResult Component), null with no block, no
// live component or no owner; comp_out: that component (null with no block or a freed one).
Vec3 trace_profile(const void* pawn, const Vec3& start, const Vec3& end, uint64_t profile,
                   const void** actor_out = nullptr, const void** comp_out = nullptr);
// Card 264: the same trace on a collision channel (kEccVisibility: what the player sees), the channel's default
// responses (FCollisionResponseParams::DefaultResponseParam); same outputs as trace_profile. inside_out (card 328): the
// blocking hit started inside the body (FHitResult bStartPenetrating).
Vec3 trace_channel(const void* pawn, const Vec3& start, const Vec3& end, int32_t channel,
                   const void** actor_out = nullptr, const void** comp_out = nullptr, bool* inside_out = nullptr);
// FHitResult offsets, read by reflection once (claim_c157.cpp).
struct HitLayout {
    size_t bone, my_bone, comp, actor, item, my_item;
    bool read;
};
const HitLayout& hit_layout();
Vec3 c24_trace(const void* pawn, const Vec3& start, const Vec3& end);
Vec3* detour_target_location(const void* iface, Vec3* out);
bool detour_deproject(const void* pc, float x, float y, Vec3* loc, Vec3* dir);
// ---- Card 025: NPC list and a test kill of the nearest NPC trooper (UBWCOOP_KILL_TROOPER=1) ----
// Host/standalone only, once the local character is on foot: every 5 s for 60 s, each ASTNonPlayableCharacterBase
// in the world's levels (UWorld::Levels +0x1C8, from GetLevel 0x04340130; ULevel::Actors +0xA0, from SortActorList
// 0x03DC9A50). After the first tick (step 6), the pawn teleports to 400 units from the nearest NPC whose class name
// holds "Trooper" and faces it; 1 s later one shot of the live target, then it dies through
// USTCombatUtilityLibrary::InstaKillTarget(bIsOverkill = false), so the death and corpse play instead of a gib.
// A raw Health write would not raise death: UADHealthComponent::RefreshState only sets IsDead (+0x148).
// After the last kill shot the target revives: its Health base goes 0 -> 50 through
// UAbilitySystemComponent::SetNumericAttributeBase, whose value-change delegate reaches
// ASTNonPlayableCharacterBase::HandleHealthValueChangedEvent (0x05193030; old <= 0 and new > 0 = revive: removes
// Tag_Corpse, SetCurrentBaseState(2), StopStateMachine_Dying, StartStateMachine_Core, then BP_OnRevived).
using InstaKillFn = void (*)(void* target, bool overkill);
using NpcDeathEventFn = void (*)(void* npc, const void* damage_event);
using NpcStartDyingFn = void (*)(void* npc);
extern StaticClassFn g_npc_class;
extern InstaKillFn g_insta_kill;
// Card 131: AActor::GetActorBounds (the test runner's `look actor <name> bounds`): world-space box centre and extent.
using ActorBoundsFn = void (*)(const void* actor, bool only_colliding, Vec3* origin, Vec3* extent, bool child_actors);
extern ActorBoundsFn g_actor_bounds;
extern NpcDeathEventFn g_orig_npc_death_event;
extern NpcStartDyingFn g_orig_npc_start_dying;
using GameplayAttributeCtorFn = GameplayAttributeRaw* (*)(GameplayAttributeRaw* self, void* property);
using NpcHealthChangedFn = void (*)(void* npc, float a, float b);
using NpcRevivedFn = void (*)(void* npc);
extern GameplayAttributeCtorFn g_gameplay_attribute_ctor;
extern GameplayAttributeRaw g_c25_health_attr;
extern bool g_c25_health_attr_built;
extern NpcHealthChangedFn g_orig_npc_health_changed;
extern NpcRevivedFn g_orig_npc_revived;
constexpr size_t kHealthIsDead = 0x148;  // UADHealthComponent bool, set by RefreshState
constexpr uint32_t kC25MaxNpcs = 256;    // kept per tick; the rest are only counted
constexpr double kC25MoveDistance = 400.0;  // the pawn's distance to the target after the move
constexpr double kC25MoveAbove = 50.0;      // pawn Z = target Z + this when the pawn would end up below the target
constexpr double kC25FacePitch = -10.0;
struct C25Series {
    const char* tag; // logged file name: card025b-<tag><seconds>.png
    const ULONGLONG* ms;
    uint32_t count;
};
struct C25Npc {
    void* actor;  // valid for this tick only
    Vec3 loc;
    double dist;  // to the player pawn
    double front; // cosine of the angle between the camera forward and the NPC
};
struct C25List {
    C25Npc npc[kC25MaxNpcs];
    uint32_t count;
    uint32_t total;
};
struct C25State {
    ULONGLONG next_ms;
    ULONGLONG pre_at_ms;     // 0 = none pending
    ULONGLONG kill_at_ms;    // 0 = none pending
    ULONGLONG revive_at_ms;  // 0 = none pending
    ULONGLONG series_ms;     // start of the current shot series (the kill or the revive)
    const C25Series* series; // nullptr = no shots pending
    uint64_t target;         // FName of the chosen trooper; 0 = none
    uint32_t shots;          // taken in the current series
    uint32_t ticks;
    uint32_t death_lines;
    uint32_t target_lines;
    uint32_t revived_calls;  // BP_OnRevived on the target
    bool armed;
};
extern bool g_c25_kill;
extern C25State g_c25;
C25Npc c25_measure(void* actor, const Vec3& pawn_loc, const ViewPoint& view);
void c25_collect(const void* world, const Vec3& pawn_loc, const ViewPoint& view, C25List& out);
int c25_dead(const void* actor, float* health);
void c25_face(void* pc, const Vec3& eye, const Vec3& at);
bool c25_move(void* pawn, void* pc, const Vec3& target);
bool c56_disconnect(void* pc);
const GameplayAttributeRaw* c19_attribute(void* field);
const GameplayAttributeRaw* c25_health_attribute();
bool c25_build_attribute(const char* name, GameplayAttributeRaw* out);
void detour_npc_death_event(void* npc, const void* damage_event);
void detour_npc_start_dying(void* npc);
void detour_npc_health_changed(void* npc, float a, float b);
void detour_npc_revived(void* npc);
extern StaticClassFn g_sprite_data_class;
extern StaticClassFn g_npc_data_class;
extern bool g_c27_dump;
constexpr size_t kMapPropKey = 0x70;
constexpr size_t kMapHashNext = 0x84;
constexpr size_t kMapHashIndex = 0x88;
constexpr size_t kMapSize = 0x8C;
constexpr size_t kObjectPropClass = 0x70;
// FScriptSet / FScriptMap: TSparseArray Data (ptr +0x0, Num +0x8), AllocationFlags TBitArray (inline uint32[4]
// +0x10, secondary pointer +0x20 when not null, NumBits +0x28), NumFreeIndices +0x34.
constexpr size_t kSparseBitsInline = 0x10;
constexpr size_t kSparseBitsSecondary = 0x20;
constexpr size_t kSparseNumBits = 0x28;
constexpr int32_t kSparseInlineBits = 128;
constexpr int32_t kC27MaxSparse = 65536;
constexpr uint32_t kC27MaxLines = 2000;   // per output file
constexpr int32_t kC27MaxElements = 1024; // per container
constexpr size_t kC27TextChars = 320;
struct C27State {
    bool armed;
    ULONGLONG due_ms; // 0 until the pawn is first on foot
};
extern C27State g_c27;
struct C27Out {
    HANDLE file;
    uint32_t lines;
    uint32_t write_fails;
    bool capped;
};
struct C27Sparse {
    const uint8_t* data;
    const uint32_t* bits;
    int32_t slots; // Data.Num, live and free
    int32_t live;
};
bool c27_owned(const void* child, const void* parent);
bool c27_struct_ok(const uint8_t* field, const void* s);
bool c27_map_ok(const uint8_t* field);
bool c27_bit(const C27Sparse& s, int32_t i);
bool c27_sparse(const uint8_t* addr, C27Sparse& s);
void c27_emit(C27Out& out, const char* text, size_t len);
void c27_write(C27Out& out, const char* fmt, ...);
long long c27_sint(const uint8_t* addr, int32_t size);
unsigned long long c27_uint(const uint8_t* addr, int32_t size);
void c27_str(const uint8_t* addr, char* buf, size_t cap);
void c27_hex(const char* kind, const uint8_t* addr, int32_t size, char* buf, size_t cap);
void c27_text(PropKind kind, const char* kind_name, const uint8_t* field, const uint8_t* addr, char* buf, size_t cap);
bool c27_is_sprite_data(const void* object);
bool c27_open(const char* prefix, const char* tag, C27Out& out);
bool c27_verify(const void* asset);
// ---- Card 029: weapon, mech and turret audit (log only) ----
// Sprite sets ride UBWCOOP_DUMP_SPRITE_ANIMS (card 027's switch): after card 027's dump, every BP_CharacterAnimations_*
// set (found loaded, else loaded with StaticLoadObject) gets one line per Animations key in card029-sets.txt.
// The rest is UBWCOOP_AUDIT_029, on both sides: an equipped-weapon poll of every player pawn in the levels, a
// deduplicated log of every ability activation with the owner pawn's side, a vehicle scan (mechs and mounted
// turrets: roles, bReplicates, controller, location and yaw, logged when they change), log-only detours on
// EnterMech, LeaveMech, MountTurret and LeaveTurret, and a test driver on the machine's own pawn: equip each weapon
// row in turn and press then release each non-axis input tag of its weapon ASC (through the original, so card
// 024's relay sends nothing), then enter the nearest mech and mount the nearest turret, turn it, and leave.
using C29EquipFn = void (*)(void* pawn, int32_t index);
using C29SelfFn = void (*)(void* self);
extern C29SelfFn g_c494_show_weapon;             // card 494: ASTWeapon::ShowWeapon
extern StaticClassFn g_c494_st_weapon_class;     // card 494: ASTWeapon::StaticClass
using C29InteractFn = void (*)(void* self, void* interactor);
using C29LookFn = void (*)(void* self, float value);
using C29EnterFn = void (*)(void* vehicle, void* pawn);
extern StaticClassFn g_mech_class;
extern StaticClassFn g_turret_class;
extern C29EquipFn g_equip_weapon;
using C29RemoveFn = void (*)(void* inventory, void* weapon);
extern C29RemoveFn g_remove_weapon;
extern C29SelfFn g_mech_request_leave;
extern C29InteractFn g_mech_on_interaction;
extern C29InteractFn g_turret_on_interaction;
extern C29LookFn g_turret_look_h;
extern C29LookFn g_turret_look_v; // card 125: the gun's pitch after the camera
extern C29EnterFn g_orig_enter_mech;
extern C29SelfFn g_orig_leave_mech;
extern C29EnterFn g_orig_mount_turret;
extern C29SelfFn g_orig_leave_turret;
extern bool g_c29_audit;
constexpr uint32_t kC29LoadFlags = 0x2010; // LOAD_NoWarn | LOAD_Quiet
struct C29Set {
    const char* dir; // under /Game/
    const char* name;
};
constexpr C29Set kC29Sets[] = {
    {"Blueprints/Enemies/Humans/MoritaTrooper", "BP_CharacterAnimations_MoritaTrooper"},
    {"AnimationData/NPCs", "BP_CharacterAnimations_FemaleMoritaTrooper"},
    {"Blueprints/Enemies/Humans/MoritaTrooper", "BP_CharacterAnimations_ControllerTrooper"},
    {"AnimationData/NPCs", "BP_CharacterAnimations_MK2Trooper"},
    {"Blueprints/Enemies/Humans/ShotgunTrooper", "BP_CharacterAnimations_Shotgun_Trooper"},
    {"AnimationData/NPCs", "BP_CharacterAnimations_Epulse88Trooper"},
    {"AnimationData/NPCs", "BP_CharacterAnimations_LaserTrooperBlue"},
    {"AnimationData/NPCs", "BP_CharacterAnimations_LaserTrooperRed"},
    {"AnimationData/NPCs", "BP_CharacterAnimations_FlamerFieldScientist"},
    {"AnimationData/NPCs", "BP_CharacterAnimations_MedicTrooper"},
    {"AnimationData/NPCs", "BP_CharacterAnimations_RadioTrooper"},
    {"AnimationData/NPCs", "BP_CharacterAnimations_SquadLeader"},
    {"AnimationData/NPCs", "BP_CharacterAnimations_NonCombatTrooper"},
    {"AnimationData/NPCs", "BP_CharacterAnimations_NonCombatTrooperFemale"},
    {"Blueprints/Enemies/Humans/TurretTrooper", "BP_CharacterAnimations_TurretTrooper"},
    {"Blueprints/Enemies/Humans/M7Mech", "BP_CharacterAnimations_M7Razorback"},
    {"AnimationData/NPCs", "BP_CharacterAnimations_2DTargetWarriorBug"},
};
// The reflected layout of USpriteAnimationData.Animations down to a frame's TimeToDisplay and Materials.
struct C29Layout {
    int32_t anims;   // Animations map offset in the asset
    int32_t value;   // sequence offset in a map pair
    int32_t stride;  // map pair stride
    int32_t frames;  // Frames array offset in the sequence
    int32_t frame_size;
    int32_t time;      // TimeToDisplay offset in a frame
    int32_t materials; // Materials array offset in a frame
    int32_t uniform;   // UniformFrameTime offset in the sequence
    const uint8_t* uniform_flag; // bUseUniformFrameTime
};
bool c29_flag(const uint8_t* base, const uint8_t* prop);
const void* c29_load_set(const C29Set& set, const char** how);
void c29_dump_set(C27Out& out, const C29Set& set, C29Layout& L, bool& have_layout);
constexpr uint32_t kC29Pawns = 8;
constexpr uint32_t kC29Vehicles = 32;
constexpr uint32_t kC29Acts = 96;
constexpr uint32_t kC29ActLines = 200;
struct C29Seen {
    int32_t pawn;   // object index
    int32_t weapon; // object index of the equipped weapon, -2 before the first poll
};
struct C29Vehicle {
    int32_t index;
    Vec3 loc;
    double yaw;
    double gun_yaw;
    int32_t controller; // object index
    int used;
};
struct C29Act {
    int32_t ability_class; // object index
    uint32_t pawn_side;
    bool server;
    bool ok;
    uint32_t count;
};
enum class C29Stage : uint8_t { Wait, Weapons, Mech, MechOut, Turret, TurretOut, Done };
struct C29State {
    bool armed;
    ULONGLONG on_foot_ms;
    ULONGLONG next_poll_ms;
    ULONGLONG next_tick_ms;
    ULONGLONG stage_ms;
    C29Stage stage;
    int32_t row;
    int32_t key;          // next weapon input tag; even keys press, odd release
    uint64_t pressed;     // FName of the held tag, 0 when none
    int32_t vehicle;      // object index of the vehicle the driver used
    const void* weapon;   // client: the equipped weapon whose tags were last pressed
    C29Seen seen[kC29Pawns];
    uint32_t seen_count;
    C29Vehicle vehicles[kC29Vehicles];
    uint32_t vehicle_count;
    C29Act acts[kC29Acts];
    uint32_t act_count;
    uint32_t equip_lines, act_lines, vehicle_lines, driver_lines, event_lines;
};
extern C29State g_c29;
int c29_bool(const void* object, const char* const* names);
constexpr const char* kC29Hidden[] = {"bHidden", nullptr};
const void* c29_controller(const void* vehicle);
double c29_gun_yaw(const void* vehicle);
void detour_mount_turret(void* turret, void* pawn);
void detour_leave_turret(void* turret);
extern C29SelfFn g_orig_c447_pawn_leaving; // card 447: APlayerController::PawnLeavingGame
void detour_c447_pawn_leaving(void* pc);
void* c29_server_copy(const void* own);
void* c29_server_copy_nth(const void* own, int n);
// Card 502 finding 2 (b263), host, test runs: the server copy whose client sent `role` (1 "client", 2 "client2"),
// so a rejoined client keeps its role; PC order (c29_server_copy_nth) with a WARN when no PC has the role stored.
void* c29_server_copy_role(const void* own, uint32_t role);
void c502_receive_role(void* pc, uint32_t role); // host: ChanOp::Role
constexpr int kC22MaxOwnFields = 64;    // ThirdPersonRenderer's own class properties
constexpr size_t kC22LineChars = 1024;
constexpr const char* kC22CompProps[] = {"SkeletalMeshAsset", "SkinnedAsset", "SkeletalMesh", "AnimClass",
                                         "AnimScriptInstance", "bVisible", "bHiddenInGame", "bOwnerNoSee",
                                         "bOnlyOwnerSee", "AttachParent"};
void c22_prop_text(const void* object, const char* name, char* buf, size_t cap);
void c22_comp_text(const void* component, char* out, size_t cap);
bool c22_is_base_class(const void* cls);
void c22_log_map(const char* tag, const uint8_t* field, const uint8_t* addr);
void c22_own_props(const char* tag, const void* object, int max_supers);
// ---- Card 028: remote player sprite body ----
// On a networked map every ASTPlayerCharacter the local machine does not control (the client's pawn on the host, a
// simulated proxy on the client) gets an NPC-style body: the sprite renderer and variant animator classes of the first
// BP_AI_Trooper_Morita_C in the levels, added locally with AddComponentByClass (never replicated). The renderer's
// own numeric and asset settings and its StaticMesh are copied from the Morita's renderer; the animator shows
// BP_CharacterAnimations_FemaleMoritaTrooper. The animator binds itself to the owner's first USTSpriteRenderComponent
// in its BeginPlay, so the renderer is added and finished first. Every kC28StepMs one animation key is picked from the
// pawn's replicated state and started when it changes. Pawns are keyed by object index; the components are found
// again each step with FindComponentByClass, so no game pointer is kept between steps.
using StartSequenceFn = void (*)(void* animator, uint64_t key, float rate);
extern StartSequenceFn g_start_sequence;
extern StaticClassFn g_sprite_render_class;
extern StaticClassFn g_variant_animator_class;
constexpr uint32_t kC28MaxPawns = 8;
constexpr ULONGLONG kC28DeathMs = 1100; // Death01 is 1.06 s (card 027), then Corpse01
constexpr uint8_t kC28NoCollision = 0;                // ECollisionEnabled::NoCollision
enum class C28Key : uint8_t { None, Idle, Walk, Run, Flinch, Attack, Death, Corpse, StandUp, Count };
struct C28Slot {
    int32_t index;      // UObject index of the pawn
    C28Key key;         // last key started; None until the first start
    float health;       // last seen; < 0 unknown
    ULONGLONG hurt_ms;  // last health drop, 0 none
    ULONGLONG dead_ms;  // first step seen dead, 0 alive
    uint32_t shot_keys; // UBWCOOP_C28_SHOTS: bit per key already scheduled for a shot
    ULONGLONG shot_ms;  // pending key shot, 0 none
    Vec3 last_loc;      // client: the pawn's location at the last step (the orbit jump test)
    bool loc_known;
    bool approached;    // host: moved next to the client pawn for the pending key shot
    Vec3 step_loc;      // host: the server copy's location at the last speed sample
    ULONGLONG step_ms;  // host: time of that sample, 0 none
    double speed;       // last speed used for the key (2D, units per second)
    int32_t renderer;   // card 034: UObject index of the renderer card 028 added, -1 none
    C28Key starting;    // card 034: key passed to the StartAnimationSequence call in progress (its size change)
    uint32_t fit_keys;  // card 034: bit per key whose first refit was logged
    uint32_t z_keys;    // card 034: bit per key whose world z was logged
    C28Key z_seen;      // card 034: s.key at the previous step (world z is logged one step after a change)
    float fit_hh;       // card 034: capsule half-height of the last refit, 0 none (crouch changes it)
};
struct C28State {
    C28Slot slot[kC28MaxPawns];
    uint32_t count;
    ULONGLONG next_ms;
    ULONGLONG attach_next_ms;
    uint64_t key_name[static_cast<size_t>(C28Key::Count)]; // FName cache
    uint32_t key_lines;
    uint32_t attach_lines;
    bool template_logged;
    uint32_t shots;        // UBWCOOP_C28_SHOTS: requested this map
    ULONGLONG orbit_ms;    // host: orbit start, 0 not scheduled
    uint32_t orbit_cue;    // host: next kC28Orbit row
    double orbit_yaw;      // host: the client pawn's yaw at the first orbit move
    ULONGLONG series_ms;   // client: when the host's orbit jump was seen, 0 not seen
    uint32_t series_shot;  // client: next kC28SeriesMs row
    int32_t npc_renderer;  // card 034: UObject index of the template Morita's renderer, -1 not seen
    bool offsets_ok;       // card 034: reflected RelativeLocation / RelativeScale3D match the disassembly offsets
    uint32_t fit_lines;
    uint32_t warn_lines;
    uint32_t npc_lines;
    uint32_t z_lines;
};
extern C28State g_c28;
extern bool g_c28_shots;
extern bool g_c92_log; // UBWCOOP_C92_LOG (card 092)
// ---- Card 094: the client's jump and mantle on the server copy (log only, jump_c94.cpp) ----
// FGameplayAbilityActivationInfo is larger than 8 bytes, so it arrives by pointer; FGameplayTag (an FName) by value.
using EndAbilityFn = void (*)(void* ability, int32_t handle, const void* actor_info, const void* activation,
                              bool replicate, bool cancelled);
using CommitAbilityFn = bool (*)(void* ability, int32_t handle, const void* actor_info, const void* activation,
                                 void* failure_tags);
using MantleSearchFn = void* (*)(void* ability, float a, float b, float c, float d, float e, float f, float g,
                                 void* actor);
using HandleGameplayEventFn = int32_t (*)(void* asc, uint64_t tag, const void* payload);
using ClientEndAbilityFn = void (*)(void* asc, int32_t handle, const void* activation);
using MonitoredTagChangedFn = void (*)(void* asc, uint64_t tag, int32_t count);
// Card 101: the mantle lerp task; the FVector by value arrives by pointer.
using C101LerpTickFn = void (*)(void* task, float dt, const double* offset);
using C101LerpEndFn = void (*)(void* task, const double* location);
extern bool g_c94_log;       // UBWCOOP_C94_LOG
extern bool g_gate_c94;      // g_c94_log: the card 094 detours
extern bool g_gate_c94_diag; // g_c94_log or UBWCOOP_DIAG: the character jump detours
extern EndAbilityFn g_orig_c94_end_ability;
extern CommitAbilityFn g_orig_c94_commit;
extern MantleSearchFn g_orig_c94_mantle_search;
extern HandleGameplayEventFn g_orig_c94_gameplay_event;
extern ClientEndAbilityFn g_orig_c94_client_end;
extern MonitoredTagChangedFn g_orig_c94_tag_changed;
extern C101LerpTickFn g_orig_c101_lerp_tick;
extern C101LerpEndFn g_orig_c101_lerp_end;
void detour_c101_lerp_tick(void* task, float dt, const double* offset);
void detour_c101_lerp_end(void* task, const double* location);
bool c101_in_mantle(); // host: a server-copy mantle window is open
// Card 101 (host): a server copy's GA_Mantling_Move activation opens its client-authoritative mantle window;
// c101_cmc_tick closes timed-out windows on the CMC that owns them.
void c101_note_try(const void* asc, const void* ability, bool ok, bool server);
void c101_forget(const void* actor); // host: free a destroyed server copy's window (nullptr: all, map change)
void c101_cmc_tick(void* cmc);
bool pressed_jump(const void* character);
void c94_try(const void* asc, int32_t handle, const void* ability, bool ok, bool server, unsigned long long caller);
void c430_reload_try(const void* asc, int32_t handle, bool ok, bool server_request); // card 430 diag (attributes.cpp)
void c430_retry_step(ULONGLONG now); // card 430: client, the blocked reload try made again (attributes.cpp)
void c94_jump_call(const char* what, const void* character, int result);
void detour_c94_end_ability(void* ability, int32_t handle, const void* actor_info, const void* activation,
                            bool replicate, bool cancelled);
bool detour_c94_commit(void* ability, int32_t handle, const void* actor_info, const void* activation,
                       void* failure_tags);
void* detour_c94_mantle_search(void* ability, float a, float b, float c, float d, float e, float f, float g,
                               void* actor);
int32_t detour_c94_gameplay_event(void* asc, uint64_t tag, const void* payload);
void detour_c94_client_end(void* asc, int32_t handle, const void* activation);
void detour_c94_tag_changed(void* asc, uint64_t tag, int32_t count);
// ---- Card 034: keep the remote sprite's feet on the ground ----
// USTSpriteRenderComponent::AnimatorFrameSizeChanged (native, run by the animator's OnFrameSizeChange through its exec
// thunk) only sets the scale from the frame size and PPM, so a quad that grows around its centre moves its bottom
// edge. After the original, a renderer card 028 attached (its object index in the slot table, owner checked) is
// refitted with FitMeshToCollider(2 * capsule half-height), the half-height read at each refit. Any other renderer
// runs the original only; the template Morita's size changes are logged with its relative z before and after.
using FrameSizeChangedFn = void (*)(void* renderer, const double* size); // const FVector2D& (two doubles)
using FitMeshFn = void (*)(void* renderer, float collider_height);
extern FrameSizeChangedFn g_orig_frame_size_changed;
extern FitMeshFn g_fit_mesh;
void detour_frame_size_changed(void* renderer, const double* size);
C28Key c43_sprite_key(const void* pawn, ULONGLONG now);
constexpr const char* kC33Hidden[] = {"bHiddenInGame", nullptr};
struct C33Slot {
    int32_t index;  // UObject index of the pawn
    int32_t arms;   // object index of the last FirstPersonMesh hidden, -1 none
    int32_t weapon; // object index of the last weapon root hidden, -1 none
    int32_t form;   // card 196: key of the last bug form root line (root index, or kC196FormNo*), INT32_MIN none
};
struct C33State {
    C33Slot slot[kC28MaxPawns];
    uint32_t count;
    uint32_t lines;
};
extern C33State g_c33;
// Card 275: the per-pawn slot tables of cards 022, 028 and 033 key a slot by the pawn's object index and never free
// one, so a ninth new pawn on one map (rejoin, death and respawn, relevancy re-creation) found its table full. A new
// pawn that finds its table full now takes the first slot whose pawn is gone (its FWeakObjectPtr no longer resolves),
// reset to the table's initial values. Only when every slot holds a live pawn does the table stay full.
// remote_body.cpp keeps one weak handle per slot, parallel to each table.
enum class C275Table : uint8_t { Sprint, Body, Hide, Count };
void c275_note(C275Table table, uint32_t at, const void* pawn); // slot `at` of `table` now holds `pawn`
int32_t c275_gone_slot(C275Table table, uint32_t count);        // first slot below `count` whose pawn is gone, or -1
void c275_log_reuse(C275Table table, int32_t old_index, const void* pawn);
// The slot of `pawn` in `slots` (first `count` in use, `cap` in all), added as `fresh` when new; a full table reuses
// a gone pawn's slot. Null when the table is full of live pawns.
template <typename Slot>
Slot* c275_slot(C275Table table, Slot* slots, uint32_t& count, uint32_t cap, const void* pawn, const Slot& fresh) {
    const int32_t index = fresh.index;
    for (uint32_t i = 0; i < count; ++i) {
        if (slots[i].index == index) {
            c275_note(table, i, pawn);
            return &slots[i];
        }
    }
    uint32_t at = count;
    if (count < cap) {
        ++count;
    } else {
        const int32_t gone = c275_gone_slot(table, count);
        if (gone < 0) {
            return nullptr;
        }
        at = static_cast<uint32_t>(gone);
        c275_log_reuse(table, slots[at].index, pawn);
    }
    slots[at] = fresh;
    c275_note(table, at, pawn);
    return &slots[at];
}
extern StaticClassFn g_enemy_class;
extern bool g_c23_audit;
extern bool g_c23_spawn;
constexpr size_t kActorRepFlags = 0x5B;     // bit 3 = AActor::bReplicates (AActor::SetReplicates)
constexpr size_t kActorHiddenByte = 0x58;   // bit 7 bHidden (AActor::SetActorHiddenInGame 0x037F2440)
constexpr uint8_t kActorHiddenBit = 0x80;
constexpr uint8_t kRepBit = 0x08;
constexpr uint32_t kC23Max = 256;      // enemies kept per scan; the rest are only counted
constexpr uint32_t kC23Known = 128;    // enemies tracked for appear/gone/health events
constexpr uint32_t kC23SpawnTries = 3;
struct C23Seen {
    const void* actor; // identity only
    uint64_t name;     // FName, with the pointer the identity key
    float set_health;
    float cur_health;
};
struct C23Enemy {
    void* actor; // valid for this scan only
    Vec3 loc;
    double dist; // to this side's pawn; -1 without one
};
struct C23List {
    C23Enemy e[kC23Max];
    uint32_t count;
    uint32_t total;
};
struct C23State {
    C23Seen seen[kC23Known];
    uint32_t seen_count;
    ULONGLONG next_scan_ms;
    ULONGLONG next_tick_ms;
    ULONGLONG next_shot_ms;
    ULONGLONG landed_ms;   // host: first scan with the pawn on foot; 0 = not yet
    ULONGLONG spawn_ms;    // 0 = no spawned bug
    ULONGLONG relay_ms;    // when the relayed presses reached kC23KillPresses; 0 = not yet
    ULONGLONG kill_ms;     // 0 = no kill yet
    uint64_t spawned_name; // FName of the spawned bug
    Vec3 kill_loc;
    uint32_t ticks;
    uint32_t event_lines;
    uint32_t shots;
    uint32_t kill_shots;
    uint32_t spawn_tries;
    bool armed;
    bool untracked_logged;
};
extern C23State g_c23;
void c23_collect(const void* world, const void* pawn, const void* cls, C23List& out);
float c23_set_health(const void* actor); // the enemy's health attribute set CurrentHealth, -1 without one
bool c23_trooper(const void* actor);
bool c37_enemy(const void* actor); // ASTEnemyBase and not an AI trooper
const void* c23_own_pawn(void* pc);
// --- Card 037: enemy health on the client -------------------------------------------------------------------------
// Client session, every frame (card 132: a 250 ms scan delivered several hits and deaths in one step): every ASTCharacter in the world except the client-own pawn (card 021 has it)
// adopts the replicated health set by card 021's rule (c21_adopt_set), then a Health change since the last scan runs
// UADHealthComponent::HealthAttributeChanged with a 0x48 FOnAttributeChangeData (New +0x38, Old +0x3C), as the
// server's attribute delegate does. The first value seen per pawn (and the one right after an adoption) is only
// recorded; Old <= 0 with New > 0 is never notified: that is the NPC handler's revive branch (card 025), logged as a
// skip. UBWCOOP_NO_ENEMY_HEALTH=1 turns adoption and notify off (step 1 diagnosis). With UBWCOOP_ENEMYAUDIT=1 both
// sides log the health sets (component set and every health set in the ASC's SpawnedAttributes) at each pawn's first
// sighting and every kC37DumpMs for non-trooper enemies, the NPC death path and health handler calls, and the host
// logs each Health change of a non-trooper enemy with its damage effect (UADHealthComponent::
// AttributeSetPostEffectApplied). With UBWCOOP_SPAWN_BUG=1 the host fires its own weapon at the bug
// kC37BurstDelayMs after the spawn for kC37BurstHoldMs, so host and relayed client shots can be compared.
using PostEffectFn = void (*)(void* component, const void* callback_data);
extern StaticClassFn g_st_character_class;
extern PostEffectFn g_orig_post_effect;
extern bool g_c37_off;
extern int32_t g_c132_decal_cap; // UBWCOOP_DECAL_CAP (card 132 test), 0 unset; written in ubw::init
extern bool g_c132_god; // UBWCOOP_GOD (card 132 step 3a); written in ubw::init before any hook
extern bool g_c132_god_host_only; // UBWCOOP_GOD_HOST_ONLY=1 (both sides): host pawn god only, client pawn Health logged
constexpr uint32_t kC37Max = 256; // pawns tracked per scan; the rest are only counted
constexpr uint32_t kC37EventLines = 160;
struct C37Seen {
    const void* actor; // identity only
    uint64_t name;     // FName, with the pointer the identity key
    float health;
    ULONGLONG since;   // card 146: when this pawn was first scanned (an in-view kill against a relevancy arrival)
};
struct C37State {
    C37Seen seen[kC37Max];
    uint32_t seen_count;
    ULONGLONG next_dump_ms;
    ULONGLONG burst_ms; // host: when the burst pressed; 0 = not yet
    uint32_t adopt_lines;
    uint32_t notify_lines;
    uint32_t skip_lines;
    uint32_t dump_lines;
    uint32_t event_lines;
    uint32_t damage_lines;
    uint32_t adopts;
    uint32_t notifies;
    uint32_t revive_skips;
    uint32_t revive_calls; // client: NPC handler calls with Old <= 0 and New > 0
    bool armed;            // client: adoption scan
    bool audit;            // both: dumps and event lines
    bool burst_held;
    bool untracked_logged;
};
extern C37State g_c37;
void detour_post_effect(void* component, const void* data);
// --- Card 040: where the x1.5 and x0.1 hits come from (log only) ---------------------------------------------------
// Host, UBWCOOP_C40_DAMAGE_TRACE=1: each UADDamageHandler::Process whose target ASC (params +0) belongs to a
// non-trooper enemy is one traced hit (up to kC40Hits per map; nested Process calls pass untraced). While it runs,
// the stage hooks below log the damage (params +0x3C, armour +0x40, health +0x44) around the base and AI
// ModifyDamage, SplitDamage, ApplyDamageToArmour and ApplyDamageToHealth; the two attribute reads inside the base
// ModifyDamage (UADAbilitySystemComponent::GetGameplayAttributeValueOrDefault); both damage modifier subsystem calls
// with their tag containers; every hit volume's IsWithinVolume (both points, with its cooldown) and TryTriggerVolume.
// The origin is the effect context's instigator (context +0x8): the host's own pawn is "host", another player pawn
// "relayed"; a hit whose EffectCauser (context +0x10) is not a projectile is "<origin>-other" (a test kill, a melee).
// The effect context's FHitResult comes from its own virtual GetHitResult (vtable +0x70), as
// USTAIDamageHandler::ModifyDamage gets it.
// Test driver (with UBWCOOP_SPAWN_BUG): card 023 scans every kC40ScanMs and skips its own test kill. The host holds
// fire on each card 023 bug from kC40PressDelayMs after its spawn until it dies (or kC40BurstCapMs), aiming at
// kC40AimLift heights in turn; kC40RespawnGapMs after a death card 023 spawns the next bug, up to kC40ExtraBugs, until
// both origins have kC40ShotsWanted traced shots. The client holds its own fire (through the card 026 relay) on the
// nearest live enemy, aiming at the same heights. Nothing is written to the game except the test driver's spawns,
// aim and fire, as cards 023 and 037 already do.
using DamageProcessFn = void (*)(void* handler, void* params);
using DamageStageFn = void (*)(const void* handler, void* params);
using AttrOrDefaultFn = float (*)(void* asc, void* attribute, float default_value); // FGameplayAttribute by value
using DamageByTagsFn = float (*)(const void* subsystem, float damage, const void* tags_a, const void* tags_b);
using ModifiedDamageFn = float (*)(const void* subsystem, const void* set, float damage, const void* tags_a,
                                   const void* tags_b);
using WithinVolumeFn = bool (*)(const void* volume, const double* point);
using TryTriggerFn = void (*)(void* volume, void* params);
extern DamageProcessFn g_orig_c40_process;
extern DamageStageFn g_orig_c40_base_modify;
extern DamageStageFn g_orig_c40_ai_modify;
extern DamageStageFn g_orig_c40_split;
extern DamageStageFn g_orig_c40_armour;
extern DamageStageFn g_orig_c40_health;
extern AttrOrDefaultFn g_orig_c40_attr;
extern DamageByTagsFn g_orig_c40_by_tags;
extern ModifiedDamageFn g_orig_c40_modified;
extern WithinVolumeFn g_orig_c40_within;
extern TryTriggerFn g_orig_c40_try_trigger;
void detour_c40_process(void* handler, void* params);
void detour_c40_base_modify(const void* handler, void* params);
void detour_c40_ai_modify(const void* handler, void* params);
void detour_c40_split(const void* handler, void* params);
void detour_c40_armour(const void* handler, void* params);
void detour_c40_health(const void* handler, void* params);
float detour_c40_attr(void* asc, void* attribute, float default_value);
float detour_c40_by_tags(const void* subsystem, float damage, const void* tags_a, const void* tags_b);
float detour_c40_modified(const void* subsystem, const void* set, float damage, const void* tags_a,
                          const void* tags_b);
bool detour_c40_within(const void* volume, const double* point);
void detour_c40_try_trigger(void* volume, void* params);
// --- Card 038: enemies die on the client ---------------------------------------------------------------------------
// Death runs on the authority only, so a bug the host kills stays standing on the client. On the client, when card
// 037's scan sees a non-local NPC's Health cross from > 0 to <= 0 (once per actor per map, fixed-size set), this
// calls only the death visuals: USTBugAnimPhysComponent::OnDeath (it sets the anim state at +0x430 to 0x10, and the
// component's own tick plays the fall; it never reads the event, a zeroed buffer is passed anyway) when the pawn has
// one, else ASTNonPlayableCharacterBase::TryPlayDeathAnimation (card038d). Card 131: before that call (not on a
// gib), the client's copy is first put where the host's FSM_Enemy_Dying puts it: SetCurrentBaseState(Dying), which
// runs the Blueprint's ReceiveBaseStateChanged, then HandleDying: SetShowingStaticMesh(false) when the NPC shows a
// static mesh (+0xA40; a WarriorBug does, so without it the skeletal death never shows), the death bark, OnIsDying,
// the Dying tag, TryTakeCorpseTicket (without a game mode, as on every client, it InstaKills the NPC unless it is
// overkilled; the damage stops at the ability system's authority gate, so nothing changes, card 363) and, for a 2D NPC,
// EnableCorpseBoxCollider. CurrentBaseState is not replicated, so the client copy otherwise stays Aware.
// USTGoreComponent::OnDeath is hooked on the host only to log; the host never called it, so the
// client does not either. Never HandleDeathEvent, StartDying, ReceiveStartDying or any
// score, ticket, drop, AI-registration or Tag_Corpse code. The game-logic death callees are hooked only to count
// calls per side (the client must show zero outside its own card 131 HandleDying). With UBWCOOP_ENEMYAUDIT=1 the host logs the three visual natives
// with the time since the NPC's HandleDeathEvent, and the client shoots its window kC38ShotMs after the first non-trooper
// death edge, facing where it died, and logs the bug's anim state at each shot.
using BugAnimDeathFn = void (*)(void* component, const void* damage_event);
using GoreDeathFn = void (*)(void* component, const Vec3* location);
using NpcVoidFn = void (*)(void* npc);
using EnemyKilledFn = void (*)(void* meta_game, uint64_t tag); // FGameplayTag (one FName) by value
using DeregisterAiFn = void (*)(void* awareness, void* npc);
extern StaticClassFn g_bug_anim_class;
extern StaticClassFn g_gore_class;
extern BugAnimDeathFn g_orig_bug_anim_death;
extern GoreDeathFn g_orig_gore_death;
extern NpcVoidFn g_orig_try_play_death;
extern NpcVoidFn g_orig_enemy_start_dying;
extern EnemyKilledFn g_orig_enemy_killed;
extern NpcVoidFn g_orig_release_tickets;
extern DeregisterAiFn g_orig_deregister_ai;
extern NpcVoidFn g_orig_corpse_ticket;
// Dead actors tracked at once. Card 130 item 15: an entry lives only while its actor is in the client's card 037 scan
// with Health <= 0 (c38_prune drops the gone and the revived each scan), and only that scan adds entries, so after a
// prune the set holds at most one scan's corpses; it was 128 per map, and every death after the 128th lost its visual.
constexpr uint32_t kC38Max = kC37Max;
enum C38Logic : uint32_t {
    kC38NpcDeathEvent, kC38NpcStartDying, kC38EnemyStartDying, kC38EnemyKilled, kC38ReleaseTickets,
    kC38DeregisterAi, kC38CorpseTicket, kC38StartOverkill, kC38ReleaseCorpse, kC38HandleDying, kC38OverkillEffect,
    kC38Logic
};
struct C38Dead {
    const void* actor; // identity only
    uint64_t name;
    bool dying = false;     // card 131: the client entered Dying for it (base state, HandleDying): its revive undoes it
    uint8_t capsule_before = 0; // card 131: the root capsule's ECollisionEnabled before card 039 set NoCollision
};
struct C38State {
    C38Dead dead[kC38Max];
    uint32_t dead_count;
    uint32_t dead_peak; // card 130: most entries at once this map (the corpse ticket limit plus fresh corpses)
    uint32_t pruned;    // card 130: entries freed by c38_prune this map
    uint32_t logic[kC38Logic];
    uint32_t calls_bug, calls_npc;
    uint32_t calls_dying;  // card 131: client SetCurrentBaseState(Dying) and HandleDying calls
    uint32_t dying_lines; // card 132 HandleDying lines
    uint32_t held;       // card 132: client NPC copies' local switches to Falling held at the host's mode
    uint32_t drift;      // card 132: client NPC copies set back to the host's z (c132_drift_clamp)
    uint32_t drift_skipped; // card 328: set-backs not written because the place encroaches another body
    const void* trace_owner; // card 328 step 3: the copy traced after its set-back (identity only)
    uint32_t trace_left;     // card 328 step 3: frames of that trace still to log; 0 = none armed
    uint32_t traces;         // card 328 step 3: traces armed this map (cap kC328TraceMax)
    uint32_t corpse_collision_lines; // card 132: c132_corpse_collision lines this map
    bool in_dying;         // card 131: the client's own HandleDying is running (its callees are not game calls)
    bool corpse;           // card 131: that HandleDying is for a corpse first seen dead: its barks are blocked
    uint32_t calls_corpse, calls_revive, barks_blocked; // card 131: corpse-seen paths, revive undos, barks blocked
    uint32_t edge_lines, host_lines, logic_lines;
    ULONGLONG death_ms;  // host: the last NPC HandleDeathEvent
    ULONGLONG edge_ms;   // client: the first bug death edge; 0 = none yet
    C38Dead shot_bug;    // client: that enemy, the first non-trooper edge (identity only)
    Vec3 shot_loc;       // client: where it died
    uint32_t shots;
    bool state_due;      // client: log the shot bug's anim state at the next scan
    bool full_logged;
};
extern C38State g_c38;
void c38_logic(C38Logic which, const void* actor);
void c38_log_counts(const char* when);
void detour_bug_anim_death(void* component, const void* damage_event);
void detour_gore_death(void* component, const Vec3* location);
void detour_try_play_death(void* npc);
void detour_enemy_start_dying(void* npc);
void detour_enemy_killed(void* meta_game, uint64_t tag);
void detour_release_tickets(void* npc);
void detour_deregister_ai(void* awareness, void* npc);
void detour_corpse_ticket(void* npc);
// --- Card 039: client gibs and corpse collision -------------------------------------------------------------------
// Host trace (log only): StartOverkillState, SpawnGoreResponse, TryPlayOverkillAnimation, ReleaseCorpseTicket,
// OverkillDamageEffectReceived, HandleDying and UpdateColliders, each with the time since the NPC's HandleDeathEvent.
// StartOverkillState, ReleaseCorpseTicket, HandleDying and OverkillDamageEffectReceived also count as game logic in
// card 038's table (the client must show zero). Client, on card 038's death edge: an NPC whose replicated Health is at
// or below -|OverkillThreshold| (the host's UADHealthComponent rule) gets only the gore visuals:
// USTGoreComponent::OnOverkill at GetGoreLocation (the same SpawnGoreResponse build StartOverkillState makes), then
// the response's Niagara systems through UNiagaraFunctionLibrary::SpawnSystemAtLocation, because SpawnGoreResponse
// skips them (and the pooled chunk projectiles) without a GameMode. Never StartOverkillState or HandleDying on the
// client. With UBWCOOP_ENEMYAUDIT=1 both sides log each NPC's corpse box, capsule and mesh collision kC39ProbeMs
// after its death. UBWCOOP_NO_CLIENT_GORE=1 turns the client calls off (diagnosis only).
using GoreOverkillFn = void (*)(void* component, const Vec3* location);
using SpawnGoreFn = void (*)(void* context, const void* data, const Vec3* location, const Vec3* direction,
                             const Vec3* rotation, float* out);
using OverkillEffectFn = void (*)(void* health, const void* callback, const void* damage_event);
using GoreLocationFn = Vec3* (*)(const void* npc, Vec3* out); // hidden return pointer
// USkeletalMeshComponent::UpdateKinematicBonesToAnim(const TArray<FTransform>& InComponentSpaceTransforms,
// ETeleportType, bool bNeedsSkinning, EAllowKinematicDeferral)
using KinematicBonesFn = void (*)(void* mesh, const void* transforms, uint8_t teleport, bool needs_skinning,
                                  int32_t deferral);
using BodySimulatingFn = bool (*)(const void* body); // FBodyInstanceCore::ShouldInstanceSimulatingPhysics
using SkeletalComFn = Vec3* (*)(const void* mesh, Vec3* out); // USkeletalMeshComponent::GetSkeletalCenterOfMass
using SetSimulateFn = void (*)(void* mesh, bool simulate); // USkeletalMeshComponent::SetSimulatePhysics
using NpcBoolFn = bool (*)(const void* npc);
using HealthFloatFn = float (*)(const void* health);
using CollisionEnabledFn = uint8_t (*)(const void* component);
using ProfileNameFn = uint64_t* (*)(const void* component, uint64_t* out); // FName, hidden return pointer
extern NpcVoidFn g_orig_start_overkill;
extern SpawnGoreFn g_orig_spawn_gore;
// Card 132 gore trace (log only): SpawnGoreResponse's decals (SpawnGoreDecals) and the decal manager's two spawns.
// FVector/FRotator by value are passed by pointer (MSVC x64, > 8 bytes); FName by value is 8 bytes.
using SpawnGoreDecalsFn = void (*)(void* context, const void* data, const Vec3* location, const Vec3* direction);
using SpawnDecalAttachedFn = void* (*)(void* manager, void* material, const Vec3* size, void* component,
                                        uint64_t socket, const Vec3* location, const Vec3* rotation, uint8_t attach,
                                        float life);
using SpawnDecalAtLocationFn = void* (*)(void* manager, const void* context, void* material, const Vec3* size,
                                          const Vec3* location, const Vec3* rotation, float life);
extern SpawnGoreDecalsFn g_orig_spawn_gore_decals;
extern SpawnDecalAttachedFn g_orig_spawn_decal_attached;
extern SpawnDecalAtLocationFn g_orig_spawn_decal_at;
void detour_spawn_gore_decals(void* context, const void* data, const Vec3* location, const Vec3* direction);
void* detour_spawn_decal_attached(void* manager, void* material, const Vec3* size, void* component, uint64_t socket,
                                  const Vec3* location, const Vec3* rotation, uint8_t attach, float life);
void* detour_spawn_decal_at(void* manager, const void* context, void* material, const Vec3* size,
                            const Vec3* location, const Vec3* rotation, float life);
extern NpcVoidFn g_orig_try_play_overkill;
extern NpcVoidFn g_orig_release_corpse;
extern NpcVoidFn g_orig_handle_dying;
// Card 131: ASTNonPlayableCharacterBase::SetCurrentBaseState; `state` an ESTBaseNPCState (kNpcStateDying = 3).
constexpr size_t kNpcBaseState = 0xAE8; // ESTBaseNPCState byte (CurrentBaseState, not replicated); StartOverkillState writes 5
constexpr uint8_t kNpcStateDying = 3;    // ESTBaseNPCState::Dying (Unaware 0, Alerted 1, Aware 2, Dead 4, Overkilled 5)
using NpcSetStateFn = void (*)(void* npc, uint8_t state);
extern NpcSetStateFn g_c131_set_base_state;
extern NpcVoidFn g_c318_stop_dying_sm;  // card 318: StopStateMachine_Dying
extern NpcVoidFn g_c318_start_core_sm;  // card 318: StartStateMachine_Core
extern NpcVoidFn g_c318_on_revived;     // card 318: BP_OnRevived (raw)
void c318_map_end();                    // card 318, client: per-map revive tally (enemies.cpp)
uint32_t c318_sprite_revive(void* npc); // card 318, client: drop a revived copy's sprite death row and wait (pose_c132.cpp)
int c318_sprite_now(const void* npc, char* seq, size_t size, int32_t* frame, int32_t* frames); // pose_c132.cpp
// Card 131: a client NPC copy first seen dead gets the Dying visuals without the death bark (PlayBark and
// PlayBarkOnNPCNearThePlayer, both called by HandleDying); the detours pass through unless g_c38.corpse is set.
// A revive (Health <= 0 -> > 0) undoes what the client did, as the host's HandleHealthValueChangedEvent does: base
// state Aware, the Dying tag off the +0x680 component's tag counts (+0xF18), the capsule's collision back.
constexpr uint8_t kNpcStateAware = 2;          // ESTBaseNPCState::Aware: the host's revive sets it
constexpr size_t kNpcTagComponent = 0x680;     // the component whose tag counts take Dying (HandleDying, the revive)
constexpr size_t kTagCountContainer = 0xF18;   // its FGameplayTagCountContainer
constexpr size_t kTagChangedVfunc = 0x960;     // its tag-changed notify (tag, count), called after an update
constexpr size_t kHandleDyingTagLoad = 0x23C;  // HandleDying: mov rcx, [rip+disp32] loading the Dying FGameplayTag
using BarkFn = bool (*)(void* self, void* bark);              // bark: as the caller passes it (TSubclassOf by address)
using TagMapFn = bool (*)(void* container, const uint64_t* tag, int32_t delta);
using TagChangedFn = void (*)(void* component, const uint64_t* tag, int32_t count);
extern BarkFn g_orig_npc_play_bark, g_orig_bark_near_player;
extern TagMapFn g_c131_update_tag_map;
using FedScoreSubsystemFn = void* (*)(void* world);
using FedScoreFn = int32_t (*)(const void* subsystem);
extern FedScoreSubsystemFn g_c131_score_subsystem;
extern FedScoreFn g_c131_fed_score;
// Card 475: static const FText& FText::GetEmpty().
using FTextGetEmptyFn = const void* (*)();
extern FTextGetEmptyFn g_c475_text_empty;
// Card 131: the local FedScore of the world `actor` is in, or -1 when it cannot be read.
int32_t c131_score(void* actor);
bool detour_npc_play_bark(void* npc, void* bark);
bool detour_bark_near_player(void* context, void* bark);
// Card 451: RequestBark with the range taken from the nearest player, not the host's view only (enemies.cpp).
using C451BarkFn = bool (*)(void* subsystem, void* speaker, void* const* bark, void* const* voice);
extern C451BarkFn g_orig_c451_request_bark;
bool detour_c451_request_bark(void* subsystem, void* speaker, void* const* bark, void* const* voice);
// Card 451 part 2 (bark_c451.cpp): FBarkDelegate::Broadcast(const FADActiveBark&) relayed host to client for subtitles.
using C451BroadcastFn = void (*)(void* delegate, const void* bark);
extern C451BroadcastFn g_orig_c451_broadcast;
void detour_c451_broadcast(void* delegate, const void* bark);
// Card 493 (cave_c493.cpp): host reruns FED_C5M2 `Cave in` for every other player; the rerun's re-arm is skipped.
// ASTSaveableLevelScriptActor::CallFunctionWithDelay(float, FString by value): rcx this, xmm1 delay, r8 FString*.
using C493DelayFn = void (*)(void* self, float delay, void* fstring);
extern C493DelayFn g_orig_c493_delay;
void detour_c493_delay(void* self, float delay, void* fstring);
extern bool g_c493_pe; // detour_process_event calls c493_after_pe
void c493_after_pe(void* object, const void* function);
void c493_arm_map();
bool c98_dead_now(const void* actor, bool& killable); // world_c98.cpp
void c451_receive(void* pc, const WireString* s); // client: ChanOp::Bark
void c451_relay_summary();                        // both sides, LoadMap enter
void* c322_world_subsystem(void* ctx, void* cls); // radio_c322.cpp: GetWorldSubsystem(ctx, cls)
extern NpcVoidFn g_orig_update_colliders;
extern OverkillEffectFn g_orig_overkill_effect;
extern GoreOverkillFn g_orig_gore_overkill;
extern NpcVoidFn g_enable_corpse_box;
extern GoreLocationFn g_gore_location;
extern KinematicBonesFn g_kinematic_bones;
// Card 524: USceneComponent::K2_SetRelativeLocation(FVector NewLocation, bool bSweep, FHitResult& SweepHitResult,
// bool bTeleport). The FVector is passed by value, which the x64 ABI passes as a pointer to a copy.
using SetRelativeLocationFn = void (*)(void* component, const void* location, bool sweep, void* hit, bool teleport);
extern SetRelativeLocationFn g_set_relative_location;
extern BodySimulatingFn g_body_simulating;
extern SkeletalComFn g_skeletal_com;
using CollisionTypeFn = uint8_t (*)(const void* component); // UPrimitiveComponent::GetCollisionObjectType
extern CollisionTypeFn g_collision_type;
extern SetSimulateFn g_orig_c132_set_simulate;
extern ExecThunkFn g_orig_c132_exec_set_simulate;
void detour_c132_exec_set_simulate(void* context, void* frame, void* result);
void detour_c132_set_simulate(void* mesh, bool simulate);
using ShowStaticFn = void (*)(void* animator, bool show, bool immediate); // USTSkeletalAnimatorComponent::SetShowingStaticMesh_Internal
extern ShowStaticFn g_orig_c132_show_static;
void detour_c132_show_static(void* animator, bool show, bool immediate);
// hb51 senior point 3: a dead host NPC's body 0 stops simulating with no SetSimulatePhysics(false) call. These are
// the other USkeletalMeshComponent paths that change body simulation or replace the bodies; each logs (capped) the
// owner, body 0 simulating and the body array before and after, and the caller. Log only.
using MeshBoolFn = void (*)(void* mesh, bool value);                              // SetAllBodiesSimulatePhysics
using MeshBelowFn = void (*)(void* mesh, const void* bone, bool simulate, bool self); // SetAllBodiesBelowSimulatePhysics
using MeshVoidFn = void (*)(void* mesh);                                          // ResetAllBodiesSimulatePhysics, TermArticulated
using MeshSceneFn = void (*)(void* mesh, void* scene);                            // InitArticulated
extern MeshBoolFn g_orig_c132_all_bodies;
extern MeshBelowFn g_orig_c132_bodies_below;
extern MeshVoidFn g_orig_c132_reset_bodies;
extern MeshVoidFn g_orig_c132_term_articulated;
extern MeshSceneFn g_orig_c132_init_articulated;
void detour_c132_all_bodies(void* mesh, bool simulate);
void detour_c132_bodies_below(void* mesh, const void* bone, bool simulate, bool self);
void detour_c132_reset_bodies(void* mesh);
void detour_c132_term_articulated(void* mesh);
void detour_c132_init_articulated(void* mesh, void* scene);
extern ExecThunkFn g_orig_c132_exec_set_hidden;
extern SetHiddenInGameFn g_orig_c132_set_hidden;
void detour_c132_exec_set_hidden(void* context, void* frame, void* result);
void detour_c132_set_hidden(void* comp, bool hidden, uint8_t propagation);
extern NpcBoolFn g_is_npc_2d;
extern HealthFloatFn g_overkill_threshold;
extern CollisionEnabledFn g_collision_enabled;
using HurtShakeFn = void (*)(void* pawn, float damage); // card 454: ASTPlayerCharacter::PlayHurtCameraShake
extern HurtShakeFn g_c454_hurt_shake;
using IsPlayerFn = bool (*)(const void* actor); // card 258: USTCombatUtilityLibrary::IsPlayer, a player gameplay tag on the ASC
extern IsPlayerFn g_c258_is_player;
extern ProfileNameFn g_collision_profile;
using NpcEndPlayFn = void (*)(void* npc, uint32_t reason); // EEndPlayReason::Type in the low byte
extern NpcEndPlayFn g_orig_npc_end_play;
extern bool g_c39_off;
void detour_start_overkill(void* npc);
void detour_spawn_gore(void* context, const void* data, const Vec3* location, const Vec3* direction,
                       const Vec3* rotation, float* out);
void detour_try_play_overkill(void* npc);
void detour_release_corpse(void* npc);
void detour_overkill_effect(void* health, const void* callback, const void* damage_event);
void detour_handle_dying(void* npc);
void detour_update_colliders(void* npc);
void detour_npc_end_play(void* npc, uint32_t reason);
void detour_gore_overkill(void* component, const Vec3* location);
// --- Card 032: mech spawn and enter audit (log only, plus a host test driver) -------------------------------------
// UBWCOOP_SPAWN_MECH=pickup|mech|pad, set on both machines. Both sides: a kC32ScanMs scan of every actor whose class
// name holds one of kC32Patterns (or that is an ASTPlayerMechBase), one row when found, moved or gone; the pickups
// table row TacMechSupport once per map; EnterMech, LeaveMech and the mech OnInteraction with the caller, the weak PC
// at kMechController and the pilot's state; the first kC32PeCalls calls of each UFunction whose name or outer holds
// one of kC32PeWords. Host driver, kC32StartMs after both player pawns are on foot: spawn (pickup, mech or pad), get a
// mech, enter it with OnInteraction, drive it with AddMovementInput, shoot its window, RequestLeaveMech. Client: an
// OnInteraction on the mech kC32ClientInteractMs after it first sees one, and its own screenshots.
using TryThrowFlareFn = void (*)(void* component, int32_t index);
using EquipTacSuppFn = void (*)(void* component, void* const* config_class, bool flag); // TSubclassOf by pointer
using BlockingVolumeFn = bool (*)(void* component);
extern TryThrowFlareFn g_orig_try_throw_flare;
extern EquipTacSuppFn g_orig_equip_tac_supp;
extern BlockingVolumeFn g_in_blocking_volume;
extern C29InteractFn g_orig_mech_on_interaction;
extern bool g_c32_pe;
constexpr uint32_t kC32Tracked = 48;
constexpr uint32_t kC32PeLines = 400;
constexpr uint32_t kC32PeCalls = 3;            // lines per UFunction
constexpr uint32_t kC32PeSlots = 2048;         // power of two
constexpr uint32_t kC32PeProbe = 16;
enum class C32Stage : uint8_t { Wait, Spawn, Walk, Throw, AwaitMech, Settle, Drive, Done };
struct C32Track {
    int32_t index; // object index
    Vec3 loc;
    bool pad;      // BP_MechDropPlatform
    bool pilotable; // ASTPlayerMechBase
    bool spawned;  // by this driver
    bool live;     // listed by the last scan
};
struct C32Pe {
    int32_t index; // function object index + 1; 0 = empty slot
    uint8_t interesting;
    uint8_t calls;
};
struct C32State {
    bool armed;
    bool table_logged;
    bool view_logged; // one view row: own pawn and camera component heights
    bool host_shot, client_shot, client_interacted, client_after, client_done;
    C32Stage stage;
    DWORD thread;
    ULONGLONG start_ms;   // both on foot (host) or own on foot (client); 0 = not yet
    ULONGLONG stage_ms;
    ULONGLONG next_scan_ms;
    ULONGLONG mech_seen_ms;  // first pilotable mech; 0 = none yet
    ULONGLONG mech_moved_ms; // first moved sample of it; 0 = none yet
    ULONGLONG interact_ms;
    int32_t mech;   // object index of the driver's mech, -1 = none
    int32_t pickup; // object index of the spawned pickup, -1 = none
    Vec3 pickup_loc;
    uint32_t throws;
    C32Track track[kC32Tracked];
    uint32_t track_count;
    uint32_t row_lines, event_lines, driver_lines, pe_lines;
    C32Pe pe[kC32PeSlots];
};
extern C32State g_c32;
void detour_try_throw_flare(void* component, int32_t index);
// Card 100: USTTacticalSupportComponent::HandleTacticalSupportInput (0x050EDD50). The client's own call does not run
// locally; its slot goes to the host, where the server copy's component runs the original.
extern TryThrowFlareFn g_orig_c100_tac_input; // same shape: (component, slot)
using C100ChargeFn = int32_t (*)(void* component, int32_t slot);
extern C100ChargeFn g_c100_charge; // GetAbilityCharge (0x050E8CB0)
void detour_c100_tac_input(void* component, int32_t slot);
void c100_receive_tac(void* pc, uint32_t index);
// Card 104: ability animations (anim_c104.cpp). USTSkeletalAnimatorComponent::StartAnimationSequence (0x05104C50),
// UADSkeletalAnimatorComponent::StartAnimationSequenceFixedTime (0x04A24B90), USTAbilityTask_PlayAnimation::DoAction
// (0x050E3BC0) and AnimationEnded (0x050DBCB0): counted and logged per owner side. The last two originals are
// g_orig_c74_anim[1] and [2]; the card 074/076 log detours run inside ours when on.
using C104StartFn = void (*)(void* animator, uint64_t key, float value);
extern C104StartFn g_orig_c104_start;
extern C104StartFn g_orig_c104_start_fixed;
void detour_c104_start(void* animator, uint64_t key, float rate);
void detour_c104_start_fixed(void* animator, uint64_t key, float total);
void detour_c104_do_action(void* task);
void detour_c104_anim_ended(void* task);
void c104_dump();
struct C104Policy {
    uint8_t* addr[64];
    uint8_t was[64];
    int32_t n;
};
void c104_policy_set(void* asc, int32_t handle, const char* what, C104Policy* p);
void c104_policy_restore(const C104Policy& p);
void c101_note_client_sent(int32_t handle); // host: the client's ServerTryActivateAbility named this spec handle
void c105_activation(const void* asc, int32_t handle, const void* ability, bool ok, bool client_request);
// Card 104: the tactical support charge mirror. Host: each copy poll sends its server copy's slot charges and ready
// flags (TacCharge "charge ready" per slot) on a change; `sent` is that copy's last text. Client: applied to its own
// pawn's slots through UpdateAbilityCharge (the delta) and SetIsAbilityReadyForUse, which broadcast to the HUD.
using C104ChargeFn = void (*)(void* slot, int32_t delta);
using C104ReadyFn = void (*)(void* slot, bool ready);
extern C104ChargeFn g_c104_update_charge;
extern C104ReadyFn g_c104_set_ready;
// Card 333: USTTacticalSupportSlot::DestroyTacticalSupportAbility, the game's own removal of a slot's ability. The
// client calls it on its own slot when the host's TacCharge row says that slot is empty (charge -1).
using C104DestroyFn = void (*)(void* slot);
extern C104DestroyFn g_c104_destroy_slot;
void c333_arm_map(); // card 333: the client's host-slot flags start over (from c98_arm_map)
void c104_charge_poll(void* pc, const void* pawn, char* sent, size_t cap);
int32_t c104_pawn_charge_text(const void* pawn, char* out, size_t cap); // card 327 PW
void c104_receive_charge(void* pc, const WireString* s);
const char* c106_set_charge(void* pawn, int32_t slot_index, int32_t value, bool ready); // card 106 test command
// Card 104: host. A tactical support actor (a landed flare, a strike's BP_TacSupp_ actor) goes to every client as a
// copy (WorldOp 'P', class path and place); its destroy follows ('D'). No-op off the listen server or on replication.
void c104_relay_spawn(const void* actor, const Vec3& at);
uint32_t c104_copy_host_id(const void* actor); // card 306: client, a relayed copy's host id, 0 if none
void* c104_relayed_by_id(uint32_t id);           // card 306: host, the live relayed actor with this id
// Card 104/105: host. A relayed actor's end ('D', its journal row dropped): its destroy, or a relayed pickup taken.
void c104_relay_destroy(const void* world, const void* actor);
// Card 121: client in a co-op session (g_client_session). The save engine neither saves nor loads there:
// USTSaveSystemLibrary::PerformSave (0x05211400) is the one entry of every engine save (Save_CheckPoint, Save_Manual,
// Save_Quicksave, ReturnToMainMenu's checkpoint, the deferred retry), and USTSaveSystemLibrary::Load (0x0520A580) of
// every engine load (Load, LoadLatestSave, Load_Quicksave and the quickload key). A skipped save never reaches
// USTSaveComponent::PrepareForSave, so no OnPreSave / PreSaveGame runs on state the client never filled in (card 119's
// copies, card 121's squad with a null member). Level completion and the campaign slot file go through
// USTCampaignSlotTracker::TrySaveStateToFile, settings through UGameUserSettings: both untouched. The host is untouched.
using C121PerformSaveFn = void (*)(void* world, void* metadata, void* delegate, bool flag);
using C121LoadFn = void (*)(void* context, void* save_data, void* metadata, bool* file_io_success, bool load_pawns,
                            bool destroy_actors);
extern C121PerformSaveFn g_orig_c121_perform_save;
extern C121LoadFn g_orig_c121_load;
void detour_c121_perform_save(void* world, void* metadata, void* delegate, bool flag);
int c375_host_pc_first(void* world); // hooks_ubw.cpp: host PC to PC list slot 0; returns its old slot, -1 = not found
void detour_c121_load(void* context, void* save_data, void* metadata, bool* file_io_success, bool load_pawns,
                      bool destroy_actors);
void c121_arm_map(); // per-map line cap (save_c121.cpp)
// Card 137 step 3: host. After every ASTSaveSystemEngine::LoadFromSlot (the in-place restore and the frontend Continue
// route alike) the journals that a load can make stale are rebuilt from the actors' state (save_c121.cpp drives one pass
// over the level actors; each journal's owner applies its rule to each actor).
using C137LoadSlotFn = void (*)(void* engine, void* slot_name, bool a, bool b, const void* data);
extern C137LoadSlotFn g_orig_c137_load_slot;
void detour_c137_load_slot(void* engine, void* slot_name, bool a, bool b, const void* data);
struct C137LoadCounts {
    uint32_t k_dropped, k_added, h_fixed;  // c98 K rows of live actors, dead actors with no K row, H rows rewritten
    uint32_t s_dropped, s_added, s_lines;  // card 464: c98 S rows of alive satchel buildings, destroyed ones with
                                           // no row; per-row S lines this load (own cap)
    uint32_t e_fixed, e_added;             // card 145 E rows rewritten from bEnabled, disabled pickups with no row
    uint32_t door_fixed, door_added;       // c95 door rows rewritten from DoorState, open doors with no row
    uint32_t c95c_fixed, c95c_removed;     // card 147: c95 collision rows rewritten, rows whose actor is gone
    uint32_t c95c_before, c95c_after;      // card 147: c95 collision rows before and after the load
    uint32_t lines;                        // per-row lines this load (capped)
    int32_t actors;                        // card 326: level actors the world pass saw
};
void c98_load_actor(const void* actor, C137LoadCounts& n);  // world_c98.cpp
void c145_load_actor(const void* actor, C137LoadCounts& n); // world_c98.cpp
void c95_load_actor(const void* actor, C137LoadCounts& n);  // door_c95.cpp
void c95_load_collisions(const void* world, C137LoadCounts& n); // door_c95.cpp: card 147
bool c137_load_line(C137LoadCounts& n);                     // save_c121.cpp: per-row line cap
// Card 107: client. True only while c104_client_spawn spawns a host copy, so the spawn detour lets that one through.
extern bool g_c107_copy_spawn;
inline constexpr char kC104StrikePrefix[] = "BP_TacSupp_"; // the strikes' actors (not BP_TacSuppPickup_)
inline constexpr char kC105RackPrefix[] = "BP_ResupplyRack_"; // card 105: a resupply pod's rack
// Card 074's asset path ("<package>.<name>", or "-") and its find-or-load, shared with the card 104 relay.
// Card 119: 192 chars with the terminator. The longest relayed path seen is 138 (package 95 + "." +
// BP_TacSupp_BombingRunProjectile_Tutorial_C); the wire parsers' "%191s" widths follow this size.
inline constexpr size_t kAssetPathChars = 192;
struct C74Path {
    char text[kAssetPathChars];
};
C74Path c74_asset_path(const void* asset);
void* c74_find_asset(const char* path, void* cls);
// Card 104: the support flare's chain (ASTSupportFlare RunLZQuery, LZQueryFinished, StartActivationTimer, ActivateFlare,
// ExpireFlare) and USTTacticalSupportComponent::OnAbilityUsed, log only.
constexpr uint32_t kC104FlareFns = 3; // RunLZQuery, ActivateFlare, ExpireFlare
using C104FlareFn = void (*)(void* flare);
using C104FlareTimerFn = void (*)(void* flare, float delay);
using C104LzFn = void (*)(void* flare, void* result);
using C104UsedFn = void (*)(void* component, uint64_t tag, int32_t slot);
extern C104FlareFn g_orig_c104_flare[kC104FlareFns];
extern C104FlareTimerFn g_orig_c104_flare_timer;
extern C104LzFn g_orig_c104_lz;
extern C104FlareFn g_orig_c104_flare_begin; // card 249: ASTSupportFlare::BeginPlay
constexpr uintptr_t kC249LzStatus = 0x28;    // FEnvQueryResult: int status, 1 = success
constexpr uintptr_t kC249LzItemsNum = 0x08;  // FEnvQueryResult: Items.Num
extern C104UsedFn g_orig_c104_used;
void detour_c104_flare_run(void* flare);
void detour_c104_flare_activate(void* flare);
void detour_c104_flare_begin(void* flare);
void detour_c104_flare_expire(void* flare);
void detour_c104_flare_lz(void* flare, void* result);
void detour_c104_flare_timer(void* flare, float delay);
void detour_c104_used(void* component, uint64_t tag, int32_t slot);
bool c73_class_is(const void* cls, const char* name); // grenades.cpp
void detour_equip_tac_supp(void* component, void* const* config_class, bool flag);
bool c32_pe_interesting(const void* function);
void c32_note_pe(const void* object, const void* function);
// --- Card 030: player death audit (log only, plus a host test driver) -------------------------------------------
// UBWCOOP_KILL_PLAYER=host|client, set on both machines. Host test driver: kC30KillDelayMs after both player pawns
// are on foot, USTCombatUtilityLibrary::DealDamage on the named player's pawn (the host's own pawn, or the client's
// server copy) every kC30HitGapMs, kC30HitFraction of MaxHealth per hit, until Health is 0, then kC30ExtraHits more.
// Both sides: the death window opens at that pawn's first Health 0 (the health-change detour, HandleDeathEvent, or
// the kC30PollMs poll on the client, whose replicated Health does not run the handler). Inside it: one line per new
// (object class, UFunction) pair through UObject::ProcessEvent, screenshots at kC30ShotMs, the interact key once and
// every input tag, interact and server activation. It closes kC30AfterCompleteMs after CompleteDeath, or
// kC30WindowMaxMs after Health 0 without one.
// The causer comes first: DealDamage's second argument is the damaged actor (its root location and the
// GenerateHitResult hit actor, disassembly at 0x5233F60, card 157); the first is the instigator and causer.
using DealDamageFn = void (*)(void* causer, void* target, float amount, bool ignore_modifiers);
using DeathEventFn = void (*)(void* pawn, const void* damage_event);
using CompleteDeathFn = void (*)(void* pawn);
using SetAutoSaveFn = void (*)(void* tracker, bool enabled);
extern DealDamageFn g_deal_damage;
extern DeathEventFn g_orig_player_death_event;
extern DeathEventFn g_orig_start_death;
extern DeathEventFn g_orig_start_death_mi;
extern DeathEventFn g_bug_start_death; // card 196: ASTPlayerCharacterBug::StartDeath, called direct (not hooked)
extern CompleteDeathFn g_orig_complete_death;
extern SetAutoSaveFn g_orig_set_auto_save;
extern ProcessEventFn g_orig_process_event;
extern HealthChangedFn g_orig_health_changed;
extern config::KillPlayer g_c30_mode;
extern bool g_c30_window;
constexpr uint32_t kC30EventLines = 300; // ProcessEvent lines per map
constexpr uint32_t kC30MaxPlayers = 8;
struct C30Seen {
    const void* cls;      // identity only
    const void* function; // identity only
};
struct C30Pawns {
    void* target; // valid for this tick only
    uint32_t players;
    bool host_on_foot;
    bool copy_on_foot;
};
struct C30State {
    C30Seen seen[kC30EventLines];
    uint32_t seen_count;
    uint32_t pe_dropped;
    uint32_t lines;
    uint32_t health_lines;
    uint32_t hits;
    uint32_t landed; // hits that lowered Health
    uint32_t hits_after_zero;
    uint32_t shots;
    DWORD thread;            // game thread, from c30_arm_map
    ULONGLONG next_poll_ms;
    ULONGLONG both_ms;       // host: both pawns first on foot; 0 = not yet
    ULONGLONG next_hit_ms;
    ULONGLONG zero_ms;       // this side: the target's first Health 0; 0 = not yet
    ULONGLONG complete_ms;   // first CompleteDeath; 0 = none
    ULONGLONG key_up_ms;     // 0 = interact not held
    int32_t max_health_offset; // UADAttributeSetHealth.MaxHealth; -1 = not found
    int32_t on_death_offset;   // reflected OnDeath offset from c30_log_death_props; 0 = not seen
    ObjectName dead;         // the last pawn through HandleDeathEvent
    bool armed;
    bool props_logged;
    bool key_done;
    bool driver_done;
};
extern C30State g_c30;
bool c30_line();
uint32_t c30_target_side();
C30Pawns c30_find(const void* world);
int32_t c30_set_offset(const char* attribute);
void* c30_nearest_enemy(const void* world, const void* pawn);
void detour_process_event(void* object, void* function, void* parms);
void detour_player_death_event(void* pawn, const void* damage_event);
void detour_start_death(void* pawn, const void* damage_event);
void detour_start_death_mi(void* pawn, const void* damage_event);
void detour_complete_death(void* pawn);
void detour_set_auto_save(void* tracker, bool enabled);
void detour_health_changed(void* component, const void* change_data);
// --- Card 031: lobby overlay render test (UBWCOOP_OVERLAY_TEST) -------------------------------
// Native UMG, no assets. UUserWidget is abstract, so a concrete game widget class
// (WB_TutorialPrompt_C: CanvasPanel root, no ubergraph) is created for the local PC with
// CreateWidgetInstance; its canvas is cleared and a UTextBlock and a UEditableTextBox built with
// StaticConstructObject_Internal (outer = the WidgetTree, RF_Transactional, as
// UWidgetTree::ConstructWidget does) are added. F8 toggles it; keys are read with GetAsyncKeyState
// (user32 resolved at runtime, own foreground window only) because UIOnly makes the viewport ignore
// input. Single player (net mode 0) only. No game pointer is kept across frames: the widget, the
// box and the game's focus widget are FWeakObjectPtrs resolved at use; the world is compared by
// address only, never dereferenced from the stored value.
// ABI notes (this build's disassembly):
//   FStaticConstructObjectParameters: 0x80 bytes; the ctor sets Class +0, Outer +8 (transient
//     package), Name +0x10, SetFlags +0x18, the rest zero. No exported dtor: nothing to release.
//   SCOI reads UClass::ClassFlags at +0xD4 (test $0x10000080, 0xd4(%r15)); CLASS_Abstract = 0x1.
//   FText: 0x10 bytes (refcounted ITextData* +0, flags +8). UTextBlock::SetText(FText) takes the
//     by-value FText by pointer and releases it itself: the caller must not destroy it again.
//   CreateWidgetInstance: rdx points to the TSubclassOf (mov (%r15),%rcx), r8 = FName by value.
//   FWeakObjectPtr::operator=(FObjectPtr): rdx points to the object pointer (mov (%rdx),%rdx).
//   FInputModeUIOnly / FInputModeGameAndUI: lock mode byte +0x18 (GameAndUI hide cursor +0x19);
//     FInputModeGameOnly: bConsumeCaptureMouseDown +8. Their TSharedPtr widget is null after the
//     ctor, so their dtors have nothing to free.
using ScoParamsCtorFn = void* (*)(void* params, const void* cls);
using ScoInternalFn = void* (*)(const void* params);
using PanelCountFn = int32_t (*)(const void* panel);
using PanelChildAtFn = void* (*)(const void* panel, int32_t index);
using SlotSetPositionFn = void (*)(void* slot, const double* position);
using SlotSetAutoSizeFn = void (*)(void* slot, bool auto_size);
using TextBlockSetTextFn = void (*)(void* text_block, void* text_by_value);
extern ScoParamsCtorFn g_sco_params_ctor;
extern ScoInternalFn g_sco_internal;
extern StaticClassFn g_user_widget_class;
extern StaticClassFn g_canvas_panel_class;
extern StaticClassFn g_widget_tree_class;
extern WidgetFn g_panel_clear_children;
extern PanelCountFn g_panel_child_count;
extern PanelChildAtFn g_panel_child_at;
extern SlotSetPositionFn g_slot_set_position;
extern SlotSetAutoSizeFn g_slot_set_auto_size;
extern TextBlockSetTextFn g_text_block_set_text;
extern InputModeCtorFn g_input_mode_game_and_ui_ctor;
extern InputModeCtorFn g_input_mode_ui_only_ctor;
constexpr size_t kScoParamsBytes = 0x80;
constexpr size_t kScoOuter = 0x08;
constexpr size_t kScoSetFlags = 0x18;
constexpr uint32_t kRfTransactional = 0x8;
constexpr size_t kInputModeLock = 0x18;
constexpr size_t kInputModeHideCursor = 0x19;
constexpr size_t kInputModeConsumeMouse = 0x08;
enum class C31Kind : uint8_t { None, GameOnly, GameAndUI, UIOnly };
constexpr const char* kC31KindText[] = {"none", "GameOnly", "GameAndUI", "UIOnly"};
struct C31State {
    C31Weak widget;       // the overlay UUserWidget
    C31Weak box;          // its UEditableTextBox
    C31Weak game_focus;   // the object whose BP last called SetInputMode_UIOnlyEx (the restore target)
    uintptr_t world_id;   // address of the last game world seen: compared only, never dereferenced
    uintptr_t built_world; // world_id when the overlay was built
    C31Kind game_kind;    // the game's last SetInputMode, recorded outside the mod's own calls
    uint8_t game_lock;    // its EMouseLockMode (UIOnly / GameAndUI)
    uint8_t game_flag;    // GameAndUI bHideCursorDuringCapture / GameOnly bConsumeCaptureMouseDown
    bool shown;
    bool focus_pending;   // focus the box one tick after it is shown (its Slate widget is then arranged)
    bool own_mode_call;   // true while the mod sets an input mode itself
    uint8_t keys;         // bit per kC31Keys entry: down at the previous poll
    uint32_t logs;        // capped per-event lines
    uint32_t key_logs;
    uint32_t worlds;
};
extern config::OverlayTest g_c31_mode;
extern C31State g_c31;
bool c31_line();
void c31_weak_set(C31Weak& w, void* object);
void* c31_weak_get(const C31Weak& w);
bool c392_kept_npc(const void* actor); // card 528: an NPC carried out of the left map by seamless travel (client)
// Card 368: the live actors of one class, for the steps that read the same few actors many times a second. The level
// walk (c23_collect, every actor of the map) runs once per kC368WalkMs for the class and on a world change; the calls
// between read the same actors through weak refs. An actor's first sighting can be up to one period late; a destroyed
// actor drops out at once. One cache per class, shared by every reader of that class.
constexpr ULONGLONG kC368WalkMs = 250;
struct C368Cache {
    C31Weak weak[kC23Max];
    uint32_t count;
    const void* world;  // identity only: the world the refs were collected in
    ULONGLONG next_ms;  // when the next level walk is due
};
extern C368Cache g_c368_players; // g_player_character_class()
extern C368Cache g_c368_turrets; // g_turret_class()
// The live actors into out[0..], in level order; returns how many.
uint32_t c368_actors(C368Cache& cache, const void* world, const void* cls, void* out[kC23Max]);
void* c31_construct(void* cls, void* outer);
void* c31_find_widget_class();
void* c31_object_prop(const void* object, const char* name);
void* c31_root_canvas(void* tree);
void c31_set_text(void* text_block, const wchar_t* text);
void* c31_text_overlay(void* pc, const wchar_t* text, double x, double y); // card 345b (lobby_ui.cpp)
// --- Card 110: a local marker over each other player (ubw/player_marker.cpp) ---
// FVector by value goes by pointer (24 bytes); FVector2D& out; bViewportRelative in r9.
using ProjectToScreenFn = bool (*)(const void* pc, const double* world, double* screen, bool viewport_relative);
using ViewportScaleFn = float (*)(const void* world_context); // UWidgetLayoutLibrary::GetViewportScale(UObject*)
extern ProjectToScreenFn g_project_to_screen;
extern ViewportScaleFn g_viewport_scale;
void c110_step(void* engine);
// --- Card 416: teammate minimap blip and ground ring (ubw/player_marker.cpp) ---
// The five ISTMinimapMarkerInterface static wrappers (hooked). GetWorldLocation returns its FVector through a hidden
// pointer (rcx = out, rdx = object, rax = out); GetWorldYaw returns a float in xmm0.
using C416IconFn = void* (*)(const void* object);
using C416LocationFn = Vec3* (*)(Vec3* out, const void* object);
using C416YawFn = float (*)(const void* object);
using C416BoolFn = bool (*)(const void* object);
// Called: USTMinimapComponent::ShowOnMinimap; UUserWidget::SetColorAndOpacity (FLinearColor by pointer);
// UPrimitiveComponent::CreateDynamicMaterialInstance (FName by value); UMaterialInstanceDynamic::SetVectorParameterValue
// (FName by value, FLinearColor by pointer); UStaticMeshComponent::SetStaticMesh.
using C416ShowFn = void (*)(void* minimap, const void* actor);
using C416ColorFn = void (*)(void* widget, float* rgba);
using C416CreateMidFn = void* (*)(void* component, int32_t element, void* material, uint64_t name);
using C416VectorParamFn = void (*)(void* mid, uint64_t name, float* rgba);
using C416SetMeshFn = bool (*)(void* component, void* mesh);
extern C416IconFn g_orig_c416_icon;
extern C416LocationFn g_orig_c416_location;
extern C416YawFn g_orig_c416_yaw;
extern C416BoolFn g_orig_c416_edge;
extern C416BoolFn g_orig_c416_show;
extern C416ShowFn g_c416_show_on_minimap;
extern C416ColorFn g_c416_set_color;
extern C416CreateMidFn g_c416_create_mid;
extern C416VectorParamFn g_c416_set_vector;
extern C416SetMeshFn g_c416_set_mesh;
void* detour_c416_icon(const void* object);
Vec3* detour_c416_location(Vec3* out, const void* object);
float detour_c416_yaw(const void* object);
bool detour_c416_edge(const void* object);
bool detour_c416_show(const void* object);
constexpr uint32_t kC41Npcs = 32;
constexpr uint32_t kC41Seqs = 64;
constexpr ULONGLONG kC41DumpMs = 30000;
enum C41Cb : uint32_t { kC41Frame, kC41Ended, kC41AttackEnd, kC41ActionEnd, kC41Cbs };
using TryPlaySeqFn = void (*)(void* npc, uint64_t name, bool force); // FName by value
using TryPlayTimedFn = void (*)(void* npc, uint64_t name, float time);
using NpcCallbackFn = void (*)(void* npc);
using AnimEndFn = void (*)(void* self, uint8_t reason); // EAnimationSequenceEnd
using GetNetGuidFn = uint64_t* (*)(const void* map, uint64_t* out, const void* object); // FNetworkGUID by hidden ptr
using GetObjectFromGuidFn = void* (*)(void* map, const uint64_t* guid, bool ignore_must_be_mapped);
extern TryPlaySeqFn g_orig_c41_try_play;
extern TryPlayTimedFn g_orig_c41_try_timed;
extern NpcCallbackFn g_orig_c41_frame;
extern NpcCallbackFn g_orig_c41_ended;
extern AnimEndFn g_orig_c41_attack_end;
extern AnimEndFn g_orig_c41_action_end;
extern GetNetGuidFn g_get_net_guid;
extern GetObjectFromGuidFn g_object_from_guid;
extern bool g_c41_trace;
struct C41Npc {
    C38Dead npc;        // identity only
    uint32_t listeners; // signature of the last logged listener sets
};
struct C41Seq {
    uint64_t name; // FName value
    uint32_t calls;  // TryPlayAnimationSequence
    uint32_t timed;  // TryPlayAnimationAgumentedByTime
    uint32_t forced; // force true
    uint32_t same;   // force false and already current: the original returns without a restart
};
struct C41State {
    C41Npc npc[kC41Npcs];
    uint32_t npc_count;
    C41Seq seq[kC41Seqs];
    uint32_t seq_count;
    uint32_t seq_dropped;
    uint32_t cb[kC41Cbs];
    uint32_t cb_lines[kC41Cbs];
    uint32_t lines;
    uint32_t listener_lines;
    ULONGLONG next_dump_ms;
};
extern C41State g_c41;
void c41_relay_arm_map();
void c41_relay_totals(const char* when); // card 041/436: the Anim relay totals rows (map totals, quit totals)
void detour_c41_try_play(void* npc, uint64_t name, bool force);
void detour_c41_try_timed(void* npc, uint64_t name, float time);
void detour_c41_frame(void* npc);
void detour_c41_ended(void* npc);
void detour_c41_attack_end(void* component, uint8_t reason);
void detour_c41_action_end(void* action, uint8_t reason);
void c41_dump(const char* when, const void* world);
// Card 042. Gib signals name the NPC by NetGUID ({guid low 14 bits, guid high 14 bits, cell}; both GUID args 0 =
// no usable GUID, the cell route of card 039). The client spawns the gore chunks (the parts SpawnGoreResponse only
// spawns behind its GameMode gate) from its own poolers, built the way ASTGameplayGameMode::DeferredBeginPlay builds
// the host's. UBWCOOP_C42_TEST drives a corpse walk test and two close overkills.
constexpr uint32_t kC42Kinds = 6; // DeferredBeginPlay order: Sticky {Sprite, StaticMesh, SkeletalMesh}, then NonSticky
// Card 201: pre-allocated actors per client pool, the host's config count (100; card 042 capped it at 24, so the
// client recycled pieces the host still showed). The game's own pool actors, built per kind on first use.
constexpr int32_t kC42PoolMax = 100;
using C42InitFn = void (*)(void* pooler, void* part_cls, int32_t count, void* owner);
extern StaticClassFn g_c42_pooler_cls[kC42Kinds];
extern StaticClassFn g_c42_part_cls[kC42Kinds];
extern StaticClassFn g_c42_data_cls[kC42Kinds];
extern C42InitFn g_pooler_init;
// --- Card 043: downed and revive (first pass) -----------------------------------------------------
// Host-authoritative. A player is Up, Downed (kC43BleedS of bleedout) or Out; only players that are not Up are in
// g_c43.e, plus a revived player for kC43StandUpMs (the remote sprite's StandUp). Gate: the host (NM_ListenServer)
// with more than one player; single player and standalone stay vanilla. HandleDeathEvent on a player while another
// is Up is skipped: Health goes back to 1, the player's abilities are cancelled and it is Downed. Damage while
// Downed drains the bleedout (1 s per kC43DamagePerS). An Up teammate within kC43ReviveRange holding interact for
// kC43HoldDownedMs (kC43HoldOutMs from Out) revives it at kC43ReviveFraction of MaxHealth. With nobody Up, the
// host's own pawn runs the real death (game over); a server copy never runs StartDeath. The client learns the states
// from ChanOp::Down frames and sends its interact edges as ChanOp::Use. Card 209: every Down frame names its pawn by
// NetGUID, and client entries are keyed by that GUID, so a third or fourth player's state reaches every client.
constexpr float kC43BleedS = 30.0f;
constexpr float kC43ReviveFraction = 0.3f; // a revive's Health, as a fraction of MaxHealth (card 143 load rule too)
constexpr ULONGLONG kC43StandUpMs = 1200;
constexpr int32_t kC43Max = kC17Slots; // entries and holds
constexpr uint32_t kC43RefuseLines = 40;
constexpr size_t kCharacterAsc = 0x680;             // ASTCharacter's AbilitySystemComponent (as kNpcAsc)
enum class C43Life : uint8_t { Up, Downed, Out, Dead, Count };
constexpr const char* kC43LifeText[] = {"Up", "Downed", "Out", "Dead"};
struct C43Entry {
    int32_t pawn_id;    // host: the pawn's InternalIndex; client: its NetGUID (below 2^28, card 209)
    uint32_t slot;      // host PlayerController index
    C43Life state;      // Up only inside the StandUp window
    float secs_left;    // bleedout
    float saved_walk;   // MaxWalkSpeed before Downed, restored on revive
    ULONGLONG down_ms;  // Downed started (sprite Death, then Corpse)
    ULONGLONG up_ms;    // revived; the entry leaves kC43StandUpMs later
    int32_t shown_secs; // the whole seconds last broadcast
    int32_t prio_state = -1; // card 294: the state of the last logged priority send (bleedout seconds repeat it)
    uint64_t guid = 0;       // card 509, host: the pawn's NetGUID, stored at each transition while connections live
};
struct C43Hold {
    int32_t reviver_id;
    int32_t target_id;
    ULONGLONG start_ms;
};
struct C43Counts {
    uint32_t down_sent, down_recv, use_sent, use_recv, transitions;
};
struct C43State {
    C43Entry e[kC43Max];
    int32_t count;
    C43Hold hold[kC43Max];
    int32_t holds;
    C43Counts n;
    uint64_t fire_tag; // FName values
    uint64_t walk_tag;
    ULONGLONG last_ms;  // host: the last tick, for the bleedout
    ULONGLONG kill_ms;  // host: the next real-death hit on the host's pawn; 0 = none pending
    uint32_t kill_tries;
    int32_t max_offset; // MaxHealth in the health set; valid when max_known
    uint32_t lines;
    uint32_t sec_lines;
    uint32_t refuse_lines;
    uint32_t down_warns; // card 209: Down rows not sent or not stored
    uint32_t prio_lines; // card 294: Down priority-send rows
    bool max_known;
    bool all_down;
    bool gate_logged;
    bool own_held;    // this machine's interact is held (edge dedupe)
    bool client_seen; // client: a Down frame arrived this map
    bool in_reset;    // inside c43_set_health
    bool in_kill;     // inside c43_kill_host's real-death hit (card 196: the test god lets it land)
    bool view_down;   // this machine's own downed view (camera lowered) is applied
    int32_t view_pawn; // the pawn whose camera the downed view lowered (a respawn replaces it)
    float view_walk;  // client: MaxWalkSpeed saved by the own downed view
    bool death_pending; // client (card 044): the host's pawn went Dead; the own pawn's real death runs next tick
    bool death_done;    // client: the own pawn's real StartDeath has run this map
    bool death_pass;    // client: inside that call; the StartDeath detours let it through
};
extern C43State g_c43;
using CancelAllAbilitiesFn = void (*)(void* asc, void* ignore);
using IsAutoSaveEnabledFn = bool (*)(const void* tracker);
using AddLocalOffsetFn = void (*)(void* component, const Vec3* delta, bool sweep, void* out_hit, uint8_t teleport);
extern CancelAllAbilitiesFn g_cancel_all_abilities;
extern IsAutoSaveEnabledFn g_is_auto_save_enabled;
extern AddLocalOffsetFn g_add_local_offset;
extern InteractFn g_orig_release_interaction;
// The PlayerControllers of this tick by slot, with their pawns (any class) and the pawns' ids.
struct C43Player {
    void* pc;
    void* pawn;
    int32_t id;
};
struct C43Roster {
    C43Player p[kC43Max];
    uint32_t players; // PlayerControllers present
};
bool c43_down_state(C43Life s);
int32_t c43_find(int32_t id);
void c43_remove(int32_t i);
void c43_remove_hold(int32_t h);
int32_t c43_find_hold(int32_t reviver_id);
C43Roster c43_roster(const void* world);
int32_t c43_slot(const C43Roster& r, int32_t id);
void c43_set_health(const void* pawn, float value);
void c43_set_component_health(const void* component, float value, const void* owner);
float c43_max_health(const void* component);
float c43_walk(const void* pawn);
void c43_set_walk(const void* pawn, float value);
int c43_autosave(const void* context);
void c43_release_own_fire(const void* pawn);
void c43_own_view(void* pawn, bool down);
void c43_stand_all(void* pc);
void c43_broadcast(const C43Roster& r, C43Entry& e);
void c43_transition(const C43Roster& r, C43Entry& e, const void* pawn, const char* why);
void c43_enter_down(void* pawn, const C43Roster& r, uint32_t slot, C43Life state, ULONGLONG now);
void c43_all_down(const C43Roster& r, const void* host, const char* why);
void c43_use_edge(void* reviver, bool pressed);
void c43_totals(const char* what);
void c43_revive(const C43Roster& r, C43Entry& e, void* pawn, ULONGLONG held_ms, ULONGLONG now);
// Card 130 item 14: host AI troopers pick up downed players and dead trooper NPCs (death.cpp).
using NpcBoolFn = bool (*)(const void* npc);
extern NpcBoolFn g_c130_overkilled;
// FVector by value: MSVC x64 passes the 24-byte struct as a pointer to a caller copy.
using NpcForcedMoveFn = void (*)(void* npc, const Vec3* location, float acceptance_radius);
extern NpcBoolFn g_c130_is_aware, g_c130_forced_pending, g_c130_forced_moving;
extern NpcVoidFn g_c130_become_aware, g_c130_end_forced;
extern NpcVoidFn g_c130_stop_aware, g_c130_start_aware, g_c130_begin_forced;
extern NpcForcedMoveFn g_c130_set_forced_move;
// FString by value: MSVC x64 returns it through a caller buffer, freed with FMemory::Free.
using SmStateNameFn = FStringRaw* (*)(const void* instance, FStringRaw* out);
extern SmStateNameFn g_c130_sm_state_name;
using AttackBoolFn = bool (*)(const void* attack_component);
using AttackAbortFn = bool (*)(void* attack_component, bool force);
extern StaticClassFn g_c130_attack_class; // USTAttackComponent
extern AttackBoolFn g_c130_is_attacking;
extern AttackAbortFn g_c130_abort_attack;
void c130_pickup_step(void* world, const C43Roster& r, ULONGLONG now);
void c130_pickup_map();
constexpr size_t kC44ParmsBytes = 0x100;        // zeroed parameters for the restart button's bound function
constexpr ULONGLONG kC44WindowMs = 30000;
constexpr uint32_t kC44ContextLines = 12;
constexpr int kC44MaxSupers = 8;
constexpr int kC44MaxFields = 256;
constexpr int32_t kC44MaxKids = 32;
struct C44State {
    ULONGLONG start_ms;    // client: the own pawn's real StartDeath ran
    ULONGLONG complete_ms; // this machine's last CompleteDeath
    ULONGLONG window_ms;   // UI input contexts are logged for kC44WindowMs from here
    ULONGLONG done_ms;     // client test: the run ends
    uint64_t construct;    // FName "Construct"
    uint64_t screen_weak;  // FWeakObjectPtr: the last death screen
    uint64_t restart_fn;   // FName: the RestartMission button's OnClicked function on the screen
    uint64_t checkpoint_fn; // FName (card 045): the LoadLastSave button's OnClicked function on the screen
    uint32_t lines;
    uint32_t context_lines;
    uint32_t complete_lines;
    bool fallback_done;
    bool test_over; // the test drivers stop (host: restart driven; client: done)
};
extern C44State g_c44;
struct C44Kid {
    void* obj;
    ObjectName name; // the property holding it
};
int32_t c44_kids(const void* obj, C44Kid* out);
bool c44_press(uint64_t fn_name_value, const char* button);
extern uint32_t g_c45_phase;
void c45_test_on_foot(const C43Roster& r, ULONGLONG now, bool solo);
void c45_test_restart(bool solo);
// --- Card 043 test driver (UBWCOOP_C43_TEST=1) ---
// Host: kill the server copy and revive it from Out, get downed and wait for the client's revive, then go down
// with the copy for game over. Client: hold interact on the downed host. Both sides queue the card's screenshots.
constexpr ULONGLONG kC43TestStartMs = 20000; // after both pawns are on foot (self-hits land nothing ~22 s)
constexpr int32_t kC43Shots = 8;
constexpr double kC44EyeUp = 60.0;          // the view above the camera pawn's actor location
constexpr double kC44FeetDown = 70.0;       // the target's feet below its actor location
constexpr ULONGLONG kC44AimLeadMs = 200;    // an aimed shot turns the view this long before it is taken
constexpr uint8_t kC44ShotUi = 1;  // shot showui
constexpr uint8_t kC44ShotAim = 2; // the view turns to the other player's pawn first
enum class C43Stage : uint8_t {
    WaitFoot, KillCopy, Drain, WaitOut, CheckHold, WaitCopyUp, KillHost, WaitHostUp, KillHost2, KillAll, Finish, Done,
    Count
};
struct C43Shot {
    ULONGLONG at_ms;
    const char* what;
    uint8_t flags; // kC44Shot*
};
struct C43Test {
    C43Stage stage;
    ULONGLONG at_ms; // the stage's next action or deadline
    uint32_t hits;
    C43Shot shot[kC43Shots];
    int32_t shots;
    bool key_held;
    ULONGLONG key_ms;        // client: the interact key went down
    ULONGLONG host_down_ms;  // client: the host's first Downed
    bool own_shot, host_shot, over_shot;
    uint32_t refuse_step;  // host (card 044): 0 none, 1 pressed, 2 released, 3 reviver in
    int32_t refuse_ammo;   // host: ammo at the press
    uint32_t refuse_fires; // host: host-own FireProjectile calls at the press
    uint32_t fire_step;    // client (card 044): relayed Fire frames sent
    ULONGLONG fire_ms;     // client: the next relayed Fire frame
    Vec3 view_spot;        // host: the copy's spot for the client's host-downed shots (a clear view)
    bool view_spot_set;
};
extern C43Test g_c43t;
void c43_shot(ULONGLONG at_ms, const char* what, uint8_t flags = 0);
void* c43_client_host_pawn(const void* world); // test drivers only (card 209 row 10)
// Card 209, client: every player pawn of sprint side "other" in one level scan, into out[0..]; the rest null.
int32_t c43_client_other_pawns(const void* world, void* out[kC43Max]);
int32_t c43_client_id(const void* pawn); // client: a player pawn's entry id (its NetGUID), -1 when it has none
// Card 209, client: a row whose pawn GUID does not resolve yet is held (last value wins per op and GUID) and applied
// from the client tick once it does; the table is dropped at map change.
enum class C209Op : uint8_t { BugForm, Warp, Resupply, Count }; // Resupply: card 502 finding 3, a card491 U row
void c209_hold(C209Op op, uint64_t guid, uint32_t value);
// Card 502 finding 3 (b263), client: a held card491 U row's level, applied once its pawn resolves (score_c475.cpp).
void c491_level_apply(void* pawn, uint64_t guid, int32_t level);
void c209_step(void* pc);
void c209_arm_map();
double c44_aim(void* pc, const Vec3& eye, const Vec3& target);
void* c44_other_pawn(const void* world);
void c44_aim_other(const void* world, void* pc);
void c43_shot_step(const void* world, void* pc, ULONGLONG now);
C43Life c43_life(const void* pawn);
void c43_next(C43Stage stage, ULONGLONG at_ms);
// --- Card 045: co-op restart after game over ---
// Browse logs every URL (FURL layout checked once with FURL's own ctor). A listen server with 2+ players that
// Browses to a local map without `listen` (the death screen's restarts) gets `listen` added to the URL copy in place,
// with the engine allocator, so LoadMap listens again. A client whose session drops (a local Browse while
// g_client_session) waits for that load, then SetClientTravel to its UBWCOOP_TRAVEL address every kC45RetryMs for
// up to kC45CapMs until a pending LoadMap succeeds. The client's death screen has its restart and load buttons
// disabled. Game thread only, like Browse, LoadMap and Tick.
using FurlCtorFn = void* (*)(void* self, void* base, const wchar_t* text, int travel_type);
using FurlDtorFn = void (*)(void* self);
using WidgetSetIsEnabledFn = void (*)(void* widget, bool enabled);
extern FurlCtorFn g_furl_ctor;
extern FurlDtorFn g_furl_dtor;
extern WidgetSetIsEnabledFn g_widget_set_is_enabled;
// FURL: Protocol, Host (FString), Port, Valid (int32), Map, RedirectURL (FString), Op (TArray<FString>), Portal.
constexpr size_t kFurlProtocol = 0x00;
constexpr size_t kFurlHost = 0x10;
constexpr size_t kFurlPort = 0x20;
constexpr size_t kFurlValid = 0x24;
constexpr size_t kFurlMap = 0x28;
constexpr size_t kFurlOp = 0x48;
constexpr size_t kFurlPortal = 0x58;
constexpr int32_t kC45MaxOps = 32;
constexpr uint32_t kC45UrlLines = 120;
constexpr ULONGLONG kC45PendingMs = 90000;  // backstop for a Pending try with no LoadMap return and no failure (engine connect timeout 60 s, plus the map load)
constexpr ULONGLONG kC45ShotMs = 10000;     // client test: the showui shot after a reconnect
constexpr ULONGLONG kC45ClientDoneMs = 20000; // client test: done this long after the second reconnect
constexpr ULONGLONG kC45HostDoneMs = 6000;  // host test: done this long after the last phase's on foot
constexpr size_t kC45MapChars = 128;
enum class C45Follow : uint8_t { Off, Armed, Retrying, Pending };
constexpr int32_t kC45Screens = 4; // client death screens kept per map (game over, checkpoint, spare)
struct C45State {
    int8_t layout;       // 0 unchecked, 1 verified, -1 failed: URLs are neither logged nor changed
    C45Follow follow;
    bool ours;           // the follow's SetClientTravel ran: the next Browse is its try
    bool this_ours;      // the Browse in flight is the follow's try
    uint32_t tries;
    uint32_t reconnects;
    uint32_t url_lines;
    ULONGLONG drop_ms;   // client: the session dropped
    ULONGLONG landed_ms; // client: the local map after the drop loaded; tries start
    ULONGLONG next_ms;   // client: the next try
    ULONGLONG done_ms;   // test: "card045: <side> test done" is logged then
    void* pawn;          // the local PC's last pawn; a new one after a game over is a respawn in place
    uint64_t screens[kC45Screens]; // client: FWeakObjectPtr of each death screen this map
    int32_t screen_count;
    wchar_t landing[kC45MapChars]; // client: the drop's local map
};
extern C45State g_c45;
struct FArrayRaw {
    FStringRaw* data;
    int32_t num;
    int32_t max;
};
int fs_len(const void* fs);
const wchar_t* fs_text(const void* fs);
bool c45_has_option(const uint8_t* url, const wchar_t* key);
void c45_log_url(const char* what, const uint8_t* url);
void c45_check_layout();
bool c45_add_option(uint8_t* url, const wchar_t* text);
void c45_host_browse(void* engine, uint8_t* url);
void c45_client_browse(const uint8_t* url);
int c45_is_enabled(const void* widget);
void c45_remove_screens(void* pc, ULONGLONG now);
// --- Card 052: co-op lobby panel at the main menu ---
// --- Card 052: the in-game co-op lobby panel ---
// The card 031 overlay, on by default at the main menu (3D_Frontend, net mode 0). Host mode adds `listen` to the
// menu's own mission Browse; a typed address joins through the env-var client path (travel_step); a failed join is
// shown at the menu. The address and the host flag persist in UBWCoop.ini (lobby.cpp). Game thread only.
using C52WrapperFn = void (*)(void* object, const void* value); // UFUNCTION native wrapper: value by const&
using FTextDtorFn = void (*)(void* text);
using TransitionToLevelFn = void (*)(void* game_instance, int32_t chapter, int32_t level, FStringRaw* options);
using NetworkFailureFn = void (*)(void* engine, void* world, void* driver, int32_t type, const void* message);
using TravelFailureFn = void (*)(void* engine, void* world, int32_t type, const void* message);
extern StaticClassFn g_border_class;
extern StaticClassFn g_vbox_class;
extern StaticClassFn g_sizebox_class;
extern C52WrapperFn g_border_set_brush_color;
extern C52WrapperFn g_border_set_padding;
extern C52WrapperFn g_sizebox_set_width;
extern C52WrapperFn g_sizebox_set_height;
extern C52WrapperFn g_box_set_text;
extern FTextDtorFn g_ftext_dtor;
extern TransitionToLevelFn g_transition_to_level; // the original (card 381 hooks it)
// Card 381 (restart.cpp): a client in the host's session does not run the game's own level transitions.
using TransitionToLatestFn = void (*)(void* game_instance, FStringRaw* options);
extern TransitionToLatestFn g_orig_c381_latest;
void detour_c381_transition(void* game_instance, int32_t chapter, int32_t level, FStringRaw* options);
void detour_c381_transition_latest(void* game_instance, FStringRaw* options);
// --- Card 350: the card 344 return and the client's frontend options (menu_c350.cpp) ---
// UGameplayStatics::OpenLevel (0x03C62660): FName by value (8 bytes), FString by value (a pointer to a caller copy the
// callee frees: engine allocator). HasOption (0x03C594B0) and ParseOption (0x03C62910) take Options by value the same
// way (ParseOption returns its FString through a caller buffer). ClientTravelInternal_Implementation (0x0403D540):
// FGuid by value is a pointer to a caller copy.
using OpenLevelFn = void (*)(const void* context, uint64_t level_name, bool absolute, FStringRaw* options);
using HasOptionFn = bool (*)(FStringRaw* options, const FStringRaw* key);
using ParseOptionFn = FStringRaw* (*)(FStringRaw* out, FStringRaw* options, const FStringRaw* key);
using ClientTravelInternalFn = void (*)(void* pc, const FStringRaw* url, int32_t type, bool seamless, const void* guid);
extern OpenLevelFn g_c350_open_level;
extern HasOptionFn g_orig_c350_has_option;
extern ParseOptionFn g_orig_c350_parse_option;
extern ClientTravelInternalFn g_orig_c350_client_travel;
bool detour_c350_has_option(FStringRaw* options, const FStringRaw* key);
FStringRaw* detour_c350_parse_option(FStringRaw* out, FStringRaw* options, const FStringRaw* key);
void detour_c350_client_travel(void* pc, const FStringRaw* url, int32_t type, bool seamless, const void* guid);
// Host: OpenLevel to the frontend with FromLevelQuit?ChapterID=?LevelID= (the pause menu QUIT's own options). False
// (WARN) when a symbol is unresolved and nothing ran.
bool c350_host_return_to_menu(void* context, const void* game_instance);
void c350_client_session_end(); // clears the stored frontend options where g_client_session goes false
// Card 344: host. The card 320 hold timed out and the host went back to the menu; the card 052 panel says why.
void c52_note_no_confirm();
// Card 132 point 1: USTSaveSystemLibrary::LoadLatestSave (0x0520B350), the load RESTART CHECKPOINT makes in place.
using LoadLatestSaveFn = bool (*)(void* world_context);
extern LoadLatestSaveFn g_load_latest_save;
// Card 143 test: USTSaveSystemLibrary::Save_Quicksave, called by `travel save` (c143_quicksave).
using SaveQuicksaveFn = void (*)(void* world_context);
extern SaveQuicksaveFn g_save_quicksave;
const char* c143_quicksave(void* world);
// Card 132: ASTGameplayGameMode::UpdateSignificanceManager (0x051408C0). The game mode's Tick tail-calls it every
// frame (exe+0x513AA6E), and the game mode exists only on the host, so a client's significance manager never updates.
// Its body takes only the world (AActor::GetWorld) and the local player controller from `this`, so the client calls
// it with its local PC as `this`.
using UpdateSignificanceFn = void (*)(void* actor);
extern UpdateSignificanceFn g_update_significance;
// USignificanceManager::Get (0x04BBC630): the world's significance manager, or null.
using SignificanceGetFn = void* (*)(const void* world);
extern SignificanceGetFn g_significance_get;
// ASTPlayerController::AddSignificanceLocation (0x051BCD00): appends to the PC's significance location array
// (TArray<FTransform, TInlineAllocator<4>> at +0x8E0, Num +0xA70); false when 4 are held.
using AddSignificanceLocationFn = bool (*)(void* pc, const Vec3* location);
extern AddSignificanceLocationFn g_add_significance_location;
void client_significance_step(void* engine);
// Host: the same function, hooked. Its significance views are the host PC's camera and AddSignificanceLocation
// entries only, so NPCs near a client but far from the host ran at low significance (longer actor and movement tick
// intervals, static mesh animation). The detour adds each remote pawn's location for the call.
void detour_c132_update_significance(void* game_mode);
// Card 321 (b224, diagnostic): ASTGameplayGameMode::Tick_PlayerCombatTracker (0x0513DAF0). After it runs, the
// combat level byte at game mode +0x460 (0/1/2, drives the music) gets one row per change. The game mode exists only
// on the host, so a client row would mean a client game mode.
using CombatTrackerTickFn = void (*)(void* game_mode, float dt);
extern CombatTrackerTickFn g_orig_c321_combat_tick;
void detour_c321_combat_tick(void* game_mode, float dt);
using MusicTickFn = void (*)(void* music, float dt);
extern MusicTickFn g_orig_c321_music_tick;
void detour_c321_music_tick(void* music, float dt);
using MusicPlayFn = void (*)(void* music);
extern MusicPlayFn g_c321_set_and_play;
using MusicAutoFn = void (*)(void* music, bool flag);
extern MusicAutoFn g_c321_set_to_auto; // card 321 b227
using MusicTriggerFn = void (*)(void* music, uint8_t type, float value, bool flag);
extern MusicTriggerFn g_c321_trigger_music; // card 321 b228
void c321_level_send(void* pc);                    // host: the combat level to one client PC (join replay)
void c321_level_receive(void* pc, uint32_t level); // client: ChanOp::CombatLevel
extern uint32_t g_c491_window; // card 491 b227: depth of the subsystem ObjectiveComplete score window
// Diagnostic (UBWCOOP_C41_ANIM_TRACE): ASTNonPlayableCharacterBase::HandleDamageEvent (0x05192430), the path to
// USTGoreComponent::OnDamage and UADWoundWorldSubsystem::AddWound. One line per call: time, NPC, caller.
using NpcDamageFn = void (*)(void* npc, const void* damage_event);
extern NpcDamageFn g_orig_c132_npc_damage;
void detour_c132_npc_damage(void* npc, const void* damage_event);
// Card 132 hit blood (hit_c132.cpp): USTGoreComponent::OnDamage, host only; the host relays each hit, the client calls
// the original with an event rebuilt from it.
using GoreDamageFn = void (*)(void* gore, const void* damage_event);
extern GoreDamageFn g_orig_c132_gore_damage;
void detour_c132_gore_damage(void* gore, const void* damage_event);
void c132_hit_step(void* engine);                        // host, per tick: flush the tick's hits
void c132_receive_hit(void* pc, const WireString* s);    // client: ChanOp::Hit
void c132_hit_arm_map();                                 // per map, from c98_arm_map
bool c132_rep_location(const void* character, Vec3* out); // client: ReplicatedMovement.Location, false if unresolved
// Client: a collision write on the mesh of an NPC whose card 038 death edge ran, logged with its caller (hb40 pose gate).
void c132_corpse_collision(const void* owner, const void* component, unsigned before, unsigned after,
                           unsigned long long caller);
// Card 132 (pose_c132.cpp): the corpse pose journal.
using PoseBodyWorldFn = void* (*)(const void* body, void* out_transform, bool with_projection, bool force_global_pose);
using PoseBodySetFn = void (*)(void* body, const void* transform, int32_t teleport, bool auto_wake);
using PoseMeshAwakeFn = bool (*)(void* mesh);                       // USkeletalMeshComponent::IsAnyRigidBodyAwake
using PoseMeshVoidFn = void (*)(void* mesh);                        // USkeletalMeshComponent::PutAllRigidBodiesToSleep
using PoseMeshVelFn = void (*)(void* mesh, const Vec3* v, bool add); // SetAllPhysicsLinear/AngularVelocity(InRadians)
using PoseMeshGravityFn = void (*)(void* mesh, bool on);             // USkeletalMeshComponent::SetEnableGravity
extern PoseBodyWorldFn g_pose_body_world;  // FBodyInstance::GetUnrealWorldTransform
extern PoseBodySetFn g_pose_body_set;      // FBodyInstance::SetBodyTransform
extern PoseMeshAwakeFn g_pose_mesh_awake;
extern PoseMeshVoidFn g_pose_mesh_sleep;
extern PoseMeshVelFn g_pose_mesh_linear;
extern PoseMeshVelFn g_pose_mesh_angular;
extern PoseMeshGravityFn g_pose_mesh_gravity; // card 152 r2: a streamed copy simulates with gravity off
void c132_pose_arrival(void* npc);                     // card 137, client: a copy first seen dead (no death play)
void c177_sprite_death_host(void* npc); // card 177, host: a sprite NPC death row (pose_c132.cpp)
// Card 269 (pose_c132.cpp), host: a 3D NPC's death name at TryPlayDeathAnimation; its base state changes (a rest row
// when it reaches Dead with no pose row).
void c269_death_note(void* npc);
bool c291_death_edge(void* npc, bool play); // card 291 (b187), client: edge time; true: the host's death row came first and was played
void c269_state_host(void* npc, uint8_t new_state);
void c207_shown_corpse(void* npc, const void* mesh); // card 207: pose_c132.cpp
void c506_dead_note(void* npc, const char* path); // card 506: pose_c132.cpp, host: a 3D NPC dead by health
using BaseStateChangedFn = void (*)(void* npc, uint8_t old_state, uint8_t new_state); // ESTBaseNPCState bytes
extern BaseStateChangedFn g_orig_c269_state;
void detour_c269_state(void* npc, uint8_t old_state, uint8_t new_state);
uint64_t c41_current(const void* npc); // the sequence the NPC's player is on now, 0 when none (enemies.cpp)
constexpr size_t kNpcCurrentHealth = 0xAC0; // ASTNonPlayableCharacterBase::CurrentHealth (float; enemies.cpp, card 177)
// Card 192: true when `pawn` is an ASTPlayerCharacterMI (class check, no MI field read). False for null; any other
// pawn class (BP_PlayerCharacterBug_C on a bug-side map) logs once per map per feature that it skipped (remote_body.cpp).
bool c192_mi_pawn(const void* pawn, const char* feature);
void c192_arm_map(); // card 192: the skip lines' totals, then the per-map table is emptied (from c132_pose_map_end)
void c38_corpse_death_play(void* actor);               // card 137, client: the death visual a held corpse skipped
void c132_pose_track(void* mesh);                        // host: a ragdoll start (detour_c132_set_simulate)
void c132_pose_sim_change(void* mesh, bool simulate, int was_simulating);    // card 137: the game's SetAllBodiesSimulatePhysics
bool c132_pose_game_ragdoll(uint64_t guid, uintptr_t caller_rva); // card 146, client: true blocks the game's ragdoll start on a posed copy
bool c207_pose_game_unfreeze(void* mesh, uintptr_t caller_rva); // card 207 step 3, client: true blocks the game's SetAllBodiesSimulatePhysics(1) on a rest-row corpse
bool c152_pose_game_start(void* mesh, uintptr_t caller_rva); // card 140, client: true blocks the game's SetAllBodiesSimulatePhysics(1) on a copy waiting for its stream row
bool c152_pose_game_freeze(void* mesh, uintptr_t caller_rva); // card 152 r3, client: true blocks the game's SetAllBodiesSimulatePhysics(0) on a streaming copy
void c132_pose_step(void* engine);                       // both, per tick: host samples and sends, client retries
void c132_receive_pose(void* pc, const WireString* s);   // client: ChanOp::Pose
void c132_pose_replay(void* pc);                         // host: a joining PC's first Pong
uint32_t c140_pose_host_check(uint32_t round);           // host, per digest round: sent corpses moved since the send
uint32_t c132_pose_check(uint32_t round);                // client, per digest round: mismatches
void c152_pose_wait_clear_all();                         // card 152, client: ragdoll wait table, map end
bool c132_ragdoll_root(const void* mesh, Vec3& out);     // ragdoll body 0 world position (pose_c132.cpp)
bool c132_posed_root(const void* npc, Vec3& out);        // client: body 0 of a host-posed (kinematic) corpse
// Where the player sees the NPC (enemies.cpp): the sprite renderer when drawn, else the ragdoll root body, else the mesh.
Vec3 c132_drawn_location(const void* npc, uint32_t* source);
const char* c132_off_shot_arm(); // enemies.cpp: the `offshot` command; null when armed
const char* c63_shot(const char* name); // testrun.cpp: a named game-window shot, <tag>-<name>.png; null when requested
void c39_hit_call(bool on);                              // client: a replayed host hit is running (enemies.cpp)
// Card 132 gore decals (decal_c132.cpp): the host relays every gore decal it spawns and releases; the client spawns
// only those and releases the exact copy the host released.
using DecalRemoveFn = void (*)(void* manager, void* decal);                          // USTDecalManager::RemoveDecal
using DecalSetIntFn = void (*)(void* decal, int32_t v);                              // UDecalComponent::SetSortOrder
using DecalSetFloatFn = void (*)(void* decal, float v);                              // SetFadeScreenSize, SetLifeSpan
using DecalFadeOutFn = void (*)(void* decal, float start, float duration, bool destroy_owner); // SetFadeOut
using DecalFadeInFn = void (*)(void* decal, float start, float duration);           // SetFadeIn
extern DecalRemoveFn g_decal_remove;
extern DecalSetIntFn g_decal_set_sort;
extern DecalSetFloatFn g_decal_set_screen;
extern DecalFadeOutFn g_decal_fade_out;
extern DecalFadeInFn g_decal_fade_in;
extern DecalSetFloatFn g_decal_life_span;
bool c132_decal_suppress(const void* material);          // client: its own gore decal spawn is dropped
void c132_decal_before(void* manager);                   // both, before a decal spawn: release rows, test cap
void c132_decal_spawned(void* manager, void* decal, void* material, const Vec3* size, void* component,
                        uint64_t socket, const Vec3* location, const Vec3* rotation, uint8_t attach, float life,
                        const void* caller);             // host, after a decal spawn (attach 255: SpawnDecalAtLocation)
void c132_decal_step(void* engine);                      // per tick: host flushes rows, client checks copies
void c132_receive_decal(void* pc, const WireString* s);  // client: ChanOp::Decal
void c132_decal_replay(void* pc);                        // host: a joining PC's first Pong
// Card 132 (dropship.cpp): troop dropship state relay, visuals only.
void c132_dropship_receive(void* pc, const WireString* s); // client: ChanOp::Dropship
void c132_dropship_replay(void* pc);                       // host: a joining PC's first Pong
void c159_dropship_step(void* engine);                       // card 159: host check rounds + L tracker, client hold
void c132_dropship_map_end();                              // card 200: both, at LoadMap enter
void c132_dropship_arm_map();                              // both: map start
void c159_dropship_report(const char* when);               // both: card 159 totals (map end, quit)
void c132_util_step(); // card 132 item 4a (grenades.cpp): frame time and live fallback count per window

// Card 252 step 1b (diagnostic, UBWCOOP_C132_FX only): QueryPerformanceCounter ticks per class per frame window, read
// and cleared by c132_util_step's frame window row. Classes nest (Chan holds FxApply, PoseApply and DigestRecv; Tick
// holds PoseStep and DigestStep); the row's "other" is Tick less PoseStep and DigestStep. log::line keeps its own count
// (ue::log::take_line_ticks) and overlaps every class.
enum class C252Class : uint8_t { Chan, FxApply, Audit, PoseApply, PoseStep, DigestRecv, DigestStep, Tick, Count };
extern int64_t g_c252_ticks[static_cast<size_t>(C252Class::Count)]; // grenades.cpp, game thread only

// Adds the QueryPerformanceCounter span of its scope to one class while the effect audit is on.
struct C252Timer {
    explicit C252Timer(C252Class c) : cls(c) {
        if (g_c132_fx_log) {
            LARGE_INTEGER t;
            QueryPerformanceCounter(&t);
            t0 = t.QuadPart;
        }
    }
    ~C252Timer() {
        if (t0 != 0) {
            LARGE_INTEGER t;
            QueryPerformanceCounter(&t);
            g_c252_ticks[static_cast<size_t>(cls)] += t.QuadPart - t0;
        }
    }
    C252Timer(const C252Timer&) = delete;
    C252Timer& operator=(const C252Timer&) = delete;
    C252Class cls;
    int64_t t0 = 0;
};
void c132_decal_round(uint32_t round);                   // host, digest round end: an R row
void c132_decal_respawn_totals();                        // card 309, host: totals at a respawn in place
// Card 132 diagnostic (net_budget.cpp): host replication bits per actor class and client connection saturation.
using ReplicateActorFn = int64_t (*)(void* channel); // int64 UActorChannel::ReplicateActor(), bits written
extern ReplicateActorFn g_orig_net_replicate_actor;
int64_t detour_net_replicate_actor(void* channel);
// void UNetDriver::ProcessRemoteFunctionForChannelPrivate(UActorChannel*, const FClassNetCache*,
//   const FFieldNetCache*, UObject*, UNetConnection*, UFunction*, void* Parms, FOutParmRec*, FFrame*, bool IsServer,
//   ERemoteFunctionSendPolicy, EProcessRemoteFunctionFlags&): every RPC sent on an actor channel.
using RemoteFunctionForChannelFn = void (*)(void* driver, void* channel, const void* class_cache,
                                            const void* field_cache, void* target, void* conn, void* function,
                                            void* parms, void* out_parms, void* stack, bool is_server,
                                            int32_t send_policy, void* flags);
extern RemoteFunctionForChannelFn g_orig_net_remote_function;
void detour_net_remote_function(void* driver, void* channel, const void* class_cache, const void* field_cache,
                                void* target, void* conn, void* function, void* parms, void* out_parms, void* stack,
                                bool is_server, int32_t send_policy, void* flags);
void net_budget_step(void* engine); // host, every frame
void net_budget_arm_map();
void c140_pace_step(void* engine);       // card 140, host: first in ubw_post_tick (gate sample, chan pacing)
void c151_flush_step();                  // card 151, host: last in ubw_post_tick (this tick's chan rows, batched)
bool c140_pace_live(const void* pc, ue::chan::PaceLive* out); // card 140, host: chan's live pace gate reading
void c140_pace_logout(const void* pc);  // card 140, host: Logout drops that PC's chan pace queue
extern NetworkFailureFn g_orig_handle_network_failure;
void c260_join_replay(void* pc); // card 290 part B, host: the map's EndLevel to a late joiner (objectives.cpp)
// Card 128: connection close, timeout and timeout-value hooks (net_drop.cpp).
using C128CloseFn = void (*)(void* conn, void* result);
using C128TimeoutFn = void (*)(void* conn, const void* error);
using C128TimeoutValueFn = float (*)(void* conn);
using C128LexFn = const wchar_t* (*)(uint32_t result);
extern C128CloseFn g_orig_c128_close;
extern C128TimeoutFn g_orig_c128_timeout;
extern C128TimeoutValueFn g_orig_c128_timeout_value;
extern C128LexFn g_c128_lex;
void detour_c128_close(void* conn, void* result);
void detour_c128_timeout(void* conn, const void* error);
float detour_c128_timeout_value(void* conn);
using C128CleanUpFn = void (*)(void* conn);
using C128KickFn = bool (*)(void* session, void* pc, const void* reason);
extern C128CleanUpFn g_orig_c128_cleanup;
extern C128KickFn g_orig_c128_kick;
void detour_c128_cleanup(void* conn);
bool detour_c128_kick(void* session, void* pc, const void* reason);
// Card 502 (3): host, every listen tick: once per world, log the GameSession's MaxPlayers and raise it to 4.
void c502_session_step(void* world);
// The object and UFunction of the latest UObject::ProcessEvent entry (set by detour_process_event): a reliable RPC
// that overflows its channel closes the connection inside that call, so the close names it.
// The innermost ProcessEvent in progress on this thread (thread-local: worker threads run thread-safe anim Blueprint
// updates through ProcessEvent, and a shared global let the game thread restore a worker's object after it was freed).
extern thread_local const void* g_c128_pe_object;
extern thread_local const void* g_c128_pe_function;
extern TravelFailureFn g_orig_handle_travel_failure;
constexpr size_t kC52ReasonChars = 48;
constexpr wchar_t kC52MenuMapW[] = L"3D_Frontend";
enum class C52Status : uint8_t { None, Connecting, Failed, Lost, Invalid, Cancelled, NoConfirm };
struct C52State {
    C31Weak widget;
    C31Weak info;   // UTextBlock: title, host line, join line
    C31Weak box;    // UEditableTextBox
    C31Weak status_line; // UTextBlock: the join status
    uintptr_t world_id;      // last game world seen: compared only, never dereferenced
    ULONGLONG world_ms;      // when it was first seen
    ULONGLONG arm_ms;        // the last join's arm
    ULONGLONG host_browse_ms; // the host-mode Browse that got `listen`; 0 once Listen returned
    lobby::Settings settings;
    lobby::Ips ips;
    upnp::State upnp_shown;  // card 153: the router lookup state the panel last drew
    uint16_t port;           // card 153: this host's listen port (-port=, else lobby::kDefaultPort)
    C52Status status;
    char reason[kC52ReasonChars];
    uint32_t shown_sec;      // Connecting: elapsed seconds on screen
    uint8_t keys;            // bit per C52Key: down at the previous poll
    bool at_frontend;        // this world is the main menu in net mode 0
    bool menu_map;           // this world is 3D_Frontend in any net mode (the client's hangar too)
    bool build_failed;       // this world: no retry
    bool user_hidden;        // F8
    bool typing;             // the box has focus
    bool focus_pending;      // focus the box on the next tick
    bool join_pending;       // armed, no LoadMap or failure yet
    bool env_travel;         // UBWCOOP_TRAVEL set: status only, keys off
    bool ips_read;
    bool dirty;
};
extern C52State g_c52;
extern config::C52Test g_c52_test;
void detour_handle_network_failure(void* engine, void* world, void* driver, int32_t type, const void* message);
void detour_handle_travel_failure(void* engine, void* world, int32_t type, const void* message);
// Card 290 part A: a host travel is not a failure. Host: c290_host_travel at its Browse (c45_browse_enter) sends a
// 'V' world row. Client: failures 3 (ConnectionLost) and 5 (FailureReceived) within kC290TravelMs of that row skip
// the game's failure dialog (UGameEngine::HandleNetworkFailure_NotifyGameInstance, which runs the game instance's
// HandleNetworkError event) and the card 052 panel's "Connection lost"; the engine's disconnect and the card 045
// follow run as before.
using C290NotifyFn = void (*)(void* engine, void* world, void* driver, int32_t type);
extern C290NotifyFn g_orig_c290_notify;
void detour_c290_notify(void* engine, void* world, void* driver, int32_t type);
void c290_host_travel(void* engine, const char* reason); // world_c98.cpp
bool c290_travel_failure(int32_t type);                    // world_c98.cpp: client, true = the host's travel
inline constexpr ULONGLONG kC290TravelMs = 10000;          // a failure this long after the travel row is the travel
// Card 294: one host restart is one client transition. Host: an in-place save load (LoadLastSave on the death
// screen, a quickload) sends the card 290 travel row on the next engine tick. Client: from that row until the new
// map, the death screen up stays the only one and a respawn in place in the old world is held.
void c294_host_load(const char* reason);                    // save_c121.cpp: host, at the save engine load
void c294_host_step(void* engine);                          // save_c121.cpp: notes the engine for c294_host_load
void c294_client_travel(const char* reason, ULONGLONG now); // death.cpp: client, the travel row arrived
bool c294_skip_complete_death(const void* pawn, unsigned long long caller); // death.cpp: client, true = skip
bool c294_hold_respawn(const void* pawn, const void* old, ULONGLONG now);    // death.cpp: client, true = held
void c294_shown(ULONGLONG now);                                              // death.cpp: client, respawn ran
// Card 295: a revive's MaxWalkSpeed from USTAttributeSet_Movement::WalkSpeed on both machines (death.cpp); the
// host's Up frame carries its speed in v3; the client counts ClientAdjustPosition for 10 s after its revive.
float c295_restore_walk(void* pawn, float saved, float host, const char* why); // -1 = no value, nothing written
float c295_host_value();
uint32_t c295_wire_walk(const void* pawn);
void c295_host_walk(uint32_t walk);
void c295_client_up(float set, ULONGLONG now, const void* pawn);
void c295_note_adjust(const void* cmc, const double* new_loc);
void c295_step(ULONGLONG now);
void c52_init(bool env_travel);
void c52_join(const wchar_t (&address)[lobby::kAddrChars]); // card 374: the `travel join` test command
// --- Card 052: null-component guard for SpawnSoundAttachedToComponentWithAIAwareness ---
// card052h: 12 s after the client followed the host into FED_C2M1 (card 045 reconnect), a Blueprint's
// movement-updated handler called this with a null USceneComponent. The function reads component+0x1E0 with
// no check (EXCEPTION_ACCESS_VIOLATION reading 0x1E0). With no component there is nothing to attach a sound
// to, so the guard returns null: the Blueprint gets None, as for any sound that did not spawn.
using SpawnSoundAttachedFn = void* (*)(void* component, uint64_t attach_point, uint64_t tag, Vec3 location,
                                       bool stop_when_destroyed, float volume, float pitch, float start_time,
                                       void* attenuation, void* concurrency, bool auto_destroy);
extern SpawnSoundAttachedFn g_orig_spawn_sound_attached;
void* detour_spawn_sound_attached(void* component, uint64_t attach_point, uint64_t tag, Vec3 location,
                                  bool stop_when_destroyed, float volume, float pitch, float start_time,
                                  void* attenuation, void* concurrency, bool auto_destroy);

// --- Card 117: null-mesh guard for UADSkeletalAnimatorComponent::StartAnimationSequence ---
// c117a: about 1 s after the client rejoined FED_C2M1 (card 045 reconnect), an RPC for a just-spawned actor reached
// this with the animator's skeletal mesh (+0xF8) still null. The function checks its animation data (+0xF0) but
// not the mesh, and GetAnimInstance read mesh+0x8F8 (EXCEPTION_ACCESS_VIOLATION reading 0x8F8). With no mesh there
// is nothing to play the montage on, so the guard skips the call.
using SkelAnimStartFn = void (*)(void* animator, uint64_t key, float rate);
extern SkelAnimStartFn g_orig_skel_anim_start;
void detour_skel_anim_start(void* animator, uint64_t key, float rate);
// --- Card 118: always-on crash trace (crash_trace.cpp) ---
// A vectored handler logs one block per fatal exception (access violation, illegal instruction, stack overflow,
// integer divide), at most four per run, and returns EXCEPTION_CONTINUE_SEARCH. UE's crash path is hooked to log
// its error text: ReportCrash (called from the GuardedMain __except filter) and FWindowsErrorOutputDevice::
// HandleError (shows the "Fatal error" dialog from GErrorHist).
void install_crash_trace(); // from init_after_resolve, once g_exe_base is set
void c118_describe_pc(uint64_t pc, char* out, size_t cap); // "exe+0x..", "ubwcoop+0x..", "<module>+0x.."
using ReportCrashFn = int (*)(EXCEPTION_POINTERS* info);
extern ReportCrashFn g_orig_report_crash;
int detour_report_crash(EXCEPTION_POINTERS* info);
using HandleErrorFn = void (*)(void* device);
extern HandleErrorFn g_orig_handle_error;
void detour_handle_error(void* device);
// --- Card 046: kill floor in co-op ---
// ASTPlayerCharacter::Tick asks IsBelowKillThreshold; on true (and latch +0x8F8 clear) it stops the CMC, zeroes its
// velocity, sets Flying through vtable +0x638, sets the latch and runs StartDeath (vtable +0xA40), which skips the
// card 043 downed flow. The detour runs the original first and changes only true results for player pawns:
// - host, card 043 gate, another player Up: the pawn moves beside that player (c46_side_spot), stops, takes
//   kC46DamageFraction of MaxHealth (DealDamage, so a low pawn goes Downed through HandleDeathEvent); false.
//   A server copy's client gets ChanOp::Warp (args: the target pawn's NetGUID, card 209, then the host's spot as
//   an offset from the target, card 461) and moves its own pawn to that spot.
// - host, nobody else Up: a server copy goes Out and moves to the host's pawn (false: it never runs StartDeath);
//   the host's own pawn runs the vanilla death after the card 043 all-down frames (true).
// - client: always false for player pawns (the host owns the outcome).
// NPCs (ASTNonPlayableCharacterBase::Tick) always get the original result. One warp per pawn per kC46CooldownMs.
using IsBelowKillThresholdFn = bool (*)(void* actor);
using GetChapterKillZFn = float (*)(void* game_instance);
using SetMovementModeFn = void (*)(void* cmc, uint8_t mode, uint8_t custom_mode);
extern SetMovementModeFn g_orig_set_movement_mode;
void detour_set_movement_mode(void* cmc, uint8_t mode, uint8_t custom_mode);
using SimulateMovementFn = void (*)(void* cmc, float delta);
extern SimulateMovementFn g_orig_simulate_movement;
void detour_simulate_movement(void* cmc, float delta);
extern IsBelowKillThresholdFn g_orig_is_below_kill_threshold;
extern GetChapterKillZFn g_get_chapter_kill_z;
constexpr double kC46WarpUp = 100.0;         // uu above the side spot when FindTeleportSpot finds no room there
constexpr double kC46RejoinSideUu = 150.0;   // a warped or rejoined pawn stands this far beside its target
constexpr int32_t kC46OffsetBias = 16384;    // card 461: a Warp offset axis arg is uu + this (15-bit arg)
constexpr double kC46OffsetMaxUu = 16383.0;  // card 461: a Warp offset axis is clamped to +- this
// Card 461: one Warp offset axis (uu, rounded and clamped) as a 15-bit arg, and back.
inline uint32_t c46_offset_arg(double uu) {
    const double c = uu < -kC46OffsetMaxUu ? -kC46OffsetMaxUu : uu > kC46OffsetMaxUu ? kC46OffsetMaxUu : uu;
    return static_cast<uint32_t>(static_cast<int32_t>(c < 0.0 ? c - 0.5 : c + 0.5) + kC46OffsetBias);
}
inline double c46_offset_uu(uint32_t arg) {
    return static_cast<double>(static_cast<int32_t>(arg) - kC46OffsetBias);
}
constexpr float kC46DamageFraction = 0.25f;  // of MaxHealth, per warp
constexpr ULONGLONG kC46CooldownMs = 2000;   // per pawn
constexpr ULONGLONG kC46FollowMs = 500;      // the position check after a warp
constexpr double kC46NearUu = 200.0;         // "Done when": the warped pawn ends this close to its target
constexpr int32_t kC46Rows = 8;              // pawn rows (cooldowns, client suppressions, follow-ups)
constexpr uint32_t kC46NpcLines = 5;
constexpr uint32_t kC46GateLines = 3;
constexpr uint32_t kC46StillLines = 3; // per cooldown row
struct C46Warp {
    int32_t id; // pawn InternalIndex (host)
    ULONGLONG ms;
    uint32_t still_lines;
};
struct C46Follow {
    int32_t id;        // host: pawn InternalIndex; client: the pawn's NetGUID (card 209)
    int32_t target_id; // host: pawn InternalIndex; client: the pawn's NetGUID
    ULONGLONG at_ms;
};
struct C46State {
    C46Warp warp[kC46Rows];
    int32_t warps;
    int32_t quiet[kC46Rows]; // client: pawns whose first suppression was logged
    int32_t quiets;
    C46Follow follow[kC46Rows];
    int32_t follows;
    Vec3 warp_offset[kC46Rows]; // client, card 461: each Warp's spot - target offset, by warp_count % kC46Rows (a held
                                // Warp, card 209, reads its own row when it resolves)
    uint32_t warp_count; // host: warps this map; client: Warp frames applied
    uint32_t vanilla;    // host: kill floor results passed through to the vanilla death
    uint32_t start_deaths;
    uint32_t lines;
    uint32_t npc_lines;
    uint32_t gate_lines; // host: gate-off lines (Tick asks every frame while a dead pawn lies below)
    const void* anchor_world; // card 146, host: the world of the last Save_CheckPoint this map (null: none yet)
    Vec3 anchor;              // card 146, host: that save's checkpoint zone (or the saver's spot)
    bool no_anchor_warned;    // card 370, host: the one "no checkpoint save yet" WARN this map
};
extern C46State g_c46;
bool c46_line();
float c46_kill_z(const void* context);
int32_t c46_find_warp(int32_t id);
void c46_mark_warp(int32_t id, ULONGLONG now);
void c46_add_follow(int32_t id, int32_t target_id, ULONGLONG now);
bool c46_side_spot(const void* pawn, const void* target, Vec3* out);
bool c46_move_to(void* pawn, const Vec3& to);
// Card 146, host: Save_CheckPoint notes the checkpoint zone nearest the saving player (c46_note_checkpoint). With
// nobody Up, a rejoined pawn spawns, and a server copy below the kill height while all are down goes back, at
// c46_rescue_spot: that zone, then beside the host's pawn in any state, then the first PlayerStart moved by the
// engine's encroachment adjustment. `from` names the one used. False only when the map has no PlayerStart.
void c46_note_checkpoint(void* world);
bool c46_rescue_spot(void* world, const void* cdo, const C43Roster& r, Vec3* out, const char** from);
void c46_receive_warp_to(void* pc, const ue::chan::WireString* s);
void* c46_up_target(const C43Roster& r, int32_t except);
// Card 389, host, every tick (acts once a second): a player pawn Falling for 6 s goes beside a grounded Up player.
void c389_step(void* world, ULONGLONG now);
// Card 046: warp a player's pawn beside `target` (Warp by NetGUID or WarpTo by position, to the client).
void c46_warp(void* pawn, void* target, const C43Roster& r, int32_t slot, ULONGLONG now);
// Card 399, host, every tick (acts every 100 ms): while the host rides its transport dropship every other player's
// pawn rides it too; when the host is off it, each is detached and warped beside the host.
void c399_step(void* world, ULONGLONG now);
// Card 399, client, every tick: its own pawn attached to a transport dropship rides (gravity, collision, input and
// camera); returns whether it rides.
bool c399_client_step(uint8_t* pawn, void* pc);
// Card 399, client: a Warp that arrives while its own pawn rides ends the ride first; the own pawn, or null.
void* c399_client_land(void* pc);
// Card 399, client: after the landing warp, the control rotation takes the host pawn's yaw (`source` names where the
// yaw came from, for the row).
void c399_client_face(void* pc, double yaw, const char* source);
// Card 400, host (from c399_step): a remote player's trooper is hidden, takes no Health loss and is no enemy target
// while card 320 has sent its client IntroStart and no IntroDone came back (the client is loading or on its screen).
// One row per pawn; a rider stays card 399's to show. c400_shielded: the pawn has a row (the damage and target gates).
void c400_step(const C43Roster& r, ULONGLONG now);
bool c400_shielded(const void* pawn);
using C399SetTransformFn = bool (*)(void* actor, const void* transform, bool sweep, void* hit, int32_t teleport);
using C399DetachFn = void (*)(void* actor, const void* rules);
using C399ViewBlendFn = void (*)(void* pc, void* target, float blend_time, uint8_t blend_func, float blend_exp,
                                 bool lock_outgoing);
using C399GetViewFn = void* (*)(const void* pc);
extern C399SetTransformFn g_c399_set_transform; // AActor::SetActorTransform
extern C399DetachFn g_c399_detach;              // AActor::DetachFromActor
extern C399ViewBlendFn g_c399_view_blend;       // APlayerController::SetViewTargetWithBlend
extern C399GetViewFn g_c399_get_view;           // APlayerController::GetViewTarget
// Card 461 (diagnostic): virtual bool UPrimitiveComponent::MoveComponentImpl(const FVector& Delta, const FQuat& NewRotation,
// bool bSweep, FHitResult* OutHit, EMoveComponentFlags MoveFlags, ETeleportType Teleport) (pdb public, hooked under
// UBWCOOP_DIAG). EMoveComponentFlags is a plain enum (int); ETeleportType is a uint8 enum class.
using C461MoveFn = bool (*)(void* comp, const void* delta, const void* rotation, bool sweep, void* hit, int32_t flags,
                            uint8_t teleport);
extern C461MoveFn g_orig_c461_move;
bool detour_c461_move(void* comp, const void* delta, const void* rotation, bool sweep, void* hit, int32_t flags,
                      uint8_t teleport);
// Card 461: true while a released rider's watch window is open (the only cost of the move hook outside the window).
extern bool g_c461_armed;
// Card 461: Fall damage while a watch is open: one row with the damage, the caller stack and, per watched pawn, its
// movement mode, velocity and the reflected Fall/Air/Apex fields of the pawn and its CharacterMovementComponent.
void c461_fall_rows(float damage, float out, const char* stack);
// Card 461: the movement base of `pawn` (a character) as "<component> of <owner>" into `buf`, or "none".
void c461_base_text(const void* pawn, char* buf, size_t cap);
// Card 461 b168, around every c46 warp (dropship.cpp): clear the impart bits before the move (the saved mask, or -1),
// then start the frame watch that restores them. `slot`: host roster slot; client 0 (own pawn).
int32_t c461_warp_clear(void* pawn, int32_t slot, ULONGLONG now);
void c461_warp_watch(void* pawn, int32_t slot, ULONGLONG now, int32_t impart);
const double* c46_spawn_spot(const void* world, const void* cls, const double* transform, unsigned long long caller,
                             double* out);
// Card 046 PlayerStart diagnosis and the rejoin spot. MSVC x64 passes the by-value FVector and FRotator (3 doubles
// each) as pointers to caller-made copies.
// bool UWorld::OverlapMultiByChannel(TArray<FOverlapResult>&, const FVector&, const FQuat&, ECollisionChannel,
//      const FCollisionShape&, const FCollisionQueryParams&, const FCollisionResponseParams&) const  -- detour (logs
//      the overlaps only while card 046 probes a PlayerStart)
using OverlapMultiFn = bool (*)(const void* world, void* out, const Vec3* location, const void* rotation,
                                int32_t channel, const void* shape, const void* params, const void* response);
// bool UWorld::EncroachingBlockingGeometry(const AActor*, FVector, FRotator, FVector* ProposedAdjustment)  -- called
using EncroachingFn = bool (*)(void* world, const void* actor, const Vec3* location, const Vec3* rotation,
                               Vec3* adjustment);
// bool UWorld::FindTeleportSpot(const AActor*, FVector& TestLocation, FRotator)  -- called
using TeleportSpotFn = bool (*)(void* world, const void* actor, Vec3* location, const Vec3* rotation);
extern OverlapMultiFn g_orig_c46_overlap_multi;
extern EncroachingFn g_c46_encroaching;
extern TeleportSpotFn g_c46_teleport_spot;
bool detour_c46_overlap_multi(const void* world, void* out, const Vec3* location, const void* rotation,
                              int32_t channel, const void* shape, const void* params, const void* response);
void c39_comp_text(const void* comp, char* out, size_t size);
// --- Card 048 step 0: the kill-floor hit ignores armour ---
// UADHealthUtilityLibrary::DealDamage takes two FGameplayTagContainers by value (MSVC x64: a pointer to a
// caller-built copy, r9 and [rsp+0x28]) and frees both arrays of both in its epilogue (0x04A3B67A-0x04A3B6AB), so
// the tag array comes from FMemory::Malloc, is built fresh per call and is never freed here. The source container
// (first) carries Health.DamageIgnoreArmour, which UADDamageHandler::IsIgnoreArmour 0x04A3D1F0 looks up.
struct TagContainerRaw {
    void* tags; // TArray<FGameplayTag> GameplayTags: FGameplayTag is one FName (8 bytes)
    int32_t num;
    int32_t max;
    void* parents; // TArray<FGameplayTag> ParentTags: left empty (Free(nullptr) is a no-op)
    int32_t parents_num;
    int32_t parents_max;
};
using HealthDealDamageFn = void (*)(void* target, const void* causer, float amount, TagContainerRaw* source_tags,
                                    TagContainerRaw* target_tags);
extern HealthDealDamageFn g_health_deal_damage;
struct C48Armour {
    float armour;
    float max_armour;
};
C48Armour c48_armour(const void* component);
bool c48_deal_ignore_armour(void* pawn, float amount);
void c46_hurt(void* pawn);
constexpr size_t kInventoryItems = 0xD8;         // UADInventory.Items: TArray<USTInventoryItem*>
constexpr size_t kInventoryLastEquipped = 0x124; // UADWeaponInventory.LastEquippedWeaponIndex
constexpr size_t kWeaponDefinitionClass = 0x2D0; // AADWeapon.WeaponDefinitionClass
constexpr int32_t kC48ItemsListed = 8;
constexpr ULONGLONG kC48ClientDumpMs = 5000; // client: own pawn again after the respawn in place
using PawnVoidFn = void (*)(void* pawn);
using LoadOwnerPawnFn = void (*)(void* save, void* pawn, void* world, void* archive);
using GetSaveIdFn = uint32_t* (*)(uint32_t* out_guid, const void* actor);
using ActorFullyLoadedFn = void (*)(void* actor, void* save);
using AddObjectiveFn = void (*)(void* subsystem, void* objective);
using SetObjectiveMapFn = void (*)(void* subsystem, void* map);
using LoadSettingsFn = void (*)(void* mode, const void* data);
using RemoveContextFn = void (*)(void* iface, const void* context, const uint8_t* options);
extern PawnVoidFn g_orig_load_asc;
extern LoadOwnerPawnFn g_orig_load_owner_pawn;
extern GetSaveIdFn g_get_save_id;
extern ActorFullyLoadedFn g_orig_actor_fully_loaded;
extern ExecFn g_orig_exec_complete_death;
extern AddObjectiveFn g_orig_add_objective;
extern SetObjectiveMapFn g_orig_set_objective_map;
extern LoadSettingsFn g_orig_load_settings;
extern RemoveContextFn g_remove_mapping_context;
struct C48State {
    uint32_t lines;           // audit lines (cap kC48Lines)
    uint32_t objective_lines; // AddObjectiveObject lines (cap kC48ObjectiveLines)
    uint32_t objectives;      // AddObjectiveObject calls since the last per-map reset
    uint32_t asc_loads;       // LoadAbilitySystemComponent calls, whole process
    ULONGLONG client_dump_ms; // client: when to log the own pawn again (0 = none)
};
extern C48State g_c48;
bool c48_line();
void c48_inventory_text(const void* pawn, char* out, size_t cap);
void c48_pawn_line(const char* what, const void* pawn);
void detour_load_asc(void* pawn);
extern PawnVoidFn g_orig_load_asc_bug; // card 198
void detour_load_asc_bug(void* pawn);  // card 198: ASTPlayerCharacterBug::LoadAbilitySystemComponent
void detour_load_owner_pawn(void* save, void* pawn, void* world, void* archive);
void detour_actor_fully_loaded(void* actor, void* save);
void detour_exec_complete_death(void* context, void* frame, void* result);
void detour_add_objective(void* subsystem, void* objective);
void detour_set_objective_map(void* subsystem, void* map);
void detour_load_settings(void* mode, const void* data);
const void* c48_find_context(const void* pc, const char* name, void** iface_out);
unsigned c48_capture_mode(void* pc);
void c48_client_cursor(void* pc);
// Card 156: the client's one owner for every return to Up (revive, respawn in place, map load) and its +5 s check.
void c156_client_up(void* pc, const char* kind, bool restore, ULONGLONG now);
void c156_client_check(void* pc, ULONGLONG now);
void c156_arm_map();
constexpr size_t kSubsystemObjectives = 0x60; // USTLevelObjectiveSubsystem TMap<FName, USTLevelObjective*>
                                              // (GetAllObjectives 0x05158F30 copies it)
constexpr size_t kObjectiveElementBytes = 0x18; // TSetElement<TPair<FName, ptr>>: key, value, hash next, hash index
constexpr size_t kObjectiveElementValue = 0x8;
constexpr size_t kObjectiveId = 0x58;        // USTLevelObjective FName (GetObjectiveID 0x030E5E80)
constexpr size_t kObjectiveMarker = 0x68;    // bool (UpdateVisibility 0x05176C50, first argument)
constexpr size_t kObjectiveComplete = 0x69;  // bool (IsComplete 0x019E5C40)
constexpr size_t kObjectiveInList = 0x6A;    // bool (GetShowInObjectiveList 0x02A30BD0)
constexpr size_t kObjectiveParent = 0x70;    // USTLevelObjective* (GetParentObjective 0x050E9A30)
// Card 070: TArray<AActor*>, the actors passed at Add (each objective init 0x051513E0 / 0x05151520 / 0x05151680
// assigns its actor argument to +0x88, then calls CreateASTLevelObjectiveMarker once per element).
constexpr size_t kObjectiveActors = 0x88;
constexpr int32_t kC49MaxObjectives = 256;   // TMap slots read
constexpr ULONGLONG kC49WorldCheckMs = 90000; // after the map load; the level script adds objectives at about +65 s
using C49AddFn = void (*)(void* ctx, uint64_t id, const void* actors, bool, bool, bool, bool);
using C49AddSubFn = void (*)(void* ctx, uint64_t parent, uint64_t id, bool, const void* actors, bool, bool, bool, bool);
using C49AddCountFn = void (*)(void* ctx, uint64_t id, const void* actors, int32_t count, bool, bool, bool, bool);
using C49AddCountSubsFn = void (*)(void* ctx, uint64_t id, const void* actors, bool, bool, int32_t count, bool, bool);
using C49AddCountSubFn = void (*)(void* ctx, uint64_t parent, uint64_t id, bool, const void* actors, int32_t count,
                                  bool, bool, bool, bool);
using C49AddCountSubSubsFn = void (*)(void* ctx, uint64_t parent, uint64_t id, bool, const void* actors, bool, bool,
                                      int32_t count, bool, bool);
using C49NameFn = void (*)(void* ctx, uint64_t id);
using C49Name2Fn = void (*)(void* ctx, uint64_t a, uint64_t b);
using C49VisibilityFn = void (*)(void* ctx, uint64_t id, bool, bool);
using C49CheckAllFn = bool (*)(void* ctx);
using C49GateFn = bool (*)(void* ctx, uint64_t id, void* ids); // TArray<FName> by value: pointer to the caller's copy
using C49SaveFn = void (*)(void* engine, void* metadata, void* delegate, bool flag);
using C49GetSubsystemFn = void* (*)(const void* world);
using C49ExistsFn = bool (*)(const void* subsystem, uint64_t id);
extern C49AddFn g_orig_c49_add;
extern C49AddSubFn g_orig_c49_add_sub;
extern C49AddCountFn g_orig_c49_add_count;
extern C49AddCountSubsFn g_orig_c49_add_count_subs;
extern C49AddCountSubFn g_orig_c49_add_count_sub;
extern C49AddCountSubSubsFn g_orig_c49_add_count_sub_subs;
extern C49NameFn g_orig_c49_complete;
extern C49NameFn g_orig_c49_complete_any;
extern C49Name2Fn g_orig_c49_complete_sub;
extern C49NameFn g_orig_c49_increment;
extern C49VisibilityFn g_orig_c49_visibility;
extern C49NameFn g_orig_c49_callout;
extern C49CheckAllFn g_orig_c49_check_all;
extern C49GateFn g_orig_c49_gate;
extern C49SaveFn g_orig_c49_save;
extern C49GetSubsystemFn g_c49_get_subsystem;
extern C49ExistsFn g_c49_exists;
extern StaticClassFn g_c49_library_class;
extern StaticClassFn g_c49_objective_class;
extern StaticClassFn g_c49_count_class;
extern StaticClassFn g_c49_manager_class;
void detour_c49_complete(void* ctx, uint64_t id);
void detour_c49_complete_sub(void* ctx, uint64_t a, uint64_t b);
struct C49State {
    uint32_t audit_lines;
    uint32_t depth;          // library calls in progress (nesting)
    uint32_t replay;         // card 100, client: host frames being replayed (nesting)
    bool params_logged;      // once per process
    ULONGLONG world_check_ms; // when to log the world check (0 = none)
    ULONGLONG panel_next_ms;  // next OBJECTIVES panel state read (diagnosis)
    uint64_t panel_sig;       // the last logged panel state
    ULONGLONG items_ms;       // when to log the panel's list items after a state change (0 = none due)
};
extern C49State g_c49;
bool c49_objectives(const void* subsystem, SparseView& v);
int32_t c49_objective_count(const void* subsystem);
constexpr size_t kObjectiveCountTarget = 0x98;    // USTLevelObjectiveCount int32 (GetTargetCount 0x050E9AF0)
constexpr size_t kObjectiveCountCurrent = 0x9C;   // int32 (GetCurrentCount 0x050E9AD0)
constexpr size_t kObjectiveCountSubs = 0xA0;      // bool: counts its sub-objectives; set to 1 only by
// CreateNewCountLevelObjective_SubObjectives 0x05151520 (0 by CreateNewCountLevelObjective 0x051513E0); the
// count class's CanHaveSubobjectives (vtable +0x2D0, 0x036DB5F0) returns it, and AddSubObjective 0x0522A890
// returns early when the parent's CanHaveSubobjectives is false (card 305)
constexpr int32_t kC49JournalCap = 512; // card 308: FED_C2M1's snapshot outgrew 128 (129 frames at 33 objectives)
constexpr int32_t kC49Shots = 8;
constexpr ULONGLONG kC49ShotAfterMs = 3000;  // after a completion
// Both instances save into one Screenshots folder and pick the next free number on their own, so two shots in the
// same second overwrite each other: the client's shots wait this much longer.
constexpr ULONGLONG kC49ClientShotLagMs = 2000;
constexpr ULONGLONG kC53TestDelayMs = 6000; // card 053 test: snapshot end to the client's rebuild
constexpr ULONGLONG kC53EventMs = 10000;     // card 053 test: host snapshot to the post-rebuild completion
struct C49Frame {
    wchar_t w[kChanTextFrame]; // an Objective frame, or a card 070 ObjectiveActors text frame
    int32_t num;
};
struct C49Journal {
    C49Frame frame[kC49JournalCap];
    int32_t n;
    int32_t dropped;  // card 066: frames not recorded since the last reset (the journal was full)
    int32_t peak;     // card 308: the most frames held on this map (reset at c49_map_end only)
    bool full_logged;
    bool building;    // card 066: a snapshot (checkpoint or compaction) is being recorded; no compaction starts
    bool record_only; // card 066: a compaction is rebuilding the journal; c49_emit sends nothing live
    bool compact_off; // card 066: a compaction did not fit on this map; the old behaviour (no compaction) holds
};
struct C49Shot {
    ULONGLONG at_ms;
    const char* what; // literal
};
struct C49Mirror {
    int32_t synced[kC17Slots]; // host: object indices of the remote PCs that got this map's journal
    int32_t n_synced;
    uint64_t snapshot_ids[kC49MaxObjectives]; // client: the ids of the snapshot being received
    int32_t n_snapshot;
    uint32_t lines;
    uint32_t frames;          // client: frames applied this map
    ULONGLONG first_add_ms;   // host: the first library Add of this map
    bool completed;           // host test: the test objective was completed this map
    bool delegates_logged;    // client: once per process
    ULONGLONG c53_due_ms;     // client: when card 053's objective list rebuild runs (0 = none due)
    C49Shot shot[kC49Shots];
    int32_t shots;
};
extern C49Journal g_c49_journal;
extern int32_t g_c49_cap;
extern C49Mirror g_c49m;
extern bool g_c49_test;
extern wchar_t g_c49_test_id[config::kMaxWeaponLen + 1];
extern ULONGLONG g_c53_event_ms;
void c49_shot(ULONGLONG at_ms, const char* what);
void c49_shot_step(void* pc, ULONGLONG now);
void c49_journal_reset();
int32_t c49_snapshot_journal(const void* world, const SparseView& v);
const uint8_t* c49_panel(const uint8_t* subsystem);
void c49_item_lines(const uint8_t* box, int32_t depth);
const char* c66_dump(void* world, uint32_t seq);
void c49_panel_watch(void* pc, ULONGLONG now);
// `skip` (card 092): never returned, so a warp or look by class name does not pick the mover itself.
void* c64_find_actor(const void* world, const char* name, const Vec3& from, const void* skip = nullptr,
                     bool live = false);
void c49_test_step(void* pc, ULONGLONG now);
void detour_c49_add(void* ctx, uint64_t id, const void* actors, bool b0, bool b1, bool b2, bool b3);
void detour_c49_add_sub(void* ctx, uint64_t parent, uint64_t id, bool b0, const void* actors, bool b1, bool b2,
                        bool b3, bool b4);
void detour_c49_add_count(void* ctx, uint64_t id, const void* actors, int32_t count, bool b0, bool b1, bool b2,
                          bool b3);
void detour_c49_add_count_subs(void* ctx, uint64_t id, const void* actors, bool b0, bool b1, int32_t count, bool b2,
                               bool b3);
void detour_c49_add_count_sub(void* ctx, uint64_t parent, uint64_t id, bool b0, const void* actors, int32_t count,
                              bool b1, bool b2, bool b3, bool b4);
void detour_c49_add_count_sub_subs(void* ctx, uint64_t parent, uint64_t id, bool b0, const void* actors, bool b1,
                                   bool b2, int32_t count, bool b3, bool b4);
void detour_c49_complete_any(void* ctx, uint64_t id);
void detour_c49_increment(void* ctx, uint64_t id);
// Card 097: the native objective completion and count (USTLevelObjective / USTLevelObjectiveCount members).
using C97ObjectiveFn = void (*)(void* objective);
using C97SubsystemCompleteFn = void (*)(void* subsystem, const void* objective);
extern C97ObjectiveFn g_orig_c97_complete;
extern C97ObjectiveFn g_orig_c97_increment;
extern C97SubsystemCompleteFn g_orig_c97_subsystem_complete;
void detour_c97_complete(void* objective);
void detour_c97_increment(void* objective);
void detour_c97_subsystem_complete(void* subsystem, const void* objective);

// Card 098: world-change measurement (log only, world_c98.cpp).
enum class C98Kind : uint8_t { Spawn, Destroy, Hide, Show, Timeline, Attach, Collision, Count };
inline constexpr const char* kC98KindText[] = {"spawn", "destroy", "hide", "show", "timeline", "attach", "collision"};
static_assert(sizeof(kC98KindText) / sizeof(kC98KindText[0]) == static_cast<size_t>(C98Kind::Count),
              "one text per kind");
using C98DestroyFn = bool (*)(void* world, void* actor, bool net_force, bool modify_level);
using C98HiddenFn = void (*)(void* actor, bool hidden);
using C98SelfFn = void (*)(void* self);
using C98AttachFn = bool (*)(void* actor, void* parent, const void* rules, uint64_t socket);
extern C98DestroyFn g_orig_c98_destroy;
extern C98HiddenFn g_orig_c98_hidden;
extern C98SelfFn g_orig_c98_timeline_play;
extern C98SelfFn g_orig_c98_timeline_from_start;
extern C98SelfFn g_orig_c98_timeline_reverse;          // card 130 item 2
extern C98SelfFn g_orig_c98_timeline_reverse_from_end; // card 130 item 2
extern C98SelfFn g_orig_c98_timeline_stop;             // card 130 item 2
extern C98AttachFn g_orig_c98_attach;
void c98_note(C98Kind kind, const void* actor, const void* detail, unsigned long long caller);
void c98_dump(const char* why);
void c98_arm_map();
void c98_world_map_end(); // card 184, LoadMap enter: the world journal and card 104 tables start over
void c98_world_totals(const char* when); // card 308: the world relay totals row (map totals, quit totals)
bool detour_c98_destroy(void* world, void* actor, bool net_force, bool modify_level);
void c218_destroy_line(const void* actor, unsigned long long caller); // dropship.cpp: card 218 diagnostic row
void detour_c98_hidden(void* actor, bool hidden);
void detour_c98_timeline_play(void* timeline);
void detour_c98_timeline_from_start(void* timeline);
void detour_c98_timeline_reverse(void* timeline);
void detour_c98_timeline_reverse_from_end(void* timeline);
void detour_c98_timeline_stop(void* timeline);
// Card 130 item 2. Host: a UTimelineComponent call (C98TimelineOp) on a placed, unreplicated actor, relayed as WorldOp
// 'L' and journalled per timeline. The client calls the same original on its copy's same-named timeline.
enum class C98TimelineOp : uint8_t { Play, PlayFromStart, Reverse, ReverseFromEnd, Stop, Count };
void c98_host_timeline(const void* timeline, C98TimelineOp op);
// Card 261: UTimelineComponent::SetPlayRate(float). Host: relayed as WorldOp 'B' under the same conditions as 'L'.
using C261RateFn = void (*)(void* timeline, float rate);
extern C261RateFn g_orig_c261_set_play_rate;
void detour_c261_set_play_rate(void* timeline, float rate);
void c261_host_rate(const void* timeline, float rate);
// Card 311: host. A range target's card 065 Activate into the world journal (replayed to a client's first Pong while the
// component is still active), and the row's end with its owner.
void c311_host_activate(const void* component, bool reset);
void c311_host_forget(const void* actor);
bool detour_c98_attach(void* actor, void* parent, const void* rules, uint64_t socket);
// Card 098 world relay: ASTSatchelBuildingBase::ReceiveOnSatchelExploded(bIsPrematureExplosion), the Blueprint event
// that destroys a bug hole, relayed from the host to every client (WorldOp) and replayed on a client's first Pong.
using C98SatchelFn = void (*)(void* building, bool premature);
extern C98SatchelFn g_orig_c98_satchel;
// Card 133: relayed hold interactions (hold.cpp). Every timed interaction ends in the interface's
// OnTimedInteractionCompleted; the client's own completion on a card 054 relay class goes to the host as HoldDone.
extern C29InteractFn g_orig_c133_completed;
void detour_c133_completed(void* iface, void* interactor);
void c133_host_done(void* pc, const WireString* s);
// Card 418: the satchel site's fuse start; the host sends each to the clients as HoldDone, the client arms its copy.
using C133StartFn = void (*)(void* site);
extern C133StartFn g_orig_c133_start;
void detour_c133_start(void* site);
// Card 456 (log only): a timed hold's start, interrupt and release; the interactor trace result each tick.
extern C29InteractFn g_orig_c456_started;
extern C29InteractFn g_orig_c456_interrupted;
extern C29InteractFn g_orig_c456_released;
void detour_c456_started(void* iface, void* interactor);
void detour_c456_interrupted(void* iface, void* interactor);
void detour_c456_released(void* iface, void* interactor);
void c456_note_trace(const void* interactor, const void* found, const void* used);
void c133_client_arm(void* pc, const WireString* s);
bool c133_is_timed(void* iface);
bool c133_can_interact(void* iface, void* interactor, char* why, size_t cap);
void c133_client_blocked(void* building, bool premature);
bool c133_client_applied(void* building);
void c133_client_step(ULONGLONG now);
// Card 248: IADInteractableInterface::TickInteractable(float dt, UADInteractorComponent*), slot 0x50, the one
// implementation every interface vtable holds (12 of 12). Every interactor's TickComponent whose focus is a started
// timed target calls it with itself; it advances the target's shared progress and completes with that interactor.
// ?TickInteractable@IADInteractableInterface@@UEAAMMPEAVUADInteractorComponent@@@Z (tools/pdb_rva.py lookup).
inline constexpr offsets::Symbol kC248TickInteractable = {"IADInteractableInterface_TickInteractable", 0x04A206D0u};
using C248TickFn = float (*)(void* iface, float dt, void* interactor);
extern C248TickFn g_orig_c248_tick;
float detour_c248_tick(void* iface, float dt, void* interactor);

// Card 191 diagnostic: FSkeletalMeshComponentEndPhysicsTickFunction::ExecuteTick (exe 0x041874F0; static trace in
// docs/cards/191). `self` is the tick function; its Target (the USkeletalMeshComponent) is at +0x28.
inline constexpr offsets::Symbol kC191EndTick = {"FSkeletalMeshComponentEndPhysicsTickFunction_ExecuteTick", 0x041874F0u};
using C191EndTickFn = void (*)(void* self, float dt, int32_t tick_type, int32_t thread, const void* completion);
extern C191EndTickFn g_orig_c191_end_tick;
void detour_c191_end_tick(void* self, float dt, int32_t tick_type, int32_t thread, const void* completion);
void detour_c98_satchel(void* building, bool premature);
const char* c125_spawn_bug(void* world, const Vec3& at, double yaw, const char* kind);
void c98_receive_world(void* pc, const WireString* s);
void c98_replay(void* pc);
void c98_replay_events(void* pc); // card 323 (b257): the world journal's event rows, before every state journal
// hb55: client, around a journal replay's one-shot apply (a barrel's OnDeath, a satchel building's explode). A late
// joiner gets the end state, not the explosion the host made minutes before: while on, the always-on effect spawn
// detours (grenades.cpp) spawn no effect and play no sound. Returns the effects muted so far this process.
void c98_fx_quiet(bool on);
uint32_t c98_fx_muted();
// Card 099: UGameplayStatics::GetPlayerController(context, 0) on the host returns a server copy's own controller when
// the context belongs to that copy. The hook row's original slot is g_get_player_controller itself, so every mod call
// goes through the trampoline. The other hooks are log only (UBWCOOP_C99_LOG=1).
extern bool g_c99_log;
extern StaticClassFn g_pawn_class;
void* detour_c99_get_pc(const void* ctx, int32_t index);
// Card 103: on the host, an index-0 GetPlayerController call whose context belongs to an enemy returns the
// controller of that enemy's player: its primary target when that is a player pawn, else the nearest player pawn.
// Attack tickets (GetTicketPriorityScore), perception, line of sight and facing then use the player the enemy fights,
// not always the host. SetPrimaryTarget is hooked always (it records each enemy's primary target).
// UBWCOOP_C103_LOG=1 (host, log only): target selection per enemy, and the enemy-context calls counted per caller.
extern bool g_c103_log;
using C103SelectFn = void (*)(void* component);
using C103SetFn = bool (*)(void* component, void* actor);
using C103FindFn = const void* (*)(const void* component, const void* actor);
extern C103SelectFn g_orig_c103_select;
extern C103SetFn g_orig_c103_set;
extern C103FindFn g_c103_find;
void detour_c103_select(void* component);
bool detour_c103_set(void* component, void* actor);
void* c103_enemy_pc(const void* ctx, unsigned long long rva);
void c103_arm_map();
// Card 532 (target_c532.cpp, always on): client target ticks stop; the host's primary target per NPC is sent
// (ChanOp::NpcTarget) and returned by the client's GetPrimaryTarget.
using C532TickFn = void (*)(void* component, float delta, int32_t tick_type, void* tick_function);
using C532GetPrimaryFn = bool (*)(const void* component, uint8_t* out);
using C532FindPrimaryFn = const void* (*)(const void* component);
extern C532TickFn g_orig_c532_tick;
extern C532GetPrimaryFn g_orig_c532_get_primary;
extern C532FindPrimaryFn g_c532_find_primary;
void detour_c532_tick(void* component, float delta, int32_t tick_type, void* tick_function);
bool detour_c532_get_primary(const void* component, uint8_t* out);
void c532_host_note(void* component);              // host, after SelectPrimaryTarget
void c532_tick(ULONGLONG now);                     // both sides, c42_step
void c532_receive(void* pc, const WireString* s);  // client: ChanOp::NpcTarget
void c532_arm_map();                               // both sides, map arm (c99_arm_map)
// Card 531 (perf_save_c531.cpp, performance mod family): per-thread cache of the actor's save component behind
// USTSaveComponent::GetSaveId. On by default; UBWCOOP_C531_OFF=1 leaves the detour out.
extern GetSaveIdFn g_orig_c531_save_id;
extern StaticClassFn g_c531_static_class; // USTSaveComponent::StaticClass
extern bool g_c531_on;
uint32_t* detour_c531_save_id(uint32_t* out, const void* actor);
void c531_tick(ULONGLONG now); // game thread, c42_step: totals every 60 s
// Card 535 (floor_c535.cpp, performance mod family): client NavWalking NPC proxies get FindFloor's no-floor result
// without the sweep. On by default; UBWCOOP_C535_OFF=1 leaves the detour out.
using FindFloorFn = void (*)(const void* cmc, const void* location, void* out, bool can_use_cached,
                             const void* downward);
extern FindFloorFn g_orig_c535_find_floor;
extern bool g_c535_on;
void detour_c535_find_floor(const void* cmc, const void* location, void* out, bool can_use_cached,
                            const void* downward);
void c535_tick(ULONGLONG now); // game thread, c42_step: totals every 60 s
// Card 536 (perf_component_c536.cpp, performance mod family): per-thread cache behind AActor::FindComponentByClass.
// On by default; UBWCOOP_C536_OFF=1 leaves the detour out. The TSubclassOf argument is passed by pointer.
using FindComponentByClassFn = void* (*)(const void* actor, void* const* cls);
extern FindComponentByClassFn g_orig_c536_find;
extern bool g_c536_on;
void* detour_c536_find(const void* actor, void* const* cls);
using OwnedComponentFn = void (*)(void* actor, void* component);
using ResetOwnedComponentsFn = void (*)(void* actor);
extern OwnedComponentFn g_orig_c537_add;
extern OwnedComponentFn g_orig_c537_remove;
extern ResetOwnedComponentsFn g_orig_c537_reset;
void detour_c537_add(void* actor, void* component);
void detour_c537_remove(void* actor, void* component);
void detour_c537_reset(void* actor);
void c536_tick(ULONGLONG now); // game thread, c42_step: totals and none callers every 60 s
// Card 538 (perf_target_c538.cpp, performance mod family, diag): per-frame repeat counts behind
// USTAttackComponent::DoesTargetMeetConditions. On by default; UBWCOOP_C538_OFF=1 leaves the detour out.
using TargetConditionsFn = bool (*)(const void* self, const void* cond, void* target);
extern TargetConditionsFn g_orig_c538_conditions;
extern bool g_c538_on;
extern bool g_c540_on; // card 540: same-frame result memo in the same detour; UBWCOOP_C540_OFF=1 turns it off
bool detour_c538_conditions(const void* self, const void* cond, void* target);
void c538_tick(ULONGLONG now); // game thread, c42_step: advances the frame, totals every 60 s
// Card 539 (perf_move_c539.cpp, performance mod family, diag): client NPC proxy capsule sweep counts, on card 461's
// MoveComponentImpl row. UBWCOOP_C539_OFF=1 turns the counts off.
extern bool g_c539_on;
extern bool g_gate_c461_c539;
extern bool g_c542_on;
extern bool g_c544_on; // card 544: dynamic tree padding (UBWCOOP_C544_OFF=1 turns it off)
bool c539_move(void* comp, const void* delta, const void* rotation, bool sweep, void* hit, int32_t flags,
               uint8_t teleport);
void c539_tick(ULONGLONG now); // game thread, c42_step: totals every 60 s
void c544_tick(void* engine);   // game thread, c42_step: card 544 dynamic tree padding, once per process
// Card 534 (query_c534.cpp, diag with UBWCOOP_C132_FX=1): game-thread cost of the world collision queries by caller
// or by owner class. Entry order matches kC534Entry in the source and the hook rows.
constexpr int32_t kC534Entries = 13;
using C534Fn = bool (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                        uintptr_t);
extern C534Fn g_orig_c534[kC534Entries];
template <int K>
bool detour_c534(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6,
                 uintptr_t a7, uintptr_t a8);
void c534_tick(ULONGLONG now); // game thread, c42_step
// Card 545 (query_bp_c545.cpp, diag with UBWCOOP_C132_FX=1): the native UKismetSystemLibrary
// CapsuleTraceSingleForObjects (entry 0) and SphereOverlapComponents (entry 1), keyed by return address.
constexpr int32_t kC545Entries = 2;
using C545CapsuleFn = bool (*)(const void* context, const void* start, const void* end, float radius,
                               float half_height, const void* types, bool complex, const void* ignore, int32_t draw,
                               void* hit, bool ignore_self, const void* color, const void* hit_color, float draw_time);
using C545SphereFn = bool (*)(const void* context, const void* position, float radius, const void* types,
                              const void* class_filter, const void* ignore, void* out);
extern C545CapsuleFn g_orig_c545_capsule;
extern C545SphereFn g_orig_c545_sphere;
bool detour_c545_capsule(const void* context, const void* start, const void* end, float radius, float half_height,
                         const void* types, bool complex, const void* ignore, int32_t draw, void* hit, bool ignore_self,
                         const void* color, const void* hit_color, float draw_time);
bool detour_c545_sphere(const void* context, const void* position, float radius, const void* types,
                        const void* class_filter, const void* ignore, void* out);
void c545_tick(ULONGLONG now); // game thread, c42_step
// Card 548 (perf_overlap_c548.cpp, diag with UBWCOOP_C132_FX=1): virtual UpdateOverlapsImpl(const TArrayView<const
// FOverlapInfo>* pending, bool notify, const TArrayView<const FOverlapInfo>* at_end) of UPrimitiveComponent (entry 0),
// USkinnedMeshComponent (1) and USkeletalMeshComponent (2), rows per player-owned component. Entry 1 shares its
// address with USceneComponent::UpdateOverlapsImpl (identical code folding, RVA 0x3afef70), so it times scene
// components such as the camera too.
constexpr int32_t kC548Entries = 3;
using C548ImplFn = bool (*)(void* comp, const void* pending, bool notify, const void* at_end);
extern C548ImplFn g_orig_c548_impl[kC548Entries];
bool detour_c548_prim(void* comp, const void* pending, bool notify, const void* at_end);
bool detour_c548_skinned(void* comp, const void* pending, bool notify, const void* at_end);
bool detour_c548_skeletal(void* comp, const void* pending, bool notify, const void* at_end);
void c548_tick(ULONGLONG now); // game thread, c42_step

// Card 111: wave budget and ticket limits scaled by player count (bug_scale.cpp).
using C111FactorFn = int32_t (*)(const void* wave, void* const* data, int32_t budget);
using C111CreateFn = void (*)(void* manager, uint8_t type, int32_t limit, float rate);
using C111TryRefreshFn = bool (*)(void* manager, uint8_t type, int32_t limit, float rate);
using C111RequestFn = bool (*)(void* manager, void* enemy, uint8_t type, int32_t cost, bool steal);
using C111RefreshFn = void (*)(void* manager);
extern C111FactorFn g_orig_c111_factor;
extern C111CreateFn g_orig_c111_create;
extern C111TryRefreshFn g_orig_c111_try_refresh;
extern C111RequestFn g_orig_c111_request;
extern C111RefreshFn g_c111_refresh;
int32_t detour_c111_factor(const void* wave, void* const* data, int32_t budget);
void detour_c111_create(void* manager, uint8_t type, int32_t limit, float rate);
bool detour_c111_try_refresh(void* manager, uint8_t type, int32_t limit, float rate);
bool detour_c111_request(void* manager, void* enemy, uint8_t type, int32_t cost, bool steal);
void c111_step(void* engine);
int32_t c111_holders(uint8_t type);
float c122_pref_scale(void* component);
void c122_pref_restore(void* component, float base);
const void* c103_pick_of(const void* enemy);
// Card 122: attack pacing measurement, host only, UBWCOOP_C122_LOG (attack_pace.cpp).
using C122CanFn = bool (*)(void* enemy, uint8_t type, bool dummy, int32_t cost);
using C122ChooseFn = bool (*)(void* comp, float dt);
using C122PerformFn = void (*)(void* comp);
extern bool g_c122_log;
extern C122CanFn g_orig_c122_can;
extern C122ChooseFn g_orig_c122_choose;
extern C122PerformFn g_orig_c122_perform;
bool detour_c122_can(void* enemy, uint8_t type, bool dummy, int32_t cost);
bool detour_c122_choose(void* comp, float dt);
void detour_c122_perform(void* comp);
using C122FitFn = bool (*)(const void* zone, void* enemy);
using C122SizeFn = int32_t (*)(void* enemy);
extern C122FitFn g_orig_c122_fit;
extern C122SizeFn g_c122_size;
extern StaticClassFn g_c122_target_class; // UADTargetComponent
bool detour_c122_fit(const void* zone, void* enemy);
void c122_step(void* engine);
// Card 125: turret fire, aim and heat on both machines (turret_sync.cpp). UBWCOOP_C125_LOG gates the measurement hooks.
// FGameplayTag (one FName, 8 bytes) is passed by value in a register.
using C125TagFn = void (*)(void* recv, uint64_t tag);
extern bool g_c125_log;
extern C125TagFn g_orig_c125_tag_pressed;
extern C125TagFn g_orig_c125_tag_released;
extern C29LookFn g_orig_c125_look_h;
extern C29LookFn g_orig_c125_look_v;
void detour_c125_tag_pressed(void* recv, uint64_t tag);
void detour_c125_tag_released(void* recv, uint64_t tag);
void detour_c125_look_h(void* recv, float value);
void detour_c125_look_v(void* recv, float value);
void c125_step(void* engine);
bool c98_host_turret(const void* turret, uint32_t value);
void c98_forget_turret(const void* turret); // card 471
void c125_host_forget(const void* actor);   // card 471
// Card 130 item 12. Host, from detour_health_changed: a placed, unreplicated actor's Health crossed to 0 (a bug nest,
// hive, tent or other killable building: its death is Blueprint logic on the health component's OnDeath, which the
// client's copy never runs). Journalled as WorldOp 'K'; the client kills its same-named copy with InstaKillTarget.
void c98_host_kill(const void* actor, float old_health, float new_health);
using HealthIsDeadFn = bool (*)(const void* component);
extern StaticClassFn g_c130_health_class; // UADHealthComponent
// Card 104: true when the host relayed `actor` (a runtime, non-replicated spawn) as a client copy.
bool c104_is_relayed(const void* actor);
// Card 256 class fix 2, host: the digest detail (card 237) named `actor`; a relayed actor sends its rest row again, once per round.
void c256_heal_rest(const void* actor, uint32_t round);
// Card 098: bit (1 << C98Kind) set for each kind the tally counted on class `cls` this map.
uint32_t c98_class_changes(const char* cls);
// Card 132 step 1: the host's per-class inventory of the world, with the mod mechanism that carries each class.
extern StaticClassFn g_c132_timeline_class;      // UTimelineComponent
// Card 315 step 1 (world_c98.cpp): the actor's first timeline's name, position, length and playing, for a log row.
struct C315TimelineText {
    char text[128];
};
C315TimelineText c315_timeline_text(const void* actor);
void c315_host_step(void* engine); // card 315 (world_c98.cpp): host, looping timeline phase rows every 3 s
void c487_host_step(void* engine); // card 487 (world_c98.cpp): host, hide checks for deaths with no Health change
extern StaticClassFn g_c132_satchel_class;       // ASTSatchelBuildingBase
extern StaticClassFn g_c132_custom_pickup_class; // ASTCustomPickupBase
void c132_inventory(const void* world, const char* why);
// Card 132 step 2d: the digest's extra-state classes, in kC132Ext order (digest_c132.cpp).
inline constexpr int32_t kC132ExtClasses = 11;
extern StaticClassFn g_c132_ext_class[kC132ExtClasses];
// Card 423: row 10 is the Blueprint BP_DestroyableActor_Base_C (ext_c132.cpp): its UClass is found by path while this
// map has none yet, and its IsDestroyed byte offset is read from the class's property, so the row's offset is runtime.
inline constexpr uint8_t kC132ExtDestroyable = 10;
void* c423_destroyable_class();    // the class, or null while not loaded (or refused, with a WARN)
uint16_t c423_destroyed_offset();  // IsDestroyed's byte in the actor; 0 until the class is found
bool c423_waiting(const void* actor); // client: the copy's live row still waits for our own script (kC423WaitMs)
// Card 299 (diagnostic): one row for a placed actor with an STMeshUpdate component (the nests), after a host kill or a
// client kill / load-dead apply. Not gated on UBWCOOP_DIAG.
void c299_nest_row(const void* actor, const char* role);
int32_t owned_components(const void* actor, const void* out[], int32_t max); // objective_c284.cpp
// The kC132Ext row of `actor` (digest_c132.cpp): its index, the field's byte offsets (hi 0: none) and the field
// (lo | hi << 8). False when no row's class matches.
struct C132ExtField {
    uint8_t row;
    uint16_t lo, hi;
    uint16_t ext;
};
bool c132_ext_field(const void* actor, C132ExtField& f);
bool c98_replicates(const void* actor); // AActor::bReplicates (world_c98.cpp)
bool c98_bp_destroyed(const void* building); // a placed building Blueprint's Destroyed bool (world_c98.cpp)
// Card 132 step 2d (ext_c132.cpp): the host's kC132Ext state carried to the client as card 098 'X' rows.
using C132ExtSetFn = void (*)(void* actor, bool on);    // ASTLaser::SetLaserActive, ASTHazardousZoneBase::SetHazardActive
using C132ExtCallFn = void (*)(void* actor);            // BeginMoveTo*Position, ASTLargeElevator::NextFloor / PrevFloor
using C132ExtDirFn = void (*)(void* actor, uint8_t dir); // ASTLargeElevator::SetMoveDirection(EMoveDirectionType)
extern C132ExtSetFn g_c132_set_laser_active;
extern C132ExtSetFn g_c132_set_hazard_active;
extern C132ExtCallFn g_c132_hazard_close;
extern C132ExtCallFn g_c132_hazard_open;
extern C132ExtCallFn g_c132_elevator_next;
extern C132ExtCallFn g_c132_elevator_prev;
extern C132ExtDirFn g_c132_elevator_dir;
void c98_host_ext(const void* actor, const char* name, const Vec3& at, uint32_t value); // host: journal, send
void c132_client_ext(void* pc, const char* name, const Vec3& at, uint32_t value);      // client: an 'X' row
// Card 132 note 2: card 104 copy rest rows (world_c98.cpp). FName by value (8 bytes); 0 = NAME_None, the root body.
using PrimBodyQueryFn = bool (*)(const void* component, uint64_t bone);
using PrimBodySleepFn = void (*)(void* component, uint64_t bone);
extern PrimBodyQueryFn g_prim_awake;      // UPrimitiveComponent::RigidBodyIsAwake
extern PrimBodyQueryFn g_prim_simulating; // UPrimitiveComponent::IsSimulatingPhysics
using C317MoveCallFn = void (*)(void* movement);
extern C317MoveCallFn g_c317_stop_movement; // UMovementComponent::StopMovementImmediately (card 317 step 2)
extern C317MoveCallFn g_c317_deactivate;    // UMovementComponent::Deactivate (card 317 step 2)
extern PrimBodySleepFn g_prim_sleep;      // UPrimitiveComponent::PutRigidBodyToSleep
void c104_settle_step(); // host, per tick
void c287_client_step(); // card 287: client, per tick (relayed bodies' motion between host rows)
const void* c140_attach_parent(const void* root); // card 140: a scene component's AttachParent, null when not attached
// Card 140: host, from the card 132 ext sweep, per level actor. full: also look at a root that is not Movable.
void c140_placed_visit(const void* actor, bool full);
bool c140_placed_candidate(const void* actor);
// Card 402 keeps a copy of every level's Actors array and compares it with the live one. An actor found in a slot
// below the level's last count (a freed slot taken again, or a slot whose actor changed) goes into a ring of
// kC402Ring rows. c402_refills runs the compare, then copies the rows after `seq` into `out` (up to `max`) and moves
// `seq` on. False when rows were lost (ring overrun, world or level set changed, or the copy is off): the caller
// must read every slot again. Game thread only.
inline constexpr uint32_t kC402Ring = 256;
struct C402New {
    int32_t level; // index in UWorld::Levels
    int32_t index; // index in ULevel::Actors
    void* actor;   // compare with the slot before use
};
bool c402_refills(const void* world, uint32_t& seq, C402New* out, int32_t max, int32_t& n);
// Card 407: the placed actors named ref.name (c50_name text), from the name index kept beside card 402's copy. Returns
// how many were written to `out`, or -1 when the index cannot answer (no copy, table full, more than `max` actors
// carry the name, or the name is over 255 chars): the caller walks the levels. Game thread only.
inline constexpr int32_t kC407Hits = 32;
struct C50NameRef;
int32_t c407_named(const void* world, const C50NameRef& ref, C402New* out, int32_t max);
// Card 330 split (diagnostic): c140_placed_visit calls past the placed/not-replicated early-out, and calls that reach
// c140_placed_entry. Counted by c140_placed_visit, read and zeroed by the card 132 ext totals row.
extern uint32_t g_c140_visit_passed;
extern uint32_t g_c140_visit_entry;
void c132_ext_step(void* engine);                                                       // both, per tick
void c137_host_frame(bool begin);  // client: a host frame's handler begins / ends (the ledger's recv callback)
void c137_own_input(bool begin);   // the own pawn's Interact begins / ends
void c137_client_change(const void* actor, const char* field, int32_t old_value, int32_t new_value,
                        unsigned long long caller); // card 137 2a: client, log only
void c137_host_apply(const void* actor, const char* field, int32_t old_value, int32_t new_value,
                     unsigned long long caller); // card 149: a host row applied past the detours, cause "host row"
void c137_change_dump(const char* why);
void c137_change_arm_map();
void c137_pickup_probe(const void* world, const char* when, bool detail); // card 137 2c/P7: load + 2 s, + 12 s, then summaries
void c132_ext_replay(void* pc);                                                          // host: a joining PC's first Pong (card 137 3a)
bool c98_send_ext(void* pc, const char* name, const Vec3& at, uint32_t value);         // host: one 'X' row to one PC
bool c98_send_ext_pack(void* pc, const char* text);  // host: one card 420 'x' frame to one PC
void c420_client_ext_pack(void* pc, const char* pairs); // client: an 'x' frame's "n hash value ..." (ext_c132.cpp)
// Card 137 step 6b (gib_c137.cpp): gib parts from the host only.
using ImpartFn = void (*)(void* part, const Vec3* a, const Vec3* b, const Vec3* c); // ImpartInitialRotationAndImpulse
extern ImpartFn g_orig_c137_impart_base;
extern ImpartFn g_orig_c137_impart_mesh;
void detour_c137_impart_base(void* part, const Vec3* a, const Vec3* b, const Vec3* c);
void detour_c137_impart_mesh(void* part, const Vec3* a, const Vec3* b, const Vec3* c);
void c137_receive_gib(void* pc, const WireString* s); // client: ChanOp::GibPart
void c137_receive_gib_pose(void* pc, const WireString* s); // client, card 519: ChanOp::GibPose
void c137_gib_tick(ULONGLONG now);                    // every tick, both sides (from c42_step)
void c137_gib_dump(const char* why);
void* c42_spawn_exact(void* pc, void* data_cls, const Vec3& loc, float f, const Vec3& a, const Vec3& b,
                      const Vec3& c); // client: one host part from its pool (enemies.cpp)
int32_t c42_kind(const void* cls); // the pool kind of a gore part data class, or -1 (enemies.cpp)
// Card 137 step 6c (change_c137.cpp): host zone damage log.
using ZoneDamageFn = void (*)(void* zone, void* actor); // ASTHazardousZoneBase::DamageActor
extern ZoneDamageFn g_orig_c137_zone_damage;
void detour_c137_zone_damage(void* zone, void* actor);
// Card 132 step 2 (digest_c132.cpp): the host's world state digest, compared on the client.
bool c104_copy_host_id(const void* actor, uint32_t& host_id); // client: our card 104 copy's host object index
void c132_arm_map();                                           // per map, from c98_arm_map
void c132_digest_step(void* engine);                           // every tick: the host's round timer and frames
void c132_digest_now(const void* world);                       // host: a round now (`objectives` command)
void c132_receive(void* pc, const WireString* s);              // client: ChanOp::Digest
void c132_host_query(void* pc, const WireString* s);           // host: ChanOp::DigestQuery
void c328_host_row(void* pc, uint64_t guid); // card 328, host: DigestQuery "Z<guid>" (an NPC a client clamped)
// Card 132 (ledger_c132.cpp): the send ledger. c132_ledger_send and c132_ledger_recv are the ue::chan on_send and
// on_recv; handlers of world-state ops report what the received frame did with c132_ledger_result.
// Pending: the handler kept the row and applies it later (its own apply line or WARN reports the outcome).
enum class C132Result : uint8_t { Applied, Skipped, Refused, Pending };
struct C132LedgerKey {
    uint32_t seq;     // the last row received for the key
    uint32_t sent_ms; // the sender's stamp
    uint32_t at_ms;   // its handler's end here (masked like the stamp)
    const char* op;
};
ue::chan::ChanStamp c132_ledger_send(void* pc, const WireString& s, bool to_host, const char* unsent, uint64_t enq_ms);
void c132_ledger_recv(void* pc, wchar_t op, const ue::chan::ChanStamp& stamp, bool stamped, ue::chan::ChanRecv phase,
                      const WireString& s);
void c132_ledger_result(C132Result result, const char* reason); // reason may be null
void c132_ledger_rtt(const void* pc, uint32_t rtt);             // card 026 pong
void c132_ledger_ack_to(void* pc);                              // host: in its ping to a remote PC
void c132_ledger_ack(void* pc, uint32_t ack);                   // client: ChanOp::LedgerAck
void c132_ledger_step(void* pc0, ULONGLONG now);                // both: from the card 026 ping step
uint32_t c132_ledger_sent_seq(const void* pc);                  // the last seq sent to pc (0: none)
uint32_t c132_ledger_recv_seq(const void* pc);                  // the last seq received from pc (0: none)
bool c132_ledger_clock_offset(const void* pc, int32_t* offset); // card 313: own ms - pc's ms (false: no sample yet)
int32_t c132_ledger_frame_age_ms(const void* pc, int32_t max_ms); // card 408 F5: the handled frame's age (-1: none)
void c132_ledger_session_end();                                // card 385: client session over, drop the clock minimum
bool c132_ledger_key_row(uint32_t key, C132LedgerKey& out);     // the last row received for a digest key
void c132_ledger_arm_map();                                     // per map, from c132_arm_map
void c132_ledger_report(const char* why);                       // every op, both directions
extern HealthIsDeadFn g_c130_is_dead;
// FHealthCompDamageEventDelegate::Broadcast: `delegate` is the multicast field, `event` an FADDamageEvent.
using HealthDamageBroadcastFn = void (*)(const void* delegate, const void* event);
extern HealthDamageBroadcastFn g_c131_broadcast_damage;
// Card 163: host, a placed satchel building destroyed by a damage event is relayed as an 'S' row (world_c98.cpp).
extern HealthDamageBroadcastFn g_orig_c163_broadcast;
void detour_c163_broadcast(const void* delegate, const void* event);
void c125_client_turret(void* pc, const char* name, const Vec3& at, uint32_t value);
void c467_map_end(); // card 467: the client's held turret rows start over
void c125_host_aim(void* pc, const WireString*, const ChanArgs& args);
extern SetBoolFn g_set_arms_visibility; // card 125: ASTPlayerCharacterMI::SetArmsVisbility
extern SetWorldRotationFn g_set_rel_rotation; // USceneComponent::SetRelativeRotation (same parameters)
// Card 359: USceneComponent::SetRelativeScale3D(FVector); the FVector (3 doubles) goes by pointer to a copy.
using SetRelScaleFn = void (*)(void* component, const double* scale);
extern SetRelScaleFn g_set_rel_scale; // pose_c132.cpp
// Card 359 (b191): USceneComponent::AddWorldOffset(FVector delta, bool sweep, FHitResult* hit, ETeleportType); the
// FVector goes by pointer to a copy, ETeleportType is a uint8 enum (1 = TeleportPhysics).
using AddWorldOffsetFn = void (*)(void* component, const double* delta, bool sweep, void* hit, uint8_t teleport);
extern AddWorldOffsetFn g_add_world_offset; // pose_c132.cpp
// Card 114: the difficulty values file (difficulty.cpp). The damage detour takes card 040's GetModifiedDamageByTags
// row (always installed) and calls detour_c40_by_tags.
using C114GetterFn = float (*)(const void* difficulty);
using C114CurrentFn = void* (*)(const void* context);
using C114LoadFn = bool (*)(void* difficulty);
extern C114GetterFn g_orig_c114_ammo;
extern C114GetterFn g_orig_c114_heal;
extern C114GetterFn g_orig_c114_fedscore;
extern C114CurrentFn g_c114_current;
extern C114LoadFn g_c114_load;
using C491LevelFn = uint8_t (*)(const void* difficulty); // USTDifficulty::GetCurrentDifficultyLevel (card 491 diag)
extern C491LevelFn g_c491_difficulty_level;
float detour_c114_damage(const void* subsystem, float damage, const void* instigator_tags, const void* receiver_tags);
float detour_c114_ammo(const void* difficulty);
float detour_c114_heal(const void* difficulty);
float detour_c114_fedscore(const void* difficulty);
// Card 117: USTSelfHealAbility::OnGiveAbility (log only): the regen period comes from USTDifficulty +0x44 read inline.
using C114HealGiveFn = void (*)(void* ability, const void* actor_info, const void* spec);
extern C114HealGiveFn g_orig_c114_heal_give;
void detour_c114_heal_give(void* ability, const void* actor_info, const void* spec);
void c114_step(void* engine);
void c114_arm_map(); // c50_arm_map: the old world is gone
void c114_wave(const void* data);
void c114_send(void* pc);
void c114_receive(void* pc, const WireString* s);
// Card 112: waves set up but not started are regenerated for a new player count (bug_scale.cpp).
using C112SetProceduralFn = void (*)(void* wave, int32_t budget, const void* procedural);
using C112ActivateFn = void (*)(void* wave);
extern C112SetProceduralFn g_c112_set_procedural;
extern C112ActivateFn g_orig_c112_activate;
void detour_c112_activate(void* wave);
void c99_arm_map();
// Card 102: an acting-player scope for that gate. On the host, inside BeginComponentOverlap, ApplyDamageFromImpactHit
// Result and Interact, when the acting actor belongs to a server copy, an index-0 call returns that copy's controller
// whatever the context (world actors ask "is this the player?" with their own context). Innermost scope wins.
enum C102Entry : uint8_t { kC102None, kC102Overlap, kC102Damage, kC102Interaction, kC102Enemy, kC102Entries };
struct C102Scope {
    const void* acting; // the server copy, or null
    uint8_t entry;      // C102Entry
    const void* site;   // card 302: the world actor acted on (the overlapped pickup, the interaction target), or null
};
constexpr size_t kC102OverlapComponent = 0xE0; // FOverlapInfo: TWeakObjectPtr<UPrimitiveComponent> of the other side
C102Scope c102_enter(const void* actor, uint8_t entry, const void* site = nullptr);
void c102_leave(const C102Scope& saved);
void c102_interact(void* interactor);
using C102OverlapFn = void (*)(void* component, const void* info, bool notify);
extern C102OverlapFn g_orig_c102_overlap;
void detour_c102_overlap(void* component, const void* info, bool notify);
using C99BeginPlayFn = void (*)(void* actor, bool from_streaming);
extern C99BeginPlayFn g_orig_c99_begin_play;
void detour_c99_begin_play(void* actor, bool from_streaming);
// Card 138 (begin_c138.cpp): client, AGameStateBase::OnRep_ReplicatedHasBegunPlay is held until the local PC has a
// possessed BP_PlayerCharacterMI. c138_step releases it (engine tick); c138_hold_ping (the card 120 Pong gate) tries a
// release and is true while it is held; c138_note_begin_play logs BP_PickupTracker's pawn (client); c138_load_map drops a hold at a map change.
using C138OnRepFn = void (*)(void* game_state);
extern uint32_t g_c138_hold_ms; // UBWCOOP_C138_HOLD_MS (test): minimum hold, pawn or not
extern C138OnRepFn g_orig_c138_on_rep;
void detour_c138_on_rep(void* game_state);
bool c138_hold_ping();
bool c138_holding();
void c138_step();
void c138_note_begin_play(const void* actor);
void c138_load_map();
using C99Sound2DFn = void (*)(const void* ctx, void* sound, float volume, float pitch, float start,
                              void* concurrency, const void* owner, bool ui);
using C99SpawnSound2DFn = void* (*)(const void* ctx, void* sound, float volume, float pitch, float start,
                                    void* concurrency, bool persist, bool auto_destroy);
using C99AdSound2DFn = void (*)(void* manager, const void* ctx, void* sound, float volume, float pitch, float start,
                                void* concurrency, const void* owner);
using C99SoundFNameFn = void (*)(void* ctx, uint64_t name, const Vec3* location, const void* rotation, float volume,
                                 float pitch, float start, void* attenuation, void* concurrency, void* owner);
using C99ShakeFn = void* (*)(void* manager, const void* const* shake_class, float scale, uint8_t space, const void* rotation);
using C99ScoreTextFn = void (*)(void* subsystem, uint64_t tag, void* text, int32_t amount);
using C99ScoreFn = void (*)(void* subsystem, uint64_t tag);
extern C99Sound2DFn g_orig_c99_sound_2d;
extern C99SpawnSound2DFn g_orig_c99_spawn_sound_2d;
extern C99AdSound2DFn g_orig_c99_ad_sound_2d;
extern C99SoundFNameFn g_orig_c99_sound_fname;
extern C99ShakeFn g_orig_c99_shake;
extern C99ScoreTextFn g_orig_c99_score_text;
extern C99ScoreFn g_orig_c99_score;
void detour_c99_sound_2d(const void* ctx, void* sound, float volume, float pitch, float start, void* concurrency,
                         const void* owner, bool ui);
void* detour_c99_spawn_sound_2d(const void* ctx, void* sound, float volume, float pitch, float start,
                                void* concurrency, bool persist, bool auto_destroy);
void detour_c99_ad_sound_2d(void* manager, const void* ctx, void* sound, float volume, float pitch, float start,
                            void* concurrency, const void* owner);
void detour_c99_sound_fname(void* ctx, uint64_t name, const Vec3* location, const void* rotation, float volume,
                            float pitch, float start, void* attenuation, void* concurrency, void* owner);
void* detour_c99_shake(void* manager, const void* const* shake_class, float scale, uint8_t space, const void* rotation);
void detour_c99_score_text(void* subsystem, uint64_t tag, void* text, int32_t amount);
void detour_c99_score(void* subsystem, uint64_t tag);
// Card 160: the host skips a server copy's local-only effects (2D sounds above, these three).
using C160MpcScalarFn = void (*)(void* ctx, void* collection, uint64_t name, float value);
using ExecThunkFn = void (*)(void* ctx, void* frame, void* result);
extern C160MpcScalarFn g_orig_c160_mpc_scalar;
extern ExecThunkFn g_orig_c160_exec_create;
extern CreateWidgetInstanceFn g_orig_c160_widget_world;
void detour_c160_mpc_scalar(void* ctx, void* collection, uint64_t name, float value);
void detour_c160_exec_create(void* ctx, void* frame, void* result);
void* detour_c160_widget_world(void* world, void* const* cls, uint64_t name);
// Card 160: the server copy's IronSights reads its client's toggle-ADS settings and input device.
using C160SettingsFn = void* (*)();                    // USTGameUserSettings::GetSTGameUserSettings
using C160BoolGetFn = bool (*)(const void* self);      // IsToggleADS*Enabled, CurrentInputGamepad
using C160BoolSetFn = void (*)(void* self, bool on);   // SetToggleADSKBM (a field write)
using C160SubsystemFn = void* (*)(const void* game_instance); // UGameInstance::GetSubsystem<UADInputDetectionSubsystem>
extern C160SettingsFn g_c160_settings;
extern C160BoolGetFn g_c160_toggle_kbm;
extern C160BoolGetFn g_c160_toggle_ctrl;
extern C160BoolSetFn g_c160_set_toggle_kbm;
extern C160SubsystemFn g_c160_input_subsystem;
extern C160BoolGetFn g_c160_gamepad;
extern ExecThunkFn g_orig_c160_exec_toggle_kbm;
extern ExecThunkFn g_orig_c160_exec_toggle_ctrl;
extern ExecThunkFn g_orig_c160_exec_gamepad;
void detour_c160_exec_toggle_kbm(void* ctx, void* frame, void* result);
void detour_c160_exec_toggle_ctrl(void* ctx, void* frame, void* result);
void detour_c160_exec_gamepad(void* ctx, void* frame, void* result);
uint32_t c160_client_ads_bits(const void* pawn);          // client: the settings bits for an ADS edge
void c160_host_ads_bits(const void* pc, uint32_t arg);   // host: an ADS edge's bits, kept for that PC's server copy
void detour_c49_visibility(void* ctx, uint64_t id, bool b0, bool b1);
void detour_c49_callout(void* ctx, uint64_t id);
bool detour_c49_check_all(void* ctx);
bool detour_c49_gate(void* ctx, uint64_t id, void* ids);
void detour_c49_save(void* engine, void* metadata, void* delegate, bool flag);
void c49_world_check(void* pc);
void c53_test_step(void* pc, ULONGLONG now);
void c53_step(void* pc, ULONGLONG now);
bool detour_is_below_kill_threshold(void* actor);
void c46_follow_step(const void* world, void* pc, ULONGLONG now);
// Test driver (UBWCOOP_C46_TEST). Host: (a) the copy dropped to KillZ - kC46DropBelow, (b) the host's pawn,
// (c) the copy at Health 20 (5 if the hit left it Up), then the bleedout to Out, (d) the host's pawn with the copy
// Out: vanilla death and game over. Solo: one host drop. Client: shots after each warp and on the death screen.
enum class C46Stage : uint8_t { WaitFoot, DropCopy, DropHost, LowCopy, DropLow, WaitOut, DropAll, Over, Done };
struct C46Test {
    C46Stage stage;
    ULONGLONG at_ms;   // the stage acts from here
    ULONGLONG drop_ms; // the current drop; 0 = not dropped yet
    uint32_t count_at; // the watched counter at the drop
    uint32_t low_try;  // test (c): index into kC46LowHealth; the next try after a hit left the copy Up
    double host_z;     // client: the host's pawn z last tick
    bool host_z_set;
    uint32_t warps_seen; // client: Warp frames shot
    bool over_shot;
    ULONGLONG done_ms; // client: the run ends
};
extern C46Test g_c46t;
void c46_test_host(void* world, ULONGLONG now);
void c46_test_client(void* world, ULONGLONG now);
// --- Card 050: weapon sync ---
// Host: each server copy's weapon list (the definition class path of each weapon and the equipped index) goes to its
// client as one Weapons text frame on the client's first Pong, on a new copy pawn, after the copy's
// LoadAbilitySystemComponent, and whenever the list changes (polled every kC50PollMs). Client: the own pawn's weapon
// inventory is made to match the last list (extras removed, missing ones given, the equipped one equipped) when it
// arrives, on a new own pawn and after AssignStartingInventories. The host is the authority: the client's own
// pickups only stay when the server copy took them too, which step 3a makes happen.
// Step 3a: a client's Use press on the host is relayed to the server copy's Interact when the target is a weapon,
// item or tactical support pickup. The client sends the name of the placed actor its interactor targets (UseName)
// just before the Use; when the server copy's own trace target (+0x100) has another name, the host finds the placed
// pickup by that name and sets it as the copy's interactor target before Interact (Addendum 5).
constexpr int32_t kC50Weapons = 8;          // weapons per list
constexpr int32_t kC50PathMax = 160;        // chars per class path
constexpr int32_t kC50Items = 16;           // inventory entries looked at
constexpr size_t kC50ItemStride = 16;       // UADInventory.Items entry size; the weapon pointer is first
constexpr size_t kC50ObjectFlags = 0x8;     // UObjectBase::ObjectFlags
constexpr uint32_t kC50WasLoaded = 0x80000; // RF_WasLoaded: placed in a level, so the same name on both machines
constexpr ULONGLONG kC50UseNameMs = 2000;   // host: a UseName counts for a Use press this long
constexpr double kC50UseRangeUu = 500.0;    // host: the named target must lie this near the client's location (card 133)
constexpr uint32_t kC50Lines = 200;      // card 050 event lines
using C50ClassFn = void (*)(void* inventory, void* const* definition_class); // TSubclassOf by hidden pointer
using C50IndexFn = void (*)(void* inventory, int32_t index);
using C50SelfFn = void (*)(void* self);
using C50PickupWeaponFn = void (*)(void* pickup, void* character);
using C50SaveFn = void (*)(void* context, void* data, bool flag); // FSTPlayerLoadPointData by hidden pointer
extern C50ClassFn g_orig_c50_remove_by_class;
extern C50ClassFn g_orig_c50_equip_by_class;
extern C50IndexFn g_orig_c50_equip_by_index;
extern C50SelfFn g_orig_c50_unequip;
extern C50SelfFn g_orig_c50_pickup;
extern C50SelfFn g_orig_c50_disable_pickup;
extern C50SelfFn g_orig_c254_pickup_implementation; // card 254
extern C50PickupWeaponFn g_orig_c50_pickup_weapon;
extern C50SaveFn g_orig_c50_save_checkpoint;
extern StaticClassFn g_c50_weapon_pickup_class;
extern StaticClassFn g_c56_pickup_class;
using C50OverlapFn = void* (*)(const void* pickup); // AADPickupBase::GetOverlappingCharacter
extern C50OverlapFn g_c50_overlapping_character;
// Card 145: a pickup's bEnabled on the host reaches the client as a world row (world_c98.cpp: 'E' for a placed
// pickup, 'N' for a card 104 copy). The host runs it after every AADPickupBase::OnUpdatePickupStatus; the client
// applies a row with the game's SetPickupEnabled. It replaces card 056's name-only PickupGone, which matched the first
// of two same-named placed pickups and never sent a re-enable (playtest 2026-11-08 finding 17).
using C145SetEnabledFn = void (*)(void* pickup, bool enabled); // AADPickupBase::SetPickupEnabled
extern C50SelfFn g_orig_c145_update_status;
extern C145SetEnabledFn g_c145_set_enabled;
void detour_c145_update_status(void* pickup);
void c145_host_status(const void* pickup); // world_c98.cpp
uint32_t c145_enabled(const void* pickup); // world_c98.cpp: the bEnabled byte +0x2A8, 1 or 0
extern bool g_c221_host_enable; // world_c98.cpp: client, a host row is applying SetPickupEnabled (card 221)
bool c221_host_disabled(const void* pickup); // world_c98.cpp: client, the host's last row for it was 0 (card 221)
constexpr size_t kPickupEnabledByte = 0x2A8; // AADPickupBase::bEnabled (card 132 digest pickup bit; see digest_c132.cpp)
// USceneComponent::bVisible: SetVisibility 0x03AF7060 writes bit 5 of +0x1A0 (card 132 digest mesh bit; digest_c132.cpp).
inline constexpr size_t kSceneVisibleByte = 0x1A0;
inline constexpr uint8_t kSceneVisibleBit = 0x20;
extern StaticClassFn g_c50_item_pickup_class;
extern StaticClassFn g_c50_tactical_pickup_class;
extern StaticClassFn g_c50_weapon_class;
extern StaticClassFn g_c50_interactable_class;
extern StaticClassFn g_c54_switch_class;
extern StaticClassFn g_c54_zipline_class;
extern StaticClassFn g_c54_satchel_site_class;
extern StaticClassFn g_c54_secret_class;
extern StaticClassFn g_c54_limited_class;
// Card 289 (world_c98.cpp): ASTSecretPickup::Pickup_Implementation's credit, relayed. Host: a credited placed secret
// sends a journalled 'G' row. Client: the row runs the same credit once per secret; its own take skips the credit.
using C289MetaFn = void* (*)(const void* context);                  // USTCommonUtilityLibrary::GetSTMetaGame
using C289LockedFn = bool (*)(const void* context, const void* tag); // USTSecretsUtilityLibrary::IsSecretLocked
using C289UnlockFn = void (*)(const void* context, const void* tag); // USTSecretsUtilityLibrary::UnlockSecret
extern C50SelfFn g_orig_c289_secret_pickup;
extern C289MetaFn g_c289_get_meta;
extern C50SelfFn g_c289_secret_found;
extern C289LockedFn g_c289_is_locked;
extern C289UnlockFn g_c289_unlock;
void detour_c289_secret_pickup(void* pickup);
// Card 054: the interactables a client Use is relayed to, first match wins (subclasses before their parents). A
// target of any other class (mech, turret, dropship) is logged and not relayed: the client's own run stands.
enum class C54Kind : uint8_t { Weapon, Item, Tactical, Secret, Switch, SatchelSite, Zipline, Limited };
struct C54Relay {
    const StaticClassFn* cls;
    C54Kind kind;
    const char* label;
};
constexpr C54Relay kC54Relays[] = {
    {&g_c50_weapon_pickup_class, C54Kind::Weapon, "weapon pickup"},
    {&g_c50_tactical_pickup_class, C54Kind::Tactical, "tactical support pickup"},
    {&g_c54_secret_class, C54Kind::Secret, "secret pickup"},
    {&g_c50_item_pickup_class, C54Kind::Item, "item pickup (satchel, crates)"},
    {&g_c54_switch_class, C54Kind::Switch, "switch"},
    {&g_c54_satchel_site_class, C54Kind::SatchelSite, "satchel placement site"},
    {&g_c54_zipline_class, C54Kind::Zipline, "zipline point"},
    {&g_c54_limited_class, C54Kind::Limited, "limited interaction actor"},
};
constexpr int32_t kC54RelayRows = static_cast<int32_t>(sizeof(kC54Relays) / sizeof(kC54Relays[0]));
inline constexpr size_t kC104ChargeText = 64; // card 104: TacCharge text, 4 slots of "charge ready"
struct C50Copy {
    int32_t pc_id;
    int32_t pawn_id;
    const char* force; // static text: why the next poll sends even without a change; null = only on a change
    ULONGLONG next_ms;
    bool ponged;
    bool seed; // card 072: the copy's AmmoSet is still due for this pawn
    char sent[kChanTextMax + 1];
    char ammo[kChanTextMax + 1]; // card 072: the client's last AmmoMirror text for this pawn; re-applied each poll
    char charge[kC104ChargeText]; // card 104: the last TacCharge text sent for this copy
    char row[96]; // card 327: the last player row logged for this copy's slot
    uint32_t role; // card 502 finding 2: the client's test role (1 "client", 2 "client2"); 0 none sent
};
// Card 230: a c95_wire_target name ("parent/component" for a child actor, each part at most kChanNameMax). Every
// buffer that holds or parses one is kC95TargetMax + 1; kChanNameMax stays 64.
constexpr int32_t kC95TargetMax = 2 * kChanNameMax + 1;
// Card 283: the longest plain actor name a world or collision row carries (a placed child actor's own name can
// run past kChanNameMax, e.g. 82 characters). Named frames keep kChanNameMax.
constexpr int32_t kC95ActorNameMax = static_cast<int32_t>(kAssetPathChars) - 1;
struct C50State {
    C50Copy copy[kC17Slots]; // host
    int32_t copies;
    int32_t asc_pawn_id;     // host: the last server copy through LoadAbilitySystemComponent
    char use_name[kC95TargetMax + 1]; // card 230
    int32_t use_pc_id;
    ULONGLONG use_name_ms;
    Vec3 use_at;             // host: the client target's location sent with use_name (card 133)
    bool use_at_ok;
    int32_t relayed_id;      // host: the copy whose press was relayed; its release is relayed too
    char text[kChanTextMax + 1]; // client: the last Weapons text
    bool have_text;
    bool pending;            // client: apply on the next tick with an own pawn on foot
    bool applying;           // client: inventory calls below are card 050's own
    int32_t own_id;
    ULONGLONG chapter_ms;    // client: when card 049's Chapter frame was applied; 0 = not this map
    ULONGLONG switch_due_ms; // client, card 312: no apply before this (the client's switch request is in flight)
    uint32_t sends, recvs, applies, relays, matches, mismatches, unguarded_gives, lines, audit_lines, shots;
    uint32_t pickup_blocks, pickup_block_lines; // client, card 140: the game's DisablePickup blocked on a placed pickup
    uint32_t c327_rows; // host, card 327: player rows logged this map (cap kC327Rows)
};
extern C50State g_c50;
struct C50List {
    void* weapon[kC50Items];
    int32_t count;
    int32_t equipped; // index into weapon, -1 = none
    int32_t skipped;  // entries that are not an AADWeapon
};
bool c50_line();
bool c50_placed(const void* object);
int32_t c50_name(const void* object, char* out, int32_t cap);
bool c50_name_is(const void* object, const char* name, int32_t name_len); // card 404
// Card 405: one by-name scan's name; c50_name_match is c50_name_is that remembers the name's FName (see hooks_ubw.cpp).
struct C50NameRef {
    const char* name;
    int32_t name_len; // -1: longer than the caller's cap, matches nothing
    uint64_t hash;
    uint32_t idx;
    uint32_t number;
    bool known; // idx and number are set
};
C50NameRef c50_name_ref(const char* name, size_t max_len);
bool c50_name_match(const void* object, C50NameRef& ref);
int32_t c50_class_path(const void* cls, char* out, int32_t cap);
// Card 312: an Items entry as a weapon: the entry itself when it is an AADWeapon, else null (an empty slot or another
// item kind). Every read of a weapon field (kWeaponDefinitionClass) from an Items entry goes through it.
void* c50_weapon(void* entry);
C50List c50_list(void* inventory);
int32_t c50_text(void* inventory, char* out, int32_t cap);
// Card 312: the equipped slot (EquippedItemIndex) of a weapon inventory, -1 without one; and the slot layout as log
// text, "<equipped slot>|<slot>=<definition>|..." (Items positions, empty slots left out). Logging only.
int32_t c50_equipped_slot(void* inventory);
int32_t c50_slot_text(void* inventory, char* out, int32_t cap);
void c50_totals(const char* what);
C50Copy* c50_copy(int32_t pc_id, bool create);
// Card 327 PW (weapons.cpp): one player's inventory hash (weapon list, ammo map, tactical charge), 16 bits;
// kC327NoHash when there is none to compare. The host's goes in each PC's digest B frame, the client compares its own.
inline constexpr uint32_t kC327NoHash = 0x10000;
struct C327Texts {
    char weapons[kChanTextMax + 1];
    char ammo[kChanTextMax + 1];
    char charge[kC104ChargeText];
};
uint32_t c327_host_hash(void* pc);
uint32_t c327_own_hash(const void* pawn, C327Texts& t);
void c50_host_step(void* world, ULONGLONG now);
void c50_peer_host_step(void* world, ULONGLONG now); // card 050 b205: every player's list to every client
void c50_peer_force(); // card 050 b205: send every list again (a client's first Pong)
void c50_host_nudge();  // card 495: a host inventory changed; the Weapons and PeerWeapons compares run next step
void c50_peer_arm_map();
void* c50_resolve(const char* path);
void c50_client_step(void* pc, ULONGLONG now);
void c50_peer_client_step(void* pc, ULONGLONG now); // card 050 b205: other players' copies
const C54Relay* c54_relay_row(const void* target);
void* c50_pick_target(void* pawn, void* interactor);
// Card 054: a pawn's location logged once a second for a while (the zipline ride, on both machines).
constexpr size_t kC54SwitchEnabled = 0x40; // ASTSwitch: interface +0x40, the enabled byte (Addendum 1)
constexpr ULONGLONG kC54TrackMs = 12000;
constexpr ULONGLONG kC54TrackEveryMs = 1000;
struct C54Track {
    void* pawn;    // read only while weak still resolves to it
    C31Weak weak;  // card 319 run 1 class fix: liveness by weak ref, never object_index on a maybe-freed pointer
    const char* what; // static text
    ULONGLONG until_ms;
    ULONGLONG next_ms;
};
extern C54Track g_c54_track;
void c54_track(void* pawn, const char* what);
int32_t c54_switch_enabled(const C54Relay* row, const void* iface);
void c50_interact(void* pawn, void* interactor, void* target, const C54Relay* row);
// Card 095: placed doors (ASTDoor) do not replicate either, and the level script opens them on the host only (the
// capture-the-flag gate of FED_C1M1): the client's own copy stays shut and blocks the client. The host sends each
// OpenDoor, CloseDoor and SetDoorActive on a placed door by name (DoorOp "<name> <op>") to each client that has sent
// a Pong, and keeps the door's last open and active state in a per-map journal that a client's first Pong of a map
// gets whole. The client calls the same function on its own copy, found by name.
using DoorFn = void (*)(void* door);
using DoorActiveFn = void (*)(void* door, bool active);
extern StaticClassFn g_c95_door_class;
extern DoorFn g_orig_c95_open;
extern DoorFn g_orig_c95_close;
extern DoorActiveFn g_orig_c95_active;
void detour_c95_open(void* door);
void detour_c95_close(void* door);
void detour_c95_active(void* door, bool active);
void c95_receive(void* pc, const WireString* s);
// Card 426: `name` may be a level key "<name>@<8 hex>" (c50_level_key): then only the copy in that level matches.
void* c50_find_placed(const void* world, const StaticClassFn* const* cls, int32_t n, const char* name);
// Card 426: "<c50_name>@<FNV-1a of its level's name, 8 hex>", for a wire name that must tell apart same-named placed
// actors of different sublevels (C5M1 has two BP_ProceduralDropshipTroops_C_0). Length or -1 (no name, no room).
int32_t c50_level_key(const void* actor, char* out, int32_t cap);
void* c50_find_placed_any(const void* world, const char* name); // card 236: any class, every actor of every level
void c95_replay(void* pc);
void c95_replay_step(); // card 219: host tick, the paced collision rows of each join replay
void c95_map_end();     // card 200: at LoadMap enter
void c95_host_door_state(void* door, uint8_t state, int buffer); // host: journal and send a BP door's DoorState (card 161)
// --- Card 161: BP_Door_Base doors host owned (door_c161.cpp) ---
extern bool g_c161_pe;                             // a door is tracked: detour_process_event calls c161_pe
bool c161_pe(const void* object, const void* function); // true: the client refuses this call on a door
// Card 507: void ProcessLocalScriptFunction(UObject* Context, FFrame& Stack, void* Result), every script function body
using LocalScriptFn = void (*)(void* context, void* stack, void* result);
extern LocalScriptFn g_orig_c507_local_script;
void detour_c507_local_script(void* context, void* stack, void* result); // client: refuses the BP VM's own door calls
void c161_step(void* engine);
bool c161_receive(const void* world, const char* name, char op, const char* text); // client: a DoorOp no ASTDoor took; true when taken
const char* c161_cmd(const char* name, uint8_t op); // host test command `door open|close` on a BP door
void c288_presence_fired(const void* pawn); // card 288, host: a player presence fires now (before its original)
void c288_trigger_at(Vec3 at, bool objective); // card 288, host: a player-trigger close starts now, `at` on the inner
                                                // side; card 498: `objective` makes its doors one hold group
void c492_switch_press(const void* target, const void* pawn); // card 492, host: a switch press sets the card 288 trigger
// --- Card 408: flak and plasma bug buildings' attack driven by the host (satchel_c408.cpp) ---
extern bool g_c408_pe;                                   // a building is on the map: detour_process_event calls c408_pe
bool c408_pe(void* object, const void* function, void* parms); // client: true refuses its own timer event or card 419 burst; host: notes it
void c408_after_pe(const void* object, const void* function); // host: the event's 'Q' row after it returned
void c408_step(void* engine);
void c408_arm_map();
void c408_map_end(); // card 477: LoadMap enter and test quit
bool c408_host_row(const void* building, uint32_t value, const Vec3* at = nullptr); // world_c98.cpp: one live 'Q' row (`at`: card 419 burst location, else the building's); false when not sent
const char* c408_client_row(void* pc, const char* name, const Vec3& at, uint32_t value); // null: applied
float c313_advance_timeline(void* timeline, int32_t age_ms); // card 408 F5: a playing timeline moved on; -1: not playing
bool c408_building(const void* actor); // a building of the class (its TurnInPlace timeline rows are not sent)
bool c408_attack(const void* actor);   // a building of the class or its plasma projectile (effects stay local)
bool c408_projectile(const void* actor); // its plasma projectile only
// --- Card 158: the client's own held grenade (grenade_c158.cpp) ---
extern bool g_c158_pe;                             // client: a grenade ability is noted: detour_process_event calls c158_after_pe
void c158_note_ability(const void* ability);       // client, from c73_bind: record its OnAnimationStarted / OnDoAction / Success
void c158_after_pe(void* object, const void* function);
void c158_host_throw(const void* ability, const void* projectile, const Vec3& v); // host: a server copy throw's aim line
void c158_reset_map();
void c347_client_step(void* pc); // death.cpp c50_step, client: camera resend limit
// --- Card 169: AbilityTask SpawnActor held visuals on the client's own ability (spawn_c169.cpp) ---
// TargetData is by value (the caller's copy, by pointer; the callee destroys it); TSubclassOf by pointer.
using C169BeginSpawningFn = bool (*)(void* task, void* ability, void* target_data, void* const* cls, void** spawned);
using C169DestroyFn = void (*)(void* actor);
extern C169BeginSpawningFn g_orig_c169_begin_spawning;
extern C169DestroyFn g_orig_c169_destroy;
using C169ObjectGetFn = void* (*)(const void* self);
using C169MontagePositionFn = float (*)(const void* anim_instance, const void* montage);
extern C169ObjectGetFn g_c169_anim_instance;  // USkeletalMeshComponent::GetAnimInstance
extern C169ObjectGetFn g_c169_active_montage; // UAnimInstance::GetCurrentActiveMontage
extern C169MontagePositionFn g_c169_montage_position; // UAnimInstance::Montage_GetPosition
bool detour_c169_begin_spawning(void* task, void* ability, void* target_data, void* const* cls, void** spawned);
void detour_c169_destroy(void* actor); // AActor::K2_DestroyActor: a held visual's on-screen state at its end
// --- Card 174: the client's own flare in hand (spawn_c169.cpp) ---
// ServerTryActivateAbilityWithEventData(handle, InputPressed, FPredictionKey by pointer, FGameplayEventData by
// pointer to the caller's temporary, which the callee destroys).
using C169ServerTryFn = void (*)(void* asc, int32_t handle, bool pressed, const void* key, void* event_data);
using C174EventDataDtorFn = void (*)(void* event_data); // FGameplayEventData::~FGameplayEventData
extern C169ServerTryFn g_orig_c174_server_try;
extern C174EventDataDtorFn g_c174_event_data_dtor;
extern StaticClassFn g_c174_tac_class; // USTUseTacticalSupportAbility
void detour_c174_server_try(void* asc, int32_t handle, bool pressed, const void* key, void* event_data);
bool c174_block_throw(const void* component); // client own pawn: true when TryThrowFlare must not run
void c169_reset_map();
void c169_step(); // card 324: the held visual's view row 300 ms after its spawn
using C324ViewportSizeFn = void (*)(const void* pc, int32_t* x, int32_t* y); // APlayerController::GetViewportSize
extern C324ViewportSizeFn g_c324_viewport_size;
void c161_arm_map();
void c161_map_end(); // card 477 sweep: LoadMap enter and test quit
const char* c95_complete(void* world, void* pc, const char* id_text);
void c95_receive_collision(void* pc, const WireString* s);
void c421_receive_want(void* pc, const WireString* s); // card 421: client asks for whole rows
// Card 098: the 095 fanout and nearest-copy lookup, shared by the world relay (world_c98.cpp).
struct C95Fanout {
    int32_t to, sent;
    uint32_t ahead; // card 294, priority sends: the most frames queued ahead of the row on one connection
};
// priority (card 294): the row skips each connection's pace FIFO (chan_send_client_now); an op or row kind is always
// sent this way or never.
C95Fanout c95_send_all(const void* world, ChanOp op, const char* text, bool priority = false);
int32_t c95_ponged_count(const void* world); // host: remote PCs past their first Pong this map
bool c95_send_one(void* pc, ChanOp op, const char* text); // card 132: one PC
// Card 519: a blob frame of n bytes to c95_send_all's set (paced, never priority).
C95Fanout c95_send_blob_all(const void* world, ChanOp op, const uint8_t* bytes, int32_t n);
void* c95_find_nearest(const void* world, const char* name, const Vec3& at);
const void* c95_child_cac(const void* actor);
const void* c95_anchor(const void* actor);
int32_t c95_wire_target(const void* target, char* out, size_t cap);
void* c95_component(void* actor, const char* name); // the actor's component of this name (door_c95.cpp)
// profile (card 237): the component's new collision profile FName after a SetCollisionProfileName that changed it,
// else 0; a profile write is relayed even when CollisionEnabled stays the same.
void c95_relay_collision(const void* owner, const void* component, unsigned before, unsigned after,
                         unsigned long long caller, uint64_t profile = 0);
// ISTSignificanceObjectInterface::ProcessSignificanceUpdate (card 147): collision changes inside it are this machine's
// view LOD, never relayed.
using SignificanceChangedFn = void (*)(void* self);
extern SignificanceChangedFn g_orig_c95_process_significance;
void detour_c95_process_significance(void* self);
extern int32_t g_c95_significance_depth; // door_c95.cpp: inside this machine's significance update (card 483 reads it)
void c95_note_collision(const char* call, const void* owner, const void* component, unsigned before, unsigned after,
                        unsigned long long caller, const void* node);
const char* c95_cmd(void* world, const void* pawn, const char* name, uint8_t op);
void c453_arm_map();          // card 453 step 1: every map arm (door_c95.cpp)
void c453_step(void* engine); // card 453 step 1: host diag, the ASTDoor class rows once per map
void detour_c50_pickup_weapon(void* pickup, void* character);
void detour_c50_pickup(void* pickup);
void detour_c50_disable_pickup(void* pickup);
void detour_c254_pickup_implementation(void* pickup); // card 254
const void* c50_client_pickup_overlapper(void* pickup);
void detour_c50_save_checkpoint(void* context, void* data, bool flag);
void detour_c50_remove_by_class(void* inventory, void* const* cls);
void detour_c50_equip_by_class(void* inventory, void* const* cls);
void detour_c50_equip_by_index(void* inventory, int32_t index);
void detour_c50_unequip(void* inventory);
constexpr size_t kC57NetExecutionPolicy = 0x198; // UGameplayAbility, one byte (docs/STATE.md)
using C57EquipFn = void (*)(void* pawn, int32_t index);
using C57ActivateFn = void (*)(void* ability, int32_t handle, const void* actor_info, const void* activation_info,
                               const void* event_data);
using C57CanActivateFn = bool (*)(const void* ability, int32_t handle, const void* actor_info, const void* source_tags,
                                  const void* target_tags, void* relevant_tags);
using C57RequestFn = void* (*)(const void* pawn, void* ret, int32_t a, int32_t b, const void* actor);
using C57CycleFn = void (*)(void* inventory, bool forward);
extern C57EquipFn g_orig_c57_equip_weapon;
extern C57ActivateFn g_orig_c57_activate;
extern C57CanActivateFn g_orig_c57_can_activate;
extern C50SelfFn g_orig_c57_begin_switch;
extern C57RequestFn g_orig_c57_request;
extern C57CycleFn g_orig_c57_cycle;
extern void* g_c57_cdo;
void detour_c57_equip_weapon(void* pawn, int32_t index);
void detour_c57_activate(void* ability, int32_t handle, const void* actor_info, const void* activation_info,
                         const void* event_data);
bool detour_c57_can_activate(const void* ability, int32_t handle, const void* actor_info, const void* source_tags,
                             const void* target_tags, void* relevant_tags);
void detour_c57_begin_switch(void* ability);
void* detour_c57_request(const void* pawn, void* ret, int32_t a, int32_t b, const void* actor);
void detour_c57_cycle(void* inventory, bool forward);
using C141SwitchFn = void (*)(void* pawn, int32_t index, int32_t direction);
using C141UnderslungStateFn = bool (*)(const void* weapon);
using C141ReceiveEventFn = void (*)(void* ability, const void* payload);
extern C141SwitchFn g_c141_send_switch;
extern C141UnderslungStateFn g_c141_underslung_state;
extern C57ActivateFn g_orig_c141_activate;
extern C141ReceiveEventFn g_orig_c141_receive;
void detour_c141_activate(void* ability, int32_t handle, const void* actor_info, const void* activation_info,
                          const void* event_data);
void detour_c141_receive(void* ability, const void* payload);
constexpr size_t kC62InstancingPolicy = 0xCF; // UGameplayAbility, one byte (docs/STATE.md)
using C62RespondFn = bool (*)(const void* ability, const void* actor_info, const void* event_data);
extern C62RespondFn g_orig_c62_respond;
extern SprintEndFn g_orig_c62_end;
bool c62_line();
bool c62_is_switch(const void* ability);
bool detour_c62_respond(const void* ability, const void* actor_info, const void* event_data);
void detour_c62_end(void* ability, int32_t handle, const void* actor_info, const void* activation_info,
                    bool replicate, bool was_cancelled);
enum class C50Stage : uint8_t { Wait, Look, Aim, Pressed, Released, Done };
struct C50Test {
    C50Stage stage;
    ULONGLONG at_ms;   // stage start
    ULONGLONG poll_ms; // client: next proximity check
    void* pickup;      // read only while pickup_weak still resolves to it
    C31Weak pickup_weak;
    bool host_moved;
};
extern C50Test g_c50t;
// Card 054: the driver's steps, in order (UBWCOOP_C50_TEST=054); card 050 (=1) runs the weapon pickup step only.
struct C54Step {
    const StaticClassFn* cls;
    const char* class_prefix; // class name prefix the target must have; null = any placed actor of cls
    const char* label;        // static text
};
const C54Step* c54_step();
void c50_test_host(const void* world, ULONGLONG now);
void c50_test_client(const void* world, void* pc, ULONGLONG now);
void c57_test_solo(const void* world, void* pc, ULONGLONG now);
struct C60Test {
    bool started;      // the solo stages own g_c50t
    ULONGLONG start_ms;
    ULONGLONG done_ms; // first tick with the solo stages Done
    ULONGLONG down_ms; // the 2 key went down
    bool released;
};
extern C60Test g_c60t;
void c60_press_two(void* pc, ULONGLONG now);
void c60_test_host_own(const void* world, void* pc, ULONGLONG now);
constexpr ULONGLONG kC63PollMs = 100;
constexpr ULONGLONG kC63StateMs = 250;
constexpr int32_t kC63Queue = 16;
struct C63 {
    cmdfile::Cmd q[kC63Queue]; // q[0] is the running or next command
    int32_t count;
    bool started;              // q[0] has started (a key is down or an axis run is under way)
    ULONGLONG until;           // q[0]: the release or end time
    uint8_t chord;             // q[0] Key chord (card 107): 0 key2 not yet down, 1 down, 2 released
    ULONGLONG next_poll;
    ULONGLONG next_state;
    uint32_t last_seq;         // the last finished command's seq
    wchar_t tag[ue::config::kMaxTagLen + 1];
};
extern C63 g_c63;
constexpr wchar_t kC64NavSysClass[] = L"/Script/NavigationSystem.NavigationSystemV1";
bool c64_set_cdo_bool(const wchar_t* path, const char* name);
constexpr size_t kFunctionFlags = 0xB0;     // UFunction::FunctionFlags (EFunctionFlags), before NumParms
constexpr size_t kC67WidgetVisibility = 0xDC; // UWidget::Visibility (ESlateVisibility), card 049 layout
constexpr int32_t kC67Kinds = 5;         // the four kC67Classes paths, then WB_Cutscenes_C (taken from a dump)
constexpr int32_t kC67MaxVisited = 48;   // objects printed per dump (a repeat prints one "seen above" line)
constexpr int32_t kC67Seen = 8;          // sighted objects kept per class (keys only)
constexpr int32_t kC67MaxCalls = 96;     // distinct (object, function) ProcessEvent pairs logged per map
constexpr int32_t kC67MaxListed = 32;    // classes whose functions were listed
constexpr ULONGLONG kC67LaterMs = 5000;
struct C67Call {
    const void* object;
    const void* function;
};
struct C67 {
    const void* cls[kC67Kinds];
    const void* seen[kC67Kinds][kC67Seen];
    int32_t nseen[kC67Kinds];
    const void* listed[kC67MaxListed];
    int32_t nlisted;
    C67Call calls[kC67MaxCalls];
    int32_t ncalls;
    uint32_t dumps;
    ULONGLONG later_ms; // non-zero: dump the live stack then (5 s after a pause, the screen drawn)
    ULONGLONG next_resolve;
    const void* visited[kC67MaxVisited]; // objects printed in the current dump
    int32_t nvisited;
    char map[64]; // the game world's name, taken in c67_step after each map reset ("" until then)
};
extern C67 g_c67;
int32_t c67_kind_of(const void* object);
bool c67_remember(int32_t k, const void* object);
void c67_dump(const void* stack, const char* why);
struct C67UiStep {
    const char* cls;     // the top widget's class
    const wchar_t* fn;   // its BP function, called through ProcessEvent
    bool first_true;     // set the function's first parameter (a bool) to true
};
const C67UiStep kC67UiSteps[] = {
    {"WB_Cutscenes_C", L"On Video Ended", true},                   // the skip: On Video Ended(Skipped=true)
    {"WB_Loading_Screen_Final_C", L"Remove Current Menu", false}, // the any-key close (WB_StackElement)
};
constexpr int32_t kC67UiStepCount = static_cast<int32_t>(sizeof(kC67UiSteps) / sizeof(kC67UiSteps[0]));
void* c67_ui_top(const void* stack, int32_t* num);
const uint8_t* c67_stack_array(const void* stack);
// Card 225 (cine_skip.cpp): the game's WB_Stack_C (the last pause requester) holds a widget. Both sides, play build.
bool cine_stack_open();
bool c67_ui_held(void* world, const void* stack);
const char* c67_ui_call(void* top, int32_t step);
int32_t c67_ui_step_of(const void* top);
// Card 130 (cine_skip.cpp): skip every WB_Cutscenes_C the moment its WB_Stack_C requests the pause, host and client.
void cine_note_pause(void* requester);
void cine_skip_step();
// Card 135 (intro_c135.cpp): the press-to-start screen on both sides (host continue sent, client GameStarted).
void c135_map_end();                                 // both sides, at LoadMap enter (ubw_load_map_enter)
void c135_note_pause(void* stack);                   // both sides, from cine_note_pause (a WB_Stack_C pause)
void c135_note_unpause(void* requester);             // host, from detour_request_unpause
void c135_join_replay(void* pc);                     // host, from sync_join_replay
void c135_receive(void* pc, const WireString* s);    // client: ChanOp::IntroStart
void c135_step();                                    // both sides, every tick (from cine_skip_step)
bool c135_screen_up();                               // client: its card 135 screen is up (death.cpp c156 check)
// Card 130: UMovieSceneSequencePlayer::Play detour (ubw/cine_skip.cpp). A cinematic level sequence (its package under
// a kCineSeqDirs folder) is queued and skipped next tick through its own GoToEndAndStop, the game's skip path.
using SequencePlayFn = void (*)(void* player);
extern SequencePlayFn g_orig_sequence_play;
void detour_sequence_play(void* player);
void c63_run(void* world, void* pc, ULONGLONG now);
void c63_write_state(void* engine, ULONGLONG now);
// --- Card 126: autoplay (test runner, UBWCOOP_CMD=1), autoplay.cpp; it walks and turns with testrun.cpp's code ---
const char* c64_goto_start(void* world, void* pc, const cmdfile::Cmd& c, ULONGLONG now); // seq 0: no result line
void c64_end(void* pc, const char* why, ULONGLONG now);
bool c64_walking();
bool c64_project(void* world, void* pawn, const Vec3& point, const Vec3& extent, Vec3& out); // navmesh point near `point`
double c124_turn(void* pc, double want_yaw, double want_pitch, ULONGLONG now);
Vec3 c124_view_rot(void* pc);
double c124_wrap(double deg);
bool c126_on();
const char* c126_set(void* pc, bool on, ULONGLONG now);
void c126_tick(void* world, void* pc, ULONGLONG now);
// --- Card 284: the objective picker, shared by autoplay (card 126) and the `objective` verbs (objective_c284.cpp) ---
// Defined in autoplay.cpp. Kinds (what a step needs) come from data rows, never guessed: an unknown class is Other.
// Marker: ActorReference_C anchor, or no actor with an objective marker actor (card 284 review 4, 5); Count: a count
// step with no actor and no children (review 6: no `defend` kind until a map shows one).
enum class C126Kind : uint8_t { Presence, Use, Nest, Kill, Marker, Count, Other };
enum class C126ShapeKind : uint8_t { None, Box, Sphere, Capsule };
// An overlap component's world shape: ComponentToWorld with its scale applied to the extent, as the engine's overlap
// test uses it. Box: ext = half extents; sphere: ext.x = radius; capsule: ext.x = radius, ext.z = half height.
struct C126Shape {
    C126ShapeKind kind;
    Vec3 center;
    double q[4]; // rotation quat X, Y, Z, W
    Vec3 ext;
};
struct C126Pick {
    const uint8_t* objective;
    void* actor; // ObjectiveActors[0]; null only when picked with need_actor false
    double dist;
    int32_t slot;
};
const uint8_t* c126_find(const void* world, uint64_t id);   // the objective with FName id, or null
void* c126_actor(const uint8_t* o);                         // ObjectiveActors[0], or null
const void* c126_active_parent(const SparseView& v);        // the open top-level objective whose steps alone show markers, else the lowest slot
const uint8_t* c126_root(const uint8_t* o);                     // the open top-level objective above o (o for a root), or null
// The active step under the active parent (or under an open sub of it: count containers' steps, card 284 5a), marker
// on or (keep) the one worked on; the last slot wins. need_actor false: steps with no ObjectiveActors count too.
bool c126_pick(const void* world, uint64_t current, bool keep, bool need_actor, const Vec3& from, C126Pick& out);
C126Kind c126_kind_of(const void* world, const uint8_t* o); // from its actors' classes and the objective's class
int32_t c126_children(const void* world, const uint8_t* o); // objectives whose parent is o (>0: a container, not a step)
void* c126_marker_of(const void* world, const uint8_t* o);  // the BP_ObjectiveMarkerActor_C of o, or null
const char* c126_kind_text(C126Kind k);                     // presence, use, nest, kill, marker, count, other
const char* c126_kind_needs(C126Kind k);                    // what the step needs, printed by `objective now`
double c126_num(const void* obj, const char* name, size_t sub); // reflected float or double property; 0 if missing
bool c126_shape_find(const void* actor, C126Shape& out, const void** comp); // its overlap shape; false: none
bool c126_shape_comp(const void* c, C126Shape& out); // c's shape when a box, sphere or capsule; false: none
bool c126_overlaps(const C126Shape& sh, const Vec3& p, double cr, double ch); // upright capsule p against the shape
bool c126_capsule_inside(const C126Shape& sh, const void* pawn, const Vec3& pos); // pawn's capsule at pos inside
// Navmesh point near target within kC126ProjectUu (tried in turn) and in z kC126ProjectDz, or shape sh's whole z
// extent when larger (sh may be null); *within: the x, y extent that hit.
bool c126_project(const void* world, void* pawn, const Vec3& target, const C126Shape* sh, Vec3& at, double* within);
void c64_set_yaw(void* pc, double yaw, double pitch);       // testrun.cpp: the local controller's view
// Card 284 verbs (objective_c284.cpp), host only; null on success, else why not (expect: the VOID text). The _step
// forms run every tick until they return true (*why null: passed); timed_out: the command's c.ms is up.
const char* c284_now(void* world, void* pc, uint32_t seq);
const char* c284_next(void* world, void* pc, uint32_t seq); // b238 `objective next`: the next doable step becomes current
bool c284_expect_step(void* world, void* pc, const char* id_text, bool timed_out, uint32_t seq, const char** why);
const char* c284_go_start(void* world, void* pc, uint8_t copy, uint32_t seq); // copy: who (0 host, 1 client, 2 client2, 3 all) plus cmdfile::kGoHome
const char* c284_go_settle(void* world, void* pc, uint32_t seq);             // after the settle (the go's c.ms)
bool c284_go_still(void* world, void* pc, uint64_t now, uint32_t seq);     // card 513: every tick of the settle
bool c284_wait_step(void* world, void* pc, const char* id_text, bool timed_out, const char** why); // true: ended
// --- Card 065 step 1: which level triggers fire on which machine (log only, UBWCOOP_C65_LOG=1) ---
// UADLevelTriggerComponent::Trigger itself is a shared empty `ret` (never hooked); the two reaction subclasses
// override it. Their Blueprint caller is named the card 014 way, on the execTrigger thunk -- which the linker shares
// with six other exec functions, so a context that is not a UADLevelTriggerComponent passes straight through.
using C65TriggerFn = void (*)(void* component);
using C65ActivateFn = void (*)(void* component, bool reset);
extern StaticClassFn g_c65_trigger_class;
extern C65TriggerFn g_orig_c65_trigger;
extern C65TriggerFn g_orig_c65_stateful_trigger;
extern ExecThunkFn g_orig_c65_exec_trigger;
extern ExecThunkFn g_orig_c65_exec_activate;
extern C65ActivateFn g_orig_c65_activate;
void detour_c65_trigger(void* component);
void detour_c65_stateful_trigger(void* component);
void detour_c65_exec_trigger(void* context, void* frame, void* result);
void detour_c65_exec_activate(void* context, void* frame, void* result);
void detour_c65_activate(void* component, bool reset);
// --- Card 227: which presence triggers start events (log only, with UBWCOOP_C65_LOG=1) ---
// ASTPlayerPresence::BeginPlay (folded with three other classes' BeginPlay) dumps each presence's LTOnEnter/LTOnExit
// reaction classes; OnTriggerEntered/Exited (rdx: the presence again, r8: the entering actor) log begin/end lines.
using C227BeginPlayFn = void (*)(void* actor);
using C227EdgeFn = void (*)(void* presence, void* self, void* actor);
extern StaticClassFn g_c227_presence_class;
extern C227BeginPlayFn g_orig_c227_begin_play;
extern C227EdgeFn g_orig_c227_entered;
extern C227EdgeFn g_orig_c227_exited;
// The original edge for gated presence g (objectives.cpp); on the client a gated presence's door reactions are left out.
void c227_run_edge(C227EdgeFn orig, int32_t g, void* presence, void* self, void* actor, size_t lt_off);
void detour_c227_begin_play(void* actor);
void detour_c227_entered(void* presence, void* self, void* actor);
void detour_c227_exited(void* presence, void* self, void* actor);
// Card 245 (b192, objectives.cpp): ASTPlayerPresence::OnBeginOverlap traced for player bodies (same signature as the
// card 338 fall zone overlap), and the save system's load flag its gate 4 reads.
using C245LoadFn = bool (*)(const void* world_context); // USTSaveSystemLibrary::IsLoadInProgress
extern C338OverlapFn g_orig_c245_overlap;
extern C245LoadFn g_c245_load_in_progress;
void detour_c245_overlap(void* presence, void* comp, void* other, void* other_comp, int32_t body, bool sweep,
                         const void* hit);
// Card 301 gap G (b212, objectives.cpp): ASTPlayerPresence::execSetTriggerEventsEnabled; on the host, other living
// players already inside a presence the script enables go through OnBeginOverlap.
extern ExecFn g_orig_c301_exec_enable;
void detour_c301_exec_enable(void* ctx, void* frame, void* result);
using C227OverlapFn = bool (*)(const void* actor, const void* other); // AActor::IsOverlappingActor
extern C227OverlapFn g_c227_is_overlapping;
// Card 220 (objectives.cpp): true for a BP_TutorialPromptTrigger_C, whose edges and enabled byte are per player.
bool c220_per_player(const void* actor);
void c220_after_enter(const void* presence); // card 220 (b187): the prompt widget rows after an own pawn's enter
void c220_step(); // card 220 (b187): the 1 s recheck rows, from ubw_post_tick while one is due
extern bool g_c220_due;
void c114_ftext(const uint8_t* addr, char* out, size_t cap); // difficulty.cpp: an FText's display string, cleaned
// Card 228 item 1 (objectives.cpp): fires a held gated presence once every living player is inside. c43_step.
void c227_recheck(void* world, ULONGLONG now);
void c495_recheck(void* world, ULONGLONG now); // card 495, host: held per-player pickup bases (weapons.cpp)
void c495_map_end(); // card 495, at LoadMap enter: the per-player pickup table starts over
// Card 303 step 1 (stall_c303.cpp): frame watchdog, possession rows for mech and turret, the mount row.
void c303_tick();                                         // game thread, every ubw_post_tick
// Card 330 step 1 (frame_c330.cpp, both sides, always on): per-step QueryPerformanceCounter time of ubw_post_tick,
// c23_collect calls, a window row every 30 s and per-step rows at map end. C330_STEP(call) times one step call; its
// call site registers once (a function-local static) and the call text names the step in the rows.
uint16_t c330_register(const char* name);
struct C330Span {
    explicit C330Span(uint16_t step);
    ~C330Span();
    C330Span(const C330Span&) = delete;
    C330Span& operator=(const C330Span&) = delete;
    uint16_t id;
    int64_t t0;
};
#define C330_STEP(call)                                                                                                \
    do {                                                                                                               \
        static const uint16_t c330_id_ = c330_register(#call);                                                         \
        const C330Span c330_span_(c330_id_);                                                                           \
        call;                                                                                                          \
    } while (false)
void c330_tick_begin();                                   // ubw_post_tick start
void c330_tick_end();                                     // ubw_post_tick end: the window row when due
void c330_collect(const void* ret, uint64_t actors, int64_t ticks); // c23_collect: its caller, actors walked, QPC ticks
void c330_recv(wchar_t op, int64_t ticks);                // c252_ledger_recv: one received frame's handler time
void c330_map_end(const void* old_world);                 // LoadMap enter: per-step rows, collect by caller
void c527_spawn(const void* cls, const void* spawned, int64_t ticks); // card 527: one SpawnActor call's QPC ticks (FX flag)
// score_c475.cpp: one struct value at `at` as "{member=value,...}" (number members only); chars written.
int c491_struct_text(const void* st, const uint8_t* at, char* out, size_t cap);
// Card 527 item 2 (frame_c330.cpp, FX flag only): NPC tick time per kind, a window row and a map row.
constexpr int32_t kC527TickActor = 0;    // ASTNonPlayableCharacterBase::Tick
constexpr int32_t kC527TickMovement = 1; // UCharacterMovementComponent::TickComponent, NPC owner (detour_cmc_tick)
constexpr int32_t kC527TickMesh = 2;     // USkeletalMeshComponent::TickComponent, NPC owner
constexpr int32_t kC527TickSprite = 3;   // UADSpriteAnimatorComponent::TickComponent, NPC owner
constexpr int32_t kC527TickKinds = 4;
using C527ActorTickFn = void (*)(void* actor, float delta);
extern C527ActorTickFn g_orig_c527_actor_tick;
extern CmcTickFn g_orig_c527_mesh_tick;
extern CmcTickFn g_orig_c527_sprite_tick;
void detour_c527_actor_tick(void* npc, float delta);
void detour_c527_mesh_tick(void* comp, float delta, int32_t tick_type, void* tick_function);
void detour_c527_sprite_tick(void* comp, float delta, int32_t tick_type, void* tick_function);
// Times one component tick for `kind` when its owner is an NPC (FX flag), else just calls `orig`.
void c527_component_tick(int32_t kind, CmcTickFn orig, void* comp, float delta, int32_t tick_type,
                         void* tick_function);
void c330_pe(uint64_t tsc); // detour_process_event: one call's mod time (time stamp counter ticks, the original excluded)
void c303_press(const char* what);                        // client: a mount or leave request sent (static string)
void c303_possession(const char* event, const void* pawn); // client: OnRep_Pawn, ClientRestart, Ack, SetPawn
// Cards 297 / 298 (b8, diagnostic only, attributes.cpp): the client pilot's mech rows.
void c297_possessed(const void* pawn);                       // client: the mount press's possession (any pawn)
void c297_tick(void* engine);                                // client: arms swap on own mech enter/exit, 1 s check, tag rows
void c297_shot(const void* mech);                            // client: one row per shot of its own piloted mech
using SetMeshVisibilityFn = void (*)(void* character, void* mesh, bool visible); // ASTPlayerCharacter::SetMeshVisibility
extern SetMeshVisibilityFn g_c297_set_mesh_visibility;
void c297_map_end();
void c298_mount_row(const char* when, const void* pawn);     // both sides: pitch limits, controller, fuel (mech only)
int32_t c297_bool_call(void* object, uint64_t& cache, const wchar_t* name); // native bool getter: 1, 0, -1 absent
void c303_map_end();
// Card 303 step 3 (stall_c303.cpp, both sides, always on): GetSizeForDevID (a monitor's EDID size and DPI through
// setupapi, about 1 s under Proton) cached per device id for the process.
using C303GetSizeFn = bool (*)(const FStringRaw* id, int32_t* width, int32_t* height, int32_t* dpi);
extern C303GetSizeFn g_orig_c303_get_size;
bool detour_c303_get_size(const FStringRaw* id, int32_t* width, int32_t* height, int32_t* dpi);
// Card 322 step 1 (radio_c322.cpp, diagnostic, both sides): conversation, music, dilation and out-of-bounds rows.
using C322StartConversationFn = void (*)(void* self, void* data_class, int32_t a, int32_t b, bool c);
using C322AddConversationFn = void (*)(void* self, void* data_class, int32_t a, int32_t b);
using C322StartFromListFn = void (*)(void* self, void* list, int32_t a, int32_t b, bool c); // TArray by value: pointer
using C322TriggerMusicFn = void (*)(void* self, uint8_t type, float value, bool flag);
using C322PlayDilationFn = void (*)(void* self, void* handle); // FADTimeDilationHandle by value: pointer
using C322ThisFn = void (*)(void* self);
using C322ExecFn = void (*)(void* context, void* stack, void* result); // UFunction exec thunk
constexpr int32_t kC322Execs = 7; // hooked exec thunks, in kHooksUbw order
extern C322StartConversationFn g_orig_c322_start_conversation;
extern C322AddConversationFn g_orig_c322_add_conversation;
extern C322StartFromListFn g_orig_c322_start_from_list;
extern C322TriggerMusicFn g_orig_c322_trigger_music;
extern C322PlayDilationFn g_orig_c322_play_dilation;
extern C322ThisFn g_orig_c322_dilation_start;
extern C322ThisFn g_orig_c322_oob_entered;
extern C322ThisFn g_orig_c322_oob_exited;
extern C322ExecFn g_orig_c322_exec[kC322Execs];
extern const void* g_c322_bp_caller; // FFrame::Object of the innermost hooked exec thunk, or null
template <int I> void detour_c322_exec(void* context, void* stack, void* result); // instantiated 0..6
void detour_c322_start_conversation(void* self, void* data_class, int32_t a, int32_t b, bool c);
void detour_c322_add_conversation(void* self, void* data_class, int32_t a, int32_t b);
void detour_c322_start_from_list(void* self, void* list, int32_t a, int32_t b, bool c);
void detour_c322_trigger_music(void* self, uint8_t type, float value, bool flag);
void detour_c322_play_dilation(void* self, void* handle);
void detour_c322_dilation_start(void* ability);
void detour_c322_oob_entered(void* component);
void detour_c322_oob_exited(void* component);
// Card 360 (radio_c322.cpp, always on): the out-of-bounds expiry kill runs on the host only; a client skips it.
using C360TimeLeftFn = float (*)(const void* component);
extern C322ThisFn g_orig_c360_kill_owning_actor;
extern C360TimeLeftFn g_c360_time_left;
void detour_c360_kill_owning_actor(void* component);
// Card 361 (insta_c361.cpp, always on): InstaKillTarget detour (a client never kills a player-class actor; the host's
// kill comes by the Down frame) and the card 321 hazard half (host: TickMoveToNewPostion runs for a server copy in the
// zone's path). g_c361_oob_expiry: set by card 360's detour around the KillOwningActor original (a tail jump).
using C361HazardTickFn = void (*)(void* zone);
extern C361HazardTickFn g_orig_c361_hazard_tick;
extern bool g_c361_oob_expiry;
void detour_c361_insta_kill(void* target, bool overkill);
void detour_c361_hazard_tick(void* zone);
// Card 449 (dropship.cpp): ASTProceduralDropshipTroops::CanTakeoff. Host: asked for every player's trooper;
// client: a copy with a host row leaves its takeoff to the host's frames.
using C449CanTakeoffFn = bool (*)(void* dropship);
extern C449CanTakeoffFn g_orig_c449_can_takeoff;
bool detour_c449_can_takeoff(void* dropship);
void c361_map_end();
void c362_down_frame(int32_t id, unsigned long long now); // card 362: client, a Down frame (not Up) for entry id
void c362_step();                                         // card 362: client, every engine tick
void c322_map_end();
void c322_receive(void* pc, const WireString* s); // card 322 part A: ChanOp::Radio, the host's conversation start
void c227_map_end(); // both sides, at LoadMap enter (ubw_load_map_enter): the gated presence table starts over
// Card 300: one-way closures (kC300Closures, objectives.cpp). Host: c300_host_send sends the 'F' world row when the
// all-players gate closes one (world_c98.cpp). Client: c300_client_close runs the closure's entry edge for that row.
void c300_host_send(const void* presence);
void c300_client_close(void* presence, void* own_pawn);
// --- Card 068: why the client minimap shows no icons (log only, UBWCOOP_C68_LOG=1) ---
// The chain: ASTLevelObjectiveMarker::ObjectiveUpdated (marker visibility edge) -> PC 0's
// USTMinimapComponent::ShowOnMinimap (appends to ActiveMarkers when in range) -> the widget's bound delegate ->
// USTMinimapWidget::AddNewMarker (makes the icon). UpdateMarkerLocation runs per marker per frame: counted only.
using C68MarkerFn = void (*)(void* marker, const void* objective);
using C68ShowFn = void (*)(void* minimap, const void* actor);
using C68IndexFn = void (*)(void* widget, int32_t index);
using C68WidgetFn = void (*)(void* widget);
using C68UpdateFn = void (*)(void* widget, void* marker_data); // FSTMinimapMarkerData by value = hidden pointer
constexpr size_t kMinimapRange = 0xDC;          // USTMinimapComponent::MinimapRange (float)
constexpr ULONGLONG kC68LineMs = 1000;
extern C68MarkerFn g_orig_c68_objective_updated;
extern C68ShowFn g_orig_c68_show;
extern C68IndexFn g_orig_c68_add;
extern C68IndexFn g_orig_c68_remove;
extern C68WidgetFn g_orig_c68_init;
extern C68UpdateFn g_orig_c68_update;
extern uint32_t g_c68_updates;
extern ULONGLONG g_c68_next_ms;
int32_t c68_marker_count(const void* minimap);
void detour_c68_objective_updated(void* marker, const void* objective);
void detour_c68_show(void* minimap, const void* actor);
void detour_c68_add(void* widget, int32_t index);
void detour_c68_remove(void* widget, int32_t index);
void detour_c68_update(void* widget, void* marker_data);
void detour_c68_init(void* widget);
// --- Card 069: why the client has no reserve ammo and cannot reload (log only, UBWCOOP_C69_LOG=1) ---
// The reserve lives in the pawn's USTAmmunitionInventory (ASTPlayerCharacterMI+0xA48), keyed by gameplay tag.
// ResetInventory fills it from the loadout (AddAmmo); a reload takes rounds out of it (RequestAmmo). Each hook logs
// its first kC69Logs calls per map and counts the rest; the counts go on the 1 s line.
using C69VoidFn = void (*)(void* self);
using C69AmmoFn = int32_t (*)(void* inventory, uint64_t tag, int32_t amount); // FGameplayTag (one FName) by value
using C69RequestFn = int32_t (*)(void* inventory, uint64_t tag, int32_t amount, bool flag);
using C69GetAmmoFn = int32_t (*)(const void* inventory, uint64_t tag, bool flag);
using C69GetMaxFn = int32_t (*)(const void* inventory, uint64_t tag);
enum C69Hook { kC69Reset, kC69Add, kC69Set, kC69Request, kC69Perform, kC69DoAction, kC69Hooks };
constexpr size_t kMiAmmoInventory = 0xA48;      // ASTPlayerCharacterMI::AmmunitionInventory
constexpr size_t kWeaponTypeTag = 0x2C8;        // AADWeapon::WeaponTypeTag (FGameplayTag)
constexpr size_t kAmmoMaxMap = 0xC8;            // USTAmmunitionInventory::MaxAmmoMap (TMap: sparse array first)
constexpr size_t kAmmoMap = 0x118;              // USTAmmunitionInventory::AmmoMap (TMap<FGameplayTag, int32>)
constexpr ULONGLONG kC69LineMs = 1000;
extern C69VoidFn g_orig_c69_reset;
extern C69AmmoFn g_orig_c69_add;
extern C69AmmoFn g_orig_c69_set;
extern C69RequestFn g_orig_c69_request;
extern C69VoidFn g_orig_c69_perform;
extern C69VoidFn g_orig_c69_do_action;
extern C69GetAmmoFn g_c69_get_ammo;
extern C69GetMaxFn g_c69_get_max;
extern uint32_t g_c69_calls[kC69Hooks];
extern ULONGLONG g_c69_next_ms;
void detour_c69_reset(void* pawn);
int32_t detour_c69_add(void* inventory, uint64_t tag, int32_t amount);
int32_t detour_c69_set(void* inventory, uint64_t tag, int32_t amount);
int32_t detour_c69_request(void* inventory, uint64_t tag, int32_t amount, bool flag);
void detour_c69_perform(void* action);
void detour_c69_do_action(void* action);
int32_t c69_map_count(const void* inventory, size_t map);
void c69_map_text(const void* inventory, char* out, size_t cap);
// --- Card 073: where a client's grenade lives (log, UBWCOOP_C73_LOG=1) and how it reaches the client (always) ---
// The HUD (WB_Inventory_Item) reads GetSTPlayerCharacter -> ItemInventory -> FindItemByDefinition(ItemDefinition)
// -> Quantity. USTItemInventory keeps its items at +0xD8 (16-byte entries, the item first; Num +0xE0: the loop of
// FindItemIndexByDefinition). Each detour logs its first kC73Logs calls per map with the caller and counts the rest;
// the 1 s line logs the local pawn's items and grenade ability specs whenever their text changes.
// AddAbilityToActor returns its FGameplayAbilitySpecHandle through a hidden pointer (the handle has a constructor).
// FADAbilityToInputTags: the ability class +0, TArray<FGameplayTag> InputTags +8 / Num +0x10.
// The fix (card 073 step 2, every co-op session): pickups run only on the host, and neither the item inventory nor
// the input tag binds that AddAbilityToActor makes replicate; only the ability spec does. After each item change on
// a remote player's server copy the host sends that client a GrenadeItem frame with the copy's new quantity
// (c73_relay); the client sets its own pawn's quantity to it (c73_receive), so its HUD reads it and its own equip runs.
// That equip calls AddAbilityToActor on the client, where GiveAbility is refused: c73_client_grant binds the input
// tags to the replicated spec instead, and sets the ability instance's SourceItem to our own item (CanActivateAbility
// needs it). The client's G activates locally and asks the host; only the server copy reaches OnDoAction, which spends
// the item (RemoveItemByDefinition, relayed back as the new count) and spawns the grenade. The grenade does not
// replicate: the client takes its damage but does not see it. When the host removes the spent grenade's spec, the
// client drops the now dangling input tag bind before its next press (c73_drop_stale).
using C73AddAbilityFn = int32_t* (*)(int32_t* out, void* actor, const void* binding, void* source);
using C73AddItemFn = void* (*)(void* inventory, void* const* definition, int32_t amount);
using C73RemoveItemFn = void* (*)(void* inventory, void* const* definition, int32_t amount, bool flag);
using C73ConsumeFn = int32_t (*)(void* ability, int32_t amount);
enum C73Hook { kC73Ability, kC73Add, kC73Remove, kC73Consume, kC73Hooks };
constexpr int32_t kC73MaxTags = 4;
constexpr ULONGLONG kC73LineMs = 1000;
constexpr size_t kC73Line = 512;
extern C73AddAbilityFn g_orig_c73_ability;
extern C73AddItemFn g_orig_c73_add;
extern C73RemoveItemFn g_orig_c73_remove;
extern C73ConsumeFn g_orig_c73_consume;
struct C73State {
    uint32_t calls[kC73Hooks];
    ULONGLONG next_ms;
    bool dumped_inventory;
    bool dumped_item;
    char last[kC73Line];
    char last_copy[kC73Line]; // host: the first remote player's server copy (card 074: its count across a respawn)
};
extern C73State g_c73;
// Card 073 fix: a client grant whose spec has not replicated yet (the ability UClass stays loaded with its item).
struct C73Grant {
    void* ability;                // the grant's ability class; null = free slot
    void* item;                   // the grant's source: our own STInventoryItem (the ability's SourceItem)
    uint64_t tags[kC73MaxTags];   // its input tags (FGameplayTag = one FName)
    int32_t tags_num;
    ULONGLONG since_ms;
};
constexpr int32_t kC73Waits = 16; // card 210: a bug form's five grants wait at once
constexpr ULONGLONG kC73WaitMs = 10000;
extern C73Grant g_c73_wait[kC73Waits];
// Client: GrenadeItem counts that came while our pawn had no item inventory (c73_defer), latest per definition.
struct C73Pending {
    void* definition; // item definition class (lives for the process)
    int32_t want;
    ULONGLONG since_ms; // first deferral
};
constexpr uint32_t kC73Pending = 8; // grenade definitions; one frame per definition per snapshot
extern C73Pending g_c73_pending[kC73Pending];
extern uint32_t g_c73_pending_count;
extern uint32_t g_c73_pending_peak;
void c73_pending_step(void* pc, ULONGLONG now);
using C73FindItemFn = void* (*)(void* inventory, void* const* definition);
extern C73FindItemFn g_c73_find_item;
extern C73AddItemFn g_c73_add_item;
using C73UnbindFn = void (*)(void* asc, const int32_t* handle);
extern C73UnbindFn g_c73_unbind;
constexpr int32_t kC73MaxBound = 8;      // handles checked per tag
bool c73_bind(void* pawn, const C73Grant& g, int32_t* out);
bool c73_inventory_holds(const void* inventory, const void* item); // card 351
void* c73_item_inventory(const void* pawn);
const char* c234_grenade_fill(void* world, int32_t want, uint32_t seq); // b234, test only (grenades.cpp)
int32_t* detour_c73_ability(int32_t* out, void* actor, const void* binding, void* source);
void* detour_c73_add(void* inventory, void* const* definition, int32_t amount);
void* detour_c354_add(void* inventory, void* const* definition, int32_t amount);
bool c354_own_local(void* inventory); // card 354: client session, our own pawn, not the host frame's write
extern C73AddItemFn g_orig_c354_add;
void* detour_c73_remove(void* inventory, void* const* definition, int32_t amount, bool flag);
int32_t detour_c73_consume(void* ability, int32_t amount);
void c73_step_pawn(void* pawn, const char* who, char* last);
// --- Card 074 step 1: where each side's grenade throw goes (log only, UBWCOOP_C74_LOG=1) ---
// The chain on the host: StartAnimations / StartThrowingAnimation, the montage's notify -> OnDoAction_Implementation
// -> UADAbilityTask_FireProjectile::FireProjectile (the task factory, card 020 log) -> the task's Activate ->
// UADProjectilePooler::SpawnProjectile -> OnProjectileSpawned -> OnGrenadeSpawned -> InitGrenade; later Explode ->
// ApplyDamageFromImpactHitResult and TrySpawnDamageZone. Each step logs its side, so a client run shows where the
// client's own activation stops and whether the host player's grenade spawns anything on the client.
using C74AbilityFn = void (*)(void* ability);
using C74TaskFn = void (*)(void* task);
using C74SpawnFn = void* (*)(void* const* init_data_class, const double* transform, const void* world);
using C74GrenadeSpawnedFn = void (*)(void* ability, void* projectile);
using C74InitGrenadeFn = void (*)(void* projectile, const void* params);
using C74ExplodeFn = void (*)(void* projectile);
using C74ApplyDamageFn = void* (*)(void* results, void* instigator, void* causer, const void* damage, const void* tags,
                                   const void* hit, const void* tag);
using C74ZoneFn = void* (*)(void* context, const void* zone, const void* hit);
constexpr size_t kAttackResultsNum = 0x8;      // FSTAttackDamageResults: TArray<FSTDamageResult> Num
extern C74AbilityFn g_orig_c74_start_anims;
extern C74AbilityFn g_orig_c74_start_throw;
extern C74AbilityFn g_orig_c74_do_action;
extern C74TaskFn g_orig_c74_activate;
extern C74SpawnFn g_orig_c74_spawn;
extern C74GrenadeSpawnedFn g_orig_c74_spawned;
extern C74InitGrenadeFn g_orig_c74_init;
extern C74ExplodeFn g_orig_c74_explode;
extern C74ApplyDamageFn g_orig_c74_damage;
extern C74ZoneFn g_orig_c74_zone;
extern C74TaskFn g_orig_c74_anim[3];
extern C74ExplodeFn g_orig_c74_proj[3];
void detour_c74_start_anims(void* ability);
void detour_c74_start_throw(void* ability);
void detour_c74_do_action(void* ability);
extern C74SpawnFn g_c74_spawn_fn;
struct C76Mech {
    const void* task;     // host: the last mech or turret FireProjectile task, until its Activate (compared, never read)
    C31Weak pilot_pc;     // that mech's or turret's controller (card 331: weak, read at the task's later Activate)
    C31Weak vehicle;      // that mech or turret, set with task (its class names the relay log line)
    uint32_t relayed;     // host: mech and turret rounds sent
    uint32_t lines;       // host log lines
    uint32_t copy_lines;  // client log lines
};
extern C76Mech g_c76_mech;
void detour_c74_activate(void* task);
void* detour_c74_spawn(void* const* init_data_class, const double* transform, const void* world);
// Card 132 bug projectile check (proj_c132.cpp): every pooled projectile launch, both sides.
void c132_proj_task(const void* task, const void* shooter);
void c132_proj_activating(const void* task);
void c132_proj_copying(bool on);
bool c132_proj_is_copying(); // card 417: the launch in progress is a host relay copy
void c417_step();            // grenades.cpp: both sides, each frame: release the queued card 417 launches
void c417_arm_map();
void c132_proj_launch(const void* init_data, const double* transform, const void* context, const void* proj,
                      const void* ra);
void c132_proj_round(uint32_t round);
void c132_proj_map();
void c136_discard_component(void* comp); // card 526: client discard of a host-sent component (fx_c136.cpp)
bool c132_proj_unown(const void* init_data); // card 286: a released own launch leaves the own count
void c132_fx_probe(const char* what, const char* asset, const Vec3* at, const void* actor, const void* ra);
void detour_c74_spawned(void* ability, void* projectile);
void detour_c74_init(void* projectile, const void* params);
void detour_c74_explode(void* projectile);
void detour_c74_consume(void* projectile);
void detour_c74_release(void* projectile);
// The explosion effect is not the projectile's: OnGrenadeSpawned binds a gameplay cue notify (GC_Impacts_Grenade_Frag_C,
// GC_Impacts_Grenade_Incendiary_C) to the host's grenade, and its OnExecute plays a sound (PlaySoundAtLocation, through
// UADAudioManager) and a Niagara system (STSpawnSystemAtLocation) inside LifeSpanExpired. The copy has no binding, so
// the host catches the first of each while a relayed grenade's LifeSpanExpired runs (g_c74_fx) and sends their names;
// the client finds those assets by name and plays them at the host's places.
using C74StNiagaraFn = void* (*)(void* manager, void* context, void* system, const Vec3* location,
                                 const Vec3* rotation, const Vec3* scale, bool auto_destroy, bool auto_activate,
                                 int32_t pool, bool pre_cull);
using C74PlaySoundFn = void (*)(const void* context, void* sound, const Vec3* location, const Vec3* rotation,
                                float volume, float pitch, float start, void* attenuation, void* concurrency,
                                const void* owner, const void* params);
using C74SpawnSoundFn = void* (*)(const void* context, void* sound, const Vec3* location, const Vec3* rotation,
                                  float volume, float pitch, float start, void* attenuation, void* concurrency,
                                  bool auto_destroy);
using C74CueFn = void (*)(void* asc, uint64_t tag, const void* parameters); // FGameplayTag: one FName by value
extern bool g_c74_fx;
extern SpawnNiagaraFn g_orig_c74_niagara;
extern C74StNiagaraFn g_orig_c74_st_niagara;
extern C74PlaySoundFn g_orig_c74_play_sound;
extern C74SpawnSoundFn g_orig_c74_spawn_sound;
extern C74CueFn g_orig_c74_cue;
using C74SetLifeSpanFn = void (*)(void* actor, float seconds);
extern C74SetLifeSpanFn g_set_life_span;
extern StaticClassFn g_niagara_system_class;
extern StaticClassFn g_sound_base_class;
// Card 132 item 4a: USTCommonUtilityLibrary::STSpawnNiagraSystemAtLocation (static; same arguments as
// UNiagaraFunctionLibrary::SpawnSystemAtLocation). Client: spawned through the stock function (no GameMode there).
using C132UtilNiagaraFn = SpawnNiagaraFn;
extern C132UtilNiagaraFn g_orig_c132_util_niagara;
void* detour_c132_util_niagara(void* context, void* system, const Vec3* location, const Vec3* rotation,
                               const Vec3* scale, bool auto_destroy, bool auto_activate, int32_t pool, bool pre_cull);
void* detour_c74_st_niagara(void* manager, void* context, void* system, const Vec3* location, const Vec3* rotation,
                            const Vec3* scale, bool auto_destroy, bool auto_activate, int32_t pool, bool pre_cull);
void detour_c74_play_sound(const void* context, void* sound, const Vec3* location, const Vec3* rotation,
                           float volume, float pitch, float start, void* attenuation, void* concurrency,
                           const void* owner, const void* params);
void* detour_c74_niagara(const void* context, void* system, const Vec3* location, const Vec3* rotation,
                         const Vec3* scale, bool auto_destroy, bool auto_activate, int32_t pool, bool pre_cull);
void* detour_c74_spawn_sound(const void* context, void* sound, const Vec3* location, const Vec3* rotation,
                             float volume, float pitch, float start, void* attenuation, void* concurrency,
                             bool auto_destroy);
void detour_c74_cue(void* asc, uint64_t tag, const void* parameters);
void detour_c74_life(void* projectile);
// Card 132: AADProjectile::OnHit, the relayed round's impact (grenades.cpp). The FVector impulse is by value: a pointer.
using C132OnHitFn = void (*)(void* self, void* hit_comp, void* other, void* other_comp, const Vec3* impulse,
                             const void* hit);
extern C132OnHitFn g_orig_c132_on_hit;
void detour_c132_on_hit(void* self, void* hit_comp, void* other, void* other_comp, const Vec3* impulse,
                        const void* hit);
// Card 132 step 2e (proj_c132.cpp): c132_fx_probe logs every effect spawn with UBWCOOP_C132_FX=1; these hook the
// spawn chokepoints the card 074 detours do not cover.
bool c132_fx_install(uint8_t* base, const IMAGE_NT_HEADERS64* nt);
// Card 136 step 1 (fx_c136.cpp): the effects manager's capture points. Each capture point's detour calls c136_fx_enter
// before its original and c136_fx_leave after it, on both sides.
enum class C136Point : uint8_t {
    PlaySound,
    SpawnSound,
    SoundAttached,
    Niagara,
    NiagaraAttached,
    NiagaraAttachedScale,
    NiagaraParams,
    NiagaraAttachedParams,
    Emitter,
    EmitterAttached,
    EmitterAttachedScale,
    Cue,
    CueContext,
    CueNonReplicated,
    DecalAt,
    DecalAttached,
    NiagaraActivate,
    NiagaraDeactivate,
    NiagaraDeactivateImmediate,
    CueHandle, // run 6: UGameplayCueManager::HandleGameplayCue, a client scope only (never an event, never sent)
    Count
};
// One captured call, as its detour read it. Transform: world for an at-location call, relative to `attach` otherwise.
struct C136Call {
    C136Point point;
    const void* asset = nullptr;      // system, sound, emitter template or decal material; null for a cue
    uint64_t tag = 0;                 // a cue's FGameplayTag (its FName)
    const void* context = nullptr;    // the world context object
    const void* owner = nullptr;      // an explicit owner (sound OwningActor, cue target, ASC or component owner)
    const void* attach = nullptr;     // the component attached to
    const void* component = nullptr;  // point 6: the UNiagaraComponent acted on
    const void* cue_params = nullptr; // point 4: FGameplayCueParameters
    bool cue_context = false;         // point 4, the overload with an effect context
    uint64_t socket = 0;              // attach socket FName
    Vec3 at{};
    Vec3 rot{};
    Vec3 scale{1.0, 1.0, 1.0};
    float look[3] = {}; // sound volume, pitch, start time; decal life; else 0
    uint8_t flags = 0;  // kC136Flag* (card 136 step 2: the spawn's own switches, played the same on the client)
    const void* atten = nullptr; // sound points: USoundAttenuation override (null: the sound's own)
    const void* conc = nullptr;  // sound points: USoundConcurrency override (null: the sound's own)
    bool located = true;         // false: a sound called with no location (2D; see the card's ownerless rule)
};
inline constexpr uint8_t kC136FlagAutoDestroy = 0x01;
inline constexpr uint8_t kC136FlagAutoActivate = 0x02;
inline constexpr int kC136FlagLocationShift = 2; // bits 2 to 4: EAttachLocation of an attached spawn
inline constexpr uint8_t kC136FlagLocationMask = 0x07;
inline constexpr uint8_t kC136FlagStopWithParent = 0x20; // SpawnSoundAttached bStopWhenAttachedDestroyed
inline constexpr uint8_t kC136FlagReset = 0x40;          // Activate bReset (played the same on the client)
inline constexpr uint8_t kC136FlagPlayerSource = 0x80;   // card 304: a sound of the sender's own player pawn or its weapon
inline constexpr size_t kNiagaraAsset = 0x520; // UNiagaraComponent::Asset (GetFXSystemAsset 0x01722800)
inline constexpr size_t kNiagaraPoolingMethod = 0x650; // UNiagaraComponent::PoolingMethod (ReleaseToPool: cmpb $0x2)
uint32_t c136_fx_enter(const C136Call& c);
void c136_fx_leave(C136Point point, uint32_t id, const void* made);
bool c136_setvar_install(uint8_t* base, const IMAGE_NT_HEADERS64* nt); // UBWCOOP_C136_LOG=1 only
void c136_arm_map(); // per map, the registry's "effects" arm (card 326): c136_report("map arm", true)
void c136_report(const char* when, bool keep_state = false); // the totals lines, then the live set and counts start over (map end, quit); card 325: keep_state (host arm) resets the counts only
uint64_t c41_guid(const void* actor); // the actor's NetGUID value, 0 when none (enemies.cpp)
// Card 209 (enemies.cpp): a NetGUID in two channel values, false when 0 or too wide; the reverse; the object a GUID
// names on the client; the player pawn it names (null while unresolved).
bool c41_guid_args(uint64_t guid, uint32_t& lo, uint32_t& hi);
uint64_t c41_args_guid(uint32_t lo, uint32_t hi);
void* c41_object_from_guid(void* pc, uint64_t guid);
void* c41_resolve_player(void* pc, uint64_t guid);
// Card 136 step 3, client: the block decision of the outermost captured call on this thread, inherited by its inner
// calls (and kept after it returns until the next outermost call). The effect points (1, 2, 3, 5) skip their original
// and return null when it is set; cues and point 6 always run.
bool c136_blocked();
const char* c136_point_name(C136Point point); // the capture point's log name (fx_c136.cpp)
// Card 136 steps 2 to 4 (fx_c136_net.cpp). Host: c136_fx_enter queues each event, c136_fx_leave completes it.
void c136_net_queue(uint32_t id, const C136Call& c, const void* owner, const void* instigator, const char* key,
                    uint32_t of);
void c136_net_done(uint32_t id, const void* made); // outermost leave: the seed is read, the row is complete
bool c136_client_own(const C136Call& c, const void* owner, const void* instigator); // client: its own effect
bool c136_attack_local(const C136Call& c, const void* owner); // card 408: a flak or plasma bug building attack effect
void c136_set_playing(bool on);                    // client: the manager plays a host row (nothing blocked)
void c136_step(void* engine);                      // per tick: host flush and live sweep; client local pawn
void c136_receive_fx(void* pc, const WireString* s); // client: ChanOp::Fx
void c136_join_replay(void* pc);                   // host: a joining PC's first Pong (asset table, journal)
void c136_net_arm_map(const char* when, bool keep_state);           // per map, from c136_report
// proj_c132.cpp: the original of an attached or emitter point (13 slots, as the detour forwards them).
void* c136_pass_play(C136Point point, const uint64_t* slots);
bool c132_decal_gore(const void* material);    // decal_c132.cpp: the card 132 relay owns this decal
void* c132_decal_manager(const void* context); // decal_c132.cpp: the game instance's USTDecalManager
using C136CueContextFn = void (*)(void* asc, uint64_t tag, const void* context); // context handle by value: pointer
using C136CueLocalFn = void (*)(void* target, uint64_t tag, const void* parameters);
using C136ActivateFn = void (*)(void* component, bool reset);
using C136ComponentFn = void (*)(void* component);
using C136CueActorFn = void* (*)(const void* parameters); // FGameplayCueParameters::GetInstigator
using C136CueParamsFn = void (*)(void* parameters);       // FGameplayCueParameters() and ~FGameplayCueParameters()
using C136CueFromContextFn = void (*)(void* parameters, const void* context); // FGameplayCueParameters(const Handle&)
extern C136CueContextFn g_orig_c136_cue_context;
extern C136CueLocalFn g_orig_c136_cue_local;
extern C136ActivateFn g_orig_c136_activate;
extern C136ComponentFn g_orig_c136_deactivate;
extern C136ComponentFn g_orig_c136_deactivate_now;
extern C136CueActorFn g_c136_cue_instigator;
extern C136CueParamsFn g_c136_cue_params_ctor;
extern C136CueParamsFn g_c136_cue_params_dtor;
extern C136CueFromContextFn g_c136_cue_params_from_context;
// USceneComponent::GetSocketLocation / GetSocketRotation (card 136 fix 5): this rcx, hidden return rdx, FName r8.
using C136SocketLocationFn = Vec3* (*)(const void* component, Vec3* out, uint64_t socket);
using C136SocketRotationFn = Vec3* (*)(const void* component, Vec3* out, uint64_t socket); // FRotator: pitch, yaw, roll
extern C136SocketLocationFn g_c136_socket_location;
extern C136SocketRotationFn g_c136_socket_rotation;
// UAnimInstance::TriggerSingleAnimNotify (card 136 review 2): the notify's anim instance is the owner source for the
// effects it spawns with the world as context (Morita casing and fire sound).
using C136AnimNotifyFn = void (*)(void* anim_instance, void* notify_ref);
extern C136AnimNotifyFn g_orig_c136_anim_notify;
void detour_c136_anim_notify(void* anim_instance, void* notify_ref);
// Client: an effect that plays outside the manager, the client's own effects and the block (the decal relay, an
// ownerless component op), counted by asset (card 136 review 2 "extra").
void c136_client_extra(const void* asset);
void detour_c136_cue_context(void* asc, uint64_t tag, const void* context);
void detour_c136_cue_local(void* target, uint64_t tag, const void* parameters);
// UGameplayCueManager::HandleGameplayCue (card 136 run 6 fix B): this, AActor* target, FGameplayTag (by value: the
// FName), EGameplayCueEvent::Type (low byte), const FGameplayCueParameters&, EGameplayCueExecutionOptions.
using C136CueHandleFn = void (*)(void* manager, void* target, uint64_t tag, uint64_t event, const void* parameters,
                                 uint64_t options);
extern C136CueHandleFn g_orig_c136_cue_handle;
void detour_c136_cue_handle(void* manager, void* target, uint64_t tag, uint64_t event, const void* parameters,
                            uint64_t options);
// AADProjectile and ASTGorePartBase (card 136 run 6 fix C): their trail component ops are local on each side.
extern StaticClassFn g_projectile_class;
extern StaticClassFn g_gore_part_class;
void detour_c136_activate(void* component, bool reset);
// Cards 409 step 2 and 418: UNiagaraFunctionLibrary::execSpawnSystemAtLocation and
// UGameplayStatics::execSpawnSoundAtLocation (spawn body inlined: the Blueprint path past the spawn detours).
extern ExecThunkFn g_orig_c409_spawn_thunk;
extern ExecThunkFn g_orig_c418_sound_thunk;
void detour_c409_spawn_thunk(void* context, void* frame, void* result);
void detour_c418_sound_thunk(void* context, void* frame, void* result);
using C409DestroyFn = void (*)(void* component, bool promote_children); // USceneComponent::DestroyComponent
using C409ReleaseFn = void (*)(void* component);                         // UNiagaraComponent::ReleaseToPool
extern C409DestroyFn g_scene_destroy_component;
extern C409ReleaseFn g_c409_release_to_pool;
// From detour_c136_activate: true when the Activate was a 409 thunk spawn (handled).
bool c409_activate_spawn(void* component, bool reset);
// From detour_pl_play_internal: true when the play is a 418 thunk spawn; then *id is the event id, the caller plays
// only when c136_blocked() is false and closes with c136_fx_leave(C136Point::SpawnSound, *id, nullptr).
bool c418_play_enter(void* comp, const void* sound, float volume, float pitch, const void* atten, uint32_t* id);
void c409_report(const char* when); // per map, from c136_report
void detour_c136_deactivate(void* component);
void detour_c136_deactivate_now(void* component);
void* detour_c74_damage(void* results, void* instigator, void* causer, const void* damage, const void* tags,
                        const void* hit, const void* tag);
void* detour_c74_zone(void* context, const void* zone, const void* hit);
void detour_c74_anim_activate(void* task);
void detour_c74_anim_do(void* task);
void detour_c74_anim_end(void* task);
// --- Card 071: the ammo a pickup credits to a client's server copy goes to that client ---
// Ammo pickups run only on the host: USTWeaponUtilityLibrary::AddAmmunitions credits the overlapping pawn's
// USTAmmunitionInventory, which does not replicate (card 069). For a remote player's server copy the host sends that
// client one Ammo text frame, "<ammunition tag> <amount> ...", with the amount the copy actually gained per tag
// (GetAmmoCount after the original minus before; 0 is not sent). The client adds it to its own pawn with AddAmmo:
// its own fire and reload change its counts, so only the amount added is true on both sides. Known limit (logged,
// not fixed): the copy never fires, so its count only rises; at GetMaxAmmo the host adds 0 and the client gets nothing.
using C71AddAmmunitionsFn = int32_t (*)(void* actor, const void* tags, const void* amounts); // two TArray refs
extern C71AddAmmunitionsFn g_orig_c71_add_ammunitions;
extern C69AmmoFn g_c71_add_ammo;
int32_t detour_c71_add_ammunitions(void* actor, const void* tags, const void* amounts);
// --- Card 072: the client owns its ammunition counts; its server copy mirrors them ---
// Seed (host to client, once per copy pawn, from c50_host_poll): the copy's whole AmmoMap as absolute counts on one
// AmmoSet text frame, "<ammunition tag> <count> ..."; the client sets each on its own pawn with SetAmmoCount. This
// gives the client the Laser reserve that ResetInventory gives only pawns on the host (card 069).
// Mirror (client to host, at most every kC72MirrorMs, only when the map text changed): the client's own AmmoMap on
// one AmmoMirror frame through ServerChangeName (card 050's UseName path). The host sets each count, clamped to
// 0 .. GetMaxAmmo, on the sender's own server copy only. The client's count is the truth: a pickup between two
// mirror frames reaches the client through card 071's relay, and the client's next frame carries it back, so both
// sides agree after one round trip. The copy also runs its own reload on the relayed fire, which lowers it by a
// magazine, so the host keeps the last mirror text per copy (C50Copy::ammo) and re-applies it on each card 050 poll.
// A relayed pickup credit clears that text, so a stale re-apply never takes the gain back before the client's frame.
// SetAmmoCount is not AddAmmunitions, so a mirror does not trigger card 071.
constexpr ULONGLONG kC72MirrorMs = 250; // 4 Hz
// Card 444: while the reserve map is unchanged and the magazine did not rise, the frame carries the magazine pairs
// only (its text starts with kC274MagazineMark), at most every kC72MagazineMs. The host only raises a copy's
// magazine (card 274), so a falling count is a drift correction, not state the copy needs 4 times a second.
constexpr ULONGLONG kC72MagazineMs = 1000;
constexpr size_t kC72MagazineText = 192; // kC274Behaviours pairs of '@' + tag (kChanNameMax) + count
extern C69AmmoFn g_c72_set_ammo;
struct C72Client {
    ULONGLONG next_ms;
    char sent[kChanTextMax + 1]; // the reserve map text of the last full AmmoMirror frame
    char sent_magazine[kC72MagazineText]; // card 444: the magazine pairs of the last frame, full or magazine-only
    ULONGLONG magazine_ms;                // card 444: no magazine-only frame before this
    bool holes_warned;
    char held[kChanTextMax + 1]; // card 073: the AmmoSet text not applied yet ("" = none held)
    ULONGLONG held_ms;           // when it arrived
    int32_t magazine_rounds;     // card 274: the equipped weapon's magazine rounds at the last step
};
extern C72Client g_c72;
int32_t c72_map_text(const void* inventory, char* out, size_t cap);
void c72_apply(void* inventory, const char* text, bool max_cap, const char* what, char* log_text, size_t log_cap);
// --- Card 274: the server copy's magazine follows the client's ---
// The AmmoMirror text also carries the client's equipped weapon's magazine counts as "@<ammunition tag> <count>" pairs
// (GetEquippedAmmunitionCount; GetAmmoCountOfEquippedAmmunitionType reads the reserve, not the magazine). The client
// sends at once when the magazine rose (a reload) and otherwise on the card 072 cadence. The host raises the copy's
// equipped weapon's magazine with SetEquippedAmmunition when the client's count is higher, never lowers it.
constexpr uint8_t kC274Behaviours = 2; // ESTAmmoBehaviour 1 primary, 2 secondary (card 137 grants use the same values)
constexpr char kC274MagazineMark = '@';
using GetEquippedAmmoCountFn = int32_t (*)(const void* weapon, uint8_t behaviour);
using GetEquippedAmmoTypeFn = uint64_t* (*)(uint64_t* out_tag, const void* weapon, uint8_t behaviour); // FGameplayTag by hidden pointer
extern GetEquippedAmmoCountFn g_c274_magazine_count;
extern GetEquippedAmmoTypeFn g_c274_magazine_type;
size_t c274_magazine_text(const void* pawn, char* out, size_t used, size_t cap, int32_t* rounds);
int32_t c274_magazine_rounds(const void* pawn);
void c274_magazine_apply(const void* pawn, const char* text, const char* what);
// --- Card 075: co-op mechs ---------------------------------------------------------------------------------------
// Pads and pods do not replicate (card 032): each machine runs the pod drop, and the pod's Blueprint spawns a
// BP_BaseMech_C through its SpawnActor node. The host's mech replicates, so on a client that second spawn is a
// duplicate the client owns (local role 3). The client's Blueprint spawn of an ASTPlayerMechBase class returns null.
// The mech has no server RPCs: a client's enter (OnInteraction) and exit input never reach the host. The client
// sends MechEnter with its mech's location, and MechLeave; the host runs OnInteraction with the sender's server copy
// (the host's own enter path) or RequestLeaveMech (the host's own exit input path) on the mech that PC drives.
using C75WorldSpawnFn = void* (*)(void* world, void* cls, const double* transform, const uint8_t* params);
extern C75WorldSpawnFn g_orig_c75_world_spawn;
extern C29SelfFn g_orig_c75_exit_pressed;
extern C29SelfFn g_orig_c75_pc_exit;
extern StaticClassFn g_actor_class;
extern StaticClassFn g_spline_class; // card 236: USplineComponent
const void* c75_nearest_pad(const void* world, const Vec3& loc, double* dist);
void* nearest_of_class(const void* world, const void* cls, const Vec3& loc, double* dist);
void* detour_c75_world_spawn(void* world, void* cls, const double* transform, const uint8_t* params);
// Card 132: USTWeaponUtilityLibrary::SpawnWeaponPickup (vehicles.cpp). kC132WeaponDefPickupClass: the definition's
// pickup class (USTWeaponDefinition +0x120, read by SpawnWeaponPickup before its spawn).
using SpawnWeaponPickupFn = void* (*)(void* context, void* def, const double* loc, const double* rot, uint64_t tag1,
                                      int32_t count1, uint64_t tag2, int32_t count2);
extern SpawnWeaponPickupFn g_orig_c132_spawn_weapon_pickup;
// Card 132: USTWeaponUtilityLibrary::RemoveWeaponFromOwnerInventory (vehicles.cpp), logged on both sides.
using RemoveWeaponFromOwnerFn = bool (*)(void* weapon, bool arg2, bool spawn_pickup);
extern RemoveWeaponFromOwnerFn g_orig_c132_remove_weapon;
bool detour_c132_remove_weapon(void* weapon, bool arg2, bool spawn_pickup);
constexpr size_t kC132WeaponDefPickupClass = 0x120;
void* detour_c132_spawn_weapon_pickup(void* context, void* def, const double* loc, const double* rot, uint64_t tag1,
                                      int32_t count1, uint64_t tag2, int32_t count2);
bool c105_client_blocks(const void* cls, unsigned long long caller);
void c105_log_block(const char* what, const void* cls, const Vec3& at);
void detour_c75_mech_on_interaction(void* iface, void* interactor);
// Card 452: the mech's CanInteract, false on the client for a mech another player pilots (vehicles.cpp).
using C452CanInteractFn = bool (*)(void* iface, void* interactor, void* failed);
extern C452CanInteractFn g_orig_c452_mech_can_interact;
bool detour_c452_mech_can_interact(void* iface, void* interactor, void* failed);
void detour_c123_turret_on_interaction(void* iface, void* interactor);
void c123_client_press(void* interactor);
extern C29InteractFn g_orig_turret_on_interaction;
void detour_c75_exit_pressed(void* self);
void detour_c75_pc_exit(void* pc);
// The soldier behind each mech's last EnterMech (identity only), so the leave lines can name it: the mech's
// ControllingPlayerCharacter reads null on the host while a player drives it. Two players, two pads: kC75Pilots slots.
constexpr int kC75Pilots = 4;
struct C75Pilot {
    const void* mech;
    const void* soldier;
};
extern C75Pilot g_c75_pilots[kC75Pilots];
const void* c75_leave_soldier(const void* mech);
// --- Card 079: a mech is not a card 043 player ------------------------------------------------------------------
// Always on in co-op: a mech's HandleDeathEvent and StartDeath run the game's own destruction (c43_death and
// c43_skip_start_death let them through; c43_enter_down refuses a mech), and the host refuses EnterMech for a
// pawn that card 043 holds Downed, Out or Dead. A client's MechEnter is already refused earlier, by
// c75_sender_copy; this detour covers the host's own E and anything else that reaches EnterMech.
// Log only (UBWCOOP_C79_LOG=1): the mech's StartDeath, self-destruct and DeathComplete, and each PlayerController
// pawn change. Test only (UBWCOOP_C79_KILL=1): the host deals lethal hits to a piloted mech after its EnterMech.
using C79DeathFn = void (*)(void* mech, const void* damage_event);
using C79MechFn = void (*)(void* mech);
constexpr uint32_t kC79Lines = 96;
constexpr uint32_t kC79RefuseLines = 32;
constexpr ULONGLONG kC79KillDelayMs = 8000;
struct C79State {
    ULONGLONG kill_ms; // host test: the next lethal hit; 0 = none pending
    uint32_t kill_tries;
    uint32_t lines;
    uint32_t refuse_lines;
    uint32_t watch_lines;
    int32_t last_id[kC43Max]; // each slot's PC pawn id at the last step
    bool down_done;           // host test: the down test has run this session
};
extern C79State g_c79;
extern C79DeathFn g_orig_c79_start_death;
extern C79MechFn g_orig_c79_death_complete;
extern C79MechFn g_orig_c79_self_destruct;
extern C79MechFn g_orig_c79_sd_init;
extern C79MechFn g_orig_c79_sd_complete;
float c79_health(const void* pawn);
bool c79_refuse_enter(const void* mech, const void* pawn);
void c79_note_enter(const void* mech);
void detour_c79_start_death(void* mech, const void* damage_event);
void detour_c79_death_complete(void* mech);
void detour_c79_self_destruct(void* mech);
void detour_c79_sd_init(void* mech);
void detour_c79_sd_complete(void* mech);
void c79_watch_pawns(const C43Roster& r, ULONGLONG now);
void c79_kill_step(const void* world, const C43Roster& r, ULONGLONG now);
void c79_down_step(const void* world, const C43Roster& r, ULONGLONG now);
void detour_c75_enter_mech(void* mech, void* pawn);
// Card 255: ASTPlayerController::StartBoardingTheMech (0x051DE360) is `add rcx,0x740; jmp` the sparse
// OnWantsToBoardMech Broadcast; EnterMech calls it on the boarding pawn's PC before the boarding montage. Host: a
// client's boarding also broadcasts on the host's local PC, whose delegate the level scripts bind (FED_C2M1).
extern C29SelfFn g_orig_c255_start_boarding;
void detour_c255_start_boarding(void* pc);
void detour_c75_leave_mech(void* mech);
// 1 Hz: the ASTPlayerMechBase actors in the world. A change in their count, or in how many this side owns (local
// role 3: on a client, a duplicate), logs every mech with its roles, location and nearest pad.
constexpr ULONGLONG kC75CountMs = 1000;
constexpr uint32_t kC75CountLines = 32;
struct C75Count {
    ULONGLONG next_ms;
    uint32_t mechs;
    uint32_t own;
    uint32_t lines;
    const void* piloted; // identity only: the mech whose location was logged last
    Vec3 piloted_at;
    uint32_t move_lines;
    uint32_t cmc_lines;
};
extern C75Count g_c75c;
void c75_piloted_step(const C23List& list);
void c75_client_possess_step(const C23List& list);
constexpr uint32_t kC76Abilities = 16;
constexpr uint32_t kC76Held = 4;            // mech input tags tracked as held
constexpr uint32_t kC76Mechs = 4;
constexpr uint32_t kC76Npcs = 24;
constexpr double kC76NearMech = 4000.0;     // spawns, FX and NPCs within this many uu of a mech are logged
constexpr ULONGLONG kC76StepMs = 1000;
enum C76Kind : uint32_t { kC76Tag, kC76Try, kC76Fire, kC76Spawn, kC76Damage, kC76Fx, kC76Health, kC76Kinds };
struct C76Npc {
    int32_t id; // object_index; 0 = free
    float health;
    bool seen;  // in range on the last step
};
struct C76State {
    uint32_t lines[kC76Kinds];
    uint64_t held[kC76Held];             // FName keys of the mech input tags pressed now
    const void* ability[kC76Abilities];  // identity only: activation classes seen
    uint32_t ability_lines[kC76Abilities];
    Vec3 mech_at[kC76Mechs];
    uint32_t mechs;
    C76Npc npc[kC76Npcs];
    ULONGLONG next_ms;
};
extern C76State g_c76;
bool c76_note(C76Kind k);
const void* c76_mech(const void* actor);
double c76_mech_dist(const Vec3& at);
bool c76_near_mech(const Vec3* at);
unsigned long long c76_now();
bool c76_ability_note(const void* cls);
void c76_npc(const void* actor, double dist, ULONGLONG now);

void c21_mark_health_net();
void c24_reset_map();
const uint8_t* c27_find_any(const void* ustruct, const char* const* names);
int32_t c43_find_pawn(const void* pawn);
int c45_browse_enter(void* engine, void* url);
void c45_browse_return(int result);
void c48_client_step(void* pc, ULONGLONG now);
void c48_respawn(const void* pawn, ULONGLONG now);
void c49_step(void* pc, ULONGLONG now);
void c50_send_use_name(void* interactor, void* pc);
void c52_host_browse(void* engine, uint8_t* url);
void c52_listen(bool ok);
bool c63_init();
void c63_step(void* engine);
void c64_allow_client_nav();
bool c68_install(uint8_t* base, const IMAGE_NT_HEADERS64* nt);
void c68_step(void* engine);
bool c69_install(uint8_t* base, const IMAGE_NT_HEADERS64* nt);
void c69_step(void* engine);
void c72_client_step(void* pc, ULONGLONG now);
void c73_client_step(void* pc, ULONGLONG now);
bool c73_install(uint8_t* base, const IMAGE_NT_HEADERS64* nt);
void c73_step(void* engine);
bool c74_install(uint8_t* base, const IMAGE_NT_HEADERS64* nt);
void c75_count_step(const void* world, ULONGLONG now);
void c76_step(const void* world, ULONGLONG now);
bool c79_install(uint8_t* base, const IMAGE_NT_HEADERS64* nt);
void c79_step(const void* world, ULONGLONG now);
int card019_sync_aggregators(void* asc, const void* set);
extern uint32_t g_c24_local_lines;
extern uint32_t g_c24_rpc_lines;
extern bool g_c57_solo;
extern bool g_c60_host_own;
extern bool g_c68_log;
extern bool g_c69_log;
extern bool g_c79_kill;
extern bool g_c79_log;
extern bool g_gate_c23;
extern bool g_gate_c23_c25;
extern bool g_gate_c29_c32;
extern bool g_gate_c30;
extern bool g_gate_c32;
extern bool g_gate_c40;
extern bool g_gate_c41;
extern bool g_gate_c62;
extern bool g_gate_c191;
extern bool g_gate_c62_diag;
void read_card_flags();

// Card 155: the play-build logs (playlog.cpp). Every line goes through pl_allow (per-class lines per second, a global
// cap; drops counted per class for the summary).
enum class PlClass : uint8_t { Clock, Rtt, Hit, Shot, Launch, Input, Cursor, Widget, Weapon, Ability, Ads, Grenade,
                               Door, DoorBlock, Audio, FxWatch, Count };
enum class PlCount : uint8_t { ShotOwn, ShotCopy, Hit, Down, Death, Load, Join, Rejoin, Mismatch, Count };
using AudioPlayInternalFn = void (*)(void* comp, const void* request, void* sound_override);
using CmcHandleImpactFn = void (*)(void* cmc, const void* hit, const Vec3* delta);
using RemoveFromParentFn = void (*)(void* widget);
using RemoveMappingContextFn = RemoveContextFn;
extern AudioPlayInternalFn g_orig_pl_play_internal;
extern CmcHandleImpactFn g_orig_pl_impact;
extern AddToViewportFn g_orig_pl_add_to_viewport;
extern RemoveFromParentFn g_orig_pl_remove_from_parent;
extern RemoveMappingContextFn g_orig_pl_remove_mapping_context;
bool pl_allow(PlClass c);                                   // true: the caller may write one line of class c now
bool pl_once(uint64_t key, ULONGLONG now, ULONGLONG ms);    // true at most once per key per ms (64-row table)
void pl_count(PlCount c, uint32_t n = 1);                   // a session counter for the summary
int32_t pl_player_slot(const void* actor);                  // slot of a PC pawn, or -1 (pointer compares)
uint32_t pl_shot_seq();                                     // the last own shot seq (card 157 puts it on the wire)
void pl_rtt(uint32_t ms);                                   // a card 026 ping round trip
uint32_t pl_rtt_last();                                   // card 159: the last card 026 round trip (ms), 0 before one
uint32_t pl_rtt_floor();                                 // card 159 reopen: lowest of the last 5 (card 340) round trips (ms), 0 before one
void pl_remote_seen(const void* pawn);                      // client: card 028 drew this remote pawn
void pl_step(void* world, ULONGLONG now);                   // per tick, both sides (fire.cpp c24_step)
bool pl_shot(const void* shooter, const Vec3& at, const Vec3& rot, bool copying); // true: a player launch, logged here
bool pl_shot_undo(); // card 286: the client's released own grenade leaves its open burst; false when none was open
void pl_activation(const void* asc, const void* ability, bool ok, bool server_request);
void pl_ads_set(int ads);                                    // card 160: the own ADS state, FOV now and +kPlAdsFovMs
void pl_execute(const void* instance);                      // EI Execute: an FInputActionInstance
// volume: card 176, the component's VolumeMultiplier as PlayInternal reads it; below 0 when not known (not printed).
void pl_sound(const void* owner, const void* sound, const Vec3& at, const void* attenuation, int spatial,
              float volume = -1.0f);
void pl_sound_row(const void* owner, const void* sound, const Vec3& at, const void* override_atten, bool attached);
bool pl_remote_source(const void* owner, const Vec3& at, double max_uu, double& d); // card 162
double pl_listener_uu(const Vec3& at);                      // card 173: uu from the own pawn to `at`, -1 with none
// Card 162 (fx_c136.cpp): the attenuation a sound plays with; a remote player's sound with none gets its fallback.
// Card 304: player_source is the sender's word (kC136FlagPlayerSource) that the sound is its player's; without it the
// owner's class and role decide, and only then the playlog's remote list (which empties 3000 ms after a sighting).
void* c162_remote_atten(const void* context, const void* sound, const void* atten, const void* owner, const Vec3& at,
                        bool player_source);
const void* c304_player_pawn(const void* owner); // the player pawn the owner is (or a weapon's pawn), or null
void c304_row(const void* sound, bool player_source, const char* key, bool owner_resolved, const void* atten,
             const Vec3& at); // key: "guid", "placed" or "world"
void c304_map_end(); // pl_map: the per-map card304 tally line, then reset
// Card 314 (fx_c136.cpp): an audio component play of another machine's player: an unspatialized one gets the card 162
// fallback as its AttenuationSettings (at atten_at); one with no settings (2D) is muted by VolumeMultiplier (at
// volume_at) unless `muted` (card 176's verdict) already did. One row per sound per map.
void c314_component_play(void* comp, const void* owner, const void* sound, int32_t atten_at, int32_t volume_at,
                         bool muted);
void c314_map_end(); // pl_map: the per-map card314 tally line, then reset
// Card 314 review: VolumeMultiplier (at volume_at) to 0, keeping the replaced value for c314_restore_volume (both mute
// sites, cards 176 and 314). c314_restore_volume, before every component play: a component this muted gets its value
// back, so a later play the local player should hear is not silent.
void c314_mute(void* comp, int32_t volume_at);
void c314_restore_volume(void* comp, int32_t volume_at);
void* c162_call_atten(const C136Call& c);
void pl_clock_client(void* pc, const ue::chan::WireString* s);
void pl_clock_host(void* pc, const ue::chan::WireString* s);
void pl_first_pong(void* pc);                               // host: a client's first Pong of a map
void pl_map();                                              // per map arm: the map-end summary, then reset
void pl_map_end(); // card 182, LoadMap enter: clears the g_pl pointers into the old world
void pl_session_end();
void detour_pl_play_internal(void* comp, const void* request, void* sound_override);
// Card 176, host (player_c99.cpp): mutes a server copy's 2D play of an audio component (VolumeMultiplier at
// volume_at = 0) before PlayInternal runs; logged in the card 160 style. True when muted.
bool c176_mute_copy_2d(void* comp, const void* owner, const void* sound, int32_t volume_at);
const void* c176_settings_to_apply(const void* comp); // card 314: GetAttenuationSettingsToApply, null for none
void detour_pl_impact(void* cmc, const void* hit, const Vec3* delta);
void detour_pl_add_to_viewport(void* widget, int32_t z);
void detour_pl_remove_from_parent(void* widget);
void detour_pl_remove_mapping_context(void* iface, const void* context, const uint8_t* options);

// --- Card 326: the sync registry (sync_registry.cpp), one row per state mechanism ---
void sync_map_end(void* old_world);  // ubw_load_map_enter: card 332 teardown begins, then each map_end in table order
void sync_map_arm();                 // client_diag_load_map_return: teardown ends, then each map_arm (live() checked)
void sync_map_failed();              // client_diag_load_map_return, LoadMap failed: the teardown state ends
void sync_join_replay(void* pc);     // host, chan_ops.cpp c26_host_pong at the first Pong (c50_first_pong)
void sync_after_load(const void* world, C137LoadCounts& n); // host, save_c121.cpp detour_c137_load_slot
void sync_quit(void* world);         // testrun.cpp quit: teardown begins, then the quit reports
// Map end and live() functions each mechanism exports to the table.
void c43_map_end();            // hooks_ubw.cpp: card 043 entries and holds (also the respawn in place, death.cpp)
void c132_ext_map_end();       // ext_c132.cpp (was c132_ext_arm_map)
void c132_pose_map_end();      // pose_c132.cpp (was c132_pose_arm_map)
void c132_decal_map_end();     // decal_c132.cpp (was c132_decal_arm_map)
void c137_gib_map_end();       // gib_c137.cpp (was c137_gib_arm_map)
void c133_map_end();           // hold.cpp (was c133_map)
void c49_join_replay(void* pc); // objectives.cpp (was c49_first_pong)
void c43_join_replay(void* pc); // death.cpp: card 327, every other player's life state
void c75_join_replay(void* pc); // vehicles.cpp: card 323, the mech seats held now
const void* c75_soldier_of(const void* mech); // vehicles.cpp: card 327, the soldier piloting this mech, or null
const void* c75_pilot_body(const void* pawn); // vehicles.cpp: card 300, the listed soldier in a piloted mech, or null
void c137_gib_join_replay(void* pc); // gib_c137.cpp: card 323, the rest rows of the parts alive now
void c74_join_replay(void* pc);      // grenades.cpp: card 323, the grenades in flight (and card 356 c73_send_all)
void c73_send_all(void* pc);         // grenades.cpp: card 356, the server copy's item counts to its client
void c133_join_replay(void* pc);     // hold.cpp: card 323, the holds in progress
int32_t c49_live();
int32_t c95_live();
int32_t c98_live();
int32_t c132_ext_live();
int32_t c132_pose_live();
int32_t c132_decal_live();
int32_t c132_dropship_live();
int32_t c136_live();
int32_t c208_live();
int32_t c310_live();
int32_t c43_live();
int32_t c137_gib_live();
int32_t c74_live();
int32_t c133_live();
// --- Card 332: the host's world teardown state (sync_registry.cpp) ---
void c332_teardown_end();                 // the arm (and a failed load): the teardown row, then the state clears
void c332_detach();                       // dllmain detach: the teardown row if one is open
const char* c332_hold(wchar_t op);        // c139_client_hold: non-null while the teardown state is on (counted)
bool c332_on();                           // the teardown state is on: c332_hold refuses every host frame
bool c332_leaving(const void* world);     // world is the one being left (c310_exec)
bool c332_in_leaving_world(const void* object); // object's outer chain reaches the world being left (c49_emit)
void c332_journal_skipped();              // c49_record: one teardown call not journalled
bool sync_frame_ok(const void* pc, wchar_t op); // ChanOps::frame_ok: false drops a frame (counted; card 332 client,
                                                // card 348 host)
// Card 348 (fire.cpp): the host's ping counter at its map arm; a Pong to an older ping is stale (counted).
void c348_ping_arm();                 // host, from sync_map_arm
bool c348_pong_current(uint32_t n);   // host: Pong n answers a ping sent after the last map arm
uint32_t c348_stale_pongs_take();     // host: this map's stale Pongs, reset
const char* c132_op_name(uint8_t op);     // ledger_c132.cpp

} // namespace ubw::hooks
