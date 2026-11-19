#include "ue/net.h"

#include "ue/log.h"
#include "ue/reflect.h"

namespace ue::net {
namespace {

EngineSteps g_steps{}; // written once by set_engine_steps before any hook is enabled
void* g_engine = nullptr; // card 346: the UGameEngine detour_tick last ran on (the seamless steps' engine)
bool g_seamless_changed = false; // card 346: a Cleanup fired inside the running FSeamlessTravelHandler::Tick

// FWorldContext::TravelURL (FString: data, num, max). UEngine::Browse empties it first (exe 0x1443D4887);
// UEngine::TickWorldTravel calls Browse while its num is above 1 (0x1443EFC86), so a Browse the mod does not run
// must empty it too, or the engine calls Browse again every tick with the same URL.
constexpr size_t kWorldContextTravelUrl = 0xA8;

// What the original does before any return that the mod's "not run" path skips: TravelURL emptied, the by-value URL
// destroyed (the original destroys its copy on every path, 0x1443D48C6).
void browse_not_run_cleanup(void* world_context, void* url_copy) {
    auto* const travel = static_cast<uint8_t*>(world_context) + kWorldContextTravelUrl;
    auto* const data = *reinterpret_cast<wchar_t**>(travel);
    if (data != nullptr) {
        data[0] = L'\0';
    }
    *reinterpret_cast<int32_t*>(travel + 8) = 0; // num 0: empty, the buffer stays with the FString
    g_furl_destroy(url_copy);
}

} // namespace

GameEngineInitFn g_orig_game_engine_init = nullptr;
GameEngineTickFn g_orig_tick = nullptr;
BrowseFn g_orig_browse = nullptr;
LoadMapFn g_orig_load_map = nullptr;
ListenFn g_orig_listen = nullptr;
ListenFn g_listen = nullptr;
ActorNetModeFn g_actor_net_mode = nullptr;
GetPlayerControllerFn g_get_player_controller = nullptr; // UGameplayStatics::GetPlayerController
GetGameWorldFn g_get_game_world = nullptr;
SetClientTravelFn g_set_client_travel = nullptr;
FurlDestroyFn g_furl_destroy = nullptr;
SeamlessCleanupFn g_orig_seamless_cleanup = nullptr;
SeamlessTickFn g_orig_seamless_tick = nullptr;
ServerTravelFn g_server_travel = nullptr;

const char* browse_result_name(int r) {
    static const char* const kNames[] = {"Success", "Failure", "Pending"};
    return (r >= 0 && r < 3) ? kNames[r] : "?";
}

void set_engine_steps(const EngineSteps& steps) {
    g_steps = steps;
}

unsigned net_mode(const void* actor) {
    return g_actor_net_mode(actor) & 0xFFu;
}

// UGameEngine::Init(IEngineLoop*): the first engine hook to fire.
void detour_game_engine_init(void* self, void* engine_loop) {
    log::line("UGameEngine::Init fired (this=%p, loop=%p)", self, engine_loop);
    g_orig_game_engine_init(self, engine_loop);
    log::line("UGameEngine::Init returned");
    if (g_steps.init_return != nullptr) {
        g_steps.init_return();
    }
}

// Hot path: every frame. The mod's per-frame work runs in the two steps.
void detour_tick(void* self, float delta_seconds, bool idle_mode) {
    g_engine = self;
    if (g_steps.pre_tick != nullptr) {
        g_steps.pre_tick(self); // before the engine Tick: scripted keys are processed in this frame's PC tick
    }
    g_orig_tick(self, delta_seconds, idle_mode);
    if (g_steps.post_tick != nullptr) {
        g_steps.post_tick(self);
    }
}

// FURL by value arrives as a pointer to the caller's copy; the original destroys it.
int detour_browse(void* self, void* world_context, void* url_copy, void* error) {
    log::line("UEngine::Browse enter (ctx=%p)", world_context);
    const int stepped = g_steps.browse_enter != nullptr ? g_steps.browse_enter(self, url_copy) : kBrowseRun;
    if (stepped != kBrowseRun) {
        log::line("UEngine::Browse not run by the mod: returned %d (%s)", stepped, browse_result_name(stepped));
        browse_not_run_cleanup(world_context, url_copy);
        return stepped;
    }
    const int r = g_orig_browse(self, world_context, url_copy, error);
    log::line("UEngine::Browse returned %d (%s)", r, browse_result_name(r));
    if (g_steps.browse_return != nullptr) {
        g_steps.browse_return(r);
    }
    ue::reflect::log_fstring("UEngine::Browse Error", error);
    return r;
}

bool detour_load_map(void* self, void* world_context, void* url_copy, void* pending, void* error) {
    if (g_steps.load_map_enter != nullptr) {
        g_steps.load_map_enter(self, pending); // before the enter line, as the step's own lines always were
    }
    log::line("UEngine::LoadMap enter (ctx=%p, pending=%p)", world_context, pending);
    const bool ok = g_orig_load_map(self, world_context, url_copy, pending, error);
    log::line("UEngine::LoadMap returned %s", ok ? "true" : "false");
    if (g_steps.load_map_return != nullptr) {
        g_steps.load_map_return(ok, pending);
    }
    ue::reflect::log_fstring("UEngine::LoadMap Error", error);
    return ok;
}

bool detour_listen(void* world, void* url) {
    const bool ok = g_orig_listen(world, url);
    log::line("UWorld::Listen(world=%p) returned %s", world, ok ? "true" : "false");
    if (g_steps.listen_return != nullptr) {
        g_steps.listen_return(ok);
    }
    return ok;
}

// Card 346: UNetDriver::CleanupWorldForSeamlessTravel, called once per seamless map change from
// FSeamlessTravelHandler::Tick (0x14435868C) while the old world is still the context's world.
void detour_seamless_cleanup(void* net_driver) {
    log::line("UNetDriver::CleanupWorldForSeamlessTravel enter (driver=%p)", net_driver);
    if (g_steps.seamless_enter != nullptr && g_engine != nullptr) {
        g_steps.seamless_enter(g_engine);
    }
    g_seamless_changed = true;
    g_orig_seamless_cleanup(net_driver);
}

// Card 346: the arrive step runs after the Tick that switched worlds returns: inside it the new world's GameMode is
// spawned and BeginPlay runs after PostSeamlessTravelGarbageCollect (0x144358D01 vs 0x144358D97).
void* detour_seamless_tick(void* handler) {
    void* const loaded = g_orig_seamless_tick(handler);
    if (!g_seamless_changed) {
        return loaded;
    }
    g_seamless_changed = false;
    log::line("FSeamlessTravelHandler::Tick returned after a map change (loaded world=%p)", loaded);
    if (g_steps.seamless_return != nullptr) {
        g_steps.seamless_return();
    }
    return loaded;
}

} // namespace ue::net
