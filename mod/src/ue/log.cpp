#include "ue/log.h"

#include <windows.h>

#include <stdarg.h>
#include <stdio.h>

namespace ue::log {
namespace {

HANDLE g_file = INVALID_HANDLE_VALUE;
SRWLOCK g_lock = SRWLOCK_INIT;

const wchar_t* g_stem = L"UBWCoop"; // card 578: ubwperf.dll standalone opens "ubwperf"
SinkFn g_sink = nullptr;            // card 578: ubwperf.dll in co-op writes through the co-op log
constexpr wchar_t kLogExt[] = L".log";
constexpr size_t kMaxNameLen = 64; // "UBWCoop-" + 16-char tag + "-YYYYMMDD-HHMMSS" + ".log" fits with room

// Appends src (NUL-terminated) to dst at *pos. False if it would not fit with a NUL.
bool append(wchar_t* dst, size_t cap, size_t* pos, const wchar_t* src) {
    for (size_t i = 0; i < cap; ++i) {
        if (src[i] == L'\0') {
            dst[*pos] = L'\0';
            return true;
        }
        if (*pos + 1 >= cap) {
            return false;
        }
        dst[(*pos)++] = src[i];
    }
    return false;
}

// "UBWCoop<ext>", or "UBWCoop-<tag><ext>" when tag is non-null and non-empty.
bool build_name(const wchar_t* tag, const wchar_t* ext, wchar_t* name, size_t cap) {
    size_t pos = 0;
    const bool tagged = tag != nullptr && tag[0] != L'\0';
    return append(name, cap, &pos, g_stem) && (!tagged || append(name, cap, &pos, L"-")) &&
           (!tagged || append(name, cap, &pos, tag)) && append(name, cap, &pos, ext);
}

} // namespace

bool path_next_to_exe(const wchar_t* tag, const wchar_t* ext, wchar_t* path, size_t cap) {
    wchar_t name[kMaxNameLen];
    if (!build_name(tag, ext, name, kMaxNameLen) || cap > MAXDWORD) {
        return false;
    }
    const DWORD len = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(cap));
    if (len == 0 || len >= cap) {
        return false; // failed or truncated
    }
    // Cut after the last backslash; the exe always has a directory component.
    DWORD cut = len;
    while (cut > 0 && path[cut - 1] != L'\\' && path[cut - 1] != L'/') {
        --cut;
    }
    size_t pos = cut;
    return cut != 0 && append(path, cap, &pos, name);
}

bool open_next_to_exe(const wchar_t* tag, const wchar_t* stem) {
    g_stem = stem;
    // Card 130 item 16: one log per launch. The name carries the local start time, so a relaunch never truncates
    // the last session's log: UBWCoop-<tag>-YYYYMMDD-HHMMSS.log (UBWCoop-YYYYMMDD-HHMMSS.log untagged).
    SYSTEMTIME t;
    GetLocalTime(&t);
    const wchar_t stamp[] = {
        static_cast<wchar_t>(L'0' + t.wYear / 1000 % 10), static_cast<wchar_t>(L'0' + t.wYear / 100 % 10),
        static_cast<wchar_t>(L'0' + t.wYear / 10 % 10),   static_cast<wchar_t>(L'0' + t.wYear % 10),
        static_cast<wchar_t>(L'0' + t.wMonth / 10),       static_cast<wchar_t>(L'0' + t.wMonth % 10),
        static_cast<wchar_t>(L'0' + t.wDay / 10),         static_cast<wchar_t>(L'0' + t.wDay % 10),
        L'-',
        static_cast<wchar_t>(L'0' + t.wHour / 10),        static_cast<wchar_t>(L'0' + t.wHour % 10),
        static_cast<wchar_t>(L'0' + t.wMinute / 10),      static_cast<wchar_t>(L'0' + t.wMinute % 10),
        static_cast<wchar_t>(L'0' + t.wSecond / 10),      static_cast<wchar_t>(L'0' + t.wSecond % 10),
        L'\0'};
    wchar_t stamped[kMaxNameLen];
    size_t pos = 0;
    const bool tagged = tag != nullptr && tag[0] != L'\0';
    if (!(!tagged || (append(stamped, kMaxNameLen, &pos, tag) && append(stamped, kMaxNameLen, &pos, L"-"))) ||
        !append(stamped, kMaxNameLen, &pos, stamp)) {
        return false;
    }
    wchar_t path[MAX_PATH];
    if (!path_next_to_exe(stamped, kLogExt, path, MAX_PATH)) {
        return false; // nowhere to log
    }
    const HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    g_file = h;
    return true;
}

namespace {

// Formats the "[ms][T]" prefix, the body and a newline into buf; returns the byte count (0 on failure).
size_t format_line(char* buf, size_t cap, const char* fmt, va_list ap) {
    const int head = snprintf(buf, cap, "[%10llu ms][T%5lu] ", static_cast<unsigned long long>(GetTickCount64()),
                              GetCurrentThreadId());
    if (head < 0 || static_cast<size_t>(head) >= cap) {
        return 0;
    }
    const int body = vsnprintf(buf + head, cap - static_cast<size_t>(head), fmt, ap);
    size_t n = static_cast<size_t>(head);
    if (body < 0) {
        n += static_cast<size_t>(snprintf(buf + head, cap - static_cast<size_t>(head), "<format error>"));
    } else {
        // vsnprintf truncates; keep room for the newline.
        const size_t room = cap - 2 - static_cast<size_t>(head);
        n += static_cast<size_t>(body) < room ? static_cast<size_t>(body) : room;
    }
    buf[n++] = '\n';
    return n;
}

volatile LONG64 g_line_ticks = 0; // card 252 step 1b, card 330: running total
volatile LONG64 g_line_count = 0; // card 330: lines written, running total

// Card 252 step 1b: adds the QueryPerformanceCounter span of one line() to g_line_ticks when it leaves scope.
struct LineTimer {
    LARGE_INTEGER t0;
    LineTimer() { QueryPerformanceCounter(&t0); }
    ~LineTimer() {
        LARGE_INTEGER t1;
        QueryPerformanceCounter(&t1);
        InterlockedAdd64(&g_line_ticks, t1.QuadPart - t0.QuadPart);
    }
    LineTimer(const LineTimer&) = delete;
    LineTimer& operator=(const LineTimer&) = delete;
};

} // namespace

void line_totals(int64_t* ticks, int64_t* lines) {
    *ticks = InterlockedCompareExchange64(&g_line_ticks, 0, 0);
    *lines = InterlockedCompareExchange64(&g_line_count, 0, 0);
}

void line(const char* fmt, ...) {
    if (g_file == INVALID_HANDLE_VALUE && g_sink == nullptr) {
        return;
    }
    const LineTimer timer;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    const size_t n = format_line(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n == 0) {
        return;
    }
    InterlockedIncrement64(&g_line_count);
    if (g_sink != nullptr) {
        g_sink(buf, n);
        return;
    }
    write_raw(buf, n);
}

void write_raw(const char* text, size_t n) {
    if (g_file == INVALID_HANDLE_VALUE || n == 0 || n > MAXDWORD) {
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    DWORD written = 0;
    // A failed log write cannot itself be logged; there is no other sink.
    (void)WriteFile(g_file, text, static_cast<DWORD>(n), &written, nullptr);
    ReleaseSRWLockExclusive(&g_lock);
}

void set_sink(SinkFn sink) {
    g_sink = sink;
}

void crash_line(const char* fmt, ...) {
    if (g_file == INVALID_HANDLE_VALUE) {
        return;
    }
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    const size_t n = format_line(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n == 0) {
        return;
    }
    // The faulting thread may itself hold the lock (a fault inside line()); a crash line written without the
    // lock can interleave with another thread's line, which is better than a deadlock in the crash path.
    const bool locked = TryAcquireSRWLockExclusive(&g_lock) != 0;
    DWORD written = 0;
    (void)WriteFile(g_file, buf, static_cast<DWORD>(n), &written, nullptr);
    if (locked) {
        ReleaseSRWLockExclusive(&g_lock);
    }
}

void flush() {
    if (g_file == INVALID_HANDLE_VALUE) {
        return;
    }
    // Best effort: the process may be about to die; there is nothing to do on failure.
    (void)FlushFileBuffers(g_file);
}

bool note_skip(uint32_t& count, const char* what) {
    if (count < kMaxLoggedSkips) {
        ++count;
        return true;
    }
    if (count == kMaxLoggedSkips) {
        ++count;
        line("guard: %s ... further skips not logged", what);
    }
    return false;
}

bool note_event_cap(uint32_t& count, uint32_t cap, const char* what) {
    if (count < cap) {
        ++count;
        return true;
    }
    if (count == cap) {
        ++count;
        line("diag: %s ... further events not logged", what);
    }
    return false;
}

bool note_event(uint32_t& count, const char* what) {
    return note_event_cap(count, kMaxLoggedEvents, what);
}

} // namespace ue::log
