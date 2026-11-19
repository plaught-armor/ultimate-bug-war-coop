// Player-pawn collision (card 014): the collision bit at cold points and the client's own-pawn watch
// and fix.

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

// --- Card 014: player-pawn collision bit at cold points; the client's own-pawn watch and fix ---
// Game thread only. No pawn pointer is kept: the own pawn is looked up per Tick and compared with
// the previous Tick's by FName value only.
constexpr uint32_t kPawnWatchLogs = 8;
constexpr uint32_t kTrackedPawnClasses = 8;
constexpr size_t kCmcDropshipDword = 0x1A0;   // dword EnterDropship +0x90..+0xB9 copies to dropship+0x74C, then zeroes

struct PawnClassesSeen {
    uint32_t count;
    uint64_t name[kTrackedPawnClasses]; // class FName values, for the once-per-class CDO line
};
PawnClassesSeen g_pawn_classes{}; // per process: a CDO does not change between maps

PawnWatch g_pawn_watch = kPawnWatchReset;

int collision_bit(const uint8_t* actor) {
    return actor[kActorCollisionFlags] & kActorEnableCollisionBit;
}

void log_pawn_class_cdo(const void* pawn) {
    const void* const cls = read_ptr(pawn, kObjectClass);
    if (cls == nullptr) {
        return;
    }
    uint64_t name = 0;
    memcpy(&name, static_cast<const uint8_t*>(cls) + kObjectName, sizeof(name));
    for (uint32_t i = 0; i < g_pawn_classes.count; ++i) {
        if (g_pawn_classes.name[i] == name) {
            return;
        }
    }
    if (g_pawn_classes.count == kTrackedPawnClasses) {
        return; // 8 player pawn classes seen: later ones are not logged
    }
    g_pawn_classes.name[g_pawn_classes.count++] = name;
    const auto* const cdo = static_cast<const uint8_t*>(read_ptr(cls, kClassDefaultObject));
    log::line("pawn-collision: %s class %s CDO %p bActorEnableCollision %d", side_text(), object_name(cls).text,
              static_cast<const void*>(cdo), cdo != nullptr ? collision_bit(cdo) : -1);
}

// ASTPlayerCharacter's own overrides: every pawn they see is a player pawn (own or remote).
void log_player_pawn(const char* what, const uint8_t* pawn, int before, const void* controller) {
    log_pawn_class_cdo(pawn);
    if (!note_event_cap(g_player_pawn_logs, kPawnWatchLogs, "player pawn PostInit/PossessedBy")) {
        return;
    }
    log::line("pawn-collision: %s %s(%p %s, LocalRole %u RemoteRole %u, net mode %u, controller %p) "
              "bActorEnableCollision %d -> %d",
              side_text(), what, static_cast<const void*>(pawn), class_name(pawn).text, pawn[kActorLocalRole],
              pawn[kActorRemoteRole], net_mode(pawn), controller, before, collision_bit(pawn));
}

void detour_post_init(void* pawn) {
    g_last_hook = "PostInitializeComponents";
    const int before = collision_bit(static_cast<const uint8_t*>(pawn));
    g_orig_post_init(pawn);
    log_player_pawn("PostInitializeComponents", static_cast<const uint8_t*>(pawn), before, nullptr);
}

void detour_possessed_by(void* pawn, void* controller) {
    g_last_hook = "PossessedBy";
    const int before = collision_bit(static_cast<const uint8_t*>(pawn));
    g_orig_possessed_by(pawn, controller);
    log_player_pawn("PossessedBy", static_cast<const uint8_t*>(pawn), before, controller);
    // Card 019 host baseline: the host's on-foot character after the dropship landing.
    // is_a(PlayerController) first: GetLocalPlayer faults on null and reads garbage on an AIController.
    if (autoinput::host_script() && g_dropship_watch.seen && controller != nullptr &&
        is_a(controller, g_player_controller_class()) && is_local_pc(controller) && !is_net_client(controller) &&
        is_a(pawn, g_character_class()) && autoinput::arm(false)) {
        start_own_rows();
        start_card020_rows();
    }
}

void log_own_pawn_change(const uint8_t* pawn, int bit, bool new_pawn) {
    if (!note_event_cap(g_pawn_watch.change_logs, kPawnWatchLogs, "own pawn collision change")) {
        return;
    }
    const auto* const cmc =
        is_a(pawn, g_character_class()) ? static_cast<const uint8_t*>(read_ptr(pawn, kCharacterMovement)) : nullptr;
    uint32_t dword = 0;
    if (cmc != nullptr) {
        memcpy(&dword, cmc + kCmcDropshipDword, sizeof(dword));
    }
    log::line("pawn-collision: client own pawn %p %s%s: bActorEnableCollision %d -> %d at tick %u since map load, "
              "LocalRole %u, last hook %s, CMC %p +0x1A0 %u",
              static_cast<const void*>(pawn), class_name(pawn).text, new_pawn ? " (new pawn)" : "",
              new_pawn ? -1 : g_pawn_watch.bit, bit, g_pawn_watch.ticks, pawn[kActorLocalRole], g_last_hook,
              static_cast<const void*>(cmc), dword);
}

// The fix: the game's own SetActorEnableCollision (the trampoline, so the hook does not count it),
// which also updates the components' collision.
void fix_own_pawn(uint8_t* pawn) {
    g_orig_set_actor_collision(pawn, true);
    const int after = collision_bit(pawn);
    PawnWatch& w = g_pawn_watch;
    ++w.reenables_map;
    ++w.reenables_row;
    w.bit = after;
    if (note_event_cap(w.fix_logs, kPawnWatchLogs, "own pawn collision FIX")) {
        log::line("%spawn-collision: FIX client own pawn %p %s: SetActorEnableCollision(true), bActorEnableCollision "
                  "0 -> %d at tick %u (re-enable %u this map)",
                  after == 0 ? "WARN " : "", static_cast<void*>(pawn), class_name(pawn).text, after, w.ticks,
                  w.reenables_map);
    }
}

// Logs the start and the end of a hold; at the end, which path brings collision back.
void note_dropship_hold(const uint8_t* pawn, const uint8_t* dropship, int bit) {
    PawnWatch& w = g_pawn_watch;
    if (!note_event_cap(w.hold_logs, kPawnWatchLogs, "own pawn dropship hold")) {
        return;
    }
    if (dropship != nullptr) {
        log::line("pawn-collision: client own pawn %p %s held by dropship %p %s (player-inside 1, state %u) at tick %u, "
                  "bActorEnableCollision %d: guard paused",
                  static_cast<const void*>(pawn), class_name(pawn).text, static_cast<const void*>(dropship),
                  class_name(dropship).text, dropship[kDropshipState], w.ticks, bit);
        return;
    }
    log::line("pawn-collision: client own pawn %p %s released at tick %u, last hook %s, bActorEnableCollision %d: %s",
              static_cast<const void*>(pawn), class_name(pawn).text, w.ticks, g_last_hook, bit,
              bit != 0 ? "game path (collision already back)" : "guard path (guard re-enables collision)");
}

// Client only (armed per client LoadMap), every UGameEngine::Tick after the engine's Tick.
void own_pawn_collision_step(void* engine) {
    PawnWatch& w = g_pawn_watch;
    ++w.ticks;
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    auto* const pawn = pc != nullptr ? static_cast<uint8_t*>(read_ptr(pc, kControllerPawn)) : nullptr;
    if (pawn == nullptr) {
        return;
    }
    const int bit = collision_bit(pawn);
    uint64_t name = 0;
    memcpy(&name, pawn + kObjectName, sizeof(name));
    const bool new_pawn = w.bit < 0 || name != w.pawn_name;
    if (new_pawn || bit != w.bit) {
        log_own_pawn_change(pawn, bit, new_pawn);
        w.pawn_name = name;
        w.bit = bit;
    }
    c399_client_step(pawn, pc); // card 399: the own pawn rides the host's dropship (gravity, input, camera)
    const uint8_t* const dropship = holding_dropship(pawn);
    if ((dropship != nullptr) != w.held) {
        note_dropship_hold(pawn, dropship, bit);
        w.held = dropship != nullptr;
    }
    if (dropship == nullptr && bit == 0 && pawn[kActorLocalRole] == kRoleAutonomousProxy && is_net_client(pawn) &&
        is_a(pawn, g_player_character_class())) {
        fix_own_pawn(pawn);
    }
}

// Hot path (every move of every character): only the watched pawn (watched_pawn) does work.
void detour_perform_movement(void* cmc, float delta_seconds) {
    const auto* const owner = static_cast<const uint8_t*>(read_ptr(cmc, kCmcCharacterOwner));
    const auto* const updated = static_cast<const uint8_t*>(read_ptr(cmc, kMoveUpdatedComponent));
    if (!watched_pawn(owner) || updated == nullptr || g_move_cmc != nullptr) {
        g_orig_perform_movement(cmc, delta_seconds);
        return;
    }
    const unsigned long long caller = exe_rva(_ReturnAddress());
    const Vec3 start = read_vec3(updated, kComponentTranslation);
    note_move_entry(static_cast<const uint8_t*>(cmc), owner, updated);
    g_move_cmc = cmc;
    g_orig_perform_movement(cmc, delta_seconds);
    g_move_cmc = nullptr;
    note_move_exit(static_cast<const uint8_t*>(cmc));
    if (!g_client_session || read_ptr(cmc, kMoveUpdatedComponent) != updated) {
        return; // card 012's own-pawn bookkeeping is client-only; a changed UpdatedComponent is not measured
    }
    note_local_move(owner, updated, start, caller);
}

// Card 012: per-key Enhanced Input entry; PlayerInput exists only for local players, so every
// call is local player input. FInputKeyEventArgs::Key at +0x10, its FName first (InputKey +0x1F).
constexpr size_t kKeyArgsKeyName = 0x10;
constexpr size_t kKeyArgsAmount = 0x2C; // float AmountDepressed

C482Press g_c482_press{};

bool detour_player_input_key(void* player_input, const void* args) {
    ++g_counts.keys;
    const ObjectName key = fname_text(static_cast<const uint8_t*>(args) + kKeyArgsKeyName);
    if (c52_swallow_key(key.text)) {
        return true; // card 052: F8 toggles the lobby panel at the menu; the game never sees it
    }
    float amount = 0.0f; // FInputKeyEventArgs::AmountDepressed (InputKey reads +0x2C)
    memcpy(&amount, static_cast<const uint8_t*>(args) + kKeyArgsAmount, sizeof(amount));
    if (amount > 0.0f && !(strncmp(key.text, "Mouse", 5) == 0 && strstr(key.text, "Button") == nullptr)) {
        snprintf(g_c482_press.key, sizeof(g_c482_press.key), "%s", key.text);
        g_c482_press.t = GetTickCount64();
        g_c482_press.injected = autoinput::injecting();
        g_c482_press.driver_seq = g_c482_press.injected && g_c63.started ? g_c63.q[0].seq : 0;
    }
    if (g_input_window_armed) {
        note_input_window_key(key.text, amount); // card 014: may reset the row counters, so before counting
    }
    const double abs_amount = fabs(static_cast<double>(amount));
    const bool is_x = strcmp(key.text, "MouseX") == 0;
    const bool is_y = strcmp(key.text, "MouseY") == 0;
    g_counts.mouse_x += is_x ? 1u : 0u;
    g_counts.mouse_y += is_y ? 1u : 0u;
    g_counts.mouse_x_nonzero += (is_x && abs_amount > 0.0) ? 1u : 0u;
    g_counts.mouse_y_nonzero += (is_y && abs_amount > 0.0) ? 1u : 0u;
    g_counts.mouse_x_abs += is_x ? abs_amount : 0.0;
    g_counts.mouse_y_abs += is_y ? abs_amount : 0.0;
    g_counts.mouse_2d += strcmp(key.text, "Mouse2D") == 0 ? 1u : 0u;
    return g_orig_player_input_key(player_input, args);
}

void detour_handle_input_move(void* pc, const void* value) {
    ++g_counts.move;
    g_orig_handle_input_move(pc, value);
}

// Bookkeeping for one finished PerformMovement of the client's own pawn.
void note_local_move(const void* owner, const uint8_t* updated, const Vec3& start, unsigned long long caller) {
    MoveDiag& d = g_move_diag;
    const Vec3 end = read_vec3(updated, kComponentTranslation);
    const double moved = vec_dist(start, end);
    ++d.calls;
    d.displacement += moved;
    d.still += moved < kStillTolerance ? 1u : 0u;
    d.updated_is_root = updated == read_ptr(owner, kActorRootComponent) ? 1 : 0;
    d.mobility = updated[kComponentMobility];
    if (d.calls == 1) {
        d.caller_first = caller;
    }
    if (caller == d.caller_first) {
        ++d.calls_first;
    } else {
        d.caller_other = caller;
        ++d.calls_other;
    }
    if (d.have_end && vec_dist(start, d.last_end) > kPinTolerance) {
        if (d.adjust_seen) {
            ++d.pins_after_adjust;
        } else {
            ++d.pins_other;
            d.pin_from = d.last_end;
            d.pin_to = start;
        }
    }
    d.last_end = end;
    d.have_end = true;
    d.adjust_seen = false;
}

// The watched pawn: the client's own (autonomous proxy) pawn, or on the host the remote client's
// pawn (authority here, autonomous proxy on its owner) as the control.
bool watched_pawn(const uint8_t* owner) {
    if (owner == nullptr) {
        return false;
    }
    if (g_client_session) {
        return owner[kActorLocalRole] == kRoleAutonomousProxy;
    }
    return owner[kActorLocalRole] == kRoleAuthority && owner[kActorRemoteRole] == kRoleAutonomousProxy;
}

void note_move_entry(const uint8_t* cmc, const uint8_t* owner, const uint8_t* updated) {
    MoveChain& c = g_chain;
    c.actor_collision = owner[kActorCollisionFlags] & kActorEnableCollisionBit;
    c.body_collision = updated[kBodyCollisionEnabled];
    c.floor_flags = cmc[kCmcCurrentFloorFlags];
    c.mode = cmc[kCmcMovementMode];
    c.controller = read_ptr(owner, kPawnController) != nullptr ? 1 : 0;
    if (c.snap_pending) {
        c.v_before = read_vec3(cmc, kMoveVelocity);
        c.a_before = read_vec3(cmc, kCmcAcceleration);
    }
}

void note_move_exit(const uint8_t* cmc) {
    MoveChain& c = g_chain;
    if (!c.snap_pending) {
        return;
    }
    c.v_after = read_vec3(cmc, kMoveVelocity);
    c.a_after = read_vec3(cmc, kCmcAcceleration);
    c.snap_pending = false;
    c.snap_done = true;
}

void start_own_rows() {
    g_own_rows_left = kOwnRows;
    g_sprint.own_row = 0;
    g_sprint.own_next_ms = GetTickCount64();
    g_sprint.row[kSideHostOwn] = SprintCounts{};
}

// The first nonzero non-mouse key on the local player input after LoadMap. With UBWCOOP_AUTOINPUT
// on, only a scripted key opens it, so the rows cover the script.
void note_input_window_key(const char* key, float amount) {
    if (amount == 0.0f || text_contains(key, "Mouse") || (autoinput::enabled() && !autoinput::injecting())) {
        return;
    }
    log::line("%s: input window opens on key %s (amount %.3f), %u rows follow", g_client_session ? "rot" : "srv", key,
              static_cast<double>(amount), kRotRows);
    open_input_window();
}

// detour_player_input_key: F8 is the panel's at the menu, never the game's.
bool c52_swallow_key(const char* key) {
    if (!g_c52.at_frontend || strcmp(key, "F8") != 0) {
        return false;
    }
    log::line("card052: F8 InputKey event kept from the game");
    return true;
}

} // namespace ubw::hooks
