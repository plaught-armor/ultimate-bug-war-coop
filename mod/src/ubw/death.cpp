// Death: the player death audit and host driver (card 030), downed and revive with its test driver
// (card 043), the client's game over and death screen (card 044), the co-op kill floor (card 046), and
// the kill-floor armour, checkpoint audit and respawn cursor (card 048), a mech is not a card 043 player
// (card 079), and a down player's interactions refused (card 081).

#include "ubw/hooks_internal.h"

#include <intrin.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace ubw::hooks {

void c46_note_start_death(const void* pawn);
void c48_complete_death(const void* pawn);
bool c43_death(void* pawn);
bool c43_skip_start_death(const void* pawn);
void c44_after_pe(void* object, const void* function, uint64_t name); // card 044: client game over, downed checks
void c44_complete_death(const void* pawn);
bool c43_health_pre(const void* owner);
void c43_health_post(const void* owner, float old_health, float new_health);

C81State g_c81{};

DealDamageFn g_deal_damage = nullptr;
DeathEventFn g_orig_player_death_event = nullptr;
DeathEventFn g_orig_start_death = nullptr;
DeathEventFn g_orig_start_death_mi = nullptr;
DeathEventFn g_bug_start_death = nullptr; // card 196: ASTPlayerCharacterBug::StartDeath (resolved, not hooked)
CompleteDeathFn g_orig_complete_death = nullptr;
SetAutoSaveFn g_orig_set_auto_save = nullptr;
ProcessEventFn g_orig_process_event = nullptr;
PeHot g_pe{}; // card 573 b463: detour_process_event's gate word and FName ids, one cache line
HealthChangedFn g_orig_health_changed = nullptr; // g_health_attribute_changed (card 021) enters the detour too
config::KillPlayer g_c30_mode = config::KillPlayer::Off; // written in ubw::init before any hook

constexpr size_t kPawnOnDeath = 0x898;         // ASTPlayerCharacter::OnDeath: FMulticastScriptDelegate (TArray)
constexpr size_t kPawnDeathTimer = 0x9DC;      // ASTPlayerCharacter::TimeUntilDeathCompletion (float)
constexpr size_t kPropertyRepNotify = 0x48;    // FProperty::RepNotifyFunc (FName) per UE5 source; unverified here,
                                               // so only its raw 8 bytes are logged (card030a crashed resolving it)
constexpr uint64_t kCpfRepNotify = 0x100000000ull; // CPF_RepNotify
// card030b: a 25-point self hit took only 0.7-1.5 Health (about 4%), and none in the first ~22 s on foot.
constexpr float kC30HitFraction = 4.0f;
constexpr ULONGLONG kC30KillDelayMs = 20000;
constexpr ULONGLONG kC30HitGapMs = 500;
constexpr uint32_t kC30ExtraHits = 2;
constexpr uint32_t kC30SelfHits = 3; // hits with the pawn as its own causer before the nearest enemy is used
constexpr uint32_t kC30MaxHits = 40;
constexpr ULONGLONG kC30PollMs = 100;
constexpr ULONGLONG kC30ShotMs[] = {500, 3500, 10000};
constexpr uint32_t kC30Shots = sizeof(kC30ShotMs) / sizeof(kC30ShotMs[0]);
constexpr ULONGLONG kC30ClientShotLagMs = 1000; // one shared Screenshots folder: same-name shots would collide
constexpr ULONGLONG kC30KeyMs = 5000;          // interact press after Health 0; the client presses kC30ClientKeyLagMs later
constexpr ULONGLONG kC30ClientKeyLagMs = 1000;
constexpr ULONGLONG kC30KeyHoldMs = 600;
constexpr ULONGLONG kC30AfterCompleteMs = 10000;
constexpr ULONGLONG kC30WindowMaxMs = 40000;

constexpr uint32_t kC30Lines = 200;      // other card 030 lines per map
constexpr uint32_t kC30HealthLines = 80;
constexpr uint32_t kC30Delegates = 8;
constexpr uint32_t kC30MaxSupers = 16;
constexpr int kC30MaxProps = 1024;

C30State g_c30{}; // game thread; reset per map by c30_arm_map

bool c30_line() {
    return note_event_cap(g_c30.lines, kC30Lines, "card030 death audit");
}

// The sprint side the KILL_PLAYER pawn has on this machine.
uint32_t c30_target_side() {
    const bool host = g_c30_mode == config::KillPlayer::Host;
    if (g_client_session) {
        return host ? kSideOther : kSideClientOwn;
    }
    return host ? kSideHostOwn : kSideServerCopy;
}

void c30_open(const char* why, const void* pawn) {
    if (g_c30.zero_ms != 0) {
        return;
    }
    g_c30.zero_ms = GetTickCount64();
    pe_gate_set(kPeGate_c30_window, true);
    log::line("card030: %s death window open (%s) on %s (%s); ProcessEvent lines from here", side(), why,
              object_name(pawn).text, kSprintSideText[sprint_side(pawn)]);
}

void c30_close(ULONGLONG now) {
    pe_gate_set(kPeGate_c30_window, false);
    log::line("card030: %s death window closed %llu ms after Health 0 (CompleteDeath %s): %u ProcessEvent lines, "
              "%u repeats not logged after the cap",
              side(), now - g_c30.zero_ms, g_c30.complete_ms != 0 ? "seen" : "not seen", g_c30.seen_count,
              g_c30.pe_dropped);
}

// Offset of a named FGameplayAttributeData in UADAttributeSetHealth, or -1.
int32_t c30_set_offset(const char* attribute) {
    const void* const cls = g_health_set_class != nullptr ? g_health_set_class() : nullptr;
    void* field = cls != nullptr ? read_ptr(cls, kStructChildProperties) : nullptr;
    for (int props = 0; props < kC21MaxSetProps && field != nullptr; ++props, field = read_ptr(field, kFieldNext)) {
        if (strcmp(fname_text(static_cast<const uint8_t*>(field) + kFieldName).text, attribute) == 0) {
            return read_i32(field, kPropertyOffset);
        }
    }
    return -1;
}

float c30_max_health(const void* component) {
    const void* const set = component != nullptr ? read_ptr(component, kHealthComponentSet) : nullptr;
    if (set == nullptr || g_c30.max_health_offset < 0) {
        return -1.0f;
    }
    return read_f32(set, static_cast<size_t>(g_c30.max_health_offset) + kAttrDataCurrent);
}

// Once per map: every property named *Death* or *Dead* on the pawn's class chain (blueprint class included).
void c30_log_death_props(const void* pawn) {
    g_c30.props_logged = true;
    const void* cls = read_ptr(pawn, kObjectClass);
    uint32_t found = 0;
    for (uint32_t s = 0; s < kC30MaxSupers && cls != nullptr; ++s, cls = read_ptr(cls, kStructSuper)) {
        void* field = read_ptr(cls, kStructChildProperties);
        for (int p = 0; p < kC30MaxProps && field != nullptr; ++p, field = read_ptr(field, kFieldNext)) {
            const auto* const f = static_cast<const uint8_t*>(field);
            const ObjectName name = fname_text(f + kFieldName);
            if (strstr(name.text, "Death") == nullptr && strstr(name.text, "Dead") == nullptr) {
                continue;
            }
            ++found;
            if (strcmp(name.text, "OnDeath") == 0) {
                g_c30.on_death_offset = read_i32(field, kPropertyOffset);
            }
            const uint64_t flags = read_u64(field, kPropertyFlags);
            log::line("card030: %s property %s.%s offset 0x%X flags 0x%016llX CPF_Net %d CPF_RepNotify %d raw+0x48 "
                      "0x%016llX",
                      side(), object_name(cls).text, name.text, read_i32(field, kPropertyOffset),
                      static_cast<unsigned long long>(flags), (flags & kCpfNet) != 0 ? 1 : 0,
                      (flags & kCpfRepNotify) != 0 ? 1 : 0,
                      static_cast<unsigned long long>(read_u64(field, kPropertyRepNotify)));
        }
    }
    log::line("card030: %s %u Death/Dead properties on the %s class chain", side(), found, class_name(pawn).text);
}

// Host: one DealDamage hit on the target; logs Health before and after.
void c30_hit(const void* world, void* target) {
    void* component = nullptr;
    const float before = c21_health(static_cast<const uint8_t*>(target), &component);
    const float max_health = c30_max_health(component);
    const bool self = g_c30.hits < kC30SelfHits || g_c30.landed != 0;
    void* const bug = self ? nullptr : c30_nearest_enemy(world, target);
    // No bug in reach: self-damage again with ignore_modifiers set (false landed nothing in card030a/b).
    const bool ignore_modifiers = !self && bug == nullptr;
    void* const causer = bug != nullptr ? bug : target;
    if (max_health <= 0.0f) {
        g_c30.driver_done = true;
        log::line("WARN card030: host hit %u: MaxHealth %.1f -- driver stopped", g_c30.hits + 1,
                  static_cast<double>(max_health));
        return;
    }
    ++g_c30.hits;
    g_c30.hits_after_zero += before <= 0.0f ? 1u : 0u;
    g_deal_damage(causer, target, max_health * kC30HitFraction, ignore_modifiers);
    const float after = c21_health(static_cast<const uint8_t*>(target), &component);
    g_c30.landed += after < before ? 1u : 0u;
    log::line("card030: host hit %u: DealDamage(%s (%s), causer %s, %.1f, %s): Health %.1f -> %.1f (lost %.1f of "
              "MaxHealth %.1f), TimeUntilDeathCompletion %.2f",
              g_c30.hits, object_name(target).text, kSprintSideText[sprint_side(target)], object_name(causer).text,
              static_cast<double>(max_health * kC30HitFraction), ignore_modifiers ? "true" : "false",
              static_cast<double>(before),
              static_cast<double>(after), static_cast<double>(before - after), static_cast<double>(max_health),
              static_cast<double>(read_f32(target, kPawnDeathTimer)));
}

// Host: waits kC30KillDelayMs after both pawns are on foot, then hits every kC30HitGapMs.
void c30_drive(const void* world, const C30Pawns& p, ULONGLONG now) {
    if (g_c30.both_ms == 0 && p.host_on_foot && p.copy_on_foot) {
        g_c30.both_ms = now;
        g_c30.next_hit_ms = now + kC30KillDelayMs;
        log::line("card030: host both player pawns on foot; first hit in %llu ms", kC30KillDelayMs);
    }
    if (g_c30.both_ms == 0 || now < g_c30.next_hit_ms || p.target == nullptr) {
        return;
    }
    g_c30.next_hit_ms = now + kC30HitGapMs;
    if (g_c30.hits_after_zero >= kC30ExtraHits || g_c30.hits >= kC30MaxHits) {
        g_c30.driver_done = true;
        log::line("%scard030: host driver done: %u hits, %u lowered Health, %u after Health 0", g_c30.landed != 0 ? "" : "WARN ",
                  g_c30.hits, g_c30.landed, g_c30.hits_after_zero);
        return;
    }
    c30_hit(world, p.target);
}

// Both sides: the target's Health poll, then the timed shots and interact key from Health 0.
void c30_window_step(void* pc, const C30Pawns& p, ULONGLONG now) {
    if (g_c30.zero_ms == 0 && p.target != nullptr) {
        void* component = nullptr;
        const float health = c21_health(static_cast<const uint8_t*>(p.target), &component);
        if (health <= 0.0f && health != -1.0f) { // -1: no health set
            c30_open("Health poll", p.target);
        }
    }
    if (g_c30.zero_ms == 0) {
        return;
    }
    const ULONGLONG t = now - g_c30.zero_ms;
    const ULONGLONG shot_lag = g_client_session ? kC30ClientShotLagMs : 0;
    if (g_c30.shots < kC30Shots && t >= kC30ShotMs[g_c30.shots] + shot_lag) {
        ++g_c30.shots;
        request_shot(pc, false);
        log::line("card030: %s shot %u at %llu ms after Health 0", side(), g_c30.shots, t);
    }
    const ULONGLONG key_at = kC30KeyMs + (g_client_session ? kC30ClientKeyLagMs : 0);
    if (!g_c30.key_done && t >= key_at) {
        g_c30.key_done = true;
        const bool sent = autoinput::key_event(pc, L"E", true);
        g_c30.key_up_ms = sent ? now + kC30KeyHoldMs : 0;
        log::line("%scard030: %s interact key E pressed at %llu ms after Health 0: %s", sent ? "" : "WARN ", side(), t,
                  sent ? "sent" : "not sent");
    }
    if (g_c30.key_up_ms != 0 && now >= g_c30.key_up_ms) {
        g_c30.key_up_ms = 0;
        autoinput::key_event(pc, L"E", false);
        log::line("card030: %s interact key E released", side());
    }
    const bool complete_end = g_c30.complete_ms != 0 && now >= g_c30.complete_ms + kC30AfterCompleteMs;
    if (pe_gate(kPeGate_c30_window) && (complete_end || t >= kC30WindowMaxMs)) {
        c30_close(now);
    }
}

void c30_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    if (!g_c30.armed || now < g_c30.next_poll_ms) {
        return;
    }
    g_c30.next_poll_ms = now + kC30PollMs;
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    if (pc == nullptr) {
        return;
    }
    const C30Pawns p = c30_find(world);
    if (p.target != nullptr && !g_c30.props_logged) {
        c30_log_death_props(p.target);
    }
    if (!g_client_session && !g_c30.driver_done) {
        c30_drive(world, p, now);
    }
    c30_window_step(pc, p, now);
}

void c30_note_event(const void* object, const void* function) {
    if (GetCurrentThreadId() != g_c30.thread) {
        return;
    }
    const void* const cls = read_ptr(object, kObjectClass);
    for (uint32_t i = 0; i < g_c30.seen_count; ++i) {
        if (g_c30.seen[i].cls == cls && g_c30.seen[i].function == function) {
            return;
        }
    }
    if (g_c30.seen_count >= kC30EventLines) {
        ++g_c30.pe_dropped;
        return;
    }
    g_c30.seen[g_c30.seen_count++] = C30Seen{cls, function};
    log::line("card030: %s PE +%llu ms %s.%s on %s", side(), GetTickCount64() - g_c30.zero_ms,
              object_name(cls).text, object_name(function).text, object_name(object).text);
}

namespace {

// Card 408: the ProcessEvent in progress on this thread, restored on every return of a nested one, refused or run
// (c136_attack_local reads it outside any ProcessEvent, so a stale object there is a freed one).
struct C128PeScope {
    const void* const prev_object;
    const void* const prev_function;
    C128PeScope(const void* object, const void* function)
        : prev_object(g_c128_pe_object), prev_function(g_c128_pe_function) {
        g_c128_pe_object = object;
        g_c128_pe_function = function;
    }
    ~C128PeScope() {
        g_c128_pe_object = prev_object;
        g_c128_pe_function = prev_function;
    }
    C128PeScope(const C128PeScope&) = delete;
    C128PeScope& operator=(const C128PeScope&) = delete;
};

// Card 572 step 3, timed calls only: when the original started and its ticks (both 0 when a refusal skipped it).
struct C572PeOrig {
    uint64_t start;
    uint64_t ticks;
};

// Card 573 b463: the gates of the notes and refusals ahead of the FName read, and of the notes after the original.
constexpr uint32_t kPeGatesEarly = kPeGate_c30_window | kPeGate_c32_pe | kPeGate_c74_pe | kPeGate_c99_log |
                                   kPeGate_c161_pe | kPeGate_c376_pe | kPeGate_c345b_pe;
constexpr uint32_t kPeGatesAfter = kPeGate_c408_pe | kPeGate_c44_pe | kPeGate_c67_pe | kPeGate_c158_pe | kPeGate_c493_pe;

// Card 573 b463: the five FName ids, until all resolve (each hook's own fname_cached retried the same way).
void pe_names_resolve() {
    PeNames& n = g_pe.names;
    const bool ok = fname_cached(n.exit, L"Exit") != 0 && fname_cached(n.player_exit, L"PlayerExit") != 0 &&
                    fname_cached(n.fed_score_gained, L"FedScoreGained") != 0 &&
                    fname_cached(n.receive_begin_play, L"ReceiveBeginPlay") != 0 &&
                    fname_cached(n.construct, L"Construct") != 0;
    g_pe.names_ready = ok ? 1u : 0u;
}

// The notes and refusals that need no FName, in their order. True: a refusal skipped the call. Each gate is loaded
// again at its own test, so a sub-hook that changes a later gate is seen as before.
bool c128_pe_early(void* object, void* function) {
    if (pe_gate(kPeGate_c30_window)) {
        c30_note_event(object, function);
    }
    if (pe_gate(kPeGate_c32_pe)) {
        c32_note_pe(object, function);
    }
    if (pe_gate(kPeGate_c74_pe)) {
        c74_note_pe(object, function);
    }
    if (pe_gate(kPeGate_c99_log)) {
        c582_building_pe(object, function); // card 582 G3 b450: per-hit building damage row
    }
    if (pe_gate(kPeGate_c161_pe) && c161_pe(object, function)) {
        return true; // card 161: the client's own door trigger, refused (the host owns the door)
    }
    if (pe_gate(kPeGate_c376_pe) && c376_pe(object, function)) {
        return true; // card 376: the client's own RESTART MISSION, refused (the host restarts the mission)
    }
    if (pe_gate(kPeGate_c345b_pe) && c345b_pe(object, function)) {
        return true; // card 345b: the client's own level-end Next, held until the host's Next
    }
    return false;
}

// The notes and refusals before the original, then the original; false when a refusal skipped it. `orig` (timed
// calls only): the original's time stamp counter span, taken out of the hook's own cost. `name`: the function's FName,
// read once here for every hook that compares it (c600 or c491 reads it on every call, so it is no new read).
template <bool kTimed>
bool c128_pe_run(void* object, void* function, void* parms, C572PeOrig* orig, uint64_t& name) {
    const C128PeScope scope(object, function);
    if ((pe_gates() & kPeGatesEarly) != 0 && c128_pe_early(object, function)) {
        return false;
    }
    if (g_pe.names_ready == 0) {
        pe_names_resolve();
    }
    name = read_u64(function, kObjectName);
    if (pe_gate(kPeGate_c408_pe) && c408_pe(object, function, parms, name)) {
        return false; // card 408: the client's own flak or plasma bug building timer event (the host's row calls it)
    }
    const bool client = pe_gate(kPeGate_client);
    if (!client && c600_exit_pe(object, function, parms, name)) {
        return false; // card 600: a spawner's player exit while another player is still inside
    }
    if (client && c491_pe(object, function, name)) {
        return false; // card 491 b262: a player pawn's own resupply step (the host's "U" row runs it)
    }
    if constexpr (kTimed) {
        orig->start = __rdtsc();
        g_orig_process_event(object, function, parms);
        orig->ticks = __rdtsc() - orig->start;
    } else {
        g_orig_process_event(object, function, parms);
    }
    return true;
}

// The notes after a run original. One load skips them all while every gate is closed; each open one is loaded again
// at its own test (the original or an earlier note may change it).
void c128_pe_after(void* object, void* function, uint64_t name) {
    if ((pe_gates() & kPeGatesAfter) == 0) {
        return;
    }
    if (pe_gate(kPeGate_c408_pe)) {
        c408_after_pe(object, function);
    }
    if (pe_gate(kPeGate_c44_pe)) {
        c44_after_pe(object, function, name);
    }
    if (pe_gate(kPeGate_c67_pe)) {
        c67_note_pe(object, function);
    }
    if (pe_gate(kPeGate_c158_pe)) {
        c158_after_pe(object, function);
    }
    if (pe_gate(kPeGate_c493_pe)) {
        c493_after_pe(object, function); // card 493: cave-in rerun per player
    }
}

// Card 572 step 2 (c397b398a: ProcessEvent 36.7 ms of 215.4 mod ms per window, 4 __rdtsc per call): only 1 call in
// kC572PeEvery is timed, and its cost stands for that many calls. A race on the counter only moves which call is
// sampled (ProcessEvent runs on several threads).
constexpr uint32_t kC572PeEvery = 16; // a power of two
// Card 573 b463: one counter per thread, so the threads that run ProcessEvent no longer bounce one cache line. Only
// this detour reads it.
thread_local uint32_t t_c572_pe_tick = 0;

} // namespace

// Hot path: every ProcessEvent. The mod's own time in a sampled call (all but the original) goes to the per-frame
// ProcessEvent cost row (frame_c330.cpp, c330_pe).
void detour_process_event(void* object, void* function, void* parms) {
    uint64_t name = 0;
    if ((++t_c572_pe_tick & (kC572PeEvery - 1u)) != 0) {
        if (c128_pe_run<false>(object, function, parms, nullptr, name)) {
            c128_pe_after(object, function, name);
        }
        return;
    }
    const uint64_t t0 = __rdtsc();
    C572PeOrig orig{0, 0};
    if (c128_pe_run<true>(object, function, parms, &orig, name)) {
        c128_pe_after(object, function, name);
    }
    const uint64_t t1 = __rdtsc();
    const uint64_t body = t1 - t0 - orig.ticks;
    c330_pe(body, kC572PeEvery);
    // Card 572 step 3: where a sampled body's ticks go. A refused call has no original: all of it counts as before.
    const uint64_t before = orig.start != 0 ? orig.start - t0 : body;
    c572_pe_split(before, body - before, body);
}

void c30_pawn_line(const char* what, const void* pawn) {
    void* component = nullptr;
    const float health = c21_health(static_cast<const uint8_t*>(pawn), &component);
    log::line("card030: %s %s %s (%s, role %u/%u): Health %.1f, TimeUntilDeathCompletion %.2f, caller exe+0x%llX; "
              "%s, last mod notify %lld ms ago",
              side(), what, object_name(pawn).text, kSprintSideText[sprint_side(pawn)],
              static_cast<const uint8_t*>(pawn)[kActorLocalRole], static_cast<const uint8_t*>(pawn)[kActorRemoteRole],
              static_cast<double>(health), static_cast<double>(read_f32(pawn, kPawnDeathTimer)),
              exe_rva(_ReturnAddress()), g_c21_in_notify ? "inside mod notify" : "not inside mod notify",
              g_c21_notify_ms != 0 ? static_cast<long long>(GetTickCount64() - g_c21_notify_ms) : -1ll);
}

void detour_player_death_event(void* pawn, const void* damage_event) {
    pl_count(PlCount::Death); // card 155
    if (g_c30.armed) {
        c30_open("HandleDeathEvent", pawn);
        g_c30.dead = object_name(pawn);
        c30_pawn_line("HandleDeathEvent enter", pawn);
    }
    if (!c43_death(pawn)) {
        g_orig_player_death_event(pawn, damage_event);
    }
    if (g_c30.armed) {
        c30_pawn_line("HandleDeathEvent return", pawn);
    }
}

void detour_start_death(void* pawn, const void* damage_event) {
    if (g_c30.armed) {
        c30_pawn_line("StartDeath (ASTPlayerCharacter) enter", pawn);
    }
    c46_note_start_death(pawn);
    if (!c43_skip_start_death(pawn)) {
        g_orig_start_death(pawn, damage_event);
    }
}

void detour_start_death_mi(void* pawn, const void* damage_event) {
    if (g_c30.armed) {
        c30_pawn_line("StartDeath (ASTPlayerCharacterMI) enter", pawn);
    }
    c46_note_start_death(pawn);
    if (!c43_skip_start_death(pawn)) {
        g_orig_start_death_mi(pawn, damage_event);
    }
}

// OnDeath's bound delegates: object and function name of each.
void c30_log_on_death(const void* pawn) {
    if (g_c30.on_death_offset != static_cast<int32_t>(kPawnOnDeath)) {
        log::line("WARN card030: %s OnDeath not dumped: reflected offset 0x%X, expected 0x%zX", side(),
                  g_c30.on_death_offset, kPawnOnDeath);
        return;
    }
    const auto* const data = static_cast<const uint8_t*>(read_ptr(pawn, kPawnOnDeath));
    const int32_t num = read_i32(pawn, kPawnOnDeath + kArrayNum);
    log::line("card030: %s OnDeath on %s: %d bound", side(), object_name(pawn).text, num);
    for (int32_t i = 0; data != nullptr && i < num && i < static_cast<int32_t>(kC30Delegates); ++i) {
        const uint8_t* const d = data + static_cast<size_t>(i) * kScriptDelegateBytes;
        const void* const target = g_weak_get(d);
        log::line("card030: %s OnDeath[%d]: %s (%s).%s", side(), i, object_name(target).text, class_name(target).text,
                  fname_text(d + kScriptDelegateName).text);
    }
}

void detour_complete_death(void* pawn) {
    if (c294_skip_complete_death(pawn, exe_rva(_ReturnAddress()))) {
        return; // card 294
    }
    c44_complete_death(pawn);
    c48_complete_death(pawn);
    if (g_c30.armed) {
        c30_pawn_line("CompleteDeath enter", pawn);
        c30_log_on_death(pawn);
        g_c30.complete_ms = g_c30.complete_ms != 0 ? g_c30.complete_ms : GetTickCount64();
    }
    g_orig_complete_death(pawn);
    if (g_c30.armed) {
        log::line("card030: %s CompleteDeath return", side());
    }
}

void detour_set_auto_save(void* tracker, bool enabled) {
    if (g_c30.armed) {
        log::line("card030: %s USTDebugSaveTracker::SetAutoSaveEnabled(%d) on %s, caller exe+0x%llX; last dead pawn "
                  "%s, %llu ms after Health 0",
                  side(), enabled ? 1 : 0, object_name(tracker).text, exe_rva(_ReturnAddress()), g_c30.dead.text,
                  g_c30.zero_ms != 0 ? GetTickCount64() - g_c30.zero_ms : 0ull);
    }
    g_orig_set_auto_save(tracker, enabled);
}

// Card 132 step 3a, host with UBWCOOP_GOD: a player pawn's Health loss is put back to the old value before the game
// sees the change (no HUD drop, no death event from it). The put-back change is a gain, so it passes. True = skip the
// original. Logged for the first kC132GodLines blocks, then counted.
constexpr uint32_t kC132GodLines = 40;
static uint32_t g_c132_god_blocks = 0;
static bool c132_god_block(const void* owner, const void* component, const void* change_data) {
    if (!g_c132_god || g_client_session || g_c43.in_reset || g_c43.in_kill ||
        !is_a(owner, g_player_character_class())) { // card 196: c43_kill_host's own hit lands
        return false;
    }
    if (g_c132_god_host_only && sprint_side(owner) != kSideHostOwn) {
        return false; // UBWCOOP_GOD_HOST_ONLY: the client's pawn takes its damage (card 132 hb53 note 3)
    }
    const float old_health = read_f32(change_data, kAttributeChangeOld);
    const float new_health = read_f32(change_data, kAttributeChangeNew);
    if (new_health >= old_health) {
        return false;
    }
    ++g_c132_god_blocks;
    if (g_c132_god_blocks <= kC132GodLines) {
        log::line("card132: god %s (%s) damage %.1f blocked, Health %.1f -> %.1f put back (%u blocked)",
                  object_name(owner).text, kSprintSideText[sprint_side(owner)],
                  static_cast<double>(old_health - new_health), static_cast<double>(new_health),
                  static_cast<double>(old_health), g_c132_god_blocks);
    }
    c43_set_component_health(component, old_health, owner);
    return true;
}

// Card 400, host: a remote player's pawn hidden until its client confirms (c400_shielded) loses no Health: the loss is
// put back the card 132 way. Logged for the first kC400BlockLines blocks, then counted.
constexpr uint32_t kC400BlockLines = 8;
static uint32_t g_c400_blocks = 0;
static bool c400_damage_block(const void* owner, const void* component, const void* change_data) {
    if (g_client_session || g_c43.in_reset || !c400_shielded(owner)) {
        return false;
    }
    const float old_health = read_f32(change_data, kAttributeChangeOld);
    const float new_health = read_f32(change_data, kAttributeChangeNew);
    if (new_health >= old_health) {
        return false;
    }
    ++g_c400_blocks;
    if (g_c400_blocks <= kC400BlockLines) {
        log::line("card400: host t=%llu %s damage %.1f blocked (client not confirmed), Health %.1f put back (%u "
                  "blocked)", static_cast<unsigned long long>(GetTickCount64()), object_name(owner).text,
                  static_cast<double>(old_health - new_health), static_cast<double>(old_health), g_c400_blocks);
    }
    c43_set_component_health(component, old_health, owner);
    return true;
}

// Card 132 (hb45 review): every placed BP_WarriorBug_Corpses_Destructible on C2M1 goes Health 400 -> 0 on the host
// in the same ms as the host pawn's possess. Each side logs the first kDeathCallerLines non-player deaths per process
// with the caller of the Health change, so the source (vanilla init or this mod) shows in a solo load too.
constexpr uint32_t kDeathCallerLines = 12;
uint32_t g_death_caller_lines = 0;

void c132_log_death_caller(const void* owner, const void* change_data, unsigned long long caller) {
    const float old_health = read_f32(change_data, kAttributeChangeOld);
    const float new_health = read_f32(change_data, kAttributeChangeNew);
    if (old_health <= 0.0f || new_health > 0.0f || is_a(owner, g_player_character_class()) ||
        !note_event_cap(g_death_caller_lines, kDeathCallerLines, "card132 death callers")) {
        return;
    }
    char chain[256];
    caller_chain(chain, sizeof(chain), 10); // frames above this helper: the detour (not exe), then the game's chain
    log::line("card132: %s t=%llu %s %s Health %.1f -> %.1f, caller exe+0x%llX, net mode %u, chain %s", side(),
              static_cast<unsigned long long>(GetTickCount64()), class_name(owner).text, object_name(owner).text,
              static_cast<double>(old_health), static_cast<double>(new_health), caller, net_mode(owner), chain);
}

// Card 132 hb53 note 3, with UBWCOOP_GOD_HOST_ONLY=1: every Health change of the client's pawn, on the host (its server
// copy) and on the client (its own pawn), with the caller chain, so the log shows one damage source (the host's,
// replicated) and no second, local one on the client. Capped at kC132ClientHealthLines per side.
constexpr uint32_t kC132ClientHealthLines = 80;
static uint32_t g_c132_client_health_lines = 0;
static void c132_log_client_health(const void* owner, const void* change_data) {
    const uint32_t s = sprint_side(owner);
    if (!g_c132_god_host_only || (s != kSideServerCopy && s != kSideClientOwn) ||
        !note_event_cap(g_c132_client_health_lines, kC132ClientHealthLines, "card132 client pawn health lines")) {
        return;
    }
    char chain[200];
    caller_chain(chain, sizeof(chain), 6);
    log::line("card132: %s t=%llu client pawn health %s (%s) %.1f -> %.1f at (%.0f, %.0f, %.0f)%s, chain %s", side(),
              static_cast<unsigned long long>(GetTickCount64()), object_name(owner).text, kSprintSideText[s],
              static_cast<double>(read_f32(change_data, kAttributeChangeOld)),
              static_cast<double>(read_f32(change_data, kAttributeChangeNew)), actor_location(owner).x,
              actor_location(owner).y, actor_location(owner).z, g_c21_in_notify ? " [mod notify]" : "", chain);
}

// Player-pawn Health changes: old -> new, caller (0 = inside this mod, card 021's client notify).
void detour_health_changed(void* component, const void* change_data) {
    const void* const owner = g_c30.armed ? read_ptr(component, kComponentOwner) : nullptr;
    if (owner != nullptr && is_a(owner, g_player_character_class())) {
        const float now_health = read_f32(change_data, kAttributeChangeNew);
        const float old_health = read_f32(change_data, kAttributeChangeOld);
        if (now_health <= 0.0f && sprint_side(owner) == c30_target_side()) {
            c30_open("HealthAttributeChanged", owner);
        }
        if (note_event_cap(g_c30.health_lines, kC30HealthLines, "card030 health change")) {
            log::line("card030: %s Health %.1f -> %.1f on %s (%s), caller exe+0x%llX%s", side(),
                      static_cast<double>(old_health), static_cast<double>(now_health), object_name(owner).text,
                      kSprintSideText[sprint_side(owner)], exe_rva(_ReturnAddress()),
                      g_c21_in_notify ? " [mod notify]" : " [game]");
        }
    }
    const void* const pawn43 = read_ptr(component, kComponentOwner);
    if (pawn43 != nullptr) {
        c132_log_death_caller(pawn43, change_data, exe_rva(_ReturnAddress()));
        if (is_a(pawn43, g_player_character_class())) {
            c132_log_client_health(pawn43, change_data);
        }
    }
    if (pawn43 != nullptr &&
        (c400_damage_block(pawn43, component, change_data) || c132_god_block(pawn43, component, change_data))) {
        return;
    }
    const bool bleeding = pawn43 != nullptr && c43_health_pre(pawn43);
    g_orig_health_changed(component, change_data);
    if (pawn43 != nullptr && g_client_session && read_f32(change_data, kAttributeChangeOld) > 0.0f &&
        read_f32(change_data, kAttributeChangeNew) <= 0.0f) {
        c137_client_change(pawn43, "alive", 1, 0, exe_rva(_ReturnAddress())); // card 137 2a
    }
    if (pawn43 != nullptr && !g_client_session) { // card 198 read-back: the game's own change after a Restore apply
        c198_health_change(pawn43, read_f32(change_data, kAttributeChangeOld), read_f32(change_data, kAttributeChangeNew));
    }
    if (pawn43 != nullptr && !g_client_session) { // card 130 item 12: placed killable buildings
        c98_host_kill(pawn43, read_f32(change_data, kAttributeChangeOld), read_f32(change_data, kAttributeChangeNew));
    }
    if (bleeding) {
        c43_health_post(pawn43, read_f32(change_data, kAttributeChangeOld), read_f32(change_data, kAttributeChangeNew));
    }
}

constexpr float kC43DamagePerS = 10.0f;
constexpr ULONGLONG kC43HoldDownedMs = 3000;
constexpr ULONGLONG kC43HoldOutMs = 6000;
constexpr double kC43ReviveRange = 200.0;

// Card 209: a Down frame is {pawn GUID low, GUID high, state, whole seconds, host slot}; no self flag on the wire.
constexpr uint32_t kC43DownLines = 40;
constexpr uint32_t kC294DownLines = 40; // card 294: Down priority-send rows per map
constexpr double kC43CameraDrop = 60.0;

constexpr uint32_t kC43Lines = 200;
constexpr uint32_t kC43SecLines = 120;

constexpr uint32_t kC43KillTries = 5;
constexpr ULONGLONG kC43KillGapMs = 1000;

constexpr size_t kGameInstanceSaveTracker = 0x1D0;  // USTGameInstance's USTDebugSaveTracker
constexpr uint8_t kTeleportNone = 0;                // ETeleportType::None

static_assert(sizeof(kC43LifeText) / sizeof(kC43LifeText[0]) == static_cast<size_t>(C43Life::Count));

C43State g_c43{};

CancelAllAbilitiesFn g_cancel_all_abilities = nullptr;
IsAutoSaveEnabledFn g_is_auto_save_enabled = nullptr;
AddLocalOffsetFn g_add_local_offset = nullptr;
InteractFn g_orig_release_interaction = nullptr;

bool c43_line() {
    return note_event_cap(g_c43.lines, kC43Lines, "card043");
}

void c43_remove_hold(int32_t h) {
    g_c43.hold[h] = g_c43.hold[--g_c43.holds];
}

// Players whose pawn is not Downed, Out or Dead, `except` (an id) left out.
uint32_t c43_up_count(const C43Roster& r, int32_t except) {
    uint32_t up = 0;
    for (int32_t s = 0; s < kC43Max; ++s) {
        const int32_t i = r.p[s].pawn != nullptr ? c43_find(r.p[s].id) : -1;
        const bool down = i >= 0 && c43_down_state(g_c43.e[i].state);
        up += r.p[s].pawn != nullptr && r.p[s].id != except && !down ? 1u : 0u;
    }
    return up;
}

float c43_walk(const void* pawn) {
    const void* const cmc = read_ptr(pawn, kCharacterMovement);
    return cmc != nullptr ? read_f32(cmc, kCmcMaxWalkSpeed) : -1.0f;
}

void c43_set_walk(const void* pawn, float value) {
    auto* const cmc = static_cast<uint8_t*>(read_ptr(pawn, kCharacterMovement));
    if (cmc != nullptr && value >= 0.0f) {
        memcpy(cmc + kCmcMaxWalkSpeed, &value, sizeof(value));
    }
}

// --- Card 295: a revive's walk speed comes from the game's own movement attribute ---
// Playtest 2026-11-13: each machine saved its own MaxWalkSpeed at its own down edge. The host's copy saved 875 for
// all four client downs; the client saved 875, 875, 1524 and 1524 (sprinting when downed: GE_Sprint's speed), so
// after the last two revives the client predicted at 1524 while the host simulated at 875. The game's speed is
// USTAttributeSet_Movement::WalkSpeed: PostAttributeChange (0x051D7BB0) writes its new value to MaxWalkSpeed, and the
// host's CancelAllAbilities at the down ends GE_Sprint, so after a revive the attribute holds the pawn's current
// speed on both machines. The saved value is only the fallback (no movement set, or card 019's zero attribute).
namespace {
constexpr size_t kAttrCurrentValue = 0xC;       // FGameplayAttributeData: vtable, BaseValue +0x8, CurrentValue +0xC
constexpr ULONGLONG kC295WindowMs = 10000;      // corrections counted after a client revive
struct C295 {
    ULONGLONG until_ms;   // client: end of the correction window (0 = none)
    uint32_t host_walk;   // client: the host's speed from the own Up frame (0 = none)
    uint32_t corrections; // client: ClientAdjustPosition calls in the window
    double worst;         // client: largest correction distance in the window (uu)
};
C295 g_c295{};

float c295_game_walk(const void* pawn) {
    const void* const asc = c21_character_ptr(pawn, kCharacterAsc);
    const void* const set = asc != nullptr ? find_movement_set(asc) : nullptr;
    return set != nullptr ? read_f32(set, static_cast<size_t>(kWalkSpeedOffset) + kAttrCurrentValue) : -1.0f;
}
} // namespace

// Both sides, at a revive or restart of a downed pawn: MaxWalkSpeed from, in order, the host's value (client: the own
// Up frame's v3; the host simulates the pawn the client is corrected against, and the client's attribute copy is filled
// by the mod, cards 018 to 020), the WalkSpeed attribute, `saved`. Only a positive value is written; with none, the
// speed is left as it is and the row says "no value". Returns the speed written, or -1.
float c295_restore_walk(void* pawn, float saved, float host, const char* why) {
    const float game = c295_game_walk(pawn);
    const char* from = "no value";
    float walk = -1.0f;
    if (host > 0.0f) {
        walk = host;
        from = "the host's Up frame";
    } else if (game > 0.0f) {
        walk = game;
        from = "the WalkSpeed attribute";
    } else if (saved > 0.0f) {
        walk = saved;
        from = "the saved value";
    }
    if (walk > 0.0f) {
        c43_set_walk(pawn, walk);
    }
    const auto* const cmc = static_cast<const uint8_t*>(read_ptr(pawn, kCharacterMovement));
    log::line("card295: %s t=%llu %s %s: walk speed %.0f from %s (host %.0f, saved %.0f, attribute %.0f), movement "
              "mode %u", side(), static_cast<unsigned long long>(GetTickCount64()), why, object_name(pawn).text,
              static_cast<double>(walk), from, static_cast<double>(host), static_cast<double>(saved),
              static_cast<double>(game), cmc != nullptr ? cmc[kCmcMovementMode] : 0xFFu);
    return walk;
}

// Host: the walk speed an Up frame carries (v3), rounded; 0 when the pawn has no movement component.
uint32_t c295_wire_walk(const void* pawn) {
    const float walk = pawn != nullptr ? c43_walk(pawn) : -1.0f;
    return walk > 0.0f ? static_cast<uint32_t>(lroundf(walk)) : 0u;
}

// Client, at its own Up frame: the host's speed for the own pawn, compared at the own view's revive.
void c295_host_walk(uint32_t walk) {
    g_c295.host_walk = walk;
}

// Client, at its own revive: the host's speed from the own Up frame (0 = none) for c295_restore_walk.
float c295_host_value() {
    return static_cast<float>(g_c295.host_walk);
}

// Client, after its own revive set the speed: the client's WalkSpeed attribute compared with the host's value (a
// diagnostic: the host's value is the one set), and the correction window opened.
void c295_client_up(float set, ULONGLONG now, const void* pawn) {
    const float attribute = c295_game_walk(pawn);
    if (g_c295.host_walk != 0 && fabsf(attribute - static_cast<float>(g_c295.host_walk)) > 1.0f) {
        log::line("WARN card295: client WalkSpeed attribute %.0f differs from the host row %u (speed set %.0f)",
                  static_cast<double>(attribute), g_c295.host_walk, static_cast<double>(set));
    }
    g_c295.host_walk = 0;
    g_c295.until_ms = now + kC295WindowMs;
    g_c295.corrections = 0;
    g_c295.worst = 0.0;
}

// Client, at ClientAdjustPosition on the own pawn's CMC: counted inside the window.
void c295_note_adjust(const void* cmc, const double* new_loc) {
    if (g_c295.until_ms == 0 || new_loc == nullptr) {
        return;
    }
    ++g_c295.corrections;
    const void* const owner = read_ptr(cmc, kCmcCharacterOwner);
    const void* const root = owner != nullptr ? read_ptr(owner, kActorRootComponent) : nullptr;
    if (root == nullptr) {
        return;
    }
    const Vec3 server = read_vec3(new_loc, 0);
    const Vec3 here = read_vec3(root, kComponentTranslation);
    const double dx = server.x - here.x;
    const double dy = server.y - here.y;
    const double dz = server.z - here.z;
    const double d = sqrt(dx * dx + dy * dy + dz * dz);
    g_c295.worst = d > g_c295.worst ? d : g_c295.worst;
}

// Client, every tick: the window's row when it ends.
void c295_step(ULONGLONG now) {
    if (g_c295.until_ms == 0 || now < g_c295.until_ms) {
        return;
    }
    g_c295.until_ms = 0;
    log::line("card295: client corrections in the 10 s after the revive: %u, worst %.0f uu", g_c295.corrections,
              g_c295.worst);
}

// USTDebugSaveTracker::IsAutoSaveEnabled: 1, 0, or -1 when there is no tracker.
int c43_autosave(const void* context) {
    void* const gi = g_get_st_game_instance(context);
    const void* const tracker = gi != nullptr ? read_ptr(gi, kGameInstanceSaveTracker) : nullptr;
    if (tracker == nullptr) {
        return -1;
    }
    return g_is_auto_save_enabled(tracker) ? 1 : 0;
}

// Both sides: this machine's held own fire is released when its pawn goes down.
void c43_release_own_fire(const void* pawn) {
    for (int32_t kind = 0; kind < kFireKinds; ++kind) {
        C26Own& own = g_c24.own[kind];
        if (own.held && own.pawn_id == object_index(pawn)) {
            own.held = false;
            c26_send_fire(pawn, read_ptr(pawn, kPawnController), kind, false, "downed");
        }
    }
}

// Host: entry e's Down frame args (state, whole seconds of bleedout left). Reads e only: the bleedout change test
// (shown_secs) stays with c43_broadcast. False, with a capped WARN, when the pawn's GUID is not sendable.
bool c43_down_args(const C43Roster& r, const C43Entry& e, ChanArgs& args) {
    const int32_t shown = static_cast<int32_t>(e.secs_left > 0.0f ? ceilf(e.secs_left) : 0.0f);
    const int32_t slot = c43_slot(r, e.pawn_id);
    const uint64_t guid = slot >= 0 ? c41_guid(r.p[slot].pawn) : 0;
    uint32_t lo = 0;
    uint32_t hi = 0;
    if (!c41_guid_args(guid, lo, hi)) {
        if (note_event_cap(g_c43.down_warns, kC43DownLines, "card209 Down")) {
            log::line("WARN card209: host Down %s for pawn id %d (slot %d) not sent: guid 0x%llx not sendable",
                      kC43LifeText[static_cast<size_t>(e.state)], e.pawn_id, slot,
                      static_cast<unsigned long long>(guid));
        }
        return false;
    }
    // Card 295: an Up frame has no bleedout seconds; v3 carries the host's walk speed for the pawn instead.
    const uint32_t v3 = e.state == C43Life::Up && slot >= 0 ? c295_wire_walk(r.p[slot].pawn)
                                                            : static_cast<uint32_t>(shown);
    args = ChanArgs{{lo, hi, static_cast<uint32_t>(e.state), v3, e.slot}};
    return true;
}

// Host: one Down frame per remote PC for entry e.
void c43_broadcast(const C43Roster& r, C43Entry& e) {
    e.shown_secs = static_cast<int32_t>(e.secs_left > 0.0f ? ceilf(e.secs_left) : 0.0f);
    ChanArgs args{};
    if (!c43_down_args(r, e, args)) {
        return;
    }
    // Card 294: Down is a priority op (always): a life state change skips the pace queue.
    uint32_t sent = 0;
    for (int32_t s = 0; s < kC43Max; ++s) {
        uint32_t ahead = 0;
        if (!ue::chan::chan_send_client_now(r.p[s].pc, chan_frame(ChanOp::Down, args), &ahead)) {
            continue;
        }
        ++sent;
        if (e.prio_state != static_cast<int32_t>(e.state) &&
            note_event_cap(g_c43.prio_lines, kC294DownLines, "card294 Down")) {
            log::line("card294: host t=%llu Down %s for pawn id %d (slot %u) sent now, ahead of %u queued frames",
                      static_cast<unsigned long long>(GetTickCount64()), kC43LifeText[static_cast<size_t>(e.state)],
                      e.pawn_id, e.slot, ahead);
        }
    }
    e.prio_state = sent != 0 ? static_cast<int32_t>(e.state) : e.prio_state;
    g_c43.n.down_sent += sent;
}

// Host, the registry's "player" join replay (card 323 gap 1): every live life-state entry to the PC past its first
// Pong, so a joiner does not see a downed or out player standing. The client's c43_receive_down takes a repeated state
// as no change, so a PC that already had the frame logs nothing new.
void c43_join_replay(void* pc) {
    void* const world = g_client_session || g_c43.count == 0 ? nullptr : g_actor_get_world(pc);
    if (world == nullptr) {
        return;
    }
    const C43Roster r = c43_roster(world);
    uint32_t sent = 0;
    for (int32_t i = 0; i < g_c43.count; ++i) {
        ChanArgs args{};
        uint32_t ahead = 0;
        const bool ok = c43_down_args(r, g_c43.e[i], args) &&
                        ue::chan::chan_send_client_now(pc, chan_frame(ChanOp::Down, args), &ahead);
        sent += ok ? 1u : 0u;
    }
    g_c43.n.down_sent += sent;
    log::line("card323: host t=%llu Down join replay to %s: %u of %d entries sent",
              static_cast<unsigned long long>(GetTickCount64()), object_name(pc).text, sent, g_c43.count);
}

void c43_gate_line(const void* pawn, uint32_t players) {
    if (!g_c43.gate_logged) {
        g_c43.gate_logged = true;
        log::line("card043: host gate off for %s (net mode %u, players %u, all down %d): vanilla death",
                  object_name(pawn).text, net_mode(pawn), players, g_c43.all_down ? 1 : 0);
    }
}

// HandleDeathEvent detour body. True = skip the original.
bool c43_death(void* pawn) {
    if (is_a(pawn, g_mech_class())) {
        c79_mech_death(pawn); // card 079: the mech's own destruction runs
        return false;
    }
    if (g_c132_god && is_a(pawn, g_player_character_class()) &&
        (!g_client_session || sprint_side(pawn) == kSideClientOwn) &&
        !(g_c132_god_host_only && sprint_side(pawn) != kSideHostOwn) && !g_c43.in_kill) {
        void* component = nullptr;
        c21_health(static_cast<const uint8_t*>(pawn), &component);
        const float max_health = c43_max_health(component);
        if (!g_client_session && max_health > 0.0f) {
            c43_set_health(pawn, max_health);
        }
        log::line("card132: god %s HandleDeathEvent on %s (%s) skipped%s", side(), object_name(pawn).text,
                  kSprintSideText[sprint_side(pawn)], g_client_session ? "" : ", Health back to max");
        return true;
    }
    if (g_client_session) {
        const bool skip = g_c43.client_seen && sprint_side(pawn) == kSideClientOwn;
        if (skip) {
            log::line("WARN card043: client HandleDeathEvent on its own pawn %s skipped", object_name(pawn).text);
        }
        return skip;
    }
    void* const world = g_actor_get_world(pawn);
    c586_end_hold_of(pawn, world, "HandleDeathEvent"); // card 586: the seat or missile hands the player back first
    const C43Roster r = world != nullptr ? c43_roster(world) : C43Roster{};
    const int32_t id = object_index(pawn);
    const int32_t slot = c43_slot(r, id);
    if (net_mode(pawn) != kNetModeListenServer || r.players < 2 || g_c43.all_down || slot < 0) {
        c43_gate_line(pawn, r.players);
        return false;
    }
    const int32_t i = c43_find(id);
    if (i >= 0 && g_c43.e[i].state != C43Life::Up) {
        c43_set_health(pawn, 1.0f);
        if (c43_line()) {
            log::line("card043: host HandleDeathEvent on %s while %s: skipped, Health back to 1",
                      object_name(pawn).text, kC43LifeText[static_cast<size_t>(g_c43.e[i].state)]);
        }
        return true;
    }
    if (i >= 0) {
        c43_remove(i); // a revived player still in its StandUp window
    }
    const ULONGLONG now = GetTickCount64();
    if (c43_up_count(r, id) != 0) {
        c43_enter_down(pawn, r, static_cast<uint32_t>(slot), C43Life::Downed, now);
        return true;
    }
    if (sprint_side(pawn) == kSideHostOwn) {
        c43_all_down(r, pawn, "all down: host death");
        return false;
    }
    c43_enter_down(pawn, r, static_cast<uint32_t>(slot), C43Life::Out, now); // the tick runs the host's death
    return true;
}

// StartDeath detours: true = skip. A server copy never runs it while the gate holds (it would turn the host's
// autosave off); the client's own pawn never runs it once a Down frame has arrived.
bool c43_skip_start_death(const void* pawn) {
    if (g_c43.death_pass) {
        return false; // card 044: the client's own real death (MI StartDeath calls the base directly)
    }
    if (is_a(pawn, g_mech_class())) {
        return false; // card 079: the mech's StartDeath calls the base; its destruction is never skipped
    }
    const uint32_t s = sprint_side(pawn);
    const bool copy = !g_client_session && s == kSideServerCopy && net_mode(pawn) == kNetModeListenServer;
    const bool own_client = g_client_session && s == kSideClientOwn && g_c43.client_seen;
    if (copy || own_client) {
        log::line("WARN card043: %s StartDeath on %s (%s) skipped", side(), object_name(pawn).text,
                  kSprintSideText[s]);
    }
    return copy || own_client;
}

// Health hook, before the original: true when this host pawn is Downed or Out, so the change is clamped.
bool c43_health_pre(const void* owner) {
    if (g_client_session || g_c43.in_reset || g_c43.count == 0) {
        return false;
    }
    const int32_t i = c43_find(object_index(owner));
    return i >= 0 && (g_c43.e[i].state == C43Life::Downed || g_c43.e[i].state == C43Life::Out);
}

// Health hook, after the original: damage drains the bleedout; Health goes back to 1.
void c43_health_post(const void* owner, float old_health, float new_health) {
    const int32_t i = c43_find(object_index(owner));
    if (i < 0) {
        return;
    }
    C43Entry& e = g_c43.e[i];
    const float drain = (old_health - new_health) / kC43DamagePerS;
    if (drain > 0.0f && e.state == C43Life::Downed) {
        e.secs_left -= drain;
        if (c43_line()) {
            log::line("card043: host t=%llu %s took %.1f damage while Downed: bleedout -%.1f s, %.1f s left",
                      static_cast<unsigned long long>(GetTickCount64()), object_name(owner).text,
                      static_cast<double>(old_health - new_health), static_cast<double>(drain),
                      static_cast<double>(e.secs_left));
        }
    }
    if (new_health != 1.0f) {
        c43_set_health(owner, 1.0f);
    }
}

// Host: entry e is revived by a completed hold: Up, Health kC43ReviveFraction of MaxHealth, walk speed back.
void c43_revive(const C43Roster& r, C43Entry& e, void* pawn, ULONGLONG held_ms, ULONGLONG now) {
    void* component = nullptr;
    c21_health(static_cast<const uint8_t*>(pawn), &component);
    const float max_health = c43_max_health(component);
    const C43Life was = e.state;
    e.state = C43Life::Up; // before the Health write, so the health hook leaves it alone
    e.up_ms = now;
    e.secs_left = 0.0f;
    c43_set_health(pawn, max_health > 0.0f ? max_health * kC43ReviveFraction : 1.0f);
    c295_restore_walk(pawn, e.saved_walk, 0.0f, "revive"); // card 295
    char why[64];
    snprintf(why, sizeof(why), "revived from %s after a %llu ms hold", kC43LifeText[static_cast<size_t>(was)],
             static_cast<unsigned long long>(held_ms));
    c43_transition(r, e, pawn, why);
}

// Host, at a respawn in place (RESTART CHECKPOINT), before the per-map reset drops the entries: every down entry
// goes Up with one Down frame per PC, so a client whose own entry was down stands up whatever its own reset does
// (its entry is keyed by side, so a stale one holds the new own pawn at walk 0), and a pawn the restart kept gets
// its saved MaxWalkSpeed back. Card 130 item 10.
void c43_stand_all(void* pc) {
    void* const world = g_actor_get_world(pc);
    if (g_client_session || world == nullptr || g_c43.count == 0) {
        return;
    }
    const C43Roster r = c43_roster(world);
    const ULONGLONG now = GetTickCount64();
    uint32_t stood = 0;
    uint32_t not_sent = 0;
    for (int32_t i = 0; i < g_c43.count; ++i) {
        C43Entry& e = g_c43.e[i];
        if (!c43_down_state(e.state)) {
            continue;
        }
        const int32_t s = c43_slot(r, e.pawn_id);
        if (s >= 0) {
            c295_restore_walk(r.p[s].pawn, e.saved_walk, 0.0f, "restart"); // card 295
        }
        log::line("card043: host t=%llu restart: slot %u %s -> Up (pawn %s, saved walk %.0f)",
                  static_cast<unsigned long long>(now), e.slot, kC43LifeText[static_cast<size_t>(e.state)],
                  s >= 0 ? "kept" : "replaced", static_cast<double>(e.saved_walk));
        e.state = C43Life::Up;
        e.up_ms = now;
        e.secs_left = 0.0f;
        ++stood;
        // Card 209: a replaced pawn's old id has no roster slot (guid 0); the client resets its own table at the
        // same load, so the frame is not needed. Only a kept pawn gets its Up frame.
        if (s >= 0) {
            c43_broadcast(r, e);
        } else {
            ++not_sent;
        }
    }
    log::line("card043: host restart: %u down entries stood up, Down sent %u; %u replaced pawns not sent (the "
              "client resets its own table at this load)", stood, g_c43.n.down_sent, not_sent);
}

// Host: one hold. False = the hold is over (dropped or completed) and is removed by the caller.
bool c43_hold_step(const C43Roster& r, const C43Hold& h, ULONGLONG now) {
    const int32_t rs = c43_slot(r, h.reviver_id);
    const int32_t ts = c43_slot(r, h.target_id);
    const int32_t ti = c43_find(h.target_id);
    const int32_t ri = c43_find(h.reviver_id);
    const bool reviver_up = rs >= 0 && (ri < 0 || !c43_down_state(g_c43.e[ri].state));
    const bool target_down = ts >= 0 && ti >= 0 && (g_c43.e[ti].state == C43Life::Downed ||
                                                    g_c43.e[ti].state == C43Life::Out);
    const double dist = rs >= 0 && ts >= 0 ? vec_dist(actor_location(r.p[rs].pawn), actor_location(r.p[ts].pawn)) : 0.0;
    if (!reviver_up || !target_down || g_c43.all_down || dist > kC43ReviveRange) {
        log::line("card043: host hold dropped after %llu ms: reviver up %d, target down %d, all down %d, distance %.0f",
                  static_cast<unsigned long long>(now - h.start_ms), reviver_up ? 1 : 0, target_down ? 1 : 0,
                  g_c43.all_down ? 1 : 0, dist);
        return false;
    }
    const ULONGLONG need = g_c43.e[ti].state == C43Life::Downed ? kC43HoldDownedMs : kC43HoldOutMs;
    if (now - h.start_ms < need) {
        return true;
    }
    c43_revive(r, g_c43.e[ti], r.p[ts].pawn, now - h.start_ms, now);
    return false;
}

// Host: one Downed entry's bleedout over dt seconds; at 0 it is Out.
void c43_bleed(const C43Roster& r, C43Entry& e, void* pawn, float dt) {
    e.secs_left -= dt;
    if (e.secs_left <= 0.0f) {
        e.state = C43Life::Out;
        e.secs_left = 0.0f;
        c43_transition(r, e, pawn, "bleedout over");
        return;
    }
    if (static_cast<int32_t>(ceilf(e.secs_left)) != e.shown_secs) {
        c43_broadcast(r, e);
        if (note_event_cap(g_c43.sec_lines, kC43SecLines, "card043 bleedout")) {
            log::line("card043: host t=%llu bleedout %s: %d s left", static_cast<unsigned long long>(GetTickCount64()),
                      object_name(pawn).text, e.shown_secs);
        }
    }
}

// Host: per entry, a gone pawn leaves, a StandUp window ends, walk speed stays 0, Downed bleeds.
void c43_entries_step(const C43Roster& r, float dt, ULONGLONG now) {
    for (int32_t i = g_c43.count - 1; i >= 0; --i) {
        C43Entry& e = g_c43.e[i];
        const int32_t s = c43_slot(r, e.pawn_id);
        if (s < 0 || (e.state == C43Life::Up && now - e.up_ms >= kC43StandUpMs)) {
            if (s < 0) {
                log::line("card043: host entry %d (%s) left: pawn gone", e.pawn_id,
                          kC43LifeText[static_cast<size_t>(e.state)]);
            }
            c43_remove(i);
            continue;
        }
        if (e.state == C43Life::Downed || e.state == C43Life::Out) {
            c43_set_walk(r.p[s].pawn, 0.0f);
        }
        if (e.state == C43Life::Downed) {
            c43_bleed(r, e, r.p[s].pawn, dt);
        }
    }
}

namespace {
// Card 211, host, inside c43_kill_host's in_kill window: the kill hit with `bug` as its causer; when that leaves
// Health unchanged (a bug pawn hit by a bug NPC: the game zeroes same-side damage, card 211 "Senior trace"), the
// same damage again at once with the host as its own causer. Returns which form landed (kC211Landed index).
enum C211Landed : uint8_t { kC211SelfOnly, kC211Causer, kC211SelfAfterCauser, kC211Neither };
constexpr const char* kC211LandedText[] = {
    "the self-caused hit (no causer found)",
    "the causer's hit",
    "the self-caused hit (the causer's hit left Health unchanged)",
    "none (neither the causer's hit nor the self-caused hit changed Health)",
};
C211Landed c211_kill_hit(void* host, void* bug, void* component, float damage) {
    if (bug == nullptr) {
        g_deal_damage(host, host, damage, true);
        return kC211SelfOnly;
    }
    const float before = c21_health(static_cast<const uint8_t*>(host), &component);
    g_deal_damage(bug, host, damage, false);
    if (c21_health(static_cast<const uint8_t*>(host), &component) != before) {
        return kC211Causer;
    }
    g_deal_damage(host, host, damage, true);
    return c21_health(static_cast<const uint8_t*>(host), &component) != before ? kC211SelfAfterCauser : kC211Neither;
}
} // namespace

// Host: the real death of the host's own pawn once nobody is Up, retried kC43KillTries times kC43KillGapMs apart.
void c43_kill_host(void* world, const C43Roster& r, ULONGLONG now) {
    void* const host = r.p[0].pawn;
    if (host != nullptr && is_a(host, g_mech_class())) { // card 081: no hit on a piloted mech, no try counted
        g_c43.kill_ms = now + kC43KillGapMs;
        ++g_c81.mech_skips;
        if (note_event_cap(g_c81.mech_lines, kC81Lines, "card081 mech")) {
            log::line("card081: host t=%llu real-death hit skipped: player 0 pilots %s; %u hits so far, retry in %llu "
                      "ms (skip #%u)",
                      static_cast<unsigned long long>(now), object_name(host).text, g_c43.kill_tries,
                      static_cast<unsigned long long>(kC43KillGapMs), g_c81.mech_skips);
        }
        return;
    }
    void* component = nullptr;
    const float before = host != nullptr ? c21_health(static_cast<const uint8_t*>(host), &component) : -1.0f;
    if (host == nullptr || before <= 0.0f || g_c43.kill_tries >= kC43KillTries) {
        log::line("%scard043: host real death: %u hits, Health %.1f", before <= 0.0f ? "" : "WARN ",
                  g_c43.kill_tries, static_cast<double>(before));
        g_c43.kill_ms = 0;
        return;
    }
    ++g_c43.kill_tries;
    g_c43.kill_ms = now + kC43KillGapMs;
    void* const bug = c30_nearest_enemy(world, host);
    const float max_health = c43_max_health(component);
    g_c43.in_kill = true; // card 196: the test god does not put this hit back
    const C211Landed landed = c211_kill_hit(host, bug, component, (max_health > 0.0f ? max_health : 100.0f) * 10.0f);
    g_c43.in_kill = false;
    log::line("card043: host t=%llu real-death hit %u on %s (causer %s): Health %.1f -> %.1f, autosave %d",
              static_cast<unsigned long long>(now), g_c43.kill_tries, object_name(host).text, object_name(bug).text,
              static_cast<double>(before),
              static_cast<double>(c21_health(static_cast<const uint8_t*>(host), &component)), c43_autosave(host));
    log::line("%scard211: host t=%llu real-death hit %u: landed %s", landed == kC211Neither ? "WARN " : "",
              static_cast<unsigned long long>(now), g_c43.kill_tries, kC211LandedText[landed]);
}

// ---- Card 130 item 14: every host AI trooper picks up a downed character ----------------------------------------
// The game's only NPC revive is the medic's USTAttackActionHeal::PerformHeal (exe+0x5165C40): GiveHealth on its
// target, dead or not, and an NPC's Health going 0 -> above 0 revives it (HandleHealthValueChangedEvent, then
// BP_OnRevived). No pick-up exists apart from that heal, and a card 043 downed player is no target of it (the downed
// state is mod code). Here the host sends the nearest free living trooper NPC (a class name holding "Trooper", the
// card 023 rule) to each card 043 Downed or Out player and each dead trooper NPC, through the game's own forced move
// (c130_force_move), reissued when it has ended short of the target. A plain STMoveToActor was taken but overruled by
// the trooper's combat behaviour: c130r and c130t parked a trooper at 333 and 274 uu for 30 s. Calling
// BeginForcedMoveToBehaviour directly was overruled the same way (c130w: 29 orders in 30 s, each ended in under a
// second, 618 uu covered), and so was the pending request the library sets: c130d5 sent three troopers in a row to
// the downed host and each left the move for Flee From Target, Move To Target LOS or Move To Desired Zone, so the
// player bled out. The overruling is the trooper's FSM_Enemy_Aware, so a pick holds its trooper out of it: the Aware
// machine is stopped, the move begun directly, and the machine started again when the pick ends. Once it stays within kC43ReviveRange for the hold time the pick-up completes: a player through
// c43_revive (the same restore, transition and broadcast as a player's hold), an NPC through the card 025 Health base
// write at kC43ReviveFraction of MaxHealth. A medic's own heal action still runs on top.
NpcBoolFn g_c130_overkilled = nullptr;
NpcBoolFn g_c130_is_aware = nullptr, g_c130_forced_pending = nullptr, g_c130_forced_moving = nullptr;
NpcVoidFn g_c130_become_aware = nullptr, g_c130_end_forced = nullptr;
NpcVoidFn g_c130_stop_aware = nullptr, g_c130_start_aware = nullptr, g_c130_begin_forced = nullptr;
NpcForcedMoveFn g_c130_set_forced_move = nullptr;
SmStateNameFn g_c130_sm_state_name = nullptr;
StaticClassFn g_c130_attack_class = nullptr;
AttackBoolFn g_c130_is_attacking = nullptr;
AttackAbortFn g_c130_abort_attack = nullptr;
namespace {
constexpr uint32_t kC130Picks = 16;
constexpr ULONGLONG kC130ScanMs = 500;
constexpr ULONGLONG kC130MoveMs = 1000;
constexpr ULONGLONG kC130GiveUpMs = 30000; // a trooper that never arrives frees the target for another
// A trooper whose begun forced move never stays running this long has no path to the target (Begin ends the move
// itself when the controller's move request fails). It is dropped from the pick and skipped for the rest of the map,
// so the next nearest trooper goes instead.
constexpr ULONGLONG kC130StallMs = 6000;
constexpr uint32_t kC130Shunned = 32;
constexpr double kC130SeekRange = 4000.0;
constexpr uint32_t kC130Lines = 24;
struct C130Pick {
    int32_t target_id;  // the downed player pawn's or dead NPC's InternalIndex
    int32_t trooper_id; // the trooper NPC's InternalIndex
    bool player;
    ULONGLONG assigned_ms;
    ULONGLONG near_ms; // first scan within range; 0 = not near
    ULONGLONG move_ms; // the last forced-move check
    uint32_t orders;   // forced moves issued so far
    uint32_t checks;   // forced-move checks so far (the diagnostic line's cadence)
    uint32_t aborts;   // attacks aborted so the pending move could be taken
    ULONGLONG wait_ms; // since the request went pending without the move starting; 0 = moving or no request
};
struct C130State {
    C130Pick pick[kC130Picks];
    uint32_t count, peak, done, gave_up, lost, lines, move_lines;
    int32_t shunned[kC130Shunned]; // InternalIndex of troopers that do not take forced moves (this map)
    uint32_t shunned_count;
    ULONGLONG next_ms;
    bool full_warned;
};
C130State g_c130p;
C23List g_c130_list; // the NPCs of this scan; 256 rows, kept off the stack

bool c130_shunned(int32_t id) {
    for (uint32_t i = 0; i < g_c130p.shunned_count; ++i) {
        if (g_c130p.shunned[i] == id) {
            return true;
        }
    }
    return false;
}
bool c130_line() {
    return g_c130p.lines++ < kC130Lines;
}
void* c130_npc(int32_t id) {
    for (uint32_t i = 0; i < g_c130_list.count; ++i) {
        if (object_index(g_c130_list.e[i].actor) == id) {
            return g_c130_list.e[i].actor;
        }
    }
    return nullptr;
}
bool c130_alive_trooper(const void* npc) {
    return npc != nullptr && c23_trooper(npc) && c23_set_health(npc) > 0.0f;
}
// A trooper the game is already force-moving: a mission script's ForceMoveNPCSTo* owns it, or this pick does.
// The Aware state machine instance (USTAggregatedSMInstance) StartStateMachine_Aware stores at the NPC's +0xA18.
constexpr uint32_t kNpcAwareSm = 0xA18;
// The trooper's current Aware state name into out (empty when the machine is not started), for the check lines.
void c130_sm_state(const void* npc, wchar_t* out, size_t chars) {
    out[0] = L'\0';
    const void* const sm = read_ptr(npc, kNpcAwareSm);
    if (sm == nullptr || g_c130_sm_state_name == nullptr) {
        return;
    }
    FStringRaw name{};
    g_c130_sm_state_name(sm, &name);
    swprintf(out, chars, L"%ls", fs_text(&name));
    if (name.data != nullptr) {
        g_memory_free(const_cast<wchar_t*>(name.data));
    }
}
bool c130_forced(const void* npc) {
    return g_c130_forced_pending(npc) || g_c130_forced_moving(npc);
}
// FSM_Enemy_Aware takes "Base to MoveToForcedLocation" when IsMovingToForcedLocation, or when CanEverMove, the
// pending request is set, the attack component is not attacking and the NPC is not a 2D squad member (decoded from
// the package's transition graph). Its other states (Flee From Target, Move To Target LOS, ...) leave the move again.
// A running attack is aborted too (forced: the pick-up outranks it); with the machine stopped no new one starts.
// True when aborted.
bool c130_clear_attack(void* trooper) {
    void* const attack = find_component(trooper, g_c130_attack_class);
    if (attack == nullptr || !g_c130_is_attacking(attack)) {
        return false;
    }
    return g_c130_abort_attack(attack, true);
}
// The per-NPC body of USTAIUtilityLibrary::ForceMoveNPCSToActorLocation (exe+0x51FB870), aware and the pending
// request, then the hold: StopStateMachine_Aware (a no-op when the machine is not active) after BecomeAware, which
// may start it, and BeginForcedMoveToBehaviour at once (it takes the request, clears pending, sets moving and asks
// the controller to move; a failed request ends the move again). ASTNonPlayableCharacterBase::OnMoveCompleted ends
// it on arrival. Acceptance 0 takes the movement component's default goal distance (50 uu). True when an attack was
// aborted.
bool c130_force_move(void* trooper, const void* target) {
    if (!g_c130_is_aware(trooper)) {
        g_c130_become_aware(trooper);
    }
    g_c130_stop_aware(trooper);
    const bool aborted = c130_clear_attack(trooper);
    const Vec3 at = actor_location(target);
    g_c130_set_forced_move(trooper, &at, 0.0f);
    g_c130_begin_forced(trooper);
    return aborted;
}
// A finished or dropped pick hands its trooper back to its own behaviour: a running forced move ends, and a held
// (ordered) trooper that still lives gets its Aware machine started again (a dead one is the Dying machine's).
void c130_release(const C130Pick& p, void* trooper) {
    if (trooper == nullptr) {
        return;
    }
    if (c130_forced(trooper)) {
        g_c130_end_forced(trooper);
    }
    if (p.orders != 0 && c130_alive_trooper(trooper)) {
        g_c130_start_aware(trooper);
    }
}
bool c130_dead_trooper(const void* npc) {
    const float hp = npc != nullptr && c23_trooper(npc) ? c23_set_health(npc) : -1.0f;
    return hp >= 0.0f && hp <= 0.0f && !g_c130_overkilled(npc); // -1 = no health set; a gibbed corpse stays dead
}
// The target's pawn when it still needs a pick-up, else null.
void* c130_target(const C43Roster& r, const C130Pick& p) {
    if (!p.player) {
        void* const npc = c130_npc(p.target_id);
        return c130_dead_trooper(npc) ? npc : nullptr;
    }
    const int32_t i = c43_find(p.target_id);
    const int32_t s = c43_slot(r, p.target_id);
    return i >= 0 && s >= 0 && (g_c43.e[i].state == C43Life::Downed || g_c43.e[i].state == C43Life::Out)
               ? r.p[s].pawn
               : nullptr;
}
bool c130_picked(int32_t id, bool trooper) {
    for (uint32_t i = 0; i < g_c130p.count; ++i) {
        if ((trooper ? g_c130p.pick[i].trooper_id : g_c130p.pick[i].target_id) == id) {
            return true;
        }
    }
    return false;
}
// The nearest free living trooper within kC130SeekRange of the target, or null.
void* c130_nearest_free(const void* target) {
    void* best = nullptr;
    double best_dist = kC130SeekRange;
    const Vec3 at = actor_location(target);
    for (uint32_t i = 0; i < g_c130_list.count; ++i) {
        void* const npc = g_c130_list.e[i].actor;
        const double d = npc != target && c130_alive_trooper(npc) && !c130_picked(object_index(npc), true) &&
                                 !c130_forced(npc) && !c130_shunned(object_index(npc))
                             ? vec_dist(g_c130_list.e[i].loc, at)
                             : 1e30;
        best = d <= best_dist ? npc : best;
        best_dist = d <= best_dist ? d : best_dist;
    }
    return best;
}
void c130_assign(void* target, bool player, ULONGLONG now) {
    const int32_t id = object_index(target);
    if (c130_picked(id, false)) {
        return;
    }
    if (g_c130p.count >= kC130Picks) {
        if (!g_c130p.full_warned) {
            g_c130p.full_warned = true;
            log::line("WARN card130: host AI pick-up table full (%u): %s waits", kC130Picks, object_name(target).text);
        }
        return;
    }
    void* const trooper = c130_nearest_free(target);
    if (trooper == nullptr) {
        return;
    }
    g_c130p.pick[g_c130p.count++] = C130Pick{id, object_index(trooper), player, now, 0, 0, 0, 0, 0, 0};
    if (g_c130p.count > g_c130p.peak) {
        g_c130p.peak = g_c130p.count;
        log::line("card130: host AI pick-up peak %u of %u", g_c130p.peak, kC130Picks);
    }
    if (c130_line()) {
        log::line("card130: host t=%llu AI pick-up: %s (%s) goes to %s %s, %.0f uu",
                  static_cast<unsigned long long>(now),
                  object_name(trooper).text, class_name(trooper).text, player ? "downed player" : "dead NPC",
                  object_name(target).text, vec_dist(actor_location(trooper), actor_location(target)));
    }
}
// The NPC's Health base to kC43ReviveFraction of MaxHealth (card 025's revive route); 0 -> above 0 revives it.
void c130_revive_npc(void* npc) {
    void* component = nullptr;
    c21_health(static_cast<const uint8_t*>(npc), &component);
    const float max_health = c43_max_health(component);
    c43_set_health(npc, max_health > 0.0f ? max_health * kC43ReviveFraction : 1.0f);
}
// The pick's trooper never started its forced move: drop the pick, hand the trooper back and skip it for the map.
// Always false (the pick ends); the target is assigned again on the next scan.
bool c130_shun(const C130Pick& p, void* trooper, ULONGLONG now) {
    ++g_c130p.lost;
    if (g_c130p.shunned_count < kC130Shunned) {
        g_c130p.shunned[g_c130p.shunned_count++] = p.trooper_id;
    }
    wchar_t state[64];
    c130_sm_state(trooper, state, sizeof(state) / sizeof(state[0]));
    log::line("card130: host t=%llu AI pick-up of entry %d dropped: %s gets no forced move running (not moving %llu "
              "ms, %u attacks aborted, aware state \"%ls\"); %u troopers skipped this map",
              static_cast<unsigned long long>(now), p.target_id, object_name(trooper).text,
              static_cast<unsigned long long>(now - p.wait_ms), p.aborts, state, g_c130p.shunned_count);
    c130_release(p, trooper);
    return false;
}
// One pick: false when it is done or dropped.
bool c130_pick_step(const C43Roster& r, C130Pick& p, ULONGLONG now) {
    void* const target = c130_target(r, p);
    void* const trooper = c130_npc(p.trooper_id);
    if (target == nullptr || !c130_alive_trooper(trooper) || now - p.assigned_ms >= kC130GiveUpMs) {
        const bool timed_out = target != nullptr && trooper != nullptr && now - p.assigned_ms >= kC130GiveUpMs;
        ++(timed_out ? g_c130p.gave_up : g_c130p.lost);
        const char* const why = target == nullptr ? "the target is up or gone"
                                : timed_out        ? "no arrival in time"
                                                   : "the trooper is down or gone";
        if (c130_line()) {
            log::line("card130: host t=%llu AI pick-up of entry %d dropped: %s", static_cast<unsigned long long>(now),
                      p.target_id, why);
        }
        c130_release(p, trooper);
        return false;
    }
    const double dist = vec_dist(actor_location(trooper), actor_location(target));
    if (dist > kC43ReviveRange) {
        p.near_ms = 0;
        if (now - p.move_ms >= kC130MoveMs) {
            p.move_ms = now;
            if (!g_c130_forced_moving(trooper)) {
                // the first order, or the last move ended short of the target
                p.aborts += c130_force_move(trooper, target) ? 1 : 0;
                ++p.orders;
            }
            const bool waiting = !g_c130_forced_moving(trooper);
            p.wait_ms = waiting ? (p.wait_ms != 0 ? p.wait_ms : now) : 0;
            if (p.wait_ms != 0 && now - p.wait_ms >= kC130StallMs) {
                return c130_shun(p, trooper, now);
            }
            // Every 5th check: where the trooper is and what its forced move is doing.
            if (++p.checks % 5 == 0 && g_c130p.move_lines < kC130Lines) {
                ++g_c130p.move_lines;
                const Vec3 at = actor_location(trooper);
                wchar_t state[64];
                c130_sm_state(trooper, state, sizeof(state) / sizeof(state[0]));
                log::line("card130: host t=%llu AI pick-up check %u: %s at (%.0f, %.0f, %.0f), %.0f uu from entry %d, "
                          "%u forced moves, %u attacks aborted, moving %d, pending %d, aware state \"%ls\"",
                          static_cast<unsigned long long>(now), p.checks, object_name(trooper).text, at.x, at.y, at.z,
                          dist, p.target_id, p.orders, p.aborts, g_c130_forced_moving(trooper) ? 1 : 0,
                          g_c130_forced_pending(trooper) ? 1 : 0, state);
            }
        }
        return true;
    }
    p.near_ms = p.near_ms != 0 ? p.near_ms : now;
    const int32_t ti = p.player ? c43_find(p.target_id) : -1;
    const ULONGLONG need = ti >= 0 && g_c43.e[ti].state == C43Life::Out ? kC43HoldOutMs : kC43HoldDownedMs;
    if (now - p.near_ms < need) {
        return true;
    }
    ++g_c130p.done;
    // slot: the card 043 roster slot of a player (0 is the host), -1 for an NPC
    log::line("card130: host t=%llu AI pick-up done: %s (%s) picked up %s %s (slot %d) after %llu ms near, "
              "%llu ms total",
              static_cast<unsigned long long>(now), object_name(trooper).text, class_name(trooper).text,
              p.player ? "player" : "NPC", object_name(target).text, p.player ? c43_slot(r, p.target_id) : -1,
              static_cast<unsigned long long>(now - p.near_ms), static_cast<unsigned long long>(now - p.assigned_ms));
    c130_release(p, trooper);
    if (p.player) {
        c43_revive(r, g_c43.e[ti], target, now - p.near_ms, now);
    } else {
        c130_revive_npc(target);
    }
    return false;
}
} // namespace

// Host, every kC130ScanMs: drop finished picks, advance the rest, then assign troopers to new targets.
void c130_pickup_step(void* world, const C43Roster& r, ULONGLONG now) {
    const bool resolved = g_c130_set_forced_move != nullptr &&
                          g_c130_end_forced != nullptr && g_c130_is_aware != nullptr && g_c130_become_aware != nullptr &&
                          g_c130_forced_pending != nullptr && g_c130_forced_moving != nullptr &&
                          g_c130_attack_class != nullptr && g_c130_is_attacking != nullptr &&
                          g_c130_abort_attack != nullptr && g_c130_stop_aware != nullptr &&
                          g_c130_start_aware != nullptr && g_c130_begin_forced != nullptr &&
                          g_c130_overkilled != nullptr && g_npc_class != nullptr;
    if (now < g_c130p.next_ms || !resolved) {
        return;
    }
    g_c130p.next_ms = now + kC130ScanMs;
    // The scan comes first: c130_npc matches object indices against g_c130_list, so the list must be this tick's (card
    // 331: the all-down branch below used the previous scan's list, kC130ScanMs old, whose actors may be freed).
    c23_collect(world, nullptr, g_npc_class(), g_c130_list);
    // All down is the mission's game over (card 043): no pick-up revives anyone after it, the same gate as a player
    // revive. Running picks hand their troopers back (c130u revived the client from Out 54 s after the all down).
    if (g_c43.all_down) {
        for (uint32_t i = 0; i < g_c130p.count; ++i) {
            ++g_c130p.lost;
            if (c130_line()) {
                log::line("card130: host t=%llu AI pick-up of entry %d dropped: all down",
                          static_cast<unsigned long long>(now), g_c130p.pick[i].target_id);
            }
            c130_release(g_c130p.pick[i], c130_npc(g_c130p.pick[i].trooper_id));
        }
        g_c130p.count = 0;
        return;
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < g_c130p.count; ++i) {
        if (c130_pick_step(r, g_c130p.pick[i], now)) {
            g_c130p.pick[kept++] = g_c130p.pick[i];
        }
    }
    g_c130p.count = kept;
    for (int32_t i = 0; i < g_c43.count; ++i) {
        const int32_t s = c43_slot(r, g_c43.e[i].pawn_id);
        if (s >= 0 && (g_c43.e[i].state == C43Life::Downed || g_c43.e[i].state == C43Life::Out)) {
            c130_assign(r.p[s].pawn, true, now);
        }
    }
    for (uint32_t i = 0; i < g_c130_list.count; ++i) {
        if (c130_dead_trooper(g_c130_list.e[i].actor)) {
            c130_assign(g_c130_list.e[i].actor, false, now);
        }
    }
}

void c130_pickup_map() {
    if (g_c130p.done + g_c130p.gave_up + g_c130p.lost + g_c130p.peak != 0) {
        log::line("card130: host AI pick-up map totals: %u done, %u timed out, %u dropped, peak %u of %u", g_c130p.done,
                  g_c130p.gave_up, g_c130p.lost, g_c130p.peak, kC130Picks);
    }
    g_c130p = C130State{};
}

// Card 374: host. A player that joins after the all-down (the host's pawn Dead, the host on its death screen) had no
// entry: its copy stayed Up on the host while the client showed the death screen (the Dead join replay). Such a copy
// goes Out like every copy present at the all-down. Only while the host's own pawn still holds its Dead entry, so a
// pawn the restart or load spawns before the per-map reset is not touched.
void c374_late_copies(const C43Roster& r, ULONGLONG now) {
    const int32_t host = g_c43.all_down && r.p[0].pawn != nullptr ? c43_find(r.p[0].id) : -1;
    if (host < 0 || g_c43.e[host].state != C43Life::Dead) {
        return;
    }
    for (uint32_t s = 1; s < kC43Max; ++s) {
        static int32_t refused = -1; // a pawn c43_enter_down refused (mech, table full): asked once, not every tick
        if (r.p[s].pawn == nullptr || c43_find(r.p[s].id) >= 0 || r.p[s].id == refused) {
            continue;
        }
        log::line("card374: host t=%llu %s (slot %u) joined after the all-down: Out", static_cast<unsigned long long>(now),
                  object_name(r.p[s].pawn).text, s);
        c43_enter_down(r.p[s].pawn, r, s, C43Life::Out, now);
        refused = c43_find(r.p[s].id) < 0 ? r.p[s].id : refused;
    }
}

void c43_host_step(void* world, ULONGLONG now) {
    const C43Roster r = c43_roster(world);
    const float dt = g_c43.last_ms != 0 ? static_cast<float>(now - g_c43.last_ms) / 1000.0f : 0.0f;
    g_c43.last_ms = now;
    c43_entries_step(r, dt, now);
    for (int32_t h = g_c43.holds - 1; h >= 0; --h) {
        if (!c43_hold_step(r, g_c43.hold[h], now)) {
            c43_remove_hold(h);
        }
    }
    c130_pickup_step(world, r, now); // card 130 item 14
    c374_late_copies(r, now);
    // Card 366: no players >= 2 term. A Downed or Out host whose last teammate left (Logout or timeout) has nobody to
    // revive it; with the term it stayed down for good. One player and no entry (solo play) never gets here: count is 0.
    if (g_c43.count != 0 && !g_c43.all_down && r.p[0].pawn != nullptr && c43_up_count(r, -1) == 0) {
        c43_all_down(r, r.p[0].pawn, r.players >= 2 ? "all down: nobody Up" : "all down: last teammate left (card 366)");
        g_c43.kill_ms = now;
    }
    if (g_c43.kill_ms != 0 && now >= g_c43.kill_ms) {
        c43_kill_host(world, r, now);
    }
}

// Card 130 item 11: both sides. The Downed or Out teammate that the own Up pawn `own` is within kC43ReviveRange of,
// or null. The host reads its entries and roster as c43_use_edge does; the client reads every other player pawn's
// entry (card 209). Read only: the prompt shows where a press would start a hold.
const void* c43_revivable_near(const void* own) {
    void* const world = own != nullptr ? g_actor_get_world(own) : nullptr;
    if (world == nullptr || c43_is_down(own) || g_c43.all_down) {
        return nullptr;
    }
    if (g_client_session) {
        void* others[kC43Max];
        const int32_t n = c43_client_other_pawns(world, others);
        const void* best = nullptr;
        double best_dist = kC43ReviveRange;
        for (int32_t i = 0; i < n; ++i) {
            const int32_t e = c43_find_pawn(others[i]);
            const bool down = e >= 0 && (g_c43.e[e].state == C43Life::Downed || g_c43.e[e].state == C43Life::Out);
            const double d = down ? vec_dist(actor_location(own), actor_location(others[i])) : 1e30;
            best = d <= best_dist ? others[i] : best;
            best_dist = d <= best_dist ? d : best_dist;
        }
        return best;
    }
    const C43Roster r = c43_roster(world);
    const void* best = nullptr;
    double best_dist = kC43ReviveRange;
    for (int32_t i = 0; i < g_c43.count; ++i) {
        const int32_t slot = c43_slot(r, g_c43.e[i].pawn_id);
        const bool down = g_c43.e[i].state == C43Life::Downed || g_c43.e[i].state == C43Life::Out;
        const double d = slot >= 0 && down ? vec_dist(actor_location(own), actor_location(r.p[slot].pawn)) : 1e30;
        best = d <= best_dist ? r.p[slot].pawn : best;
        best_dist = d <= best_dist ? d : best_dist;
    }
    return best;
}

// Host: an interact edge from the reviver pawn (the host's own, or a copy's relayed Use frame).
void c43_use_edge(void* reviver, bool pressed) {
    const int32_t id = object_index(reviver);
    const int32_t h = c43_find_hold(id);
    const ULONGLONG now = GetTickCount64();
    if (!pressed) {
        if (h >= 0) {
            log::line("card043: host t=%llu use release by %s: held %llu ms", static_cast<unsigned long long>(now),
                      object_name(reviver).text, static_cast<unsigned long long>(now - g_c43.hold[h].start_ms));
            c43_remove_hold(h);
        }
        return;
    }
    void* const world = g_actor_get_world(reviver);
    const C43Roster r = world != nullptr ? c43_roster(world) : C43Roster{};
    if (h >= 0 || c43_is_down(reviver) || g_c43.all_down || g_c43.holds >= kC43Max) {
        log::line("card043: host use press by %s ignored: hold %d, down %d, all down %d", object_name(reviver).text,
                  h, c43_is_down(reviver) ? 1 : 0, g_c43.all_down ? 1 : 0);
        return;
    }
    int32_t best = -1;
    double best_dist = kC43ReviveRange;
    for (int32_t i = 0; i < g_c43.count; ++i) {
        const int32_t s = c43_slot(r, g_c43.e[i].pawn_id);
        const bool down = g_c43.e[i].state == C43Life::Downed || g_c43.e[i].state == C43Life::Out;
        const double d = s >= 0 && down ? vec_dist(actor_location(reviver), actor_location(r.p[s].pawn)) : 1e30;
        best = d <= best_dist ? i : best;
        best_dist = d <= best_dist ? d : best_dist;
    }
    if (best < 0) {
        log::line("card043: host t=%llu use press by %s: no downed teammate within %.0f uu",
                  static_cast<unsigned long long>(now), object_name(reviver).text, kC43ReviveRange);
        return;
    }
    g_c43.hold[g_c43.holds++] = C43Hold{id, g_c43.e[best].pawn_id, now};
    log::line("card043: host t=%llu use press by %s: reviving entry %d (%s, %.0f uu, needs %llu ms)",
              static_cast<unsigned long long>(now), object_name(reviver).text, g_c43.e[best].pawn_id,
              kC43LifeText[static_cast<size_t>(g_c43.e[best].state)], best_dist,
              static_cast<unsigned long long>(g_c43.e[best].state == C43Life::Downed ? kC43HoldDownedMs
                                                                                     : kC43HoldOutMs));
}

// Both sides: this machine's own downed view: camera lowered, fire released; the client also zeroes walk speed.
void c43_own_view(void* pawn, bool down) {
    if (down == g_c43.view_down || pawn == nullptr || !c43_is_trooper(pawn, "own view")) { // card 586: no camera
        return;
    }
    g_c43.view_down = down;
    const bool same = down || object_index(pawn) == g_c43.view_pawn; // up on a new pawn: its camera was never lowered
    g_c43.view_pawn = down ? object_index(pawn) : -1;
    void* const camera = same ? read_ptr(pawn, kCharacterCamera) : nullptr;
    const Vec3 delta{0.0, 0.0, down ? -kC43CameraDrop : kC43CameraDrop};
    if (camera != nullptr) {
        g_add_local_offset(camera, &delta, false, nullptr, kTeleportNone);
    }
    if (down) {
        c43_release_own_fire(pawn);
    }
    if (g_client_session && down) {
        g_c43.view_walk = c43_walk(pawn);
        c43_set_walk(pawn, 0.0f);
    } else if (g_client_session) {
        const float set = c295_restore_walk(pawn, g_c43.view_walk, c295_host_value(), "revive"); // card 295
        c295_client_up(set, GetTickCount64(), pawn);
    }
    log::line("card043: %s own view %s: camera %p moved %.0f uu, walk speed %.0f", side(),
              down ? "downed" : "up", camera, delta.z, static_cast<double>(c43_walk(pawn)));
}

// Client: one Down frame from the host: v0, v1 = the pawn's NetGUID (card 209), v2 = state, v3 = seconds, v4 = the
// host slot. The entry is keyed by the GUID, so it exists before the pawn resolves; "own" is the own pawn's GUID.
void c43_receive_down(void* pc, const ChanArgs& args) {
    ++g_c43.n.down_recv;
    g_c43.client_seen = true;
    const uint64_t guid = c41_args_guid(args.v[0], args.v[1]);
    const int32_t id = static_cast<int32_t>(guid); // below 2^28 (c41_args_guid)
    const bool self = guid != 0 && id == c43_client_id(c23_own_pawn(pc));
    const C43Life state = args.v[2] < static_cast<uint32_t>(C43Life::Count) ? static_cast<C43Life>(args.v[2])
                                                                           : C43Life::Up;
    const bool up = state == C43Life::Up;
    const uint32_t secs = up ? 0u : args.v[3]; // card 295: an Up frame's v3 is the host's walk speed
    if (up && self) {
        c295_host_walk(args.v[3]);
    }
    const ULONGLONG now = GetTickCount64();
    int32_t i = guid != 0 ? c43_find(id) : -1;
    if (i < 0 && guid != 0 && g_c43.count < kC43Max) {
        i = g_c43.count++;
        g_c43.e[i] = C43Entry{id, args.v[4], C43Life::Up, 0.0f, -1.0f, 0, 0, -1};
    }
    if (i < 0) {
        if (note_event_cap(g_c43.down_warns, kC43DownLines, "card209 Down")) {
            log::line("WARN card209: client Down %s for guid 0x%llx dropped: %s",
                      kC43LifeText[static_cast<size_t>(state)], static_cast<unsigned long long>(guid),
                      guid == 0 ? "guid 0" : "entry table full");
        }
        return;
    }
    if (!up) {
        c362_down_frame(id, now); // card 362: the host's kill of a pawn whose InstaKill the client skipped
    }
    C43Entry& e = g_c43.e[i];
    const C43Life was = e.state;
    const bool changed = state != was || e.down_ms == 0;
    e.secs_left = static_cast<float>(secs);
    if (changed) {
        e.down_ms = c43_down_state(state) && !c43_down_state(was) ? now : e.down_ms;
        e.down_ms = e.down_ms == 0 ? now : e.down_ms;
        e.up_ms = state == C43Life::Up ? now : 0;
        e.state = state;
        log::line("card043: client t=%llu %s guid 0x%llx (host slot %u) %s -> %s, %u s bleedout (Down received %u)",
                  static_cast<unsigned long long>(now), self ? "own pawn" : "other pawn",
                  static_cast<unsigned long long>(guid), e.slot, kC43LifeText[static_cast<size_t>(was)],
                  kC43LifeText[static_cast<size_t>(state)], secs, g_c43.n.down_recv);
    } else if (self && static_cast<int32_t>(secs) != e.shown_secs &&
               note_event_cap(g_c43.sec_lines, kC43SecLines, "card043 own bleedout")) {
        log::line("card043: client own view: %s, %u s left", kC43LifeText[static_cast<size_t>(state)], secs);
    }
    e.shown_secs = static_cast<int32_t>(secs);
    if (!self && state == C43Life::Dead && changed && !g_c43.death_done) {
        g_c43.death_pending = true; // card 044: all down; the own pawn dies on the next tick
    }
    if (self) {
        c43_own_view(const_cast<void*>(c23_own_pawn(pc)), c43_down_state(state));
    }
    if (self && changed && state == C43Life::Up && c43_down_state(was)) {
        // Card 156: a revive. The death UI is restored only when it went up; an open pause menu stays.
        constexpr const char* kKind[] = {"", "revive from Downed", "revive from Out", "up from Dead"};
        c156_client_up(pc, kKind[static_cast<size_t>(was)], g_c45.screen_count > 0, now);
    }
}

void c44_client_death(void* pc);

// Client tick: walk speed stays 0 while the own pawn is down; Up entries leave after the StandUp window.
void c43_client_step(void* pc, ULONGLONG now) {
    if (g_c43.death_pending) {
        c44_client_death(pc);
    }
    c209_step(pc); // card 209: held rows whose pawn resolved
    const int32_t own_id = g_c43.count > 0 ? c43_client_id(c23_own_pawn(pc)) : -1;
    for (int32_t i = g_c43.count - 1; i >= 0; --i) {
        const C43Entry& e = g_c43.e[i];
        if (e.state == C43Life::Up && now - e.up_ms >= kC43StandUpMs) {
            c43_remove(i);
        } else if (own_id >= 0 && e.pawn_id == own_id && c43_down_state(e.state)) {
            void* const own = const_cast<void*>(c23_own_pawn(pc));
            if (own != nullptr) {
                c43_set_walk(own, 0.0f);
            }
        }
    }
}

// --- Card 209: player rows held until their pawn's NetGUID resolves (client) ---
struct C209Row {
    uint64_t guid;
    uint32_t value;
    C209Op op;
};
constexpr int32_t kC209Rows = kC43Max * static_cast<int32_t>(C209Op::Count); // one per op and player
constexpr const char* kC209OpText[] = {"BugForm", "Warp", "Resupply"};
static_assert(sizeof(kC209OpText) / sizeof(kC209OpText[0]) == static_cast<size_t>(C209Op::Count));
struct C209State {
    C209Row row[kC209Rows];
    int32_t count;
    uint32_t held, applied, full;
};
C209State g_c209{};

void c209_hold(C209Op op, uint64_t guid, uint32_t value) {
    int32_t i = 0;
    for (; i < g_c209.count && i < kC209Rows; ++i) {
        if (g_c209.row[i].op == op && g_c209.row[i].guid == guid) {
            break; // last value wins
        }
    }
    if (i >= kC209Rows) {
        ++g_c209.full;
        log::line("WARN card209: client %s %u for guid 0x%llx dropped: held table full (%d)",
                  kC209OpText[static_cast<size_t>(op)], value, static_cast<unsigned long long>(guid), kC209Rows);
        return;
    }
    g_c209.count = i == g_c209.count ? g_c209.count + 1 : g_c209.count;
    g_c209.row[i] = C209Row{guid, value, op};
    ++g_c209.held;
    log::line("card209: client t=%llu %s %u for guid 0x%llx held: the pawn does not resolve yet (%d held)",
              static_cast<unsigned long long>(GetTickCount64()), kC209OpText[static_cast<size_t>(op)], value,
              static_cast<unsigned long long>(guid), g_c209.count);
}

// Client tick: each held row whose pawn resolves now leaves the table and is applied.
void c209_step(void* pc) {
    for (int32_t i = g_c209.count - 1; i >= 0; --i) {
        void* const pawn = c41_resolve_player(pc, g_c209.row[i].guid);
        if (pawn == nullptr) {
            continue;
        }
        const C209Row r = g_c209.row[i];
        g_c209.row[i] = g_c209.row[--g_c209.count];
        ++g_c209.applied;
        log::line("card209: client t=%llu held %s %u for guid 0x%llx applied: resolved to %s",
                  static_cast<unsigned long long>(GetTickCount64()), kC209OpText[static_cast<size_t>(r.op)], r.value,
                  static_cast<unsigned long long>(r.guid), object_name(pawn).text);
        if (r.op == C209Op::BugForm) {
            c208_apply(pc, pawn, r.guid, static_cast<int32_t>(r.value));
        } else if (r.op == C209Op::Resupply) {
            c491_level_apply(pawn, r.guid, static_cast<int32_t>(r.value)); // card 502 finding 3
        } else {
            c46_warp_apply(pc, pawn, r.guid, r.value);
        }
    }
}

void c209_arm_map() {
    if (g_c209.held + g_c209.full != 0) {
        log::line("card209: map end: rows held %u, applied %u, dropped unresolved %d, table full %u", g_c209.held,
                  g_c209.applied, g_c209.count, g_c209.full);
    }
    g_c209 = C209State{};
}

// Host: a Use frame from a server copy (the client's interact edge).
void c43_receive_use(void* pawn, uint32_t arg) {
    ++g_c43.n.use_recv;
    log::line("card043: host t=%llu Use %u received from %s (Use received %u)",
              static_cast<unsigned long long>(GetTickCount64()), arg, object_name(pawn).text, g_c43.n.use_recv);
    if (net_mode(pawn) == kNetModeListenServer) {
        c43_use_edge(pawn, arg != 0);
        c50_relay_use(pawn, arg != 0); // card 050 step 3a: pickups, unless the press started a revive hold
    }
}

// --- Card 044: the client's game over, the death screen's buttons ---
// Client: on the all-down Down frame its own pawn runs the real StartDeath once, through the trampoline (the
// countdown then reaches CompleteDeath, OnDeath and the death screen). Both sides: CompleteDeath opens a window in
// which UI input contexts are logged, and the death screen's Construct dumps its widgets' bound delegates. The
// state outlives maps, so a restart driven by the test does not re-run the test drivers.
constexpr size_t kC44DamageEventBytes = 0x200; // StartDeath's damage event, zeroed: its reads are count-guarded

constexpr ULONGLONG kC44FallbackMs = 8000; // no CompleteDeath this long after the client's StartDeath: called here
constexpr uint32_t kC44Lines = 200;

constexpr uint32_t kC44CompleteLines = 8;

constexpr int32_t kC44MaxBound = 8;
constexpr size_t kC44PathChars = 96;

C44State g_c44{};

bool c44_line() {
    return note_event_cap(g_c44.lines, kC44Lines, "card044");
}

void c44_pawn_line(const char* what, const void* pawn) {
    void* component = nullptr;
    const float health = c21_health(static_cast<const uint8_t*>(pawn), &component);
    log::line("card044: %s t=%llu %s %s (%s, role %u/%u): Health %.1f, TimeUntilDeathCompletion %.2f, "
              "IsAutoSaveEnabled %d",
              side(), static_cast<unsigned long long>(GetTickCount64()), what, object_name(pawn).text,
              kSprintSideText[sprint_side(pawn)], static_cast<const uint8_t*>(pawn)[kActorLocalRole],
              static_cast<const uint8_t*>(pawn)[kActorRemoteRole], static_cast<double>(health),
              static_cast<double>(read_f32(pawn, kPawnDeathTimer)), c43_autosave(pawn));
}

// Client: the own pawn's real StartDeath, once per map, through the trampoline (never the detour).
void c44_client_death(void* pc) {
    g_c43.death_pending = false;
    void* const own = const_cast<void*>(c23_own_pawn(pc));
    if (own == nullptr || g_c43.death_done) {
        log::line("WARN card044: client real death not run: own pawn %p, already done %d", own,
                  g_c43.death_done ? 1 : 0);
        return;
    }
    g_c43.death_done = true;
    alignas(16) static uint8_t damage_event[kC44DamageEventBytes];
    memset(damage_event, 0, sizeof(damage_event));
    // Card 196: a bug pawn runs its own override (not hooked; its base call reaches detour_start_death, which passes
    // under death_pass), so the equipped form's weapon ASC is cancelled as on the host.
    const size_t at = c196_inventory_at(own);
    const bool bug = at == kBugFormsInventory && g_bug_start_death != nullptr;
    const bool mi = at == kMiWeaponInventory;
    const DeathEventFn fn = mi ? g_orig_start_death_mi : (bug ? g_bug_start_death : g_orig_start_death);
    const char* const call = bug ? "StartDeath (bug override) call" : "StartDeath (base trampoline) call";
    c44_pawn_line(mi ? "StartDeath (MI trampoline) call" : call, own);
    if (bug) {
        log::line("card196: client t=%llu bug StartDeath (form ASC cancel)",
                  static_cast<unsigned long long>(GetTickCount64()));
    }
    g_c44.start_ms = GetTickCount64();
    g_c44.window_ms = g_c44.start_ms;
    g_c43.death_pass = true;
    fn(own, damage_event);
    g_c43.death_pass = false;
    c44_pawn_line("StartDeath return", own);
}

// Client tick: no CompleteDeath kC44FallbackMs after the own real StartDeath: logged and called once here.
void c44_client_tick(void* pc, ULONGLONG now) {
    if (g_c44.start_ms == 0 || g_c44.fallback_done || g_c44.complete_ms >= g_c44.start_ms ||
        now < g_c44.start_ms + kC44FallbackMs) {
        return;
    }
    g_c44.fallback_done = true;
    void* const own = const_cast<void*>(c23_own_pawn(pc));
    log::line("WARN card044: client no CompleteDeath %llu ms after StartDeath (own pawn %s, "
              "TimeUntilDeathCompletion %.2f): called here",
              static_cast<unsigned long long>(now - g_c44.start_ms), object_name(own).text,
              own != nullptr ? static_cast<double>(read_f32(own, kPawnDeathTimer)) : -1.0);
    if (own != nullptr) {
        detour_complete_death(own);
    }
}

// CompleteDeath entry, both sides: opens the input-context window and the death-screen gate.
void c44_complete_death(const void* pawn) {
    const ULONGLONG now = GetTickCount64();
    g_c44.complete_ms = now;
    g_c44.window_ms = now;
    pe_gate_set(kPeGate_c44_pe, true);
    if (note_event_cap(g_c44.complete_lines, kC44CompleteLines, "card044 CompleteDeath")) {
        log::line("card044: %s t=%llu CompleteDeath on %s (%s): %lld ms after the client's StartDeath, OnDeath "
                  "bound %d, IsAutoSaveEnabled %d",
                  side(), static_cast<unsigned long long>(now), object_name(pawn).text,
                  kSprintSideText[sprint_side(pawn)],
                  g_c44.start_ms != 0 ? static_cast<long long>(now - g_c44.start_ms) : -1ll,
                  read_i32(pawn, kPawnOnDeath + kArrayNum), c43_autosave(pawn));
    }
}

// Host: a Fire frame from a downed server copy (card 109: the relay passes both Fire 1 and Fire 0).
void c44_host_fire(const void* pawn, uint32_t arg) {
    if (!is_a(pawn, g_player_mi_class()) || !c43_is_down(pawn) || !c44_line()) {
        return;
    }
    log::line("card044: host t=%llu Fire %u from downed %s: %s; server-copy FireProjectile calls so far %u",
              static_cast<unsigned long long>(GetTickCount64()), arg, object_name(pawn).text,
              arg != 0 ? "press passed to the relay" : "release passed to the relay", g_c44_fires[kSideServerCopy]);
}

// Bound entries of one multicast inline delegate property of `obj`. Notes the RestartMission click function.
void c44_dump_bound(const void* screen, const void* obj, const void* field, const char* path) {
    const size_t off = static_cast<size_t>(read_i32(field, kPropertyOffset));
    const auto* const data = static_cast<const uint8_t*>(read_ptr(obj, off));
    const int32_t num = read_i32(obj, off + kArrayNum);
    const ObjectName prop = fname_text(static_cast<const uint8_t*>(field) + kFieldName);
    for (int32_t i = 0; data != nullptr && i < num && i < kC44MaxBound; ++i) {
        const uint8_t* const d = data + static_cast<size_t>(i) * kScriptDelegateBytes;
        const void* const target = g_weak_get(d);
        if (c44_line()) {
            log::line("card044: %s death screen %s.%s[%d of %d] -> %s (%s).%s", side(), path, prop.text, i, num,
                      object_name(target).text, class_name(target).text, fname_text(d + kScriptDelegateName).text);
        }
        if (target == screen && g_c44.restart_fn == 0 && strstr(path, "Restart") != nullptr &&
            strstr(prop.text, "Click") != nullptr) {
            g_c44.restart_fn = read_u64(d, kScriptDelegateName);
        }
        if (target == screen && g_c44.checkpoint_fn == 0 && strstr(path, "LoadLastSave") != nullptr &&
            strstr(prop.text, "Click") != nullptr) {
            g_c44.checkpoint_fn = read_u64(d, kScriptDelegateName);
        }
    }
}

// Every multicast inline delegate property of `obj` (class chain): its bound entries.
void c44_dump_delegates(const void* screen, const void* obj, const char* path) {
    const void* cls = read_ptr(obj, kObjectClass);
    for (int depth = 0; depth < kC44MaxSupers && cls != nullptr; ++depth, cls = read_ptr(cls, kStructSuper)) {
        const void* f = read_ptr(cls, kStructChildProperties);
        for (int i = 0; i < kC44MaxFields && f != nullptr; ++i, f = read_ptr(f, kFieldNext)) {
            if (strcmp(fname_text(read_ptr(f, kFieldClassPrivate)).text, "MulticastInlineDelegateProperty") == 0) {
                c44_dump_bound(screen, obj, f, path);
            }
        }
    }
}

// One nested WB_* widget of the screen: its own widgets' delegates (the third and last level).
void c44_dump_nested(const void* screen, const C44Kid& kid) {
    C44Kid sub[kC44MaxKids];
    const int32_t m = c44_kids(kid.obj, sub);
    for (int32_t j = 0; j < m; ++j) {
        char path[kC44PathChars];
        snprintf(path, sizeof(path), "%s.%s", kid.name.text, sub[j].name.text);
        if (c44_line()) {
            log::line("card044: %s death screen widget %s (%s)", side(), path, class_name(sub[j].obj).text);
        }
        c44_dump_delegates(screen, sub[j].obj, path);
    }
}

void c44_dump_screen(void* screen) {
    log::line("card044: %s t=%llu death screen %s (%s) constructed; widgets and bound delegates follow", side(),
              static_cast<unsigned long long>(GetTickCount64()), object_name(screen).text, class_name(screen).text);
    c44_dump_delegates(screen, screen, "self");
    C44Kid kids[kC44MaxKids];
    const int32_t n = c44_kids(screen, kids);
    for (int32_t i = 0; i < n; ++i) {
        const ObjectName kc = class_name(kids[i].obj);
        if (c44_line()) {
            log::line("card044: %s death screen widget %s (%s)", side(), kids[i].name.text, kc.text);
        }
        c44_dump_delegates(screen, kids[i].obj, kids[i].name.text);
        if (strncmp(kc.text, "WB_", 3) == 0) {
            c44_dump_nested(screen, kids[i]);
        }
    }
    log::line("card044: %s death screen: %d widgets; RestartMission click function %s, LoadLastSave click function %s",
              side(), n, g_c44.restart_fn != 0 ? fname_text(&g_c44.restart_fn).text : "(not found)",
              g_c44.checkpoint_fn != 0 ? fname_text(&g_c44.checkpoint_fn).text : "(not found)");
}

// ProcessEvent, after the original, while the gate is open: the death screen's Construct (its bindings are made).
void c44_after_pe(void* object, const void* function, uint64_t name) {
    if (function == nullptr || !pe_name_is(name, g_pe.names.construct)) { // card 573 b463: the detour's one FName read
        return;
    }
    if (strncmp(class_name(object).text, "WB_PlayerDeathScreen", 20) != 0) {
        return;
    }
    pe_gate_set(kPeGate_c44_pe, false);
    g_weak_assign(&g_c44.screen_weak, &object);
    c44_dump_screen(object);
    if (g_client_session) {
        c45_client_screen(object); // card 045: the client cannot restart the host's mission
    }
}

// Host test: the RestartMission click; the next map runs no test.
void c44_restart() {
    g_c44.test_over = true;
    c44_press(g_c44.restart_fn, "RestartMission");
}

// Card 045 test (UBWCOOP_C45_TEST=1 with UBWCOOP_C43_TEST=1); defined with card 045 below.
uint32_t g_c45_phase = 0; // restarts the host test has driven (outlives maps)

constexpr ULONGLONG kC43HitGapMs = 500;
constexpr uint32_t kC43MaxHits = 40;
constexpr uint32_t kC43SelfHits = 3;
constexpr float kC43DrainHit = 50.0f; // 5 s of bleedout
constexpr double kC43TestGap = 120.0; // uu between the reviver and the downed pawn

constexpr ULONGLONG kC43ClientKeyMs = 2600; // card 044: after the Death01 and Corpse01 shots
constexpr double kC44ShotGap = 350.0;       // uu between the camera's pawn and the downed pawn it shoots

constexpr ULONGLONG kC44ClientDoneMs = 30000; // client: the run ends this long after the host's game over
constexpr ULONGLONG kC44RefusePressMs = 1000; // host test, after the host's Down: Fire and Walk pressed
constexpr ULONGLONG kC44RefuseReleaseMs = 1500;
constexpr ULONGLONG kC44ReviverInMs = 2000;   // then the copy comes in to revive
constexpr ULONGLONG kC44FireAfterMs = 1000;   // client test, after the own Down: Fire 1, then Fire 0
constexpr ULONGLONG kC44FireGapMs = 500;

constexpr ULONGLONG kC43ClientHoldMs = 8000;

C43Test g_c43t{};

void c44_aim_other(const void* world, void* pc) {
    const void* const own = c23_own_pawn(pc);
    const void* const other = c44_other_pawn(world);
    if (own == nullptr || other == nullptr) {
        log::line("WARN card044: %s shot aim skipped: own pawn %p, other pawn %p", side(), own, other);
        return;
    }
    const Vec3 e = actor_location(own);
    const Vec3 t = actor_location(other);
    const double pitch = c44_aim(pc, e, t);
    log::line("card044: %s shot aim: %s at %s, %.0f uu, pitch %.1f", side(), object_name(own).text,
              object_name(other).text, vec_dist(e, t), pitch);
}

// Host test: one DealDamage hit (self-causer for the first kC43SelfHits, then the nearest bug).
void c43_hit(void* world, void* target, float amount) {
    void* const bug = g_c43t.hits >= kC43SelfHits ? c30_nearest_enemy(world, target) : nullptr;
    void* component = nullptr;
    const float before = c21_health(static_cast<const uint8_t*>(target), &component);
    const float max_health = c43_max_health(component);
    const float damage = amount > 0.0f ? amount : (max_health > 0.0f ? max_health : 100.0f) * 4.0f;
    ++g_c43t.hits;
    g_deal_damage(bug != nullptr ? bug : target, target, damage, bug == nullptr && g_c43t.hits > kC43SelfHits);
    log::line("card043: host t=%llu test hit %u on %s (causer %s, %.0f): Health %.1f -> %.1f",
              static_cast<unsigned long long>(GetTickCount64()), g_c43t.hits, object_name(target).text,
              object_name(bug).text, static_cast<double>(damage), static_cast<double>(before),
              static_cast<double>(c21_health(static_cast<const uint8_t*>(target), &component)));
}

// Host test: hit `target` every kC43HitGapMs until it is in state `want`. True = reached (or given up).
bool c43_hit_until(void* world, void* target, bool want_all_down, ULONGLONG now) {
    const bool reached = want_all_down ? g_c43.all_down : c43_down_state(c43_life(target));
    if (reached || g_c43t.hits >= kC43MaxHits) {
        log::line("%scard043: host test %s after %u hits", reached ? "" : "WARN ", reached ? "down" : "gave up",
                  g_c43t.hits);
        g_c43t.hits = 0;
        return true;
    }
    if (now >= g_c43t.at_ms) {
        g_c43t.at_ms = now + kC43HitGapMs;
        c43_hit(world, target, 0.0f);
    }
    return false;
}

// Host test: `mover` is put `gap` uu from `to`; the host's PC faces `to` when the host moves (card 044: a shot
// distance aims at its feet).
void c43_place(void* pc, void* mover, const void* to, double gap) {
    const Vec3 t = actor_location(to);
    const Vec3 at{t.x + gap, t.y, t.z};
    const bool moved = g_set_actor_location(mover, &at, false, nullptr, kTeleportPhysics);
    if (sprint_side(mover) == kSideHostOwn && gap > kC43TestGap) {
        c44_aim(pc, at, t);
    } else if (sprint_side(mover) == kSideHostOwn) {
        c25_face(pc, at, t);
    }
    log::line("card043: host test placed %s %.0f uu from %s: moved %d", object_name(mover).text, gap,
              object_name(to).text, moved ? 1 : 0);
}

// Host test, the copy's half: down, drain, Out, revived by the host's held interact.
void c43_test_copy(void* world, void* pc, void* host, void* copy, ULONGLONG now) {
    const int32_t ci = c43_find(object_index(copy));
    switch (g_c43t.stage) {
    case C43Stage::KillCopy:
        if (c43_hit_until(world, copy, false, now)) {
            c43_place(pc, host, copy, kC44ShotGap);
            c43_shot(now + 500, "remote Death01 +0.5 s (copy downed)", kC44ShotAim);
            c43_shot(now + 2000, "remote Corpse01 (copy downed)", kC44ShotAim);
            c43_next(C43Stage::Drain, now + 3000);
        }
        return;
    case C43Stage::Drain:
        if (now >= g_c43t.at_ms) {
            log::line("card043: host test drain hit: bleedout %.1f s before", ci >= 0 ? g_c43.e[ci].secs_left : -1.0);
            c43_hit(world, copy, kC43DrainHit);
            log::line("card043: host test drain hit: bleedout %.1f s after", ci >= 0 ? g_c43.e[ci].secs_left : -1.0);
            c43_next(C43Stage::WaitOut, now);
        }
        return;
    case C43Stage::WaitOut:
        if (c43_life(copy) == C43Life::Out) {
            c43_place(pc, host, copy, kC43TestGap);
            autoinput::key_event(pc, L"E", true);
            g_c43t.key_held = true;
            c43_next(C43Stage::CheckHold, now + 1000);
        }
        return;
    case C43Stage::CheckHold:
        if (now >= g_c43t.at_ms) {
            if (c43_find_hold(object_index(host)) < 0) {
                log::line("WARN card043: host test: no hold 1 s after E; F7 fallback: use edge called directly");
                c43_use_edge(host, true);
            }
            c43_next(C43Stage::WaitCopyUp, now + 15000);
        }
        return;
    default:
        break;
    }
    const bool up = c43_life(copy) == C43Life::Up;
    if (up || now >= g_c43t.at_ms) {
        autoinput::key_event(pc, L"E", false);
        g_c43t.key_held = false;
        if (up) {
            c43_place(pc, host, copy, kC44ShotGap);
            c43_shot(now + 300, "remote StandUp +0.3 s (copy revived)", kC44ShotAim);
            c43_shot(now + 2000, "remote standing +2 s (copy revived)", kC44ShotAim);
        } else {
            log::line("WARN card043: host test: copy not revived in 15 s");
        }
        c43_next(g_c45_test ? C43Stage::KillHost2 : C43Stage::KillHost, now + 5000); // card 045: no host revive
    }
}

// Card 044 host test: Fire (weapon ASC) and Walk (pawn ASC) pressed or released on the downed host pawn.
void c44_refuse(void* host, bool press) {
    const void* weapon = nullptr;
    void* const weapon_asc = c24_weapon_asc(host, &weapon);
    void* const pawn_asc = c21_character_ptr(host, kCharacterAsc);
    const uint64_t fire = fname_cached(g_c43.fire_tag, L"InputTag.Fire");
    const uint64_t walk = fname_cached(g_c43.walk_tag, L"InputTag.Walk");
    if (weapon_asc != nullptr) {
        (press ? detour_asc_tag_pressed : detour_asc_tag_released)(weapon_asc, &fire);
    }
    if (pawn_asc != nullptr) {
        (press ? detour_asc_tag_pressed : detour_asc_tag_released)(pawn_asc, &walk);
    }
    const int32_t ammo = weapon != nullptr ? g_get_ammo_count(weapon) : -1;
    if (press) {
        g_c43t.refuse_ammo = ammo;
        g_c43t.refuse_fires = g_c44_fires[kSideHostOwn];
    }
    log::line("card044: host t=%llu test %s Fire (weapon %s, ASC %p) and Walk (pawn ASC %p) on the downed host: "
              "ammo %d (at the press %d), host-own FireProjectile calls %u (at the press %u)",
              static_cast<unsigned long long>(GetTickCount64()), press ? "pressed" : "released", object_name(weapon).text,
              weapon_asc, pawn_asc, ammo, g_c43t.refuse_ammo, g_c44_fires[kSideHostOwn], g_c43t.refuse_fires);
}

// Card 044 host test, the host revived: the copy goes back to its spot for the host-downed shots (proven clear).
void c44_reviver_out(void* copy, const void* host) {
    const Vec3 to = g_c43t.view_spot;
    const bool moved = g_c43t.view_spot_set && g_set_actor_location(copy, &to, false, nullptr, kTeleportPhysics);
    log::line("%scard044: host test reviver back to the host-downed shot spot (%.0f, %.0f, %.0f): moved %d, "
              "%.0f uu from %s at (%.0f, %.0f, %.0f)",
              moved ? "" : "WARN ", to.x, to.y, to.z, moved ? 1 : 0, vec_dist(actor_location(copy), actor_location(host)),
              object_name(host).text, actor_location(host).x, actor_location(host).y, actor_location(host).z);
}

// Card 044 host test, while the host is downed: the refused tags, then the reviver comes in.
void c44_host_wait(void* pc, void* host, void* copy, ULONGLONG now) {
    const ULONGLONG since = now - g_c43t.host_down_ms;
    if (g_c43t.refuse_step == 0 && since >= kC44RefusePressMs) {
        c44_refuse(host, true);
        g_c43t.refuse_step = 1;
    } else if (g_c43t.refuse_step == 1 && since >= kC44RefuseReleaseMs) {
        c44_refuse(host, false);
        g_c43t.refuse_step = 2;
    } else if (g_c43t.refuse_step == 2 && since >= kC44ReviverInMs) {
        c43_place(pc, copy, host, kC43TestGap);
        g_c43t.refuse_step = 3;
    }
}

// Host test, the host's half: downed and revived by the client, then everyone down.
void c43_test_host(void* world, void* pc, void* host, void* copy, ULONGLONG now) {
    switch (g_c43t.stage) {
    case C43Stage::KillHost:
    case C43Stage::KillHost2:
        if (now < g_c43t.at_ms && g_c43t.hits == 0) {
            return;
        }
        if (g_c43t.hits == 0) {
            c43_place(pc, copy, host, g_c43t.stage == C43Stage::KillHost ? kC44ShotGap : kC43TestGap);
        }
        if (c43_hit_until(world, host, false, now)) {
            const bool first = g_c43t.stage == C43Stage::KillHost;
            if (first) {
                c43_shot(now + 1500, "own downed view (host)");
                g_c43t.host_down_ms = now;
                g_c43t.refuse_step = 0;
                g_c43t.view_spot = actor_location(copy);  // the client's host-downed shots are taken from here
                g_c43t.view_spot_set = true;
            }
            c43_next(first ? C43Stage::WaitHostUp : C43Stage::KillAll, first ? now + 20000 : now);
        }
        return;
    case C43Stage::WaitHostUp:
        c44_host_wait(pc, host, copy, now);
        if (c43_life(host) == C43Life::Up || now >= g_c43t.at_ms) {
            if (c43_life(host) != C43Life::Up) {
                log::line("WARN card043: host test: host not revived in 20 s");
            } else {
                c44_reviver_out(copy, host);
            }
            c43_next(C43Stage::KillHost2, now + 5000);
        }
        return;
    case C43Stage::KillAll:
        if (c43_hit_until(world, copy, true, now)) {
            c43_shot(now + 3000, "game over menu (host)", kC44ShotUi);
            c43_shot(now + 8000, "game over menu (host, later)", kC44ShotUi);
            c43_next(C43Stage::Finish, now + 10000);
        }
        return;
    default:
        if (now >= g_c43t.at_ms) {
            c43_totals("test totals");
            log::line("card043: host test done");
            c43_next(C43Stage::Done, now);
            if (g_c45_test) {
                c45_test_restart(false);
            } else {
                c44_restart();
            }
        }
        return;
    }
}

// Test, one player (SOLO=1): the host's own pawn takes the real-death hits. The gate is off with one player, so
// HandleDeathEvent and StartDeath run vanilla.
void c43_test_solo(void* world, const C43Roster& r, ULONGLONG now) {
    constexpr ULONGLONG kSoloStartMs = kC43TestStartMs + 10000; // past the ~22 s window where self-hits land nothing
    if (g_c43t.stage == C43Stage::WaitFoot && g_c45_test && g_c45_phase > 0) {
        if (c30_find(world).host_on_foot) {
            c45_test_on_foot(r, now, true);
        }
        return;
    }
    if (g_c43t.stage == C43Stage::WaitFoot) {
        if (c30_find(world).host_on_foot) {
            log::line("card043: host test solo (net mode %u, players %u): first hit in %llu ms",
                      net_mode(r.p[0].pawn), r.players, static_cast<unsigned long long>(kSoloStartMs));
            c43_shot(now + kSoloStartMs + 3000, "solo death (host)");
            c43_shot(now + kSoloStartMs + 8000, "solo death (host, later)");
            c43_next(C43Stage::KillAll, now + kSoloStartMs);
        }
        return;
    }
    if (now < g_c43t.at_ms || g_c43t.stage == C43Stage::Done) {
        return;
    }
    if (g_c43t.stage != C43Stage::KillAll) {
        c43_totals("test totals");
        log::line("card043: host test done");
        c43_next(C43Stage::Done, now);
        if (g_c45_test) {
            c45_test_restart(true);
        }
        return;
    }
    // c43_host_step also runs pending hits on a listen server; here only the first hit, or the retries elsewhere.
    const bool due = g_c43.kill_ms != 0 && now >= g_c43.kill_ms && net_mode(r.p[0].pawn) != kNetModeListenServer;
    if ((g_c43.kill_tries == 0 && g_c43.kill_ms == 0) || due) { // kill_ms set, no try: a card 081 mech skip
        c43_kill_host(world, r, now);
    } else if (g_c43.kill_ms == 0) {
        c43_next(C43Stage::Finish, now + 10000);
    }
}

void c43_test_host_step(void* world, void* pc, ULONGLONG now) {
    if (g_c44.test_over) {
        return; // card 044: the restart was driven; the next map runs no test
    }
    const C43Roster r = c43_roster(world);
    void* const host = r.p[0].pawn;
    void* copy = nullptr;
    for (int32_t s = 1; s < kC43Max && copy == nullptr; ++s) {
        copy = r.p[s].pawn != nullptr && is_a(r.p[s].pawn, g_player_character_class()) ? r.p[s].pawn : nullptr;
    }
    if (host != nullptr && copy == nullptr && r.players == 1) {
        c43_test_solo(world, r, now);
        return;
    }
    if (g_c43t.stage == C43Stage::Done || host == nullptr || copy == nullptr) {
        return;
    }
    if (g_c43t.stage == C43Stage::WaitFoot) {
        const C30Pawns p = c30_find(world);
        if (p.host_on_foot && p.copy_on_foot && g_c45_test) {
            c45_test_on_foot(r, now, false);
        } else if (p.host_on_foot && p.copy_on_foot) {
            log::line("card043: host test: both on foot; first hit in %llu ms",
                      static_cast<unsigned long long>(g_c43_start_ms));
            c43_next(C43Stage::KillCopy, now + g_c43_start_ms);
        }
        return;
    }
    if (g_c43t.stage == C43Stage::KillCopy && now < g_c43t.at_ms && g_c43t.hits == 0) {
        return;
    }
    if (g_c43t.stage < C43Stage::KillHost) {
        c43_test_copy(world, pc, host, copy, now);
    } else {
        c43_test_host(world, pc, host, copy, now);
    }
}

void c43_client_key(void* pc, bool down, ULONGLONG now) {
    autoinput::key_event(pc, L"E", down);
    g_c43t.key_held = down;
    g_c43t.key_ms = now;
    log::line("card043: client t=%llu test interact key %s", static_cast<unsigned long long>(now), down ? "down" : "up");
}

// Client test: shots of the own downed view, the host's Corpse and StandUp and game over; the held revive.
// Card 044 client test, while the own pawn is downed: Fire 1 then Fire 0 sent to the host.
void c44_client_fire(void* pc, ULONGLONG now) {
    if (g_c43t.fire_ms == 0 || now < g_c43t.fire_ms || g_c43t.fire_step >= 2) {
        return;
    }
    const uint32_t arg = g_c43t.fire_step == 0 ? 1u : 0u;
    const bool sent = chan_to_host(pc, ChanOp::Fire, arg);
    log::line("%scard044: client t=%llu test relayed Fire %u from the downed own pawn: sent %d",
              sent ? "" : "WARN ", static_cast<unsigned long long>(now), arg, sent ? 1 : 0);
    ++g_c43t.fire_step;
    g_c43t.fire_ms = now + kC44FireGapMs;
}

void c43_test_client_step(void* world, void* pc, ULONGLONG now) {
    if (g_c44.test_over) {
        return;
    }
    c44_client_fire(pc, now);
    const int32_t self = c43_find_pawn(c23_own_pawn(pc));
    const int32_t host = c43_find_pawn(c43_client_host_pawn(world));
    const C43Life host_state = host >= 0 ? g_c43.e[host].state : C43Life::Up;
    if (!g_c43t.own_shot && self >= 0 && c43_down_state(g_c43.e[self].state)) {
        g_c43t.own_shot = true;
        g_c43t.fire_ms = now + kC44FireAfterMs;
        c43_shot(now + 1500, "own downed view (client)");
    }
    if (g_c43t.host_down_ms == 0 && host_state == C43Life::Downed) {
        g_c43t.host_down_ms = now;
        c43_shot(now + 500, "remote Death01 +0.5 s (host downed)", kC44ShotAim);
        c43_shot(now + 1600, "remote Corpse01 (host downed)", kC44ShotAim);
    }
    if (g_c43t.host_down_ms != 0 && !g_c43t.host_shot && !g_c43t.key_held && !g_c45_test &&
        now >= g_c43t.host_down_ms + kC43ClientKeyMs) {
        void* const target = c43_client_host_pawn(world);
        const void* const own = c23_own_pawn(pc);
        if (target != nullptr && own != nullptr) {
            c25_face(pc, actor_location(own), actor_location(target));
        }
        g_c43t.host_shot = true;
        c43_client_key(pc, true, now);
    }
    if (g_c43t.key_held && (host_state == C43Life::Up || now >= g_c43t.key_ms + kC43ClientHoldMs)) {
        if (host_state == C43Life::Up) {
            c43_shot(now + 300, "remote StandUp +0.3 s (host revived)", kC44ShotAim);
            c43_shot(now + 2000, "remote standing +2 s (host revived)", kC44ShotAim);
        }
        c43_client_key(pc, false, now);
    }
    if (!g_c43t.over_shot && host_state == C43Life::Dead) {
        g_c43t.over_shot = true;
        c43_shot(now + 5000, "client death screen", kC44ShotUi);
        c43_shot(now + 9000, "client death screen (later)", kC44ShotUi);
        if (!g_c45_test) {
            g_c44.done_ms = now + kC44ClientDoneMs; // outlives a restart's map change
        }
    }
}

// Card 330 step 2: the sub-steps are timed by name too (c43_step was 367.5 us mean on the client, b50r2, with no
// level walk through c23_collect).
void c43_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    if (g_c44.done_ms != 0 && now >= g_c44.done_ms) {
        g_c44.done_ms = 0;
        g_c44.test_over = true;
        c43_totals("test totals");
        log::line("card044: client test done");
    }
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    if (pc == nullptr) {
        return;
    }
    C330_STEP(c45_respawn_check(pc, now));
    if (g_client_session) {
        C330_STEP(c43_client_step(pc, now));
        C330_STEP(c44_client_tick(pc, now));
        C330_STEP(c133_client_step(now)); // card 133
    } else if (net_mode(pc) == kNetModeListenServer) {
        C330_STEP(c43_host_step(world, now));
    }
    C330_STEP(c227_recheck(world, now)); // card 228 item 1: held presences after a down, death or exit
    C330_STEP(c495_recheck(world, now)); // card 495: a held per-player pickup base after a down, death or exit
    C330_STEP(c46_step(world, pc, now));
    C330_STEP(c50_step(world, pc, now)); // card 050: weapon sync
    if (g_c46_test) {
        c43_shot_step(world, pc, now); // the card 046 driver queues its shots here; the card 043 stages stay off
        return;
    }
    if (!g_c43_test) {
        return;
    }
    if (g_client_session) {
        c43_test_client_step(world, pc, now);
    } else {
        c43_test_host_step(world, pc, now); // standalone too: the solo test
    }
    c43_shot_step(world, pc, now);
}

IsBelowKillThresholdFn g_orig_is_below_kill_threshold = nullptr;
GetChapterKillZFn g_get_chapter_kill_z = nullptr;

constexpr uint32_t kC46Lines = 120;

C46State g_c46{};

bool c46_line() {
    return note_event_cap(g_c46.lines, kC46Lines, "card046");
}

void c46_arm_map() {
    g_c46 = C46State{};
}

// StartDeath detours: the card's "no StartDeath" proof (test runs only).
void c46_note_start_death(const void* pawn) {
    if (!g_c46_test) {
        return;
    }
    ++g_c46.start_deaths;
    if (c46_line()) {
        log::line("card046: %s t=%llu StartDeath %u on %s (%s) at z %.0f, KillZ %.0f", side(),
                  static_cast<unsigned long long>(GetTickCount64()), g_c46.start_deaths, object_name(pawn).text,
                  kSprintSideText[sprint_side(pawn)], actor_location(pawn).z, static_cast<double>(c46_kill_z(pawn)));
    }
}

static_assert(sizeof(TagContainerRaw) == 0x20, "FGameplayTagContainer is 0x20 bytes");

HealthDealDamageFn g_health_deal_damage = nullptr;
// HealthSystemTags::Health_DamageIgnoreArmour: IsIgnoreArmour loads its first 8 bytes (the tag FName) with
// `mov rcx, [rip+...]` at 0x144A3D204.
constexpr size_t kIgnoreArmourTagRva = 0x941EC88;
// HealthSystemTags::Health_DamageIgnoreModifiers (PDB public 0003:4713616, IgnoreArmour + 8).
constexpr size_t kIgnoreModifiersTagRva = 0x941EC90;

// Armour and MaxArmour current values of the pawn's health set (-1 when unknown).
C48Armour c48_armour(const void* component) {
    static int32_t armour_off = -2;
    static int32_t max_off = -2;
    if (armour_off == -2) {
        armour_off = c30_set_offset("Armour");
        max_off = c30_set_offset("MaxArmour");
        log::line("card048: %s health set offsets: Armour 0x%X, MaxArmour 0x%X", side(), armour_off, max_off);
    }
    const void* const set = component != nullptr ? read_ptr(component, kHealthComponentSet) : nullptr;
    if (set == nullptr || armour_off < 0 || max_off < 0) {
        return C48Armour{-1.0f, -1.0f};
    }
    return C48Armour{read_f32(set, static_cast<size_t>(armour_off) + kAttrDataCurrent),
                     read_f32(set, static_cast<size_t>(max_off) + kAttrDataCurrent)};
}

// The armour- and modifier-ignoring hit: false (nothing called) when a tag is unset or the allocation fails.
// Run card048k with only IgnoreArmour: Armour held but Health fell 3 of 25 (the damage modifier stage, skipped
// only by Health.DamageIgnoreModifiers, which IsIgnoreDamageModification 0x04A3D250 reads at 0x14941EC90).
bool c48_deal_ignore_armour(void* pawn, float amount) {
    uint64_t tags[2] = {};
    memcpy(&tags[0], g_exe_base + kIgnoreArmourTagRva, sizeof(tags[0]));
    memcpy(&tags[1], g_exe_base + kIgnoreModifiersTagRva, sizeof(tags[1]));
    if (tags[0] == 0 || tags[1] == 0) {
        log::line("WARN card048: host Health.DamageIgnoreArmour 0x%llX or Health.DamageIgnoreModifiers 0x%llX is 0",
                  static_cast<unsigned long long>(tags[0]), static_cast<unsigned long long>(tags[1]));
        return false;
    }
    void* const data = g_memory_malloc(sizeof(tags), 0);
    if (data == nullptr) {
        log::line("WARN card048: host FMemory::Malloc of the tag array failed");
        return false;
    }
    memcpy(data, tags, sizeof(tags));
    TagContainerRaw source{data, 2, 2, nullptr, 0, 0};
    TagContainerRaw target{};
    g_health_deal_damage(pawn, pawn, amount, &source, &target);
    return true;
}

// --- Card 048 step 1: checkpoint audit (log only) and step 3: the client's cursor after a respawn in place ---
// On the host every checkpoint restore runs inside ASTSaveSystemEngine::LoadFromSlot: USTSaveComponent::Load ->
// LoadOwnerVariables_Pawn, then FullyLoaded -> ASTCharacter::ActorFullyLoaded -> LoadAbilitySystemComponent ->
// USTWeaponInventory::LoadInventory. These detours log each step for player pawns; every original runs unchanged.

constexpr uint32_t kC48Lines = 160;          // audit lines per process
constexpr uint32_t kC48ObjectiveLines = 64;  // AddObjectiveObject lines per process
constexpr int32_t kC48MaxObjectiveSlots = 256;

constexpr uint8_t kC48ContextOptions = 0x1;  // FModifyContextOptions: bIgnoreAllPressedKeysUntilRelease (default)

PawnVoidFn g_orig_load_asc = nullptr;
LoadOwnerPawnFn g_orig_load_owner_pawn = nullptr;
GetSaveIdFn g_get_save_id = nullptr;
ActorFullyLoadedFn g_orig_actor_fully_loaded = nullptr;
ExecFn g_orig_exec_complete_death = nullptr;
AddObjectiveFn g_orig_add_objective = nullptr;
SetObjectiveMapFn g_orig_set_objective_map = nullptr;
LoadSettingsFn g_orig_load_settings = nullptr;
RemoveContextFn g_remove_mapping_context = nullptr;

C48State g_c48{}; // game thread

bool c48_line() {
    return note_event_cap(g_c48.lines, kC48Lines, "card048 audit");
}

// One audit line for a pawn: save id, Health, Armour, position, inventory.
void c48_pawn_line(const char* what, const void* pawn) {
    if (pawn == nullptr || !c48_line()) {
        return;
    }
    void* component = nullptr;
    const float health = c21_health(static_cast<const uint8_t*>(pawn), &component);
    const C48Armour a = c48_armour(component);
    const Vec3 at = actor_location(pawn);
    uint32_t id[4] = {};
    g_get_save_id(id, pawn);
    char inv[320];
    c48_inventory_text(pawn, inv, sizeof(inv));
    log::line("card048: %s t=%llu %s: %s (%s) save id %08X-%08X-%08X-%08X, Health %.1f, Armour %.1f, at (%.0f, "
              "%.0f, %.0f), %s",
              side(), static_cast<unsigned long long>(GetTickCount64()), what, object_name(pawn).text,
              kSprintSideText[sprint_side(pawn)], id[0], id[1], id[2], id[3], static_cast<double>(health),
              static_cast<double>(a.armour), at.x, at.y, at.z, inv);
}

void detour_load_asc(void* pawn) {
    ++g_c48.asc_loads;
    char chain[96];
    caller_chain(chain, sizeof(chain));
    if (c48_line()) {
        log::line("card048: %s LoadAbilitySystemComponent #%u on %s; callers %s", side(), g_c48.asc_loads,
                  object_name(pawn).text, chain);
    }
    c48_pawn_line("LoadAbilitySystemComponent before", pawn);
    g_orig_load_asc(pawn);
    c48_pawn_line("LoadAbilitySystemComponent after", pawn);
    c143_after_load_asc(pawn); // card 143: a row saved while downed comes back at the revive Health
    c50_after_load_asc(pawn); // card 050: a server copy's weapons go to its client again
}

// Card 380 (run c379b65): the first int of a saved pawn row is "held by a controller at save time" (exe+0x520B861,
// read through the archive's fast-path buffer at archive+0x8: {current, end}). Returns 1 or 0, -1 = not readable.
// Run c379b66: the in-place checkpoint load's archive has no fast-path buffer, so the engine reads through the
// virtual Serialize. The peek does the same and seeks back: FArchive vtable Tell +0x28, Serialize +0x180, Seek +0x1B0
// (slots read from the FMemoryReader vtables in the exe).
int c380_peek_slow(void* archive) {
    void* const* const vtable = static_cast<void* const*>(read_ptr(archive, 0));
    if (vtable == nullptr) {
        return -1;
    }
    const auto tell = reinterpret_cast<int64_t (*)(void*)>(vtable[0x28 / 8]);
    const auto serialize = reinterpret_cast<void (*)(void*, void*, int64_t)>(vtable[0x180 / 8]);
    const auto seek = reinterpret_cast<void (*)(void*, int64_t)>(vtable[0x1B0 / 8]);
    const int64_t pos = tell(archive);
    int32_t value = 0;
    serialize(archive, &value, sizeof(value));
    seek(archive, pos);
    if (tell(archive) != pos) {
        log::line("WARN card380: host archive %p did not seek back to %lld after the peek", archive,
                  static_cast<long long>(pos));
    }
    return value != 0 ? 1 : 0;
}

int c380_saved_possessed(void* archive) {
    const void* const* const fast = static_cast<const void* const*>(read_ptr(archive, 0x8));
    if (fast == nullptr) {
        return c380_peek_slow(archive);
    }
    const uint8_t* const cur = static_cast<const uint8_t*>(fast[0]);
    if (cur == nullptr || cur + sizeof(int32_t) > static_cast<const uint8_t*>(fast[1])) {
        return c380_peek_slow(archive);
    }
    int32_t value = 0;
    memcpy(&value, cur, sizeof(value));
    return value != 0 ? 1 : 0;
}

// Card 380: LoadOwnerVariables_Pawn gives a saved player pawn to the PC at the saved PC list index. A save taken with
// a client and loaded with fewer players has no PC there: the pawn stayed in the world, loaded and armed, with no
// controller (an extra trooper). The host destroys it. A row saved with no controller (the player sat in a mech or
// the dropship) is left alone.
void c380_orphan(void* pawn, int saved_possessed) {
    if (g_client_session || read_ptr(pawn, kPawnController) != nullptr) {
        return;
    }
    if (saved_possessed == -1) { // the peek had no archive state to read
        log::line("WARN card380: host t=%llu saved player pawn %p has no controller after the load, but its row's "
                  "possessed flag was not readable (no archive fast-path pointer): left in the world",
                  static_cast<unsigned long long>(GetTickCount64()), pawn);
        return;
    }
    if (saved_possessed != 1) {
        return;
    }
    log::line("card380: host t=%llu saved player pawn %p has no PC at its saved list index (fewer players than at the "
              "save): destroyed",
              static_cast<unsigned long long>(GetTickCount64()), pawn);
    detour_c169_destroy(pawn);
}

void detour_load_owner_pawn(void* save, void* pawn, void* world, void* archive) {
    const bool player = is_a(pawn, g_player_character_class());
    const int saved_possessed = player ? c380_saved_possessed(archive) : -1;
    if (player) {
        c48_pawn_line("LoadOwnerVariables_Pawn before", pawn);
    }
    g_orig_load_owner_pawn(save, pawn, world, archive);
    if (player) {
        c48_pawn_line("LoadOwnerVariables_Pawn after", pawn);
        c380_orphan(pawn, saved_possessed);
    }
}

void detour_actor_fully_loaded(void* actor, void* save) {
    if (is_a(actor, g_player_character_class()) && c48_line()) {
        log::line("card048: %s t=%llu ActorFullyLoaded on %s (%s), save component %s, caller exe+0x%llX", side(),
                  static_cast<unsigned long long>(GetTickCount64()), object_name(actor).text,
                  kSprintSideText[sprint_side(actor)], object_name(save).text, exe_rva(_ReturnAddress()));
    }
    g_orig_actor_fully_loaded(actor, save);
}

// The Blueprint caller of CompleteDeath: Stack.Node should name CompleteDeath (checks the FFrame offsets), and
// PreviousFrame's Node the calling function.
void detour_exec_complete_death(void* context, void* frame, void* result) {
    const void* const prev = read_ptr(frame, kFramePrevious);
    char chain[320];
    caller_chain(chain, sizeof(chain), kMaxStackFrames);
    if (c48_line()) {
        log::line("card048: %s t=%llu execCompleteDeath: Context %s (%s), Stack.Node %s, Stack.Object %s, "
                  "PreviousFrame %p Node %s Object %s; callers %s",
                  side(), static_cast<unsigned long long>(GetTickCount64()), object_name(context).text,
                  class_name(context).text, object_name(read_ptr(frame, kFrameNode)).text,
                  object_name(read_ptr(frame, kFrameObject)).text, prev,
                  object_name(prev != nullptr ? read_ptr(prev, kFrameNode) : nullptr).text,
                  object_name(prev != nullptr ? read_ptr(prev, kFrameObject) : nullptr).text, chain);
    }
    g_orig_exec_complete_death(context, frame, result);
}

void c48_complete_death(const void* pawn) {
    char chain[96];
    caller_chain(chain, sizeof(chain));
    if (c48_line()) {
        log::line("card048: %s t=%llu CompleteDeath on %s (%s); callers %s", side(),
                  static_cast<unsigned long long>(GetTickCount64()), object_name(pawn).text,
                  kSprintSideText[sprint_side(pawn)], chain);
    }
}

void detour_add_objective(void* subsystem, void* objective) {
    ++g_c48.objectives;
    char chain[96];
    caller_chain(chain, sizeof(chain));
    if (note_event_cap(g_c48.objective_lines, kC48ObjectiveLines, "card048 AddObjectiveObject")) {
        log::line("card048: %s t=%llu AddObjectiveObject #%u: %s (%s) on %s; callers %s", side(),
                  static_cast<unsigned long long>(GetTickCount64()), g_c48.objectives, object_name(objective).text,
                  class_name(objective).text, object_name(subsystem).text, chain);
    }
    g_orig_add_objective(subsystem, objective);
}

void detour_set_objective_map(void* subsystem, void* map) {
    SparseView v{};
    const bool ok = map != nullptr && sparse_view(static_cast<const uint8_t*>(map), kC48MaxObjectiveSlots, v);
    if (c48_line()) {
        log::line("card048: %s t=%llu SetObjectiveMapPostSave on %s: %d slots (%s), caller exe+0x%llX", side(),
                  static_cast<unsigned long long>(GetTickCount64()), object_name(subsystem).text, ok ? v.count : -1,
                  ok ? "read" : "not read", exe_rva(_ReturnAddress()));
    }
    g_orig_set_objective_map(subsystem, map);
    c49_snapshot(subsystem); // card 049, host
}

void detour_load_settings(void* mode, const void* data) {
    char chain[96];
    caller_chain(chain, sizeof(chain));
    if (c48_line()) {
        log::line("card048: %s t=%llu ASTGameplayGameMode::LoadSettingsFromSave on %s (%s); callers %s", side(),
                  static_cast<unsigned long long>(GetTickCount64()), object_name(mode).text, class_name(mode).text,
                  chain);
    }
    g_orig_load_settings(mode, data);
}

// Every map and every respawn in place (both sides): the objective count since the last reset.
void c48_arm_map() {
    log::line("card048: %s t=%llu per-map reset: %u AddObjectiveObject calls since the last reset, %u "
              "LoadAbilitySystemComponent calls so far",
              side(), static_cast<unsigned long long>(GetTickCount64()), g_c48.objectives, g_c48.asc_loads);
    g_c48.objectives = 0;
}

// The pc's applied mapping context named `name` (null when not mapped); `iface_out` gets the subsystem interface.
const void* c48_find_context(const void* pc, const char* name, void** iface_out) {
    const void* const local_player = g_get_local_player(pc);
    auto* const subsystem = local_player != nullptr ? static_cast<uint8_t*>(g_get_enhanced_input(local_player)) : nullptr;
    *iface_out = subsystem != nullptr ? subsystem + kSubsystemInputInterface : nullptr;
    if (*iface_out == nullptr) {
        return nullptr;
    }
    const auto get =
        reinterpret_cast<GetPlayerInputFn>((*static_cast<void* const* const*>(*iface_out))[kIfaceGetPlayerInput]);
    const auto* const input = static_cast<const uint8_t*>(get(*iface_out));
    SparseView v{};
    if (input == nullptr || !sparse_view(input + kPlayerInputAppliedContexts, kMaxAppliedContexts, v)) {
        return nullptr;
    }
    for (int32_t i = 0; i < v.count; ++i) {
        const void* const context =
            sparse_allocated(v, i) ? read_ptr(v.data + static_cast<size_t>(i) * kAppliedContextBytes, 0) : nullptr;
        if (context != nullptr && strcmp(object_name(context).text, name) == 0) {
            return context;
        }
    }
    return nullptr;
}

unsigned c48_capture_mode(void* pc) {
    void* const world = g_actor_get_world(pc);
    const auto* const viewport = world != nullptr ? static_cast<const uint8_t*>(g_world_game_viewport(world)) : nullptr;
    return viewport != nullptr ? viewport[kViewportCaptureMode] : 0xFFu;
}

// Client respawn in place, after the GameOnly input mode: cursor off, the death screen's UI context removed.
void c48_client_cursor(void* pc) {
    const unsigned before = cursor_bit(pc);
    const unsigned capture = c48_capture_mode(pc);
    g_orig_set_show_mouse_cursor(pc, false);
    void* iface = nullptr;
    const void* const imc = c48_find_context(pc, "IMC_DummyUI_KBM", &iface);
    if (imc != nullptr) {
        const uint8_t options[8] = {kC48ContextOptions};
        g_remove_mapping_context(iface, imc, options);
    }
    const bool still = c48_find_context(pc, "IMC_DummyUI_KBM", &iface) != nullptr;
    log::line("%scard048: client cursor: bShowMouseCursor %u -> %u, viewport capture %u -> %u; IMC_DummyUI_KBM %s%s",
              cursor_bit(pc) == 0 && !still ? "" : "WARN ", before, cursor_bit(pc), capture, c48_capture_mode(pc),
              imc != nullptr ? "was mapped, removed" : "not mapped", still ? ", STILL mapped" : "");
}

// ---- Card 156: the client's one back-to-Up owner ----
// The death UI goes up through the HUD's own BP "Set Input Mode UI" (BP_HUD_Manager_C): UIOnly, the cursor shown and
// ASTHUDGameplay's UIMode set. Its OnMouseChange then writes bShowMouseCursor straight from UIMode, so an engine
// GameOnly alone leaves the cursor coming back on the next mouse event (playtest 2026-11-08: bShowMouseCursor 1 again
// 5 s after the respawn reset, with no setter call). The inverse is the HUD's own "Set Input Mode Game".
// IMC_DummyUI_KBM (added with the death screen) lives in the LocalPlayer's input subsystem, which outlives a map load.
constexpr size_t kPcMyHud = 0x358;          // APlayerController::MyHUD (GetHUD 0x4045D90)
constexpr size_t kHudUiMode = 0x3C8;        // ASTHUDGameplay UIMode byte (IsUIMode 0x5209620, SetUIMode 0x521CA00)
constexpr ULONGLONG kC156CheckMs = 5000;
constexpr uint16_t kC156ParmsBytes = 64;
constexpr unsigned kC156CaptureGame = 1;    // viewport capture mode after GameOnly (card 048 logs)

struct C156State {
    ULONGLONG check_ms; // client: when the +5 s check runs (0 = none)
    bool deferred;      // client: the check waited on the card 135 screen (logged once per check)
    const char* kind;   // the transition the check follows
    uint64_t fn_name;   // FName "Set Input Mode Game" (cached)
};
C156State g_c156{};

// The pc's HUD when it is the game's BP HUD manager (an ASTHUDGameplay), else null.
uint8_t* c156_hud(void* pc) {
    auto* const hud = static_cast<uint8_t*>(read_ptr(pc, kPcMyHud));
    return hud != nullptr && strncmp(class_name(hud).text, "BP_HUD_Manager", 14) == 0 ? hud : nullptr;
}

unsigned c156_ui_mode(void* pc) {
    const uint8_t* const hud = c156_hud(pc);
    return hud != nullptr ? hud[kHudUiMode] : 0xFFu;
}

int32_t c156_live_screens() {
    int32_t live = 0;
    for (int32_t i = 0; i < g_c45.screen_count; ++i) {
        live += g_weak_get(&g_c45.screens[i]) != nullptr ? 1 : 0;
    }
    return live;
}

// The HUD's own "Set Input Mode Game", as the game calls it leaving a menu. False (logged) when it cannot be called.
bool c156_hud_game_mode(void* pc) {
    uint8_t* const hud = c156_hud(pc);
    auto* const fn = hud != nullptr && fname_cached(g_c156.fn_name, L"Set Input Mode Game") != 0
                         ? static_cast<uint8_t*>(g_find_function(hud, g_c156.fn_name))
                         : nullptr;
    uint16_t size = 0;
    if (fn != nullptr) {
        memcpy(&size, fn + kFunctionParmsSize, sizeof(size));
    }
    if (fn == nullptr || size > kC156ParmsBytes) {
        log::line("WARN card156: client HUD %s (%p): Set Input Mode Game %p, parms size %u: not called",
                  hud != nullptr ? class_name(hud).text : "(not a BP_HUD_Manager)", static_cast<void*>(hud),
                  static_cast<void*>(fn), size);
        return false;
    }
    alignas(16) uint8_t parms[kC156ParmsBytes] = {};
    g_orig_process_event(hud, fn, parms);
    return true;
}

// Client: one owner for every return to Up. `restore`: the death UI was up (death screens, UIMode), so the screens
// go, input GameOnly, the HUD's UIMode off through its own function, the cursor off and IMC_DummyUI_KBM removed.
// Otherwise (a revive with no death UI) it only arms the check. The check runs kC156CheckMs later either way.
void c156_client_up(void* pc, const char* kind, bool restore, ULONGLONG now) {
    const unsigned ui_before = c156_ui_mode(pc);
    const int32_t tracked = g_c45.screen_count;
    bool called = false;
    if (restore) {
        c45_remove_screens(pc, now);
        called = ui_before == 1 && c156_hud_game_mode(pc);
        c48_client_cursor(pc);
    }
    log::line("%scard156: client t=%llu back to Up (%s): %s; %d death screens tracked, HUD UIMode %u -> %u, Set Input "
              "Mode Game %s, bShowMouseCursor %u",
              restore && c156_ui_mode(pc) == 1 ? "WARN " : "", static_cast<unsigned long long>(now), kind,
              restore ? "restore" : "check only", tracked, ui_before, c156_ui_mode(pc), called ? "called" : "not called",
              cursor_bit(pc));
    g_c156.check_ms = now + kC156CheckMs;
    g_c156.kind = kind;
}

// Every new map: a check armed on the map being left is dropped (c154-r1: armed in the hangar 5 s before the host's
// next travel, it ran late during the mission load and logged that map's half-built HUD as a WARN).
void c156_arm_map() {
    g_c156.check_ms = 0;
}

// Client, every tick: the +5 s check after a return to Up.
void c156_client_check(void* pc, ULONGLONG now) {
    if (g_c156.check_ms == 0 || now < g_c156.check_ms) {
        return;
    }
    // Card 135: the client's press-to-start screen sets UI input and the cursor on purpose; the check runs once the
    // screen closes.
    if (c135_screen_up()) {
        if (!g_c156.deferred) {
            g_c156.deferred = true;
            log::line("card156: client t=%llu back to Up (%s) +5 s check deferred: the card 135 screen is up",
                      static_cast<unsigned long long>(now), g_c156.kind);
        }
        return;
    }
    g_c156.check_ms = 0;
    g_c156.deferred = false;
    // The hangar (3D_Frontend) has no gameplay HUD and shows the cursor on purpose: nothing to hold to game input.
    // c52_step set menu_map on this world's first tick, seconds before this check is due.
    if (g_c52.menu_map) {
        log::line("card156: client t=%llu back to Up (%s) +5 s: not checked on the menu map, bShowMouseCursor %u",
                  static_cast<unsigned long long>(now), g_c156.kind, cursor_bit(pc));
        return;
    }
    void* iface = nullptr;
    const bool mapped = c48_find_context(pc, "IMC_DummyUI_KBM", &iface) != nullptr;
    const int32_t live = c156_live_screens();
    // Card 146 r2: the own pawn went down again before the check (a map load during an all-down): its death UI is
    // expected then, so only an Up pawn is held to the game-input state.
    const int32_t own = c43_find_pawn(c23_own_pawn(pc));
    const bool up = own < 0 || g_c43.e[own].state == C43Life::Up;
    const bool ok = !up || (cursor_bit(pc) == 0 && c48_capture_mode(pc) == kC156CaptureGame && c156_ui_mode(pc) != 1 &&
                            !mapped && live == 0);
    log::line("%scard156: client t=%llu back to Up (%s) +5 s: own pawn %s%s, bShowMouseCursor %u, viewport capture %u, "
              "HUD UIMode %u, IMC_DummyUI_KBM %s, death screens live %d",
              ok ? "" : "WARN ", static_cast<unsigned long long>(now), g_c156.kind,
              own < 0 ? "Up (no entry)" : kC43LifeText[static_cast<size_t>(g_c43.e[own].state)],
              up ? "" : " (death UI expected, not checked)", cursor_bit(pc), c48_capture_mode(pc), c156_ui_mode(pc),
              mapped ? "mapped" : "not mapped", live);
}

constexpr uint32_t kC79WatchLines = 48;

constexpr ULONGLONG kC79KillGapMs = 1000;
constexpr uint32_t kC79KillTries = 5;
constexpr uint32_t kC79BugHits = 2;         // test kill: hits from the nearest bug before the mech's own
constexpr float kC79MechMaxHealth = 400.0f; // the pod mech's MaxHealth when the health set cannot be read
constexpr double kC79DownRange = 600.0;     // down test: the pawn is this close to a mech
constexpr float kC79OutBleed = 0.5f;        // down test, host: bleedout left, so the pawn goes Out at once

C79State g_c79{};
C79DeathFn g_orig_c79_start_death = nullptr;
C79MechFn g_orig_c79_death_complete = nullptr;
C79MechFn g_orig_c79_self_destruct = nullptr;
C79MechFn g_orig_c79_sd_init = nullptr;
C79MechFn g_orig_c79_sd_complete = nullptr;

float c79_health(const void* pawn) {
    void* component = nullptr;
    // Only a player character (soldier or mech) has HealthComponent at +0x688: the dropship pawn does not (c79a
    // fault at travel).
    return is_a(pawn, g_player_character_class()) ? c21_health(static_cast<const uint8_t*>(pawn), &component) : -1.0f;
}

// One line of a mech's state at a step of its destruction.
void c79_mech_state(const char* what, const char* when, const void* mech) {
    if (!note_event_cap(g_c79.lines, kC79Lines, "card079")) {
        return;
    }
    const void* const pc = g_weak_get(static_cast<const uint8_t*>(mech) + kMechController);
    const void* const pc_pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    const void* const pilot = c75_leave_soldier(mech);
    log::line("card079: %s t=%llu %s %s: mech %s (%s) Health %.1f, controller %s; pilot %s Health %.1f, bHidden %d; "
              "weak PC %s (its pawn %s, Health %.1f)",
              side(), static_cast<unsigned long long>(GetTickCount64()), what, when, object_name(mech).text,
              kSprintSideText[sprint_side(mech)], static_cast<double>(c79_health(mech)),
              object_name(read_ptr(mech, kPawnController)).text, object_name(pilot).text,
              static_cast<double>(c79_health(pilot)), pilot != nullptr ? c29_bool(pilot, kC29Hidden) : -1,
              object_name(pc).text, object_name(pc_pawn).text, static_cast<double>(c79_health(pc_pawn)));
}

void detour_c79_start_death(void* mech, const void* damage_event) {
    c79_mech_state("mech StartDeath", "before", mech);
    g_orig_c79_start_death(mech, damage_event);
    c79_mech_state("mech StartDeath", "after", mech);
}

void detour_c79_death_complete(void* mech) {
    c591_death_note(mech, "before");
    c79_mech_state("mech DeathComplete", "before", mech);
    g_orig_c79_death_complete(mech);
    c79_mech_state("mech DeathComplete", "after", mech);
    c591_death_note(mech, "after");
}

void detour_c79_self_destruct(void* mech) {
    c79_mech_state("StartSelfDestruction", "before", mech);
    g_orig_c79_self_destruct(mech);
    c79_mech_state("StartSelfDestruction", "after", mech);
}

void detour_c79_sd_init(void* mech) {
    c79_mech_state("OnSelfDestructSequenceInitialized", "before", mech);
    g_orig_c79_sd_init(mech);
    c79_mech_state("OnSelfDestructSequenceInitialized", "after", mech);
}

void detour_c79_sd_complete(void* mech) {
    c79_mech_state("OnSelfDestructSequenceComplete", "before", mech);
    g_orig_c79_sd_complete(mech);
    c79_mech_state("OnSelfDestructSequenceComplete", "after", mech);
}

// Card 591 diag (UBWCOOP_C79_LOG=1, host): the host crash at DeathComplete +0x7A reads a null mech+0xC20 (the body
// mesh) about 3 s after the 60 s abandoned-mech self-destruct eject. These rows name what nulls it: the body pointer
// at each LeaveMech, on every change, every 100 ms after an eject, at each DeathComplete call, and every
// DestroyComponent whose component's outer is a mech, with its caller. Log only; nothing here changes the game.
constexpr size_t kC591Body = 0xC20;            // ASTPlayerMechBase body mesh (attributes.cpp kMechSecondMesh)
constexpr uint32_t kC591Lines = 200;
constexpr ULONGLONG kC591PollMs = 100;
constexpr ULONGLONG kC591LeaveWatchMs = 75000; // the 60 s abandoned-mech timer plus the 3 s eject, with margin
constexpr ULONGLONG kC591EjectWatchMs = 10000;
constexpr ULONGLONG kC591AfterDeathMs = 500; // the mech actor may be destroyed after DeathComplete

struct C591State {
    const void* mech;      // the mech watched since its last LeaveMech; nullptr = none
    const void* world;     // the world at the first step of the watch; another world ends it unread (map change)
    const void* last_body; // its +0xC20 at the last row or poll
    ULONGLONG end_ms;      // the watch ends here
    ULONGLONG eject_ms;    // the eject LeaveMech (no controller); 0 = only a normal leave so far
    ULONGLONG next_ms;     // the next 100 ms poll row after an eject
    uint32_t lines;
    uint32_t deaths;       // DeathComplete calls this session, any mech
    uint32_t mech_deaths;  // DeathComplete calls on the watched mech
};
C591State g_c591{};
C409DestroyFn g_orig_c591_destroy = nullptr;

unsigned long long c591_caller_rva(const void* ra, const char** module) {
    const unsigned long long exe = exe_rva(ra);
    if (exe != 0) {
        *module = "exe";
        return exe;
    }
    *module = "dwmapi";
    return static_cast<unsigned long long>(static_cast<const uint8_t*>(ra) -
                                           reinterpret_cast<const uint8_t*>(&__ImageBase));
}

void c591_body_row(const char* what, const void* mech, ULONGLONG now) {
    const void* const body = read_ptr(mech, kC591Body);
    g_c591.last_body = body;
    if (!note_event_cap(g_c591.lines, kC591Lines, "card591")) {
        return;
    }
    log::line("card591: %s t=%llu %s: mech %s %p body +0xC20 %p (%s %s), eject %s%llu ms ago, mech DeathComplete "
              "calls %u (all mechs %u)",
              side(), static_cast<unsigned long long>(now), what, object_name(mech).text, mech, body,
              object_name(body).text, class_name(body).text, g_c591.eject_ms != 0 ? "" : "none, ",
              static_cast<unsigned long long>(g_c591.eject_ms != 0 ? now - g_c591.eject_ms : 0), g_c591.mech_deaths,
              g_c591.deaths);
}

void c591_death_note(const void* mech, const char* when) {
    const ULONGLONG now = GetTickCount64();
    if (when[0] == 'b') {
        ++g_c591.deaths;
        g_c591.mech_deaths += mech == g_c591.mech ? 1u : 0u;
    }
    if (mech == g_c591.mech) {
        g_c591.end_ms = now + kC591AfterDeathMs;
    }
    char what[48];
    snprintf(what, sizeof(what), "DeathComplete %s", when);
    c591_body_row(what, mech, now);
}

void c591_destroy_note(const void* comp, const void* ra) {
    const void* const owner = read_ptr(comp, kObjectOuter);
    if (owner == nullptr || !is_a(owner, g_mech_class())) {
        return;
    }
    if (!note_event_cap(g_c591.lines, kC591Lines, "card591")) {
        return;
    }
    const char* module = "";
    const unsigned long long rva = c591_caller_rva(ra, &module);
    log::line("card591: %s t=%llu DestroyComponent %s (%s) %p on mech %s %p: %s, caller %s+0x%llX",
              side(), static_cast<unsigned long long>(GetTickCount64()), object_name(comp).text,
              class_name(comp).text, comp, object_name(owner).text, owner,
              comp == read_ptr(owner, kC591Body) ? "IS its body +0xC20" : "not its body", module, rva);
}

void detour_c591_destroy(void* comp, bool promote_children) {
    if (comp != nullptr) {
        c591_destroy_note(comp, _ReturnAddress());
    }
    g_orig_c591_destroy(comp, promote_children);
}

// detour_c75_leave_mech, before the original, with UBWCOOP_C79_LOG=1: watch this mech's body pointer from here.
void c591_leave_note(const void* mech, bool piloted) {
    const ULONGLONG now = GetTickCount64();
    if (mech != g_c591.mech) {
        g_c591.mech = mech;
        g_c591.world = nullptr;
        g_c591.mech_deaths = 0;
        g_c591.eject_ms = 0;
    }
    if (!piloted) {
        g_c591.eject_ms = now; // no weak PC: the game's own eject (self-destruct), not a player's exit
        g_c591.next_ms = now + kC591PollMs;
    }
    g_c591.end_ms = now + (piloted ? kC591LeaveWatchMs : kC591EjectWatchMs);
    c591_body_row(piloted ? "LeaveMech (piloted)" : "LeaveMech (no PC: eject)", mech, now);
}

void c591_step(const void* world, ULONGLONG now) {
    if (g_c591.mech == nullptr) {
        return;
    }
    if (g_c591.world == nullptr) {
        g_c591.world = world;
    }
    if (world != g_c591.world) {
        g_c591.mech = nullptr; // the map changed: the watched mech is gone, so it is not read again
        return;
    }
    if (now >= g_c591.end_ms) {
        c591_body_row("watch end", g_c591.mech, now);
        g_c591.mech = nullptr;
        return;
    }
    const bool changed = read_ptr(g_c591.mech, kC591Body) != g_c591.last_body;
    const bool poll = g_c591.eject_ms != 0 && now >= g_c591.next_ms;
    if (poll) {
        g_c591.next_ms = now + kC591PollMs;
    }
    if (changed || poll) {
        c591_body_row(changed ? "body changed" : "poll", g_c591.mech, now);
    }
}

// Log: each PlayerController whose pawn changed since the last step, with the new pawn's class and Health.
void c79_watch_pawns(const C43Roster& r, ULONGLONG now) {
    for (int32_t s = 0; s < kC43Max; ++s) {
        const int32_t id = r.p[s].pc != nullptr ? r.p[s].id : -1;
        if (id == g_c79.last_id[s]) {
            continue;
        }
        if (note_event_cap(g_c79.watch_lines, kC79WatchLines, "card079 pawn changes")) {
            log::line("card079: %s t=%llu slot %d PC %s pawn %d -> %d: %s (%s, %s) Health %.1f", side(),
                      static_cast<unsigned long long>(now), s, object_name(r.p[s].pc).text, g_c79.last_id[s], id,
                      object_name(r.p[s].pawn).text, class_name(r.p[s].pawn).text,
                      r.p[s].pawn != nullptr ? kSprintSideText[sprint_side(r.p[s].pawn)] : "-",
                      static_cast<double>(c79_health(r.p[s].pawn)));
        }
        g_c79.last_id[s] = id;
    }
}

// Host test: a lethal hit on the first piloted mech each kC79KillGapMs until its Health is 0 or no PC holds one.
void c79_kill_step(const void* world, const C43Roster& r, ULONGLONG now) {
    void* mech = nullptr;
    for (int32_t s = 0; s < kC43Max && mech == nullptr; ++s) {
        mech = is_a(r.p[s].pawn, g_mech_class()) ? r.p[s].pawn : nullptr;
    }
    void* component = nullptr;
    const float before = mech != nullptr ? c21_health(static_cast<const uint8_t*>(mech), &component) : -1.0f;
    if (mech == nullptr || before <= 0.0f || g_c79.kill_tries >= kC79KillTries) {
        log::line("%scard079: host t=%llu test kill over after %u hits: piloted mech %s, Health %.1f",
                  before <= 0.0f ? "" : "WARN ", static_cast<unsigned long long>(now), g_c79.kill_tries,
                  object_name(mech).text, static_cast<double>(before));
        g_c79.kill_ms = 0;
        return;
    }
    ++g_c79.kill_tries;
    g_c79.kill_ms = now + kC79KillGapMs;
    // The first kC79BugHits hits come from the nearest bug with the damage modifiers; c79b showed a mech takes
    // nothing from them, so later hits are the mech's own with the modifiers ignored (as card 043's test does).
    const bool own = g_c79.kill_tries > kC79BugHits;
    void* const bug = own ? nullptr : c30_nearest_enemy(world, mech);
    const float max_health = c43_max_health(component);
    g_deal_damage(bug != nullptr ? bug : mech, mech, (max_health > 0.0f ? max_health : kC79MechMaxHealth) * 10.0f,
                  bug == nullptr);
    log::line("card079: host t=%llu test kill hit %u on %s (causer %s, modifiers %s): Health %.1f -> %.1f",
              static_cast<unsigned long long>(now), g_c79.kill_tries, object_name(mech).text,
              object_name(bug != nullptr ? bug : mech).text, bug == nullptr ? "ignored" : "applied",
              static_cast<double>(before), static_cast<double>(c79_health(mech)));
}

// Host test: the first time the test player's pawn is within kC79DownRange of a mech (mode 3: a zipline point),
// card 043 downs it.
void c79_down_step(const void* world, const C43Roster& r, ULONGLONG now) {
    const int32_t slot = g_c79_down == 1 ? 0 : 1;
    void* const pawn = r.p[slot].pawn;
    if (r.players < 2 || !is_a(pawn, g_player_character_class()) || is_a(pawn, g_mech_class()) ||
        c43_is_down(pawn)) {
        return;
    }
    double dist = -1.0;
    const void* const cls = g_c79_down == 3 ? (g_c54_zipline_class != nullptr ? g_c54_zipline_class() : nullptr)
                                            : g_mech_class();
    const void* const mech = cls != nullptr ? nearest_of_class(world, cls, actor_location(pawn), &dist) : nullptr;
    if (mech == nullptr || dist > kC79DownRange) {
        return;
    }
    g_c79.down_done = true;
    c43_enter_down(pawn, r, static_cast<uint32_t>(slot), C43Life::Downed, now);
    const int32_t i = c43_find(r.p[slot].id);
    if (i >= 0 && slot == 0) {
        g_c43.e[i].secs_left = kC79OutBleed;
    }
    log::line("card079: host t=%llu test down: %s (slot %d) %.0f units from %s: %s%s",
              static_cast<unsigned long long>(now), object_name(pawn).text, slot, dist, object_name(mech).text,
              i >= 0 ? kC43LifeText[static_cast<size_t>(g_c43.e[i].state)] : "WARN not downed",
              i >= 0 && slot == 0 ? ", bleedout cut to 0.5 s" : "");
}

// Hot path through detour_process_event while armed: one probe of a small open-addressed table per call.
void c32_note_pe(const void* object, const void* function) {
    if (GetCurrentThreadId() != g_c32.thread || function == nullptr) {
        return;
    }
    const int32_t key = read_i32(function, kObjectIndex) + 1;
    C32Pe* slot = nullptr;
    for (uint32_t p = 0; p < kC32PeProbe && slot == nullptr; ++p) {
        C32Pe& s = g_c32.pe[(static_cast<uint32_t>(key) + p) & (kC32PeSlots - 1)];
        if (s.index == key || s.index == 0) {
            slot = &s;
        }
    }
    if (slot == nullptr) {
        return;
    }
    if (slot->index == 0) {
        *slot = C32Pe{key, static_cast<uint8_t>(c32_pe_interesting(function) ? 1 : 0), 0};
    }
    if (slot->interesting == 0 || slot->calls >= kC32PeCalls) {
        return;
    }
    ++slot->calls;
    if (note_event_cap(g_c32.pe_lines, kC32PeLines, "card032 PE")) {
        log::line("card032: %s PE %s.%s on %s (call %u)", side(), object_name(read_ptr(function, kObjectOuter)).text,
                  object_name(function).text, object_name(object).text, slot->calls);
    }
}

// The nearest bug to `pawn`, or null (the fallback causer when self-damage lands nothing). Filtered by class
// name: AI troopers also count as ASTEnemyBase (card 023), and a trooper causer dealt no damage (card030a).
void* c30_nearest_enemy(const void* world, const void* pawn) {
    if (g_enemy_class == nullptr) {
        return nullptr;
    }
    static C23List list; // game thread only; too large for the stack
    c23_collect(world, pawn, g_enemy_class(), list);
    void* best = nullptr;
    double best_dist = 0.0;
    for (uint32_t i = 0; i < list.count; ++i) {
        if (strstr(class_name(list.e[i].actor).text, "Bug") == nullptr) {
            continue;
        }
        if (best == nullptr || list.e[i].dist < best_dist) {
            best = list.e[i].actor;
            best_dist = list.e[i].dist;
        }
    }
    return best;
}

int32_t c43_find_hold(int32_t reviver_id) {
    for (int32_t h = 0; h < g_c43.holds; ++h) {
        if (g_c43.hold[h].reviver_id == reviver_id) {
            return h;
        }
    }
    return -1;
}

// The WB_* widgets and Buttons held by `obj`'s object properties (class chain), up to kC44MaxKids.
int32_t c44_kids(const void* obj, C44Kid* out) {
    int32_t n = 0;
    const void* cls = read_ptr(obj, kObjectClass);
    for (int depth = 0; depth < kC44MaxSupers && cls != nullptr; ++depth, cls = read_ptr(cls, kStructSuper)) {
        const void* f = read_ptr(cls, kStructChildProperties);
        for (int i = 0; i < kC44MaxFields && f != nullptr && n < kC44MaxKids; ++i, f = read_ptr(f, kFieldNext)) {
            const ObjectName kind = fname_text(read_ptr(f, kFieldClassPrivate));
            const bool object = strcmp(kind.text, "ObjectProperty") == 0 || strcmp(kind.text, "ObjectPtrProperty") == 0;
            void* const kid = object ? read_ptr(obj, static_cast<size_t>(read_i32(f, kPropertyOffset))) : nullptr;
            const ObjectName kc = class_name(kid);
            if (kid != nullptr && (strncmp(kc.text, "WB_", 3) == 0 || strcmp(kc.text, "Button") == 0)) {
                out[n++] = C44Kid{kid, fname_text(static_cast<const uint8_t*>(f) + kFieldName)};
            }
        }
    }
    return n;
}

// Host test: a death screen button's bound function (`fn_name`), called as its click would. False = not driven.
bool c44_press(uint64_t fn_name_value, const char* button) {
    void* const screen = g_c44.screen_weak != 0 ? g_weak_get(&g_c44.screen_weak) : nullptr;
    void* const fn = screen != nullptr && fn_name_value != 0 ? g_find_function(screen, fn_name_value) : nullptr;
    const ObjectName fn_name = fname_text(&fn_name_value);
    if (fn == nullptr) {
        log::line("WARN card044: host test %s not driven: screen %s, click function %s, UFunction %p", button,
                  object_name(screen).text, fn_name_value != 0 ? fn_name.text : "(not found)", fn);
        return false;
    }
    log::line("card044: host t=%llu test restart: ProcessEvent(%s, %s), as the %s click",
              static_cast<unsigned long long>(GetTickCount64()), object_name(screen).text, fn_name.text, button);
    alignas(16) static uint8_t parms[kC44ParmsBytes];
    memset(parms, 0, sizeof(parms));
    g_orig_process_event(screen, fn, parms);
    log::line("card044: host t=%llu test restart call returned", static_cast<unsigned long long>(GetTickCount64()));
    return true;
}

void c43_shot_step(const void* world, void* pc, ULONGLONG now) {
    for (int32_t i = g_c43t.shots - 1; i >= 0; --i) {
        C43Shot& s = g_c43t.shot[i];
        if ((s.flags & kC44ShotAim) != 0 && now + kC44AimLeadMs >= s.at_ms) {
            s.flags = static_cast<uint8_t>(s.flags & ~kC44ShotAim);
            c44_aim_other(world, pc);
        }
        if (now >= s.at_ms) {
            log::line("card043: %s t=%llu screenshot%s: %s", side(), static_cast<unsigned long long>(now),
                      (s.flags & kC44ShotUi) != 0 ? " (showui)" : "", s.what);
            if ((s.flags & kC44ShotUi) != 0) {
                request_shot(pc, true);
            } else {
                request_shot(pc, false);
            }
            g_c43t.shot[i] = g_c43t.shot[--g_c43t.shots];
        }
    }
}

void c43_next(C43Stage stage, ULONGLONG at_ms) {
    log::line("card043: host t=%llu test stage %u -> %u", static_cast<unsigned long long>(GetTickCount64()),
              static_cast<unsigned>(g_c43t.stage), static_cast<unsigned>(stage));
    g_c43t.stage = stage;
    g_c43t.at_ms = at_ms;
}

// --- Card 294: one host restart is one client transition ---
// Playtest 2026-11-13: the host's LoadLastSave (an in-place save load) showed on the client as a second death screen
// (the load's script CompleteDeath), a respawn in the old world (the load's new pawn) and then the card 143 Reload
// rejoin. Client, from the host's travel row (card 290 part A, sent for Browse and for an in-place load) until the new
// map: a second CompleteDeath while a death screen is up is skipped, and a respawn in place is held (the death screen
// stays up, card 156 restore does not run). The hold ends at the new map's first pawn, or, when no travel follows
// (no Reload or Browse within kC290TravelMs), the held respawn runs as before.
namespace {
struct C294 {
    ULONGLONG started_ms;  // client: the travel row that began the hold (0 = no hold)
    const void* dead_pawn; // client: the pawn of the last CompleteDeath that ran (compared only, never read)
    const void* held_pawn; // client: the last held pawn (its row is logged once)
    int32_t screens;       // death screens built since the row, counting those up at its arrival
    int32_t held;          // respawns in place held
    int32_t shown;         // respawns in place shown in the old world
};
C294 g_c294{};

bool c294_screen_up() {
    for (int32_t i = 0; i < g_c45.screen_count && i < static_cast<int32_t>(kC45Screens); ++i) {
        if (g_weak_get(&g_c45.screens[i]) != nullptr) {
            return true;
        }
    }
    return false;
}

bool c294_holding(ULONGLONG now) {
    return g_c294.started_ms != 0 && (now - g_c294.started_ms <= kC290TravelMs || g_c45.follow != C45Follow::Off);
}

void c294_done(ULONGLONG now, const char* how) {
    log::line("%scard294: client t=%llu restart done in %llu ms: death screens built %d, respawns in the old world %d "
              "(held %d, %s)",
              g_c294.screens > 1 || g_c294.shown > 0 ? "WARN " : "", static_cast<unsigned long long>(now),
              static_cast<unsigned long long>(now - g_c294.started_ms), g_c294.screens, g_c294.shown, g_c294.held,
              how);
    g_c294 = C294{};
}
} // namespace

void c294_client_travel(const char* reason, ULONGLONG now) {
    if (!g_client_session) {
        return;
    }
    g_c294.started_ms = now;
    g_c294.held_pawn = nullptr;
    g_c294.screens = g_c45.screen_count;
    g_c294.held = 0;
    g_c294.shown = 0;
    log::line("card294: client t=%llu host restart (%s): death screen kept, respawn in place held (%d death screens "
              "tracked)",
              static_cast<unsigned long long>(now), reason, g_c45.screen_count);
}

// Client, at CompleteDeath: true skips it. A pawn whose death screen is up, or any pawn during the hold, already has
// its death shown; the repeat would build a second death screen.
bool c294_skip_complete_death(const void* pawn, unsigned long long caller) {
    if (!g_client_session) {
        return false;
    }
    const ULONGLONG now = GetTickCount64();
    const bool repeat = g_c44.complete_ms != 0 && (pawn == g_c294.dead_pawn || c294_holding(now));
    if (!repeat || !c294_screen_up()) {
        g_c294.dead_pawn = pawn;
        return false;
    }
    log::line("card294: client t=%llu second CompleteDeath skipped (death screen up): %s, caller exe+0x%llX",
              static_cast<unsigned long long>(now), object_name(pawn).text, caller);
    return true;
}

// Client, at a new local pawn: true holds it (the respawn in place does not run this tick). `old` null is the new
// map's first pawn: the hold is done.
bool c294_hold_respawn(const void* pawn, const void* old, ULONGLONG now) {
    if (g_c294.started_ms == 0) {
        return false;
    }
    if (old == nullptr) {
        c294_done(now, "new map");
        return false;
    }
    if (!c294_holding(now)) {
        return false;
    }
    if (pawn != g_c294.held_pawn) {
        g_c294.held_pawn = pawn;
        ++g_c294.held;
        log::line("card294: client t=%llu respawn in place held: new pawn %s in the old world, %llu ms after the host "
                  "restart row; death screen kept",
                  static_cast<unsigned long long>(now), class_name(pawn).text,
                  static_cast<unsigned long long>(now - g_c294.started_ms));
    }
    return true;
}

// Client: true from the host's restart row until the new map's first pawn (or a respawn in place with no travel).
// A pawn seen in this window lives in the old world and is destroyed by the travel (card 570, run c397b398a).
bool c294_restart_pending() {
    return g_c294.started_ms != 0;
}

// Client, after a respawn in place ran: no travel followed the row, so the hold ends here.
void c294_shown(ULONGLONG now) {
    if (g_c294.started_ms != 0) {
        ++g_c294.shown;
        c294_done(now, "no travel followed");
    }
}

// Client death screen: RestartMission, LoadLastSave and LoadGame disabled (the host owns the mission); Exit kept.
void c45_client_screen(void* screen) {
    if (g_c294.started_ms != 0) {
        ++g_c294.screens; // card 294
    }
    if (g_c45.screen_count < kC45Screens) {
        g_weak_assign(&g_c45.screens[g_c45.screen_count++], &screen);
    }
    C44Kid kids[kC44MaxKids];
    const int32_t n = c44_kids(screen, kids);
    int32_t disabled = 0;
    int exit_enabled = -2;
    for (int32_t i = 0; i < n; ++i) {
        const char* const name = kids[i].name.text;
        const bool off = strstr(name, "RestartMission") != nullptr || strstr(name, "LoadLastSave") != nullptr ||
                         strstr(name, "LoadGame") != nullptr;
        if (off) {
            g_widget_set_is_enabled(kids[i].obj, false);
            ++disabled;
        } else if (strstr(name, "Exit") != nullptr) {
            exit_enabled = c45_is_enabled(kids[i].obj);
        } else {
            continue;
        }
        log::line("card045: client death screen button %s (%s): %s, bIsEnabled %d", name, class_name(kids[i].obj).text,
                  off ? "disabled" : "kept", c45_is_enabled(kids[i].obj));
    }
    log::line("%scard045: client death screen: %d buttons disabled (RestartMission, LoadLastSave, LoadGame), Exit "
              "bIsEnabled %d", disabled == 3 && exit_enabled == 1 ? "" : "WARN ", disabled, exit_enabled);
}

// Both sides, every tick: RESTART CHECKPOINT (LoadLastSave) reloads in place, with no Browse or LoadMap, and
// respawns every player. A new local pawn while the game-over state is still set gets the per-map reset here.
void c45_respawn_check(void* pc, ULONGLONG now) {
    void* const pawn = read_ptr(pc, kControllerPawn);
    c48_client_step(pc, now);
    c49_step(pc, now);
    c295_step(now); // card 295
    if (g_client_session) {
        c156_client_check(pc, now);
    }
    if (pawn == nullptr || pawn == g_c45.pawn) {
        return;
    }
    if (g_client_session && c294_hold_respawn(pawn, g_c45.pawn, now)) {
        return; // card 294: g_c45.pawn stays the old one, so the check repeats until the hold ends
    }
    void* const old = g_c45.pawn;
    g_c45.pawn = pawn;
    const bool stale = g_c43.all_down || g_c43.death_done || g_c44.start_ms != 0 || g_c44.complete_ms != 0;
    if (old == nullptr || !stale) {
        return;
    }
    log::line("card045: %s t=%llu respawn in place (new pawn %s, no map load): per-map reset", side(),
              static_cast<unsigned long long>(now), class_name(pawn).text);
    if (g_client_session) {
        c156_client_up(pc, "respawn in place", true, now);
        c294_shown(now);
    }
    c45_arm_map();
    c43_stand_all(pc); // host: Up frames before the entries go
    c43_map_end();     // card 326: the entries and holds (the map path runs this at map end)
    c43_arm_map();
    pawn_slots_respawn(); // card 130 item 15
    fx_watch_map("the respawn in place"); // the load destroyed the watched effects: no false "component gone"
    g_c45.pawn = pawn;
    c48_respawn(pawn, now);
    if (g_c45_test && g_c43_test && g_client_session) {
        c43_shot(now + kC45ShotMs, "after the checkpoint (client)", kC44ShotUi);
        g_c45.done_ms = now + kC45ClientDoneMs;
    }
}

// Host test, both on foot. Pair phase 0: the host then the copy down (game over); phase 1: the copy down, out and
// revived by the host's hold, then game over; phase 2, and solo phase 1: done.
void c45_test_on_foot(const C43Roster& r, ULONGLONG now, bool solo) {
    log::line("card045: host t=%llu test phase %u: on foot, net mode %u, players %u", static_cast<unsigned long long>(now),
              g_c45_phase, net_mode(r.p[0].pawn), r.players);
    if (g_c45_phase > 0) {
        c43_shot(now + 3000, "after the restart (host)", kC44ShotUi);
    }
    if (g_c45_phase >= (solo ? 1u : 2u)) {
        g_c45.done_ms = now + kC45HostDoneMs;
        c43_next(C43Stage::Done, now);
    } else {
        c43_next(g_c45_phase == 0 ? C43Stage::KillHost2 : C43Stage::KillCopy, now + kC43TestStartMs);
    }
}

// Host test, game over: phase 0 presses RESTART MISSON, pair phase 1 RESTART CHECKPOINT.
void c45_test_restart(bool solo) {
    if (g_c45_phase >= (solo ? 1u : 2u)) {
        return;
    }
    const bool checkpoint = g_c45_phase == 1;
    ++g_c45_phase;
    log::line("card045: host t=%llu test restart %u: %s", static_cast<unsigned long long>(GetTickCount64()), g_c45_phase,
              checkpoint ? "RESTART CHECKPOINT (LoadLastSave)" : "RESTART MISSON (RestartMission)");
    if (!c44_press(checkpoint ? g_c44.checkpoint_fn : g_c44.restart_fn, checkpoint ? "LoadLastSave" : "RestartMission")) {
        g_c44.test_over = true;
        log::line("WARN card045: host test stopped: the restart was not driven");
    }
}

// Both sides, at the respawn in place: the local pawn now; the client again kC48ClientDumpMs later.
void c48_respawn(const void* pawn, ULONGLONG now) {
    c48_pawn_line("respawn in place", pawn);
    if (c48_line()) {
        log::line("card048: %s respawn in place: LoadAbilitySystemComponent calls so far %u", side(), g_c48.asc_loads);
    }
    if (g_client_session) { // card 156: the cursor and IMC reset moved into c156_client_up
        g_c48.client_dump_ms = now + kC48ClientDumpMs;
    }
}

// Client, every tick: the delayed own-pawn line after a respawn in place.
void c48_client_step(void* pc, ULONGLONG now) {
    if (g_c48.client_dump_ms == 0 || now < g_c48.client_dump_ms) {
        return;
    }
    g_c48.client_dump_ms = 0;
    c48_pawn_line("respawn in place +5 s", read_ptr(pc, kControllerPawn));
    void* iface = nullptr;
    const bool mapped = c48_find_context(pc, "IMC_DummyUI_KBM", &iface) != nullptr;
    log::line("card048: client respawn +5 s: bShowMouseCursor %u, viewport capture %u, IMC_DummyUI_KBM %s, "
              "LoadAbilitySystemComponent calls %u, AddObjectiveObject calls since reset %u",
              cursor_bit(pc), c48_capture_mode(pc), mapped ? "mapped" : "not mapped", g_c48.asc_loads,
              g_c48.objectives);
}

// Host, after SetObjectiveMapPostSave: the journal becomes the snapshot of the restored objective map.
void c49_snapshot(void* subsystem) {
    SparseView v{};
    const void* const world = subsystem != nullptr ? read_ptr(subsystem, kObjectOuter) : nullptr;
    if (g_client_session || world == nullptr || !c49_objectives(subsystem, v)) {
        log::line("card049: %s snapshot skipped (client %d, world %p)", side(), g_client_session ? 1 : 0, world);
        return;
    }
    g_c49_journal.compact_off = false;
    log::line("card049: host t=%llu checkpoint snapshot from %s: offsets id +0x%zX, parent +0x%zX, marker +0x%zX, "
              "complete +0x%zX, in list +0x%zX, count target +0x%zX current +0x%zX; map +0x%zX stride 0x%zX",
              static_cast<unsigned long long>(GetTickCount64()), object_name(subsystem).text, kObjectiveId,
              kObjectiveParent, kObjectiveMarker, kObjectiveComplete, kObjectiveInList, kObjectiveCountTarget,
              kObjectiveCountCurrent, kSubsystemObjectives, kObjectiveElementBytes);
    const int32_t n = c49_snapshot_journal(world, v);
    c49_shot(GetTickCount64() + kC49ShotAfterMs, "card053 host panel after the checkpoint");
    c49_shot(GetTickCount64() + kC53TestDelayMs + kC49ShotAfterMs + kC49ClientShotLagMs,
             "card053 host panel at the client's after shot");
    if (g_c49_test && g_c45_phase == 2 && g_c53_event_ms == 0) {
        g_c53_event_ms = GetTickCount64() + kC53EventMs; // card053x3: one completion after the client's rebuild
    }
    log::line("card049: host snapshot: %d objectives, %d journal frames", n, g_c49_journal.n);
}

// Both sides, every tick (from c45_respawn_check).
void c49_step(void* pc, ULONGLONG now) {
    if (g_c49.world_check_ms != 0 && now >= g_c49.world_check_ms) {
        g_c49.world_check_ms = 0;
        c49_world_check(pc);
    }
    c49_test_step(pc, now);
    c53_step(pc, now); // card 053: client only (due only after a snapshot end)
    c53_test_step(pc, now); // card053x3: host test only
    c49_shot_step(pc, now);
    if (g_c49_test) {
        c49_panel_watch(pc, now); // panel diagnosis (widget reads every 250 ms): test runs only
    }
}

void c46_step(void* world, void* pc, ULONGLONG now) {
    c389_step(world, now); // card 389: host only
    c399_step(world, now); // card 399: host only
    if (g_c46.follows != 0) {
        c46_follow_step(world, pc, now);
    }
    if (!g_c46_test || g_c46t.stage == C46Stage::Done) {
        return;
    }
    if (g_client_session) {
        c46_test_client(world, now);
    } else {
        c46_test_host(world, now);
    }
}

void c50_after_load_asc(const void* pawn) {
    if (!g_client_session && sprint_side(pawn) == kSideServerCopy) {
        g_c50.asc_pawn_id = object_index(pawn);
    }
}

void c54_track_step(ULONGLONG now) {
    if (g_c54_track.pawn == nullptr || now < g_c54_track.next_ms) {
        return;
    }
    if (now > g_c54_track.until_ms || c31_weak_get(g_c54_track.weak) != g_c54_track.pawn) {
        log::line("card054: %s position log of %s ends", side(), g_c54_track.what);
        g_c54_track.pawn = nullptr;
        return;
    }
    g_c54_track.next_ms += kC54TrackEveryMs;
    const Vec3 at = actor_location(g_c54_track.pawn);
    log::line("card054: %s t=%llu %s %s at (%.0f, %.0f, %.0f)", side(), static_cast<unsigned long long>(now),
              g_c54_track.what, object_name(g_c54_track.pawn).text, at.x, at.y, at.z);
}

void c50_relay_use(void* pawn, bool pressed) {
    void* const interactor = !g_client_session && sprint_side(pawn) == kSideServerCopy
                                 ? pawn_interactor(pawn)
                                 : nullptr;
    if (interactor == nullptr) {
        return;
    }
    const int32_t id = object_index(pawn);
    if (!pressed) {
        if (g_c50.relayed_id == id) {
            g_c50.relayed_id = -1;
            g_orig_release_interaction(interactor);
        }
        return;
    }
    if (c43_is_down(pawn)) { // card 081: no relayed Use for a down copy; the release branch above is unchanged
        ++g_c81.relays;
        if (note_event_cap(g_c81.relay_lines, kC81Lines, "card081 relay")) {
            log::line("card081: host t=%llu relay for %s refused: its copy is %s (refusal #%u)",
                      static_cast<unsigned long long>(GetTickCount64()), object_name(pawn).text, c43_life_text(pawn),
                      g_c81.relays);
        }
        return;
    }
    if (c43_find_hold(id) >= 0) {
        log::line("card050: host relay for %s skipped: the press started a revive hold", object_name(pawn).text);
        return;
    }
    void* const target = c50_pick_target(pawn, interactor);
    if (target == nullptr) {
        return;
    }
    const C54Relay* const row = c54_relay_row(target);
    if (row == nullptr) {
        log::line("card054: host relay for %s declined: target %s is a %s, not in the relay table (log only; the "
                  "client's own interaction stands)",
                  object_name(pawn).text, object_name(target).text, class_name(target).text);
        return;
    }
    c50_interact(pawn, interactor, target, row);
}

void c50_step(void* world, void* pc, ULONGLONG now) {
    C330_STEP(c208_step(world, pc, now)); // card 208: the bug form switch relay
    C330_STEP(c345b_step(pc));            // card 345b: the level-end Next widget, the waiting line, a held relay
    if (g_client_session) {
        C330_STEP(c50_client_step(pc, now));
        C330_STEP(c50_peer_client_step(pc, now)); // card 050 b205: the other players' weapons
        C330_STEP(c72_client_step(pc, now)); // card 072
        C330_STEP(c73_client_step(pc, now)); // card 073
        C330_STEP(c310_client_step(pc));     // card 310: the kept level script rows on a new own pawn
        C330_STEP(c347_client_step(pc));     // card 347: the camera resend limit
        C330_STEP(c494_client_step(pc));     // card 494: the drawn weapon after an own throw
        if (g_c50_test) {
            c50_test_client(world, pc, now);
        }
    } else {
        C330_STEP(c50_host_step(world, now));
        C330_STEP(c599_host_step(world));          // card 599: each copy's camera manager at its view, each frame
        C330_STEP(c50_peer_host_step(world, now)); // card 050 b205
        C330_STEP(c143_host_step(world, now)); // card 143: a rejoined client's held Restore, once its new copy is ready
        C330_STEP(c278_host_step(world));      // card 278: script loadout rows on a respawned copy, after any Restore
        C330_STEP(c310_host_step(world, now)); // card 310: the default class compare, the map rows on a new copy pawn
        C330_STEP(c582_tracker_step(now));     // card 582 G4: pickup tracker binds for a changed pawn
        C330_STEP(c95_replay_step());          // card 219: the next paced collision rows of each join replay
        if (g_c50_test && g_c57_solo) {
            c57_test_solo(world, pc, now);
            if (g_c50t.stage == C50Stage::Done) {
                c60_press_two(pc, now); // card 062 solo baseline
            }
        } else if (g_c50_test && g_c60_host_own && (g_c60t.started || c54_step() == nullptr)) {
            c60_test_host_own(world, pc, now); // after the host test's last step, never beside it
        } else if (g_c50_test) {
            c50_test_host(world, now);
        }
    }
    if (g_c50_test && !g_c43_test && !g_c46_test) {
        c43_shot_step(world, pc, now); // the card 050 driver queues its shots here
    }
    c54_track_step(now);
    C330_STEP(c75_count_step(world, now)); // card 075
    if (g_c76_log) {
        c76_step(world, now);
    }
    if (g_c79_log || g_c79_kill || g_c79_down != 0) {
        c79_step(world, now);
    }
}

// Hot path while kPeGate_c67_pe: every ProcessEvent. `object` is live here (the call is on it).
void c67_note_pe(void* object, const void* function) {
    const int32_t k = c67_kind_of(object);
    if (k < 0) {
        return;
    }
    if (c67_remember(k, object)) {
        log::line("card067: seen %s %s (%p) first in ProcessEvent %s, Visibility %u", class_name(object).text,
                  prop_name(static_cast<const uint8_t*>(object) + kObjectName, 8).text, object,
                  object_name(function).text, static_cast<const uint8_t*>(object)[kC67WidgetVisibility]);
    }
    for (int32_t i = 0; i < g_c67.ncalls; ++i) {
        if (g_c67.calls[i].object == object && g_c67.calls[i].function == function) {
            return;
        }
    }
    if (g_c67.ncalls < kC67MaxCalls) {
        g_c67.calls[g_c67.ncalls++] = C67Call{object, function};
        log::line("card067: ProcessEvent %s.%s (%p)", class_name(object).text, object_name(function).text, object);
    }
}

// Client tick: grants waiting for their replicated spec are bound once it arrives; after kC73WaitMs a wait is dropped.
void c73_client_step(void* pc, ULONGLONG now) {
    c73_pending_step(pc, now);
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    for (int32_t i = 0; i < kC73Waits; ++i) {
        C73Grant& g = g_c73_wait[i];
        if (g.ability == nullptr) {
            continue;
        }
        const bool own = sprint_side(pawn) == kSideClientOwn;
        if (own && c73_bind(pawn, g, nullptr)) {
            g = C73Grant{};
        } else if (own && g.item != nullptr && !c73_inventory_holds(c73_item_inventory(pawn), g.item)) {
            // Card 351: the item left our inventory (the host removed it in the grant's tick): its spec went with it.
            log::line("card073: client t=%llu grant %s: its item left the inventory after %llu ms, wait ended",
                      static_cast<unsigned long long>(now), object_name(g.ability).text,
                      static_cast<unsigned long long>(now - g.since_ms));
            g = C73Grant{};
        } else if (now - g.since_ms >= kC73WaitMs) {
            log::line("WARN card073: client t=%llu grant %s: no replicated spec after %llu ms, input not bound",
                      static_cast<unsigned long long>(now), object_name(g.ability).text,
                      static_cast<unsigned long long>(kC73WaitMs));
            g = C73Grant{};
        }
    }
}

// Each ProcessEvent the task's DoAction broadcast makes: the bound function and its flags (FUNC_BlueprintAuthorityOnly
// 0x4, FUNC_Net 0x40, FUNC_NetServer 0x200000), to see why the client's own throw never reaches OnDoAction.
void c74_note_pe(const void* object, const void* function) {
    uint32_t flags = 0;
    memcpy(&flags, static_cast<const uint8_t*>(function) + kFunctionFlags, sizeof(flags));
    log::line("card074: %s   %s: ProcessEvent %s.%s, function flags 0x%08X", side_text(),
              g_c74_fx ? "LifeSpanExpired" : "DoAction broadcast",
              class_name(object).text, object_name(function).text, flags);
}

// Client, every tick: at most every kC72MirrorMs, our own AmmoMap to the host when its text changed.
void c72_client_step(void* pc, ULONGLONG now) {
    if (c72_take_set(pc, now)) {
        return; // card 073: no mirror while a seed waits, or the new pawn's own counts would lower its copy first
    }
    void* const pawn = read_ptr(pc, kControllerPawn);
    c570_magazine_step(pawn, now); // card 570: a checkpoint rejoin's kept magazine, before the mirror reads it
    int32_t rounds = c274_magazine_rounds(pawn); // card 274
    const bool reloaded = rounds > g_c72.magazine_rounds; // card 274: a reload (or a switch to a fuller weapon) goes now
    if (rounds != g_c72.magazine_rounds) {
        c570_magazine_note(pawn); // card 570: kept for a Reload that comes after the pawn is gone
    }
    g_c72.magazine_rounds = rounds;
    if (now < g_c72.next_ms && !reloaded) {
        return;
    }
    g_c72.next_ms = now + kC72MirrorMs;
    const void* const inventory = is_a(pawn, g_player_mi_class()) ? read_ptr(pawn, kMiAmmoInventory) : nullptr;
    static char text[kChanTextMax + 1];
    int32_t len = c72_map_text(inventory, text, sizeof(text));
    if (len < 0 && !g_c72.holes_warned) {
        g_c72.holes_warned = true;
        log::line("WARN card072: client: own AmmoMap has free slots; not mirrored while it does");
    }
    if (len <= 0) {
        return;
    }
    const size_t map_len = static_cast<size_t>(len);
    c274_magazine_text(pawn, text, map_len, sizeof(text), &rounds);
    // Card 444: text = "<map>" or "<map> <magazine pairs>"; the magazine part starts after the separating space.
    const char* const magazine = text + map_len + (text[map_len] == ' ' ? 1 : 0);
    const bool map_changed = strncmp(text, g_c72.sent, map_len) != 0 || g_c72.sent[map_len] != '\0';
    if (!map_changed && strcmp(magazine, g_c72.sent_magazine) == 0) {
        return;
    }
    const bool magazine_only = !map_changed && !reloaded && magazine[0] != '\0';
    if (magazine_only && now < g_c72.magazine_ms) {
        return;
    }
    const char* const out = magazine_only ? magazine : text;
    static wchar_t wire[kChanTextFrame];
    const WireString w = chan_text_frame(ChanOp::AmmoMirror, out, wire);
    const bool sent = w.num > 0 && chan_send_host(pc, w);
    log::line("card072: client t=%llu AmmoMirror %s: %s", static_cast<unsigned long long>(now),
              sent ? "sent" : "WARN not sent", out);
    if (!sent) {
        return;
    }
    snprintf(g_c72.sent_magazine, sizeof(g_c72.sent_magazine), "%s", magazine);
    g_c72.magazine_ms = now + kC72MagazineMs;
    if (!magazine_only) {
        memcpy(g_c72.sent, text, map_len);
        g_c72.sent[map_len] = '\0';
    }
}

// HandleDeathEvent on a mech (both sides): the mech is not a card 043 player, so its own destruction runs.
void c79_mech_death(const void* mech) {
    if (!note_event_cap(g_c79.lines, kC79Lines, "card079")) {
        return;
    }
    log::line("card079: %s t=%llu HandleDeathEvent on mech %s (%s): not a card 043 player, the game's own "
              "destruction runs; Health %.1f, pilot %s",
              side(), static_cast<unsigned long long>(GetTickCount64()), object_name(mech).text,
              kSprintSideText[sprint_side(mech)], static_cast<double>(c79_health(mech)),
              object_name(c75_leave_soldier(mech)).text);
}

void c79_step(const void* world, ULONGLONG now) {
    if (world == nullptr) {
        return;
    }
    const C43Roster r = c43_roster(world);
    if (g_c79_log) {
        c79_watch_pawns(r, now);
        c591_step(world, now);
    }
    if (g_c79_kill && !g_client_session && g_c79.kill_ms != 0 && now >= g_c79.kill_ms) {
        c79_kill_step(world, r, now);
    }
    if (g_c79_down != 0 && !g_client_session && !g_c79.down_done) {
        c79_down_step(world, r, now);
    }
}

// Installed after MH_Initialize, before the hook tables, and only with UBWCOOP_C79_LOG=1.
bool c79_install(uint8_t* base, const IMAGE_NT_HEADERS64* nt) {
    const HookSpec hooks[] = {
        {&offsets::ASTPlayerMechBase_StartDeath, reinterpret_cast<void*>(&detour_c79_start_death),
         reinterpret_cast<void**>(&g_orig_c79_start_death)},
        {&offsets::ASTPlayerMechBase_DeathComplete, reinterpret_cast<void*>(&detour_c79_death_complete),
         reinterpret_cast<void**>(&g_orig_c79_death_complete)},
        {&offsets::ASTPlayerMechBase_StartSelfDestruction, reinterpret_cast<void*>(&detour_c79_self_destruct),
         reinterpret_cast<void**>(&g_orig_c79_self_destruct)},
        {&offsets::ASTPlayerMechBase_OnSelfDestructSequenceInitialized, reinterpret_cast<void*>(&detour_c79_sd_init),
         reinterpret_cast<void**>(&g_orig_c79_sd_init)},
        {&offsets::ASTPlayerMechBase_OnSelfDestructSequenceComplete, reinterpret_cast<void*>(&detour_c79_sd_complete),
         reinterpret_cast<void**>(&g_orig_c79_sd_complete)},
        // Card 591 diag. g_scene_destroy_component (card 409) keeps the raw address, so its calls pass this detour.
        {&offsets::USceneComponent_DestroyComponent, reinterpret_cast<void*>(&detour_c591_destroy),
         reinterpret_cast<void**>(&g_orig_c591_destroy)},
    };
    for (const HookSpec& h : hooks) {
        void* const target = code_address(base, nt, *h.sym);
        if (target == nullptr || !create_and_enable(target, h.detour, h.original, h.sym->name)) {
            return false;
        }
    }
    log::line("hooks: card 079 mech destruction log on (%zu log detours)", sizeof(hooks) / sizeof(hooks[0]));
    return true;
}

void c75_count_step(const void* world, ULONGLONG now) {
    if (world == nullptr || now < g_c75c.next_ms) {
        return;
    }
    g_c75c.next_ms = now + kC75CountMs;
    static C23List list;
    c23_collect(world, nullptr, g_mech_class(), list);
    uint32_t own = 0;
    for (uint32_t i = 0; i < list.count; ++i) {
        own += static_cast<const uint8_t*>(list.e[i].actor)[kActorLocalRole] == kRoleAuthority ? 1u : 0u;
    }
    c75_piloted_step(list);
    if (g_client_session) {
        c75_client_possess_step(list);
    }
    if ((list.count == g_c75c.mechs && own == g_c75c.own) ||
        !note_event_cap(g_c75c.lines, kC75CountLines, "card075 mech counts")) {
        return;
    }
    log::line("card075: %s t=%llu mechs %u -> %u, local role 3 %u -> %u", side(), static_cast<unsigned long long>(now),
              g_c75c.mechs, list.count, g_c75c.own, own);
    g_c75c.mechs = list.count;
    g_c75c.own = own;
    for (uint32_t i = 0; i < list.count; ++i) {
        const auto* const m = static_cast<const uint8_t*>(list.e[i].actor);
        double pad_dist = -1.0;
        const void* const pad = c75_nearest_pad(world, list.e[i].loc, &pad_dist);
        log::line("card075: %s   mech %s roles %u/%u at (%.0f, %.0f, %.0f), nearest pad %s at %.0f units",
                  side(), object_name(m).text, m[kActorLocalRole], m[kActorRemoteRole], list.e[i].loc.x,
                  list.e[i].loc.y, list.e[i].loc.z, object_name(pad).text, pad_dist);
    }
}

// 1 Hz, both sides: the mechs' locations, then the Health of every NPC near one.
void c76_step(const void* world, ULONGLONG now) {
    if (world == nullptr || now < g_c76.next_ms) {
        return;
    }
    g_c76.next_ms = now + kC76StepMs;
    static C23List mechs;
    c23_collect(world, nullptr, g_mech_class(), mechs);
    g_c76.mechs = mechs.count < kC76Mechs ? mechs.count : kC76Mechs;
    for (uint32_t i = 0; i < g_c76.mechs; ++i) {
        g_c76.mech_at[i] = mechs.e[i].loc;
    }
    for (C76Npc& n : g_c76.npc) {
        n.seen = false;
    }
    if (g_c76.mechs == 0) {
        return;
    }
    static C25List npcs;
    c25_collect(world, Vec3{}, ViewPoint{}, npcs);
    for (uint32_t i = 0; i < npcs.count; ++i) {
        const double d = c76_mech_dist(npcs.npc[i].loc);
        if (d >= 0.0 && d <= kC76NearMech) {
            c76_npc(npcs.npc[i].actor, d, now);
        }
    }
}

} // namespace ubw::hooks
