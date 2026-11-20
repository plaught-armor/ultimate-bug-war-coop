// Card 532 (always on, both sides): the client stops running NPC targeting and shows the host's primary target.
//
// UADTargetComponent::TickComponent has no authority check, so the client ran the whole target search for every NPC
// (GatherTargetsInRange, SelectPrimaryTarget: about 8 to 10% of its game thread in c263L345). The class replicates
// nothing, so the client's result was its own guess. The one client reader that shows is
// FSTBugProceduralAnimationData::UpdateFromNPC: when GetPrimaryTarget returns a target whose HasLineOfSight (+0x48)
// is 2, the bug's body turns toward TargetLastKnownLocation (+0x30). A host bug chasing player A could face player B
// on the client screen.
//
// Host: after each SelectPrimaryTarget (its own interval per NPC), the primary target and its HasLineOfSight are
// compared with the last values sent for that component; a change queues one NpcTarget row
//   varint NPC NetGUID, varint (target NetGUID << 2 | HasLineOfSight)   (target 0 = none)
// and the queue goes out to every ponged client at most every kC532FlushMs. When the ponged count grows (a join),
// the table empties, so every NPC with a target is sent again at its next selection.
// Client: TickComponent turns its own tick off (Activate may turn it back on; the next tick turns it off again).
// GetPrimaryTarget returns the host's target for an NPC with a row: Target (+0x0), HasLineOfSight (+0x48), and
// TargetLastKnownLocation (+0x30) as the target's location now. An NPC without a row keeps the original result.
// Rows whose NPC or target is not resolved yet wait in a ring and are retried for kC532PendingMs.
//
// Rows: card532: totals every kC532ReportMs while rows move (host: rows, bytes, frames, skipped; client: applied,
// pending, dropped, ticks stopped, overlays).

#include "ubw/hooks_internal.h"

#include <string.h>

namespace ubw::hooks {

C532TickFn g_orig_c532_tick = nullptr;
C532GetPrimaryFn g_orig_c532_get_primary = nullptr;
C532FindPrimaryFn g_c532_find_primary = nullptr;

namespace {

constexpr size_t kC532Target = 0x00;          // FADTargetData::Target (TWeakObjectPtr<AActor>, 8 bytes)
constexpr size_t kC532LastKnown = 0x30;       // FADTargetData::TargetLastKnownLocation (FVector, doubles)
constexpr size_t kC532HasLos = 0x48;          // FADTargetData::HasLineOfSight (enum byte)
constexpr size_t kC532SetTickEnabledVfn = 0x410; // UActorComponent::SetComponentTickEnabled (pose_c132.cpp)
constexpr uint32_t kC532Slots = 2048;         // power of two
constexpr uint32_t kC532Probe = 8;
constexpr int32_t kC532RowMax = 20;           // two varints of at most 10 bytes
constexpr ULONGLONG kC532FlushMs = 50;        // card latency target: under 100 ms
constexpr uint32_t kC532Pending = 64;
constexpr ULONGLONG kC532PendingMs = 2000;
constexpr ULONGLONG kC532ReportMs = 60000;

struct C532Row {
    C31Weak key;     // host: the target component; client: the NPC
    C31Weak target;  // last sent (host) or the host's target (client)
    uint8_t los;
    bool used;
};

struct C532Pending {
    void* pc;
    uint64_t npc;
    uint64_t target;
    uint8_t los;
    ULONGLONG at;
};

struct C532State {
    C532Row row[kC532Slots];
    uint8_t out[kChanBlobBytes];
    int32_t used;
    ULONGLONG flushed;
    const void* world;
    int32_t ponged;
    C532Pending pending[kC532Pending];
    uint32_t pending_n;
    ULONGLONG report_at;
    // host
    uint32_t rows, frames, skipped, full, resets;
    uint64_t bytes;
    // client
    uint32_t applied, dropped, stopped, bad;
    uint64_t overlays;
};
C532State g_c532{}; // game thread

uint32_t c532_hash(const void* key) {
    return static_cast<uint32_t>((reinterpret_cast<uintptr_t>(key) >> 4) * 0x9E3779B97F4A7C15ULL >> 53) &
           (kC532Slots - 1);
}

C532Row* c532_find(const void* key) {
    const uint32_t h = c532_hash(key);
    for (uint32_t i = 0; i < kC532Probe; ++i) {
        C532Row& r = g_c532.row[(h + i) & (kC532Slots - 1)];
        if (r.used && c31_weak_get(r.key) == key) {
            return &r;
        }
    }
    return nullptr;
}

// The key's row, else a new one in an empty slot or a slot whose key is gone; null when the probe run is full.
C532Row* c532_slot(void* key) {
    C532Row* const found = c532_find(key);
    if (found != nullptr) {
        return found;
    }
    const uint32_t h = c532_hash(key);
    for (uint32_t i = 0; i < kC532Probe; ++i) {
        C532Row& r = g_c532.row[(h + i) & (kC532Slots - 1)];
        if (!r.used || c31_weak_get(r.key) == nullptr) {
            r = C532Row{};
            c31_weak_set(r.key, key);
            r.used = true;
            return &r;
        }
    }
    ++g_c532.full;
    return nullptr;
}

int32_t c532_put(uint8_t* at, uint64_t v) {
    int32_t n = 0;
    for (int32_t k = 0; k < 10; ++k) {
        const uint8_t b = static_cast<uint8_t>(v & 0x7F);
        v >>= 7;
        at[n++] = static_cast<uint8_t>(b | (v != 0 ? 0x80 : 0));
        if (v == 0) {
            break;
        }
    }
    return n;
}

bool c532_get(const uint8_t* bytes, int32_t n, int32_t& at, uint64_t& v) {
    v = 0;
    for (int32_t k = 0; k < 10 && at < n; ++k) {
        const uint8_t b = bytes[at++];
        v |= static_cast<uint64_t>(b & 0x7F) << (7 * k);
        if ((b & 0x80) == 0) {
            return true;
        }
    }
    return false;
}

void c532_flush(ULONGLONG now) {
    if (g_c532.used > 0 && g_c532.world != nullptr) {
        (void)c95_send_blob_all(g_c532.world, ChanOp::NpcTarget, g_c532.out, g_c532.used);
        ++g_c532.frames;
        g_c532.bytes += static_cast<uint64_t>(g_c532.used);
    }
    g_c532.used = 0;
    g_c532.flushed = now;
}

// Client: one row; false when the NPC or the target is not resolved yet.
bool c532_apply(void* pc, uint64_t npc_guid, uint64_t target_guid, uint8_t los) {
    void* const npc = c41_object_from_guid(pc, npc_guid);
    void* const target = target_guid != 0 ? c41_object_from_guid(pc, target_guid) : nullptr;
    if (npc == nullptr || (target_guid != 0 && target == nullptr)) {
        return false;
    }
    C532Row* const r = c532_slot(npc);
    if (r != nullptr) {
        c31_weak_set(r->target, target);
        r->los = los;
        ++g_c532.applied;
    }
    return true;
}

void c532_pend(void* pc, uint64_t npc, uint64_t target, uint8_t los, ULONGLONG now) {
    for (uint32_t i = 0; i < g_c532.pending_n; ++i) { // a newer row for the same NPC replaces the waiting one
        C532Pending& p = g_c532.pending[i];
        if (p.npc == npc) {
            p = C532Pending{pc, npc, target, los, now};
            return;
        }
    }
    if (g_c532.pending_n == kC532Pending) {
        ++g_c532.dropped;
        return;
    }
    g_c532.pending[g_c532.pending_n++] = C532Pending{pc, npc, target, los, now};
}

void c532_retry(ULONGLONG now) {
    uint32_t keep = 0;
    for (uint32_t i = 0; i < g_c532.pending_n && i < kC532Pending; ++i) {
        const C532Pending p = g_c532.pending[i];
        if (c532_apply(p.pc, p.npc, p.target, p.los)) {
            continue;
        }
        if (now - p.at >= kC532PendingMs) {
            ++g_c532.dropped;
            continue;
        }
        g_c532.pending[keep++] = p;
    }
    g_c532.pending_n = keep;
}

void c532_report(ULONGLONG now) {
    if (now < g_c532.report_at) {
        return;
    }
    g_c532.report_at = now + kC532ReportMs;
    if (g_client_session) {
        if (g_c532.applied + g_c532.stopped != 0) {
            log::line("card532: client t=%llu rows applied %u, pending %u, dropped %u, bad frames %u, target ticks "
                      "stopped %u, GetPrimaryTarget overlays %llu, table full %u",
                      static_cast<unsigned long long>(now), g_c532.applied, g_c532.pending_n, g_c532.dropped,
                      g_c532.bad, g_c532.stopped, static_cast<unsigned long long>(g_c532.overlays), g_c532.full);
        }
        return;
    }
    if (g_c532.rows != 0) {
        log::line("card532: host t=%llu rows %u in %u frames, %llu bytes (%.1f B/row), skipped %u (no NetGUID or los "
                  "out of range), table full %u, join resets %u",
                  static_cast<unsigned long long>(now), g_c532.rows, g_c532.frames,
                  static_cast<unsigned long long>(g_c532.bytes),
                  static_cast<double>(g_c532.bytes) / static_cast<double>(g_c532.rows), g_c532.skipped, g_c532.full,
                  g_c532.resets);
    }
}

} // namespace

// Host, after the original SelectPrimaryTarget (detour_c103_select).
void c532_host_note(void* component) {
    if (g_client_session || g_c532_find_primary == nullptr) {
        return;
    }
    const uint8_t* const data = static_cast<const uint8_t*>(g_c532_find_primary(component));
    // Target is a TWeakObjectPtr<AActor> ({index, serial}), not a raw pointer (c263L347: read raw, the host crashed
    // in GetWorld on 0x137DF00038749).
    C31Weak target_weak{};
    if (data != nullptr) {
        memcpy(&target_weak, data + kC532Target, sizeof(target_weak));
    }
    void* const target = c31_weak_get(target_weak);
    const uint8_t los = data != nullptr ? data[kC532HasLos] : 0;
    C532Row* const r = c532_slot(component);
    if (r == nullptr || (c31_weak_get(r->target) == target && r->los == los)) {
        return;
    }
    const void* const owner = read_ptr(component, kComponentOwner);
    const uint64_t npc_guid = owner != nullptr ? c41_guid(owner) : 0;
    const uint64_t target_guid = target != nullptr ? c41_guid(target) : 0;
    if (npc_guid == 0 || (target != nullptr && target_guid == 0) || los > 3 || target_guid >> 62 != 0) {
        ++g_c532.skipped; // the row stays unchanged, so the next selection tries again
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_c532.used + kC532RowMax > kChanBlobBytes) {
        c532_flush(now);
    }
    g_c532.used += c532_put(g_c532.out + g_c532.used, npc_guid);
    g_c532.used += c532_put(g_c532.out + g_c532.used, target_guid << 2 | los);
    g_c532.world = g_actor_get_world(owner);
    ++g_c532.rows;
    c31_weak_set(r->target, target);
    r->los = los;
}

// Both sides, per frame (c42_step).
void c532_tick(ULONGLONG now) {
    if (g_client_session) {
        if (g_c532.pending_n != 0) {
            c532_retry(now);
        }
    } else {
        const int32_t ponged = g_c532.world != nullptr ? c95_ponged_count(g_c532.world) : 0;
        if (ponged > g_c532.ponged) { // a join: every NPC's state is sent again at its next selection
            c532_flush(now);
            memset(g_c532.row, 0, sizeof(g_c532.row));
            ++g_c532.resets;
        }
        g_c532.ponged = ponged;
        if (g_c532.used > 0 && now - g_c532.flushed >= kC532FlushMs) {
            c532_flush(now);
        }
    }
    c532_report(now);
}

// Both sides, map arm: the map's totals, then every table, pending row (they hold the old map's PC) and the world
// pointer (c95_ponged_count reads it) start empty.
void c532_arm_map() {
    g_c532.report_at = 0;
    c532_report(GetTickCount64());
    g_c532 = C532State{};
}

// Client: ChanOp::NpcTarget.
void c532_receive(void* pc, const WireString* s) {
    static uint8_t bytes[kChanBlobBytes]; // game thread only
    const int32_t n = ue::chan::chan_blob(s, bytes, kChanBlobBytes);
    const ULONGLONG now = GetTickCount64();
    int32_t at = 0;
    for (int32_t k = 0; k < kChanBlobBytes && at < n; ++k) {
        uint64_t npc = 0;
        uint64_t packed = 0;
        if (!c532_get(bytes, n, at, npc) || !c532_get(bytes, n, at, packed)) {
            ++g_c532.bad;
            log::line("WARN card532: client NpcTarget bad row at byte %d of %d (bad frames %u): rest dropped", at, n,
                      g_c532.bad);
            return;
        }
        const uint8_t los = static_cast<uint8_t>(packed & 3);
        if (!c532_apply(pc, npc, packed >> 2, los)) {
            c532_pend(pc, npc, packed >> 2, los, now);
        }
    }
}

void detour_c532_tick(void* component, float delta, int32_t tick_type, void* tick_function) {
    if (!g_client_session) {
        g_orig_c532_tick(component, delta, tick_type, tick_function);
        return;
    }
    using SetTickFn = void (*)(void*, bool);
    reinterpret_cast<SetTickFn>(read_ptr(read_ptr(component, 0), kC532SetTickEnabledVfn))(component, false);
    ++g_c532.stopped;
}

bool detour_c532_get_primary(const void* component, uint8_t* out) {
    if (!g_client_session) {
        return g_orig_c532_get_primary(component, out);
    }
    const C532Row* const r = c532_find(read_ptr(component, kComponentOwner));
    if (r == nullptr) {
        return g_orig_c532_get_primary(component, out);
    }
    void* const target = c31_weak_get(r->target);
    if (target == nullptr) {
        return false;
    }
    const Vec3 at = actor_location(target);
    memcpy(out + kC532Target, &r->target, sizeof(r->target)); // TWeakObjectPtr {index, serial}, as the original writes
    memcpy(out + kC532LastKnown, &at, sizeof(at));
    out[kC532HasLos] = r->los;
    ++g_c532.overlays;
    return true;
}

} // namespace ubw::hooks
