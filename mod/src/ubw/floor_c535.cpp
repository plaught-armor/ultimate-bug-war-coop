// Card 535 (performance mod family, card 527): client NPC proxies skip floor sweeps that just found nothing.
//
// On the client, UCharacterMovementComponent::SimulateMovement runs for every NPC simulated proxy each frame and calls
// FindFloor, which sweeps the capsule down (ComputeFloorDist -> FloorSweepTest -> SweepSingleByChannel).
// c263L349/L350 (b343/b345, card 534 rows): the client FloorSweepTest row is 271904 calls, 3102 ms in the 30 s peak
// window (10.3% of the GameThread), 18 times the host's calls.
//
// v1 (b346) skipped every sweep of a NavWalking proxy, on the premise that NavWalking ignores WorldStatic and
// WorldDynamic so the sweep never finds a floor. c263L352 refuted it: 50% of the verify checks found a blocking,
// walkable floor. A hidden walkable floor sends SimulateMovement down the no-floor branch (fall velocity,
// SetMovementMode(Falling)) instead of AdjustFloorHeight and SetBase, so v1 changed what the client draws.
//
// v2 (b348): per proxy. A real sweep that finds no blocking hit arms kC535SkipAfterMiss skips for that proxy's
// movement component; a skip writes the same no-hit result (FFindFloorResult::Clear, the early out's stores at exe
// 0x143a2b6a7..0x143a2b710). A proxy whose last real sweep hit anything always sweeps. So a floor that appears under a
// proxy with no floor is seen at most kC535SkipAfterMiss calls late (3 frames, 50 ms at 60 fps; the latency target is
// 100 ms). Only the client, only owners with LocalRole == SimulatedProxy, only MovementMode Walking or NavWalking.
// Host decisions do not change (the host never takes this path).
//
// Verify: every kC535VerifyEvery-th skip runs the original instead (into the real result) and counts results with
// bBlockingHit or bWalkableFloor set: floors the skip would have hidden. Such a hit also disarms the proxy.
// Table: kC535Slots direct-mapped slots keyed by the component address, game thread only (SimulateMovement). A slot
// taken over by another component, or a component freed and reused at the same address, can only cost a sweep or
// give at most kC535SkipAfterMiss skips, the same bound.
//
// Not done: the cached-floor path's bForceNextFloorCheck update (+0x2e9 bit 0x10, only with bCanUseCached and a
// moving base). Leaving the bit as it is can only force a later full check.
// Off switch for A/B runs: UBWCOOP_C535_OFF=1 (the detour is then not installed).

#include "ubw/hooks_internal.h"

#include <atomic>
#include <string.h>

namespace ubw::hooks {

FindFloorFn g_orig_c535_find_floor = nullptr;
bool g_c535_on = true; // written in ubw::init before any hook (UBWCOOP_C535_OFF=1 turns it off)

namespace {

// FFindFloorResult stores, as FindFloor's early out writes them.
constexpr size_t kC535Flags = 0x00;        // uint32 bit field: bBlockingHit 1, bWalkableFloor 2, bLineTrace 4
constexpr uint32_t kC535FlagBits = 7;
constexpr size_t kC535Dists = 0x04;        // float FloorDist, float LineDist
constexpr size_t kC535Hit = 0x10;          // FHitResult, 0x100 bytes zeroed
constexpr size_t kC535HitSize = 0x100;
constexpr size_t kC535HitTime = 0x14;      // float Time = 1.0
constexpr size_t kC535HitItem = 0xb4;      // int32 = -1
constexpr size_t kC535HitZero16 = 0xc8;    // 16 bytes 0
constexpr size_t kC535HitHandle = 0xd8;    // {uint64 0, int32 -1, uint32 0}
constexpr uint32_t kC535VerifyEvery = 256;
constexpr uint32_t kC535SkipAfterMiss = 3;
constexpr uint32_t kC535Slots = 1024; // power of two
constexpr ULONGLONG kC535ReportMs = 60000;

struct C535Counts {
    std::atomic<uint64_t> sweeps, misses, skips, checks, blocking, walkable;
};
C535Counts g_c535{};

struct C535Slot {
    const void* cmc;
    uint32_t skips_left;
};
C535Slot g_c535_slots[kC535Slots] = {}; // game thread (SimulateMovement)
uint32_t g_c535_verify = 0;             // game thread
ULONGLONG g_c535_report_at = 0;    // game thread

void c535_store_u32(uint8_t* out, size_t at, uint32_t v) { memcpy(out + at, &v, sizeof(v)); }

// The no-floor result, byte for byte as the early out (FFindFloorResult::Clear) writes it.
void c535_clear(uint8_t* out) {
    uint32_t flags = 0;
    memcpy(&flags, out + kC535Flags, sizeof(flags));
    c535_store_u32(out, kC535Flags, flags & ~kC535FlagBits);
    memset(out + kC535Dists, 0, 8);
    memset(out + kC535Hit, 0, kC535HitSize);
    c535_store_u32(out, kC535HitItem, 0xFFFFFFFFu);
    const float one = 1.0f;
    memcpy(out + kC535HitTime, &one, sizeof(one));
    memset(out + kC535HitZero16, 0, 16);
    memset(out + kC535HitHandle, 0, 16);
    c535_store_u32(out, kC535HitHandle + 8, 0xFFFFFFFFu);
}

bool c535_proxy(const void* cmc) {
    const uint8_t mode = static_cast<const uint8_t*>(cmc)[kCmcMovementMode];
    if (!g_client_session || (mode != kMoveWalking && mode != kMoveNavWalking)) {
        return false;
    }
    const void* const owner = read_ptr(cmc, kCmcCharacterOwner);
    return owner != nullptr && static_cast<const uint8_t*>(owner)[kActorLocalRole] == kRoleSimulatedProxy;
}

C535Slot& c535_slot(const void* cmc) {
    return g_c535_slots[(reinterpret_cast<uintptr_t>(cmc) >> 4) * 0x9E3779B97F4A7C15ULL >> 54 & (kC535Slots - 1)];
}

uint32_t c535_flags(const void* out) {
    uint32_t flags = 0;
    memcpy(&flags, out, sizeof(flags));
    return flags;
}

} // namespace

void detour_c535_find_floor(const void* cmc, const void* location, void* out, bool can_use_cached,
                            const void* downward) {
    if (!c535_proxy(cmc)) {
        g_orig_c535_find_floor(cmc, location, out, can_use_cached, downward);
        return;
    }
    C535Slot& slot = c535_slot(cmc);
    if (slot.cmc == cmc && slot.skips_left != 0) {
        --slot.skips_left;
        if (++g_c535_verify < kC535VerifyEvery) {
            c535_clear(static_cast<uint8_t*>(out));
            g_c535.skips.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        g_c535_verify = 0;
        g_orig_c535_find_floor(cmc, location, out, can_use_cached, downward);
        const uint32_t flags = c535_flags(out);
        g_c535.checks.fetch_add(1, std::memory_order_relaxed);
        g_c535.blocking.fetch_add(flags & 1, std::memory_order_relaxed);
        g_c535.walkable.fetch_add((flags >> 1) & 1, std::memory_order_relaxed);
        slot.skips_left = (flags & 3) != 0 ? 0 : slot.skips_left;
        return;
    }
    g_orig_c535_find_floor(cmc, location, out, can_use_cached, downward);
    const bool miss = (c535_flags(out) & 3) == 0;
    slot.cmc = cmc;
    slot.skips_left = miss ? kC535SkipAfterMiss : 0;
    g_c535.sweeps.fetch_add(1, std::memory_order_relaxed);
    g_c535.misses.fetch_add(miss ? 1 : 0, std::memory_order_relaxed);
}

// Game thread, per frame (c42_step): totals every kC535ReportMs once proxy sweeps happen.
void c535_tick(ULONGLONG now) {
    if (!g_c535_on || now < g_c535_report_at) {
        return;
    }
    g_c535_report_at = now + kC535ReportMs;
    const uint64_t skips = g_c535.skips.load(std::memory_order_relaxed);
    if (skips == 0 && g_c535.sweeps.load(std::memory_order_relaxed) == 0) {
        return;
    }
    log::line("card535: %s t=%llu FindFloor NPC proxies: real sweeps %llu (no hit %llu), skips %llu, "
              "checks %llu (every %u skips), found blocking %llu, walkable %llu",
              g_client_session ? "client" : "host", static_cast<unsigned long long>(now),
              static_cast<unsigned long long>(g_c535.sweeps.load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_c535.misses.load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(skips),
              static_cast<unsigned long long>(g_c535.checks.load(std::memory_order_relaxed)), kC535VerifyEvery,
              static_cast<unsigned long long>(g_c535.blocking.load(std::memory_order_relaxed)),
              static_cast<unsigned long long>(g_c535.walkable.load(std::memory_order_relaxed)));
}

} // namespace ubw::hooks
