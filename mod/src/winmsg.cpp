#include "winmsg.h"

#include "ue/log.h"

namespace ubw::winmsg {
namespace log = ue::log;
namespace {

struct Find {
    const User32* u;
    HWND found;
};

BOOL CALLBACK on_window(HWND window, LPARAM param) {
    Find* const f = reinterpret_cast<Find*>(param);
    DWORD pid = 0;
    f->u->window_pid(window, &pid);
    if (pid == GetCurrentProcessId() && f->u->visible(window)) {
        f->found = window;
        return FALSE;
    }
    return TRUE;
}

} // namespace

bool resolve(User32* u, const char* who, const char* effect) {
    const HMODULE m = GetModuleHandleW(L"user32.dll");
    if (m == nullptr) {
        log::line("ERROR %s: GetModuleHandleW(user32.dll) failed, err=%lu -- %s", who, GetLastError(), effect);
        return false;
    }
    u->enum_windows = reinterpret_cast<EnumWindowsFn>(reinterpret_cast<void*>(GetProcAddress(m, "EnumWindows")));
    u->window_pid =
        reinterpret_cast<WindowPidFn>(reinterpret_cast<void*>(GetProcAddress(m, "GetWindowThreadProcessId")));
    u->visible = reinterpret_cast<IsWindowVisibleFn>(reinterpret_cast<void*>(GetProcAddress(m, "IsWindowVisible")));
    u->post = reinterpret_cast<PostMessageFn>(reinterpret_cast<void*>(GetProcAddress(m, "PostMessageW")));
    if (u->enum_windows == nullptr || u->window_pid == nullptr || u->visible == nullptr || u->post == nullptr) {
        log::line("ERROR %s: a user32 export is missing -- %s", who, effect);
        return false;
    }
    return true;
}

HWND own_window(const User32& u) {
    Find f{&u, nullptr};
    u.enum_windows(&on_window, reinterpret_cast<LPARAM>(&f));
    return f.found;
}

} // namespace ubw::winmsg
