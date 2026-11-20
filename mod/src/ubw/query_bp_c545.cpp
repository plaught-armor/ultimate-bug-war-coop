// Card 545 (performance mod family, card 527): who makes the host's costliest Kismet queries.
//
// c263L362 host callers (card 543 step 1): UKismetSystemLibrary::CapsuleTraceSingleForObjects is the largest host
// query at 696.4 ms per 30 s window, and SphereOverlapComponents costs 140.2 ms at 1507 us mean. b360 hooked the
// Blueprint exec thunks and saw no call in c263L366, so the calls are native. A static scan of the exe finds direct
// calls from USTGroundedMovementControl::Tick_DirectMovement, several USTFlyingMovementControl functions,
// UADShapeUtilityLibrary's exec thunks, ASTGorePartBase::OnPostLoad and the Kismet exec thunks. Since b361 the
// detours sit on the two native functions themselves. Rows are keyed by (entry, return address, world context class),
// with calls, total and max time on the game thread, and reported every 30 s like card 534, so the totals line up with
// card534 rows of the same run. The return address is printed as an exe RVA, to symbolize offline.
// Rows hold a weak reference to the class, so the report never names an unloaded (streamed Blueprint) class.
// The counts change no result. Gate: UBWCOOP_C132_FX=1 (diag runs).

#include "ubw/hooks_internal.h"

#include <algorithm> // std::sort
#include <atomic>
#include <intrin.h> // _ReturnAddress

namespace ubw::hooks {

C545CapsuleFn g_orig_c545_capsule = nullptr;
C545SphereFn g_orig_c545_sphere = nullptr;

namespace {

constexpr ULONGLONG kC545WindowMs = 30000;
constexpr uint32_t kC545Slots = 512; // power of two
constexpr uint32_t kC545Probe = 16;
constexpr int32_t kC545TopRows = 16;
constexpr const char* kC545Name[kC545Entries] = {"CapsuleTraceSingleForObjects", "SphereOverlapComponents"};

struct C545Row {
    const void* ret; // return address; nullptr = empty slot
    const void* cls; // class of the world context object
    int32_t entry;
    uint32_t calls;
    int64_t ticks;
    int64_t max;
    C31Weak weak_cls;
};

struct C545State {
    C545Row row[kC545Slots];
    uint32_t dropped; // calls whose probe found no slot
    uint32_t calls[kC545Entries];
    int64_t ticks[kC545Entries];
    int64_t window_t0;
    ULONGLONG window_ms0;
};
C545State g_c545{};
std::atomic<uint32_t> g_c545_other[kC545Entries] = {};
std::atomic<DWORD> g_c545_game_thread{0}; // set by c545_tick; 0 until the first frame

int64_t c545_now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

double c545_ms(int64_t ticks) {
    static const double kPerMs = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart) / 1000.0;
    }();
    return static_cast<double>(ticks) / kPerMs;
}

void c545_add(int32_t entry, const void* ret, const void* context, int64_t d) {
    g_c545.calls[entry] += 1;
    g_c545.ticks[entry] += d;
    const void* const cls = context != nullptr ? read_ptr(context, kObjectClass) : nullptr;
    const uint64_t h = (reinterpret_cast<uintptr_t>(ret) ^ reinterpret_cast<uintptr_t>(cls) * 31 ^
                        static_cast<uint64_t>(entry)) * 0x9E3779B97F4A7C15ULL;
    uint32_t i = static_cast<uint32_t>(h >> 55) & (kC545Slots - 1);
    for (uint32_t p = 0; p < kC545Probe; ++p, i = (i + 1) & (kC545Slots - 1)) {
        C545Row& r = g_c545.row[i];
        if (r.ret == nullptr) {
            r.ret = ret;
            r.cls = cls;
            r.entry = entry;
            if (cls != nullptr) {
                c31_weak_set(r.weak_cls, const_cast<void*>(cls));
            }
        } else if (r.ret != ret || r.cls != cls || r.entry != entry) {
            continue;
        }
        ++r.calls;
        r.ticks += d;
        r.max = d > r.max ? d : r.max;
        return;
    }
    ++g_c545.dropped;
}

void c545_report(ULONGLONG now, int64_t t) {
    const char* const side = g_client_session ? "client" : "host";
    const double window = c545_ms(t - g_c545.window_t0);
    C545Row* top[kC545TopRows] = {};
    int32_t n = 0;
    for (C545Row& r : g_c545.row) {
        if (r.ret == nullptr) {
            continue;
        }
        if (n < kC545TopRows) {
            top[n++] = &r;
        } else if (r.ticks > top[n - 1]->ticks) {
            top[n - 1] = &r;
        } else {
            continue;
        }
        std::sort(top, top + n, [](const C545Row* x, const C545Row* y) { return x->ticks > y->ticks; });
    }
    for (int32_t e = 0; e < kC545Entries; ++e) {
        const uint32_t other = g_c545_other[e].exchange(0, std::memory_order_relaxed);
        if (g_c545.calls[e] == 0 && other == 0) {
            continue;
        }
        log::line("card545: %s t=%llu kismet query total %s: game thread %u calls %.1f ms (%.2f%% of %.0f ms); other "
                  "threads %u calls", side, static_cast<unsigned long long>(now), kC545Name[e], g_c545.calls[e],
                  c545_ms(g_c545.ticks[e]), 100.0 * c545_ms(g_c545.ticks[e]) / window, window, other);
    }
    for (int32_t i = 0; i < n; ++i) {
        const C545Row& r = *top[i];
        const bool cls_live = r.cls != nullptr && c31_weak_get(r.weak_cls) == r.cls;
        const uintptr_t base = reinterpret_cast<uintptr_t>(ue::hooktable::g_exe_base);
        const uintptr_t ret = reinterpret_cast<uintptr_t>(r.ret);
        log::line("card545: %s t=%llu kismet query top %d: %s from exe+0x%llx on %s: %u calls, %.1f ms (%.2f%%), "
                  "%.1f us mean, %.1f us max", side, static_cast<unsigned long long>(now), i + 1, kC545Name[r.entry],
                  static_cast<unsigned long long>(ret >= base ? ret - base : 0),
                  cls_live ? object_name(r.cls).text : r.cls != nullptr ? "(class unloaded)" : "(no object)",
                  r.calls, c545_ms(r.ticks), 100.0 * c545_ms(r.ticks) / window,
                  1000.0 * c545_ms(r.ticks) / static_cast<double>(r.calls), 1000.0 * c545_ms(r.max));
    }
    if (g_c545.dropped != 0) {
        log::line("WARN card545: %s %u calls not keyed (table of %u full)", side, g_c545.dropped,
                  kC545Slots);
    }
}

} // namespace

// The exact signatures: FVector and FLinearColor arguments pass by pointer, floats in their own slots.
bool detour_c545_capsule(const void* context, const void* start, const void* end, float radius, float half_height,
                         const void* types, bool complex, const void* ignore, int32_t draw, void* hit, bool ignore_self,
                         const void* color, const void* hit_color, float draw_time) {
    const DWORD game_thread = g_c545_game_thread.load(std::memory_order_relaxed);
    if (game_thread == 0 || GetCurrentThreadId() != game_thread) {
        g_c545_other[0].fetch_add(1, std::memory_order_relaxed);
        return g_orig_c545_capsule(context, start, end, radius, half_height, types, complex, ignore, draw, hit,
                                   ignore_self, color, hit_color, draw_time);
    }
    const void* const ret = _ReturnAddress();
    const int64_t t0 = c545_now();
    const bool r = g_orig_c545_capsule(context, start, end, radius, half_height, types, complex, ignore, draw, hit,
                                       ignore_self, color, hit_color, draw_time);
    c545_add(0, ret, context, c545_now() - t0);
    return r;
}

bool detour_c545_sphere(const void* context, const void* position, float radius, const void* types,
                        const void* class_filter, const void* ignore, void* out) {
    const DWORD game_thread = g_c545_game_thread.load(std::memory_order_relaxed);
    if (game_thread == 0 || GetCurrentThreadId() != game_thread) {
        g_c545_other[1].fetch_add(1, std::memory_order_relaxed);
        return g_orig_c545_sphere(context, position, radius, types, class_filter, ignore, out);
    }
    const void* const ret = _ReturnAddress();
    const int64_t t0 = c545_now();
    const bool r = g_orig_c545_sphere(context, position, radius, types, class_filter, ignore, out);
    c545_add(1, ret, context, c545_now() - t0);
    return r;
}

// Game thread, per frame (c42_step).
void c545_tick(ULONGLONG now) {
    if (!g_c132_fx_log) {
        return;
    }
    g_c545_game_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    const int64_t t = c545_now();
    if (g_c545.window_ms0 == 0) {
        g_c545.window_ms0 = now;
        g_c545.window_t0 = t;
        return;
    }
    if (now - g_c545.window_ms0 < kC545WindowMs) {
        return;
    }
    c545_report(now, t);
    g_c545 = C545State{};
    g_c545.window_ms0 = now;
    g_c545.window_t0 = t;
}

} // namespace ubw::hooks
