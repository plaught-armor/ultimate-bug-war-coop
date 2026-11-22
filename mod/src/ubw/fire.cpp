// Fire relay: the client fire relay (card 024), its transport over the mod channel with the edge lines and
// ping/pong (card 026), the server copy aiming its shots from the client's own view (card 024 step 3), and
// the mech fire check (card 076, log only).

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

SpecHandleRpcFn g_orig_server_input_pressed = nullptr;
SpecHandleRpcFn g_orig_server_input_released = nullptr;
EndAbilityRpcFn g_orig_server_end_ability = nullptr;
CancelAbilityRpcFn g_orig_server_cancel_ability = nullptr;

void detour_server_input_pressed(void* asc, int32_t handle) {
    g_orig_server_input_pressed(asc, handle);
    c24_log_rpc("ServerSetInputPressed", asc, handle, 0);
}

void detour_server_input_released(void* asc, int32_t handle) {
    g_orig_server_input_released(asc, handle);
    c24_log_rpc("ServerSetInputReleased", asc, handle, 0);
}

void detour_server_end_ability(void* asc, int32_t handle, const void* activation_info, const void* prediction_key) {
    g_orig_server_end_ability(asc, handle, activation_info, prediction_key);
    c24_log_rpc("ServerEndAbility", asc, handle, 0);
}

void detour_server_cancel_ability(void* asc, int32_t handle, const void* activation_info) {
    g_orig_server_cancel_ability(asc, handle, activation_info);
    c24_log_rpc("ServerCancelAbility", asc, handle, 0);
}

// ---- Card 026: the card 024 fire relay over the mod channel ----
// The transport is ue/chan.h, the op set ubw/chan_ops.h; the handler tables are below c26_pong.
constexpr int32_t kChanMaxChars = 64;    // card 024 PlayerState name log buffer

constexpr int32_t kC24WeaponSlots = 8;
constexpr uint32_t kC24RelayLines = 60;
constexpr uint32_t kC26EdgeLines = 60;
constexpr uint32_t kC26PingLines = 80;
// Card 175: the longest the host holds a copy's release for the client's rounds. The weapon's fire interval is not
// read here; the copy trails the client by about one round (c172-r1: the client's round at +92 ms of a 118 ms press),
// and 300 ms covers two or three rounds of an automatic rifle.
constexpr ULONGLONG kC175HoldMs = 300;
constexpr uint32_t kC175HoldLines = 40;
constexpr ULONGLONG kC276RepressMs = 100; // card 276: a press with no round this long after it (and each try) re-presses
constexpr uint32_t kC276Represses = 3;    // card 276: tries per press
constexpr uint32_t kC276Lines = 60;       // card 276: press lines and re-press lines, each per run
constexpr uint32_t kC352Lines = 40;       // card 352: refused press rows, each side, per run
constexpr uint32_t kC276DiagLines = 400;  // card 276 diag: press end rows and reload rows (one per press), each per run
void c276_note_release(int32_t pawn_id, ULONGLONG now);
constexpr uint32_t kC178Lines = 16;
constexpr uint32_t kC180Lines = 16;
constexpr size_t kConnState = 0x134;   // UNetConnection::State (EConnectionState, low byte)
constexpr uint8_t kUsockOpen = 3;      // USOCK_Open
constexpr size_t kVtFlushNet = 0x308;  // UNetConnection::FlushNet vtable slot (97)
using FlushNetFn = void (*)(void* connection, bool ignore_simulation);

LogoutFn g_orig_logout = nullptr;
AscInputTagFn g_orig_asc_tag_released = nullptr;
GetWeaponBySlotFn g_get_weapon_by_slot = nullptr;

C24State g_c24{};

// Card 076: the input tag FName that a fire kind presses.
uint64_t c24_kind_tag(int32_t kind) {
    if (kind == kFireAds) {
        return fname_cached(g_c24.ads_tag, L"InputTag.ADS");
    }
    if (kind == kFireReload) {
        return fname_cached(g_c24.reload_tag, L"InputTag.Reload");
    }
    return kind == kFireMechAlt ? fname_cached(g_c24.mech_alt_tag, L"InputTag.MechSecondaryAbility")
                                : fname_cached(g_c24.fire_tag, L"InputTag.Fire");
}

// Card 352: WeaponSystem.Blocked count on the pawn's ASC plus its equipped weapon's ASC (0 = not blocked, -1 = no
// count). The game's fire, reload and underslung abilities (BP_WeaponFireAbility, BP_WeaponReloadAbility,
// GA_Weapon_UnderslungAbility) do not start under this tag, which a held support item (GA_UseTacticalSupportAbility,
// GA_GrenadeThrowAbility, GA_HumanMelee) puts on the pawn. ADS is not gated: no ADS ability names the tag.
int32_t c352_blocked(const void* pawn, int32_t kind) {
    if (kind == kFireAds || pawn == nullptr || g_c310_tag_count == nullptr || g_get_asc == nullptr) {
        return kind == kFireAds ? 0 : -1;
    }
    const uint64_t tag = fname_cached(g_c24.blocked_tag, L"WeaponSystem.Blocked");
    const void* weapon = nullptr;
    const void* const weapon_asc = c24_weapon_asc(pawn, &weapon);
    const void* const pawn_asc = g_get_asc(pawn, true);
    const int32_t on_pawn = pawn_asc != nullptr && tag != 0 ? g_c310_tag_count(pawn_asc, tag) : 0;
    const int32_t on_weapon = weapon_asc != nullptr && tag != 0 && weapon_asc != pawn_asc ? g_c310_tag_count(weapon_asc, tag) : 0;
    return (on_pawn > 0 ? on_pawn : 0) + (on_weapon > 0 ? on_weapon : 0);
}

// Card 160: the relay line text for a kind's press and release.
const char* c24_kind_text(int32_t kind, bool press) {
    if (kind == kFireAds) {
        return press ? "press (ADS 1)" : "release (ADS 0)";
    }
    if (kind == kFireReload) {
        return press ? "press (reload)" : "release (reload)";
    }
    if (kind == kFireMechAlt) {
        return press ? "press (mech secondary)" : "release (mech secondary)";
    }
    return press ? "press (Fire 1)" : "release (Fire 0)";
}

uint32_t c24_fire_arg(int32_t kind, bool on) {
    return (static_cast<uint32_t>(kind) << 1) | (on ? kFireOnBit : 0u);
}

void c26_edge_line(const char* fmt_what, uint32_t value, const char* why) {
    if (note_event_cap(g_c24.edge_lines, kC26EdgeLines, "card026 fire edge")) {
        log::line("card026: %s t=%llu %s %u (%s)", side(), static_cast<unsigned long long>(GetTickCount64()),
                  fmt_what, value, why);
    }
}

// The own pawn when this call is a fire tag on that pawn's weapon ASC (host-own on the host, client-own on the
// client), else null; *kind gets its fire kind. The pawn ASC gets the same tag every frame too; only the weapon ASC
// call counts. Card 076: InputTag.MechSecondaryAbility counts on a mech, on the client only (the host's own mech
// presses its own weapon and needs no frame).
// Card 196: the first tags pressed on an own bug pawn's form ASC (which tag a bug attack uses is in its assets, not
// readable statically). One line per new (ASC, tag), capped per run.
constexpr uint32_t kC196PressLines = 12;
struct C196Press {
    uint32_t lines;
    int32_t asc_id;
    uint64_t key;
};
C196Press g_c196_press{}; // game thread

void c196_note_bug_press(const void* asc, const void* tag) {
    const void* const owner = asc != nullptr ? read_ptr(asc, kComponentOwner) : nullptr;
    const void* const pawn = owner != nullptr ? weapon_pawn(owner) : nullptr;
    if (pawn == nullptr || c196_inventory_at(pawn) != kBugFormsInventory || is_a(owner, g_player_character_class())) {
        return;
    }
    uint64_t key = 0;
    memcpy(&key, tag, sizeof(key));
    if (key == g_c196_press.key && object_index(asc) == g_c196_press.asc_id) {
        return;
    }
    g_c196_press.key = key;
    g_c196_press.asc_id = object_index(asc);
    if (note_event_cap(g_c196_press.lines, kC196PressLines, "card196 bug press")) {
        log::line("card196: %s t=%llu bug form press %s on %s (%s) of %s (%s)", side(),
                  static_cast<unsigned long long>(GetTickCount64()), fname_text(tag).text, object_name(owner).text,
                  class_name(owner).text, object_name(pawn).text, kSprintSideText[sprint_side(pawn)]);
    }
}

const void* c26_own_fire_pawn(const void* asc, const void* tag, int32_t* kind) {
    uint64_t key = 0;
    memcpy(&key, tag, sizeof(key));
    const uint64_t fire = c24_kind_tag(kFirePrimary);
    const uint64_t alt = g_client_session ? c24_kind_tag(kFireMechAlt) : 0;
    const uint64_t ads = g_client_session ? c24_kind_tag(kFireAds) : 0;
    // Card 274: both sides. Card 276: the client's reload goes from its reload ability's activation
    // (c276_reload_activation), not the key (an empty magazine reloads with no key press).
    const uint64_t reload = g_client_session ? 0 : c24_kind_tag(kFireReload);
    if (asc == nullptr || key == 0 || (key != fire && key != alt && key != ads && key != reload)) {
        return nullptr;
    }
    *kind = key == fire ? kFirePrimary : (key == alt ? kFireMechAlt : (key == ads ? kFireAds : kFireReload));
    const void* const owner = read_ptr(asc, kComponentOwner);
    // Card 125: the client's own mounted turret presses Fire on the turret's own ASC (its weapon is the host's only).
    if (g_client_session && is_a(owner, g_turret_class())) {
        return *kind == kFirePrimary && sprint_side(owner) == kSideClientOwn ? owner : nullptr;
    }
    const void* const pawn = owner != nullptr ? weapon_pawn(owner) : nullptr;
    const uint32_t own_side = g_client_session ? kSideClientOwn : kSideHostOwn;
    // Card 196: a weapon ASC of an MI or bug pawn (a bug form is an AADWeapon); never the pawn's own ASC.
    const size_t at = c196_inventory_at(pawn);
    if (is_a(owner, g_player_character_class()) || at == 0 || sprint_side(pawn) != own_side) {
        return nullptr;
    }
    const bool mech = is_a(pawn, g_mech_class());
    if (*kind == kFireAds || *kind == kFireReload) { // card 160/274: a soldier weapon only (not a mech, not a bug form)
        return mech || at != kMiWeaponInventory ? nullptr : pawn;
    }
    return *kind == kFirePrimary || mech ? pawn : nullptr;
}

void detour_asc_tag_released(void* asc, const void* tag) {
    c73_drop_stale(asc, tag);
    if (g_c76_log) {
        c76_tag(asc, tag, false);
    }
    c26_own_fire(asc, tag, false);
    g_orig_asc_tag_released(asc, tag);
}

// Both sides, per tick: a held own fire whose pawn is gone or replaced gets its release frame.
void c26_own_check(void* world) {
    void* const pc = g_get_player_controller(world, 0);
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    for (int32_t kind = 0; kind < kFireKinds; ++kind) {
        C26Own& own = g_c24.own[kind];
        if (own.held && object_index(pawn) != own.pawn_id) {
            own.held = false;
            c26_send_fire(world, pc, kind, false, "own pawn changed");
        }
    }
}

// Host: the pawn's weapon with this InternalIndex, searched in its inventory slots, or null.
const void* c24_weapon_by_id(const void* pawn, int32_t weapon_id) {
    const void* const inventory = c24_inventory(pawn);
    for (int32_t slot = 0; inventory != nullptr && slot < kC24WeaponSlots; ++slot) {
        const void* const weapon = g_get_weapon_by_slot(inventory, static_cast<uint8_t>(slot));
        if (weapon != nullptr && object_index(weapon) == weapon_id) {
            return weapon;
        }
    }
    return nullptr;
}

int32_t c24_find_relay(int32_t pawn_id, int32_t kind) {
    for (int32_t i = 0; i < g_c24.relays.count; ++i) {
        if (g_c24.relays.item[i].pawn_id == pawn_id && g_c24.relays.item[i].kind == kind) {
            return i;
        }
    }
    return -1;
}

void c24_relay_line(const char* what, const void* pawn, const void* weapon) {
    if (note_event_cap(g_c24.relay_lines, kC24RelayLines, "card024 relay")) {
        log::line("card024: host relay %s t=%llu: pawn %s, weapon %s", what,
                  static_cast<unsigned long long>(GetTickCount64()), object_name(pawn).text, object_name(weapon).text);
    }
}

// Card 276 diag, host: one row per primary press as its relay ends: the copy's magazine at the press, the re-presses
// it took and the rounds the copy fired for it (re-presses run only while 0 rounds are fired, so rounds above 0 after
// a re-press mean the re-press produced them). A null pawn (gone, or a map reset) is named by its id.
void c276_end_line(const C24Relay& r, const void* pawn, const char* why) {
    if (!note_event_cap(g_c24.c276_end_lines, kC276DiagLines, "card276 press end")) {
        return;
    }
    char who[kChanNameMax + 16];
    if (pawn != nullptr) {
        snprintf(who, sizeof(who), "%s", object_name(pawn).text);
    } else {
        snprintf(who, sizeof(who), "pawn %d", r.pawn_id);
    }
    log::line("card276: host t=%llu press end on %s: magazine at the press %d, re-presses %u, rounds %u, %s (%s)",
              static_cast<unsigned long long>(GetTickCount64()), who, r.press_mag, r.represses,
              r.fired, r.represses == 0 ? "no re-press" : (r.fired > 0 ? "re-press produced a round" : "re-press "
              "produced no round"), why);
}

// Host: release relay i on the weapon it pressed (found again by id; a missing weapon is logged) and drop it.
void c24_release(int32_t i, const void* pawn, const char* why) {
    const C24Relay r = g_c24.relays.item[i];
    g_c24.relays.item[i] = g_c24.relays.item[--g_c24.relays.count];
    if (r.kind == kFirePrimary) {
        c276_end_line(r, pawn, why); // card 276 diag, also when the pressed weapon is gone
        c181_clear(r.pawn_id, object_name(pawn).text, why); // card 181: the press's round row ends
    }
    const void* const weapon = pawn != nullptr ? c24_weapon_by_id(pawn, r.weapon_id) : nullptr;
    void* const asc = weapon != nullptr ? read_ptr(weapon, kWeaponAsc) : nullptr;
    if (asc == nullptr) {
        log::line("card024: host relay release (%s) skipped: pressed weapon %d not found (pawn %s)", why, r.weapon_id,
                  object_name(pawn).text);
        return;
    }
    const uint64_t tag = c24_kind_tag(r.kind);
    g_orig_asc_tag_released(asc, &tag);
    c24_relay_line(why, pawn, weapon);
    if (r.kind == kFirePrimary) { // card 276: the press line's "ms after the last release"
        c276_note_release(r.pawn_id, GetTickCount64());
    }
}

// Card 276, host: the ActiveCount of the first ability spec on a weapon ASC whose ability class name holds `part`
// ("FireAbility", "ReloadAbility"); -1 when the ASC has no such spec.
constexpr int32_t kC276MaxSpecs = 64;
int32_t c276_spec_active(const void* asc, const char* part) {
    const auto* const items = static_cast<const uint8_t*>(asc != nullptr ? read_ptr(asc, kAscSpecs) : nullptr);
    const int32_t num = asc != nullptr ? read_i32(asc, kAscAbilityNum) : 0;
    const int32_t n = items == nullptr || num <= 0 ? 0 : (num < kC276MaxSpecs ? num : kC276MaxSpecs);
    for (int32_t k = 0; k < n; ++k) {
        const uint8_t* const spec = items + static_cast<size_t>(k) * kSpecBytes;
        if (strstr(class_name(read_ptr(spec, kSpecAbility)).text, part) != nullptr) {
            return spec[kSpecActiveCount];
        }
    }
    return -1;
}

void c276_note_release(int32_t pawn_id, ULONGLONG now) {
    int32_t at = -1;
    for (int32_t k = 0; k < kC24Relays && at < 0; ++k) {
        at = g_c24.c276_release_pawn[k] == pawn_id && g_c24.c276_release_ms[k] != 0 ? k : -1;
    }
    if (at < 0) {
        at = g_c24.c276_release_next;
        g_c24.c276_release_next = (g_c24.c276_release_next + 1) % kC24Relays;
    }
    g_c24.c276_release_pawn[at] = pawn_id;
    g_c24.c276_release_ms[at] = now;
}

// Card 276, host: every relayed primary press, with the copy's fire ability state before the press and the time since
// that pawn's last primary release (-1: none yet).
void c276_press_line(const void* pawn, const void* asc, ULONGLONG now) {
    if (!note_event_cap(g_c24.c276_press_lines, kC276Lines, "card276 press")) {
        return;
    }
    long long since = -1;
    for (int32_t k = 0; k < kC24Relays; ++k) {
        const bool own = g_c24.c276_release_pawn[k] == object_index(pawn) && g_c24.c276_release_ms[k] != 0;
        since = own ? static_cast<long long>(now - g_c24.c276_release_ms[k]) : since;
    }
    log::line("card276: host press with the copy's fire ability active %d, %lld ms after the last release",
              c276_spec_active(asc, "FireAbility"), since);
}

// Card 276, host (c24_host_guard, per tick): a held primary relay of a soldier weapon whose copy has fired no round
// kC276RepressMs after its press (and after each try), not in a copy reload, gets its tag released and pressed again,
// at most kC276Represses times per press. A press that lands while the copy's fire ability from the press before is
// still active starts nothing (card 243 run A: 0 of 4 and 0 of 5 rounds).
// Card 181 run a: the gate counts from the later of the press and the last try or reload skip (r.repress_ms), so a
// long reload does not let every try pass on consecutive ticks. No try while the fire ability is active: an active
// ability whose Fire card 181 refused waits for its Round, and a release would end its automatic loop. An ability
// left active by the press before ends by itself; the try runs on the first tick that sees it inactive.
void c276_repress(C24Relay& r, const void* pawn, const void* weapon, ULONGLONG now) {
    const bool soldier = !is_a(pawn, g_mech_class()) && !is_a(pawn, g_turret_class());
    const ULONGLONG since = r.repress_ms > r.press_ms ? r.repress_ms : r.press_ms;
    if (r.kind != kFirePrimary || !soldier || now - since < kC276RepressMs) {
        return;
    }
    // Card 181 gap: a held weapon whose fire ability fires one round per activation (b19 run 2: client rounds 3,
    // copy rounds 1, unused 2). Once the copy has fired, a round the client reported that no copy Fire has spent
    // (a credit waiting) gets the same re-press, bounded by the credits and not by kC276Represses.
    const uint32_t waiting = r.fired != 0 ? c181_credits(r.pawn_id) : 0;
    if (r.fired != 0 ? waiting == 0 : r.represses >= kC276Represses) {
        return;
    }
    void* const asc = read_ptr(weapon, kWeaponAsc);
    const uint64_t tag = c24_kind_tag(kFirePrimary);
    if (asc == nullptr || tag == 0) {
        return;
    }
    if (c276_spec_active(asc, "ReloadAbility") > 0) {
        r.repress_ms = now; // the first try comes kC276RepressMs after the reload ends
        return;
    }
    // Card 296: on a spool weapon the press starts the SpoolUp ability, and the fire ability follows only when the
    // windup animation ends (EPulse88 about 0.5 s, nuke launcher 2 to 3 s). A re-press inside the windup released the
    // tag (the spool's `Interupt`) and started the fire ability at once, so the copy fired with no windup (host
    // 83206353 spool, 83206463 fire). No try while SpoolUp or SpoolDown is active; the gate counts from its end.
    // Sibling sweep (asset list): the only windup or charge abilities on player weapons are
    // BP_Weapon_SpoolUpAbility_NukeLauncher / _EPulse88 and their SpoolDown (parents GA_Weapon_SpoolUpAbility /
    // GA_Weapon_SpoolDownAbility); the one other charge ability is BP_BugForm_ChargeAbility_Warrior (a bug form, not
    // a soldier weapon, so not on this path).
    const int32_t spool_up = c276_spec_active(asc, "SpoolUp");
    const int32_t spool_down = c276_spec_active(asc, "SpoolDown");
    if (spool_up > 0 || spool_down > 0) {
        if (r.repress_ms <= r.press_ms && note_event_cap(g_c24.c296_hold_lines, kC276Lines, "card296 hold")) {
            log::line("card296: host t=%llu re-press of %s held during the spool (SpoolUp active %d, SpoolDown "
                      "active %d, %llu ms after the press)",
                      static_cast<unsigned long long>(now), object_name(pawn).text, spool_up, spool_down,
                      static_cast<unsigned long long>(now - r.press_ms));
        }
        r.repress_ms = now; // the first try comes kC276RepressMs after the spool ends
        return;
    }
    // Card 284 step 5d: -1 (the ASC has no FireAbility spec) is not "idle": no re-press then, as with one active.
    const int32_t active = c276_spec_active(asc, "FireAbility");
    if (active != 0) {
        return;
    }
    ++r.represses;
    r.repress_ms = now;
    g_orig_asc_tag_released(asc, &tag);
    g_orig_asc_tag_pressed(asc, &tag);
    if (note_event_cap(g_c24.c276_repress_lines, kC276Lines, "card276 re-press")) {
        log::line("card276: host re-pressed Fire on %s (try %u, %llu ms after the press, fire ability active %d, "
                  "%s %u)",
                  object_name(pawn).text, r.represses, static_cast<unsigned long long>(now - r.press_ms), active,
                  waiting != 0 ? "credit waiting" : "no round yet, credits", waiting);
    }
}

// Card 276, client (pl_activation, after InternalTryActivateAbility): the own soldier weapon's reload ability
// activated. The host's copy gets a reload press and release (Fire kind 3), so a reload with no key press (an empty
// magazine) reaches the copy too.
void c276_reload_activation(const void* asc, const void* ability, bool ok, bool server_request) {
    if (!g_client_session || !ok || server_request || ability == nullptr ||
        strncmp(class_name(ability).text, "BP_WeaponReloadAbility", 22) != 0) {
        return;
    }
    const void* const owner = asc != nullptr ? read_ptr(asc, kComponentOwner) : nullptr;
    const void* const pawn = owner != nullptr ? weapon_pawn(owner) : nullptr;
    if (pawn == nullptr || sprint_side(pawn) != kSideClientOwn || c196_inventory_at(pawn) != kMiWeaponInventory ||
        is_a(pawn, g_mech_class())) {
        return;
    }
    void* const pc = read_ptr(pawn, kPawnController);
    c26_send_fire(pawn, pc, kFireReload, true, "reload ability");
    c26_send_fire(pawn, pc, kFireReload, false, "reload ability");
    ++g_c24.c276_reload_sends; // card 276 diag: the card 026 edge rows are capped at kC26EdgeLines (run b4a: after 2)
    if (note_event_cap(g_c24.c276_reload_lines, kC276DiagLines, "card276 reload")) {
        log::line("card276: client t=%llu reload ability %s: reload press and release sent (%u so far)",
                  static_cast<unsigned long long>(GetTickCount64()), class_name(ability).text,
                  g_c24.c276_reload_sends);
    }
}

// Card 296 diagnostic, client: the own soldier weapon's spool-up, from its activation to its end, then up to
// kC296FireWaitMs for the fire ability. One row per spool: what ended it (a client-own DoAction or AnimationEnded
// from the card 104 task hooks, or the fire input's release inside the windup, which cancels it with no event), the
// own primary press and release times against the spool's start, and whether the fire ability activated.
constexpr ULONGLONG kC296FireWaitMs = 1000; // a fire activation this long after the spool's end still counts
constexpr ULONGLONG kC296MaxSpoolMs = 10000; // a spool still active after this is reported as stuck
constexpr uint32_t kC296Rows = 60;          // per run
struct C296Spool {
    uint64_t asc_weak;   // FWeakObjectPtr to the own weapon ASC, 0 when no spool is open
    ObjectName spool, fire;
    ULONGLONG start_ms, end_ms; // end_ms 0 while the spool is active
    uint32_t actions0, ends0;   // client-own task callbacks at the start
    uint32_t actions, ends;     // and at the end
    bool fired;
    const char* end_how;        // static text, set at the end
    ULONGLONG press_ms, release_ms; // the last own primary press and release edges (any weapon)
    uint32_t rows;
};
C296Spool g_c296{};

void c296_row() {
    C296Spool& s = g_c296;
    const uint32_t da = s.actions - s.actions0;
    const uint32_t de = s.ends - s.ends0;
    const bool released = s.release_ms >= s.start_ms && s.release_ms <= s.end_ms;
    const char* const reason = da > 0   ? "DoAction"
                               : de > 0 ? "AnimationEnded"
                               : released ? "fire input released inside the windup, no task callback"
                                          : "no task callback and no release";
    if (note_event_cap(s.rows, kC296Rows, "card296 spool")) {
        // b10: a hold still down at the row printed the stale release edge (release -271397729 ms). No release
        // since the last press prints "held".
        char release[32] = "held";
        if (s.release_ms >= s.press_ms) {
            snprintf(release, sizeof(release), "%lld ms",
                     static_cast<long long>(s.release_ms) - static_cast<long long>(s.start_ms));
        }
        log::line("card296: client t=%llu spool-up %s ended: %s (%s, spool %llu ms, press %lld ms and release %s "
                  "from its start, client-own DoAction +%u AnimationEnded +%u); fire ability %s activated %d",
                  static_cast<unsigned long long>(s.end_ms), s.spool.text, reason, s.end_how,
                  static_cast<unsigned long long>(s.end_ms - s.start_ms),
                  static_cast<long long>(s.press_ms) - static_cast<long long>(s.start_ms), release, da, de,
                  s.fired ? s.fire.text : "none", s.fired ? 1 : 0);
        if (!s.fired) {
            log::line("WARN card296: client spool-up %s ended without a fire activation (%s)", s.spool.text, reason);
        }
    }
    s.asc_weak = 0;
    s.fired = false;
    s.end_ms = 0;
}

void c296_end(ULONGLONG now, const char* how) {
    g_c296.end_ms = now;
    g_c296.end_how = how;
    c104_side_counts(kSideClientOwn, &g_c296.actions, &g_c296.ends);
}

// Client, c26_own_fire: an own primary press or release edge.
void c296_input(bool pressed) {
    (pressed ? g_c296.press_ms : g_c296.release_ms) = GetTickCount64();
}

// Client, pl_activation: the own soldier weapon's SpoolUp opens a spool; a FireAbility on the same ASC closes it.
void c296_activation(const void* asc, const void* ability, bool ok, bool server_request) {
    if (!g_client_session || !ok || server_request || asc == nullptr || ability == nullptr ||
        g_weak_assign == nullptr) {
        return;
    }
    const void* const owner = read_ptr(asc, kComponentOwner);
    const void* const pawn = owner != nullptr ? weapon_pawn(owner) : nullptr;
    if (pawn == nullptr || sprint_side(pawn) != kSideClientOwn) {
        return;
    }
    const ObjectName name = class_name(ability);
    const ULONGLONG now = GetTickCount64();
    if (strstr(name.text, "SpoolUpAbility") != nullptr) {
        if (g_c296.asc_weak != 0) {
            c296_end(g_c296.end_ms != 0 ? g_c296.end_ms : now, "a new spool started");
            c296_row();
        }
        void* const obj = const_cast<void*>(asc);
        g_weak_assign(&g_c296.asc_weak, &obj);
        g_c296.spool = name;
        g_c296.start_ms = now;
        g_c296.end_ms = 0;
        g_c296.fired = false;
        c104_side_counts(kSideClientOwn, &g_c296.actions0, &g_c296.ends0);
        return;
    }
    const void* const open = g_c296.asc_weak != 0 && g_weak_get != nullptr ? g_weak_get(&g_c296.asc_weak) : nullptr;
    if (open == asc && !g_c296.fired && strstr(name.text, "FireAbility") != nullptr) {
        g_c296.fired = true;
        g_c296.fire = name;
    }
}

// Card 437 part C (run c423b136a: client held fire 9000 ms on the nuke launcher, fired, reloaded and spooled again by
// itself at 499025261 with no input edge; the host copy got no press for that second spool, re-pressed only when
// Round 2 arrived 2 s later, and the card 175 bound ended the press before its 2 s windup: unused 1). Client: the own
// soldier weapon's SpoolUp activating while primary fire is held, later than kC437RespoolMs after the press edge, is
// a re-spool. The host gets a release with the rounds so far and a new press, as if the key went up and down, so
// the copy starts its windup now (after its reload: card 276 re-press). Sent directly: the card 178 deferral would
// put the release after the press.
constexpr ULONGLONG kC437RespoolMs = 500; // the press's own spool activates in the press frame
constexpr uint32_t kC437RespoolLines = 40;
uint32_t g_c437_respool_lines = 0;
void c437_respool_activation(const void* asc, const void* ability, bool ok, bool server_request) {
    if (!g_client_session || !ok || server_request || asc == nullptr || ability == nullptr ||
        strstr(class_name(ability).text, "SpoolUpAbility") == nullptr) {
        return;
    }
    const void* const owner = read_ptr(asc, kComponentOwner);
    const void* const pawn = owner != nullptr ? weapon_pawn(owner) : nullptr;
    const C26Own& own = g_c24.own[kFirePrimary];
    const ULONGLONG now = GetTickCount64();
    if (pawn == nullptr || sprint_side(pawn) != kSideClientOwn || is_a(pawn, g_mech_class()) || !own.held ||
        own.pawn_id != object_index(pawn) || now - g_c296.press_ms < kC437RespoolMs) {
        return;
    }
    void* const pc = read_ptr(pawn, kPawnController);
    const uint32_t up = c24_fire_arg(kFirePrimary, false) | c175_press_rounds(false);
    const uint32_t down = c24_fire_arg(kFirePrimary, true) | c175_press_rounds(true);
    const bool sent = chan_to_host(pc, ChanOp::Fire, up) && chan_to_host(pc, ChanOp::Fire, down);
    if (!sent || note_event_cap(g_c437_respool_lines, kC437RespoolLines, "card437 re-spool")) {
        log::line("%scard437: client t=%llu %s while fire is held, %llu ms after the press: release (0x%X) and press "
                  "sent to the host%s",
                  sent ? "" : "WARN ", static_cast<unsigned long long>(now), class_name(ability).text,
                  static_cast<unsigned long long>(now - g_c296.press_ms), up, sent ? "" : " FAILED");
    }
    g_c296.press_ms = now; // the new press for the spool row
}

// Client, every ubw_post_tick: notices the spool's end (its spec no longer active) and writes the row once the fire
// ability activated or kC296FireWaitMs passed.
void c296_tick() {
    if (!g_client_session || g_c296.asc_weak == 0 || g_weak_get == nullptr) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    const void* const asc = g_weak_get(&g_c296.asc_weak);
    if (g_c296.end_ms == 0) {
        const int32_t active = asc != nullptr ? c276_spec_active(asc, "SpoolUp") : -1;
        if (active > 0 && now - g_c296.start_ms < kC296MaxSpoolMs) {
            return;
        }
        c296_end(now, active > 0 ? "still active after 10 s" : active == 0 ? "spec inactive" : "weapon ASC gone");
    }
    if (g_c296.fired || now - g_c296.end_ms > kC296FireWaitMs) {
        c296_row();
    }
}

// Card 175, host: a client's release with its rounds for the press. A count of 0, or a copy already at the count,
// releases at once; else the release waits in the relay for c24_host_guard (c175_hold_due).
// Card 437 part D: the client counts a key press across a weapon change; a press the host carried to the new weapon
// starts at 0, so the rounds of the presses it ended (`carried`) come off the client's count first (never below 0).
void c175_release_or_hold(int32_t i, const void* pawn, uint32_t rounds) {
    C24Relay& r = g_c24.relays.item[i];
    const uint32_t own = rounds > r.carried ? rounds - r.carried : 0u;
    if (own == 0 || r.fired >= own) {
        c24_release(i, pawn, c24_kind_text(r.kind, false));
        return;
    }
    r.wanted = own;
    r.release_ms = GetTickCount64() + kC175HoldMs;
    if (note_event_cap(g_c24.hold_lines, kC175HoldLines, "card175 hold")) {
        log::line("card175: host t=%llu release of %s held: the copy fired %u of the client's %u rounds",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(pawn).text, r.fired, own);
    }
}

// Card 175, host: true when held release i is due (the copy reached the count, or the bound passed).
bool c175_hold_due(const C24Relay& r, ULONGLONG now) {
    return r.release_ms != 0 && (r.fired >= r.wanted || now >= r.release_ms);
}

// Card 175, host: ends held release i, logging whether the copy caught up, the next press ended it (card 276:
// by_press, from c24_relay) or the bound ended it short (c175_hold_due).
void c175_end_hold(int32_t i, const void* pawn, const C24Relay& r, ULONGLONG now, bool by_press) {
    const bool met = r.fired >= r.wanted;
    if (note_event_cap(g_c24.hold_lines, kC175HoldLines, "card175 hold")) {
        log::line("card175: host t=%llu held release of %s %s: the copy fired %u of the client's %u rounds (bound %llu ms)",
                  static_cast<unsigned long long>(now), object_name(pawn).text,
                  met ? "done" : by_press ? "ended by the next press" : "ended short by the bound",
                  r.fired, r.wanted, static_cast<unsigned long long>(kC175HoldMs));
    }
    c24_release(i, pawn, "release (card 175 hold)");
}

// Host: a server-copy FireProjectile; counted on that pawn's held primary relay, if any.
void c175_copy_round(const void* pawn) {
    const int32_t id = object_index(pawn);
    for (int32_t i = 0; i < g_c24.relays.count; ++i) {
        if (g_c24.relays.item[i].pawn_id == id && g_c24.relays.item[i].kind == kFirePrimary) {
            ++g_c24.relays.item[i].fired;
        }
    }
}

// Host: a press or release of one fire kind for one server-copy pawn. A press while already held is ignored; a press
// while a card 175 release is held ends that hold first.
void c24_relay(const void* pawn, int32_t kind, bool fire, uint32_t rounds) {
    const int32_t i = c24_find_relay(object_index(pawn), kind);
    if (kind == kFirePrimary) {
        c437_drop(object_index(pawn)); // card 437: a key edge from the client ends a pending carry
    }
    if (kind == kFireReload && fire) { // card 276 diag: pairs with the client's reload send rows
        ++g_c24.c276_reload_relays;
        if (note_event_cap(g_c24.c276_reload_lines, kC276DiagLines, "card276 reload")) {
            log::line("card276: host t=%llu reload press received for %s (%u so far)%s",
                      static_cast<unsigned long long>(GetTickCount64()), object_name(pawn).text,
                      g_c24.c276_reload_relays, i >= 0 ? ", reload relay already held" : "");
        }
    }
    if (!fire) {
        if (i >= 0) {
            c175_release_or_hold(i, pawn, rounds);
        }
        return;
    }
    if (i >= 0 && g_c24.relays.item[i].release_ms == 0) {
        return;
    }
    if (i >= 0) {
        c175_end_hold(i, pawn, g_c24.relays.item[i], GetTickCount64(), true);
    }
    if (kind == kFirePrimary) {
        c181_clear(object_index(pawn), object_name(pawn).text, "next press"); // card 181: a press starts a clean row
    }
    // Card 352: backstop for a press the client sent while its copy is blocked (a held support item on the copy).
    const int32_t blocked = c352_blocked(pawn, kind);
    if (blocked > 0) {
        if (note_event_cap(g_c24.c352_lines, kC352Lines, "card352 host refuse")) {
            log::line("card352: host t=%llu relay press for %s while its copy is blocked: %s, WeaponSystem.Blocked %d",
                      static_cast<unsigned long long>(GetTickCount64()), object_name(pawn).text,
                      c24_kind_text(kind, true), blocked);
        }
        // Not refused: the client's own rule (c26_own_fire) already held the press back under its own tag. The
        // copy's tag can trail the client's by the round trip; the press goes on and card 276 re-presses it.
    }
    const void* weapon = nullptr;
    void* const asc = c24_weapon_asc(pawn, &weapon);
    void* component = nullptr;
    // Card 125: a mounted turret has no health of its own to gate on; its rider's copy is gated by the card 123 mount.
    const bool turret = is_a(pawn, g_turret_class());
    const float health = turret ? 1.0f : c21_health(static_cast<const uint8_t*>(pawn), &component);
    const uint64_t tag = c24_kind_tag(kind);
    if (asc == nullptr || (component == nullptr && !turret) || health <= 0.0f || g_c24.relays.count >= kC24Relays ||
        tag == 0) {
        log::line("card024: host relay press skipped: pawn %s, weapon %s, weapon ASC %p, health %.1f, relays %d",
                  object_name(pawn).text, object_name(weapon).text, asc, static_cast<double>(health),
                  g_c24.relays.count);
        return;
    }
    const ULONGLONG now = GetTickCount64();
    g_c24.relays.item[g_c24.relays.count++] =
        C24Relay{object_index(pawn), object_index(weapon), kind, 0u, 0u, 0u, now, 0u,
                 weapon != nullptr && g_c274_magazine_count != nullptr ? g_c274_magazine_count(weapon, 1) : -1, 0u, 0u};
    ++g_c24.relayed_presses;
    if (kind == kFirePrimary) {
        c276_press_line(pawn, asc, now);
    }
    g_orig_asc_tag_pressed(asc, &tag);
    c24_relay_line(c24_kind_text(kind, true), pawn, weapon);
}

// Both sides: a Pong for our ping n; logs the round trip.
void c26_pong(const void* from_pc, uint32_t n) {
    const ULONGLONG sent = g_c24.ping.sent_ms[n % kC26PingSlots];
    const ULONGLONG now = GetTickCount64();
    if (note_event_cap(g_c24.ping_lines, kC26PingLines, "card026 ping/pong")) {
        log::line("card026: %s pong %u from pc %s: rtt %llu ms", side(), n, object_name(from_pc).text,
                  static_cast<unsigned long long>(sent != 0 ? now - sent : 0));
    }
    if (sent != 0) {
        c132_ledger_rtt(from_pc, static_cast<uint32_t>(now - sent));
        pl_rtt(static_cast<uint32_t>(now - sent), c303_last_frame_ms()); // card 155, card 583 part 2
    }
}

// --- Card 348: Pongs to pings sent before the host's map arm ---
namespace {
struct C348Ping {
    bool armed;
    uint32_t arm_n;  // the ping counter at the last map arm (pings up to it were sent before the arm)
    uint32_t stale;  // this map's Pongs to older pings
};
C348Ping g_c348{}; // game thread
} // namespace

void c348_ping_arm() {
    g_c348.armed = true;
    g_c348.arm_n = g_c24.ping.last_n;
}

// The counter wraps at kChanArgMax; a ping is after the arm when its distance from arm_n is 1..half the ring.
bool c348_pong_current(uint32_t n) {
    const uint32_t ahead = (n + kChanArgMax - g_c348.arm_n % kChanArgMax) % kChanArgMax;
    const bool current = !g_c348.armed || (ahead != 0 && ahead <= kChanArgMax / 2);
    g_c348.stale += current ? 0u : 1u;
    return current;
}

uint32_t c348_stale_pongs_take() {
    const uint32_t n = g_c348.stale;
    g_c348.stale = 0;
    return n;
}

// Host: a leaving player's per-controller state goes. Its pawn is gone by now (APlayerController::Destroyed runs
// PawnLeavingGame first): the pawn half is in detour_c447_pawn_leaving (vehicles.cpp).
void detour_logout(void* game_mode, void* exiting) {
    c49_logout(exiting);
    c50_logout(exiting);
    c140_pace_logout(exiting);
    c320_logout(exiting); // card 320: a client waited for by the title-screen hold
    g_orig_logout(game_mode, exiting);
}

// Both sides, every kC26PingMs: the client pings the host, the host pings every remote PC.
// Host: the remote PCs in the world now.
uint32_t c154_remote_pcs(void* world) {
    uint32_t n = 0;
    for (int s = 0; s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        n += pc != nullptr && c26_is_remote_pc(pc) ? 1u : 0u;
    }
    return n;
}

void c26_ping_step(void* world, ULONGLONG now) {
    void* const pc0 = g_get_player_controller(world, 0);
    if (!g_client_session) {
        // Card 154: a client connection that just opened gets its first Ping now, not up to kC26PingMs later (the
        // c154-r2 hangar trip waited 3.7 s for it). Its Pong rtt is the Ping-to-Pong time.
        const uint32_t remotes = c154_remote_pcs(world);
        if (remotes > g_c24.ping.remotes) {
            log::line("card154: host t=%llu remote PCs %u -> %u: Ping now (was due in %lld ms)",
                      static_cast<unsigned long long>(now), g_c24.ping.remotes, remotes,
                      static_cast<long long>(g_c24.ping.next_ms) - static_cast<long long>(now));
            g_c24.ping.next_ms = now;
        }
        g_c24.ping.remotes = remotes;
    }
    if (now < g_c24.ping.next_ms || (g_client_session && sprint_side(pc0) != kSideClientOwn)) {
        return;
    }
    g_c24.ping.next_ms = now + kC26PingMs;
    c132_ledger_step(pc0, now);
    const uint32_t n = (g_c24.ping.last_n + 1) % kChanArgMax;
    uint32_t sent = 0;
    if (g_client_session) {
        sent = chan_to_host(pc0, ChanOp::Ping, n) ? 1 : 0;
    } else {
        for (int s = 0; s < kC17Slots; ++s) {
            void* const pc = g_get_player_controller(world, s);
            if (pc != nullptr && c26_is_remote_pc(pc)) {
                sent += chan_to_client(pc, ChanOp::Ping, n) ? 1 : 0;
                c132_ledger_ack_to(pc);
            }
        }
    }
    if (sent == 0) {
        return;
    }
    g_c24.ping.last_n = n;
    g_c24.ping.sent_ms[n % kC26PingSlots] = now;
    if (note_event_cap(g_c24.ping_lines, kC26PingLines, "card026 ping/pong")) {
        log::line("card026: %s ping %u sent to %u pc(s)", side(), n, sent);
    }
}

void c24_log_names(void* world, ULONGLONG now);

// Host, per tick, card 076: a held primary relay on a mech presses again. A held key calls AbilityInputTagPressed
// every frame, and the mech's fire ability fires one round per press (the soldier fire ability loops on one press),
// so one relayed press gave one round. Card 125: a mounted turret's held fire presses every frame too (the client's own
// turret sends HandleInputTag_Pressed each tick while the button is held), so its relay presses again the same way.
void c24_mech_repress(const void* pawn, const void* weapon, const C24Relay& r) {
    if (r.kind != kFirePrimary || (!is_a(pawn, g_mech_class()) && !is_a(pawn, g_turret_class()))) {
        return;
    }
    void* const asc = read_ptr(weapon, kWeaponAsc);
    const uint64_t tag = c24_kind_tag(kFirePrimary);
    if (asc != nullptr && tag != 0) {
        g_orig_asc_tag_pressed(asc, &tag);
    }
}

// Host: release relays whose weapon changed; drop relays whose pawn is gone (nothing left to release on).
void c24_host_guard(void* world) {
    const ULONGLONG now = GetTickCount64();
    for (int32_t i = g_c24.relays.count - 1; i >= 0; --i) {
        const C24Relay r = g_c24.relays.item[i];
        const void* pawn = nullptr;
        for (int s = 0; s < kC17Slots && pawn == nullptr; ++s) {
            const void* const pc = g_get_player_controller(world, s);
            const void* const p = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
            pawn = (p != nullptr && object_index(p) == r.pawn_id) ? p : nullptr;
        }
        const void* weapon = nullptr;
        if (pawn != nullptr) {
            c24_weapon_asc(pawn, &weapon);
        }
        if (pawn == nullptr) {
            log::line("card024: host relay dropped: pawn %d is gone (its weapon is not released)", r.pawn_id);
            for (int s = 0; s < kC17Slots; ++s) { // card 587 diag: a PC now on a pawn whose Instigator is the trooper
                const void* const pc = g_get_player_controller(world, s);
                const void* const p = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
                const void* const inst = p != nullptr ? read_ptr(p, kActorInstigator) : nullptr;
                if (inst != nullptr && inst != p && object_index(inst) == r.pawn_id) {
                    c587_trooper_diag("card024 pawn gone", pc);
                }
            }
            if (r.kind == kFirePrimary) {
                c276_end_line(r, nullptr, "pawn gone"); // card 276 diag
                c181_clear(r.pawn_id, "a gone pawn", "pawn gone"); // card 181
            }
            g_c24.relays.item[i] = g_c24.relays.item[--g_c24.relays.count];
        } else if (object_index(weapon) != r.weapon_id) {
            const bool held = r.kind == kFirePrimary && r.release_ms == 0;
            c24_release(i, pawn, "release (weapon changed)"); // r is this guard's copy: the slot is reused after it
            if (held) {
                c437_note(pawn, r.fired + r.carried); // card 437: the client's key is still down
            }
        } else if (c175_hold_due(r, now)) {
            c175_end_hold(i, pawn, r, now, false);
        } else {
            c24_mech_repress(pawn, weapon, r);
            c276_repress(g_c24.relays.item[i], pawn, weapon, now);
        }
    }
}

// Flushes pc's connection now (UNetConnection::FlushNet through its own vtable slot +0x308); false when the connection
// is missing or not Open. Game thread, outside the driver's TickDispatch/TickFlush. Card 345b uses it on the host: a
// frame sent just before a handler that blocks the game thread (the level-end video's load, 2014 ms in playb68r3)
// otherwise leaves only with the tick's TickFlush, after the block.
bool net_flush_pc(void* pc) {
    void* const conn = pc != nullptr ? read_ptr(pc, kPcNetConnection) : nullptr;
    const uint8_t state = conn != nullptr ? static_cast<uint8_t>(read_i32(conn, kConnState) & 0xff) : 0;
    if (state != kUsockOpen) {
        return false;
    }
    reinterpret_cast<FlushNetFn>(read_ptr(read_ptr(conn, 0), kVtFlushNet))(conn, false);
    return true;
}

// Card 180, client: a release sent from c24_step (after the engine Tick) missed that frame's UNetDriver::TickFlush and
// left one frame late (c176-r3: presses 4-13 ms, releases 20-26 ms), so the host copy held fire one round longer. The
// own connection is flushed at once through its own vtable slot +0x308 (UNetConnection::FlushNet, exe+0x3E6A2F0;
// UIpConnection does not override it). This runs on the game thread from ubw_post_tick, outside the driver's
// TickDispatch/TickFlush. FlushNet with an empty send buffer sends nothing (or a due keepalive). Only an Open connection
// is flushed; a missing or closing one is skipped with a capped line.
void c180_flush_net(void* pc) {
    void* const conn = read_ptr(pc, kPcNetConnection);
    const uint8_t state = conn != nullptr ? static_cast<uint8_t>(read_i32(conn, kConnState) & 0xff) : 0;
    if (state != kUsockOpen) {
        if (note_event_cap(g_c24.flush_skip_lines, kC180Lines, "card180 skip")) {
            log::line("card180: client t=%llu own connection not flushed after the release: %s, state %u",
                      static_cast<unsigned long long>(GetTickCount64()), conn != nullptr ? "not open" : "no connection",
                      static_cast<unsigned>(state));
        }
        return;
    }
    void* const slot = read_ptr(read_ptr(conn, 0), kVtFlushNet);
    if (!g_c24.flush_slot_logged) {
        g_c24.flush_slot_logged = true;
        const uintptr_t rva = reinterpret_cast<uintptr_t>(slot) - reinterpret_cast<uintptr_t>(g_exe_base);
        log::line("card180: own connection %s FlushNet slot +0x308 = exe+0x%llX (UNetConnection::FlushNet exe+0x%X): %s",
                  object_name(conn).text, static_cast<unsigned long long>(rva), offsets::UNetConnection_FlushNet.rva,
                  rva == offsets::UNetConnection_FlushNet.rva ? "base, no override" : "override");
    }
    reinterpret_cast<FlushNetFn>(slot)(conn, false);
    ++g_c24.flushes;
    if (note_event_cap(g_c24.flush_lines, kC180Lines, "card180 flush")) {
        log::line("card180: client t=%llu own connection flushed after the release (%u so far)",
                  static_cast<unsigned long long>(GetTickCount64()), g_c24.flushes);
    }
}

// Card 178, client: the release a primary press is held for, sent now (after the engine Tick) with its round count.
// The own PC is found again by InternalIndex; a changed one gets no release (the host drops the relay with the pawn).
// after_tick (card 180): sent from c24_step, so the own connection is flushed when the release went out; a release
// sent from a press is still inside the frame and leaves with that frame's TickFlush.
void c178_flush_release(void* pc, bool after_tick) {
    if (!g_c24.release_due) {
        return;
    }
    g_c24.release_due = false;
    if (pc == nullptr || object_index(pc) != g_c24.release_pc_id) {
        log::line("WARN card178: client release not sent: own PC %d is gone (now %s)", g_c24.release_pc_id,
                  object_name(pc).text);
        return;
    }
    const uint32_t late = g_c44_fires[kSideClientOwn] - g_c24.release_fired_at;
    if (note_event_cap(g_c24.release_lines, kC178Lines, "card178 release")) {
        log::line("card178: client t=%llu primary release sent after the frame: %u rounds since the press, %u of them "
                  "after AbilityInputTagReleased",
                  static_cast<unsigned long long>(GetTickCount64()), g_c44_fires[kSideClientOwn] - g_c24.press_rounds,
                  late);
    }
    const uint64_t bytes = chan_bytes_sent(static_cast<size_t>(ChanOp::Fire));
    g_c24.release_flushing = true;
    c26_send_fire(pc, pc, kFirePrimary, false, g_c24.release_why);
    g_c24.release_flushing = false;
    if (after_tick && chan_bytes_sent(static_cast<size_t>(ChanOp::Fire)) != bytes) {
        c180_flush_net(pc);
    }
}

// Card 178, client: the fire ability can fire a last round in the frame of the release, after AbilityInputTagReleased:
// the animator's DoAction notify in that frame's montage update calls USTWeaponFireAbility::Fire (exe+0x5236320),
// whose FireProjectile call is at exe+0x5236707 (c176-r2: release at 88297973, round 88297974). A primary release is
// therefore held to the end of the frame, where c24_step (after the engine Tick) sends it with the final count. A
// press while a release is held sends that release first, so the host sees the edges in order. True when held.
bool c178_defer_release(void* own_pc, int32_t kind, bool on, const char* why) {
    if (!g_client_session || kind != kFirePrimary || g_c24.release_flushing) {
        return false;
    }
    if (on) {
        c178_flush_release(own_pc, false);
        return false;
    }
    g_c24.release_due = true;
    g_c24.release_pc_id = object_index(own_pc);
    g_c24.release_fired_at = g_c44_fires[kSideClientOwn];
    g_c24.release_why = why;
    return true;
}

void c24_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    void* const world = g_get_game_world(engine);
    if (world == nullptr) {
        return;
    }
    c24_log_names(world, now);
    if (!g_client_session) {
        c24_host_guard(world);
        c586_frame(world); // card 586: troopers held by a client's seat or missile
    } else {
        c76_client_imc(world);
        c120_client_step(g_get_player_controller(world, 0));
        c430_retry_step(now);
    }
    c26_own_check(world);
    if (g_client_session) {
        c178_flush_release(g_get_player_controller(world, 0), true); // after c26_own_check: its release goes this frame
    }
    c26_ping_step(world, now);
    pl_step(world, now); // card 155
}

// Proof that prefixed strings never reach AGameModeBase::ChangeName: every 20 s, each PC's PlayerState name
// (AController::PlayerState +0x2B0, APlayerState::PlayerNamePrivate +0x340, an FString), capped.
constexpr size_t kControllerPlayerState = 0x2B0;
constexpr size_t kPlayerStateName = 0x340;
constexpr ULONGLONG kC24NamesMs = 20000;
constexpr uint32_t kC24NameLines = 16;
ULONGLONG g_c24_names_next_ms = 0;
uint32_t g_c24_name_lines = 0;

void c24_log_names(void* world, ULONGLONG now) {
    if (now < g_c24_names_next_ms) {
        return;
    }
    g_c24_names_next_ms = now + kC24NamesMs;
    for (int s = 0; s < kC17Slots; ++s) {
        const void* const pc = g_get_player_controller(world, s);
        const void* const ps = pc != nullptr ? read_ptr(pc, kControllerPlayerState) : nullptr;
        const auto* const text = ps != nullptr ? static_cast<const wchar_t*>(read_ptr(ps, kPlayerStateName)) : nullptr;
        const int32_t num = ps != nullptr ? read_i32(ps, kPlayerStateName + 8) : 0;
        char name[kChanMaxChars + 1] = {};
        for (int32_t i = 0; text != nullptr && i < num - 1 && i < kChanMaxChars; ++i) {
            name[i] = text[i] >= 0x20 && text[i] < 0x7F ? static_cast<char>(text[i]) : '?';
        }
        if (pc != nullptr && note_event_cap(g_c24_name_lines, kC24NameLines, "card024 player names")) {
            log::line("card024: %s pc %d player name '%s' (%d chars)", side(), s, name, num > 0 ? num - 1 : 0);
        }
    }
}

GetPlayerViewPointFn g_player_view_point = nullptr;
GetTargetLocationFn g_orig_target_location = nullptr;
DeprojectFn g_orig_deproject = nullptr;

constexpr size_t kMiTargetIface = 0xA30;    // GetTargetLocation_Implementation's this is the pawn + 0xA30
constexpr double kTargetTraceLength = 10000.0; // the original's end = start + dir * 10000

constexpr uint32_t kC24AimLines = 40;

C24Aim g_c24_aim{};

Vec3* detour_target_location(const void* iface, Vec3* out) {
    const void* const pawn = static_cast<const uint8_t*>(iface) - kMiTargetIface;
    if (g_client_session || sprint_side(pawn) != kSideServerCopy) {
        return g_orig_target_location(iface, out);
    }
    ViewPoint view{};
    if (!c24_view_point(pawn, view) || g_actor_get_world(pawn) == nullptr) {
        if (note_event_cap(g_c24_aim.fallback_lines, kC24AimLines, "card024 aim fallbacks")) {
            log::line("card024: host server-copy GetTargetLocation: no controller view point, original used");
        }
        return g_orig_target_location(iface, out);
    }
    const Vec3 end{view.loc.x + view.dir.x * kTargetTraceLength, view.loc.y + view.dir.y * kTargetTraceLength,
                   view.loc.z + view.dir.z * kTargetTraceLength};
    *out = c24_trace(pawn, view.loc, end);
    if (note_event_cap(g_c24_aim.target_lines, kC24AimLines, "card024 aim targets")) {
        log::line("card024: host server-copy GetTargetLocation from own view (%.1f, %.1f, %.1f) dir (%.3f, %.3f, "
                  "%.3f): target (%.1f, %.1f, %.1f)",
                  view.loc.x, view.loc.y, view.loc.z, view.dir.x, view.dir.y, view.dir.z, out->x, out->y, out->z);
    }
    return out;
}

bool detour_deproject(const void* pc, float x, float y, Vec3* loc, Vec3* dir) {
    ViewPoint view{};
    if (g_c24_aim.pawn == nullptr || !c24_view_point(g_c24_aim.pawn, view)) {
        return g_orig_deproject(pc, x, y, loc, dir);
    }
    *loc = view.loc;
    *dir = view.dir;
    if (note_event_cap(g_c24_aim.deproject_lines, kC24AimLines, "card024 zero-threshold deprojections")) {
        log::line("card024: host server-copy zero-threshold deprojection branch ran: view (%.1f, %.1f, %.1f) dir "
                  "(%.3f, %.3f, %.3f) used instead of player 0's screen",
                  view.loc.x, view.loc.y, view.loc.z, view.dir.x, view.dir.y, view.dir.z);
    }
    return true;
}

// --- Card 076: mech fire in co-op, step 1 check (log only, UBWCOOP_C76_LOG=1) ---------------------------------------
// Where each side's mech fire goes: the mech's input tag edges, the ability activations on its ASC (host: inside a
// client's InternalServerTryActivateAbility), FireProjectile with a mech avatar, and near a mech the pooler spawns,
// impact damage and Niagara systems; at 1 Hz, Health changes of the NPCs near a mech. The pooler, damage and plain
// Niagara lines ride the card 074 log detours, so c74_install also runs with this switch (its card 074 lines stay
// off without UBWCOOP_C74_LOG).
constexpr uint32_t kC76Lines = 64;          // per kind, per process
constexpr uint32_t kC76PerAbility = 4;      // activation lines per ability class (CrushProps runs all drive long)

C76State g_c76{}; // game thread

bool c76_note(C76Kind k) {
    return note_event_cap(g_c76.lines[k], kC76Lines, "card076 mech fire");
}

// The actor itself when it is a mech, else its Owner when that is a mech (a mech's weapon), else null.
const void* c76_mech(const void* actor) {
    if (actor == nullptr || is_a(actor, g_mech_class())) {
        return actor;
    }
    const void* const owner = read_ptr(actor, kActorOwner);
    return is_a(owner, g_mech_class()) ? owner : nullptr;
}

// The distance from at to the nearest mech of the last step, or -1 with none.
double c76_mech_dist(const Vec3& at) {
    double best = -1.0;
    for (uint32_t i = 0; i < g_c76.mechs; ++i) {
        const double d = vec_dist(at, g_c76.mech_at[i]);
        best = best < 0.0 || d < best ? d : best;
    }
    return best;
}

bool c76_near_mech(const Vec3* at) {
    const double d = at != nullptr ? c76_mech_dist(*at) : -1.0;
    return d >= 0.0 && d <= kC76NearMech;
}

unsigned long long c76_now() {
    return static_cast<unsigned long long>(GetTickCount64());
}

// The first kC76PerAbility activations of each ability class; false once spent or the table is full.
bool c76_ability_note(const void* cls) {
    for (uint32_t i = 0; i < kC76Abilities; ++i) {
        if (g_c76.ability[i] == nullptr) {
            g_c76.ability[i] = cls;
        }
        if (g_c76.ability[i] == cls) {
            return ++g_c76.ability_lines[i] <= kC76PerAbility;
        }
    }
    return false;
}

// The NPC's slot, taking a free one or one not in range on the last step; null when all are in use.
C76Npc* c76_npc_slot(int32_t id) {
    C76Npc* spare = nullptr;
    for (C76Npc& n : g_c76.npc) {
        if (n.id == id) {
            return &n;
        }
        spare = spare == nullptr && (n.id == 0 || !n.seen) ? &n : spare;
    }
    if (spare != nullptr) {
        *spare = C76Npc{id, -1.0f, false};
    }
    return spare;
}

// One NPC near a mech: a line when its Health differs from the last step (not on first sight).
void c76_npc(const void* actor, double dist, ULONGLONG now) {
    float health = 0.0f;
    const int dead = c25_dead(actor, &health);
    C76Npc* const n = c76_npc_slot(object_index(actor));
    if (n == nullptr) {
        return;
    }
    const float before = n->health;
    n->health = health;
    n->seen = true;
    if (before < 0.0f || before == health || !c76_note(kC76Health)) {
        return;
    }
    log::line("card076: %s t=%llu NPC %s %s Health %.1f -> %.1f dead %d, %.0f uu from a mech", side(),
              static_cast<unsigned long long>(now), class_name(actor).text, object_name(actor).text,
              static_cast<double>(before), static_cast<double>(health), dead, dist);
}

// Host: every held relay of this pawn, of any fire kind, is released.
void c24_release_pawn(const void* pawn, const char* why) {
    const int32_t id = object_index(pawn);
    for (int32_t i = g_c24.relays.count - 1; i >= 0; --i) {
        if (g_c24.relays.item[i].pawn_id == id) {
            c24_release(i, pawn, why);
        }
    }
}

// Client, per tick: the mech contexts follow the own pawn into and out of a mech.
void c76_client_imc(void* world) {
    void* const pc = g_get_player_controller(world, 0);
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    const void* const mech = is_a(pawn, g_mech_class()) && sprint_side(pawn) == kSideClientOwn ? pawn : nullptr;
    const int32_t id = object_index(mech);
    if (id == g_c76_imc.mech_id) {
        return;
    }
    void* const local_player = pc != nullptr ? g_get_local_player(pc) : nullptr;
    auto* const subsystem = local_player != nullptr ? static_cast<uint8_t*>(g_get_enhanced_input(local_player)) : nullptr;
    if (subsystem == nullptr) {
        return; // tried again next tick
    }
    void* const iface = subsystem + kSubsystemInputInterface;
    const int32_t removed = g_c76_imc.count;
    c76_imc_remove(iface);
    if (mech != nullptr) {
        c76_imc_add(iface, mech);
    }
    g_c76_imc.mech_id = id;
    const bool none = mech != nullptr && g_c76_imc.count == 0; // a mech without its contexts: RMB stays ADS
    if (g_c76_log || none) {
        log::line("%scard076: client t=%llu own pawn %s: %d mech input context(s) removed, %d added at priority %d",
                  none ? "WARN " : "", static_cast<unsigned long long>(GetTickCount64()), object_name(pawn).text,
                  removed, g_c76_imc.count, kMechContextPriority);
    }
}

// Card 508, host: a leaver's down entry goes Up on every other remote PC. The clients' table drops an entry only
// after its Up (StandUp window), so without this frame a downed leaver stays Downed there to the map's end and holds
// one of kC43Max rows. Card 509: the guid is the one stored at the entry's last transition; at leave time the
// leaving connection may be the only one and already closing, so a fresh lookup reads 0.
void c508_leaver_up(const void* pc, const void* pawn, const C43Entry& e) {
    const C43Roster r = c43_roster(g_actor_get_world(pawn));
    void* to[kC43Max] = {};
    uint32_t n = 0;
    for (int32_t s = 0; s < kC43Max; ++s) {
        if (r.p[s].pc != nullptr && r.p[s].pc != pc && c26_is_remote_pc(r.p[s].pc)) {
            to[n++] = r.p[s].pc;
        }
    }
    const char* const was = kC43LifeText[static_cast<size_t>(e.state)];
    if (n == 0) {
        log::line("card508: host t=%llu leaver pawn id %d (slot %u) was %s: no other remote PC, no Up sent",
                  static_cast<unsigned long long>(GetTickCount64()), e.pawn_id, e.slot, was);
        return;
    }
    uint32_t lo = 0;
    uint32_t hi = 0;
    if (!c41_guid_args(e.guid, lo, hi)) {
        log::line("WARN card508: host leaver pawn id %d (slot %u) was %s: stored guid 0x%llx not sendable, no Up sent",
                  e.pawn_id, e.slot, was, static_cast<unsigned long long>(e.guid));
        return;
    }
    const ChanArgs args{{lo, hi, static_cast<uint32_t>(C43Life::Up), 0u, e.slot}};
    uint32_t sent = 0;
    for (uint32_t k = 0; k < n; ++k) {
        uint32_t ahead = 0;
        sent += ue::chan::chan_send_client_now(to[k], chan_frame(ChanOp::Down, args), &ahead) ? 1u : 0u;
    }
    g_c43.n.down_sent += sent;
    log::line("card508: host t=%llu leaver pawn id %d (slot %u) was %s: guid 0x%llx, Up sent to %u of %u",
              static_cast<unsigned long long>(GetTickCount64()), e.pawn_id, e.slot, was,
              static_cast<unsigned long long>(e.guid), sent, n);
}

// Logout: the leaving player's entry and holds go.
void c43_logout(const void* pc, const void* pawn) {
    const int32_t id = object_index(pawn);
    const int32_t i = pawn != nullptr ? c43_find(id) : -1;
    if (i >= 0) {
        if (c43_down_state(g_c43.e[i].state)) {
            c508_leaver_up(pc, pawn, g_c43.e[i]);
        }
        c43_remove(i);
    }
    for (int32_t h = g_c43.holds - 1; h >= 0; --h) {
        if (g_c43.hold[h].reviver_id == id || g_c43.hold[h].target_id == id) {
            c43_remove_hold(h);
        }
    }
}

// Host: a leaving PC loses its synced slot (a later PC may reuse its object index).
void c49_logout(const void* pc) {
    const int32_t index = object_index(pc);
    for (int32_t i = 0; pc != nullptr && i < g_c49m.n_synced; ++i) {
        if (g_c49m.synced[i] == index) {
            g_c49m.synced[i] = g_c49m.synced[--g_c49m.n_synced];
            log::line("card049: host %s left: journal sync slot freed", object_name(pc).text);
            return;
        }
    }
}

void c50_logout(const void* pc) {
    const int32_t id = object_index(pc);
    for (int32_t i = 0; i < g_c50.copies; ++i) {
        if (g_c50.copy[i].pc_id == id) {
            g_c50.copy[i] = g_c50.copy[--g_c50.copies];
            return;
        }
    }
}

} // namespace ubw::hooks
