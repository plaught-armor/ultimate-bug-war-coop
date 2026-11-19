#pragma once

// Card 014: scripted input for unattended runs (UBWCOOP_AUTOINPUT=1).
//
// A fixed key/axis script is fed to the local PlayerController's InputKey (the PC vtable slot
// UGameViewportClient::InputKey and ::InputAxis call after their console/UI checks), so the keys
// travel the same path as real input: APlayerController::InputKey -> PlayerInput->InputKey ->
// UEnhancedPlayerInput::InputKey. Game thread only. No game pointer is kept: the engine is passed
// in on every call and the PC is looked up each tick.

#include <stdint.h>

namespace ubw::autoinput {

using GetGameWorldFn = void* (*)(void* engine);
using WorldGetFn = void* (*)(const void* world);
using GetLocalPlayerFn = void* (*)(const void* pc);
using FKeyCtorFn = void* (*)(void* key, const wchar_t* name);
using FKeyDtorFn = void (*)(void* key);
using KeyArgsCtorFn = void* (*)(void* args, void* viewport, int32_t device, const void* key, int32_t event,
                                float amount, bool touch, uint64_t timestamp);
using RequestExitFn = void (*)(const wchar_t* reason);

// Resolved by ubw::init before any hook is enabled.
struct Api {
    const uint8_t* exe_base;
    uint32_t pc_input_key_rva; // APlayerController::InputKey(const FInputKeyEventArgs&): expected slot target
    GetGameWorldFn get_game_world;
    WorldGetFn world_own_pc; // card 349: the local PC (own_pc), not GetFirstPlayerController
    GetLocalPlayerFn get_local_player;
    FKeyCtorFn fkey_ctor;
    FKeyDtorFn fkey_dtor;
    KeyArgsCtorFn key_args_ctor;
    RequestExitFn request_exit;
};

// Stores the API, whether scripted input is on, the script start delay after arm() and where it is
// armed (`host_sprint`: on the host once its landed character is possessed; card 020 runs the same
// parity script either way). Call once, before any hook is enabled.
void init(const Api& api, bool enabled, uint32_t start_delay_ms, bool host_sprint);

bool enabled();

// True when enabled with UBWCOOP_HOST_SPRINT=1: the script is armed on the host after landing.
bool host_script();

// True only while a scripted key is inside the PC's InputKey call.
bool injecting();

// True from arm() until the script ends (and, on a net client, the exit is requested).
// Read by the Tick hook: off costs one load and one branch.
extern bool g_running;
inline bool running() {
    return g_running;
}

// Starts the script after the configured delay, once per map. True when armed by this call; the
// host sprint script refuses a net client.
bool arm(bool net_client);

// Card 020: the parity script's current action name ("pre" before the start, "done" after the end),
// for the host rows. Game thread.
const char* current_action();

// Card 030: one Pressed or Released event of `key_name` through `pc`'s InputKey, outside the script.
// False (nothing sent) when pc is null or its InputKey slot does not match. Game thread.
bool key_event(void* pc, const wchar_t* key_name, bool pressed);

// Card 063: one Axis event of `value` on `key_name` (e.g. MouseX) through `pc`'s InputKey, outside the script.
// False (nothing sent) when pc is null or its InputKey slot does not match. Game thread.
bool axis_event(void* pc, const wchar_t* key_name, float value);

// Game thread, every UGameEngine::Tick while running(), before the engine's Tick.
void tick(void* engine);

// UEngine::LoadMap entry: releases held keys on the current local PC, then disarms.
void load_map_enter(void* engine);

} // namespace ubw::autoinput
