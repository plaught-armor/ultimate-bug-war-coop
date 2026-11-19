// Card 330 step 1 (diagnostic, both sides, always on): what the mod costs per frame and which step costs it.
//   - Every step call in ubw_post_tick runs inside C330_STEP: one QueryPerformanceCounter pair per call, added to the
//     step's slot (a fixed table indexed by the call site's registered id; no allocation).
//   - c23_collect reports each call (caller return address, actors walked, ticks) through c330_collect.
//   - log::line's time and line count come from ue::log's running totals.
//   - Every received channel frame's handler reports its time through c330_recv (chan_ops.cpp c252_ledger_recv). The
//     handlers run from the engine's RPC receive, outside ubw_post_tick, so this time is not part of "mod total".
//   - Every kC330WindowMs one window row: frames, mod total and its share of the window, longest mod tick, the top
//     kC330Top steps, log time and lines, collect calls, actors and time.
//   - At map end one row per step with time, longest first, and one collect-by-caller row; then the map totals start
//     over.
// Game thread only (ubw_post_tick, c23_collect callers and LoadMap enter all run there).

#include "ubw/hooks_internal.h"

#include <intrin.h>
#include <windows.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace ubw::hooks {
namespace {

constexpr uint16_t kC330Steps = 128;          // registered step call sites (ubw_post_tick has 58 today)
constexpr ULONGLONG kC330WindowMs = 30000;    // window row period
constexpr int32_t kC330Top = 5;               // steps named in the window row
constexpr int32_t kC330Callers = 48;          // c23_collect call sites told apart (31 today)
constexpr double kC330OverMs = 1.0;           // a step call over this counts in the map row
constexpr double kC330SlowMs = 5.0;           // b224: a step call over this gets its own row
constexpr uint32_t kC330SlowRows = 8;         // b224: slow-call rows per step per session
constexpr size_t kC330RowMax = 1400;          // characters of the collect-by-caller row
constexpr size_t kC330Ops = 96;               // channel ops told apart in the recv part (ChanOp::Count is below it)
constexpr int32_t kC330RecvTop = 3;           // ops named in the window row's recv part

struct C330Step {
    const char* name; // the call text (C330_STEP's #call); printed up to its '('
    int64_t win_ticks;
    int64_t win_max;
    int64_t map_ticks;
    int64_t map_max;
    uint64_t map_calls;
    uint32_t map_over; // calls over kC330OverMs this map
    uint32_t slow_rows; // slow-call rows printed this session (kept across maps)
};

struct C330Caller {
    const void* ret; // return address inside c23_collect's caller
    uint64_t calls;
    int64_t ticks;
};

struct C330Collect {
    uint64_t calls;
    uint64_t actors;
    int64_t ticks;
};

struct C330Recv {
    uint32_t calls;
    int64_t ticks;
    int64_t max;
};

struct C330 {
    C330Recv recv[kC330Ops];   // this window's received frames by op (slot 0: ops past the table)
    C330Step step[kC330Steps];
    uint16_t steps;
    uint16_t dropped;          // registrations past kC330Steps (their calls are not timed)
    int64_t tick_t0;           // QPC at the open tick's start, 0 between ticks
    int64_t win_mod;           // ticks inside ubw_post_tick this window
    int64_t win_max_tick;
    uint32_t win_frames;
    ULONGLONG win_start_ms;
    int64_t log_ticks_seen;    // ue::log totals at the last window row
    int64_t log_lines_seen;
    C330Collect win_collect;
    C330Caller caller[kC330Callers];
    int32_t callers;
    uint64_t caller_dropped;   // collect calls from call sites past kC330Callers (counted in the totals only)
    double freq;               // QPC ticks per ms
    // Card 409 batch: the ProcessEvent hook's own time per frame (time stamp counter ticks; any thread, folded at
    // ubw_post_tick end). The map's TSC and QPC at its start convert ticks to time at map end.
    uint64_t pe_frame;
    uint64_t pe_frame_calls;
    uint64_t pe_map;
    uint64_t pe_map_max;
    uint64_t pe_map_calls;
    uint32_t pe_frames;
    uint64_t pe_tsc0;
    int64_t pe_qpc0;
};
C330 g_c330 = {};

int64_t c330_now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

double c330_ms(int64_t ticks) {
    if (g_c330.freq == 0.0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        g_c330.freq = static_cast<double>(f.QuadPart) / 1000.0;
    }
    return static_cast<double>(ticks) / g_c330.freq;
}

int c330_name_len(const char* name) {
    const char* const paren = strchr(name, '(');
    return static_cast<int>(paren != nullptr ? paren - name : static_cast<ptrdiff_t>(strlen(name)));
}

// The window's top steps by time, into `out` (ids, at most kC330Top); returns how many.
int32_t c330_top(uint16_t* out) {
    int32_t n = 0;
    for (uint16_t i = 0; i < g_c330.steps; ++i) {
        if (g_c330.step[i].win_ticks == 0) {
            continue;
        }
        int32_t at = n < kC330Top ? n++ : kC330Top;
        while (at > 0 && g_c330.step[out[at - 1]].win_ticks < g_c330.step[i].win_ticks) {
            if (at < kC330Top) {
                out[at] = out[at - 1];
            }
            --at;
        }
        if (at < kC330Top) {
            out[at] = i;
        }
    }
    return n;
}

// The window's received frames: total, then the kC330RecvTop ops with the most handler time. Clears the table.
void c330_recv_text(char* out, size_t cap) {
    uint64_t calls = 0;
    int64_t ticks = 0;
    for (size_t i = 0; i < kC330Ops; ++i) {
        calls += g_c330.recv[i].calls;
        ticks += g_c330.recv[i].ticks;
    }
    int at = snprintf(out, cap, "%llu frames %.1f ms", static_cast<unsigned long long>(calls), c330_ms(ticks));
    for (int32_t k = 0; k < kC330RecvTop && at > 0 && static_cast<size_t>(at) < cap; ++k) {
        size_t best = kC330Ops;
        for (size_t i = 0; i < kC330Ops; ++i) {
            if (g_c330.recv[i].ticks > 0 && (best == kC330Ops || g_c330.recv[i].ticks > g_c330.recv[best].ticks)) {
                best = i;
            }
        }
        if (best == kC330Ops) {
            break;
        }
        const C330Recv& r = g_c330.recv[best];
        at += snprintf(out + at, cap - static_cast<size_t>(at), ", %s %u frames %.1f ms max %.2f",
                       best != 0 ? c132_op_name(static_cast<uint8_t>(best)) : "other", r.calls, c330_ms(r.ticks),
                       c330_ms(r.max));
        g_c330.recv[best] = C330Recv{};
    }
    memset(g_c330.recv, 0, sizeof(g_c330.recv));
}

void c330_window_row(ULONGLONG now) {
    uint16_t top[kC330Top] = {};
    const int32_t n = c330_top(top);
    char steps[512];
    int at = 0;
    for (int32_t k = 0; k < n && at >= 0 && static_cast<size_t>(at) < sizeof(steps); ++k) {
        const C330Step& s = g_c330.step[top[k]];
        at += snprintf(steps + at, sizeof(steps) - static_cast<size_t>(at), "%s%.*s %.1f max %.2f", k == 0 ? "" : ", ",
                       c330_name_len(s.name), s.name, c330_ms(s.win_ticks), c330_ms(s.win_max));
    }
    int64_t log_ticks = 0;
    int64_t log_lines = 0;
    log::line_totals(&log_ticks, &log_lines);
    const ULONGLONG window = now - g_c330.win_start_ms;
    const double mod_ms = c330_ms(g_c330.win_mod);
    char recv[256];
    c330_recv_text(recv, sizeof(recv));
    log::line("card330: %s t=%llu window %llu ms: %u frames, mod %.1f ms total (%.2f%% of frame time), max tick %.2f "
              "ms; top %d steps %s; log %.1f ms %lld lines; collect %llu calls %llu actors %.1f ms; untimed steps %u; "
              "recv (outside mod total) %s",
              side(), static_cast<unsigned long long>(now), static_cast<unsigned long long>(window), g_c330.win_frames,
              mod_ms, window != 0 ? mod_ms * 100.0 / static_cast<double>(window) : 0.0,
              c330_ms(g_c330.win_max_tick), n, n != 0 ? steps : "none", c330_ms(log_ticks - g_c330.log_ticks_seen),
              static_cast<long long>(log_lines - g_c330.log_lines_seen),
              static_cast<unsigned long long>(g_c330.win_collect.calls),
              static_cast<unsigned long long>(g_c330.win_collect.actors), c330_ms(g_c330.win_collect.ticks),
              g_c330.dropped, recv);
    g_c330.log_ticks_seen = log_ticks;
    g_c330.log_lines_seen = log_lines;
    for (uint16_t i = 0; i < g_c330.steps; ++i) {
        g_c330.step[i].win_ticks = 0;
        g_c330.step[i].win_max = 0;
    }
    g_c330.win_mod = 0;
    g_c330.win_max_tick = 0;
    g_c330.win_frames = 0;
    g_c330.win_collect = C330Collect{};
    g_c330.win_start_ms = now;
}

// One frame's ProcessEvent hook time into the map totals (from c330_tick_end).
void c330_pe_fold() {
    if (g_c330.pe_tsc0 == 0) {
        g_c330.pe_tsc0 = __rdtsc();
        g_c330.pe_qpc0 = c330_now();
    }
    const uint64_t f = g_c330.pe_frame;
    g_c330.pe_map += f;
    g_c330.pe_map_max = f > g_c330.pe_map_max ? f : g_c330.pe_map_max;
    g_c330.pe_map_calls += g_c330.pe_frame_calls;
    ++g_c330.pe_frames;
    g_c330.pe_frame = 0;
    g_c330.pe_frame_calls = 0;
}

// Card 409 batch (efficiency audit, card 329 row shape): the map's ProcessEvent hook cost per frame, mean and max.
void c330_pe_row(const char* map) {
    const uint64_t tsc = __rdtsc() - g_c330.pe_tsc0;
    const double ms = c330_ms(c330_now() - g_c330.pe_qpc0);
    const double us_per_tsc = g_c330.pe_tsc0 != 0 && tsc != 0 ? ms * 1000.0 / static_cast<double>(tsc) : 0.0;
    const double frames = g_c330.pe_frames != 0 ? static_cast<double>(g_c330.pe_frames) : 1.0;
    const double calls = g_c330.pe_map_calls != 0 ? static_cast<double>(g_c330.pe_map_calls) : 1.0;
    log::line("card329: %s map %s ProcessEvent hook (mod time, the original excluded): %u frames, %.1f us mean per "
              "frame, %.1f us max, %llu calls (%.1f per frame, %.3f us each)", side(), map, g_c330.pe_frames,
              static_cast<double>(g_c330.pe_map) * us_per_tsc / frames,
              static_cast<double>(g_c330.pe_map_max) * us_per_tsc,
              static_cast<unsigned long long>(g_c330.pe_map_calls), static_cast<double>(g_c330.pe_map_calls) / frames,
              static_cast<double>(g_c330.pe_map) * us_per_tsc / calls);
    g_c330.pe_map = g_c330.pe_map_max = g_c330.pe_map_calls = 0;
    g_c330.pe_frames = 0;
    g_c330.pe_tsc0 = __rdtsc();
    g_c330.pe_qpc0 = c330_now();
}

void c330_caller_row(const char* map) {
    char text[kC330RowMax];
    int at = 0;
    const auto* const base = reinterpret_cast<const uint8_t*>(&__ImageBase);
    for (int32_t i = 0; i < g_c330.callers && at >= 0 && static_cast<size_t>(at) < sizeof(text); ++i) {
        const C330Caller& c = g_c330.caller[i];
        at += snprintf(text + at, sizeof(text) - static_cast<size_t>(at), "%smod+0x%llx %llu calls %.1f ms",
                       i == 0 ? "" : ", ",
                       static_cast<unsigned long long>(static_cast<const uint8_t*>(c.ret) - base),
                       static_cast<unsigned long long>(c.calls), c330_ms(c.ticks));
    }
    log::line("card330: %s map %s collect by caller: %s; other call sites %llu calls", side(), map,
              g_c330.callers != 0 ? text : "none", static_cast<unsigned long long>(g_c330.caller_dropped));
}

} // namespace

uint16_t c330_register(const char* name) {
    if (g_c330.steps >= kC330Steps) {
        ++g_c330.dropped;
        log::line("WARN card330: %s step %s not timed: table of %u full", side(), name, kC330Steps);
        return kC330Steps;
    }
    g_c330.step[g_c330.steps].name = name;
    return g_c330.steps++;
}

C330Span::C330Span(uint16_t step) : id(step), t0(step < kC330Steps ? c330_now() : 0) {}

C330Span::~C330Span() {
    if (t0 == 0) {
        return;
    }
    const int64_t d = c330_now() - t0;
    C330Step& s = g_c330.step[id];
    s.win_ticks += d;
    s.win_max = d > s.win_max ? d : s.win_max;
    s.map_ticks += d;
    s.map_max = d > s.map_max ? d : s.map_max;
    ++s.map_calls;
    const double ms = c330_ms(d);
    s.map_over += ms > kC330OverMs ? 1u : 0u;
    if (ms > kC330SlowMs && s.slow_rows < kC330SlowRows) {
        ++s.slow_rows;
        log::line("card330: %s t=%llu step %.*s took %.2f ms", side(), static_cast<unsigned long long>(GetTickCount64()),
                  c330_name_len(s.name), s.name, ms);
    }
}

void c330_tick_begin() {
    g_c330.tick_t0 = c330_now();
    if (g_c330.win_start_ms == 0) {
        g_c330.win_start_ms = GetTickCount64();
    }
}

void c330_tick_end() {
    if (g_c330.tick_t0 == 0) {
        return;
    }
    const int64_t d = c330_now() - g_c330.tick_t0;
    g_c330.tick_t0 = 0;
    c330_pe_fold();
    g_c330.win_mod += d;
    g_c330.win_max_tick = d > g_c330.win_max_tick ? d : g_c330.win_max_tick;
    ++g_c330.win_frames;
    const ULONGLONG now = GetTickCount64();
    if (now - g_c330.win_start_ms >= kC330WindowMs) {
        c330_window_row(now);
    }
}

void c330_pe(uint64_t tsc) {
    g_c330.pe_frame += tsc;
    ++g_c330.pe_frame_calls;
}

void c330_recv(wchar_t op, int64_t ticks) {
    C330Recv& r = g_c330.recv[static_cast<size_t>(op) < kC330Ops ? static_cast<size_t>(op) : 0];
    ++r.calls;
    r.ticks += ticks;
    r.max = ticks > r.max ? ticks : r.max;
}

void c330_collect(const void* ret, uint64_t actors, int64_t ticks) {
    ++g_c330.win_collect.calls;
    g_c330.win_collect.actors += actors;
    g_c330.win_collect.ticks += ticks;
    int32_t i = 0;
    while (i < g_c330.callers && g_c330.caller[i].ret != ret) {
        ++i;
    }
    if (i == g_c330.callers && g_c330.callers < kC330Callers) {
        g_c330.caller[g_c330.callers++] = C330Caller{ret, 0, 0};
    }
    if (i == kC330Callers) {
        ++g_c330.caller_dropped;
        return;
    }
    ++g_c330.caller[i].calls;
    g_c330.caller[i].ticks += ticks;
}

// The map's step rows, longest total first (one row per step with time), the collect-by-caller row; the map totals
// start over.
void c330_map_end(const void* old_world) {
    char map[128];
    snprintf(map, sizeof(map), "%s", old_world != nullptr ? object_name(old_world).text : "none");
    bool shown[kC330Steps] = {};
    for (uint16_t row = 0; row < g_c330.steps; ++row) {
        uint16_t best = kC330Steps;
        for (uint16_t i = 0; i < g_c330.steps; ++i) {
            const bool better = best == kC330Steps || g_c330.step[i].map_ticks > g_c330.step[best].map_ticks;
            best = !shown[i] && g_c330.step[i].map_ticks != 0 && better ? i : best;
        }
        if (best == kC330Steps) {
            break;
        }
        shown[best] = true;
        const C330Step& s = g_c330.step[best];
        log::line("card330: %s map %s step %.*s: %.1f ms total, %.1f us mean, %.2f ms max, %u calls over 1 ms", side(),
                  map, c330_name_len(s.name), s.name, c330_ms(s.map_ticks),
                  c330_ms(s.map_ticks) * 1000.0 / static_cast<double>(s.map_calls), c330_ms(s.map_max), s.map_over);
    }
    c330_caller_row(map);
    c330_pe_row(map);
    for (uint16_t i = 0; i < g_c330.steps; ++i) {
        C330Step& s = g_c330.step[i];
        s = C330Step{s.name, s.win_ticks, s.win_max, 0, 0, 0, 0, s.slow_rows};
    }
    g_c330.callers = 0;
    g_c330.caller_dropped = 0;
}

} // namespace ubw::hooks
