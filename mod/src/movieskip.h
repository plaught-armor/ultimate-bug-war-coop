#pragma once

// Startup movie skip (card 135: UBWCOOP_TEST_SKIP=1, tests only): presses through the startup splash movies so test runs
// reach the menu and travel sooner. A worker thread polls the movie player's "startup movie playing"
// byte and, only while it is set, posts Enter and a left click to this process's own game window;
// the movie player's skip handler (FDefaultGameMoviePlayer::OnAnyDown) takes any key or mouse button.

#include <stdint.h>

namespace ubw::movieskip {

// Starts the polling thread. Call once, after ubw::init has verified the exe build (the global
// RVA below is build-specific). Logs and returns false when the thread cannot start.
bool start(const uint8_t* exe_base);

} // namespace ubw::movieskip
