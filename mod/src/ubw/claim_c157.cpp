// Card 157: client hit claims. A client's own player round hits where the client saw its target; the host's copy of
// that round (launched by the server copy pawn, flown against the host's bug positions) deals no damage of its own.
//
// Client: each own player round (instigator a player character with a local controller) is noted at launch with the
// card 155 shot number (pl_shot_seq). Its first OnHit sends one HitClaim text frame to the host:
//   "<shot seq> <npc guid hex, 0 = not an NPC> px py pz nx ny nz <bone>"
// The client's own OnHit then runs as before (its impact effects on its own screen).
//
// Host: each round launched by a server copy player character is noted per remote PC, in launch order. Its OnHit does
// not run: the round parks (the hit kept, the actor and components weak) until its claim. Claims come in hit order
// per PC and take that PC's oldest unclaimed round, parked or still flying:
//   - an NPC claim: basic checks (the NPC resolves and is alive, the point is within kRange of the shooter's pawn, a
//     world-static trace from the shooter's view point reaches it within kTraceSlack), then the round's original
//     OnHit runs with a hit built at the claim's point on the NPC's mesh. Its damage and effects are the host's own.
//   - a miss claim (the client's round hit no NPC): a parked round replays its own hit when that hit was not an NPC
//     (so a wall impact shows on the host) and goes back to the pool otherwise; a flying round is marked, and its
//     later hit replays the same way.
// Card 157 run 4: a round that overlaps a pawn (a bug's mesh) never runs OnHit. AADProjectile::OnOverlapBegin
// (inlined in its exec thunk, exe+0x4A37190) broadcasts the same hit delegates OnHit does (this +0x2B0 dynamic, +0x2C0
// native), and those deal the damage. So the overlap is the second contact path: the host parks a noted round's NPC
// overlap like an OnHit (the claim then runs OnHit with the claim's hit), and the client claims its round's NPC overlap.
// A non-NPC overlap runs as before. A server-copy round that reaches an NPC with no table entry counts as copy damage
// (WARN): the no-double-hit rule says it must stay 0.
// A parked round with no claim after kParkMs replays a non-NPC hit, or goes back to the pool (WARN: a lost claim).
// A claim with no round after kClaimMs is dropped (WARN). Trust model: friends-only, basic crash-safety checks only.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ubw/chan_ops.h"
#include "ubw/hooks_internal.h"

namespace ubw::hooks {

uint64_t c41_guid(const void* actor);            // enemies.cpp
void* c41_resolve_guid(void* pc, uint64_t guid); // enemies.cpp
void c132_sent_drop(const void* proj);            // grenades.cpp (card 281: a made round)
void c132_rounds_forget(const void* proj);        // grenades.cpp

namespace {

using chan::ChanOp;

constexpr uint32_t kRounds = 64;        // host: remote rounds in flight or parked; client: own rounds in flight
constexpr uint32_t kClaims = 32;        // host: claims waiting for their round
constexpr uint64_t kParkMs = 1500;      // host: a parked round's wait for its claim
constexpr uint64_t kClaimMs = 1500;     // host: a claim's wait for its round
constexpr double kRange = 30000.0;      // the point's furthest distance from the shooter's pawn (uu), with margin
constexpr double kTraceSlack = 600.0;   // the world-static trace may stop this short of the point (uu)
constexpr size_t kHitBytes = 0x100;     // FHitResult
constexpr size_t kBoneChars = 64;
constexpr size_t kCharacterMesh = 0x328; // ACharacter::Mesh
constexpr uint32_t kLines = 120;        // per-claim log lines (card 165: every claim of a bug run); counts are in the totals line
constexpr size_t kHitImpactPoint = 0x28;
constexpr size_t kHitImpactNormal = 0x58;
constexpr size_t kHitLocation = 0x10;
constexpr size_t kHitNormal = 0x40;
constexpr size_t kHitTime = 0x4;
constexpr size_t kHitItem = 0xA4;
constexpr size_t kHitMyItem = 0xA8;
constexpr size_t kHitFlags = 0xAD;
constexpr size_t kHitActorWeak = 0xB8;
constexpr size_t kHitBone = 0xE0;     // fallbacks only: hit_layout() reads the reflected offsets
constexpr uint32_t kParkLines = 10;   // host: one diag line per parked round
constexpr uint32_t kLaunchLines = 12; // card 165: host lines on player launches (who fired, which side)
constexpr int kOverlapParams = 6;     // OverlappedComponent, OtherActor, OtherComp, OtherBodyIndex, bFromSweep, SweepResult
constexpr size_t kFrameCode = 0x20;   // FFrame::Code (null when called through ProcessEvent, a delegate)
constexpr size_t kFrameLocals = 0x28; // FFrame::Locals (the parms struct; execOnOverlapBegin's StepExplicitProperty)
// Card 281: AADProjectile's impact delegate list (TArray of {FWeakObjectPtr, FName}, as BindToProjectileImpact adds)
// and the engine's array grow (exe+0x11653C0: int32 grow(packed size 0x10 | align 4 << 8, array, &max), returns the
// old max), which BindToProjectileImpact (exe+0x4A36B70) calls before it writes data[num++].
constexpr size_t kProjImpactList = 0x2B0;
constexpr size_t kPairBytes = 0x10;
constexpr uint32_t kGrowRva = 0x011653C0;
constexpr uint32_t kGrowPacked = 0x410;
constexpr uint8_t kGrowHead[5] = {0x48, 0x89, 0x6C, 0x24, 0x18}; // mov [rsp+18h], rbp

enum class RoundState : uint8_t { Flying, Parked, Miss };

struct Round {
    void* proj;          // null: free slot
    const void* pc;      // host: the shooter's server copy PC; client: null
    C31Weak pc_weak;     // card 331: pc's object; a PC freed while the round flies reads gone before any deref
    uint32_t seq;        // client: the shot number; host: the launch count
    const void* init;    // host: the round's init data class (card 281: a made round's class)
    uint64_t born;
    RoundState state;
    uint64_t parked_at;  // host, Parked
    void* hit_comp;      // host, Parked: the round's own component
    C31Weak other;       // host, Parked: what the host's copy hit
    C31Weak other_comp;
    bool other_npc;
    Vec3 impulse;
    alignas(16) uint8_t hit[kHitBytes];
};

struct Claim {
    void* pc;            // null: free slot
    C31Weak pc_weak;     // card 331: pc's object, checked before the claim is served
    uint32_t seq;
    uint64_t guid;
    Vec3 point;
    Vec3 normal;
    char bone[kBoneChars];
    uint64_t arrived;
};

// Card 281, host: per server copy PC, what its last claimed round was: the init data class and the first pair of the
// round's +0x2B0 impact delegate list ({weak object, function name}, 16 bytes), with the weapon it came from.
// b189: a server copy round's list is empty at park and claim (c423b188a: 12 of 12 rounds num 0), and those rounds
// still deal the damage (claims 17-19: Health 400 -> 396 -> 392 -> 388), so the pair is optional: `bound` false
// means the round had none and the made round gets none.
struct MadeRecord {
    const void* pc;      // null: free slot
    C31Weak weapon;      // the copy pawn's equipped weapon when recorded
    const void* init;
    bool bound;          // pair holds the round's first impact pair
    alignas(8) uint8_t pair[kPairBytes];
};

// Card 331: the noted PC while its object still lives at that address; null once it is gone (a client that left
// with a round in flight or a claim waiting). Every deref of a noted PC past the tick it was noted goes through this.
const void* live_pc(const void* pc, const C31Weak& w) {
    return pc != nullptr && c31_weak_get(w) == pc ? pc : nullptr;
}

struct C157State {
    Round rounds[kRounds];
    Claim claims[kClaims];
    MadeRecord records[kC17Slots]; // card 281
    uint32_t launches;
    uint32_t sent;         // client: claims sent (NPC and miss)
    uint32_t sent_npc;
    uint32_t applied;      // host: NPC claims applied
    uint32_t refused;
    uint32_t misses;       // host: miss claims resolved
    uint32_t parked;
    uint32_t lost;         // host: parked rounds that timed out with an NPC hit
    uint32_t orphans;      // host: claims with no round
    uint32_t full;         // a table was full
    uint32_t overlaps;     // host: rounds parked on an NPC overlap; client: claims sent from an NPC overlap
    uint32_t copy_damage;  // host: server-copy rounds that reached an NPC outside the table (must stay 0)
    uint32_t park_lines;
    uint32_t launch_lines; // card 165: host launch diag lines
    uint32_t forget_lines; // card 165: host forget and claim diag lines
    uint32_t forgets;      // card 165: noted rounds forgotten before a claim took them
    uint32_t made;         // card 281: claims served by a made round
    uint32_t unrecorded;   // card 281: rounds record_round skipped (one of its four checks failed)
    uint32_t record_lines; // card 281: diag rows on those skips
    uint32_t lines;
    uint64_t profile;      // FName "Spectator" (blocks world static only)
};
C157State g_c157{};

// FHitResult offsets read by reflection once (/Script/Engine.HitResult); the constants above stand when a name is
// not found. Kept across maps. HitLayout is declared in hooks_internal.h (card 264: trace_profile reads the actor).
HitLayout g_hit{kHitBone, kHitBone + 8, kHitComponent, kHitActorWeak, kHitItem, kHitMyItem, false};

// AADProjectile::OnOverlapBegin's parm offsets in FFrame::Locals, in declaration order; read once.
struct OverlapLayout {
    size_t at[kOverlapParams];
    bool read;
    bool ok;
};
OverlapLayout g_overlap{};

uint64_t now_ms() {
    return GetTickCount64();
}

bool line_ok() {
    return note_event_cap(g_c157.lines, kLines, "card157 claims");
}

} // namespace

// Card 264: outside the anonymous namespace so trace_profile (hooks_ubw.cpp) reads the hit actor.
const HitLayout& hit_layout() {
    if (g_hit.read) {
        return g_hit;
    }
    g_hit.read = true;
    void* const s = g_static_find_object != nullptr
                        ? g_static_find_object(nullptr, nullptr, L"/Script/Engine.HitResult", false) : nullptr;
    struct Row {
        const char* name;
        size_t* at;
    };
    const Row rows[] = {{"BoneName", &g_hit.bone}, {"MyBoneName", &g_hit.my_bone}, {"Component", &g_hit.comp},
                        {"HitObjectHandle", &g_hit.actor}, {"Item", &g_hit.item}, {"MyItem", &g_hit.my_item}};
    char text[320] = {};
    size_t n = 0;
    bool all = s != nullptr;
    for (const Row& r : rows) {
        const uint8_t* const prop = s != nullptr ? prop_find_prop(s, r.name) : nullptr;
        if (prop != nullptr) {
            *r.at = static_cast<size_t>(read_i32(prop, kPropertyOffset));
        }
        all = all && prop != nullptr;
        const int w = snprintf(text + n, sizeof(text) - n, " %s +0x%zX%s", r.name, *r.at, prop != nullptr ? "" : " (kept)");
        n = w > 0 && n + static_cast<size_t>(w) < sizeof(text) ? n + static_cast<size_t>(w) : n;
    }
    log::line("%scard157: %s FHitResult %s by reflection:%s", all ? "" : "WARN ", side_text(),
              s != nullptr ? "offsets" : "struct not found, constants", text);
    return g_hit;
}

namespace {

// Reads the overlap's parms from the frame; false when the call is from bytecode (Code set) or the layout is unknown.
bool overlap_args(const void* frame, void** overlapped, void** other, void** other_comp, bool* sweep,
                  const uint8_t** hit) {
    if (!g_overlap.read) {
        g_overlap.read = true;
        const void* const fn = g_projectile_class != nullptr && g_static_find_object != nullptr
                                   ? g_static_find_object(nullptr, g_projectile_class(), L"OnOverlapBegin", false)
                                   : nullptr;
        const uint8_t* f = fn != nullptr ? static_cast<const uint8_t*>(read_ptr(fn, kStructChildProperties)) : nullptr;
        char text[320] = {};
        size_t n = 0;
        int i = 0;
        for (; f != nullptr && i < kOverlapParams; ++i, f = static_cast<const uint8_t*>(read_ptr(f, kFieldNext))) {
            g_overlap.at[i] = static_cast<size_t>(read_i32(f, kPropertyOffset));
            const int w = snprintf(text + n, sizeof(text) - n, " %s +0x%zX", fname_text(f + kFieldName).text,
                                   g_overlap.at[i]);
            n = w > 0 && n + static_cast<size_t>(w) < sizeof(text) ? n + static_cast<size_t>(w) : n;
        }
        g_overlap.ok = i == kOverlapParams;
        log::line("%scard157: %s OnOverlapBegin parms (%d):%s", g_overlap.ok ? "" : "WARN ", side_text(), i, text);
    }
    const uint8_t* const locals = static_cast<const uint8_t*>(read_ptr(frame, kFrameLocals));
    if (!g_overlap.ok || read_ptr(frame, kFrameCode) != nullptr || locals == nullptr) {
        return false;
    }
    *overlapped = read_ptr(locals, g_overlap.at[0]);
    *other = read_ptr(locals, g_overlap.at[1]);
    *other_comp = read_ptr(locals, g_overlap.at[2]);
    *sweep = locals[g_overlap.at[4]] != 0;
    *hit = locals + g_overlap.at[5];
    return true;
}

// The controller of the player character that fired `proj`, when it is `want_side`'s; else null. `shooter` is the
// fire task's avatar (the weapon, or the pawn), as pl_shot reads it: the pawn, else the weapon's owner, else the
// projectile's instigator (null on a pooled round at launch).
const void* shooter_of(const void* proj, const void* shooter, uint32_t want_side) {
    if (g_player_character_class == nullptr) {
        return nullptr;
    }
    const void* const cand[3] = {shooter, shooter != nullptr ? weapon_pawn(shooter) : nullptr,
                                 proj != nullptr ? read_ptr(proj, kActorInstigator) : nullptr};
    for (const void* const pawn : cand) {
        if (pawn != nullptr && is_a(pawn, g_player_character_class())) {
            const void* const pc = read_ptr(pawn, kPawnController);
            return pc != nullptr && sprint_side(pc) == want_side ? pc : nullptr;
        }
    }
    return nullptr;
}

Round* round_find(const void* proj) {
    for (Round& r : g_c157.rounds) {
        if (r.proj != nullptr && r.proj == proj) {
            return &r;
        }
    }
    return nullptr;
}

void round_add(void* proj, const void* pc, uint32_t seq, const void* init) {
    Round* slot = round_find(proj); // a pooled projectile reused: its old entry is stale
    for (uint32_t i = 0; slot == nullptr && i < kRounds; ++i) {
        slot = g_c157.rounds[i].proj == nullptr ? &g_c157.rounds[i] : nullptr;
    }
    if (slot == nullptr) {
        ++g_c157.full;
        if (line_ok()) {
            log::line("WARN card157: %s t=%llu round table full (%u): %s not noted", side_text(),
                      static_cast<unsigned long long>(now_ms()), kRounds, class_name(proj).text);
        }
        return;
    }
    *slot = Round{};
    slot->proj = proj;
    slot->pc = pc;
    c31_weak_set(slot->pc_weak, const_cast<void*>(pc));
    slot->seq = seq;
    slot->init = init;
    slot->born = now_ms();
}

bool is_npc(const void* actor) {
    return actor != nullptr && g_npc_class != nullptr && is_a(actor, g_npc_class());
}

// The hit's BoneName as frame-safe text: printable ASCII only (chan_text_chars refuses the rest), else "None".
void text_bone(const void* hit, char* out, size_t cap) {
    const ObjectName n = fname_text(static_cast<const uint8_t*>(hit) + hit_layout().bone);
    snprintf(out, cap, "%s", n.text[0] != '\0' ? n.text : "None");
    for (size_t i = 0; out[i] != '\0'; ++i) {
        const unsigned char c = static_cast<unsigned char>(out[i]);
        if (c < 0x21 || c > 0x7E) {
            snprintf(out, cap, "None");
            return;
        }
    }
}

// Client: the own round's first hit goes to the host as a claim.
void client_claim(const Round& r, const void* other, const void* hit) {
    const uint64_t guid = is_npc(other) ? c41_guid(other) : 0;
    const Vec3 p = read_vec3(hit, kHitImpactPoint);
    const Vec3 n = read_vec3(hit, kHitImpactNormal);
    char bone[kBoneChars];
    text_bone(hit, bone, sizeof(bone));
    char text[192];
    snprintf(text, sizeof(text), "%u %llx %.1f %.1f %.1f %.3f %.3f %.3f %s", r.seq,
             static_cast<unsigned long long>(guid), p.x, p.y, p.z, n.x, n.y, n.z, bone);
    static wchar_t wire[kChanTextFrame];
    const WireString w = chan_text_frame(ChanOp::HitClaim, text, wire);
    void* const pc = const_cast<void*>(live_pc(r.pc, r.pc_weak)); // the shooter's controller, noted at launch
    const bool ok = w.num > 0 && pc != nullptr && chan_send_host(pc, w);
    ++g_c157.sent;
    g_c157.sent_npc += guid != 0 ? 1u : 0u;
    if (!ok || guid != 0 || line_ok()) {
        log::line("%scard157: client t=%llu HitClaim shot %u %s %s at (%.1f, %.1f, %.1f) bone %s: %s (%u sent, %u on "
                  "NPCs)", ok ? "" : "WARN ", static_cast<unsigned long long>(now_ms()), r.seq,
                  guid != 0 ? "NPC" : "miss", class_name(other).text, p.x, p.y, p.z, bone, ok ? "sent" : "not sent",
                  g_c157.sent, g_c157.sent_npc);
    }
}

// Host: the round's original OnHit with its kept hit (a non-NPC hit still alive), else back to the pool.
// Returns true when the kept hit ran.
bool replay_or_release(Round r) {
    void* const other = c31_weak_get(r.other);
    void* const other_comp = c31_weak_get(r.other_comp);
    const bool replay = r.state == RoundState::Parked && !r.other_npc && other != nullptr;
    if (replay) {
        g_orig_c132_on_hit(r.proj, r.hit_comp, other, other_comp, &r.impulse, r.hit);
    } else {
        g_orig_c74_proj[2](r.proj);
    }
    return replay;
}

// Host: the hit the claim names, built on the NPC's mesh at the claim's point.
void build_hit(uint8_t* hit, void* npc, void* mesh, const Claim& c) {
    memset(hit, 0, kHitBytes);
    const int32_t none = -1;
    const float one = 1.0f;
    memcpy(hit, &none, sizeof(none)); // FaceIndex
    memcpy(hit + kHitTime, &one, sizeof(one));
    memcpy(hit + kHitLocation, &c.point, sizeof(c.point));
    memcpy(hit + kHitImpactPoint, &c.point, sizeof(c.point));
    memcpy(hit + kHitNormal, &c.normal, sizeof(c.normal));
    memcpy(hit + kHitImpactNormal, &c.normal, sizeof(c.normal));
    const HitLayout& l = hit_layout();
    memcpy(hit + l.item, &none, sizeof(none));
    memcpy(hit + l.my_item, &none, sizeof(none));
    hit[kHitFlags] = 1;
    g_weak_assign(hit + l.actor, &npc);
    g_weak_assign(hit + l.comp, &mesh);
    wchar_t w[kBoneChars];
    size_t i = 0;
    for (; c.bone[i] != '\0' && i + 1 < kBoneChars; ++i) {
        w[i] = static_cast<wchar_t>(c.bone[i]);
    }
    w[i] = L'\0';
    g_fname_ctor(hit + l.bone, w, kFNameAdd);
}

// Host: why an NPC claim is refused, or null.
const char* claim_check(const Claim& c, void* npc, double* dist_out, double* short_out) {
    *dist_out = 0.0;
    *short_out = 0.0;
    const void* const pawn = read_ptr(c.pc, kControllerPawn);
    if (pawn == nullptr) {
        return "no shooter pawn";
    }
    if (npc == nullptr) {
        return "NPC not found";
    }
    void* component = nullptr;
    const float health = c21_health(static_cast<const uint8_t*>(npc), &component);
    if (health > -0.5f && health <= 0.0f) { // -1: no health set read, not refused
        return "NPC dead";
    }
    const Vec3 from = c74_actor_loc(pawn);
    const double dx = c.point.x - from.x, dy = c.point.y - from.y, dz = c.point.z - from.z;
    *dist_out = sqrt(dx * dx + dy * dy + dz * dz);
    if (*dist_out > kRange) {
        return "out of range";
    }
    ViewPoint view{};
    if (!c24_view_point(pawn, view)) {
        return nullptr; // no view point: the range check stands alone
    }
    const Vec3 stop = trace_profile(pawn, view.loc, c.point, fname_cached(g_c157.profile, L"Spectator"));
    const double sx = c.point.x - stop.x, sy = c.point.y - stop.y, sz = c.point.z - stop.z;
    *short_out = sqrt(sx * sx + sy * sy + sz * sz);
    return *short_out > kTraceSlack ? "line of fire blocked" : nullptr;
}

// Host: the PC's oldest round still waiting for a claim.
Round* oldest_open(const void* pc) {
    Round* best = nullptr;
    for (Round& r : g_c157.rounds) {
        if (r.proj != nullptr && r.pc == pc && r.state != RoundState::Miss && (best == nullptr || r.born < best->born)) {
            best = &r;
        }
    }
    return best;
}

void apply_npc(Round r, const Claim& c) {
    void* const npc = c41_resolve_guid(c.pc, c.guid);
    double dist = 0.0, short_by = 0.0;
    const char* const why = claim_check(c, npc, &dist, &short_by);
    if (why != nullptr) {
        ++g_c157.refused;
        log::line("WARN card157: host t=%llu HitClaim shot %u from pc %s on %llx refused: %s (distance %.0f, trace short "
                  "by %.0f); round %s back to the pool (%u refused)", static_cast<unsigned long long>(now_ms()), c.seq,
                  object_name(c.pc).text, static_cast<unsigned long long>(c.guid), why, dist, short_by,
                  r.state == RoundState::Parked ? "parked" : "flying", g_c157.refused);
        g_orig_c74_proj[2](r.proj);
        return;
    }
    void* const mesh = read_ptr(npc, kCharacterMesh);
    void* hit_comp = r.state == RoundState::Parked ? r.hit_comp : read_ptr(r.proj, kActorRootComponent);
    alignas(16) uint8_t hit[kHitBytes];
    build_hit(hit, npc, mesh, c);
    const Vec3 zero{};
    const RoundState was = r.state;
    const float health_before = c23_set_health(npc); // card 165: one claimed shot, one drop
    g_orig_c132_on_hit(r.proj, hit_comp, npc, mesh, &zero, hit);
    const float health_after = c23_set_health(npc);
    ++g_c157.applied;
    if (line_ok() || (g_c157.applied % 50u) == 0) {
        log::line("card157: host t=%llu HitClaim shot %u from pc %s applied to %s at (%.1f, %.1f, %.1f) bone %s on "
                  "round %s (%s, waited %llu ms; distance %.0f, trace short by %.0f), Health %.1f -> %.1f (%u applied)",
                  static_cast<unsigned long long>(now_ms()), c.seq, object_name(c.pc).text, object_name(npc).text,
                  c.point.x, c.point.y, c.point.z, c.bone, class_name(r.proj).text,
                  was == RoundState::Parked ? "parked" : "flying",
                  static_cast<unsigned long long>(now_ms() - c.arrived), dist, short_by,
                  static_cast<double>(health_before), static_cast<double>(health_after), g_c157.applied);
    }
}

// Card 281, host: the equipped weapon of a server copy PC's pawn, null without one.
void* copy_weapon(const void* pc) {
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    const void* const weapons = pawn != nullptr && g_player_mi_class != nullptr && is_a(pawn, g_player_mi_class())
                                    ? read_ptr(pawn, kMiWeaponInventory)
                                    : nullptr;
    return weapons != nullptr && g_get_equipped_weapon != nullptr ? g_get_equipped_weapon(weapons) : nullptr;
}

MadeRecord* record_find(const void* pc) {
    for (MadeRecord& m : g_c157.records) {
        if (m.pc == pc) {
            return &m;
        }
    }
    return nullptr;
}

// Card 281, host: a real round of the PC is taken by a claim or parks; its init data and first impact pair (if any)
// become the PC's record (an unreadable impact list, no init data or no weapon leaves the record as it was). `when`
// names the caller and `claim` the claim's shot number (0 when parked with no claim yet) for the skip row.
void record_round(const Round& r, const char* when, uint32_t claim) {
    const uint8_t* const list = static_cast<const uint8_t*>(r.proj) + kProjImpactList;
    const uint8_t* const data = static_cast<const uint8_t*>(read_ptr(list, 0));
    int32_t num = 0;
    memcpy(&num, list + 8, sizeof(num));
    const void* const pc = live_pc(r.pc, r.pc_weak); // card 331: a gone PC records nothing
    void* const weapon = copy_weapon(pc);
    const char* const why = num < 0 || (num > 0 && data == nullptr) ? "impact list unreadable"
                            : r.init == nullptr ? "init data null"
                            : weapon == nullptr ? "copy_weapon null"
                                                : nullptr;
    if (why != nullptr) {
        ++g_c157.unrecorded;
        if (note_event_cap(g_c157.record_lines, kLaunchLines, "card281 record skips")) {
            const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
            log::line("card281: host t=%llu round %s launch %u of pc %s not recorded (%s, claim shot %u): %s (num %d, "
                      "init %s, weapon %s, pawn %s) (%u skipped)", static_cast<unsigned long long>(now_ms()),
                      class_name(r.proj).text, r.seq, object_name(pc).text, when, claim, why, num,
                      object_name(r.init).text, weapon != nullptr ? class_name(weapon).text : "none",
                      pawn != nullptr ? class_name(pawn).text : "none", g_c157.unrecorded);
        }
        return;
    }
    MadeRecord* slot = record_find(r.pc);
    slot = slot != nullptr ? slot : record_find(nullptr);
    if (slot == nullptr) {
        ++g_c157.full;
        return;
    }
    slot->pc = r.pc;
    c31_weak_set(slot->weapon, weapon);
    slot->init = r.init;
    slot->bound = num > 0;
    if (slot->bound) {
        memcpy(slot->pair, data, kPairBytes);
    } else {
        memset(slot->pair, 0, kPairBytes);
    }
}

// Card 281, host: the engine's array grow, when its first bytes are the ones read from the disassembly.
using GrowFn = int32_t (*)(uint32_t packed, void* array, int32_t* max);
GrowFn grow_fn() {
    const uint8_t* const at = g_exe_base != nullptr ? g_exe_base + kGrowRva : nullptr;
    return at != nullptr && memcmp(at, kGrowHead, sizeof(kGrowHead)) == 0
               ? reinterpret_cast<GrowFn>(const_cast<uint8_t*>(at))
               : nullptr;
}

// Card 281, host: adds `pair` to the made round's impact list as BindToProjectileImpact does (grow when full, then
// data[num++]). An identical pair already there is kept as it is. Returns why it was not added, or null.
const char* bind_pair(void* proj, const uint8_t* pair) {
    uint8_t* const list = static_cast<uint8_t*>(proj) + kProjImpactList;
    int32_t num = 0, max = 0;
    memcpy(&num, list + 8, sizeof(num));
    memcpy(&max, list + 12, sizeof(max));
    const uint8_t* const data = static_cast<const uint8_t*>(read_ptr(list, 0));
    if (num < 0 || num > max || (num > 0 && data == nullptr)) {
        return "impact list unreadable";
    }
    if (num > 0) { // a pooled round normally comes back with an empty list
        return memcmp(data, pair, kPairBytes) == 0 && num == 1 ? nullptr : "round already bound to another event";
    }
    const GrowFn grow = grow_fn();
    if (grow == nullptr) {
        return "engine array grow not found at exe+0x11653C0";
    }
    if (num == max) {
        grow(kGrowPacked, list, reinterpret_cast<int32_t*>(list + 12));
    }
    uint8_t* const grown = static_cast<uint8_t*>(read_ptr(list, 0));
    memcpy(&max, list + 12, sizeof(max));
    if (grown == nullptr || max <= num) {
        return "engine array grow failed";
    }
    memcpy(grown + static_cast<size_t>(num) * kPairBytes, pair, kPairBytes);
    const int32_t next = num + 1;
    memcpy(list + 8, &next, sizeof(next));
    return nullptr;
}

// Card 281, host: a round made at the claim's point from the PC's record, with the record's impact pair bound; or null
// with `why` set. Made through the pooler's original (no client relay, no launch note: the client fired this shot).
void* made_round(const Claim& c, const MadeRecord& rec, const char** why) {
    const void* const pawn = read_ptr(c.pc, kControllerPawn);
    const void* const world = pawn != nullptr ? g_actor_get_world(pawn) : nullptr;
    alignas(16) double t[12] = {}; // FTransform<double>: quat, translation (+pad), scale (+pad)
    t[3] = 1.0;
    t[4] = c.point.x;
    t[5] = c.point.y;
    t[6] = c.point.z;
    t[8] = t[9] = t[10] = 1.0;
    void* cls = const_cast<void*>(rec.init);
    void* const proj = world != nullptr && g_orig_c74_spawn != nullptr ? g_orig_c74_spawn(&cls, t, world) : nullptr;
    if (proj == nullptr) {
        *why = "pooler made no round";
        return nullptr;
    }
    c132_sent_drop(proj); // a pooled round handed out again: its old entries go, as detour_c74_spawn does
    c132_rounds_forget(proj);
    Round* const stale = round_find(proj);
    if (stale != nullptr) {
        *stale = Round{};
    }
    *why = rec.bound ? bind_pair(proj, rec.pair) : nullptr;
    if (*why != nullptr) {
        g_orig_c74_proj[2](proj);
        return nullptr;
    }
    return proj;
}

// Card 281, host: an NPC claim whose wait ran out takes a made round when its PC has a record for the weapon the copy
// holds now and the claim passes claim_check. True: served (apply_npc ran). False: `why` says why not.
bool serve_made(const Claim& c, const char** why) {
    const MadeRecord* const rec = record_find(c.pc);
    if (rec == nullptr) {
        *why = nullptr; // no record: today's drop
        return false;
    }
    C31Weak bound{};
    memcpy(&bound, rec->pair, sizeof(bound));
    void* const npc = c41_resolve_guid(c.pc, c.guid);
    double dist = 0.0, short_by = 0.0;
    *why = copy_weapon(c.pc) == nullptr || copy_weapon(c.pc) != c31_weak_get(rec->weapon) ? "weapon changed"
           : rec->bound && c31_weak_get(bound) == nullptr                                ? "bound object gone"
                                                                                         : claim_check(c, npc, &dist,
                                                                                                       &short_by);
    void* const proj = *why == nullptr ? made_round(c, *rec, why) : nullptr;
    if (proj == nullptr) {
        return false;
    }
    ++g_c157.made;
    log::line("card281: host t=%llu claim shot %u from pc %s served by a made round (init %s, impact pair %s) "
              "(%u made)",
              static_cast<unsigned long long>(now_ms()), c.seq, object_name(c.pc).text, object_name(rec->init).text,
              rec->bound ? "bound" : "none", g_c157.made);
    Round r{};
    r.proj = proj;
    r.pc = c.pc;
    r.pc_weak = c.pc_weak;
    r.init = rec->init;
    r.born = now_ms();
    r.state = RoundState::Flying;
    apply_npc(r, c);
    return true;
}

// Host: the claim takes its round; false while no round waits.
bool resolve(const Claim& c) {
    Round* const slot = oldest_open(c.pc);
    if (slot == nullptr) {
        return false;
    }
    record_round(*slot, "claim", c.seq); // card 281
    if (c.guid == 0 && slot->state == RoundState::Flying) {
        slot->state = RoundState::Miss; // its later hit replays (non-NPC) or goes back to the pool
        ++g_c157.misses;
        return true;
    }
    const Round r = *slot;
    *slot = Round{}; // out of the table first: OnHit and ReleaseToPool re-enter c157_forget
    if (c.guid != 0) {
        apply_npc(r, c);
        return true;
    }
    ++g_c157.misses;
    const bool replayed = replay_or_release(r);
    if (r.other_npc && line_ok()) {
        log::line("card157: host t=%llu miss claim shot %u from pc %s: the host's copy hit an NPC, round back to the "
                  "pool without damage (%s)", static_cast<unsigned long long>(now_ms()), c.seq,
                  object_name(c.pc).text, replayed ? "replayed" : "released");
    }
    return true;
}

void claims_step(uint64_t now) {
    for (uint32_t pass = 0; pass < kClaims; ++pass) { // arrival order: the oldest waiting claim first
        Claim* oldest = nullptr;
        for (Claim& c : g_c157.claims) {
            oldest = c.pc != nullptr && (oldest == nullptr || c.arrived < oldest->arrived) ? &c : oldest;
        }
        if (oldest == nullptr) {
            return;
        }
        const Claim c = *oldest;
        if (live_pc(c.pc, c.pc_weak) == nullptr) { // card 331: the claimer left; its rounds park and time out
            *oldest = Claim{};
            ++g_c157.orphans;
            log::line("WARN card331: host t=%llu HitClaim shot %u from pc %p: the PC is gone, claim dropped (%u so far)",
                      static_cast<unsigned long long>(now), c.seq, c.pc, g_c157.orphans);
            continue;
        }
        if (resolve(c)) {
            *oldest = Claim{};
            continue;
        }
        if (now - c.arrived <= kClaimMs) {
            return; // later claims of this PC wait behind it; another PC's wait too (rare, one tick)
        }
        *oldest = Claim{};
        const char* why = nullptr;
        if (c.guid != 0 && serve_made(c, &why)) { // card 281
            continue;
        }
        if (why != nullptr) {
            log::line("WARN card281: host t=%llu claim shot %u from pc %s: no made round: %s",
                      static_cast<unsigned long long>(now), c.seq, object_name(c.pc).text, why);
        }
        ++g_c157.orphans;
        log::line("WARN card157: host t=%llu HitClaim shot %u from pc %s (%s) found no round in %llu ms: dropped (%u "
                  "so far)", static_cast<unsigned long long>(now), c.seq, object_name(c.pc).text,
                  c.guid != 0 ? "NPC" : "miss", static_cast<unsigned long long>(kClaimMs), g_c157.orphans);
    }
}

void parked_step(uint64_t now) {
    for (Round& slot : g_c157.rounds) {
        if (slot.proj == nullptr || slot.state != RoundState::Parked || now - slot.parked_at <= kParkMs) {
            continue;
        }
        const Round r = slot;
        slot = Round{};
        const bool replayed = replay_or_release(r);
        g_c157.lost += r.other_npc ? 1u : 0u;
        if (r.other_npc || line_ok()) {
            log::line("%scard157: host t=%llu round %s of pc %s parked %llu ms with no claim: %s (%u lost on NPCs)",
                      r.other_npc ? "WARN " : "", static_cast<unsigned long long>(now), class_name(r.proj).text,
                      object_name(live_pc(r.pc, r.pc_weak)).text, static_cast<unsigned long long>(kParkMs),
                      replayed ? "own hit replayed" : "back to the pool", g_c157.lost);
        }
    }
}

// Host: the round waits for its claim with this contact kept (OnHit's hit, or the overlap's sweep result).
void park(Round* slot, void* hit_comp, void* other, void* other_comp, const Vec3* impulse, const void* hit) {
    record_round(*slot, "park", 0); // card 281
    slot->state = RoundState::Parked;
    slot->parked_at = now_ms();
    slot->hit_comp = hit_comp;
    c31_weak_set(slot->other, other);
    c31_weak_set(slot->other_comp, other_comp);
    slot->other_npc = is_npc(other);
    slot->impulse = impulse != nullptr ? *impulse : Vec3{};
    memcpy(slot->hit, hit, kHitBytes);
    ++g_c157.parked;
}

// Host: a server-copy player round reaching an NPC with no table entry deals the copy's damage (no-double-hit rule:
// must stay 0). The instigator is the shooter's pawn on a launched round.
void copy_check(const void* proj, const void* other, const char* path) {
    if (!is_npc(other) || shooter_of(proj, nullptr, kSideServerCopy) == nullptr) {
        return;
    }
    ++g_c157.copy_damage;
    if (line_ok()) {
        log::line("WARN card157: host t=%llu server-copy round %s reached NPC %s by %s outside the round table: the "
                  "copy's damage stands (%u)", static_cast<unsigned long long>(now_ms()), class_name(proj).text,
                  object_name(other).text, path, g_c157.copy_damage);
    }
}

// Host: an own-table round overlapping an NPC. True: handled (the original overlap does not run).
bool host_overlap(Round* slot, void* self, void* overlapped, void* other, void* other_comp, const uint8_t* hit) {
    if (slot->state == RoundState::Miss) {
        *slot = Round{};
        g_orig_c74_proj[2](self); // the client saw no NPC there: no damage
        return true;
    }
    if (slot->state == RoundState::Parked) {
        return true;
    }
    park(slot, overlapped, other, other_comp, nullptr, hit);
    ++g_c157.overlaps;
    if (note_event_cap(g_c157.park_lines, kParkLines, "card157 overlap parks")) {
        log::line("card157: host t=%llu round %s of pc %s parked on its overlap with %s (%s): the claim decides the "
                  "damage (%u overlap parks)", static_cast<unsigned long long>(now_ms()), class_name(self).text,
                  object_name(slot->pc).text, object_name(other).text, class_name(other_comp).text, g_c157.overlaps);
    }
    return true;
}

// Client: the own round's NPC overlap goes out as its claim; with no sweep, the round's place is the point.
void client_overlap(Round* slot, void* self, void* other, bool sweep, const uint8_t* hit) {
    alignas(16) uint8_t h[kHitBytes];
    memcpy(h, hit, kHitBytes);
    if (!sweep) {
        memset(h, 0, kHitBytes);
        const Vec3 at = c74_actor_loc(self);
        memcpy(h + kHitImpactPoint, &at, sizeof(at));
    }
    const Round r = *slot;
    *slot = Round{};
    ++g_c157.overlaps;
    client_claim(r, other, h);
}

} // namespace

// Both sides: AADProjectile::execOnOverlapBegin (the round's overlap; its body deals the hit delegates' damage).
ExecThunkFn g_orig_c157_overlap = nullptr;

void detour_c157_overlap(void* context, void* frame, void* result) {
    void* overlapped = nullptr;
    void* other = nullptr;
    void* other_comp = nullptr;
    bool sweep = false;
    const uint8_t* hit = nullptr;
    const bool args = frame != nullptr && overlap_args(frame, &overlapped, &other, &other_comp, &sweep, &hit);
    Round* const slot = args ? round_find(context) : nullptr;
    bool handled = false;
    if (args && slot == nullptr && !g_client_session) {
        copy_check(context, other, "overlap");
    } else if (slot != nullptr && is_npc(other)) {
        if (g_client_session) {
            client_overlap(slot, context, other, sweep, hit);
        } else {
            handled = host_overlap(slot, context, overlapped, other, other_comp, hit);
        }
    }
    if (!handled) {
        g_orig_c157_overlap(context, frame, result);
    }
}

namespace {

// Card 165 diag, host: a player round's launch with its shooter candidates, the first player pawn's controller and
// that controller's side; capped. Rounds no player fired (AI) are not logged.
void launch_diag(const void* proj, const void* shooter, const void* pc) {
    const void* const cand[3] = {shooter, shooter != nullptr ? weapon_pawn(shooter) : nullptr,
                                 proj != nullptr ? read_ptr(proj, kActorInstigator) : nullptr};
    int32_t pawn_at = -1;
    for (int32_t i = 0; i < 3 && pawn_at < 0 && g_player_character_class != nullptr; ++i) {
        pawn_at = cand[i] != nullptr && is_a(cand[i], g_player_character_class()) ? i : -1;
    }
    if (pawn_at < 0 || !note_event_cap(g_c157.launch_lines, kLaunchLines, "card165 launch diag")) {
        return;
    }
    const void* const ctrl = read_ptr(cand[pawn_at], kPawnController);
    log::line("card165: host t=%llu launch %s: shooter %s (%s), its owner %s (%s), instigator %s (%s); pawn is "
              "candidate %d, controller %s %p side %u (server copy %u): %s", static_cast<unsigned long long>(now_ms()),
              class_name(proj).text, object_name(cand[0]).text, class_name(cand[0]).text, object_name(cand[1]).text,
              class_name(cand[1]).text, object_name(cand[2]).text, class_name(cand[2]).text, pawn_at,
              object_name(ctrl).text, ctrl, sprint_side(ctrl), kSideServerCopy, pc != nullptr ? "noted" : "not noted");
}

} // namespace

// Card 281 (b187), test only, host: rounds of the client copy still to discard (`discard <n>`); game thread.
static uint32_t g_c281_discard = 0;

// Card 281, host: a just-noted client copy round becomes a Miss while discards are armed. A Miss round on an NPC goes
// back to the pool with no damage (c157_on_hit), so its claim waits out kClaimMs and takes the made-round path.
static void c281_discard_step(const void* proj) {
    Round* const r = g_client_session || g_c281_discard == 0 ? nullptr : round_find(proj);
    if (r == nullptr) {
        return;
    }
    r->state = RoundState::Miss;
    --g_c281_discard;
    log::line("card281: host t=%llu test discards round %s (launch %u, pc %s): %u discards left",
              static_cast<unsigned long long>(now_ms()), class_name(proj).text, r->seq, object_name(r->pc).text,
              g_c281_discard);
}

// Both sides, from c132_proj_launch after the pooler returned (an own launch, not a copy).
void c157_launch(void* proj, const void* shooter, const void* init) {
    if (proj == nullptr) {
        return;
    }
    const void* const pc = shooter_of(proj, shooter, g_client_session ? kSideClientOwn : kSideServerCopy);
    if (!g_client_session) {
        launch_diag(proj, shooter, pc);
    }
    if (pc == nullptr) {
        return;
    }
    ++g_c157.launches;
    round_add(proj, pc, g_client_session ? pl_shot_seq() : g_c157.launches, init);
    c281_discard_step(proj);
}

// Card 281 (b187), test only, host: `discard <n>` makes the host lose the client copy's next n rounds, so their claims
// can only be served by a made round (serve_made). Returns the command's failure text, or nullptr.
const char* c281_discard_arm(uint32_t n, uint32_t seq) {
    if (g_client_session) {
        return "discard is host only";
    }
    g_c281_discard = n;
    log::line("card281: host t=%llu cmd %u: the client copy's next %u rounds are discarded",
              static_cast<unsigned long long>(now_ms()), seq, n);
    return nullptr;
}

// Both sides: the projectile is back in the pool, or not a player round after all (a grenade, a vehicle's round).
// Card 165 diag: a host round forgotten before a claim took it logs who forgot it (capped, every one counted).
void c157_forget(const void* proj, const char* why) {
    Round* const r = proj != nullptr ? round_find(proj) : nullptr;
    if (r == nullptr) {
        return;
    }
    if (!g_client_session) {
        ++g_c157.forgets;
        if (note_event_cap(g_c157.forget_lines, kLaunchLines, "card165 forget diag")) {
            log::line("card165: host t=%llu round %s (launch %u, pc %p, state %u) forgotten %llu ms after its note by "
                      "%s (%u so far)", static_cast<unsigned long long>(now_ms()), class_name(proj).text, r->seq,
                      r->pc, static_cast<unsigned>(r->state), static_cast<unsigned long long>(now_ms() - r->born), why,
                      g_c157.forgets);
        }
    }
    *r = Round{};
}

// Both sides, first in detour_c132_on_hit. True: handled (host: parked, or a marked miss resolved), the caller returns.
// Client: the claim goes out and false is returned, so the client's own OnHit runs on.
bool c157_on_hit(void* self, void* hit_comp, void* other, void* other_comp, const Vec3* impulse, const void* hit) {
    Round* const slot = round_find(self);
    if (slot == nullptr && !g_client_session) {
        copy_check(self, other, "OnHit");
    }
    if (slot == nullptr || hit == nullptr) {
        return false;
    }
    if (g_client_session) {
        const Round r = *slot;
        *slot = Round{};
        client_claim(r, other, hit);
        return false;
    }
    if (slot->state == RoundState::Miss) {
        *slot = Round{};
        if (is_npc(other)) {
            g_orig_c74_proj[2](self); // the client saw no NPC there: no damage
        } else {
            g_orig_c132_on_hit(self, hit_comp, other, other_comp, impulse, hit);
        }
        return true;
    }
    if (slot->state == RoundState::Parked) {
        return true; // a second hit while parked: the first one is kept
    }
    park(slot, hit_comp, other, other_comp, impulse, hit);
    return true;
}

// Host: a client's HitClaim frame.
void c157_receive_claim(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    Claim c{};
    unsigned long long guid = 0;
    if (chan_text(s, text, sizeof(text)) <= 0 ||
        sscanf_s(text, "%u %llx %lf %lf %lf %lf %lf %lf %63s", &c.seq, &guid, &c.point.x, &c.point.y, &c.point.z,
                 &c.normal.x, &c.normal.y, &c.normal.z, c.bone, static_cast<unsigned>(sizeof(c.bone))) != 9) {
        log::line("WARN card157: host: bad HitClaim frame from pc %s", object_name(pc).text);
        return;
    }
    c.pc = pc;
    c31_weak_set(c.pc_weak, pc);
    c.guid = guid;
    c.arrived = now_ms();
    if (note_event_cap(g_c157.forget_lines, kLaunchLines, "card165 forget diag")) { // card 165 diag: both pointers
        uint32_t open = 0;
        uint32_t mine = 0;
        const void* other = nullptr;
        for (const Round& r : g_c157.rounds) {
            open += r.proj != nullptr ? 1 : 0;
            mine += r.proj != nullptr && r.pc == pc ? 1 : 0;
            other = r.proj != nullptr && r.pc != pc ? r.pc : other;
        }
        log::line("card165: host t=%llu HitClaim shot %u from pc %s %p: %u rounds noted, %u of this pc, other pc %p",
                  static_cast<unsigned long long>(c.arrived), c.seq, object_name(pc).text, pc, open, mine, other);
    }
    for (Claim& slot : g_c157.claims) {
        if (slot.pc == nullptr) {
            slot = c;
            return;
        }
    }
    ++g_c157.full;
    log::line("WARN card157: host t=%llu claim table full (%u): HitClaim shot %u from pc %s dropped",
              static_cast<unsigned long long>(c.arrived), kClaims, c.seq, object_name(pc).text);
}

// Card 157 diagnostic (run 1): ASTPlayerCharacter::OnDamageCaused, the shooter's hit marker path. Logs which side
// and pawn it runs for, the damage (+0x184) and the event's first 0x60 bytes as qwords, so the relay (run 2) knows
// which fields name host objects. Capped.
C157DamageCausedFn g_orig_c157_damage_caused = nullptr;
uint32_t g_c157_caused_lines = 0;

void detour_c157_damage_caused(void* self, const void* event) {
    g_orig_c157_damage_caused(self, event);
    c166_host_damage_caused(self, event); // card 166: a server copy's marker goes to its client
    if (self == nullptr || event == nullptr || !note_event_cap(g_c157_caused_lines, 30, "card157 OnDamageCaused")) {
        return;
    }
    uint64_t q[12] = {};
    memcpy(q, event, sizeof(q));
    const void* const pawn = static_cast<const uint8_t*>(self) - kCauserIface; // self is the causer interface
    const uint32_t s = sprint_side(pawn);
    log::line("card157: %s t=%llu OnDamageCaused on %s (%s) damage %.2f event %llx %llx %llx %llx %llx %llx %llx %llx "
              "%llx %llx %llx %llx", side_text(), static_cast<unsigned long long>(now_ms()), object_name(pawn).text,
              s == kSideHostOwn ? "host own" : s == kSideServerCopy ? "server copy" : s == kSideClientOwn ? "client own"
                                                                                                         : "other",
              read_f32(event, 0x184), q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8], q[9], q[10], q[11]);
}

// Client, per tick: the own pawn's Armour and Health as the client holds them, one line per change (capped), so a
// host hit line's "Armour a -> b" can be matched on the client (card 141 note: finding 19).
struct C157Vitals {
    float armour;
    float health;
    bool seen;
    uint32_t lines;
    const void* pawn;  // card 454: the pawn the values belong to
    uint64_t pawn_ms;  // card 454: when that pawn was first seen
    uint32_t shakes;   // card 454: log rows
};
C157Vitals g_c157_vitals{};
HurtShakeFn g_c454_hurt_shake = nullptr;

// Card 454: the game plays the hurt camera shake from ASTPlayerCharacter::HandleDamageEvent, which runs only where
// the damage event is raised (the host), and not for a server copy. The client plays it itself when its own pawn's
// Armour plus Health drops. No shake in the first seconds of a pawn: the card 448 rejoin restore sets Health then.
void c454_hurt_shake(const uint8_t* pawn, float lost, C157Vitals& v) {
    constexpr uint64_t kC454SettleMs = 6000;
    constexpr float kC454MinLoss = 0.5f;
    if (lost < kC454MinLoss || g_c454_hurt_shake == nullptr || now_ms() - v.pawn_ms < kC454SettleMs) {
        return;
    }
    g_c454_hurt_shake(const_cast<uint8_t*>(pawn), lost);
    if (note_event_cap(v.shakes, 8, "card454 client hurt shake")) {
        log::line("card454: client t=%llu own pawn lost %.1f (Armour + Health): PlayHurtCameraShake",
                  static_cast<unsigned long long>(now_ms()), static_cast<double>(lost));
    }
}

void c157_client_vitals(void* engine) {
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    const auto* const pawn = pc != nullptr ? static_cast<const uint8_t*>(read_ptr(pc, kControllerPawn)) : nullptr;
    if (pawn == nullptr || !is_a(pawn, g_player_character_class())) {
        return;
    }
    void* component = nullptr;
    const float health = c21_health(pawn, &component);
    const C48Armour a = component != nullptr ? c48_armour(component) : C48Armour{-1.0f, -1.0f};
    C157Vitals& v = g_c157_vitals;
    if (v.pawn != pawn) { // card 454: a new pawn starts a new record
        v.pawn = pawn;
        v.pawn_ms = now_ms();
        v.seen = false;
    }
    // Card 454: -1.0 is a value not read (no health component or set), never a loss.
    const bool both_read = v.seen && v.armour >= 0.0f && a.armour >= 0.0f && v.health >= 0.0f && health >= 0.0f;
    c454_hurt_shake(pawn, both_read ? (v.armour - a.armour) + (v.health - health) : 0.0f, v);
    const bool changed = !v.seen || fabsf(a.armour - v.armour) > 0.05f || fabsf(health - v.health) > 0.05f;
    if (changed && note_event_cap(v.lines, 60, "card157 client vitals")) {
        log::line("card157: client t=%llu own pawn Armour %.1f -> %.1f (max %.1f), Health %.1f -> %.1f",
                  static_cast<unsigned long long>(now_ms()), v.seen ? v.armour : -1.0f, a.armour, a.max_armour,
                  v.seen ? v.health : -1.0f, health);
    }
    v.armour = a.armour;
    v.health = health;
    v.seen = true;
}

// Host, per tick: claims meet their rounds, parked rounds time out. Client: the own pawn's vitals line.
void c157_step(void* engine) {
    if (g_client_session) {
        c157_client_vitals(engine);
        return;
    }
    const uint64_t now = now_ms();
    claims_step(now);
    parked_step(now);
}

// Both sides, map load: every noted projectile belongs to the world being torn down.
void c157_map_end() {
    if (g_c157.launches > 0 || g_c157.sent > 0) {
        log::line("card157: %s totals: %u rounds noted, %u claims sent (%u on NPCs), %u applied, %u refused, %u misses, "
                  "%u parked (%u on overlaps), %u lost, %u orphan claims, %u made rounds (%u rounds not recorded), %u table full, copy damage %u",
                  side_text(), g_c157.launches, g_c157.sent, g_c157.sent_npc, g_c157.applied, g_c157.refused,
                  g_c157.misses, g_c157.parked, g_c157.overlaps, g_c157.lost, g_c157.orphans, g_c157.made, g_c157.unrecorded, g_c157.full,
                  g_c157.copy_damage);
    }
    const uint64_t profile = g_c157.profile;
    g_c157 = C157State{};
    g_c157.profile = profile;
}

} // namespace ubw::hooks
