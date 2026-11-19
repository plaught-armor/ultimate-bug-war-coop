// Card 094: the client's jump and mantle on the host's server copy. Log only, installed only with UBWCOOP_C94_LOG=1
// (g_gate_c94): the activations and ends of the jump and mantle abilities, their commits, the mantle search task,
// the mantle gameplay events, and the character jump calls, each with the pawn's side, movement mode and Z.

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

bool g_c94_log = false; // UBWCOOP_C94_LOG; written in read_card_flags before any hook
bool g_gate_c94 = false;
bool g_gate_c94_diag = false;

EndAbilityFn g_orig_c94_end_ability = nullptr;
CommitAbilityFn g_orig_c94_commit = nullptr;
MantleSearchFn g_orig_c94_mantle_search = nullptr;
HandleGameplayEventFn g_orig_c94_gameplay_event = nullptr;
ClientEndAbilityFn g_orig_c94_client_end = nullptr;
MonitoredTagChangedFn g_orig_c94_tag_changed = nullptr;

namespace {

constexpr uint32_t kC94Lines = 400;
uint32_t g_c94_lines = 0;
constexpr const char* kSideText[kSprintSides] = {"host own", "server copy", "client own", "other"};

bool c94_line() {
    return note_event_cap(g_c94_lines, kC94Lines, "card094");
}

// The jump, mantle, slide and shunt abilities: the ones that move the pawn or block the mantle.
bool c94_watched(const void* ability) {
    const ObjectName cls = class_name(ability);
    return text_contains(cls.text, "Jump") || text_contains(cls.text, "Mantl") || text_contains(cls.text, "Slide") ||
           text_contains(cls.text, "Shunt");
}

struct PawnState {
    const char* side;
    unsigned mode;
    Vec3 at;
};

PawnState pawn_state(const void* pawn) {
    const uint32_t s = pawn != nullptr ? sprint_side(pawn) : kSideOther;
    const auto* const cmc = static_cast<const uint8_t*>(pawn != nullptr ? read_ptr(pawn, kCharacterMovement) : nullptr);
    return PawnState{kSideText[s < kSprintSides ? s : kSideOther], cmc != nullptr ? cmc[kCmcMovementMode] : 0xFFu,
                     pawn != nullptr ? actor_location(pawn) : Vec3{}};
}

ULONGLONG c94_now() {
    return GetTickCount64();
}

} // namespace

// After InternalTryActivateAbility, both sides.
void c94_try(const void* asc, int32_t handle, const void* ability, bool ok, bool server, unsigned long long caller) {
    if (!g_c94_log || !c94_watched(ability) || !c94_line()) {
        return;
    }
    const uint8_t* const spec = c21_spec_for(asc, handle);
    const auto* const cdo = static_cast<const uint8_t*>(spec != nullptr ? read_ptr(spec, 0x10) : nullptr);
    const PawnState p = pawn_state(read_ptr(asc, kComponentOwner));
    log::line("card094: %s t=%llu %s InternalTryActivateAbility(handle %d)%s = %d, ability %s, ActiveCount %d, net "
              "policy %d, instancing %d, mode %u, at (%.1f, %.1f, %.1f), caller exe+0x%llX",
              side(), static_cast<unsigned long long>(c94_now()), p.side, handle,
              server ? " for a client request" : "", ok ? 1 : 0, class_name(ability).text,
              spec != nullptr ? static_cast<int>(spec[kSpecActiveCount]) : -1, cdo != nullptr ? cdo[0x198] : -1,
              cdo != nullptr ? cdo[0xCF] : -1, p.mode, p.at.x, p.at.y, p.at.z, caller);
}

// A character jump call (ACharacter::Jump, CanJump, CheckJumpInput) on a player pawn.
void c94_jump_call(const char* what, const void* character, int result) {
    if (!g_c94_log || !is_a(character, g_player_mi_class()) || !c94_line()) {
        return;
    }
    const PawnState p = pawn_state(character);
    log::line("card094: %s t=%llu %s %s = %d, bPressedJump %u, mode %u, at (%.1f, %.1f, %.1f)", side(),
              static_cast<unsigned long long>(c94_now()), p.side, what, result, pressed_jump(character) ? 1u : 0u,
              p.mode, p.at.x, p.at.y, p.at.z);
}

void detour_c94_end_ability(void* ability, int32_t handle, const void* actor_info, const void* activation,
                            bool replicate, bool cancelled) {
    c439_note_end(ability, handle, cancelled);
    if (g_c94_log && c94_watched(ability) && c94_line()) { // card 439: the hook is also installed under UBWCOOP_DIAG
        log::line("card094: %s t=%llu EndAbility %s (handle %d), replicate %d, cancelled %d", side(),
                  static_cast<unsigned long long>(c94_now()), class_name(ability).text, handle, replicate ? 1 : 0,
                  cancelled ? 1 : 0);
    }
    g_orig_c94_end_ability(ability, handle, actor_info, activation, replicate, cancelled);
}

bool detour_c94_commit(void* ability, int32_t handle, const void* actor_info, const void* activation,
                       void* failure_tags) {
    const bool ok = g_orig_c94_commit(ability, handle, actor_info, activation, failure_tags);
    if (c94_watched(ability) && c94_line()) {
        log::line("card094: %s t=%llu CommitAbility %s (handle %d) = %d", side(),
                  static_cast<unsigned long long>(c94_now()), class_name(ability).text, handle, ok ? 1 : 0);
    }
    return ok;
}

void* detour_c94_mantle_search(void* ability, float a, float b, float c, float d, float e, float f, float g,
                               void* actor) {
    void* const task = g_orig_c94_mantle_search(ability, a, b, c, d, e, f, g, actor);
    if (c94_line()) {
        const PawnState p = pawn_state(actor);
        log::line("card094: %s t=%llu %s SearchForMantleLocation by %s (%.1f %.1f %.1f %.1f %.1f %.1f %.1f) task %p, "
                  "mode %u, at (%.1f, %.1f, %.1f)",
                  side(), static_cast<unsigned long long>(c94_now()), p.side, class_name(ability).text,
                  static_cast<double>(a), static_cast<double>(b), static_cast<double>(c), static_cast<double>(d),
                  static_cast<double>(e), static_cast<double>(f), static_cast<double>(g), task, p.mode, p.at.x,
                  p.at.y, p.at.z);
    }
    return task;
}

int32_t detour_c94_gameplay_event(void* asc, uint64_t tag, const void* payload) {
    C104Policy policy{};
    c104_policy_set(asc, -1, "HandleGameplayEvent", &policy);
    const int32_t n = g_orig_c94_gameplay_event(asc, tag, payload);
    c104_policy_restore(policy);
    if (!g_c94_log) {
        return n; // this detour is always installed (card 104 policy): no name lookup per event without the log
    }
    const ObjectName name = fname_text(&tag);
    if ((text_contains(name.text, "Mantle") || text_contains(name.text, "Jump")) && c94_line()) {
        const PawnState p = pawn_state(read_ptr(asc, kComponentOwner));
        log::line("card094: %s t=%llu %s HandleGameplayEvent %s = %d abilities, mode %u, at (%.1f, %.1f, %.1f)",
                  side(), static_cast<unsigned long long>(c94_now()), p.side, name.text, n, p.mode, p.at.x, p.at.y,
                  p.at.z);
    }
    return n;
}

void detour_c94_client_end(void* asc, int32_t handle, const void* activation) {
    if (c94_line()) {
        const uint8_t* const spec = c21_spec_for(asc, handle);
        log::line("card094: %s t=%llu ClientEndAbility(handle %d) ability %s", side(),
                  static_cast<unsigned long long>(c94_now()), handle,
                  class_name(spec != nullptr ? read_ptr(spec, 0x10) : nullptr).text);
    }
    g_orig_c94_client_end(asc, handle, activation);
}

// An owned-tag count change that can trigger an ability (OwnedTagAdded/Present triggers), jump and mantle tags only.
void detour_c94_tag_changed(void* asc, uint64_t tag, int32_t count) {
    const ObjectName name = fname_text(&tag);
    if ((text_contains(name.text, "Jump") || text_contains(name.text, "Mantl")) && c94_line()) {
        const PawnState p = pawn_state(read_ptr(asc, kComponentOwner));
        log::line("card094: %s t=%llu %s MonitoredTagChanged %s count %d", side(),
                  static_cast<unsigned long long>(c94_now()), p.side, name.text, count);
    }
    g_orig_c94_tag_changed(asc, tag, count);
}

} // namespace ubw::hooks

// ---- Card 101: the mantle lerp and the server copy's client-authoritative mantle window ----
// GA_Mantling_Move moves the pawn with a USTAbilityTaskLerpActorAlongCurve: SetActorLocation every tick, outside
// CharacterMovement. On the host the server copy's lerp runs out of phase with the client's, and the ServerMove
// simulation in between corrects the client back off the ledge. While a server copy's mantle is active its CMC takes
// the client's position (bIgnoreClientMovementErrorChecksAndCorrection and bServerAcceptClientAuthoritativePosition).
// The activation opens the window, each lerp tick and the lerp's end extend it, and the CMC's own tick closes it once
// it times out, restoring the two bits. Logging needs UBWCOOP_C94_LOG=1.
namespace ubw::hooks {

C101LerpTickFn g_orig_c101_lerp_tick = nullptr;
C101LerpEndFn g_orig_c101_lerp_end = nullptr;

namespace {

constexpr uint32_t kC101Lines = 300;
uint32_t g_c101_lines = 0;
constexpr size_t kLerpElapsed = 0x90;
constexpr size_t kLerpDuration = 0x94;
constexpr size_t kLerpTarget = 0xB0;
constexpr size_t kLerpActor = 0xC8;
constexpr size_t kCmcClientAuthFlags = 0x555; // ServerCheckClientError tests bit 0x2, ServerShouldUseAuthoritativePosition 0x4
constexpr uint8_t kCmcClientAuthBits = 0x06;  // bIgnoreClientMovementErrorChecksAndCorrection | bServerAcceptClientAuthoritativePosition
constexpr ULONGLONG kC101OpenMs = 600;        // activation to the first lerp tick (the mantle search runs first)
constexpr ULONGLONG kC101TickMs = 250;        // after each lerp tick
constexpr ULONGLONG kC101EndMs = 150;         // after the lerp's end: the client's last moves of the mantle
constexpr int32_t kC101Windows = 4;           // one per mantling server copy

struct C101Window {
    uint8_t* cmc; // nullptr: free slot
    uint8_t was;  // the two bits before the window
    ULONGLONG until;
};
C101Window g_c101_windows[kC101Windows] = {};

bool c101_line() {
    return g_c94_log && note_event_cap(g_c101_lines, kC101Lines, "card101");
}

// Host: open or extend the window of a server copy's CMC.
void c101_window(const void* pawn, ULONGLONG ms, const char* why) {
    auto* const cmc = static_cast<uint8_t*>(pawn != nullptr ? read_ptr(pawn, kCharacterMovement) : nullptr);
    if (cmc == nullptr || g_client_session || sprint_side(pawn) != kSideServerCopy) {
        return;
    }
    int32_t slot = -1;
    for (int32_t i = 0; i < kC101Windows; ++i) {
        if (g_c101_windows[i].cmc == cmc || (slot < 0 && g_c101_windows[i].cmc == nullptr)) {
            slot = i;
        }
    }
    if (slot < 0) {
        return; // all slots busy: this mantle stays server-corrected
    }
    C101Window& w = g_c101_windows[slot];
    if (w.cmc != cmc) {
        w = C101Window{cmc, static_cast<uint8_t>(cmc[kCmcClientAuthFlags] & kCmcClientAuthBits), 0};
        cmc[kCmcClientAuthFlags] = static_cast<uint8_t>(cmc[kCmcClientAuthFlags] | kCmcClientAuthBits);
        if (c101_line()) {
            log::line("card101: host t=%llu mantle window opens (%s), cmc %p", static_cast<unsigned long long>(c94_now()),
                      why, static_cast<void*>(cmc));
        }
    }
    const ULONGLONG until = c94_now() + ms;
    w.until = until > w.until ? until : w.until;
}

} // namespace

bool c101_in_mantle() {
    const ULONGLONG now = c94_now();
    for (const C101Window& w : g_c101_windows) {
        if (w.cmc != nullptr && now < w.until) {
            return true;
        }
    }
    return false;
}

void c101_note_try(const void* asc, const void* ability, bool ok, bool server) {
    if (!ok || !server || g_client_session || !text_contains(class_name(ability).text, "Mantling_Move")) {
        return;
    }
    c101_window(read_ptr(asc, kComponentOwner), kC101OpenMs, "GA_Mantling_Move activated");
}

// Host, card 105: a server copy being destroyed frees its window slot (its CMC goes with it, so nothing is written).
// actor == nullptr (map change): every slot is freed.
void c101_forget(const void* actor) {
    bool any = false;
    for (const C101Window& w : g_c101_windows) {
        any = any || w.cmc != nullptr;
    }
    if (!any || (actor != nullptr && sprint_side(actor) != kSideServerCopy)) {
        return; // the common case: no window open, or not a server copy (only pawns are read below)
    }
    const void* const cmc = actor != nullptr ? read_ptr(actor, kCharacterMovement) : nullptr;
    for (C101Window& w : g_c101_windows) {
        if (w.cmc != nullptr && (actor == nullptr || (cmc != nullptr && w.cmc == cmc))) {
            if (actor != nullptr && c101_line()) {
                log::line("card101: host t=%llu mantle window freed: %s destroyed, cmc %p",
                          static_cast<unsigned long long>(c94_now()), object_name(actor).text, cmc);
            }
            w = C101Window{};
        }
    }
}

// Host, before each CMC tick: the CMC is live here, so its timed-out window closes on it.
void c101_cmc_tick(void* cmc) {
    for (C101Window& w : g_c101_windows) {
        if (w.cmc != cmc || c94_now() < w.until) {
            continue;
        }
        w.cmc[kCmcClientAuthFlags] =
            static_cast<uint8_t>((w.cmc[kCmcClientAuthFlags] & ~kCmcClientAuthBits) | w.was);
        if (c101_line()) {
            log::line("card101: host t=%llu mantle window closes, cmc %p", static_cast<unsigned long long>(c94_now()),
                      cmc);
        }
        w = C101Window{};
    }
}

void detour_c101_lerp_tick(void* task, float dt, const double* offset) {
    const void* const actor = read_ptr(task, kLerpActor);
    // Only a player pawn is read: the task can lerp any actor, and pawn_state reads Character fields.
    const bool player = actor != nullptr && is_a(actor, g_player_mi_class());
    const PawnState before = player && g_c94_log ? pawn_state(actor) : PawnState{kSideText[kSideOther], 0xFFu, Vec3{}};
    g_orig_c101_lerp_tick(task, dt, offset);
    if (!player) {
        return;
    }
    c101_window(actor, kC101TickMs, "lerp tick");
    if (!c101_line()) {
        return;
    }
    const PawnState after = pawn_state(actor);
    const auto* const t = static_cast<const uint8_t*>(task);
    float elapsed = 0.0f;
    float duration = 0.0f;
    memcpy(&elapsed, t + kLerpElapsed, sizeof(elapsed));
    memcpy(&duration, t + kLerpDuration, sizeof(duration));
    const Vec3 target = read_vec3(task, kLerpTarget);
    log::line("card101: %s t=%llu %s lerp tick %.3f/%.3f dt %.4f, target (%.1f, %.1f, %.1f), mode %u, (%.1f, %.1f, "
              "%.1f) -> (%.1f, %.1f, %.1f)",
              side(), static_cast<unsigned long long>(c94_now()), after.side, static_cast<double>(elapsed),
              static_cast<double>(duration), static_cast<double>(dt), target.x, target.y, target.z, after.mode,
              before.at.x, before.at.y, before.at.z, after.at.x, after.at.y, after.at.z);
}

void detour_c101_lerp_end(void* task, const double* location) {
    const void* const actor = read_ptr(task, kLerpActor);
    g_orig_c101_lerp_end(task, location);
    if (actor == nullptr || !is_a(actor, g_player_mi_class())) {
        return;
    }
    c101_window(actor, kC101EndMs, "lerp end");
    if (!c101_line()) {
        return;
    }
    const PawnState p = pawn_state(actor);
    const Vec3 at = read_vec3(location, 0);
    log::line("card101: %s t=%llu %s EndLocationLerp at (%.1f, %.1f, %.1f), pawn mode %u at (%.1f, %.1f, %.1f)", side(),
              static_cast<unsigned long long>(c94_now()), p.side, at.x, at.y, at.z, p.mode, p.at.x, p.at.y, p.at.z);
}

} // namespace ubw::hooks
