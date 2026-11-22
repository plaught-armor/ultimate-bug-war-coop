// Card 588: what moves the client's own gunship seat, and how smoothly.
//
// User report 2026-11-21: "The client looks laggy in the gunship". The Door Gunner seat
// (BP_MountableTurret_Gunner_C) flies its spline in its own ReceiveTick on the host only; card 561 keeps the client
// copy's tick off. The client possesses the seat (an ACharacter, LocalRole 2 AutonomousProxy), so
// AActor::OnRep_ReplicatedMovement does not apply ReplicatedMovement to it (that path is SimulatedProxy only), and the
// most likely mover is the host CMC's ClientAdjustPosition corrections, which arrive one round trip late and snap.
//
// Client only. While the own pawn is a turret, the per-frame step measures how the seat's location changes and a row
// every kC588RowMs says what moved it: OnRep_ReplicatedMovement calls on the seat, and ClientAdjustPosition calls on
// its CMC with their mean and largest correction distance.
//
// Fix (b415 data, c588b415a): the seat moved only on corrections, about 16 per second at 60 fps, 70 to 80 uu each,
// with 0 OnRep. The view stood still for 3 to 4 frames and then jumped. The client now flies the seat between
// corrections: each correction gives a sample of the host's location, two samples give the velocity, and the seat is
// placed at the last sample plus velocity x (time since it + the one-way lag). The correction itself is led by the
// same lag before the CMC applies it, so the correction frame does not step back.

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

namespace {
constexpr ULONGLONG kC588RowMs = 1000;
constexpr uint32_t kC588Rows = 120; // rows per map; a ride is about 20 s per seat
constexpr double kC588LagCapMs = 250.0;   // card 159 kDsLeadCapMs: the one-way lag the lead uses at most
constexpr ULONGLONG kC588HoldMs = 250;    // no correction this long: the seat holds (stopped, or the link stalled)
constexpr ULONGLONG kC588MinGapMs = 20;   // two corrections closer than this give no velocity sample
constexpr double kC588JumpUu = 1000.0;    // a sample this far from the last one is a teleport (mount: 7724 uu)
constexpr double kC588MaxSpeed = 5000.0;  // uu/s; a faster sample is a teleport too (flight: about 1200)

struct C588Window {
    uint32_t frames, moved, onrep, adjust;
    double step_sum, step_max, adjust_sum, adjust_max;
};
struct C588Lead {
    Vec3 sample;      // last correction location, as the host sent it
    Vec3 vel;         // uu/s, from the last two samples
    ULONGLONG sample_ms;
    bool have_sample, have_vel;
};
struct C588 {
    const void* seat; // own possessed turret; compared only, never read in the detours
    C588Lead lead;
    uint32_t led;     // frames this window the step placed the seat
    Vec3 last;
    bool have_last;
    ULONGLONG row_ms;
    C588Window w;
    // per-map totals
    uint32_t rows, seats, frames, moved, onrep, adjust, led_total, teleports;
    double step_max, adjust_max;
};
C588 g_c588{};

double c588_lag_ms() {
    const double lag = static_cast<double>(pl_rtt_floor()) / 2.0;
    return lag < kC588LagCapMs ? lag : kC588LagCapMs;
}

// The led location at now_ms, or false when there is no velocity or the last correction is too old.
bool c588_led(ULONGLONG now_ms, Vec3* out) {
    const C588Lead& l = g_c588.lead;
    if (!l.have_vel || now_ms - l.sample_ms > kC588HoldMs) {
        return false;
    }
    const double t = (static_cast<double>(now_ms - l.sample_ms) + c588_lag_ms()) / 1000.0;
    *out = Vec3{l.sample.x + l.vel.x * t, l.sample.y + l.vel.y * t, l.sample.z + l.vel.z * t};
    return true;
}

void c588_row(const void* seat) {
    const C588Window& w = g_c588.w;
    Vec3 rep{};
    const bool have_rep = c132_rep_location(seat, &rep);
    const Vec3 at = actor_location(seat);
    const double dx = rep.x - at.x;
    const double dy = rep.y - at.y;
    const double dz = rep.z - at.z;
    if (g_c588.rows < kC588Rows) {
        log::line("card588: client seat %p role %u: %u frames, %u moved, step mean %.1f max %.1f uu, %u OnRep, %u "
                  "ClientAdjustPosition (mean %.1f max %.1f uu), %u frames led (speed %.0f, lag %.0f ms), "
                  "ReplicatedMovement %s %.1f uu from the actor",
                  seat, static_cast<unsigned>(*(static_cast<const uint8_t*>(seat) + kActorLocalRole)), w.frames,
                  w.moved, w.moved != 0 ? w.step_sum / w.moved : 0.0, w.step_max, w.onrep, w.adjust,
                  w.adjust != 0 ? w.adjust_sum / w.adjust : 0.0, w.adjust_max, g_c588.led,
                  sqrt(g_c588.lead.vel.x * g_c588.lead.vel.x + g_c588.lead.vel.y * g_c588.lead.vel.y), c588_lag_ms(),
                  have_rep ? "at" : "unresolved,",
                  have_rep ? sqrt(dx * dx + dy * dy + dz * dz) : 0.0);
    }
    ++g_c588.rows;
    g_c588.frames += w.frames;
    g_c588.moved += w.moved;
    g_c588.onrep += w.onrep;
    g_c588.adjust += w.adjust;
    g_c588.step_max = w.step_max > g_c588.step_max ? w.step_max : g_c588.step_max;
    g_c588.adjust_max = w.adjust_max > g_c588.adjust_max ? w.adjust_max : g_c588.adjust_max;
    g_c588.led_total += g_c588.led;
    g_c588.led = 0;
    g_c588.w = C588Window{};
}
} // namespace

// Client, per tick (ubw_post_tick): follow the own seat.
void c588_step(void* engine) {
    if (!g_client_session) {
        return;
    }
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    const void* const cls = pawn != nullptr ? g_turret_class() : nullptr;
    const void* const seat = cls != nullptr && is_a(pawn, cls) ? pawn : nullptr;
    if (seat != g_c588.seat) {
        if (g_c588.seat != nullptr && g_c588.w.frames != 0) {
            c588_row(g_c588.seat); // seat still valid: a destroyed seat was cleared by c588_forget first
        }
        g_c588.seat = seat;
        g_c588.lead = C588Lead{};
        g_c588.have_last = false;
        g_c588.w = C588Window{};
        g_c588.row_ms = GetTickCount64() + kC588RowMs;
        g_c588.seats += seat != nullptr ? 1 : 0;
    }
    if (seat == nullptr) {
        return;
    }
    Vec3 to{};
    if (c588_led(GetTickCount64(), &to) && g_set_actor_location != nullptr) {
        g_set_actor_location(const_cast<void*>(seat), &to, false, nullptr, kTeleportPhysics);
        ++g_c588.led;
    }
    const Vec3 at = actor_location(seat);
    ++g_c588.w.frames;
    if (g_c588.have_last) {
        const double dx = at.x - g_c588.last.x;
        const double dy = at.y - g_c588.last.y;
        const double dz = at.z - g_c588.last.z;
        const double d = sqrt(dx * dx + dy * dy + dz * dz);
        if (d > 0.01) {
            ++g_c588.w.moved;
            g_c588.w.step_sum += d;
            g_c588.w.step_max = d > g_c588.w.step_max ? d : g_c588.w.step_max;
        }
    }
    g_c588.last = at;
    g_c588.have_last = true;
    if (GetTickCount64() >= g_c588.row_ms) {
        g_c588.row_ms = GetTickCount64() + kC588RowMs;
        c588_row(seat);
    }
}

// detour_c98_destroy, client, every actor: one compare. The own seat is still valid here, so its last window is
// logged now; after this the seat is gone, so c588_step must not read it (c586b418a: the seat was destroyed in the
// same net frame as the SetPawn back to the trooper, before ubw_post_tick saw the pawn change, and the row read the
// freed seat).
void c588_forget(const void* actor) {
    if (actor != g_c588.seat || actor == nullptr) {
        return;
    }
    if (g_c588.w.frames != 0) {
        c588_row(actor);
    }
    g_c588.seat = nullptr;
    g_c588.lead = C588Lead{};
    g_c588.have_last = false;
    g_c588.w = C588Window{};
}

// detour_c588_onrep_movement, every actor: one compare.
void c588_note_onrep(const void* actor) {
    if (actor == g_c588.seat && actor != nullptr) {
        ++g_c588.w.onrep;
    }
}

// detour_client_adjust_position: one compare on the CMC's owner, then the correction distance.
void c588_note_adjust(const void* cmc, const double* new_loc) {
    if (g_c588.seat == nullptr || new_loc == nullptr || read_ptr(cmc, kCmcCharacterOwner) != g_c588.seat) {
        return;
    }
    const Vec3 server = read_vec3(new_loc, 0);
    const Vec3 here = actor_location(g_c588.seat);
    const double dx = server.x - here.x;
    const double dy = server.y - here.y;
    const double dz = server.z - here.z;
    const double d = sqrt(dx * dx + dy * dy + dz * dz);
    ++g_c588.w.adjust;
    g_c588.w.adjust_sum += d;
    g_c588.w.adjust_max = d > g_c588.w.adjust_max ? d : g_c588.w.adjust_max;
}

// detour_client_adjust_position, after c588_note_adjust: a correction on the own seat is a sample of the host's
// location. Returns the location the CMC applies: the sample led by the one-way lag (written to led_out), or new_loc
// itself when the seat is not the own turret or there is no velocity yet.
const double* c588_lead_adjust(const void* cmc, const double* new_loc, double* led_out) {
    if (g_c588.seat == nullptr || new_loc == nullptr || read_ptr(cmc, kCmcCharacterOwner) != g_c588.seat) {
        return new_loc;
    }
    C588Lead& l = g_c588.lead;
    const Vec3 s = read_vec3(new_loc, 0);
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG gap = now - l.sample_ms;
    if (l.have_sample && gap >= kC588MinGapMs) {
        const double dt = static_cast<double>(gap) / 1000.0;
        const Vec3 v{(s.x - l.sample.x) / dt, (s.y - l.sample.y) / dt, (s.z - l.sample.z) / dt};
        const double dx = s.x - l.sample.x;
        const double dy = s.y - l.sample.y;
        const double dz = s.z - l.sample.z;
        const double jump = sqrt(dx * dx + dy * dy + dz * dz);
        const bool teleport = jump > kC588JumpUu || jump / dt > kC588MaxSpeed;
        g_c588.teleports += teleport ? 1 : 0;
        // Two samples average the arrival jitter; a teleport starts over with no velocity.
        const Vec3 avg{(l.vel.x + v.x) / 2, (l.vel.y + v.y) / 2, (l.vel.z + v.z) / 2};
        l.vel = teleport ? Vec3{} : (l.have_vel ? avg : v);
        l.have_vel = !teleport;
    }
    if (!l.have_sample || gap >= kC588MinGapMs) {
        l.sample = s;
        l.sample_ms = now;
    }
    l.have_sample = true;
    Vec3 led{};
    if (!c588_led(now, &led)) {
        return new_loc;
    }
    led_out[0] = led.x;
    led_out[1] = led.y;
    led_out[2] = led.z;
    return led_out;
}

C588OnRepFn g_orig_c588_onrep_movement = nullptr;

void detour_c588_onrep_movement(void* actor) {
    c588_note_onrep(actor);
    g_orig_c588_onrep_movement(actor);
}

void c588_map_end() {
    if (g_c588.seats != 0) {
        log::line("card588: client seat motion: %u seats, %u rows, %u frames, %u moved, step max %.1f uu, %u OnRep, "
                  "%u ClientAdjustPosition (max %.1f uu), %u frames led, %u teleport samples",
                  g_c588.seats, g_c588.rows, g_c588.frames, g_c588.moved, g_c588.step_max, g_c588.onrep,
                  g_c588.adjust, g_c588.adjust_max, g_c588.led_total, g_c588.teleports);
    }
    g_c588 = C588{};
}

} // namespace ubw::hooks
