// Card 158: the client's own grenade shows in its hand.
//
// GA_GrenadeThrowAbility_C's OnAnimationStarted spawns the held grenade (GrenadeVisualClassRef) through an AbilityTask
// SpawnActor, whose BeginSpawningActor spawns only with net authority. Card 169's shared detour (spawn_c169.cpp) spawns
// it on the client's own ability, and the Blueprint's own Success handler stores GrenadeVisuals and attaches it; the
// Blueprint's OnDoAction / K2_OnEndAbility destroy it as on the host. After OnAnimationStarted this file logs the held
// grenade's attach state, and at OnDoAction the throw's aim.
//
// The throw's aim needs no change: OnDoAction_Implementation aims with the PlayerController's GetPlayerViewPoint plus
// LaunchAngle, and on the server that view point is the client's camera (docs/cards/158-client-grenade.md). The host
// logs each server copy throw's launch direction against LaunchAngle so a run can compare it with the press aim.

#include "ubw/hooks_internal.h"

namespace ubw::hooks {


namespace {

constexpr int32_t kC158Classes = 4;  // ability Blueprints with their own OnAnimationStarted (frag/incendiary share one)
constexpr uint32_t kC158Lines = 40;  // card158 lines per session, each side

// Card 573 b463: one flat array, so the ProcessEvent check is one scan. Per ability class i: fn[2 i] is its
// OnAnimationStarted, fn[2 i + 1] its OnDoAction (the Blueprint destroys GrenadeVisuals there).
constexpr int32_t kC158Started = 0;
constexpr int32_t kC158DoAction = 1;

struct C158State {
    const void* fn[2 * kC158Classes];
    int32_t n; // ability classes noted
    uint64_t fname[2];
    const void* held;     // the actor the client spawned for its current throw, until OnDoAction
    ULONGLONG held_ms;
    uint32_t lines;
    uint32_t spawned;
    uint32_t failed;
};
C158State g_c158{};

constexpr size_t kPcmServerUpdateCameraTimeout = 0x27C8; // APlayerCameraManager, compared at 0x14406625B
constexpr float kC347TimeoutS = 0.2F;                    // 5 sends per second while the view is still
const void* g_c347_camera = nullptr;                     // the camera manager already set; compared, never read

bool c158_line() {
    return note_event_cap(g_c158.lines, kC158Lines, "card158 lines");
}

int32_t c158_find(const void* function, bool do_action) {
    const int32_t kind = do_action ? kC158DoAction : kC158Started;
    for (int32_t i = 0; i < g_c158.n; ++i) {
        if (g_c158.fn[2 * i + kind] == function) {
            return i;
        }
    }
    return -1;
}

// Client own OnAnimationStarted, after it ran: card 169's spawn and the Blueprint's Success set GrenadeVisuals.
void c158_started(void* ability) {
    const void* const avatar = avatar_of(read_ptr(ability, kAbilityActorInfo));
    if (sprint_side(avatar) != kSideClientOwn) {
        return;
    }
    const uint8_t* prop = nullptr;
    const void* const actor = prop_object_field(ability, "GrenadeVisuals", &prop);
    const ULONGLONG now = GetTickCount64();
    if (actor == nullptr) {
        ++g_c158.failed;
        if (c158_line()) {
            log::line("WARN card158: client t=%llu %s: OnAnimationStarted left GrenadeVisuals unset",
                      static_cast<unsigned long long>(now), class_name(ability).text);
        }
        return;
    }
    ++g_c158.spawned;
    g_c158.held = actor;
    g_c158.held_ms = now;
    const void* const root = read_ptr(actor, kActorRootComponent);
    const uint8_t* const r = static_cast<const uint8_t*>(root);
    const void* const parent = c140_attach_parent(root);
    if (c158_line()) {
        log::line("card158: client t=%llu %s: held grenade %s (%s) set as GrenadeVisuals by Success; attached to %s "
                  "(%s), root visible %d, actor hidden %d",
                  static_cast<unsigned long long>(now), class_name(ability).text, object_name(actor).text,
                  class_name(actor).text, object_name(parent).text, object_name(read_ptr(parent, kComponentOwner)).text,
                  r != nullptr && (r[kSceneVisibleByte] & kSceneVisibleBit) != 0 ? 1 : 0,
                  (static_cast<const uint8_t*>(actor)[kActorHiddenByte] & kActorHiddenBit) != 0 ? 1 : 0);
    }
}

// Client own OnDoAction: the throw's aim here (the host's OnDoAction aims with this camera plus LaunchAngle) and how
// long the held grenade showed.
void c158_did_action(void* ability) {
    const void* const avatar = avatar_of(read_ptr(ability, kAbilityActorInfo));
    if (sprint_side(avatar) == kSideClientOwn) {
        c494_after_throw(); // card 494: the drawn weapon after the throw
    }
    if (sprint_side(avatar) != kSideClientOwn || !c158_line()) {
        g_c158.held = nullptr;
        return;
    }
    const void* const pc = read_ptr(avatar, kPawnController);
    const double* const rot =
        pc != nullptr ? reinterpret_cast<const double*>(static_cast<const uint8_t*>(pc) + kControllerControlRotation) : nullptr;
    const uint8_t* prop = nullptr;
    const void* const now_held = prop_object_field(ability, "GrenadeVisuals", &prop);
    const ULONGLONG now = GetTickCount64();
    log::line("%scard158: client t=%llu %s OnDoAction: aim P %.2f Y %.2f; held grenade %s for %llu ms; GrenadeVisuals %s",
              g_c158.held != nullptr ? "" : "WARN ", static_cast<unsigned long long>(now),
              class_name(ability).text, rot != nullptr ? rot[0] : 0.0, rot != nullptr ? rot[1] : 0.0,
              g_c158.held != nullptr ? "shown" : "never shown",
              static_cast<unsigned long long>(g_c158.held != nullptr ? now - g_c158.held_ms : 0),
              now_held == nullptr ? "cleared" : "still set (DestroyActor does not clear the variable)");
    g_c158.held = nullptr;
}

} // namespace

// Client, from c73_bind: the bound grenade ability's Blueprint functions, once per function set.
void c158_note_ability(const void* ability) {
    if (!g_client_session || ability == nullptr || g_c158.n >= kC158Classes ||
        fname_cached(g_c158.fname[0], L"OnAnimationStarted") == 0 || fname_cached(g_c158.fname[1], L"OnDoAction") == 0) {
        return;
    }
    const void* const started = g_find_function(ability, g_c158.fname[0]);
    if (started == nullptr || c158_find(started, false) >= 0) {
        return;
    }
    const void* const do_action = g_find_function(ability, g_c158.fname[1]);
    g_c158.fn[2 * g_c158.n + kC158Started] = started;
    g_c158.fn[2 * g_c158.n + kC158DoAction] = do_action;
    ++g_c158.n;
    pe_gate_set(kPeGate_c158_pe, true);
    log::line("%scard158: client %s: OnAnimationStarted %s, OnDoAction %s", do_action != nullptr ? "" : "WARN ",
              class_name(ability).text, object_name(started).text, object_name(do_action).text);
}

// Hot path (kPeGate_c158_pe), after each ProcessEvent: pointer compares against the recorded functions.
void c158_after_pe(void* object, const void* function) {
    const int32_t count = 2 * g_c158.n;
    for (int32_t i = 0; i < count; ++i) {
        if (g_c158.fn[i] != function) {
            continue;
        }
        // An OnAnimationStarted is never null (c158_note_ability) and no function is both kinds, so the first match
        // is the one the two separate scans found.
        if ((i & 1) == kC158Started) {
            c158_started(object);
        } else {
            c158_did_action(object);
        }
        return;
    }
}

// Host, from OnGrenadeSpawned: a server copy's throw, its launch direction and LaunchAngle.
void c158_host_throw(const void* ability, const void* projectile, const Vec3& v) {
    const void* const avatar = avatar_of(read_ptr(ability, kAbilityActorInfo));
    if (g_client_session || sprint_side(avatar) != kSideServerCopy || !c158_line()) {
        return;
    }
    const uint8_t* const angle = prop_find_prop(read_ptr(ability, kObjectClass), "LaunchAngle");
    const double launch = angle != nullptr ? read_f32(ability, static_cast<size_t>(read_i32(angle, kPropertyOffset))) : 0.0;
    const double flat = sqrt(v.x * v.x + v.y * v.y);
    const double pitch = atan2(v.z, flat) * 57.29577951308232;
    const double yaw = atan2(v.y, v.x) * 57.29577951308232;
    log::line("%scard158: host t=%llu server copy throw %s (%s): launch P %.2f Y %.2f, LaunchAngle %.2f, aim P %.2f Y %.2f",
              angle != nullptr ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()),
              class_name(projectile).text, class_name(ability).text, pitch, yaw, launch, pitch - launch, yaw);
}

// Card 347, client, each frame: the own camera manager resends an unchanged view to the host within kC347TimeoutS.
//
// APlayerCameraManager::UpdateCamera (RVA 0x04065FB0) sends ServerUpdateCamera only on a frame whose view differs from
// the frame before, or once the time since the last send passes ServerUpdateCameraTimeout (+0x27C8). A send lost or
// throttled on the last frame of a turn leaves the host's camera cache on the older view until that timeout, and
// every server copy ability that aims with GetPlayerViewPoint (the grenade throw) uses the stale view.
void c347_client_step(void* pc) {
    void* const camera = read_ptr(pc, kPcCameraManager);
    if (camera == nullptr || camera == g_c347_camera) {
        return;
    }
    g_c347_camera = camera;
    const float was = read_f32(camera, kPcmServerUpdateCameraTimeout);
    if (!(was > kC347TimeoutS)) {
        log::line("card347: client camera manager %s resend limit %.3f s kept", class_name(camera).text, was);
        return;
    }
    const float now = kC347TimeoutS;
    memcpy(static_cast<uint8_t*>(camera) + kPcmServerUpdateCameraTimeout, &now, sizeof(now));
    log::line("card347: client camera manager %s resend limit %.3f s -> %.3f s", class_name(camera).text, was, now);
}

// Card 599, host, each frame: each server copy PC's camera manager actor is moved to that copy's view.
//
// GA_HumanMelee (DealDamage, TraceForTarget), GA_WeaponMeleeAbility, GA_TauntAbility and BP_GuidedMissile aim with
// GetPlayerCameraManager(0)'s K2_GetActorLocation and GetActorForwardVector, and BP_SatchelBuilding_Flakbug takes its
// GetDistanceTo. The engine moves a camera manager actor only in its own UpdateCamera, which runs for a local
// controller; ServerUpdateCamera fills only the camera cache. So a copy's camera manager stayed at (0,0,0) with zero
// rotation (run c582b451a), and a client's knife traced from the world origin along +X. The view is the cache POV
// the client's ServerUpdateCamera filled (card 347 keeps it fresh); with no cache yet (Timestamp 0), the copy pawn's
// camera component and the PC's ControlRotation.
namespace {
constexpr size_t kC599CacheTimestamp = 0x0; // FCameraCacheEntry: float Timestamp
constexpr size_t kC599PovLocation = 0x10;   // POV.Location (FMinimalViewInfo, after the Timestamp and pad)
constexpr size_t kC599PovRotation = 0x28;   // POV.Rotation, after the three-double Location
struct C599Slot {
    const void* pcm; // the camera manager last set in this slot; compared, never read
    Vec3 at;
    Vec3 rot;
};
struct C599State {
    C599Slot slot[kC17Slots];
    int32_t cache_at; // reflected CameraCachePrivate offset, -1 unresolved, -2 absent
    uint32_t sets;
    uint32_t from_cache;
    uint32_t fallback;
};
C599State g_c599{{}, -1, 0, 0, 0};

int32_t c599_cache_at(const void* pcm) {
    if (g_c599.cache_at == -1) {
        const uint8_t* const prop = prop_find_prop(read_ptr(pcm, kObjectClass), "CameraCachePrivate");
        g_c599.cache_at = prop != nullptr ? read_i32(prop, kPropertyOffset) : -2;
        log::line("%scard599: host reflected CameraCachePrivate on %s: %s +0x%X", g_c599.cache_at < 0 ? "WARN " : "",
                  class_name(pcm).text, g_c599.cache_at < 0 ? "absent" : "at",
                  g_c599.cache_at < 0 ? 0 : g_c599.cache_at);
    }
    return g_c599.cache_at;
}

void c599_set(C599Slot& s, void* pc, void* pcm) {
    const int32_t cache = c599_cache_at(pcm);
    const bool cached = cache >= 0 && read_f32(pcm, static_cast<size_t>(cache) + kC599CacheTimestamp) > 0.0F;
    const void* const pawn = read_ptr(pc, kControllerPawn);
    if (!cached && pawn == nullptr) {
        return;
    }
    const Vec3 at = cached ? read_vec3(pcm, static_cast<size_t>(cache) + kC599PovLocation) : camera_location(pawn);
    const Vec3 rot = cached ? read_vec3(pcm, static_cast<size_t>(cache) + kC599PovRotation)
                            : read_vec3(pc, kControllerControlRotation);
    if (s.pcm == pcm && memcmp(&s.at, &at, sizeof(at)) == 0 && memcmp(&s.rot, &rot, sizeof(rot)) == 0) {
        return;
    }
    if (s.pcm != pcm) {
        log::line("card599: host t=%llu copy %s camera manager %s set to its view from %s: (%.0f, %.0f, %.0f) P %.1f "
                  "Y %.1f",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(pc).text, object_name(pcm).text,
                  cached ? "the camera cache" : "the camera component and control rotation", at.x, at.y, at.z, rot.x,
                  rot.y);
    }
    g_set_actor_location_rotation(pcm, &at, &rot, false, nullptr, kTeleportPhysics);
    s = C599Slot{pcm, at, rot};
    ++g_c599.sets;
    ++(cached ? g_c599.from_cache : g_c599.fallback);
}
} // namespace

void c599_host_step(void* world) {
    for (int s = 0; s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        void* const pcm = pc != nullptr && sprint_side(pc) == kSideServerCopy ? read_ptr(pc, kPcCameraManager)
                                                                              : nullptr;
        if (pcm != nullptr) {
            c599_set(g_c599.slot[s], pc, pcm);
        }
    }
}

// Map change: the held actor pointer is stale.
void c158_reset_map() {
    if (g_c599.sets != 0) {
        log::line("card599: host map totals: %u copy camera manager moves (%u from the camera cache, %u fallback)",
                  g_c599.sets, g_c599.from_cache, g_c599.fallback);
    }
    g_c599 = C599State{{}, g_c599.cache_at, 0, 0, 0}; // a new map's camera managers may reuse the addresses
    if (g_c158.spawned != 0 || g_c158.failed != 0) {
        log::line("card158: %s map totals: %u held grenades set, %u not", side(), g_c158.spawned, g_c158.failed);
    }
    g_c158.held = nullptr;
    g_c347_camera = nullptr; // card 347: a new map's camera manager may reuse the address
    g_c158.spawned = 0;
    g_c158.failed = 0;
}

} // namespace ubw::hooks
