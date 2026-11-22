// Card 578: ubwperf.dll entry. The performance cards (531, 536/537, 538/540, 539/542, 544, 548, 568, 576, 577) live
// in this dll; see perf/ubwperf_api.h for the two load paths. This file holds the dll's role pick, its hook and call
// tables, its own copies of the few co-op definitions the perf files read, and the co-op boundary (ubwperf_init).

#include "perf/ubwperf_api.h"
#include <atomic>
#include "ubw/hooks_internal.h"
#include "proxy.h"

// The engine call slots of co-op ue/net.cpp, ue/actor.cpp and ue/input.cpp that the perf files read; filled by
// kCallsPerf below (this dll links none of those files).
namespace ue::net {
GetPlayerControllerFn g_get_player_controller = nullptr;
GetGameWorldFn g_get_game_world = nullptr;
} // namespace ue::net
namespace ue::actor {
GetWorldFn g_actor_get_world = nullptr;
} // namespace ue::actor
namespace ue::input {
ConsoleCommandFn g_pc_console_command = nullptr;
} // namespace ue::input

namespace ubw::hooks {

std::atomic<uint32_t> g_perf_frame{1}; // card 594: perf_tick writes (game thread), any thread reads (perf_frame)

// --- Own copies of co-op definitions the perf files read (separate dll: no clash with dwmapi.dll's). ---
bool g_client_session = false; // co-op writes it through UbwPerfApi::client_session; standalone stays host
bool g_c132_fx_log = false;    // UBWCOOP_C132_FX: the card 548 rows
C461MoveFn g_orig_c461_move = nullptr; // co-op: its card 461 trampoline, copied in perf_install
StaticClassFn g_npc_class = nullptr;
StaticClassFn g_pawn_class = nullptr;
BodySimulatingFn g_body_simulating = nullptr;     // card 573
CollisionEnabledFn g_collision_enabled = nullptr; // card 573

const char* side() {
    return g_client_session ? "client" : "host";
}

namespace {
void* (*g_host_own_pc)(const void* world) = nullptr; // co-op own_pc; null standalone
} // namespace

void* own_pc(const void* world) {
    if (g_host_own_pc != nullptr) {
        return g_host_own_pc(world);
    }
    // Standalone: one local player, index 0 (no remote PCs exist in a single-player world).
    return world != nullptr && ue::net::g_get_player_controller != nullptr ? ue::net::g_get_player_controller(world, 0)
                                                                          : nullptr;
}

void* c31_weak_get(const C31Weak& w) {
    return g_weak_get != nullptr ? g_weak_get(&w) : nullptr; // unresolved: no object (callers treat it as gone)
}

void c31_weak_set(C31Weak& w, void* object) {
    if (object == nullptr || g_weak_assign == nullptr) {
        w = C31Weak{};
        return;
    }
    g_weak_assign(&w, &object);
}

bool c568_due_slot(int32_t slot, const void* world); // perf_sig_c568.cpp

namespace {

using ue::hooktable::CallSpec;
using ue::hooktable::HookSpec;
using ue::hooktable::InstallSet;

bool g_standalone = false; // no co-op dwmapi.dll in the process: the engine tick and significance rows are ours
ue::net::GameEngineTickFn g_orig_engine_tick = nullptr;
UpdateSignificanceFn g_orig_significance = nullptr;
void perf_tick(void* engine, unsigned long long now);

void detour_perf_engine_tick(void* self, float delta_seconds, bool idle_mode) {
    g_orig_engine_tick(self, delta_seconds, idle_mode);
    perf_tick(self, GetTickCount64());
}

// Standalone card 568: the host game mode path of co-op detour_c132_update_significance, without its co-op views.
void detour_perf_significance(void* game_mode) {
    void* const world = game_mode != nullptr && g_actor_get_world != nullptr ? g_actor_get_world(game_mode) : nullptr;
    if (world != nullptr && !c568_due_slot(0, world)) {
        return; // between 30 Hz runs the manager keeps its last values
    }
    g_orig_significance(game_mode);
}

const HookSpec kHooksPerf[] = {
    {&offsets::FRecastNavMeshGenerator_RebuildDirtyAreas, reinterpret_cast<void*>(&detour_c577_rebuild),
     reinterpret_cast<void**>(&g_orig_c577_rebuild), &g_c577_on},
    {&offsets::UNavigationSystemV1_GetNavDataForPropsConst, reinterpret_cast<void*>(&detour_c593_resolve),
     reinterpret_cast<void**>(&g_orig_c593_resolve), &g_c577_on}, // card 593 step 4: nav data resolves (host diag)
    {&offsets::ASTNonPlayableCharacterBase_PossessedBy, reinterpret_cast<void*>(&detour_c593_possess),
     reinterpret_cast<void**>(&g_orig_c593_possess), &g_c577_on}, // card 593 step 4: AI class nav row (host diag)
    {&offsets::USTSaveComponent_GetSaveId, reinterpret_cast<void*>(&detour_c531_save_id),
     reinterpret_cast<void**>(&g_orig_c531_save_id), &g_c531_on}, // card 531: save component cache (perf)
    {&offsets::AActor_AddOwnedComponent, reinterpret_cast<void*>(&detour_c537_add),
     reinterpret_cast<void**>(&g_orig_c537_add), &g_c536_on},
    {&offsets::AActor_RemoveOwnedComponent, reinterpret_cast<void*>(&detour_c537_remove),
     reinterpret_cast<void**>(&g_orig_c537_remove), &g_c536_on},
    {&offsets::AActor_ResetOwnedComponents, reinterpret_cast<void*>(&detour_c537_reset),
     reinterpret_cast<void**>(&g_orig_c537_reset), &g_c536_on},
    {&offsets::AActor_FindComponentByClass, reinterpret_cast<void*>(&detour_c536_find),
     reinterpret_cast<void**>(&g_orig_c536_find), &g_c536_on}, // card 536: component lookup cache (perf)
    {&offsets::USTAttackComponent_DoesTargetMeetConditions, reinterpret_cast<void*>(&detour_c538_conditions),
     reinterpret_cast<void**>(&g_orig_c538_conditions), &g_c538_on}, // card 538: targeting repeat counts (perf diag)
    {&offsets::UADTargetComponent_GatherTargetsInRange, reinterpret_cast<void*>(&detour_c594_gather),
     reinterpret_cast<void**>(&g_orig_c594_gather), &g_c594_on}, // card 594: host target gather counts (diag)
    {&offsets::UADTargetComponent_UpdateTargetData, reinterpret_cast<void*>(&detour_c594_update),
     reinterpret_cast<void**>(&g_orig_c594_update), &g_c594_on}, // card 594 diag
    {&offsets::UADTargetComponent_FindOrAddActor, reinterpret_cast<void*>(&detour_c594_find_or_add),
     reinterpret_cast<void**>(&g_orig_c594_find_or_add), &g_c594_on}, // card 594 diag
    {&offsets::UADGASSubsystem_GetAllAbilitySystemActors, reinterpret_cast<void*>(&detour_c594_gas_actors),
     reinterpret_cast<void**>(&g_orig_c594_gas_actors), &g_c594_on}, // card 594 diag
    {&offsets::UPrimitiveComponent_UpdateOverlapsImpl, reinterpret_cast<void*>(&detour_c548_prim),
     reinterpret_cast<void**>(&g_orig_c548_impl[0]), &g_c132_fx_log}, // card 548 diag
    {&offsets::USkinnedMeshComponent_UpdateOverlapsImpl, reinterpret_cast<void*>(&detour_c548_skinned),
     reinterpret_cast<void**>(&g_orig_c548_impl[1]), &g_c132_fx_log}, // card 548 diag
    {&offsets::USkeletalMeshComponent_UpdateOverlapsImpl, reinterpret_cast<void*>(&detour_c548_skeletal),
     reinterpret_cast<void**>(&g_orig_c548_impl[2]), &g_c132_fx_log}, // card 548 diag
    {&offsets::USkeletalMeshComponent_UpdateKinematicBonesToAnim, reinterpret_cast<void*>(&detour_c573_bones),
     reinterpret_cast<void**>(&g_orig_c573_bones), &g_c573_on}, // card 573: kinematic bone update counts
    {&offsets::FNiagaraSystemInstance_TickInstanceParameters_GameThread, reinterpret_cast<void*>(&detour_c573_niagara),
     reinterpret_cast<void**>(&g_orig_c573_niagara), &g_c573_on}, // card 573 rank 2: Niagara parameter tick counts
    {&offsets::UGameEngine_Tick, reinterpret_cast<void*>(&detour_perf_engine_tick),
     reinterpret_cast<void**>(&g_orig_engine_tick), &g_standalone}, // co-op: its c42_step calls tick
    {&offsets::ASTGameplayGameMode_UpdateSignificanceManager, reinterpret_cast<void*>(&detour_perf_significance),
     reinterpret_cast<void**>(&g_orig_significance), &g_standalone}, // co-op: its detours call c568_due
};

const CallSpec kCallsPerf[] = {
    {&offsets::FObjectPtr_IsA, reinterpret_cast<void**>(&g_object_ptr_is_a)},
    {&offsets::FWeakObjectPtr_Get, reinterpret_cast<void**>(&g_weak_get)},
    {&offsets::FWeakObjectPtr_Assign, reinterpret_cast<void**>(&g_weak_assign)},
    {&offsets::FName_GetDisplayNameEntry, reinterpret_cast<void**>(&g_name_entry)},
    {&offsets::FMemory_Free, reinterpret_cast<void**>(&g_memory_free)},
    {&offsets::UGameEngine_GetGameWorld, reinterpret_cast<void**>(&ue::net::g_get_game_world)},
    {&offsets::UGameplayStatics_GetPlayerController, reinterpret_cast<void**>(&ue::net::g_get_player_controller)},
    {&offsets::APlayerController_ConsoleCommand, reinterpret_cast<void**>(&ue::input::g_pc_console_command)},
    {&offsets::AActor_GetWorld, reinterpret_cast<void**>(&g_actor_get_world)},
    {&offsets::USTSaveComponent_StaticClass, reinterpret_cast<void**>(&g_c531_static_class)}, // card 531
    {&offsets::ASTNonPlayableCharacterBase_StaticClass, reinterpret_cast<void**>(&g_npc_class)},
    {&offsets::APawn_StaticClass, reinterpret_cast<void**>(&g_pawn_class)},
    {&offsets::FBodyInstanceCore_ShouldInstanceSimulatingPhysics, reinterpret_cast<void**>(&g_body_simulating)},
    {&offsets::UPrimitiveComponent_GetCollisionEnabled, reinterpret_cast<void**>(&g_collision_enabled)},
};

// The switches co-op flags.cpp read before card 578; same names and texts.
void read_perf_flags() {
    using ue::config::read_switch;
    g_c132_fx_log = read_switch(L"UBWCOOP_C132_FX", "card 132 effect audit");
    g_c531_on = !read_switch(L"UBWCOOP_C531_OFF", "card 531 GetSaveId cache off (A/B)");
    g_c536_on = !read_switch(L"UBWCOOP_C536_OFF", "card 536 FindComponentByClass cache off (A/B)");
    g_c577_on = !read_switch(L"UBWCOOP_C577_OFF", "card 577 client navmesh rebuild skip off");
    g_c593_hold_on = !read_switch(L"UBWCOOP_C593_HOLD_OFF", "card 593 held navmesh rebuilds off (A/B)");
    g_c538_on = !read_switch(L"UBWCOOP_C538_OFF", "card 538 DoesTargetMeetConditions counts off");
    g_c594_on = !read_switch(L"UBWCOOP_C594_OFF", "card 594 host target gather counts off");
    g_c540_on = !read_switch(L"UBWCOOP_C540_OFF", "card 540 DoesTargetMeetConditions memo off (A/B)");
    g_c539_on = !read_switch(L"UBWCOOP_C539_OFF", "card 539 client NPC proxy sweep counts off");
    g_c544_on = !read_switch(L"UBWCOOP_C544_OFF", "card 544 Chaos dynamic tree padding off (A/B)");
    g_c568_on = !read_switch(L"UBWCOOP_C568_OFF", "card 568 significance 30 Hz throttle off (A/B)");
    g_c542_on = !read_switch(L"UBWCOOP_C542_OFF", "card 542 client NPC proxy capsules ignore PhysicsBody off (A/B)");
    g_c573_on = !read_switch(L"UBWCOOP_C573_OFF", "card 573 UpdateKinematicBonesToAnim counts off");
}

// Card 571 rows for this dll's own hooks, on a 10 s window (no row while UBWCOOP_C571 is off).
constexpr unsigned long long kPerfCountWindowMs = 10000;
unsigned long long g_count_window_start = 0;

void perf_tick(void* engine, unsigned long long now) {
    g_perf_frame.fetch_add(1, std::memory_order_relaxed);
    c536_tick(now);    // card 536: FindComponentByClass cache totals
    c538_tick(now);    // card 538: DoesTargetMeetConditions repeat counts
    c594_tick();       // card 594: host target gather counts (diag)
    c539_tick(now);    // card 539: client NPC proxy sweep counts
    c548_tick(now);    // card 548: player primitive UpdateOverlapsImpl rows (diag)
    c573_tick(now);    // card 573: UpdateKinematicBonesToAnim counts
    c573_niagara_tick(now); // card 573 rank 2: Niagara TickInstanceParameters_GameThread counts
    c544_tick(engine); // card 544: dynamic tree padding, once
    if (g_count_window_start == 0) {
        g_count_window_start = now;
    } else if (now - g_count_window_start >= kPerfCountWindowMs) {
        ue::hooktable::log_hook_counts(side(), now, now - g_count_window_start);
        g_count_window_start = now;
    }
}

bool g_installed = false;

// Resolves both tables, applies the card 576 byte patch, then hooks with this dll's own MinHook. Once.
bool perf_install_rows() {
    using namespace ue::hooktable;
    uint8_t* base = nullptr;
    const IMAGE_NT_HEADERS64* nt = nullptr;
    if (!exe_image(base, nt) || !image_matches_pdb(base, nt, offsets::kPdbGuid, offsets::kPdbAge)) {
        return false;
    }
    const InstallSet sets[] = {{kHooksPerf, kCallsPerf}};
    if (!resolve_all(base, nt, sets)) {
        return false;
    }
    g_exe_base = base;
    c576_patch_worker_spin(base); // card 576: workers read the new count on their next idle entry
    const MH_STATUS st = MH_Initialize();
    if (st != MH_OK) {
        log::line("FATAL ubwperf: MH_Initialize = %s", MH_StatusToString(st));
        return false;
    }
    return install_hooks(base, nt, sets);
}

bool perf_install() {
    if (g_installed) {
        log::line("WARN ubwperf: install called twice; ignored");
        return false;
    }
    g_installed = true;
    const bool ok = perf_install_rows();
    log::line(ok ? "ubwperf: hooks installed (%s)" : "ERROR ubwperf: hooks not (fully) installed (%s) -- see above",
              g_standalone ? "standalone" : "co-op");
    return ok;
}

bool perf_c539_move(void* comp, const void* delta, const void* rotation, bool sweep, void* hit, int32_t flags,
                    uint8_t teleport) {
    return c539_move(comp, delta, rotation, sweep, hit, flags, teleport);
}

bool perf_c568_due(int32_t slot, const void* world) {
    return c568_due_slot(slot, world);
}

void* const* g_host_c461_slot = nullptr; // co-op: its card 461 trampoline slot, read once its hooks are in

// Co-op install(): after the host's own install, so its card 461 trampoline is set.
bool perf_install_coop() {
    g_orig_c461_move = reinterpret_cast<C461MoveFn>(*g_host_c461_slot);
    return perf_install();
}

UbwPerfApi g_api{kUbwPerfVersion, &g_client_session, false, &perf_install_coop, &perf_tick, &perf_c539_move,
                 &perf_c568_due};

} // namespace

uint32_t perf_frame() {
    return g_perf_frame.load(std::memory_order_relaxed);
}

const UbwPerfApi* perf_init(const UbwPerfHost* host) {
    if (host == nullptr || host->version != kUbwPerfVersion || host->log_write == nullptr ||
        host->own_pc == nullptr || host->orig_c461_move == nullptr) {
        return nullptr; // no log sink to say so in; the host logs the null
    }
    ue::log::set_sink(host->log_write);
    log::line("ubwperf: co-op init, api version %u", kUbwPerfVersion);
    g_host_own_pc = host->own_pc;
    g_host_c461_slot = host->orig_c461_move;
    read_perf_flags();
    g_api.wants_c461_move = g_c539_on || g_c542_on; // cards 539 and 542 use the co-op card 461 MoveComponentImpl row
    return &g_api;
}

namespace {

// A co-op dwmapi.dll in the process exports this marker; with one, it drives this dll through ubwperf_init.
bool coop_present() {
    const HMODULE coop = GetModuleHandleW(L"dwmapi.dll");
    return coop != nullptr && GetProcAddress(coop, "ubwcoop_marker") != nullptr;
}

DWORD WINAPI standalone_worker(LPVOID) {
    if (coop_present()) {
        log::line("ubwperf: co-op dwmapi.dll present; it drives this dll (standalone install skipped)");
        return 0;
    }
    g_standalone = true;
    read_perf_flags();
    return perf_install() ? 0 : 1;
}

// True when this module's file name is winhttp.dll (the single-player proxy name).
bool loaded_as_winhttp(HMODULE self) {
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return false;
    }
    const wchar_t* name = path;
    for (DWORD i = 0; i < n; ++i) {
        if (path[i] == L'\\' || path[i] == L'/') {
            name = path + i + 1;
        }
    }
    return _wcsicmp(name, L"winhttp.dll") == 0;
}

BOOL attach_as_winhttp(HMODULE self) {
    const ue::config::LogTag tag = ue::config::read_log_tag();
    if (!ue::log::open_next_to_exe(tag.status == ue::config::TagStatus::Ok ? tag.text : nullptr, L"ubwperf")) {
        OutputDebugStringA("ubwperf: cannot open the log file next to the exe\n");
    }
    log::line("ubwperf attach: winhttp proxy module %p", static_cast<void*>(self));
    if (!ubw::proxy::load_real_dll(self)) {
        return FALSE; // without the real winhttp the game cannot work; refuse to load so the failure is obvious
    }
    const HANDLE t = CreateThread(nullptr, 0, &standalone_worker, nullptr, 0, nullptr);
    if (t == nullptr) {
        log::line("ERROR ubwperf attach: CreateThread failed, err=%lu", GetLastError());
        return TRUE; // the proxy still works; only the perf cuts are missing
    }
    CloseHandle(t);
    return TRUE;
}

} // namespace

} // namespace ubw::hooks

extern "C" __declspec(dllexport) const UbwPerfApi* ubwperf_init(const UbwPerfHost* host) {
    return ubw::hooks::perf_init(host);
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }
    DisableThreadLibraryCalls(self);
    // As ubwperf.dll the co-op dll loads it and calls ubwperf_init; nothing runs here.
    return ubw::hooks::loaded_as_winhttp(self) ? ubw::hooks::attach_as_winhttp(self) : TRUE;
}
