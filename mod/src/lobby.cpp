#include "lobby.h"

#include "ue/log.h"

#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

namespace ubw::lobby {
namespace log = ue::log;
namespace {

constexpr size_t kPathChars = 300;
constexpr DWORD kIniMaxBytes = 512;
constexpr ULONG kAdapterBufBytes = 32 * 1024;
constexpr int kMaxAdapters = 64;
constexpr int kMaxUnicast = 16;
constexpr wchar_t kIniTail[] = L"\\StarshipTroopers_UBW\\Saved\\UBWCoop.ini";

using GetAdaptersAddressesFn = ULONG(WINAPI*)(ULONG family, ULONG flags, void* reserved,
                                              IP_ADAPTER_ADDRESSES* out, ULONG* size);

// Reads up to 3 decimal digits at s[*i]; false when none or the value is above `max`.
bool parse_num(const wchar_t* s, size_t* i, int digits, uint32_t max, uint32_t* out) {
    uint32_t v = 0;
    int n = 0;
    for (; n < digits && s[*i] >= L'0' && s[*i] <= L'9'; ++n, ++*i) {
        v = v * 10u + static_cast<uint32_t>(s[*i] - L'0');
    }
    *out = v;
    return n > 0 && v <= max && !(s[*i] >= L'0' && s[*i] <= L'9');
}

bool ini_path(wchar_t (&path)[kPathChars]) {
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, static_cast<DWORD>(kPathChars));
    const size_t tail = wcslen(kIniTail);
    if (n == 0 || n + tail >= kPathChars) {
        log::line("WARN card052: LOCALAPPDATA unusable (len %lu, err %lu) -- lobby settings not saved or read", n,
                  GetLastError());
        return false;
    }
    wcscpy_s(path + n, kPathChars - n, kIniTail);
    return true;
}

// One "key=value" line into `s`; false when the key is unknown or the value invalid.
bool parse_line(const char* line, size_t len, Settings* s, bool* seen_addr, bool* seen_host) {
    if (len == 8 && strncmp(line, "address=", 8) == 0) { // saved before any join: no address yet
        s->address[0] = L'\0';
        *seen_addr = true;
        return true;
    }
    if (len > 8 && strncmp(line, "address=", 8) == 0 && len - 8 < kAddrChars) {
        wchar_t w[kAddrChars] = {};
        for (size_t i = 8; i < len; ++i) {
            w[i - 8] = static_cast<wchar_t>(static_cast<unsigned char>(line[i]));
        }
        *seen_addr = parse_address(w, s->address);
        return *seen_addr;
    }
    if (len == 6 && strncmp(line, "host=", 5) == 0 && (line[5] == '0' || line[5] == '1')) {
        s->host = line[5] == '1';
        *seen_host = true;
        return true;
    }
    if (len == 9 && strncmp(line, "stamina=", 8) == 0 && (line[8] == '0' || line[8] == '1')) { // card 490, optional
        s->stamina = line[8] == '1';
        return true;
    }
    return false;
}

// Parses the whole file text; false (logged by the caller) on any bad or missing line.
bool parse_ini(const char* text, DWORD bytes, Settings* s) {
    bool seen_addr = false;
    bool seen_host = false;
    DWORD start = 0;
    for (DWORD i = 0; i <= bytes; ++i) {
        if (i < bytes && text[i] != '\n') {
            continue;
        }
        DWORD end = i;
        while (end > start && (text[end - 1] == '\r' || text[end - 1] == ' ')) {
            --end;
        }
        if (end > start && !parse_line(text + start, end - start, s, &seen_addr, &seen_host)) {
            return false;
        }
        start = i + 1;
    }
    return seen_addr && seen_host;
}

// The adapter's first usable IPv4 address appended to `out` (loopback and 169.254.x.x skipped).
void add_adapter(const IP_ADAPTER_ADDRESSES* a, Ips* out) {
    if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
        return;
    }
    const IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress;
    for (int k = 0; k < kMaxUnicast && u != nullptr && out->count < kMaxIps; ++k, u = u->Next) {
        const sockaddr* const sa = u->Address.lpSockaddr;
        if (sa == nullptr || sa->sa_family != AF_INET) {
            continue;
        }
        const auto* const b = reinterpret_cast<const uint8_t*>(&reinterpret_cast<const sockaddr_in*>(sa)->sin_addr);
        if (b[0] == 127 || (b[0] == 169 && b[1] == 254)) {
            continue;
        }
        snprintf(out->ip[out->count], kIpChars, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
        ++out->count;
    }
}

} // namespace

bool parse_address(const wchar_t* in, wchar_t (&out)[kAddrChars]) {
    out[0] = L'\0';
    size_t i = 0;
    while (in[i] == L' ') {
        ++i;
    }
    uint32_t q[4] = {};
    for (int k = 0; k < 4; ++k) {
        if (!parse_num(in, &i, 3, 255, &q[k]) || (k < 3 && in[i++] != L'.')) {
            return false;
        }
    }
    uint32_t port = kDefaultPort;
    if (in[i] == L':' && (++i, !parse_num(in, &i, 5, 65535, &port) || port == 0)) {
        return false;
    }
    while (in[i] == L' ') {
        ++i;
    }
    if (in[i] != L'\0') {
        return false;
    }
    swprintf(out, kAddrChars, L"%u.%u.%u.%u:%u", q[0], q[1], q[2], q[3], port);
    return true;
}

Settings load() {
    Settings s{};
    wchar_t path[kPathChars];
    if (!ini_path(path)) {
        return s;
    }
    const HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        log::line("card052: no lobby settings at %ls (err %lu): defaults (host off, no address)", path, GetLastError());
        return s;
    }
    char text[kIniMaxBytes + 1] = {};
    DWORD bytes = 0;
    const BOOL ok = ReadFile(f, text, kIniMaxBytes, &bytes, nullptr);
    const DWORD err = GetLastError();
    CloseHandle(f);
    Settings parsed{};
    if (!ok || bytes == kIniMaxBytes || !parse_ini(text, bytes, &parsed)) {
        log::line("WARN card052: lobby settings %ls unreadable or invalid (ReadFile %d, err %lu, %lu bytes): ignored",
                  path, ok ? 1 : 0, ok ? 0ul : err, bytes);
        return s;
    }
    log::line("card052: lobby settings read from %ls: address %ls, host %d, stamina %d", path, parsed.address,
              parsed.host ? 1 : 0, parsed.stamina ? 1 : 0);
    return parsed;
}

bool save(const Settings& s) {
    wchar_t path[kPathChars];
    if (!ini_path(path)) {
        return false;
    }
    char text[96];
    const int n = snprintf(text, sizeof(text), "address=%ls\r\nhost=%d\r\nstamina=%d\r\n", s.address, s.host ? 1 : 0,
                           s.stamina ? 1 : 0);
    const HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE || n <= 0) {
        log::line("WARN card052: lobby settings not saved to %ls (CreateFileW err %lu)", path, GetLastError());
        return false;
    }
    DWORD wrote = 0;
    const BOOL ok = WriteFile(f, text, static_cast<DWORD>(n), &wrote, nullptr);
    const DWORD err = GetLastError();
    CloseHandle(f);
    log::line("%scard052: lobby settings %s %ls: address %ls, host %d, stamina %d", ok ? "" : "WARN ",
              ok ? "saved to" : "NOT saved to", path, s.address, s.host ? 1 : 0, s.stamina ? 1 : 0);
    if (!ok) {
        log::line("WARN card052: WriteFile err %lu, %lu of %d bytes", err, wrote, n);
    }
    return ok != 0;
}

uint16_t listen_port() {
    const wchar_t* const cmd = GetCommandLineW();
    constexpr wchar_t kKey[] = L"-port=";
    constexpr size_t kKeyChars = sizeof(kKey) / sizeof(kKey[0]) - 1;
    constexpr size_t kMaxCmdChars = 32768; // CreateProcess command line limit
    for (size_t i = 0; cmd != nullptr && i < kMaxCmdChars && cmd[i] != L'\0'; ++i) {
        if (_wcsnicmp(cmd + i, kKey, kKeyChars) != 0) {
            continue;
        }
        size_t j = i + kKeyChars;
        uint32_t port = 0;
        if (parse_num(cmd, &j, 5, 65535, &port) && port >= 1024) {
            return static_cast<uint16_t>(port);
        }
        log::line("WARN card153: -port= on the command line is not 1024 to 65535: using %u", kDefaultPort);
        return kDefaultPort;
    }
    return kDefaultPort;
}

Ips local_ipv4() {
    Ips out{};
    const HMODULE dll = LoadLibraryW(L"iphlpapi.dll");
    const auto get = dll != nullptr ? reinterpret_cast<GetAdaptersAddressesFn>(
                                          reinterpret_cast<void*>(GetProcAddress(dll, "GetAdaptersAddresses")))
                                    : nullptr;
    if (get == nullptr) {
        log::line("WARN card052: iphlpapi.dll GetAdaptersAddresses unavailable (err %lu): no LAN address shown",
                  GetLastError());
        return out;
    }
    alignas(8) static uint8_t buf[kAdapterBufBytes];
    ULONG size = kAdapterBufBytes;
    auto* const first = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf);
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    const ULONG r = get(AF_INET, flags, nullptr, first, &size);
    if (r != NO_ERROR) {
        log::line("WARN card052: GetAdaptersAddresses = %lu (size %lu): no LAN address shown", r, size);
        return out;
    }
    const IP_ADAPTER_ADDRESSES* a = first;
    for (int k = 0; k < kMaxAdapters && a != nullptr && out.count < kMaxIps; ++k, a = a->Next) {
        add_adapter(a, &out);
    }
    log::line("card052: LAN IPv4 addresses: %d (%s %s %s)", out.count, out.ip[0], out.ip[1], out.ip[2]);
    return out;
}

} // namespace ubw::lobby
