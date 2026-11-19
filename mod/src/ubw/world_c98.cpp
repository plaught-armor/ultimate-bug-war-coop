// Card 098: the host's world changes, measured. Every destroy, hide/show, Blueprint timeline start, attach, spawn and
// collision write is counted per (class, kind of change), with whether the actor is placed (net-startup) and whether
// it replicates. The first events of each row are logged with name, location and caller; the table is dumped by the
// `objectives` test command and at map change. The tally is log only.
// The world relay (second half): a world change that the engine cannot carry, because it is a Blueprint event on a
// placed actor that does not replicate, is relayed from the host by placed name plus location (WorldOp), journalled
// per map and replayed on a client's first Pong. Today one kind: a satchel building's ReceiveOnSatchelExploded (the
// bug hole's death; grenades and rifle fire do not reach it). The client calls the original on its nearest copy.

#include "ubw/hooks_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace ubw::hooks {

StaticClassFn g_c130_health_class = nullptr; // card 130 item 12
HealthIsDeadFn g_c130_is_dead = nullptr;     // card 130 item 12
HealthDamageBroadcastFn g_c131_broadcast_damage = nullptr; // card 131

C98DestroyFn g_orig_c98_destroy = nullptr;
C98HiddenFn g_orig_c98_hidden = nullptr;
C98SelfFn g_orig_c98_timeline_play = nullptr;
C98SelfFn g_orig_c98_timeline_from_start = nullptr;
C98SelfFn g_orig_c98_timeline_reverse = nullptr;
C98SelfFn g_orig_c98_timeline_reverse_from_end = nullptr;
C98SelfFn g_orig_c98_timeline_stop = nullptr;
C261RateFn g_orig_c261_set_play_rate = nullptr; // card 261
C98AttachFn g_orig_c98_attach = nullptr;
C98SatchelFn g_orig_c98_satchel = nullptr;
HealthDamageBroadcastFn g_orig_c163_broadcast = nullptr; // card 163
bool g_c107_copy_spawn = false; // card 107: see hooks_internal.h
uint32_t g_c132_refused = 0;     // card 132: client game destroys of card 104 copies refused this map

void c104_relay_destroy(const void* world, const void* actor); // card 104, defined with the world relay below

namespace {

constexpr int32_t kC98Rows = 192;       // (class, kind) rows per map
constexpr uint32_t kC98FirstLines = 2;  // event lines per row
constexpr uint32_t kC98Lines = 500;     // event lines per map
constexpr size_t kC98ClassChars = 64;


struct C98Row {
    char cls[kC98ClassChars];
    C98Kind kind;
    uint32_t count, placed, replicated;
};

struct C98State {
    C98Row row[kC98Rows];
    int32_t n;
    uint32_t lines, dropped;
};

C98State g_c98{}; // game thread

C98Row* c98_row(const char* cls, C98Kind kind) {
    for (int32_t i = 0; i < g_c98.n; ++i) {
        if (g_c98.row[i].kind == kind && strcmp(g_c98.row[i].cls, cls) == 0) {
            return &g_c98.row[i];
        }
    }
    if (g_c98.n >= kC98Rows) {
        ++g_c98.dropped;
        return nullptr;
    }
    C98Row& r = g_c98.row[g_c98.n++];
    snprintf(r.cls, sizeof(r.cls), "%s", cls);
    r.kind = kind;
    return &r;
}

} // namespace

bool c98_replicates(const void* actor) {
    return (static_cast<const uint8_t*>(actor)[kActorRepFlags] & 0x08u) != 0;
}

// Card 434 (diagnostic): AActor::NetCullDistanceSquared by reflection, as a distance in uu. -1 when not found.
double c434_cull_uu(const void* actor) {
    const uint8_t* const f = prop_find_prop(read_ptr(actor, kObjectClass), "NetCullDistanceSquared");
    const float sq = f != nullptr ? read_f32(actor, static_cast<size_t>(read_i32(f, kPropertyOffset))) : -1.0f;
    return sq >= 0.0f ? sqrt(static_cast<double>(sq)) : -1.0;
}

uint64_t c41_guid(const void* actor); // enemies.cpp

// Card 434 step 2 (diagnostic), host: an NPC the client may not have. b130 (FED_C2M1): 60 cue rows for warrior bugs
// and mechs went out with the world key (no NetGUID at the send) and 20 with a guid the client could not resolve,
// while every NPC class reads the default 15000 uu net cull distance at its spawn. The row reads what else keeps an
// actor off a client: RemoteRole, NetDormancy, the cull distance now (a blueprint may write it after the spawn) and
// the distance to each remote PC's pawn. Logged when the nearest remote pawn is at least min_far away (0: always).
// b133: the line cap is per map (c399b132a spent all 80 lines on FED_C3M1 and FED_C3M2, none on FED_C5M2).
// b134: FX rows have their own cap (c423b131a: 78 of the 80 lines were FX rows on the spawn frame).
// b135 (step 3): per remote PC, whether its connection holds an actor channel for the owner (channel) and whether
// the owner's own IsNetRelevantFor passes for the server's viewer of that PC (relevant: the PC, its view target, its
// view point); for a pawn, its movement base. APawn::IsNetRelevantFor hands the answer to the base actor when the
// base is a skeletal mesh component, and an NPC standing on an actor the client does not have is not relevant.
C434FindChannelFn g_c434_find_channel = nullptr;

// Host, card 434: whether pc's connection holds an actor channel for `actor`. The engine closes the channel of an
// actor that stays not relevant to the client (past the net cull distance, 15000 uu for every NPC class) and the
// client destroys its copy, so a row by NetGUID sent after that finds no NPC (c423b139a: 91 client Anim misses,
// all on a channel closed with reason 3, relevancy). True when it cannot be told (no connection, no symbol).
bool c434_has_channel(const void* pc, const void* actor) {
    void* const conn = pc != nullptr ? read_ptr(pc, kPcNetConnection) : nullptr;
    if (conn == nullptr || actor == nullptr || g_c434_find_channel == nullptr) {
        return true;
    }
    C31Weak weak{};
    c31_weak_set(weak, const_cast<void*>(actor));
    return g_c434_find_channel(conn, &weak) != nullptr;
}
static uint32_t g_c434_lines[2] = {}; // [0] Anim rows, [1] FX rows; game thread; cleared in c98_world_map_end
void c434_owner_row(const void* owner, const char* why, double min_far, bool fx) {
    const uint32_t kC434Lines = fx ? 20u : 80u; // per map
    uint32_t& lines = g_c434_lines[fx ? 1 : 0];
    const void* const world = owner != nullptr && lines < kC434Lines ? g_actor_get_world(owner) : nullptr;
    if (world == nullptr) {
        return;
    }
    const Vec3 at = actor_location(owner);
    char far_text[320] = "";
    size_t used = 0;
    double nearest = -1.0;
    const auto relevant_fn = reinterpret_cast<C434RelevantFn>(read_ptr(read_ptr(owner, 0), kActorVtIsNetRelevantFor));
    for (int s = 0; s < kC17Slots && used + 100 < sizeof(far_text); ++s) {
        void* const pc = g_get_player_controller(world, s);
        const void* const pawn = pc != nullptr && sprint_side(pc) == kSideServerCopy ? read_ptr(pc, kControllerPawn)
                                                                                    : nullptr;
        if (pawn == nullptr) {
            continue;
        }
        const double d = vec_dist(at, actor_location(pawn));
        nearest = nearest < 0.0 || d < nearest ? d : nearest;
        // The host's FNetViewer measures the cull distance from this PC's view point, not from its pawn.
        Vec3 view{};
        Vec3 view_rot{};
        if (g_player_view_point != nullptr) {
            g_player_view_point(pc, &view, &view_rot);
        }
        void* const conn = read_ptr(pc, kPcNetConnection);
        int channel = -1;
        if (conn != nullptr && g_c434_find_channel != nullptr) {
            C31Weak weak{};
            c31_weak_set(weak, const_cast<void*>(owner));
            channel = g_c434_find_channel(conn, &weak) != nullptr ? 1 : 0;
        }
        const void* const target = g_c399_get_view != nullptr ? g_c399_get_view(pc) : nullptr;
        const int relevant = g_player_view_point != nullptr
                                 ? (relevant_fn(owner, pc, target != nullptr ? target : pawn, &view) ? 1 : 0)
                                 : -1;
        used += static_cast<size_t>(snprintf(far_text + used, sizeof(far_text) - used,
                                             " slot %d %.0f uu (view point %.0f uu) channel %d relevant %d", s, d,
                                             g_player_view_point != nullptr ? vec_dist(at, view) : -1.0, channel,
                                             relevant));
    }
    if (nearest < min_far || !note_event_cap(lines, kC434Lines, "card434 owner")) {
        return; // no remote pawn, or one nearer than asked
    }
    const auto* const a = static_cast<const uint8_t*>(owner);
    const uint8_t* const dorm = prop_find_prop(read_ptr(owner, kObjectClass), "NetDormancy");
    void* const root = read_ptr(owner, kActorRootComponent);
    char base_text[224] = "base none";
    if (g_pawn_class != nullptr && is_a(owner, g_pawn_class())) {
        const auto base_fn = reinterpret_cast<C434BaseFn>(read_ptr(read_ptr(owner, 0), kPawnVtGetMovementBase));
        const void* const base = base_fn(owner);
        const auto* const b = base != nullptr ? static_cast<const uint8_t*>(read_ptr(base, kComponentOwner)) : nullptr;
        if (base != nullptr) {
            snprintf(base_text, sizeof(base_text), "base %s (%s) of %s (%s) (base flags 0x%02x, RemoteRole %u)",
                     object_name(base).text, class_name(base).text, object_name(b).text, class_name(b).text,
                     b != nullptr ? b[kActorHiddenByte] : 0u, b != nullptr ? b[kActorRemoteRole] : 0u);
        }
    } else {
        snprintf(base_text, sizeof(base_text), "base n/a (not a pawn)");
    }
    log::line("card434: host %s: %s (%s) guid 0x%llx, RemoteRole %u (0 = not replicated), NetDormancy %d, net cull now "
              "%.0f uu, actor flags 0x%02x (0x80 bHidden), root collision %u (APawn::IsNetRelevantFor, exe+0x3F67330: "
              "hidden with root collision 0 is not relevant), %s, at (%.0f, %.0f, %.0f), from the remote pawns:%s",
              why, object_name(owner).text, class_name(owner).text, static_cast<unsigned long long>(c41_guid(owner)),
              a[kActorRemoteRole], dorm != nullptr ? a[read_i32(dorm, kPropertyOffset)] : -1, c434_cull_uu(owner),
              a[kActorHiddenByte], root != nullptr && g_collision_enabled != nullptr ? g_collision_enabled(root) : 255u,
              base_text, at.x, at.y, at.z, far_text);
}

// One world change on actor (never null-checked by the caller). detail: the component or attach parent, or null.
void c98_note(C98Kind kind, const void* actor, const void* detail, unsigned long long caller) {
    if (actor == nullptr) {
        return;
    }
    const ObjectName cls = class_name(actor);
    C98Row* const r = c98_row(cls.text, kind);
    if (r == nullptr) {
        return;
    }
    const bool placed = c50_placed(actor);
    const bool rep = c98_replicates(actor);
    ++r->count;
    r->placed += placed ? 1u : 0u;
    r->replicated += rep ? 1u : 0u;
    if (r->count > kC98FirstLines || !note_event_cap(g_c98.lines, kC98Lines, "card098 world events")) {
        return;
    }
    const Vec3 at = actor_location(actor);
    log::line("card098: %s %s %s %s at (%.0f, %.0f, %.0f) placed %d replicates %d detail %s caller exe+0x%llX; net "
              "cull %.0f uu", side(),
              kC98KindText[static_cast<int>(kind)], cls.text, object_name(actor).text, at.x, at.y, at.z,
              placed ? 1 : 0, rep ? 1 : 0, object_name(detail).text, caller, c434_cull_uu(actor));
}

uint32_t c98_class_changes(const char* cls) {
    uint32_t mask = 0;
    for (int32_t i = 0; i < g_c98.n; ++i) {
        mask |= strcmp(g_c98.row[i].cls, cls) == 0 ? 1u << static_cast<uint32_t>(g_c98.row[i].kind) : 0u;
    }
    return mask;
}

// The table, one line per (class, kind).
void c98_dump(const char* why) {
    c137_change_dump(why); // card 137 2a
    c137_gib_dump(why);    // card 137 6b
    log::line("card098: %s tally (%s): %d rows, %u dropped", side(), why, g_c98.n, g_c98.dropped);
    for (int32_t i = 0; i < g_c98.n; ++i) {
        const C98Row& r = g_c98.row[i];
        log::line("card098: %s tally %s %s n=%u placed=%u replicated=%u", side(), r.cls,
                  kC98KindText[static_cast<int>(r.kind)], r.count, r.placed, r.replicated);
    }
}

// Card 346 (b31 crash): a destroyed camera manager's PCOwner (APlayerCameraManager+0x2A8, read by UpdateCamera at
// 0x144065FC4), so the log says whose camera went on a seamless arrive. Few per map: no cap.
constexpr size_t kCameraManagerPcOwner = 0x2A8;

void c346_camera_destroy_line(const void* actor, unsigned long long caller) {
    if (actor == nullptr || strstr(class_name(actor).text, "CameraManager") == nullptr) {
        return;
    }
    const void* const owner = read_ptr(actor, kCameraManagerPcOwner);
    log::line("card346: %s t=%llu camera manager %p (%s) destroyed, PCOwner %p class %s, caller exe+0x%llX", side(),
              static_cast<unsigned long long>(GetTickCount64()), actor, class_name(actor).text, owner,
              class_name(owner).text, caller);
}

// UWorld::DestroyActor(AActor*, bNetForce, bShouldModifyLevel): AActor::Destroy, K2_DestroyActor and life span end
// all come here.
// Card 132: on the client a card 104 copy of a host actor ends only by the host's 'D' row (c104_client_destroy calls
// g_orig_c98_destroy directly, past this detour). Any destroy that reaches here for a copy is the client's own game
// logic (seen: ASTCustomPickupBase::OnSignificanceChanged's runtime-pickup cleanup 4 ms after a late-join spawn), so
// it is refused and the host stays the one that decides.
bool detour_c98_destroy(void* world, void* actor, bool net_force, bool modify_level) {
    const unsigned long long caller = exe_rva(_ReturnAddress());
    uint32_t host_id = 0;
    if (g_client_session && c104_copy_host_id(actor, host_id)) {
        static uint32_t last_logged = UINT32_MAX;
        ++g_c132_refused;
        if (host_id != last_logged) {
            last_logged = host_id;
            log::line("card132: client kept host copy %s host %u (game destroy refused, caller exe+0x%llX)",
                      object_name(actor).text, host_id, caller);
        }
        return false;
    }
    c218_destroy_line(actor, caller); // card 218 diagnostic, before the original
    c346_camera_destroy_line(actor, caller); // card 346 b31: whose camera manager goes
    c98_note(C98Kind::Destroy, actor, nullptr, caller);
    c137_client_change(actor, "destroyed", 0, 1, caller); // card 137 2a
    c104_relay_destroy(world, actor); // card 104: a relayed copy's end
    if (!g_client_session) {
        c101_forget(actor); // card 105: a destroyed server copy's mantle window slot
        c311_host_forget(actor); // card 311: a range target's Activate row
        c125_host_forget(actor); // card 471: a destroyed turret's row slot and 'T' journal row
    }
    return g_orig_c98_destroy(world, actor, net_force, modify_level);
}

void c98_host_hide(const void* actor, bool hidden); // below

void detour_c98_hidden(void* actor, bool hidden) {
    const bool was = actor != nullptr && (static_cast<const uint8_t*>(actor)[kActorHiddenByte] & kActorHiddenBit) != 0;
    c98_note(hidden ? C98Kind::Hide : C98Kind::Show, actor, nullptr, exe_rva(_ReturnAddress())); // calls, not changes
    g_orig_c98_hidden(actor, hidden);
    c137_client_change(actor, "bHidden", was ? 1 : 0, hidden ? 1 : 0, exe_rva(_ReturnAddress())); // card 137 2a
    if (!g_client_session && actor != nullptr && was != hidden) {
        c98_host_hide(actor, hidden);
    }
}

void detour_c98_timeline_play(void* timeline) {
    c98_note(C98Kind::Timeline, read_ptr(timeline, kComponentOwner), timeline, exe_rva(_ReturnAddress()));
    g_orig_c98_timeline_play(timeline);
    c98_host_timeline(timeline, C98TimelineOp::Play); // card 130 item 2
}

void detour_c98_timeline_from_start(void* timeline) {
    c98_note(C98Kind::Timeline, read_ptr(timeline, kComponentOwner), timeline, exe_rva(_ReturnAddress()));
    g_orig_c98_timeline_from_start(timeline);
    c98_host_timeline(timeline, C98TimelineOp::PlayFromStart);
}

// Card 130 item 2: the rest of the timeline's play controls, relayed the same way (a target that pops up with Play
// goes down with Reverse). Not tallied: card 098's row counts starts only.
void detour_c98_timeline_reverse(void* timeline) {
    g_orig_c98_timeline_reverse(timeline);
    c98_host_timeline(timeline, C98TimelineOp::Reverse);
}

void detour_c98_timeline_reverse_from_end(void* timeline) {
    g_orig_c98_timeline_reverse_from_end(timeline);
    c98_host_timeline(timeline, C98TimelineOp::ReverseFromEnd);
}

void detour_c98_timeline_stop(void* timeline) {
    g_orig_c98_timeline_stop(timeline);
    c98_host_timeline(timeline, C98TimelineOp::Stop);
}

// Card 261: the play rate a Blueprint sets before it plays (BP_SplineRiderMonorail_C: spline length / speed), relayed
// the same way. Without it the client's copy plays at its default rate and reaches the end long before the host's.
void detour_c261_set_play_rate(void* timeline, float rate) {
    g_orig_c261_set_play_rate(timeline, rate);
    c261_host_rate(timeline, rate);
}

bool detour_c98_attach(void* actor, void* parent, const void* rules, uint64_t socket) {
    c98_note(C98Kind::Attach, actor, parent != nullptr ? read_ptr(parent, kComponentOwner) : nullptr,
             exe_rva(_ReturnAddress()));
    return g_orig_c98_attach(actor, parent, rules, socket);
}

namespace {

constexpr int32_t kC98WorldRows = 1024;   // world relay journal rows per map (card 105: also live 'P' rows; card 130:
                                          // FED_C1M1 alone spawns 81 runtime pickups; card 132: 'X' rows, one per
                                          // persistent actor whose state changed; card 308: 512 filled on FED_C2M1)
constexpr uint32_t kC98WorldLines = 100;  // world relay lines per map
constexpr uint32_t kC163DamageKill = 2;  // card 163: the 'S' value of a satchel building's damage kill
constexpr char kC98KindSatchel = 'S';     // WorldOp kind: ReceiveOnSatchelExploded(value); card 163: value
                                          // kC163DamageKill is the Blueprint's damage kill instead
constexpr char kC98KindSpawn = 'P';       // card 104: a copy of class path `name` at x y z; value = host object index
constexpr char kC98KindDestroy = 'D';     // card 104: destroy the copy of host object index `value` (name "-")
constexpr char kC98KindKill = 'K';       // card 130 item 12: a placed killable building died; value = 0
constexpr char kC98KindTimeline = 'L';    // card 130 item 2: "<placed name>|<timeline name>", value = C98TimelineOp
constexpr char kC98KindRate = 'B';        // card 261: "<placed name>|<timeline name>", value = the play rate's float
                                          // bits; replayed before every other row (the Blueprint sets it before Play)
constexpr uint32_t kC261Lines = 24;       // card 261: rate lines per side per map
constexpr char kC98KindExtPack = 'x';     // card 420: "x n hash value hash value ...", the join replay's ext values
                                          // by name hash (ext_c132.cpp); not a "kind name x y z value" row
constexpr char kC98KindExt = 'X';         // card 132 step 2d: a kC132Ext actor's state (see ext_c132.cpp)
constexpr char kC98KindRest = 'M';        // card 132 note 2: a card 104 copy came to rest at x y z; name = "pitch,yaw,roll",
                                          // value = host object index (one row per actor: the latest rest)
constexpr char kC98KindHide = 'H';        // hb46: a placed, unreplicated actor's bHidden changed in play; value = hidden
constexpr char kC98KindReplay = 'R';      // hb55: journal replay bracket, value 1 before the rows and 0 after (name "-")
constexpr char kC98KindTurret = 'T';      // card 125: a placed turret's state, value = c125 packed state (see turret_sync)
constexpr char kC98KindPlaced = 'A';      // card 140: a placed, unreplicated actor host logic moved stands still at x y z;
                                          // name = "<placed name>|<load x,y,z>|<pitch,yaw,roll>", value 0
constexpr char kC98KindEnable = 'E';     // card 145: a placed pickup's bEnabled changed on the host; value = enabled
                                          // (no row means enabled, the level's default)
constexpr char kC98KindCopyEnable = 'N'; // card 145: a card 104 pickup copy's bEnabled; name "-", value = host object
constexpr char kC98KindTravel = 'V';     // card 290: the host leaves the map by its own travel; name = reason
constexpr char kC98KindSecret = 'G';     // card 289: a placed secret the host's take credited; value = host count
constexpr char kC98KindClosure = 'F';    // card 300: a one-way closure presence the host's all-players gate closed
                                          // index, enabled = x (0 or 1; y and z 0)
constexpr char kC98KindActivate = 'C';   // card 311: a range target's card 065 Activate; name "<placed owner>|<component
                                          // object name>", value = reset. Journal only (card 065 sends the live frame);
                                          // replayed before the timeline rows, while the host's component is active
constexpr char kC98KindC408 = 'Q';       // card 408: a flak or plasma bug building's timer event ran on the host; name =
                                          // placed building, value = c408 packed event (satchel_c408.cpp). Live only, not
                                          // journalled
constexpr uint32_t kC145UnrelayedLines = 20; // host: lines for a runtime pickup's change with no relay row
constexpr uint16_t kC104StillTicks = 30;  // card 132 note 2: ticks under kC104StillUu each that count as rest (no sleep)
constexpr double kC104StillUu = 0.5;      // uu per tick
constexpr double kC104KickUu = 5.0;       // uu from the rest point: awake again, the next rest is a new row
constexpr double kC104SampleUu = 10.0;    // card 140: an awake body this far from its last row sends a new one
                                          // (card 287: 50 uu moved the client's copy in visible steps)
constexpr uint32_t kC104SampleMs = 100;   // card 140: at most one moving row per actor per this many ms (card 287: 200)
constexpr uint32_t kC104RestLines = 40;   // client rest lines per session
constexpr int32_t kC104RestClasses = 32;  // host: classes named in a first-rest line
constexpr size_t kC104RelativeRotation = 0x158; // USceneComponent::RelativeRotation (remote_body.cpp, card 034a); a
                                                // simulating root has no parent, so it is the world rotation
constexpr int32_t kC104Copies = 256;      // card 104: relayed actors alive at once, per side (card 105: + pod drops;
                                          // card 130: 64 filled on FED_C1M1's CTF laser drops)

// One relayed world change. The host's actor pointer is the journal key (compared only, never read after the map).
struct C98World {
    const void* actor;
    char name[kAssetPathChars]; // a placed name, or (card 104) a class path
    Vec3 at;
    char kind;
    uint32_t value;
};

struct C98Worlds {
    C98World row[kC98WorldRows];
    int32_t n;
    int32_t peak;      // card 308: the most rows held on this map
    bool full_logged;  // card 308: the first journal-full row of the map carries the kind counts
    uint32_t lines, dropped, sent, applied, missed;
    uint32_t timeline_state, timeline_gone, timeline_lines; // card 185: replayed 'L' rows
    uint32_t rate_lines;                                    // card 261
    uint32_t c256_host_lines, c256_client_lines;            // card 256: first-rest rows, kC104Copies each
    uint32_t c317_lines;                                    // card 317: motion rows, kC317Lines
    uint32_t c317_stop_lines;                               // card 317 step 2: client stop rows, kC317StopLines
};

// Card 185: a replayed 'L' row's value. The op in the low bits (a live row's value is the op alone), then the host
// timeline's state at the replay: playing, reversing and its playback position in ms. kC185EndByOp: the host's
// timeline was not found at the replay, so the client sets the op's end (its length for Play / PlayFromStart, 0 for
// Reverse / ReverseFromEnd), not playing.
constexpr uint32_t kC185OpMask = 0x0F;
constexpr uint32_t kC185State = 0x10;
constexpr uint32_t kC185Playing = 0x20;
constexpr uint32_t kC185Reversing = 0x40;
constexpr uint32_t kC185EndByOp = 0x80;
constexpr uint32_t kC185PosShift = 8;
constexpr uint32_t kC185PosMaxMs = 0xFFFFFF; // 24 bits, 4.6 h
constexpr uint32_t kC185Lines = 24;          // per side per map
constexpr int32_t kC313MaxAgeMs = 2000;      // card 313: a replayed state older than this is applied as sent (WARN)

using C185SetPosFn = void (*)(void* timeline, float position, bool fire_events, bool fire_update);
using C185FloatFn = float (*)(const void* timeline);
using C185BoolFn = bool (*)(const void* timeline);

// Card 313: client. The host's clock at its replay build, from the opening 'R' row's x ("R - <ms> 0 0 1"): every
// replayed row is sampled inside that one call, so one time covers them. Set only between the two 'R' rows.
struct C313Replay {
    uint32_t sampled_ms; // the host's GetTickCount64() & kChanStampMsMask
    bool set;            // false: no time in the bracket (a host before card 313) or no replay in progress
};

// Card 315: a looping timeline's phase, set once by the card 185 replay, is lost on the next long frame (the engine
// caps one frame's delta time, so a 1912 ms stall cost the client 1.56 s of phase, c315b167a). Every kC315ScanMs the
// host repeats the state of each journalled ('L') timeline that is playing and looping: an 'O' header "O - <host ms>
// 0 0 <rows>" (the sample time of the rows after it, as card 313's replay bracket), then one row per timeline,
// "O <placed name>|<timeline name> x y z value", value = kC185Playing, kC185Reversing and the position in ms above
// kC185PosShift. The client ages the position the card 313 way and sets it only when its own is more than kC315OffMs
// off, wrap-aware; it never stops or starts the timeline. Not journalled: the next scan serves a late joiner.
constexpr char kC98KindPhase = 'O';
constexpr ULONGLONG kC315ScanMs = 3000;
constexpr double kC315OffMs = 100.0;
constexpr uint32_t kC315Lines = 24;       // correction and WARN lines per side per map
constexpr int32_t kC315Timelines = 64;    // client: distinct timelines counted per map
struct C315State {
    // host
    ULONGLONG due;
    uint32_t scans, rows, chars, host_lines;
    // client
    C313Replay sample; // the last 'O' header's host time
    const void* seen[kC315Timelines]; // compared only
    int32_t nseen;
    uint32_t received, corrections, in_sync, unaged, missed, not_playing, lines;
    double largest_ms;
};

template <class Fn>
Fn c185_fn(const offsets::Symbol& s) {
    return reinterpret_cast<Fn>(const_cast<uint8_t*>(g_exe_base + s.rva));
}

C98Worlds g_c98w{}; // game thread

// Card 132 note 2: host. A relayed actor's rest state. Rest is physics asleep (RigidBodyIsAwake false), or, for a
// body the solver never puts to sleep, kC104StillTicks ticks in a row moving under kC104StillUu each. A rested actor
// that moves kC104KickUu from its rest point is awake again, and its next rest is a new row.
struct C104Rest {
    Vec3 last;     // location at the last tick
    Vec3 rest_at;  // location of the last rest row
    uint16_t still; // ticks in a row under kC104StillUu
    bool rested;
    uint32_t row_ms; // GetTickCount64 of the last row ('M'), for kC104SampleMs
    uint32_t spawn_ms;   // card 256: GetTickCount64 of the 'P' row
    uint16_t rows;       // card 256: 'M' rows sent
    bool first_logged;   // card 256: the first-rest row is written
    uint32_t heal_round; // card 256: digest round + 1 of the last rest row sent again (c256_heal_rest)
    bool c317_logged;    // card 317: the first-rest motion row is written
    bool sampled;        // card 317 step 2: a non-simulating root sent moving samples since its last rest row
};

// Card 104. Host: the actors relayed as copies (compared only; card 132 note 2 reads one only through its weak pointer,
// which is null once the actor is gone). Client: host object index to our copy.
struct C104Relayed {
    const void* actor[kC104Copies];
    C31Weak weak[kC104Copies]; // card 132 note 2
    C104Rest rest[kC104Copies];
    int32_t n, peak; // card 130: peak alive, logged at map change
};
struct C104Copy {
    uint32_t host_id;
    void* actor; // read only after c104_in_world finds it in a level's actor list
    bool rest_logged; // card 256: the first rest row's line is written
    bool c317_logged; // card 317: the first-rest motion row is written
};
struct C104Copies {
    C104Copy row[kC104Copies];
    int32_t n;
};
C104Relayed g_c104_relayed{}; // game thread

// Card 140 run 8 (c140h): host. Relayed actors whose 'P' row waits for the next tick. A Blueprint SpawnActor node
// spawns deferred: UWorld::SpawnActor returns the actor at the origin and FinishSpawning places it later in the same
// Blueprint call, after the collision handling (AdjustIfPossibleButAlwaysSpawn) may have pushed it out of what it
// overlaps. The spawn transform was then not where the actor stands (the resupply rack 218 uu off, its pickups 150 uu
// low), and every Blueprint the copy runs at its own BeginPlay (the objective marker moved onto the rack) ran there.
// Membership is the state: an actor leaves the set when its row goes or it ends first.
struct C104Pending {
    const void* actor[kC104Copies];
    int32_t n;
    uint32_t sent, ended; // logged at map change
    uint32_t samples;     // card 140: moving rows (c104_sample_moving), logged at map change
};
C104Pending g_c104_pending{}; // game thread

C104Copies g_c104_copies{};   // game thread
bool g_c98_replaying = false; // client: between the host's journal replay brackets (hb55); game thread
C313Replay g_c313_replay{};   // client: card 313; game thread
C315State g_c315{};           // card 315: host scan and client corrections; game thread

int c98_world_text(const C98World& r, char* out, size_t cap) {
    return snprintf(out, cap, "%c %s %.0f %.0f %.0f %u", r.kind, r.name, r.at.x, r.at.y, r.at.z, r.value);
}

bool c98_world_line() {
    return note_event_cap(g_c98w.lines, kC98WorldLines, "card098 world relay");
}

// The journal row of this actor and kind, or null.
C98World* c98_journal_row(const void* actor, char kind) {
    for (int32_t i = 0; i < g_c98w.n; ++i) {
        if (g_c98w.row[i].actor == actor && g_c98w.row[i].kind == kind) {
            return &g_c98w.row[i];
        }
    }
    return nullptr;
}

// Card 308: the journal's rows by kind now, as "K:n" pairs in out ("-" when empty).
void c98_kind_counts(char* out, size_t cap) {
    uint32_t count[128] = {};
    for (int32_t i = 0; i < g_c98w.n; ++i) {
        ++count[static_cast<uint8_t>(g_c98w.row[i].kind) & 0x7Fu];
    }
    size_t at = 0;
    out[0] = '\0';
    for (int32_t k = 0x21; k < 0x7F && at < cap; ++k) {
        if (count[k] != 0) {
            const int w = snprintf(out + at, cap - at, "%s%c:%u", at == 0 ? "" : " ", static_cast<char>(k), count[k]);
            at += w > 0 ? static_cast<size_t>(w) : 0;
        }
    }
    if (at == 0) {
        snprintf(out, cap, "-");
    }
}

// Host: the row into the journal (one row per actor and kind, the last value wins). Null when the journal is full.
C98World* c98_journal_put(const C98World& row) {
    C98World* r = c98_journal_row(row.actor, row.kind);
    if (r == nullptr && g_c98w.n < kC98WorldRows) {
        r = &g_c98w.row[g_c98w.n++];
        g_c98w.peak = g_c98w.n > g_c98w.peak ? g_c98w.n : g_c98w.peak;
    }
    g_c98w.dropped += r == nullptr ? 1u : 0u;
    if (r != nullptr) {
        *r = row;
    }
    return r;
}

// Host: journal the change (one row per actor and kind, the last value wins) and send it to every ponged client.
// Card 105: a live card 104 copy ('P') is journalled too, and its row goes when the actor does (c98_unjournal).
// Card 125: `quiet` rows (turret state, up to 10 per second per turret) log no line of their own.
C95Fanout c98_host_world(const C98World& row, const void* world, bool journal, bool quiet = false) {
    const C98World* const r = journal ? c98_journal_put(row) : nullptr;
    char text[kAssetPathChars + 64];
    c98_world_text(row, text, sizeof(text));
    const C95Fanout f = c95_send_all(world, ChanOp::WorldOp, text);
    g_c98w.sent += static_cast<uint32_t>(f.sent);
    // Card 256: a failed send and a full journal are logged always, outside the line cap.
    // Card 346: during the card 332 teardown every send is refused and counted in the teardown row: plain, not WARN.
    const bool teardown = f.sent != f.to && c332_on();
    const bool warn = (f.sent != f.to && !teardown) || (journal && r == nullptr);
    if (warn || teardown || (!quiet && c98_world_line())) {
        log::line("%scard098: host t=%llu world %s: sent to %d of %d clients%s%s", warn ? "WARN " : "",
                  static_cast<unsigned long long>(GetTickCount64()), text, f.sent, f.to,
                  teardown ? " (card 332 teardown)" : "", r != nullptr || !journal ? "" : " (journal full: not replayed)");
    }
    if (journal && r == nullptr && !g_c98w.full_logged) { // card 308: what holds the slots, once per map
        g_c98w.full_logged = true;
        char kinds[256];
        c98_kind_counts(kinds, sizeof(kinds));
        log::line("WARN card308: host t=%llu world journal full (%d of %d rows): kinds %s",
                  static_cast<unsigned long long>(GetTickCount64()), g_c98w.n, kC98WorldRows, kinds);
    }
    return f; // card 289: the caller's own line names its clients
}

// Card 105: host. The actor's journal row of this kind goes (its 'D' follows; a late joiner gets neither).
void c98_unjournal(const void* actor, char kind) {
    for (int32_t i = 0; i < g_c98w.n; ++i) {
        if (g_c98w.row[i].actor == actor && g_c98w.row[i].kind == kind) {
            g_c98w.row[i] = g_c98w.row[--g_c98w.n];
            return;
        }
    }
}

bool c98_send_one(void* pc, const char* text) {
    static wchar_t wire[kChanTextFrame];
    const WireString w = chan_text_frame(ChanOp::WorldOp, text, wire);
    return w.num > 0 && chan_send_client(pc, w);
}

// Host: the 'S' row of a placed, non-replicated satchel building, false (row untouched past its name) for any other.
// The row is filled before the event runs (the event may destroy the actor).
bool c98_satchel_row(const void* building, uint32_t value, C98World& row) {
    const bool relay = building != nullptr && c50_placed(building) && net_mode(building) == kNetModeListenServer &&
                       !c98_replicates(building) && c50_name(building, row.name, sizeof(row.name)) > 0;
    if (relay) {
        row.actor = building;
        row.at = actor_location(building);
        row.kind = kC98KindSatchel;
        row.value = value;
    }
    return relay;
}

bool g_c163_in_satchel = false; // host: inside ReceiveOnSatchelExploded (its own self damage is not a damage kill)

} // namespace

// A placed building Blueprint's "Destroyed" bool (BP_KillableBuilding_Base; card 163: BP_SatchelBuilding_Base, set by
// its Become Destroyed event on both of its kill paths); false for a class with no such field.
bool c98_bp_destroyed(const void* building) {
    const uint8_t* const f = prop_find_prop(read_ptr(building, kObjectClass), "Destroyed");
    return f != nullptr && prop_bool_ok(f) &&
           (static_cast<const uint8_t*>(building)[read_i32(f, kPropertyOffset) + f[kBoolByteOffset]] &
            f[kBoolFieldMask]) != 0;
}

// Both sides. Host, on a placed, non-replicated building: the change is captured before the original (the event may
// destroy the actor) and relayed after it. The client's relayed calls go to the original directly, not here.
void detour_c98_satchel(void* building, bool premature) {
    C98World row{};
    const bool relay = c98_satchel_row(building, premature ? 1u : 0u, row);
    const void* const world = relay ? g_actor_get_world(building) : nullptr;
    if (c98_world_line()) {
        log::line("card098: %s t=%llu ReceiveOnSatchelExploded(%d) on %s (%s), relay %d, caller exe+0x%llX", side(),
                  static_cast<unsigned long long>(GetTickCount64()), premature ? 1 : 0, object_name(building).text,
                  class_name(building).text, relay ? 1 : 0, exe_rva(_ReturnAddress()));
    }
    if (g_client_session && building != nullptr && c50_placed(building) && !c98_replicates(building)) {
        c133_client_blocked(building, premature); // card 133: the host's 'S' row is the only explode here
        return;
    }
    const bool was_in = g_c163_in_satchel;
    g_c163_in_satchel = true;
    g_orig_c98_satchel(building, premature);
    g_c163_in_satchel = was_in;
    if (relay) {
        c98_host_world(row, world, true);
    }
}

// Card 125: host. A placed turret's packed state, journalled per turret (the last state wins, so a late joiner gets
// every turret's rest aim) and sent to every client. False when the turret has no placed name.
bool c98_host_turret(const void* turret, uint32_t value) {
    C98World row{};
    if (turret == nullptr || c50_name(turret, row.name, sizeof(row.name)) <= 0) {
        return false;
    }
    row.actor = turret;
    row.at = actor_location(turret);
    row.kind = kC98KindTurret;
    row.value = value;
    c98_host_world(row, g_actor_get_world(turret), true, true);
    return true;
}

// Card 471: host. A destroyed turret's 'T' row goes, so a join replay names no turret that is gone.
void c98_forget_turret(const void* turret) {
    c98_unjournal(turret, kC98KindTurret);
}

// Card 132 step 2d: host. A placed, unreplicated kC132Ext actor's state changed (ext_c132.cpp packs `value`). One
// journalled row per actor, the last state wins, so a late joiner gets every changed actor. Quiet: ext_c132 logs.
void c98_host_ext(const void* actor, const char* name, const Vec3& at, uint32_t value) {
    C98World row{};
    row.actor = actor;
    snprintf(row.name, sizeof(row.name), "%s", name);
    row.at = at;
    row.kind = kC98KindExt;
    row.value = value;
    c98_host_world(row, g_actor_get_world(actor), true, true);
}

// Card 137 3a: host. One ext actor's current value to one joining PC, outside the journal (ext_c132.cpp
// c132_ext_replay sends every tracked actor, changed or not, so the client needs no baseline of its own).
bool c98_send_ext(void* pc, const char* name, const Vec3& at, uint32_t value) {
    C98World row{};
    snprintf(row.name, sizeof(row.name), "%s", name);
    row.at = at;
    row.kind = kC98KindExt;
    row.value = value;
    char text[kAssetPathChars + 64];
    c98_world_text(row, text, sizeof(text));
    return c98_send_one(pc, text);
}

// Card 420: host. One 'x' frame (ext_c132.cpp c132_ext_replay builds the text) to one joining PC, outside the journal.
bool c98_send_ext_pack(void* pc, const char* text) {
    return text[0] == kC98KindExtPack && text[1] == ' ' && c98_send_one(pc, text);
}

// Card 299 (diagnostic): the nest's USTMeshUpdateComponent state after its death (card 299 step 1b, disassembly):
// +0xB9 bound flag (BeginPlay binds UpdateMeshBasedOnDamageThreshold only when set), the threshold array count +0xC8,
// the health component +0xD8, the current threshold index +0xE0 (-1 at BeginPlay), the pending async entry +0x108 and
// the current mesh component +0xD0. The host's row and the client's are compared: same mesh name, or which step failed.
constexpr size_t kC299Bound = 0xB9;
constexpr size_t kC299Thresholds = 0xC8;
constexpr size_t kC299Mesh = 0xD0;
constexpr size_t kC299Health = 0xD8;
constexpr size_t kC299Index = 0xE0;
constexpr size_t kC299Pending = 0x108;

// c423b126a: FindComponentByClass gave no STMeshUpdate component on 2 host and 4 client nest rows though the
// Blueprint has an SCS node of that name; the row lists what the actor owns, as name:class, to name the class.
namespace {

constexpr int32_t kC299Owned = 32;

void c299_owned_row(const void* actor, const char* role) {
    const void* comps[kC299Owned] = {};
    const int32_t n = owned_components(actor, comps, kC299Owned);
    char text[1600] = "";
    size_t used = 0;
    for (int32_t i = 0; i < n && used + 200 < sizeof(text); ++i) {
        const auto cls = class_name(comps[i]);
        int w = snprintf(text + used, sizeof(text) - used, " %s:%s", object_name(comps[i]).text, cls.text);
        used += w > 0 ? static_cast<size_t>(w) : 0;
        // Senior b131: an AudioComponent also names its sound asset and attach parent (which nest part it plays on).
        if (strcmp(cls.text, "AudioComponent") == 0 && used < sizeof(text)) {
            const void* const sound = c31_object_prop(comps[i], "Sound");
            const void* const parent = c140_attach_parent(comps[i]);
            w = snprintf(text + used, sizeof(text) - used, "(sound %s, parent %s)",
                         sound != nullptr ? object_name(sound).text : "none",
                         parent != nullptr ? object_name(parent).text : "none");
            used += w > 0 ? static_cast<size_t>(w) : 0;
        }
        used = used < sizeof(text) ? used : sizeof(text) - 1; // a truncated write leaves the text full, never past it
    }
    log::line("card299: %s nest %s owns %d component(s):%s", role, object_name(actor).text, n, text);
}

} // namespace

void c299_nest_row(const void* actor, const char* role) {
    // c423b125a: the bare-name g_find_first_object lookup gave no row on either side for the two O02 nests, which have
    // the component; the native class is resolved by its script path (game module Aloft) and a miss is logged.
    static void* const update_class = find_class(L"/Script/Aloft.STMeshUpdateComponent");
    // c423b126a: every K-killed corpse pile (BP_WarriorBug_Corpses_Destructible_01/02_C) reached this row through
    // c98_host_kill and the client K path, 223 host and 446 client rows; they are not nests and are skipped.
    if (strstr(object_name(actor).text, "_Corpses_") != nullptr) {
        return;
    }
    const auto* const u = static_cast<const uint8_t*>(find_component_of(actor, update_class));
    if (u == nullptr) {
        log::line("card299: %s t=%llu nest %s: no STMeshUpdate component (class %s)", role,
                  static_cast<unsigned long long>(GetTickCount64()), object_name(actor).text,
                  update_class != nullptr ? "found" : "not found");
        c299_owned_row(actor, role);
        return;
    }
    const void* const mesh = read_ptr(u, kC299Mesh);
    const uint8_t* prop = nullptr;
    const void* const sm = mesh != nullptr ? prop_object_field(mesh, "StaticMesh", &prop) : nullptr;
    log::line("card299: %s t=%llu nest %s mesh %s bound %u thresholds %d health %s index %d pending %s collision %d",
              role, static_cast<unsigned long long>(GetTickCount64()), object_name(actor).text,
              sm != nullptr ? object_name(sm).text : (mesh != nullptr ? "none" : "no component"), u[kC299Bound],
              read_i32(u, kC299Thresholds), read_ptr(u, kC299Health) != nullptr ? "non-null" : "null",
              read_i32(u, kC299Index), read_u64(u, kC299Pending) != 0 ? "non-null" : "null",
              mesh != nullptr ? static_cast<int>(static_cast<const uint8_t*>(mesh)[kBodyCollisionEnabled]) : -1);
}

// Card 130 item 12: host. A placed, unreplicated actor with a health component reached Health 0. Its death (mesh swap,
// FX, spawner off, minimap icon) is Blueprint logic on the component's OnDeath, so the client's copy, which takes no
// damage of the host's, stays alive unless told. One journalled row per actor, so a late joiner gets every dead one.
// Host: the actor's one 'K' row, journalled and sent. False when it is not a placed, unreplicated actor of a listen
// server, already has its 'K' row (one per death) or cannot be named.
bool c98_relay_kill(const void* actor, C98World& row) {
    const bool relay = c50_placed(actor) && net_mode(actor) == kNetModeListenServer && !c98_replicates(actor) &&
                       c98_journal_row(actor, kC98KindKill) == nullptr && c50_name(actor, row.name, sizeof(row.name)) > 0;
    if (!relay) {
        return false;
    }
    row.actor = actor;
    row.at = actor_location(actor);
    row.kind = kC98KindKill;
    row.value = 0;
    c98_host_world(row, g_actor_get_world(actor), true);
    c299_nest_row(actor, "host");
    return true;
}

void c98_host_kill(const void* actor, float old_health, float new_health) {
    C98World row{};
    if (old_health > 0.0f && new_health <= 0.0f && c98_relay_kill(actor, row)) {
        log::line("card098: host t=%llu %s %s Health %.1f -> %.1f: kill relayed",
                  static_cast<unsigned long long>(GetTickCount64()), class_name(actor).text, row.name,
                  static_cast<double>(old_health), static_cast<double>(new_health));
    }
}

void c487_hidden(const void* actor); // below

// hb46: host. A placed, unreplicated actor's SetActorHiddenInGame changed its bHidden on the game thread (the corpse
// piles' overkill destruction hides the pile: Blueprint logic on damage the client's copy never takes). One journalled
// row per actor, the last state wins. Hides made on the loading thread (spawn points, metric guides at level load)
// happen on both sides alike and are not relayed.
void c98_host_hide(const void* actor, bool hidden) {
    C98World row{};
    const bool relay = g_is_in_game_thread != nullptr && g_is_in_game_thread() && c50_placed(actor) &&
                       net_mode(actor) == kNetModeListenServer && !c98_replicates(actor) &&
                       c50_name(actor, row.name, sizeof(row.name)) > 0;
    if (!relay) {
        return;
    }
    row.actor = actor;
    row.at = actor_location(actor);
    row.kind = kC98KindHide;
    row.value = hidden ? 1u : 0u;
    c98_host_world(row, g_actor_get_world(actor), true);
    if (hidden) {
        c487_hidden(actor); // card 487: a death with no Health change
    }
}

// Card 137 step 3: a placed killable building is dead now: its health component says so, or (a building loaded dead,
// whose health may be restored without a death) its Blueprint's Destroyed bool is set. `killable` is false for an
// actor with neither.
bool c98_dead_now(const void* actor, bool& killable) {
    const void* const health = g_c130_health_class != nullptr && g_c130_is_dead != nullptr
                                   ? find_component(actor, g_c130_health_class) : nullptr;
    killable = health != nullptr;
    return killable && (c98_bp_destroyed(actor) || g_c130_is_dead(health));
}

// Card 487 (c244b213a): the bridge C4 blast destroyed 6 benches and 2 bins (Blueprint death, no HealthAttributeChanged,
// so c98_host_kill never ran); the client got only their 'H' rows and kept them alive (digest "dead host 1 client 0" on
// 22 rounds to the run end). A host hide of a placed killable actor with no 'K' row is checked again kC487DelayMs later
// (its Blueprint may set Destroyed after the hide) and sends the 'K' row when the actor is dead by then.
namespace {

constexpr int32_t kC487Pending = 64;
constexpr ULONGLONG kC487DelayMs = 250;
constexpr uint32_t kC487Lines = 32;
struct C487Row {
    const void* actor; // read only while object_listed and its FName still match
    uint64_t fname;
    ULONGLONG due;
};
struct C487State {
    C487Row row[kC487Pending];
    int32_t n;
    uint32_t relayed, alive, gone, full, lines;
};
C487State g_c487{}; // game thread, host

void c487_kill(const void* actor, const char* when) {
    C98World row{};
    if (!c98_relay_kill(actor, row)) {
        return;
    }
    ++g_c487.relayed;
    if (note_event_cap(g_c487.lines, kC487Lines, "card487 kill rows")) {
        log::line("card487: host t=%llu %s %s at (%.0f, %.0f, %.0f) dead with no Health change (%s): kill relayed",
                  static_cast<unsigned long long>(GetTickCount64()), class_name(actor).text, row.name, row.at.x,
                  row.at.y, row.at.z, when);
    }
}

} // namespace

void c487_hidden(const void* actor) {
    bool killable = false;
    if (c98_journal_row(actor, kC98KindKill) != nullptr) {
        return;
    }
    if (c98_dead_now(actor, killable)) {
        c487_kill(actor, "at its hide");
        return;
    }
    if (!killable) {
        return;
    }
    if (g_c487.n >= kC487Pending) {
        ++g_c487.full;
        if (note_event_cap(g_c487.lines, kC487Lines, "card487 kill rows")) {
            log::line("WARN card487: host hide check list full (%d): %s %s not checked for a death", kC487Pending,
                      class_name(actor).text, object_name(actor).text);
        }
        return;
    }
    g_c487.row[g_c487.n++] = C487Row{actor, read_u64(actor, kObjectName), GetTickCount64() + kC487DelayMs};
}

// Card 464: host, after a save load. A satchel building's 'S' row goes when the load revived it (Destroyed false: the
// save is older than the explosion, c460b168a _C_2), else the rejoin replay destroys the building the load brought
// back. A satchel building with no health component (not covered by the K rule) that the save holds destroyed and that
// has no 'S' row gets one with value 0. Own line cap: the c95 collision rows spend the shared one (c460).
static void c98_load_satchel(const void* actor, bool killable, C98World& row, C137LoadCounts& n) {
    constexpr uint32_t kC464Lines = 16;
    const bool destroyed = c98_bp_destroyed(actor);
    const bool had = c98_journal_row(actor, kC98KindSatchel) != nullptr;
    const bool satchel = !had && !killable && destroyed && g_c132_satchel_class != nullptr &&
                         is_a(actor, g_c132_satchel_class());
    if (had && !destroyed) {
        c98_unjournal(actor, kC98KindSatchel);
        ++n.s_dropped;
    } else if (satchel) {
        row.actor = actor;
        row.at = actor_location(actor);
        row.kind = kC98KindSatchel;
        row.value = 0;
        n.s_added += c98_journal_put(row) != nullptr ? 1u : 0u;
    }
    if (((had && !destroyed) || satchel) && note_event_cap(n.s_lines, kC464Lines, "card137 load reconcile c98 S")) {
        log::line("card137: host load reconcile c98 S %s %s: %s", class_name(actor).text, row.name,
                  satchel ? "destroyed with no row, row added" : "alive, row dropped");
    }
}

// Card 137 step 3: host, after a save load (LoadFromSlot returned). The load sets actors' state with no OnDeath and,
// for a restore, revives what died after the save, so the journal is rebuilt from the actor: a K row of an alive
// building goes, a dead building with no K row gets one (a kill held by the save: an earlier session's slot or the
// frontend Continue route), and an H row takes the actor's current bHidden. Journal only: nothing is sent here (a
// client in the session during a restore is card 143). Actors no longer in a level keep their rows.
void c98_load_actor(const void* actor, C137LoadCounts& n) {
    C98World row{};
    if (!c50_placed(actor) || net_mode(actor) != kNetModeListenServer || c98_replicates(actor) ||
        c50_name(actor, row.name, sizeof(row.name)) <= 0) {
        return;
    }
    bool killable = false;
    const bool dead = c98_dead_now(actor, killable);
    const bool had = c98_journal_row(actor, kC98KindKill) != nullptr;
    if (killable && had && !dead) {
        c98_unjournal(actor, kC98KindKill);
        ++n.k_dropped;
    } else if (killable && !had && dead) {
        row.actor = actor;
        row.at = actor_location(actor);
        row.kind = kC98KindKill;
        row.value = 0;
        n.k_added += c98_journal_put(row) != nullptr ? 1u : 0u;
    }
    if (killable && had != dead && c137_load_line(n)) {
        log::line("card137: host load reconcile c98 K %s %s: %s", class_name(actor).text, row.name,
                  dead ? "dead with no row, row added" : "alive, row dropped");
    }
    c98_load_satchel(actor, killable, row, n);
    C98World* const h = c98_journal_row(actor, kC98KindHide);
    const uint32_t hidden = (static_cast<const uint8_t*>(actor)[kActorHiddenByte] & kActorHiddenBit) != 0 ? 1u : 0u;
    if (h != nullptr && h->value != hidden) {
        h->value = hidden;
        ++n.h_fixed;
        if (c137_load_line(n)) {
            log::line("card137: host load reconcile c98 H %s %s: row now %u", class_name(actor).text, row.name, hidden);
        }
    }
}

// Card 130 item 2: host. A timeline call on a placed actor that does not replicate (the TacSupport range's pop-up
// targets, the grenade range targets): its movement is the Blueprint's timeline update, which the client's copy runs
// only when its own timeline plays. Journalled per timeline component (the last call wins, so a late joiner gets each
// target's last state). A repeat of the journalled call is not sent again, except PlayFromStart, which restarts.
// Replicated owners (the dropship's FadeInTimeline) are the engine's.
// Card 261: the relay conditions and row of a host timeline, shared by the calls ('L') and the play rate ('B'): a
// placed owner on the listen server that does not replicate, name "<placed name>|<timeline name>". False: not relayed.
static bool c98_timeline_row(const void* timeline, char kind, C98World& row) {
    const void* const owner = timeline != nullptr ? read_ptr(timeline, kComponentOwner) : nullptr;
    char comp[kC98ClassChars];
    const bool relay = owner != nullptr && c50_placed(owner) && net_mode(owner) == kNetModeListenServer &&
                       !c98_replicates(owner) && !c408_building(owner) && c50_name(timeline, comp, sizeof(comp)) > 0 &&
                       c50_name(owner, row.name, sizeof(row.name)) > 0;
    if (!relay) {
        return false;
    }
    const size_t used = strlen(row.name);
    if (snprintf(row.name + used, sizeof(row.name) - used, "|%s", comp) >= static_cast<int>(sizeof(row.name) - used)) {
        log::line("WARN card098: host timeline %s on %s: name too long, not relayed", comp, row.name);
        return false;
    }
    row.actor = timeline;
    row.at = actor_location(owner);
    row.kind = kind;
    return true;
}

void c98_host_timeline(const void* timeline, C98TimelineOp op) {
    C98World row{};
    if (!c98_timeline_row(timeline, kC98KindTimeline, row)) {
        return;
    }
    for (int32_t i = 0; i < g_c98w.n && op != C98TimelineOp::PlayFromStart; ++i) {
        if (g_c98w.row[i].actor == timeline && g_c98w.row[i].kind == kC98KindTimeline &&
            g_c98w.row[i].value == static_cast<uint32_t>(op)) {
            return;
        }
    }
    row.value = static_cast<uint32_t>(op);
    c98_host_world(row, g_actor_get_world(read_ptr(timeline, kComponentOwner)), true);
}

// Card 408: host. One live 'Q' row of a flak or plasma bug building's timer event (not journalled: a late joiner's
// buildings start their own loop, the next host row aligns them). False: not a placed, unreplicated actor, or a send
// failed (c98_host_world logs that WARN).
bool c408_host_row(const void* building, uint32_t value, const Vec3* at) {
    C98World row{};
    if (!c50_placed(building) || net_mode(building) != kNetModeListenServer || c98_replicates(building) ||
        c50_name(building, row.name, sizeof(row.name)) <= 0) {
        return false;
    }
    row.actor = building;
    row.at = at != nullptr ? *at : actor_location(building); // card 419: the burst row carries the round's location
    row.kind = kC98KindC408;
    row.value = value;
    const C95Fanout f = c98_host_world(row, g_actor_get_world(building), false, true);
    return f.sent == f.to;
}

// Card 261: host. A play rate set on a relayed timeline, journalled per timeline (the last rate wins); a repeat of the
// journalled rate is not sent again.
void c261_host_rate(const void* timeline, float rate) {
    C98World row{};
    if (!c98_timeline_row(timeline, kC98KindRate, row)) {
        return;
    }
    memcpy(&row.value, &rate, sizeof(row.value));
    const C98World* const last = c98_journal_row(timeline, kC98KindRate);
    if (last != nullptr && last->value == row.value) {
        return;
    }
    c98_host_world(row, g_actor_get_world(read_ptr(timeline, kComponentOwner)), true);
    if (note_event_cap(g_c98w.rate_lines, kC261Lines, "card261 play rate")) {
        log::line("card261: host world %c %s: play rate %.4f sent", kC98KindRate, row.name, static_cast<double>(rate));
    }
}

// Card 311: UActorComponent::bIsActive of a component: 1, 0, or -1 (property not found). Found once by reflection
// on the first component's class (all callers pass the card 065 trigger class) and kept.
static int c311_active(const void* component) {
    static const uint8_t* prop = nullptr; // game thread only
    static bool tried = false;
    if (!tried && component != nullptr) {
        tried = true;
        static constexpr const char* kNames[] = {"bIsActive", nullptr};
        const uint8_t* const p = c27_find_any(read_ptr(component, kObjectClass), kNames);
        prop = prop_kind_is(p, "BoolProperty") && prop_bool_ok(p) ? p : nullptr;
        if (prop == nullptr) {
            log::line("WARN card311: bIsActive not found on %s: replay rows go ungated",
                      object_name(read_ptr(component, kObjectClass)).text);
        }
    }
    return prop != nullptr && component != nullptr ? (c29_flag(static_cast<const uint8_t*>(component), prop) ? 1 : 0)
                                                   : -1;
}

// Card 311: host, after the card 065 Activate ran and its live frame went. The journal row (no send: the live frame
// is card 065's), so a client that loads the map again gets the target's activation in its replay.
void c311_host_activate(const void* component, bool reset) {
    const void* const owner = component != nullptr ? read_ptr(component, kComponentOwner) : nullptr;
    C98World row{};
    const ObjectName comp = prop_name(static_cast<const uint8_t*>(component) + kObjectName, 8);
    const int32_t len = owner != nullptr && c50_placed(owner) ? c50_name(owner, row.name, sizeof(row.name)) : -1;
    const size_t used = len > 0 ? static_cast<size_t>(len) : 0;
    if (len <= 0 ||
        snprintf(row.name + used, sizeof(row.name) - used, "|%s", comp.text) >= static_cast<int>(sizeof(row.name) - used)) {
        log::line("WARN card311: host Activate(%d) %s on %s: no placed name, not journalled", reset ? 1 : 0, comp.text,
                  owner != nullptr ? object_name(owner).text : "no owner");
        return;
    }
    row.actor = owner;
    row.at = actor_location(owner);
    row.kind = kC98KindActivate;
    row.value = reset ? 1u : 0u;
    const bool kept = c98_journal_put(row) != nullptr;
    log::line("%scard311: host journalled Activate(%d) %s %s, bIsActive %d; journal %d%s", kept ? "" : "WARN ",
              reset ? 1 : 0, object_name(owner).text, comp.text, c311_active(component), g_c98w.n,
              kept ? "" : " (journal full: not replayed)");
}

// Card 311: host, the owner's end. Its Activate row goes (only range targets carry one).
void c311_host_forget(const void* actor) {
    if (!g_client_session && strstr(class_name(actor).text, "RangeTarget") != nullptr) {
        c98_unjournal(actor, kC98KindActivate);
    }
}

PrimBodyQueryFn g_prim_awake = nullptr;      // card 132 note 2
PrimBodyQueryFn g_prim_simulating = nullptr;
C317MoveCallFn g_c317_stop_movement = nullptr;
C317MoveCallFn g_c317_deactivate = nullptr;
PrimBodySleepFn g_prim_sleep = nullptr;

bool c104_is_relayed(const void* actor) {
    for (int32_t i = 0; i < g_c104_relayed.n; ++i) {
        if (g_c104_relayed.actor[i] == actor) {
            return true;
        }
    }
    return false;
}

// Card 132: client. Compares pointers only; the copy is not read.
bool c104_copy_host_id(const void* actor, uint32_t& host_id) {
    for (int32_t i = 0; i < g_c104_copies.n; ++i) {
        if (g_c104_copies.row[i].actor == actor) {
            host_id = g_c104_copies.row[i].host_id;
            return true;
        }
    }
    return false;
}

namespace {

// Card 140: the UPrimitiveComponent body calls (IsSimulatingPhysics, RigidBodyIsAwake, PutRigidBodyToSleep) read the
// component's BodyInstance, which a root that is a plain USceneComponent (BP_TacSupp_DangerZone_C) does not have:
// c140i's client crashed in PutRigidBodyToSleep on that copy's first rest row. Every such call checks this first.
bool c104_root_is_primitive(const void* root) {
    static void* primitive = nullptr;
    primitive = primitive != nullptr ? primitive : find_class(L"/Script/Engine.PrimitiveComponent");
    return root != nullptr && primitive != nullptr && is_a(root, primitive);
}

// Card 140: host. Whether relayed root `root` simulates physics; false for a root that is not a primitive.
bool c104_root_simulates(const void* root) {
    return g_prim_simulating != nullptr && c104_root_is_primitive(root) && g_prim_simulating(root, 0);
}

// Card 287: host, per map. Moving samples sent, and the most sent in one 1 s window (windows start at a send).
// Logged and cleared in c98_world_map_end.
struct C287Sent {
    uint32_t sent;
    uint32_t peak;
    uint32_t in_window;
    ULONGLONG window_ms;
};
C287Sent g_c287_sent{};

void c287_count_sent(ULONGLONG now) {
    ++g_c287_sent.sent;
    if (now - g_c287_sent.window_ms >= 1000) {
        g_c287_sent.window_ms = now;
        g_c287_sent.in_window = 0;
    }
    ++g_c287_sent.in_window;
    g_c287_sent.peak = g_c287_sent.in_window > g_c287_sent.peak ? g_c287_sent.in_window : g_c287_sent.peak;
}

// Card 317: both sides, diagnostic. One row per relayed actor at its spawn ('P' sent or copy spawned) and at its
// first rest ('M' sent or, on the client, before the first rest row is applied): what moves it. The root's class and
// whether it simulates physics, the actor's first movement component (UMovementComponent or a subclass, such as the
// UProjectileMovementComponent of a thrown flare) and that component's Velocity, and the root's yaw. The thrown flare
// copy flies along yaw 0 while the host's flies along the thrower's yaw; these rows show which of the two sides the
// velocity comes from. Bounded by kC317Lines per side per map.
constexpr uint32_t kC317Lines = 64;
void c317_motion_row(const char* side_name, const void* actor, const char* stage, uint32_t host_id) {
    if (actor == nullptr || !note_event_cap(g_c98w.c317_lines, kC317Lines, "card317 motion")) {
        return;
    }
    static void* movement = nullptr;
    movement = movement != nullptr ? movement : find_class(L"/Script/Engine.MovementComponent");
    const void* const root = read_ptr(actor, kActorRootComponent);
    const void* const move = find_component_of(actor, movement);
    const Vec3 v = move != nullptr ? read_vec3(move, kMoveVelocity) : Vec3{};
    const double yaw = root != nullptr ? read_vec3(root, kC104RelativeRotation).y : 0.0;
    const bool simulating = root != nullptr && c104_root_is_primitive(root) && g_prim_simulating != nullptr &&
                            g_prim_simulating(root, 0);
    log::line("card317: %s %s root %s simulating %d, movement component %s velocity (%.0f, %.0f, %.0f), actor yaw "
              "%.1f (%s, host id %u)",
              side_name, class_name(actor).text, root != nullptr ? class_name(root).text : "none", simulating ? 1 : 0,
              move != nullptr ? class_name(move).text : "none", v.x, v.y, v.z, yaw, stage, host_id);
}

// Card 132 note 2 / card 140: host. The rest row ('M') of relayed actor `i` at `at`, with its root's rotation.
// Card 287: `moving` marks a moving sample (c104_sample_moving): name "pitch,yaw,roll,1", which the client blends
// toward; a rest is "pitch,yaw,roll,0", which ends the motion asleep. Both are the one journal row per actor (the
// last one wins), and a moving sample sends no card098 line (ten a second per rolling body).
void c104_send_rest(int32_t i, const void* actor, const void* root, const Vec3& at, bool moving) {
    if (!moving && !g_c104_relayed.rest[i].c317_logged) {
        g_c104_relayed.rest[i].c317_logged = true;
        c317_motion_row("host", actor, "first rest", static_cast<uint32_t>(object_index(actor)));
    }
    const Vec3 rot = read_vec3(root, kC104RelativeRotation);
    C98World row{};
    row.actor = g_c104_relayed.actor[i];
    snprintf(row.name, sizeof(row.name), "%.2f,%.2f,%.2f,%d", rot.x, rot.y, rot.z, moving ? 1 : 0);
    row.at = at;
    row.kind = kC98KindRest;
    row.value = static_cast<uint32_t>(object_index(actor));
    c98_host_world(row, g_actor_get_world(actor), true, moving);
    const ULONGLONG now = GetTickCount64();
    g_c104_relayed.rest[i].row_ms = static_cast<uint32_t>(now);
    ++g_c104_relayed.rest[i].rows; // card 256
    if (moving) {
        c287_count_sent(now); // card 287
    }
}

// Card 145: whether `actor` is an AADPickupBase (every pickup class: weapon, ammo, health, tactical support, secret).
bool c145_is_pickup(const void* actor) {
    return actor != nullptr && g_c56_pickup_class != nullptr && is_a(actor, g_c56_pickup_class());
}

} // namespace

// See hooks_internal.h (weapons.cpp's detour_c145_update_status reads it too).
uint32_t c145_enabled(const void* pickup) {
    return static_cast<const uint8_t*>(pickup)[kPickupEnabledByte] != 0 ? 1u : 0u;
}

// Card 221: client, set while c145_client_enable applies a host row through SetPickupEnabled (game thread only).
bool g_c221_host_enable = false;

namespace {

// Card 221: client. The pickups whose last host row ('E' or 'N') was 0; presence is the state. Only these may have
// a local enable set back (detour_c145_update_status): a pickup the host never disabled sends no row, and its
// Blueprint enable at load is the host's state too. Weak handles, so a destroyed pickup's reused address does not
// match. Cleared at LoadMap enter (c98_world_map_end), game thread only.
constexpr int32_t kC221HostOff = 256;
struct C221HostOff {
    C31Weak row[kC221HostOff];
    int32_t n;
    bool full_logged; // the one capped WARN when a 0 row finds no room
};
C221HostOff g_c221_off{};

int32_t c221_off_find(const void* pickup) {
    int32_t at = -1;
    for (int32_t i = 0; i < g_c221_off.n && at < 0; ++i) {
        at = c31_weak_get(g_c221_off.row[i]) == pickup ? i : -1;
    }
    return at;
}

// A host row for `pickup` arrived: 0 adds it, 1 removes it.
void c221_off_note(void* pickup, uint32_t want) {
    const int32_t at = c221_off_find(pickup);
    if (want != 0) {
        if (at >= 0) {
            g_c221_off.row[at] = g_c221_off.row[--g_c221_off.n];
        }
        return;
    }
    if (at >= 0) {
        return;
    }
    int32_t slot = g_c221_off.n;
    for (int32_t i = 0; i < g_c221_off.n && slot == g_c221_off.n; ++i) {
        slot = c31_weak_get(g_c221_off.row[i]) == nullptr ? i : slot; // a destroyed pickup's row is free
    }
    if (slot >= kC221HostOff) {
        if (!g_c221_off.full_logged) {
            g_c221_off.full_logged = true;
            log::line("WARN card221: client host-disabled table full (%d)", kC221HostOff);
        }
        return;
    }
    c31_weak_set(g_c221_off.row[slot], pickup);
    g_c221_off.n += slot == g_c221_off.n ? 1 : 0;
}

} // namespace

bool c221_host_disabled(const void* pickup) {
    return pickup != nullptr && c221_off_find(pickup) >= 0;
}

namespace {

// Card 145: host. The 'N' row of relayed pickup `live` (journalled while it lives, so a late joiner gets it after the
// 'P' row). `always` sends it when it equals the journal row too (the spawn sends the state the copy starts from).
void c145_send_copy_enable(const void* live, bool always) {
    const uint32_t enabled = c145_enabled(live);
    const C98World* const last = c98_journal_row(live, kC98KindCopyEnable);
    if (!always && last != nullptr && static_cast<uint32_t>(last->at.x) == enabled) {
        return;
    }
    C98World row{};
    row.actor = live;
    snprintf(row.name, sizeof(row.name), "-");
    row.at = Vec3{static_cast<double>(enabled), 0.0, 0.0};
    row.kind = kC98KindCopyEnable;
    row.value = static_cast<uint32_t>(object_index(live));
    c98_host_world(row, g_actor_get_world(live), true);
}

// Card 256: host. One line per relayed pickup at its first rest row, bounded by kC104Copies per map.
void c256_host_rest(int32_t i, const void* actor, bool simulating, bool asleep) {
    C104Rest& s = g_c104_relayed.rest[i];
    if (s.first_logged || !c145_is_pickup(actor) ||
        !note_event_cap(g_c98w.c256_host_lines, static_cast<uint32_t>(kC104Copies), "card256 host rest")) {
        return;
    }
    s.first_logged = true;
    log::line("card256: host pickup %d rest at (%.0f, %.0f, %.0f) after %u ms, %u rows sent, simulating %d, %s",
              object_index(actor), s.rest_at.x, s.rest_at.y, s.rest_at.z,
              static_cast<uint32_t>(GetTickCount64()) - s.spawn_ms, static_cast<uint32_t>(s.rows), simulating ? 1 : 0,
              asleep ? "asleep" : "still");
}

// Card 104 / card 140: host. The 'P' row of relayed actor `i` at the place it stands. A root that does not simulate
// (a pickup held on the resupply rack, the danger zone) gets its rest row ('M') at once too: the client spawns its
// copy with no rotation and its own physics state, and c140j's client copy of a rack pickup fell 190 uu while the
// host's stayed on the rack. The rest row puts the copy to sleep where the host's stands.
void c104_send_spawn(int32_t i, const void* live) {
    const Vec3 at = actor_location(live);
    C98World row{};
    row.actor = g_c104_relayed.actor[i];
    snprintf(row.name, sizeof(row.name), "%s", c74_asset_path(read_ptr(live, kObjectClass)).text);
    row.at = at;
    row.kind = kC98KindSpawn;
    row.value = static_cast<uint32_t>(object_index(live));
    c98_host_world(row, g_actor_get_world(live), true); // card 105: a late joiner gets it while it lives
    g_c104_relayed.rest[i] = C104Rest{at, at, 0, false, 0, 0, 0, false, 0, false, false};
    g_c104_relayed.rest[i].spawn_ms = static_cast<uint32_t>(GetTickCount64()); // card 256
    c317_motion_row("host", live, "spawn", row.value);
    const void* const root = read_ptr(live, kActorRootComponent);
    if (root != nullptr && !c104_root_simulates(root)) {
        g_c104_relayed.rest[i].rested = true;
        c104_send_rest(i, live, root, at, false);
        c256_host_rest(i, live, false, false);
    }
    if (c145_is_pickup(live)) {
        c145_send_copy_enable(live, true); // card 145: the copy starts from the host's bEnabled, not its class default
    }
}

} // namespace

// Card 306: client. The host id of a live relayed copy, 0 when the actor is not one.
uint32_t c104_copy_host_id(const void* actor) {
    for (int32_t i = 0; actor != nullptr && i < g_c104_copies.n; ++i) {
        if (g_c104_copies.row[i].actor == actor) {
            return g_c104_copies.row[i].host_id;
        }
    }
    return 0;
}

// Card 306: host. The live relayed actor whose 'P' row carried this id (object_index), else null.
void* c104_relayed_by_id(uint32_t id) {
    for (int32_t i = 0; id != 0 && i < g_c104_relayed.n; ++i) {
        void* const live = c31_weak_get(g_c104_relayed.weak[i]);
        if (live != nullptr && static_cast<uint32_t>(object_index(live)) == id) {
            return live;
        }
    }
    return nullptr;
}

// Card 104: host. See hooks_internal.h. The pointer is kept to match the destroy, never read again.
void c104_relay_spawn(const void* actor, const Vec3& at) {
    if (actor == nullptr || net_mode(actor) != kNetModeListenServer || c98_replicates(actor)) {
        return;
    }
    const C74Path path = c74_asset_path(read_ptr(actor, kObjectClass));
    const void* const world = g_actor_get_world(actor);
    const bool full = g_c104_relayed.n >= kC104Copies;
    if (world == nullptr || strcmp(path.text, "-") == 0 || full) {
        log::line("WARN card104: host t=%llu %s (%s) not relayed: %s", static_cast<unsigned long long>(GetTickCount64()),
                  object_name(actor).text, path.text,
                  world == nullptr ? "no world" : (full ? "relayed table full (kC104Copies)" : "no class path"));
        return;
    }
    g_c104_relayed.weak[g_c104_relayed.n] = C31Weak{};
    c31_weak_set(g_c104_relayed.weak[g_c104_relayed.n], const_cast<void*>(actor));
    g_c104_relayed.rest[g_c104_relayed.n] = C104Rest{at, at, 0, false, 0, 0, 0, false, 0, false, false};
    g_c104_relayed.actor[g_c104_relayed.n++] = actor;
    g_c104_relayed.peak = g_c104_relayed.n > g_c104_relayed.peak ? g_c104_relayed.n : g_c104_relayed.peak;
    const Vec3 now = actor_location(actor);
    if (vec_len(now) < 1.0 && vec_len(at) >= 1.0 && g_c104_pending.n < kC104Copies) {
        g_c104_pending.actor[g_c104_pending.n++] = actor; // card 140: deferred spawn, see C104Pending
        return;
    }
    c104_send_spawn(g_c104_relayed.n - 1, actor);
}

namespace {

// Card 140: host. Drops `actor` from the pending set; true when it was there.
bool c104_unpend(const void* actor) {
    for (int32_t i = 0; i < g_c104_pending.n; ++i) {
        if (g_c104_pending.actor[i] == actor) {
            g_c104_pending.actor[i] = g_c104_pending.actor[--g_c104_pending.n];
            return true;
        }
    }
    return false;
}

// Card 140: host. Each pending actor's 'P' row at the place FinishSpawning gave it.
void c104_flush_pending() {
    for (int32_t p = 0; p < g_c104_pending.n; ++p) {
        const void* const actor = g_c104_pending.actor[p];
        int32_t i = -1;
        for (int32_t k = 0; k < g_c104_relayed.n && i < 0; ++k) {
            i = g_c104_relayed.actor[k] == actor ? k : -1;
        }
        void* const live = i >= 0 ? c31_weak_get(g_c104_relayed.weak[i]) : nullptr;
        if (live == nullptr) {
            continue; // its destroy drops the relayed row; nothing was sent
        }
        c104_send_spawn(i, live);
        ++g_c104_pending.sent;
    }
    g_c104_pending.n = 0;
}

} // namespace

// Card 104: host. A relayed actor's destroy goes to the clients, keyed by its object index.
void c104_relay_destroy(const void* world, const void* actor) {
    int32_t at = -1;
    for (int32_t i = 0; i < g_c104_relayed.n && at < 0; ++i) {
        at = g_c104_relayed.actor[i] == actor ? i : -1;
    }
    if (at < 0) {
        return;
    }
    --g_c104_relayed.n;
    g_c104_relayed.actor[at] = g_c104_relayed.actor[g_c104_relayed.n];
    g_c104_relayed.weak[at] = g_c104_relayed.weak[g_c104_relayed.n];
    g_c104_relayed.rest[at] = g_c104_relayed.rest[g_c104_relayed.n];
    if (c104_unpend(actor)) {
        ++g_c104_pending.ended; // card 140: it ended before its 'P' row went; the client never had a copy
        return;
    }
    c98_unjournal(actor, kC98KindSpawn); // card 105
    c98_unjournal(actor, kC98KindRest);  // card 132 note 2
    c98_unjournal(actor, kC98KindCopyEnable); // card 145
    C98World row{};
    row.actor = actor;
    snprintf(row.name, sizeof(row.name), "-");
    row.at = actor_location(actor);
    row.kind = kC98KindDestroy;
    row.value = static_cast<uint32_t>(object_index(actor));
    c98_host_world(row, world, false);
}

// Card 145: host, after AADPickupBase::OnUpdatePickupStatus, which every bEnabled change runs (DisablePickup and
// SetPickupEnabled write the byte and call it; every override calls the base). A placed pickup's change is an 'E' row
// keyed by name and location (duplicate placed names, such as the range sublevel's BP_AmmoPickup_MoritaSmall_C_0, resolve
// by location), and a relayed copy's is an 'N' row keyed by its host object index. Each is sent only when it differs
// from the actor's journal row (no 'E' row means enabled). A pending relayed pickup's 'N' row goes with its 'P' row.
void c145_host_status(const void* pickup) {
    if (pickup == nullptr || net_mode(pickup) != kNetModeListenServer) {
        return;
    }
    for (int32_t i = 0; i < g_c104_relayed.n; ++i) {
        if (g_c104_relayed.actor[i] == pickup) {
            for (int32_t p = 0; p < g_c104_pending.n; ++p) {
                if (g_c104_pending.actor[p] == pickup) {
                    return;
                }
            }
            c145_send_copy_enable(pickup, false);
            return;
        }
    }
    const uint32_t enabled = c145_enabled(pickup);
    if (!c50_placed(pickup)) {
        static uint32_t lines = 0; // per session
        if (!c98_replicates(pickup) && note_event_cap(lines, kC145UnrelayedLines, "card145 unrelayed pickup")) {
            log::line("card145: host t=%llu pickup %s (%s) enabled %u: not placed and not relayed, no row",
                      static_cast<unsigned long long>(GetTickCount64()), object_name(pickup).text,
                      class_name(pickup).text, enabled);
        }
        return;
    }
    const C98World* const last = c98_journal_row(pickup, kC98KindEnable);
    if ((last != nullptr ? last->value : 1u) == enabled) {
        return;
    }
    C98World row{};
    if (c50_name(pickup, row.name, sizeof(row.name)) <= 0) {
        log::line("WARN card145: host pickup %s: name not sendable", object_name(pickup).text);
        return;
    }
    row.actor = pickup;
    row.at = actor_location(pickup);
    row.kind = kC98KindEnable;
    row.value = enabled;
    c98_host_world(row, g_actor_get_world(pickup), true);
}

// --- Card 290 part A: a host travel is not a failure ---
// Host, at its Browse (c45_browse_enter): one 'V' row to every client, not journalled (it is about this connection).
// Client: the row's arrival time; ConnectionLost (3) and FailureReceived (5) within kC290TravelMs of it are the
// host's travel. Kept across LoadMap: the failures come after the old map's end.
namespace {
constexpr int32_t kC290ConnectionLost = 3;
constexpr int32_t kC290FailureReceived = 5;
ULONGLONG g_c290_travel_at = 0; // client: when the host's last travel row arrived (0 = never); game thread
} // namespace

void c290_host_travel(void* engine, const char* reason) {
    void* const world = engine != nullptr ? g_get_game_world(engine) : nullptr;
    // Card 349: any PC in the world will do (net mode only); not the local PC.
    void* const pc = world != nullptr ? g_world_first_pc(world) : nullptr;
    if (pc == nullptr || net_mode(pc) != kNetModeListenServer) {
        return; // standalone or no world: no clients to tell
    }
    C98World row{};
    snprintf(row.name, sizeof(row.name), "%s", reason);
    row.kind = kC98KindTravel;
    char text[kAssetPathChars + 64];
    c98_world_text(row, text, sizeof(text));
    // Card 294: the travel row is a priority row (always): it goes ahead of each connection's paced queue, so a load
    // burst already queued cannot hold it past the transition it announces.
    const C95Fanout f = c95_send_all(world, ChanOp::WorldOp, text, true);
    g_c98w.sent += static_cast<uint32_t>(f.sent);
    log::line("%scard290: host t=%llu travel row sent to %d of %d clients: sent now, ahead of %u queued frames (%s)",
              f.sent == f.to ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), f.sent, f.to, f.ahead,
              reason);
}

bool c290_travel_failure(int32_t type) {
    return g_client_session && (type == kC290ConnectionLost || type == kC290FailureReceived) &&
           g_c290_travel_at != 0 && GetTickCount64() - g_c290_travel_at <= kC290TravelMs;
}

// --- Card 289: a secret's credit reaches every player ---
// ASTSecretPickup::Pickup_Implementation (0x051D41B0) credits the take before its tail call of the base (card 254):
// when USTSecretsUtilityLibrary::IsSecretLocked(GetWorld(), SecretTag) it calls UnlockSecret(GetWorld(), SecretTag)
// (the campaign slot's unlocked secrets: saved progress), then USTMetaGame::SecretFound on GetSTMetaGame(this) when
// that object is not garbage: OnNewSecretDiscovered (+0x100) is broadcast and SecretsFoundCount (+0x48, BlueprintVisible,
// not replicated) goes up by one. USTMetaGame is per machine, so the host's take credited only the host. The host
// sends one journalled 'G' row per credited placed secret; the client runs the same credit on its own objects once
// per secret, and its own take of a placed secret skips the credit (the host's row is the only source, as card 254
// makes the host's take the only take).
C50SelfFn g_orig_c289_secret_pickup = nullptr;
C289MetaFn g_c289_get_meta = nullptr;     // USTCommonUtilityLibrary::GetSTMetaGame
C50SelfFn g_c289_secret_found = nullptr;  // USTMetaGame::SecretFound
C289LockedFn g_c289_is_locked = nullptr;  // USTSecretsUtilityLibrary::IsSecretLocked
C289UnlockFn g_c289_unlock = nullptr;     // USTSecretsUtilityLibrary::UnlockSecret

namespace {

constexpr size_t kC289SecretTag = 0x4B8;       // ASTSecretPickup's FGameplayTag (lea rdx,[rbx+0x4b8] at 0x051D41C5)
constexpr size_t kC289SecretsFound = 0x48;     // USTMetaGame::SecretsFoundCount (NewProp offset 0x48)
constexpr size_t kC289ObjectFlags = 0x08;      // UObjectBase::ObjectFlags
constexpr uint32_t kC289Garbage = 0x40000000u; // the flag Pickup_Implementation tests before SecretFound (bit 30)
constexpr int32_t kC289Secrets = 64;           // client: secrets credited from host rows per map

// Client: the secrets credited from host rows this map (object indices). Membership is the state.
struct C289Credited {
    int32_t id[kC289Secrets];
    int32_t n;
};
C289Credited g_c289{}; // game thread

struct C289Credit {
    int32_t before, after; // SecretsFoundCount around SecretFound; -1 when no live USTMetaGame
    bool unlocked;         // UnlockSecret ran (the secret was locked in the campaign slot)
};

bool c289_ready() {
    return g_c289_get_meta != nullptr && g_c289_secret_found != nullptr && g_c289_is_locked != nullptr &&
           g_c289_unlock != nullptr;
}

// The live USTMetaGame of `pickup`'s game, or null (the same test Pickup_Implementation makes).
void* c289_meta(const void* pickup) {
    void* const meta = g_c289_get_meta(pickup);
    if (meta == nullptr || (static_cast<uint32_t>(read_i32(meta, kC289ObjectFlags)) & kC289Garbage) != 0) {
        return nullptr;
    }
    return meta;
}

int32_t c289_count(const void* meta) {
    return meta != nullptr ? read_i32(meta, kC289SecretsFound) : -1;
}

// Client: the credit Pickup_Implementation gives a take, without the take (no base call: no FX, no OnPickup).
C289Credit c289_credit(void* pickup) {
    C289Credit c{-1, -1, false};
    void* const world = g_actor_get_world(pickup);
    const void* const tag = static_cast<const uint8_t*>(pickup) + kC289SecretTag;
    if (world != nullptr && g_c289_is_locked(world, tag)) {
        g_c289_unlock(world, tag);
        c.unlocked = true;
    }
    void* const meta = c289_meta(pickup);
    if (meta == nullptr) {
        return c;
    }
    c.before = c289_count(meta);
    g_c289_secret_found(meta);
    c.after = c289_count(meta);
    return c;
}

bool c289_credited(int32_t id) {
    for (int32_t i = 0; i < g_c289.n; ++i) {
        if (g_c289.id[i] == id) {
            return true;
        }
    }
    return false;
}

// Host, after the original credited `pickup` (count before -> after): its 'G' row to every client, journalled.
void c289_host_send(const void* pickup, int32_t before, int32_t after) {
    C98World row{};
    if (c50_name(pickup, row.name, static_cast<int32_t>(sizeof(row.name))) <= 0) {
        log::line("WARN card289: host secret %s: name not sendable", object_name(pickup).text);
        return;
    }
    if (after <= before) {
        log::line("WARN card289: host t=%llu secret %s not credited by the take (count %d -> %d), no row",
                  static_cast<unsigned long long>(GetTickCount64()), row.name, before, after);
        return;
    }
    row.actor = pickup;
    row.at = actor_location(pickup);
    row.kind = kC98KindSecret;
    row.value = static_cast<uint32_t>(after);
    const C95Fanout f = c98_host_world(row, g_actor_get_world(pickup), true);
    log::line("%scard289: host t=%llu secret %s credited, sent to %d clients (count %d -> %d)",
              f.sent == f.to ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), row.name, f.sent,
              before, after);
}

// Client: its own take of a placed secret. The credit is skipped (the host's row brings it); the base call still
// goes through the game's address, so card 254 decides it as for every pickup.
void c289_client_own_take(void* pickup) {
    char name[kAssetPathChars];
    if (c50_name(pickup, name, static_cast<int32_t>(sizeof(name))) <= 0) {
        snprintf(name, sizeof(name), "%s", object_name(pickup).text);
    }
    log::line("card289: client t=%llu own take of secret %s: credit skipped, the host's row credits it",
              static_cast<unsigned long long>(GetTickCount64()), name);
    reinterpret_cast<C50SelfFn>(const_cast<uint8_t*>(g_exe_base + offsets::AADPickupBase_Pickup_Implementation.rva))(
        pickup);
}

// Card 300 (client): the host closed a one-way closure; the client's copy of the presence runs its entry edge.
void c300_client_row(void* pc, const C98World& r, const char* text) {
    void* const presence = c95_find_nearest(g_actor_get_world(pc), r.name, r.at);
    if (presence == nullptr || g_c227_presence_class == nullptr || !is_a(presence, g_c227_presence_class())) {
        ++g_c98w.missed;
        c132_ledger_result(C132Result::Skipped, "no closure presence matches (see the WARN line)");
        log::line("WARN card300: client t=%llu world %s: no presence matches, closure not run",
                  static_cast<unsigned long long>(GetTickCount64()), text);
        return;
    }
    ++g_c98w.applied;
    c132_ledger_result(C132Result::Applied, nullptr);
    log::line("card300: client t=%llu closure %s from the host row%s", static_cast<unsigned long long>(GetTickCount64()),
              r.name, g_c98_replaying ? "; journal replay" : "");
    c300_client_close(presence, const_cast<void*>(c23_own_pawn(pc)));
}

} // namespace

// Card 300 (host): the all-players gate closed a one-way closure: its 'F' row to every client, journalled.
void c300_host_send(const void* presence) {
    C98World row{};
    if (c50_name(presence, row.name, static_cast<int32_t>(sizeof(row.name))) <= 0) {
        log::line("WARN card300: host closure %s: name not sendable, client not closed", object_name(presence).text);
        return;
    }
    row.actor = presence;
    row.at = actor_location(presence);
    row.kind = kC98KindClosure;
    row.value = 0;
    const C95Fanout f = c98_host_world(row, g_actor_get_world(presence), true);
    log::line("%scard300: host t=%llu closure %s row sent to %d of %d clients", f.sent == f.to ? "" : "WARN ",
              static_cast<unsigned long long>(GetTickCount64()), row.name, f.sent, f.to);
}

void detour_c289_secret_pickup(void* pickup) {
    if (!c289_ready() || !c50_placed(pickup)) {
        g_orig_c289_secret_pickup(pickup);
        return;
    }
    if (g_client_session) {
        c289_client_own_take(pickup);
        return;
    }
    if (net_mode(pickup) != kNetModeListenServer) {
        g_orig_c289_secret_pickup(pickup);
        return;
    }
    const int32_t before = c289_count(c289_meta(pickup));
    g_orig_c289_secret_pickup(pickup);
    c289_host_send(pickup, before, c289_count(c289_meta(pickup)));
}

namespace {

// Client: the host's 'G' row. The same credit on the client's own objects, once per secret per map.
void c289_client_secret(void* pc, const C98World& r, const char* text) {
    void* const actor = c95_find_nearest(g_actor_get_world(pc), r.name, r.at);
    if (actor == nullptr || g_c54_secret_class == nullptr || !is_a(actor, g_c54_secret_class()) || !c289_ready()) {
        ++g_c98w.missed;
        c132_ledger_result(C132Result::Skipped, "no placed secret matches (see the WARN line)");
        log::line("WARN card289: client t=%llu world %s: no placed secret pickup matches, skipped",
                  static_cast<unsigned long long>(GetTickCount64()), text);
        return;
    }
    const int32_t id = object_index(actor);
    if (c289_credited(id)) {
        c132_ledger_result(C132Result::Skipped, "already credited (card 289)");
        log::line("card289: client t=%llu secret %s already credited", static_cast<unsigned long long>(GetTickCount64()),
                  r.name);
        return;
    }
    if (g_c289.n < kC289Secrets) {
        g_c289.id[g_c289.n++] = id;
    } else {
        log::line("WARN card289: client credited list full (%d): %s not kept", kC289Secrets, r.name);
    }
    const C289Credit c = c289_credit(actor);
    ++g_c98w.applied;
    c132_ledger_result(C132Result::Applied, nullptr);
    log::line("%scard289: client t=%llu secret %s credited from the host row (count %d -> %d)%s%s",
              c.after > c.before ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), r.name, c.before,
              c.after, c.unlocked ? "; unlocked in the campaign slot" : "",
              g_c98_replaying ? "; journal replay" : "");
}

} // namespace

// Card 145 (replaces card 056's name journal): host, one placed pickup after a LoadFromSlot. The load sets bEnabled
// from the save without OnUpdatePickupStatus, so the 'E' row follows the pickup's state now. Journal only.
void c145_load_actor(const void* actor, C137LoadCounts& n) {
    if (!c145_is_pickup(actor) || !c50_placed(actor) || net_mode(actor) != kNetModeListenServer) {
        return;
    }
    const uint32_t enabled = c145_enabled(actor);
    C98World* const r = c98_journal_row(actor, kC98KindEnable);
    if ((r != nullptr ? r->value : 1u) == enabled) {
        return;
    }
    if (r != nullptr) {
        r->value = enabled;
        ++n.e_fixed;
    } else {
        C98World row{};
        if (c50_name(actor, row.name, sizeof(row.name)) <= 0) {
            return;
        }
        row.actor = actor;
        row.at = actor_location(actor);
        row.kind = kC98KindEnable;
        row.value = enabled;
        n.e_added += c98_journal_put(row) != nullptr ? 1u : 0u;
    }
    if (c137_load_line(n)) {
        log::line("card137: host load reconcile c145 E %s (%s): row now %u", object_name(actor).text,
                  class_name(actor).text, enabled);
    }
}

namespace {

// Client: whether `actor` is still in one of the world's level actor lists (a copy the game destroyed is not).
bool c104_in_world(const void* world, const void* actor) {
    const auto* const levels = static_cast<void* const*>(world != nullptr ? read_ptr(world, kWorldLevels) : nullptr);
    const int32_t num_levels = levels != nullptr ? read_i32(world, kWorldLevels + kArrayNum) : 0;
    bool found = false;
    for (int32_t l = 0; l < num_levels && l < kMaxLevels && !found; ++l) {
        auto* const* const actors = levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors))
                                                        : nullptr;
        const int32_t num = actors != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; a < num && a < kMaxLevelActors && !found; ++a) {
            found = actors[a] == actor;
        }
    }
    return found;
}

// Card 317 step 2: client. A copy whose root does not simulate is moved by the host's rows (moving samples, then the
// rest), so its own UProjectileMovementComponent must not move it too: spawned with no rotation, it flew along yaw 0
// at (3500, 0, 0) while the host's flew along the thrower's yaw, and fought every row. Stopped (Velocity zero) and
// deactivated (no tick) once at spawn. A simulating root keeps its physics (the rows already wake and blend it), and
// other movement components (a pickup's rotating movement) move nothing the rows carry.
constexpr uint32_t kC317StopLines = 32; // card317 stop rows per map
void c317_stop_copy_movement(void* actor, uint32_t host_id) {
    static void* projectile = nullptr;
    projectile = projectile != nullptr ? projectile : find_class(L"/Script/Engine.ProjectileMovementComponent");
    void* const root = read_ptr(actor, kActorRootComponent);
    void* const move = projectile != nullptr && root != nullptr && !c104_root_simulates(root)
                           ? find_component_of(actor, projectile)
                           : nullptr;
    if (move == nullptr || g_c317_stop_movement == nullptr || g_c317_deactivate == nullptr) {
        return;
    }
    const Vec3 v = read_vec3(move, kMoveVelocity);
    g_c317_stop_movement(move);
    g_c317_deactivate(move);
    if (note_event_cap(g_c98w.c317_stop_lines, kC317StopLines, "card317 stop")) {
        log::line("card317: client copy of host %u %s: %s stopped, velocity was (%.0f, %.0f, %.0f)", host_id,
                  class_name(actor).text, class_name(move).text, v.x, v.y, v.z);
    }
}

// Client: the host's 'P' row. Our copy runs its own Blueprint (the flare's smoke, the pod's fall and landing); the
// host's actor deals any damage.
void c104_client_spawn(void* pc, const C98World& r) {
    void* const cls = c74_find_asset(r.name, g_uclass_class());
    void* const world = cls != nullptr ? g_actor_get_world(pc) : nullptr;
    void* actor = nullptr;
    if (world != nullptr && g_c104_copies.n < kC104Copies) {
        alignas(16) uint8_t params[kSpawnParamsSize] = {};
        g_spawn_params_ctor(params);
        params[kSpawnCollisionOverride] = kSpawnAdjustAlways;
        const Vec3 rot{};
        g_c107_copy_spawn = true; // card 107: our runtime pickup block lets the host's copy through
        actor = g_world_spawn_actor(world, cls, &r.at, &rot, params);
        g_c107_copy_spawn = false;
    }
    if (actor != nullptr) {
        g_c104_copies.row[g_c104_copies.n++] = C104Copy{r.value, actor, false, false};
        c317_stop_copy_movement(actor, r.value);
        c317_motion_row("client", actor, "spawn", r.value);
    }
    log::line("%scard104: client t=%llu copy of host %u: %s at (%.0f, %.0f, %.0f) -> %s", actor != nullptr ? "" : "WARN ",
              static_cast<unsigned long long>(GetTickCount64()), r.value, r.name, r.at.x, r.at.y, r.at.z,
              actor != nullptr ? object_name(actor).text
                               : (cls == nullptr ? "class not found" : (world == nullptr ? "no world" : "no spawn")));
    c132_ledger_result(actor != nullptr ? C132Result::Applied : C132Result::Refused,
                       actor != nullptr ? nullptr
                                        : (cls == nullptr ? "class not found"
                                                          : (world == nullptr ? "no world" : "no spawn or table full")));
}

// Card 130 item 5: client. A live copy first sweeps to the host's last location (the 'D' row's x y z) and
// kC104SweepPast beyond it, so a projectile that falls slower than the host's (spawned with no rotation or velocity)
// still meets what the host's hit, and its own Blueprint hit and explode path runs there. True when the copy is gone
// after the move (its Blueprint destroyed it).
constexpr double kC104SweepPast = 50.0; // uu past the host's last point, along the move
bool c104_sweep_to(void* world, void* actor, const Vec3& to) {
    const Vec3 from = actor_location(actor);
    const Vec3 d{to.x - from.x, to.y - from.y, to.z - from.z};
    const double len = vec_len(d);
    if (len <= 1.0) {
        return false; // it is where the host's ended (a pickup, a landed pod): nothing to meet
    }
    const double k = (len + kC104SweepPast) / len;
    const Vec3 past{from.x + d.x * k, from.y + d.y * k, from.z + d.z * k};
    alignas(16) uint8_t hit[kHitSize] = {};
    const bool moved = g_set_actor_location(actor, &past, true, hit, 0);
    const bool gone = !c104_in_world(world, actor);
    log::line("card104: client t=%llu copy %s swept %.0f uu to the host's last point (%.0f, %.0f, %.0f): moved %d, %s",
              static_cast<unsigned long long>(GetTickCount64()), class_name(actor).text, len, to.x, to.y, to.z,
              moved ? 1 : 0, gone ? "its Blueprint ended it" : "still live");
    return gone;
}

// Card 287: client. A relayed body's motion between host rows. c104_sample_moving sends a moving sample every 100 ms
// while the host's body moves 10 uu, and the old apply teleported the copy to each row and put it to sleep, so a
// rolling flare stood still and jumped. Now each copy in motion has one entry here: the pose it shows now (`shown`),
// the pose of the newest row (`to`), and the time to get there (the spacing of the last two rows). The client tick
// (c287_client_step) moves every entry's copy toward `to`, place linear and rotation along the shortest quaternion
// arc, its body asleep so its own physics does not fight the move. A rest row ends the motion: the copy glides to
// the rest pose over the same time, is set exactly there, sleeps, and the entry goes. A row more than
// kC287TeleportUu from the copy is set at once (a late joiner, a missed row). Membership is the state: a copy at rest
// has no entry. One table for every card 104 copy, bounded by kC104Copies like the copies.
constexpr double kC287TeleportUu = 300.0; // a row this far from the copy is set at once, not blended
constexpr double kC470HopMaxUu = 1500.0;  // card 470: a host sample-to-sample step above this is a real teleport
constexpr uint32_t kC287FirstMs = 100;    // the first sample's blend time (the host's sample spacing)
constexpr uint32_t kC287MinMs = 16;       // blend time bounds, so a burst or a gap in rows neither snaps nor crawls
constexpr uint32_t kC287MaxMs = 500;
constexpr uint32_t kC287Lines = 48;       // card287 rows per map (two per copy that moves)

struct C287Pose {
    Vec3 at;
    double q[4]; // X, Y, Z, W
};
struct C287Blend {
    uint32_t host_id;
    C31Weak copy;
    C287Pose from, to, shown;
    uint32_t start_ms, dur_ms; // GetTickCount64 at the newest row, and the time from `from` to `to`
    uint32_t first_ms;         // the first moving sample's arrival
    uint32_t samples, teleports;
    double largest;            // uu: the largest move one tick made
    bool rest;                 // `to` is the rest pose: sleep there and leave the table
    bool first_set;            // card 470: the first moving sample was set at once (counted in `teleports`)
};
struct C287Blends {
    C287Blend row[kC104Copies];
    int32_t n;
    uint32_t lines;
};
C287Blends g_c287{}; // client, game thread

constexpr double kC287HalfRad = 3.14159265358979323846 / 360.0; // degrees to radians, halved
constexpr double kC287Deg = 180.0 / 3.14159265358979323846;

// FRotator::Quaternion (pitch, yaw, roll in degrees).
void c287_quat(const double rot[3], double q[4]) {
    const double sp = sin(rot[0] * kC287HalfRad), cp = cos(rot[0] * kC287HalfRad);
    const double sy = sin(rot[1] * kC287HalfRad), cy = cos(rot[1] * kC287HalfRad);
    const double sr = sin(rot[2] * kC287HalfRad), cr = cos(rot[2] * kC287HalfRad);
    q[0] = cr * sp * sy - sr * cp * cy;
    q[1] = -cr * sp * cy - sr * cp * sy;
    q[2] = cr * cp * sy - sr * sp * cy;
    q[3] = cr * cp * cy + sr * sp * sy;
}

// FQuat::Rotator (degrees), with its gimbal-lock branches.
void c287_rotator(const double q[4], double rot[3]) {
    const double sing = q[2] * q[0] - q[3] * q[1];
    const double yaw = atan2(2.0 * (q[3] * q[2] + q[0] * q[1]), 1.0 - 2.0 * (q[1] * q[1] + q[2] * q[2])) * kC287Deg;
    const double lock = 0.4999995;
    const double xw = 2.0 * atan2(q[0], q[3]) * kC287Deg;
    rot[1] = yaw;
    rot[0] = sing < -lock ? -90.0 : (sing > lock ? 90.0 : asin(2.0 * sing) * kC287Deg);
    rot[2] = sing < -lock ? -yaw - xw
                          : (sing > lock ? yaw - xw
                                         : atan2(-2.0 * (q[3] * q[0] + q[1] * q[2]),
                                                 1.0 - 2.0 * (q[0] * q[0] + q[1] * q[1])) * kC287Deg);
}

// The pose between `a` (t 0) and `b` (t 1): place linear, rotation along the shortest arc (normalized lerp when the
// two are nearly equal, where slerp's sine is near 0).
C287Pose c287_mix(const C287Pose& a, const C287Pose& b, double t) {
    C287Pose out{};
    out.at = Vec3{a.at.x + (b.at.x - a.at.x) * t, a.at.y + (b.at.y - a.at.y) * t, a.at.z + (b.at.z - a.at.z) * t};
    double dot = a.q[0] * b.q[0] + a.q[1] * b.q[1] + a.q[2] * b.q[2] + a.q[3] * b.q[3];
    const double sign = dot < 0.0 ? -1.0 : 1.0;
    dot *= sign;
    double wa = 1.0 - t, wb = t * sign;
    if (dot < 0.9995) {
        const double ang = acos(dot), sn = sin(ang);
        wa = sin((1.0 - t) * ang) / sn;
        wb = sin(t * ang) / sn * sign;
    }
    double len = 0.0;
    for (int32_t k = 0; k < 4; ++k) {
        out.q[k] = a.q[k] * wa + b.q[k] * wb;
        len += out.q[k] * out.q[k];
    }
    len = len > 0.0 ? sqrt(len) : 1.0;
    for (int32_t k = 0; k < 4; ++k) {
        out.q[k] /= len;
    }
    return out;
}

// The copy's pose now: its place and its root's rotation (a simulating root has no parent).
C287Pose c287_pose_of(const void* actor, const void* root) {
    C287Pose p{};
    p.at = actor_location(actor);
    const Vec3 r = read_vec3(root, kC104RelativeRotation);
    const double rot[3] = {r.x, r.y, r.z};
    c287_quat(rot, p.q);
    return p;
}

// Sets the copy's actor to pose `p`, its root body asleep. False when the copy or its root is gone.
bool c287_set(void* actor, const C287Pose& p) {
    void* const root = read_ptr(actor, kActorRootComponent);
    if (root == nullptr) {
        return false;
    }
    double rot[3] = {};
    c287_rotator(p.q, rot);
    g_set_actor_location(actor, &p.at, false, nullptr, kTeleportPhysics);
    g_set_world_rotation(root, rot, false, nullptr, kTeleportPhysics);
    if (g_prim_sleep != nullptr && c104_root_is_primitive(root)) {
        g_prim_sleep(root, 0); // NAME_None: the root body
    }
    return true;
}

int32_t c287_find(uint32_t host_id) {
    int32_t at = -1;
    for (int32_t i = 0; i < g_c287.n && at < 0; ++i) {
        at = g_c287.row[i].host_id == host_id ? i : -1;
    }
    return at;
}

void c287_drop(int32_t i) {
    g_c287.row[i] = g_c287.row[--g_c287.n];
}

// The rest row: the copy's whole motion, at rest, measured after the last set.
void c287_rest_line(const C287Blend& b, void* actor, const char* how) {
    if (g_c287.lines >= kC287Lines) {
        return;
    }
    ++g_c287.lines;
    const uint32_t now = static_cast<uint32_t>(GetTickCount64());
    // Card 470: the hops after the first sample apart from the first sample's set-at-once, so the row reads without
    // subtraction.
    const uint32_t first = b.first_set ? 1u : 0u;
    log::line("card287: client t=%llu copy %s host %u: %u samples over %u ms, largest step %.1f uu, at rest %.1f uu "
              "from the host's (%s, %u hops after the first sample, first sample %s)",
              static_cast<unsigned long long>(GetTickCount64()), object_name(actor).text, b.host_id, b.samples,
              now - b.first_ms, b.largest, vec_dist(actor_location(actor), b.to.at), how, b.teleports - first,
              b.first_set ? "set at once" : "glided");
}

// Card 287: client. Row `r` ('M') for copy `actor`: a moving sample starts or retargets the copy's blend; a rest row
// with a blend running becomes its last glide. True when the row is handled here; false sends a rest row with no
// motion running (or one too far to glide) to the old set-at-once path.
bool c287_take(uint32_t host_id, void* actor, void* root, const Vec3& at, const double rot[3], bool moving) {
    int32_t i = c287_find(host_id);
    if (!moving && i < 0) {
        return false; // nothing moving: c104_client_rest sets the rest pose at once, as before
    }
    const uint32_t now = static_cast<uint32_t>(GetTickCount64());
    const C287Pose now_pose = c287_pose_of(actor, root);
    // Card 470 (b40b168a flare host 122633: 6 set-at-once hops in flight): a copy that moves over kC287TeleportUu
    // between two host samples is one sample behind by design. Only a copy more than two samples behind, or a host
    // step above kC470HopMaxUu, is set at once. A copy with no entry yet has no last sample: the plain limit holds.
    const double lag = vec_dist(now_pose.at, at);
    const double hop = i >= 0 ? vec_dist(g_c287.row[i].to.at, at) : 0.0;
    const bool too_far = lag > kC287TeleportUu && (lag > 2.0 * hop || hop > kC470HopMaxUu);
    if (!moving && too_far) {
        g_c287.row[i].to.at = at; // too far to glide: set at once, asleep, and the motion ends here
        c287_quat(rot, g_c287.row[i].to.q);
        ++g_c287.row[i].teleports;
        c287_set(actor, g_c287.row[i].to);
        c287_rest_line(g_c287.row[i], actor, "set at once");
        c287_drop(i);
        return true;
    }
    if (i < 0) {
        if (g_c287.n >= kC104Copies) {
            log::line("WARN card287: client blend table full (%d): copy %s host %u set at once", g_c287.n,
                      object_name(actor).text, host_id);
            return false;
        }
        i = g_c287.n++;
        g_c287.row[i] = C287Blend{};
        g_c287.row[i].host_id = host_id;
        c31_weak_set(g_c287.row[i].copy, actor);
        g_c287.row[i].shown = now_pose;
        g_c287.row[i].first_ms = now;
        g_c287.row[i].start_ms = now - kC287FirstMs; // the first blend takes kC287FirstMs
        if (g_c287.lines < kC287Lines) {
            ++g_c287.lines;
            log::line("card287: client t=%llu copy %s host %u: first moving sample, %.0f uu from the copy%s",
                      static_cast<unsigned long long>(GetTickCount64()), object_name(actor).text, host_id,
                      vec_dist(now_pose.at, at), too_far ? ", set at once" : "");
        }
    }
    C287Blend& b = g_c287.row[i];
    const uint32_t gap = now - b.start_ms;
    b.from = b.shown;
    b.to.at = at;
    c287_quat(rot, b.to.q);
    b.dur_ms = gap < kC287MinMs ? kC287MinMs : (gap > kC287MaxMs ? kC287MaxMs : gap);
    b.start_ms = now;
    b.rest = !moving;
    b.samples += moving ? 1u : 0u;
    if (too_far) {
        b.from = b.to; // set at once: the tick puts it there with no step counted
        b.shown = b.to;
        b.first_set = b.first_set || b.samples <= 1u; // card 470: this set is the first moving sample's
        ++b.teleports;
        c287_set(actor, b.to);
    }
    return true;
}

// Card 287: client. One blend entry's tick. False when the entry leaves the table (copy gone, or at rest).
bool c287_tick_one(C287Blend& b, uint32_t now) {
    void* const actor = c31_weak_get(b.copy);
    if (actor == nullptr) {
        return false;
    }
    const uint32_t spent = now - b.start_ms;
    const bool done = spent >= b.dur_ms;
    if (done && !b.rest && b.shown.at.x == b.to.at.x && b.shown.at.y == b.to.at.y && b.shown.at.z == b.to.at.z) {
        return true; // at the newest sample, asleep: wait for the next row
    }
    const C287Pose p = done ? b.to : c287_mix(b.from, b.to, static_cast<double>(spent) / b.dur_ms);
    const double step = vec_dist(p.at, b.shown.at);
    b.largest = step > b.largest ? step : b.largest;
    b.shown = p;
    if (!c287_set(actor, p)) {
        return false;
    }
    if (done && b.rest) {
        c287_rest_line(b, actor, "glided");
        return false;
    }
    return true;
}

// Client: the host's 'D' row: our copy goes too, unless its own Blueprint destroyed it already.
void c104_client_destroy(void* pc, uint32_t host_id, const Vec3& last) {
    int32_t at = -1;
    for (int32_t i = 0; i < g_c104_copies.n && at < 0; ++i) {
        at = g_c104_copies.row[i].host_id == host_id ? i : -1;
    }
    void* const world = g_actor_get_world(pc);
    void* const actor = at >= 0 ? g_c104_copies.row[at].actor : nullptr;
    const bool live = actor != nullptr && c104_in_world(world, actor) && !c104_sweep_to(world, actor, last);
    if (at >= 0) {
        g_c104_copies.row[at] = g_c104_copies.row[--g_c104_copies.n];
    }
    const int32_t blend = c287_find(host_id);
    if (blend >= 0) {
        c287_drop(blend); // card 287: its motion ends with it
    }
    if (live) {
        g_orig_c98_destroy(world, actor, false, true);
    }
    log::line("card104: client t=%llu host %u destroyed: our copy %s", static_cast<unsigned long long>(GetTickCount64()),
              host_id, at < 0 ? "unknown" : (live ? "destroyed" : "already gone"));
    c132_ledger_result(at < 0 ? C132Result::Skipped : C132Result::Applied,
                       at < 0 ? "no copy of that host id" : (live ? nullptr : "already gone"));
}

// Card 132 note 2: client. The host's 'M' row: our copy goes to the host's rest location and rotation and sleeps, so
// a pickup's copy (a late joiner's falls from the spawn point and lands elsewhere) lies where the host's does.
void c104_client_rest(void* pc, const C98World& r) {
    int32_t at = -1;
    for (int32_t i = 0; i < g_c104_copies.n && at < 0; ++i) {
        at = g_c104_copies.row[i].host_id == r.value ? i : -1;
    }
    void* const world = g_actor_get_world(pc);
    void* const actor = at >= 0 && c104_in_world(world, g_c104_copies.row[at].actor) ? g_c104_copies.row[at].actor
                                                                                   : nullptr;
    double rot[3] = {};
    int32_t moving = 0; // card 287: the 4th field, 1 on a moving sample (a row with three is a rest)
    const int32_t fields = sscanf_s(r.name, "%lf,%lf,%lf,%d", &rot[0], &rot[1], &rot[2], &moving);
    const bool parsed = fields >= 3;
    void* const root = actor != nullptr ? read_ptr(actor, kActorRootComponent) : nullptr;
    const Vec3 from = actor != nullptr ? actor_location(actor) : Vec3{};
    const bool is_moving = fields == 4 && moving != 0;
    if (root != nullptr && parsed && !is_moving && !g_c104_copies.row[at].c317_logged) {
        g_c104_copies.row[at].c317_logged = true; // card 317: the copy's own motion before the host's rest is applied
        c317_motion_row("client", actor, "first rest", r.value);
    }
    const bool blended = root != nullptr && parsed && c287_take(r.value, actor, root, r.at, rot, is_moving);
    const int32_t blend = blended ? -1 : c287_find(r.value);
    if (blend >= 0) {
        c287_drop(blend); // card 287: the copy or its root is gone, or the table was full
    }
    bool moved = false;
    if (root != nullptr && parsed && !blended) {
        moved = g_set_actor_location(actor, &r.at, false, nullptr, kTeleportPhysics);
        g_set_world_rotation(root, rot, false, nullptr, kTeleportPhysics);
        if (g_prim_sleep != nullptr && c104_root_is_primitive(root)) {
            g_prim_sleep(root, 0); // NAME_None: the root body
        }
    }
    static uint32_t lines = 0;
    if (!blended && (lines < kC104RestLines || root == nullptr)) {
        ++lines;
        log::line("%scard104: client t=%llu host %u rest at (%.0f, %.0f, %.0f) rot (%s): our copy %s %s", root != nullptr && parsed ? "" : "WARN ",
                  static_cast<unsigned long long>(GetTickCount64()), r.value, r.at.x, r.at.y, r.at.z, r.name,
                  actor != nullptr ? object_name(actor).text : "-",
                  !parsed ? "bad rotation" : (root == nullptr ? (at < 0 ? "unknown host id" : "gone or no root")
                                                              : (moved ? "moved and put to sleep" : "set (no move)")));
        if (root != nullptr && parsed) {
            log::line("card104: client copy %s was %.0f uu from the host's rest", object_name(actor).text,
                      vec_len(Vec3{r.at.x - from.x, r.at.y - from.y, r.at.z - from.z}));
        }
    }
    if (root != nullptr && parsed && !is_moving && !g_c104_copies.row[at].rest_logged &&
        note_event_cap(g_c98w.c256_client_lines, static_cast<uint32_t>(kC104Copies), "card256 client rest")) {
        g_c104_copies.row[at].rest_logged = true; // card 256: one line per copy, at its first applied rest row
        const bool primitive = c104_root_is_primitive(root);
        log::line("card256: client copy %u rest applied, moved %.0f uu, root collision %d, simulating %d", r.value,
                  vec_dist(r.at, from), primitive ? static_cast<int>(static_cast<const uint8_t*>(root)[kBodyCollisionEnabled]) : -1,
                  primitive && g_prim_simulating != nullptr && g_prim_simulating(root, 0) ? 1 : 0);
    }
    c132_ledger_result(root != nullptr && parsed ? C132Result::Applied : C132Result::Skipped,
                       root != nullptr && parsed ? nullptr : (at < 0 ? "no copy of that host id" : "copy gone or bad row"));
}


// Card 140: client. The host's 'A' row: our copy of a placed actor (found by name nearest its load place, which both
// sides share) goes where the host's stands still, with its rotation.
void c140_client_placed(void* pc, const C98World& r) {
    char placed[kAssetPathChars] = {};
    double anchor[3] = {}, rot[3] = {};
    const char* const bar = strchr(r.name, '|');
    const size_t len = bar != nullptr ? static_cast<size_t>(bar - r.name) : 0;
    const bool parsed = len > 0 && len < sizeof(placed) &&
                        sscanf_s(bar, "|%lf,%lf,%lf|%lf,%lf,%lf", &anchor[0], &anchor[1], &anchor[2], &rot[0], &rot[1],
                                 &rot[2]) == 6;
    if (parsed) {
        memcpy(placed, r.name, len);
    }
    void* const actor = parsed ? c95_find_nearest(g_actor_get_world(pc), placed, Vec3{anchor[0], anchor[1], anchor[2]})
                               : nullptr;
    void* const root = actor != nullptr ? read_ptr(actor, kActorRootComponent) : nullptr;
    const Vec3 from = root != nullptr ? actor_location(actor) : Vec3{};
    if (root != nullptr) {
        g_set_actor_location(actor, &r.at, false, nullptr, kTeleportPhysics);
        g_set_world_rotation(root, rot, false, nullptr, kTeleportPhysics);
        if (g_prim_sleep != nullptr && c104_root_is_primitive(root)) {
            g_prim_sleep(root, 0); // NAME_None: the root body
        }
        ++g_c98w.applied;
    } else {
        ++g_c98w.missed;
    }
    static uint32_t lines = 0;
    if (lines < kC104RestLines || root == nullptr) {
        ++lines;
        log::line("%scard140: client t=%llu placed %s to (%.0f, %.0f, %.0f): %s, was %.0f uu away", root != nullptr ? "" : "WARN ",
                  static_cast<unsigned long long>(GetTickCount64()), r.name, r.at.x, r.at.y, r.at.z,
                  !parsed ? "bad row" : (root == nullptr ? "no copy by that name" : object_name(actor).text),
                  root != nullptr ? vec_dist(r.at, from) : 0.0);
    }
    c132_ledger_result(root != nullptr ? C132Result::Applied : C132Result::Skipped,
                       root != nullptr ? nullptr : (parsed ? "no copy by that name" : "bad row"));
}

// Card 140: host. A simulating relayed body that is still moving: the rest rows alone left the client's copy where
// the last rest put it while the host's rolled on (c140j: the tutorial bombing-run flare rolled 234 uu down the pit
// slope over 3 s with no row). While awake, a body more than kC104SampleUu from its last row sends one, at most
// every kC104SampleMs; the client moves its copy there (the gate's tolerance is 100 uu). Its next rest sends the
// rest row as before. Projectiles' roots do not simulate and never come here. Card 287: the row is marked moving,
// and the client blends its copy toward it (c287_take) instead of setting it there asleep.
void c104_sample_moving(int32_t i, const void* actor, const void* root, const Vec3& at) {
    C104Rest& s = g_c104_relayed.rest[i];
    const uint32_t now = static_cast<uint32_t>(GetTickCount64());
    const double off = vec_len(Vec3{at.x - s.rest_at.x, at.y - s.rest_at.y, at.z - s.rest_at.z});
    if (off <= kC104SampleUu || now - s.row_ms < kC104SampleMs) {
        return;
    }
    s.rest_at = at;
    ++g_c104_pending.samples;
    c104_send_rest(i, actor, root, at, true);
}

// Card 132 note 2: host. One relayed actor's rest edge; see C104Rest. Card 140: a root that does not simulate is
// moved only by game logic (a Blueprint SetActorLocation, an attach); it gets a new rest row when it stands still
// kC104StillTicks ticks more than kC104KickUu from the last row's place. Card 317 step 2: while such a root moves
// (a projectile movement component, an interp, a Blueprint move: the thrown flare, the bombing-run bombs, the bomber,
// both pods) it sends the same moving samples as a simulating body, and the rest row after them is always sent, so
// the copy's blend ends asleep at the host's rest.
void c104_settle_one(int32_t i) {
    void* const actor = c31_weak_get(g_c104_relayed.weak[i]);
    void* const root = actor != nullptr ? read_ptr(actor, kActorRootComponent) : nullptr;
    if (root == nullptr) {
        return;
    }
    const bool simulating = c104_root_simulates(root);
    C104Rest& s = g_c104_relayed.rest[i];
    const Vec3 at = actor_location(actor);
    const double step = vec_len(Vec3{at.x - s.last.x, at.y - s.last.y, at.z - s.last.z});
    s.still = step < kC104StillUu ? static_cast<uint16_t>(s.still < kC104StillTicks ? s.still + 1 : s.still) : 0;
    s.last = at;
    const bool off_rest = vec_len(Vec3{at.x - s.rest_at.x, at.y - s.rest_at.y, at.z - s.rest_at.z}) > kC104KickUu;
    if (!simulating) {
        if (s.still < kC104StillTicks) {
            const uint32_t samples_before = g_c104_pending.samples;
            c104_sample_moving(i, actor, root, at);
            s.sampled = s.sampled || g_c104_pending.samples != samples_before;
            return;
        }
        if (off_rest || s.sampled) {
            s.rested = true;
            s.sampled = false;
            s.rest_at = at;
            c104_send_rest(i, actor, root, at, false);
            c256_host_rest(i, actor, false, false);
        }
        return;
    }
    if (s.rested) {
        s.rested = !off_rest;
        s.still = s.rested ? s.still : 0;
        return;
    }
    const bool asleep = !g_prim_awake(root, 0);
    if (!asleep && s.still < kC104StillTicks) {
        c104_sample_moving(i, actor, root, at);
        return;
    }
    s.rested = true;
    s.rest_at = at;
    static const void* logged[kC104RestClasses] = {};
    const void* const cls = read_ptr(actor, kObjectClass);
    bool known = false;
    int32_t free_at = -1;
    for (int32_t c = 0; c < kC104RestClasses && !known; ++c) {
        known = logged[c] == cls;
        free_at = free_at < 0 && logged[c] == nullptr ? c : free_at;
    }
    if (!known && free_at >= 0) {
        logged[free_at] = cls;
        log::line("card104: host settle class %s: first rest row (%s)", object_name(cls).text,
                  asleep ? "physics asleep" : "still for kC104StillTicks ticks");
    }
    c104_send_rest(i, actor, root, at, false);
    c256_host_rest(i, actor, true, asleep);
}

} // namespace

// Card 256 class fix 2: host. The digest's mismatch detail (card 237, a client's "X<round> <key> ...") named `actor`:
// a relayed actor gets its rest row ('M', where it stands now) again, at most once per actor per round, so a row that
// was lost or not applied heals in one round for every relayed class.
void c256_heal_rest(const void* actor, uint32_t round) {
    int32_t i = 0;
    while (i < g_c104_relayed.n && g_c104_relayed.actor[i] != actor) {
        ++i;
    }
    const void* const root = i < g_c104_relayed.n ? read_ptr(actor, kActorRootComponent) : nullptr;
    if (root == nullptr || g_c104_relayed.rest[i].heal_round == round + 1) {
        return;
    }
    C104Rest& s = g_c104_relayed.rest[i];
    s.heal_round = round + 1;
    const Vec3 at = actor_location(actor);
    c104_send_rest(i, actor, root, at, !s.rested); // card 287: a body still moving heals with a moving sample
    if (note_event_cap(g_c98w.c256_host_lines, static_cast<uint32_t>(kC104Copies), "card256 host rest")) {
        log::line("card256: host %d rest row sent again at (%.0f, %.0f, %.0f) for digest round %u", object_index(actor),
                  at.x, at.y, at.z, round);
    }
}

namespace {

// Card 140: host. A placed, unreplicated actor that host logic moved. c140h to c140k: the TacSupport_SO4/SO41
// objective marker ActorReference_C_0 follows the host's resupply rack, and the client's stays where the map put it
// ("no row sent" every digest round). Its mover is a Blueprint SetActorLocation with no mod hook and no engine
// replication, so the host polls: the card 132 ext sweep hands every level actor to c140_placed_visit. An actor is
// first seen where the map put it (its anchor, which both sides share), and when it stands still more than
// kC104KickUu from its last sent place it sends an 'A' row: the client finds its copy by name nearest the anchor.
// An attached root follows its parent and is left out, as are the ext actors (ext_c132.cpp owns their motion) and
// actors that never stand still.
constexpr int32_t kC140PlacedSlots = 65536;  // power of 2; FED_C2M1 has about 31000 level actors
constexpr int32_t kC140PlacedMax = kC140PlacedSlots / 4 * 3;
constexpr double kC140PlacedStillUu = 1.0;   // between two visits: standing still
constexpr uint32_t kC140PlacedJournal = 128; // 'A' rows journalled per map (the journal of kC98WorldRows is shared)
constexpr uint8_t kC140Movable = 2;          // EComponentMobility::Movable (low nibble of kComponentMobility)
constexpr uint32_t kC140MoveWarns = 8;       // WARN lines for a root that moved without being Movable
constexpr int32_t kC140PlacedClasses = 32;   // classes named in a first-row line

struct C140Placed {
    const void* actor; // compared only; the level arrays give the live pointer
    uint64_t fname;    // with the pointer: a new actor at a freed address is a new entry
    float anchor[3], sent[3], prev[3];
};
struct C140PlacedState {
    C140Placed slot[kC140PlacedSlots];
    int32_t n;
    uint32_t rows, journalled, full, unnamed;
    const void* cls[kC140PlacedClasses];
    int32_t ncls;
};
C140PlacedState g_c140_placed{}; // game thread, host

Vec3 c140_vec(const float* f) {
    return Vec3{f[0], f[1], f[2]};
}

void c140_set(float* f, const Vec3& v) {
    f[0] = static_cast<float>(v.x);
    f[1] = static_cast<float>(v.y);
    f[2] = static_cast<float>(v.z);
}

// The actor's entry; a new one (fresh) holds `at` as its anchor. Null when the table is full.
C140Placed* c140_placed_entry(const void* actor, const Vec3& at, bool& fresh) {
    const uint64_t fname = read_u64(actor, kObjectName);
    const uint32_t h = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(actor) >> 4) * 2654435761u;
    fresh = false;
    for (int32_t i = 0; i < kC140PlacedSlots; ++i) {
        C140Placed& s = g_c140_placed.slot[(h + static_cast<uint32_t>(i)) & (kC140PlacedSlots - 1)];
        if (s.actor == actor && s.fname == fname) {
            return &s;
        }
        if (s.actor != nullptr) {
            continue;
        }
        if (g_c140_placed.n >= kC140PlacedMax) {
            ++g_c140_placed.full;
            return nullptr;
        }
        ++g_c140_placed.n;
        s.actor = actor;
        s.fname = fname;
        c140_set(s.anchor, at);
        c140_set(s.sent, at);
        c140_set(s.prev, at);
        fresh = true;
        return &s;
    }
    return nullptr;
}

} // namespace

// USceneComponent::AttachParent, found once by reflection on the first root (0: not found, nothing counts attached).
// Null for a root that is not attached.
const void* c140_attach_parent(const void* root) {
    static int32_t offset = -1;
    if (root == nullptr) {
        return nullptr;
    }
    if (offset < 0) {
        const uint8_t* const f = prop_find_prop(read_ptr(root, kObjectClass), "AttachParent");
        offset = f != nullptr ? read_i32(f, kPropertyOffset) : 0;
        log::line("%scard140: %s AttachParent offset 0x%X", offset > 0 ? "" : "WARN ",
                  g_client_session ? "client" : "host", static_cast<unsigned>(offset));
    }
    return offset > 0 ? read_ptr(root, static_cast<size_t>(offset)) : nullptr;
}

namespace {

bool c140_root_attached(const void* root) {
    return c140_attach_parent(root) != nullptr;
}

void c140_note_class(const void* actor, const C98World& row) {
    const void* const cls = read_ptr(actor, kObjectClass);
    for (int32_t i = 0; i < g_c140_placed.ncls; ++i) {
        if (g_c140_placed.cls[i] == cls) {
            return;
        }
    }
    if (g_c140_placed.ncls >= kC140PlacedClasses) {
        return;
    }
    g_c140_placed.cls[g_c140_placed.ncls++] = cls;
    log::line("card140: host placed class %s: first move row %s at (%.0f, %.0f, %.0f)", object_name(cls).text, row.name,
              row.at.x, row.at.y, row.at.z);
}

// The 'A' row: name "<placed name>|<anchor x,y,z>|<pitch,yaw,roll>" (an unattached root's relative rotation is its
// world rotation), at = where it stands. One journal row per actor, the last place wins, up to kC140PlacedJournal.
void c140_send_placed(const void* actor, const void* root, C140Placed& s, const Vec3& at) {
    char placed[kAssetPathChars];
    const Vec3 rot = read_vec3(root, kC104RelativeRotation);
    C98World row{};
    const int len = c50_name(actor, placed, sizeof(placed)) > 0
                        ? snprintf(row.name, sizeof(row.name), "%s|%.0f,%.0f,%.0f|%.2f,%.2f,%.2f", placed, s.anchor[0],
                                   s.anchor[1], s.anchor[2], rot.x, rot.y, rot.z)
                        : -1;
    c140_set(s.sent, at); // a row that cannot be named is not tried again until the actor moves on
    if (len <= 0 || static_cast<size_t>(len) >= sizeof(row.name)) {
        ++g_c140_placed.unnamed;
        return;
    }
    row.actor = actor;
    row.at = at;
    row.kind = kC98KindPlaced;
    const bool had = c98_journal_row(actor, kC98KindPlaced) != nullptr;
    const bool journal = had || g_c140_placed.journalled < kC140PlacedJournal;
    g_c140_placed.journalled += journal && !had ? 1u : 0u;
    ++g_c140_placed.rows;
    c140_note_class(actor, row);
    c98_host_world(row, g_actor_get_world(actor), journal);
}

} // namespace

uint32_t g_c140_visit_passed = 0;
uint32_t g_c140_visit_entry = 0;

// Card 140: host, from the card 132 ext sweep (ext_c132.cpp), for every level actor that is not an ext actor.
// Card 330: the engine refuses to move a root that is not Movable in a game world, so such a root is looked at only
// in a deep sweep (ext_c132.cpp kExtDeepEvery); one that moved all the same still sends its row then, with a WARN.
// Card 401: an actor every sweep must visit (c140_placed_visit with `full` false gets past its mobility test).
bool c140_placed_candidate(const void* actor) {
    if (g_client_session || !c50_placed(actor) || c98_replicates(actor)) {
        return false;
    }
    const void* const root = read_ptr(actor, kActorRootComponent);
    return root != nullptr && (static_cast<const uint8_t*>(root)[kComponentMobility] & 0x0F) == kC140Movable;
}

void c140_placed_visit(const void* actor, bool full) {
    if (g_client_session || !c50_placed(actor) || c98_replicates(actor)) {
        return;
    }
    ++g_c140_visit_passed; // card 330 split
    const void* const root = read_ptr(actor, kActorRootComponent);
    if (root == nullptr) {
        return;
    }
    const uint8_t mobility = static_cast<const uint8_t*>(root)[kComponentMobility] & 0x0F;
    if (mobility != kC140Movable && !full) {
        return;
    }
    if (c140_root_attached(root) || net_mode(actor) != kNetModeListenServer) {
        return;
    }
    const Vec3 at = actor_location(actor);
    bool fresh = false;
    ++g_c140_visit_entry; // card 330 split
    C140Placed* const s = c140_placed_entry(actor, at, fresh);
    if (s == nullptr || fresh) {
        return;
    }
    const double step = vec_dist(at, c140_vec(s->prev));
    c140_set(s->prev, at);
    if (step > kC140PlacedStillUu || vec_dist(at, c140_vec(s->sent)) <= kC104KickUu) {
        return;
    }
    static uint32_t warns = 0;
    if (mobility != kC140Movable && warns < kC140MoveWarns) {
        ++warns;
        log::line("WARN card140: host placed %s %s moved to (%.0f, %.0f, %.0f) with root mobility %u, not Movable: "
                  "seen by the full sweep only",
                  class_name(actor).text, object_name(actor).text, at.x, at.y, at.z, static_cast<unsigned>(mobility));
    }
    c140_send_placed(actor, root, *s, at);
}

namespace {

} // namespace

// Card 132 note 2: host, per tick. Every relayed actor sends a rest row on each new rest (card 140: a root that does
// not simulate too, on each move). Deferred spawns' 'P' rows go first.
void c104_settle_step() {
    if (g_client_session || g_prim_awake == nullptr || g_prim_simulating == nullptr) {
        return;
    }
    c104_flush_pending(); // card 140: deferred spawns' 'P' rows, at their finished place
    for (int32_t i = 0; i < g_c104_relayed.n; ++i) {
        c104_settle_one(i);
    }
}

// Card 287: client, per tick. Every card 104 copy in motion moves one tick toward its newest host row; an entry whose
// copy is gone or that reached its rest pose leaves the table (swap with the last, so that slot is ticked again).
void c287_client_step() {
    if (!g_client_session || g_c287.n == 0) {
        return;
    }
    const uint32_t now = static_cast<uint32_t>(GetTickCount64());
    int32_t i = 0;
    for (int32_t guard = 0; guard < kC104Copies && i < g_c287.n; ++guard) {
        if (c287_tick_one(g_c287.row[i], now)) {
            ++i;
        } else {
            c287_drop(i);
        }
    }
}

namespace {

// Card 131: the client's kill of a placed copy (the whole 'K' class). InstaKillTarget does nothing here: its damage
// spec reaches UAbilitySystemComponent::ApplyGameplayEffectSpecToSelf, which applies nothing on a non-authority
// owner without a valid prediction key (exe+0x494D259), so IsDead stayed 0 (c130d4, c130d5). The client instead makes
// the two effects the host's kill has on its own copy: the Health base value goes to 0 (HealthAttributeChanged ->
// RefreshState: IsDead, the Dead tag, OnHealthAttributeChanged), then OnDeath (UADHealthComponent+0xF8, the
// delegate the Blueprint binds) is broadcast with a damage event naming the receiver, as FatalDamageEffectReceived
// does. Kill credit (the causer interface) and the death gameplay event are host logic and are not made here.
constexpr size_t kDamageEventSize = 0x1E0;          // FADDamageEvent (StructParams SizeOf)
constexpr size_t kDamageEventReceiver = 0x10;       // FADDamageEvent::Receiver
constexpr size_t kDamageEventReceiverHealth = 0x18; // FADDamageEvent::ReceiverHealthComponent
constexpr size_t kHealthComponentOnDeath = 0xF8;    // UADHealthComponent::OnDeath (NewProp_OnDeath offset)

// Card 137 step 2: a join replay puts a dead building in place the way a save load does, not by its death. The
// SaveComponent's SaveFullyLoaded bound event (BP_KillableBuilding_Base 2268 -> 2129) runs Ambient FX Setup on the
// existing LoopingVFX / LoopingSFX components (no new component, no loop), then with Destroyed set: ShowOnMinimap
// false, Setup Dead From Load (HideActor when HideOnDestroy), Setup Hitmarker Tag. No destruction FX, no blast and no
// fuse (OnDeath ran the DamageDealer Destroy override: ApplyDamageAtLocation and fuse re-arms). LoopingVFX.Activate
// there is a card 136 capture whose owner is a placed actor, so it is not client-own and is blocked; the host's
// loop journal is its source. Returns what was done, for the line.
const char* c98_load_dead(void* actor) {
    static uint64_t fn_name = 0;
    void* const fn = fname_cached(fn_name, L"BndEvt__BP_KillableBuilding_Base_SaveComponent_K2Node_ComponentBoundEvent"
                                           L"_2_SaveFullyLoadedDelegate__DelegateSignature") != 0
                         ? g_find_function(actor, fn_name) : nullptr;
    const uint8_t* const f = prop_find_prop(read_ptr(actor, kObjectClass), "Destroyed");
    void* const save = c31_object_prop(actor, "SaveComponent");
    if (fn == nullptr || f == nullptr || !prop_bool_ok(f) || save == nullptr) {
        return "Health base 0 (no SaveFullyLoaded path on this class)";
    }
    uint8_t* const addr = static_cast<uint8_t*>(actor) + read_i32(f, kPropertyOffset);
    addr[f[kBoolByteOffset]] = static_cast<uint8_t>(addr[f[kBoolByteOffset]] | f[kBoolFieldMask]);
    alignas(16) uint8_t parms[16] = {};
    memcpy(parms, &save, sizeof(save));
    g_orig_process_event(actor, fn, parms);
    return "Health base 0, Destroyed set and SaveFullyLoaded run (no OnDeath)";
}

void c98_client_kill(void* pc, const C98World& r, const char* text) {
    void* const actor = c95_find_nearest(g_actor_get_world(pc), r.name, r.at);
    void* const health = actor != nullptr && g_c130_health_class != nullptr
                             ? find_component(actor, g_c130_health_class) : nullptr;
    if (health == nullptr || g_c131_broadcast_damage == nullptr || g_c130_is_dead == nullptr) {
        ++g_c98w.missed;
        c132_ledger_result(C132Result::Skipped, "no placed copy matches (see the WARN line)");
        log::line("WARN card098: client t=%llu world %s: no placed copy with a health component (%s), skipped",
                  static_cast<unsigned long long>(GetTickCount64()), text,
                  actor != nullptr ? class_name(actor).text : "no actor");
        return;
    }
    const bool was_dead = g_c130_is_dead(health);
    const uint32_t muted = c98_fx_muted();
    const char* done = was_dead ? "already dead, skipped" : "Health base 0 and OnDeath broadcast";
    if (!was_dead && g_c98_replaying) {
        c98_fx_quiet(true); // nothing in the load path spawns a one-shot; the mute count proves it (0 expected)
        c43_set_component_health(health, 0.0f, actor);
        done = c98_load_dead(actor);
        c98_fx_quiet(false);
        ++g_c98w.applied;
        c132_ledger_result(C132Result::Applied, nullptr);
    } else if (!was_dead) {
        c98_fx_quiet(g_c98_replaying); // hb55: a late joiner's barrel dies without the explosion the host made earlier
        c43_set_component_health(health, 0.0f, actor);
        alignas(16) uint8_t event[kDamageEventSize] = {};
        memcpy(event + kDamageEventReceiver, &actor, sizeof(actor));
        memcpy(event + kDamageEventReceiverHealth, &health, sizeof(health));
        g_c131_broadcast_damage(static_cast<uint8_t*>(health) + kHealthComponentOnDeath, event);
        c98_fx_quiet(false);
        ++g_c98w.applied;
        c132_ledger_result(C132Result::Applied, nullptr);
    }
    const bool dead = g_c130_is_dead(health);
    log::line("%scard098: client t=%llu world %s (%s): %s, IsDead now %d; client FedScore %d%s%u", was_dead || dead ? "" : "WARN ",
              static_cast<unsigned long long>(GetTickCount64()), text, class_name(actor).text,
              done, dead ? 1 : 0, c131_score(actor),
              g_c98_replaying ? "; journal replay, effects muted " : "; live, effects muted ", c98_fx_muted() - muted);
    c299_nest_row(actor, "client");
}

// hb46: client. The host's bHidden on our same-named placed copy. The original is called directly (the detour relays
// only on the host).
void c98_client_hide(void* pc, const C98World& r, const char* text) {
    void* const actor = c95_find_nearest(g_actor_get_world(pc), r.name, r.at);
    if (actor == nullptr) {
        ++g_c98w.missed;
        c132_ledger_result(C132Result::Skipped, "no placed copy matches (see the WARN line)");
        log::line("WARN card098: client t=%llu world %s: no placed copy matches, skipped",
                  static_cast<unsigned long long>(GetTickCount64()), text);
        return;
    }
    const bool was = (static_cast<const uint8_t*>(actor)[kActorHiddenByte] & kActorHiddenBit) != 0;
    g_orig_c98_hidden(actor, r.value != 0);
    c137_host_apply(actor, "bHidden", was ? 1 : 0, r.value != 0 ? 1 : 0, 0); // card 149: the mod's apply
    ++g_c98w.applied;
    c132_ledger_result(C132Result::Applied, nullptr);
    log::line("card098: client t=%llu world %s (%s): hidden now %d", static_cast<unsigned long long>(GetTickCount64()),
              text, class_name(actor).text, (static_cast<const uint8_t*>(actor)[kActorHiddenByte] & kActorHiddenBit) != 0);
}

// Card 292: ASTSatchelPlacementSite. NeutralizeSite 0x05161300 and the host's own Explode 0x051567E0 (the path of a
// value 0 or 1 row) both hide the site mesh +0x468 (SetVisibility false, propagated), dim the light +0x480, clear the
// charge timer +0x500 and set the neutralized byte +0x50E; ResetSatchelSite_Implementation 0x0516C480 sets it back to 0.
constexpr size_t kC292SiteMesh = 0x468;
constexpr size_t kC292SiteNeutralized = 0x50E;

// Card 163: client, a damage kill row. BP_SatchelBuilding_Base's damage path (its HealthComponent damage event: an
// ObjectiveDmg hit over the building's Health) runs SatchelPlacementSiteInstance->NeutralizeSite, Become Destroyed(false)
// and Killed by Damage; Become Destroyed(false) is what ReceiveOnSatchelExploded(false) runs, so the caller makes that
// call, and these are the two around it. Returns what ran, for the line.
const char* c163_client_site(void* building) {
    static uint64_t neutralize = 0;
    void* const site = c31_object_prop(building, "SatchelPlacementSiteInstance");
    void* const fn = site != nullptr && fname_cached(neutralize, L"NeutralizeSite") != 0
                         ? g_find_function(site, neutralize) : nullptr;
    if (fn == nullptr) {
        return site == nullptr ? "no site" : "WARN site has no NeutralizeSite";
    }
    alignas(16) uint8_t parms[16] = {};
    g_orig_process_event(site, fn, parms);
    return "site NeutralizeSite run";
}

// Card 292, client: after an applied 'S' row (any value), the building's site state for the card292 row; WARN when
// the site is still visible or not neutralized (the interaction prompt reads the neutralized site as used up).
void c292_site_line(void* building, const char* ran) {
    void* const site = c31_object_prop(building, "SatchelPlacementSiteInstance");
    if (site == nullptr) {
        log::line("card292: client t=%llu %s site none: %s", static_cast<unsigned long long>(GetTickCount64()),
                  object_name(building).text, ran);
        return;
    }
    const auto* const s = static_cast<const uint8_t*>(site);
    const auto* const mesh = *reinterpret_cast<const uint8_t* const*>(s + kC292SiteMesh);
    const int visible = mesh != nullptr && (mesh[kSceneVisibleByte] & kSceneVisibleBit) != 0 ? 1 : 0;
    const int interaction = s[kC292SiteNeutralized] == 0 ? 1 : 0;
    log::line("%scard292: client t=%llu %s site %s: %s; visible %d, interaction %d",
              visible != 0 || interaction != 0 ? "WARN " : "", static_cast<unsigned long long>(GetTickCount64()),
              object_name(building).text, object_name(site).text, ran, visible, interaction);
}

const char* c163_client_killed(void* building) {
    static uint64_t killed = 0;
    void* const fn = fname_cached(killed, L"Killed by Damage") != 0 ? g_find_function(building, killed) : nullptr;
    if (fn == nullptr) {
        return "WARN no Killed by Damage";
    }
    alignas(16) uint8_t parms[16] = {};
    g_orig_process_event(building, fn, parms);
    return "Killed by Damage run";
}

void c98_client_satchel(void* pc, const C98World& r, const char* text) {
    void* const actor = c95_find_nearest(g_actor_get_world(pc), r.name, r.at);
    if (actor == nullptr || !text_contains(class_name(actor).text, "SatchelBuilding")) {
        ++g_c98w.missed;
        c132_ledger_result(C132Result::Skipped, "no placed copy matches (see the WARN line)");
        log::line("WARN card098: client t=%llu world %s: no placed satchel building matches, skipped",
                  static_cast<unsigned long long>(GetTickCount64()), text);
        return;
    }
    if (c133_client_applied(actor)) { // card 133: one explode per building (live row and journal replay)
        log::line("card133: client t=%llu world %s: %s already exploded from the host's row, skipped",
                  static_cast<unsigned long long>(GetTickCount64()), text, object_name(actor).text);
        c132_ledger_result(C132Result::Skipped, "already exploded (card 133)");
        return;
    }
    const uint32_t muted = c98_fx_muted();
    const bool damage = r.value == kC163DamageKill;
    c98_fx_quiet(g_c98_replaying); // hb55: a late joiner's building falls without the explosion the host made earlier
    // Card 292: every row neutralizes the site; the host's Explode (value 0, 1) does it inside the site's own code,
    // which the client does not run, and the damage kill (2) runs NeutralizeSite in the Blueprint.
    const char* const site = c163_client_site(actor);
    g_orig_c98_satchel(actor, r.value == 1);
    const char* const killed = damage ? c163_client_killed(actor) : nullptr;
    c98_fx_quiet(false);
    c292_site_line(actor, site);
    ++g_c98w.applied;
    c132_ledger_result(C132Result::Applied, nullptr);
    log::line("card098: client t=%llu world %s (%s): applied ReceiveOnSatchelExploded; client FedScore %d%s%u",
              static_cast<unsigned long long>(GetTickCount64()), text, class_name(actor).text, c131_score(actor),
              g_c98_replaying ? "; journal replay, effects muted " : "; live, effects muted ", c98_fx_muted() - muted);
    if (damage) {
        const auto* const a = static_cast<const uint8_t*>(actor);
        log::line("card163: client t=%llu %s damage kill applied: %s, Become Destroyed(false), %s; Destroyed now %d, "
                  "hidden %d", static_cast<unsigned long long>(GetTickCount64()), object_name(actor).text, site, killed,
                  c98_bp_destroyed(actor) ? 1 : 0, (a[kActorHiddenByte] & kActorHiddenBit) != 0 ? 1 : 0);
        c299_nest_row(actor, "client"); // card 299: nests killed by damage die through this path (c423b124a)
    }
}

// Card 185: client, a replayed 'L' row (between the two 'R' rows) carrying the host timeline's state. The copy is set
// to the host's position (no events, the update fired so the moved parts follow), then continues the host's way with
// the plain Play or Reverse; a stopped host timeline stops the copy first (card 335). kC185EndByOp (the host's timeline was gone):
// the end of the op, Play / PlayFromStart at the length, Reverse / ReverseFromEnd at 0, Stop where it is.
// Card 313: client. The replay's age at this row: now minus the host's sample time minus the card 132 clock offset
// (own clock minus the host's). The age covers the host's send queue and the wire. -1: no sample time, no offset
// yet, or an age outside [0, kC313MaxAgeMs] (each a WARN; the caller applies the position as sent).
// Card 315: the sample is the caller's (the replay bracket's, or a card 315 header's); `fallback` names what the caller
// does without an age, in the WARN line. Null `fallback`: no line (card 315 counts these in its map tally).
int32_t c313_age_ms(const void* pc, const char* text, const C313Replay& sample, const char* fallback) {
    int32_t offset = 0;
    const bool have_offset = c132_ledger_clock_offset(pc, &offset);
    if (!sample.set || !have_offset) {
        if (fallback != nullptr) {
            log::line("WARN card313: client t=%llu world %s: %s, %s", static_cast<unsigned long long>(GetTickCount64()),
                      text, !sample.set ? "no host sample time in the replay bracket" : "no clock offset to the host yet",
                      fallback);
        }
        return -1;
    }
    const uint32_t now = static_cast<uint32_t>(GetTickCount64()) & kChanStampMsMask;
    const uint32_t d = (now - sample.sampled_ms - static_cast<uint32_t>(offset)) & kChanStampMsMask;
    const int32_t age = d >= (1u << 29) ? static_cast<int32_t>(d) - (1 << 30) : static_cast<int32_t>(d);
    if (age < 0 || age > kC313MaxAgeMs) {
        if (fallback != nullptr) {
            log::line("WARN card313: client t=%llu world %s: age %d ms (host sampled t=%u, clock offset %d ms) outside "
                      "0 to %d, %s", static_cast<unsigned long long>(GetTickCount64()), text, age, sample.sampled_ms,
                      offset, kC313MaxAgeMs, fallback);
        }
        return -1;
    }
    return age;
}

// Card 313: the position age_ms after pos, the way the timeline itself moves: at its play rate (card 261 rows are
// applied first), forward for Play and backward for Reverse, wrapped by the length when looping, else clamped.
float c313_aged_position(const void* timeline, float pos, float len, bool reversing, int32_t age_ms) {
    const float rate = c185_fn<C185FloatFn>(offsets::UTimelineComponent_GetPlayRate)(timeline);
    const bool looping = c185_fn<C185BoolFn>(offsets::UTimelineComponent_IsLooping)(timeline);
    const double step = static_cast<double>(age_ms) / 1000.0 * static_cast<double>(rate) * (reversing ? -1.0 : 1.0);
    const double l = static_cast<double>(len);
    double p = static_cast<double>(pos) + step;
    if (l <= 0.0) {
        return pos;
    }
    if (looping) {
        p = fmod(p, l);
        return static_cast<float>(p < 0.0 ? p + l : p);
    }
    return static_cast<float>(p < 0.0 ? 0.0 : (p > l ? l : p));
}

void c185_client_state(const void* pc, void* timeline, const C98World& r, const char* text, const char* owner) {
    const uint32_t op = r.value & kC185OpMask;
    const bool by_op = (r.value & kC185EndByOp) != 0;
    const bool forward = op == static_cast<uint32_t>(C98TimelineOp::Play) ||
                         op == static_cast<uint32_t>(C98TimelineOp::PlayFromStart);
    const float len = c185_fn<C185FloatFn>(offsets::UTimelineComponent_GetTimelineLength)(timeline);
    const float sent = by_op ? (forward ? len : 0.0f)
                             : static_cast<float>(static_cast<double>(r.value >> kC185PosShift) / 1000.0);
    const bool playing = !by_op && (r.value & kC185Playing) != 0;
    const bool reversing = !by_op && (r.value & kC185Reversing) != 0;
    const int32_t age = playing ? c313_age_ms(pc, text, g_c313_replay, "position applied as sent") : -1; // card 313: a stopped timeline does not move
    const float pos = age >= 0 ? c313_aged_position(timeline, sent, len, reversing, age) : sent;
    // Card 335: a host timeline that is not playing stops the copy first. The copy may be playing on its own: a
    // replayed Activate (card 311) earlier in the same replay starts a fresh map copy's timeline from 0, and
    // SetPlaybackPosition alone leaves it playing (c310-proof run 11, TargetRangeTarger_C_3: host 3.533 of 6.00 not
    // playing, client playing for the rest of the map).
    if (!playing) {
        g_orig_c98_timeline_stop(timeline);
    }
    if (!(by_op && op == static_cast<uint32_t>(C98TimelineOp::Stop))) {
        c185_fn<C185SetPosFn>(offsets::UTimelineComponent_SetPlaybackPosition)(timeline, pos, false, true);
    }
    if (playing) {
        (reversing ? g_orig_c98_timeline_reverse : g_orig_c98_timeline_play)(timeline);
    }
    ++g_c98w.applied;
    c132_ledger_result(C132Result::Applied, nullptr);
    if (note_event_cap(g_c98w.timeline_lines, kC185Lines, "card185 timeline state")) {
        log::line("card098: client t=%llu world %s (%s): applied timeline state: position %.2f + %d ms = %.2f of %.2f, "
                  "playing %d, reversing %d%s", static_cast<unsigned long long>(GetTickCount64()), text, owner, sent,
                  age >= 0 ? age : 0, pos, len, playing ? 1 : 0, reversing ? 1 : 0,
                  by_op ? " (host timeline gone: end by op)" : "");
    }
}

// Card 130 item 2: client. The host's timeline call on our same-named placed copy's same-named timeline. The original
// is called directly (the detour relays only on the host).
void c98_client_timeline(void* pc, C98World& r, const char* text) {
    static const C98SelfFn* const kCall[] = {&g_orig_c98_timeline_play, &g_orig_c98_timeline_from_start,
                                             &g_orig_c98_timeline_reverse, &g_orig_c98_timeline_reverse_from_end,
                                             &g_orig_c98_timeline_stop};
    static_assert(sizeof(kCall) / sizeof(kCall[0]) == static_cast<size_t>(C98TimelineOp::Count), "one call per op");
    char* const bar = strchr(r.name, '|');
    const uint32_t op = r.value & kC185OpMask; // card 185: the state bits above the op
    const C98SelfFn call = op < static_cast<uint32_t>(C98TimelineOp::Count) ? *kCall[op] : nullptr;
    if (bar != nullptr) {
        *bar = '\0';
    }
    void* const actor = bar != nullptr ? c95_find_nearest(g_actor_get_world(pc), r.name, r.at) : nullptr;
    const uint8_t* prop = nullptr;
    void* const timeline = actor != nullptr ? const_cast<void*>(prop_object_field(actor, bar + 1, &prop)) : nullptr;
    if (call == nullptr || timeline == nullptr || strcmp(class_name(timeline).text, "TimelineComponent") != 0) {
        ++g_c98w.missed;
        c132_ledger_result(C132Result::Skipped, "no placed copy matches (see the WARN line)");
        log::line("WARN card098: client t=%llu world %s: no placed copy with that timeline (%s, %s), skipped",
                  static_cast<unsigned long long>(GetTickCount64()), text,
                  actor != nullptr ? class_name(actor).text : "no actor",
                  timeline != nullptr ? class_name(timeline).text : "no timeline");
        return;
    }
    if (g_c98_replaying && (r.value & (kC185State | kC185EndByOp)) != 0) {
        // card 185: the host's state, not the op again
        c185_client_state(pc, timeline, r, text, class_name(actor).text);
        return;
    }
    call(timeline);
    ++g_c98w.applied;
    c132_ledger_result(C132Result::Applied, nullptr);
    log::line("card098: client t=%llu world %s (%s): applied timeline call %u",
              static_cast<unsigned long long>(GetTickCount64()), text, class_name(actor).text, op);
}

// Card 261: client. The host's play rate on our same-named placed copy's same-named timeline, found as
// c98_client_timeline finds it; the original SetPlayRate is called directly (the detour relays only on the host).
void c261_client_rate(void* pc, C98World& r, const char* text) {
    char* const bar = strchr(r.name, '|');
    if (bar != nullptr) {
        *bar = '\0';
    }
    void* const actor = bar != nullptr ? c95_find_nearest(g_actor_get_world(pc), r.name, r.at) : nullptr;
    const uint8_t* prop = nullptr;
    void* const timeline = actor != nullptr ? const_cast<void*>(prop_object_field(actor, bar + 1, &prop)) : nullptr;
    if (timeline == nullptr || strcmp(class_name(timeline).text, "TimelineComponent") != 0 ||
        g_orig_c261_set_play_rate == nullptr) {
        ++g_c98w.missed;
        c132_ledger_result(C132Result::Skipped, "no placed copy matches (see the WARN line)");
        log::line("WARN card261: client t=%llu world %s: no placed copy with that timeline (%s, %s), skipped",
                  static_cast<unsigned long long>(GetTickCount64()), text,
                  actor != nullptr ? class_name(actor).text : "no actor",
                  timeline != nullptr ? class_name(timeline).text : "no timeline");
        return;
    }
    float rate = 0.0f;
    memcpy(&rate, &r.value, sizeof(rate));
    g_orig_c261_set_play_rate(timeline, rate);
    ++g_c98w.applied;
    c132_ledger_result(C132Result::Applied, nullptr);
    if (note_event_cap(g_c98w.rate_lines, kC261Lines, "card261 play rate")) {
        log::line("card261: client world %c %s|%s: play rate %.4f applied", kC98KindRate, r.name, bar + 1,
                  static_cast<double>(rate));
    }
}

// Card 145: client. An 'E' row (our nearest same-named placed pickup) or an 'N' row (our copy of host object index
// `value`) sets bEnabled through the game's own SetPickupEnabled, whose OnUpdatePickupStatus sets the mesh and the
// collision as the host's did. A pickup already in that state counts as applied.
void c145_client_enable(void* pc, const C98World& r, const char* text) {
    const bool copy = r.kind == kC98KindCopyEnable;
    const uint32_t want = copy ? (r.at.x >= 0.5 ? 1u : 0u) : (r.value != 0 ? 1u : 0u);
    void* pickup = nullptr;
    if (copy) {
        void* const world = g_actor_get_world(pc);
        for (int32_t i = 0; i < g_c104_copies.n && pickup == nullptr; ++i) {
            void* const a = g_c104_copies.row[i].actor;
            pickup = g_c104_copies.row[i].host_id == r.value && c104_in_world(world, a) ? a : nullptr;
        }
    } else {
        pickup = c95_find_nearest(g_actor_get_world(pc), r.name, r.at);
    }
    const unsigned long long now = GetTickCount64();
    if (pickup == nullptr || !c145_is_pickup(pickup) || g_c145_set_enabled == nullptr) {
        ++g_c98w.missed;
        c132_ledger_result(C132Result::Skipped, "no pickup matches (see the WARN line)");
        log::line("WARN card145: client t=%llu world %s: %s", now, text,
                  pickup == nullptr ? (copy ? "no copy of that host object" : "no placed actor of that name")
                                    : (g_c145_set_enabled == nullptr ? "SetPickupEnabled not bound" : "not a pickup"));
        return;
    }
    c221_off_note(pickup, want); // card 221: the already case too
    const bool already = c145_enabled(pickup) == want;
    if (!already) {
        g_c221_host_enable = true; // card 221: this enable is the host's; detour_c145_update_status keeps it
        g_c145_set_enabled(pickup, want != 0);
        g_c221_host_enable = false;
    }
    ++g_c98w.applied;
    c132_ledger_result(C132Result::Applied, already ? "already in the host's state" : nullptr);
    if (c98_world_line()) {
        log::line("card145: client t=%llu world %s (%s at %.0f, %.0f, %.0f): %s", now, text, object_name(pickup).text,
                  actor_location(pickup).x, actor_location(pickup).y, actor_location(pickup).z,
                  already ? "already" : (c145_enabled(pickup) == want ? "applied" : "WARN applied, byte unchanged"));
    }
}

// Card 235: a WorldOp text "kind name x y z value" whose name may hold spaces (MissleSilo_C_1|Fire Bomb): the kind
// is the first char, the last four fields come off the end, and the name is everything between. False when a part
// is missing, the name is empty or too long, or text follows the value.
bool c98_world_parse(const char* text, C98World& r, double& x, double& y, double& z, unsigned& value) {
    const size_t len = strnlen(text, kChanTextMax + 1);
    size_t cut = 0;
    int32_t spaces = 0;
    for (size_t i = len; i-- > 2 && spaces < 4;) {
        if (text[i] == ' ' && ++spaces == 4) {
            cut = i;
        }
    }
    if (len < 3 || text[1] != ' ' || cut <= 2 || cut - 2 >= sizeof(r.name)) {
        return false;
    }
    r.kind = text[0];
    memcpy(r.name, text + 2, cut - 2);
    r.name[cut - 2] = '\0';
    int used = 0;
    return sscanf_s(text + cut, " %lf %lf %lf %u%n", &x, &y, &z, &value, &used) == 4 &&
           static_cast<size_t>(used) == len - cut;
}

// Card 311: the card 065 trigger component of an 'C' row ("<placed owner>|<component object name>") in `world`: the
// nearest same-named placed owner, then its component by name (as c65_receive_activate finds it). Null when absent;
// *owner_out is the owner found (null for no such actor).
void* c311_find(const void* world, const C98World& r, void** owner_out) {
    char owner_name[kAssetPathChars];
    snprintf(owner_name, sizeof(owner_name), "%s", r.name);
    char* const bar = strchr(owner_name, '|');
    *owner_out = nullptr;
    if (bar == nullptr || world == nullptr || g_c65_trigger_class == nullptr || g_static_find_object == nullptr) {
        return nullptr;
    }
    *bar = '\0';
    void* const owner = c95_find_nearest(world, owner_name, r.at);
    *owner_out = owner;
    wchar_t w[kAssetPathChars];
    size_t n = 0;
    for (const char* c = bar + 1; *c != '\0' && n + 1 < kAssetPathChars; ++c) {
        w[n++] = static_cast<wchar_t>(*c);
    }
    w[n] = L'\0';
    return owner != nullptr ? g_static_find_object(g_c65_trigger_class(), owner, w, false) : nullptr;
}

// Card 311: client. The host's journalled range-target Activate, replayed on our copy of that component. One-shot
// effects of the activation are muted inside a replay (c98_fx_quiet), as for a replayed kill.
void c311_client_activate(void* pc, const C98World& r, const char* text) {
    void* owner = nullptr;
    void* const component = c311_find(g_actor_get_world(pc), r, &owner);
    const char* const bar = strchr(r.name, '|');
    const int owner_len = bar != nullptr ? static_cast<int>(bar - r.name) : static_cast<int>(strlen(r.name));
    log::line("%scard311: client t=%llu replayed Activate(%u) %.*s.%s: %s", component != nullptr ? "" : "WARN ",
              static_cast<unsigned long long>(GetTickCount64()), r.value, owner_len, r.name,
              bar != nullptr ? bar + 1 : "-",
              component != nullptr ? "activated" : (owner != nullptr ? "no such component" : "no such actor"));
    if (component == nullptr || g_orig_c65_activate == nullptr) {
        ++g_c98w.missed;
        c132_ledger_result(C132Result::Skipped, "no placed copy matches (see the WARN line)");
        return;
    }
    c98_fx_quiet(g_c98_replaying);
    g_orig_c65_activate(component, r.value != 0);
    c98_fx_quiet(false);
    ++g_c98w.applied;
    c132_ledger_result(C132Result::Applied, nullptr);
    (void)text;
}

} // namespace

// Card 408 F5: a playing timeline moved on by age_ms the way it moves itself (c313_aged_position), with the update
// fired and no events, so the parts it drives follow. Returns the new position; -1 when it is not playing.
float c313_advance_timeline(void* timeline, int32_t age_ms) {
    if (timeline == nullptr || strcmp(class_name(timeline).text, "TimelineComponent") != 0 ||
        !c185_fn<C185BoolFn>(offsets::UTimelineComponent_IsPlaying)(timeline)) {
        return -1.0f;
    }
    const float pos = c185_fn<C185FloatFn>(offsets::UTimelineComponent_GetPlaybackPosition)(timeline);
    const float len = c185_fn<C185FloatFn>(offsets::UTimelineComponent_GetTimelineLength)(timeline);
    const bool reversing = c185_fn<C185BoolFn>(offsets::UTimelineComponent_IsReversing)(timeline);
    const float aged = c313_aged_position(timeline, pos, len, reversing, age_ms);
    c185_fn<C185SetPosFn>(offsets::UTimelineComponent_SetPlaybackPosition)(timeline, aged, false, true);
    return aged;
}

// Card 315 step 1: the actor's first timeline as " timeline <name> position <p> of <len>, playing <n>", or "" when
// it has none, for the card 237 mismatch rows of both sides.
C315TimelineText c315_timeline_text(const void* actor) {
    C315TimelineText out{};
    const void* const timeline =
        g_c132_timeline_class != nullptr && actor != nullptr ? find_component(actor, g_c132_timeline_class) : nullptr;
    if (timeline == nullptr) {
        return out;
    }
    snprintf(out.text, sizeof(out.text), " timeline %s position %.3f of %.2f, playing %d", object_name(timeline).text,
             static_cast<double>(c185_fn<C185FloatFn>(offsets::UTimelineComponent_GetPlaybackPosition)(timeline)),
             static_cast<double>(c185_fn<C185FloatFn>(offsets::UTimelineComponent_GetTimelineLength)(timeline)),
             c185_fn<C185BoolFn>(offsets::UTimelineComponent_IsPlaying)(timeline) ? 1 : 0);
    return out;
}

// Card 315: client, one counted distinct timeline per map (kC315Timelines at most). True when this is the timeline's
// first row this map: g_c315 resets at map end, and the client's join replay (the first rows of each timeline) comes
// after the map load, so a new timeline here is its first sample since the join replay. A full table returns false.
static bool c315_seen(const void* timeline) {
    for (int32_t i = 0; i < g_c315.nseen; ++i) {
        if (g_c315.seen[i] == timeline) {
            return false;
        }
    }
    if (g_c315.nseen < kC315Timelines) {
        g_c315.seen[g_c315.nseen++] = timeline;
        return true;
    }
    return false;
}

// Card 315: client. A wrap-aware difference a - b in ms on a loop of len seconds, in (-len/2, len/2].
static double c315_loop_off_ms(float a, float b, float len) {
    const double l = static_cast<double>(len);
    double d = static_cast<double>(a) - static_cast<double>(b);
    if (l > 0.0) {
        d = fmod(d, l);
        d = d > l / 2.0 ? d - l : (d <= -l / 2.0 ? d + l : d);
    }
    return d * 1000.0;
}

// Card 315: client. An 'O' row (after its header): the host's playing looping timeline at the header's sample time.
// Our copy is found as c98_client_timeline finds it; its position is set to the aged host position only when it is
// more than kC315OffMs off. A copy that is not playing is counted and left as it is (no Stop, no Play here).
static void c315_client_row(void* pc, C98World& r, const char* text) {
    ++g_c315.received;
    char* const bar = strchr(r.name, '|');
    if (bar != nullptr) {
        *bar = '\0';
    }
    void* const actor = bar != nullptr ? c95_find_nearest(g_actor_get_world(pc), r.name, r.at) : nullptr;
    const uint8_t* prop = nullptr;
    void* const timeline = actor != nullptr ? const_cast<void*>(prop_object_field(actor, bar + 1, &prop)) : nullptr;
    const bool line = g_c315.lines < kC315Lines;
    if (timeline == nullptr || strcmp(class_name(timeline).text, "TimelineComponent") != 0) {
        ++g_c315.missed;
        c132_ledger_result(C132Result::Skipped, "no placed copy with that timeline");
        g_c315.lines += line ? 1u : 0u;
        if (line) {
            log::line("WARN card315: client t=%llu world %s: no placed copy with that timeline (%s), skipped",
                      static_cast<unsigned long long>(GetTickCount64()), text,
                      actor != nullptr ? class_name(actor).text : "no actor");
        }
        return;
    }
    const bool first = c315_seen(timeline);
    const bool playing = c185_fn<C185BoolFn>(offsets::UTimelineComponent_IsPlaying)(timeline);
    const int32_t age = c313_age_ms(pc, text, g_c315.sample, nullptr);
    if (!playing || age < 0) {
        g_c315.not_playing += playing ? 0u : 1u;
        g_c315.unaged += playing ? 1u : 0u;
        c132_ledger_result(C132Result::Skipped, playing ? "no age for the host sample" : "own copy not playing");
        g_c315.lines += line ? 1u : 0u;
        // Card 315 (b169): the join replay's sample of a timeline can come with no age (no clock offset yet, or a
        // header older than 2 s), which is expected, so the first unaged sample of each timeline is a plain row;
        // later ones stay WARN.
        const bool expected = playing && first;
        if (line) {
            log::line("%scard315: client t=%llu world %s (%s): %s, not corrected", expected ? "" : "WARN ",
                      static_cast<unsigned long long>(GetTickCount64()), text, class_name(actor).text,
                      !playing   ? "own copy not playing"
                      : expected ? "no age for the host sample (first sample since the join replay)"
                                 : "no age for the host sample (no header time, no clock offset, or older than 2 s)");
        }
        return;
    }
    const float len = c185_fn<C185FloatFn>(offsets::UTimelineComponent_GetTimelineLength)(timeline);
    const float own = c185_fn<C185FloatFn>(offsets::UTimelineComponent_GetPlaybackPosition)(timeline);
    const float sent = static_cast<float>(static_cast<double>(r.value >> kC185PosShift) / 1000.0);
    const float host = c313_aged_position(timeline, sent, len, (r.value & kC185Reversing) != 0, age);
    const double off = c315_loop_off_ms(host, own, len);
    const double mag = off < 0.0 ? -off : off;
    c132_ledger_result(C132Result::Applied, mag > kC315OffMs ? nullptr : "already in phase");
    if (mag <= kC315OffMs) {
        ++g_c315.in_sync;
        return;
    }
    c185_fn<C185SetPosFn>(offsets::UTimelineComponent_SetPlaybackPosition)(timeline, host, false, true);
    ++g_c315.corrections;
    g_c315.largest_ms = mag > g_c315.largest_ms ? mag : g_c315.largest_ms;
    g_c315.lines += line ? 1u : 0u;
    if (line) {
        log::line("card315: client t=%llu corrected %s|%s (%s): own %.3f host %.3f (aged %d ms from %.3f) of "
                  "%.2f, off %.0f ms", static_cast<unsigned long long>(GetTickCount64()), r.name, bar + 1,
                  class_name(actor).text, static_cast<double>(own), static_cast<double>(host), age,
                  static_cast<double>(sent), static_cast<double>(len), off);
    }
}

// Client: the host's WorldOp frame "kind name x y z value": a satchel event on our nearest same-named placed copy, or
// (card 104) a relayed actor's copy spawned or destroyed.
void c98_receive_world(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    C98World r{};
    double x = 0.0, y = 0.0, z = 0.0;
    unsigned value = 0;
    const bool got = chan_text(s, text, sizeof(text)) > 0;
    if (got && text[0] == kC98KindExtPack && text[1] == ' ') {
        c420_client_ext_pack(pc, text + 2); // card 420: pairs, not a row; reports its own ledger result
        return;
    }
    const bool ok = got && c98_world_parse(text, r, x, y, z, value); // card 235
    r.at = Vec3{x, y, z};
    r.value = value;
    if (ok && r.kind == kC98KindSpawn) {
        c104_client_spawn(pc, r);
    } else if (ok && r.kind == kC98KindDestroy) {
        c104_client_destroy(pc, r.value, r.at);
    } else if (ok && r.kind == kC98KindSatchel && value <= kC163DamageKill) {
        c98_client_satchel(pc, r, text);
    } else if (ok && r.kind == kC98KindKill) {
        c98_client_kill(pc, r, text); // card 130 item 12
    } else if (ok && r.kind == kC98KindHide && value <= 1) {
        c98_client_hide(pc, r, text); // hb46
    } else if (ok && r.kind == kC98KindTurret) {
        c125_client_turret(pc, r.name, r.at, r.value); // card 125
    } else if (ok && r.kind == kC98KindTimeline) {
        c98_client_timeline(pc, r, text); // card 130 item 2
    } else if (ok && r.kind == kC98KindRate) {
        c261_client_rate(pc, r, text); // card 261
    } else if (ok && r.kind == kC98KindExt) {
        c132_client_ext(pc, r.name, r.at, r.value); // card 132 step 2d
    } else if (ok && r.kind == kC98KindRest) {
        c104_client_rest(pc, r); // card 132 note 2
    } else if (ok && (r.kind == kC98KindEnable || r.kind == kC98KindCopyEnable)) {
        c145_client_enable(pc, r, text); // card 145
    } else if (ok && r.kind == kC98KindTravel) {
        g_c290_travel_at = GetTickCount64(); // card 290
        c132_ledger_result(C132Result::Applied, nullptr);
        log::line("card290: client t=%llu host travel row (%s): failures 3 and 5 within %llu ms are the travel",
                  static_cast<unsigned long long>(g_c290_travel_at), r.name,
                  static_cast<unsigned long long>(kC290TravelMs));
        c294_client_travel(r.name, g_c290_travel_at); // card 294
    } else if (ok && r.kind == kC98KindSecret) {
        c289_client_secret(pc, r, text); // card 289
    } else if (ok && r.kind == kC98KindClosure) {
        c300_client_row(pc, r, text); // card 300
    } else if (ok && r.kind == kC98KindActivate) {
        c311_client_activate(pc, r, text); // card 311
    } else if (ok && r.kind == kC98KindC408) {
        const char* const why = c408_client_row(pc, r.name, r.at, r.value); // card 408
        c132_ledger_result(why == nullptr ? C132Result::Applied : C132Result::Skipped, why);
    } else if (ok && r.kind == kC98KindPlaced) {
        c140_client_placed(pc, r); // card 140
    } else if (ok && r.kind == kC98KindPhase && strcmp(r.name, "-") == 0) {
        // card 315: the header; x carries the host's sample time of the rows after it
        const bool sampled = x > 0.0 && x <= static_cast<double>(kChanStampMsMask);
        g_c315.sample = C313Replay{sampled ? static_cast<uint32_t>(x) : 0u, sampled};
        c132_ledger_result(C132Result::Applied, nullptr);
    } else if (ok && r.kind == kC98KindPhase) {
        c315_client_row(pc, r, text); // card 315
    } else if (ok && r.kind == kC98KindReplay && value <= 1) {
        g_c98_replaying = value != 0; // hb55
        const bool sampled = g_c98_replaying && x > 0.0 && x <= static_cast<double>(kChanStampMsMask); // card 313
        g_c313_replay = C313Replay{sampled ? static_cast<uint32_t>(x) : 0u, sampled};
        log::line("card098: client t=%llu world journal replay %s (host sampled t=%u)",
                  static_cast<unsigned long long>(GetTickCount64()), g_c98_replaying ? "begins" : "ends",
                  g_c313_replay.sampled_ms);
    } else {
        log::line("WARN card098: client: bad WorldOp frame \"%.160s\"", text); // card 235: the refused text
        c132_ledger_result(C132Result::Refused, "bad frame");
    }
}

// Card 185: host, at the replay. The journal row's timeline, found again the way the client finds its copy:
// c95_find_nearest over the world's live level actor lists by placed name and location, the named property, and the
// TimelineComponent class check. The journal's pointer is only compared with the one found, never read. Null: gone.
void* c185_host_find(const void* world, const C98World& r) {
    char name[kAssetPathChars];
    snprintf(name, sizeof(name), "%s", r.name);
    char* const bar = strchr(name, '|');
    if (bar == nullptr) {
        return nullptr;
    }
    *bar = '\0';
    void* const actor = c95_find_nearest(world, name, r.at);
    const uint8_t* prop = nullptr;
    const void* const timeline = actor != nullptr ? prop_object_field(actor, bar + 1, &prop) : nullptr;
    const bool found = timeline != nullptr && timeline == r.actor &&
                       strcmp(class_name(timeline).text, "TimelineComponent") == 0;
    return found ? const_cast<void*>(timeline) : nullptr;
}

// Card 185: host. The value a replayed 'L' row carries: the op and the host timeline's state now (kC185State), or
// the op with kC185EndByOp when the timeline is gone.
uint32_t c185_replay_value(const void* world, const C98World& r) {
    void* const timeline = c185_host_find(world, r);
    const bool line = note_event_cap(g_c98w.timeline_lines, kC185Lines, "card185 timeline state");
    if (timeline == nullptr) {
        ++g_c98w.timeline_gone;
        if (line) {
            log::line("card098: host t=%llu replay timeline %s: not found, sent as the end of op %u (%u gone)",
                      static_cast<unsigned long long>(GetTickCount64()), r.name, r.value, g_c98w.timeline_gone);
        }
        return (r.value & kC185OpMask) | kC185EndByOp;
    }
    const float pos = c185_fn<C185FloatFn>(offsets::UTimelineComponent_GetPlaybackPosition)(timeline);
    const float len = c185_fn<C185FloatFn>(offsets::UTimelineComponent_GetTimelineLength)(timeline);
    const bool playing = c185_fn<C185BoolFn>(offsets::UTimelineComponent_IsPlaying)(timeline);
    const bool reversing = c185_fn<C185BoolFn>(offsets::UTimelineComponent_IsReversing)(timeline);
    const double ms = pos > 0.0f ? static_cast<double>(pos) * 1000.0 + 0.5 : 0.0;
    const uint32_t pos_ms = ms < static_cast<double>(kC185PosMaxMs) ? static_cast<uint32_t>(ms) : kC185PosMaxMs;
    ++g_c98w.timeline_state;
    if (line) {
        log::line("card098: host t=%llu replay timeline %s: position %.2f of %.2f, playing %d, reversing %d (op %u)",
                  static_cast<unsigned long long>(GetTickCount64()), r.name, pos, len, playing ? 1 : 0,
                  reversing ? 1 : 0, r.value);
    }
    return (r.value & kC185OpMask) | kC185State | (playing ? kC185Playing : 0u) | (reversing ? kC185Reversing : 0u) |
           (pos_ms << kC185PosShift);
}

// Card 315: host. One 'O' row to every ponged client; the header goes first, once per scan, before the first row.
static void c315_host_send(const void* world, const C98World& row, bool& header, ULONGLONG now) {
    char text[kAssetPathChars + 64];
    if (!header) {
        header = true;
        snprintf(text, sizeof(text), "%c - %u 0 0 1", kC98KindPhase, static_cast<uint32_t>(now) & kChanStampMsMask);
        c95_send_all(world, ChanOp::WorldOp, text);
    }
    const int w = c98_world_text(row, text, sizeof(text));
    const C95Fanout f = c95_send_all(world, ChanOp::WorldOp, text);
    g_c315.rows += f.sent > 0 ? 1u : 0u;
    g_c315.chars += f.sent > 0 && w > 0 ? static_cast<uint32_t>(w) : 0u;
    if (f.sent != f.to && g_c315.host_lines < kC315Lines) {
        ++g_c315.host_lines;
        log::line("WARN card315: host t=%llu world %s: sent to %d of %d clients", static_cast<unsigned long long>(now),
                  text, f.sent, f.to);
    }
}

// Card 487: host, every tick. Each hide check that is due: the actor (still listed, same FName) relays its 'K' row when
// it is dead now; the row goes either way.
void c487_host_step(void* engine) {
    if (g_client_session || engine == nullptr || g_c487.n == 0) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    int32_t keep = 0;
    for (int32_t i = 0; i < g_c487.n && i < kC487Pending; ++i) {
        const C487Row r = g_c487.row[i];
        if (now < r.due) {
            g_c487.row[keep++] = r;
            continue;
        }
        if (!object_listed(r.actor) || read_u64(r.actor, kObjectName) != r.fname) {
            ++g_c487.gone;
            continue;
        }
        bool killable = false;
        if (c98_dead_now(r.actor, killable)) {
            c487_kill(r.actor, "checked after its hide");
        } else {
            ++g_c487.alive;
        }
    }
    g_c487.n = keep;
}

// Card 315: host, every kC315ScanMs with a ponged client. Each journalled timeline ('L', one row per timeline) that
// is playing and looping now goes out as an 'O' row with its position (c315_host_send). Logged per scan up to
// kC315Lines, tallied at map end.
void c315_host_step(void* engine) {
    void* const world = engine != nullptr && !g_client_session ? g_get_game_world(engine) : nullptr;
    const ULONGLONG now = GetTickCount64();
    if (world == nullptr || now < g_c315.due || c95_ponged_count(world) <= 0) {
        return;
    }
    g_c315.due = now + kC315ScanMs;
    ++g_c315.scans;
    bool header = false;
    int32_t sent = 0;
    for (int32_t i = 0; i < g_c98w.n; ++i) {
        if (g_c98w.row[i].kind != kC98KindTimeline) {
            continue;
        }
        const void* const timeline = c185_host_find(world, g_c98w.row[i]);
        if (timeline == nullptr || !c185_fn<C185BoolFn>(offsets::UTimelineComponent_IsPlaying)(timeline) ||
            !c185_fn<C185BoolFn>(offsets::UTimelineComponent_IsLooping)(timeline)) {
            continue;
        }
        const float pos = c185_fn<C185FloatFn>(offsets::UTimelineComponent_GetPlaybackPosition)(timeline);
        const bool reversing = c185_fn<C185BoolFn>(offsets::UTimelineComponent_IsReversing)(timeline);
        const double ms = pos > 0.0f ? static_cast<double>(pos) * 1000.0 + 0.5 : 0.0;
        const uint32_t pos_ms = ms < static_cast<double>(kC185PosMaxMs) ? static_cast<uint32_t>(ms) : kC185PosMaxMs;
        C98World row = g_c98w.row[i];
        row.kind = kC98KindPhase;
        row.value = kC185Playing | (reversing ? kC185Reversing : 0u) | (pos_ms << kC185PosShift);
        c315_host_send(world, row, header, now);
        ++sent;
    }
    if (sent != 0 && g_c315.host_lines < kC315Lines) {
        ++g_c315.host_lines;
        log::line("card315: host t=%llu scan %u: %d looping timelines sent (%u rows, %u chars this map)",
                  static_cast<unsigned long long>(now), g_c315.scans, sent, g_c315.rows, g_c315.chars);
    }
}

// Card 311: host. True when an 'C' row goes in this replay: the host's own component, found again by name (an in-place
// load may have replaced the owner), is active now. bIsActive unreadable (-1): sent. One line per row.
static bool c311_replay_gate(void* pc, const C98World& r) {
    void* owner = nullptr;
    void* const component = c311_find(g_actor_get_world(pc), r, &owner);
    const int active = component != nullptr ? c311_active(component) : 0;
    const bool send = active != 0;
    log::line("%scard311: host replay Activate(%u) %s to %s: bIsActive %d%s", component != nullptr ? "" : "WARN ",
              r.value, r.name, object_name(pc).text, active,
              send ? "" : (component != nullptr ? ", skipped (inactive on host)" : ", skipped (no host component)"));
    return send;
}

// Host: a remote PC's first Pong of this map gets every journalled world change.
// Card 323 (b257, c245b256a): a journalled event row whose Blueprint has side effects. The registry's "world events"
// join replay sends these before every state journal, so the state rows (collision, hidden, door, timeline, ext) land
// last. The c245b256a rejoin replayed CollisionOp "BlockingVolume_14..17 - 0" first, then the 'F' closure of
// BP_PlayerPresence_C_73, whose script set the four volumes back to 1 on the client (host 0): 8 digest rounds of
// mismatches 4. None of these needs a state row first: S, K and C act on a placed actor the client loaded; F closes a
// placed presence; G credits a count. 'C' keeps card 311's order (before the 'L' rows, which are state). The 'B' play
// rate rows (state, no side effects) go with the events: card 261 replays them before every other row, so a timeline
// an event plays runs at the host's rate.
namespace {
bool c98_event_kind(char kind) {
    return kind == kC98KindSatchel || kind == kC98KindKill || kind == kC98KindClosure ||
           kind == kC98KindSecret || kind == kC98KindActivate || kind == kC98KindRate;
}

// One bracketed pass set over the journal: events (true) or state rows (false).
void c98_replay_part(void* pc, bool events) {
    // Card 132 note 2: two passes, rest rows last. c98_unjournal moves the last row into a hole, so a copy's 'M' row can
    // sit before its 'P' row, and an 'M' that arrives first finds no copy.
    // hb55: the rows go between two 'R' rows, so the client applies a one-shot row (a kill, an explode) as the end
    // state, without the effects the host's live event made. One channel, so the order holds.
    int32_t sent = 0;
    int32_t gated = 0; // card 311: Activate rows held back (the host's component is not active)
    int32_t part = 0;  // the journal rows of this part
    for (int32_t i = 0; i < g_c98w.n; ++i) {
        part += c98_event_kind(g_c98w.row[i].kind) == events ? 1 : 0;
    }
    if (part != 0) {
        char begin[48]; // card 313: x carries the host's clock now, the sample time of every row below
        snprintf(begin, sizeof(begin), "R - %u 0 0 1", static_cast<uint32_t>(GetTickCount64()) & kChanStampMsMask);
        c98_send_one(pc, begin);
    }
    // Card 261: three passes, play rate rows first, so a timeline's 'L' row (and its card 185 state) plays at the rate.
    for (int32_t pass = 0; pass < 3; ++pass) {
        for (int32_t i = 0; i < g_c98w.n; ++i) {
            const char kind = g_c98w.row[i].kind;
            const bool late = kind == kC98KindRest || kind == kC98KindCopyEnable; // card 145
            const int32_t row_pass = kind == kC98KindRate || kind == kC98KindActivate ? 0 : (late ? 2 : 1); // card 311
            if (row_pass != pass || c98_event_kind(kind) != events) {
                continue;
            }
            C98World row = g_c98w.row[i];
            if (row.kind == kC98KindActivate && !c311_replay_gate(pc, row)) {
                ++gated;
                continue; // card 311: the host's component is not active now (a save from before the Activate)
            }
            if (row.kind == kC98KindTimeline) {
                row.value = c185_replay_value(g_actor_get_world(pc), row); // card 185: the host's state, not the op
            }
            char text[kAssetPathChars + 64];
            c98_world_text(row, text, sizeof(text));
            sent += c98_send_one(pc, text) ? 1 : 0;
        }
    }
    if (part != 0) {
        c98_send_one(pc, "R - 0 0 0 0");
        log::line("%scard098: host t=%llu world journal replay to %s (first Pong of the map): %d of %d %s rows; "
                  "timelines with state %u, gone %u (map totals); Activate rows held %d",
                  sent + gated == part ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()),
                  object_name(pc).text, sent, part, events ? "event" : "state", g_c98w.timeline_state,
                  g_c98w.timeline_gone, gated);
    }
}
} // namespace

// Card 323 (b257): the registry's "world events" join replay, before every state journal.
void c98_replay_events(void* pc) {
    c98_replay_part(pc, true);
}

// The registry's "world" join replay: the journal's state rows.
void c98_replay(void* pc) {
    c98_replay_part(pc, false);
}

// Card 184: both sides, at LoadMap enter. The world journal and the card 104 relay tables start over here, not after
// LoadMap returned: the new world's BeginPlay records rows inside LoadMap (a BP_TacSupp_Pod_MechDrop_C spawn, placed
// building kills and enables), and a reset after the return wiped them, so the destroy match and the join replay
// missed them. The old world's teardown records no world row (sweep in the card).
// Card 308: the world relay totals row (when: "map totals" at LoadMap enter, "quit totals" at the test quit); no row
// with nothing held and nothing missed.
void c98_world_totals(const char* when) {
    if (g_c98w.n != 0 || g_c98w.missed != 0) {
        char kinds[256]; // card 308: peak and the rows held now by kind
        c98_kind_counts(kinds, sizeof(kinds));
        log::line("card098: %s world relay %s: %d rows (peak %d of %d; kinds %s), %u dropped, %u sent, %u "
                  "applied, %u missed", side(), when, g_c98w.n, g_c98w.peak, kC98WorldRows, kinds, g_c98w.dropped,
                  g_c98w.sent, g_c98w.applied, g_c98w.missed);
    }
}

void c98_world_map_end() {
    g_c434_lines[0] = 0; // card 434: the owner row caps are per map
    g_c434_lines[1] = 0;
    c98_world_totals("map totals");
    if (g_c104_relayed.peak != 0) {
        log::line("card104: %s map: relayed actors peak %d of %d alive", side(), g_c104_relayed.peak, kC104Copies);
    }
    if (g_c104_pending.sent != 0 || g_c104_pending.ended != 0 || g_c104_pending.n != 0 ||
        g_c104_pending.samples != 0) {
        log::line("card140: %s map: %u deferred spawns relayed at their finished place, %u ended first, %d unsent, "
                  "%u moving rows", side(), g_c104_pending.sent, g_c104_pending.ended, g_c104_pending.n,
                  g_c104_pending.samples);
    }
    if (g_c287_sent.sent != 0) {
        log::line("card287: %s map: %u moving rows sent, peak %u in one second", side(), g_c287_sent.sent,
                  g_c287_sent.peak);
    }
    g_c287_sent = C287Sent{};
    memset(&g_c98w, 0, sizeof(g_c98w)); // card 308: 238 kB, not through a stack temporary
    g_c104_relayed = C104Relayed{}; // card 104: the old map's actors are gone
    g_c104_pending = C104Pending{}; // card 140
    g_c289 = C289Credited{};        // card 289: the old map's credited secrets
    g_c221_off = C221HostOff{};    // card 221: the old map's host-disabled pickups
    // Card 326: moved from c98_arm_map, so state tables are cleared at map end only.
    g_c98_replaying = false; // hb55: a replay cut off by the map change does not carry over
    g_c313_replay = C313Replay{}; // card 313
    if (g_c315.scans != 0) {
        log::line("card315: host map: %u scans, %u looping timeline rows sent, %u chars (rows only)", g_c315.scans,
                  g_c315.rows, g_c315.chars);
    }
    if (g_client_session) {
        log::line("card315: client looping timelines %d, corrections %u, largest %.0f ms (rows %u, in phase %u, "
                  "unaged %u, own not playing %u, no copy %u)", g_c315.nseen, g_c315.corrections, g_c315.largest_ms,
                  g_c315.received, g_c315.in_sync, g_c315.unaged, g_c315.not_playing, g_c315.missed);
    }
    memset(&g_c315, 0, sizeof(g_c315)); // card 315: per map
    if (g_c487.relayed + g_c487.alive + g_c487.gone + g_c487.full != 0) {
        log::line("card487: host map: hide checks relayed %u kill rows, %u alive, %u gone, %u not checked (list full)",
                  g_c487.relayed, g_c487.alive, g_c487.gone, g_c487.full);
    }
    g_c487 = C487State{}; // card 487: per map
    if (g_client_session) {
        // Card 132: the copies are actors of the old world, which the map change tears down with it; none carries over.
        // Logged at every client map end, zero included, so the count is always in the log.
        log::line("card132: client map end: %d card 104 copies tracked, %u game destroys refused this map",
                  g_c104_copies.n, g_c132_refused);
    }
    g_c132_refused = 0;
    if (g_c140_placed.rows != 0 || g_c140_placed.full != 0 || g_c140_placed.unnamed != 0) {
        log::line("%scard140: host map: %u placed move rows (%u actors journalled), %d placed actors tracked, %u not "
                  "tracked (table full), %u unnamed", g_c140_placed.full != 0 || g_c140_placed.unnamed != 0 ? "WARN " : "",
                  g_c140_placed.rows, g_c140_placed.journalled, g_c140_placed.n, g_c140_placed.full,
                  g_c140_placed.unnamed);
    }
    g_c140_placed = C140PlacedState{}; // card 140: the old map's actors are gone (rows come from the tick only)
    g_c104_copies = C104Copies{}; // client: copies are made from received rows, on the tick only
    g_c287 = C287Blends{};        // card 287: their motions go with them
}

int32_t c98_live() {
    return g_c98w.n;
}

void c98_arm_map() {
    pl_map(); // card 155: the map-end summary
    c137_change_arm_map(); // card 137 2a
    if (g_c98.n > 0) {
        c98_dump("map change");
    }
    g_c98 = C98State{};
    c121_arm_map(); // card 121
    net_budget_arm_map(); // card 132 diagnostic
    c132_arm_map(); // card 132
    c132_hit_arm_map(); // card 132 hit blood
    c166_arm_map(); // card 166 shooter markers
    c333_arm_map(); // card 333 tactical slot flags
    c101_forget(nullptr); // card 105: the old map's pawns are gone
}

// Card 163: both sides; the host relays. Every UADHealthComponent damage event goes through this Broadcast (OnDeath
// +0xF8, +0x108, +0x118: its 4 callers). A placed satchel building that a damage event destroys (its Blueprint's damage
// path: Become Destroyed and Killed by Damage, not ReceiveOnSatchelExploded; the client's nuke on the host's
// Plasmabug building in the 2026-11-08 playtest) is relayed as an 'S' kC163DamageKill row. Only the outermost
// broadcast is watched (Become Destroyed's own ApplyDamageAtLocation on the building nests one), and none inside
// ReceiveOnSatchelExploded (that path has its own row).
void detour_c163_broadcast(const void* delegate, const void* event) {
    static uint32_t depth = 0;
    void* const receiver = !g_client_session && depth == 0 && !g_c163_in_satchel && event != nullptr &&
                                   g_c132_satchel_class != nullptr
                               ? read_ptr(event, kDamageEventReceiver) : nullptr;
    const bool watch = receiver != nullptr && is_a(receiver, g_c132_satchel_class()) && !c98_bp_destroyed(receiver);
    C98World row{};
    const bool relay = watch && c98_satchel_row(receiver, kC163DamageKill, row);
    const void* const world = relay ? g_actor_get_world(receiver) : nullptr;
    ++depth;
    g_orig_c163_broadcast(delegate, event);
    --depth;
    if (!relay || !c98_bp_destroyed(receiver)) {
        return;
    }
    log::line("card163: host t=%llu %s (%s) destroyed by a damage event (Destroyed 0 -> 1): relayed as S %u",
              static_cast<unsigned long long>(GetTickCount64()), row.name, class_name(receiver).text, kC163DamageKill);
    c98_host_world(row, world, true);
    c299_nest_row(receiver, "host"); // card 299: the satchel-building nests die here, not in c98_host_kill
}

} // namespace ubw::hooks
