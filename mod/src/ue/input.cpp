#include "ue/input.h"

#include <wchar.h>

namespace ue::input {

using ue::reflect::FStringRaw;

GetLocalPlayerFn g_get_local_player = nullptr;
ue::actor::GetSubsystemFn g_get_enhanced_input = nullptr;
FKeyDestructorFn g_fkey_destructor = nullptr;   // FKey::~FKey, called
GetInterfaceAddressFn g_get_interface_address = nullptr; // UObjectBaseUtility::GetInterfaceAddress
WorldGetFn g_world_game_viewport = nullptr;              // UWorld::GetGameViewport
InputModeCtorFn g_input_mode_game_only_ctor = nullptr; // FInputModeGameOnly::FInputModeGameOnly
AddMovementInputFn g_add_movement_input = nullptr;
ConsoleCommandFn g_pc_console_command = nullptr;
RequestScreenshotFn g_request_screenshot = nullptr; // FScreenshotRequest::RequestScreenshot (static)

unsigned cursor_bit(const void* pc) {
    return *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(pc) + ue::actor::kPcCursorFlags) & 1u;
}

void console_command(void* pc, const wchar_t* cmd) {
    const int32_t len = static_cast<int32_t>(wcslen(cmd)) + 1; // FString Num counts the terminator
    const FStringRaw text{cmd, len, len};
    FStringRaw out{};
    g_pc_console_command(pc, &out, &text, false);
    if (out.data != nullptr) {
        ue::reflect::g_memory_free(const_cast<wchar_t*>(out.data));
    }
}

void request_shot(void* pc, bool show_ui) {
    console_command(pc, show_ui ? L"shot showui" : L"shot");
}

} // namespace ue::input
