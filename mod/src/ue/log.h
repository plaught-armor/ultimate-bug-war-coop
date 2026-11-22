#pragma once

// Plain-file logger: UBWCoop[-<tag>]-YYYYMMDD-HHMMSS.log next to the host exe, one per launch. Thread-safe.
// The shipping game writes no log of its own, so this is our only runtime trace.

#include <stddef.h>
#include <stdint.h>

namespace ue::log {

// Creates UBWCoop-YYYYMMDD-HHMMSS.log, or UBWCoop-<tag>-YYYYMMDD-HHMMSS.log when tag is non-null and non-empty
// (caller validates the tag; the time is the local launch time, card 130 item 16). Returns false (and logging stays a no-op) on failure.
// Safe to call under the loader lock (kernel32 only).
bool open_next_to_exe(const wchar_t* tag, const wchar_t* stem = L"UBWCoop");

// Writes the full path of "UBWCoop<ext>" (or "UBWCoop-<tag><ext>") next to the exe into path.
// False when it does not fit in cap or the exe path cannot be read. Kernel32 only.
bool path_next_to_exe(const wchar_t* tag, const wchar_t* ext, wchar_t* path, size_t cap);

// Card 578: a line writer that replaces this module's file. ubwperf.dll loaded by the co-op dll sets the co-op
// dll's write_raw, so both dlls write one log under one lock. Set once, before the first line from that module.
using SinkFn = void (*)(const char* text, size_t n);
void set_sink(SinkFn sink);
// Writes n bytes of an already formatted line (prefix and newline included) to the file, under the log lock.
void write_raw(const char* text, size_t n);

// printf-style line; a timestamp + thread id prefix and a newline are added.
void line(const char* fmt, ...)
#if defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

// Crash-path variant of line() (card 118): a 512-byte stack buffer, never waits for the log lock (writes
// without it when another holder has it) and allocates nothing. For exception handlers and fatal-error hooks.
void crash_line(const char* fmt, ...)
#if defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

// Card 252 step 1b, card 330: running totals since start of the QueryPerformanceCounter ticks spent in line() (format
// plus write, every thread) and of the lines written; each reader keeps the totals it last saw.
void line_totals(int64_t* ticks, int64_t* lines);

// FlushFileBuffers on the log file (card 118): makes crash lines survive an immediate process exit.
void flush();

constexpr uint32_t kMaxLoggedSkips = 8;
constexpr uint32_t kMaxLoggedEvents = 16;

// Counts one guard skip. Returns true while the caller should log the skip's details (the first
// kMaxLoggedSkips); the next skip logs "<what> ... further skips not logged" once, after which
// the counter saturates at kMaxLoggedSkips + 1 and nothing more is logged.
bool note_skip(uint32_t& count, const char* what);

// Counts one diagnostic event; true while the caller should log it (the first `cap`),
// then logs "<what> ... further events not logged" once and saturates.
bool note_event_cap(uint32_t& count, uint32_t cap, const char* what);

// note_event_cap with cap kMaxLoggedEvents.
bool note_event(uint32_t& count, const char* what);

} // namespace ue::log
