// Card 169: the held visuals an AbilityTask SpawnActor makes, on the client's own ability.
//
// UAbilityTask_SpawnActor::BeginSpawningActor (exe+0x494E3B0) spawns only when the task's Ability has net authority;
// on the client it broadcasts DidNotSpawn and returns false. The static sweep (docs/cards/169-ability-spawn-visuals.md)
// found three Blueprints that use the task, each for a local held visual that the Blueprint itself attaches to the
// first person mesh and destroys: GA_GrenadeThrowAbility (GrenadeVisuals), GA_UseTacticalSupportAbility (Flare
// Visuals) and GA_HumanMelee (KnifeVisuals). None spawns host state, so the client's own ability spawns locally.
//
// On a client-own ability the detour spawns the class deferred, as the native authority path does (identity
// transform, AlwaysSpawn, bDeferConstruction), and returns true: the Blueprint's own FinishSpawningActor finishes it
// and broadcasts Success, the same path the host runs. The by-value target data handle (the callee destroys it, x64
// ABI) is released by calling the original with the task's Ability cleared: with no Ability it neither spawns nor
// broadcasts DidNotSpawn, only destroys the handle and returns false. A failed spawn falls back to the original call.
//
// Each spawned actor stays in a small held table until K2_DestroyActor (the Blueprints' end of a held visual), where
// the client logs whether it was rendered in the last 0.2 s and its distance from the camera.

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

C169BeginSpawningFn g_orig_c169_begin_spawning = nullptr;
C169DestroyFn g_orig_c169_destroy = nullptr;
C169ServerTryFn g_orig_c174_server_try = nullptr;
C174EventDataDtorFn g_c174_event_data_dtor = nullptr;
C324ViewportSizeFn g_c324_viewport_size = nullptr;
StaticClassFn g_c174_tac_class = nullptr;
C169ObjectGetFn g_c169_anim_instance = nullptr;
C169ObjectGetFn g_c169_active_montage = nullptr;
C169MontagePositionFn g_c169_montage_position = nullptr;

namespace {

constexpr size_t kTaskAbility = 0x68;          // UAbilityTask::Ability (BeginSpawningActor+0x26)
constexpr size_t kSpawnDeferByte = 0x32;       // FActorSpawnParameters bitfield (BeginSpawningActor+0xDD: or 4)
constexpr uint8_t kSpawnDeferBit = 0x04;       // bDeferConstruction
constexpr size_t kSpawnScaleMethod = 0x31;     // TransformScaleMethod (BeginSpawningActor+0xEB writes 0x101 at 0x30)
constexpr uint8_t kSpawnAlwaysSpawn = 1;       // ESpawnActorCollisionHandlingMethod::AlwaysSpawn
constexpr uint8_t kSpawnScaleMultiply = 1;     // ESpawnActorScaleMethod::MultiplyWithRoot
constexpr int32_t kC169Held = 8;               // held visuals at once (one per ability in use)
constexpr uint32_t kC169Lines = 60;            // card169 lines per session
constexpr float kC169RenderTolerance = 0.2f;   // WasRecentlyRendered window, seconds
constexpr size_t kPawnFirstPersonMesh = 0x848; // ASTPlayerCharacter::FirstPersonMesh (remote_body.cpp)
// Card 174: UAbilitySystemComponent fields (card 017 notes in hooks_internal.h).
constexpr size_t kC174AscAvatar = 0x3E8;       // AvatarActor (SetAvatarActor_Direct)
constexpr size_t kC174AscSpecs = 0x508;        // ActivatableAbilities.Items (FindAbilitySpecFromHandle)
constexpr size_t kC174AscSpecCount = 0x510;    // ActivatableAbilities.Items Num
constexpr size_t kC174SpecBytes = 0xF0;        // one FGameplayAbilitySpec
constexpr size_t kC174SpecHandle = 0x0C;       // FGameplayAbilitySpec::Handle
constexpr size_t kC174SpecAbility = 0x10;      // FGameplayAbilitySpec::Ability
constexpr int32_t kC174MaxSpecs = 256;         // bound on the spec walk
constexpr uint32_t kC174Lines = 20;            // card174 lines per session

// Card 331: actor is compared raw (the destroy detour's match: a matched actor is alive); every read of actor or
// avatar on a later tick goes through c169_live, so a pawn or visual freed without K2_DestroyActor reads null.
struct C169Held {
    const void* actor;
    const void* avatar; // the pawn whose ability spawned it, for the camera distance and its montage
    C31Weak actor_weak;
    C31Weak avatar_weak;
    ULONGLONG at;
    uint32_t who;       // sprint_side of the avatar
    bool viewed;        // card 324: the row 300 ms after the spawn is written
};

struct C169State {
    C169Held held[kC169Held];
    int32_t n;
    uint32_t lines;
    uint32_t spawned;
    uint32_t failed;
    uint32_t rendered;
    uint32_t unseen;
    uint32_t c174_lines;   // card 174: skipped server sends and blocked local flare throws
    uint32_t c174_skipped; // ServerTryActivateAbilityWithEventData sends not made
    uint32_t c174_blocked; // client-own TryThrowFlare calls not run
    uint32_t c324_lines;   // card 324: view rows this map
};
C169State g_c169{};

bool c169_line() {
    return note_event_cap(g_c169.lines, kC169Lines, "card169 lines");
}

// The class spawned deferred into the avatar's world, as the authority path spawns it, or null.
void* c169_spawn(const void* avatar, void* cls) {
    void* const world = avatar != nullptr ? g_actor_get_world(avatar) : nullptr;
    if (world == nullptr || cls == nullptr) {
        return nullptr;
    }
    alignas(16) uint8_t params[kSpawnParamsSize] = {};
    g_spawn_params_ctor(params);
    params[kSpawnCollisionOverride] = kSpawnAlwaysSpawn;
    params[kSpawnScaleMethod] = kSpawnScaleMultiply;
    params[kSpawnDeferByte] |= kSpawnDeferBit;
    const Vec3 at{};
    const Vec3 rot{};
    return g_world_spawn_actor(world, cls, &at, &rot, params);
}

// The original with no Ability: it skips the spawn and the DidNotSpawn broadcast and destroys the target data.
void c169_release_target_data(void* task, void* ability, void* target_data, void* const* cls) {
    uint8_t* const t = static_cast<uint8_t*>(task);
    void* const none = nullptr;
    memcpy(t + kTaskAbility, &none, sizeof(none));
    void* spawned = nullptr;
    const bool ok = g_orig_c169_begin_spawning(task, ability, target_data, cls, &spawned);
    memcpy(t + kTaskAbility, &ability, sizeof(ability));
    if ((ok || spawned != nullptr) && c169_line()) {
        log::line("WARN card169: client BeginSpawningActor with no Ability returned %d, actor %s", ok ? 1 : 0,
                  object_name(spawned).text);
    }
}

void c169_hold(const void* actor, const void* avatar, uint32_t who, ULONGLONG now) {
    if (g_c169.n >= kC169Held) {
        if (c169_line()) {
            log::line("WARN card169: client held table full (%d): %s not tracked", kC169Held, object_name(actor).text);
        }
        return;
    }
    C169Held& h = g_c169.held[g_c169.n++];
    h = C169Held{actor, avatar, {}, {}, now, who, false};
    c31_weak_set(h.actor_weak, const_cast<void*>(actor));
    c31_weak_set(h.avatar_weak, const_cast<void*>(avatar));
}

// Card 331: the entry with actor and avatar re-read from their weak pointers (null when gone).
C169Held c169_live(const C169Held& h) {
    C169Held out = h;
    out.actor = c31_weak_get(h.actor_weak);
    out.avatar = c31_weak_get(h.avatar_weak);
    return out;
}

// USceneComponent::AttachSocketName as text, by reflection once ("?" when the property is not found).
void c169_socket(const void* root, char* out, uint32_t cap) {
    static int32_t offset = -1;
    if (offset < 0 && root != nullptr) {
        const uint8_t* const f = prop_find_prop(read_ptr(root, kObjectClass), "AttachSocketName");
        offset = f != nullptr ? read_i32(f, kPropertyOffset) : 0;
    }
    snprintf(out, cap, "?");
    if (root != nullptr && offset > 0) {
        fname_into(static_cast<const uint8_t*>(root) + offset, out, cap);
    }
}

// The avatar's first person montage at this moment: its name and position in seconds ("none", -1 when not playing).
struct C169Montage {
    const void* montage;
    float position;
};
C169Montage c169_montage(const void* avatar) {
    const void* const mesh = avatar != nullptr ? read_ptr(avatar, kPawnFirstPersonMesh) : nullptr;
    const void* const anim = mesh != nullptr ? g_c169_anim_instance(mesh) : nullptr;
    const void* const montage = anim != nullptr ? g_c169_active_montage(anim) : nullptr;
    return C169Montage{montage, montage != nullptr ? g_c169_montage_position(anim, montage) : -1.0f};
}

// Card 324 (diagnostic): what the player sees of a held visual. The FirstPersonMesh's own visible flag and
// bHiddenInGame, the visual's mesh component's bVisible, bOwnerNoSee and bOnlyOwnerSee (by reflection), the arms'
// hand_r socket projected to the local controller's viewport (x, y and on screen 0|1), and the montage on the
// FirstPersonMesh with its position. Written 300 ms after the spawn (c169_step) and at the destroy, once per visual
// class per map past the first kC324Lines rows (the sibling sweep: grenade, flare, satchel, medkit, knife).
constexpr uint32_t kC324Lines = 48;          // card324 rows per map
constexpr ULONGLONG kC324ViewMs = 300;       // the spawn row's delay
constexpr size_t kC324Flag = 8;              // one reflected flag's text
void c324_view_row(const C169Held& h, const char* stage, ULONGLONG now) {
    if (!note_event_cap(g_c169.c324_lines, kC324Lines, "card324")) {
        return;
    }
    static void* mesh_class = nullptr;
    mesh_class = mesh_class != nullptr ? mesh_class : find_class(L"/Script/Engine.MeshComponent");
    static uint64_t hand_name = 0;
    const void* const arms = h.avatar != nullptr ? read_ptr(h.avatar, kPawnFirstPersonMesh) : nullptr;
    const void* const mesh = mesh_class != nullptr ? find_component_of(h.actor, mesh_class) : nullptr;
    char arms_hidden[kC324Flag] = "-";
    char vis[kC324Flag] = "-";
    char owner_no_see[kC324Flag] = "-";
    char only_owner_see[kC324Flag] = "-";
    if (arms != nullptr) {
        c22_prop_text(arms, "bHiddenInGame", arms_hidden, sizeof(arms_hidden));
    }
    if (mesh != nullptr) {
        c22_prop_text(mesh, "bVisible", vis, sizeof(vis));
        c22_prop_text(mesh, "bOwnerNoSee", owner_no_see, sizeof(owner_no_see));
        c22_prop_text(mesh, "bOnlyOwnerSee", only_owner_see, sizeof(only_owner_see));
    }
    const uint64_t hand = fname_cached(hand_name, L"hand_r");
    void* const world = h.avatar != nullptr ? g_actor_get_world(h.avatar) : nullptr;
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    Vec3 at{};
    double screen[2] = {-1.0, -1.0};
    int32_t w = 0;
    int32_t ht = 0;
    bool projected = false;
    if (arms != nullptr && hand != 0 && pc != nullptr && g_c136_socket_location != nullptr &&
        g_project_to_screen != nullptr && g_c324_viewport_size != nullptr) {
        g_c136_socket_location(arms, &at, hand);
        const double where[3] = {at.x, at.y, at.z};
        projected = g_project_to_screen(pc, where, screen, true);
        g_c324_viewport_size(pc, &w, &ht);
    }
    const bool on_screen = projected && screen[0] >= 0.0 && screen[1] >= 0.0 && screen[0] < w && screen[1] < ht;
    const C169Montage m = c169_montage(h.avatar);
    const uint8_t* const a = static_cast<const uint8_t*>(arms);
    log::line("card324: %s t=%llu %s %s (%s) at %s, %llu ms: FirstPersonMesh visible %d hidden in game %s; mesh %s "
              "visible %s owner no see %s only owner see %s; hand_r on screen %d at (%.0f, %.0f) of %dx%d; montage %s "
              "at %.3f s",
              side(), static_cast<unsigned long long>(now), class_name(h.actor).text,
              h.who == kSideServerCopy ? "server copy" : "own pawn", object_name(h.actor).text, stage,
              static_cast<unsigned long long>(now - h.at),
              a != nullptr && (a[kSceneVisibleByte] & kSceneVisibleBit) != 0 ? 1 : 0, arms_hidden,
              mesh != nullptr ? class_name(mesh).text : "none", vis, owner_no_see, only_owner_see, on_screen ? 1 : 0,
              screen[0], screen[1], w, ht, object_name(m.montage).text, m.position);
}

} // namespace

// Card 324: game thread, every tick. The row 300 ms after each held visual's spawn.
void c169_step() {
    const ULONGLONG now = GetTickCount64();
    for (int32_t i = 0; i < g_c169.n; ++i) {
        C169Held& h = g_c169.held[i];
        const C169Held live = c169_live(h);
        if (live.actor == nullptr) { // card 331: gone without K2_DestroyActor; the entry goes, no row
            g_c169.held[i--] = g_c169.held[--g_c169.n];
            continue;
        }
        if (!h.viewed && now - h.at >= kC324ViewMs) {
            h.viewed = true;
            c324_view_row(live, "300 ms", now);
        }
    }
}

// Client own ability: spawn locally (the Blueprint's FinishSpawningActor then broadcasts Success). Host own or a
// server copy's: the original, its actor tracked (the host's own is the control; the server copy's shows what the
// host spawns for a client's ability). Else the original.
bool detour_c169_begin_spawning(void* task, void* ability, void* target_data, void* const* cls, void** spawned) {
    const void* const avatar = ability != nullptr ? avatar_of(read_ptr(ability, kAbilityActorInfo)) : nullptr;
    const uint32_t who = avatar != nullptr ? sprint_side(avatar) : kSideOther;
    if (!g_client_session && (who == kSideHostOwn || who == kSideServerCopy)) {
        // The host's own held visual (the control for the client's) or a server copy's (same destroy-time check).
        const bool ok = g_orig_c169_begin_spawning(task, ability, target_data, cls, spawned);
        if (ok && *spawned != nullptr) {
            c169_hold(*spawned, avatar, who, GetTickCount64());
        }
        return ok;
    }
    if (!g_client_session || who != kSideClientOwn || read_ptr(task, kTaskAbility) != ability) {
        return g_orig_c169_begin_spawning(task, ability, target_data, cls, spawned);
    }
    void* const c = cls != nullptr ? *cls : nullptr;
    void* const actor = c169_spawn(avatar, c);
    const ULONGLONG now = GetTickCount64();
    if (actor == nullptr) {
        ++g_c169.failed;
        if (c169_line()) {
            log::line("WARN card169: client t=%llu %s: %s not spawned (no world or class); DidNotSpawn as before",
                      static_cast<unsigned long long>(now), class_name(ability).text, object_name(c).text);
        }
        return g_orig_c169_begin_spawning(task, ability, target_data, cls, spawned);
    }
    c169_release_target_data(task, ability, target_data, cls);
    *spawned = actor;
    ++g_c169.spawned;
    c169_hold(actor, avatar, who, now);
    if (c169_line()) {
        log::line("card169: client t=%llu %s: spawned %s (%s) for its own ability; FinishSpawningActor and Success next",
                  static_cast<unsigned long long>(now), class_name(ability).text, object_name(actor).text,
                  class_name(actor).text);
    }
    return true;
}

// Card 174: true when `handle` names a USTUseTacticalSupportAbility (or child) spec on `asc` (class pointer test).
bool c174_tac_spec(const void* asc, int32_t handle) {
    const uint8_t* const items = static_cast<const uint8_t*>(read_ptr(asc, kC174AscSpecs));
    const int32_t n = read_i32(asc, kC174AscSpecCount);
    const void* const tac = g_c174_tac_class != nullptr ? g_c174_tac_class() : nullptr;
    for (int32_t i = 0; items != nullptr && tac != nullptr && i < n && i < kC174MaxSpecs; ++i) {
        const uint8_t* const spec = items + static_cast<size_t>(i) * kC174SpecBytes;
        if (read_i32(spec, kC174SpecHandle) == handle) {
            const void* const ability = read_ptr(spec, kC174SpecAbility);
            return ability != nullptr && is_a(ability, tac);
        }
    }
    return false;
}

// Card 174: UAbilitySystemComponent::ServerTryActivateAbilityWithEventData, the client's activation RPC stub. For
// the client's own pawn and a tactical support ability, the send is not made: the event data's target data
// (STGameplayAbilityTargetData_TacticalSupportInput) has no NetSerialize (card 100: LowLevelFatalError), and the
// host's server copy runs the ability from the TacSupport slot instead. The by-value event data belongs to the
// callee (x64 ABI: the caller passes its temporary by pointer); the stub's last act is ~FGameplayEventData on it
// (exe+0x49722E0), so the skip calls that destructor once. The prediction key arrives by pointer and is a plain
// 16-byte copy (movups, no destructor). Every other call runs the original.
void detour_c174_server_try(void* asc, int32_t handle, bool pressed, const void* key, void* event_data) {
    const void* const avatar = asc != nullptr ? read_ptr(asc, kC174AscAvatar) : nullptr;
    if (!g_client_session || avatar == nullptr || sprint_side(avatar) != kSideClientOwn ||
        g_c174_event_data_dtor == nullptr || !c174_tac_spec(asc, handle)) {
        g_orig_c174_server_try(asc, handle, pressed, key, event_data);
        return;
    }
    g_c174_event_data_dtor(event_data);
    ++g_c169.c174_skipped;
    if (note_event_cap(g_c169.c174_lines, kC174Lines, "card174 lines")) {
        log::line("card174: client t=%llu own %s: ServerTryActivateAbilityWithEventData(handle %d) not sent (the "
                  "host's server copy runs it from TacSupport); local prediction runs (%u skipped)",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(avatar).text, handle,
                  g_c169.c174_skipped);
    }
}

// Card 174: the client's own local tactical support ability reaches OnDoAction; its TryThrowFlare would spend the
// charge and throw a client-only flare. Not run: the host's server copy throws the flare and spends the charge.
bool c174_block_throw(const void* component) {
    const void* const owner = component != nullptr ? read_ptr(component, kComponentOwner) : nullptr;
    if (!g_client_session || owner == nullptr || sprint_side(owner) != kSideClientOwn) {
        return false;
    }
    ++g_c169.c174_blocked;
    if (note_event_cap(g_c169.c174_lines, kC174Lines, "card174 lines")) {
        log::line("card174: client t=%llu own %s: TryThrowFlare not run here (the host's server copy throws) (%u "
                  "blocked)", static_cast<unsigned long long>(GetTickCount64()), object_name(owner).text,
                  g_c169.c174_blocked);
    }
    return true;
}

// Every K2_DestroyActor; work only while a held visual is tracked (pointer compares).
void detour_c169_destroy(void* actor) {
    for (int32_t i = 0; i < g_c169.n; ++i) {
        if (g_c169.held[i].actor != actor) {
            continue;
        }
        const C169Held h = c169_live(g_c169.held[i]); // card 331: avatar null when the pawn is gone
        g_c169.held[i] = g_c169.held[--g_c169.n];
        // Card 174: a server copy's held visual sits on a first person mesh the host never renders, so its unseen
        // end is expected; the client's own visual is the one that must be seen.
        const bool seen = g_actor_recently_rendered(actor, kC169RenderTolerance);
        ++(seen ? g_c169.rendered : g_c169.unseen);
        const void* const root = read_ptr(actor, kActorRootComponent);
        const Vec3 at = root != nullptr ? read_vec3(root, kComponentTranslation) : Vec3{};
        const Vec3 cam = camera_location(h.avatar);
        const double dx = at.x - cam.x;
        const double dy = at.y - cam.y;
        const double dz = at.z - cam.z;
        const ULONGLONG now = GetTickCount64();
        c324_view_row(h, "destroy", now); // card 324
        const uint8_t* const r = static_cast<const uint8_t*>(root);
        char socket[64];
        c169_socket(root, socket, sizeof(socket));
        const C169Montage m = c169_montage(h.avatar);
        if (c169_line()) {
            log::line("card169: %s t=%llu held %s of the %s destroyed during first person montage %s at %.3f s",
                      side(), static_cast<unsigned long long>(now), class_name(actor).text,
                      h.who == kSideServerCopy ? "server copy" : "own pawn", object_name(m.montage).text, m.position);
        }
        if (c169_line()) {
            log::line("%scard169: %s t=%llu held %s (%s) destroyed after %llu ms: rendered in the last %.1f s %d, "
                      "%.0f cm from the camera, attached to %s socket %s, root visible %d, actor hidden %d",
                      seen || h.who == kSideServerCopy ? "" : "WARN ", side(), static_cast<unsigned long long>(now), object_name(actor).text,
                      class_name(actor).text, static_cast<unsigned long long>(now - h.at), kC169RenderTolerance,
                      seen ? 1 : 0, sqrt(dx * dx + dy * dy + dz * dz), object_name(c140_attach_parent(root)).text,
                      socket, r != nullptr && (r[kSceneVisibleByte] & kSceneVisibleBit) != 0 ? 1 : 0,
                      (static_cast<const uint8_t*>(actor)[kActorHiddenByte] & kActorHiddenBit) != 0 ? 1 : 0);
        }
        break;
    }
    g_orig_c169_destroy(actor);
}

// Map change: held pointers are stale.
void c169_reset_map() {
    if (g_c169.spawned != 0 || g_c169.failed != 0) {
        log::line("card169: %s map totals: %u held visuals spawned (%u rendered, %u not, %d still held), %u not spawned",
                  side(), g_c169.spawned, g_c169.rendered, g_c169.unseen, g_c169.n, g_c169.failed);
    }
    g_c169 = C169State{};
}

} // namespace ubw::hooks
