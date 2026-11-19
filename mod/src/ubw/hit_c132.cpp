// Card 132: hit blood and wounds on the client.
// The host runs ASTNonPlayableCharacterBase::HandleDamageEvent only on the server, and that is the one caller of
// USTGoreComponent::OnDamage, which adds the wound decal (UADWoundWorldSubsystem::AddWound), the blood burst
// (SpawnGoreResponse) and bone damage (DamageBone). So a client saw blood only at deaths (the Gib relay), never at hits.
// The host hooks OnDamage and, after the original, queues each hit: the NPC's NetGUID, impact point and normal,
// damage, the wound type value, and the two bone names. Once per tick (c132_hit_step) the queue goes out as Hit text
// frames. The client rebuilds a damage event from each hit and calls its own OnDamage on the NPC copy's gore component,
// which applies the client's own gore filters and thresholds; nothing on the client changes health or game logic.
//
// FADDamageEvent fields OnDamage (exe+0x519BF60) reads, and the client sets:
//   +0x060 FHitResult (0x100): passed to AddWound, which copies it; ImpactPoint +0x88, ImpactNormal +0xB8 are its
//          +0x28 and +0x58; the hit actor weak pointer +0xB8, component weak pointer +0xD8, BoneName +0xE0
//   +0x150 FName bone, passed to FindBoneToDamage and DamageBone
//   +0x184 float damage, compared to GetMaxHealth times the gore data thresholds
//   +0x190 TMap<FGameplayTag, float>, looked up with Aloft::Tag_Abilities_WoundType; the value goes to AddWound
// TMap (TSet over TSparseArray) at +0x190: element data +0x00, Num +0x08, Max +0x0C, allocation bits inline +0x10 or
// heap +0x20, NumBits +0x28, MaxBits +0x2C, FirstFreeIndex +0x30, NumFreeIndices +0x34, hash inline +0x38, hash heap
// +0x40, HashSize +0x48. An element is 20 bytes: the tag FName +0x0, the float +0x8, HashNextId +0xC, HashIndex +0x10.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

GoreDamageFn g_orig_c132_gore_damage = nullptr;

uint64_t c41_guid(const void* actor);             // enemies.cpp
void* c41_resolve_guid(void* pc, uint64_t guid);  // enemies.cpp

namespace {

constexpr size_t kEventHit = 0x60;
constexpr size_t kEventBone = 0x150;
constexpr size_t kEventDamage = 0x184;
constexpr size_t kEventTags = 0x190;
constexpr size_t kEventSize = 0x400; // past the last field OnDamage reads (+0x1D8)
constexpr size_t kHitFaceIndex = 0x0;
constexpr size_t kHitTime = 0x4;
constexpr size_t kHitLocation = 0x10;
constexpr size_t kHitImpactPoint = 0x28;
constexpr size_t kHitNormal = 0x40;
constexpr size_t kHitImpactNormal = 0x58;
constexpr size_t kHitItem = 0xA4;
constexpr size_t kHitMyItem = 0xA8;
constexpr size_t kHitFlags = 0xAD; // bit 0 bBlockingHit
constexpr size_t kHitActor = 0xB8;
constexpr size_t kHitBoneName = 0xE0;
constexpr size_t kHitCompTranslation = 0x200; // USceneComponent ComponentToWorld (+0x1E0) translation
constexpr size_t kMapNum = 0x08;
constexpr size_t kMapMax = 0x0C;
constexpr size_t kMapBitsInline = 0x10;
constexpr size_t kMapBitsHeap = 0x20;
constexpr size_t kMapNumBits = 0x28;
constexpr size_t kMapMaxBits = 0x2C;
constexpr size_t kMapFirstFree = 0x30;
constexpr size_t kMapNumFree = 0x34;
constexpr size_t kMapHashInline = 0x38;
constexpr size_t kMapHashSize = 0x48;
constexpr size_t kElemStride = 20;
constexpr size_t kElemValue = 0x8;
constexpr size_t kElemHashNext = 0xC;
constexpr int32_t kMapScan = 64;          // tag map elements read on the host
constexpr size_t kGoreHealth = 0xD8;      // UADHealthComponent*, OnDamage's IsOverkilled check
constexpr size_t kGoreFilterSize = 0xC8;  // the filter delegate OnDamage executes first (nonzero = bound)
constexpr size_t kCharacterMesh = 0x328;  // ACharacter::Mesh
constexpr int32_t kHitMax = 96;           // hits queued per tick; more are counted as dropped
constexpr int32_t kHitBoneMax = 64;       // bone name chars
constexpr size_t kHitText = 200;          // one hit's text
constexpr uint32_t kHitLines = 40;        // per side per map
constexpr double kHitFar = 1000.0;        // uu: a host impact point this far from the client's NPC means a stale position
constexpr uint32_t kHitFarLines = 20;     // per map
constexpr uint32_t kHitBadNameLines = 10; // card 238: per map
// Card 140: the host's NPC location at the hit against the client copy's ReplicatedMovement.Location at apply. At
// 740 uu/s a 300 ms send gap is about 220 uu, so more than this means the copy's replication lags.
constexpr double kHitDrift = 300.0;
constexpr int32_t kHitLateMs = 300;     // card 223: a frame this old was applied late; its drift is not the channel's
constexpr int32_t kHitAgeMaxMs = 60000; // card 223: frame ages above this read as unknown

struct HitRow {
    uint64_t guid;
    Vec3 point, normal;
    float damage, wound;
    Vec3 host_at; // card 140: the host's actor location at the hit
    char bone[kHitBoneMax], hit_bone[kHitBoneMax];
};

struct HitRelay {
    HitRow rows[kHitMax];
    int32_t count;
    uint32_t queued, dropped, no_guid, frames, sent;      // host
    uint32_t received, applied, unmapped, no_gore, bad;   // client
    uint32_t far_hits;  // client: hits whose host location is kHitDrift or more from the copy's replicated location
    double drift_max;   // client: the largest host location to replicated location distance
    uint32_t no_point;  // both sides: rows with no hit point (impact (0, 0, 0), bone None), skipped
    uint32_t lines, far_lines;
    uint32_t bad_names, bad_name_lines; // card 238, host: events with a bone FName outside the name pool
    uint32_t moved, moved_lines;        // card 474, client: impact points moved onto the copy
    uint64_t wound_tag; // the wound tag FName read from this process's Aloft::Tag_Abilities_WoundType
};

HitRelay g_hit{}; // game thread

uint64_t hit_wound_tag() {
    if (g_hit.wound_tag == 0 && g_exe_base != nullptr) {
        memcpy(&g_hit.wound_tag, g_exe_base + offsets::Aloft_Tag_Abilities_WoundType.rva, sizeof(g_hit.wound_tag));
    }
    return g_hit.wound_tag;
}

// Host: the event's wound type value, 0 when the map has none. Reads only allocated elements (the allocation bits).
float hit_wound_value(const uint8_t* event) {
    const uint8_t* const map = event + kEventTags;
    const auto* const data = static_cast<const uint8_t*>(read_ptr(map, 0));
    const int32_t max = read_i32(map, kMapNum);
    const void* const heap = read_ptr(map, kMapBitsHeap);
    const auto* const bits = static_cast<const uint32_t*>(heap != nullptr ? heap : map + kMapBitsInline);
    const uint64_t tag = hit_wound_tag();
    for (int32_t i = 0; data != nullptr && i < max && i < kMapScan; ++i) {
        const bool allocated = (bits[i / 32] >> (i % 32) & 1u) != 0;
        uint64_t key = 0;
        memcpy(&key, data + static_cast<size_t>(i) * kElemStride, sizeof(key));
        if (allocated && key == tag) {
            float v = 0.0f;
            memcpy(&v, data + static_cast<size_t>(i) * kElemStride + kElemValue, sizeof(v));
            return v;
        }
    }
    return 0.0f;
}

// An FName's text as one token for the wire: printable, no spaces, "None" when it does not fit.
// Card 238: false when the FName is not in the name pool (fname_entry_checked refused it); out is then "None".
bool hit_name_token(const void* fname, char* out, size_t cap) {
    if (fname_entry_checked(fname) == nullptr) {
        snprintf(out, cap, "%s", "None");
        return false;
    }
    const ObjectName n = fname_text(fname);
    size_t len = strlen(n.text);
    bool ok = len > 0 && len < cap;
    for (size_t i = 0; ok && i < len; ++i) {
        ok = n.text[i] > ' ' && n.text[i] <= '~' && n.text[i] != ';'; // ';' ends a row
    }
    snprintf(out, cap, "%s", ok ? n.text : "None");
    return true;
}

// Card 238: an AI trooper's projectile hit carried a bone FName outside the name pool (the C5M2 crash). The row
// still goes out with bone None; the event is counted and named here.
void hit_note_bad_name(const void* npc, bool bone_ok, bool hit_bone_ok) {
    ++g_hit.bad_names;
    if (note_event_cap(g_hit.bad_name_lines, kHitBadNameLines, "card238 bad bone name")) {
        log::line("WARN card238: host hit %s: %s%s%s FName not in the name pool, sent as None (%u such events, %u "
                  "FName refusals in all)", object_name(npc).text, bone_ok ? "" : "event bone",
                  !bone_ok && !hit_bone_ok ? " and " : "", hit_bone_ok ? "" : "hit result bone", g_hit.bad_names,
                  fname_refusals());
    }
}

bool hit_no_point(const HitRow& r) {
    return r.point.x == 0.0 && r.point.y == 0.0 && r.point.z == 0.0 && strcmp(r.bone, "None") == 0;
}

void hit_queue(const void* gore, const uint8_t* event) {
    const void* const npc = read_ptr(gore, kComponentOwner);
    const uint64_t guid = npc != nullptr ? c41_guid(npc) : 0;
    if (guid == 0) {
        ++g_hit.no_guid;
        return;
    }
    if (g_hit.count >= kHitMax) {
        ++g_hit.dropped;
        return;
    }
    HitRow& r = g_hit.rows[g_hit.count++];
    r.guid = guid;
    r.point = read_vec3(event, kEventHit + kHitImpactPoint);
    r.normal = read_vec3(event, kEventHit + kHitImpactNormal);
    memcpy(&r.damage, event + kEventDamage, sizeof(r.damage));
    r.wound = hit_wound_value(event);
    const bool bone_ok = hit_name_token(event + kEventBone, r.bone, sizeof(r.bone));
    const bool hit_bone_ok = hit_name_token(event + kEventHit + kHitBoneName, r.hit_bone, sizeof(r.hit_bone));
    if (!bone_ok || !hit_bone_ok) {
        hit_note_bad_name(npc, bone_ok, hit_bone_ok);
    }
    if (hit_no_point(r)) { // nothing to place gore at (it would land at the world origin)
        --g_hit.count;
        ++g_hit.no_point;
        return;
    }
    ++g_hit.queued;
    // hb47 0x138: an impact 4042 uu from the NPC on the host as well (both sides had it at the same place), so the
    // event's point is not on the body. Name such events here, where the damage and bone are known.
    // Card 189: a ragdolling corpse's actor stays at the death place while its mesh falls, so the measure (and the
    // point sent for the client's hit_drift) is the ragdoll root (body 0) when the NPC has one; the client compares
    // with its own copy's ragdoll root under the same test.
    // Run c423b137a: 2 client WARN rows (460 and 566 uu) on corpses a pose row held on both sides: the host sent
    // its actor location (body 0 kinematic) and the client compared its body 0. Same test on both sides now.
    Vec3 at = actor_location(npc);
    if (!c132_ragdoll_root(read_ptr(npc, kCharacterMesh), at)) {
        (void)c132_posed_root(npc, at);
    }
    r.host_at = at;
    const double off = sqrt((r.point.x - at.x) * (r.point.x - at.x) + (r.point.y - at.y) * (r.point.y - at.y) +
                            (r.point.z - at.z) * (r.point.z - at.z));
    if (off >= kHitFar && note_event_cap(g_hit.far_lines, kHitFarLines, "card132 host hit far")) {
        // Card 167: the hit component and its world location, to tell a stale collision body from a stale point.
        const void* const comp = g_weak_get != nullptr ? g_weak_get(event + kEventHit + kHitComponent) : nullptr;
        const Vec3 comp_at = comp != nullptr ? read_vec3(comp, kHitCompTranslation) : Vec3{};
        log::line("WARN card132: host hit %s guid 0x%llx: impact (%.0f, %.0f, %.0f) is %.0f uu from the NPC at (%.0f, "
                  "%.0f, %.0f); damage %.1f wound %.2f bone %s hit bone %s; hit component %s (%s) at (%.0f, %.0f, "
                  "%.0f)", object_name(npc).text,
                  static_cast<unsigned long long>(guid), r.point.x, r.point.y, r.point.z, off, at.x, at.y, at.z,
                  r.damage, r.wound, r.bone, r.hit_bone, comp != nullptr ? object_name(comp).text : "null",
                  comp != nullptr ? class_name(comp).text : "-", comp_at.x, comp_at.y, comp_at.z);
    }
    if (note_event_cap(g_hit.lines, kHitLines, "card132 hit")) {
        log::line("card132: host hit %s guid 0x%llx at (%.0f, %.0f, %.0f) normal (%.2f, %.2f, %.2f) damage %.1f "
                  "wound %.2f bone %s hit bone %s", object_name(npc).text, static_cast<unsigned long long>(guid),
                  r.point.x, r.point.y, r.point.z, r.normal.x, r.normal.y, r.normal.z, r.damage, r.wound, r.bone,
                  r.hit_bone);
    }
}

int hit_row_text(const HitRow& r, char* out, size_t cap) {
    return snprintf(out, cap, "%llx %.0f %.0f %.0f %.3f %.3f %.3f %.2f %.3f %.0f %.0f %.0f %s %s;",
                    static_cast<unsigned long long>(r.guid), r.point.x, r.point.y, r.point.z, r.normal.x, r.normal.y,
                    r.normal.z, r.damage, r.wound, r.host_at.x, r.host_at.y, r.host_at.z, r.bone, r.hit_bone);
}

void hit_send(const void* world, const char* text) {
    const C95Fanout f = c95_send_all(world, ChanOp::Hit, text);
    ++g_hit.frames;
    g_hit.sent += static_cast<uint32_t>(f.sent);
}

} // namespace

void detour_c132_gore_damage(void* gore, const void* damage_event) {
    g_orig_c132_gore_damage(gore, damage_event);
    if (!g_client_session && gore != nullptr && damage_event != nullptr) {
        hit_queue(gore, static_cast<const uint8_t*>(damage_event));
    }
}

// Host, once per tick: the tick's hits as Hit frames, as many rows per frame as fit in kChanTextMax.
void c132_hit_step(void* engine) {
    if (g_hit.count == 0) {
        return;
    }
    const void* const world = !g_client_session && engine != nullptr ? g_get_game_world(engine) : nullptr;
    char frame[kChanTextMax + 1];
    size_t used = 0;
    for (int32_t i = 0; world != nullptr && i < g_hit.count; ++i) {
        char row[kHitText];
        const int n = hit_row_text(g_hit.rows[i], row, sizeof(row));
        if (n <= 0 || static_cast<size_t>(n) >= sizeof(row)) {
            ++g_hit.dropped;
            continue;
        }
        if (used + static_cast<size_t>(n) > static_cast<size_t>(kChanTextMax)) {
            hit_send(world, frame);
            used = 0;
        }
        memcpy(frame + used, row, static_cast<size_t>(n) + 1);
        used += static_cast<size_t>(n);
    }
    if (world != nullptr && used != 0) {
        hit_send(world, frame);
    }
    g_hit.count = 0;
}

namespace {

// Client: the damage event OnDamage reads, rebuilt from one hit. `elem` holds the one-element wound tag map.
void hit_build_event(uint8_t* event, uint8_t* elem, void* npc, void* mesh, const HitRow& r) {
    uint8_t* const hit = event + kEventHit;
    const int32_t none = -1;
    const float one = 1.0f;
    memcpy(hit + kHitFaceIndex, &none, sizeof(none));
    memcpy(hit + kHitTime, &one, sizeof(one));
    memcpy(hit + kHitLocation, &r.point, sizeof(r.point));
    memcpy(hit + kHitImpactPoint, &r.point, sizeof(r.point));
    memcpy(hit + kHitNormal, &r.normal, sizeof(r.normal));
    memcpy(hit + kHitImpactNormal, &r.normal, sizeof(r.normal));
    memcpy(hit + kHitItem, &none, sizeof(none));
    memcpy(hit + kHitMyItem, &none, sizeof(none));
    hit[kHitFlags] = 1;
    g_weak_assign(hit + kHitActor, &npc);
    g_weak_assign(hit + kHitComponent, &mesh);
    wchar_t w[kHitBoneMax];
    for (int32_t pass = 0; pass < 2; ++pass) {
        const char* const text = pass == 0 ? r.bone : r.hit_bone;
        size_t i = 0;
        for (; text[i] != '\0' && i + 1 < kHitBoneMax; ++i) {
            w[i] = static_cast<wchar_t>(text[i]);
        }
        w[i] = L'\0';
        g_fname_ctor(pass == 0 ? event + kEventBone : hit + kHitBoneName, w, kFNameAdd);
    }
    memcpy(event + kEventDamage, &r.damage, sizeof(r.damage));
    const uint64_t tag = hit_wound_tag();
    const int32_t one_i = 1, zero_i = 0;
    memcpy(elem, &tag, sizeof(tag));
    memcpy(elem + kElemValue, &r.wound, sizeof(r.wound));
    memcpy(elem + kElemHashNext, &none, sizeof(none));
    uint8_t* const map = event + kEventTags;
    void* const data = elem;
    memcpy(map, &data, sizeof(data));
    memcpy(map + kMapNum, &one_i, sizeof(one_i));
    memcpy(map + kMapMax, &one_i, sizeof(one_i));
    memcpy(map + kMapBitsInline, &one_i, sizeof(one_i)); // element 0 allocated
    memcpy(map + kMapNumBits, &one_i, sizeof(one_i));
    const int32_t bits_max = 128; // the inline allocation: four dwords
    memcpy(map + kMapMaxBits, &bits_max, sizeof(bits_max));
    memcpy(map + kMapFirstFree, &none, sizeof(none));
    memcpy(map + kMapNumFree, &zero_i, sizeof(zero_i));
    memcpy(map + kMapHashInline, &zero_i, sizeof(zero_i)); // bucket 0 holds element 0
    memcpy(map + kMapHashSize, &one_i, sizeof(one_i));
}

bool hit_parse(const char* text, HitRow& r) {
    unsigned long long guid = 0;
    const int got = sscanf_s(text, "%llx %lf %lf %lf %lf %lf %lf %f %f %lf %lf %lf %63s %63s", &guid, &r.point.x,
                             &r.point.y, &r.point.z, &r.normal.x, &r.normal.y, &r.normal.z, &r.damage, &r.wound,
                             &r.host_at.x, &r.host_at.y, &r.host_at.z, r.bone,
                             static_cast<unsigned>(sizeof(r.bone)), r.hit_bone, static_cast<unsigned>(sizeof(r.hit_bone)));
    r.guid = guid;
    char* const end = strchr(r.hit_bone, ';'); // the row's last token runs on into the next row
    if (end != nullptr) {
        *end = '\0';
    }
    return got == 14 && guid != 0;
}

// Client, card 140: how far the client copy's ReplicatedMovement.Location is from the host's NPC location at the hit.
// The host location is read at the hit and the copy's at apply, so a pacing wait adds speed times wait. Card 189: a
// copy with a ragdoll root (body 0 simulating) compares that root, as the host's hit_queue sent its own.
// Run c423b130a: 13 WARN rows on one corpse (guid 0xd0), each 324 uu, the distance of the host's ragdoll root from
// its actor: the copy was held by the host's pose row (kinematic), so its body 0 was not read. It is now.
// Card 474: returns the shift for r's impact point. A hit classed late (frame age over kHitLateMs) or kHitDrift or more
// from a live copy (no ragdoll root, not posed) moves by the copy's ReplicatedMovement.Location minus the host's
// location at the hit, so the gore lands on the copy's body where the copy is now, not where the host's NPC was when
// the frame was built. Fresh hits, ragdolls and posed corpses keep the host's point (zero shift).
Vec3 hit_drift(const void* pc, const void* npc, const HitRow& r) {
    Vec3 rep = {};
    const bool ragdoll = c132_ragdoll_root(read_ptr(npc, kCharacterMesh), rep) || c132_posed_root(npc, rep);
    if (!ragdoll && !c132_rep_location(npc, &rep)) {
        return Vec3{};
    }
    const double dx = r.host_at.x - rep.x;
    const double dy = r.host_at.y - rep.y;
    const double dz = r.host_at.z - rep.z;
    const double d = sqrt(dx * dx + dy * dy + dz * dz);
    g_hit.drift_max = d > g_hit.drift_max ? d : g_hit.drift_max;
    // Card 223: 49 of 51 rows on 2026-11-16 were applied in the first tick after a client frame of 1.0 to 2.1 s (the
    // game's own hitch); the row was on time on the wire. Such a row is plain and not counted.
    const int32_t age = c132_ledger_frame_age_ms(pc, kHitAgeMaxMs);
    const bool late = age > kHitLateMs;
    const bool move = !ragdoll && (late || d >= kHitDrift);
    const Vec3 shift = move ? Vec3{-dx, -dy, -dz} : Vec3{};
    if (move) {
        ++g_hit.moved;
        if (note_event_cap(g_hit.moved_lines, kHitFarLines, "card474 hit moved")) {
            log::line("card474: client hit %s guid 0x%llx: frame age %d ms%s, host location %.0f uu from the copy's "
                      "ReplicatedMovement.Location: impact point moved by (%.0f, %.0f, %.0f) to (%.0f, %.0f, %.0f)",
                      object_name(npc).text, static_cast<unsigned long long>(r.guid), age,
                      late ? " (late)" : " (on time)", d, shift.x, shift.y, shift.z, r.point.x + shift.x,
                      r.point.y + shift.y, r.point.z + shift.z);
        }
    }
    if (d < kHitDrift) {
        return shift;
    }
    g_hit.far_hits += late ? 0u : 1u;
    if (note_event_cap(g_hit.far_lines, kHitFarLines, "card140 hit drift")) {
        const Vec3 at = actor_location(npc);
        log::line("%scard140: client hit %s guid 0x%llx: host location (%.0f, %.0f, %.0f) is %.0f uu from the "
                  "copy's %s (%.0f, %.0f, %.0f), kHitDrift %.0f; the copy is at (%.0f, %.0f, "
                  "%.0f), local role %u; card223: frame age %d ms%s", late ? "" : "WARN ", object_name(npc).text,
                  static_cast<unsigned long long>(r.guid), r.host_at.x,
                  r.host_at.y, r.host_at.z, d, ragdoll ? "ragdoll root" : "ReplicatedMovement.Location", rep.x, rep.y, rep.z,
                  kHitDrift, at.x, at.y, at.z,
                  static_cast<const uint8_t*>(npc)[kActorLocalRole], age,
                  late ? " (applied late, not counted)" : "");
    }
    return shift;
}

void hit_apply(void* pc, const HitRow& r) {
    void* const npc = c41_resolve_guid(pc, r.guid);
    void* const gore = npc != nullptr && g_gore_class != nullptr ? find_component(npc, g_gore_class) : nullptr;
    void* const mesh = npc != nullptr ? read_ptr(npc, kCharacterMesh) : nullptr;
    g_hit.unmapped += npc == nullptr ? 1 : 0;
    g_hit.no_gore += npc != nullptr && (gore == nullptr || mesh == nullptr) ? 1 : 0;
    if (gore == nullptr || mesh == nullptr || g_fname_ctor == nullptr || g_weak_assign == nullptr) {
        return;
    }
    alignas(16) uint8_t event[kEventSize] = {};
    alignas(8) uint8_t elem[kElemStride + 4] = {};
    const Vec3 shift = hit_drift(pc, npc, r); // card 474: before the event, which carries the moved point
    HitRow m = r;
    m.point = Vec3{r.point.x + shift.x, r.point.y + shift.y, r.point.z + shift.z};
    hit_build_event(event, elem, npc, mesh, m);
    c39_hit_call(true);
    g_orig_c132_gore_damage(gore, event);
    c39_hit_call(false);
    ++g_hit.applied;
    if (note_event_cap(g_hit.lines, kHitLines, "card132 hit")) {
        log::line("card132: client hit %s guid 0x%llx at (%.0f, %.0f, %.0f) damage %.1f wound %.2f bone %s: OnDamage "
                  "ran (filter %s, health component %p)", object_name(npc).text,
                  static_cast<unsigned long long>(r.guid), m.point.x, m.point.y, m.point.z, r.damage, r.wound, r.bone,
                  read_i32(gore, kGoreFilterSize) != 0 ? "bound" : "unbound", read_ptr(gore, kGoreHealth));
    }
}

} // namespace

// Client: one Hit frame, rows split on ';'.
void c132_receive_hit(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    if (chan_text(s, text, sizeof(text)) <= 0) {
        ++g_hit.bad;
        return;
    }
    const char* row = text;
    for (int32_t i = 0; i < kHitMax && *row != '\0'; ++i) {
        HitRow r{};
        ++g_hit.received;
        if (hit_parse(row, r) && hit_no_point(r)) {
            ++g_hit.no_point; // no gore at the world origin
        } else if (hit_parse(row, r)) {
            hit_apply(pc, r);
        } else {
            ++g_hit.bad;
        }
        const char* const end = strchr(row, ';');
        row = end != nullptr ? end + 1 : row + strlen(row);
    }
}

void c132_hit_arm_map() {
    if (g_hit.queued != 0 || g_hit.received != 0 || g_hit.no_point != 0 || g_hit.bad_names != 0) {
        log::line("card132: %s hit relay map totals: %u queued, %u dropped, %u without guid, %u frames, %u sent; %u "
                  "received, %u applied, %u unmapped, %u without gore or mesh, %u bad, %u drift over kHitDrift (largest %.0f "
                  "uu), %u without a hit point skipped, %u bad bone names (card 238), %u impact points moved (card 474)",
                  side(), g_hit.queued, g_hit.dropped, g_hit.no_guid, g_hit.frames, g_hit.sent, g_hit.received,
                  g_hit.applied, g_hit.unmapped, g_hit.no_gore, g_hit.bad, g_hit.far_hits, g_hit.drift_max,
                  g_hit.no_point, g_hit.bad_names, g_hit.moved);
    }
    g_hit = HitRelay{};
}

} // namespace ubw::hooks
