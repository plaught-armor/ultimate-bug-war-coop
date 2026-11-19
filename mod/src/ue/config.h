#pragma once

// Per-instance settings from environment variables. Two game instances share one exe
// directory, so nothing per-instance can live in a file next to the exe.
//   UBWCOOP_LOG_TAG         log file becomes UBWCoop-<tag>-YYYYMMDD-HHMMSS.log (1-16 chars of [A-Za-z0-9_-])
//   UBWCOOP_TRAVEL          URL handed to UEngine::SetClientTravel; unset = no travel
//   UBWCOOP_TRAVEL_DELAY_MS delay after the first Tick with a game world (default 5000)
// The environment primitives below are public so the game-side readers can share them.

#include <stddef.h>
#include <stdint.h>

#include <windows.h>

namespace ue::config {

inline constexpr size_t kMaxTagLen = 16;
inline constexpr size_t kMaxUrlLen = 512;
inline constexpr uint32_t kDefaultDelayMs = 5000;
inline constexpr uint32_t kMaxDelayMs = 600000;

enum class EnvStatus { Unset, Ok, Empty, TooLong, Error };

struct EnvRead {
    EnvStatus status;
    DWORD len;   // chars excluding NUL when Ok; required size when TooLong
    DWORD error; // GetLastError() when Error
};

// GetEnvironmentVariableW with every return case told apart. buf gets a NUL-terminated
// value only when status == Ok; otherwise buf[0] is NUL.
EnvRead read_env(const wchar_t* name, wchar_t* buf, DWORD cap);

// True for [A-Za-z0-9_-], the characters of a log tag and of the other name-like values.
bool is_tag_char(wchar_t c);

// Parses an unsigned decimal of at most 10 digits. False on empty, non-digit or overflow.
bool parse_u32(const wchar_t* s, DWORD len, uint32_t* out);

// Reads a delay in ms: unset = kDefaultDelayMs, unparsable = kDefaultDelayMs (logged), clamped to kMaxDelayMs
// (logged).
uint32_t read_delay_ms(const wchar_t* name);

// Reads a "1"-only switch; logs unset and invalid values with `what` (the effect when on).
bool read_switch(const wchar_t* name, const char* what);

enum class TagStatus { Unset, Ok, Invalid };

struct LogTag {
    wchar_t text[kMaxTagLen + 1]; // NUL-terminated; empty unless status == Ok
    TagStatus status;
};

struct Travel {
    wchar_t url[kMaxUrlLen + 1]; // NUL-terminated; empty unless enabled
    uint32_t delay_ms;
    bool enabled;
};

// Reads and validates UBWCOOP_LOG_TAG. Does not log (runs before the log is open).
// Safe under the loader lock (kernel32 only).
LogTag read_log_tag();

// Reads UBWCOOP_TRAVEL / UBWCOOP_TRAVEL_DELAY_MS and logs the resulting values and any
// parse/length failure.
Travel read_travel();

} // namespace ue::config
