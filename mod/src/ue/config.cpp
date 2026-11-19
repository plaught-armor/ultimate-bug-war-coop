#include "ue/config.h"

#include "ue/log.h"

#include <wchar.h>

namespace ue::config {

namespace log = ue::log;

EnvRead read_env(const wchar_t* name, wchar_t* buf, DWORD cap) {
    buf[0] = L'\0';
    SetLastError(ERROR_SUCCESS);
    const DWORD n = GetEnvironmentVariableW(name, buf, cap);
    const DWORD err = GetLastError();
    if (n == 0) {
        buf[0] = L'\0';
        if (err == ERROR_ENVVAR_NOT_FOUND) {
            return {EnvStatus::Unset, 0, 0};
        }
        return err == ERROR_SUCCESS ? EnvRead{EnvStatus::Empty, 0, 0} : EnvRead{EnvStatus::Error, 0, err};
    }
    if (n >= cap) {
        buf[0] = L'\0'; // truncated: n is the required size including the NUL
        return {EnvStatus::TooLong, n, 0};
    }
    return {EnvStatus::Ok, n, 0};
}

bool is_tag_char(wchar_t c) {
    return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'_' ||
           c == L'-';
}

bool parse_u32(const wchar_t* s, DWORD len, uint32_t* out) {
    if (len == 0 || len > 10) {
        return false;
    }
    uint64_t v = 0;
    for (DWORD i = 0; i < len; ++i) {
        if (s[i] < L'0' || s[i] > L'9') {
            return false;
        }
        v = v * 10u + static_cast<uint64_t>(s[i] - L'0');
    }
    if (v > 0xFFFFFFFFull) {
        return false;
    }
    *out = static_cast<uint32_t>(v);
    return true;
}

uint32_t read_delay_ms(const wchar_t* name) {
    wchar_t buf[16];
    const EnvRead r = read_env(name, buf, 16);
    if (r.status == EnvStatus::Unset) {
        return kDefaultDelayMs;
    }
    uint32_t v = 0;
    if (r.status != EnvStatus::Ok || !parse_u32(buf, r.len, &v)) {
        log::line("WARN config: %ls unparsable (status=%d, err=%lu) -- using default %u", name,
                  static_cast<int>(r.status), r.error, kDefaultDelayMs);
        return kDefaultDelayMs;
    }
    if (v > kMaxDelayMs) {
        log::line("WARN config: %ls=%u clamped to %u", name, v, kMaxDelayMs);
        return kMaxDelayMs;
    }
    return v;
}

bool read_switch(const wchar_t* name, const char* what) {
    wchar_t buf[4];
    const EnvRead r = read_env(name, buf, 4);
    if (r.status == EnvStatus::Unset) {
        log::line("config: %ls unset -- %s off", name, what);
        return false;
    }
    if (r.status == EnvStatus::Ok && r.len == 1 && buf[0] == L'1') {
        return true;
    }
    log::line("WARN config: %ls not \"1\" (status=%d, err=%lu) -- %s off", name, static_cast<int>(r.status), r.error,
              what);
    return false;
}

LogTag read_log_tag() {
    LogTag tag{};
    const EnvRead r = read_env(L"UBWCOOP_LOG_TAG", tag.text, static_cast<DWORD>(kMaxTagLen + 1));
    if (r.status == EnvStatus::Unset) {
        tag.status = TagStatus::Unset;
        return tag;
    }
    tag.status = TagStatus::Invalid;
    if (r.status != EnvStatus::Ok) {
        tag.text[0] = L'\0';
        return tag;
    }
    for (DWORD i = 0; i < r.len; ++i) {
        if (!is_tag_char(tag.text[i])) {
            tag.text[0] = L'\0';
            return tag;
        }
    }
    tag.status = TagStatus::Ok;
    return tag;
}

Travel read_travel() {
    Travel t{};
    t.delay_ms = read_delay_ms(L"UBWCOOP_TRAVEL_DELAY_MS");
    const EnvRead r = read_env(L"UBWCOOP_TRAVEL", t.url, static_cast<DWORD>(kMaxUrlLen + 1));
    switch (r.status) {
    case EnvStatus::Ok:
        t.enabled = true;
        log::line("config: UBWCOOP_TRAVEL=\"%ls\" delay=%u ms", t.url, t.delay_ms);
        break;
    case EnvStatus::Unset:
        log::line("config: UBWCOOP_TRAVEL unset -- no auto-travel");
        break;
    case EnvStatus::Empty:
        log::line("WARN config: UBWCOOP_TRAVEL is empty -- no auto-travel");
        break;
    case EnvStatus::TooLong:
        log::line("WARN config: UBWCOOP_TRAVEL needs %lu chars incl. NUL, limit %zu -- no auto-travel", r.len,
                  kMaxUrlLen + 1);
        break;
    case EnvStatus::Error:
        log::line("ERROR config: GetEnvironmentVariableW(UBWCOOP_TRAVEL) err=%lu -- no auto-travel", r.error);
        break;
    }
    return t;
}

} // namespace ue::config
