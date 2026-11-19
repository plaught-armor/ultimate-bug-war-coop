// Card 303 step 1 (diagnostic): mounting a mech or turret on the client stalls 0.74 to 0.97 s with no log row.
// Two candidates: a game-thread stall inside the possession change, or the controller's channel queuing its bunches
// while the game thread runs. These rows tell them apart:
//   1. A frame watchdog on both sides. The game thread stamps each tick (c303_tick, from ubw_post_tick). A second
//      thread polls every kC303PollMs; when the frame is kC303SampleMs[k] old it suspends the game thread, copies
//      its context and stack, resumes it, and unwinds the copy (a real unwind: up to kC303Frames return addresses;
//      no lock is taken while the thread is suspended). Up to kC303Samples samples per frame. The game thread logs
//      `card303: <side> t=<ms> frame of <ms> ms (tick <n>), <k> stack sample(s)` when the frame ends, then
//      `card303: <side> t=<ms> sample <k> at +<ms> ms, frame <i> on: mod+0x.. < ..` rows. kC303FrameRows long
//      frames per map, then a tally at map end.
//   2. Possession rows for mech and turret transitions (input_diag.cpp, c303_possession) are not under the
//      session cap of the possession lines and carry the tick number.
//   3. The mount row: the client's press (c303_press, at the MechEnter / MechLeave / TurretMount / TurretLeave send)
//      to the possession of the new pawn, in ms and ticks, with the longest frame in between.
// Step 3 (b177, the fix, both sides, always on): the b176 samples put the mount and leave stalls (about 1.1 s, on both
// sides) in USTSafeZoneScale::NativeConstruct > ApplySafeZone > FSlateApplicationBase::GetDisplayMetrics >
// FDisplayMetrics::RebuildDisplayMetrics > GetSizeForDevID, which walks every monitor device through setupapi and
// the registry for its EDID (about 1 s under Proton). A monitor's EDID does not change while the game runs, so the
// result is cached per device id for the process (detour_c303_get_size):
//   `card303: <side> display size for <id> read in <ms> ms: <w>x<h> dpi <d> ok <k>, cached (entry <i> of 8)`
// once per monitor per process; every later call is a table hit.

#include "ubw/hooks_internal.h"

#include <windows.h>

namespace ubw::hooks {
namespace {

constexpr ULONGLONG kC303LongMs = 200;   // a frame longer than this is sampled and logged
constexpr DWORD kC303PollMs = 20;        // watchdog poll period
constexpr int32_t kC303Frames = 32;      // return addresses kept per sample (RIP first)
constexpr LONG kC303Samples = 3;         // samples per long frame
constexpr ULONGLONG kC303SampleMs[kC303Samples] = {200, 500, 800}; // frame age at which sample k is taken
constexpr size_t kC303StackCopy = 0x40000;  // bytes of the game thread's stack copied per sample
constexpr size_t kC303StackSlack = 0x10000; // zeroed bytes after the copy: an unwind read past the copy stays inside
constexpr int32_t kC303RowFrames = 8;       // frames per log row
constexpr size_t kC303AddrCap = 64;         // "<module>+0x<offset>" text (crash_trace.cpp describe)
constexpr uint32_t kC303FrameRows = 64;   // long-frame rows per map
constexpr ULONGLONG kC303PressMaxMs = 5000; // a press with no possession inside this is dropped (and logged)

struct C303Sample {
    uint64_t age_ms;             // age of the frame when the sample was taken
    int32_t n;
    uint64_t pc[kC303Frames];    // RIP, then the return addresses of a real unwind
};

// Written by the game thread (stamp, tick) and read by the watchdog. The samples are written by the watchdog:
// sample[k] is complete before sample_n becomes k + 1, and sample_tick is set last for the first sample of a frame.
// The game thread reads sample[0 .. sample_n) only when sample_tick equals its own tick.
struct C303Watch {
    volatile LONG64 stamp_ms;     // GetTickCount64 at the last tick end
    volatile LONG64 tick;         // ticks since start
    volatile LONG64 sample_tick;  // tick the samples belong to, -1 none
    volatile LONG sample_n;       // samples published for sample_tick
    C303Sample sample[kC303Samples];
    HANDLE thread;                // the game thread, opened with suspend and get-context rights
    const uint8_t* stack_base;    // the game thread's stack top (TEB StackBase), read on that thread
    bool started;
};
C303Watch g_c303w{0, 0, -1, 0, {}, nullptr, nullptr, false};
alignas(16) uint8_t g_c303_stack[kC303StackCopy + kC303StackSlack]; // watchdog thread only: the stack copy

struct C303Map {
    uint32_t rows;     // long-frame rows this map
    uint32_t long_n;   // long frames this map
    ULONGLONG longest; // longest frame this map
    // card 303 item 3: the open press
    const char* what;  // "MechEnter" etc., null when no press is open
    ULONGLONG press_ms;
    LONG64 press_tick;
    ULONGLONG press_longest; // longest frame since the press
};
C303Map g_c303{};

// Watchdog thread, on the stack copy. One unwind step. Every register that points into the copied part of the game
// thread's stack [lo, lo + n) is moved to the same offset in the copy first, so the unwinder reads the copy only
// (frame-pointer frames included). A frame without unwind data is a leaf: its return address is at [rsp].
// b182: false when RtlVirtualUnwind must not run on `fe`: its UNWIND_INFO is not inside the image or has an unknown
// version (1 and 2 are known), or it names a frame register (byte 3, low 4 bits; 0 = none) whose value is outside the
// copy, so the unwind would read through a register the copy does not hold (c423b181b: EXCEPTION_ACCESS_VIOLATION
// reading 0x8 at ntdll.dll+0x3A41C under c303_step, in the 1361 ms level-end frame).
bool c303_frame_ok(const CONTEXT* ctx, DWORD64 image, const RUNTIME_FUNCTION* fe, DWORD64 copy, size_t n) {
    static_assert(offsetof(CONTEXT, R15) - offsetof(CONTEXT, Rax) == 15 * sizeof(DWORD64),
                  "CONTEXT Rax..R15 are the unwind register numbers 0..15 in order");
    const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) {
        return false;
    }
    const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + static_cast<DWORD64>(dos->e_lfanew));
    const DWORD size = nt->Signature == IMAGE_NT_SIGNATURE ? nt->OptionalHeader.SizeOfImage : 0;
    if (size < 4 || fe->UnwindData == 0 || fe->UnwindData > size - 4) {
        return false;
    }
    const auto* const info = reinterpret_cast<const uint8_t*>(image + fe->UnwindData);
    const uint8_t version = info[0] & 0x7;
    if (version != 1 && version != 2) {
        return false;
    }
    const uint8_t frame_reg = info[3] & 0xF;
    if (frame_reg == 0) {
        return true;
    }
    const DWORD64 v = (&ctx->Rax)[frame_reg];
    return v >= copy && v < copy + n;
}

bool c303_step(CONTEXT* ctx, DWORD64 lo, size_t n) {
    DWORD64* const regs[] = {&ctx->Rsp, &ctx->Rbp, &ctx->Rbx, &ctx->Rsi, &ctx->Rdi,
                             &ctx->R12, &ctx->R13, &ctx->R14, &ctx->R15};
    const DWORD64 copy = reinterpret_cast<DWORD64>(g_c303_stack);
    for (DWORD64* const r : regs) {
        if (*r >= lo && *r < lo + n) {
            *r = copy + (*r - lo);
        }
    }
    if (ctx->Rsp < copy || ctx->Rsp + 8 > copy + n) {
        return false;
    }
    DWORD64 image = 0;
    const PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(ctx->Rip, &image, nullptr);
    if (fe == nullptr) {
        ctx->Rip = *reinterpret_cast<const DWORD64*>(ctx->Rsp);
        ctx->Rsp += 8;
    } else {
        if (!c303_frame_ok(ctx, image, fe, copy, n)) {
            return false; // the walk ends at this frame
        }
        PVOID handler_data = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, ctx->Rip, fe, ctx, &handler_data, &establisher, nullptr);
    }
    return ctx->Rip != 0;
}

// Watchdog thread. The game thread is suspended only for GetThreadContext and one copy of its stack (no heap, no
// lock, no other system call while it is suspended). The unwind runs on the copy after the resume, so a lock the
// game thread holds (loader, function table) cannot deadlock the watchdog.
void c303_take(HANDLE thread, LONG64 tick, LONG k, ULONGLONG age) {
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
        return;
    }
    size_t copied = 0;
    if (GetThreadContext(thread, &ctx)) {
        const auto* const sp = reinterpret_cast<const uint8_t*>(ctx.Rsp);
        const size_t avail = sp < g_c303w.stack_base ? static_cast<size_t>(g_c303w.stack_base - sp) : 0;
        copied = avail < kC303StackCopy ? avail : kC303StackCopy;
        memcpy(g_c303_stack, sp, copied);
    }
    ResumeThread(thread);
    if (copied == 0) {
        return;
    }
    memset(g_c303_stack + copied, 0, kC303StackSlack);
    C303Sample& s = g_c303w.sample[k];
    s.age_ms = age;
    s.n = 0;
    const DWORD64 lo = ctx.Rsp;
    for (int32_t i = 0; i < kC303Frames; ++i) {
        s.pc[s.n++] = ctx.Rip;
        if (!c303_step(&ctx, lo, copied)) {
            break;
        }
    }
    InterlockedExchange(&g_c303w.sample_n, k + 1);
    InterlockedExchange64(&g_c303w.sample_tick, tick);
}

DWORD WINAPI c303_watch(void*) {
    for (;;) { // the process's lifetime: the thread ends with the process
        Sleep(kC303PollMs);
        // Tick first, then the stamp: c303_tick writes the stamp before the tick, so a tick read here is never paired
        // with an older frame's stamp (a fresh frame would read as long and its sample land under the wrong tick).
        const LONG64 tick = InterlockedCompareExchange64(&g_c303w.tick, 0, 0);
        const LONG64 stamp = InterlockedCompareExchange64(&g_c303w.stamp_ms, 0, 0);
        const ULONGLONG now = GetTickCount64();
        if (stamp == 0 || now - static_cast<ULONGLONG>(stamp) < kC303LongMs) {
            continue; // not started, or the frame is short
        }
        const ULONGLONG age = now - static_cast<ULONGLONG>(stamp);
        const LONG k = InterlockedCompareExchange64(&g_c303w.sample_tick, 0, 0) == tick
                           ? InterlockedCompareExchange(&g_c303w.sample_n, 0, 0) : 0;
        if (k >= kC303Samples || age < kC303SampleMs[k]) {
            continue; // this frame has all its samples, or the next one is not due
        }
        c303_take(g_c303w.thread, tick, k, age);
    }
}

void c303_start() {
    g_c303w.started = true;
    // A play build never suspends the game thread: the watchdog runs only in test runs (UBWCOOP_TEST_SKIP, which
    // tools/run_pair.sh sets). The frame rows below still log the long frames, with "no sample" for the stack.
    if (!config::test_skip()) {
        log::line("card303: frame watchdog off (not a test run): long frames are logged without a stack sample");
        return;
    }
    g_c303w.stack_base = static_cast<const uint8_t*>(reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase);
    g_c303w.thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                                GetCurrentThreadId());
    if (g_c303w.thread == nullptr) {
        log::line("WARN card303: %s OpenThread on the game thread failed, err=%lu: no frame watchdog",
                  g_client_session ? "client" : "host", GetLastError());
        return;
    }
    const HANDLE t = CreateThread(nullptr, 0, &c303_watch, nullptr, 0, nullptr);
    if (t == nullptr) {
        log::line("WARN card303: CreateThread failed, err=%lu: no frame watchdog", GetLastError());
        return;
    }
    CloseHandle(t);
    log::line("card303: frame watchdog on: frames over %llu ms are sampled (poll %lu ms, %ld unwound samples of "
              "%d frames)",
              static_cast<unsigned long long>(kC303LongMs), kC303PollMs, kC303Samples, kC303Frames);
}

// Game thread: the rows of one sample, kC303RowFrames frames per row.
void c303_sample_rows(const char* side, ULONGLONG now, const C303Sample& s, LONG k) {
    for (int32_t at = 0; at < s.n && at < kC303Frames; at += kC303RowFrames) {
        char text[kC303RowFrames * (kC303AddrCap + 3) + 1];
        size_t len = 0;
        text[0] = '\0';
        for (int32_t i = at; i < s.n && i < at + kC303RowFrames; ++i) {
            char addr[kC303AddrCap];
            c118_describe_pc(s.pc[i], addr, sizeof(addr));
            const int w = snprintf(text + len, sizeof(text) - len, "%s%s", i == at ? "" : " < ", addr);
            len += w > 0 ? static_cast<size_t>(w) : 0;
            len = len < sizeof(text) - 1 ? len : sizeof(text) - 1;
        }
        log::line("card303: %s t=%llu sample %ld at +%llu ms, frame %d on: %s", side,
                  static_cast<unsigned long long>(now), k + 1, static_cast<unsigned long long>(s.age_ms), at, text);
    }
}

void c303_long_row(ULONGLONG now, ULONGLONG frame, LONG64 tick) {
    ++g_c303.long_n;
    if (g_c303.rows >= kC303FrameRows) {
        return;
    }
    ++g_c303.rows;
    const char* const side = g_client_session ? "client" : "host";
    LONG n = InterlockedCompareExchange64(&g_c303w.sample_tick, 0, 0) == tick
                 ? InterlockedCompareExchange(&g_c303w.sample_n, 0, 0) : 0;
    n = n < kC303Samples ? n : kC303Samples;
    log::line("card303: %s t=%llu frame of %llu ms (tick %lld), %ld stack sample(s)", side,
              static_cast<unsigned long long>(now), static_cast<unsigned long long>(frame),
              static_cast<long long>(tick), n);
    for (LONG k = 0; k < n; ++k) {
        c303_sample_rows(side, now, g_c303w.sample[k], k);
    }
}

// Step 3: the GetSizeForDevID cache. GetSizeForDevID(Id, Width, Height, DPI) (exe disassembly) writes the three
// outputs together, only when a device's EDID matches Id, and then returns true; otherwise it writes none and returns
// false. The cache stores what the original wrote (kC303SizeNone: not written) and the result, failures included, and
// a hit replays exactly that. The original is called outside the lock: two threads may both miss, and the second
// store finds the id present and skips.
constexpr int32_t kC303SizeEntries = 8;
constexpr int32_t kC303SizeIdMax = 127;                 // characters of a cached device id
constexpr int32_t kC303SizeNone = INT32_MIN;            // output not written (an EDID size is 12 bits)
struct C303SizeEntry {
    wchar_t id[kC303SizeIdMax + 1];
    int32_t len;                                        // characters, no terminator
    int32_t out[3];                                     // Width, Height, DPI as the original wrote them, or kC303SizeNone
    bool ok;
};
struct C303SizeCache {
    SRWLOCK lock;
    int32_t n;                                          // entries in use; an entry is never changed once added
    C303SizeEntry e[kC303SizeEntries];
    volatile LONG warned;                               // one pass-through WARN per process
};
C303SizeCache g_c303s{SRWLOCK_INIT, 0, {}, 0};

// Index of the entry for id[0 .. len), or -1. Caller holds the lock.
int32_t c303_size_find(const wchar_t* id, int32_t len) {
    for (int32_t i = 0; i < g_c303s.n && i < kC303SizeEntries; ++i) {
        if (g_c303s.e[i].len == len && memcmp(g_c303s.e[i].id, id, static_cast<size_t>(len) * sizeof(wchar_t)) == 0) {
            return i;
        }
    }
    return -1;
}

void c303_size_warn(const char* why, int32_t num) {
    if (InterlockedExchange(&g_c303s.warned, 1) == 0) {
        log::line("WARN card303: %s display size not cached (%s, id num %d): GetSizeForDevID passed through",
                  g_client_session ? "client" : "host", why, num);
    }
}

} // namespace

C303GetSizeFn g_orig_c303_get_size = nullptr;

// Any thread (Slate's display metrics). See the step 3 note above c303_size_find.
bool detour_c303_get_size(const FStringRaw* id, int32_t* width, int32_t* height, int32_t* dpi) {
    int32_t* const outs[3] = {width, height, dpi};
    const wchar_t* const data = id != nullptr ? id->data : nullptr;
    const int32_t num = id != nullptr ? id->num : 0;
    if (data == nullptr || num < 1 || num - 1 > kC303SizeIdMax) {
        c303_size_warn(data == nullptr || num < 1 ? "empty id" : "id over 127 characters", num);
        return g_orig_c303_get_size(id, width, height, dpi);
    }
    const int32_t len = num - 1; // num counts the terminator
    AcquireSRWLockShared(&g_c303s.lock);
    const int32_t at = c303_size_find(data, len);
    const C303SizeEntry hit = at >= 0 ? g_c303s.e[at] : C303SizeEntry{};
    const bool full = at < 0 && g_c303s.n >= kC303SizeEntries;
    ReleaseSRWLockShared(&g_c303s.lock);
    if (at >= 0) {
        for (int32_t k = 0; k < 3; ++k) {
            if (hit.out[k] != kC303SizeNone) {
                *outs[k] = hit.out[k];
            }
        }
        return hit.ok;
    }
    if (full) {
        c303_size_warn("table full", num);
        return g_orig_c303_get_size(id, width, height, dpi);
    }
    C303SizeEntry e{};
    memcpy(e.id, data, static_cast<size_t>(len) * sizeof(wchar_t));
    e.id[len] = L'\0';
    e.len = len;
    e.out[0] = e.out[1] = e.out[2] = kC303SizeNone;
    const ULONGLONG t0 = GetTickCount64();
    e.ok = g_orig_c303_get_size(id, &e.out[0], &e.out[1], &e.out[2]);
    const ULONGLONG ms = GetTickCount64() - t0;
    for (int32_t k = 0; k < 3; ++k) {
        if (e.out[k] != kC303SizeNone) {
            *outs[k] = e.out[k];
        }
    }
    AcquireSRWLockExclusive(&g_c303s.lock);
    int32_t stored = c303_size_find(data, len) >= 0 ? -2 : -1; // -2: another thread stored it first
    if (stored == -1 && g_c303s.n < kC303SizeEntries) {
        stored = g_c303s.n;
        g_c303s.e[g_c303s.n++] = e;
    }
    ReleaseSRWLockExclusive(&g_c303s.lock);
    const char* const side = g_client_session ? "client" : "host";
    if (stored >= 0) {
        log::line("card303: %s display size for %ls read in %llu ms: %dx%d dpi %d ok %d, cached (entry %d of %d)", side,
                  e.id, static_cast<unsigned long long>(ms), e.out[0], e.out[1], e.out[2], e.ok ? 1 : 0, stored + 1,
                  kC303SizeEntries);
    } else if (stored == -1) {
        c303_size_warn("table full", num);
    }
    return e.ok;
}

// Game thread, end of every engine tick (ubw_post_tick).
void c303_tick() {
    const ULONGLONG now = GetTickCount64();
    if (!g_c303w.started) {
        c303_start();
    }
    const LONG64 tick = InterlockedCompareExchange64(&g_c303w.tick, 0, 0);
    const LONG64 last = InterlockedCompareExchange64(&g_c303w.stamp_ms, 0, 0);
    const ULONGLONG frame = last != 0 ? now - static_cast<ULONGLONG>(last) : 0;
    g_c303.longest = frame > g_c303.longest ? frame : g_c303.longest;
    g_c303.press_longest = g_c303.what != nullptr && frame > g_c303.press_longest ? frame : g_c303.press_longest;
    if (frame > kC303LongMs) {
        c303_long_row(now, frame, tick);
    }
    if (g_c303.what != nullptr && now - g_c303.press_ms > kC303PressMaxMs) {
        log::line("WARN card303: client %s press at t=%llu: no possession in %llu ms", g_c303.what,
                  static_cast<unsigned long long>(g_c303.press_ms), static_cast<unsigned long long>(kC303PressMaxMs));
        g_c303.what = nullptr;
    }
    // Stamp before tick: the watchdog reads tick, then stamp (c303_watch).
    InterlockedExchange64(&g_c303w.stamp_ms, static_cast<LONG64>(GetTickCount64()));
    InterlockedExchange64(&g_c303w.tick, tick + 1);
}

long long c303_tick_number() {
    return static_cast<long long>(InterlockedCompareExchange64(&g_c303w.tick, 0, 0));
}

// Client: a mount or dismount request was sent to the host (card 075 / 123 send).
void c303_press(const char* what) {
    g_c303.what = what;
    g_c303.press_ms = GetTickCount64();
    g_c303.press_tick = InterlockedCompareExchange64(&g_c303w.tick, 0, 0);
    g_c303.press_longest = 0;
}

// Client: a possession event for a pawn (input_diag.cpp). Item 2: mech and turret transitions log every event with
// the tick. Item 3: the first AcknowledgePossession after a press closes the mount row.
void c303_possession(const char* event, const void* pawn) {
    const bool player = pawn != nullptr && g_player_character_class != nullptr && is_a(pawn, g_player_character_class());
    const ULONGLONG now = GetTickCount64();
    const LONG64 tick = InterlockedCompareExchange64(&g_c303w.tick, 0, 0);
    if (!player || g_c303.what != nullptr) {
        log::line("card303: client t=%llu possession %s pawn %s (tick %lld)", static_cast<unsigned long long>(now),
                  event, pawn != nullptr ? class_name(pawn).text : "null", static_cast<long long>(tick));
    }
    if (g_c303.what == nullptr || strcmp(event, "AcknowledgePossession") != 0) {
        return;
    }
    const ULONGLONG ms = now - g_c303.press_ms;
    log::line("card303: client t=%llu %s %s: press to possession %llu ms, %lld ticks (host and wire not split on the "
              "client: see the host's card075/123 row), longest frame %llu ms",
              static_cast<unsigned long long>(now), g_c303.what, pawn != nullptr ? class_name(pawn).text : "null",
              static_cast<unsigned long long>(ms), static_cast<long long>(tick - g_c303.press_tick),
              static_cast<unsigned long long>(g_c303.press_longest));
    c297_possessed(pawn);                 // card 297 (diagnostic): the enter row, the tag watch armed or cleared
    c298_mount_row("possession", pawn);   // card 298 (diagnostic)
    if (g_c303.press_longest > 50) {
        log::line("WARN card303: client %s %s frame of %llu ms", g_c303.what, pawn != nullptr ? class_name(pawn).text : "null",
                  static_cast<unsigned long long>(g_c303.press_longest));
    }
    g_c303.what = nullptr;
}

void c303_map_end() {
    if (g_c303.long_n > 0) {
        log::line("card303: %s map end: %u frames over %llu ms (%u logged), longest %llu ms",
                  g_client_session ? "client" : "host", g_c303.long_n, static_cast<unsigned long long>(kC303LongMs),
                  g_c303.rows, static_cast<unsigned long long>(g_c303.longest));
    }
    g_c303 = C303Map{};
    c297_map_end();
}

} // namespace ubw::hooks
