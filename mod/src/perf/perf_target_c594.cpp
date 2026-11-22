// Card 594 step 1 (performance mod family, host diag, no behavior change): how much work the host's
// UADTargetComponent target gather does per tick.
//
// UADTargetComponent::TickComponent (RVA 0x051AC040) calls FindPrimaryTargetData. With a primary it calls
// UpdateTargetData(primary) (return address RVA 0x051AC0A6); without one it zeroes +0x1A0 and calls
// GatherTargetsInRange (return address RVA 0x051AC120). Co-op dwmapi.dll already hooks TickComponent (card 532), so
// this dll counts ticks from those two calls: ticks = primary updates + gathers with no primary.
//
// GatherTargetsInRange: GetAllAbilitySystemActors into a local array (A = its length), a squared range test against
// +0x1A8 per actor, USTSaveComponent::GetSaveId per in-range live actor (R, counted in the card 531 detour while a
// gather runs on that thread), a set lookup at +0xF0, and FindOrAddActor for each actor not yet in the set (N).
// UpdateTargetData writes IsEnemyFaction(faction, target) + 1 to FADTargetData+0x4A (1 = not enemy or no faction
// component, 2 = enemy); the row counts both values after the original returns.
//
// One `card594:` row every 600 frames (perf_tick). Host only: the client returns straight to the originals.
// UBWCOOP_C594_OFF=1: none of the five hooks is installed.
//
// Step 2 (an IsEnemyFaction memo, raw RVA 0x04A27320) was removed in b438: the c593b437ca/b bug-wave A/B
// measured 14 of 8389 host GameThread samples with it on against 12 of 8623 off, under the 30 % bar (card 594).

#include "ubw/hooks_internal.h"

#include <atomic>
#include <intrin.h>
#include <string.h>

namespace ubw::hooks {

C594GatherFn g_orig_c594_gather = nullptr;
C594UpdateFn g_orig_c594_update = nullptr;
C594FindOrAddFn g_orig_c594_find_or_add = nullptr;
C594GasActorsFn g_orig_c594_gas_actors = nullptr;
bool g_c594_on = true; // read_perf_flags, before any hook (UBWCOOP_C594_OFF=1 turns it off)

namespace {

constexpr uintptr_t kC594PrimaryUpdateRet = 0x051AC0A6; // TickComponent, after UpdateTargetData(primary)
constexpr uintptr_t kC594NoPrimaryGatherRet = 0x051AC120; // TickComponent, after GatherTargetsInRange
constexpr size_t kC594Faction = 0x4A;     // FADTargetData: IsEnemyFaction + 1
constexpr size_t kC594ArrayNum = 0x08;    // TArray: Num
constexpr uint32_t kC594RowFrames = 600;

struct C594Counts {
    std::atomic<uint64_t> gathers, gathers_no_primary, gathers_other, gas_actors, in_range, adds;
    std::atomic<uint64_t> updates, updates_primary, not_enemy, enemy, other_faction;
};
C594Counts g_c594{};
thread_local bool t_c594_in_gather = false;
uint32_t g_c594_frames = 0; // game thread

uintptr_t c594_ret_rva(void* ret) {
    return reinterpret_cast<uintptr_t>(ret) - reinterpret_cast<uintptr_t>(ue::hooktable::g_exe_base);
}

void c594_add(std::atomic<uint64_t>& n, uint64_t v) {
    n.fetch_add(v, std::memory_order_relaxed);
}

uint64_t c594_take(std::atomic<uint64_t>& n) {
    return n.exchange(0, std::memory_order_relaxed);
}

} // namespace

void detour_c594_gather(void* self) {
    if (g_client_session) {
        g_orig_c594_gather(self);
        return;
    }
    const uintptr_t ret = c594_ret_rva(_ReturnAddress());
    c594_add(g_c594.gathers, 1);
    c594_add(ret == kC594NoPrimaryGatherRet ? g_c594.gathers_no_primary : g_c594.gathers_other, 1);
    t_c594_in_gather = true;
    g_orig_c594_gather(self);
    t_c594_in_gather = false;
}

void detour_c594_update(void* self, void* data, float delta_seconds) {
    g_orig_c594_update(self, data, delta_seconds);
    if (g_client_session) {
        return;
    }
    c594_add(g_c594.updates, 1);
    if (c594_ret_rva(_ReturnAddress()) == kC594PrimaryUpdateRet) {
        c594_add(g_c594.updates_primary, 1);
    }
    uint8_t faction = 0;
    memcpy(&faction, static_cast<const uint8_t*>(data) + kC594Faction, 1);
    c594_add(faction == 1 ? g_c594.not_enemy : faction == 2 ? g_c594.enemy : g_c594.other_faction, 1);
}

void* detour_c594_find_or_add(void* self, void* actor) {
    if (!t_c594_in_gather) {
        return g_orig_c594_find_or_add(self, actor);
    }
    c594_add(g_c594.adds, 1);
    t_c594_in_gather = false; // FindOrAddActor calls GetSaveId again: keep R one count per in-range actor
    void* const entry = g_orig_c594_find_or_add(self, actor);
    t_c594_in_gather = true;
    return entry;
}

void detour_c594_gas_actors(const void* subsystem, void* out_actors) {
    g_orig_c594_gas_actors(subsystem, out_actors);
    if (!t_c594_in_gather) {
        return;
    }
    int32_t num = 0;
    memcpy(&num, static_cast<const uint8_t*>(out_actors) + kC594ArrayNum, sizeof(num));
    c594_add(g_c594.gas_actors, num > 0 ? static_cast<uint64_t>(num) : 0);
}

void c594_note_save_id() {
    if (t_c594_in_gather) {
        c594_add(g_c594.in_range, 1);
    }
}

void c594_tick() {
    if (!g_c594_on || g_client_session || ++g_c594_frames < kC594RowFrames) {
        return;
    }
    g_c594_frames = 0;
    const uint64_t g = c594_take(g_c594.gathers), p = c594_take(g_c594.gathers_no_primary);
    const uint64_t go = c594_take(g_c594.gathers_other), a = c594_take(g_c594.gas_actors);
    const uint64_t r = c594_take(g_c594.in_range), n = c594_take(g_c594.adds);
    const uint64_t u = c594_take(g_c594.updates), up = c594_take(g_c594.updates_primary);
    const uint64_t f = c594_take(g_c594.not_enemy), e = c594_take(g_c594.enemy);
    const uint64_t fo = c594_take(g_c594.other_faction);
    log::line("card594: host %u frames: ticks %llu (primary updates %llu + gathers with no primary %llu), gathers %llu "
              "(other callers %llu), GAS actors scanned %llu, in range %llu%s, adds %llu, UpdateTargetData %llu, "
              "not enemy %llu, enemy %llu, other faction byte %llu",
              kC594RowFrames, up + p, up, p, g, go, a, r, g_c531_on ? "" : " (card 531 off: not counted)", n, u, f, e,
              fo);
}

} // namespace ubw::hooks
