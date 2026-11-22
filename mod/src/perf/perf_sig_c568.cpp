// Card 568 (performance mod family, card 567 target 1): UpdateSignificanceManager ran every frame on both sides
// (client: the co-op client significance step, 16.5 ms/s average, 54 ms/s in combat; host: the game mode tick's tail
// jump, cost unmeasured). It runs at a fixed kC568PeriodMs instead; between runs the manager keeps its last
// significance values, so NPC tick buckets and LODs change at most one period late. A new world always runs at once.
// UBWCOOP_C568_OFF=1: every frame. Moved from co-op enemies.cpp by card 578; co-op calls it through
// UbwPerfApi::c568_due, standalone through perf_main.cpp's own UpdateSignificanceManager row.

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

bool g_c568_on = true; // written before any hook

namespace {

constexpr int64_t kC568PeriodMs = 33; // 30 Hz
constexpr int32_t kC568Slots = 2;     // [0] host game mode, [1] client step
struct C568Gate {
    int64_t next_qpc;
    const void* world;
    uint32_t runs;
    uint32_t skips;
};
C568Gate g_c568_gate[kC568Slots] = {};

} // namespace

bool c568_due_slot(int32_t slot, const void* world) {
    if (!g_c568_on || slot < 0 || slot >= kC568Slots) {
        return true;
    }
    C568Gate* const gate = &g_c568_gate[slot];
    LARGE_INTEGER now;
    LARGE_INTEGER freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    if (world != gate->world) {
        log::line("card568: %s significance throttle %lld ms: new world, last world %u run(s), %u skipped", side(),
                  static_cast<long long>(kC568PeriodMs), gate->runs, gate->skips);
        *gate = C568Gate{0, world, 0, 0};
    }
    if (now.QuadPart < gate->next_qpc) {
        ++gate->skips;
        return false;
    }
    ++gate->runs;
    gate->next_qpc = now.QuadPart + freq.QuadPart * kC568PeriodMs / 1000;
    return true;
}

} // namespace ubw::hooks
