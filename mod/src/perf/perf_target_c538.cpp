// Card 538 (performance mod family, card 527): diagnostic counts behind USTAttackComponent::DoesTargetMeetConditions.
//
// c263L355 (b350, host perf): DoesTargetMeetConditions is 3.1% of Background Work and the TSet<FGuid, FADTargetData>
// find it reaches (through the owner's targeting component, exe 0x145189f50) another 1.8%. Both are cache-miss bound
// (hot loads: the target's health component flags at exe 0x145186873, the 128 B FADTargetData chain at 0x145177c0f),
// so a cut has to make fewer calls, not cheaper ones. The caller seen in the branch stacks is
// USTAttackComponent::IsAttackUsableAgainstTarget, once per attack config and target.
//
// This detour only counts, it changes no result. Per thread, per frame (g_c538_frame, bumped by the game thread
// tick), two direct-mapped tables record each (component, target) and each (component, conditions, target) seen:
// - same target: the call's (component, target) was already seen this frame on this thread (the target data and
//   health lookups a per-frame memo could share);
// - same conditions: the whole (component, conditions, target) was already seen (a full-result memo), and of those,
//   how many returned a different result than the stored one (a memo would have been wrong).
// Repeats on other threads, and slots taken over by another key, are not counted: the counts are lower bounds.
// Off switch: UBWCOOP_C538_OFF=1 (the detour is then not installed).
//
// Card 540 (performance mod lever, same detour): c263L358 (b352, host) showed 52860 calls per frame at the peak,
// 33.6% of them exact (component, conditions, target) repeats in the same frame, and 0 of the repeats with a
// different result. The conditions pointer is a member of the caller's attack config (IsAttackUsableAgainstTarget
// passes config + 0x10), so it names stable data, not a stack temporary. With card 540 on, a repeat in the same frame
// on the same thread returns the stored result instead of calling the original, which reads only the target's data
// row, health component and gameplay tags. One repeat in kC540VerifyEvery still calls the original and counts a
// result that differs from the stored one (the memo would have been wrong). The table is per thread and keyed by the
// frame, so nothing is shared and nothing outlives the frame. Off switch: UBWCOOP_C540_OFF=1 (counts only).

#include "ubw/hooks_internal.h"

#include <atomic>

namespace ubw::hooks {

TargetConditionsFn g_orig_c538_conditions = nullptr;
bool g_c538_on = true; // written in ubw::init before any hook (UBWCOOP_C538_OFF=1 turns it off)
bool g_c540_on = true; // written in ubw::init before any hook (UBWCOOP_C540_OFF=1 turns the memo off)

namespace {

constexpr uint32_t kC538Slots = 1024; // power of two, (component, target) table per thread (24 KB)
// Power of two, (component, conditions, target) table per thread, 32 B rows. c263L360 (b354, 4096 slots, 128 KB):
// served 42.6% of host calls, but the detour rose from 75 to 211 BG samples, more than the original saved (313 to 249).
// b356 halves it to 64 KB and skips the pair table while the memo is on (one table row per call, not two).
constexpr uint32_t kC540Slots = 2048;
constexpr uint32_t kC540VerifyEvery = 256; // power of two: one memo hit in this many calls the original
constexpr uint32_t kC538FoldCalls = 4096;
constexpr ULONGLONG kC538ReportMs = 60000;

std::atomic<uint32_t> g_c538_frame{1}; // game thread writes, every thread reads

struct C538Pair {
    const void* self;
    const void* target;
    uint32_t frame;
};

struct C538Triple {
    const void* self;
    const void* cond;
    const void* target;
    uint32_t frame;
    bool result;
};

struct C538Counts {
    uint32_t calls, true_results, same_target, same_cond, differ, served, verified;
};

struct C538Thread {
    C538Pair pair[kC538Slots];
    C538Triple triple[kC540Slots];
    C538Counts n;
    uint32_t verify_tick;
};
thread_local C538Thread t_c538{};

struct C538Shared {
    std::atomic<uint64_t> calls, true_results, same_target, same_cond, differ, served, verified;
};
C538Shared g_c538{};
ULONGLONG g_c538_report_at = 0;  // game thread
uint32_t g_c538_frame_at = 0;    // game thread: frame at the last report
uint64_t g_c538_calls_at = 0;    // game thread: calls at the last report

uint32_t c538_slot(uintptr_t a, uintptr_t b, uintptr_t c, uint32_t slots) {
    const uint64_t key = (a >> 4) ^ (b << 7) ^ (c << 19);
    return static_cast<uint32_t>(key * 0x9E3779B97F4A7C15ULL >> 40) & (slots - 1);
}

void c538_fold(C538Thread& t) {
    g_c538.calls.fetch_add(t.n.calls, std::memory_order_relaxed);
    g_c538.true_results.fetch_add(t.n.true_results, std::memory_order_relaxed);
    g_c538.same_target.fetch_add(t.n.same_target, std::memory_order_relaxed);
    g_c538.same_cond.fetch_add(t.n.same_cond, std::memory_order_relaxed);
    g_c538.differ.fetch_add(t.n.differ, std::memory_order_relaxed);
    g_c538.served.fetch_add(t.n.served, std::memory_order_relaxed);
    g_c538.verified.fetch_add(t.n.verified, std::memory_order_relaxed);
    t.n = C538Counts{};
}

} // namespace

// Card 540: a same-frame repeat returns the stored result, except one in kC540VerifyEvery, which calls the original.
bool c540_lookup(C538Thread& t, const C538Triple& q, const void* self, const void* cond, void* target,
                 uint32_t frame, bool* result) {
    const bool repeat = q.self == self && q.cond == cond && q.target == target && q.frame == frame;
    t.n.same_cond += repeat ? 1 : 0;
    if (repeat && g_c540_on && (++t.verify_tick & (kC540VerifyEvery - 1)) != 0) {
        ++t.n.served;
        *result = q.result;
        return true;
    }
    *result = g_orig_c538_conditions(self, cond, target);
    t.n.verified += repeat && g_c540_on ? 1 : 0;
    t.n.differ += (repeat && q.result != *result) ? 1 : 0;
    return false;
}

bool detour_c538_conditions(const void* self, const void* cond, void* target) {
    C538Thread& t = t_c538;
    const uint32_t frame = g_c538_frame.load(std::memory_order_relaxed);
    const uintptr_t s = reinterpret_cast<uintptr_t>(self);
    const uintptr_t c = reinterpret_cast<uintptr_t>(cond);
    const uintptr_t g = reinterpret_cast<uintptr_t>(target);
    if (!g_c540_on) { // the same-target count is card 538's diagnostic; the memo skips its cache line
        C538Pair& p = t.pair[c538_slot(s, g, 0, kC538Slots)];
        t.n.same_target += (p.self == self && p.target == target && p.frame == frame) ? 1 : 0;
        p = C538Pair{self, target, frame};
    }
    C538Triple& q = t.triple[c538_slot(s, g, c, kC540Slots)];
    bool result = false;
    if (!c540_lookup(t, q, self, cond, target, frame, &result)) {
        q = C538Triple{self, cond, target, frame, result};
    }
    t.n.true_results += result ? 1 : 0;
    if (++t.n.calls >= kC538FoldCalls) {
        c538_fold(t);
    }
    return result;
}

// Game thread, per frame (c42_step): advances the frame, totals every kC538ReportMs once calls happen.
void c538_tick(ULONGLONG now) {
    if (!g_c538_on) {
        return;
    }
    const uint32_t frame = g_c538_frame.fetch_add(1, std::memory_order_relaxed) + 1;
    if (now < g_c538_report_at) {
        return;
    }
    g_c538_report_at = now + kC538ReportMs;
    const uint64_t calls = g_c538.calls.load(std::memory_order_relaxed);
    const uint32_t frames = frame - g_c538_frame_at;
    const uint64_t window = calls - g_c538_calls_at;
    g_c538_frame_at = frame;
    g_c538_calls_at = calls;
    if (calls == 0) {
        return;
    }
    log::line("card538: %s t=%llu DoesTargetMeetConditions: calls %llu (%llu in %u frames, %.1f per frame), true %llu, "
              "same target in frame %llu (%.1f%%), same conditions in frame %llu (%.1f%%), of those differ %llu; card540 memo %s, served %llu, "
              "verified %llu",
              g_client_session ? "client" : "host", static_cast<unsigned long long>(now),
              static_cast<unsigned long long>(calls), static_cast<unsigned long long>(window), frames,
              frames != 0 ? static_cast<double>(window) / frames : 0.0,
              static_cast<unsigned long long>(g_c538.true_results.load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_c538.same_target.load(std::memory_order_relaxed)),
              100.0 * g_c538.same_target.load(std::memory_order_relaxed) / calls,
              static_cast<unsigned long long>(g_c538.same_cond.load(std::memory_order_relaxed)),
              100.0 * g_c538.same_cond.load(std::memory_order_relaxed) / calls,
              static_cast<unsigned long long>(g_c538.differ.load(std::memory_order_relaxed)), g_c540_on ? "on" : "off",
              static_cast<unsigned long long>(g_c538.served.load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_c538.verified.load(std::memory_order_relaxed)));
}

} // namespace ubw::hooks
