// Card 573 (performance mod family): UpdateKinematicBonesToAnim counts, before any cut.
//
// c601b455a: the Chaos cluster on the host GameThread (SetKinematicTarget, IsStatic, GetUserData, TAABBTree updates)
// is the per-body lambda of USkeletalMeshComponent::UpdateKinematicBonesToAnim (exe+0x4019C30): each frame it moves
// each kinematic physics body of a skeletal mesh to its animated bone. A lossless cut may only skip calls whose bodies
// nothing reads, so this probe counts, per 10 s window and per owner class:
//   - calls, inclusive time, bodies per call (USkeletalMeshComponent::Bodies count at +0xB80);
//   - calls whose owner is dead or dying (NPC CurrentBaseState Dying/Dead/Overkilled);
//   - calls on a ragdoll (any body simulating);
//   - calls on a hidden owner or mesh (AActor bHidden, or the mesh's bVisible clear). Pool actors sit hidden, so a
//     pooled owner is counted here and named by its class row;
//   - calls on a mesh with collision off (GetCollisionEnabled == NoCollision);
//   - calls with KinematicBonesUpdateType SkipAllBones and no simulating body (the function returns at once, cheap).
// Game-thread calls only are keyed; other threads are one count. The counts change no result.
// Gate: on by default, UBWCOOP_C573_OFF=1 removes the hook.

#include "ubw/hooks_internal.h"

#include <algorithm> // std::sort
#include <atomic>

namespace ubw::hooks {

bool g_c573_on = true; // read_perf_flags, before any hook (UBWCOOP_C573_OFF=1 turns it off)
KinematicBonesFn g_orig_c573_bones = nullptr;

namespace {

constexpr ULONGLONG kC573WindowMs = 10000;
constexpr uint32_t kC573Slots = 256; // power of two; one row per owner class
constexpr uint32_t kC573Probe = 16;
constexpr int32_t kC573TopRows = 12;
constexpr size_t kC573Bodies = 0xB80;      // USkeletalMeshComponent::Bodies (TArray<FBodyInstance*>)
constexpr int32_t kC573BodiesMax = 256;    // a bug's physics asset has a few dozen bodies
constexpr size_t kC573UpdateType = 0xA6C;  // USkeletalMeshComponent::KinematicBonesUpdateType (TEnumAsByte)
constexpr uint8_t kC573SkipAllBones = 1;
constexpr uint8_t kC573NoCollision = 0;    // ECollisionEnabled::NoCollision
constexpr uint8_t kC573StateDying = 3;     // ESTBaseNPCState: Dying 3, Dead 4, Overkilled 5
constexpr uint8_t kC573StateOverkilled = 5;

struct C573Row {
    const void* cls; // owner class; nullptr = empty slot
    uint32_t calls;
    uint32_t bodies; // sum over calls
    uint32_t bodies_max;
    uint32_t dead;
    uint32_t ragdoll;
    uint32_t hidden;
    uint32_t collision_off;
    uint32_t skip_all; // SkipAllBones and no simulating body: the early return
    uint32_t any_skippable; // dead, hidden or collision off, and not a ragdoll
    int64_t ticks;
    char name[64];
};

struct C573State {
    C573Row row[kC573Slots];
    uint32_t calls;
    uint32_t no_owner;
    uint32_t dropped;
    int64_t ticks;
    int64_t window_t0;
    ULONGLONG window_ms0;
};
C573State g_c573{};
std::atomic<uint32_t> g_c573_other_thread{0}; // calls off the game thread (not keyed)
std::atomic<DWORD> g_c573_game_thread{0};

int64_t c573_now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

double c573_ms(int64_t ticks) {
    static const double kPerMs = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart) / 1000.0;
    }();
    return static_cast<double>(ticks) / kPerMs;
}

struct C573Mesh {
    uint32_t bodies;
    bool ragdoll;
};

C573Mesh c573_read_mesh(const void* mesh) {
    const void* const* const bodies = static_cast<const void* const*>(read_ptr(mesh, kC573Bodies));
    const int32_t count = read_i32(mesh, kC573Bodies + 0x8);
    if (bodies == nullptr || count <= 0 || count > kC573BodiesMax) {
        return {0, false};
    }
    C573Mesh m{static_cast<uint32_t>(count), false};
    for (int32_t i = 0; i < count && !m.ragdoll && g_body_simulating != nullptr; ++i) {
        m.ragdoll = bodies[i] != nullptr && g_body_simulating(bodies[i]);
    }
    return m;
}

C573Row* c573_row(const void* cls, const void* owner) {
    uint32_t i = static_cast<uint32_t>((reinterpret_cast<uintptr_t>(cls) * 0x9E3779B97F4A7C15ULL) >> 56) &
                 (kC573Slots - 1);
    for (uint32_t p = 0; p < kC573Probe; ++p, i = (i + 1) & (kC573Slots - 1)) {
        C573Row& r = g_c573.row[i];
        if (r.cls == cls) {
            return &r;
        }
        if (r.cls == nullptr) {
            r.cls = cls;
            snprintf(r.name, sizeof(r.name), "%s", class_name(owner).text); // once per class per window
            return &r;
        }
    }
    ++g_c573.dropped;
    return nullptr;
}

bool c573_dead(const void* owner) {
    if (g_npc_class == nullptr || !is_a(owner, g_npc_class())) {
        return false;
    }
    const uint8_t state = static_cast<const uint8_t*>(owner)[kNpcBaseState];
    return state >= kC573StateDying && state <= kC573StateOverkilled;
}

bool c573_hidden(const void* owner, const void* mesh) {
    const bool actor_hidden = (static_cast<const uint8_t*>(owner)[kActorHiddenByte] & kActorHiddenBit) != 0;
    const bool mesh_hidden = (static_cast<const uint8_t*>(mesh)[kSceneVisibleByte] & kSceneVisibleBit) == 0;
    return actor_hidden || mesh_hidden;
}

void c573_count(const void* mesh, int64_t d) {
    const void* const owner = read_ptr(mesh, kComponentOwner);
    const void* const cls = owner != nullptr ? read_ptr(owner, kObjectClass) : nullptr;
    if (cls == nullptr) {
        ++g_c573.no_owner;
        return;
    }
    C573Row* const r = c573_row(cls, owner);
    if (r == nullptr) {
        return;
    }
    const C573Mesh m = c573_read_mesh(mesh);
    const bool dead = c573_dead(owner);
    const bool hidden = c573_hidden(owner, mesh);
    const bool collision_off = g_collision_enabled != nullptr && g_collision_enabled(mesh) == kC573NoCollision;
    const bool skip_all = static_cast<const uint8_t*>(mesh)[kC573UpdateType] == kC573SkipAllBones && !m.ragdoll;
    ++r->calls;
    r->bodies += m.bodies;
    r->bodies_max = std::max(r->bodies_max, m.bodies);
    r->dead += dead ? 1u : 0u;
    r->ragdoll += m.ragdoll ? 1u : 0u;
    r->hidden += hidden ? 1u : 0u;
    r->collision_off += collision_off ? 1u : 0u;
    r->skip_all += skip_all ? 1u : 0u;
    r->any_skippable += (dead || hidden || collision_off) && !m.ragdoll && !skip_all ? 1u : 0u;
    r->ticks += d;
}

void c573_report(ULONGLONG now, int64_t t) {
    const char* const s = side();
    const double window = c573_ms(t - g_c573.window_t0);
    uint32_t skippable = 0;
    int64_t skippable_ticks = 0; // estimate: class mean time times its skippable calls
    for (const C573Row& r : g_c573.row) {
        if (r.cls != nullptr && r.calls != 0) {
            skippable += r.any_skippable;
            skippable_ticks += r.ticks * r.any_skippable / r.calls;
        }
    }
    log::line("card573: %s t=%llu UpdateKinematicBonesToAnim total: %u game-thread calls, %.1f ms (%.2f%% of %.0f ms), "
              "%u other-thread calls, %u no owner; skippable (dead, hidden or collision off, not ragdoll, not "
              "SkipAllBones) %u calls, about %.1f ms",
              s, static_cast<unsigned long long>(now), g_c573.calls, c573_ms(g_c573.ticks),
              100.0 * c573_ms(g_c573.ticks) / window, window, g_c573_other_thread.exchange(0, std::memory_order_relaxed), g_c573.no_owner, skippable,
              c573_ms(skippable_ticks));
    C573Row* top[kC573TopRows] = {};
    int32_t n = 0;
    for (C573Row& r : g_c573.row) {
        if (r.cls == nullptr) {
            continue;
        }
        if (n < kC573TopRows) {
            top[n++] = &r;
        } else if (r.ticks > top[n - 1]->ticks) {
            top[n - 1] = &r;
        } else {
            continue;
        }
        std::sort(top, top + n, [](const C573Row* x, const C573Row* y) { return x->ticks > y->ticks; });
    }
    for (int32_t i = 0; i < n; ++i) {
        const C573Row& r = *top[i];
        log::line("card573: %s t=%llu kinematic bones top %d: %s: %u calls, %.1f ms, %.1f us mean, bodies %.1f mean "
                  "%u max; dead %u, ragdoll %u, hidden %u, collision off %u, SkipAllBones %u, skippable %u",
                  s, static_cast<unsigned long long>(now), i + 1, r.name, r.calls, c573_ms(r.ticks),
                  1000.0 * c573_ms(r.ticks) / static_cast<double>(r.calls),
                  static_cast<double>(r.bodies) / static_cast<double>(r.calls), r.bodies_max, r.dead, r.ragdoll,
                  r.hidden, r.collision_off, r.skip_all, r.any_skippable);
    }
    if (g_c573.dropped != 0) {
        log::line("WARN card573: %s %u calls not keyed (table of %u full)", s, g_c573.dropped, kC573Slots);
    }
}

} // namespace

void detour_c573_bones(void* mesh, const void* transforms, uint8_t teleport, bool needs_skinning, int32_t deferral) {
    const DWORD game_thread = g_c573_game_thread.load(std::memory_order_relaxed);
    if (game_thread == 0 || mesh == nullptr) {
        g_orig_c573_bones(mesh, transforms, teleport, needs_skinning, deferral);
        return;
    }
    if (GetCurrentThreadId() != game_thread) {
        g_orig_c573_bones(mesh, transforms, teleport, needs_skinning, deferral);
        g_c573_other_thread.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const int64_t t0 = c573_now();
    g_orig_c573_bones(mesh, transforms, teleport, needs_skinning, deferral);
    const int64_t d = c573_now() - t0;
    ++g_c573.calls;
    g_c573.ticks += d;
    c573_count(mesh, d); // after the original: the state it leaves is the state the next frame's call sees
}

// Game thread, per frame (perf_tick).
void c573_tick(ULONGLONG now) {
    if (!g_c573_on) {
        return;
    }
    g_c573_game_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    const int64_t t = c573_now();
    if (g_c573.window_ms0 == 0) {
        g_c573.window_ms0 = now;
        g_c573.window_t0 = t;
        return;
    }
    if (now - g_c573.window_ms0 < kC573WindowMs) {
        return;
    }
    c573_report(now, t);
    g_c573 = C573State{};
    g_c573.window_ms0 = now;
    g_c573.window_t0 = t;
}

} // namespace ubw::hooks
