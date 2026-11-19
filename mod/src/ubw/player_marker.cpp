// Card 110: a local marker over each other player. Every co-op machine (listen host or client) keeps one UMG
// overlay of its own (the card 031 recipe: WB_TutorialPrompt_C, cleared canvas, one UTextBlock per remote slot) and
// moves each text block every frame to the other player's head, projected with
// APlayerController::ProjectWorldLocationToScreen and divided by UWidgetLayoutLibrary::GetViewportScale (viewport
// pixels to Slate units). Being a screen overlay it shows through walls, always faces the camera, and is never
// replicated, never an objective. Label: the pawn's PlayerState name; "DOWN <n>s" is added while Downed (card 043
// entry, which the client fills from ChanOp::Down), "DOWN" alone while Out. A slot is hidden when its pawn is gone,
// Dead or behind the camera; the overlay is rebuilt on each new world.
//
// The game's own world marker (ASTLevelObjectiveMarker) was not used: CreateASTLevelObjectiveMarker (0x05150E20)
// spawns the settings' ObjectiveMarkerClass at the actor, stores the objective at marker+0x2B0, attaches the marker
// to the actor and calls ObjectiveUpdated (0x051614C0), which reads the objective (vtable +0x2B0, flags +0x68/+0x69)
// to decide visibility, and the label comes from the objective's data. A marker without a USTLevelObjective has no
// label and no visibility source.

#include "ubw/hooks_internal.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

namespace ubw::hooks {

ProjectToScreenFn g_project_to_screen = nullptr;
ViewportScaleFn g_viewport_scale = nullptr;

// ---- Card 416: a minimap blip and a ground ring for each other player ----
// Both local to each machine, never replicated, driven from c110_step over the same c110_others list.
//
// Blip: the player character does not implement ISTMinimapMarkerInterface, so a bare ShowOnMinimap(pawn) gets no
// icon, sits at world zero and is removed by the next USTMinimapComponent::TickComponent. The interface's five static
// Execute_* wrappers (what ShowOnMinimap, GetMinimapLocation, GetMinimapAngle and TickComponent call) are detoured:
// a pawn in the teammate table below is answered by the mod (T_Ally_Arrow, actor location and yaw, remain at the edge,
// shown while not Dead or hidden), every other object goes to the original. c416_step adds the entry with the game's
// own ShowOnMinimap once a second when the PC minimap has none for the pawn, and tints the marker widget; the game's
// tick removes it when ShouldShowOnMinimap turns false.
//
// Ring: a UStaticMeshComponent on the pawn (ControlRig_Circle_3mm scaled to a 60 cm outer radius, else the engine
// cylinder squashed to a 1 cm disc), no collision, no shadow, no decals, a GizmoMaterial dynamic instance with
// GizmoColor set to the teammate colour, 2 cm above the capsule bottom. Visibility is set on a state change only.

C416IconFn g_orig_c416_icon = nullptr;
C416LocationFn g_orig_c416_location = nullptr;
C416YawFn g_orig_c416_yaw = nullptr;
C416BoolFn g_orig_c416_edge = nullptr;
C416BoolFn g_orig_c416_show = nullptr;
C416ShowFn g_c416_show_on_minimap = nullptr;
C416ColorFn g_c416_set_color = nullptr;
C416CreateMidFn g_c416_create_mid = nullptr;
C416VectorParamFn g_c416_set_vector = nullptr;
C416SetMeshFn g_c416_set_mesh = nullptr;

namespace {

constexpr float kC416Colour[4] = {0.0f, 0.85f, 1.0f, 1.0f}; // linear cyan: squad and objective icons are white, gold, red
constexpr ULONGLONG kC416CheckMs = 1000;     // per slot: the minimap entry check and a ring (re)build
constexpr uint32_t kC416Lines = 48;          // per map
constexpr size_t kMinimapMarkers = 0xF8;     // USTMinimapComponent::ActiveMarkers data (ShowOnMinimap+0x102)
constexpr size_t kMinimapMarkerCount = 0x100;
constexpr size_t kMarkerStride = 0x38;       // FSTMinimapMarkerData
constexpr size_t kMarkerSource = 0x20;       // source actor (ShowOnMinimap+0x131)
constexpr size_t kMarkerWidget = 0x28;       // marker widget, set by USTMinimapWidget::AddNewMarker during the broadcast
constexpr int32_t kC416MaxMarkers = 1024;    // loop bound over ActiveMarkers
constexpr double kC416RingRadius = 60.0;     // cm, outer radius
constexpr double kC416RingLift = 2.0;        // cm above the capsule bottom
constexpr double kC416DiscHalf = 0.5;        // fallback disc: 1 cm high
constexpr double kC416Sin45 = 0.70710678118654752;
constexpr uint8_t kC416Movable = 2;          // EComponentMobility::Movable
constexpr size_t kBoundsExtent = 0x18;       // FBoxSphereBounds (doubles): Origin +0, BoxExtent +0x18
constexpr char kC416IconPath[] = "/Game/UI/Fed_HUD/GenericItemExports/T_Ally_Arrow.T_Ally_Arrow";
constexpr char kC416RingPath[] = "/ControlRig/Controls/ControlRig_Circle_3mm.ControlRig_Circle_3mm";
constexpr char kC416DiscPath[] = "/Engine/BasicShapes/Cylinder.Cylinder";
constexpr char kC416MaterialPath[] = "/Engine/EngineMaterials/GizmoMaterial.GizmoMaterial";

struct alignas(16) C416Transform {
    double rotation[4]; // quaternion x, y, z, w
    double translation[4];
    double scale[4];
};

struct C416Slot {
    C31Weak ring;            // the ring component; used only while its owner is the slot's pawn
    ULONGLONG next_check_ms;
    const void* tinted;      // the marker widget last tinted: compared only, never dereferenced
    bool ring_visible;
    bool blip_logged;        // this map
    bool late_logged;        // this map: a tint after the add was logged
};
struct C416State {
    const void* pawn[kC43Max]; // the teammate table: the detours compare object pointers against it
    bool show[kC43Max];        // ShouldShowOnMinimap's answer per slot
    C416Slot s[kC43Max];
    C31Weak icon;
    uint64_t gizmo_color;      // FName cache
    uint32_t lines;            // this map
    bool icon_failed;          // this map: the icon did not load (WARN once), not retried
    bool ring_off;             // this map: no ring mesh or component class, not retried
};
C416State g_c416{};

bool c416_line() {
    return note_event_cap(g_c416.lines, kC416Lines, "card416");
}

// The teammate slot of `object`, or -1: at most kC43Max - 1 pointer compares.
int32_t c416_slot_of(const void* object) {
    if (object == nullptr) {
        return -1;
    }
    for (int32_t s = 1; s < kC43Max; ++s) {
        if (g_c416.pawn[s] == object) {
            return s;
        }
    }
    return -1;
}

void* c416_asset(const char* path, const wchar_t* class_path) {
    void* const cls = find_class(class_path);
    return cls != nullptr ? c74_find_asset(path, cls) : nullptr;
}

// The blip icon, loaded once and held weakly (reloaded when it dies). Null when it cannot be loaded (WARN once a map).
void* c416_icon() {
    void* icon = g_weak_get(&g_c416.icon);
    if (icon != nullptr || g_c416.icon_failed) {
        return icon;
    }
    icon = c416_asset(kC416IconPath, L"/Script/Engine.Texture2D");
    if (icon != nullptr) {
        c31_weak_set(g_c416.icon, icon);
    } else {
        g_c416.icon_failed = true;
        log::line("WARN card416: %s blip icon %s not loaded: the blip has no icon (not retried this map)", side(),
                  kC416IconPath);
    }
    return icon;
}

// The minimap entry whose source is `pawn`, or null.
const uint8_t* c416_marker_of(const void* minimap, const void* pawn) {
    const auto* const data = static_cast<const uint8_t*>(read_ptr(minimap, kMinimapMarkers));
    const int32_t n = read_i32(minimap, kMinimapMarkerCount);
    for (int32_t i = 0; data != nullptr && i < n && i < kC416MaxMarkers; ++i) {
        const uint8_t* const e = data + static_cast<size_t>(i) * kMarkerStride;
        if (read_ptr(e, kMarkerSource) == pawn) {
            return e;
        }
    }
    return nullptr;
}

// Tints the entry's marker widget when it is there and not the one tinted last. True when it tinted.
bool c416_tint(C416Slot& k, const uint8_t* entry) {
    void* const widget = read_ptr(entry, kMarkerWidget);
    if (widget == nullptr || widget == k.tinted) {
        return false;
    }
    float colour[4] = {kC416Colour[0], kC416Colour[1], kC416Colour[2], kC416Colour[3]}; // by value: a caller copy
    g_c416_set_color(widget, colour);
    k.tinted = widget;
    return true;
}

// Adds slot `s`'s pawn to the local PC's minimap when it has no entry and tints its marker widget; an entry whose
// widget was made after the add (or replaced) is tinted at a later check.
void c416_blip(void* pc, int32_t s, const void* pawn) {
    void* const minimap = is_a(pc, g_st_pc_class()) ? read_ptr(pc, kPcMinimap) : nullptr;
    if (minimap == nullptr || read_ptr(minimap, kMinimapOrigin) == nullptr) {
        return; // card 120: no origin yet
    }
    C416Slot& k = g_c416.s[s];
    const uint8_t* const present = c416_marker_of(minimap, pawn);
    if (present != nullptr) {
        if (c416_tint(k, present) && !k.late_logged) {
            k.late_logged = true;
            log::line("card416: %s blip slot %d widget %s tinted late", side(), s,
                      object_name(read_ptr(present, kMarkerWidget)).text);
        }
        return;
    }
    const void* const icon = c416_icon();
    const int32_t before = read_i32(minimap, kMinimapMarkerCount);
    g_c416_show_on_minimap(minimap, pawn);
    const uint8_t* const entry = c416_marker_of(minimap, pawn);
    void* const widget = entry != nullptr ? read_ptr(entry, kMarkerWidget) : nullptr;
    if (entry != nullptr) {
        c416_tint(k, entry);
    }
    if (entry == nullptr) {
        if (c416_line()) {
            log::line("WARN card416: %s blip slot %d missing after ShowOnMinimap(%s) (markers %d -> %d)", side(), s,
                      object_name(pawn).text, before, read_i32(minimap, kMinimapMarkerCount));
        }
        return;
    }
    if (!k.blip_logged || c416_line()) {
        log::line("card416: %s blip slot %d added (icon %s, widget %s%s, markers %d -> %d, pawn %s)", side(), s,
                  object_name(icon).text, widget != nullptr ? object_name(widget).text : "none",
                  widget != nullptr ? " tinted" : "", before, read_i32(minimap, kMinimapMarkerCount),
                  object_name(pawn).text);
    }
    k.blip_logged = true;
}

// Sets a reflected bool of `object`. False when the property is absent.
bool c416_set_flag(void* object, const char* name, bool on) {
    const uint8_t* const prop = prop_find_prop(read_ptr(object, kObjectClass), name);
    if (prop == nullptr) {
        return false;
    }
    uint8_t* const at = static_cast<uint8_t*>(object) + read_i32(prop, kPropertyOffset) + prop[kBoolByteOffset];
    const uint8_t mask = prop[kBoolFieldMask];
    *at = on ? static_cast<uint8_t>(*at | mask) : static_cast<uint8_t>(*at & ~mask);
    return true;
}

// Sets a reflected byte (enum) of `object`. False when the property is absent.
bool c416_set_byte(void* object, const char* name, uint8_t value) {
    const uint8_t* const prop = prop_find_prop(read_ptr(object, kObjectClass), name);
    if (prop == nullptr) {
        return false;
    }
    static_cast<uint8_t*>(object)[read_i32(prop, kPropertyOffset)] = value;
    return true;
}

// The ring mesh (ControlRig circle, else the engine cylinder as a disc) and its bounds extent. Null when neither
// loads with a usable extent.
void* c416_mesh(Vec3& extent, bool& disc) {
    const char* const paths[2] = {kC416RingPath, kC416DiscPath};
    for (int32_t i = 0; i < 2; ++i) {
        void* const mesh = c416_asset(paths[i], L"/Script/Engine.StaticMesh");
        const uint8_t* const prop = mesh != nullptr ? prop_find_prop(read_ptr(mesh, kObjectClass), "ExtendedBounds") : nullptr;
        if (prop == nullptr) {
            log::line("WARN card416: %s ring mesh %s %s", side(), paths[i], mesh == nullptr ? "not loaded" : "has no ExtendedBounds");
            continue;
        }
        extent = read_vec3(mesh, static_cast<size_t>(read_i32(prop, kPropertyOffset)) + kBoundsExtent);
        // The ring needs two in-plane extents (it may be flat, one extent 0); the disc divides by all three.
        const int32_t positive = (extent.x > 0.0) + (extent.y > 0.0) + (extent.z > 0.0);
        if (positive >= (i == 0 ? 2 : 3)) {
            disc = i == 1;
            return mesh;
        }
        log::line("WARN card416: %s ring mesh %s extent (%.3f, %.3f, %.3f) unusable", side(), paths[i], extent.x,
                  extent.y, extent.z);
    }
    return nullptr;
}

// The ring's place on the pawn: the mesh's thinnest axis turned to world Z, scaled so the outer radius is
// kC416RingRadius (the disc also squashed to 1 cm), kC416RingLift above the capsule bottom.
C416Transform c416_ring_transform(const Vec3& e, bool disc, double half_height, int32_t& normal) {
    const double ext[3] = {e.x, e.y, e.z};
    normal = 2;
    for (int32_t a = 0; a < 2; ++a) {
        normal = ext[a] < ext[normal] ? a : normal;
    }
    const double r0 = ext[normal == 0 ? 1 : 0];
    const double r1 = ext[normal == 2 ? 1 : 2];
    const double u = kC416RingRadius / (r0 > r1 ? r0 : r1);
    C416Transform t = {{0, 0, 0, 1}, {0, 0, kC416RingLift - half_height, 0}, {u, u, u, 0}};
    if (disc) {
        t.scale[normal] = kC416DiscHalf / ext[normal];
    }
    if (normal == 0) { // about Y: mesh X to world Z
        t.rotation[1] = kC416Sin45;
        t.rotation[3] = kC416Sin45;
    } else if (normal == 1) { // about X: mesh Y to world Z
        t.rotation[0] = kC416Sin45;
        t.rotation[3] = kC416Sin45;
    }
    return t;
}

// Adds the ring to `pawn`; null (logged) when it cannot.
void* c416_ring_build(void* pawn, int32_t s) {
    void* const cls = find_class(L"/Script/Engine.StaticMeshComponent");
    Vec3 extent{};
    bool disc = false;
    void* const mesh = cls != nullptr ? c416_mesh(extent, disc) : nullptr;
    if (mesh == nullptr) {
        g_c416.ring_off = true;
        log::line("WARN card416: %s no ring this map (component class %s, mesh none)", side(), cls != nullptr ? "found" : "MISSING");
        return nullptr;
    }
    const void* const root = read_ptr(pawn, kActorRootComponent);
    const uint8_t* const hh_prop = root != nullptr ? prop_find_prop(read_ptr(root, kObjectClass), "CapsuleHalfHeight") : nullptr;
    const double hh = hh_prop != nullptr ? read_f32(root, static_cast<size_t>(read_i32(hh_prop, kPropertyOffset))) : 0.0;
    int32_t normal = 2;
    const C416Transform t = c416_ring_transform(extent, disc, hh, normal);
    void* holder = cls;
    void* const ring = g_add_component_by_class(pawn, &holder, false, &t, true);
    if (ring == nullptr) {
        if (c416_line()) {
            log::line("WARN card416: %s ring slot %d: AddComponentByClass on %s returned null", side(), s, object_name(pawn).text);
        }
        return nullptr;
    }
    g_orig_set_collision_enabled(ring, kC28NoCollision);
    const bool flags = c416_set_flag(ring, "CastShadow", false) && c416_set_flag(ring, "bReceivesDecals", false) &&
                       c416_set_flag(ring, "bGenerateOverlapEvents", false) && c416_set_byte(ring, "Mobility", kC416Movable);
    const bool mesh_set = g_c416_set_mesh(ring, mesh);
    void* const material = c416_asset(kC416MaterialPath, L"/Script/Engine.MaterialInterface");
    void* const mid = material != nullptr ? g_c416_create_mid(ring, 0, material, 0) : nullptr;
    if (mid != nullptr) {
        float colour[4] = {kC416Colour[0], kC416Colour[1], kC416Colour[2], kC416Colour[3]}; // by value: a caller copy
        g_c416_set_vector(mid, fname_cached(g_c416.gizmo_color, L"GizmoColor"), colour);
    }
    g_finish_add_component(pawn, ring, false, &t);
    log::line("%scard416: %s ring slot %d on %s: %s mesh %s (extent %.2f, %.2f, %.2f, normal axis %d), scale %.3f, "
              "relative z %.1f (half height %.1f), flags %s, SetStaticMesh %d, material %s, instance %s, GizmoColor %s",
              mid == nullptr || !flags || hh <= 0.0 ? "WARN " : "", side(), s, object_name(pawn).text, object_name(ring).text,
              object_name(mesh).text, extent.x, extent.y, extent.z, normal, t.scale[normal == 2 ? 0 : 2],
              t.translation[2], hh, flags ? "set" : "NOT ALL SET", mesh_set ? 1 : 0, object_name(material).text,
              object_name(mid).text, mid != nullptr ? "set" : "not set");
    return ring;
}

// Slot `s`'s ring: built at the check cadence when missing, visibility set only when `show` changes.
void c416_ring_step(C416Slot& k, int32_t s, void* pawn, bool show, bool check) {
    void* ring = g_weak_get(&k.ring);
    // A different pawn in the slot drops the ring from tracking; no pawn (a client rescan gap) only hides it, so the
    // same pawn coming back reuses its ring instead of getting a second one.
    if (ring != nullptr && pawn != nullptr && read_ptr(ring, kComponentOwner) != pawn) {
        if (k.ring_visible) {
            g_scene_set_visibility(ring, false, 0); // a pawn no longer in this slot keeps no ring shown
        }
        c31_weak_set(k.ring, nullptr);
        ring = nullptr;
    }
    if (ring == nullptr && show && check && !g_c416.ring_off) {
        ring = c416_ring_build(pawn, s);
        c31_weak_set(k.ring, ring);
        k.ring_visible = true; // a new component is visible
    }
    if (ring != nullptr && show != k.ring_visible) {
        g_scene_set_visibility(ring, show, 0);
        k.ring_visible = show;
        if (c416_line()) {
            log::line("card416: %s ring slot %d %s", side(), s, show ? "shown" : "hidden");
        }
    }
}

// Per frame: refreshes the teammate table the detours read, then per slot the ring and (once a second) the blip.
void c416_step(void* pc, void* const others[kC43Max], ULONGLONG now) {
    for (int32_t s = 1; s < kC43Max; ++s) {
        void* const pawn = others[s];
        const int32_t i = pawn != nullptr ? c43_find_pawn(pawn) : -1;
        const bool hidden = pawn != nullptr && (static_cast<const uint8_t*>(pawn)[kActorHiddenByte] & kActorHiddenBit) != 0;
        const bool show = pawn != nullptr && !hidden && (i < 0 || g_c43.e[i].state != C43Life::Dead);
        g_c416.pawn[s] = pawn;
        g_c416.show[s] = show;
        C416Slot& k = g_c416.s[s];
        const bool check = now >= k.next_check_ms;
        if (check) {
            k.next_check_ms = now + kC416CheckMs;
        }
        c416_ring_step(k, s, pawn, show, check);
        if (show && check) {
            c416_blip(pc, s, pawn);
        }
    }
}

// A new world: the table, the per-map logs and flags start over. Rings stay tracked (their owner is checked), so a
// teammate pawn kept through seamless travel keeps its one ring.
void c416_new_world() {
    for (int32_t s = 0; s < kC43Max; ++s) {
        g_c416.pawn[s] = nullptr;
        g_c416.show[s] = false;
        g_c416.s[s].next_check_ms = 0;
        g_c416.s[s].blip_logged = false;
        g_c416.s[s].late_logged = false;
        g_c416.s[s].tinted = nullptr;
    }
    g_c416.lines = 0;
    g_c416.icon_failed = false;
    g_c416.ring_off = false;
}

} // namespace

void* detour_c416_icon(const void* object) {
    return c416_slot_of(object) < 0 ? g_orig_c416_icon(object) : g_weak_get(&g_c416.icon);
}

Vec3* detour_c416_location(Vec3* out, const void* object) {
    if (c416_slot_of(object) < 0) {
        return g_orig_c416_location(out, object);
    }
    *out = actor_location(object);
    return out;
}

float detour_c416_yaw(const void* object) {
    const void* const root = c416_slot_of(object) >= 0 ? read_ptr(object, kActorRootComponent) : nullptr;
    return root == nullptr ? g_orig_c416_yaw(object) : static_cast<float>(component_pitch_yaw(root).y);
}

bool detour_c416_edge(const void* object) {
    return c416_slot_of(object) >= 0 || g_orig_c416_edge(object);
}

bool detour_c416_show(const void* object) {
    const int32_t s = c416_slot_of(object);
    return s < 0 ? g_orig_c416_show(object) : g_c416.show[s];
}

namespace {

constexpr int32_t kC110ZOrder = 90;           // under the card 031 / 052 panels (100)
constexpr double kC110HeadZ = 110.0;          // cm above the actor location (the capsule centre)
constexpr ULONGLONG kC110ScanMs = 1000;       // client: how often the other player pawns are searched for
constexpr ULONGLONG kC110PosLogMs = 5000;     // per-slot position line
constexpr uint32_t kC110Lines = 80;
constexpr size_t kC110LabelChars = 64;
constexpr int32_t kC110NameChars = 32;
constexpr size_t kC110PlayerStateName = 0x340; // APlayerState::PlayerNamePrivate (card 024)
constexpr uint8_t kC110VisCollapsed = 1;
constexpr uint8_t kC110VisHitTestInvisible = 3;
constexpr uint8_t kC110VisSelfHitTestInvisible = 4;

struct C110Slot {
    C31Weak text;                     // the slot's UTextBlock
    C31Weak canvas_slot;              // its UCanvasPanelSlot
    wchar_t shown[kC110LabelChars];   // the label last set
    bool visible;
    ULONGLONG pos_log_ms;
};
struct C110State {
    C110Slot s[kC43Max];
    C31Weak widget;
    C31Weak others[kC43Max]; // client (card 209): every other player pawn, cached between scans
    uintptr_t world_id;     // compared only, never dereferenced
    ULONGLONG next_scan_ms;
    int32_t ps_offset;      // APawn::PlayerState (reflected once); -1 = not found yet
    uint32_t lines;
    bool built;
    bool failed;            // the build failed in this world: not retried until the next one
};
C110State g_c110{};

bool c110_line() {
    return note_event_cap(g_c110.lines, kC110Lines, "card110");
}

// The other player pawns by slot: host, the roster's player characters past slot 0 (its own); client (card 209),
// every player pawn of side "other" in slots 1 on, rescanned each kC110ScanMs so a late joiner gets a tag.
void c110_others(const void* world, ULONGLONG now, void* out[kC43Max]) {
    if (!g_client_session) {
        const C43Roster r = c43_roster(world);
        for (int32_t s = 1; s < kC43Max; ++s) {
            out[s] = r.p[s].pawn != nullptr && is_a(r.p[s].pawn, g_player_character_class()) ? r.p[s].pawn : nullptr;
        }
        return;
    }
    if (now >= g_c110.next_scan_ms) {
        g_c110.next_scan_ms = now + kC110ScanMs;
        void* found[kC43Max];
        c43_client_other_pawns(world, found);
        for (int32_t i = 0; i < kC43Max; ++i) {
            c31_weak_set(g_c110.others[i], found[i]);
        }
    }
    for (int32_t s = 1; s < kC43Max; ++s) {
        out[s] = g_weak_get(&g_c110.others[s - 1]);
    }
}

// The pawn's PlayerState name (APlayerState::PlayerNamePrivate, an FString) as wide text, or L"Player".
void c110_name(const void* pawn, wchar_t* out, size_t cap) {
    if (g_c110.ps_offset < 0) {
        const uint8_t* const prop = prop_find_prop(read_ptr(pawn, kObjectClass), "PlayerState");
        g_c110.ps_offset = prop != nullptr ? read_i32(prop, kPropertyOffset) : -1;
        log::line("card110: APawn::PlayerState offset %s0x%X", prop != nullptr ? "" : "NOT FOUND ",
                  static_cast<unsigned>(g_c110.ps_offset));
    }
    const void* const ps = g_c110.ps_offset >= 0 ? read_ptr(pawn, static_cast<size_t>(g_c110.ps_offset)) : nullptr;
    const auto* const text = ps != nullptr ? static_cast<const wchar_t*>(read_ptr(ps, kC110PlayerStateName)) : nullptr;
    const int32_t num = ps != nullptr ? read_i32(ps, kC110PlayerStateName + kArrayNum) : 0;
    int32_t n = 0;
    for (; text != nullptr && n < num - 1 && n < kC110NameChars && static_cast<size_t>(n) + 1 < cap; ++n) {
        out[n] = text[n] >= 0x20 ? text[n] : L'?';
    }
    out[n] = 0;
    if (n == 0) {
        swprintf(out, cap, L"Player");
    }
}

// The label: name, plus DOWN (and the seconds left while Downed). Empty when the pawn is Dead.
void c110_label(const void* pawn, wchar_t* out, size_t cap) {
    const int32_t i = c43_find_pawn(pawn);
    const C43Life life = i >= 0 ? g_c43.e[i].state : C43Life::Up;
    if (life == C43Life::Dead) {
        out[0] = 0;
        return;
    }
    wchar_t name[kC110NameChars + 1] = {};
    c110_name(pawn, name, kC110NameChars + 1);
    if (life == C43Life::Downed) {
        swprintf(out, cap, L"%ls\nDOWN %ds", name, static_cast<int>(g_c43.e[i].secs_left + 0.5f));
    } else if (life == C43Life::Out) {
        swprintf(out, cap, L"%ls\nDOWN", name);
    } else {
        swprintf(out, cap, L"%ls", name);
    }
}

// Writes LayoutData.Alignment (FVector2D) of a canvas slot before Slate builds it: the text's bottom centre sits on
// the projected point. Offsets come from reflection (FAnchorData's own "Alignment" member).
void c110_align(void* canvas_slot) {
    const uint8_t* const layout = prop_find_prop(read_ptr(canvas_slot, kObjectClass), "LayoutData");
    const void* const anchor_struct = layout != nullptr ? read_ptr(layout, kStructPropStruct) : nullptr;
    const uint8_t* const align = anchor_struct != nullptr ? prop_find_prop(anchor_struct, "Alignment") : nullptr;
    if (align == nullptr) {
        log::line("ERROR card110: no LayoutData.Alignment on %s -- labels left-aligned", class_name(canvas_slot).text);
        return;
    }
    auto* const at = static_cast<uint8_t*>(canvas_slot) + read_i32(layout, kPropertyOffset) + read_i32(align, kPropertyOffset);
    double was[2] = {};
    memcpy(was, at, sizeof(was));
    const double want[2] = {0.5, 1.0};
    memcpy(at, want, sizeof(want));
    if (c110_line()) {
        log::line("card110: slot %s Alignment at +0x%X was (%.2f, %.2f), now (0.50, 1.00)", object_name(canvas_slot).text,
                  static_cast<unsigned>(at - static_cast<uint8_t*>(canvas_slot)), was[0], was[1]);
    }
}

// Builds the overlay for `pc`: one collapsed text block per remote slot. True when built.
bool c110_build(void* pc) {
    void* const cls = c31_find_widget_class();
    void* const widget = cls != nullptr ? g_create_widget_instance(pc, &cls, 0) : nullptr;
    void* const tree = widget != nullptr ? c31_object_prop(widget, "WidgetTree") : nullptr;
    void* const canvas = tree != nullptr ? c31_root_canvas(tree) : nullptr;
    if (canvas == nullptr) {
        log::line("ERROR card110: overlay not built (class %p, widget %p, tree %p)", cls, widget, tree);
        return false;
    }
    for (int32_t s = 1; s < kC43Max; ++s) {
        void* const text = c31_construct(g_text_block_class(), tree);
        void* const canvas_slot = text != nullptr ? g_panel_add_child(canvas, text) : nullptr;
        if (canvas_slot == nullptr) {
            log::line("ERROR card110: text block for slot %d not added (text %p)", s, text);
            return false;
        }
        g_slot_set_auto_size(canvas_slot, true);
        c110_align(canvas_slot);
        g_widget_set_visibility(text, kC110VisCollapsed);
        c31_weak_set(g_c110.s[s].text, text);
        c31_weak_set(g_c110.s[s].canvas_slot, canvas_slot);
    }
    g_add_to_viewport(widget, kC110ZOrder);
    g_widget_set_visibility(widget, kC110VisSelfHitTestInvisible);
    c31_weak_set(g_c110.widget, widget);
    log::line("card110: %s overlay %s built for pc %p (%d slots, z %d)", side(), object_name(widget).text, pc,
              kC43Max - 1, kC110ZOrder);
    return true;
}

void c110_hide(int32_t s, const char* why) {
    C110Slot& k = g_c110.s[s];
    void* const text = g_weak_get(&k.text);
    if (!k.visible || text == nullptr) {
        return;
    }
    g_widget_set_visibility(text, kC110VisCollapsed);
    k.visible = false;
    if (c110_line()) {
        log::line("card110: %s slot %d hidden (%s)", side(), s, why);
    }
}

// Positions slot `s` over `pawn` and sets its label; hides it when the pawn is Dead or off the view.
void c110_show(void* pc, int32_t s, const void* pawn, ULONGLONG now) {
    C110Slot& k = g_c110.s[s];
    void* const text = g_weak_get(&k.text);
    void* const canvas_slot = g_weak_get(&k.canvas_slot);
    if (text == nullptr || canvas_slot == nullptr) {
        return;
    }
    if (cine_stack_open()) {
        c110_hide(s, "screen up"); // card 225: a screen, movie or menu on the game's widget stack
        return;
    }
    if ((static_cast<const uint8_t*>(pawn)[kActorHiddenByte] & kActorHiddenBit) != 0) {
        c110_hide(s, "pawn hidden"); // card 399: a rider inside the dropship
        return;
    }
    wchar_t label[kC110LabelChars] = {};
    c110_label(pawn, label, kC110LabelChars);
    if (label[0] == 0) {
        c110_hide(s, "Dead");
        return;
    }
    Vec3 head = actor_location(pawn);
    head.z += kC110HeadZ;
    double screen[2] = {};
    const double world[3] = {head.x, head.y, head.z};
    if (!g_project_to_screen(pc, world, screen, true)) {
        c110_hide(s, "behind the camera");
        return;
    }
    const float scale = g_viewport_scale(pc);
    const double pos[2] = {screen[0] / (scale > 0.0f ? scale : 1.0f), screen[1] / (scale > 0.0f ? scale : 1.0f)};
    g_slot_set_position(canvas_slot, pos);
    if (wcscmp(label, k.shown) != 0) {
        c31_set_text(text, label);
        memcpy(k.shown, label, sizeof(k.shown)); // same size, NUL-terminated by swprintf
        if (c110_line()) {
            log::line("card110: %s slot %d label '%ls' (pawn %s)", side(), s, label, object_name(pawn).text);
        }
    }
    if (!k.visible) {
        g_widget_set_visibility(text, kC110VisHitTestInvisible);
        k.visible = true;
        if (c110_line()) {
            log::line("card110: %s slot %d shown over %s", side(), s, object_name(pawn).text);
        }
    }
    if (now >= k.pos_log_ms && c110_line()) {
        k.pos_log_ms = now + kC110PosLogMs;
        log::line("card110: %s slot %d at (%.0f, %.0f) scale %.2f, head (%.0f, %.0f, %.0f)", side(), s, pos[0], pos[1],
                  scale, head.x, head.y, head.z);
    }
}

// A new world: drops the old overlay (removed if it is still alive) and every cached pointer.
void c110_new_world(uintptr_t world_id) {
    void* const old = g_weak_get(&g_c110.widget);
    if (old != nullptr) {
        g_widget_remove_from_parent(old);
    }
    const uint32_t lines = g_c110.lines;
    const int32_t ps_offset = g_c110.ps_offset;
    g_c110 = C110State{};
    g_c110.lines = lines;
    g_c110.ps_offset = ps_offset;
    g_c110.world_id = world_id;
    c416_new_world();
}

} // namespace

void c110_step(void* engine) {
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr; // card 346: not g_world_first_pc
    if (pc == nullptr || g_project_to_screen == nullptr) {
        return;
    }
    // A new world, seen in every net mode: a world address can be reused by a later map (the standalone menu world in
    // between must count as a change). A built overlay whose widget is gone (its world was torn down) is rebuilt too.
    if (reinterpret_cast<uintptr_t>(world) != g_c110.world_id ||
        (g_c110.built && g_weak_get(&g_c110.widget) == nullptr)) {
        c110_new_world(reinterpret_cast<uintptr_t>(world));
    }
    if (net_mode(pc) == kNetModeStandalone) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    void* others[kC43Max] = {};
    c110_others(world, now, others);
    c416_step(pc, others, now); // card 416: the blips and rings, before the overlay's gate
    // Card 414: the overlay is built only once a plate could show. A teammate kept through seamless travel is hidden
    // or behind a screen in the transition world, and a build there cost one 9 to 11 ms frame per map change for an
    // overlay the next world drops.
    bool any = false;
    for (int32_t s = 1; s < kC43Max; ++s) {
        const auto* const other = static_cast<const uint8_t*>(others[s]);
        any = any || (other != nullptr && (g_c110.built || (other[kActorHiddenByte] & kActorHiddenBit) == 0));
    }
    if (!g_c110.built && (!any || g_c110.failed || cine_stack_open())) {
        return;
    }
    if (!g_c110.built) {
        g_c110.built = c110_build(pc);
        g_c110.failed = !g_c110.built;
        if (!g_c110.built) {
            return;
        }
    }
    for (int32_t s = 1; s < kC43Max; ++s) {
        if (others[s] == nullptr) {
            c110_hide(s, "no pawn");
            continue;
        }
        c110_show(pc, s, others[s], now);
    }
}

} // namespace ubw::hooks
