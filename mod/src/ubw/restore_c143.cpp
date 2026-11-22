// Card 143: a host checkpoint restore in place (LoadFromSlot with no map load) with clients in the session.
// The host's world is the save's after the load; a client's is not, and a resend in place cannot revive what the save
// revived, so each client re-runs the join (the card 045 follow: SetClientTravel to the host, a fresh map load, the
// join replays). The rejoin makes a new server copy with starting state, so the restored copy's state travels with
// the client: the host sends it as Reload before the client leaves, the client sends it back as Restore at the first
// host ping after the reconnect, and the host applies it to the new copy once AssignStartingInventories ran on it.
// Position is not carried: card 046 places the rejoined copy beside the host.

#include <stdio.h>
#include <string.h>

#include "ubw/chan_ops.h"
#include "ubw/hooks_internal.h"

namespace ubw::hooks {

namespace {

constexpr ULONGLONG kC143ApplyLateMs = 5000;  // host: apply without an Assign mark this long after the Restore
constexpr ULONGLONG kC143HoldMaxMs = 30000;   // host: a Restore whose controller never appears is dropped then
constexpr float kC43HeldHealth = 1.0f;        // card 043 holds a downed pawn here; a save row can carry it
constexpr int32_t kC143Marks = kC17Slots;     // host: the newest server copies AssignStartingInventories ran on
constexpr ULONGLONG kC143ReadBackMs = 1000;   // host: card 198, Health read back this long after an apply

size_t c570_items_text(const void* pc, const void* pawn, char* out, size_t cap); // card 570, defined after g_c440
void c570_apply_items(void* pawn, const char* items, ULONGLONG now);

struct C143Hold {
    int32_t pc_id;
    ULONGLONG recv_ms;
    ULONGLONG state_ms; // card 463: when the text's state was read (the load's Reload, or the card 440 keep)
    char text[kChanTextMax + 1];
};

struct C143State {
    C143Hold hold[kC17Slots]; // host: Restore texts waiting for their new copy
    int32_t holds;
    const void* mark[kC143Marks]; // host: server copy pawns whose AssignStartingInventories ran since the last Reload
    int32_t marks;
    bool armed; // host: a Reload went out this session, so copies are marked
    ULONGLONG reload_ms; // host: card 463, the last load in place (the Reload snapshot time)
    char text[kChanTextMax + 1]; // client: the Reload text, held across the rejoin
    bool held;
    uint32_t reconnects_at; // client: g_c45.reconnects when the Reload came
    int32_t check_pc_id = -1; // host: card 198, the controller whose applied Health is read back (-1: none)
    ULONGLONG check_ms;     // host: when the read-back runs
    float check_want;       // host: the Health the apply set
    // b9 (card 198 read-back): the game's own Health changes on the checked pawn since the apply, so a read-back
    // that differs names its cause (damage, a heal, a down) instead of a bare WARN.
    int32_t check_pawn_id = -1;   // host: object index of the pawn the apply set (-1: none)
    const char* check_life;       // host: c43_life_text at the apply (a literal)
    uint32_t check_drops;         // host: Health decreases since the apply
    uint32_t check_rises;         // host: Health increases since the apply
    float check_lost;             // host: the Health those decreases took
};

C143State g_c143{};

bool c143_marked(const void* pawn) {
    for (int32_t i = 0; i < g_c143.marks; ++i) {
        if (g_c143.mark[i] == pawn) {
            return true;
        }
    }
    return false;
}

// "<health> <armour>;<weapons>;<ammo>;<items>" from an MI server copy pawn. When it does not fit, the items part
// (card 570) is left out first, then the ammo part (WARN each). Card 198: any other player pawn (the bug pawn) gives "<health>;;": the weapon and ammo inventories are MI
// fields, and the bug pawn's health set is not verified to hold the Armour attribute c48_armour reads.
int32_t c143_snapshot(void* pawn, char* out, size_t cap) {
    void* component = nullptr;
    const float health = c21_health(static_cast<const uint8_t*>(pawn), &component);
    if (!c192_mi_pawn(pawn, "c143 Reload inventory")) {
        const int n = snprintf(out, cap, "%.1f;;", static_cast<double>(health));
        return n < 0 || static_cast<size_t>(n) >= cap ? -1 : n;
    }
    const C48Armour a = c48_armour(component);
    static char weapons[kChanTextMax + 1];
    static char ammo[kChanTextMax + 1];
    void* const inventory = read_ptr(pawn, kMiWeaponInventory);
    if (inventory == nullptr || c50_text(inventory, weapons, sizeof(weapons)) <= 0) {
        weapons[0] = '\0';
    }
    if (c72_map_text(read_ptr(pawn, kMiAmmoInventory), ammo, sizeof(ammo)) <= 0) {
        ammo[0] = '\0';
    }
    static char items[kChanTextMax + 1];
    c570_items_text(read_ptr(pawn, kPawnController), pawn, items, sizeof(items));
    int n = snprintf(out, cap, "%.1f %.1f;%s;%s;%s", static_cast<double>(health), static_cast<double>(a.armour),
                     weapons, ammo, items);
    if (n < 0 || static_cast<size_t>(n) >= cap) {
        log::line("WARN card570: host snapshot of %s too long (%d chars): items left out", object_name(pawn).text, n);
        n = snprintf(out, cap, "%.1f %.1f;%s;%s", static_cast<double>(health), static_cast<double>(a.armour), weapons,
                     ammo);
    }
    if (n < 0 || static_cast<size_t>(n) >= cap) {
        log::line("WARN card143: host snapshot of %s too long (%d chars): ammo left out", object_name(pawn).text, n);
        n = snprintf(out, cap, "%.1f %.1f;%s;", static_cast<double>(health), static_cast<double>(a.armour), weapons);
    }
    return n < 0 || static_cast<size_t>(n) >= cap ? -1 : n;
}

// Card 371: the Reload text for a client whose copy has no pawn after the load (the save was taken before its pawn
// existed: a map's first checkpoint, a late join). The client still rejoins, on a login pawn, and sends no Restore.
constexpr char kC143NoRow[] = "none";

void c143_send_reload(void* pc, ULONGLONG now) {
    void* const pawn = read_ptr(pc, kControllerPawn);
    static char text[kChanTextMax + 1];
    static wchar_t wire[kChanTextFrame];
    int32_t len = 0;
    if (pawn == nullptr || !is_a(pawn, g_player_character_class())) {
        len = snprintf(text, sizeof(text), "%s", kC143NoRow);
        log::line("card371: host t=%llu %s has no player pawn after the load (%s): Reload with no saved state",
                  static_cast<unsigned long long>(now), object_name(pc).text, object_name(pawn).text);
    } else {
        len = c143_snapshot(pawn, text, sizeof(text));
    }
    const ue::chan::WireString w = len > 0 ? chan_text_frame(ChanOp::Reload, text, wire) : ue::chan::WireString{};
    // Card 294: Reload is a priority op (always): it skips the pace queue, ahead of the load's burst.
    uint32_t ahead = 0;
    const bool sent = w.num > 0 && ue::chan::chan_send_client_now(pc, w, &ahead);
    log::line("%scard143: host t=%llu Reload to %s (copy %s): %s, ahead of %u queued frames: %s", sent ? "" : "WARN ",
              static_cast<unsigned long long>(now), object_name(pc).text, object_name(pawn).text,
              sent ? "sent now" : "not sent", ahead, text);
}

// The Restore text: "<health> <armour>;" (an MI pawn) or "<health>;" (card 198: the bug pawn, no Armour). The
// weapon and ammo parts follow. Null when the text is bad.
const char* c143_parse(const char* text, float& health, float& armour, bool& has_armour) {
    int used = 0;
    has_armour = sscanf_s(text, "%f %f;%n", &health, &armour, &used) == 2 && used > 0;
    if (has_armour) {
        return text + used;
    }
    used = 0;
    return sscanf_s(text, "%f;%n", &health, &used) == 1 && used > 0 ? text + used : nullptr;
}

void c143_set_armour(void* pawn, void* component, float armour) {
    static GameplayAttributeRaw armour_attr{};
    static bool armour_built = false;
    armour_built = armour_built || c25_build_attribute("Armour", &armour_attr);
    void* const asc = component != nullptr ? read_ptr(component, kHealthComponentAsc) : nullptr;
    if (asc != nullptr && armour_built) {
        g_set_numeric_attribute_base(asc, &armour_attr, armour);
    } else {
        log::line("WARN card143: host Armour of %s not set: asc %p, attribute built %d", object_name(pawn).text, asc,
                  armour_built ? 1 : 0);
    }
}

// The MI weapon and ammo parts; card 072 sends the restored counts on the next poll. False: weapons not restored.
bool c143_apply_inventory(void* pc, void* pawn, const char* weapons, const char* ammo, ULONGLONG state_ms,
                          char* ammo_log, size_t cap) {
    void* const inventory = read_ptr(pawn, kMiWeaponInventory);
    const bool weapons_ok =
        inventory != nullptr && weapons[0] != '\0' && c143_apply_weapons(inventory, weapons, state_ms);
    void* const ammo_inventory = read_ptr(pawn, kMiAmmoInventory);
    if (ammo_inventory != nullptr && ammo[0] != '\0') {
        c72_apply(ammo_inventory, ammo, false, "Restore", ammo_log, cap);
    }
    C50Copy* const c = c50_copy(object_index(pc), false);
    if (c != nullptr) {
        // Card 580: the restored counts go to the client as AmmoSet now; waiting for the next poll left the client's
        // reserve at 0 for 309 ms after a checkpoint reload (c397b400a). A failed send retries on the poll.
        c->seed = !c72_seed(pc, pawn);
        c->ammo[0] = '\0';  // the client's mirror of its starting counts is not re-applied over them
    }
    return weapons_ok;
}

// Card 198: on a pawn that is not MI (the bug pawn) only Health is applied; "weapons not restored" is then expected.
void c143_apply(void* pc, void* pawn, const C143Hold& h, bool marked, ULONGLONG now) {
    float health = 0.0f;
    float armour = 0.0f;
    bool has_armour = false;
    const char* const rest = c143_parse(h.text, health, armour, has_armour);
    if (rest == nullptr) {
        log::line("WARN card143: host t=%llu Restore from %s not applied: bad text \"%s\"",
                  static_cast<unsigned long long>(now), object_name(pc).text, h.text);
        return;
    }
    static char weapons[kChanTextMax + 1];
    const char* const semi = strchr(rest, ';');
    const size_t wlen = semi != nullptr ? static_cast<size_t>(semi - rest) : strlen(rest);
    memcpy(weapons, rest, wlen);
    weapons[wlen] = '\0';
    // Card 570: "<ammo>;<items>"; a kept inventory (card 440) and an older text have no items part.
    static char ammo[kChanTextMax + 1];
    const char* const ammo_start = semi != nullptr ? semi + 1 : "";
    const char* const semi2 = strchr(ammo_start, ';');
    const size_t alen = semi2 != nullptr ? static_cast<size_t>(semi2 - ammo_start) : strlen(ammo_start);
    memcpy(ammo, ammo_start, alen);
    ammo[alen] = '\0';
    const char* const items = semi2 != nullptr ? semi2 + 1 : "";
    const bool mi = c192_mi_pawn(pawn, "c143 Restore inventory");
    void* component = nullptr;
    const float health_before = c21_health(static_cast<const uint8_t*>(pawn), &component);
    const float armour_before = mi ? c48_armour(component).armour : -1.0f;
    if (health >= 0.0f) { // card 440: a kept inventory carries no Health
        c43_set_health(pawn, health);
    }
    if (mi && has_armour) {
        c143_set_armour(pawn, component, armour);
    }
    char ammo_log[256] = {};
    const bool weapons_ok = mi && c143_apply_inventory(pc, pawn, weapons, ammo, h.state_ms, ammo_log, sizeof(ammo_log));
    if (mi) {
        c570_apply_items(pawn, items, now);
    }
    const float health_after = c21_health(static_cast<const uint8_t*>(pawn), &component);
    log::line("%scard143: host t=%llu Restore applied to %s (copy %s, %s, %llu ms after it came): Health %.1f -> %.1f "
              "(want %.1f), Armour %.1f -> %.1f (want %.1f%s), weapons %s: %s, ammo %s",
              mi && !(weapons_ok && marked) ? "WARN " : "", static_cast<unsigned long long>(now), object_name(pc).text,
              object_name(pawn).text,
              marked ? "after its AssignStartingInventories" : mi ? "late, no Assign mark" : "no Assign on this class",
              static_cast<unsigned long long>(now - h.recv_ms), static_cast<double>(health_before),
              static_cast<double>(health_after), static_cast<double>(health), static_cast<double>(armour_before),
              static_cast<double>(mi ? c48_armour(component).armour : -1.0f), static_cast<double>(armour),
              has_armour ? "" : ", not carried", weapons_ok ? "restored" : mi ? "not restored" : "not MI, none carried",
              weapons, ammo_log[0] != '\0' ? ammo_log : ammo);
    g_c143.check_pc_id = health >= 0.0f ? h.pc_id : -1; // card 440: no Health set, no read-back
    g_c143.check_ms = now + kC143ReadBackMs;
    g_c143.check_want = health;
    g_c143.check_pawn_id = object_index(pawn);
    g_c143.check_life = c43_life_text(pawn);
    g_c143.check_drops = 0;
    g_c143.check_rises = 0;
    g_c143.check_lost = 0.0f;
}

void* c143_find_pc(void* world, int32_t pc_id) {
    for (int s = 0; s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        if (pc != nullptr && object_index(pc) == pc_id) {
            return pc;
        }
    }
    return nullptr;
}

// Card 198: no static trace shows whether the bug pawn's spawn path (PossessedBy, ActorFullyLoaded, BeginPlay) sets
// Health after LoadAbilitySystemComponent, which the MI Assign mark guards against; this line reads it back.
void c143_read_back(void* world, ULONGLONG now) {
    if (g_c143.check_pc_id < 0 || now < g_c143.check_ms) {
        return;
    }
    void* const pc = c143_find_pc(world, g_c143.check_pc_id);
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    void* component = nullptr;
    const float health = pawn != nullptr ? c21_health(static_cast<const uint8_t*>(pawn), &component) : -1.0f;
    const bool same_pawn = pawn != nullptr && object_index(pawn) == g_c143.check_pawn_id;
    const char* const life = pawn != nullptr ? c43_life_text(pawn) : "none";
    // b9: a differing Health is a defect only when the game changed nothing since the apply (the apply did not
    // stick); damage, a heal or a life-state change since then is the game's own and named in the row.
    const bool changed = same_pawn && (g_c143.check_drops + g_c143.check_rises > 0 || life != g_c143.check_life);
    log::line("%scard198: host t=%llu Restore read-back %llu ms after the apply: controller %d pawn %s%s Health %.1f "
              "(applied %.1f); since the apply: %u damage (lost %.1f), %u rises, life %s -> %s",
              health == g_c143.check_want || changed ? "" : "WARN ", static_cast<unsigned long long>(now),
              static_cast<unsigned long long>(kC143ReadBackMs), g_c143.check_pc_id,
              pawn != nullptr ? object_name(pawn).text : "none", same_pawn ? "" : " (not the applied pawn)",
              static_cast<double>(health), static_cast<double>(g_c143.check_want), g_c143.check_drops,
              static_cast<double>(g_c143.check_lost), g_c143.check_rises, g_c143.check_life, life);
    g_c143.check_pc_id = -1;
    g_c143.check_pawn_id = -1;
}

void c143_remove_hold(int32_t i) {
    g_c143.hold[i] = g_c143.hold[g_c143.holds - 1];
    --g_c143.holds;
}

} // namespace

// --- Card 440: a rejoining client's weapons and ammo (host) ---
// A rejoin makes a new server copy with the map's starting inventory, and a placed pickup taken before the leave stays
// taken, so the weapons were lost for the map. The host keeps each copy's last weapon list and ammo map (the card 327
// row texts, on each row change). A kept entry whose controller is gone is an orphan; the first card 327 row of a
// controller with no entry adopts the newest orphan as a held Restore with no Health ("-1.0;<weapons>;<ammo>"), which
// c143_host_step applies after the new pawn's AssignStartingInventories. Health and Armour are the new pawn's own.
// Cleared at map arm and at a checkpoint load in place (the Reload and Restore carry that state).
// With two clients gone at once, the first to rejoin gets the newest entry (no player identity is read).
namespace {

constexpr size_t kC445ChargeText = 64;        // card 445: four "charge ready" pairs
constexpr ULONGLONG kC445MaxMs = 40000;       // card 445: an owed charge is dropped then (a Restore hold lasts 30 s)

struct C440Keep {
    int32_t pc_id;
    ULONGLONG kept_ms;
    char weapons[kChanTextMax + 1];
    char ammo[kChanTextMax + 1];
    char charge[kC445ChargeText]; // card 445: the card 327 charge text ("charge ready" per slot)
    float health; // card 448: read at Logout (c448_logout); below 0 = none, the held Restore then sets no Health
    float armour; // card 448: below 0 = not read, the held Restore then carries Health only
};

// Card 445: a kept charge text owed to a rejoined controller's new pawn (c445_step).
struct C445Owed {
    int32_t pc_id;
    ULONGLONG since_ms;
    char charge[kC445ChargeText];
};

// Card 446: the last relayed count of one item definition on a controller's server copy. `owed` from the moment a
// rejoined controller takes the entry until c446_step sets it on the new pawn. The definition is a class object,
// alive for the map; the table is emptied at each map arm and load. `pawn_id` is the copy the count was read on: a
// live controller on another marine (a respawn) no longer has that count, so c446_step drops the entry.
struct C446Item {
    int32_t pc_id;
    int32_t pawn_id;
    void* definition;
    int32_t count;
    bool owed;
    ULONGLONG since_ms;
};
constexpr int32_t kC446Items = kC17Slots * 8;

struct C440State {
    C440Keep keep[kC17Slots];
    int32_t keeps;
    C445Owed owed[kC17Slots];
    int32_t oweds;
    C446Item item[kC446Items];
    int32_t items;
};

C440State g_c440{};

// Puts k in the Restore holds for pc_id. False when a Restore is already held for it (the client's own wins) or the
// holds are full.
bool c440_hold(int32_t pc_id, const C440Keep& k, ULONGLONG now) {
    int32_t i = 0;
    while (i < g_c143.holds && g_c143.hold[i].pc_id != pc_id) {
        ++i;
    }
    if (i < g_c143.holds || g_c143.holds >= kC17Slots) {
        log::line("%scard440: host kept inventory not held for controller %d: %s", i < g_c143.holds ? "" : "WARN ",
                  pc_id, i < g_c143.holds ? "a Restore from the client is held" : "holds full");
        return false;
    }
    C143Hold& h = g_c143.hold[i];
    char head[32] = "-1.0"; // card 448: "<health> <armour>" or "<health>" when the Logout read them (c143_parse)
    if (k.health >= 0.0f && k.armour >= 0.0f) {
        snprintf(head, sizeof(head), "%.1f %.1f", static_cast<double>(k.health), static_cast<double>(k.armour));
    } else if (k.health >= 0.0f) {
        snprintf(head, sizeof(head), "%.1f", static_cast<double>(k.health));
    }
    int n = snprintf(h.text, sizeof(h.text), "%s;%s;%s", head, k.weapons, k.ammo);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(h.text)) {
        log::line("WARN card440: host kept inventory too long (%d chars): ammo left out", n);
        n = snprintf(h.text, sizeof(h.text), "%s;%s;", head, k.weapons);
    }
    if (n < 0 || static_cast<size_t>(n) >= sizeof(h.text)) {
        h.text[0] = '\0';
        return false;
    }
    h.pc_id = pc_id;
    h.recv_ms = now;
    h.state_ms = k.kept_ms; // card 463: script edits after the keep are re-applied over the list
    ++g_c143.holds;
    return true;
}

// Card 445 (run c423b144b: the copy's charge row went "20000 1" to "0 0" across a rejoin). The kept charge goes on
// the new pawn after the held Restore, since the slots' abilities come with the starting inventory.
void c445_owe(int32_t pc_id, const char* charge, ULONGLONG now) {
    if (charge[0] == '\0' || g_c440.oweds >= kC17Slots) {
        return;
    }
    C445Owed& o = g_c440.owed[g_c440.oweds++];
    o.pc_id = pc_id;
    o.since_ms = now;
    snprintf(o.charge, sizeof(o.charge), "%s", charge);
}

// Sets every slot the text carries (a slot of -1 had no ability). False while a carried slot has no live ability.
bool c445_apply(void* pawn, const char* charge) {
    int v[8] = {-1, 0, -1, 0, -1, 0, -1, 0};
    const int got = sscanf_s(charge, "%d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
    for (int32_t i = 0; i < 4 && 2 * i + 1 < got; ++i) {
        if (v[2 * i] >= 0 && c106_set_charge(pawn, i, v[2 * i], v[2 * i + 1] != 0) != nullptr) {
            return false;
        }
    }
    return true;
}

// Host, each step: an owed charge is applied once its controller's Restore hold is gone and its pawn's slots are live.
void c445_step(void* world, ULONGLONG now) {
    for (int32_t i = 0; i < g_c440.oweds && i < kC17Slots; ++i) {
        const C445Owed& o = g_c440.owed[i];
        int32_t h = 0;
        while (h < g_c143.holds && g_c143.hold[h].pc_id != o.pc_id) {
            ++h;
        }
        void* const pc = h == g_c143.holds ? c143_find_pc(world, o.pc_id) : nullptr;
        void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
        const bool done = pawn != nullptr && is_a(pawn, g_player_mi_class()) && c445_apply(pawn, o.charge);
        // The owe may carry a later tick read than this step's now: age 0 then, never an unsigned wrap.
        const ULONGLONG age = o.since_ms > now ? 0 : now - o.since_ms;
        if (!done && age < kC445MaxMs) {
            continue;
        }
        log::line("%scard445: host t=%llu kept tactical charge \"%s\" for controller %d: %s, %llu ms after the rejoin",
                  done ? "" : "WARN ", static_cast<unsigned long long>(now), o.charge, o.pc_id,
                  done ? "set on the new pawn" : "not set (no pawn with live slots)", static_cast<unsigned long long>(age));
        g_c440.owed[i--] = g_c440.owed[--g_c440.oweds];
    }
}

// Card 446: the gone controller's kept item counts become owed to the controller that takes its inventory. Counts
// already noted for the new controller (its starting items) are dropped first.
int32_t c446_adopt(int32_t gone_id, int32_t pc_id, ULONGLONG now) {
    int32_t owed = 0;
    for (int32_t i = 0; i < g_c440.items && i < kC446Items; ++i) {
        if (g_c440.item[i].pc_id == pc_id) {
            g_c440.item[i--] = g_c440.item[--g_c440.items];
        }
    }
    for (int32_t i = 0; i < g_c440.items && i < kC446Items; ++i) {
        C446Item& e = g_c440.item[i];
        if (e.pc_id == gone_id) {
            e.pc_id = pc_id;
            e.owed = true;
            e.since_ms = now;
            ++owed;
        }
    }
    return owed;
}

// Host, each step: an owed item count is set once its controller's Restore hold is gone and its pawn has an item
// inventory. After kC445MaxMs without one the entry is dropped with a WARN.
void c446_step(void* world, ULONGLONG now) {
    for (int32_t i = 0; i < g_c440.items && i < kC446Items; ++i) {
        C446Item& e = g_c440.item[i];
        if (!e.owed) {
            void* const now_pc = c143_find_pc(world, e.pc_id);
            void* const now_pawn = now_pc != nullptr ? read_ptr(now_pc, kControllerPawn) : nullptr;
            if (now_pawn != nullptr && is_a(now_pawn, g_player_mi_class()) && object_index(now_pawn) != e.pawn_id) {
                g_c440.item[i--] = g_c440.item[--g_c440.items];
            }
            continue;
        }
        int32_t h = 0;
        while (h < g_c143.holds && g_c143.hold[h].pc_id != e.pc_id) {
            ++h;
        }
        void* const pc = h == g_c143.holds ? c143_find_pc(world, e.pc_id) : nullptr;
        void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
        void* const inventory = pawn != nullptr && is_a(pawn, g_player_mi_class()) ? c73_item_inventory(pawn) : nullptr;
        if (inventory != nullptr) {
            e.owed = false; // before the set: its relay notes the new count in this entry
            e.pawn_id = object_index(pawn);
            c446_set_count(inventory, e.definition, e.count);
        } else if (now - e.since_ms >= kC445MaxMs) {
            log::line("WARN card446: host t=%llu kept item count %s %d for controller %d not set (no pawn with an "
                      "item inventory)", static_cast<unsigned long long>(now), object_name(e.definition).text,
                      e.count, e.pc_id);
            g_c440.item[i--] = g_c440.item[--g_c440.items];
        }
    }
}

// Card 570, host: "<definition path> <count>" pairs, space separated, for each item definition card 446 noted for pc,
// with the copy's live count (0 included: the new copy then holds none of it either). The Reload text carries it, so
// the grenades survive a checkpoint rejoin (run c570b386a: the load's GrenadeItem frames came while the client had
// no pawn and were dropped at the map change). A name that would be cut is left out.
size_t c570_items_text(const void* pc, const void* pawn, char* out, size_t cap) {
    void* const inventory = c73_item_inventory(pawn);
    size_t used = 0;
    out[0] = '\0';
    const int32_t pc_id = pc != nullptr && inventory != nullptr ? object_index(pc) : -1;
    for (int32_t i = 0; pc_id >= 0 && i < g_c440.items && i < kC446Items; ++i) {
        const C446Item& e = g_c440.item[i];
        if (e.pc_id != pc_id) {
            continue;
        }
        const ObjectName package = object_name(read_ptr(e.definition, kObjectOuter));
        const ObjectName cls = object_name(e.definition);
        if (strlen(package.text) + 1 >= kNameChars || strlen(cls.text) + 1 >= kNameChars) {
            log::line("WARN card570: host item %s of %s left out of the Reload text: name cut", cls.text,
                      object_name(pc).text);
            continue;
        }
        const int w = snprintf(out + used, cap - used, "%s%s.%s %d", used > 0 ? " " : "", package.text, cls.text,
                               c73_count(inventory, e.definition));
        if (w <= 0 || static_cast<size_t>(w) >= cap - used) {
            out[used] = '\0';
            break;
        }
        used += static_cast<size_t>(w);
    }
    return used;
}

// Card 570, host: the Restore's items part set on the new copy (c446_set_count relays each count to the client).
void c570_apply_items(void* pawn, const char* items, ULONGLONG now) {
    void* const inventory = items[0] != '\0' ? c73_item_inventory(pawn) : nullptr;
    if (items[0] != '\0' && inventory == nullptr) {
        log::line("WARN card570: host t=%llu Restore items of %s not set: no item inventory: %s",
                  static_cast<unsigned long long>(now), object_name(pawn).text, items);
        return;
    }
    const char* p = items;
    for (int32_t n = 0; inventory != nullptr && n < kC446Items && *p != '\0'; ++n) {
        char path[2 * kNameChars + 2];
        int32_t want = -1;
        int used = 0;
        if (sscanf_s(p, " %129s %d%n", path, static_cast<unsigned>(sizeof(path)), &want, &used) != 2 || want < 0 ||
            used <= 0) {
            log::line("WARN card570: host t=%llu Restore items of %s: bad pair at \"%s\"",
                      static_cast<unsigned long long>(now), object_name(pawn).text, p);
            return;
        }
        p += used;
        wchar_t wide[sizeof(path)];
        for (size_t i = 0; i < sizeof(path); ++i) {
            wide[i] = static_cast<wchar_t>(path[i]);
        }
        void* const definition = find_class(wide);
        if (definition == nullptr) {
            log::line("WARN card570: host t=%llu Restore item %s of %s: no such class",
                      static_cast<unsigned long long>(now), path, object_name(pawn).text);
            continue;
        }
        c446_set_count(inventory, definition, want);
    }
}

// The newest kept entry whose controller is no longer in world, or -1.
int32_t c440_orphan(void* world) {
    int32_t best = -1;
    for (int32_t i = 0; i < g_c440.keeps; ++i) {
        if (c143_find_pc(world, g_c440.keep[i].pc_id) != nullptr) {
            continue;
        }
        best = best < 0 || g_c440.keep[i].kept_ms > g_c440.keep[best].kept_ms ? i : best;
    }
    return best;
}

} // namespace

// Host, from c73_relay: the count a server copy's item change left. An owed entry keeps its count (the new pawn's
// starting items are not the kept ones).
void c446_note_item(void* pc, const void* pawn, void* definition, int32_t count) {
    if (pc == nullptr || pawn == nullptr || definition == nullptr) {
        return;
    }
    const int32_t pc_id = object_index(pc);
    int32_t i = 0;
    while (i < g_c440.items && (g_c440.item[i].pc_id != pc_id || g_c440.item[i].definition != definition)) {
        ++i;
    }
    if (i >= kC446Items) {
        log::line("WARN card446: host item table full (%d): %s %d of controller %d not kept", kC446Items,
                  object_name(definition).text, count, pc_id);
        return;
    }
    if (i == g_c440.items) {
        g_c440.item[g_c440.items++] = C446Item{pc_id, object_index(pawn), definition, count, false, 0};
    } else if (!g_c440.item[i].owed) {
        g_c440.item[i].count = count;
        g_c440.item[i].pawn_id = object_index(pawn);
    }
}

// Host, from the card 327 row of a server copy when the row changed: the copy's weapon list and ammo map texts.
void c440_keep(void* pc, const char* weapons, const char* ammo, const char* charge) {
    if (weapons[0] == '\0') {
        return; // no list yet
    }
    const int32_t pc_id = object_index(pc);
    const ULONGLONG now = GetTickCount64();
    int32_t i = 0;
    while (i < g_c440.keeps && g_c440.keep[i].pc_id != pc_id) {
        ++i;
    }
    const int32_t orphan = i == g_c440.keeps ? c440_orphan(g_actor_get_world(pc)) : -1;
    if (orphan >= 0) {
        C440Keep& k = g_c440.keep[orphan];
        const bool held = c440_hold(pc_id, k, now);
        log::line("card440: host t=%llu %s (controller %d) takes the kept inventory of the gone controller %d (kept "
                  "%llu ms ago), %s: weapons %.300s; ammo %.300s", static_cast<unsigned long long>(now),
                  object_name(pc).text, pc_id, k.pc_id, static_cast<unsigned long long>(now - k.kept_ms),
                  held ? "held as a Restore" : "not held", k.weapons, k.ammo);
        if (held) {
            c445_owe(pc_id, k.charge, now); // card 445
        }
        const int32_t owed = c446_adopt(k.pc_id, pc_id, now);
        log::line("card446: host t=%llu controller %d takes %d kept item count(s) of the gone controller %d",
                  static_cast<unsigned long long>(now), pc_id, owed, k.pc_id);
        k.pc_id = pc_id; // the starting inventory of this row is not kept over it; the next row change is
        k.kept_ms = now;
        return;
    }
    if (i >= kC17Slots) {
        return; // every entry belongs to a live controller
    }
    C440Keep& k = g_c440.keep[i];
    k.pc_id = pc_id;
    k.kept_ms = now;
    snprintf(k.weapons, sizeof(k.weapons), "%s", weapons);
    snprintf(k.ammo, sizeof(k.ammo), "%s", ammo);
    snprintf(k.charge, sizeof(k.charge), "%s", charge);
    k.health = -1.0f; // card 448: a live controller's entry carries none; its Logout reads them
    k.armour = -1.0f;
    g_c440.keeps = i == g_c440.keeps ? g_c440.keeps + 1 : g_c440.keeps;
    g_c143.armed = true; // a rejoined copy's AssignStartingInventories marks its pawn for the held Restore
}

// Card 448, host, at Logout before card 043 drops the pawn's life row: the leaving marine's Health and Armour go into
// its kept entry, so the held Restore of the controller that takes the entry sets them (a rejoin was a full heal).
// Only an Up marine on foot: a downed or dead one, or a seat the leave did not free, rejoins with the game's values.
void c448_logout(const void* pc, const void* pawn) {
    const int32_t pc_id = object_index(pc);
    int32_t i = 0;
    while (i < g_c440.keeps && g_c440.keep[i].pc_id != pc_id) {
        ++i;
    }
    if (pc == nullptr || i == g_c440.keeps) {
        return; // no card 327 row yet: nothing is kept for this controller
    }
    C440Keep& k = g_c440.keep[i];
    const int32_t e = pawn != nullptr ? c43_find(object_index(pawn)) : -1;
    const bool up = pawn != nullptr && is_a(pawn, g_player_mi_class()) && !is_a(pawn, g_mech_class()) &&
                    !is_a(pawn, g_turret_class()) && (e < 0 || g_c43.e[e].state == C43Life::Up);
    void* component = nullptr;
    const float health = up ? c21_health(static_cast<const uint8_t*>(pawn), &component) : -1.0f;
    k.health = health > kC43HeldHealth ? health : -1.0f;
    k.armour = k.health >= 0.0f ? c48_armour(component).armour : -1.0f;
    log::line("card448: host t=%llu controller %d leaves on %s (%s): kept Health %.1f, Armour %.1f%s",
              static_cast<unsigned long long>(GetTickCount64()), pc_id, object_name(pawn).text,
              e >= 0 ? kC43LifeText[static_cast<size_t>(g_c43.e[e].state)] : "Up", static_cast<double>(k.health),
              static_cast<double>(k.armour), k.health >= 0.0f ? "" : " (none kept: the rejoin starts on the game's)");
}

void c440_arm_map() {
    g_c440.keeps = 0;
    g_c440.oweds = 0;
    g_c440.items = 0; // card 446
}

// Host, after the game's HealthAttributeChanged on a player pawn (card 198 read-back): counts the change when it is
// the pawn the last Restore apply set and its read-back is still pending.
void c198_health_change(const void* pawn, float old_health, float new_health) {
    if (g_client_session || g_c143.check_pc_id < 0 || object_index(pawn) != g_c143.check_pawn_id) {
        return;
    }
    // The apply's own write, if the game broadcasts it after the apply returns, lands on the applied value first:
    // it is not a change since the apply.
    if (g_c143.check_drops + g_c143.check_rises == 0 && new_health == g_c143.check_want) {
        return;
    }
    if (new_health < old_health) {
        ++g_c143.check_drops;
        g_c143.check_lost += old_health - new_health;
    } else if (new_health > old_health) {
        ++g_c143.check_rises;
    }
}

// Host, at the end of detour_c137_load_slot (both load routes). Card 375: a copy that has not ponged on this map yet
// (the deferred load right after a seamless travel) gets no Reload; it takes the loaded state from the join replay at
// its first Pong, and a Reload there would drop it during the press-to-start hold (card 355: host to main menu).
void c143_after_load(void* world) {
    const ULONGLONG now = GetTickCount64();
    g_c143.marks = 0; // only an Assign after this Reload marks a rejoined copy ready
    g_c440.keeps = 0; // card 440: the kept inventories are from before the load
    g_c440.oweds = 0;
    g_c143.armed = true;
    g_c143.reload_ms = now; // card 463
    for (int s = 0; s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        if (pc == nullptr || sprint_side(pc) != kSideServerCopy) {
            continue;
        }
        const C50Copy* const c = c50_copy(object_index(pc), false);
        if (c != nullptr && c->ponged) {
            c143_send_reload(pc, now);
        } else {
            log::line("card375: host t=%llu no Reload to %s: no Pong on this map yet, the join replay carries the load",
                      static_cast<unsigned long long>(now), object_name(pc).text);
        }
    }
    g_c440.items = 0; // card 446; card 570: after the Reload texts, which list the noted definitions
}

// Host, after LoadAbilitySystemComponent: a player row saved while card 043 held it downed comes back at the hold's
// Health; the pawn is respawned Up by the load, so it gets a revive's Health instead (the host's own pawn too).
void c143_after_load_asc(void* pawn) {
    if (g_client_session || pawn == nullptr || !is_a(pawn, g_player_character_class())) {
        return;
    }
    void* component = nullptr;
    const float health = c21_health(static_cast<const uint8_t*>(pawn), &component);
    if (!(health > 0.0f && health <= kC43HeldHealth)) {
        return;
    }
    const float max_health = c43_max_health(component);
    const float revive = max_health > 0.0f ? max_health * kC43ReviveFraction : kC43HeldHealth;
    c43_set_health(pawn, revive);
    void* after_component = nullptr;
    log::line("card143: host t=%llu %s (%s) loaded at the downed Health %.1f: set to %.1f (%.1f, revive fraction of "
              "MaxHealth %.1f)",
              static_cast<unsigned long long>(GetTickCount64()), object_name(pawn).text,
              kSprintSideText[sprint_side(pawn)], static_cast<double>(health), static_cast<double>(revive),
              static_cast<double>(c21_health(static_cast<const uint8_t*>(pawn), &after_component)),
              static_cast<double>(max_health));
}

// Card 198: both sides. ASTPlayerCharacterBug::LoadAbilitySystemComponent 0x051CDB60 overrides the MI one card 048
// hooks and calls ASTCharacter's directly, so the bug pawn never reaches detour_load_asc. Its card 143 step runs here.
PawnVoidFn g_orig_load_asc_bug = nullptr;

void detour_load_asc_bug(void* pawn) {
    static uint32_t lines = 0;
    g_orig_load_asc_bug(pawn);
    if (lines < 8) {
        ++lines;
        log::line("card198: %s LoadAbilitySystemComponent (bug entry) #%u on %s (%s)", side(), lines,
                  object_name(pawn).text, kSprintSideText[sprint_side(pawn)]);
    }
    c143_after_load_asc(pawn); // card 143: a row saved while downed comes back at the revive Health
}

// Host, after AssignStartingInventories: a player pawn's starting inventory is in; a held Restore can go on it.
// No side gate: a rejoining client's new copy reads host-own here, before its possession (card 143 run 2). A mark on
// the host's own pawn is harmless, since c143_host_step looks up only the held controller's current pawn.
void c143_host_assign(const void* pawn) {
    if (!g_c143.armed || pawn == nullptr || !is_a(pawn, g_player_character_class())) {
        return;
    }
    if (g_c143.marks < kC143Marks) {
        g_c143.mark[g_c143.marks++] = pawn;
    } else {
        memmove(&g_c143.mark[0], &g_c143.mark[1], sizeof(g_c143.mark[0]) * (kC143Marks - 1));
        g_c143.mark[kC143Marks - 1] = pawn;
    }
    log::line("card143: host t=%llu AssignStartingInventories on %s (%s at the call): marked (%d held Restore)",
              static_cast<unsigned long long>(GetTickCount64()), object_name(pawn).text,
              kSprintSideText[sprint_side(pawn)], g_c143.holds);
}

void c143_host_step(void* world, ULONGLONG now) {
    c143_read_back(world, now);
    c445_step(world, now); // card 445
    c446_step(world, now); // card 446
    for (int32_t i = 0; i < g_c143.holds && i < kC17Slots; ++i) {
        const C143Hold& h = g_c143.hold[i];
        void* const pc = c143_find_pc(world, h.pc_id);
        void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
        const bool ready = pawn != nullptr && is_a(pawn, g_player_character_class());
        if (!ready) {
            if (now - h.recv_ms >= kC143HoldMaxMs) {
                log::line("WARN card143: host t=%llu Restore for controller %d dropped: no player pawn in %llu ms",
                          static_cast<unsigned long long>(now), h.pc_id, static_cast<unsigned long long>(kC143HoldMaxMs));
                c143_remove_hold(i--);
            }
            continue;
        }
        const bool marked = c143_marked(pawn);
        // Card 198: only the MI pawn gets an AssignStartingInventories mark; any other class applies on the first ready step.
        if (!marked && is_a(pawn, g_player_mi_class()) && now - h.recv_ms < kC143ApplyLateMs) {
            continue;
        }
        c143_apply(pc, pawn, h, marked, now);
        c143_remove_hold(i--);
    }
}

void c143_receive_restore(void* pc, const ue::chan::WireString* s) {
    const int32_t pc_id = object_index(pc);
    int32_t i = 0;
    while (i < g_c143.holds && g_c143.hold[i].pc_id != pc_id) {
        ++i;
    }
    if (i == g_c143.holds && g_c143.holds >= kC17Slots) {
        log::line("WARN card143: host Restore from %s dropped: %d already held", object_name(pc).text, g_c143.holds);
        return;
    }
    C143Hold& h = g_c143.hold[i];
    const int32_t len = chan_text(s, h.text, sizeof(h.text));
    if (len <= 0) {
        log::line("WARN card143: host Restore from %s dropped: empty or bad text", object_name(pc).text);
        return;
    }
    h.pc_id = pc_id;
    h.recv_ms = GetTickCount64();
    h.state_ms = g_c143.reload_ms; // card 463: the Reload's state; script edits after the load are re-applied
    g_c143.holds = i == g_c143.holds ? g_c143.holds + 1 : g_c143.holds;
    log::line("card143: host t=%llu Restore received from %s (%d chars): %s", static_cast<unsigned long long>(h.recv_ms),
              object_name(pc).text, len, h.text);
}

// Client: the host loaded in place. Hold the copy's state and rejoin through the card 045 follow, at once.
void c143_receive_reload(void* pc, const ue::chan::WireString* s) {
    const ULONGLONG now = GetTickCount64();
    const int32_t len = chan_text(s, g_c143.text, sizeof(g_c143.text));
    if (len <= 0) {
        log::line("WARN card143: client t=%llu Reload dropped: empty or bad text", static_cast<unsigned long long>(now));
        return;
    }
    if (!g_travel.enabled || g_travel.url[0] == L'\0') {
        log::line("WARN card143: client t=%llu Reload received but no UBWCOOP_TRAVEL address to rejoin",
                  static_cast<unsigned long long>(now));
        return;
    }
    if (g_c45.follow != C45Follow::Off) {
        log::line("WARN card143: client t=%llu Reload received while the card 045 follow runs: not started again",
                  static_cast<unsigned long long>(now));
        return;
    }
    g_c143.held = strcmp(g_c143.text, kC143NoRow) != 0; // card 371: no saved state, no Restore after the rejoin
    c570_magazine_keep(read_ptr(pc, kControllerPawn), now); // card 570: the Reload text has no magazine
    g_c143.reconnects_at = g_c45.reconnects;
    g_c45.follow = C45Follow::Retrying;
    g_c45.tries = 0;
    g_c45.drop_ms = now;
    g_c45.landed_ms = now;
    g_c45.next_ms = now;
    g_c45.landing[0] = L'\0';
    log::line("card143: client t=%llu Reload received (%d chars): %s; rejoining \"%ls\"",
              static_cast<unsigned long long>(now), len, g_c143.text, g_travel.url);
}

// Client, at a host ping that passed the Pong gate: after the rejoin's reconnect, the held state goes back once.
void c143_client_ping(void* pc) {
    if (!g_c143.held || g_c45.reconnects <= g_c143.reconnects_at) {
        return;
    }
    g_c143.held = false;
    static wchar_t wire[kChanTextFrame];
    const ue::chan::WireString w = chan_text_frame(ChanOp::Restore, g_c143.text, wire);
    const bool sent = w.num > 0 && chan_send_host(pc, w);
    log::line("%scard143: client t=%llu Restore %s: %s", sent ? "" : "WARN ",
              static_cast<unsigned long long>(GetTickCount64()), sent ? "sent" : "not sent", g_c143.text);
}

} // namespace ubw::hooks
