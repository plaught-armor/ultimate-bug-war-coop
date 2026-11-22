// Remote body: the remote player sprite body and its test views (card 028), the remote pawn's
// first-person meshes hidden (card 033), the remote sprite's feet on the ground (card 034) and a
// remote mech keeping its body (card 075).

#include "ubw/hooks_internal.h"
#include <cmath>

namespace ubw::hooks {

StartSequenceFn g_start_sequence = nullptr;               // UADSpriteAnimatorComponent::StartAnimationSequence
StaticClassFn g_sprite_render_class = nullptr;            // USTSpriteRenderComponent
StaticClassFn g_variant_animator_class = nullptr;         // USTSpriteVariantAnimatorComponent

constexpr ULONGLONG kC28StepMs = 100;
constexpr ULONGLONG kC28AttachRetryMs = 1000;

constexpr ULONGLONG kC28FlinchMs = 300;
constexpr double kC28RunSpeed = 750.0;  // walk 700; sprint 1050 forward, 770 sideways or back (card 019)
constexpr double kC28WalkSpeed = 10.0;
// Card 564: the key flapped Walk -> Run -> Walk every 100 ms step (host: a walk at 700 sampled 650 to 800 from the
// location change; 575 Walk -> Run rows in one play log), and the client held keys under 150 ms, each change
// restarting the sequence from frame 0. Speed is smoothed, Run has a band, and a started Idle, Walk, Run, Flinch or
// Attack01 holds at least kC564HoldMs before another of those replaces it. Death, corpse and stand-up never wait.
constexpr ULONGLONG kC564HoldMs = 300;
constexpr double kC564SpeedBlend = 0.5;      // weight of the new 100 ms sample in the smoothed speed
constexpr double kC564RunLeaveSpeed = 720.0; // Run stays until the smoothed speed drops below this
constexpr int kC28MaxSupers = 4;        // renderer classes above UStaticMeshComponent
constexpr uint32_t kC28KeyLines = 600;  // key change lines per map
constexpr uint32_t kC28AttachLines = 16;
constexpr ULONGLONG kC28ShotDelayMs = 500; // UBWCOOP_C28_SHOTS: key change to its shot, so the sequence is running
// Client shot of the host's Run: the host sprints toward the client from about 1100 away (card 028 run 3), so wait
// until it is closer.
constexpr ULONGLONG kC28RunShotDelayMs = 1200;
constexpr size_t kAnimatorOnFrameChange = 0xF0;      // FMulticastScriptDelegate: invocation list TArray
constexpr size_t kAnimatorOnFrameSizeChange = 0x100;
constexpr size_t kAnimatorDataStatic = 0x128;         // AnimationData on the Morita's variant animator (card 027)

constexpr C29Set kC28Set = {"AnimationData/NPCs", "BP_CharacterAnimations_FemaleMoritaTrooper"};
// Card 034 (sprite ground fit). Offsets from the disassembly of FitMeshToCollider and K2_GetComponentScale; the
// relative ones are checked against reflection once per map (c34_check_offsets) before any refit.
constexpr size_t kRelativeLocation = 0x140;     // USceneComponent::RelativeLocation (reflected, run card034a)
constexpr size_t kRelativeRotation = 0x158;     // FitMeshToCollider reads it at +0x158/+0x168 and passes it through
constexpr size_t kRelativeScale3D = 0x170;      // USceneComponent::RelativeScale3D (FitMeshToCollider reads Z at +0x180)
constexpr size_t kComponentScale = 0x220;       // ComponentToWorld scale (K2_GetComponentScale)
constexpr double kC34QuadHalf = 50.0;           // FowardFacingQuad: 100 units tall, centred on the renderer
constexpr double kC34GroundTrace = 2000.0;      // downward trace from the capsule centre
constexpr size_t kDelegateEntry = 0x10;         // FScriptDelegate: FWeakObjectPtr (8) then FName (8)
constexpr size_t kDelegateFunction = 0x8;
constexpr int32_t kC34MaxBindings = 4;
constexpr uint32_t kC34FitLines = 48;           // per map
constexpr uint32_t kC34NpcLines = 16;
constexpr uint32_t kC34ZLines = 64;
constexpr uint32_t kC34WarnLines = 8;

constexpr const wchar_t* kC28KeyNames[] = {L"None",   L"Idle",     L"Walk",     L"Run",    L"Flinch",
                                           L"Attack01", L"Death01", L"Corpse01", L"Standup"};
static_assert(sizeof(kC28KeyNames) / sizeof(kC28KeyNames[0]) == static_cast<size_t>(C28Key::Count));

C28State g_c28{};
bool g_c28_shots = false; // UBWCOOP_C28_SHOTS; written in ubw::init before any hook
bool g_c92_log = false;   // UBWCOOP_C92_LOG; written in ubw::init before any hook

// Card 092 (log only): why the host's body bounces on the client's screen. Client, every engine frame for
// kC92WindowMs from the first frame the host's pawn (with a card 028 body) is within kC92OpenDist of the camera: the
// camera Z (PlayerCameraManager camera cache) and the own pawn's Z against the host pawn's actor Z, its sprite
// renderer's world and relative Z, key and frame, and the card 034 refits and size changes since the previous line.
constexpr ULONGLONG kC92WindowMs = 10000;
constexpr uint32_t kC92Lines = 1200;
constexpr size_t kC92PovLocation = 0x10; // FCameraCacheEntry: float Timestamp, pad, FMinimalViewInfo POV (Location first)
constexpr size_t kC92PovRotation = 0x28; // POV.Rotation, after the three-double Location
constexpr double kC92OpenDist = 600.0;   // the window opens on the first frame the host is this close (2D) to the camera
constexpr ULONGLONG kC92ShotMs = 4000;   // first of the client shot burst, after the window opens
constexpr ULONGLONG kC92ShotGapMs = 200;
constexpr uint32_t kC92Shots = 5;
struct C92State {
    int32_t remote;          // object index of the host pawn, -1 until seen
    C31Weak pawn;            // that pawn, refreshed by each c28 step that visits it (weak: card 331, it may be freed)
    ULONGLONG start_ms;      // first frame logged, 0 none yet
    ULONGLONG last_ms;
    uint32_t lines;
    uint32_t refits;         // card 034 refits of that pawn's renderer since the last line
    uint32_t sizes;          // frame-size changes of that renderer since the last line
    double size_w, size_h;   // last frame size
    int32_t cache_at;        // reflected CameraCachePrivate offset, -1 unresolved, -2 absent
    int32_t frame_at;        // reflected ActiveFrameIndex offset on the animator, -1 unresolved, -2 absent
    int32_t move_at;         // reflected CharacterMovement offset on the own pawn, -1 unresolved, -2 absent
    int32_t mode_at;         // reflected MovementMode offset on that component, -1 unresolved, -2 absent
    uint32_t shots;          // burst shots taken
    bool done;
};
C92State g_c92{-1, {}, 0, 0, 0, 0, 0, 0.0, 0.0, -1, -1, -1, -1, 0, false};

// Card 092: the burst of client shots, kC92ShotGapMs apart from kC92ShotMs after the window opened.
void c92_shot(void* pc, ULONGLONG now) {
    if (g_c92.shots < kC92Shots && now - g_c92.start_ms >= kC92ShotMs + g_c92.shots * kC92ShotGapMs) {
        request_shot(pc, false);
        ++g_c92.shots;
        log::line("card092: %s SHOT %u t=%llu", side(), g_c92.shots, now);
    }
}

struct alignas(16) C28Transform {
    double rotation[4]; // quaternion x, y, z, w
    double translation[4];
    double scale[4];
};
constexpr C28Transform kC28Identity = {{0, 0, 0, 1}, {0, 0, 0, 0}, {1, 1, 1, 0}};


// A reflected FVector / FRotator (three doubles) of `object`; zero when absent.
Vec3 c28_vec(const void* object, const char* name) {
    const uint8_t* const prop = object != nullptr ? prop_find_prop(read_ptr(object, kObjectClass), name) : nullptr;
    return prop != nullptr ? read_vec3(object, static_cast<size_t>(read_i32(prop, kPropertyOffset))) : Vec3{0, 0, 0};
}

// A reflected float of `object`, or `fallback` when absent.
float c28_float(const void* object, const char* name, float fallback) {
    const uint8_t* const prop = object != nullptr ? prop_find_prop(read_ptr(object, kObjectClass), name) : nullptr;
    return prop != nullptr ? read_f32(object, static_cast<size_t>(read_i32(prop, kPropertyOffset))) : fallback;
}

// The invocation count of the multicast delegate at `offset` of `animator`.
int32_t c28_bound(const void* animator, size_t offset) {
    return animator != nullptr ? read_i32(animator, offset + kArrayNum) : -1;
}

FrameSizeChangedFn g_orig_frame_size_changed = nullptr;
FitMeshFn g_fit_mesh = nullptr;

struct C34Z {
    double capsule_bottom; // root world z - CapsuleHalfHeight * root world scale Z
    double renderer;       // renderer world z
    double edge;           // quad bottom: renderer world z - 50 * renderer world scale Z
    double ground;         // downward trace hit z (the trace end when nothing blocks)
    bool hit;
};

// Once per map, from the template: true when the reflected offsets match the disassembly ones. False stops refits.
bool c34_check_offsets(const void* renderer) {
    const void* const cls = read_ptr(renderer, kObjectClass);
    const uint8_t* const loc = prop_find_prop(cls, "RelativeLocation");
    const uint8_t* const rot = prop_find_prop(cls, "RelativeRotation");
    const uint8_t* const scale = prop_find_prop(cls, "RelativeScale3D");
    const int32_t loc_at = loc != nullptr ? read_i32(loc, kPropertyOffset) : -1;
    const int32_t rot_at = rot != nullptr ? read_i32(rot, kPropertyOffset) : -1;
    const int32_t scale_at = scale != nullptr ? read_i32(scale, kPropertyOffset) : -1;
    const bool ok = loc_at == static_cast<int32_t>(kRelativeLocation) &&
                    rot_at == static_cast<int32_t>(kRelativeRotation) && scale_at == static_cast<int32_t>(kRelativeScale3D);
    log::line("%scard034: %s reflected RelativeLocation +0x%X (static +0x%zX), RelativeRotation +0x%X (static +0x%zX), "
              "RelativeScale3D +0x%X (static +0x%zX): %s",
              ok ? "" : "WARN ", side(), loc_at, kRelativeLocation, rot_at, kRelativeRotation, scale_at,
              kRelativeScale3D, ok ? "match, refit on" : "MISMATCH, refit off");
    return ok;
}

// Each OnFrameChange / OnFrameSizeChange binding of `animator`: bound object, its outer, and the function FName.
void c34_log_bindings(const char* who, const void* animator) {
    constexpr size_t kAt[] = {kAnimatorOnFrameChange, kAnimatorOnFrameSizeChange};
    constexpr const char* kName[] = {"OnFrameChange", "OnFrameSizeChange"};
    for (size_t d = 0; d < sizeof(kAt) / sizeof(kAt[0]); ++d) {
        const auto* const list = static_cast<const uint8_t*>(read_ptr(animator, kAt[d]));
        const int32_t num = read_i32(animator, kAt[d] + kArrayNum);
        log::line("card034: %s %s %s %s: %d bound", side(), who, object_name(animator).text, kName[d], num);
        for (int32_t i = 0; list != nullptr && i < num && i < kC34MaxBindings; ++i) {
            const uint8_t* const entry = list + static_cast<size_t>(i) * kDelegateEntry;
            const void* const target = g_weak_get(entry);
            log::line("card034:   [%d] %s (%s, outer %s) function %s (weak index %d)", i, object_name(target).text,
                      class_name(target).text, object_name(target != nullptr ? read_ptr(target, kObjectOuter) : nullptr).text,
                      fname_text(entry + kDelegateFunction).text, read_i32(entry, 0));
        }
    }
}

// World heights of `actor` (root a capsule) and its sprite `renderer`.
C34Z c34_world_z(const void* actor, const void* root, const void* renderer) {
    const Vec3 at = read_vec3(root, kComponentTranslation);
    const double hh = c28_float(root, "CapsuleHalfHeight", 0.0f);
    const Vec3 end{at.x, at.y, at.z - kC34GroundTrace};
    const Vec3 ground = c24_trace(actor, at, end);
    const double rz = read_vec3(renderer, kComponentTranslation).z;
    return C34Z{at.z - hh * read_vec3(root, kComponentScale).z, rz,
                rz - kC34QuadHalf * read_vec3(renderer, kComponentScale).z, ground.z, ground.z != end.z};
}

// One world z line for `actor` and its sprite renderer.
void c34_log_z_line(const char* who, const void* actor, const wchar_t* key) {
    const void* const root = read_ptr(actor, kActorRootComponent);
    const void* const renderer = find_component(actor, g_sprite_render_class);
    if (root == nullptr || renderer == nullptr) {
        log::line("WARN card034: %s world z %s %s: root %s, renderer %s", side(), who, object_name(actor).text,
                  object_name(root).text, object_name(renderer).text);
        return;
    }
    const C34Z z = c34_world_z(actor, root, renderer);
    log::line("card034: %s world z %s %s key %ls: capsule bottom %.1f, renderer %.1f, quad bottom %.1f, ground %.1f%s; "
              "quad bottom - capsule bottom %.1f, capsule bottom - ground %.1f; relative z %.1f, relative scale Z %.3f, "
              "world scale Z %.3f, relative pitch %.1f, capsule half-height %.1f",
              side(), who, object_name(actor).text, key, z.capsule_bottom, z.renderer, z.edge, z.ground,
              z.hit ? "" : " (no hit)", z.edge - z.capsule_bottom, z.capsule_bottom - z.ground,
              read_vec3(renderer, kRelativeLocation).z, read_vec3(renderer, kRelativeScale3D).z,
              read_vec3(renderer, kComponentScale).z, read_vec3(renderer, kRelativeRotation).x,
              static_cast<double>(c28_float(root, "CapsuleHalfHeight", 0.0f)));
}

// Copies one reflected property of the renderer: bools by mask, numbers by value, and object references to
// objects the NPC does not own (assets). True when copied.
bool c28_copy_field(const uint8_t* prop, const void* from, void* to, const void* npc) {
    ObjectName kind{};
    const PropKind k = prop_kind(prop, kind);
    const size_t off = static_cast<size_t>(read_i32(prop, kPropertyOffset));
    const size_t size = static_cast<size_t>(read_i32(prop, kPropertyElementSize));
    const auto* const src = static_cast<const uint8_t*>(from) + off;
    auto* const dst = static_cast<uint8_t*>(to) + off;
    if (k == PropKind::Bool) {
        const uint8_t mask = prop[kBoolFieldMask];
        const uint8_t at = prop[kBoolByteOffset];
        dst[at] = static_cast<uint8_t>((dst[at] & ~mask) | (src[at] & mask));
        return true;
    }
    const bool number = k == PropKind::Float || k == PropKind::Double || k == PropKind::SInt || k == PropKind::UInt;
    const bool object = strcmp(kind.text, "ObjectProperty") == 0 || strcmp(kind.text, "ObjectPtrProperty") == 0;
    const void* const value = object ? read_ptr(src, 0) : nullptr;
    const bool asset = value != nullptr && read_ptr(value, kObjectOuter) != npc && read_ptr(value, kObjectOuter) != from;
    if ((!number && !asset) || size == 0 || size > 8) {
        return false;
    }
    memcpy(dst, src, size);
    return true;
}

// Copies the renderer's own class settings (the classes above UStaticMeshComponent) and its StaticMesh.
uint32_t c28_copy_renderer(const void* from, void* to) {
    const void* const npc = read_ptr(from, kComponentOwner);
    uint32_t copied = 0;
    const void* s = read_ptr(from, kObjectClass);
    for (int depth = 0; depth < kC28MaxSupers && s != nullptr && strcmp(object_name(s).text, "StaticMeshComponent") != 0;
         ++depth, s = read_ptr(s, kStructSuper)) {
        const void* f = read_ptr(s, kStructChildProperties);
        for (int i = 0; i < kC22MaxOwnFields && f != nullptr; ++i, f = read_ptr(f, kFieldNext)) {
            copied += c28_copy_field(static_cast<const uint8_t*>(f), from, to, npc) ? 1u : 0u;
        }
    }
    const uint8_t* const mesh = prop_find_prop(read_ptr(from, kObjectClass), "StaticMesh");
    if (mesh == nullptr || !c28_copy_field(mesh, from, to, npc)) {
        log::line("WARN card028: %s StaticMesh not copied (property %s)", side(), mesh != nullptr ? "found" : "missing");
        return copied;
    }
    return copied + 1;
}

// The renderer's place on the pawn: the Morita renderer's relative x, y and scale; its z is the card 034 rule of
// USTSpriteRenderComponent::FitMeshToCollider(2 * pawn half-height), 50 * scale Z - pawn half-height, which puts the
// 100-unit quad's bottom edge on the capsule bottom (Morita: 114.3 - 100 = 14.3, its logged z). Rotation is
// identity: the renderer turns itself. Card 034's detour refits on every later frame-size change.
C28Transform c28_body_transform(const void* pawn, const void* npc, const void* npc_renderer) {
    const Vec3 loc = c28_vec(npc_renderer, "RelativeLocation");
    const Vec3 scale = c28_vec(npc_renderer, "RelativeScale3D");
    const double npc_hh = c28_float(read_ptr(npc, kActorRootComponent), "CapsuleHalfHeight", 0.0f);
    const double pawn_hh = c28_float(read_ptr(pawn, kActorRootComponent), "CapsuleHalfHeight", 0.0f);
    C28Transform t = kC28Identity;
    t.translation[0] = loc.x;
    t.translation[1] = loc.y;
    t.translation[2] = kC34QuadHalf * scale.z - pawn_hh;
    t.scale[0] = scale.x;
    t.scale[1] = scale.y;
    t.scale[2] = scale.z;
    log::line("card028: %s body offset: Morita renderer relative (%.1f, %.1f, %.1f) scale (%.3f, %.3f, %.3f), capsule "
              "half-height Morita %.1f pawn %.1f -> pawn relative z %.1f (card 034 fit rule; Morita by the same rule "
              "%.1f, card 028 rule gave %.1f)",
              side(), loc.x, loc.y, loc.z, scale.x, scale.y, scale.z, npc_hh, pawn_hh, t.translation[2],
              kC34QuadHalf * scale.z - npc_hh, loc.z - npc_hh + pawn_hh);
    return t;
}

// Before BeginPlay: AnimationData = the female set, bUseVariants off (else BeginPlay may pick a variant index).
bool c28_setup_animator(void* animator, const void* asset) {
    const void* const cls = read_ptr(animator, kObjectClass);
    const uint8_t* const data = prop_find_prop(cls, "AnimationData");
    const uint8_t* const use = prop_find_prop(cls, "bUseVariants");
    if (data == nullptr || use == nullptr) {
        log::line("WARN card028: %s %s: AnimationData %s, bUseVariants %s", side(), class_name(animator).text,
                  data != nullptr ? "found" : "missing", use != nullptr ? "found" : "missing");
        return false;
    }
    const size_t off = static_cast<size_t>(read_i32(data, kPropertyOffset));
    memcpy(static_cast<uint8_t*>(animator) + off, &asset, sizeof(asset));
    auto* const flags = static_cast<uint8_t*>(animator) + read_i32(use, kPropertyOffset);
    flags[use[kBoolByteOffset]] = static_cast<uint8_t>(flags[use[kBoolByteOffset]] & ~use[kBoolFieldMask]);
    log::line("card028: %s animator AnimationData +0x%zX (static +0x%zX, %s), bUseVariants +0x%X mask 0x%02X cleared",
              side(), off, kAnimatorDataStatic, off == kAnimatorDataStatic ? "match" : "MISMATCH",
              read_i32(use, kPropertyOffset), use[kBoolFieldMask]);
    return true;
}

// Once per map: how the Morita's body is wired (the template the remote body copies).
void c28_log_template(const void* npc, const void* renderer, const void* animator) {
    char text[kC22LineChars];
    c22_comp_text(renderer, text, sizeof(text));
    const Vec3 rot = c28_vec(renderer, "RelativeRotation");
    log::line("card028: %s template %s: renderer %s (%s) via FindComponentByClass (actor root %s), "
              "RelativeRotation (%.1f, %.1f, %.1f); animator %s (%s), AnimationData %s, OnFrameChange bound %d, "
              "OnFrameSizeChange bound %d",
              side(), object_name(npc).text, object_name(renderer).text, class_name(renderer).text,
              object_name(read_ptr(npc, kActorRootComponent)).text, rot.x, rot.y, rot.z, object_name(animator).text,
              class_name(animator).text, object_name(read_ptr(animator, kAnimatorDataStatic)).text,
              c28_bound(animator, kAnimatorOnFrameChange), c28_bound(animator, kAnimatorOnFrameSizeChange));
    log::line("card028:   renderer %s", text);
    c22_own_props("card028 Morita renderer", renderer, kC28MaxSupers);
    c22_own_props("card028 Morita animator", animator, kC28MaxSupers);
    g_c28.npc_renderer = object_index(renderer); // card 034: its size changes are logged, never refitted
    g_c28.offsets_ok = c34_check_offsets(renderer);
    c34_log_bindings("Morita animator", animator);
}

// The first BP_AI_Trooper_Morita_C in the world's levels, or null.
// With `close_to` (card 034), the one nearest to that actor's location instead.
const void* c28_find_morita(const void* world, const void* close_to = nullptr) {
    const void* const cls = g_find_first_object(nullptr, L"BP_AI_Trooper_Morita_C", 0, 0, nullptr);
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = levels != nullptr ? read_i32(world, kWorldLevels + kArrayNum) : 0;
    const Vec3 at = close_to != nullptr ? read_vec3(read_ptr(close_to, kActorRootComponent), kComponentTranslation) : Vec3{0, 0, 0};
    const void* best = nullptr;
    double best_d2 = 0.0;
    for (int32_t l = 0; cls != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const void* const level = levels[l];
        const auto* const actors = level != nullptr ? static_cast<void* const*>(read_ptr(level, kLevelActors)) : nullptr;
        const int32_t num = actors != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; a < num && a < kMaxLevelActors; ++a) {
            if (actors[a] == nullptr || !is_a(actors[a], cls)) {
                continue;
            }
            if (close_to == nullptr) {
                return actors[a];
            }
            const void* const root = read_ptr(actors[a], kActorRootComponent);
            if (root == nullptr) {
                continue;
            }
            const Vec3 p = read_vec3(root, kComponentTranslation);
            const double d2 = (p.x - at.x) * (p.x - at.x) + (p.y - at.y) * (p.y - at.y) + (p.z - at.z) * (p.z - at.z);
            if (best == nullptr || d2 < best_d2) {
                best = actors[a];
                best_d2 = d2;
            }
        }
    }
    return best;
}

// Card 034: world z of the remote pawn and of the Morita nearest to it, once per key per slot, logged one c28 step
// after the key changed (so the size change and its refit have run). Capped per map.
void c34_log_z(const uint8_t* pawn, const void* world, C28Slot& s) {
    const C28Key seen = s.z_seen;
    s.z_seen = s.key;
    const uint32_t bit = 1u << static_cast<size_t>(s.key);
    if (s.key == C28Key::None || s.key != seen || (s.z_keys & bit) != 0) {
        return;
    }
    s.z_keys |= bit;
    if (!note_event_cap(g_c28.z_lines, kC34ZLines, "card034 world z")) {
        return;
    }
    const wchar_t* const key = kC28KeyNames[static_cast<size_t>(s.key)];
    c34_log_z_line("remote", pawn, key);
    const void* const npc = c28_find_morita(world, pawn);
    if (npc != nullptr) {
        c34_log_z_line("nearest Morita", npc, kC28KeyNames[0]);
    }
}

// Card 034: the slot whose card 028 renderer is `renderer_index` and whose pawn is `owner_index`, or null. Both
// indices must match, so a recycled object index never refits a renderer card 028 did not add.
C28Slot* c34_slot_of_renderer(int32_t renderer_index, int32_t owner_index) {
    for (uint32_t i = 0; i < g_c28.count && i < kC28MaxPawns; ++i) {
        C28Slot& s = g_c28.slot[i];
        if (s.renderer == renderer_index && s.index == owner_index) {
            return &s;
        }
    }
    return nullptr;
}

// Card 034: FitMeshToCollider(2 * capsule half-height) on a card 028 renderer: after its size change (`size` set) and
// whenever the half-height differs from the last refit's (`size` null; crouch takes it 95 -> 55 and lowers the root,
// run card034a). The half-height is read from the owner's root at each refit. Logged: the first refit of each key per
// slot, and every half-height change.
void c34_refit(void* renderer, const void* owner, C28Slot& s, C28Key key, const double* size) {
    const float hh = c28_float(read_ptr(owner, kActorRootComponent), "CapsuleHalfHeight", 0.0f);
    if (!g_c28.offsets_ok || hh <= 0.0f || g_fit_mesh == nullptr) {
        if (note_event_cap(g_c28.warn_lines, kC34WarnLines, "card034 refit skipped")) {
            log::line("WARN card034: %s refit of %s skipped: offsets %s, half-height %.1f, FitMeshToCollider %s",
                      side(), object_name(owner).text, g_c28.offsets_ok ? "ok" : "unchecked or mismatched",
                      static_cast<double>(hh), g_fit_mesh != nullptr ? "resolved" : "missing");
        }
        return;
    }
    const Vec3 before = read_vec3(renderer, kRelativeLocation);
    const float old_hh = s.fit_hh;
    g_fit_mesh(renderer, 2.0f * hh);
    s.fit_hh = hh;
    if (s.index == g_c92.remote) {
        ++g_c92.refits; // card 092
    }
    const uint32_t bit = 1u << static_cast<size_t>(key);
    const bool first = (s.fit_keys & bit) == 0;
    s.fit_keys |= bit;
    if ((!first && size != nullptr) || !note_event_cap(g_c28.fit_lines, kC34FitLines, "card034 refit")) {
        return;
    }
    const Vec3 after = read_vec3(renderer, kRelativeLocation);
    log::line("card034: %s refit %s key %ls (%s %.1f x %.1f): scale Z %.3f, relative z %.1f -> %.1f, x %.1f -> %.1f, "
              "y %.1f -> %.1f, capsule half-height %.1f (last refit %.1f)",
              side(), object_name(owner).text, kC28KeyNames[static_cast<size_t>(key)],
              size != nullptr ? "size change" : "half-height change, frame", size != nullptr ? size[0] : 0.0,
              size != nullptr ? size[1] : 0.0, read_vec3(renderer, kRelativeScale3D).z, before.z, after.z, before.x,
              after.x, before.y, after.y, static_cast<double>(hh), static_cast<double>(old_hh));
}

// Card 034, each c28 step: refit when the remote pawn's capsule half-height differs from the last refit's.
void c34_track_half_height(const uint8_t* pawn, C28Slot& s) {
    const float hh = c28_float(read_ptr(pawn, kActorRootComponent), "CapsuleHalfHeight", 0.0f);
    if (hh == s.fit_hh) {
        return;
    }
    void* const renderer = find_component(pawn, g_sprite_render_class);
    if (renderer != nullptr && object_index(renderer) == s.renderer) {
        c34_refit(renderer, pawn, s, s.key, nullptr);
    }
}

// Card 034 detour on USTSpriteRenderComponent::AnimatorFrameSizeChanged. The original always runs; only a card 028
// renderer is refitted. The template Morita's size changes are logged (relative z and scale Z, before and after).
void detour_frame_size_changed(void* renderer, const double* size) {
    const int32_t index = object_index(renderer);
    const bool npc = g_c28.template_logged && index >= 0 && index == g_c28.npc_renderer;
    const Vec3 z0 = npc ? read_vec3(renderer, kRelativeLocation) : Vec3{0, 0, 0};
    const double s0 = npc ? read_vec3(renderer, kRelativeScale3D).z : 0.0;
    g_orig_frame_size_changed(renderer, size);
    const void* const owner = read_ptr(renderer, kComponentOwner);
    C28Slot* const s = owner != nullptr && g_c28.count > 0 ? c34_slot_of_renderer(index, object_index(owner)) : nullptr;
    if (s != nullptr) {
        if (s->index == g_c92.remote) { // card 092
            ++g_c92.sizes;
            g_c92.size_w = size[0];
            g_c92.size_h = size[1];
        }
        c34_refit(renderer, owner, *s, s->starting, size);
        return;
    }
    if (npc && note_event_cap(g_c28.npc_lines, kC34NpcLines, "card034 Morita size change")) {
        log::line("card034: %s Morita %s size change %.1f x %.1f: relative z %.1f -> %.1f, scale Z %.3f -> %.3f", side(),
                  object_name(owner).text, size[0], size[1], z0.z, read_vec3(renderer, kRelativeLocation).z, s0,
                  read_vec3(renderer, kRelativeScale3D).z);
    }
}

C28Slot* c28_slot(const void* pawn);

// Adds a deferred component of `cls` to the pawn, or null (logged).
void* c28_add(uint8_t* pawn, const void* cls) {
    void* holder = const_cast<void*>(cls);
    void* const component = g_add_component_by_class(pawn, &holder, false, &kC28Identity, true);
    if (component == nullptr) {
        log::line("WARN card028: %s AddComponentByClass(%s) on %s returned null", side(), object_name(cls).text,
                  object_name(pawn).text);
    }
    return component;
}

// The renderer first (copied settings, no collision, Morita offset), then the animator (female set). The animator's
// BeginPlay, run by FinishAddComponent, binds it to the renderer. True when both were added.
bool c28_attach(uint8_t* pawn, const void* npc, void* npc_renderer, void* npc_animator, const void* asset) {
    void* const renderer = c28_add(pawn, read_ptr(npc_renderer, kObjectClass));
    if (renderer == nullptr) {
        return false;
    }
    const uint32_t copied = c28_copy_renderer(npc_renderer, renderer);
    g_orig_set_collision_enabled(renderer, kC28NoCollision);
    const C28Transform at = c28_body_transform(pawn, npc, npc_renderer);
    g_finish_add_component(pawn, renderer, false, &at);
    C28Slot* const slot = c28_slot(pawn); // card 034: before the animator, whose BeginPlay may resize
    if (slot == nullptr) {
        log::line("WARN card034: %s slot table full: %s will not be refitted", side(), object_name(pawn).text);
    } else {
        slot->renderer = object_index(renderer);
    }
    void* const animator = c28_add(pawn, read_ptr(npc_animator, kObjectClass));
    if (animator == nullptr) {
        return false;
    }
    const bool set = c28_setup_animator(animator, asset);
    g_finish_add_component(pawn, animator, false, &kC28Identity);
    char text[kC22LineChars];
    c22_comp_text(renderer, text, sizeof(text));
    log::line("card028: %s attached to %s (index %d, role %u): renderer %s (%s, %u settings copied, registered "
              "found again %s), animator %s (%s, setup %s, AnimationData now %s, OnFrameChange bound %d, "
              "OnFrameSizeChange bound %d)",
              side(), object_name(pawn).text, object_index(pawn), pawn[kActorLocalRole], object_name(renderer).text,
              class_name(renderer).text, copied, find_component(pawn, g_sprite_render_class) == renderer ? "yes" : "NO",
              object_name(animator).text, class_name(animator).text, set ? "ok" : "FAILED",
              object_name(read_ptr(animator, kAnimatorDataStatic)).text, c28_bound(animator, kAnimatorOnFrameChange),
              c28_bound(animator, kAnimatorOnFrameSizeChange));
    log::line("card028:   new renderer %s", text);
    c34_log_bindings("remote animator", animator);
    return true;
}

// Card 413: the first object reference on `object` (its class chain) that holds a `cls` instance, or null. A class
// default object has no owned-component list, so FindComponentByClass finds nothing on it.
constexpr int kC413Supers = 12;
constexpr int kC413Fields = 400;
const void* c413_ref_of_class(const void* object, const void* cls) {
    const void* s = object != nullptr && cls != nullptr ? read_ptr(object, kObjectClass) : nullptr;
    for (int depth = 0; depth < kC413Supers && s != nullptr; ++depth, s = read_ptr(s, kStructSuper)) {
        const void* f = read_ptr(s, kStructChildProperties);
        for (int i = 0; i < kC413Fields && f != nullptr; ++i, f = read_ptr(f, kFieldNext)) {
            ObjectName kind{};
            (void)prop_kind(f, kind);
            if (strcmp(kind.text, "ObjectProperty") != 0 && strcmp(kind.text, "ObjectPtrProperty") != 0) {
                continue;
            }
            const void* const value = read_ptr(object, static_cast<size_t>(read_i32(f, kPropertyOffset)));
            if (value != nullptr && is_a(value, cls)) {
                return value;
            }
        }
    }
    return nullptr;
}

// Card 413: the Morita class default object, or null while the class is not loaded (front end, transition map). Its
// two default subobjects carry the same settings, offset and scale as a spawned Morita's (run c413b106a, 9 maps).
const void* c413_morita_default() {
    const void* const cls = g_find_first_object(nullptr, L"BP_AI_Trooper_Morita_C", 0, 0, nullptr);
    return cls != nullptr ? read_ptr(cls, kClassDefaultObject) : nullptr;
}

// Finds the template and the asset, logs the template once per map, then attaches. Retried each kC28AttachRetryMs.
void c28_try_attach(uint8_t* pawn, const void* world, ULONGLONG now) {
    if (now < g_c28.attach_next_ms) {
        return;
    }
    g_c28.attach_next_ms = now + kC28AttachRetryMs;
    // Card 413: a map with no Morita in play yet (FED_C2M1's first 30 s, playb72) takes the template from the class
    // default object, so the remote body never waits for one to spawn.
    const void* const live = c28_find_morita(world);
    const void* const npc = live != nullptr ? live : c413_morita_default();
    void* const npc_renderer = live != nullptr ? find_component(live, g_sprite_render_class)
                                               : const_cast<void*>(c413_ref_of_class(npc, g_sprite_render_class != nullptr ? g_sprite_render_class() : nullptr));
    void* const npc_animator = live != nullptr ? find_component(live, g_variant_animator_class)
                                               : const_cast<void*>(c413_ref_of_class(npc, g_variant_animator_class != nullptr ? g_variant_animator_class() : nullptr));
    // The set is loaded only once a template exists: without one nothing can be attached, and the load is a
    // synchronous one (140 and 591 ms on the client in the transition map, run c411b105a).
    const char* how = "not asked";
    const bool have = npc_renderer != nullptr && npc_animator != nullptr;
    const void* const asset = have ? c29_load_set(kC28Set, &how) : nullptr;
    if (note_event_cap(g_c28.attach_lines, kC28AttachLines, "card028 attach")) {
        log::line("card028: %s attach %s: Morita %s (%s), renderer %s, animator %s, %s %s", side(),
                  object_name(pawn).text, object_name(npc).text, live != nullptr ? "in play" : "class default",
                  object_name(npc_renderer).text, object_name(npc_animator).text, kC28Set.name, how);
    }
    if (!have || asset == nullptr) {
        return;
    }
    if (!g_c28.template_logged) {
        g_c28.template_logged = true;
        c28_log_template(npc, npc_renderer, npc_animator);
    }
    c28_attach(pawn, npc, npc_renderer, npc_animator, asset);
}

// The slot of `pawn`, added when new (card 275: a gone pawn's slot when the table is full); null when full.
C28Slot* c28_slot(const void* pawn) {
    const C28Slot fresh{object_index(pawn), C28Key::None, -1.0f, 0, 0, 0, 0, Vec3{0, 0, 0}, false, false, Vec3{0, 0, 0}, 0,
                        0.0, 0.0, 0, -1, C28Key::None, 0, 0, C28Key::None, 0.0f};
    return c275_slot(C275Table::Body, g_c28.slot, g_c28.count, kC28MaxPawns, pawn, fresh);
}

// Card 275: one weak handle per slot of each table, parallel to the slots (the tables keep object indices only).
constexpr uint32_t kC275Lines = 24;
constexpr const char* kC275TableText[] = {"card022", "card028", "card033"};
static_assert(sizeof(kC275TableText) / sizeof(kC275TableText[0]) == static_cast<size_t>(C275Table::Count));
struct C275State {
    C31Weak weak[static_cast<size_t>(C275Table::Count)][kC28MaxPawns];
    uint32_t lines;
};
C275State g_c275{}; // game thread

void c275_note(C275Table table, uint32_t at, const void* pawn) {
    if (at >= kC28MaxPawns || g_weak_get == nullptr || g_weak_assign == nullptr) {
        return;
    }
    C31Weak& w = g_c275.weak[static_cast<size_t>(table)][at];
    if (g_weak_get(&w) != pawn) {
        c31_weak_set(w, const_cast<void*>(pawn));
    }
}

int32_t c275_gone_slot(C275Table table, uint32_t count) {
    if (g_weak_get == nullptr || g_weak_assign == nullptr) { // no handles were set: every slot would read as gone
        return -1;
    }
    for (uint32_t i = 0; i < count && i < kC28MaxPawns; ++i) {
        if (g_weak_get(&g_c275.weak[static_cast<size_t>(table)][i]) == nullptr) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

void c275_log_reuse(C275Table table, int32_t old_index, const void* pawn) {
    if (note_event_cap(g_c275.lines, kC275Lines, "card275 slot reuse")) {
        log::line("card275: %s %s slot of gone pawn index %d reused for %s", side(),
                  kC275TableText[static_cast<size_t>(table)], old_index, object_name(pawn).text);
    }
}

// Client: the simulated proxy's replicated CMC velocity. Host: the server copy's CMC velocity reads near 0 while the
// client walks (card 028 run 2: 34 then 8 during a 3 s walk at 700), so the speed is the 2D location change since the
// last sample instead.
double c28_speed(const void* pawn, C28Slot& s, ULONGLONG now) {
    if (g_client_session) {
        const void* const cmc = read_ptr(pawn, kCharacterMovement);
        const Vec3 v = cmc != nullptr ? read_vec3(cmc, kMoveVelocity) : Vec3{0, 0, 0};
        return sqrt(v.x * v.x + v.y * v.y);
    }
    const Vec3 at = actor_location(pawn);
    const double dx = at.x - s.step_loc.x;
    const double dy = at.y - s.step_loc.y;
    const ULONGLONG dt = s.step_ms != 0 ? now - s.step_ms : 0;
    s.step_loc = at;
    s.step_ms = now;
    return dt != 0 ? sqrt(dx * dx + dy * dy) * 1000.0 / static_cast<double>(dt) : 0.0;
}

C28Key c28_pick(const uint8_t* pawn, C28Slot& s, ULONGLONG now) {
    const C28Key forced = c43_sprite_key(pawn, now);
    if (forced != C28Key::None) {
        return forced;
    }
    void* component = nullptr;
    const float health = c21_health(pawn, &component);
    const bool known = health >= 0.0f;
    if (known && s.health >= 0.0f && health < s.health) {
        s.hurt_ms = now;
    }
    s.health = health;
    if (known && health <= 0.0f) {
        s.dead_ms = s.dead_ms != 0 ? s.dead_ms : now;
        return now - s.dead_ms >= kC28DeathMs ? C28Key::Corpse : C28Key::Death;
    }
    s.dead_ms = 0;
    if (c26_firing(pawn)) {
        return C28Key::Attack;
    }
    if (s.hurt_ms != 0 && now - s.hurt_ms < kC28FlinchMs) {
        return C28Key::Flinch;
    }
    const double speed = c28_speed(pawn, s, now);
    s.speed = speed;
    s.speed_avg = s.key_ms == 0 ? speed : s.speed_avg + (speed - s.speed_avg) * kC564SpeedBlend;
    const double run_at = s.key == C28Key::Run ? kC564RunLeaveSpeed : kC28RunSpeed;
    if (s.speed_avg > run_at) {
        return C28Key::Run;
    }
    return speed > kC28WalkSpeed ? C28Key::Walk : C28Key::Idle;
}

// Card 564: true for the keys that wait out kC564HoldMs (everything but death, corpse and stand-up).
bool c564_held_key(C28Key key) {
    return key == C28Key::Idle || key == C28Key::Walk || key == C28Key::Run || key == C28Key::Flinch ||
           key == C28Key::Attack;
}

// Starts the key's sequence when it differs from the last one started, and logs the change.
void c28_drive(const uint8_t* pawn, void* animator, C28Slot& s, ULONGLONG now) {
    const C28Key key = c28_pick(pawn, s, now);
    if (key == s.key) {
        return;
    }
    if (c564_held_key(key) && c564_held_key(s.key) && s.key_ms != 0 && now - s.key_ms < kC564HoldMs) {
        return;
    }
    const auto k = static_cast<size_t>(key);
    const uint64_t name = fname_cached(g_c28.key_name[k], kC28KeyNames[k]);
    if (name == 0) {
        log::line("WARN card028: %s no FName for key %ls", side(), kC28KeyNames[k]);
        return;
    }
    s.starting = key; // card 034: names the key of a size change raised inside the call
    g_start_sequence(animator, name, 1.0f);
    if (note_event_cap(g_c28.key_lines, kC28KeyLines, "card028 key change")) {
        log::line("card028: %s t=%llu %s key %ls -> %ls (health %.1f, speed %.0f avg %.0f, firing %d, yaw %.1f)", side(),
                  static_cast<unsigned long long>(now), object_name(pawn).text,
                  kC28KeyNames[static_cast<size_t>(s.key)], kC28KeyNames[k], static_cast<double>(s.health),
                  s.speed, s.speed_avg, c26_firing(pawn) ? 1 : 0, c28_vec(read_ptr(pawn, kActorRootComponent), "RelativeRotation").y);
    }
    s.key = key;
    s.key_ms = now;
    const uint32_t bit = 1u << k;
    if (g_c28_shots && (s.shot_keys & bit) == 0 && s.shot_ms == 0) {
        s.shot_keys |= bit;
        s.shot_ms = now + (g_client_session && key == C28Key::Run ? kC28RunShotDelayMs : kC28ShotDelayMs);
    }
}

// Remote: a simulated proxy (the host's pawn on the client) or a pawn held by a remote player controller (the
// client's pawn on the host). Never the machine's own pawn, nor the machine's own character while it rides a pawn the
// local player drives: a client's character on a turret is unpossessed there and replicates as a simulated proxy,
// and it got a sprite body in front of the client's own turret view (card 125, c125w13).
// Card 560: never the local player's own trooper while the player flies a guided missile or sits in a seat: it is
// unpossessed then and replicates as a simulated proxy, so it read as remote and got a sprite body in front of the
// player's own camera. Ownership is the local player's last on-foot pawn (c28_step), not the current controller.
bool c28_remote(const uint8_t* pawn) {
    if (g_c28.own_trooper != 0 && object_index(pawn) == g_c28.own_trooper) {
        return false;
    }
    const void* const controller = read_ptr(pawn, kPawnController);
    const bool remote_pc = controller != nullptr && is_a(controller, g_st_pc_class()) && !is_local_pc(controller);
    if (remote_pc || pawn[kActorLocalRole] != kRoleSimulatedProxy) {
        return remote_pc;
    }
    const void* const ridden = g_get_attach_parent_actor(pawn);
    const void* const driver = is_a(ridden, g_pawn_class()) ? read_ptr(ridden, kPawnController) : nullptr;
    return !is_a(driver, g_st_pc_class()) || !is_local_pc(driver);
}

// Attaches or drives one pawn. True when it is a remote pawn with a body.
// ---- Card 192: MI-only fields on a player pawn ----
// The weapon and ammunition inventories (kMiWeaponInventory, kMiAmmoInventory) are ASTPlayerCharacterMI fields. A
// bug-side map's player pawn (BP_PlayerCharacterBug_C, an ASTPlayerCharacterBug) has other data there: a read of it as
// an inventory crashed in c33_hide. Every site that reads them on a player pawn of any class asks this first.
constexpr int32_t kC192Features = 8; // distinct skipped features logged per map; later ones are counted only

struct C192Skips {
    const char* feature[kC192Features];
    uint32_t count[kC192Features];
    int32_t n;
    uint32_t unlisted;
    const char* side; // latched at each skip: at map end the session flag is already clear (card 177 r1c)
};
C192Skips g_c192{}; // game thread

bool c192_mi_pawn(const void* pawn, const char* feature) {
    if (pawn == nullptr) {
        return false;
    }
    if (is_a(pawn, g_player_mi_class())) {
        return true;
    }
    g_c192.side = side();
    int32_t i = 0;
    while (i < g_c192.n && g_c192.feature[i] != feature) {
        ++i;
    }
    if (i < g_c192.n) {
        ++g_c192.count[i];
        return false;
    }
    if (g_c192.n >= kC192Features) {
        ++g_c192.unlisted;
        return false;
    }
    g_c192.feature[g_c192.n] = feature;
    g_c192.count[g_c192.n++] = 1;
    log::line("card192: %s skips %s on %s (%s): not an ASTPlayerCharacterMI, no MI inventory (once per map)", side(),
              feature, object_name(pawn).text, class_name(pawn).text);
    return false;
}

void c192_arm_map() {
    const char* const who = g_c192.side != nullptr ? g_c192.side : side();
    for (int32_t i = 0; i < g_c192.n; ++i) {
        log::line("card192: %s map totals: %s skipped %u times", who, g_c192.feature[i], g_c192.count[i]);
    }
    if (g_c192.unlisted != 0) {
        log::line("card192: %s map totals: %u skips of features past the %d listed", who, g_c192.unlisted,
                  kC192Features);
    }
    g_c192 = C192Skips{};
}

// ---- Card 033: hide the remote pawn's first-person meshes ----
// A remote pawn's FirstPersonMesh (SKM_Arms, attached to Camera) and its equipped weapon actor are its owner's
// first-person view; on the viewing machine they float above the card 028 sprite. Run only from c28_visit (remote
// pawns, never standalone), so the owner's own view is untouched. Hidden with USceneComponent::SetHiddenInGame (the
// private worker behind the inline wrappers) with propagation to attached children. The weapon's root component is
// hidden as well, in case it is not under the arms. Rechecked every c28 step: a component shown again is re-hidden.
// Weapon actors do not replicate (card 029), so each machine hides its own copy.
constexpr uint8_t kVisibilityPropagate = 2;    // EVisibilityPropagation { NoPropagation, DirtyOnly, Propagate }
constexpr size_t kPawnFirstPersonMesh = 0x848; // ASTPlayerCharacter::FirstPersonMesh (card 022: reflected match)
constexpr uint32_t kC33Lines = 64;             // hide lines per map

constexpr const char* kC33Visible[] = {"bVisible", nullptr};

C33State g_c33{};

C33Slot* c33_slot(const void* pawn) {
    const C33Slot fresh{object_index(pawn), -1, -1, INT32_MIN}; // card 196: form key -1 is a root id too
    return c275_slot(C275Table::Hide, g_c33.slot, g_c33.count, kC28MaxPawns, pawn, fresh);
}

// Hide `comp` (and its attached children) unless its bHiddenInGame is already set. `last` is the object index of the
// component hidden last time in this role: the same one again means the game showed it again ("re-hid").
void c33_keep_hidden(const uint8_t* pawn, void* comp, const char* what, int32_t& last) {
    const int hidden = comp != nullptr ? c29_bool(comp, kC33Hidden) : 1;
    if (hidden == 1) {
        return;
    }
    const int visible = c29_bool(comp, kC33Visible);
    g_set_hidden_in_game(comp, true, kVisibilityPropagate);
    const int32_t id = object_index(comp);
    const char* const verb = last == id ? "re-hid" : last < 0 ? "hid" : "hid new";
    last = id;
    if (!note_event_cap(g_c33.lines, kC33Lines, "card033 hide")) {
        return;
    }
    const uint8_t* prop = nullptr;
    const void* const parent = prop_object_field(comp, "AttachParent", &prop);
    log::line("card033: %s %s %s of %s: %s (%s), owner %s, attach parent %s (%s); before bVisible %d bHiddenInGame %d, "
              "after bVisible %d bHiddenInGame %d",
              side(), verb, what, object_name(pawn).text, object_name(comp).text, class_name(comp).text,
              object_name(read_ptr(comp, kObjectOuter)).text, object_name(parent).text,
              parent != nullptr ? class_name(parent).text : "-", visible, hidden, c29_bool(comp, kC33Visible),
              c29_bool(comp, kC33Hidden));
}

// Card 196: a remote bug pawn's equipped form root and its visibility, or which of inventory, form and root is null:
// one line per new key per pawn, capped per run. Log only: no visibility write on a bug pawn (a form has no root).
constexpr uint32_t kC196FormLines = 8;
constexpr int32_t kC196FormNoInventory = -2;
constexpr int32_t kC196FormNoForm = -3;
constexpr int32_t kC196FormNoRoot = -4;
uint32_t g_c196_form_lines = 0; // game thread

void c196_form_root_line(const uint8_t* pawn, const void* inventory, const void* form, void* root, int32_t& last) {
    const int32_t id = inventory == nullptr ? kC196FormNoInventory
                       : form == nullptr    ? kC196FormNoForm
                       : root == nullptr    ? kC196FormNoRoot
                                            : object_index(root);
    if (id == last || !note_event_cap(g_c196_form_lines, kC196FormLines, "card196 form root")) {
        return;
    }
    last = id;
    if (root == nullptr) {
        log::line("card196: %s form root of %s: %s, inventory %s, form %s (%s)", side(), object_name(pawn).text,
                  inventory == nullptr ? "inventory null" : form == nullptr ? "form null" : "root null",
                  object_name(inventory).text, object_name(form).text, form != nullptr ? class_name(form).text : "-");
        return;
    }
    const int visible = c29_bool(root, kC33Visible);
    const int hidden = c29_bool(root, kC33Hidden);
    log::line("card196: %s form root of %s: form %s (%s), root %s; before bVisible %d bHiddenInGame %d, after bVisible "
              "%d bHiddenInGame %d",
              side(), object_name(pawn).text, object_name(form).text, class_name(form).text, object_name(root).text,
              visible, hidden, visible, hidden);
}

// The remote pawn's arms and its equipped weapon's root stay hidden on this machine (MI); a bug pawn is logged only.
void c33_hide(const uint8_t* pawn) {
    C33Slot* const s = c33_slot(pawn);
    if (s == nullptr || g_set_hidden_in_game == nullptr) {
        return;
    }
    const void* const inventory = c24_inventory(pawn); // card 196: MI weapon or bug form
    const void* const weapon = inventory != nullptr ? g_get_equipped_weapon(inventory) : nullptr;
    void* const root = weapon != nullptr ? read_ptr(weapon, kActorRootComponent) : nullptr;
    if (c196_inventory_at(pawn) == kBugFormsInventory) {
        c196_form_root_line(pawn, inventory, weapon, root, s->form); // no hide, show or visibility write
        return;
    }
    c33_keep_hidden(pawn, read_ptr(pawn, kPawnFirstPersonMesh), "FirstPersonMesh", s->arms);
    c33_keep_hidden(pawn, root, "weapon root", s->weapon);
}

// ---- Card 075: a remote mech keeps its body ----
// The mech's body is its "Mech" StaticMeshComponent. EnterMech hides it on the machine that runs the enter, as the
// pilot's cockpit view; the host runs the enter for a client's mech (card 075), so the host lost the body of the mech
// the client drives. On a remote mech (c28_remote: driven from the other machine) the body is shown again whenever
// it is hidden. Only that component, without propagation: the cockpit arms and weapons hang off Camera, not off it.
constexpr uint8_t kVisibilityNoPropagation = 0;
constexpr uint32_t kC75ShowLines = 16; // per run
uint32_t g_c75_show_lines = 0;         // game thread

void c75_show_body(const uint8_t* mech) {
    const uint8_t* prop = nullptr;
    void* const body = const_cast<void*>(prop_object_field(mech, "Mech", &prop));
    if (body == nullptr || g_set_hidden_in_game == nullptr || c29_bool(body, kC33Hidden) != 1) {
        return;
    }
    g_set_hidden_in_game(body, false, kVisibilityNoPropagation);
    if (note_event_cap(g_c75_show_lines, kC75ShowLines, "card075 show body")) {
        log::line("card075: %s showed body %s of remote mech %s (controller %s); bHiddenInGame now %d", side(),
                  object_name(body).text, object_name(mech).text, object_name(read_ptr(mech, kPawnController)).text,
                  c29_bool(body, kC33Hidden));
    }
}

// ---- Card 213: no card 028 renderer on a bug pawn ----
// The card 028 trooper sprite is the body of an MI pawn, which has first-person arms only. A bug pawn
// (ASTPlayerCharacterBug) shows its own third-person mesh, so the sprite only overlaid a trooper on it, and its card
// 034 refit warned while a form switch left the capsule at half-height 0. One line per bug pawn left out, capped.
constexpr uint32_t kC213Pawns = 8; // bug pawns logged per run; later ones are left out unlogged
int32_t g_c213_logged[kC213Pawns]{}; // object indices, game thread
uint32_t g_c213_count = 0;

void c213_note_bug(const uint8_t* pawn) {
    const int32_t index = object_index(pawn);
    for (uint32_t i = 0; i < g_c213_count && i < kC213Pawns; ++i) { // the cap's note takes the count past kC213Pawns
        if (g_c213_logged[i] == index) {
            return;
        }
    }
    if (!note_event_cap(g_c213_count, kC213Pawns, "card213 bug pawn")) {
        return;
    }
    g_c213_logged[g_c213_count - 1] = index;
    log::line("card213: %s no card 028 renderer on bug pawn %s (index %d, renderer present %d): it shows its own mesh",
              side(), object_name(pawn).text, index, find_component(pawn, g_sprite_render_class) != nullptr ? 1 : 0);
}

bool c28_visit(uint8_t* pawn, const void* world, ULONGLONG now) {
    if (net_mode(pawn) == kNetModeStandalone || !c28_remote(pawn)) {
        return false;
    }
    if (is_a(pawn, g_mech_class())) {
        c75_show_body(pawn); // card 075: no sprite body or c33 hide on a mech, its own body stays shown
        c558_remote_row(pawn); // card 558 candidate 2 diag
        return false;
    }
    if (c196_inventory_at(pawn) == kBugFormsInventory) {
        c213_note_bug(pawn); // card 213: no sprite body; still a remote pawn with a body (playlog's remote list)
        c33_hide(pawn);      // card 196: log only on a bug pawn
        return true;
    }
    if (find_component(pawn, g_sprite_render_class) == nullptr) {
        if (holding_dropship(pawn) == nullptr) {
            c28_try_attach(pawn, world, now);
        }
        return false;
    }
    void* const animator = find_component(pawn, g_variant_animator_class);
    C28Slot* const s = c28_slot(pawn);
    if (animator != nullptr && s != nullptr) {
        c28_drive(pawn, animator, *s, now);
        c34_track_half_height(pawn, *s); // card 034: crouch changes the half-height without a size change
        c34_log_z(pawn, world, *s);      // card 034: world z once per key
    }
    c33_hide(pawn); // card 033: after the sprite attach, on remote pawns only
    return true;
}

// ---- Card 028 test views (UBWCOOP_C28_SHOTS=1) ----
// Game-window shots ("shot", card 025) of the remote body. Each key gets one shot kC28ShotDelayMs after a remote pawn
// first shows it, with the local camera turned toward the pawn. The host, kC28OrbitDelayMs after its card 020 script
// is done, plays kC28Orbit: it moves itself to the front, side and back of the client pawn's yaw (a shot at each),
// then turns in place so the client sees the host's front, side and back. The client times its kC28SeriesMs shots from
// the first step it sees the host's pawn jump more than kC28JumpDistance (the first orbit move). Key shots wait while
// the orbit or the series runs, so turning the camera does not turn the pawn being shot.
enum class C28Act : uint8_t { Move, Shot, Turn };
struct C28Cue {
    ULONGLONG at_ms; // after the orbit start
    C28Act act;
    double angle;    // Move: around the client pawn's yaw; Turn: the host's yaw away from facing the client
    const char* what;
};
constexpr C28Cue kC28Orbit[] = {
    {0, C28Act::Move, 0.0, "to the client's front"},   {1000, C28Act::Shot, 0.0, "orbit front"},
    {2000, C28Act::Move, 90.0, "to the client's side"}, {3000, C28Act::Shot, 0.0, "orbit side"},
    {4000, C28Act::Move, 180.0, "to the client's back"}, {5000, C28Act::Shot, 0.0, "orbit back"},
    {7000, C28Act::Turn, 0.0, "show the host's front"}, {9000, C28Act::Turn, 90.0, "show the host's side"},
    {11000, C28Act::Turn, 180.0, "show the host's back"}, {13000, C28Act::Turn, 0.0, "face the client again"},
};
constexpr uint32_t kC28OrbitCount = sizeof(kC28Orbit) / sizeof(kC28Orbit[0]);
constexpr ULONGLONG kC28SeriesMs[] = {8000, 10000, 12000}; // client: after the jump, mid-way through each host turn
constexpr uint32_t kC28SeriesCount = sizeof(kC28SeriesMs) / sizeof(kC28SeriesMs[0]);
constexpr ULONGLONG kC28OrbitDelayMs = 3000;
constexpr double kC28OrbitDistance = 400.0;
constexpr double kC28JumpDistance = 250.0; // in one kC28StepMs step; sprint covers about 105
constexpr double kC28FrontDeg = 45.0;      // |bearing - yaw| up to this: front; up to kC28SideDeg: side; else back
constexpr double kC28SideDeg = 135.0;
// Host key shots of a moving or firing client pawn: the host first moves kC28OrbitDistance away at this angle around
// the pawn's yaw, so the shot is close up (card 028 run 1: shots from 1100-3700 away showed nothing). Idle is the
// orbit's; a negative angle means no move. Side views only: in run 2 a front (0) shot of Attack01 put the host
// inside the wall the client was facing.
constexpr double kC28ApproachDeg[] = {-1.0, -1.0, 45.0, 120.0, 90.0, -90.0, 90.0, 90.0, 90.0};
static_assert(sizeof(kC28ApproachDeg) / sizeof(kC28ApproachDeg[0]) == static_cast<size_t>(C28Key::Count));

double c28_yaw(const void* pawn) {
    return c28_vec(read_ptr(pawn, kActorRootComponent), "RelativeRotation").y;
}

// Requests a shot of `remote` from the local camera (turned toward it when `face`) and logs which side the viewer
// sees: the bearing from the remote pawn to the viewer against the remote pawn's yaw.
void c28_shot(void* pc, const uint8_t* own, const uint8_t* remote, const C28Slot& s, const char* why, bool face) {
    const Vec3 eye = actor_location(own);
    const Vec3 at = actor_location(remote);
    if (face) {
        c25_face(pc, eye, at);
    }
    request_shot(pc, false);
    const double yaw = c28_yaw(remote);
    const double d = fmod(atan2(eye.y - at.y, eye.x - at.x) * kRadToDeg - yaw + 540.0, 360.0) - 180.0;
    const double a = fabs(d);
    ++g_c28.shots;
    log::line("card028: %s SHOT %u t=%llu (%s) of %s key %ls: the viewer sees its %s (bearing - yaw %.0f, yaw %.0f), "
              "dist %.0f",
              side(), g_c28.shots, static_cast<unsigned long long>(GetTickCount64()), why, object_name(remote).text,
              kC28KeyNames[static_cast<size_t>(s.key)], a <= kC28FrontDeg ? "front" : a <= kC28SideDeg ? "side" : "back",
              d, yaw, vec_dist(eye, at));
}

// Host: teleports itself kC28OrbitDistance from the client pawn at world yaw `deg` around it and faces it.
void c28_place(void* pc, uint8_t* own, const uint8_t* remote, double deg, const char* what) {
    const Vec3 at = actor_location(remote);
    const double r = deg / kRadToDeg;
    const Vec3 to{at.x + cos(r) * kC28OrbitDistance, at.y + sin(r) * kC28OrbitDistance, at.z};
    const bool moved = g_set_actor_location(own, &to, false, nullptr, kTeleportPhysics);
    const Vec3 now = actor_location(own);
    c25_face(pc, now, at);
    log::line("card028: %s%s t=%llu move %s: SetActorLocation (%.0f, %.0f, %.0f) returned %d, host now "
              "(%.0f, %.0f, %.0f), %.0f from the client pawn",
              moved ? "" : "WARN ", side(), static_cast<unsigned long long>(GetTickCount64()), what, to.x, to.y,
              to.z, moved ? 1 : 0, now.x, now.y, now.z, vec_dist(now, at));
}

// One host orbit row: move around the client pawn, shoot it, or turn the host in place.
void c28_cue(void* pc, uint8_t* own, const uint8_t* remote, const C28Slot& s, const C28Cue& c) {
    const Vec3 at = actor_location(remote);
    if (c.act == C28Act::Shot) {
        c28_shot(pc, own, remote, s, c.what, true);
        return;
    }
    if (c.act == C28Act::Turn) {
        const Vec3 eye = actor_location(own);
        const double rot[3] = {0.0, atan2(at.y - eye.y, at.x - eye.x) * kRadToDeg + c.angle, 0.0};
        g_orig_set_control_rotation(pc, rot);
        log::line("card028: %s orbit t=%llu turn: %s (control yaw %.0f)", side(),
                  static_cast<unsigned long long>(GetTickCount64()), c.what, rot[1]);
        return;
    }
    c28_place(pc, own, remote, g_c28.orbit_yaw + c.angle, c.what);
}

// Host: schedules the orbit once its card 020 script is done, then plays the due row.
void c28_host_views(void* pc, uint8_t* own, const uint8_t* remote, const C28Slot& s, ULONGLONG now) {
    if (g_c28.orbit_cue >= kC28OrbitCount) {
        return;
    }
    if (g_c28.orbit_ms == 0) {
        if (strcmp(autoinput::current_action(), "done") == 0) {
            g_c28.orbit_ms = now + kC28OrbitDelayMs;
            log::line("card028: %s orbit around %s starts in %llu ms", side(), object_name(remote).text,
                      static_cast<unsigned long long>(kC28OrbitDelayMs));
        }
        return;
    }
    const C28Cue& c = kC28Orbit[g_c28.orbit_cue];
    if (now < g_c28.orbit_ms + c.at_ms) {
        return;
    }
    if (g_c28.orbit_cue == 0) {
        g_c28.orbit_yaw = c28_yaw(remote);
    }
    ++g_c28.orbit_cue;
    c28_cue(pc, own, remote, s, c);
}

// Client: starts the series at the host pawn's first jump, then takes its due shot.
void c28_client_views(void* pc, const uint8_t* own, const uint8_t* remote, C28Slot& s, ULONGLONG now) {
    const Vec3 at = actor_location(remote);
    const double jump = s.loc_known ? vec_dist(at, s.last_loc) : 0.0;
    s.last_loc = at;
    s.loc_known = true;
    if (g_c28.series_ms == 0 && jump > kC28JumpDistance) {
        g_c28.series_ms = now;
        log::line("card028: %s t=%llu %s jumped %.0f in one step: the host orbit; series of %u shots", side(),
                  static_cast<unsigned long long>(now), object_name(remote).text, jump, kC28SeriesCount);
    }
    if (g_c28.series_ms == 0 || g_c28.series_shot >= kC28SeriesCount ||
        now < g_c28.series_ms + kC28SeriesMs[g_c28.series_shot]) {
        return;
    }
    ++g_c28.series_shot;
    c28_shot(pc, own, remote, s, "host turn", true);
}

bool c28_views_busy() {
    const bool orbit = g_c28.orbit_ms != 0 && g_c28.orbit_cue < kC28OrbitCount;
    const bool series = g_c28.series_ms != 0 && g_c28.series_shot < kC28SeriesCount;
    return orbit || series;
}

// Per step with UBWCOOP_C28_SHOTS: the orbit or series, then a due key shot. Needs the local character on foot.
void c28_views(const void* world, const uint8_t* remote, ULONGLONG now) {
    void* const pc = remote != nullptr ? own_pc(world) : nullptr;
    auto* const own = pc != nullptr ? static_cast<uint8_t*>(read_ptr(pc, kControllerPawn)) : nullptr;
    C28Slot* const s = own != nullptr ? c28_slot(remote) : nullptr;
    if (s == nullptr || !is_a(own, g_player_character_class()) || holding_dropship(own) != nullptr) {
        return;
    }
    if (g_client_session) {
        c28_client_views(pc, own, remote, *s, now);
    } else {
        c28_host_views(pc, own, remote, *s, now);
    }
    if (s->shot_ms == 0 || c28_views_busy()) {
        return;
    }
    const double deg = kC28ApproachDeg[static_cast<size_t>(s->key)];
    if (!g_client_session && !s->approached && deg >= 0.0) {
        s->approached = true;
        c28_place(pc, own, remote, c28_yaw(remote) + deg, "next to the client pawn for its key shot");
    }
    if (now >= s->shot_ms) {
        s->shot_ms = 0;
        s->approached = false;
        c28_shot(pc, own, remote, *s, "first key", true);
    }
}

// Card 092: the reflected offset of `name` on `object`'s class, cached in `at` (-2 when absent, logged once).
int32_t c92_offset(int32_t& at, const void* object, const char* name) {
    if (at == -1 && object != nullptr) {
        const uint8_t* const prop = prop_find_prop(read_ptr(object, kObjectClass), name);
        at = prop != nullptr ? read_i32(prop, kPropertyOffset) : -2;
        log::line("%scard092: %s reflected %s on %s: %s +0x%X", at < 0 ? "WARN " : "", side(), name,
                  class_name(object).text, at < 0 ? "absent" : "at", at < 0 ? 0 : at);
    }
    return at;
}

// Card 092: the own pawn's CharacterMovement MovementMode (EMovementMode: 1 walking, 3 falling), -1 unresolved.
int32_t c92_move_mode(const void* own) {
    const int32_t move_at = c92_offset(g_c92.move_at, own, "CharacterMovement");
    const uint8_t* const move = move_at >= 0 ? static_cast<const uint8_t*>(read_ptr(own, static_cast<size_t>(move_at))) : nullptr;
    const int32_t mode_at = c92_offset(g_c92.mode_at, move, "MovementMode");
    return move != nullptr && mode_at >= 0 ? move[mode_at] : -1;
}

// Card 092: one line per engine frame while the window is open (client, UBWCOOP_C92_LOG=1).
void c92_frame(void* engine, ULONGLONG now) {
    if (!g_c92_log || !g_client_session || g_c92.done || g_c92.remote < 0) {
        return;
    }
    if (g_c92.start_ms != 0 && (now - g_c92.start_ms > kC92WindowMs || g_c92.lines >= kC92Lines)) {
        g_c92.done = true;
        log::line("card092: %s window closed: %u lines over %llu ms", side(), g_c92.lines, now - g_c92.start_ms);
        return;
    }
    const void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    const void* const own = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    const void* const camera = pc != nullptr ? read_ptr(pc, kPcCameraManager) : nullptr;
    const uint8_t* const host = static_cast<const uint8_t*>(c31_weak_get(g_c92.pawn));
    if (own == nullptr || camera == nullptr || host == nullptr) {
        return;
    }
    const void* const renderer = find_component(host, g_sprite_render_class);
    const void* const animator = find_component(host, g_variant_animator_class);
    const int32_t cache = c92_offset(g_c92.cache_at, camera, "CameraCachePrivate");
    const int32_t frame_at = c92_offset(g_c92.frame_at, animator, "ActiveFrameIndex");
    if (renderer == nullptr || cache < 0) {
        return;
    }
    const Vec3 cam = read_vec3(camera, static_cast<size_t>(cache) + kC92PovLocation);
    const Vec3 rot = read_vec3(camera, static_cast<size_t>(cache) + kC92PovRotation);
    const Vec3 own_at = read_vec3(read_ptr(own, kActorRootComponent), kComponentTranslation);
    const Vec3 host_at = read_vec3(read_ptr(host, kActorRootComponent), kComponentTranslation);
    const Vec3 sprite = read_vec3(renderer, kComponentTranslation);
    const C28Slot* const s = c28_slot(host);
    const double dist = std::hypot(host_at.x - cam.x, host_at.y - cam.y);
    if (g_c92.start_ms == 0 && dist > kC92OpenDist) {
        return;
    }
    if (g_c92.start_ms == 0) {
        g_c92.start_ms = now;
        log::line("card092: %s window open on %s: camera (%.1f, %.1f, %.1f), own pawn (%.1f, %.1f, %.1f), host pawn "
                  "(%.1f, %.1f, %.1f), camera to host 2D %.1f",
                  side(), object_name(host).text, cam.x, cam.y, cam.z, own_at.x, own_at.y, own_at.z, host_at.x,
                  host_at.y, host_at.z, dist);
    }
    ++g_c92.lines;
    log::line("card092: t=%llu dt %llu cam z %.2f pitch %.2f yaw %.2f own %.1f %.1f z %.2f mode %d host z %.2f sprite z %.2f rel z %.2f "
              "key %ls frame %d size %.1f x %.1f refit %u sizechg %u",
              now, g_c92.last_ms != 0 ? now - g_c92.last_ms : 0, cam.z, rot.x, rot.y, own_at.x, own_at.y, own_at.z,
              c92_move_mode(own), host_at.z, sprite.z,
              read_vec3(renderer, kRelativeLocation).z, kC28KeyNames[static_cast<size_t>(s != nullptr ? s->key : C28Key::None)],
              frame_at >= 0 && animator != nullptr ? read_i32(animator, static_cast<size_t>(frame_at)) : -1,
              g_c92.size_w, g_c92.size_h, g_c92.refits, g_c92.sizes);
    c92_shot(pc, now);
    g_c92.last_ms = now;
    g_c92.refits = 0;
    g_c92.sizes = 0;
}

// Card 560: the local player is back on its own trooper, and that trooper carries a card 028 body from before (a build
// without the own-trooper rule, or a body attached before the first own-pawn sample). The renderer goes and the
// first-person arms and weapon card 033 hid are shown again. Logged per removal, capped.
constexpr uint32_t kC560DropLines = 16;
// Card 591: only the renderer card 028 attached (its slot's index) goes. A mech's own body is a native sprite renderer
// at +0xC20 (card 075: c28_visit never gives a mech a card 028 body); destroying it left +0xC20 null after the next
// GC, and the abandoned-mech self-destruct's DeathComplete then crashed the host on it.
static void c560_drop_body(uint8_t* pawn) {
    if (is_a(pawn, g_mech_class())) {
        return;
    }
    void* const renderer = find_component(pawn, g_sprite_render_class);
    if (renderer == nullptr || g_scene_destroy_component == nullptr || g_set_hidden_in_game == nullptr) {
        return;
    }
    C28Slot* const s = c28_slot(pawn);
    if (s == nullptr || s->renderer != object_index(renderer)) {
        return; // not a card 028 body: the pawn's own sprite renderer stays
    }
    g_scene_destroy_component(renderer, false);
    void* const arms = read_ptr(pawn, kPawnFirstPersonMesh);
    const void* const inventory = c24_inventory(pawn);
    const void* const weapon = inventory != nullptr ? g_get_equipped_weapon(inventory) : nullptr;
    void* const root = weapon != nullptr ? read_ptr(weapon, kActorRootComponent) : nullptr;
    for (void* const comp : {arms, root}) {
        if (comp != nullptr) {
            g_set_hidden_in_game(comp, false, kVisibilityPropagate);
        }
    }
    s->renderer = -1;
    C33Slot* const h = c33_slot(pawn);
    if (h != nullptr) {
        h->arms = -1;
        h->weapon = -1;
    }
    if (note_event_cap(g_c28.drop_lines, kC560DropLines, "card560 drop body")) {
        log::line("card560: %s own trooper %s: card 028 body %s removed, arms %s and weapon root %s shown", side(),
                  object_name(pawn).text, object_name(renderer).text, object_name(arms).text, object_name(root).text);
    }
}

void c28_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    c92_frame(engine, now);
    if (now < g_c28.next_ms) {
        return;
    }
    g_c28.next_ms = now + kC28StepMs;
    const void* const world = g_get_game_world(engine);
    static void* players[kC23Max]; // game thread only
    // Card 368: the shared player list, not a level walk of its own each kC28StepMs.
    const uint32_t count = c368_actors(g_c368_players, world, g_player_character_class(), players);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    auto* const own = static_cast<uint8_t*>(const_cast<void*>(pc != nullptr ? c23_own_pawn(pc) : nullptr));
    if (own != nullptr) { // card 560: on foot and controlled now; kept while the player drives something else
        g_c28.own_trooper = object_index(own);
        c560_drop_body(own);
    }
    uint8_t* remote = nullptr; // the last remote pawn with a body
    for (uint32_t i = 0; i < count; ++i) {
        if (c28_visit(static_cast<uint8_t*>(players[i]), world, now)) {
            remote = static_cast<uint8_t*>(players[i]);
            pl_remote_seen(remote); // card 155
        }
    }
    if (g_c92_log && remote != nullptr && (g_c92.remote < 0 || g_c92.remote == object_index(remote))) {
        g_c92.remote = object_index(remote); // card 092: the first remote pawn with a body
        c31_weak_set(g_c92.pawn, remote);
    }
    if (g_c28_shots) {
        c28_views(world, remote, now);
    }
}

// Card 028: whether this pawn's sprite shows Attack01. Host: a held relay on that server copy. Client: the host's
// last Fire frame, for a pawn that is neither ours nor a server copy (with two players, the host's pawn).
bool c26_firing(const void* pawn) {
    if (!g_client_session) {
        return c24_find_relay(object_index(pawn), kFirePrimary) >= 0;
    }
    return g_c24.host_fire && sprint_side(pawn) == kSideOther;
}

// c28_pick override for a remote player sprite: Death then Corpse while down, StandUp just after a revive.
C28Key c43_sprite_key(const void* pawn, ULONGLONG now) {
    const int32_t i = g_c43.count != 0 ? c43_find_pawn(pawn) : -1;
    if (i < 0) {
        return C28Key::None;
    }
    const C43Entry& e = g_c43.e[i];
    if (e.state == C43Life::Up) {
        return now - e.up_ms < kC43StandUpMs ? C28Key::StandUp : C28Key::None;
    }
    return now - e.down_ms < kC28DeathMs ? C28Key::Death : C28Key::Corpse;
}

} // namespace ubw::hooks
