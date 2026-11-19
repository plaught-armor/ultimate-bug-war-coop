// Card 337: the client sees the new map before its title screen. After a host travel, the client's LoadMap returns
// and the world draws for the seconds until its own card 135 GameStarted pushes the loading screen.
//
// Fix (client only, play build and tests alike): world rendering off at the client's LoadMap return on a host travel
// to a gameplay map (UGameplayStatics::SetEnableWorldRendering, which sets UGameViewportClient::
// bDisableWorldRendering: the viewport draws only UI, black behind the widgets), and on again in one helper,
// c337_uncover(why):
// - the card 135 tick sees the client's own screen up (c135_screen_up: its remembered stack, element 0 the loading
//   screen, holds a widget): "title screen up";
// - every path that ends the wait without a screen: c320_client_done, a refused IntroStart, a network failure, the
//   next LoadMap entry, and a 15 s bound after the LoadMap return. These WARN.
// The flag lives on the viewport client, which outlives the map, so a missed uncover would leave the next map black:
// hence the LoadMap entry reset and the bound.
// Neither function has a native symbol; both are called through ProcessEvent on the UGameplayStatics default object,
// as card 135 calls GameStarted. The read before the write shows a value the engine set itself.
//
// Rows:
//   card337: client t=<ms> world rendering off at LoadMap return on <map> (was <0|1|?>)
//   card337: client t=<ms> world rendering on after <ms> ms (<why>)            WARN unless why is "title screen up"
//                                                                               or "screen confirmed" (card 396)

#include "ubw/hooks_internal.h"

#include <stdio.h>
#include <string.h>

namespace ubw::hooks {

StaticClassFn g_c337_statics_class = nullptr; // UGameplayStatics

namespace {

constexpr char kC337MenuMap[] = "3D_Frontend"; // the main menu and hangar: no title screen
// Card 346: seamless travel loads the engine's empty transition world first (no TransitionMap set: "Untitled"). It
// draws nothing and no title screen follows it, so a cover there only ends at the next LoadMap entry (a WARN).
constexpr char kC337TransitionMap[] = "Untitled";
constexpr ULONGLONG kC337BoundMs = 15000;
// Card 396: the cover stays this long after the screen is on its stack. Uncovering on the same tick showed the map
// before the screen had drawn (user report 2026-11-15: black, map, loading screen).
constexpr ULONGLONG kC396PaintMs = 500;
constexpr uint16_t kC337ParmsBytes = 16; // (const UObject* WorldContextObject, bool): 9 bytes, padded

struct C337State {
    void* engine; // UEngine, from the last LoadMap entry: the game world at the return and at the uncover
    bool covered;
    ULONGLONG off_ms;
    ULONGLONG up_ms; // card 396: the first tick the title screen was seen up under this cover (0: not yet)
    uint64_t set_name;
    uint64_t get_name;
};

C337State g_c337 = {};

// Card 397: the client's own copy of the loading screen under the cover, from the LoadMap return until the uncover.
// The game's screen needs the HUD, the pawn and the host's op (4 to 7 s black in c396b83); the copy needs none of
// them. It is not on a stack, takes no input and is removed by c337_uncover.
constexpr wchar_t kC397Class[] =
    L"/Game/Blueprints/UI_Aloft/Gameplay_HUD/WB_Loading_Screen_Final.WB_Loading_Screen_Final_C";
constexpr int32_t kC397ZOrder = 9000;         // above the HUD's stack
constexpr uint8_t kC397HitTestInvisible = 3;  // ESlateVisibility::HitTestInvisible
constexpr int32_t kC397MapChars = 64;

struct C397State {
    char map[kC397MapChars]; // the last TravelIds op: the map it names and its IDs
    int32_t chapter;
    int32_t level;
    bool have;
    bool tried;           // a create was logged under this cover (one line per cover)
    uint64_t widget_weak; // FWeakObjectPtr: the copy, while it is in the viewport
};

C397State g_c397 = {};

// Card 398: the chapter and level IDs for the copy on `map`. The host's TravelIds op when it names this map; on a
// direct join (no op before the LoadMap: the join from the main menu) the IDs the campaign maps carry in their
// names, FED_C<c>M<m> = chapter c-1, level m-1 (the host's screen-up rows on FED_C1M1 to FED_C5M2). The game's own
// screen gets the host's IDs from IntroStart later either way (c135 logs "chapter A->B": A is the value used here).
const char* c398_ids(const char* map, int32_t* chapter, int32_t* level) {
    if (g_c397.have && strcmp(map, g_c397.map) == 0) {
        *chapter = g_c397.chapter;
        *level = g_c397.level;
        return "TravelIds";
    }
    int c = 0;
    int m = 0;
    char tail = 0;
    if (sscanf_s(map, "FED_C%1dM%1d%c", &c, &m, &tail, 1U) == 2 && c >= 1 && m >= 1) {
        *chapter = c - 1;
        *level = m - 1;
        return "map name";
    }
    return nullptr;
}

// Client, cover on: the copy for `world` once a PC exists. One try that gets as far as the create per cover.
void c397_show(void* world) {
    if (g_c397.tried) {
        return;
    }
    const ObjectName map = object_name(world);
    int32_t chapter = -1;
    int32_t level = -1;
    const char* const source = c398_ids(map.text, &chapter, &level);
    if (source == nullptr) {
        return;
    }
    void* const pc = g_world_first_pc != nullptr ? g_world_first_pc(world) : nullptr;
    if (pc == nullptr || g_create_widget_instance == nullptr || g_add_to_viewport == nullptr ||
        g_widget_set_visibility == nullptr || g_weak_assign == nullptr) {
        return; // no PC yet: c337_step tries again
    }
    g_c397.tried = true;
    const bool ids = c397_set_ids(world, chapter, level);
    void* const cls = find_class(kC397Class);
    void* const widget = cls != nullptr ? g_create_widget_instance(pc, &cls, 0) : nullptr;
    if (widget != nullptr) {
        g_add_to_viewport(widget, kC397ZOrder);
        g_widget_set_visibility(widget, kC397HitTestInvisible);
        g_weak_assign(&g_c397.widget_weak, &widget);
    }
    const ULONGLONG now = GetTickCount64();
    log::line("%scard397: client t=%llu loading screen copy %s on %s, +%llu ms after the cover (chapter %d level %d "
              "%s, class %s)",
              widget != nullptr && ids ? "" : "WARN ", static_cast<unsigned long long>(now),
              widget != nullptr ? "up" : "not made", map.text, static_cast<unsigned long long>(now - g_c337.off_ms),
              chapter, level, ids ? "set" : "not set", cls != nullptr ? "found" : "not loaded");
    log::line("card398: client copy IDs from %s", source);
}

// Client: the copy leaves the viewport (every end of the cover, and LoadMap entry).
void c397_hide(const char* why) {
    void* const widget = g_c397.widget_weak != 0 && g_weak_get != nullptr ? g_weak_get(&g_c397.widget_weak) : nullptr;
    g_c397.widget_weak = 0;
    g_c397.tried = false;
    if (widget != nullptr && g_widget_remove_from_parent != nullptr) {
        g_widget_remove_from_parent(widget);
        log::line("card397: client t=%llu loading screen copy removed (%s)",
                  static_cast<unsigned long long>(GetTickCount64()), why);
    }
}

// The UFunction `text` on the UGameplayStatics default object with its bool's parms offset, or null when the
// function, its two parameters, or its layout is not the expected (UObject*, bool).
void* c337_fn(void*& cdo, uint64_t& cache, const wchar_t* text, int32_t& bool_at) {
    void* const cls = g_c337_statics_class != nullptr ? g_c337_statics_class() : nullptr;
    cdo = cls != nullptr ? read_ptr(cls, kClassDefaultObject) : nullptr;
    void* const fn = cdo != nullptr && g_find_function != nullptr && g_orig_process_event != nullptr &&
                             fname_cached(cache, text) != 0
                         ? g_find_function(cdo, cache)
                         : nullptr;
    uint16_t size = 0;
    if (fn != nullptr) {
        memcpy(&size, static_cast<const uint8_t*>(fn) + kFunctionParmsSize, sizeof(size));
    }
    const void* const first = fn != nullptr ? read_ptr(fn, kStructChildProperties) : nullptr;
    const void* const second = first != nullptr ? read_ptr(first, kFieldNext) : nullptr;
    bool_at = second != nullptr ? read_i32(second, kPropertyOffset) : -1;
    const bool ok = second != nullptr && size <= kC337ParmsBytes && read_i32(first, kPropertyOffset) == 0 &&
                    bool_at >= static_cast<int32_t>(sizeof(void*)) && bool_at < kC337ParmsBytes;
    return ok ? fn : nullptr;
}

// GetEnableWorldRendering(world): 1, 0, or -1 when it could not be called.
int32_t c337_get(void* world) {
    void* cdo = nullptr;
    int32_t at = -1;
    void* const fn = c337_fn(cdo, g_c337.get_name, L"GetEnableWorldRendering", at);
    if (fn == nullptr) {
        return -1;
    }
    alignas(16) uint8_t parms[kC337ParmsBytes] = {};
    memcpy(parms, &world, sizeof(world));
    g_orig_process_event(cdo, fn, parms);
    return parms[at] != 0 ? 1 : 0;
}

// SetEnableWorldRendering(world, on). False when it could not be called.
bool c337_set(void* world, bool on) {
    void* cdo = nullptr;
    int32_t at = -1;
    void* const fn = c337_fn(cdo, g_c337.set_name, L"SetEnableWorldRendering", at);
    if (fn == nullptr) {
        return false;
    }
    alignas(16) uint8_t parms[kC337ParmsBytes] = {};
    memcpy(parms, &world, sizeof(world));
    parms[at] = on ? 1 : 0;
    g_orig_process_event(cdo, fn, parms);
    return true;
}

void* c337_world() {
    return g_c337.engine != nullptr && g_get_game_world != nullptr ? g_get_game_world(g_c337.engine) : nullptr;
}

} // namespace

// Client: world rendering on again when this card switched it off. `why` names the path; WARN unless the screen is up.
void c337_uncover(const char* why) {
    if (!g_c337.covered) {
        return;
    }
    c397_hide(why); // card 397
    g_c337.covered = false;
    void* const world = c337_world();
    const bool ok = world != nullptr && c337_set(world, true);
    // Card 396: the screen closing inside kC396PaintMs (c320_client_done) is a normal end too.
    const bool screen = strcmp(why, "title screen up") == 0 || strcmp(why, "screen confirmed") == 0;
    const ULONGLONG now = GetTickCount64();
    log::line("%scard337: client t=%llu world rendering on after %llu ms (%s)%s", ok && screen ? "" : "WARN ",
              static_cast<unsigned long long>(now), static_cast<unsigned long long>(now - g_c337.off_ms), why,
              ok ? "" : ": SetEnableWorldRendering not called (no world or no function)");
}

// At UEngine::LoadMap entry, both sides: the engine for the return, and the old map's cover, if any, ends.
void c337_load_map_enter(void* engine) {
    c337_uncover("next LoadMap entry");
    c397_hide("LoadMap entry"); // card 397: a no-op after the uncover; the per-cover state starts clean
    g_c337.engine = engine;
}

// After UEngine::LoadMap returns (after client_diag_load_map_return set g_client_session). Client, host travel
// (a pending net game) to a gameplay map: world rendering off.
void c337_load_map_return(bool ok, const void* pending) {
    void* const world = ok && pending != nullptr && g_client_session ? c337_world() : nullptr;
    if (world == nullptr || strcmp(object_name(world).text, kC337MenuMap) == 0 ||
        strcmp(object_name(world).text, kC337TransitionMap) == 0) {
        return;
    }
    const int32_t was = c337_get(world);
    const bool set = c337_set(world, false);
    g_c337.covered = set;
    g_c337.off_ms = GetTickCount64();
    g_c337.up_ms = 0;
    log::line("%scard337: client t=%llu world rendering %s at LoadMap return on %s (was %d)", set ? "" : "WARN ",
              static_cast<unsigned long long>(g_c337.off_ms), set ? "off" : "not switched off (no function)",
              object_name(world).text, was);
    if (set) {
        c397_show(world); // card 397
    }
}

// Client: ChanOp::TravelIds ("<map> <chapter> <level>") from the host before a travel. Kept across the LoadMap.
void c397_receive(void* /*pc*/, const WireString* s) {
    char text[kC397MapChars + 32];
    const int32_t len = chan_text(s, text, sizeof(text));
    C397State next = {};
    const int got = len > 0 ? sscanf_s(text, "%63s %d %d", next.map, static_cast<unsigned>(sizeof(next.map)),
                                       &next.chapter, &next.level)
                            : 0;
    if (got != 3) {
        log::line("WARN card397: client TravelIds frame unreadable (%d chars, %d fields) -- ignored", len, got);
        return;
    }
    next.have = true;
    next.widget_weak = g_c397.widget_weak;
    next.tried = g_c397.tried;
    g_c397 = next;
    log::line("card397: client t=%llu TravelIds %s chapter %d level %d kept for the LoadMap return",
              static_cast<unsigned long long>(GetTickCount64()), next.map, next.chapter, next.level);
}

// Client tick (from cine_skip_step, after c135_step): the title screen up, or the bound.
void c337_step() {
    if (!g_c337.covered) {
        return;
    }
    void* const world = c337_world();
    if (world != nullptr) {
        c397_show(world); // card 397: until a PC exists
    }
    const ULONGLONG now = GetTickCount64();
    if (g_c337.up_ms == 0 && c135_screen_up()) {
        g_c337.up_ms = now;
    }
    if (g_c337.up_ms != 0) { // card 396: the screen has had kC396PaintMs to draw (or closed: c320_client_done)
        if (now - g_c337.up_ms >= kC396PaintMs) {
            c337_uncover("title screen up");
        }
        return;
    }
    if (now - g_c337.off_ms > kC337BoundMs) {
        c337_uncover("no title screen within 15 s of the LoadMap return");
    }
}

} // namespace ubw::hooks
