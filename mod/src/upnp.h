#pragma once

// Card 153: the host's external address and a UDP port mapping on the router, for the co-op panel.
//
// Decision (card 153 Do 1): a small SSDP + SOAP client over Winsock, not the OS UPnP NAT API. The Proton prefix
// has no natupnp.dll, Wine's hnetcfg may not answer CLSID_UPnPNAT with working mappings, and a friend may host on
// Windows: one code path serves both, with no COM. ws2_32.dll and winhttp.dll are loaded on first use
// (LoadLibraryW + GetProcAddress), so the proxy keeps kernel32 as its only load-time import.
//
// All network work runs on one worker thread at a time. The worker owns its buffers and the mapping it added; the
// game thread reads the published result through `poll`, which never blocks. No game objects are touched.

#include <stddef.h>
#include <stdint.h>

namespace ubw::upnp {

inline constexpr size_t kIpChars = 16; // "255.255.255.255" + NUL

enum class State : uint32_t {
    Off,      // hosting off, no lookup made, or the mapping was removed
    Looking,  // the worker is finding the router or removing the mapping
    Mapped,   // the router opened the port (AddPortMapping 200)
    NoUpnp,   // no UPnP router answered: forward the port by hand
    Failed,   // the router answered AddPortMapping with an error (`code`)
};

struct Result {
    State state;
    int32_t code;          // AddPortMapping: HTTP status, or the UPnP errorCode of a SOAP fault; 0 when not called
    uint16_t port;         // the UDP port asked for
    bool external_from_web; // `external` came from api.ipify.org, not the router
    char external[kIpChars]; // dotted quad, or empty when neither the router nor ipify gave one
};

// Hosting turned on: look up the router, the external address and map UDP `port` to this PC. Returns at once.
void start(uint16_t port);

// Hosting turned off: remove the mapping this mod added (worker thread). Returns at once.
void stop();

// The latest published result (state Looking while the worker runs). Game thread; never blocks.
Result poll();

// DLL_PROCESS_DETACH: removes the mapping this mod added with short timeouts, on the calling thread. Skipped with
// a log line while a worker runs; the mapping's 4 h lease is the backstop.
void exit_cleanup();

} // namespace ubw::upnp
