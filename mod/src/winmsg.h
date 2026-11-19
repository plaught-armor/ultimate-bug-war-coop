#pragma once

// The user32 calls that post window messages to the game's own window: the movie skip (movieskip.cpp) and the card
// 222 test driver `winkey` (ubw/testrun.cpp). user32 is resolved through GetProcAddress, never imported, so the proxy
// DLL keeps kernel32 as its only import.

#include <windows.h>

namespace ubw::winmsg {

using EnumWindowsFn = BOOL(WINAPI*)(WNDENUMPROC fn, LPARAM param);
using WindowPidFn = DWORD(WINAPI*)(HWND window, DWORD* pid);
using IsWindowVisibleFn = BOOL(WINAPI*)(HWND window);
using PostMessageFn = BOOL(WINAPI*)(HWND window, UINT msg, WPARAM w, LPARAM l);

struct User32 {
    EnumWindowsFn enum_windows;
    WindowPidFn window_pid;
    IsWindowVisibleFn visible;
    PostMessageFn post;
};

// Fills u from the loaded user32.dll. On failure logs "ERROR <who>: ... -- <effect>" and returns false.
bool resolve(User32* u, const char* who, const char* effect);

// This process's first visible top-level window (the game window), or null.
HWND own_window(const User32& u);

} // namespace ubw::winmsg
