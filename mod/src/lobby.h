#pragma once

// Card 052 lobby helpers with no engine state: the join address parser, the saved lobby settings
// (%LOCALAPPDATA%\StarshipTroopers_UBW\Saved\UBWCoop.ini) and this machine's LAN IPv4 addresses.
// kernel32 only at load time; iphlpapi.dll is loaded on first use.

#include <stddef.h>
#include <stdint.h>

namespace ubw::lobby {

inline constexpr size_t kAddrChars = 22;   // "255.255.255.255:65535" + NUL
inline constexpr uint16_t kDefaultPort = 7777;
inline constexpr int kMaxIps = 3;
inline constexpr size_t kIpChars = 16;     // "255.255.255.255" + NUL

// `in` as "a.b.c.d" or "a.b.c.d:port" (spaces around it ignored) written to `out` as "a.b.c.d:port"
// (port kDefaultPort when absent). False, with out[0] = NUL, for anything else.
bool parse_address(const wchar_t* in, wchar_t (&out)[kAddrChars]);

struct Settings {
    wchar_t address[kAddrChars]; // canonical "a.b.c.d:port"; empty when none was saved
    bool host;
    bool stamina; // card 490: the host's infinite stamina option; optional line, off when absent
};

// Reads the saved settings. Missing file: defaults, logged. A bad file: logged and ignored (defaults).
Settings load();

// Writes the lines (address=..., host=0|1, stamina=0|1), replacing the file. Logs and returns false on failure.
bool save(const Settings& s);

struct Ips {
    char ip[kMaxIps][kIpChars];
    int count;
};

// Card 153: the port this host listens on, from `-port=N` on the command line (case-insensitive, 1024 to 65535);
// kDefaultPort when absent or out of range. The engine reads the same switch for its listen URL.
uint16_t listen_port();

// Up to kMaxIps IPv4 addresses of adapters that are up, loopback and link-local skipped. Logs failures.
Ips local_ipv4();

} // namespace ubw::lobby
