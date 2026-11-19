#include "movieskip.h"

#include "ue/log.h"
#include "winmsg.h"

#include <windows.h>

namespace ubw::movieskip {
namespace log = ue::log;
namespace {

// The global FDefaultGameMoviePlayer* (GetMoviePlayer, exe+0x036D9B90, returns it + 0x10).
constexpr uintptr_t kMoviePlayerGlobalRva = 0x93B2E78;
// IsStartupMoviePlaying (exe+0x036DB5F0) reads +0xA0 of the +0x10 interface subobject.
constexpr size_t kStartupPlaying = 0xB0;
// OnAnyDown (exe+0x036DB620) skips only when this byte is set (movies skippable).
constexpr size_t kSkippable = 0xDD;

constexpr uint32_t kPollMs = 250;
constexpr uint32_t kMaxPolls = 360;       // 90 s ceiling from start()
constexpr uint32_t kDonePolls = 12;       // 3 s not playing after a movie was seen: done
constexpr uint32_t kPressEveryPolls = 2;  // one press each 500 ms while playing

const uint8_t* g_exe_base = nullptr; // set once by start()

// Posts Enter down/up and a left click at the window's top-left quarter. False when a post fails.
bool press(const winmsg::User32& u, HWND window) {
    const LPARAM key_down = 1 | (static_cast<LPARAM>(0x1C) << 16);
    const LPARAM key_up = key_down | (static_cast<LPARAM>(0xC0000000u));
    const LPARAM at = MAKELPARAM(160, 90);
    return u.post(window, WM_KEYDOWN, VK_RETURN, key_down) != 0 && u.post(window, WM_KEYUP, VK_RETURN, key_up) != 0 &&
           u.post(window, WM_LBUTTONDOWN, MK_LBUTTON, at) != 0 && u.post(window, WM_LBUTTONUP, 0, at) != 0;
}

// Null until the movie player exists.
const uint8_t* movie_player() {
    return *reinterpret_cast<const uint8_t* const volatile*>(g_exe_base + kMoviePlayerGlobalRva);
}

struct Progress {
    uint32_t last_state;  // log_state() key of the previous poll
    uint32_t movie_polls; // polls with the startup movie playing
    uint32_t presses;
    uint32_t idle_after;  // consecutive polls not playing since the last playing one
};

// Logs the player pointer state and its flag bytes (+0xA8 user-finish, +0xB0 startup playing, +0xB1)
// whenever they change, so the log shows when the movie player exists and plays.
void log_state(const uint8_t* mp, Progress* p) {
    const uint32_t state = mp == nullptr ? 0xFFFFFFFFu
                                         : static_cast<uint32_t>(*(mp + 0xA8)) | (static_cast<uint32_t>(*(mp + 0xB0)) << 8) |
                                               (static_cast<uint32_t>(*(mp + 0xB1)) << 16);
    if (state == p->last_state) {
        return;
    }
    p->last_state = state;
    if (mp == nullptr) {
        log::line("movieskip: no movie player yet");
        return;
    }
    log::line("movieskip: player %p flags +0xA8=%u +0xB0=%u +0xB1=%u", mp, state & 0xFFu, (state >> 8) & 0xFFu,
              (state >> 16) & 0xFFu);
}

// One poll. True when the skip is finished (movies seen, then not playing for kDonePolls).
bool poll(const winmsg::User32& u, Progress* p) {
    const uint8_t* const mp = movie_player();
    log_state(mp, p);
    const bool playing = mp != nullptr && *reinterpret_cast<const volatile uint8_t*>(mp + kStartupPlaying) != 0;
    if (!playing) {
        p->idle_after += p->movie_polls > 0 ? 1u : 0u;
        return p->idle_after >= kDonePolls;
    }
    p->idle_after = 0;
    if (p->movie_polls++ == 0) {
        log::line("movieskip: startup movie playing (player %p, skippable byte %u) -- pressing Enter + click", mp,
                  static_cast<unsigned>(*(mp + kSkippable)));
    }
    if (p->movie_polls % kPressEveryPolls != 1) {
        return false;
    }
    const HWND window = winmsg::own_window(u);
    if (window == nullptr || !press(u, window)) {
        log::line("WARN movieskip: press %u not sent (window %p, err=%lu)", p->presses + 1,
                  static_cast<void*>(window), GetLastError());
        return false;
    }
    ++p->presses;
    return false;
}

DWORD WINAPI worker(LPVOID) {
    winmsg::User32 u{};
    if (!winmsg::resolve(&u, "movieskip", "no skip")) {
        return 1;
    }
    Progress p{};
    p.last_state = 0xFFFFFFFEu; // matches no state: the first poll logs
    const ULONGLONG t0 = GetTickCount64();
    for (uint32_t i = 0; i < kMaxPolls; ++i) {
        if (poll(u, &p)) {
            log::line("movieskip: done after %llu ms -- %u presses, movie seen for %u polls", GetTickCount64() - t0,
                      p.presses, p.movie_polls);
            return 0;
        }
        Sleep(kPollMs);
    }
    log::line("%smovieskip: stopped at the %u ms ceiling -- %u presses, movie seen for %u polls",
              p.movie_polls == 0 ? "WARN " : "", kMaxPolls * kPollMs, p.presses, p.movie_polls);
    return 0;
}

} // namespace

bool start(const uint8_t* exe_base) {
    g_exe_base = exe_base;
    const HANDLE t = CreateThread(nullptr, 0, &worker, nullptr, 0, nullptr);
    if (t == nullptr) {
        log::line("ERROR movieskip: CreateThread failed, err=%lu -- no skip", GetLastError());
        return false;
    }
    CloseHandle(t);
    log::line("movieskip: polling the startup movie every %u ms for at most %u ms", kPollMs, kMaxPolls * kPollMs);
    return true;
}

} // namespace ubw::movieskip
