// The engine hook set (kHooksUe, kCallsUe) and ue::install: co-op dll only. The machinery in hooktable.cpp is
// shared with ubwperf.dll (card 578), which installs its own sets.
#include "ue/hooktable.h"

#include "ue/actor.h"
#include "ue/chan.h"
#include "ue/gas.h"
#include "ue/input.h"
#include "ue/log.h"
#include "ue/net.h"
#include "ue/reflect.h"
#include "ue/widget.h"

#include <MinHook.h>

#include <iterator>
#include <stdio.h>
#include <string.h>


namespace ue::hooktable {
namespace {

namespace offsets = ubw::offsets;
using namespace ue::actor;
using namespace ue::chan;
using namespace ue::gas;
using namespace ue::input;
using namespace ue::net;
using namespace ue::reflect;
using namespace ue::widget;

// The engine set: rows whose detour or call target is defined in ue/. Installed before the game set, so
// UGameEngine::Tick is hooked before the game rows; nothing calls Init or Tick until well after install ends.
const HookSpec kHooksUe[] = {
    {&offsets::UGameEngine_Init, reinterpret_cast<void*>(&detour_game_engine_init),
     reinterpret_cast<void**>(&g_orig_game_engine_init)},
    {&offsets::UEngine_Browse, reinterpret_cast<void*>(&detour_browse), reinterpret_cast<void**>(&g_orig_browse)},
    {&offsets::UEngine_LoadMap, reinterpret_cast<void*>(&detour_load_map),
     reinterpret_cast<void**>(&g_orig_load_map)},
    {&offsets::UWorld_Listen, reinterpret_cast<void*>(&detour_listen), reinterpret_cast<void**>(&g_orig_listen), &g_gate_diag},
    {&offsets::UGameEngine_Tick, reinterpret_cast<void*>(&detour_tick), reinterpret_cast<void**>(&g_orig_tick)},
    {&offsets::UNetDriver_CleanupWorldForSeamlessTravel, reinterpret_cast<void*>(&detour_seamless_cleanup),
     reinterpret_cast<void**>(&g_orig_seamless_cleanup)}, // card 346
    {&offsets::FSeamlessTravelHandler_Tick, reinterpret_cast<void*>(&detour_seamless_tick),
     reinterpret_cast<void**>(&g_orig_seamless_tick)}, // card 346
    {&offsets::APlayerController_ServerChangeName_Implementation, reinterpret_cast<void*>(&detour_server_change_name), reinterpret_cast<void**>(&g_orig_server_change_name)},
    {&offsets::APlayerController_ClientMessage_Implementation, reinterpret_cast<void*>(&detour_client_message), reinterpret_cast<void**>(&g_orig_client_message)},
};

const CallSpec kCallsUe[] = {
    {&offsets::UWorld_Listen, reinterpret_cast<void**>(&g_listen)}, // b16: the H listen; the hook row is diag-gated
    {&offsets::FText_FromString, reinterpret_cast<void**>(&g_ftext_from_string)},
    {&offsets::FText_ToString, reinterpret_cast<void**>(&g_ftext_to_string)},
    {&offsets::UTextBlock_StaticClass, reinterpret_cast<void**>(&g_text_block_class)},
    {&offsets::UEditableTextBox_StaticClass, reinterpret_cast<void**>(&g_editable_text_box_class)},
    {&offsets::UWidget_SetVisibility, reinterpret_cast<void**>(&g_widget_set_visibility)},
    {&offsets::UWidget_SetKeyboardFocus, reinterpret_cast<void**>(&g_widget_set_keyboard_focus)},
    {&offsets::UWidget_RemoveFromParent, reinterpret_cast<void**>(&g_widget_remove_from_parent)},
    {&offsets::FWeakObjectPtr_Assign, reinterpret_cast<void**>(&g_weak_assign)},
    {&offsets::UUserWidget_CreateWidgetInstance, reinterpret_cast<void**>(&g_create_widget_instance)},
    {&offsets::UUserWidget_AddToViewport, reinterpret_cast<void**>(&g_add_to_viewport)},
    {&offsets::UPanelWidget_AddChild, reinterpret_cast<void**>(&g_panel_add_child)},
    {&offsets::UWidgetBlueprintLibrary_SetInputMode_UIOnlyEx, reinterpret_cast<void**>(&g_ui_only_ex)},
    {&offsets::StaticFindObject, reinterpret_cast<void**>(&g_static_find_object)},
    {&offsets::UGameEngine_GetGameWorld, reinterpret_cast<void**>(&g_get_game_world)},
    {&offsets::UEngine_SetClientTravel, reinterpret_cast<void**>(&g_set_client_travel)},
    {&offsets::FURL_Dtor, reinterpret_cast<void**>(&g_furl_destroy)}, // card 345: a Browse not run
    {&offsets::UWorld_ServerTravel, reinterpret_cast<void**>(&g_server_travel)}, // card 346: host seamless travel
    {&offsets::FObjectPtr_IsA, reinterpret_cast<void**>(&g_object_ptr_is_a)},
    {&offsets::FUObjectItem_SetRootFlags, reinterpret_cast<void**>(&g_set_root_flags)}, // card 496: AddToRoot
    {&offsets::AActor_GetWorld, reinterpret_cast<void**>(&g_actor_get_world)},
    {&offsets::IsInGameThread, reinterpret_cast<void**>(&g_is_in_game_thread)},
    {&offsets::APlayerController_GetLocalPlayer, reinterpret_cast<void**>(&g_get_local_player)},
    {&offsets::ULocalPlayer_GetEnhancedInputSubsystem, reinterpret_cast<void**>(&g_get_enhanced_input)},
    {&offsets::FWeakObjectPtr_Get, reinterpret_cast<void**>(&g_weak_get)},
    {&offsets::AActor_InternalGetNetMode, reinterpret_cast<void**>(&g_actor_net_mode)},
    {&offsets::FKey_Destructor, reinterpret_cast<void**>(&g_fkey_destructor)},
    {&offsets::UObjectBaseUtility_GetInterfaceAddress, reinterpret_cast<void**>(&g_get_interface_address)},
    {&offsets::FName_GetDisplayNameEntry, reinterpret_cast<void**>(&g_name_entry)},
    {&offsets::FName_CtorFindName, reinterpret_cast<void**>(&g_fname_ctor)},
    {&offsets::FCollisionQueryParams_Ctor, reinterpret_cast<void**>(&g_query_params_ctor)},
    {&offsets::UWorld_LineTraceSingleByProfile, reinterpret_cast<void**>(&g_line_trace_by_profile)},
    {&offsets::UWorld_LineTraceSingleByChannel, reinterpret_cast<void**>(&g_line_trace_by_channel)},
    {&offsets::FMemory_Free, reinterpret_cast<void**>(&g_memory_free)},
    {&offsets::UNiagaraFunctionLibrary_SpawnSystemAtLocation, reinterpret_cast<void**>(&g_spawn_niagara)},
    {&offsets::AActor_GetLifeSpan, reinterpret_cast<void**>(&g_life_span)},
    {&offsets::StaticFindFirstObject, reinterpret_cast<void**>(&g_find_first_object)},
    {&offsets::AActor_SetActorLocation, reinterpret_cast<void**>(&g_set_actor_location)},
    {&offsets::AActor_SetActorLocationAndRotation, reinterpret_cast<void**>(&g_set_actor_location_rotation)},
    {&offsets::UAbilitySystemComponent_SetNumericAttributeBase, reinterpret_cast<void**>(&g_set_numeric_attribute_base)},
    {&offsets::APlayerController_ConsoleCommand, reinterpret_cast<void**>(&g_pc_console_command)},
    {&offsets::UObject_FindFunctionChecked, reinterpret_cast<void**>(&g_find_function_checked)},
    {&offsets::APlayerController_ClientMessage, reinterpret_cast<void**>(&g_client_message)},
    {&offsets::UAbilitySystemGlobals_GetAbilitySystemComponentFromActor, reinterpret_cast<void**>(&g_get_asc)},
    {&offsets::UGameplayStatics_GetPlayerController, reinterpret_cast<void**>(&g_get_player_controller)},
    {&offsets::UWorld_GetGameViewport, reinterpret_cast<void**>(&g_world_game_viewport)},
    {&offsets::FInputModeGameOnly_Ctor, reinterpret_cast<void**>(&g_input_mode_game_only_ctor)},
    {&offsets::FScreenshotRequest_RequestScreenshot, reinterpret_cast<void**>(&g_request_screenshot)},
    {&offsets::AActor_GetAttachParentActor, reinterpret_cast<void**>(&g_get_attach_parent_actor)},
    {&offsets::UGameInstance_RegisterReferencedObject, reinterpret_cast<void**>(&g_gi_register_ref)},
    {&offsets::UGameInstance_UnregisterReferencedObject, reinterpret_cast<void**>(&g_gi_unregister_ref)},
    {&offsets::UWorld_IsPaused, reinterpret_cast<void**>(&g_world_is_paused)},
    {&offsets::UAbilitySystemComponent_FindAbilitySpecFromClass, reinterpret_cast<void**>(&g_find_spec_from_class)},
    {&offsets::APawn_Internal_AddMovementInput, reinterpret_cast<void**>(&g_add_movement_input)},
    {&offsets::UEngine_GetEngineSubsystem_USTDataTableSubsystem, reinterpret_cast<void**>(&g_get_data_table_subsystem)},
    {&offsets::StaticLoadObject, reinterpret_cast<void**>(&g_static_load_object)},
    {&offsets::UClass_StaticClass, reinterpret_cast<void**>(&g_uclass_class)},
    {&offsets::UAIBlueprintHelperLibrary_SpawnAIFromClass, reinterpret_cast<void**>(&g_spawn_ai_from_class)},
    {&offsets::FActorSpawnParameters_ctor, reinterpret_cast<void**>(&g_spawn_params_ctor)},
    {&offsets::UWorld_SpawnActor, reinterpret_cast<void**>(&g_world_spawn_actor)},
    {&offsets::AActor_WasRecentlyRendered, reinterpret_cast<void**>(&g_actor_recently_rendered)}, // card 169
    {&offsets::UObject_FindFunction, reinterpret_cast<void**>(&g_find_function)},
    {&offsets::AActor_AddComponentByClass, reinterpret_cast<void**>(&g_add_component_by_class)},
    {&offsets::AActor_FinishAddComponent, reinterpret_cast<void**>(&g_finish_add_component)},
    {&offsets::USceneComponent_SetHiddenInGame, reinterpret_cast<void**>(&g_set_hidden_in_game)},
    {&offsets::USceneComponent_SetVisibility, reinterpret_cast<void**>(&g_scene_set_visibility)},
    {&offsets::USplineComponent_GetSplineLength, reinterpret_cast<void**>(&g_spline_length)},
    {&offsets::FMemory_Malloc, reinterpret_cast<void**>(&g_memory_malloc)},
    {&offsets::FMemory_Realloc, reinterpret_cast<void**>(&g_memory_realloc)},
};

} // namespace

} // namespace ue::hooktable

namespace ue {

bool install(const hooktable::InstallSet& game, const uint8_t (&guid)[16], uint32_t age,
             const hooktable::InstallSteps& steps) {
    using namespace hooktable;
    uint8_t* base = nullptr;
    const IMAGE_NT_HEADERS64* nt = nullptr;
    if (!exe_image(base, nt)) {
        return false;
    }
    log::line("hooks: exe base %p, SizeOfImage 0x%lx, TimeDateStamp 0x%08lX", static_cast<void*>(base),
              nt->OptionalHeader.SizeOfImage, nt->FileHeader.TimeDateStamp);
    if (!image_matches_pdb(base, nt, guid, age)) {
        return false;
    }
    log::line("hooks: build identity OK (CodeView GUID/age match offsets.generated.h)");
    const InstallSet sets[] = {{kHooksUe, kCallsUe}, game};
    if (!resolve_all(base, nt, sets)) {
        return false;
    }
    g_exe_base = base;
    if (!steps.after_resolve(base, nt)) {
        return false;
    }
    const MH_STATUS st = MH_Initialize();
    if (st != MH_OK) {
        log::line("FATAL hooks: MH_Initialize = %s", MH_StatusToString(st));
        return false;
    }
    if (!steps.before_hooks(base, nt)) {
        return false;
    }
    return install_hooks(base, nt, sets);
}

} // namespace ue

