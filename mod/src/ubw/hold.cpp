// Card 133: relayed hold interactions. A timed (hold) interaction on a card 054 relay class ends in the interface's
// OnTimedInteractionCompleted (exe+0x4A20020: IADInteractableInterface, not overridden by any relay class; it clears
// the hold flags and calls OnInteraction, interface vtable +0x78), on whichever side ran the hold.
//
// Client to host (card 132 rule): the client's own hold completing is the event. The client sends HoldDone with the
// target's placed name and location; the host finds the same target (name nearest to that location), checks it is
// within kC133RangeUu of both the client's location and the server copy, and that CanInteract passes on the copy,
// then runs the original OnTimedInteractionCompleted once with the copy's interactor. The host does not re-run the
// hold: card 054 does not start one for a timed class (c50_interact), and a hold completing on a server copy anyway
// is refused here with a WARN, so OnInteraction runs once on the host (the backstop; card 248 below stops the cause).
//
// Card 248: a hold's progress lives on the target (started +0x9, completed +0xa, progress +0xc), and every
// interactor's TickComponent (0x4A20230) whose focus is a started target calls TickInteractable (0x4A206D0) with
// itself, so a second player's interactor at the site doubles the progress and can complete the hold with its own
// interactor. On a card 054 relay class only the machine's own player ticks: on the host a server copy's interactor,
// on a client any interactor not the client's own, returns without ticking. Non-relay targets keep the base behaviour.
//
// Host to client: the client's own OnInteraction runs (the charge mesh and its timer show locally), but a satchel
// building on the client dies only from the host's 'S' world row, applied once (detour_c98_satchel blocks the local
// event; c98_client_satchel keeps the applied set). A client hold on a satchel site that sees no host row within the
// interaction time plus the fuse plus kC133GraceMs is logged as a WARN.
//
// Card 418, host to client: a site's fuse start (ASTSatchelPlacementSite::StartTimer, exe+0x5171A70) broadcasts
// SatchelTimerStarted, and only that starts the Blueprint's TriggeredSFX beep and the 0.5 s light pulse. A host plant
// runs StartTimer on the host only, so the client's copy stays silent and dark. The host sends every StartTimer that
// armed a site (byte kC133SiteArmed) to all clients as HoldDone "name x y z"; the client runs its copy's StartTimer
// once (skipped when its own hold armed it already) and watches for the host explode as for its own hold.

#include "ubw/hooks_internal.h"

#include <stdio.h>

namespace ubw::hooks {

C29InteractFn g_orig_c133_completed = nullptr;
C248TickFn g_orig_c248_tick = nullptr;
C133StartFn g_orig_c133_start = nullptr;
C29InteractFn g_orig_c456_started = nullptr;
C29InteractFn g_orig_c456_interrupted = nullptr;
C29InteractFn g_orig_c456_released = nullptr;

namespace {

constexpr double kC133RangeUu = 500.0;
constexpr ULONGLONG kC133GraceMs = 5000;
constexpr size_t kC133IsTimedSlot = 0x40;     // IADInteractableInterface::GetIsTimedInteraction
constexpr size_t kC133CanInteractSlot = 0x30; // CanInteract(UADInteractorComponent*, FADInteractionFailed&)
constexpr size_t kC133InteractTime = 0x90;    // from the interface subobject: GetInteractionTime_Implementation
constexpr size_t kC133SiteFuse = 0x508;       // ASTSatchelPlacementSite: StartTimer's timer rate (s)
constexpr size_t kC133SiteExploded = 0x410;   // ASTSatchelPlacementSite: the exploded delegate Explode broadcasts
constexpr size_t kC133SitePlaced = 0x50C;     // ASTSatchelPlacementSite: charge placed
constexpr size_t kC133SiteArmed = 0x50D;      // ASTSatchelPlacementSite: fuse started (StartTimer sets it)
constexpr size_t kC133FailedSize = 0x40;      // FADInteractionFailed (0x18 used); room to spare
// FADInteractionFailed: FailedInteractionReason (gameplay tag, 8 bytes) +0x00, FailedInteractionDescription (FText)
// +0x08. b239 (c247b239a host crash, exe+0x51504EB in ASTSatchelPlacementSite::CanInteract): the site copies its
// failure struct in, and its FText assignment released our FText at +0x00's Flags word (0x12) as a text pointer.
constexpr size_t kC133FailedText = 0x08;
constexpr int32_t kC133DelegateMax = 8;       // invocation list entries read
constexpr size_t kC133DelegateEntry = 0x10;   // FScriptDelegate: FWeakObjectPtr + FName
constexpr uint32_t kC133Pending = 8;
constexpr uint32_t kC133Applied = 32;
constexpr uint32_t kC248Logged = 16; // targets whose first dropped foreign tick was logged this map
constexpr uint32_t kC456Lines = 40;  // card 456: hold started/interrupted/released rows per map
constexpr size_t kC456Started = 0x9;   // IADInteractableInterface: hold started (OnTimedInteractionStarted sets it)
constexpr size_t kC456Completed = 0xA; // hold completed
constexpr size_t kC456Progress = 0xC;  // hold progress (float)
constexpr size_t kC456Holding = 0xFC;  // UADInteractorComponent: hold flag TickComponent tests before interrupting

using C133IsTimedFn = bool (*)(void* iface);
using C133CanInteractFn = bool (*)(void* iface, void* interactor, void* failed);

// Client: a satchel site hold that completed here, waiting for the host's row on its building.
struct C133Wait {
    char site[kChanNameMax + 1];
    int32_t building_id; // the building's object index, -1 = no building found (any applied row clears it)
    ULONGLONG due_ms;
};

struct C133State {
    C133Wait wait[kC133Pending];
    uint32_t waits;
    int32_t applied[kC133Applied]; // client: buildings the host's 'S' row was applied to this map (object index)
    uint32_t applieds;
    uint32_t sent, received, ran, refused, blocked, late;
    uint32_t arms_sent, arms_run, arms_own; // card 418: host StartTimer sends; client copies armed, already armed
    uint32_t foreign_ticks;              // card 248: TickInteractable calls dropped
    int32_t logged[kC248Logged];         // card 248: object index of each target logged
    uint32_t loggeds;
    uint32_t wait_peak;
    uint32_t c456_lines; // card 456 rows this map
};
C133State g_c133{}; // game thread

// Card 456: the last TraceForInteractable this machine ran (TickComponent calls it, then interrupts a held target
// that is not the result in the same tick). `found` is the game's own result, `used` what card 081/130 returned.
struct C456Trace {
    const void* interactor;
    const void* found;
    const void* used;
    ULONGLONG ms;
};
C456Trace g_c456_trace{}; // game thread

bool c133_timed(void* iface) {
    const auto fn = reinterpret_cast<C133IsTimedFn>(read_ptr(read_ptr(iface, 0), kC133IsTimedSlot));
    return fn != nullptr && fn(iface);
}

void c133_wide_text(const ue::reflect::FStringRaw* s, char* out, size_t cap) {
    size_t n = 0;
    for (int32_t i = 0; s != nullptr && s->data != nullptr && i < s->num && n + 1 < cap; ++i) {
        const wchar_t c = s->data[i];
        if (c != 0) {
            out[n++] = c < 0x80 ? static_cast<char>(c) : '?';
        }
    }
    out[n] = '\0';
}

} // namespace

// IADInteractableInterface::CanInteract (vtable +0x30; +0x28 is TryInteract, which starts or runs the interaction).
// The failure reason text goes to `why`. Card 123 shares it.
bool c133_can_interact(void* iface, void* interactor, char* why, size_t cap) {
    const auto fn = reinterpret_cast<C133CanInteractFn>(read_ptr(read_ptr(iface, 0), kC133CanInteractSlot));
    snprintf(why, cap, "%s", fn == nullptr ? "no CanInteract slot" : "");
    if (fn == nullptr) {
        return false;
    }
    alignas(16) uint8_t failed[kC133FailedSize] = {};
    g_ftext_from_string(failed + kC133FailedText, L"");
    const bool ok = fn(iface, interactor, failed);
    if (!ok && g_ftext_to_string != nullptr) {
        c133_wide_text(g_ftext_to_string(failed + kC133FailedText), why, cap);
    }
    g_ftext_dtor(failed + kC133FailedText);
    return ok;
}

namespace {

// The satchel building bound to a site's exploded delegate (the building that dies when the site explodes), or null.
void* c133_site_building(const void* site) {
    const auto* const list = static_cast<const uint8_t*>(read_ptr(site, kC133SiteExploded));
    const int32_t num = list != nullptr ? read_i32(site, kC133SiteExploded + kArrayNum) : 0;
    for (int32_t i = 0; i < num && i < kC133DelegateMax && g_weak_get != nullptr; ++i) {
        void* const bound = g_weak_get(const_cast<uint8_t*>(list) + static_cast<size_t>(i) * kC133DelegateEntry);
        if (bound != nullptr && text_contains(class_name(bound).text, "SatchelBuilding")) {
            return bound;
        }
    }
    return nullptr;
}

bool c133_applied_has(int32_t id) {
    for (uint32_t i = 0; i < g_c133.applieds; ++i) {
        if (g_c133.applied[i] == id) {
            return true;
        }
    }
    return false;
}

// Client: drops the waits a host row on building `id` answers. `id` -1 = no building known: none is dropped.
void c133_wait_clear(int32_t id, const char* building) {
    uint32_t kept = 0;
    for (uint32_t i = 0; i < g_c133.waits; ++i) {
        const C133Wait& w = g_c133.wait[i];
        if (w.building_id == id || w.building_id == -1) {
            log::line("card133: client site %s: host explode of %s arrived", w.site, building);
            continue;
        }
        g_c133.wait[kept++] = w;
    }
    g_c133.waits = kept;
}

// Client: a satchel site hold completed here; wait for the host's row on its building.
void c133_client_wait(void* site, void* iface, const char* name) {
    void* const building = c133_site_building(site);
    const float hold = read_f32(iface, kC133InteractTime);
    const float fuse = read_f32(site, kC133SiteFuse);
    const ULONGLONG due = GetTickCount64() + static_cast<ULONGLONG>((hold + fuse) * 1000.0f) + kC133GraceMs;
    const int32_t id = building != nullptr ? object_index(building) : -1;
    // Senior 2026-11-19: a site with no building bound (BP_SatchelPlacementSite_WithSwitch_C) has no explode to
    // wait for; its switch fuse (card 418) is the explode. No WARN and no wait.
    log::line("card133: client site %s (building %s): interaction time %.1f s, fuse %.1f s, charge placed %d; %s", name,
              building != nullptr ? object_name(building).text : "none bound", hold, fuse,
              static_cast<int>(static_cast<const uint8_t*>(site)[kC133SitePlaced]),
              building != nullptr ? "host explode expected" : "no building, not watched (the switch fuse explodes)");
    if (building == nullptr || c133_applied_has(id)) {
        return; // no building, or the host's row came first
    }
    if (g_c133.waits >= kC133Pending) {
        log::line("WARN card133: client site %s: wait list full (%u), not watched", name, kC133Pending);
        return;
    }
    C133Wait& w = g_c133.wait[g_c133.waits++];
    snprintf(w.site, sizeof(w.site), "%s", name);
    w.building_id = id;
    w.due_ms = due;
    g_c133.wait_peak = g_c133.waits > g_c133.wait_peak ? g_c133.waits : g_c133.wait_peak;
}

// Client: this machine's own hold completed on a relay class. HoldDone goes to the host.
void c133_client_done(void* iface, void* interactor, void* target, const C54Relay* row) {
    char name[kChanNameMax + 1];
    const int32_t len = c50_name(target, name, sizeof(name));
    const bool placed = c50_placed(target);
    const Vec3 at = actor_location(target);
    bool sent = false;
    char text[kChanTextMax + 1];
    if (len > 0 && placed && c95_wire_target(target, text, sizeof(text)) > 0) {
        wchar_t wire[kChanTextFrame];
        const WireString w = chan_text_frame(ChanOp::HoldDone, text, wire);
        void* const pc = read_ptr(read_ptr(interactor, kComponentOwner), kPawnController);
        sent = w.num > 0 && pc != nullptr && chan_send_host(pc, w);
    }
    g_c133.sent += sent ? 1 : 0;
    log::line("%scard133: client t=%llu own hold completed on %s (%s, row %s) at (%.0f, %.0f, %.0f): HoldDone %s",
              sent ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()),
              len > 0 ? name : object_name(target).text, class_name(target).text, row->label, at.x, at.y, at.z,
              sent ? "sent" : (placed ? "not sent" : "not sent (not a placed actor)"));
    if (sent && row->kind == C54Kind::SatchelSite) {
        c133_client_wait(target, iface, name);
    }
}

// Host: one refusal of a client's HoldDone.
void c133_refuse(const char* from, const char* text, const char* why) {
    ++g_c133.refused;
    log::line("WARN card133: host t=%llu HoldDone from %s (%s) refused: %s",
              static_cast<unsigned long long>(GetTickCount64()), from, text, why);
}

// Card 248: the first dropped foreign tick on a target this map, one line (capped at kC248Logged targets).
void c248_note_drop(const void* target, const C54Relay* row, const void* owner, uint32_t s) {
    const int32_t id = object_index(target);
    for (uint32_t i = 0; i < g_c133.loggeds; ++i) {
        if (g_c133.logged[i] == id) {
            return;
        }
    }
    if (g_c133.loggeds >= kC248Logged) {
        return;
    }
    g_c133.logged[g_c133.loggeds++] = id;
    log::line("card248: %s t=%llu %s (%s): tick from %s %s dropped (only this machine's own player ticks a relay "
              "hold); later drops on it counted, not logged",
              side(), static_cast<unsigned long long>(GetTickCount64()), object_name(target).text, row->label,
              s == kSideServerCopy ? "server copy" : "non-own pawn", object_name(owner).text);
}

} // namespace

// Card 248: every interactor's tick of a started timed target. See the header comment.
float detour_c248_tick(void* iface, float dt, void* interactor) {
    const void* const target = iface_object(iface);
    const C54Relay* const row = target != nullptr ? c54_relay_row(target) : nullptr;
    const void* const owner = row != nullptr && interactor != nullptr ? read_ptr(interactor, kComponentOwner) : nullptr;
    const uint32_t s = owner != nullptr ? sprint_side(owner) : kSideOther;
    const bool foreign = owner != nullptr && (g_client_session ? s != kSideClientOwn : s == kSideServerCopy);
    if (!foreign) {
        return g_orig_c248_tick(iface, dt, interactor);
    }
    ++g_c133.foreign_ticks;
    c248_note_drop(target, row, owner, s);
    return 0.0f;
}

bool c133_is_timed(void* iface) {
    return iface != nullptr && c133_timed(iface);
}

// Both sides: every timed interaction's end. See the header comment for who runs what.
void detour_c133_completed(void* iface, void* interactor) {
    void* const target = const_cast<void*>(iface_object(iface));
    const C54Relay* const row = target != nullptr ? c54_relay_row(target) : nullptr;
    const uint32_t s = row != nullptr && interactor != nullptr ? sprint_side(read_ptr(interactor, kComponentOwner)) : kSideOther;
    if (s == kSideServerCopy && !g_client_session) {
        ++g_c133.refused;
        log::line("WARN card133: host t=%llu hold completed on %s (%s) for server copy %s: not run (the client's "
                  "HoldDone is the event; card 054 does not start this hold)",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(target).text, row->label,
                  object_name(read_ptr(interactor, kComponentOwner)).text);
        return;
    }
    if (s == kSideClientOwn && g_client_session) {
        c133_client_done(iface, interactor, target, row);
    }
    const void* const pawn = interactor != nullptr ? read_ptr(interactor, kComponentOwner) : nullptr;
    const bool press = pawn != nullptr && c492_switch_press(target, pawn); // card 492: host only, switches only
    g_orig_c133_completed(iface, interactor);
    if (press) {
        c602_send_press(target, pawn); // card 602: the other clients' copies broadcast too
    }
}

// Host: a client's HoldDone ("name x y z"). The checks, then the original completion once with the copy's interactor.
void c133_host_done(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    char name[kC95TargetMax + 1]; // card 230
    static_assert(kC95TargetMax == 129, "card 230: the %129s width below");
    Vec3 at{};
    const ObjectName from_name = object_name(pc);
    const char* const from = from_name.text;
    ++g_c133.received;
    if (chan_text(s, text, sizeof(text)) <= 0 || sscanf_s(text, "%129s %lf %lf %lf", name, static_cast<unsigned>(sizeof(name)), &at.x, &at.y, &at.z) != 4) {
        c133_refuse(from, "?", "bad frame");
        return;
    }
    void* const pawn = read_ptr(pc, kControllerPawn);
    void* const interactor = pawn != nullptr && sprint_side(pawn) == kSideServerCopy
                                 ? pawn_interactor(pawn)
                                 : nullptr;
    void* const target = interactor != nullptr ? c95_find_nearest(g_actor_get_world(pawn), name, at) : nullptr;
    const C54Relay* const row = target != nullptr ? c54_relay_row(target) : nullptr;
    void* const iface = row != nullptr ? g_get_interface_address(target, g_c50_interactable_class()) : nullptr;
    if (iface == nullptr) {
        c133_refuse(from, text, interactor == nullptr ? "no server copy interactor"
                                : target == nullptr  ? "no placed actor has that name"
                                : row == nullptr     ? "target not in the card 054 relay table"
                                                     : "target has no IADInteractableInterface");
        return;
    }
    char why[kChanTextMax + 1];
    // A child actor is measured from its parent (the wire's location), with the child's own offset added to the limit.
    const Vec3 anchor = actor_location(c95_anchor(target));
    const double limit = kC133RangeUu + vec_dist(actor_location(target), anchor);
    const double to_client = vec_dist(anchor, at);
    const double to_copy = vec_dist(anchor, actor_location(pawn));
    snprintf(why, sizeof(why), "target %s (%s) %.0f uu from the client's location, %.0f uu from the copy (limit %.0f)",
             object_name(target).text, row->label, to_client, to_copy, limit);
    if (to_client > limit || to_copy > limit || !c133_timed(iface)) {
        c133_refuse(from, text, !c133_timed(iface) ? "not a timed interaction (the card 054 press relay owns it)" : why);
        return;
    }
    char fail[kChanTextMax + 1];
    if (!c133_can_interact(iface, interactor, fail, sizeof(fail))) {
        char both[kChanTextMax + 1];
        snprintf(both, sizeof(both), "CanInteract false on the copy (\"%s\"); %s", fail, why);
        c133_refuse(from, text, both);
        return;
    }
    const bool press = c492_switch_press(target, pawn); // card 492: a switch close is a player trigger
    g_orig_c133_completed(iface, interactor);
    if (press) {
        c602_send_press(target, pawn); // card 602: the other clients' copies broadcast too
    }
    ++g_c133.ran;
    const void* const building = row->kind == C54Kind::SatchelSite ? c133_site_building(target) : nullptr;
    log::line("card133: host t=%llu HoldDone from %s: %s; CanInteract 1; OnInteraction run once with %s's interactor "
              "(OnTimedInteractionCompleted, run #%u); host site building %s%s",
              static_cast<unsigned long long>(GetTickCount64()), from, why, object_name(pawn).text, g_c133.ran,
              building != nullptr ? object_name(building).text : "none bound",
              row->kind == C54Kind::SatchelSite ? (static_cast<const uint8_t*>(target)[kC133SitePlaced] != 0 ? "; site charge placed 1"
                                                                                       : "; WARN site charge placed 0")
                                                : "");
}

// Card 456 (log only): one row per timed hold start, interrupt and release on both sides, with the holder's place and
// speed and the caller; an interrupt also names what the interactor trace returned in that tick.
namespace {
void c456_row(const char* what, void* iface, void* interactor, const void* ra) {
    if (iface == nullptr || !c133_timed(iface) || g_c133.c456_lines >= kC456Lines) {
        return;
    }
    ++g_c133.c456_lines;
    const void* const pawn = interactor != nullptr ? read_ptr(interactor, kComponentOwner) : nullptr;
    const void* const move = pawn != nullptr && is_a(pawn, g_player_mi_class()) ? read_ptr(pawn, kCharacterMovement) : nullptr;
    const Vec3 v = move != nullptr ? read_vec3(move, kMoveVelocity) : Vec3{};
    const double speed = move != nullptr ? sqrt(v.x * v.x + v.y * v.y + v.z * v.z) : -1.0;
    const Vec3 at = pawn != nullptr ? actor_location(pawn) : Vec3{};
    const auto* const h = static_cast<const uint8_t*>(iface);
    float progress = 0.0f;
    memcpy(&progress, h + kC456Progress, sizeof(progress));
    char trace[2 * kNameChars + 64] = "";
    if (strcmp(what, "interrupted") == 0) {
        const bool same = g_c456_trace.interactor == interactor;
        snprintf(trace, sizeof(trace), ", holding %u, trace now %s (game %s, %llu ms ago)",
                 interactor != nullptr ? static_cast<const uint8_t*>(interactor)[kC456Holding] : 0u,
                 same ? (g_c456_trace.used != nullptr ? object_name(iface_object(g_c456_trace.used)).text : "null") : "unknown",
                 same ? (g_c456_trace.found != nullptr ? object_name(iface_object(g_c456_trace.found)).text : "null") : "unknown",
                 static_cast<unsigned long long>(GetTickCount64() - g_c456_trace.ms));
    }
    log::line("card456: %s t=%llu hold %s on %s by %s, pawn at (%.1f, %.1f, %.1f), speed %.1f, started %u completed %u "
              "progress %.2f%s, caller exe+0x%llX",
              side(), static_cast<unsigned long long>(GetTickCount64()), what, object_name(iface_object(iface)).text,
              object_name(pawn).text, at.x, at.y, at.z, speed, h[kC456Started], h[kC456Completed],
              static_cast<double>(progress), trace, exe_rva(ra));
}
} // namespace

void detour_c456_started(void* iface, void* interactor) {
    g_orig_c456_started(iface, interactor);
    c456_row("started", iface, interactor, _ReturnAddress());
}

void detour_c456_interrupted(void* iface, void* interactor) {
    c456_row("interrupted", iface, interactor, _ReturnAddress());
    g_orig_c456_interrupted(iface, interactor);
}

void detour_c456_released(void* iface, void* interactor) {
    c456_row("released", iface, interactor, _ReturnAddress());
    g_orig_c456_released(iface, interactor);
}

void c456_note_trace(const void* interactor, const void* found, const void* used) {
    g_c456_trace = C456Trace{interactor, found, used, GetTickCount64()};
}

// Card 418, host: one site's fuse started (own hold, a client's HoldDone, or a master site arming its links).
void detour_c133_start(void* site) {
    g_orig_c133_start(site);
    if (g_client_session || static_cast<const uint8_t*>(site)[kC133SiteArmed] == 0) {
        return;
    }
    char text[kChanTextMax + 1];
    if (c95_wire_target(site, text, sizeof(text)) <= 0) {
        log::line("WARN card418: host site %s fuse started: no wire name, the clients stay unarmed",
                  object_name(site).text);
        return;
    }
    const C95Fanout f = c95_send_all(g_actor_get_world(site), ChanOp::HoldDone, text);
    g_c133.arms_sent += f.sent > 0 ? 1 : 0;
    log::line("%scard418: host t=%llu site %s fuse %.1f s started: arm sent to %d of %d clients (%s)",
              f.sent == f.to ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), object_name(site).text,
              static_cast<double>(read_f32(site, kC133SiteFuse)), f.sent, f.to, text);
}

// Card 418, client: the host's arm of one site ("name x y z"). The copy's StartTimer runs once (beep, light pulse,
// the local fuse whose explode card 133 blocks), unless this machine's own hold armed it.
void c133_client_arm(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    char name[kC95TargetMax + 1];
    static_assert(kC95TargetMax == 129, "card 230: the %129s width below");
    Vec3 at{};
    if (chan_text(s, text, sizeof(text)) <= 0 || sscanf_s(text, "%129s %lf %lf %lf", name, static_cast<unsigned>(sizeof(name)), &at.x, &at.y, &at.z) != 4) {
        log::line("WARN card418: client arm frame unreadable");
        return;
    }
    void* const site = c95_find_nearest(g_actor_get_world(pc), name, at);
    const C54Relay* const row = site != nullptr ? c54_relay_row(site) : nullptr;
    if (row == nullptr || row->kind != C54Kind::SatchelSite) {
        log::line("WARN card418: client arm of %s: %s", text, site == nullptr ? "no placed actor has that name" : "not a satchel site");
        return;
    }
    if (static_cast<const uint8_t*>(site)[kC133SiteArmed] != 0) {
        ++g_c133.arms_own;
        log::line("card418: client t=%llu arm of %s: armed already (own hold)",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(site).text);
        return;
    }
    g_orig_c133_start(site);
    ++g_c133.arms_run;
    log::line("card418: client t=%llu arm of %s from the host: StartTimer run, armed %d, fuse %.1f s",
              static_cast<unsigned long long>(GetTickCount64()), object_name(site).text,
              static_cast<int>(static_cast<const uint8_t*>(site)[kC133SiteArmed]), static_cast<double>(read_f32(site, kC133SiteFuse)));
    void* const iface = g_get_interface_address(site, g_c50_interactable_class());
    if (iface != nullptr) {
        c133_client_wait(site, iface, name);
    }
}

// Client, from detour_c98_satchel: the local site's explosion reached a placed, non-replicated building. Blocked;
// the building dies only from the host's 'S' row.
void c133_client_blocked(void* building, bool premature) {
    ++g_c133.blocked;
    log::line("card133: client t=%llu local ReceiveOnSatchelExploded(%d) on %s blocked: the host's 'S' row decides "
              "(%s)",
              static_cast<unsigned long long>(GetTickCount64()), premature ? 1 : 0, object_name(building).text,
              c133_applied_has(object_index(building)) ? "already applied from the host" : "host row not yet here");
}

// Client, from c98_client_satchel: true when the host's row on `building` was applied already (skip it). Otherwise
// it is recorded as applied, and the waits it answers are dropped.
bool c133_client_applied(void* building) {
    const int32_t id = object_index(building);
    if (c133_applied_has(id)) {
        return true;
    }
    if (g_c133.applieds < kC133Applied) {
        g_c133.applied[g_c133.applieds++] = id;
    } else {
        log::line("WARN card133: client applied list full (%u): %s not kept", kC133Applied, object_name(building).text);
    }
    c133_wait_clear(id, object_name(building).text);
    return false;
}

// Client, every tick: a satchel site whose host explode is overdue.
void c133_client_step(ULONGLONG now) {
    uint32_t kept = 0;
    for (uint32_t i = 0; i < g_c133.waits; ++i) {
        const C133Wait& w = g_c133.wait[i];
        if (now >= w.due_ms) {
            ++g_c133.late;
            log::line("WARN card133: client site %s completed, no host explode", w.site);
            continue;
        }
        g_c133.wait[kept++] = w;
    }
    g_c133.waits = kept;
}

// Card 326: the registry's "holds" map end (was c133_map at the arm).
void c133_map_end() {
    if (g_c133.sent + g_c133.received + g_c133.blocked + g_c133.refused + g_c133.foreign_ticks + g_c133.arms_sent +
            g_c133.arms_run + g_c133.arms_own != 0) {
        log::line("card133: %s map totals: HoldDone sent %u, received %u, run %u, refused %u; foreign ticks dropped %u "
                  "on %u targets (card 248); local explodes blocked %u; host explodes late %u; %u waits dropped; "
                  "peaks: waits %u of %u, applied %u of %u; card 418 arms sent %u, run %u, own %u",
                  side(), g_c133.sent, g_c133.received, g_c133.ran, g_c133.refused, g_c133.foreign_ticks,
                  g_c133.loggeds, g_c133.blocked, g_c133.late,
                  g_c133.waits, g_c133.wait_peak, kC133Pending, g_c133.applieds, kC133Applied, g_c133.arms_sent,
                  g_c133.arms_run, g_c133.arms_own);
    }
    g_c133 = C133State{};
}

int32_t c133_live() {
    return static_cast<int32_t>(g_c133.waits + g_c133.applieds);
}

// Card 323 "holds" join replay (host): not settled. Hold progress is not synced live either: each machine ticks only
// its own player's hold (card 248), and no host-to-client hold progress op exists (HoldDone goes client to host; host
// to client it is only card 418's fuse start, not replayed: a joiner sees an armed site silent), so there is no
// current value to replay. The outcome reaches the joiner through the target actor (the 'S' world row). Until a
// progress op is decided this logs the row the registry expects and sends nothing.
void c133_join_replay(void* pc) {
    log::line("card323: host t=%llu holds join replay to %s: not sent (no hold progress op; %u HoldDone run this map)",
              static_cast<unsigned long long>(GetTickCount64()), object_name(pc).text, g_c133.ran);
}

} // namespace ubw::hooks
