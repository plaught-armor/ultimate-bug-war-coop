// Card 181: the host copy of a client's soldier weapon fires exactly the client's rounds. The client reports each
// round of its own weapon (one USTWeaponFireAbility::Fire that made at least one FireProjectile) with
// ChanOp::Round; the host copy's Fire runs only against a reported round. A DoAction of the copy with no round
// waiting is refused (Fire spends ammo, cooldown, projectiles, sound and PostFire only after its entry, so a refused
// call spends nothing) and is fired late when the report arrives, if its ability instance is still active.
//
// Mechs and mounted turrets are not gated on either side: their relay presses again every tick (c24_mech_repress)
// and their fire ability fires one round per press, so their copies already follow the client's presses.

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

WeaponFireFn g_orig_weapon_fire = nullptr;

namespace {

constexpr uint32_t kC181Lines = 400;     // press end rows per run (one per primary press)
constexpr uint32_t kC181WarnLines = 16;  // each WARN class per run
constexpr uint8_t kInstancedPerActor = 1; // EGameplayAbilityInstancingPolicy::InstancedPerActor
constexpr ULONGLONG kC343StragglerMs = 500; // card 343: a Round this soon after the pawn's primary release
constexpr uint32_t kC343Lines = 64;

struct C181Row {
    int32_t pawn_id;    // InternalIndex of the server-copy pawn; only meaningful while used
    uint32_t credits;   // rounds the client reported and the copy has not fired yet
    C31Weak owed;       // the ability instance of the last refused Fire; serial 0 = none
    uint32_t client_rounds; // Round reports since the row was cleared
    uint32_t copy_rounds;   // copy Fire calls let through (at its own DoAction or late)
    uint32_t refused;
    uint32_t late;
    uint32_t forced;    // card 437 part A: rounds fired outside the copy's fire ability (c437_force_fire)
    bool used;
};
struct C181State {
    C181Row rows[kC17Slots];
    uint32_t sends;      // client: Round reports sent
    uint32_t end_lines;
    uint32_t warn_lines;
    uint32_t fault_lines;
};
C181State g_c181{};

// True for the pawns this card gates: soldier player pawns (not mechs, turrets or bug forms).
bool c181_soldier(const void* shooter) {
    return shooter != nullptr && is_a(shooter, g_player_mi_class()) && !is_a(shooter, g_mech_class());
}

C181Row* c181_find(int32_t pawn_id) {
    for (C181Row& r : g_c181.rows) {
        if (r.used && r.pawn_id == pawn_id) {
            return &r;
        }
    }
    return nullptr;
}

// The pawn's row, made when missing: a free row first, else an idle one (no credit, nothing owed). Null when the
// table is full of busy rows (logged).
C181Row* c181_row(const void* pawn) {
    const int32_t id = object_index(pawn);
    C181Row* row = c181_find(id);
    for (int32_t k = 0; k < kC17Slots && row == nullptr; ++k) {
        row = !g_c181.rows[k].used ? &g_c181.rows[k] : nullptr;
    }
    for (int32_t k = 0; k < kC17Slots && row == nullptr; ++k) {
        const C181Row& r = g_c181.rows[k];
        row = r.credits == 0 && r.owed.serial == 0 ? &g_c181.rows[k] : nullptr;
    }
    if (row == nullptr) {
        if (note_event_cap(g_c181.warn_lines, kC181WarnLines, "card181 warn")) {
            log::line("WARN card181: host round table full (%d rows busy): %s not gated this time", kC17Slots,
                      object_name(pawn).text);
        }
        return nullptr;
    }
    if (!row->used || row->pawn_id != id) {
        *row = C181Row{id, 0u, C31Weak{}, 0u, 0u, 0u, 0u, 0u, true};
    }
    return row;
}

// The ActiveCount of the spec on `asc` whose ability has the class of `ability`; -1 when there is none.
constexpr int32_t kC181MaxSpecs = 64;
int32_t c181_spec_active(const void* asc, const void* ability) {
    const auto* const items = static_cast<const uint8_t*>(asc != nullptr ? read_ptr(asc, kAscSpecs) : nullptr);
    const int32_t num = asc != nullptr ? read_i32(asc, kAscAbilityNum) : 0;
    const int32_t n = items == nullptr || num <= 0 ? 0 : (num < kC181MaxSpecs ? num : kC181MaxSpecs);
    const ObjectName want = class_name(ability);
    for (int32_t k = 0; k < n; ++k) {
        const uint8_t* const spec = items + static_cast<size_t>(k) * kSpecBytes;
        if (strcmp(class_name(read_ptr(spec, kSpecAbility)).text, want.text) == 0) {
            return spec[kSpecActiveCount];
        }
    }
    return -1;
}

// The refused ability instance of `row`, when it can be fired now: still alive (weak ref), instanced per actor (the
// instance lives as long as its spec; a per-execution instance is never fired late) and its spec still active.
void* c181_owed_ready(const C181Row& row, const char** why) {
    void* const ability = row.owed.serial != 0 && g_weak_get != nullptr ? g_weak_get(&row.owed) : nullptr;
    if (ability == nullptr) {
        *why = "ability gone";
        return nullptr;
    }
    if (static_cast<const uint8_t*>(ability)[kC62InstancingPolicy] != kInstancedPerActor) {
        *why = "not instanced per actor";
        return nullptr;
    }
    const void* avatar = nullptr;
    ability_shooter(ability, &avatar);
    const void* const asc = avatar != nullptr ? read_ptr(avatar, kWeaponAsc) : nullptr;
    if (c181_spec_active(asc, ability) <= 0) {
        *why = "ability no longer active";
        return nullptr;
    }
    return ability;
}

// Host: the copy's Fire. A reported round is spent; else the call is refused and remembered.
void c181_copy_fire(void* ability, const void* shooter) {
    C181Row* const row = c181_row(shooter);
    if (row == nullptr) {
        g_orig_weapon_fire(ability);
        return;
    }
    if (row->credits > 0) {
        --row->credits;
        ++row->copy_rounds;
        g_orig_weapon_fire(ability);
        return;
    }
    ++row->refused;
    if (g_weak_assign != nullptr) {
        g_weak_assign(&row->owed, &ability);
    }
}

// Card 437 part A: FNV-1a of a weapon's class name, the weapon a Round names (client and host builds always
// match, and the class name is the same on both sides). Never 0.
uint32_t c437_weapon_key(const void* weapon) {
    const ObjectName name = class_name(weapon);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(name.text) && name.text[i] != '\0'; ++i) {
        h = (h ^ static_cast<uint8_t>(name.text[i])) * 16777619u;
    }
    return h != 0 ? h : 1u;
}

// Client: the own soldier weapon's Fire; one Round to the host when it made a projectile.
void c181_own_fire(void* ability, const void* shooter) {
    const uint32_t before = g_c44_fires[kSideClientOwn];
    const void* weapon = nullptr;
    ability_shooter(ability, &weapon);
    const uint32_t key = weapon != nullptr ? c437_weapon_key(weapon) : 0u; // read before Fire: its last round may drop it
    c439_fire_begin(); // card 439: an Equip this Fire triggers is sent after its Round
    g_orig_weapon_fire(ability);
    if (g_c44_fires[kSideClientOwn] == before) {
        c439_fire_end();
        return; // CommitAbility refused (no ammo, cooldown) or no transform: no round
    }
    void* const pc = read_ptr(shooter, kPawnController);
    if (pc != nullptr && chan_to_host(pc, ChanOp::Round, key)) {
        ++g_c181.sends;
    } else if (note_event_cap(g_c181.warn_lines, kC181WarnLines, "card181 warn")) {
        log::line("WARN card181: client t=%llu round of %s not reported: %s",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(shooter).text,
                  pc != nullptr ? "send failed" : "no controller");
    }
    c439_fire_end();
}

} // namespace

// Both sides: USTWeaponFireAbility::Fire (exe+0x5236320). Only a client's own soldier weapon (report) and the host's
// copy of a client's soldier weapon (gate) are touched; every other caller runs the original as is.
void detour_weapon_fire(void* ability) {
    const void* const shooter = ability_shooter(ability, nullptr);
    const uint32_t who = sprint_side(shooter);
    if (!c181_soldier(shooter) || (who != kSideClientOwn && who != kSideServerCopy) ||
        (who == kSideClientOwn) != g_client_session) {
        g_orig_weapon_fire(ability);
        return;
    }
    if (who == kSideClientOwn) {
        c181_own_fire(ability, shooter);
    } else {
        c181_copy_fire(ability, shooter);
    }
}

namespace {

// Card 343, host: a Round with no primary relay held, within kC343StragglerMs of the pawn's primary release. The
// client's fire ability fires one more round after its release went out (run 16: Fire 0 with 15 rounds at client
// t=329664360, round 16 at 329664374), so the copy has no press left for it and its HitClaim finds no round. The
// copy gets a press for it; the caller releases it with a count of 1 (the card 175 hold). True when pressed.
// Not covered: a second round after the release (the hold's count stays 1), and a weapon change inside the window
// (the press lands on the new weapon).
bool c343_straggler_press(const void* pawn) {
    const int32_t id = object_index(pawn);
    if (c24_find_relay(id, kFirePrimary) >= 0) {
        return false;
    }
    const ULONGLONG now = GetTickCount64();
    long long since = -1;
    for (int32_t k = 0; k < kC24Relays; ++k) {
        const bool own = g_c24.c276_release_pawn[k] == id && g_c24.c276_release_ms[k] != 0;
        since = own ? static_cast<long long>(now - g_c24.c276_release_ms[k]) : since;
    }
    if (since < 0 || since > static_cast<long long>(kC343StragglerMs)) {
        return false;
    }
    c24_relay(pawn, kFirePrimary, true, 0);
    const bool pressed = c24_find_relay(id, kFirePrimary) >= 0;
    static uint32_t lines = 0;
    if (note_event_cap(lines, kC343Lines, "card343")) {
        log::line("%scard343: host t=%llu Round from %s %lld ms after its release: %s",
                  pressed ? "" : "WARN ", static_cast<unsigned long long>(now), object_name(pawn).text, since,
                  pressed ? "one-round press" : "no press (relay skipped), round not fired");
    }
    return pressed;
}

} // namespace

void c181_round_credit(void* pc, const void* pawn, uint32_t weapon_key);

// Card 437 part B, host: fire held across a weapon change. The relay of a held primary press ends when the
// copy's weapon changes (c24_host_guard), and the client sends a press only on a key edge, so its new weapon
// fired with the key still down while the copy had no press (c423b133a: launcher emptied and dropped, Morita
// `shot c seq=3`, host `client rounds 1, copy rounds 0, unused 1`). The pawn is noted at that release; the
// first Round after it, with no client key edge in between, presses the copy's new weapon and the press
// stays held until the client's release. The copy still fires only reported rounds (card 181).
constexpr ULONGLONG kC437CarryMs = 5000; // note lifetime: an equip plus the longest windup (nuke 2 to 3 s)
constexpr uint32_t kC437Lines = 64;
struct C437Note {
    int32_t pawn_id;
    ULONGLONG at_ms; // 0 = free
    uint32_t fired;  // part D: the ended press's rounds (its own plus the ones it carried)
};
static C437Note g_c437[kC17Slots] = {};

void c437_note(const void* pawn, uint32_t fired) {
    if (!c181_soldier(pawn)) {
        return;
    }
    const int32_t id = object_index(pawn);
    C437Note* slot = nullptr;
    for (int32_t k = 0; k < kC17Slots; ++k) {
        const bool take = g_c437[k].at_ms == 0 || g_c437[k].pawn_id == id;
        slot = take && (slot == nullptr || g_c437[k].pawn_id == id) ? &g_c437[k] : slot;
    }
    if (slot != nullptr) { // one note per player slot: never full while notes are dropped on key edges
        *slot = C437Note{id, GetTickCount64(), fired};
    }
}

void c437_drop(int32_t pawn_id) {
    for (int32_t k = 0; k < kC17Slots; ++k) {
        if (g_c437[k].at_ms != 0 && g_c437[k].pawn_id == pawn_id) {
            g_c437[k] = C437Note{};
        }
    }
}

// True when the Round found a note for its pawn and the copy's current weapon got the carried press.
static bool c437_carry_press(const void* pawn) {
    const int32_t id = object_index(pawn);
    ULONGLONG at = 0;
    uint32_t carried = 0; // part D: read before c24_relay, which drops the note
    for (int32_t k = 0; k < kC17Slots; ++k) {
        const bool mine = g_c437[k].at_ms != 0 && g_c437[k].pawn_id == id;
        at = mine ? g_c437[k].at_ms : at;
        carried = mine ? g_c437[k].fired : carried;
    }
    const ULONGLONG now = GetTickCount64();
    if (at == 0 || c24_find_relay(id, kFirePrimary) >= 0) {
        return false;
    }
    if (now - at > kC437CarryMs) {
        c437_drop(id);
        return false;
    }
    c24_relay(pawn, kFirePrimary, true, 0); // drops the note (a press)
    const int32_t relay = c24_find_relay(id, kFirePrimary);
    const bool pressed = relay >= 0;
    if (pressed) {
        g_c24.relays.item[relay].carried = carried; // part D: the client's release counts these rounds too
    }
    static uint32_t lines = 0;
    if (note_event_cap(lines, kC437Lines, "card437 carry")) {
        log::line("%scard437: host t=%llu Round from %s %llu ms after its held press ended on a weapon change: "
                  "%s, %u round(s) of the press before the change", pressed ? "" : "WARN ",
                  static_cast<unsigned long long>(now), object_name(pawn).text, static_cast<unsigned long long>(now - at),
                  pressed ? "press carried to the new weapon" : "no press (relay skipped), round not fired", carried);
    }
    return pressed;
}

// Card 437 part A, host: a Round the copy's fire ability cannot fire. The client's last round and the weapon
// switch it triggers (an empty heavy weapon drops) leave in one frame and the switch is handled first, so the
// copy's fire ability never activates for that round (c423b133a: `client rounds 2, copy rounds 1, unused 1`, the
// client's second nuke never existed on the host). With the switch ability active on the copy pawn, the copy's
// weapon still the one the Round names and its fire ability idle, the round is fired at once on the fire spec's
// primary instance (InstancedPerActor only). Fire reads no active flag; a commit that fails (no ammo, cooldown,
// no actor info) returns before any projectile, and the round then stays a credit as before.
constexpr int32_t kC437MaxSpecs = 64;
constexpr uint32_t kC437ForceLines = 64;

// The first spec on `asc` whose ability class name holds `part`, or null.
static const uint8_t* c437_spec(const void* asc, const char* part) {
    const auto* const items = static_cast<const uint8_t*>(asc != nullptr ? read_ptr(asc, kAscSpecs) : nullptr);
    const int32_t num = asc != nullptr ? read_i32(asc, kAscAbilityNum) : 0;
    const int32_t n = items == nullptr || num <= 0 ? 0 : (num < kC437MaxSpecs ? num : kC437MaxSpecs);
    for (int32_t k = 0; k < n; ++k) {
        const uint8_t* const spec = items + static_cast<size_t>(k) * kSpecBytes;
        if (strstr(class_name(read_ptr(spec, kSpecAbility)).text, part) != nullptr) {
            return spec;
        }
    }
    return nullptr;
}

// True when the round was fired here (a projectile was made).
static bool c437_force_fire(C181Row& row, const void* pawn, uint32_t weapon_key) {
    const void* weapon = nullptr;
    const void* const asc = c24_weapon_asc(pawn, &weapon);
    const void* const pawn_asc = g_get_asc != nullptr ? g_get_asc(pawn, true) : nullptr;
    if (asc == nullptr || weapon == nullptr || weapon_key == 0 || c437_weapon_key(weapon) != weapon_key) {
        return false;
    }
    // Card 439 form 3 (c423b140a): the client's fire ability ran on the press with no windup (FireProjectile
    // 10 ms after the press; every press after a card 430 reload retry did so, 5 of 5 runs), while the copy
    // started its 2 s SpoolUp. The client's reload press and its AmmoMirror (magazine 0) reached the copy inside
    // that windup, so the copy's Fire found no ammo: `client rounds 1, copy rounds 0, unused 1`, the nuke never
    // existed on the host. A Round that finds the copy still in SpoolUp is fired at once, as during a switch;
    // the copy's own Fire at the windup's end then finds no credit and is refused as before.
    const bool in_switch = c276_spec_active(pawn_asc, "WeaponSwitchAbility") > 0;
    const bool in_spool = !in_switch && c276_spec_active(asc, "SpoolUp") > 0;
    if (!in_switch && !in_spool) {
        return false;
    }
    const uint8_t* const spec = c437_spec(asc, "FireAbility");
    void* const instance = spec != nullptr && spec[kSpecActiveCount] == 0 ? c73_spec_instance(spec) : nullptr;
    if (instance == nullptr || static_cast<const uint8_t*>(instance)[kC62InstancingPolicy] != kInstancedPerActor) {
        return false;
    }
    const uint32_t before = g_c44_fires[kSideServerCopy];
    g_orig_weapon_fire(instance);
    const bool made = g_c44_fires[kSideServerCopy] != before;
    if (made) {
        ++row.forced;
        ++row.copy_rounds;
    }
    static uint32_t lines = 0;
    if (note_event_cap(lines, kC437ForceLines, "card437 forced")) {
        log::line("%scard437: host t=%llu Round from %s during its %s, fire ability idle: %s on %s",
                  made ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), object_name(pawn).text,
                  in_switch ? "weapon switch" : "spool-up (card 439 form 3)",
                  made ? "fired at once" : "forced Fire made no projectile, kept as a credit",
                  class_name(weapon).text);
    }
    return made;
}

// Card 439 diagnostic, host: a Round that found a refused Fire owed and still became a credit (c423b137a: the
// client's last nuke, `client rounds 1, copy rounds 0, refused 1, late-fired 0, forced 0, unused 1`). One row
// with the reason the owed Fire was not fired and each input of the forced fire.
constexpr uint32_t kC439CreditLines = 32;
static void c439_credit_row(const void* pawn, uint32_t weapon_key, const char* why) {
    static uint32_t lines = 0;
    if (!note_event_cap(lines, kC439CreditLines, "card439 credit")) {
        return;
    }
    const void* weapon = nullptr;
    const void* const asc = c24_weapon_asc(pawn, &weapon);
    const void* const pawn_asc = g_get_asc != nullptr ? g_get_asc(pawn, true) : nullptr;
    const uint8_t* const spec = c437_spec(asc, "FireAbility");
    log::line("card439: host t=%llu Round from %s with a refused Fire owed became a credit: owed %s; weapon %s, key "
              "match %d, switch ability active %d, fire spec active %d, spool-down active %d",
              static_cast<unsigned long long>(GetTickCount64()), object_name(pawn).text, why,
              weapon != nullptr ? class_name(weapon).text : "none",
              weapon != nullptr && c437_weapon_key(weapon) == weapon_key ? 1 : 0,
              c276_spec_active(pawn_asc, "WeaponSwitchAbility"), spec != nullptr ? spec[kSpecActiveCount] : -1,
              c276_spec_active(asc, "SpoolDown"));
}

// Host: ChanOp::Round from the client that owns pc; the argument names the client's weapon (c437_weapon_key).
// A refused Fire still active is fired now; else one credit.
void c181_receive_round(void* pc, uint32_t weapon_key) {
    const void* const pawn = read_ptr(pc, kControllerPawn);
    const bool gated = sprint_side(pawn) == kSideServerCopy && c181_soldier(pawn);
    if (gated && c437_carry_press(pawn)) { // held until the client's release; the copy's Fire is owed, below
        c181_round_credit(pc, pawn, weapon_key);
        return;
    }
    if (gated && c343_straggler_press(pawn)) { // the press may run the copy's Fire: refused, owed, fired below
        c181_round_credit(pc, pawn, weapon_key);
        c24_relay(pawn, kFirePrimary, false, 1); // released once the copy fired it, or at the card 175 bound
        return;
    }
    c181_round_credit(pc, pawn, weapon_key);
}

// Card 587 diag, host, log only: a PC's possessed non-trooper pawn and the trooper its Instigator names, with the
// trooper's roles, controller and equipped weapon. `where` names the calling row. Capped per run.
static uint32_t g_c587_lines = 0;
constexpr uint32_t kC587Lines = 16;
void c587_trooper_diag(const char* where, const void* pc) {
    if (!note_event_cap(g_c587_lines, kC587Lines, "card587 diag")) {
        return;
    }
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    const auto* const trooper =
        pawn != nullptr ? static_cast<const uint8_t*>(read_ptr(pawn, kActorInstigator)) : nullptr;
    const bool is_trooper = trooper != nullptr && is_a(trooper, g_player_character_class());
    const void* const ctrl = is_trooper ? read_ptr(trooper, kPawnController) : nullptr;
    const void* const weapons =
        is_trooper && is_a(trooper, g_player_mi_class()) ? read_ptr(trooper, kMiWeaponInventory) : nullptr;
    const void* const weapon = weapons != nullptr && g_get_equipped_weapon != nullptr ? g_get_equipped_weapon(weapons)
                                                                                       : nullptr;
    log::line("card587: host t=%llu %s: pc %s pawn %s, instigator %s (trooper %d), LocalRole %d RemoteRole %d, "
              "controller %s (pc's %d), weapon %s",
              static_cast<unsigned long long>(GetTickCount64()), where, object_name(pc).text,
              object_name(pawn).text, object_name(trooper).text, is_trooper ? 1 : 0,
              is_trooper ? trooper[kActorLocalRole] : -1, is_trooper ? trooper[kActorRemoteRole] : -1,
              object_name(ctrl).text, ctrl != nullptr && ctrl == pc ? 1 : 0, object_name(weapon).text);
}

// Host: one reported round for the copy's row.
void c181_round_credit(void* pc, const void* pawn, uint32_t weapon_key) {
    C181Row* const row = sprint_side(pawn) == kSideServerCopy && c181_soldier(pawn) ? c181_row(pawn) : nullptr;
    if (row == nullptr) {
        if (note_event_cap(g_c181.warn_lines, kC181WarnLines, "card181 warn")) {
            log::line("WARN card181: host t=%llu Round from %s ignored: pawn %s has no round row",
                      static_cast<unsigned long long>(GetTickCount64()), object_name(pc).text,
                      object_name(pawn).text);
        }
        c587_trooper_diag("card181 no round row", pc); // card 587 diag
        return;
    }
    ++row->client_rounds;
    const char* why = "nothing owed";
    const bool had_owed = row->owed.serial != 0;
    void* const owed = had_owed ? c181_owed_ready(*row, &why) : nullptr;
    row->owed = C31Weak{};
    if (owed != nullptr) {
        ++row->late;
        ++row->copy_rounds;
        g_orig_weapon_fire(owed);
        return;
    }
    if (c437_force_fire(*row, pawn, weapon_key)) {
        return;
    }
    if (had_owed) {
        c439_credit_row(pawn, weapon_key, why);
    }
    if (row->credits >= kFireRoundsMax) {
        if (note_event_cap(g_c181.fault_lines, kC181WarnLines, "card181 fault")) {
            log::line("WARN card181: host %s credits at the cap %u: a Round dropped (%s)", object_name(pawn).text,
                      kFireRoundsMax, why);
        }
        return;
    }
    ++row->credits;
}

// Host: rounds the client reported for this pawn that no copy Fire has spent yet (c276_repress, card 181 gap).
uint32_t c181_credits(int32_t pawn_id) {
    const C181Row* const row = c181_find(pawn_id);
    return row != nullptr ? row->credits : 0;
}

// Card 439, host: an Equip for a copy whose row holds a reported round its Fire has not spent (the Round came
// before the copy's fire ability reached Fire; the equip would cancel that ability and the round would stay an
// unused credit). One credit is fired now on the current weapon's fire instance (InstancedPerActor only), as
// card 437 part A does; a Fire that makes no projectile leaves the credit.
constexpr uint32_t kC439EquipLines = 32;
void c439_before_equip(const void* pawn) {
    C181Row* const row = c181_soldier(pawn) ? c181_find(object_index(pawn)) : nullptr;
    if (row == nullptr || row->credits == 0) {
        return;
    }
    const void* weapon = nullptr;
    const void* const asc = c24_weapon_asc(pawn, &weapon);
    const uint8_t* const spec = c437_spec(asc, "FireAbility");
    void* const instance = spec != nullptr ? c73_spec_instance(spec) : nullptr;
    if (instance == nullptr || static_cast<const uint8_t*>(instance)[kC62InstancingPolicy] != kInstancedPerActor) {
        return;
    }
    const uint32_t before = g_c44_fires[kSideServerCopy];
    g_orig_weapon_fire(instance);
    const bool made = g_c44_fires[kSideServerCopy] != before;
    if (made) {
        --row->credits;
        ++row->forced;
        ++row->copy_rounds;
    }
    static uint32_t lines = 0;
    if (note_event_cap(lines, kC439EquipLines, "card439 equip")) {
        log::line("%scard439: host t=%llu Equip for %s with a reported round unspent: %s on %s", made ? "" : "WARN ",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(pawn).text,
                  made ? "fired before the equip" : "forced Fire made no projectile, credit kept",
                  weapon != nullptr ? class_name(weapon).text : "none");
    }
}

// Host: a primary press, the primary relay's release or drop, or the pawn going: the pawn's row ends with one line.
void c181_clear(int32_t pawn_id, const char* who, const char* why) {
    C181Row* const row = c181_find(pawn_id);
    if (row == nullptr) {
        return;
    }
    const C181Row r = *row;
    *row = C181Row{};
    if (r.client_rounds + r.copy_rounds + r.refused == 0) {
        return;
    }
    if (note_event_cap(g_c181.end_lines, kC181Lines, "card181 press end")) {
        log::line("card181: host t=%llu %s press ended (%s): client rounds %u, copy rounds %u, refused %u, late-fired "
                  "%u, forced %u, unused %u%s",
                  static_cast<unsigned long long>(GetTickCount64()), who, why, r.client_rounds, r.copy_rounds,
                  r.refused, r.late, r.forced, r.credits, r.owed.serial != 0 ? ", a refused round still owed" : "");
    }
}

} // namespace ubw::hooks
