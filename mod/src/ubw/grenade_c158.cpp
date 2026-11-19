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

bool g_c158_pe = false; // client: a grenade ability is bound; detour_process_event calls c158_after_pe

namespace {

constexpr int32_t kC158Classes = 4;  // ability Blueprints with their own OnAnimationStarted (frag/incendiary share one)
constexpr uint32_t kC158Lines = 40;  // card158 lines per session, each side

struct C158Fns {
    const void* started;   // OnAnimationStarted
    const void* do_action; // OnDoAction (the Blueprint destroys GrenadeVisuals there)
};

struct C158State {
    C158Fns fns[kC158Classes];
    int32_t n;
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
    for (int32_t i = 0; i < g_c158.n; ++i) {
        if ((do_action ? g_c158.fns[i].do_action : g_c158.fns[i].started) == function) {
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
    C158Fns& f = g_c158.fns[g_c158.n++];
    f.started = started;
    f.do_action = g_find_function(ability, g_c158.fname[1]);
    g_c158_pe = true;
    log::line("%scard158: client %s: OnAnimationStarted %s, OnDoAction %s", f.do_action != nullptr ? "" : "WARN ",
              class_name(ability).text, object_name(started).text, object_name(f.do_action).text);
}

// Hot path (g_c158_pe), after each ProcessEvent: pointer compares against the recorded functions.
void c158_after_pe(void* object, const void* function) {
    const int32_t s = c158_find(function, false);
    if (s >= 0) {
        c158_started(object);
        return;
    }
    if (c158_find(function, true) >= 0) {
        c158_did_action(object);
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

// Map change: the held actor pointer is stale.
void c158_reset_map() {
    if (g_c158.spawned != 0 || g_c158.failed != 0) {
        log::line("card158: %s map totals: %u held grenades set, %u not", side(), g_c158.spawned, g_c158.failed);
    }
    g_c158.held = nullptr;
    g_c347_camera = nullptr; // card 347: a new map's camera manager may reuse the address
    g_c158.spawned = 0;
    g_c158.failed = 0;
}

} // namespace ubw::hooks
