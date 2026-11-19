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

// CodeView debug record (RSDS) as stored in the image.
struct CvInfoPdb70 {
    DWORD signature; // 'RSDS'
    uint8_t guid[16];
    DWORD age;
    // char pdb_path[] follows
};
constexpr DWORD kRsds = 0x53445352; // "RSDS" little-endian
constexpr DWORD kMaxDebugEntries = 64;
constexpr size_t kMaxHooks = 512; // hook rows over both sets; ue::install's target array

size_t hook_count(std::span<const InstallSet> sets) {
    size_t n = 0;
    for (const InstallSet& s : sets) {
        n += s.hooks.size();
    }
    return n;
}

} // namespace

const uint8_t* g_exe_base = nullptr; // set once in ue::install before any hook is enabled
bool g_gate_diag = false;

const IMAGE_NT_HEADERS64* nt_headers(const uint8_t* base) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return nullptr;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return nullptr;
    }
    return nt;
}

bool image_matches_pdb(const uint8_t* base, const IMAGE_NT_HEADERS64* nt, const uint8_t (&guid)[16], uint32_t age) {
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    const DWORD count = dir.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
    if (dir.VirtualAddress == 0 || count == 0 || count > kMaxDebugEntries) {
        log::line("FATAL hooks: debug directory missing or implausible (rva=0x%lx size=0x%lx)",
                  dir.VirtualAddress, dir.Size);
        return false;
    }
    const auto* entries = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(base + dir.VirtualAddress);
    for (DWORD i = 0; i < count; ++i) {
        const IMAGE_DEBUG_DIRECTORY& e = entries[i];
        if (e.Type != IMAGE_DEBUG_TYPE_CODEVIEW || e.AddressOfRawData == 0 || e.SizeOfData < sizeof(CvInfoPdb70)) {
            continue;
        }
        const auto* cv = reinterpret_cast<const CvInfoPdb70*>(base + e.AddressOfRawData);
        if (cv->signature != kRsds) {
            continue;
        }
        const bool same = memcmp(cv->guid, guid, sizeof(cv->guid)) == 0 && cv->age == age;
        if (!same) {
            log::line("FATAL hooks: exe CodeView GUID/age differs from offsets.generated.h -- wrong game build");
        }
        return same;
    }
    log::line("FATAL hooks: no RSDS CodeView record in exe");
    return false;
}

void* code_address(uint8_t* base, const IMAGE_NT_HEADERS64* nt, const ubw::offsets::Symbol& sym) {
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const IMAGE_SECTION_HEADER& s = sec[i];
        if (sym.rva >= s.VirtualAddress && sym.rva < s.VirtualAddress + s.Misc.VirtualSize) {
            if ((s.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
                log::line("FATAL hooks: %s rva 0x%08X is in non-executable section", sym.name, sym.rva);
                return nullptr;
            }
            return base + sym.rva;
        }
    }
    log::line("FATAL hooks: %s rva 0x%08X is outside every section", sym.name, sym.rva);
    return nullptr;
}

bool create_and_enable(void* target, void* detour, void** original, const char* name) {
    MH_STATUS st = MH_CreateHook(target, detour, original);
    if (st != MH_OK) {
        log::line("ERROR hooks: MH_CreateHook(%s @ %p) = %s", name, target, MH_StatusToString(st));
        return false;
    }
    st = MH_EnableHook(target);
    if (st != MH_OK) {
        log::line("ERROR hooks: MH_EnableHook(%s @ %p) = %s", name, target, MH_StatusToString(st));
        return false;
    }
    log::line("hooks: %s hooked at %p", name, target);
    return true;
}

unsigned long long exe_rva(const void* p) {
    const IMAGE_NT_HEADERS64* const nt = nt_headers(g_exe_base);
    const auto* const q = static_cast<const uint8_t*>(p);
    if (nt == nullptr || q < g_exe_base || q >= g_exe_base + nt->OptionalHeader.SizeOfImage) {
        return 0;
    }
    return static_cast<unsigned long long>(q - g_exe_base);
}

bool hook_gated_off(const HookSpec& h) {
    return h.gate != nullptr && !*h.gate;
}

bool resolve_all(uint8_t* base, const IMAGE_NT_HEADERS64* nt, std::span<const InstallSet> sets,
                 std::span<void*> targets) {
    if (targets.size() != hook_count(sets)) {
        log::line("FATAL hooks: %zu targets for %zu hooks", targets.size(), hook_count(sets));
        return false;
    }
    size_t t = 0;
    for (const InstallSet& s : sets) {
        for (const HookSpec& h : s.hooks) {
            targets[t] = code_address(base, nt, *h.sym);
            if (targets[t] == nullptr) {
                return false;
            }
            ++t;
        }
    }
    for (const InstallSet& s : sets) {
        for (const CallSpec& c : s.calls) {
            void* const fn = code_address(base, nt, *c.sym);
            if (fn == nullptr) {
                return false;
            }
            *c.slot = fn;
        }
    }
    return true;
}

void log_skipped_hooks(std::span<const InstallSet> sets, uint32_t skipped) {
    const size_t total = hook_count(sets);
    log::line("hooks: %u of %zu hooks not installed (gate off), %zu installed", skipped, total, total - skipped);
    char text[768] = {};
    size_t used = 0;
    for (const InstallSet& s : sets) {
        for (const HookSpec& h : s.hooks) {
            if (!hook_gated_off(h)) {
                continue;
            }
            const char* const name = h.sym->name;
            if (used + strlen(name) + 3 > sizeof(text)) {
                log::line("hooks: skipped: %s", text);
                used = 0;
            }
            const int n = snprintf(text + used, sizeof(text) - used, "%s%s", used != 0 ? ", " : "", name);
            used += n > 0 ? static_cast<size_t>(n) : 0;
            used = used < sizeof(text) ? used : sizeof(text) - 1;
        }
    }
    if (used != 0) {
        log::line("hooks: skipped: %s", text);
    }
}

// b16: every call slot and every trampoline of an installed row must be set once install ends. One WARN row names
// each null one; no row when all are set. A trampoline of a gated-off row stays null by design, so code outside its
// detour never calls one: it calls the target through a call row (CallSpec) instead, which this pass covers.
void log_null_slots(std::span<const InstallSet> sets) {
    char text[768] = {};
    size_t used = 0;
    uint32_t nulls = 0;
    auto note = [&](const char* name) {
        ++nulls;
        const int n = snprintf(text + used, sizeof(text) - used, "%s%s", used != 0 ? ", " : "", name);
        used += n > 0 ? static_cast<size_t>(n) : 0;
        used = used < sizeof(text) ? used : sizeof(text) - 1;
    };
    for (const InstallSet& s : sets) {
        for (const CallSpec& c : s.calls) {
            if (*c.slot == nullptr) {
                note(c.sym->name);
            }
        }
        for (const HookSpec& h : s.hooks) {
            if (!hook_gated_off(h) && *h.original == nullptr) {
                note(h.sym->name);
            }
        }
    }
    if (nulls != 0) {
        log::line("WARN hooks: %u null pointers after install (call rows and installed trampolines): %s", nulls, text);
    }
}

bool install_hooks(std::span<const InstallSet> sets, std::span<void* const> targets) {
    if (targets.size() != hook_count(sets)) {
        log::line("FATAL hooks: %zu targets for %zu hooks", targets.size(), hook_count(sets));
        return false;
    }
    uint32_t skipped = 0;
    size_t t = 0;
    for (const InstallSet& s : sets) {
        for (const HookSpec& h : s.hooks) {
            const size_t i = t++;
            if (hook_gated_off(h)) {
                ++skipped;
                continue;
            }
            if (!create_and_enable(targets[i], h.detour, h.original, h.sym->name)) {
                return false;
            }
        }
    }
    log_skipped_hooks(sets, skipped);
    log_null_slots(sets);
    return true;
}

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

// Base and PE64 headers of the host exe, or false (logged FATAL).
bool exe_image(uint8_t*& base, const IMAGE_NT_HEADERS64*& nt) {
    base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    if (base == nullptr) {
        log::line("FATAL hooks: GetModuleHandleW(nullptr) failed, err=%lu", GetLastError());
        return false;
    }
    nt = nt_headers(base);
    if (nt == nullptr) {
        log::line("FATAL hooks: exe at %p has no valid PE64 headers", static_cast<void*>(base));
        return false;
    }
    return true;
}

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
    static void* targets[kMaxHooks] = {};
    const size_t n = hook_count(sets);
    if (n > std::size(targets)) {
        log::line("FATAL hooks: %zu hook rows, room for %zu", n, std::size(targets));
        return false;
    }
    if (!resolve_all(base, nt, sets, std::span{targets, n})) {
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
    return install_hooks(sets, std::span{targets, n});
}

} // namespace ue
