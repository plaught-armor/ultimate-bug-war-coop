// Card 135: the press-to-start screen (WB_Loading_Screen_Final_C, and the brief on maps that have one) on both sides.
// The host's screen comes from ASTGameplayGameMode::BeginPlay calling ASTHUDGameplay::GameStarted(bFromSave) on the
// first local player's HUD; the GameMode exists only on the server, so a client never gets that call. When the host
// leaves its screen (its WB_Stack_C, whose first element at the pause was the loading screen, calls
// RequestGameUnpause and is empty), the host logs "card135: host continued", remembers it for this map and sends one
// IntroStart text frame ("<map> <bFromSave>") to every connected client; a client that joins later gets it from
// sync_join_replay. The client waits (per tick, bounded) until its own HUD is a live BP_HUD_Manager with a possessed
// pawn, then calls GameStarted(bFromSave) on that HUD through ProcessEvent once per map: the game's own graph pushes
// the same screen (and brief) on the client's own stack. Nothing holds or replays level sequences.
// The screen's title and image come from the game instance's chapter and level IDs (WB_Loading_Screen_Final_C:
// GetSTGameInstance, GetChapterID, GetLevelID, GetUIDataForLevel; defaults "Level Name" and
// T_Loading_Screen_Klendathu). The frontend sets them on the host; a client that joins never picks a level, so the
// host reads its IDs at the screen's pause and the op carries them ("<map> <bFromSave> <chapter> <level>"); the
// client sets them on its own game instance (SetChapterID, SetLevelID) right before its GameStarted call.
// While the client's screen is up its stack is remembered too (c135_screen_up), so the card 156 +5 s game-input check
// waits until it closes. The GameStarted call also waits for the card 156 map-load restore (g_game_only_pending),
// which would otherwise set game input over the screen's UI input.
//
// Card 320: the title screen holds the world until every connected player has confirmed it. The host sends IntroStart
// at its own screen's pause (to every ponged client; later ones from the join replay while the screen is up), and the
// hold is on from that pause until the host has continued and every client it sent the op to while the hold was on
// has answered with IntroDone (its own stack emptied, or its GameStarted failed or timed out). While the hold is on
// card 096 lets the host's pause through and refuses the unpause, card 120 does not lift it, and after the host's
// continue the mod sets the pause each engine tick (the game's UpdatePause stops asking once its requesters are
// gone). Release: the last confirm or a waited-for client's Logout; the release unpauses once unless a game screen
// still holds the stack. kC135WaitMs after the host's continue with a confirm still owed, the host returns to the
// main menu (card 344, c344_no_confirm) and the hold stays on until that travel ends the map. A client that joins after the release gets
// the op over a running world, as before.
//
// Map lifetime: all state is g_c135 and g_c320, cleared by c135_map_end at LoadMap enter on both sides. Teardown
// writer: an unpause of the old world's stack after LoadMap enter would reach c135_note_unpause; it is ignored because
// the remembered stack is cleared at the reset and the unpause must come from that remembered stack.

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

namespace {

constexpr ULONGLONG kC135WaitMs = 60000;   // client: the HUD and pawn wait after the op
constexpr ULONGLONG kC396DrainMs = 5000;   // host: the hold's wait for queued frames after the last confirm
constexpr ULONGLONG kC135RestartMs = 1000; // client: a held, unacknowledged pawn this long gets ClientRestart
constexpr uint32_t kC135Restarts = 5;      // client: such calls per op
constexpr int32_t kC135MapChars = 64;      // map name chars kept
constexpr uint32_t kC135Lines = 16;        // capped log lines per map (refusals, waits)
constexpr uint16_t kC135ParmsBytes = 16;   // GameStarted parms: one bool
constexpr const char* kC135Screen = "WB_Loading_Screen_Final_C";
constexpr size_t kC135PcMyHud = 0x358;      // APlayerController::MyHUD (as death.cpp kPcMyHud)

struct C135State {
    // host
    uint64_t stack_weak;       // FWeakObjectPtr: the WB_Stack_C whose pause held the loading screen
    bool screen_seen;          // that stack's element 0 at its pause was the loading screen
    bool continued;            // the host left its screen on this map (the op is due to late joiners)
    bool from_save;            // the host's GameMode options carried FromSave
    int32_t chapter;           // the host's game instance chapter and level IDs at the screen's pause
    int32_t level;
    char map[kC135MapChars];   // the host's map name at the screen's pause (at the continue if the hold never began)
    bool no_map_warned;        // the join replay's "no map yet" WARN was written on this map
    // both sides
    ULONGLONG screen_ms;       // when the screen's stack paused (screen_seen set)
    bool prompt_logged;        // the "screen prompt ready" line was written on this map
    bool confirmed;            // client: the card 320 "screen confirmed" row was written on this map
    // client
    uint64_t pc_weak;          // FWeakObjectPtr: the PC the op came in on
    bool pending;              // an op waits for the HUD and pawn
    bool started;              // GameStarted was called on this map
    bool op_from_save;         // the op's bFromSave
    char op_map[kC135MapChars];
    int32_t op_chapter;        // the op's chapter and level IDs
    int32_t op_level;
    ULONGLONG op_ms;           // when the op came in
    ULONGLONG unacked_ms;      // when the op's PC was first seen with a pawn it has not acknowledged (0: not seen)
    uint32_t restarts;         // ClientRestart calls made for that pawn on this op
    uint32_t lines;
};
C135State g_c135{};

// Card 320, host: the title-screen hold. Sets of remote PCs by object index (dropped at Logout).
struct C320Hold {
    bool on;                    // the hold is on (host screen pause until release)
    bool host_done;             // the host continued
    bool left;                  // a waited-for client logged out
    bool pause_logged;          // the first per-tick pause call was logged
    bool unsent_logged;         // card 364: the "hold stays on" row was written on this map
    bool to_menu;               // card 344: the bound passed, the card 350 return ran; the hold stays on until map end
    ULONGLONG on_ms;
    ULONGLONG done_ms;          // the host's continue
    uint64_t pc_weak;           // FWeakObjectPtr: the host's first PC, the world context of the hold's pause
    int32_t sent[kC17Slots];    // remote PCs that got IntroStart on this map
    int32_t n_sent;
    int32_t waiting[kC17Slots]; // remote PCs sent IntroStart while the hold was on, no IntroDone yet
    int32_t n_waiting;
    int32_t done[kC17Slots];    // card 400: remote PCs whose IntroDone came in on this map
    int32_t n_done;
    ULONGLONG drain_ms;         // card 396: when every confirm was in with frames still queued (0: not seen)
};
C320Hold g_c320{};
// Card 364: clients a seamless travel carried to this map that have not been sent IntroStart on it yet. Set at the
// travel (c346_host_seamless), counted down by c320_send; not part of g_c320, which the map end between the two clears.
// Such a client has no PC in the new world until its own load ends (b50 run 1: the frontend PC destroyed at
// 388170271, the new PC spawned at 388172480, the host's continue between them), so counting PCs misses it.
int32_t g_c364_expect = 0;

// FName caches (kept across maps: names are process-wide).
struct C135Names {
    uint64_t game_started;
    uint64_t get_chapter;
    uint64_t get_level;
    uint64_t set_chapter;
    uint64_t set_level;
};
C135Names g_c135_names{};

bool c135_line() {
    if (g_c135.lines >= kC135Lines) {
        return false;
    }
    ++g_c135.lines;
    return true;
}

// The stack's element 0 (the first widget pushed), or null.
const void* c135_first(const void* stack) {
    const uint8_t* const arr = c67_stack_array(stack); // cards 411, 414: offset kept per stack class
    const int32_t num = arr != nullptr ? read_i32(arr, kArrayNum) : 0;
    void* const* const data = arr != nullptr ? static_cast<void* const*>(read_ptr(arr, kArrayData)) : nullptr;
    return data != nullptr && num > 0 && num <= read_i32(arr, kArrayMax) ? data[0] : nullptr;
}

// Host: true when the GameMode's OptionsString carries "FromSave" (case-insensitive), the option BeginPlay reads.
bool c135_host_from_save(void* world) {
    void* const mode = g_orig_get_game_mode != nullptr ? g_orig_get_game_mode(world) : nullptr;
    const uint8_t* const p = mode != nullptr ? prop_find_prop(read_ptr(mode, kObjectClass), "OptionsString") : nullptr;
    const auto* const s = p != nullptr ? reinterpret_cast<const FStringRaw*>(static_cast<const uint8_t*>(mode) +
                                                                            read_i32(p, kPropertyOffset))
                                       : nullptr;
    constexpr wchar_t kKey[] = L"FromSave";
    constexpr int32_t kKeyLen = static_cast<int32_t>(sizeof(kKey) / sizeof(kKey[0])) - 1;
    const int32_t n = s != nullptr && s->data != nullptr ? s->num - 1 : 0;
    for (int32_t i = 0; i + kKeyLen <= n && i < kMaxLoggedFStringNum; ++i) {
        if (_wcsnicmp(s->data + i, kKey, kKeyLen) == 0) {
            return true;
        }
    }
    return false;
}

// Host: the IntroStart frame to one remote PC. True when it went to the transport. Card 396: a priority frame
// (chan_send_client_now), ahead of the join replay rows queued for the PC, so the client's screen does not wait behind
// them (c395b82: 374 to 445 rows, 0.8 to 2.5 s); the hold then waits for those rows (c396_queued).
bool c135_send(void* pc) {
    char text[kC135MapChars + 32];
    snprintf(text, sizeof(text), "%s %d %d %d", g_c135.map, g_c135.from_save ? 1 : 0, g_c135.chapter, g_c135.level);
    static wchar_t wire[kChanTextFrame];
    const WireString w = chan_text_frame(ChanOp::IntroStart, text, wire);
    uint32_t ahead = 0;
    const bool sent = w.num > 0 && ue::chan::chan_send_client_now(pc, w, &ahead);
    if (sent) {
        log::line("card396: host t=%llu IntroStart to %s ahead of %u queued frame(s)",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(pc).text, ahead);
    }
    return sent;
}

bool c320_has(const int32_t* set, int32_t n, int32_t id) {
    for (int32_t i = 0; i < n && i < kC17Slots; ++i) {
        if (set[i] == id) {
            return true;
        }
    }
    return false;
}

// Swap-removes id from the set; true when it was there.
bool c320_drop(int32_t* set, int32_t& n, int32_t id) {
    for (int32_t i = 0; i < n && i < kC17Slots; ++i) {
        if (set[i] == id) {
            set[i] = set[--n];
            return true;
        }
    }
    return false;
}

// The PC's player index in its world (0 = the host's own), or -1.
int32_t c320_slot(const void* pc) {
    void* const world = pc != nullptr && g_actor_get_world != nullptr ? g_actor_get_world(pc) : nullptr;
    for (int32_t s = 0; world != nullptr && s < kC17Slots; ++s) {
        if (g_get_player_controller(world, s) == pc) {
            return s;
        }
    }
    return -1;
}

// Host: IntroStart to one remote PC, once per map. A PC newly sent the op while the hold is on joins the waiting set.
// True when the PC has the op (now or earlier).
bool c320_send(void* pc) {
    const int32_t id = object_index(pc);
    if (c320_has(g_c320.sent, g_c320.n_sent, id)) {
        return true;
    }
    if (g_c320.n_sent >= kC17Slots || !c135_send(pc)) {
        return false;
    }
    g_c320.sent[g_c320.n_sent++] = id;
    g_c364_expect -= g_c364_expect > 0 ? 1 : 0;
    if (g_c320.on && g_c320.n_waiting < kC17Slots) {
        g_c320.waiting[g_c320.n_waiting++] = id;
    }
    return true;
}

// Host: the hold ends. The world is unpaused once unless a game screen still holds the stack (card 096 then behaves
// as before: its next skipped pause lifts it).
void c320_release(const char* why) {
    g_c320.on = false;
    void* const pc0 = g_weak_get != nullptr ? g_weak_get(&g_c320.pc_weak) : nullptr;
    void* const world = pc0 != nullptr && g_actor_get_world != nullptr ? g_actor_get_world(pc0) : nullptr;
    const bool paused = world != nullptr && g_world_is_paused != nullptr && g_world_is_paused(world);
    const bool screen = cine_stack_open();
    const bool lift = paused && !screen && g_orig_c96_set_game_paused != nullptr;
    const bool ok = lift && g_orig_c96_set_game_paused(pc0, false);
    const ULONGLONG now = GetTickCount64();
    log::line("card320: host t=%llu hold off after %llu ms (%s); %s", static_cast<unsigned long long>(now),
              static_cast<unsigned long long>(now - g_c320.on_ms), why,
              lift      ? (ok ? "SetGamePaused(0) = 1" : "SetGamePaused(0) = 0")
              : !paused ? "the world was not paused"
              : screen  ? "a game screen still holds its pause: left to card 096"
                        : "no SetGamePaused resolved");
}

// Card 364, host: clients that have not been sent IntroStart on this map: the larger of the remote PCs in the hold's
// world without the op (connected, no Pong yet; card 139 refuses the op until its first Pong) and the clients a
// seamless travel carried here that are still without it (no PC in this world yet). The hold waits for them: the join
// replay at the client's first Pong sends the op and adds the PC to the waiting set. A carried client that never
// arrives is ended by the card 344 bound (host to the main menu).
int32_t c320_unsent() {
    void* const pc0 = g_weak_get != nullptr ? g_weak_get(&g_c320.pc_weak) : nullptr;
    void* const world = pc0 != nullptr && g_actor_get_world != nullptr ? g_actor_get_world(pc0) : nullptr;
    int32_t n = 0;
    for (int32_t s = 0; world != nullptr && s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        n += pc != nullptr && c26_is_remote_pc(pc) && !c320_has(g_c320.sent, g_c320.n_sent, object_index(pc)) ? 1 : 0;
    }
    return n > g_c364_expect ? n : g_c364_expect;
}

// Card 396, host: frames still in the pace FIFO of the clients that were sent IntroStart on this map. The op goes
// ahead of the join replay rows, so a client can confirm before the rows are on the wire; the hold waits for them.
uint32_t c396_queued() {
    void* const pc0 = g_weak_get != nullptr ? g_weak_get(&g_c320.pc_weak) : nullptr;
    void* const world = pc0 != nullptr && g_actor_get_world != nullptr ? g_actor_get_world(pc0) : nullptr;
    uint32_t n = 0;
    for (int32_t s = 0; world != nullptr && s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        if (pc != nullptr && c320_has(g_c320.sent, g_c320.n_sent, object_index(pc))) {
            n += ue::chan::chan_pace_depth(pc);
        }
    }
    return n;
}

// Host: at each confirm, and every tick after the host's continue (c320_step).
void c320_try_release() {
    // Card 344: no late release. Card 355: a waited-for client that left does not release; c320_step returns the host.
    if (!g_c320.on || !g_c320.host_done || g_c320.n_waiting != 0 || g_c320.to_menu || g_c320.left) {
        return;
    }
    const int32_t unsent = c320_unsent();
    if (unsent != 0) { // card 364: a connected client has not loaded the map yet
        if (!g_c320.unsent_logged) {
            g_c320.unsent_logged = true;
            log::line("card364: host t=%llu hold stays on: %d client(s) not sent IntroStart yet (no Pong on this "
                      "map; %d carried by the seamless travel)", static_cast<unsigned long long>(GetTickCount64()),
                      unsent, g_c364_expect);
        }
        return;
    }
    // Card 396: every confirm is in; the join replay rows may still be queued behind the pacer.
    const ULONGLONG now = GetTickCount64();
    const uint32_t queued = c396_queued();
    if (queued != 0 && g_c320.drain_ms == 0) {
        g_c320.drain_ms = now;
        log::line("card396: host t=%llu hold stays on: %u frame(s) still queued to the confirmed client(s)",
                  static_cast<unsigned long long>(now), queued);
    }
    if (queued != 0 && now - g_c320.drain_ms <= kC396DrainMs) {
        return;
    }
    if (g_c320.drain_ms != 0) {
        log::line("%scard396: host t=%llu queued frames waited for %llu ms, %u left", queued != 0 ? "WARN " : "",
                  static_cast<unsigned long long>(now), static_cast<unsigned long long>(now - g_c320.drain_ms), queued);
    }
    c320_release("all confirmed");
}

// Host, at its screen's pause (c135_note_pause): the op's map and bFromSave, IntroStart to every ponged client, the
// hold on.
void c320_hold_begin(void* stack) {
    void* const pc0 = g_get_player_controller != nullptr ? g_get_player_controller(stack, 0) : nullptr;
    void* const world = pc0 != nullptr && g_actor_get_world != nullptr ? g_actor_get_world(pc0) : nullptr;
    const ULONGLONG now = GetTickCount64();
    if (world == nullptr) {
        log::line("WARN card320: host t=%llu hold not on: the screen's stack has no PC or world",
                  static_cast<unsigned long long>(now));
        return;
    }
    g_weak_assign(&g_c320.pc_weak, &pc0);
    g_c135.from_save = c135_host_from_save(world);
    snprintf(g_c135.map, sizeof(g_c135.map), "%s", object_name(world).text);
    g_c320.on = true;
    g_c320.on_ms = now;
    for (int32_t s = 0; s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        const C50Copy* const c = pc != nullptr && c26_is_remote_pc(pc) ? c50_copy(object_index(pc), false) : nullptr;
        if (c != nullptr && c->ponged && !c320_send(pc)) {
            log::line("WARN card320: host t=%llu IntroStart not sent to %s at the screen's pause",
                      static_cast<unsigned long long>(now), object_name(pc).text);
        }
    }
    log::line("card320: host t=%llu hold on (%s), waiting for host 1 clients %d", static_cast<unsigned long long>(now),
              g_c135.map, g_c320.n_waiting);
}

// Card 344 (user rule 2026-11-14), host: a waited-for client did not confirm inside the bound. The mission does not
// start with the host alone: the host goes back to the main menu by the game's own pause-menu QUIT path, and the
// hold (the world's pause) stays on until that travel ends the map. Card 350: the return opens the frontend with the
// pause menu's own options (FromLevelQuit?ChapterID=?LevelID=), so the frontend HUD skips the first-start FTUE
// screens, and card 346 takes it seamless (the client stays in the session). With no game instance or the return
// unresolved the hold is released instead (WARN): a hold with no way out would hang the host.
// Card 355 (user rule 2026-11-14, "Host to main menu"): the same return when a waited-for client left during the hold
// (left = true), before or after the host's continue. With no client connected the travel is the hard path.
void c344_no_confirm(void* pc0, ULONGLONG now, bool left) {
    void* const gi = pc0 != nullptr && g_get_st_game_instance != nullptr ? g_get_st_game_instance(pc0) : nullptr;
    const char* const then = gi != nullptr ? "host returns to the main menu" : "no game instance";
    if (left) {
        log::line("WARN card355: host t=%llu a waited-for client left during the hold (host continued %d): %s",
                  static_cast<unsigned long long>(now), g_c320.host_done ? 1 : 0, then);
    } else {
        log::line("WARN card344: host t=%llu %d client(s) did not confirm (%d not loaded, card 364) within %llu ms of "
                  "the host's continue: %s", static_cast<unsigned long long>(now), g_c320.n_waiting, c320_unsent(),
                  kC135WaitMs, then);
    }
    if (gi == nullptr || !c350_host_return_to_menu(pc0, gi)) {
        log::line("WARN card344: host t=%llu no return to the menu: hold released, the host goes on alone",
                  static_cast<unsigned long long>(now));
        c320_release(left ? "disconnect" : "timeout");
        return;
    }
    g_c320.to_menu = true;
    c52_note_no_confirm();
}

// Host, every engine tick after its continue: the hold's own pause (the screen's requester is gone), and the bound.
void c320_step() {
    if (g_client_session || !g_c320.on) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    void* const pc0 = g_weak_get != nullptr ? g_weak_get(&g_c320.pc_weak) : nullptr;
    if (g_c320.left && !g_c320.to_menu) { // card 355: not from inside the engine's Logout, and before the host's continue too
        c344_no_confirm(pc0, now, true);
        return;
    }
    if (!g_c320.host_done) {
        return;
    }
    if (!g_c320.to_menu && now - g_c320.done_ms > kC135WaitMs) {
        c344_no_confirm(pc0, now, false);
        return;
    }
    c320_try_release(); // card 396: the release may wait on queued frames, so it is tried each tick
    if (!g_c320.on) {
        return;
    }
    void* const world = pc0 != nullptr && g_actor_get_world != nullptr ? g_actor_get_world(pc0) : nullptr;
    if (world == nullptr || g_world_is_paused == nullptr || g_world_is_paused(world) ||
        g_orig_c96_set_game_paused == nullptr) {
        return;
    }
    const bool ok = g_orig_c96_set_game_paused(pc0, true);
    if (!g_c320.pause_logged) {
        g_c320.pause_logged = true;
        log::line("%scard320: host t=%llu hold pause set after the continue: SetGamePaused(1) = %d", ok ? "" : "WARN ",
                  static_cast<unsigned long long>(now), ok ? 1 : 0);
    }
}

// Client: IntroDone to the host (the screen closed, or it never came). `path` names why.
void c320_client_done(const char* path) {
    // Card 344 test lever: UBWCOOP_C344_NOCONFIRM=1 withholds the confirm, so the host's bound runs out.
    static const bool withhold = [] {
        wchar_t buf[4] = {};
        return GetEnvironmentVariableW(L"UBWCOOP_C344_NOCONFIRM", buf, 4) == 1 && buf[0] == L'1';
    }();
    if (withhold) {
        log::line("card344: client t=%llu IntroDone withheld (UBWCOOP_C344_NOCONFIRM=1, %s)",
                  static_cast<unsigned long long>(GetTickCount64()), path);
        return;
    }
    void* const pc = g_weak_get != nullptr ? g_weak_get(&g_c135.pc_weak) : nullptr;
    const bool ok = pc != nullptr && chan_to_host(pc, ChanOp::IntroDone, 0);
    log::line("%scard320: client t=%llu IntroDone %s (%s)", ok ? "" : "WARN ",
              static_cast<unsigned long long>(GetTickCount64()), ok ? "sent" : "not sent", path);
    c337_uncover(path); // card 337: a no-op once the screen uncovered it
}

// USTGameInstance's int UFunction `text` (GetChapterID/GetLevelID: the return value; SetChapterID/SetLevelID: the
// one parameter) through ProcessEvent: reads into, or writes from, *io. False when the function is not found.
bool c135_gi_int(void* gi, uint64_t& cache, const wchar_t* text, int32_t* io) {
    void* const fn = gi != nullptr && g_find_function != nullptr && g_orig_process_event != nullptr &&
                             fname_cached(cache, text) != 0
                         ? g_find_function(gi, cache)
                         : nullptr;
    uint16_t size = 0;
    if (fn != nullptr) {
        memcpy(&size, static_cast<const uint8_t*>(fn) + kFunctionParmsSize, sizeof(size));
    }
    const void* const first = fn != nullptr ? read_ptr(fn, kStructChildProperties) : nullptr;
    if (first == nullptr || size != sizeof(int32_t) || read_i32(first, kPropertyOffset) != 0) {
        return false;
    }
    alignas(16) uint8_t parms[kC135ParmsBytes] = {};
    memcpy(parms, io, sizeof(int32_t));
    g_orig_process_event(gi, fn, parms);
    memcpy(io, parms, sizeof(int32_t));
    return true;
}

// The game instance of the world `actor` is in, or null.
void* c135_gi(void* actor) {
    void* const world = actor != nullptr && g_actor_get_world != nullptr ? g_actor_get_world(actor) : nullptr;
    return world != nullptr ? read_ptr(world, kWorldOwningGameInstance) : nullptr;
}

// Client: GameStarted(bFromSave) on `hud` through ProcessEvent. Null, or why it could not be called.
const char* c135_call(void* hud, bool from_save) {
    void* const fn = fname_cached(g_c135_names.game_started, L"GameStarted") != 0
                         ? g_find_function(hud, g_c135_names.game_started)
                         : nullptr;
    uint16_t size = 0;
    if (fn != nullptr) {
        memcpy(&size, static_cast<const uint8_t*>(fn) + kFunctionParmsSize, sizeof(size));
    }
    const void* const first = fn != nullptr ? read_ptr(fn, kStructChildProperties) : nullptr;
    if (fn == nullptr || first == nullptr || size > kC135ParmsBytes) {
        return "no GameStarted(bool) on the HUD";
    }
    alignas(16) uint8_t parms[kC135ParmsBytes] = {};
    parms[read_i32(first, kPropertyOffset) & (kC135ParmsBytes - 1)] = from_save ? 1 : 0;
    g_orig_process_event(hud, fn, parms);
    return nullptr;
}

} // namespace

// Card 364, host, at a travel: clients the seamless travel carries to the next map (0: none).
void c364_expect_clients(int32_t n) {
    g_c364_expect = n > 0 ? n : 0;
}

// Both sides, at LoadMap enter (ubw_load_map_enter): every card 135 row names the old world.
void c135_map_end() {
    if (g_c320.on) {
        const ULONGLONG now = GetTickCount64();
        log::line("card320: host t=%llu hold off after %llu ms (map end)", static_cast<unsigned long long>(now),
                  static_cast<unsigned long long>(now - g_c320.on_ms));
    }
    g_c135 = C135State{};
    g_c320 = C320Hold{};
}

// From cine_note_pause: a live WB_Stack_C just requested the pause. Remember it when its element 0 is the loading
// screen (the GameStarted push): the host before its continue (and its chapter and level IDs, the ones its screen
// shows), the client after its own GameStarted call.
void c135_note_pause(void* stack) {
    if (g_c135.screen_seen || (g_client_session ? !g_c135.started : g_c135.continued) || g_weak_assign == nullptr) {
        return;
    }
    const void* const first = c135_first(stack);
    if (first == nullptr || strcmp(class_name(first).text, kC135Screen) != 0) {
        return;
    }
    g_weak_assign(&g_c135.stack_weak, &stack);
    g_c135.screen_seen = true;
    g_c135.screen_ms = GetTickCount64();
    if (g_client_session) {
        return;
    }
    void* const gi = c135_gi(g_get_player_controller != nullptr ? g_get_player_controller(stack, 0) : nullptr);
    g_c135.chapter = -1;
    g_c135.level = -1;
    const bool got = c135_gi_int(gi, g_c135_names.get_chapter, L"GetChapterID", &g_c135.chapter) &&
                     c135_gi_int(gi, g_c135_names.get_level, L"GetLevelID", &g_c135.level);
    log::line("%scard135: host t=%llu screen up: %s chapter %d level %d (%s)", got ? "" : "WARN ",
              static_cast<unsigned long long>(GetTickCount64()), class_name(gi).text, g_c135.chapter, g_c135.level,
              got ? "GetChapterID, GetLevelID" : "a getter was not found: the client's screen keeps its defaults");
    c320_hold_begin(stack); // card 320
}

// Client: its card 135 screen is up (its remembered stack still holds a widget). The card 156 check waits on it.
bool c135_screen_up() {
    void* const stack = g_c135.screen_seen && g_weak_get != nullptr ? g_weak_get(&g_c135.stack_weak) : nullptr;
    int32_t num = 0;
    if (stack != nullptr) {
        (void)c67_ui_top(stack, &num);
    }
    return num > 0;
}

// Card 320 step 1 (diagnostic, client): the client's own screen stack emptied (its confirm, or the test's auto-continue)
// with the world's pause state at that moment. The row the card 320 IntroDone would be sent from: it settles whether a
// client's screen takes its confirm under a replicated host pause. Once per map.
static void c320_client_confirm(void* requester) {
    if (!g_client_session || !g_c135.started || g_c135.confirmed || !g_c135.screen_seen || g_weak_get == nullptr ||
        g_weak_get(&g_c135.stack_weak) != requester) {
        return;
    }
    int32_t num = 0;
    (void)c67_ui_top(requester, &num);
    if (num != 0) {
        return;
    }
    g_c135.confirmed = true;
    void* const pc0 = g_get_player_controller(requester, 0);
    void* const world = pc0 != nullptr && g_actor_get_world != nullptr ? g_actor_get_world(pc0) : nullptr;
    const ULONGLONG now = GetTickCount64();
    log::line("card320: client t=%llu screen confirmed on %s (+%llu ms after the screen's pause, prompt ready %d); "
              "world paused %d",
              static_cast<unsigned long long>(now), world != nullptr ? object_name(world).text : "?",
              static_cast<unsigned long long>(now - g_c135.screen_ms), g_c135.prompt_logged ? 1 : 0,
              world != nullptr && g_world_is_paused != nullptr && g_world_is_paused(world) ? 1 : 0);
    c320_client_done("screen confirmed");
}

// From detour_request_unpause, after the game's call. Host: the remembered stack, now empty, is the screen closing.
void c135_note_unpause(void* requester) {
    c320_client_confirm(requester);
    if (g_client_session || g_c135.continued || !g_c135.screen_seen || g_weak_get == nullptr ||
        g_weak_get(&g_c135.stack_weak) != requester) {
        return;
    }
    int32_t num = 0;
    (void)c67_ui_top(requester, &num);
    if (num != 0) {
        return;
    }
    // The widget is a world context: GetPlayerController resolves its world, the first PC names it.
    void* const pc0 = g_get_player_controller(requester, 0);
    void* const world = pc0 != nullptr && g_actor_get_world != nullptr ? g_actor_get_world(pc0) : nullptr;
    g_c135.continued = true;
    if (g_c135.map[0] == '\0') { // card 320: the hold never began; the op's fields come from the continue
        g_c135.from_save = world != nullptr && c135_host_from_save(world);
        snprintf(g_c135.map, sizeof(g_c135.map), "%s", world != nullptr ? object_name(world).text : "?");
    }
    uint32_t sent = 0;
    uint32_t remote = 0;
    for (int32_t s = 0; world != nullptr && s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        if (pc == nullptr || !c26_is_remote_pc(pc)) {
            continue;
        }
        ++remote;
        sent += c320_send(pc) ? 1u : 0u; // card 320: the ponged ones had it at the screen's pause or the join replay
    }
    const ULONGLONG now = GetTickCount64();
    log::line("card135: host continued t=%llu on %s (bFromSave %d): IntroStart sent to %u of %u client(s)",
              static_cast<unsigned long long>(now), g_c135.map, g_c135.from_save ? 1 : 0, sent, remote);
    // Card 320 step 1: how long the host read its screen, and whether the world was held at the continue (card 120
    // lifts the pause at a client's first Pong, so with a client in it reads 0: the world ran under the screen).
    log::line("card320: host t=%llu screen read %llu ms (prompt ready %d), world paused at the continue %d, clients %u",
              static_cast<unsigned long long>(now), static_cast<unsigned long long>(now - g_c135.screen_ms),
              g_c135.prompt_logged ? 1 : 0,
              world != nullptr && g_world_is_paused != nullptr && g_world_is_paused(world) ? 1 : 0, remote);
    if (g_c320.on) {
        g_c320.host_done = true;
        g_c320.done_ms = now;
        log::line("card320: host t=%llu confirm from host", static_cast<unsigned long long>(now));
        c320_try_release();
    }
}

// Host, from sync_join_replay: a client joining after the host's screen came up gets the op; while the card 320 hold
// is on it is waited for.
void c135_join_replay(void* pc) {
    if (g_client_session || !g_c135.screen_seen) {
        return;
    }
    if (g_c135.map[0] == '\0') { // the hold never began and the host has not continued: an op would carry no map
        if (!g_c135.no_map_warned) {
            g_c135.no_map_warned = true;
            log::line("WARN card135: host t=%llu join replay: no map yet (the hold never began), IntroStart not sent "
                      "to %s (one row per map)", static_cast<unsigned long long>(GetTickCount64()),
                      object_name(pc).text);
        }
        return;
    }
    const int32_t waiting = g_c320.n_waiting;
    const bool sent = c320_send(pc);
    if (!sent && g_c320.n_done < kC17Slots) {
        g_c320.done[g_c320.n_done++] = object_index(pc); // card 400: no op, so no IntroDone will come; the pawn shows
    }
    log::line("%scard135: host t=%llu join replay: IntroStart (%s %d) %s %s", sent ? "" : "WARN ",
              static_cast<unsigned long long>(GetTickCount64()), g_c135.map, g_c135.from_save ? 1 : 0,
              sent ? "sent to" : "not sent to", object_name(pc).text);
    if (g_c320.n_waiting > waiting) {
        log::line("card320: host t=%llu hold waits for client slot %d (join), clients %d",
                  static_cast<unsigned long long>(GetTickCount64()), c320_slot(pc), g_c320.n_waiting);
    }
}

// Card 320, host.
bool c320_holding() {
    return !g_client_session && g_c320.on;
}

// Card 400, host: the PC's client has not confirmed (no IntroDone on this map) and it was sent IntroStart or its join
// replay will send it (the host's screen ran and the map is set: a rejoining client loads from its pawn's spawn to that
// send, card400b89a). A join replay whose send fails puts the PC in done, so it shows. With no screen or no map yet no
// op will go out, so the PC is not unconfirmed.
bool c320_unconfirmed(const void* pc) {
    const int32_t id = pc != nullptr ? object_index(pc) : -1;
    if (g_client_session || id < 0 || c320_has(g_c320.done, g_c320.n_done, id)) {
        return false;
    }
    return c320_has(g_c320.sent, g_c320.n_sent, id) || (g_c135.screen_seen && g_c135.map[0] != '\0');
}

// Host: ChanOp::IntroDone from the client that owns pc.
void c320_receive_done(void* pc, uint32_t) {
    const int32_t id = object_index(pc);
    const bool waited = c320_drop(g_c320.waiting, g_c320.n_waiting, id);
    if (!c320_has(g_c320.done, g_c320.n_done, id) && g_c320.n_done < kC17Slots) {
        g_c320.done[g_c320.n_done++] = id; // card 400: the client's trooper shows from here
    }
    log::line("card320: host t=%llu confirm from client slot %d%s", static_cast<unsigned long long>(GetTickCount64()),
              c320_slot(pc), waited ? "" : " (not waited for: the hold was off or the op went before it)");
    c320_try_release();
}

// Host: a leaving PC is no longer waited for (and a later PC may reuse its object index).
void c320_logout(const void* pc) {
    const int32_t id = object_index(pc);
    const bool sent = c320_drop(g_c320.sent, g_c320.n_sent, id);
    const bool waited = c320_drop(g_c320.waiting, g_c320.n_waiting, id);
    (void)c320_drop(g_c320.done, g_c320.n_done, id); // card 400: a later PC may reuse the index
    if (!g_c320.on || !(sent || waited || c26_is_remote_pc(pc))) { // card 364: a client still loading counts too
        return;
    }
    g_c320.left = true; // card 355: c320_step returns the host to the menu; a client that had confirmed counts too
    log::line("card320: host t=%llu client %s left during the hold (%s), clients %d",
              static_cast<unsigned long long>(GetTickCount64()), object_name(pc).text,
              waited ? "waited for" : sent ? "had confirmed" : "not loaded yet", g_c320.n_waiting);
}

// Client: ChanOp::IntroStart ("<map> <bFromSave> <chapter> <level>"). Starts the wait; a second op on the same map
// is refused.
void c135_receive(void* pc, const WireString* s) {
    char text[kC135MapChars + 32];
    const int32_t len = chan_text(s, text, sizeof(text));
    char map[kC135MapChars] = {};
    int32_t from_save = -1;
    int32_t chapter = -1;
    int32_t level = -1;
    const int got = len > 0 ? sscanf_s(text, "%63s %d %d %d", map, static_cast<unsigned>(sizeof(map)), &from_save,
                                       &chapter, &level)
                            : 0;
    if (got != 4 || (from_save != 0 && from_save != 1)) {
        log::line("WARN card135: client IntroStart frame unreadable (%d chars, %d fields) -- ignored", len, got);
        c337_uncover("IntroStart frame unreadable"); // card 337: no title screen follows
        return;
    }
    if (g_weak_assign == nullptr) {
        log::line("WARN card135: client IntroStart (%s) ignored: no FWeakObjectPtr assign resolved", map);
        c337_uncover("IntroStart ignored, no weak assign"); // card 337: no title screen follows
        return;
    }
    if (g_c135.started || g_c135.pending) {
        if (c135_line()) {
            log::line("card135: client t=%llu IntroStart (%s) refused: GameStarted %s on this map",
                      static_cast<unsigned long long>(GetTickCount64()), map,
                      g_c135.started ? "already called" : "already pending");
        }
        if (g_c135.started) {
            // Card 400: no screen follows, so confirm now (the host keeps this PC's trooper hidden until it does);
            // c320_client_done uncovers too (card 337). Pending: the screen follows and confirms.
            g_weak_assign(&g_c135.pc_weak, &pc);
            c320_client_done("IntroStart refused, GameStarted already called");
        }
        return;
    }
    g_weak_assign(&g_c135.pc_weak, &pc);
    snprintf(g_c135.op_map, sizeof(g_c135.op_map), "%s", map);
    g_c135.op_from_save = from_save == 1;
    g_c135.op_chapter = chapter;
    g_c135.op_level = level;
    g_c135.op_ms = GetTickCount64();
    g_c135.unacked_ms = 0;
    g_c135.restarts = 0;
    g_c135.pending = true;
    log::line("card135: client t=%llu IntroStart (%s, bFromSave %d, chapter %d level %d) in: waiting for the HUD, pawn "
              "and the card 156 map-load restore",
              static_cast<unsigned long long>(g_c135.op_ms), g_c135.op_map, g_c135.op_from_save ? 1 : 0, chapter,
              level);
}

namespace {

// Client: the op's chapter and level IDs onto its own game instance (what the screen's Apply Data reads). Writes
// "chapter A->B level C->D" (the values before and after) or why not into out.
void c135_set_ids(void* pc, char* out, size_t size) {
    void* const gi = c135_gi(pc);
    int32_t before[2] = {-1, -1};
    int32_t after[2] = {-1, -1};
    int32_t set[2] = {g_c135.op_chapter, g_c135.op_level};
    const bool read0 = c135_gi_int(gi, g_c135_names.get_chapter, L"GetChapterID", &before[0]) &&
                       c135_gi_int(gi, g_c135_names.get_level, L"GetLevelID", &before[1]);
    const bool wrote = read0 && set[0] >= 0 && set[1] >= 0 &&
                       c135_gi_int(gi, g_c135_names.set_chapter, L"SetChapterID", &set[0]) &&
                       c135_gi_int(gi, g_c135_names.set_level, L"SetLevelID", &set[1]);
    const bool read1 = wrote && c135_gi_int(gi, g_c135_names.get_chapter, L"GetChapterID", &after[0]) &&
                       c135_gi_int(gi, g_c135_names.get_level, L"GetLevelID", &after[1]);
    const bool same = read1 && after[0] == g_c135.op_chapter && after[1] == g_c135.op_level;
    snprintf(out, size, "%s chapter %d->%d level %d->%d%s", class_name(gi).text, before[0], after[0], before[1],
             after[1],
             same      ? ""
             : !read0  ? " (WARN: a getter was not found)"
             : !wrote  ? " (WARN: not set: the op has no IDs or a setter was not found)"
                       : " (WARN: the IDs read back differ from the op)");
}

// The widget's BoolProperty `name`: 1, 0, or -1 when it is not found.
int32_t c135_bool(const void* widget, const char* name) {
    const uint8_t* const f = prop_find_prop(read_ptr(widget, kObjectClass), name);
    if (f == nullptr || !prop_bool_ok(f)) {
        return -1;
    }
    const auto* const addr = static_cast<const uint8_t*>(widget) + read_i32(f, kPropertyOffset);
    return (addr[f[kBoolByteOffset]] & f[kBoolFieldMask]) != 0 ? 1 : 0;
}

// The widget's ObjectProperty `name` (a child widget), or null.
const void* c135_child(const void* widget, const char* name) {
    const uint8_t* const f = prop_find_prop(read_ptr(widget, kObjectClass), name);
    return f != nullptr ? read_ptr(widget, static_cast<size_t>(read_i32(f, kPropertyOffset))) : nullptr;
}

// The world of a widget (its first PC's world), or null when unresolved.
void* c135_widget_world(const void* widget) {
    void* const pc0 = g_get_player_controller != nullptr ? g_get_player_controller(widget, 0) : nullptr;
    return pc0 != nullptr && g_actor_get_world != nullptr ? g_actor_get_world(pc0) : nullptr;
}

// Card 320 step 1: the world of a widget is paused, 1 or 0; -1 when unresolved.
int32_t c135_paused(const void* widget) {
    void* const world = c135_widget_world(widget);
    return world != nullptr && g_world_is_paused != nullptr ? (g_world_is_paused(world) ? 1 : 0) : -1;
}

// Both sides, every tick: once per map, the first tick the remembered screen's loading icon is hidden (ESlateVisibility
// Collapsed 1 or Hidden 2; it is SelfHitTestInvisible 4 while "LOADING..." shows), the press prompt is up. Test
// marker for the press-prompt screen shots; reads only.
// Card 323 (senior ask, b264): at the client's one-shot "screen prompt ready" row, each mech and turret pawn whose
// APawn::PlayerState is set (a seat someone holds) gets one row, so a rejoin shows the host's seat without a shot.
// Rows are capped at kC323SeatRows; the scan is bounded by kMaxLevels and kMaxLevelActors.
constexpr int32_t kC323SeatRows = 16;
void c323_seat_rows(const void* world) {
    const void* const mech = g_mech_class != nullptr ? g_mech_class() : nullptr;
    const void* const turret = g_turret_class != nullptr ? g_turret_class() : nullptr;
    if (world == nullptr || (mech == nullptr && turret == nullptr)) {
        log::line("card323: client seat seen: no world or no mech/turret class; not scanned");
        return;
    }
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    int32_t rows = 0;
    int32_t seats = 0;
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const void* const level = levels[l];
        const auto* const actors = level != nullptr ? static_cast<void* const*>(read_ptr(level, kLevelActors)) : nullptr;
        const int32_t num = level != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; actors != nullptr && a < num && a < kMaxLevelActors; ++a) {
            const void* const actor = actors[a];
            const bool seat = actor != nullptr && ((mech != nullptr && is_a(actor, mech)) ||
                                                   (turret != nullptr && is_a(actor, turret)));
            if (!seat) {
                continue;
            }
            ++seats;
            const void* const ps = read_ptr(actor, kPawnPlayerState);
            if (ps == nullptr || rows >= kC323SeatRows) {
                continue;
            }
            ++rows;
            log::line("card323: client seat seen: %s occupied (player state %s)", object_name(actor).text,
                      object_name(ps).text);
        }
    }
    log::line("card323: client seat seen: %d of %d mech/turret pawn(s) occupied", rows, seats);
}

void c135_prompt_step() {
    if (!g_c135.screen_seen || g_c135.prompt_logged || g_weak_get == nullptr) {
        return;
    }
    const void* const stack = g_weak_get(&g_c135.stack_weak);
    const void* const screen = stack != nullptr ? c135_first(stack) : nullptr;
    if (screen == nullptr || strcmp(class_name(screen).text, kC135Screen) != 0) {
        static uint32_t misses = 0; // card 411 diagnostic (temporary)
        if (misses++ % 120 == 0 && misses < 2400) {
            log::line("card411: %s prompt miss %u: stack %s first %s", g_client_session ? "client" : "host", misses,
                      stack != nullptr ? class_name(stack).text : "null",
                      screen != nullptr ? class_name(screen).text : "null");
        }
        return;
    }
    const auto* const icon = static_cast<const uint8_t*>(c135_child(screen, "WB_Loading_Icon_White"));
    const auto* const press = static_cast<const uint8_t*>(c135_child(screen, "PressToContinue"));
    const uint8_t vis = icon != nullptr ? icon[kC67WidgetVisibility] : 0;
    if (vis != 1 && vis != 2) {
        static uint32_t waits = 0; // card 411 diagnostic (temporary): why the prompt is not ready
        if (waits++ % 120 == 0 && waits < 2400) {
            log::line("card411: %s prompt wait %u: screen %s icon %p vis %u, LoadingFinished %d, Landscape %d",
                      g_client_session ? "client" : "host", waits, class_name(screen).text,
                      static_cast<const void*>(icon), vis, c135_bool(screen, "LoadingFinished"),
                      c135_bool(screen, "LandscapeLoadingFinished"));
        }
        return;
    }
    g_c135.prompt_logged = true;
    const ULONGLONG now = GetTickCount64();
    const void* const world = c135_widget_world(stack);
    log::line("card135: %s t=%llu screen prompt ready: loading icon Visibility %u, PressToContinue Visibility %d, "
              "LoadingFinished %d, LandscapeLoadingFinished %d (+%llu ms after the screen's pause); world paused %d, on %s",
              g_client_session ? "client" : "host", static_cast<unsigned long long>(now), vis,
              press != nullptr ? static_cast<int32_t>(press[kC67WidgetVisibility]) : -1,
              c135_bool(screen, "LoadingFinished"), c135_bool(screen, "LandscapeLoadingFinished"),
              static_cast<unsigned long long>(now - g_c135.screen_ms), c135_paused(stack),
              world != nullptr ? object_name(world).text : "?");
    if (g_client_session) {
        c323_seat_rows(world);
    }
}

// Client, while the op waits on the card 156 restore with the HUD and pawn live: the engine's own ClientRestart for a
// pawn the PC holds but never acknowledged. The host's ClientRestart can reach the client before the pawn actor does
// (playb71: ClientRestart with a null pawn at 418378120, the pawn 170 ms later, no AcknowledgePossession for 60 s);
// the host's world is paused by the card 320 hold, so nothing retries it, and the hold waits on this client.
void c135_restart_step(void* pc, ULONGLONG now) {
    void* const pawn = read_ptr(pc, kControllerPawn);
    if (pawn == nullptr || pawn == read_ptr(pc, kPcAcknowledgedPawn) || g_orig_client_restart == nullptr) {
        g_c135.unacked_ms = 0;
        return;
    }
    if (g_c135.unacked_ms == 0) {
        g_c135.unacked_ms = now;
        return;
    }
    if (now - g_c135.unacked_ms < kC135RestartMs || g_c135.restarts >= kC135Restarts) {
        return;
    }
    g_c135.restarts++;
    g_c135.unacked_ms = now;
    log::line("card135: client t=%llu pawn %s not acknowledged %llu ms after it came: ClientRestart call %u of %u",
              static_cast<unsigned long long>(now), class_name(pawn).text, kC135RestartMs, g_c135.restarts,
              kC135Restarts);
    client_restart_call(pc, pawn);
}

} // namespace

// Every engine tick, both sides (from cine_skip_step): the prompt-ready line. Client: the pending op's GameStarted
// once the HUD and pawn are live and the card 156 map-load restore (Set Input Mode Game at AcknowledgePossession) has
// run, so it cannot land on the screen.
void c135_step() {
    c135_prompt_step();
    c320_step(); // card 320: host hold
    if (!g_c135.pending || g_weak_get == nullptr || g_find_function == nullptr || g_orig_process_event == nullptr) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    void* const pc = g_weak_get(&g_c135.pc_weak);
    void* const world = pc != nullptr && g_actor_get_world != nullptr ? g_actor_get_world(pc) : nullptr;
    void* const hud = pc != nullptr ? read_ptr(pc, kC135PcMyHud) : nullptr;
    const bool hud_ok = hud != nullptr && strncmp(class_name(hud).text, "BP_HUD_Manager", 14) == 0;
    const bool same_map = world != nullptr && strcmp(object_name(world).text, g_c135.op_map) == 0;
    const bool pawn = pc != nullptr && read_ptr(pc, kControllerPawn) != nullptr;
    if (hud_ok && pawn && same_map && g_game_only_pending) {
        c135_restart_step(pc, now);
    }
    if (!hud_ok || !pawn || !same_map || g_game_only_pending) {
        if (now - g_c135.op_ms > kC135WaitMs) {
            g_c135.pending = false;
            log::line("WARN card135: client t=%llu GameStarted not called after %llu ms: PC %d, HUD %s, pawn %d, "
                      "map %s (op %s), card 156 restore pending %d",
                      static_cast<unsigned long long>(now), kC135WaitMs, pc != nullptr ? 1 : 0,
                      hud != nullptr ? class_name(hud).text : "none", pawn ? 1 : 0,
                      world != nullptr ? object_name(world).text : "none", g_c135.op_map, g_game_only_pending ? 1 : 0);
            c320_client_done("GameStarted not called: no screen to confirm");
        }
        return;
    }
    g_c135.pending = false;
    g_c135.started = true;
    char ids[160];
    c135_set_ids(pc, ids, sizeof(ids));
    const char* const why = c135_call(hud, g_c135.op_from_save);
    log::line("%scard135: client GameStarted t=%llu on %s (bFromSave %d, HUD %s, +%llu ms after the op; %s)%s%s; world "
              "paused %d",
              why != nullptr ? "WARN " : "", static_cast<unsigned long long>(now), g_c135.op_map,
              g_c135.op_from_save ? 1 : 0, class_name(hud).text, now - g_c135.op_ms, ids, why != nullptr ? ": " : "",
              why != nullptr ? why : "", g_world_is_paused != nullptr && g_world_is_paused(world) ? 1 : 0);
    if (why != nullptr) {
        c320_client_done("GameStarted failed: no screen to confirm");
    }
}

// Card 397, host: the game instance's chapter and level IDs (set by the frontend before this Browse) to every remote
// PC, named by the travel URL's map. Sent before ServerTravel so the client has them at its LoadMap return.
void c397_host_travel(void* world, const wchar_t* url) {
    void* const gi = world != nullptr ? read_ptr(world, kWorldOwningGameInstance) : nullptr;
    int32_t chapter = -1;
    int32_t level = -1;
    const bool got = c135_gi_int(gi, g_c135_names.get_chapter, L"GetChapterID", &chapter) &&
                     c135_gi_int(gi, g_c135_names.get_level, L"GetLevelID", &level);
    const wchar_t* base = url;
    for (const wchar_t* p = url; *p != 0 && *p != L'?'; ++p) {
        if (*p == L'/') {
            base = p + 1;
        }
    }
    char map[kC135MapChars] = {};
    for (int32_t i = 0; i < kC135MapChars - 1 && base[i] != 0 && base[i] != L'?' && base[i] < 0x7F; ++i) {
        map[i] = static_cast<char>(base[i]);
    }
    char text[kC135MapChars + 32];
    snprintf(text, sizeof(text), "%s %d %d", map, chapter, level);
    static wchar_t wire[kChanTextFrame];
    const WireString w = chan_text_frame(ChanOp::TravelIds, text, wire);
    uint32_t remote = 0;
    uint32_t sent = 0;
    for (int32_t s = 0; got && map[0] != 0 && w.num > 0 && s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        if (pc == nullptr || !c26_is_remote_pc(pc)) {
            continue;
        }
        ++remote;
        uint32_t ahead = 0;
        sent += ue::chan::chan_send_client_now(pc, w, &ahead) ? 1u : 0u;
    }
    log::line("%scard397: host t=%llu TravelIds %s %d %d to %u of %u remote PC(s)%s",
              got && sent == remote ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), map, chapter,
              level, sent, remote, got ? "" : ": a getter was not found, nothing sent");
}

// Card 397, client: the IDs onto the game instance of `world`. True when both setters ran.
bool c397_set_ids(void* world, int32_t chapter, int32_t level) {
    void* const gi = world != nullptr ? read_ptr(world, kWorldOwningGameInstance) : nullptr;
    return c135_gi_int(gi, g_c135_names.set_chapter, L"SetChapterID", &chapter) &&
           c135_gi_int(gi, g_c135_names.set_level, L"SetLevelID", &level);
}

} // namespace ubw::hooks
