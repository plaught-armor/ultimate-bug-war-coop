#pragma once

// UE net glue (split S6): the engine lifecycle detours (Init, Tick, Browse, LoadMap, Listen), which log
// the engine's own events and call the mod through a step table, plus the net-mode and role helpers and
// the travel functions. The pointers are resolved by the kCallsUe table in ue/hooktable.cpp, the detours installed by
// the kHooksUe table.

#include <stddef.h>
#include <stdint.h>

namespace ue::net {

// The mod's work at fixed points of the engine detours. Each step may be null (skipped). Filled once by
// set_engine_steps before any hook is enabled; read on the game thread only afterwards.
struct EngineSteps {
    void (*init_return)();                          // after the original Init returns
    void (*pre_tick)(void* engine);                 // before the original Tick
    void (*post_tick)(void* engine);                // after it
    int (*browse_enter)(void* engine, void* url); // kBrowseRun: run it; any other value: not run, returned as the result
    void (*browse_return)(int result);
    void (*load_map_enter)(void* engine, void* pending);
    void (*load_map_return)(bool ok, void* pending);
    void (*listen_return)(bool ok);
    // Card 346: a seamless travel's map change (twice per travel: the transition world, then the destination).
    // Enter: UNetDriver::CleanupWorldForSeamlessTravel, old world still the game world. Return: after the
    // FSeamlessTravelHandler::Tick call that ran it, new world current with its GameMode and BeginPlay done.
    void (*seamless_enter)(void* engine);
    void (*seamless_return)();
};
void set_engine_steps(const EngineSteps& steps); // once, by the caller before ue::install

// --- The engine detours and their originals (installed through kHooksUe) --------------------------------

using GameEngineInitFn = void (*)(void* self, void* engine_loop);
using GameEngineTickFn = void (*)(void* self, float delta_seconds, bool idle_mode);
using BrowseFn = int (*)(void* self, void* world_context, void* url_copy, void* error);
using LoadMapFn = bool (*)(void* self, void* world_context, void* url_copy, void* pending, void* error);
using ListenFn = bool (*)(void* world, void* url);
extern GameEngineInitFn g_orig_game_engine_init;
extern GameEngineTickFn g_orig_tick;
extern BrowseFn g_orig_browse;
extern LoadMapFn g_orig_load_map;
extern ListenFn g_orig_listen;
// UWorld::Listen itself (a call row, set whenever install succeeds). Callers outside detour_listen use this one:
// the Listen hook is diag-gated, so g_orig_listen is null in a play build (b16).
extern ListenFn g_listen;

void detour_game_engine_init(void* self, void* engine_loop);
void detour_tick(void* self, float delta_seconds, bool idle_mode);
int detour_browse(void* self, void* world_context, void* url_copy, void* error);
bool detour_load_map(void* self, void* world_context, void* url_copy, void* pending, void* error);
bool detour_listen(void* world, void* url);
// Card 346: the seamless travel map-change detours (kHooksUe).
using SeamlessCleanupFn = void (*)(void* net_driver);
using SeamlessTickFn = void* (*)(void* handler);
extern SeamlessCleanupFn g_orig_seamless_cleanup;
extern SeamlessTickFn g_orig_seamless_tick;
void detour_seamless_cleanup(void* net_driver);
void* detour_seamless_tick(void* handler);
// UWorld::ServerTravel(const FString&, bAbsolute, bShouldSkipGameNotify) (a call row): the host's seamless travel.
using ServerTravelFn = bool (*)(void* world, const void* url, bool absolute, bool skip_game_notify);
extern ServerTravelFn g_server_travel;
// The EBrowseReturnVal name of a UEngine::Browse result ("?" when out of range).
constexpr int kBrowseRun = -1; // browse_enter: run the original
constexpr int kBrowseSuccess = 0;
constexpr int kBrowseFailure = 1;
const char* browse_result_name(int r);

// --- Net mode and roles -------------------------------------------------------------------------------

using ActorNetModeFn = uint32_t (*)(const void* actor);
extern ActorNetModeFn g_actor_net_mode;

inline constexpr uint32_t kNetModeClient = 3; // ENetMode::NM_Client (UNetDriver::GetNetMode returns 3)
inline constexpr uint32_t kNetModeListenServer = 2; // ENetMode::NM_ListenServer
inline constexpr uint32_t kNetModeStandalone = 0; // ENetMode::NM_Standalone

inline constexpr size_t kActorRemoteRole = 0x60;            // uint8 (AActor::execGetRemoteRole)
inline constexpr size_t kActorLocalRole = 0x168;            // uint8 (AActor::execGetLocalRole)
inline constexpr uint8_t kRoleSimulatedProxy = 1;            // ENetRole::ROLE_SimulatedProxy
inline constexpr uint8_t kRoleAutonomousProxy = 2;      // ENetRole::ROLE_AutonomousProxy
inline constexpr uint8_t kRoleAuthority = 3;                 // ENetRole::ROLE_Authority

// The actor's ENetMode (low byte of AActor::GetNetMode).
unsigned net_mode(const void* actor);

// --- Player controller and travel ---------------------------------------------------------------------

using GetPlayerControllerFn = void* (*)(const void* world_context, int32_t index);
extern GetPlayerControllerFn g_get_player_controller; // UGameplayStatics::GetPlayerController

using GetGameWorldFn = void* (*)(void* game_engine);
using SetClientTravelFn = void (*)(void* engine, void* world, const wchar_t* url, int travel_type);
extern GetGameWorldFn g_get_game_world;
extern SetClientTravelFn g_set_client_travel;
using FurlDestroyFn = void (*)(void* url);
extern FurlDestroyFn g_furl_destroy; // FURL::~FURL: detour_browse destroys the by-value URL of a Browse it does not run
inline constexpr int kTravelAbsolute = 0; // ETravelType::TRAVEL_Absolute

} // namespace ue::net
