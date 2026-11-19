#include "upnp.h"

#include "ue/log.h"

#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include <atomic>
#include <initializer_list>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ubw::upnp {
namespace log = ue::log;
namespace {

constexpr DWORD kSsdpWaitMs = 2500;  // UDP replies to one M-SEARCH (MX 2)
constexpr DWORD kHttpWaitMs = 3000;  // each router HTTP call, and api.ipify.org
constexpr DWORD kExitWaitMs = 1000;  // DeletePortMapping at DLL detach
constexpr int kSsdpReplies = 8;      // replies read per M-SEARCH
constexpr int kMaxRecvCalls = 256;   // recv calls per HTTP reply
constexpr int kMaxJobs = 8;          // jobs one worker runs before it exits
constexpr uint32_t kLeaseSecs = 14400; // 4 h: the backstop when the game exits without DeletePortMapping
constexpr int32_t kOnlyPermanentLeases = 725; // UPnP IGD error: retry AddPortMapping with lease 0
constexpr int32_t kNoSuchEntry = 714;         // UPnP IGD error: DeletePortMapping found no mapping
constexpr size_t kPathChars = 256;
constexpr size_t kBufBytes = 32768;  // device description or SOAP reply
constexpr size_t kReqBytes = 2048;
constexpr const char* kServices[] = {"urn:schemas-upnp-org:service:WANIPConnection:1",
                                     "urn:schemas-upnp-org:service:WANPPPConnection:1"};

struct Ws {
    decltype(&::WSAStartup) startup;
    decltype(&::WSAGetLastError) last_error;
    decltype(&::socket) socket;
    decltype(&::closesocket) close;
    decltype(&::setsockopt) setsockopt;
    decltype(&::sendto) sendto;
    decltype(&::recvfrom) recvfrom;
    decltype(&::connect) connect;
    decltype(&::send) send;
    decltype(&::recv) recv;
    decltype(&::getsockname) getsockname;
    decltype(&::ioctlsocket) ioctlsocket; // senior review: connect bounded by the call's wait
    decltype(&::select) select;
    bool ready;
};

struct Wh {
    decltype(&::WinHttpOpen) open;
    decltype(&::WinHttpSetTimeouts) set_timeouts;
    decltype(&::WinHttpConnect) connect;
    decltype(&::WinHttpOpenRequest) open_request;
    decltype(&::WinHttpSendRequest) send_request;
    decltype(&::WinHttpReceiveResponse) receive_response;
    decltype(&::WinHttpReadData) read_data;
    decltype(&::WinHttpCloseHandle) close;
    bool ready;
};

// The router's control point. Worker-owned; exit_cleanup reads it only when no worker runs.
struct Igd {
    uint8_t ip[4];
    uint16_t port;
    char path[kPathChars];
    const char* service;  // one of kServices; nullptr until a router was found
    char lan[kIpChars];   // this PC's address as the router sees it (local end of the control connection)
    uint16_t mapped_port; // nonzero: this mod added a UDP mapping for this port and has not removed it
};

// The DLLs stay loaded for the process lifetime (never freed: the worker may still use them).
Ws g_ws{};
Wh g_wh{};
Igd g_igd{};
char g_buf[kBufBytes]; // worker-owned: the last HTTP reply, NUL-terminated

// Published result: a sequence lock. The worker is the only writer (odd sequence while it writes).
Result g_result{};
std::atomic<uint32_t> g_seq{0};

std::atomic<bool> g_busy{false};    // a worker thread runs (or exit_cleanup owns the state)
std::atomic<bool> g_want_on{false}; // start() asked; the worker takes it
std::atomic<bool> g_want_off{false};
std::atomic<uint16_t> g_want_port{0};

void publish(const Result& r) {
    g_seq.fetch_add(1, std::memory_order_acq_rel);
    g_result = r;
    g_seq.fetch_add(1, std::memory_order_release);
}

template <typename T>
bool load_fn(HMODULE dll, const char* dll_name, const char* name, T* out) {
    *out = reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(dll, name)));
    if (*out == nullptr) {
        log::line("ERROR card153: %s!%s not found", dll_name, name);
    }
    return *out != nullptr;
}

bool ws_load() {
    if (g_ws.ready) {
        return true;
    }
    const HMODULE dll = LoadLibraryW(L"ws2_32.dll");
    if (dll == nullptr) {
        log::line("ERROR card153: LoadLibrary ws2_32.dll failed, err=%lu -- no UPnP", GetLastError());
        return false;
    }
    const char* const n = "ws2_32";
    bool ok = load_fn(dll, n, "WSAStartup", &g_ws.startup);
    ok = load_fn(dll, n, "WSAGetLastError", &g_ws.last_error) && ok;
    ok = load_fn(dll, n, "socket", &g_ws.socket) && ok;
    ok = load_fn(dll, n, "closesocket", &g_ws.close) && ok;
    ok = load_fn(dll, n, "setsockopt", &g_ws.setsockopt) && ok;
    ok = load_fn(dll, n, "sendto", &g_ws.sendto) && ok;
    ok = load_fn(dll, n, "recvfrom", &g_ws.recvfrom) && ok;
    ok = load_fn(dll, n, "connect", &g_ws.connect) && ok;
    ok = load_fn(dll, n, "send", &g_ws.send) && ok;
    ok = load_fn(dll, n, "recv", &g_ws.recv) && ok;
    ok = load_fn(dll, n, "getsockname", &g_ws.getsockname) && ok;
    ok = load_fn(dll, n, "ioctlsocket", &g_ws.ioctlsocket) && ok;
    ok = load_fn(dll, n, "select", &g_ws.select) && ok;
    WSADATA data{};
    const int rc = ok ? g_ws.startup(MAKEWORD(2, 2), &data) : -1;
    if (ok && rc != 0) {
        log::line("ERROR card153: WSAStartup failed, rc=%d -- no UPnP", rc);
    }
    g_ws.ready = ok && rc == 0;
    return g_ws.ready;
}

bool wh_load() {
    if (g_wh.ready) {
        return true;
    }
    const HMODULE dll = LoadLibraryW(L"winhttp.dll");
    if (dll == nullptr) {
        log::line("ERROR card153: LoadLibrary winhttp.dll failed, err=%lu -- no ipify lookup", GetLastError());
        return false;
    }
    const char* const n = "winhttp";
    bool ok = load_fn(dll, n, "WinHttpOpen", &g_wh.open);
    ok = load_fn(dll, n, "WinHttpSetTimeouts", &g_wh.set_timeouts) && ok;
    ok = load_fn(dll, n, "WinHttpConnect", &g_wh.connect) && ok;
    ok = load_fn(dll, n, "WinHttpOpenRequest", &g_wh.open_request) && ok;
    ok = load_fn(dll, n, "WinHttpSendRequest", &g_wh.send_request) && ok;
    ok = load_fn(dll, n, "WinHttpReceiveResponse", &g_wh.receive_response) && ok;
    ok = load_fn(dll, n, "WinHttpReadData", &g_wh.read_data) && ok;
    ok = load_fn(dll, n, "WinHttpCloseHandle", &g_wh.close) && ok;
    g_wh.ready = ok;
    return ok;
}

// --- text ---

// Case-insensitive search for `needle` in the NUL-terminated `hay` (at most kBufBytes chars); nullptr when absent.
const char* find_ci(const char* hay, const char* needle) {
    const size_t n = strlen(needle);
    for (size_t i = 0; i < kBufBytes && hay[i] != '\0'; ++i) {
        if (_strnicmp(hay + i, needle, n) == 0) {
            return hay + i;
        }
    }
    return nullptr;
}

// Copies `from` up to the first char of `stops` or NUL. False when nothing was copied or `cap` was too small.
bool copy_until(const char* from, const char* stops, char* out, size_t cap) {
    size_t i = 0;
    for (; i + 1 < cap && from[i] != '\0' && strchr(stops, from[i]) == nullptr; ++i) {
        out[i] = from[i];
    }
    out[i] = '\0';
    return i > 0 && (from[i] == '\0' || strchr(stops, from[i]) != nullptr);
}

// "a.b.c.d" at `s` (each part 0 to 255, at most 3 digits); *used = chars read.
bool parse_ipv4(const char* s, uint8_t (&out)[4], size_t* used) {
    size_t i = 0;
    for (int k = 0; k < 4; ++k) {
        uint32_t v = 0;
        int n = 0;
        for (; n < 3 && s[i] >= '0' && s[i] <= '9'; ++n, ++i) {
            v = v * 10u + static_cast<uint32_t>(s[i] - '0');
        }
        if (n == 0 || v > 255 || (s[i] >= '0' && s[i] <= '9') || (k < 3 && s[i++] != '.')) {
            return false;
        }
        out[k] = static_cast<uint8_t>(v);
    }
    *used = i;
    return true;
}

// The whole string is a dotted quad.
bool is_quad(const char* s) {
    uint8_t ip[4] = {};
    size_t used = 0;
    return parse_ipv4(s, ip, &used) && s[used] == '\0';
}

// "http://a.b.c.d[:port][/path]" into ip, port and path ("/" when none). Host names are not resolved.
bool parse_url(const char* url, Igd* out) {
    size_t used = 0;
    if (_strnicmp(url, "http://", 7) != 0 || !parse_ipv4(url + 7, out->ip, &used)) {
        return false;
    }
    const char* p = url + 7 + used;
    uint32_t port = 80;
    if (*p == ':') {
        port = 0;
        int n = 0;
        for (++p; n < 5 && *p >= '0' && *p <= '9'; ++n, ++p) {
            port = port * 10u + static_cast<uint32_t>(*p - '0');
        }
        if (n == 0 || port == 0 || port > 65535) {
            return false;
        }
    }
    out->port = static_cast<uint16_t>(port);
    if (*p != '/' && *p != '\0') {
        return false;
    }
    return copy_until(*p == '\0' ? "/" : p, " \t\r\n\"<", out->path, kPathChars);
}

// --- sockets ---

sockaddr_in make_addr(const uint8_t (&ip)[4], uint16_t port) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = static_cast<u_short>((port >> 8) | ((port & 0xFFu) << 8)); // network order
    memcpy(&a.sin_addr, ip, 4);
    return a;
}

void set_timeouts(SOCKET s, DWORD ms) {
    g_ws.setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
    g_ws.setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
}

bool send_all(SOCKET s, const char* data, int len) {
    int sent = 0;
    for (int k = 0; k < kMaxRecvCalls && sent < len; ++k) {
        const int n = g_ws.send(s, data + sent, len - sent, 0);
        if (n <= 0) {
            log::line("WARN card153: send failed, err=%d", g_ws.last_error());
            return false;
        }
        sent += n;
    }
    return sent == len;
}

// Reads until the peer closes (HTTP/1.0, Connection: close) into g_buf, NUL-terminated. Bytes read.
int recv_all(SOCKET s) {
    int total = 0;
    constexpr int kCap = static_cast<int>(kBufBytes) - 1;
    for (int k = 0; k < kMaxRecvCalls && total < kCap; ++k) {
        const int n = g_ws.recv(s, g_buf + total, kCap - total, 0);
        if (n <= 0) {
            if (n < 0 && total == 0) {
                log::line("WARN card153: recv failed, err=%d", g_ws.last_error());
            }
            break;
        }
        total += n;
    }
    g_buf[total] = '\0';
    if (total == kCap) {
        log::line("WARN card153: HTTP reply cut at %d bytes", kCap);
    }
    return total;
}

// "HTTP/1.x NNN ..." at the start of g_buf; 0 when absent.
int http_status() {
    if (_strnicmp(g_buf, "HTTP/", 5) != 0) {
        return 0;
    }
    const char* const sp = strchr(g_buf, ' ');
    return sp != nullptr ? atoi(sp + 1) : 0;
}

void local_ip(SOCKET s, char (&lan)[kIpChars]) {
    sockaddr_in a{};
    int len = sizeof(a);
    if (g_ws.getsockname(s, reinterpret_cast<sockaddr*>(&a), &len) != 0) {
        log::line("WARN card153: getsockname failed, err=%d", g_ws.last_error());
        return;
    }
    const uint8_t* const b = reinterpret_cast<const uint8_t*>(&a.sin_addr);
    snprintf(lan, kIpChars, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

// A TCP connect bounded by `wait_ms`: a blocking connect is not bounded by SO_SNDTIMEO on Windows (about 21 s to a
// dead router), and exit_cleanup calls this inside DLL_PROCESS_DETACH under the loader lock. The socket is made
// non-blocking for the connect, waited on with select (written: connected; except: refused), then made blocking again.
bool connect_bounded(SOCKET s, const Igd& at, DWORD wait_ms) {
    const sockaddr_in a = make_addr(at.ip, at.port);
    u_long on = 1;
    if (g_ws.ioctlsocket(s, FIONBIO, &on) != 0) {
        log::line("WARN card153: FIONBIO on failed, err=%d", g_ws.last_error());
        return false;
    }
    const int rc = g_ws.connect(s, reinterpret_cast<const sockaddr*>(&a), sizeof(a));
    int err = rc == 0 ? 0 : g_ws.last_error();
    if (rc != 0 && err == WSAEWOULDBLOCK) {
        fd_set wr{};
        fd_set ex{};
        wr.fd_count = 1;
        wr.fd_array[0] = s;
        ex.fd_count = 1;
        ex.fd_array[0] = s;
        const timeval tv{static_cast<long>(wait_ms / 1000), static_cast<long>((wait_ms % 1000) * 1000)};
        const int n = g_ws.select(0, nullptr, &wr, &ex, &tv);
        err = n > 0 && wr.fd_count == 1 ? 0 : (n == 0 ? WSAETIMEDOUT : (n < 0 ? g_ws.last_error() : WSAECONNREFUSED));
        if (n == 0) {
            log::line("WARN card153: connect timed out after %lu ms", wait_ms);
        }
    }
    u_long off = 0;
    if (g_ws.ioctlsocket(s, FIONBIO, &off) != 0) {
        log::line("WARN card153: FIONBIO off failed, err=%d", g_ws.last_error());
        return false;
    }
    if (err != 0) {
        log::line("WARN card153: connect %u.%u.%u.%u:%u failed, err=%d", at.ip[0], at.ip[1], at.ip[2], at.ip[3],
                  at.port, err);
    }
    return err == 0;
}

// One HTTP exchange with `at`: reply in g_buf, this PC's address on that connection in at->lan. HTTP status, or 0.
int http_call(Igd* at, const char* req, int len, DWORD wait_ms) {
    const SOCKET s = g_ws.socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        log::line("WARN card153: TCP socket failed, err=%d", g_ws.last_error());
        return 0;
    }
    set_timeouts(s, wait_ms);
    int status = 0;
    if (connect_bounded(s, *at, wait_ms) && send_all(s, req, len) && recv_all(s) > 0) {
        status = http_status();
        local_ip(s, at->lan);
    }
    g_ws.close(s);
    return status;
}

// --- discovery ---

// Reads the device description at `loc` and takes `service`'s control URL. True with g_igd set.
bool igd_from_location(const char* loc, const char* service) {
    Igd at{};
    if (!parse_url(loc, &at)) {
        log::line("WARN card153: LOCATION %s is not http://a.b.c.d[:port]/path: skipped", loc);
        return false;
    }
    char req[kReqBytes];
    const int len = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %u.%u.%u.%u:%u\r\nConnection: close\r\n\r\n",
                             at.path, at.ip[0], at.ip[1], at.ip[2], at.ip[3], at.port);
    const int status = len > 0 && len < static_cast<int>(sizeof(req)) ? http_call(&at, req, len, kHttpWaitMs) : 0;
    const char* const svc = status == 200 ? find_ci(g_buf, service) : nullptr;
    const char* const tag = svc != nullptr ? find_ci(svc, "<controlURL>") : nullptr;
    const char* from = tag != nullptr ? tag + 12 : nullptr;
    for (size_t k = 0; from != nullptr && k < kPathChars && (*from == ' ' || *from == '\t' || *from == '\r' ||
                                                           *from == '\n'); ++k) {
        ++from; // senior review: white space before the URL; the parse_url stop set keeps CR/LF out of the request
    }
    char ctl[kPathChars];
    if (from == nullptr || !copy_until(from, " \t\r\n\"<", ctl, sizeof(ctl))) {
        log::line("WARN card153: description %s: HTTP %d, %s", loc, status,
                  svc == nullptr ? "service not listed" : "no controlURL");
        return false;
    }
    if (_strnicmp(ctl, "http://", 7) == 0) {
        Igd abs{};
        if (!parse_url(ctl, &abs)) {
            log::line("WARN card153: controlURL %s is not http://a.b.c.d: skipped", ctl);
            return false;
        }
        memcpy(at.ip, abs.ip, sizeof(at.ip));
        at.port = abs.port;
        memcpy(at.path, abs.path, sizeof(at.path));
    } else {
        snprintf(at.path, sizeof(at.path), "%s%s", ctl[0] == '/' ? "" : "/", ctl);
    }
    at.service = service;
    at.mapped_port = 0;
    g_igd = at;
    log::line("card153: UPnP router %u.%u.%u.%u:%u, control %s (%s), this PC %s", at.ip[0], at.ip[1], at.ip[2],
              at.ip[3], at.port, at.path, service, at.lan);
    return true;
}

// SSDP M-SEARCH for `service`; the first reply whose description lists it sets g_igd.
bool ssdp_find(const char* service) {
    const SOCKET s = g_ws.socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        log::line("WARN card153: UDP socket failed, err=%d", g_ws.last_error());
        return false;
    }
    set_timeouts(s, kSsdpWaitMs);
    char msg[256];
    const int len = snprintf(msg, sizeof(msg), "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n"
                             "MAN: \"ssdp:discover\"\r\nMX: 2\r\nST: %s\r\n\r\n", service);
    constexpr uint8_t kGroup[4] = {239, 255, 255, 250};
    const sockaddr_in to = make_addr(kGroup, 1900);
    bool found = false;
    if (g_ws.sendto(s, msg, len, 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) != len) {
        log::line("WARN card153: SSDP M-SEARCH send failed, err=%d", g_ws.last_error());
    }
    char reply[1024];
    char loc[kPathChars];
    int replies = 0;
    for (; replies < kSsdpReplies && !found; ++replies) {
        const int n = g_ws.recvfrom(s, reply, static_cast<int>(sizeof(reply)) - 1, 0, nullptr, nullptr);
        if (n <= 0) {
            break;
        }
        reply[n] = '\0';
        const char* l = find_ci(reply, "\nLOCATION:");
        for (l = l != nullptr ? l + 10 : nullptr; l != nullptr && (*l == ' ' || *l == '\t'); ++l) {
        }
        found = l != nullptr && copy_until(l, "\r\n", loc, sizeof(loc)) && igd_from_location(loc, service);
    }
    g_ws.close(s);
    log::line("card153: SSDP %s: %d replies read, %s", service, replies, found ? "router found" : "no router");
    return found;
}

// --- SOAP ---

// One SOAP action on g_igd: HTTP status (0 on a socket failure); the UPnP errorCode of a fault in *fault.
int soap(const char* action, const char* args, DWORD wait_ms, int32_t* fault) {
    char body[1024];
    const int blen = snprintf(body, sizeof(body), "<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\""
                              "http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\""
                              "http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:%s xmlns:u=\"%s\">%s</u:%s>"
                              "</s:Body></s:Envelope>\r\n", action, g_igd.service, args, action);
    char req[kReqBytes];
    const int len = snprintf(req, sizeof(req), "POST %s HTTP/1.0\r\nHost: %u.%u.%u.%u:%u\r\n"
                             "Content-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"%s#%s\"\r\n"
                             "Content-Length: %d\r\nConnection: close\r\n\r\n%s", g_igd.path, g_igd.ip[0],
                             g_igd.ip[1], g_igd.ip[2], g_igd.ip[3], g_igd.port, g_igd.service, action, blen, body);
    *fault = 0;
    if (blen <= 0 || blen >= static_cast<int>(sizeof(body)) || len <= 0 || len >= static_cast<int>(sizeof(req))) {
        log::line("ERROR card153: SOAP %s request does not fit its buffer", action);
        return 0;
    }
    const int status = http_call(&g_igd, req, len, wait_ms);
    const char* const code = status != 200 && status != 0 ? find_ci(g_buf, "<errorCode>") : nullptr;
    *fault = code != nullptr ? atoi(code + 11) : 0;
    return status;
}

void external_from_router(char (&out)[kIpChars]) {
    int32_t fault = 0;
    const int status = soap("GetExternalIPAddress", "", kHttpWaitMs, &fault);
    const char* const tag = status == 200 ? find_ci(g_buf, "<NewExternalIPAddress>") : nullptr;
    char ip[kIpChars] = "";
    const bool ok = tag != nullptr && copy_until(tag + 22, "<", ip, sizeof(ip)) && is_quad(ip);
    log::line("%scard153: GetExternalIPAddress: HTTP %d, UPnP error %d, address %s", ok ? "" : "WARN ", status,
              fault, ok ? ip : "(none)");
    if (ok) {
        memcpy(out, ip, sizeof(ip));
    }
}

int add_mapping(uint16_t port, uint32_t lease, int32_t* fault) {
    char args[640];
    snprintf(args, sizeof(args), "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort>"
             "<NewProtocol>UDP</NewProtocol><NewInternalPort>%u</NewInternalPort><NewInternalClient>%s"
             "</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>UBW Co-op"
             "</NewPortMappingDescription><NewLeaseDuration>%u</NewLeaseDuration>", port, port, g_igd.lan, lease);
    const int status = soap("AddPortMapping", args, kHttpWaitMs, fault);
    log::line("%scard153: AddPortMapping UDP %u -> %s:%u, lease %u s: HTTP %d, UPnP error %d",
              status == 200 ? "" : "WARN ", port, g_igd.lan, port, lease, status, *fault);
    return status;
}

void delete_mapping(DWORD wait_ms) {
    if (g_igd.mapped_port == 0) {
        return;
    }
    char args[256];
    snprintf(args, sizeof(args), "<NewRemoteHost></NewRemoteHost><NewExternalPort>%u</NewExternalPort>"
             "<NewProtocol>UDP</NewProtocol>", g_igd.mapped_port);
    int32_t fault = 0;
    const int status = soap("DeletePortMapping", args, wait_ms, &fault);
    const bool gone = status == 200 || fault == kNoSuchEntry;
    log::line("%scard153: DeletePortMapping UDP %u: HTTP %d, UPnP error %d%s", gone ? "" : "WARN ",
              g_igd.mapped_port, status, fault, gone ? "" : " (mapping left; its lease ends it)");
    if (gone) {
        g_igd.mapped_port = 0;
    }
}

// --- api.ipify.org ---

// HTTPS GET of api.ipify.org with kHttpWaitMs timeouts; true with a validated dotted quad in `out`.
bool external_from_web(char (&out)[kIpChars]) {
    if (!wh_load()) {
        return false;
    }
    HINTERNET const ses = g_wh.open(L"UBWCoop", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET const con = ses != nullptr ? g_wh.connect(ses, L"api.ipify.org", INTERNET_DEFAULT_HTTPS_PORT, 0)
                                         : nullptr;
    HINTERNET const req = con != nullptr ? g_wh.open_request(con, L"GET", L"/", nullptr, WINHTTP_NO_REFERER,
                                                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                         : nullptr;
    const int ms = static_cast<int>(kHttpWaitMs);
    // Senior review: the error is taken at the call that failed (a later call or close would overwrite it).
    bool sent = req != nullptr;
    DWORD err = sent ? 0 : GetLastError();
    sent = sent && g_wh.set_timeouts(req, ms, ms, ms, ms);
    err = sent || err != 0 ? err : GetLastError();
    sent = sent && g_wh.send_request(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    err = sent || err != 0 ? err : GetLastError();
    sent = sent && g_wh.receive_response(req, nullptr);
    err = sent || err != 0 ? err : GetLastError();
    char text[32] = {};
    DWORD total = 0;
    for (int k = 0; sent && k < 4 && total < sizeof(text) - 1; ++k) {
        DWORD got = 0;
        if (!g_wh.read_data(req, text + total, static_cast<DWORD>(sizeof(text) - 1 - total), &got)) {
            err = GetLastError();
            break;
        }
        if (got == 0) {
            break;
        }
        total += got;
    }
    for (HINTERNET h : {req, con, ses}) {
        if (h != nullptr) {
            g_wh.close(h);
        }
    }
    for (; total > 0 && (text[total - 1] == '\n' || text[total - 1] == '\r' || text[total - 1] == ' '); --total) {
    }
    text[total] = '\0';
    const bool ok = sent && is_quad(text);
    log::line("%scard153: api.ipify.org: %s, err=%lu, %lu bytes, address %s", ok ? "" : "WARN ",
              sent ? "answered" : "no answer", err, total, ok ? text : "(not a dotted quad)");
    if (ok) {
        memcpy(out, text, total + 1);
    }
    return ok;
}

// --- jobs ---

void job_on(uint16_t port) {
    Result r{};
    r.state = State::Looking;
    r.port = port;
    publish(r);
    delete_mapping(kHttpWaitMs); // a mapping from an earlier H press that was not removed yet
    const bool igd = ws_load() && (ssdp_find(kServices[0]) || ssdp_find(kServices[1]));
    r.state = State::NoUpnp;
    if (igd) {
        external_from_router(r.external);
        int32_t fault = 0;
        int status = add_mapping(port, kLeaseSecs, &fault);
        if (status != 200 && fault == kOnlyPermanentLeases) {
            status = add_mapping(port, 0, &fault);
        }
        g_igd.mapped_port = status == 200 ? port : 0;
        r.state = status == 200 ? State::Mapped : State::Failed;
        r.code = fault != 0 ? fault : status;
    }
    if (r.external[0] == '\0') {
        r.external_from_web = external_from_web(r.external);
    }
    log::line("card153: hosting lookup done: %s, external %s%s, UDP %u, code %d",
              r.state == State::Mapped ? "port opened by UPnP" : r.state == State::Failed ? "UPnP mapping failed"
                                                                                       : "no UPnP router",
              r.external[0] != '\0' ? r.external : "(none)", r.external_from_web ? " (api.ipify.org)" : "", port,
              r.code);
    publish(r);
}

void job_off() {
    delete_mapping(kHttpWaitMs);
    publish(Result{});
}

DWORD WINAPI worker(void*) {
    for (int k = 0; k < kMaxJobs; ++k) {
        if (g_want_off.exchange(false)) {
            job_off();
            continue;
        }
        if (g_want_on.exchange(false)) {
            job_on(g_want_port.load());
            continue;
        }
        g_busy.store(false);
        bool idle = false;
        if (!(g_want_on.load() || g_want_off.load()) || !g_busy.compare_exchange_strong(idle, true)) {
            return 0; // nothing asked, or a new worker took the request
        }
    }
    g_busy.store(false);
    log::line("WARN card153: worker stopped after %d jobs; a request left now runs at the next H press", kMaxJobs);
    return 0;
}

void launch() {
    bool idle = false;
    if (!g_busy.compare_exchange_strong(idle, true)) {
        return; // the running worker takes the request
    }
    const HANDLE t = CreateThread(nullptr, 0, &worker, nullptr, 0, nullptr);
    if (t == nullptr) {
        g_busy.store(false);
        log::line("ERROR card153: CreateThread failed, err=%lu -- no UPnP lookup", GetLastError());
        return;
    }
    CloseHandle(t);
}

} // namespace

void start(uint16_t port) {
    g_want_port.store(port);
    g_want_off.store(false);
    g_want_on.store(true);
    launch();
}

void stop() {
    g_want_on.store(false);
    g_want_off.store(true);
    launch();
}

Result poll() {
    Result r{};
    r.state = State::Looking;
    const uint32_t before = g_seq.load(std::memory_order_acquire);
    if (g_want_on.load() || g_busy.load() || (before & 1u) != 0) { // senior review: Looking while a worker runs
        return r;
    }
    Result copy = g_result;
    std::atomic_thread_fence(std::memory_order_acquire);
    return g_seq.load(std::memory_order_relaxed) == before ? copy : r;
}

void exit_cleanup() {
    bool idle = false;
    if (!g_busy.compare_exchange_strong(idle, true)) {
        log::line("WARN card153: exit: the UPnP worker still runs; any mapping it added ends with its 4 h lease");
        return;
    }
    if (g_igd.mapped_port != 0 && g_ws.ready) {
        delete_mapping(kExitWaitMs);
    }
}

} // namespace ubw::upnp
