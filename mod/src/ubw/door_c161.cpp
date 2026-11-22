// Card 161: the Blueprint doors (BP_Door_Base: the sliding and ramp doors) are host owned. BP_Door_Base derives from
// BP_KillableBuilding_Base, not ASTDoor, so card 095's OpenDoor/CloseDoor hooks never see it, and each side's own
// door logic ran on its own copies: the NPCDetectionCollider overlap events ("Open Door" / "Close Door", the
// montage on DoorSkeletalMesh, Block Nav / Unblock Nav on PawnBuffer) fired for whatever NPCs that side saw (the
// 2026-11-08 playtest: the client opened and closed BP_Door_Sliding_Large_C_0 by itself, 3 -> 0 -> 3 on PawnBuffer
// apart from the host's CollisionOp rows).
// Host: every tick each tracked door's DoorState (EMoveType: Closed 0, Open 1, Opening 2, Closing 3) is read; every
// change, whatever wrote it, goes out as card 095's DoorOp with the state digit ('0'..'3') and the PawnBuffer
// CollisionEnabled through its journal, so a joining client gets the last state in the first-Pong replay.
// Client: the door's own triggers are refused at ProcessEvent (the NPCDetectionCollider begin and end overlap events,
// the "Recheck Blockers" close timer and any "Open Door" / "Close Door" / "Toggle Door" not ours). A host state
// that crosses between the open half (1, 2) and the closed half (0, 3) first calls "Open Door" / "Close Door" on the
// copy of that placed name, so its montage and sounds run; then the client writes the host's DoorState and
// PawnBuffer (card 161 review after c161c: the client never settles a door on its own, c161c's close held at
// Closing while the client's pawn stood in the door). A DoorState change the client's own Blueprint makes is set
// back to the host's on the same tick and logged as client-only. Montage callbacks and the damage event still run.
// Both sides: each DoorState change is one line with the PawnBuffer CollisionEnabled, and the first ProcessEvent of
// each (door, function) is one line (which events drive a door, ran or refused).

#include "ubw/hooks_internal.h"

#include <math.h>
#include <string.h>

namespace ubw::hooks {

bool g_c507_vm = false; // client, a door is tracked: detour_c507_local_script compares door functions
LocalScriptFn g_orig_c507_local_script = nullptr;

namespace {

constexpr int32_t kC161Doors = 32;         // tracked doors per map (FED_C1M1: 2)
constexpr ULONGLONG kC161ScanMs = 5000;    // the door scan repeats this often (sublevels stream in)
constexpr uint32_t kC161StateLines = 120;  // DoorState change lines per map
constexpr uint32_t kC161PeLines = 80;      // first-call ProcessEvent lines per map
constexpr int32_t kC161PeSeen = 128;       // (door, function) pairs remembered for those lines
constexpr int32_t kC161ParmsMax = 64;      // bytes: the "Open Door" / "Close Door" parms buffer
constexpr uint8_t kC161StateMax = 3;       // EMoveType: Closed 0, Open 1, Opening 2, Closing 3
constexpr uint8_t kC161NoHost = 0xFF;      // client: no host state received for this door yet

// b461 card 573: an actor's bit in C161State::pe_mask. UObjects are 16-byte aligned, so bits 4 and up vary.
uint64_t c161_pe_bit(const void* object) {
    const uintptr_t p = reinterpret_cast<uintptr_t>(object);
    return uint64_t{1} << (((p >> 4) ^ (p >> 10)) & 63u);
}
constexpr uint32_t kC161OwnLines = 40;
constexpr ULONGLONG kC270HoldMs = 500;     // client: a montage-settled DoorState waits this long for the host's row
constexpr uint32_t kC270HoldLines = 40;    // hold lines per map
constexpr int32_t kC507Fns = 16;          // distinct door UFunctions the BP VM gate compares (one door class: 6)
constexpr uint32_t kC507Lines = 40;        // first-seen BP VM refusal lines per map
constexpr ULONGLONG kC507RateMs = 60000;   // the BP VM gate's call counts are logged this often
constexpr int kC161BufferMax = 5;          // ECollisionEnabled: NoCollision .. QueryAndProbe     // client-only DoorState change lines per map
constexpr wchar_t kC161Class[] = L"/Game/Blueprints/EnvironmentElements/Doors/Parents/BP_Door_Base.BP_Door_Base_C";

// The client refuses these on a door unless the mod itself calls them (c161_call).
constexpr const wchar_t* kC161Refused[] = {
    L"BndEvt__BP_Door_Base_NPCDetectionCollider_K2Node_ComponentBoundEvent_2_ComponentBeginOverlapSignature__"
    L"DelegateSignature",
    L"BndEvt__BP_Door_Base_NPCDetectionCollider_K2Node_ComponentBoundEvent_3_ComponentEndOverlapSignature__"
    L"DelegateSignature",
    L"Open Door",
    L"Close Door",
    L"Toggle Door",
    L"Recheck Blockers",
};
constexpr int32_t kC161RefusedN = 6;
constexpr int32_t kC161Open = 2;  // index of "Open Door" in kC161Refused
constexpr int32_t kC161Close = 3; // index of "Close Door"

struct C161Door {
    C31Weak weak;
    void* actor; // for the ProcessEvent pointer compare only; reads go through weak
    char name[kChanNameMax + 1];
    void* fn[kC161RefusedN];
    void* pawn_buffer;
    uint8_t state; // last DoorState read
    uint8_t sent;  // host: the last DoorState sent, or the map's own at tracking
    uint8_t host;  // client: the host's last DoorState (kC161NoHost before the first)
    int host_buffer; // client: the host's last PawnBuffer CollisionEnabled, -1 when not given
    bool host_row;   // client (card 507): a host row came; false while host / host_buffer are the map's own baseline
    ULONGLONG hold_ms; // client (card 270): a montage-settled DoorState is held until then; 0 when none held
    ULONGLONG c288_since; // host (card 288): a player-trigger close held since then; 0 when none held
    ULONGLONG c288_check; // host (card 288): the next re-check of a held close
    double c288_inner;    // host (card 288): the inner side's sign along the door's local x
    uint32_t c288_group;  // host (card 498): the objective trigger that closed it last; 0 = a presence-fired close
};

struct C161PeSeen {
    const void* door;
    const void* fn;
};

struct C161State {
    C161Door d[kC161Doors];
    int32_t n;
    uint64_t pe_mask; // b461 card 573: one bit per tracked door actor (c161_pe_bit); a clear bit skips the door scan
    void* cls;
    int32_t state_at; // DoorState offset, -1 unresolved
    ULONGLONG scan_due;
    C161PeSeen seen[kC161PeSeen];
    int32_t seen_n;
    uint32_t state_lines, pe_lines, refused, applied, missed, sends, full, own, own_lines, hold_lines, baseline_lines;
    // Card 507, client: the BP VM gate (detour_c507_local_script). vm_fn holds every tracked door's fn[] once.
    void* vm_fn[kC507Fns];
    int32_t vm_fn_n;
    uint32_t vm_fn_full; // door functions not gated: vm_fn was full
    C161PeSeen vm_seen[kC507Lines];
    uint32_t vm_lines, vm_calls, vm_hits, vm_refused, vm_refused_map;
    ULONGLONG vm_rate_at; // the next count line; 0 before the first
    bool calling; // c161_call is running a door function
};

C161State g_c161 = [] {
    C161State s{};
    s.state_at = -1;
    return s;
}();

uint64_t g_c161_fname[kC161RefusedN] = {};

int c161_buffer(const C161Door& d) {
    return d.pawn_buffer != nullptr ? static_cast<const uint8_t*>(d.pawn_buffer)[kBodyCollisionEnabled] : -1;
}

// Card 161 run 3 diagnostic: CollisionEnabled of the door's other collision components (Blocker1, Blocker2 and
// DoorSkeletalMesh, from the BP_Door_Sliding_Base construction script), so a run shows which one blocks pawns in
// each DoorState. -1 when the component is missing. Called on a state change only (StaticFindObject per name).
struct C161Coll {
    int b1, b2, mesh;
};

C161Coll c161_coll(void* actor) {
    const auto read = [actor](const char* name) {
        const auto* const c = static_cast<const uint8_t*>(c95_component(actor, name));
        return c != nullptr ? static_cast<int>(c[kBodyCollisionEnabled]) : -1;
    };
    return C161Coll{read("Blocker1"), read("Blocker2"), read("DoorSkeletalMesh")};
}

C161Door* c161_by_name(const char* name) {
    for (int32_t i = 0; i < g_c161.n; ++i) {
        if (strcmp(g_c161.d[i].name, name) == 0) {
            return &g_c161.d[i];
        }
    }
    return nullptr;
}

bool c161_known(const void* actor) {
    for (int32_t i = 0; i < g_c161.n; ++i) {
        if (c31_weak_get(g_c161.d[i].weak) == actor) {
            return true;
        }
    }
    return false;
}

// Card 507, client: the door's functions into the BP VM gate's compare set, each UFunction once (a door class shares
// them). A full set leaves a function ungated: one WARN per map.
void c507_vm_add(const C161Door& d) {
    for (int32_t f = 0; g_client_session && f < kC161RefusedN; ++f) {
        bool have = d.fn[f] == nullptr;
        for (int32_t i = 0; i < g_c161.vm_fn_n && !have; ++i) {
            have = g_c161.vm_fn[i] == d.fn[f];
        }
        if (have) {
            continue;
        }
        if (g_c161.vm_fn_n < kC507Fns) {
            g_c161.vm_fn[g_c161.vm_fn_n++] = d.fn[f];
        } else if (g_c161.vm_fn_full++ == 0) {
            log::line("WARN card507: client door %s function %s: BP VM gate set full (%d), not gated", d.name,
                      object_name(d.fn[f]).text, kC507Fns);
        }
    }
}

// One placed, non-replicated door into the table: its name, functions, PawnBuffer and current state.
void c161_add(void* actor) {
    if (g_c161.n >= kC161Doors) {
        ++g_c161.full;
        return;
    }
    C161Door& d = g_c161.d[g_c161.n];
    d = C161Door{};
    if (c50_name(actor, d.name, sizeof(d.name)) <= 0) {
        log::line("WARN card161: %s door %s: name not sendable, not tracked", side(), object_name(actor).text);
        return;
    }
    c31_weak_set(d.weak, actor);
    d.actor = actor;
    g_c161.pe_mask |= c161_pe_bit(actor);
    for (int32_t f = 0; f < kC161RefusedN; ++f) {
        d.fn[f] = fname_cached(g_c161_fname[f], kC161Refused[f]) != 0 ? g_find_function(actor, g_c161_fname[f])
                                                                       : nullptr;
    }
    c507_vm_add(d);
    d.pawn_buffer = c95_component(actor, "PawnBuffer");
    d.state = static_cast<const uint8_t*>(actor)[g_c161.state_at];
    d.sent = d.state;
    d.host = kC161NoHost;
    d.host_buffer = -1;
    if (g_client_session) {
        // Card 507: the host sends only changes, and a door it has not touched is in the map's own state, so that
        // state is the host's until its first row. Our own level script's open is then set back (c161_poll).
        d.host = d.state;
        d.host_buffer = c161_buffer(d);
    }
    ++g_c161.n;
    const Vec3 at = actor_location(actor);
    const C161Coll c = c161_coll(actor);
    log::line("%scard161: %s t=%llu door %s (%s) at (%.0f, %.0f, %.0f) tracked: DoorState %u, PawnBuffer %d, "
              "Blocker1 %d, Blocker2 %d, DoorSkeletalMesh %d, functions open %d close %d overlap %d/%d",
              d.fn[kC161Open] != nullptr && d.fn[kC161Close] != nullptr ? "" : "WARN ", side(),
              static_cast<unsigned long long>(GetTickCount64()), d.name, class_name(actor).text, at.x, at.y, at.z,
              d.state, c161_buffer(d), c.b1, c.b2, c.mesh, d.fn[kC161Open] != nullptr, d.fn[kC161Close] != nullptr, d.fn[0] != nullptr,
              d.fn[1] != nullptr);
}

void c161_scan(const void* world) {
    g_c161.cls = g_c161.cls != nullptr ? g_c161.cls : find_class(kC161Class);
    if (g_c161.cls == nullptr) {
        return; // the class is not loaded: this map has no such door yet
    }
    if (g_c161.state_at < 0) {
        const uint8_t* const f = prop_find_prop(g_c161.cls, "DoorState");
        g_c161.state_at = f != nullptr ? read_i32(f, kPropertyOffset) : -2;
        log::line("%scard161: %s BP_Door_Base DoorState offset %d", f != nullptr ? "" : "WARN ", side(),
                  g_c161.state_at);
    }
    if (g_c161.state_at < 0) {
        return;
    }
    static C23List list;
    c23_collect(world, nullptr, g_c161.cls, list);
    for (uint32_t i = 0; i < list.count; ++i) {
        void* const a = list.e[i].actor;
        const bool replicated = (static_cast<const uint8_t*>(a)[kActorRepFlags] & 0x08u) != 0; // bReplicates
        if (c50_placed(a) && !replicated && !c161_known(a)) {
            c161_add(a);
        }
    }
    pe_gate_set(kPeGate_c161_pe, g_c161.n > 0);
    g_c507_vm = g_client_session && g_c161.vm_fn_n > 0;
}

bool c161_open_half(uint8_t state) {
    return state == 1 || state == 2;
}

// Both sides: one line per DoorState change (from d.state), capped per map.
void c161_state_line(const C161Door& d, uint8_t* a, uint8_t state) {
    if (g_c161.state_lines < kC161StateLines) {
        ++g_c161.state_lines;
        const C161Coll c = c161_coll(a);
        log::line("card161: %s t=%llu door %s DoorState %u -> %u, PawnBuffer %d, Blocker1 %d, Blocker2 %d, "
                  "DoorSkeletalMesh %d", side(), static_cast<unsigned long long>(GetTickCount64()), d.name, d.state,
                  state, c161_buffer(d), c.b1, c.b2, c.mesh);
    }
    if (d.state == 0 && c161_open_half(state)) {
        c495_door_opened(d.name); // card 495: the client's drawn weapon at the open and 100 ms later
    }
}

// Client: the host's DoorState and PawnBuffer onto our copy (the one client write path for a door's state).
void c161_write(C161Door& d, uint8_t* a) {
    a[g_c161.state_at] = d.host;
    d.state = d.host;
    if (d.pawn_buffer != nullptr && d.host_buffer >= 0 && c161_buffer(d) != d.host_buffer) {
        const int32_t was = c161_buffer(d);
        g_orig_set_collision_enabled(d.pawn_buffer, static_cast<uint8_t>(d.host_buffer));
        c137_host_apply(a, "component collision", was, d.host_buffer, 0); // card 149: the mod's apply, no game caller
    }
}

// Card 507, client: a DoorState change before any host row for the door, set back to the map's own baseline.
void c507_baseline_line(const C161Door& d, uint8_t state) {
    if (g_c161.baseline_lines >= kC161OwnLines) {
        return;
    }
    ++g_c161.baseline_lines;
    log::line("card507: client t=%llu door %s client-only DoorState %u -> %u before any host row, set back to the "
              "baseline %u (PawnBuffer %d, set back to %d)", static_cast<unsigned long long>(GetTickCount64()),
              d.name, d.state, state, d.host, c161_buffer(d), d.host_buffer);
}

// Client: a DoorState change our own Blueprint made, not the host's: set back to the host's, counted and logged.
void c161_own(C161Door& d, uint8_t* a, uint8_t state) {
    ++g_c161.own;
    if (!d.host_row) {
        c507_baseline_line(d, state);
    } else if (g_c161.own_lines < kC161OwnLines) {
        ++g_c161.own_lines;
        log::line("WARN card161: client t=%llu door %s client-only DoorState %u -> %u, set back to the host's %u "
                  "(PawnBuffer %d, host %d)", static_cast<unsigned long long>(GetTickCount64()), d.name, d.state,
                  state, d.host, c161_buffer(d), d.host_buffer);
    }
    c161_write(d, a);
}

// Card 270: the door montage's OnCompleted settles Opening (2) to Open (1) and Closing (3) to Closed (0) on each side
// on its own; the client's copy can settle a few ms before the host's row for the same step arrives. True while
// such a step waits for that row (kC270HoldMs from the first poll that saw it); DoorState and PawnBuffer are not
// written meanwhile. The host's row ends the hold (c161_receive); with no row, the step is set back as client-only.
// Card 288 took the door reactions off the client's presence edges (c288_client_edge), so a client-only close
// (DoorState 1 -> 3) is no longer a presence step. What is left is the door's own close delay, which runs on both
// sides: b40b166a and b40b168a show the client's fire 24 ms after the host's and 8 ms before the host's row, and the
// set-back then opened the door for one frame and restarted its close montage. That step is held the same way.
bool c270_hold(C161Door& d, uint8_t state) {
    const bool timer = d.host == 1 && state == 3; // the door's own close delay
    const bool settled = (d.host == 2 && state == 1) || (d.host == 3 && state == 0) || timer;
    const ULONGLONG now = GetTickCount64();
    if (!settled) {
        d.hold_ms = 0;
        return false;
    }
    if (d.hold_ms == 0) {
        d.hold_ms = now + kC270HoldMs;
        if (g_c161.hold_lines < kC270HoldLines) {
            ++g_c161.hold_lines;
            log::line("card270: client t=%llu door %s %s DoorState %u -> %u before the host's row, "
                      "held %llu ms (PawnBuffer %d, host %d)", static_cast<unsigned long long>(now), d.name,
                      timer ? "own close delay started" : "montage settled", d.state, state,
                      static_cast<unsigned long long>(kC270HoldMs), c161_buffer(d), d.host_buffer);
        }
        return true;
    }
    if (now < d.hold_ms) {
        return true;
    }
    d.hold_ms = 0;
    return false;
}

bool c288_hold(C161Door& d, uint8_t before, uint8_t state);

// Both sides: a DoorState change is a line; the host sends every change, the client sets back its own.
void c161_poll(C161Door& d) {
    auto* const a = static_cast<uint8_t*>(c31_weak_get(d.weak));
    if (a == nullptr) {
        return;
    }
    const uint8_t state = a[g_c161.state_at];
    if (state == d.state) {
        return;
    }
    if (g_client_session && d.host != kC161NoHost && state != d.host) {
        if (!c270_hold(d, state)) {
            c161_own(d, a, state);
        }
        return;
    }
    d.hold_ms = 0;
    c161_state_line(d, a, state);
    const uint8_t before = d.state;
    d.state = state;
    if (!g_client_session && c288_hold(d, before, state)) {
        return; // card 288: the close is held and the door reopened; the Closing step is not sent
    }
    if (!g_client_session && state != d.sent && state <= kC161StateMax) {
        d.sent = state;
        ++g_c161.sends;
        c95_host_door_state(a, state, c161_buffer(d));
    }
}

// Both sides: the mod's own call of "Open Door" / "Close Door" on a door (client: the host's DoorOp; host: the test
// command). Null or why not.
const char* c161_call(C161Door& d, char op) {
    void* const actor = c31_weak_get(d.weak);
    void* const fn = d.fn[op == 'O' ? kC161Open : kC161Close];
    if (actor == nullptr || fn == nullptr) {
        return actor == nullptr ? "door gone" : "no Open Door / Close Door on this class";
    }
    const uint16_t size = *reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(fn) + kFunctionParmsSize);
    if (size > kC161ParmsMax) {
        return "parms larger than the buffer";
    }
    alignas(16) uint8_t parms[kC161ParmsMax] = {};
    g_c161.calling = true;
    g_orig_process_event(actor, fn, parms);
    g_c161.calling = false;
    return nullptr;
}

// Card 288 (host): a door close that a player trigger starts waits until every living player is through. The presence
// chain (card 227's LR_Door_Toggle_C on BP_PlayerPresence_C_7 for C1M1 door _0) reaches "Close Door" inside the
// Blueprint VM, not through ProcessEvent (the client log of 2026-11-13 has no "Close Door" / "Toggle Door" PE line
// before its DoorState 1 -> 3), so the hold sits on the DoorState change: an open-half to Closing step that c161_poll
// sees within kC288TriggerMs of a player presence firing (c288_presence_fired) is a player-trigger close. If any
// living player is on the outer side of the door or inside its PawnBuffer box, "Open Door" runs at once, the Closing
// step is not sent, and c288_step closes the door on the first re-check that finds every living player inside.
// Inner side: the side of the door's local x (its root's ComponentToWorld) where the triggering pawn stood. Dead or
// downed players and players who left (gone from the roster) do not hold the door; no timeout. NPC closes (no player
// presence fired in the window) and the mod's own calls (g_c161.calling) are unchanged.
constexpr ULONGLONG kC288TriggerMs = 250; // a Closing step this soon after a player presence fired is player-triggered
constexpr ULONGLONG kC288CheckMs = 250;   // held close re-check period
constexpr size_t kC288Outside = 160;      // the "outside: ..." text of the held row
constexpr size_t kC288Scale = 0x40;       // FTransform<double>: quat +0, translation +0x20, scale +0x40

struct C288Trigger {
    ULONGLONG ms; // when a player presence last fired; 0 = none, or consumed by a release
    Vec3 at;      // the triggering pawn's location then
    uint32_t group; // card 498: the card 492 objective trigger's hold group; 0 for a presence-fired trigger
};
C288Trigger g_c288{};
uint32_t g_c288_groups = 0; // card 498: the last hold group number given out (per process: a group never repeats)

struct C288Count {
    int32_t inside;             // living players on the inner side and clear of the PawnBuffer box
    int32_t living;             // living player pawns (the host's roster: every PlayerController's pawn)
    char outside[kC288Outside]; // the others: "<pawn> (outer side|PawnBuffer), ..."
};

double c288_f64(const void* base, size_t off) {
    double v = 0.0;
    memcpy(&v, static_cast<const uint8_t*>(base) + off, sizeof(v));
    return v;
}

// Signed distance of p along the door's local x (its root's ComponentToWorld), from the root's location.
double c288_along(const void* door, const Vec3& p) {
    const void* const root = read_ptr(door, kActorRootComponent);
    if (root == nullptr) {
        return 0.0;
    }
    const double x = c288_f64(root, kComponentRotation), y = c288_f64(root, kComponentRotation + 8),
                 z = c288_f64(root, kComponentRotation + 16), w = c288_f64(root, kComponentRotation + 24);
    const Vec3 f{1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y + w * z), 2.0 * (x * z - w * y)}; // local x in world
    const Vec3 o = read_vec3(root, kComponentTranslation);
    return (p.x - o.x) * f.x + (p.y - o.y) * f.y + (p.z - o.z) * f.z;
}

// The PawnBuffer box as card 126's shape (ComponentToWorld, BoxExtent times |scale|). False: no PawnBuffer.
bool c288_box(const void* buffer, C126Shape& out) {
    out = C126Shape{};
    if (buffer == nullptr) {
        return false;
    }
    out.kind = C126ShapeKind::Box;
    for (int i = 0; i < 4; ++i) {
        out.q[i] = c288_f64(buffer, kComponentRotation + static_cast<size_t>(i) * sizeof(double));
    }
    out.center = read_vec3(buffer, kComponentTranslation);
    const Vec3 sc = read_vec3(buffer, kComponentRotation + kC288Scale);
    out.ext = Vec3{c126_num(buffer, "BoxExtent", 0) * fabs(sc.x), c126_num(buffer, "BoxExtent", 8) * fabs(sc.y),
                   c126_num(buffer, "BoxExtent", 16) * fabs(sc.z)};
    return true;
}

// Card 498: true when the door's tested set (c288_count) includes door j: d itself, or, for an objective-trigger close
// (d.c288_group != 0), every tracked door the same trigger closed.
bool c288_in_set(const C161Door& d, const C161Door& j) {
    return &j == &d || (d.c288_group != 0 && j.c288_group == d.c288_group);
}

// One door's half-plane test of a pawn: true when it is on the inner side (j.c288_inner) and clear of the PawnBuffer.
// `why` gets "outer side" or "PawnBuffer" on false.
bool c288_through(const C161Door& j, const void* pawn, const Vec3& at, const char*& why) {
    const void* const door = c31_weak_get(j.weak);
    if (door == nullptr) {
        return true; // a door gone with its level holds nobody
    }
    C126Shape box{};
    const bool inner = c288_along(door, at) * j.c288_inner > 0.0;
    const bool in_box = inner && c288_box(j.pawn_buffer, box) && c126_capsule_inside(box, pawn, at);
    why = !inner ? "outer side" : "PawnBuffer";
    return inner && !in_box;
}

// Living players through door d: on the inner side and clear of the PawnBuffer box of every door in d's tested set
// (c288_in_set). Card 498: the doors one card 492 objective trigger closes form one hold group, so a player outside
// any of them holds every one (c492b251a: the per-door test let Gigantic_C_1 close with the host behind Medium_C_1,
// on Gigantic_C_1's inner side; card 498's first fix, BP_PlayerPresence_C_5 overlap, counted that host inside, since
// the presence box reaches 4000 uu past the stadium doors). A pawn that is Downed, Out or Dead (c43_is_down) is not
// counted (card 227's rule). Card 300 (b180): a roster pawn that is a mech is its pilot's place (card 227 counts it
// too), tested by its own location and root capsule.
C288Count c288_count(const C161Door& d, const void* door) {
    C288Count c{};
    const void* const world = g_actor_get_world(door);
    if (world == nullptr || g_player_character_class == nullptr) {
        return c;
    }
    const void* const cls = g_player_character_class();
    const void* const mech_cls = g_mech_class != nullptr ? g_mech_class() : nullptr; // card 300 (b180)
    const C43Roster r = c43_roster(world);
    size_t used = 0;
    for (int32_t s = 0; s < kC43Max; ++s) {
        const void* const pawn = r.p[s].pawn;
        if (pawn == nullptr || (!is_a(pawn, cls) && (mech_cls == nullptr || !is_a(pawn, mech_cls))) ||
            c43_is_down(pawn)) {
            continue;
        }
        ++c.living;
        const Vec3 at = actor_location(pawn);
        const char* why = nullptr;
        const C161Door* fail = nullptr;
        for (int32_t j = 0; j < g_c161.n && fail == nullptr; ++j) {
            fail = c288_in_set(d, g_c161.d[j]) && !c288_through(g_c161.d[j], pawn, at, why) ? &g_c161.d[j] : nullptr;
        }
        if (fail == nullptr) {
            ++c.inside;
            continue;
        }
        const int n = snprintf(c.outside + used, sizeof(c.outside) - used, "%s%s (%s of %s)", used > 0 ? ", " : "",
                               object_name(pawn).text, why, fail->name);
        used = n > 0 && used + static_cast<size_t>(n) < sizeof(c.outside) ? used + static_cast<size_t>(n)
                                                                            : sizeof(c.outside) - 1;
    }
    return c;
}

// Card 498, host: a group door's close is held, so every other door of its group that the same trigger closed and
// that is not held yet (its own close already ran, ahead of this one in c161_step's pass) is reopened and held too.
void c288_hold_group(const C161Door& d, ULONGLONG now) {
    for (int32_t j = 0; d.c288_group != 0 && j < g_c161.n; ++j) {
        C161Door& o = g_c161.d[j];
        if (&o == &d || o.c288_group != d.c288_group || o.c288_since != 0) {
            continue;
        }
        const char* const why = c161_call(o, 'O');
        o.c288_since = now;
        o.c288_check = now + kC288CheckMs;
        log::line("card288: host t=%llu door %s close held: group %u with %s (reopened)%s%s",
                  static_cast<unsigned long long>(now), o.name, d.c288_group, d.name,
                  why != nullptr ? ", WARN reopen: " : "", why != nullptr ? why : "");
    }
}

// c161_poll, host, after a DoorState change from `before` to `state`: true when it is a player-trigger close that is
// now held (the door is reopened and the Closing step is not sent). A held door that something else closes (no player
// presence in the window: an NPC or the level) ends its hold.
bool c288_hold(C161Door& d, uint8_t before, uint8_t state) {
    const ULONGLONG now = GetTickCount64();
    const bool closing = c161_open_half(before) && !c161_open_half(state);
    const bool player = g_c288.ms != 0 && now - g_c288.ms <= kC288TriggerMs;
    void* const door = c31_weak_get(d.weak);
    if (!closing || g_c161.calling || door == nullptr) {
        return false;
    }
    if (!player) {
        if (d.c288_since != 0) {
            log::line("card288: host t=%llu door %s hold ended: closed by no player trigger after %llu ms",
                      static_cast<unsigned long long>(now), d.name,
                      static_cast<unsigned long long>(now - d.c288_since));
            d.c288_since = 0;
        }
        return false;
    }
    d.c288_inner = c288_along(door, g_c288.at) >= 0.0 ? 1.0 : -1.0;
    d.c288_group = g_c288.group; // card 498: an objective trigger's doors are held as one group
    const C288Count c = c288_count(d, door);
    if (c.inside >= c.living) {
        return false; // every living player through (or none counted): the game's own close
    }
    const char* const why = c161_call(d, 'O');
    if (d.c288_since == 0) {
        d.c288_since = now;
        log::line("card288: host t=%llu door %s close held: %d of %d players inside (outside: %s)",
                  static_cast<unsigned long long>(now), d.name, c.inside, c.living, c.outside);
        log::line("card288: host door %s inner side %s of local x (trigger at %.0f uu), PawnBuffer %s, group %u%s%s",
                  d.name, d.c288_inner > 0.0 ? "+" : "-", c288_along(door, g_c288.at),
                  d.pawn_buffer != nullptr ? "found" : "missing", d.c288_group,
                  why != nullptr ? ", WARN reopen: " : "", why != nullptr ? why : "");
    }
    d.c288_check = now + kC288CheckMs;
    c288_hold_group(d, now);
    return true;
}

// c161_step, host: a held door closes on the first re-check that finds every living player inside.
void c288_step(C161Door& d, ULONGLONG now) {
    if (d.c288_since == 0 || now < d.c288_check) {
        return;
    }
    d.c288_check = now + kC288CheckMs;
    void* const door = c31_weak_get(d.weak);
    if (door == nullptr) {
        d.c288_since = 0;
        return;
    }
    const C288Count c = c288_count(d, door);
    if (c.inside < c.living) {
        return;
    }
    g_c288.ms = 0; // consumed: the close below must not read as a new player-trigger close
    const ULONGLONG since = d.c288_since;
    d.c288_since = 0;
    const char* const why = c161_call(d, 'C');
    log::line("%scard288: host t=%llu door %s closing: all %d players inside after %llu ms%s%s",
              why != nullptr ? "WARN " : "", static_cast<unsigned long long>(now), d.name, c.living,
              static_cast<unsigned long long>(now - since), why != nullptr ? ": " : "", why != nullptr ? why : "");
}

bool c161_seen(const void* door, const void* fn) {
    for (int32_t i = 0; i < g_c161.seen_n; ++i) {
        if (g_c161.seen[i].door == door && g_c161.seen[i].fn == fn) {
            return true;
        }
    }
    if (g_c161.seen_n < kC161PeSeen) {
        g_c161.seen[g_c161.seen_n++] = C161PeSeen{door, fn};
    }
    return false;
}

} // namespace

// Hot path (kPeGate_c161_pe): true refuses the call. A pointer compare per tracked door.
bool c161_pe(const void* object, const void* function) {
    if ((g_c161.pe_mask & c161_pe_bit(object)) == 0) {
        return false; // b461 card 573: not a tracked door (most calls); no walk over the door table
    }
    const C161Door* d = nullptr;
    for (int32_t i = 0; i < g_c161.n && d == nullptr; ++i) {
        d = g_c161.d[i].actor == object ? &g_c161.d[i] : nullptr;
    }
    if (d == nullptr) {
        return false;
    }
    bool refuse = false;
    for (int32_t f = 0; g_client_session && !g_c161.calling && f < kC161RefusedN && !refuse; ++f) {
        refuse = d->fn[f] == function;
    }
    g_c161.refused += refuse ? 1u : 0u;
    if (!c161_seen(object, function) && g_c161.pe_lines < kC161PeLines) {
        ++g_c161.pe_lines;
        log::line("card161: %s t=%llu door %s PE %s: %s", side(), static_cast<unsigned long long>(GetTickCount64()),
                  d->name, object_name(function).text, refuse ? "refused" : g_c161.calling ? "ran (mod call)" : "ran");
    }
    return refuse;
}

namespace {

// Card 507, client: a BP VM call of `node` on `context` hit the compare set. True refuses it: the door's own trigger
// function, not the mod's c161_call. First (door, function) pair logs once.
bool c507_vm_refuse(const void* context, const void* node) {
    ++g_c161.vm_hits;
    if (g_c161.calling) {
        return false;
    }
    const C161Door* d = nullptr;
    for (int32_t i = 0; i < g_c161.n && d == nullptr; ++i) {
        d = g_c161.d[i].actor == context ? &g_c161.d[i] : nullptr;
    }
    bool refuse = false;
    for (int32_t f = 0; d != nullptr && f < kC161RefusedN && !refuse; ++f) {
        refuse = d->fn[f] == node;
    }
    if (!refuse) {
        return false;
    }
    ++g_c161.vm_refused;
    ++g_c161.vm_refused_map;
    bool seen = false;
    for (uint32_t i = 0; i < g_c161.vm_lines && !seen; ++i) {
        seen = g_c161.vm_seen[i].door == context && g_c161.vm_seen[i].fn == node;
    }
    if (!seen && g_c161.vm_lines < kC507Lines) {
        g_c161.vm_seen[g_c161.vm_lines++] = C161PeSeen{context, node};
        log::line("card507: client t=%llu door %s BP VM %s: refused", static_cast<unsigned long long>(GetTickCount64()),
                  d->name, object_name(node).text);
    }
    return true;
}

// Card 507, client: the BP VM gate's cost, one line a minute while armed.
void c507_vm_rate(ULONGLONG now) {
    if (!g_c507_vm || now < g_c161.vm_rate_at) {
        return;
    }
    if (g_c161.vm_rate_at != 0) {
        log::line("card507: client t=%llu BP VM gate, last %llu ms: %u script function calls, %u door function calls, "
                  "%u refused",
                  static_cast<unsigned long long>(now), static_cast<unsigned long long>(kC507RateMs), g_c161.vm_calls,
                  g_c161.vm_hits, g_c161.vm_refused);
    }
    g_c161.vm_calls = 0;
    g_c161.vm_hits = 0;
    g_c161.vm_refused = 0;
    g_c161.vm_rate_at = now + kC507RateMs;
}

} // namespace

// Card 507: ProcessLocalScriptFunction(Context, Stack, Result), every local script UFunction body (BP VM calls through
// UObject::CallFunction and ProcessEvent both end here). The level script opens a door this way, never through
// ProcessEvent. The caller has already read the parameters into Stack; skipping the body leaves its bytecode position
// right. Miss path while armed: one counter and a compare against vm_fn.
void detour_c507_local_script(void* context, void* stack, void* result) {
    if (g_c507_vm) {
        ++g_c161.vm_calls;
        const void* const node = read_ptr(stack, kFrameNode);
        bool hit = false;
        for (int32_t i = 0; i < g_c161.vm_fn_n && !hit; ++i) {
            hit = g_c161.vm_fn[i] == node;
        }
        if (hit && c507_vm_refuse(context, node)) {
            return; // card 507: the client's own door trigger, refused (the host owns the door)
        }
    }
    if (g_c606_vm && c606_vm(context, stack)) {
        return; // card 606: the client's own horde zone Begin / End, refused (the host's row runs it)
    }
    const void* const node = c582_tracker_armed() ? read_ptr(stack, kFrameNode) : nullptr;
    g_orig_c507_local_script(context, stack, result);
    if (node != nullptr) {
        c582_tracker_local(context, node); // card 582 G4, host: a pickup tracker's delegate binds for every pawn
    }
}

void c161_step(void* engine) {
    void* const world = engine != nullptr ? g_get_game_world(engine) : nullptr;
    if (world == nullptr) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (now >= g_c161.scan_due) {
        g_c161.scan_due = now + kC161ScanMs;
        c161_scan(world);
    }
    c507_vm_rate(now);
    for (int32_t i = 0; i < g_c161.n; ++i) {
        c161_poll(g_c161.d[i]);
        if (!g_client_session) {
            c288_step(g_c161.d[i], now); // card 288: held player-trigger closes
        }
    }
}

// Card 288, host: a player presence is about to fire (card 227's detour and re-check, before the original). A door
// Closing step within kC288TriggerMs after this is a player-trigger close (c288_hold).
void c288_presence_fired(const void* pawn) {
    const bool mech = pawn != nullptr && g_mech_class != nullptr && is_a(pawn, g_mech_class()); // card 300 (b180)
    if (g_client_session || pawn == nullptr || g_player_character_class == nullptr ||
        (!is_a(pawn, g_player_character_class()) && !mech)) {
        return;
    }
    c288_trigger_at(actor_location(pawn), false);
}

// Card 288, host: a player-trigger close is about to start with `at` on the door's inner side. The objective-trigger
// path (card 492 fix 6) passes the presence centre: a counted pawn can still overlap the presence while standing on
// the outer side of the door line, so its location would put the inner side outside. Card 498: that path passes
// `objective`, and the doors this trigger closes form one hold group (c288_count); a presence-fired trigger keeps the
// per-door test.
void c288_trigger_at(Vec3 at, bool objective) {
    if (g_client_session) {
        return;
    }
    g_c288 = C288Trigger{GetTickCount64(), at, objective ? ++g_c288_groups : 0u};
}

C29InteractFn g_c602_on_interaction = nullptr;

namespace {

// Card 602: ASTSwitch::OnInteraction (exe+0x50F78A0, disassembled) takes the interactable interface at actor +0x2C8
// and never reads its interactor. It returns without broadcasting when the reactivation timer handle (interface
// +0xA0) is set, or when the lock byte (interface +0x40) is set while the component at interface +0x10 has its byte
// +0xF0 set (only a denied sound plays); otherwise it broadcasts OnPressSuccessful (actor +0x2E8).
constexpr size_t kC602Iface = 0x2C8;
constexpr size_t kC602Timer = 0xA0;
constexpr size_t kC602Lock = 0x40;
constexpr size_t kC602LockComp = 0x10;
constexpr size_t kC602LockCompByte = 0xF0;

bool c602_fires(const void* sw) {
    if (sw == nullptr || g_c54_switch_class == nullptr || !is_a(sw, g_c54_switch_class())) {
        return false;
    }
    const uint8_t* const iface = static_cast<const uint8_t*>(sw) + kC602Iface;
    if (read_ptr(iface, kC602Timer) != nullptr) {
        return false;
    }
    const uint8_t* const comp = static_cast<const uint8_t*>(read_ptr(iface, kC602LockComp));
    return iface[kC602Lock] == 0 || (comp != nullptr && comp[kC602LockCompByte] == 0);
}

} // namespace

// Card 492 fix 2, host: a press of `target` by `pawn` is about to run its original (instant: c102_interact; timed:
// detour_c133_completed and c133_host_done). A switch (its class has ASTSwitch's OnPressSuccessful property, of
// delegate type SwitchPressedDelegate: BP_Switch_C, BP_PanelSwitch_WithoutScreen_C) closes doors in the same Blueprint call (C3M2 Switch C_3 @5595, C5M1 Switch C_5
// @29072, C5M2 PanelSwitch C_1 @14353), so the press sets the card 288 trigger as a presence does.
bool c492_switch_press(const void* target, const void* pawn) {
    // b239: the property is OnPressSuccessful (NewProp_OnPressSuccessful of ASTSwitch); b237/b238 looked for the
    // delegate type name SwitchPressedDelegate, found none on any switch, and no press set the trigger.
    if (g_client_session || target == nullptr ||
        prop_find_prop(read_ptr(target, kObjectClass), "OnPressSuccessful") == nullptr) {
        return false;
    }
    c288_presence_fired(pawn);
    log::line("card492: host t=%llu switch press %s (%s) by %s: card 288 trigger set",
              static_cast<unsigned long long>(GetTickCount64()), object_name(target).text, class_name(target).text,
              object_name(pawn).text);
    return c602_fires(target);
}

// Card 602: the client's level script binds the OnPressSuccessful of its own switch copies (C3M2 Switch C_2 @24456 ->
// Plant_Generator_Reset -> GeneratorsOnCheck, the generator glow), and only a local OnInteraction broadcasts it. Card
// 054 runs a client's press on both machines; a host press ran on the host only, so the client's copy stayed
// unpressed and every graph bound to it (material parameters, the switch's own state) stayed undone there. After a
// host press that broadcast, every other client runs its own copy's OnInteraction (the presser's client ran its own
// press already).
void c602_send_press(const void* target, const void* pawn) {
    char name[kChanNameMax + 1];
    const ULONGLONG now = GetTickCount64();
    if (!c50_placed(target) || c50_level_key(target, name, sizeof(name)) <= 0) {
        log::line("WARN card602: host t=%llu switch press %s has no placed name: clients keep their copy unpressed",
                  static_cast<unsigned long long>(now), object_name(target).text);
        return;
    }
    const void* const world = g_actor_get_world(target);
    int32_t to = 0;
    int32_t sent = 0;
    for (int s = 0; world != nullptr && s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        const C50Copy* const c = pc != nullptr && sprint_side(pc) == kSideServerCopy ? c50_copy(object_index(pc), false)
                                                                                     : nullptr;
        if (c == nullptr || !c->ponged || read_ptr(pc, kControllerPawn) == pawn) {
            continue; // not a ready client, or the presser's own client (its local press ran)
        }
        ++to;
        sent += c95_send_one(pc, ChanOp::SwitchPress, name) ? 1 : 0;
    }
    log::line("%scard602: host t=%llu switch press %s by %s sent to %d of %d other clients",
              sent == to ? "" : "WARN ", static_cast<unsigned long long>(now), name, object_name(pawn).text, sent, to);
}

// Client: ChanOp::SwitchPress, the placed name of a switch the host pressed. Runs the copy's OnInteraction once; a
// copy whose own gate refuses (its reactivation timer pending, or locked) is logged and not forced.
void c602_press_receive(void* pc, const WireString* s) {
    char name[kChanNameMax + 1];
    const ULONGLONG now = GetTickCount64();
    const StaticClassFn* const cls[] = {&g_c54_switch_class};
    void* const sw = chan_text(s, name, sizeof(name)) > 0 && g_c54_switch_class != nullptr
                         ? c50_find_placed(g_actor_get_world(pc), cls, 1, name)
                         : nullptr;
    if (sw == nullptr || g_c602_on_interaction == nullptr) {
        log::line("WARN card602: client t=%llu switch press %s from the host: no placed switch has that name",
                  static_cast<unsigned long long>(now), sw == nullptr ? "(unread)" : name);
        return;
    }
    if (!c602_fires(sw)) {
        log::line("WARN card602: client t=%llu switch press %s from the host: own copy refuses (reactivation timer "
                  "pending or locked), not run",
                  static_cast<unsigned long long>(now), name);
        return;
    }
    g_c602_on_interaction(static_cast<uint8_t*>(sw) + kC602Iface, nullptr);
    log::line("card602: client t=%llu switch press %s from the host: own copy's OnInteraction ran (OnPressSuccessful)",
              static_cast<unsigned long long>(now), name);
}

// Client: the host's DoorOp for a name no ASTDoor has: "<name> <DoorState digit> [<PawnBuffer>]" (the journal
// replay leaves the PawnBuffer out; card 095's CollisionOp replay carries it). True when a tracked door took it.
bool c161_receive(const void* world, const char* name, char op, const char* text) {
    const bool digit = op >= '0' && op <= '0' + kC161StateMax;
    C161Door* d = digit ? c161_by_name(name) : nullptr;
    if (d == nullptr && digit && world != nullptr) {
        c161_scan(world); // card 507: a join replay row ahead of the periodic scan lands on the door, not dropped
        d = c161_by_name(name);
    }
    auto* const a = d != nullptr ? static_cast<uint8_t*>(c31_weak_get(d->weak)) : nullptr;
    if (d == nullptr || a == nullptr) {
        return d != nullptr; // a tracked name whose actor is gone: taken, nothing to apply
    }
    int buffer = -1;
    if (sscanf_s(text, "%*s %*c %d", &buffer) != 1 || buffer < -1 || buffer > kC161BufferMax) {
        buffer = -1;
    }
    const uint8_t host = static_cast<uint8_t>(op - '0');
    const uint8_t before = a[g_c161.state_at];
    const char* why = nullptr;
    if (c161_open_half(host) != c161_open_half(before)) {
        why = c161_call(*d, c161_open_half(host) ? 'O' : 'C'); // the door's own montage and sounds
    }
    if (d->hold_ms != 0) {
        log::line("card270: client t=%llu door %s held DoorState %u, host row %u: %s",
                  static_cast<unsigned long long>(GetTickCount64()), name, before, host,
                  host == before ? "confirmed" : "host differs, host's written");
        d->hold_ms = 0;
    }
    d->host = host;
    d->host_buffer = buffer;
    d->host_row = true;
    d->state = before;
    c161_state_line(*d, a, host);
    c161_write(*d, a);
    (why == nullptr ? g_c161.applied : g_c161.missed) += 1;
    log::line("%scard161: client t=%llu DoorOp state %u %s: %s (DoorState was %u, PawnBuffer %d, host %d)",
              why == nullptr ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), host, name,
              why == nullptr ? "applied" : why, before, c161_buffer(*d), buffer);
    return true;
}

// Test command (host): `door open|close <name>` for a tracked door. Null or why not.
const char* c161_cmd(const char* name, uint8_t op) {
    C161Door* const d = c161_by_name(name);
    if (d == nullptr) {
        return "no placed ASTDoor or BP_Door_Base door has that name";
    }
    log::line("card161: %s t=%llu cmd door %s %s", side(), static_cast<unsigned long long>(GetTickCount64()),
              op == 'O' ? "open" : "close", name);
    return c161_call(*d, static_cast<char>(op));
}

// Card 477 sweep: at LoadMap enter (and the test quit). The door class is a map Blueprint and the door table names
// old-world actors; both are read in c161_pe during the next LoadMap's BeginPlay calls, before c161_arm_map runs.
void c161_map_end() {
    if (g_c161.n != 0) {
        log::line("card161: %s map totals: %d doors tracked (%u not, table full), %u DoorState sends, %u client calls "
                  "refused, %u DoorOps applied, %u failed, %u client-only DoorState changes set back",
                  side(), g_c161.n, g_c161.full, g_c161.sends, g_c161.refused, g_c161.applied, g_c161.missed,
                  g_c161.own);
    }
    if (g_c507_vm) {
        log::line("card507: client map totals: BP VM gate %d door functions, %u not gated, %u calls refused",
                  g_c161.vm_fn_n, g_c161.vm_fn_full, g_c161.vm_refused_map);
    }
    const int32_t state_at = g_c161.state_at;
    g_c161 = C161State{}; // the class is looked up again: it may unload with the map
    g_c288 = C288Trigger{}; // card 288: no presence of the old map counts as a trigger
    g_c161.state_at = state_at; // one class layout: the offset holds
    pe_gate_set(kPeGate_c161_pe, false);
    g_c507_vm = false;
}

// The map's own arm: c161_map_end has already printed the totals and dropped the old map's tables; this reset
// covers the first map of a process, which has no LoadMap enter before it.
void c161_arm_map() {
    const int32_t state_at = g_c161.state_at;
    g_c161 = C161State{};
    g_c288 = C288Trigger{};
    g_c161.state_at = state_at;
    pe_gate_set(kPeGate_c161_pe, false);
    g_c507_vm = false;
}

} // namespace ubw::hooks
