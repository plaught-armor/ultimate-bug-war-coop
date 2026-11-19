#pragma once

// UE input: player controllers, local players, Enhanced Input mappings, the game viewport and console
// commands. The engine function pointers, their typedefs, the layout offsets and the small helpers that
// read them (split S4b). The pointers are resolved by the kCallsUe table in ue/hooktable.cpp.

#include "ue/actor.h"
#include "ue/reflect.h"

#include <stddef.h>
#include <stdint.h>

namespace ue::input {

// --- Engine function types and pointers (defined as nullptr in input.cpp, resolved by kCallsUe) ------------

using GetLocalPlayerFn = void* (*)(const void* pc);
using FKeyDestructorFn = void (*)(void* key);
using GetInterfaceAddressFn = void* (*)(void* object, const void* cls);
using WorldGetFn = void* (*)(const void* world);
using InputModeCtorFn = void* (*)(void* storage);
using AddMovementInputFn = void (*)(void* pawn, const double* world_accel, bool force);
using ConsoleCommandFn = ue::reflect::FStringRaw* (*)(void* pc, ue::reflect::FStringRaw* out,
                                                     const ue::reflect::FStringRaw* cmd, bool write_to_log);
using RequestScreenshotFn = void (*)(const ue::reflect::FStringRaw* filename, bool show_ui, bool add_suffix, bool hdr);

extern GetLocalPlayerFn g_get_local_player;
extern ue::actor::GetSubsystemFn g_get_enhanced_input;
extern FKeyDestructorFn g_fkey_destructor;
extern GetInterfaceAddressFn g_get_interface_address;
extern WorldGetFn g_world_game_viewport;
extern InputModeCtorFn g_input_mode_game_only_ctor;
extern AddMovementInputFn g_add_movement_input;
extern ConsoleCommandFn g_pc_console_command;
extern RequestScreenshotFn g_request_screenshot;

// --- AController ------------------------------------------------------------------------------------------

inline constexpr size_t kControllerControlRotation = 0x320; // FRotator (AController::GetControlRotation)
inline constexpr size_t kControllerIgnoreMove = 0x339; // uint8 IgnoreMoveInput (AController::SetIgnoreMoveInput)
inline constexpr size_t kControllerIgnoreLook = 0x33A; // uint8 IgnoreLookInput (AController::IsLookInputIgnored)

// --- Enhanced Input ---------------------------------------------------------------------------------------

inline constexpr size_t kSubsystemInputInterface = 0x30; // IEnhancedInputSubsystemInterface base
// UInputMappingContext::Mappings: TArray at +0x30 (MapKey+0x64), element 0x50 bytes (MapKey+0x12D,
// `lea rcx,[rax+4*rax]; shl rcx,4`). FEnhancedActionKeyMapping: Key (FName first) +0x28
// (FEnhancedActionKeyMapping ctor 0x044637A0).
inline constexpr size_t kContextMappings = 0x30;
inline constexpr size_t kMappingBytes = 0x50;
inline constexpr size_t kMappingKeyName = 0x28;
inline constexpr int32_t kMaxMappingsScanned = 256;
inline constexpr size_t kInstanceSourceAction = 0; // FInputActionInstance::SourceAction (DynamicSignature Execute +0x1D)

// --- UGameViewportClient ----------------------------------------------------------------------------------

inline constexpr size_t kViewportCaptureMode = 0x3BA; // UGameViewportClient::SetMouseCaptureMode store
inline constexpr size_t kViewportLockMode = 0x3BC;    // UGameViewportClient::SetMouseLockMode store

// --- Helpers ---------------------------------------------------------------------------------------------

// APlayerController::bShowMouseCursor (bit 0 of the cursor flags).
unsigned cursor_bit(const void* pc);
// The PC's ConsoleCommand(cmd), not written to the log; frees the returned FString.
void console_command(void* pc, const wchar_t* cmd);
// ConsoleCommand("shot"), or "shot showui" when show_ui is true: a plain "shot" leaves the UMG overlay out
// of the image, and showui includes it.
void request_shot(void* pc, bool show_ui);

} // namespace ue::input
