// Restart: co-op restart after game over (card 045).

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

FurlCtorFn g_furl_ctor = nullptr;
FurlDtorFn g_furl_dtor = nullptr;
WidgetSetIsEnabledFn g_widget_set_is_enabled = nullptr;

constexpr size_t kFurlCheckBytes = 0x100; // FURL is 0x68; the check's buffer has room to spare
constexpr const wchar_t* kFurlCheckText = L"/Game/Maps/FED/FED_C1M1?listen";
constexpr const wchar_t* kFurlCheckMap = L"/Game/Maps/FED/FED_C1M1";

constexpr ULONGLONG kC45RetryMs = 3000;
constexpr ULONGLONG kC45CapMs = 60000;      // the client follow gives up this long after the title load

C45State g_c45{};

bool fs_equal(const void* fs, const wchar_t* text) {
    const size_t n = wcslen(text);
    return static_cast<size_t>(fs_len(fs)) == n && _wcsnicmp(fs_text(fs), text, n) == 0;
}

// Once: FURL(nullptr, kFurlCheckText, TRAVEL_Absolute) must put the map, no host and one `listen` option at the
// offsets above, or no URL is logged or changed.
void c45_check_layout() {
    alignas(16) uint8_t u[kFurlCheckBytes] = {};
    g_furl_ctor(u, nullptr, kFurlCheckText, kTravelAbsolute);
    const auto* const op = reinterpret_cast<const FArrayRaw*>(u + kFurlOp);
    const bool ok = fs_equal(u + kFurlMap, kFurlCheckMap) && fs_len(u + kFurlHost) == 0 && op->num == 1 &&
                    op->max >= op->num && fs_equal(&op->data[0], L"listen") && read_i32(u, kFurlValid) == 1;
    g_c45.layout = ok ? 1 : -1;
    log::line("%scard045: %s FURL layout check on \"%ls\": %s (map \"%.*ls\", host length %d, port %d, valid %d, "
              "options %d/%d, protocol \"%.*ls\")",
              ok ? "" : "WARN ", side(), kFurlCheckText, ok ? "verified" : "FAILED, URLs are not changed",
              fs_len(u + kFurlMap), fs_text(u + kFurlMap), fs_len(u + kFurlHost), read_i32(u, kFurlPort),
              read_i32(u, kFurlValid), op->num, op->max, fs_len(u + kFurlProtocol), fs_text(u + kFurlProtocol));
    g_furl_dtor(u);
}

// Host: a listen server Browsing to a local map without `listen` keeps listening (card 045 restart; card 154 every
// trip: the mission end and the pause-menu quit to the hangar, and the hangar's next mission, whatever the player
// count, since a client that dropped at the last travel is still following and is not counted yet).
void c45_host_browse(void* engine, uint8_t* url) {
    void* const world = g_get_game_world(engine);
    // Card 349: any PC in the world will do (net mode only); not the local PC.
    void* const pc = world != nullptr ? g_world_first_pc(world) : nullptr;
    if (pc == nullptr || net_mode(pc) != kNetModeListenServer) {
        return; // standalone (single player) or no world: the URL is never changed
    }
    const uint32_t players = c43_roster(world).players;
    const bool local = fs_len(url + kFurlHost) == 0;
    const bool listen = c45_has_option(url, L"listen");
    const bool quit = c45_has_option(url, L"FromLevelQuit");
    if (!local || listen) {
        log::line("card045: host Browse URL unchanged: net mode 2, players %u, local map %d, listen option %d, "
                  "FromLevelQuit %d", players, local ? 1 : 0, listen ? 1 : 0, quit ? 1 : 0);
        return;
    }
    const bool added = c45_add_option(url, L"listen");
    log::line("%scard154: host t=%llu Browse to \"%.*ls\" with %u players (FromLevelQuit %d): listen %s the Browse "
              "URL (in place, engine allocator)", added ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()),
              fs_len(url + kFurlMap), fs_text(url + kFurlMap), players, quit ? 1 : 0,
              added ? "added to" : "NOT added to");
    c45_log_url("Browse URL as passed on", url);
}

// Client: a local Browse while in a session is the drop; later local fallbacks (closed/failed, or the same map) do
// not stop the follow, any other Browse does.
void c45_client_browse(const uint8_t* url) {
    const bool local = fs_len(url + kFurlHost) == 0;
    if (g_c45.this_ours) {
        log::line("card045: client try %u Browse (host \"%.*ls\")", g_c45.tries, fs_len(url + kFurlHost),
                  fs_text(url + kFurlHost));
        return;
    }
    if (g_c45.follow == C45Follow::Off) {
        if (!g_client_session || !local) {
            return;
        }
        if (c45_has_option(url, L"FromLevelQuit")) { // the player chose QUIT in the pause menu: no reconnect
            log::line("card045: client t=%llu left the session by the pause menu QUIT (FromLevelQuit): no reconnect",
                      static_cast<unsigned long long>(GetTickCount64()));
            return;
        }
        // Card 373: the death screen's Exit (OpenMenuLevel) Browses the frontend with FromGame. That is the player's
        // own choice to leave, like QUIT. The host-driven return to the frontend is a ClientTravel, not a Browse.
        if (c45_has_option(url, L"FromGame")) {
            log::line("card373: client t=%llu left the session by the death screen Exit (FromGame): no reconnect",
                      static_cast<unsigned long long>(GetTickCount64()));
            return;
        }
        if (!g_travel.enabled || g_travel.url[0] == L'\0') {
            log::line("WARN card045: client session dropped but no UBWCOOP_TRAVEL address to reconnect to");
            return;
        }
        g_c45.follow = C45Follow::Armed;
        g_c45.tries = 0;
        g_c45.drop_ms = GetTickCount64();
        swprintf(g_c45.landing, kC45MapChars, L"%.*ls", fs_len(url + kFurlMap), fs_text(url + kFurlMap));
        log::line("card045: client t=%llu session dropped (Browse to local map \"%ls\"): reconnect to \"%ls\" once "
                  "it loads", static_cast<unsigned long long>(g_c45.drop_ms), g_c45.landing, g_travel.url);
        return;
    }
    const bool fallback = local && (c45_has_option(url, L"closed") || c45_has_option(url, L"failed") ||
                                    fs_equal(url + kFurlMap, g_c45.landing));
    if (fallback) {
        log::line("card045: client follow continues: local fallback Browse to \"%.*ls\"", fs_len(url + kFurlMap),
                  fs_text(url + kFurlMap));
        return;
    }
    log::line("card045: client follow stopped after %u tries: another Browse started (host \"%.*ls\", map \"%.*ls\")",
              g_c45.tries, fs_len(url + kFurlHost), fs_text(url + kFurlHost), fs_len(url + kFurlMap),
              fs_text(url + kFurlMap));
    g_c45.follow = C45Follow::Off;
}

void c45_load_map_return(bool ok, const void* pending) {
    const ULONGLONG now = GetTickCount64();
    if (g_c45.follow == C45Follow::Armed && ok && pending == nullptr) {
        g_c45.follow = C45Follow::Retrying;
        g_c45.landed_ms = now;
        g_c45.next_ms = now + kC45RetryMs;
        log::line("card045: client t=%llu local map loaded %llu ms after the drop: first try in %llu ms",
                  static_cast<unsigned long long>(now), static_cast<unsigned long long>(now - g_c45.drop_ms),
                  static_cast<unsigned long long>(kC45RetryMs));
        return;
    }
    if (g_c45.follow != C45Follow::Pending || pending == nullptr) {
        return;
    }
    if (!ok) {
        log::line("WARN card045: client try %u: pending LoadMap returned false", g_c45.tries);
        g_c45.follow = C45Follow::Retrying;
        g_c45.next_ms = now + kC45RetryMs;
        return;
    }
    g_c45.follow = C45Follow::Off;
    ++g_c45.reconnects;
    pl_count(PlCount::Rejoin); // card 155
    log::line("card045: client t=%llu reconnected (%u): try %u, %llu ms after the drop", static_cast<unsigned long long>(now),
              g_c45.reconnects, g_c45.tries, static_cast<unsigned long long>(now - g_c45.drop_ms));
    if (g_c45_test && g_c43_test) {
        c43_shot(now + kC45ShotMs, "after the reconnect (client)", kC44ShotUi);
        g_c45.done_ms = g_c45.reconnects >= 2 ? now + kC45ClientDoneMs : 0;
    }
}

// Client follow tick: a SetClientTravel try every kC45RetryMs, for kC45CapMs.
// A Pending try is never re-issued while its pending net game lives: SetClientTravel would close that connection
// (card 130 item 15: try 2 closed try 1's connection, reason 14, at +15 s on the FED_C2M1 follow). It ends through
// c45_load_map_return or c45_pending_failed; kC45PendingMs is only the backstop past the engine's connect timeout.
// The cap is checked only between tries, so it never cuts off an attempt in flight.
void c45_follow_step(void* engine, ULONGLONG now) {
    if (now < g_c45.next_ms) {
        return;
    }
    if (g_c45.follow == C45Follow::Pending) {
        log::line("WARN card045: client try %u still Pending after %llu ms: no LoadMap return and no failure; retrying",
                  g_c45.tries, static_cast<unsigned long long>(kC45PendingMs));
    } else if (now - g_c45.landed_ms >= kC45CapMs) {
        log::line("WARN card045: client follow gave up after %u tries, %llu ms after the local map loaded", g_c45.tries,
                  static_cast<unsigned long long>(now - g_c45.landed_ms));
        g_c45.follow = C45Follow::Off;
        return;
    }
    void* const world = g_get_game_world(engine);
    if (world == nullptr) {
        return;
    }
    ++g_c45.tries;
    log::line("card045: client t=%llu try %u at +%llu ms: SetClientTravel(\"%ls\", TRAVEL_Absolute)",
              static_cast<unsigned long long>(now), g_c45.tries, static_cast<unsigned long long>(now - g_c45.landed_ms),
              g_travel.url);
    g_c45.follow = C45Follow::Retrying;
    g_c45.next_ms = now + kC45RetryMs;
    g_c45.ours = true;
    g_set_client_travel(engine, world, g_travel.url, kTravelAbsolute);
}

void c45_step(void* engine) {
    if (g_c45.follow == C45Follow::Off && g_c45.done_ms == 0) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_c45.done_ms != 0 && now >= g_c45.done_ms) {
        g_c45.done_ms = 0;
        c43_totals("test totals");
        log::line("card045: %s test done (restarts %u, reconnects %u)", side(), g_c45_phase, g_c45.reconnects);
    }
    if (g_c45.follow == C45Follow::Retrying || g_c45.follow == C45Follow::Pending) {
        c45_follow_step(engine, now);
    }
}

// bIsEnabled by reflection: 1, 0, or -1 when the property is not found.
int c45_is_enabled(const void* widget) {
    const uint8_t* const f = prop_find_prop(read_ptr(widget, kObjectClass), "bIsEnabled");
    if (f == nullptr || !prop_bool_ok(f)) {
        return -1;
    }
    const auto* const addr = static_cast<const uint8_t*>(widget) + read_i32(f, kPropertyOffset);
    return (addr[f[kBoolByteOffset]] & f[kBoolFieldMask]) != 0 ? 1 : 0;
}

// Client respawn in place: the game over and the checkpoint load each construct a death screen on the client,
// and nothing removes them. Every live one goes; input back to GameOnly.
void c45_remove_screens(void* pc, ULONGLONG now) {
    int32_t removed = 0;
    for (int32_t i = 0; i < g_c45.screen_count; ++i) {
        void* const screen = g_weak_get(&g_c45.screens[i]);
        if (screen != nullptr) {
            g_widget_remove_from_parent(screen);
            ++removed;
        }
    }
    client_force_game_only(pc);
    log::line("card045: client t=%llu respawn in place: %d of %d death screens removed, input GameOnly",
              static_cast<unsigned long long>(now), removed, g_c45.screen_count);
    g_c45.screen_count = 0;
}

// UE's FURL::HasOption: an option equal to `key` or starting with "key=" (case-insensitive).
bool c45_has_option(const uint8_t* url, const wchar_t* key) {
    const auto* const op = reinterpret_cast<const FArrayRaw*>(url + kFurlOp);
    const size_t n = wcslen(key);
    for (int32_t i = 0; op->data != nullptr && i < op->num && i < kC45MaxOps; ++i) {
        const int len = fs_len(&op->data[i]);
        const wchar_t* const t = fs_text(&op->data[i]);
        if (static_cast<size_t>(len) >= n && _wcsnicmp(t, key, n) == 0 && (static_cast<size_t>(len) == n || t[n] == L'=')) {
            return true;
        }
    }
    return false;
}

void c45_log_url(const char* what, const uint8_t* url) {
    if (!note_event_cap(g_c45.url_lines, kC45UrlLines, "card045 URL")) {
        return;
    }
    const auto* const op = reinterpret_cast<const FArrayRaw*>(url + kFurlOp);
    log::line("card045: %s t=%llu %s: protocol \"%.*ls\", host \"%.*ls\", port %d, valid %d, map \"%.*ls\", "
              "%d options, portal \"%.*ls\"",
              side(), static_cast<unsigned long long>(GetTickCount64()), what, fs_len(url + kFurlProtocol),
              fs_text(url + kFurlProtocol), fs_len(url + kFurlHost), fs_text(url + kFurlHost), read_i32(url, kFurlPort),
              read_i32(url, kFurlValid), fs_len(url + kFurlMap), fs_text(url + kFurlMap), op->num,
              fs_len(url + kFurlPortal), fs_text(url + kFurlPortal));
    for (int32_t i = 0; op->data != nullptr && i < op->num && i < kC45MaxOps; ++i) {
        log::line("card045: %s   option %d: \"%.*ls\"", side(), i, fs_len(&op->data[i]), fs_text(&op->data[i]));
    }
}

// Appends `text` to the URL copy's options with the engine allocator; Browse's own FURL destructor frees it.
bool c45_add_option(uint8_t* url, const wchar_t* text) {
    auto* const op = reinterpret_cast<FArrayRaw*>(url + kFurlOp);
    const size_t chars = wcslen(text) + 1;
    if (op->num < 0 || op->num >= kC45MaxOps || op->max < op->num) {
        log::line("WARN card045: host URL options not grown: num %d, max %d", op->num, op->max);
        return false;
    }
    if (op->num == op->max) {
        void* const grown = g_memory_realloc(op->data, static_cast<size_t>(op->num + 1) * sizeof(FStringRaw), 0);
        if (grown == nullptr) {
            log::line("WARN card045: host FMemory::Realloc of the URL options failed");
            return false;
        }
        op->data = static_cast<FStringRaw*>(grown);
        op->max = op->num + 1;
    }
    auto* const buf = static_cast<wchar_t*>(g_memory_malloc(chars * sizeof(wchar_t), 0));
    if (buf == nullptr) {
        log::line("WARN card045: host FMemory::Malloc of the option failed");
        return false;
    }
    memcpy(buf, text, chars * sizeof(wchar_t));
    op->data[op->num++] = FStringRaw{buf, static_cast<int32_t>(chars), static_cast<int32_t>(chars)};
    return true;
}

// Card 154: a client in the host's session, or following it back, never starts a mission of its own: a local Browse
// to a map other than the menu, not a drop fallback (closed/failed) and not a follow try, is refused. The host's
// travels reach the client as the drop and the follow; the client's own menus must not take it elsewhere.
bool c154_client_refuses(const uint8_t* url) {
    if (g_c45.this_ours || (!g_client_session && g_c45.follow == C45Follow::Off) || fs_len(url + kFurlHost) != 0 ||
        fs_len(url + kFurlMap) == 0 || wcsstr(fs_text(url + kFurlMap), kC52MenuMapW) != nullptr ||
        c45_has_option(url, L"closed") || c45_has_option(url, L"failed")) {
        return false;
    }
    log::line("WARN card154: client t=%llu own Browse to \"%.*ls\" refused: %s, the host chooses the map",
              static_cast<unsigned long long>(GetTickCount64()), fs_len(url + kFurlMap), fs_text(url + kFurlMap),
              g_client_session ? "in the host's session" : "following the host back");
    return true;
}

// Card 381: every menu route to a level of the client's own choice (hangar NEW GAME, mission select, CONTINUE, a
// restart) ends in USTGameInstance::TransitionToLevel or TransitionToLatestLevel, which start the game's loading
// screen (ShowAsyncLoadingScreen) and then OpenLevel. Card 154 refuses that Browse, and nothing ends the loading
// screen (card 376, runs c376b61 and c376b62: "LOADING..." stayed up, no input). In the host's session the transition
// is not run at all. `options` is the by-value FString: the callee owns it (MSVC), so a skipped call frees it.
TransitionToLatestFn g_orig_c381_latest = nullptr;

namespace {
bool c381_client_skips(const char* what, FStringRaw* options) {
    if (!g_client_session) {
        return false;
    }
    log::line("WARN card381: client t=%llu own %s not run: in the host's session, the host chooses the map",
              static_cast<unsigned long long>(GetTickCount64()), what);
    if (options != nullptr && options->data != nullptr) {
        g_memory_free(const_cast<wchar_t*>(options->data));
        *options = FStringRaw{nullptr, 0, 0};
    }
    return true;
}
} // namespace

void detour_c381_transition(void* game_instance, int32_t chapter, int32_t level, FStringRaw* options) {
    if (!c381_client_skips("TransitionToLevel", options)) {
        g_transition_to_level(game_instance, chapter, level, options);
    }
}

void detour_c381_transition_latest(void* game_instance, FStringRaw* options) {
    if (!c381_client_skips("TransitionToLatestLevel", options)) {
        g_orig_c381_latest(game_instance, options);
    }
}

// Card 345: the client's own level-end continue (WB_TrooperLevelEnd) opens the menu map with FromLevelComplete. That
// local Browse left the host's session while the host was still on its own level-end screen (playtest 2026-11-13:
// the client landed in its own menu, then the follow loaded the finished mission again). The Browse is not run and
// reports Success, so UEngine::TickWorldTravel sees no failure and the connection stays up. The client leaves the
// map with the host: the host's travel drops it and the card 045 follow takes it into the host's hangar.
bool c345_client_stays(const uint8_t* url) {
    if (!g_client_session || g_c45.this_ours || g_c45.follow != C45Follow::Off || fs_len(url + kFurlHost) != 0 ||
        wcsstr(fs_text(url + kFurlMap), kC52MenuMapW) == nullptr || !c45_has_option(url, L"FromLevelComplete")) {
        return false;
    }
    log::line("card345: client t=%llu own level-end Browse to \"%.*ls\" not run: the client stays in the host's "
              "session until the host travels", static_cast<unsigned long long>(GetTickCount64()),
              fs_len(url + kFurlMap), fs_text(url + kFurlMap));
    return true;
}

// Card 376 part 2: the pause menu's RESTART MISSION is not the client's to run in the host's session. Its RestartLevel
// starts the game's loading screen before OpenLevel (drawn off the game thread, T 1452 in c376b61 and c376b62), and
// with the Browse refused nothing ends it: "LOADING..." stayed up and no input action fired. RemoveFromParent on the
// widget did not end it (run c376b62). So the button and the confirm are not run at all: while the client's
// WB_Stack_C holds the pause, a WB_Pause_Menu_C call to the RESTARTLEVEL button event or to RestartLevel is dropped.
namespace {
constexpr const char kC376Button[] = "BndEvt__WB_Pause_Menu_RESTARTLEVEL_button";
constexpr uint32_t kC376Lines = 8;
struct C376 {
    const void* skip_cls; // the last class that was not the pause menu: one name compare per run of same-class calls
    uint32_t refused;
};
C376 g_c376{};
} // namespace


// From detour_request_pause / detour_request_unpause, after the game's call; `requester` is live.
void c376_note_pause(void* requester, bool paused) {
    if (strcmp(class_name(requester).text, "WB_Stack_C") != 0) {
        return;
    }
    pe_gate_set(kPeGate_c376_pe, paused && g_client_session); // a session lost with the menu open disarms at the unpause
    g_c376.skip_cls = nullptr;
}

// Hot path while kPeGate_c376_pe (a client's menu is open): every ProcessEvent. True: the call is not run.
bool c376_pe(void* object, void* function) {
    const void* const cls = read_ptr(object, kObjectClass);
    if (cls == g_c376.skip_cls) {
        return false;
    }
    if (strcmp(class_name(object).text, "WB_Pause_Menu_C") != 0) {
        g_c376.skip_cls = cls;
        return false;
    }
    const auto fn_name = object_name(function);
    const char* const fn = fn_name.text;
    if (!g_client_session || (strncmp(fn, kC376Button, sizeof(kC376Button) - 1) != 0 && strcmp(fn, "RestartLevel") != 0)) {
        return false;
    }
    if (g_c376.refused++ < kC376Lines) {
        log::line("card376: client t=%llu pause menu %s not run: in the host's session the host restarts the mission",
                  static_cast<unsigned long long>(GetTickCount64()), fn);
    }
    return true;
}

// Card 346: AWorld::AuthorityGameMode (UWorld::ServerTravel reads it at world+0x1A8, 0x144355360) and
// AGameModeBase::bUseSeamlessTravel (bit 0 of +0x328, tested by ProcessServerTravel at 0x143C673B8).
constexpr size_t kWorldAuthorityGameMode = 0x1A8;
constexpr size_t kGameModeSeamlessByte = 0x328;
constexpr uint8_t kGameModeSeamlessBit = 0x01;
constexpr size_t kC346UrlChars = 1024;

// Card 346: "map?opt1?opt2..." from the Browse URL copy into buf. False when it does not fit.
bool c346_url_text(const uint8_t* url, wchar_t* buf, size_t cap) {
    const auto* const op = reinterpret_cast<const FArrayRaw*>(url + kFurlOp);
    int n = swprintf(buf, cap, L"%.*ls", fs_len(url + kFurlMap), fs_text(url + kFurlMap));
    for (int32_t i = 0; n > 0 && op->data != nullptr && i < op->num && i < kC45MaxOps; ++i) {
        const int add = swprintf(buf + n, cap - static_cast<size_t>(n), L"?%.*ls", fs_len(&op->data[i]),
                                 fs_text(&op->data[i]));
        n = add < 0 ? -1 : n + add;
    }
    return n > 0;
}

// Card 346 (user rule 2026-11-14, persistent connection): a listen host with a client connected does not run its own
// Browse to a local map. It sets bUseSeamlessTravel on its GameMode and calls UWorld::ServerTravel(url, true, false):
// the GameMode sends every remote PC a seamless ClientTravel and the net driver stays up, so the client is never
// dropped. True when ServerTravel took it (the Browse is then not run). The hard paths stay: no client connected, a
// URL with valid 0, ServerTravel false. Card 369: the host's pause menu QUIT (FromLevelQuit) is seamless as well, like
// the card 344 return (card 350): the client stays in the session and gets the frontend options with its ClientTravel.
// Card 434 (C5M2 case). UNetDriver::CleanPackageMaps (exe+0x3E610B0) runs on each seamless travel: it cleans the
// guid cache (FNetGUIDCache::CleanReferences: an entry whose object is gone turns read-only and is removed at the
// first clean 90 s later, static guids too) and resets each connection's export ack state only when
// net.ResetAckStatePostSeamlessTravel is nonzero (engine default 0). With 0 the host holds a class archetype's
// path as acked while the client's cache has dropped the entry (the class was unloaded there on an earlier map),
// so a spawn bunch on a later map names an archetype guid the client cannot resolve and makes no actor (11
// bunches on FED_C5M2, b137 and b139). The value is the int32 CleanPackageMaps compares at +0x1A
// (`cmp dword [rip+disp32], 0`: 83 3D disp32 00). Cost: each guid's path goes once more per map per client.
constexpr size_t kCleanMapsCmp = 0x1A;

void c434_reset_ack_on(ULONGLONG now) {
    const uint8_t* const at = g_exe_base + offsets::UNetDriver_CleanPackageMaps.rva + kCleanMapsCmp;
    if (at[0] != 0x83 || at[1] != 0x3D || at[6] != 0x00) {
        log::line("WARN card434: host t=%llu UNetDriver::CleanPackageMaps +0x1A is %02x %02x .. %02x, not the cvar "
                  "compare: net.ResetAckStatePostSeamlessTravel left as it is",
                  static_cast<unsigned long long>(now), at[0], at[1], at[6]);
        return;
    }
    int32_t disp = 0;
    memcpy(&disp, at + 2, sizeof(disp));
    auto* const value = const_cast<int32_t*>(reinterpret_cast<const int32_t*>(at + 7 + disp));
    if (*value == 0) {
        *value = 1;
        log::line("card434: host t=%llu net.ResetAckStatePostSeamlessTravel 0 -> 1: guid paths are sent again after "
                  "each seamless travel", static_cast<unsigned long long>(now));
    }
}

bool c346_host_seamless(void* engine, const uint8_t* url) {
    c364_expect_clients(0); // card 364: a hard travel carries no client
    void* const world = g_get_game_world(engine);
    // Card 349: any PC in the world will do (net mode only); not the local PC.
    void* const pc = world != nullptr ? g_world_first_pc(world) : nullptr;
    if (pc == nullptr || net_mode(pc) != kNetModeListenServer || g_server_travel == nullptr ||
        fs_len(url + kFurlHost) != 0 || fs_len(url + kFurlMap) == 0 || read_i32(url, kFurlValid) == 0) {
        return false;
    }
    const uint32_t players = c43_roster(world).players;
    auto* const mode = static_cast<uint8_t*>(read_ptr(world, kWorldAuthorityGameMode));
    if (players < 2 || mode == nullptr) {
        return false;
    }
    static wchar_t text[kC346UrlChars];
    const ULONGLONG now = GetTickCount64();
    if (!c346_url_text(url, text, kC346UrlChars)) {
        log::line("WARN card346: host t=%llu seamless travel URL longer than %zu characters: the Browse runs",
                  static_cast<unsigned long long>(now), kC346UrlChars);
        return false;
    }
    const uint8_t was = mode[kGameModeSeamlessByte];
    mode[kGameModeSeamlessByte] = static_cast<uint8_t>(was | kGameModeSeamlessBit);
    const FStringRaw fs{text, static_cast<int32_t>(wcslen(text) + 1), static_cast<int32_t>(kC346UrlChars)};
    c397_host_travel(world, text); // card 397: before the travel, so the frame is ahead of the ClientTravel
    c434_reset_ack_on(now); // card 434: before the travel's CleanPackageMaps
    const bool ok = g_server_travel(world, &fs, true, false);
    log::line("%scard346: host t=%llu seamless travel to \"%ls\" %s (%u players, bUseSeamlessTravel was %d)",
              ok ? "" : "WARN ", static_cast<unsigned long long>(now), text,
              ok ? "started by ServerTravel, Browse not run" : "refused by ServerTravel (false): the Browse runs",
              players, (was & kGameModeSeamlessBit) != 0 ? 1 : 0);
    c364_expect_clients(ok ? static_cast<int32_t>(players) - 1 : 0); // card 364: the hold on the next map waits for them
    return ok;
}

int c45_browse_enter(void* engine, void* url) {
    g_c45.this_ours = g_c45.ours;
    g_c45.ours = false;
    if (g_c45.layout == 0) {
        c45_check_layout();
    }
    if (g_c45.layout != 1) {
        return kBrowseRun;
    }
    auto* const u = static_cast<uint8_t*>(url);
    c45_log_url("Browse URL", u);
    if (c154_client_refuses(u)) {
        // Card 376: in the host's session the refusal reports success, as card 345 does. A failure made the engine
        // raise ClientTravelFailure (11), whose handler browses to the menu with "?closed": the client dropped and
        // the follow rejoined it as a fresh join (run c376b60, client pause-menu RESTART MISSION). While following
        // the host back (no session) the failure stays: it returns the client to its menu (card 154).
        return g_client_session ? kBrowseSuccess : kBrowseFailure;
    }
    if (c345_client_stays(u)) {
        return kBrowseSuccess;
    }
    c45_client_browse(u);
    if (!g_client_session && g_c45.follow == C45Follow::Off) {
        c52_host_browse(engine, u); // card 052: host mode adds listen to the menu's mission Browse
        if (c346_host_seamless(engine, u)) {
            return kBrowseSuccess; // card 346: no travel row, no listen (the net driver stays up)
        }
        if (read_i32(u, kFurlValid) == 0) {
            log::line("card344: host t=%llu Browse URL valid 0: no listen added, no travel row",
                      static_cast<unsigned long long>(GetTickCount64()));
            return kBrowseRun; // card 344 follow-up (a): one travel row per real travel
        }
        c290_host_travel(engine, c45_has_option(u, L"FromLevelComplete") ? "level-complete"
                                 : c45_has_option(u, L"FromLevelQuit")   ? "quit"
                                                                         : "travel"); // card 290, old world still up
        c45_host_browse(engine, u); // never on a client, in or out of its session
    }
    return kBrowseRun;
}

// The engine's failure hooks (card 052) end a Pending try: the next try follows after kC45RetryMs.
void c45_pending_failed(const char* kind, const char* name) {
    if (g_c45.follow != C45Follow::Pending) {
        return;
    }
    log::line("card045: client try %u failed (%s failure %s): next try in %llu ms", g_c45.tries, kind, name,
              static_cast<unsigned long long>(kC45RetryMs));
    g_c45.follow = C45Follow::Retrying;
    g_c45.next_ms = GetTickCount64() + kC45RetryMs;
}

void c45_browse_return(int result) {
    if (!g_c45.this_ours) {
        return;
    }
    g_c45.this_ours = false;
    log::line("card045: client try %u Browse returned %d (%s)", g_c45.tries, result, browse_result_name(result));
    if (result == 2 && g_c45.follow == C45Follow::Retrying) {
        g_c45.follow = C45Follow::Pending;
        g_c45.next_ms = GetTickCount64() + kC45PendingMs;
    }
}

} // namespace ubw::hooks
