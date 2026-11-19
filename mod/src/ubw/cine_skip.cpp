// Card 130: no cinematic ever plays, test or play build, host or client. The mission brief and the in-mission movie
// cutscenes are WB_Cutscenes_C widgets pushed onto a WB_Stack_C, which then calls RequestGamePause (both sides: the
// pause interface is the local game instance's). From that call the stack is remembered; every engine tick its top
// widget is read, and a WB_Cutscenes_C top gets the card 067 skip (On Video Ended, Skipped=true: the function the
// game's own Enter path in OnKeyDown calls). A WB_Loading_Screen_Final_C top that follows a skip on the same stack
// (the brief's any-key screen) gets the card 067 close. The skip is the local key path each player has, so each side
// skips its own copy; nothing is sent. A top still there kCineRetryMs after a call is called again, kCineTries total.
//
// Level-sequence cinematics (FED_C2M1_Intro, LS_DropshipCrash, ...): every UMovieSceneSequencePlayer::Play is logged
// (kSeqLogs per process); a player whose sequence package lies under a kCineSeqDirs folder is queued and, on the
// next tick, gets its own GoToEndAndStop through ProcessEvent: the skip path the game uses, which fires OnFinished so
// the level script goes on. Gameplay sequences elsewhere (doors, lifts) are left alone. Both sides skip their own
// player; the host's replicated playback state ends there too.

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

namespace {

constexpr ULONGLONG kCineRetryMs = 1000;
constexpr uint32_t kCineTries = 3;
constexpr const char* kCineLevelEnd[] = {"IsLevelEndCutscene", nullptr};

struct CineSkip {
    uint64_t stack_weak;   // FWeakObjectPtr: the last WB_Stack_C pause requester
    ULONGLONG pause_ms;    // when it requested the pause
    const void* top;       // the top widget last called on
    int32_t top_id;        // its object index (a reused address is a new widget)
    uint32_t tries;        // calls on that top
    ULONGLONG next_ms;     // a repeat call waits until then
    bool skipped;          // a cutscene on this stack was skipped: its loading screen may be closed
    bool travel;           // card 393: `top` was a level-end cutscene and its skip ran: no repeat call on it
    uint32_t total;        // skips this process
};
CineSkip g_cine{};

constexpr uint32_t kSeqQueue = 8;
constexpr uint32_t kSeqLogs = 32;
constexpr uint32_t kSeqOuterMax = 16;  // outer chain walk bound (player -> actor -> level -> world -> package)
constexpr const char* kCineSeqDirs[] = {"/Sequences/", "/Movies/", "/Cinematics/"};

struct SeqSkip {
    void* player[kSeqQueue];
    C31Weak weak[kSeqQueue]; // liveness (card 319 run 1 class fix: no object_index on a maybe-freed pointer)
    uint32_t count;
    uint32_t logs;
    uint64_t fn_name;  // FName cache: GoToEndAndStop
};
SeqSkip g_seq{};

// The outermost object's name (the package path) of `object`, or "?".
ObjectName seq_package(const void* object) {
    const void* o = object;
    for (uint32_t i = 0; i < kSeqOuterMax && o != nullptr && read_ptr(o, kObjectOuter) != nullptr; ++i) {
        o = read_ptr(o, kObjectOuter);
    }
    return o != nullptr ? object_name(o) : ObjectName{"?"};
}

bool seq_is_cinematic(const char* package) {
    for (const char* dir : kCineSeqDirs) {
        if (strstr(package, dir) != nullptr) {
            return true;
        }
    }
    return false;
}

// One queued player: GoToEndAndStop, logged. A player freed since Play (weak ref stale) is dropped with a line.
void seq_skip(void* player, const C31Weak& weak) {
    if (c31_weak_get(weak) != player) {
        log::line("card130: %s level sequence player %p gone before its skip", side(), player);
        return;
    }
    const void* const seq = c31_object_prop(player, "Sequence");
    const ObjectName pkg = seq_package(seq);
    void* const fn = fname_cached(g_seq.fn_name, L"GoToEndAndStop") != 0 ? g_find_function(player, g_seq.fn_name) : nullptr;
    if (fn == nullptr) {
        log::line("WARN card130: %s cinematic skip of LevelSequence %s failed: %s has no GoToEndAndStop", side(),
                  pkg.text, class_name(player).text);
        return;
    }
    alignas(16) uint8_t parms[16] = {};
    g_orig_process_event(player, fn, parms);
    ++g_cine.total;
    log::line("card130: %s cinematic skipped LevelSequence %s (player %p, GoToEndAndStop the tick after Play, %u this "
              "process)",
              side(), pkg.text, player, g_cine.total);
}

void seq_step() {
    const uint32_t n = g_seq.count;
    g_seq.count = 0;
    for (uint32_t i = 0; i < n && i < kSeqQueue; ++i) {
        seq_skip(g_seq.player[i], g_seq.weak[i]);
    }
}

// One call of card 067 step `step` on `top`, with the log line. Returns false when no further call is due.
bool cine_call(void* top, int32_t step, ULONGLONG now) {
    const int32_t id = object_index(top);
    const bool same = top == g_cine.top && id == g_cine.top_id;
    // Card 393: a skipped level-end cutscene starts the travel to the frontend and stays the top widget through
    // the transition world; a repeat call there started a second travel (run c391b79).
    if (same && (now < g_cine.next_ms || g_cine.tries >= kCineTries || g_cine.travel)) {
        return false;
    }
    g_cine.travel = false;
    g_cine.tries = same ? g_cine.tries + 1 : 1;
    g_cine.top = top;
    g_cine.top_id = id;
    g_cine.next_ms = now + kCineRetryMs;
    const char* const why = c67_ui_call(top, step);
    const ObjectName name = class_name(top);
    const char* const cls = name.text;
    if (why != nullptr) {
        log::line("WARN card130: %s cinematic skip of %s failed: %s", side(), cls, why);
        return false;
    }
    if (step == 0) {
        ++g_cine.total;
        g_cine.skipped = true;
        g_cine.travel = c29_bool(top, kCineLevelEnd) == 1;
        log::line("card130: %s cinematic skipped %s %p (try %u, IsLevelEndCutscene %d, first tick it is the top widget; +%llu ms after the pause "
                  "request, %u this process)",
                  side(), cls, top, g_cine.tries, c29_bool(top, kCineLevelEnd), now - g_cine.pause_ms, g_cine.total);
    } else {
        log::line("card130: %s cinematic loading screen %s %p closed (try %u)", side(), cls, top, g_cine.tries);
    }
    if (g_cine.tries == kCineTries) {
        log::line("WARN card130: %s %s %p still the top widget after %u calls", side(), cls, top, kCineTries);
    }
    return true;
}

} // namespace

SequencePlayFn g_orig_sequence_play = nullptr;

void detour_sequence_play(void* player) {
    g_orig_sequence_play(player);
    const void* const seq = c31_object_prop(player, "Sequence");
    const ObjectName pkg = seq_package(seq);
    const bool cine = seq_is_cinematic(pkg.text);
    const bool queued = cine && config::test_skip() && g_seq.count < kSeqQueue && g_find_function != nullptr && g_orig_process_event != nullptr;
    if (queued) {
        g_seq.player[g_seq.count] = player;
        c31_weak_set(g_seq.weak[g_seq.count], player);
        ++g_seq.count;
    }
    if (g_seq.logs < kSeqLogs || (cine && !queued && config::test_skip())) {
        ++g_seq.logs;
        log::line("%scard130: %s level sequence Play: %s (%s, player %p %s, caller exe+0x%llX): %s", cine && !queued && config::test_skip() ? "WARN " : "",
                  side(), pkg.text, object_name(seq).text, player, class_name(player).text,
                  static_cast<unsigned long long>(exe_rva(_ReturnAddress())),
                  queued ? "cinematic, skipped next tick" : cine && !config::test_skip() ? "cinematic, play build: left to the player" : cine ? "cinematic, queue full or no ProcessEvent: not skipped" : "not a cinematic folder, left to play");
    }
}

// From detour_request_pause (always hooked), after the game's call; `requester` is live (it is the caller).
void cine_note_pause(void* requester) {
    if (g_weak_assign == nullptr || strcmp(class_name(requester).text, "WB_Stack_C") != 0) {
        return;
    }
    g_weak_assign(&g_cine.stack_weak, &requester);
    g_cine.pause_ms = GetTickCount64();
    g_cine.skipped = false;
    c135_note_pause(requester); // card 135: both sides, the press-to-start screen's stack
}

// Card 225: true while the last WB_Stack_C pause requester is live and holds a widget (the press-to-start screen,
// the brief video, a movie cutscene, a menu). Both sides, play build too: no config test.
bool cine_stack_open() {
    if (g_cine.stack_weak == 0 || g_weak_get == nullptr) {
        return false;
    }
    void* const stack = g_weak_get(&g_cine.stack_weak);
    int32_t num = 0;
    return stack != nullptr && c67_ui_top(stack, &num) != nullptr && num > 0;
}

// Every UGameEngine::Tick, host and client.
void cine_skip_step() {
    // b226 card 330: each part timed on its own (the client's cine_skip_step slow rows were 394-399 ms).
    C330_STEP(c135_step()); // card 135: prompt-ready line both sides, client GameStarted, play build too
    C330_STEP(c337_step()); // card 337: client world rendering on at its title screen, play build too
    C330_STEP(c362_step()); // card 362: client InstaKill skips with no Down frame in 3 s, play build too
    if (!config::test_skip()) { // card 135: the play build skips nothing; each player skips by the game's button
        return;
    }
    C330_STEP(seq_step());
    if (g_cine.stack_weak == 0 || g_weak_get == nullptr || g_find_function == nullptr ||
        g_orig_process_event == nullptr) {
        return;
    }
    void* const stack = g_weak_get(&g_cine.stack_weak);
    int32_t num = 0;
    void* const top = stack != nullptr ? c67_ui_top(stack, &num) : nullptr;
    if (stack == nullptr || num == 0) {
        g_cine.stack_weak = stack != nullptr ? g_cine.stack_weak : 0; // freed with its map
        return;
    }
    const int32_t step = top != nullptr ? c67_ui_step_of(top) : kC67UiStepCount;
    // Card 135: UBWCOOP_C135_KEEP_SCREEN keeps the press-to-start screen up after the skipped brief.
    if (step == 0 || (step == 1 && g_cine.skipped && !config::c135_keep_screen())) {
        C330_STEP(cine_call(top, step, GetTickCount64()));
    }
}

} // namespace ubw::hooks
