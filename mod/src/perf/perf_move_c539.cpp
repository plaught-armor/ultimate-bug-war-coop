// Card 539 (performance mod family, card 527): diagnostic counts of client NPC proxy capsule sweeps.
//
// c263L356 (b351, card 534 rows, client): ComponentSweepMulti by BP_AI_WarriorBug_C is the largest client query,
// 82993 calls, 1550.5 ms (5.17% of the GameThread), 18.7 us mean. On the client those are simulated proxies moved by
// SimulateMovement -> MoveSmooth -> SafeMoveUpdatedComponent (sweep) -> UPrimitiveComponent::MoveComponentImpl. The
// host's NPC is the authority, and its location replicates. A card 535 style lever would move a proxy unswept while
// its last sweeps were unblocked. These counts decide whether that is safe:
// - sweeps: client MoveComponentImpl calls with bSweep on a component whose owner is an NPC simulated proxy, with
//   a nonzero delta and an out hit;
// - blocked: the out hit has Time < 1 (a blocking hit, including initial penetration at Time 0);
// - after free: sweeps whose previous sweep of the same component was unblocked; blocked after free is how many of
//   those were blocked, which a skip would have missed (one direct-mapped slot per component, game thread).
// - by what (c263L359 showed 46% of proxy sweeps blocked): each blocked hit's component owner is an NPC, another pawn
//   (a player), or neither (the world); start penetrating counts hits that began inside the blocker.
// - blocker types: each blocked hit's component ObjectType (ECollisionChannel byte), 32 buckets.
// The counts change no result. The hook row is card 461's (detour_c461_move calls c539_move outside its release
// window). Off switch: UBWCOOP_C539_OFF=1.
//
// Card 542 (same hook): c263L361 showed 86.1% of blocked proxy sweeps hit another NPC and 92.8% started penetrating.
// The host owns NPC positions and keeps bugs apart, so on the client a proxy capsule blocked by a neighbour only lags
// behind the host position, at the cost of a penetrating sweep. b357 assumed object type PhysicsBody (5) from the
// NPC_CapsuleCollider_IsNotDead profile; c263L363 showed the NPC blockers are object type Pawn (2) (the Blueprint
// overrides the capsule), so b357 never applied. Since b358 the lever keys on the proxy capsule's own object type:
// bugs block each other because they share it. Before a client NPC proxy capsule moves, if its own type is not a
// world type (WorldStatic 0, WorldDynamic 1) or Player (15), and its response to its own type is not Ignore, that
// response is set to Ignore through the game's UPrimitiveComponent::SetCollisionResponseToChannel (which updates the
// physics filter). One side ignoring is enough for a query not to block. Player capsules keep blocking bugs and the
// world is untouched. A profile change by the game (dying, dead) overwrites the setting; the next move re-applies it.
// The row counts applied calls per own type, and moves skipped because the own type is a kept one.
// Off switch: UBWCOOP_C542_OFF=1.

#include "ubw/hooks_internal.h"

#include <string.h>

namespace ubw::hooks {

bool g_c539_on = true;   // written in ubw::init before any hook (UBWCOOP_C539_OFF=1 turns it off)
bool g_c542_on = true;   // written in ubw::init before any hook (UBWCOOP_C542_OFF=1 turns it off)

namespace {

constexpr size_t kC539HitTime = 0x04; // FHitResult::Time (float; 1 when nothing blocked)
constexpr uint32_t kC539Slots = 1024; // power of two
constexpr ULONGLONG kC539ReportMs = 60000;
constexpr uint32_t kC539Channels = 32;     // ECollisionChannel values (ECC_MAX)
constexpr uint8_t kC542WorldDynamic = 1;   // ECC_WorldStatic 0 and ECC_WorldDynamic 1 are kept
constexpr uint8_t kC542Player = 15;        // ECC_GameTraceChannel2, the game's "Player" object type, is kept
constexpr uint8_t kC542Ignore = 0;         // ECR_Ignore

using SetResponseToChannelFn = void (*)(void* component, int32_t channel, int32_t response);

struct C539Counts {
    uint64_t sweeps, blocked, after_free, blocked_after_free, by_npc, by_pawn, by_world, start_pen;
    uint64_t blocker_type[kC539Channels];
    uint64_t c542_applied[kC539Channels]; // by the proxy capsule's own object type
    uint64_t c542_kept_type;               // moves whose own type is a world type or Player
};
C539Counts g_c539{};          // game thread
ULONGLONG g_c539_report_at = 0; // game thread

struct C539Slot {
    const void* comp;
    bool blocked;
};
C539Slot g_c539_slots[kC539Slots] = {}; // game thread

bool c539_proxy_npc(const void* comp) {
    const auto* const owner = static_cast<const uint8_t*>(read_ptr(comp, kComponentOwner));
    return owner != nullptr && owner[kActorLocalRole] == kRoleSimulatedProxy && g_npc_class != nullptr &&
           is_a(owner, g_npc_class());
}

bool c539_nonzero(const void* delta) {
    double v[3] = {};
    memcpy(v, delta, sizeof(v));
    return v[0] != 0.0 || v[1] != 0.0 || v[2] != 0.0;
}

// Classifies a blocked hit by its component's owner (game thread; the weak pointer resolves through GUObjectArray).
void c539_blocker(const void* hit) {
    const auto* const h = static_cast<const uint8_t*>(hit);
    g_c539.start_pen += (h[kHitFlags] & kHitStartPenetrating) != 0 ? 1 : 0;
    const void* const comp = g_weak_get != nullptr ? g_weak_get(h + kHitComponent) : nullptr;
    const void* const owner = comp != nullptr ? read_ptr(comp, kComponentOwner) : nullptr;
    if (comp != nullptr) {
        ++g_c539.blocker_type[static_cast<const uint8_t*>(comp)[kBodyObjectType] & (kC539Channels - 1)];
    }
    if (owner != nullptr && g_npc_class != nullptr && is_a(owner, g_npc_class())) {
        ++g_c539.by_npc;
    } else if (owner != nullptr && g_pawn_class != nullptr && is_a(owner, g_pawn_class())) {
        ++g_c539.by_pawn;
    } else {
        ++g_c539.by_world;
    }
}

// Card 542: a client NPC proxy capsule stops blocking its own object type (other bug capsules).
void c542_ignore_bugs(void* comp) {
    const auto* const c = static_cast<const uint8_t*>(comp);
    const uint8_t own = c[kBodyObjectType] & (kC539Channels - 1);
    if (own <= kC542WorldDynamic || own == kC542Player) {
        ++g_c539.c542_kept_type;
        return;
    }
    if (c[kBodyResponses + own] == kC542Ignore) {
        return;
    }
    const auto set = reinterpret_cast<SetResponseToChannelFn>(
        reinterpret_cast<uintptr_t>(ue::hooktable::g_exe_base) +
        offsets::UPrimitiveComponent_SetCollisionResponseToChannel.rva);
    set(comp, own, kC542Ignore);
    ++g_c539.c542_applied[own];
}

} // namespace

bool c539_move(void* comp, const void* delta, const void* rotation, bool sweep, void* hit, int32_t flags,
               uint8_t teleport) {
    if (!g_client_session || !sweep || comp == nullptr || !c539_proxy_npc(comp)) {
        return g_orig_c461_move(comp, delta, rotation, sweep, hit, flags, teleport);
    }
    if (g_c542_on) {
        c542_ignore_bugs(comp);
    }
    const bool out = g_orig_c461_move(comp, delta, rotation, sweep, hit, flags, teleport);
    if (!g_c539_on || hit == nullptr || !c539_nonzero(delta)) {
        return out;
    }
    float time = 1.0f;
    memcpy(&time, static_cast<const uint8_t*>(hit) + kC539HitTime, sizeof(time));
    const bool blocked = time < 1.0f;
    C539Slot& slot = g_c539_slots[(reinterpret_cast<uintptr_t>(comp) >> 4) * 0x9E3779B97F4A7C15ULL >> 40 &
                                  (kC539Slots - 1)];
    if (slot.comp == comp && !slot.blocked) {
        ++g_c539.after_free;
        g_c539.blocked_after_free += blocked ? 1 : 0;
    }
    slot = C539Slot{comp, blocked};
    ++g_c539.sweeps;
    g_c539.blocked += blocked ? 1 : 0;
    if (blocked) {
        c539_blocker(hit);
    }
    return out;
}

// " <channel>:<count>" for each nonzero bucket, into `out`.
static void c539_buckets(const uint64_t (&n)[kC539Channels], char* out, size_t cap) {
    size_t at = 0;
    for (uint32_t i = 0; i < kC539Channels && at + 24 < cap; ++i) {
        if (n[i] != 0) {
            const int w = snprintf(out + at, cap - at, " %u:%llu", i, static_cast<unsigned long long>(n[i]));
            at += w > 0 ? static_cast<size_t>(w) : 0;
        }
    }
}

// Game thread, per frame (c42_step): totals every kC539ReportMs once proxy sweeps happen.
void c539_tick(ULONGLONG now) {
    if (!g_c539_on || now < g_c539_report_at) {
        return;
    }
    g_c539_report_at = now + kC539ReportMs;
    if (g_c539.sweeps == 0) {
        return;
    }
    log::line("card539: %s t=%llu NPC proxy capsule sweeps %llu, blocked %llu (%.1f%%), after an unblocked sweep %llu, "
              "of those blocked %llu (%.2f%%); blocked by NPC %llu, by pawn %llu, by world %llu, start penetrating %llu",
              g_client_session ? "client" : "host", static_cast<unsigned long long>(now),
              static_cast<unsigned long long>(g_c539.sweeps), static_cast<unsigned long long>(g_c539.blocked),
              100.0 * static_cast<double>(g_c539.blocked) / static_cast<double>(g_c539.sweeps),
              static_cast<unsigned long long>(g_c539.after_free),
              static_cast<unsigned long long>(g_c539.blocked_after_free),
              g_c539.after_free != 0
                  ? 100.0 * static_cast<double>(g_c539.blocked_after_free) / static_cast<double>(g_c539.after_free)
                  : 0.0,
              static_cast<unsigned long long>(g_c539.by_npc), static_cast<unsigned long long>(g_c539.by_pawn),
              static_cast<unsigned long long>(g_c539.by_world), static_cast<unsigned long long>(g_c539.start_pen));
    char types[256] = {};
    char applied[256] = {};
    c539_buckets(g_c539.blocker_type, types, sizeof(types));
    c539_buckets(g_c539.c542_applied, applied, sizeof(applied));
    log::line("card542: %s t=%llu proxy capsule own-type ignore %s, applied by own type (channel:count)%s, moves with a "
              "kept own type %llu; blocker object types (channel:count)%s",
              g_client_session ? "client" : "host", static_cast<unsigned long long>(now), g_c542_on ? "on" : "off",
              applied, static_cast<unsigned long long>(g_c539.c542_kept_type), types);
}

} // namespace ubw::hooks
