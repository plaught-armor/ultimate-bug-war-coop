// Card 128: why a lagging client's connection closes, the timeouts in force, and how a dropped client finds out
// (always on; the log lines are rare and capped).
//
// Measured (runs c128c-c128e): both drivers run InitialConnectTimeout 60 s and ConnectionTimeout 60 s; a client
// stopped for 70 s is closed by the host's UNetConnection::Tick timeout (ConnectionTimeout reason, then CleanUp
// destroys its PlayerController), and the client, whose receive buffer lost the close bunch, only timed out 60 s
// after it ran again. The fix, at the layer that reads the values:
// - ConnectionTimeout 20 s on every driver (kC128ConnectionTimeout), written into the driver field GetTimeoutValue
//   reads; it applies once the client's PlayerController asks for the short timeout, InitialConnectTimeout (60 s)
//   still covers the join and its map load. A stall of up to 20 s keeps the friend in.
// - On the client, a connection that went longer than its own timeout value between two ticks has been dropped by
//   the host (the host applies the same value to it): GetTimeoutValue returns kC128DroppedTimeout from then on, so
//   the engine's own Tick timeout path closes it at once and reports ConnectionTimeout through
//   HandleNetworkFailure, which card 052 shows on the main menu.
//
// - UNetConnection::Close(FNetResult&&) (0x03E623B0, non-virtual): one line per close with the reason
//   (LexToString of the result value), its error context, the next result in its chain, the connection state, the
//   driver's two timeouts and the caller chain.
// - UIpConnection::HandleConnectionTimeout(const FString&) (0x044E23D0): one line, with the engine's message.
// - UIpConnection::GetTimeoutValue() (0x044E1BA0, called every tick per connection): one line when the value a
//   connection gets changes, so the log shows which of the driver's timeouts applies and when it switches.
//
// - UNetConnection::CleanUp() (0x03E611B0): one line per connection teardown with a 10-deep caller chain, so a
//   teardown that skips Close still names its path.
// - AGameSession::KickPlayer: one line per game kick.
//
// FNetResult (DynamicToString 0x020D6AB0): +0x00 result value (int64), +0x08 the enum's FName, +0x18 ErrorContext
// (FString), +0x28 the next result in the chain. UNetConnection: +0x58 Driver, +0x134 State (2 pending, 3 open).
// UNetDriver: +0xDC InitialConnectTimeout, +0xE0 ConnectionTimeout (GetTimeoutValue 0x03E6C5B0).

#include "ubw/chan_ops.h"
#include "ubw/hooks_internal.h"

#include <stdio.h>
#include <string.h>

namespace ubw::hooks {

C128CloseFn g_orig_c128_close = nullptr;
C128TimeoutFn g_orig_c128_timeout = nullptr;
C128TimeoutValueFn g_orig_c128_timeout_value = nullptr;
C128LexFn g_c128_lex = nullptr;
C128CleanUpFn g_orig_c128_cleanup = nullptr;
C128KickFn g_orig_c128_kick = nullptr;
thread_local const void* g_c128_pe_object = nullptr;
thread_local const void* g_c128_pe_function = nullptr;

namespace {

constexpr size_t kConnDriver = 0x58;
constexpr size_t kConnState = 0x134;
constexpr size_t kDriverInitialTimeout = 0xDC;
constexpr size_t kDriverConnTimeout = 0xE0;
constexpr size_t kResultValue = 0x00;
constexpr size_t kResultContext = 0x18;
constexpr size_t kResultNext = 0x28;
constexpr int64_t kResultReliableOverflow = 65; // ENetCloseResult::RPCReliableBufferOverflow
constexpr uint32_t kC128CloseLines = 32;
constexpr uint32_t kC128TimeoutLines = 16;
constexpr uint32_t kC128ValueLines = 32;
constexpr uint32_t kC128Conns = 8;
constexpr float kC128ConnectionTimeout = 20.0f;
constexpr float kC128DroppedTimeout = 0.001f;
constexpr uint32_t kC128DriverLines = 8;
constexpr uint32_t kC128CleanUpLines = 16;
constexpr uint32_t kC128KickLines = 16;

// One client or host connection seen by GetTimeoutValue: the value it last got, when it last ticked, and whether the
// client found it dropped (from then on it times out at once).
struct C128Value {
    const void* conn;
    float value;
    uint64_t last_ms;
    bool dropped;
};

struct C128State {
    uint32_t close_lines;
    uint32_t timeout_lines;
    uint32_t value_lines;
    uint32_t driver_lines;
    uint32_t cleanup_lines;
    uint32_t kick_lines;
    C128Value values[kC128Conns];
    uint32_t value_count;
};
C128State g_c128{};

float read_float(const void* p, size_t off) {
    float v = 0.0f;
    memcpy(&v, static_cast<const uint8_t*>(p) + off, sizeof(v));
    return v;
}

// The result's name ("ENetCloseResult::..."), or "?" when the value has no name.
void result_name(const void* result, char* out, size_t cap) {
    out[0] = '\0';
    if (result == nullptr) {
        snprintf(out, cap, "none");
        return;
    }
    int64_t value = 0;
    memcpy(&value, static_cast<const uint8_t*>(result) + kResultValue, sizeof(value));
    const wchar_t* const name = g_c128_lex != nullptr ? g_c128_lex(static_cast<uint32_t>(value)) : nullptr;
    snprintf(out, cap, "%lld %ls", static_cast<long long>(value), name != nullptr ? name : L"?");
}

void driver_timeouts(const void* conn, float* initial, float* timeout) {
    const void* const driver = conn != nullptr ? read_ptr(conn, kConnDriver) : nullptr;
    *initial = driver != nullptr ? read_float(driver, kDriverInitialTimeout) : -1.0f;
    *timeout = driver != nullptr ? read_float(driver, kDriverConnTimeout) : -1.0f;
}

// The connection's slot in g_c128.values, added when there is room; nullptr when the table is full.
C128Value* conn_entry(const void* conn) {
    for (uint32_t i = 0; i < g_c128.value_count; ++i) {
        if (g_c128.values[i].conn == conn) {
            return &g_c128.values[i];
        }
    }
    if (g_c128.value_count == kC128Conns) {
        return nullptr;
    }
    C128Value* const e = &g_c128.values[g_c128.value_count++];
    *e = C128Value{conn, -1.0f, 0, false};
    return e;
}

// A closed connection leaves the table, so a new connection at the same address starts clean.
void forget_conn(const void* conn) {
    for (uint32_t i = 0; i < g_c128.value_count; ++i) {
        if (g_c128.values[i].conn == conn) {
            g_c128.values[i] = g_c128.values[--g_c128.value_count];
            return;
        }
    }
}

// ConnectionTimeout is a driver field the engine reads in GetTimeoutValue every tick; set it there, once per driver.
void set_driver_timeout(void* conn) {
    uint8_t* const driver = conn != nullptr ? static_cast<uint8_t*>(read_ptr(conn, kConnDriver)) : nullptr;
    if (driver == nullptr || read_float(driver, kDriverConnTimeout) == kC128ConnectionTimeout) {
        return;
    }
    const float old = read_float(driver, kDriverConnTimeout);
    memcpy(driver + kDriverConnTimeout, &kC128ConnectionTimeout, sizeof(kC128ConnectionTimeout));
    if (note_event_cap(g_c128.driver_lines, kC128DriverLines, "card128 driver")) {
        log::line("card128: %s t=%llu driver %p ConnectionTimeout %.1f -> %.1f s (InitialConnectTimeout %.1f kept)",
                  side(), static_cast<unsigned long long>(GetTickCount64()), driver, old, kC128ConnectionTimeout,
                  read_float(driver, kDriverInitialTimeout));
    }
}

// Host: the frames sent to remote PCs so far, by opcode (only the opcodes sent), so the line names the traffic that
// filled the reliable buffer.
void log_chan_sends() {
    char text[512];
    int32_t used = 0;
    for (size_t op = 0; op < ubw::chan::kChanOpCount && used < static_cast<int32_t>(sizeof(text)) - 16; ++op) {
        const uint32_t n = ue::chan::chan_client_sends(op);
        if (n != 0) {
            used += snprintf(text + used, sizeof(text) - static_cast<size_t>(used), " %zu:%u", op, n);
        }
    }
    text[used < static_cast<int32_t>(sizeof(text)) ? used : static_cast<int32_t>(sizeof(text)) - 1] = '\0';
    log::line("card128: host t=%llu chan frames sent to clients by opcode (op:count):%s",
              static_cast<unsigned long long>(GetTickCount64()), used > 0 ? text : " none");
}

} // namespace

void detour_c128_close(void* conn, void* result) {
    if (note_event_cap(g_c128.close_lines, kC128CloseLines, "card128 close")) {
        char chain[256];
        caller_chain(chain, sizeof(chain), 8);
        char name[96];
        char next[96];
        result_name(result, name, sizeof(name));
        result_name(result != nullptr ? read_ptr(result, kResultNext) : nullptr, next, sizeof(next));
        float initial = 0.0f;
        float timeout = 0.0f;
        driver_timeouts(conn, &initial, &timeout);
        uint32_t state = 0;
        memcpy(&state, static_cast<const uint8_t*>(conn) + kConnState, sizeof(state));
        log::line("card128: %s t=%llu connection %p closed: reason %s, then %s, state %u, timeouts initial %.1f "
                  "connection %.1f s; chain %s",
                  side(), static_cast<unsigned long long>(GetTickCount64()), conn, name, next, state, initial,
                  timeout, chain);
        if (result != nullptr) {
            log_fstring("card128: close context", static_cast<const uint8_t*>(result) + kResultContext);
        }
        int64_t value = 0;
        if (result != nullptr) {
            memcpy(&value, static_cast<const uint8_t*>(result) + kResultValue, sizeof(value));
        }
        // Only this close runs inside the RPC's own ProcessEvent, so only here are the recorded object and function
        // live.
        if (value == kResultReliableOverflow && g_c128_pe_object != nullptr && g_c128_pe_function != nullptr) {
            log::line("card128: %s overflowing reliable RPC %s on %s (%s)", side(), object_name(g_c128_pe_function).text,
                      object_name(g_c128_pe_object).text, class_name(g_c128_pe_object).text);
            log_chan_sends();
        }
    }
    forget_conn(conn);
    g_orig_c128_close(conn, result);
}

void detour_c128_timeout(void* conn, const void* error) {
    if (note_event_cap(g_c128.timeout_lines, kC128TimeoutLines, "card128 timeout")) {
        char chain[256];
        caller_chain(chain, sizeof(chain), 6);
        log::line("card128: %s t=%llu connection %p timed out; chain %s", side(),
                  static_cast<unsigned long long>(GetTickCount64()), conn, chain);
        log_fstring("card128: timeout message", error);
    }
    g_orig_c128_timeout(conn, error);
}

float detour_c128_timeout_value(void* conn) {
    set_driver_timeout(conn);
    const float value = g_orig_c128_timeout_value(conn);
    C128Value* const e = conn_entry(conn);
    if (e == nullptr) {
        return value;
    }
    const uint64_t now = GetTickCount64();
    const uint64_t gap_ms = e->last_ms != 0 ? now - e->last_ms : 0;
    e->last_ms = now;
    if (g_client_session && !e->dropped && static_cast<float>(gap_ms) > value * 1000.0f) {
        e->dropped = true;
        log::line("card128: client t=%llu connection %p did not tick for %llu ms, past its %.1f s timeout: the host "
                  "has dropped it; timing it out now",
                  static_cast<unsigned long long>(now), conn, static_cast<unsigned long long>(gap_ms), value);
    }
    if (e->dropped) {
        return kC128DroppedTimeout;
    }
    if (e->value != value && note_event_cap(g_c128.value_lines, kC128ValueLines, "card128 timeout value")) {
        float initial = 0.0f;
        float timeout = 0.0f;
        driver_timeouts(conn, &initial, &timeout);
        uint32_t state = 0;
        memcpy(&state, static_cast<const uint8_t*>(conn) + kConnState, sizeof(state));
        log::line("card128: %s t=%llu connection %p timeout now %.1f s (state %u; driver initial %.1f, connection "
                  "%.1f)",
                  side(), static_cast<unsigned long long>(now), conn, value, state, initial, timeout);
    }
    e->value = value;
    return value;
}

void detour_c128_cleanup(void* conn) {
    if (note_event_cap(g_c128.cleanup_lines, kC128CleanUpLines, "card128 cleanup")) {
        char chain[320];
        caller_chain(chain, sizeof(chain), 10);
        uint32_t state = 0;
        memcpy(&state, static_cast<const uint8_t*>(conn) + kConnState, sizeof(state));
        log::line("card128: %s t=%llu connection %p cleaned up, state %u; chain %s", side(),
                  static_cast<unsigned long long>(GetTickCount64()), conn, state, chain);
    }
    g_orig_c128_cleanup(conn);
}

bool detour_c128_kick(void* session, void* pc, const void* reason) {
    if (note_event_cap(g_c128.kick_lines, kC128KickLines, "card128 kick")) {
        char chain[256];
        caller_chain(chain, sizeof(chain), 8);
        log::line("card128: %s t=%llu game kick of pc %p; chain %s", side(),
                  static_cast<unsigned long long>(GetTickCount64()), pc, chain);
    }
    return g_orig_c128_kick(session, pc, reason);
}

} // namespace ubw::hooks
