// Weapons: how weapons reach a player and the UBWCOOP_GRANT_WEAPON test loadout (card 021), weapon sync
// and the client applying the host's list (card 050), the card 050 audit detours, the weapon switch
// audit on each pawn (card 057), why the host's own switch is refused (card 062) and the card 050 test
// driver.

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

void c50_after_assign(const void* pawn);
void c50_note_give(const void* inventory, const void* cls, bool ok);

// --- Card 267: a level script's weapon change on any player pawn reaches every other player pawn ---
// The FED_C1M1 script (GiveLaser, GiveMorita) drops and adds weapons on one player only, after a single-use pickup.
// The exec thunks mark which Blueprint call is running when the frame object is a level script actor; the
// native detours (AddWeapon, GiveWeaponByClass, RemoveWeaponFromOwnerInventory) apply the same change to every
// other player pawn (the host's own and each server copy; card 310 fix item 1: the source may be a copy), and the
// card 050 Weapons list carries it to each client. The mirror calls the trampolines with the call kind cleared, so
// a mirrored call never mirrors again; AddWeapon's own inner remove and give carry kind Add, which only AddWeapon
// mirrors.
C267Call g_c267_call = C267Call::None;
void c267_mirror_add(void* source, void* definition, bool equip);
void c267_mirror_give(const void* source, void* const* definition_class, int32_t slot, bool auto_equip);

// --- Card 021: how weapons reach a player (log-only) ---
// Native grants: GiveWeaponByClass takes its TSubclassOf by hidden pointer (rdx; +0x12 loads [rdx]).
// Blueprint thunks: FFrame::Node is the calling BP function, read before the thunk steps the frame.
constexpr uint32_t kC21GrantLines = 40; // per map, all grant hooks together

GiveByClassFn g_orig_give_by_class = nullptr;
GiveByInstanceFn g_orig_give_by_instance = nullptr;
GiveByInstanceFn g_give_by_instance = nullptr;
AddWeaponFn g_orig_add_weapon = nullptr;
LoadInventoryFn g_orig_load_inventory = nullptr;
ExecFn g_orig_exec_add_weapon = nullptr;
ExecFn g_orig_exec_add_weapon_by_tag = nullptr;
ExecFn g_orig_exec_give_by_class = nullptr;
ExecFn g_orig_exec_give_by_instance = nullptr;
ExecFn g_orig_exec_drop_current_weapon = nullptr;
ExecFn g_orig_exec_drop_weapon = nullptr;
ExecFn g_orig_exec_drop_weapon_by_tag = nullptr;

bool c21_grant_line() {
    return note_event_cap(g_c21_grant_lines, kC21GrantLines, "card021 weapon grant");
}

// "<pawn> (<side>), weapons <n>" for an inventory component.
void c21_inventory_text(const void* inventory, char* out, size_t cap) {
    const void* const owner = inventory != nullptr ? read_ptr(inventory, kComponentOwner) : nullptr;
    snprintf(out, cap, "%s (%s), weapons %d", object_name(owner).text, kSprintSideText[sprint_side(owner)],
             inventory != nullptr ? g_get_weapon_count(inventory) : -1);
}

bool detour_give_by_class(void* inventory, void* const* definition_class, int32_t slot, bool auto_equip) {
    const void* const cls = definition_class != nullptr ? *definition_class : nullptr;
    if (c463_client_skip("GiveWeaponByClass", cls)) {
        return false; // card 463
    }
    const bool ok = g_orig_give_by_class(inventory, definition_class, slot, auto_equip);
    c50_note_give(inventory, cls, ok); // card 050
    c50_host_nudge();                  // card 495
    if (g_c267_call == C267Call::Give && inventory != nullptr && c267_source(read_ptr(inventory, kComponentOwner))) {
        c267_mirror_give(read_ptr(inventory, kComponentOwner), definition_class, slot, auto_equip); // card 267
    }
    if (c21_grant_line()) {
        char inv[160];
        char chain[96];
        c21_inventory_text(inventory, inv, sizeof(inv));
        caller_chain(chain, sizeof(chain));
        log::line("card021: %s GiveWeaponByClass(%s, slot %d, equip %d) = %d on %s; callers %s", side(),
                  object_name(cls).text, slot, auto_equip ? 1 : 0, ok ? 1 : 0, inv, chain);
    }
    return ok;
}

bool detour_give_by_instance(void* inventory, void* weapon, int32_t slot, bool auto_equip) {
    const bool ok = g_orig_give_by_instance(inventory, weapon, slot, auto_equip);
    if (c21_grant_line()) {
        char inv[160];
        char chain[96];
        c21_inventory_text(inventory, inv, sizeof(inv));
        caller_chain(chain, sizeof(chain));
        log::line("card021: %s GiveWeaponByInstance(%s %s, slot %d, equip %d) = %d on %s; callers %s", side(),
                  object_name(weapon).text, class_name(weapon).text, slot, auto_equip ? 1 : 0, ok ? 1 : 0, inv,
                  chain);
    }
    return ok;
}

bool detour_add_weapon(void* actor, void* definition, bool equip) {
    if (c463_client_skip("AddWeapon", definition)) {
        return false; // card 463
    }
    const bool ok = g_orig_add_weapon(actor, definition, equip);
    c50_host_nudge(); // card 495
    if (g_c267_call == C267Call::Add && c267_source(actor)) {
        c267_mirror_add(actor, definition, equip); // card 267
    }
    if (g_gate_diag && c21_grant_line()) {
        char chain[96];
        caller_chain(chain, sizeof(chain));
        log::line("card021: %s AddWeapon(%s (%s), %s, equip %d) = %d; callers %s", side(), object_name(actor).text,
                  kSprintSideText[sprint_side(actor)], object_name(definition).text, equip ? 1 : 0, ok ? 1 : 0,
                  chain);
    }
    return ok;
}

void* detour_load_inventory(void* inventory, void* out_array) {
    void* const ret = g_orig_load_inventory(inventory, out_array);
    if (c21_grant_line()) {
        char inv[160];
        c21_inventory_text(inventory, inv, sizeof(inv));
        log::line("card021: %s USTWeaponInventory::LoadInventory on %s after, results %d, caller exe+0x%llX", side(),
                  inv, array_num(static_cast<const uint8_t*>(out_array), 0), exe_rva(_ReturnAddress()));
    }
    return ret;
}

void c21_exec(ExecFn original, const char* what, void* context, void* frame, void* result) {
    const void* const node = read_ptr(frame, kFrameNode);
    const void* const object = read_ptr(frame, kFrameObject);
    original(context, frame, result);
    if (c21_grant_line()) {
        log::line("card021: %s %s from BP function %s (in %s %s), context %s (%s)", side(), what,
                  object_name(node).text, object_name(object).text, kSprintSideText[sprint_side(object)],
                  object_name(context).text, class_name(context).text);
    }
}

// Card 267/310: true when the Blueprint frame's object is a level script actor on the host.
bool c267_level_script_frame(const void* frame) {
    static void* level_script_class = nullptr;
    if (level_script_class == nullptr) {
        level_script_class = find_class(L"/Script/Engine.LevelScriptActor");
    }
    const void* const object = read_ptr(frame, kFrameObject);
    return level_script_class != nullptr && object != nullptr && is_a(object, level_script_class) &&
           sprint_side(object) == kSideHostOwn;
}

// Card 267: kind of the running Blueprint weapon call when a level script made it on the host, else None.
C267Call c267_frame_kind(const void* frame, C267Call kind) {
    return c267_level_script_frame(frame) ? kind : C267Call::None;
}

// Card 267/310: true when pawn is a player character the host's level script can target: the host's own pawn or a
// client's server copy (card 310 fix item 1: any player, not only the host's).
bool c267_source(const void* pawn) {
    const uint32_t s = pawn != nullptr ? sprint_side(pawn) : kSideOther;
    return (s == kSideHostOwn || s == kSideServerCopy) && is_a(pawn, g_player_mi_class());
}

// Card 463: the FED_C1M1 level script functions whose weapon edits the client skips (the host mirrors them).
constexpr const char* kC463ClientSkip[] = {"RemovePlayersWeapons", "GiveUnarmedWeapon", "GiveMorita", "GiveLaser"};
constexpr uint32_t kC463SkipRows = 24; // client skip rows per session
const void* g_c463_skip_node = nullptr;
uint32_t g_c463_skip_rows = 0;
uint32_t g_c463_skips = 0;

// Card 463: client. The frame's BP function when the client's own FED_C1M1_Scripting_C runs one of kC463ClientSkip.
const void* c463_skip_frame(const void* frame) {
    const void* const object = read_ptr(frame, kFrameObject);
    if (!g_client_session || object == nullptr || strcmp(class_name(object).text, "FED_C1M1_Scripting_C") != 0) {
        return nullptr;
    }
    const void* const node = read_ptr(frame, kFrameNode);
    const ObjectName fn = object_name(node);
    for (const char* const name : kC463ClientSkip) {
        if (strcmp(fn.text, name) == 0) {
            return node;
        }
    }
    return nullptr;
}

// Card 463: client, from a native weapon edit detour. True (logged, capped) when the edit is skipped.
bool c463_client_skip(const char* native, const void* what) {
    if (g_c463_skip_node == nullptr) {
        return false;
    }
    ++g_c463_skips;
    if (note_event_cap(g_c463_skip_rows, kC463SkipRows, "card463 client skip")) {
        log::line("card463: client t=%llu skipped own level script %s: %s(%s) (only the host edits weapons; %u skips "
                  "so far)", static_cast<unsigned long long>(GetTickCount64()), object_name(g_c463_skip_node).text,
                  native, object_name(what).text, g_c463_skips);
    }
    return true;
}

// Card 267 call kind of a host level script frame; a drop inside RemovePlayersWeapons is Clear (card 463).
C267Call c267_call_kind(const void* frame, C267Call kind) {
    const C267Call k = c267_frame_kind(frame, kind);
    return k == C267Call::Drop && strcmp(object_name(read_ptr(frame, kFrameNode)).text, "RemovePlayersWeapons") == 0
               ? C267Call::Clear
               : k;
}

// card021 exec log plus the card 267 call kind and the card 463 client skip around the thunk (saved and restored for
// nested calls).
void c267_exec(ExecFn original, const char* what, C267Call kind, void* context, void* frame, void* result) {
    const C267Call outer = g_c267_call;
    const void* const outer_skip = g_c463_skip_node;
    g_c267_call = c267_call_kind(frame, kind);
    g_c463_skip_node = c463_skip_frame(frame);
    if (g_gate_diag) {
        c21_exec(original, what, context, frame, result);
    } else {
        original(context, frame, result);
    }
    g_c267_call = outer;
    g_c463_skip_node = outer_skip;
}

void detour_exec_add_weapon(void* context, void* frame, void* result) {
    c267_exec(g_orig_exec_add_weapon, "execAddWeapon", C267Call::Add, context, frame, result);
}

void detour_exec_add_weapon_by_tag(void* context, void* frame, void* result) {
    c267_exec(g_orig_exec_add_weapon_by_tag, "execAddWeaponByTag", C267Call::Add, context, frame, result);
}

void detour_exec_give_by_class(void* context, void* frame, void* result) {
    c267_exec(g_orig_exec_give_by_class, "execGiveWeaponByClass", C267Call::Give, context, frame, result);
}

void detour_exec_drop_current_weapon(void* context, void* frame, void* result) {
    c267_exec(g_orig_exec_drop_current_weapon, "execDropCurrentWeapon", C267Call::Drop, context, frame, result);
}

void detour_exec_drop_weapon(void* context, void* frame, void* result) {
    c267_exec(g_orig_exec_drop_weapon, "execDropWeapon", C267Call::Drop, context, frame, result);
}

void detour_exec_drop_weapon_by_tag(void* context, void* frame, void* result) {
    c267_exec(g_orig_exec_drop_weapon_by_tag, "execDropWeaponByTag", C267Call::Drop, context, frame, result);
}

// --- Card 278: the map's script gives and drops replayed on every new player character of a server copy ---
// A respawned copy gets the level's starting inventory (AssignStartingInventories), and a copy with no live pawn at
// the script frame gets nothing, so the host keeps the map's card 267 rows (in order) and replays them on each new MI
// pawn of a copy. Trigger: AssignStartingInventories on an MI pawn (once per new character object) puts the pawn in a
// pending ring; the step replays when a server copy PC's Pawn (+kControllerPawn) is a pending pawn, so the starting
// inventory is in first. A possess change (mech, turret) makes no new character and no AssignStartingInventories.
// A pawn given a card 143 Restore leaves the ring unreplayed: the Restore's exact list wins.
// Card 463: Clear is RemovePlayersWeapons (every weapon removed); rows carry their host time, so a card 143 Restore
// re-applies the rows newer than its state.
enum class C278Kind : uint8_t { Add, Give, Drop, Clear };
constexpr const char* kC278KindText[] = {"AddWeapon", "GiveWeaponByClass", "drop", "RemovePlayersWeapons"};
struct C278Row {
    void* object; // Add: the weapon definition; Give: the definition UClass*; Drop: the weapon UClass*; Clear: none
    int32_t slot; // Give only
    C278Kind kind;
    bool flag; // Add: equip; Give: auto_equip; Drop and Clear: RemoveWeaponFromOwnerInventory arg2
    ULONGLONG ms; // card 463: host time of the script edit
};
constexpr int32_t kC278Rows = 16;
constexpr int32_t kC278Pending = 2 * kC17Slots; // MI pawns given a starting inventory, not yet checked
struct C278State {
    C278Row rows[kC278Rows];
    const void* pending[kC278Pending];
    const void* rows_world; // the map the rows belong to (cleared on a world pointer change)
    int32_t count;
    int32_t pending_next;
    bool full_warned; // the full-table WARN, once per map
};
C278State g_c278 = {};

// Card 278: removes pawn from the pending ring. True when it was there.
bool c278_take_pending(const void* pawn) {
    bool found = false;
    for (int32_t i = 0; i < kC278Pending; ++i) {
        found = found || g_c278.pending[i] == pawn;
        g_c278.pending[i] = g_c278.pending[i] == pawn ? nullptr : g_c278.pending[i];
    }
    return found;
}

// Card 278: host, from AssignStartingInventories: a new MI character's starting inventory is in.
void c278_note_assign(const void* pawn) {
    if (pawn == nullptr || !is_a(pawn, g_player_mi_class())) {
        return;
    }
    c278_take_pending(pawn);
    g_c278.pending[g_c278.pending_next] = pawn;
    g_c278.pending_next = (g_c278.pending_next + 1) % kC278Pending;
}

// Card 278: keep one script loadout row for the map of host_pawn. A full table drops the row (WARN once per map).
void c278_record(const void* host_pawn, C278Kind kind, void* object, int32_t slot, bool flag) {
    const void* const world = g_actor_get_world(host_pawn);
    if (world != g_c278.rows_world) {
        g_c278.rows_world = world;
        g_c278.count = 0;
        g_c278.full_warned = false;
    }
    if (kind == C278Kind::Clear && g_c278.count > 0 && g_c278.rows[g_c278.count - 1].kind == C278Kind::Clear) {
        g_c278.rows[g_c278.count - 1].ms = GetTickCount64(); // card 463: one row per RemovePlayersWeapons
        return;
    }
    if (g_c278.count >= kC278Rows) {
        if (!g_c278.full_warned) {
            log::line("WARN card278: host script loadout table full (%d rows): row %s(%s) dropped", kC278Rows,
                      kC278KindText[static_cast<int>(kind)], object_name(object).text);
        }
        g_c278.full_warned = true;
        return;
    }
    g_c278.rows[g_c278.count++] = C278Row{object, slot, kind, flag, GetTickCount64()};
}

// Card 278: a card 143 Restore was applied to the pawn owning inventory; it is not replayed.
void c278_note_restored(const void* inventory) {
    const void* const pawn = inventory != nullptr ? read_ptr(inventory, kComponentOwner) : nullptr;
    if (pawn != nullptr && c278_take_pending(pawn) && g_c278.count > 0) {
        log::line("card278: host replay skipped for %s: card 143 restore",
                  object_name(read_ptr(pawn, kPawnController)).text);
    }
}

// Card 267/310: the player character of every host player controller (the host's own and each client's server
// copy, up to kC17Slots) except source, source as world context. A controller with no live MI pawn is logged.
int32_t c267_other_players(const void* source, void** out, int32_t cap) {
    int32_t n = 0;
    for (int s = 0; s < kC17Slots && n < cap; ++s) {
        const void* const pc = g_get_player_controller(source, s);
        const uint32_t side = pc != nullptr ? sprint_side(pc) : kSideOther;
        if (side != kSideHostOwn && side != kSideServerCopy) {
            continue;
        }
        void* const pawn = read_ptr(pc, kControllerPawn);
        if (pawn == source) {
            continue;
        }
        if (pawn != nullptr && is_a(pawn, g_player_mi_class())) {
            out[n++] = pawn;
        } else {
            log::line("card278: host script loadout change skipped for %s: no live pawn", object_name(pc).text);
        }
    }
    return n;
}

// Card 267: the weapon inventory of each other player of source (see c267_other_players).
int32_t c267_other_inventories(const void* source, void** out, int32_t cap) {
    void* pawns[kC17Slots] = {};
    const int32_t m = c267_other_players(source, pawns, kC17Slots);
    int32_t n = 0;
    for (int32_t i = 0; i < m && n < cap; ++i) {
        void* const inventory = read_ptr(pawns[i], kMiWeaponInventory);
        if (inventory != nullptr) {
            out[n++] = inventory;
        }
    }
    return n;
}

// Card 267: the weapon of class weapon_class in inventory's first 8 slots, or null.
void* c267_held_weapon(const void* inventory, const void* weapon_class) {
    constexpr int kSlots = 8; // inventory slots walked, as card 132
    void* match = nullptr;
    for (int slot = 0; slot < kSlots && match == nullptr; ++slot) {
        void* const w = g_get_weapon_by_slot(inventory, static_cast<uint8_t>(slot));
        match = w != nullptr && read_ptr(w, kObjectClass) == weapon_class ? w : nullptr;
    }
    return match;
}

// Card 267: the call kind is None while a mirror runs, so a nested native on another pawn does not mirror again.
void c267_mirror_add(void* source, void* definition, bool equip) {
    c278_record(source, C278Kind::Add, definition, -1, equip); // card 278
    const C267Call outer = g_c267_call;
    g_c267_call = C267Call::None;
    void* inventories[kC17Slots] = {};
    const int32_t n = c267_other_inventories(source, inventories, kC17Slots);
    for (int32_t i = 0; i < n; ++i) {
        void* const other = read_ptr(inventories[i], kComponentOwner);
        const bool ok = g_orig_add_weapon(other, definition, equip);
        log::line("card267: host level script AddWeapon(%s, equip %d) on %s (%s) mirrored to %s (%s): %d",
                  object_name(definition).text, equip ? 1 : 0, object_name(source).text,
                  kSprintSideText[sprint_side(source)], object_name(other).text, kSprintSideText[sprint_side(other)],
                  ok ? 1 : 0);
    }
    g_c267_call = outer;
}

void c267_mirror_give(const void* source, void* const* definition_class, int32_t slot, bool auto_equip) {
    if (definition_class != nullptr && *definition_class != nullptr) {
        c278_record(source, C278Kind::Give, *definition_class, slot, auto_equip); // card 278
    }
    const C267Call outer = g_c267_call;
    g_c267_call = C267Call::None;
    void* inventories[kC17Slots] = {};
    const int32_t n = c267_other_inventories(source, inventories, kC17Slots);
    for (int32_t i = 0; i < n; ++i) {
        const void* const other = read_ptr(inventories[i], kComponentOwner);
        const bool ok = g_orig_give_by_class(inventories[i], definition_class, slot, auto_equip);
        log::line("card267: host level script GiveWeaponByClass(%s, slot %d) on %s (%s) mirrored to %s (%s): %d",
                  object_name(definition_class != nullptr ? *definition_class : nullptr).text, slot,
                  object_name(source).text, kSprintSideText[sprint_side(source)], object_name(other).text,
                  kSprintSideText[sprint_side(other)], ok ? 1 : 0);
    }
    g_c267_call = outer;
}

// Card 463: removes every weapon of inventory (no pickup spawned). The number removed.
int32_t c463_clear_weapons(void* inventory, bool arg2) {
    const C50List l = c50_list(inventory);
    int32_t removed = 0;
    for (int32_t i = 0; i < l.count; ++i) {
        removed += g_orig_c132_remove_weapon(l.weapon[i], arg2, false) ? 1 : 0;
    }
    return removed;
}

// Card 267: the host's level script removed a weapon of class weapon_class from a player pawn (source); remove the
// weapon of the same class from every other player pawn (no pickup spawned). Called after the source's removal.
// Card 463: inside RemovePlayersWeapons (kind Clear) every weapon of each other pawn is removed: the other pawn can
// hold weapons the source never had (a card 278 replay, a card 143 Restore).
void c267_mirror_drop(const void* source, const void* weapon_class, bool arg2) {
    const bool clear = g_c267_call == C267Call::Clear;
    if (clear) {
        c278_record(source, C278Kind::Clear, nullptr, -1, arg2); // card 278/463
    } else {
        c278_record(source, C278Kind::Drop, const_cast<void*>(weapon_class), -1, arg2); // card 278
    }
    const C267Call outer = g_c267_call;
    g_c267_call = C267Call::None;
    void* inventories[kC17Slots] = {};
    const int32_t n = c267_other_inventories(source, inventories, kC17Slots);
    for (int32_t i = 0; i < n; ++i) {
        const void* const other = read_ptr(inventories[i], kComponentOwner);
        if (clear) {
            const int32_t removed = c463_clear_weapons(inventories[i], arg2);
            log::line("card463: host level script RemovePlayersWeapons (drop of %s) on %s (%s) mirrored to %s (%s): %d "
                      "weapons removed, list empty", object_name(weapon_class).text, object_name(source).text,
                      kSprintSideText[sprint_side(source)], object_name(other).text,
                      kSprintSideText[sprint_side(other)], removed);
            continue;
        }
        void* const match = c267_held_weapon(inventories[i], weapon_class);
        const bool ok = match != nullptr && g_orig_c132_remove_weapon(match, arg2, false);
        log::line("card267: host level script drop of %s on %s (%s) mirrored to %s (%s): %s",
                  object_name(weapon_class).text, object_name(source).text, kSprintSideText[sprint_side(source)],
                  object_name(other).text, kSprintSideText[sprint_side(other)],
                  match == nullptr ? "does not hold it" : ok ? "removed" : "WARN removal failed");
    }
    g_c267_call = outer;
}

// Card 278: AddWeapon (0x522AA30) asks HasSlotFree (USTWeaponInventory vtable +0x560) for the definition's slot tag
// (FGameplayTag, 8 bytes, at definition +0x128); when it is false, it removes the occupant with a pickup spawned
// (RemoveWeaponFromOwnerInventory(occupant, false, true)). The replay removes that occupant first, with no pickup.
using C278HasSlotFreeFn = bool (*)(void* inventory, const void* tag);
constexpr size_t kC278HasSlotFree = 0x560 / sizeof(void*);
constexpr size_t kC278DefinitionTag = 0x128;
constexpr size_t kC278TagBytes = 8;

// Card 278: the first weapon in inventory's 8 slots whose definition CDO carries tag, or null.
void* c278_tag_occupant(const void* inventory, const void* tag) {
    constexpr int kSlots = 8; // as c267_held_weapon
    for (int slot = 0; slot < kSlots; ++slot) {
        void* const w = g_get_weapon_by_slot(inventory, static_cast<uint8_t>(slot));
        const void* const def_class = w != nullptr ? read_ptr(w, kWeaponDefinitionClass) : nullptr;
        const char* const cdo =
            def_class != nullptr ? static_cast<const char*>(read_ptr(def_class, kClassDefaultObject)) : nullptr;
        if (cdo != nullptr && memcmp(cdo + kC278DefinitionTag, tag, kC278TagBytes) == 0) {
            return w;
        }
    }
    return nullptr;
}

// Card 278: one AddWeapon row on a copy. Skipped when the copy holds the class (AddWeapon would do nothing) or when
// the slot is full and no occupant of the tag is found (AddWeapon would drop one as a world pickup).
bool c278_apply_add(const C278Row& r, void* copy, void* inventory) {
    void* const cls = read_ptr(r.object, kObjectClass);
    if (g_weapon_from_class == nullptr || g_weapon_from_class(inventory, &cls) != nullptr) {
        log::line("card278: host replay of AddWeapon(%s) on %s skipped: the copy holds it", object_name(r.object).text,
                  object_name(copy).text);
        return false;
    }
    const void* const tag = static_cast<const char*>(r.object) + kC278DefinitionTag;
    const auto has_slot_free =
        reinterpret_cast<C278HasSlotFreeFn>((*static_cast<void* const* const*>(inventory))[kC278HasSlotFree]);
    if (!has_slot_free(inventory, tag)) {
        void* const occupant = c278_tag_occupant(inventory, tag);
        const bool removed = occupant != nullptr && g_orig_c132_remove_weapon(occupant, false, false);
        log::line("%scard278: host replay of AddWeapon(%s) on %s: slot full, occupant %s %s", removed ? "" : "WARN ",
                  object_name(r.object).text, object_name(copy).text, object_name(occupant).text,
                  removed ? "removed (no pickup)" : "not removed, row skipped");
        if (!removed) {
            return false;
        }
    }
    return g_orig_add_weapon(copy, r.object, r.flag);
}

// Card 278: one GiveWeaponByClass row on a copy. Skipped when the copy holds the class (the give returns false) or
// when the explicit slot holds a weapon (the give would spawn the weapon and leave it outside the inventory).
bool c278_apply_give(const C278Row& r, void* copy, void* inventory) {
    constexpr int32_t kSlots = 8; // as c267_held_weapon
    void* const cls = r.object;
    if (g_weapon_from_class == nullptr || g_weapon_from_class(inventory, &cls) != nullptr) {
        log::line("card278: host replay of GiveWeaponByClass(%s) on %s skipped: the copy holds it",
                  object_name(cls).text, object_name(copy).text);
        return false;
    }
    void* const occupant =
        r.slot >= 0 && r.slot < kSlots ? g_get_weapon_by_slot(inventory, static_cast<uint8_t>(r.slot)) : nullptr;
    if (occupant != nullptr) {
        log::line("card278: host replay of GiveWeaponByClass(%s, slot %d) on %s skipped: the slot holds %s",
                  object_name(cls).text, r.slot, object_name(copy).text, object_name(occupant).text);
        return false;
    }
    return g_orig_give_by_class(inventory, &cls, r.slot, r.flag);
}

// Card 278: one row on a copy, through the same native calls as the card 267 mirror. True when applied.
bool c278_apply_row(const C278Row& r, void* copy, void* inventory) {
    switch (r.kind) {
    case C278Kind::Add:
        return c278_apply_add(r, copy, inventory);
    case C278Kind::Give:
        return c278_apply_give(r, copy, inventory);
    case C278Kind::Drop: {
        void* const match = c267_held_weapon(inventory, r.object);
        return match != nullptr && g_orig_c132_remove_weapon(match, r.flag, false);
    }
    case C278Kind::Clear:
        return c463_clear_weapons(inventory, r.flag) > 0;
    }
    return false;
}

// Card 278: host, each tick. Replays the map's script loadout rows on each new MI pawn of a server copy.
void c278_host_step(void* world) {
    if (world != g_c278.rows_world) {
        g_c278.rows_world = world; // rows of an earlier map are dropped
        g_c278.count = 0;
        g_c278.full_warned = false;
    }
    for (int32_t s = 0; s < kC17Slots && world != nullptr; ++s) {
        const void* const pc = g_get_player_controller(world, s);
        void* const copy =
            pc != nullptr && sprint_side(pc) == kSideServerCopy ? read_ptr(pc, kControllerPawn) : nullptr;
        void* const inventory =
            copy != nullptr && c278_take_pending(copy) ? read_ptr(copy, kMiWeaponInventory) : nullptr;
        if (inventory == nullptr || g_c278.count == 0) {
            continue; // a first character with no rows yet: nothing to replay
        }
        int32_t applied = 0;
        for (int32_t i = 0; i < g_c278.count; ++i) {
            applied += c278_apply_row(g_c278.rows[i], copy, inventory) ? 1 : 0;
        }
        log::line("card278: host applied %d script loadout rows to the respawned copy %s of %s (%d rows kept)",
                  applied, object_name(copy).text, object_name(pc).text, g_c278.count);
    }
}

// Card 463: host, after a card 143 Restore on a copy's inventory: the map's script loadout rows recorded after the
// Restore's state (state_ms), applied in order over it. A state time of 0 (none known) applies none.
void c463_replay_newer(void* inventory, ULONGLONG state_ms) {
    void* const copy = read_ptr(inventory, kComponentOwner);
    if (state_ms == 0 || copy == nullptr || g_actor_get_world(copy) != g_c278.rows_world) {
        return;
    }
    int32_t due = 0;
    int32_t applied = 0;
    ULONGLONG newest = state_ms;
    for (int32_t i = 0; i < g_c278.count; ++i) {
        const C278Row& r = g_c278.rows[i];
        if (r.ms <= state_ms) {
            continue;
        }
        ++due;
        applied += c278_apply_row(r, copy, inventory) ? 1 : 0;
        newest = r.ms;
    }
    if (due > 0) {
        log::line("card463: host t=%llu Restore weapons of %s: %d script loadout rows newer than its state re-applied "
                  "(%d changed the list), the newest %llu ms after the state", static_cast<unsigned long long>(GetTickCount64()),
                  object_name(copy).text, due, applied, static_cast<unsigned long long>(newest - state_ms));
    }
}

void detour_exec_give_by_instance(void* context, void* frame, void* result) {
    c21_exec(g_orig_exec_give_by_instance, "execGiveWeaponByInstance", context, frame, result);
}

constexpr int32_t kC21TagsListed = 4;

AssignStartingFn g_orig_assign_starting = nullptr;
GetGameInstanceFn g_get_st_game_instance = nullptr;

// Names of the first kC21TagsListed tags of an FGameplayTagContainer (GameplayTags array at +0).
void c21_tags_text(const uint8_t* container, char* out, size_t cap) {
    const int32_t n = array_num(container, 0);
    const auto* const data = static_cast<const uint8_t*>(read_ptr(container, kArrayData));
    size_t used = static_cast<size_t>(snprintf(out, cap, "%d:", n));
    for (int32_t i = 0; i < n && i < kC21TagsListed && data != nullptr && used < cap; ++i) {
        used += static_cast<size_t>(snprintf(out + used, cap - used, " %s", fname_text(data + 8 * i).text));
    }
}

constexpr size_t kWeaponRowAssetName = 0x20;

bool g_c21_rows_listed = false;

void c21_list_weapon_rows(const void* game_instance) {
    g_c21_rows_listed = true;
    const void* const table = c21_weapon_table(game_instance);
    if (table == nullptr) {
        return;
    }
    const uint8_t* const map = c21_row_map(table);
    const int32_t n = array_num(map, 0);
    const auto* const data = static_cast<const uint8_t*>(read_ptr(map, kArrayData));
    log::line("card021: weapon table %s: %d rows (free %d)", object_name(table).text, n,
              *reinterpret_cast<const int32_t*>(map + kSparseNumFree));
    for (int32_t i = 0; i < n && i < kC21RowsListed && data != nullptr; ++i) {
        const uint8_t* const e = data + kRowMapElement * static_cast<size_t>(i);
        const auto* const row = static_cast<const uint8_t*>(read_ptr(e, 8));
        log::line("card021: weapon row %d %s: tag %s, class %s", i, fname_text(e).text,
                  row != nullptr ? fname_text(row + kWeaponRowTag).text : "null",
                  row != nullptr ? fname_text(row + kWeaponRowAssetName).text : "null");
    }
}

void detour_assign_starting(void* pawn, void* slots, void* weapons, void* equip, void* items) {
    g_orig_assign_starting(pawn, slots, weapons, equip, items);
    c50_after_assign(pawn); // card 050 Addendum 3: the client's own pawn, timed against the Chapter frame
    const auto* const gi = static_cast<const uint8_t*>(g_get_st_game_instance(pawn));
    if (!g_c21_rows_listed && !g_client_session) {
        c21_list_weapon_rows(gi);
    }
    if (!c21_grant_line()) {
        return;
    }
    const auto* const s = static_cast<const uint8_t*>(slots);
    const auto* const it = static_cast<const uint8_t*>(items);
    char tags[256];
    c21_tags_text(static_cast<const uint8_t*>(weapons), tags, sizeof(tags));
    log::line("card021: %s AssignStartingInventories on %s (%s): chapter %d level %d, weapons %s, equip %s, "
              "slots %d (free %d), items %d (free %d)",
              side(), object_name(pawn).text, kSprintSideText[sprint_side(pawn)],
              gi != nullptr ? *reinterpret_cast<const int32_t*>(gi + kGameInstanceChapter) : -1,
              gi != nullptr ? *reinterpret_cast<const int32_t*>(gi + kGameInstanceLevel) : -1, tags,
              fname_text(equip).text, array_num(s, 0), *reinterpret_cast<const int32_t*>(s + kSparseNumFree),
              array_num(it, 0), *reinterpret_cast<const int32_t*>(it + kSparseNumFree));
}

// --- Card 021: UBWCOOP_GRANT_WEAPON test loadout ---
// C1M1's level data gives only Weapons.Unarmed, so a real weapon is granted here: once a second,
// each player pawn this machine runs (host: own pawn and server copies; client: own pawn) that has
// no weapon with the row's tag gets the row's definition through GiveWeaponByClass(slot -1, equip).
// GetWeaponDefinitionFromTag is static and returns, in rax, the definition's default object
// (LoadSoftClassPtr result, logged as Default__BP_WeaponDefinition_*); GiveWeaponByClass wants its class.
constexpr ULONGLONG kC21GrantIntervalMs = 1000;
constexpr uint32_t kC21GrantMax = 12; // per map, all pawns together (card 136 run 4: 4 nukes, each one a regrant)

WeaponDefFromTagFn g_weapon_def_from_tag = nullptr;
WeaponFromClassFn g_weapon_from_class = nullptr;
AmmoUsesMagazineFn g_ammo_uses_magazine = nullptr;
SetEquippedAmmoFn g_set_equipped_ammo = nullptr;
AddAmmunitionFn g_add_ammunition = nullptr;
BroadcastWeaponStatusFn g_broadcast_weapon_status = nullptr;
static uint32_t g_c574_lines = 0;
constexpr uint32_t kC574Lines = 8; // per process: the refresh rows logged

// Card 574: after a mod magazine set, the weapon selector rewrites the equipped row's magazine from the weapon.
void c574_weapon_status_refresh(const void* pawn, void* inventory) {
    if (inventory == nullptr || g_broadcast_weapon_status == nullptr) {
        log::line("WARN card574: %s weapon status refresh skipped on %s: %s", side(), object_name(pawn).text,
                  inventory == nullptr ? "no weapon inventory" : "BroadcastStatusUpdate not resolved");
        return;
    }
    g_broadcast_weapon_status(inventory);
    if (g_c574_lines < kC574Lines) {
        ++g_c574_lines;
        log::line("card574: %s weapon status broadcast on %s (selector magazine refresh)", side(),
                  object_name(pawn).text);
    }
}
WeaponIndexFromTagFn g_weapon_index_from_tag = nullptr;

SetWorldRotationFn g_set_world_rotation = nullptr;
constexpr uint32_t kC21CameraLogs = 8; // per map: before/after rows of server-copy syncs that move the pitch > 1 deg

void c21_grant_off(const char* why) {
    g_c21_grant_armed = false;
    log::line("WARN card021: %s grant of %s stopped: %s", side(), g_grant_weapon.row, why);
}

// Card 137 step 5: the vanilla PickupWeapon (0x51D2FD0) gives the starting ammo after AddWeapon; the grant has no
// pickup, so it applies the same rule with the definition CDO's starting values (TryToCopyWeaponDefinitionStartingValues
// 0x51E9030 reads them there): per behaviour (1 primary, 2 secondary), element [0] of the array; a magazine type sets
// the equipped ammo, any other type goes to the pawn's ammo inventory (the static AddAmmunition).
constexpr size_t kDefStartAmmo[2] = {0xC8, 0xD8}; // TArray<{FGameplayTag tag; ...; int32 count at +0xC}>, num at +8
constexpr size_t kStartAmmoCount = 0xC;

void c21_grant_ammo(void* pawn, void* inventory, void* cls) {
    const void* const cdo = cls != nullptr ? read_ptr(cls, kClassDefaultObject) : nullptr;
    void* const weapon = cdo != nullptr && g_weapon_from_class != nullptr ? g_weapon_from_class(inventory, &cls)
                                                                           : nullptr;
    if (weapon == nullptr || g_ammo_uses_magazine == nullptr || g_set_equipped_ammo == nullptr ||
        g_add_ammunition == nullptr) {
        log::line("WARN card137: %s grant ammo skipped: %s", side(), cdo == nullptr ? "no CDO" : "no weapon instance");
        return;
    }
    for (uint8_t i = 0; i < 2; ++i) {
        const auto* const elems = static_cast<const uint8_t*>(read_ptr(cdo, kDefStartAmmo[i]));
        if (elems == nullptr || read_i32(cdo, kDefStartAmmo[i] + kArrayNum) <= 0) {
            continue;
        }
        const uint8_t behaviour = static_cast<uint8_t>(i + 1);
        uint64_t tag = 0;
        memcpy(&tag, elems, sizeof(tag));
        const int32_t count = read_i32(elems, kStartAmmoCount);
        const bool magazine = g_ammo_uses_magazine(weapon, &tag, behaviour);
        if (magazine) {
            g_set_equipped_ammo(weapon, tag, count, behaviour);
        } else {
            g_add_ammunition(pawn, tag, count);
        }
        log::line("card137: %s grant ammo %s %s %d (%s) to %s", side(), i == 0 ? "primary" : "secondary",
                  fname_text(elems).text, count, magazine ? "magazine" : "reserve", object_name(pawn).text);
    }
    c574_weapon_status_refresh(pawn, inventory); // the give's equip cached the selector row before these sets
}

// Grants the row's weapon to `pawn` when its inventory has none with that tag.
void c21_grant_to(void* pawn, uint32_t pawn_side) {
    void* const inventory = read_ptr(pawn, kMiWeaponInventory);
    const void* const table = inventory != nullptr ? c21_weapon_table(g_get_st_game_instance(pawn)) : nullptr;
    const uint8_t* const tag = table != nullptr ? c21_row_tag(table, g_grant_weapon.row) : nullptr;
    if (tag == nullptr) {
        c21_grant_off(inventory == nullptr ? "pawn has no weapon inventory" : "row or table not found");
        return;
    }
    if (g_weapon_index_from_tag(inventory, tag) >= 0) {
        g_c21_grant.held_granted[pawn_side] = g_get_equipped_weapon(inventory) == g_c21_grant.granted[pawn_side];
        g_c21_grant.missing_since[pawn_side] = 0;
        return;
    }
    // hb66: the empty launcher drops while the game's weapon switch to the Morita (GA_WeaponSwitchAbility) is still
    // swapping in; a regrant 4 ms later equipped the launcher inside that switch, the ability never ended, and the pawn
    // kept WeaponSystem.Blocked and WeaponSystem.SwitchWeapon (still owned 14 s later): no host fire after that, the
    // Morita included. The swap-in ends 200 ms after the drop, so a regrant waits until the weapon has been missing
    // for half a grant step, which with the 1 s grant step is the step after the one that saw it missing.
    const ULONGLONG now = GetTickCount64();
    if (g_c21_grant.side_grants[pawn_side] != 0) {
        if (g_c21_grant.missing_since[pawn_side] == 0) {
            g_c21_grant.missing_since[pawn_side] = now;
        }
        if (now - g_c21_grant.missing_since[pawn_side] < kC21GrantIntervalMs / 2) {
            return;
        }
    }
    g_c21_grant.missing_since[pawn_side] = 0;
    const void* const definition = g_weapon_def_from_tag(tag);
    void* cls = definition != nullptr ? read_ptr(definition, kObjectClass) : nullptr;
    if (cls == nullptr) {
        c21_grant_off("GetWeaponDefinitionFromTag returned null");
        return;
    }
    const int32_t before = g_get_weapon_count(inventory);
    const void* const held = g_get_equipped_weapon(inventory);
    // hb53: a server copy's held weapon follows its client's Equip frames (card 057), so the grant only adds the
    // weapon there. Equipping it too put the NukeLauncher back in the copy's hands 0.4 s after the client switched to
    // the Morita, and every later client press fired the empty launcher on the host (no round, no damage).
    // Card 136 hb64: a regrant (the empty launcher drops after each shot) also equipped while the pawn held the Morita,
    // so the Morita burst fired 2 nukes. A regrant equips only when the pawn's hands are empty (the dropped launcher
    // was held). hb65: the first grant must still equip; with that rule on it too, the launcher went in beside the held
    // Morita and no nuke fired (no weapon key selects it). The game may put the Morita in the hands as the empty
    // launcher drops, so "the dropped launcher was held" is read from the last grant step, not from the hands now.
    const bool first = g_c21_grant.side_grants[pawn_side] == 0;
    const bool was_held = g_c21_grant.held_granted[pawn_side];
    ++g_c21_grant.side_grants[pawn_side];
    const bool equip = pawn_side != kSideServerCopy && (first || held == nullptr || was_held);
    const bool ok = g_orig_give_by_class(inventory, &cls, -1, equip);
    const void* const equipped = g_get_equipped_weapon(inventory);
    g_c21_grant.granted[pawn_side] = equip ? equipped : nullptr;
    g_c21_grant.held_granted[pawn_side] = false;
    log::line("card021: %s grant %s (%s, %s) to %s (%s): ok %d, weapons %d -> %d, index %d, %s%s, held before %s, equipped %s", side(),
              g_grant_weapon.row, fname_text(tag).text, object_name(cls).text, object_name(pawn).text,
              kSprintSideText[pawn_side], ok ? 1 : 0, before, g_get_weapon_count(inventory),
              g_weapon_index_from_tag(inventory, tag), first ? "first grant" : "regrant", was_held ? " (the dropped one was held)" : "", object_name(held).text,
              object_name(equipped).text);
    c21_grant_ammo(pawn, inventory, cls);
    if (++g_c21_grant.done >= kC21GrantMax) {
        c21_grant_off("grant cap reached");
    }
}

void c21_test_hit(uint8_t* pawn, ULONGLONG now); // card 021 step 5 test, defined with the health watch
void c21_dump_health_sets(const uint8_t* pawn, uint32_t s);
constexpr ULONGLONG kC21SetDumpMs = 8000;

void c21_grant_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    if (now < g_c21_grant.next_ms) {
        return;
    }
    g_c21_grant.next_ms = now + kC21GrantIntervalMs;
    void* const world = g_get_game_world(engine);
    for (int i = 0; i < kC17Slots && world != nullptr && g_c21_grant_armed; ++i) {
        const void* const pc = g_get_player_controller(world, i);
        void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
        const uint32_t s = sprint_side(pawn);
        if (s != kSideOther && is_a(pawn, g_player_mi_class())) { // frontend pawns hold no inventory
            c21_grant_to(pawn, s);
        }
        if (s == kSideServerCopy && is_a(pawn, g_player_mi_class())) {
            c21_test_hit(static_cast<uint8_t*>(pawn), now);
        }
        if ((s == kSideClientOwn || s == kSideServerCopy) && is_a(pawn, g_player_mi_class()) &&
            now >= g_c21_grant.next_set_dump_ms) {
            g_c21_grant.next_set_dump_ms = now + kC21SetDumpMs;
            c21_dump_health_sets(static_cast<const uint8_t*>(pawn), s);
        }
    }
}

// Card 021 step 4: UCameraComponent::GetCameraView applies bUsePawnControlRotation only for a local player
// controller, so on the host a server copy's camera keeps pitch 0 while the client looks up and down. Do what
// GetCameraView does for a local view: put the control rotation on the camera. Server copies only (LocalRole 3,
// RemoteRole 2); the host's own camera is left to the engine.
void c21_sync_camera(uint8_t* pawn) {
    void* const cam = read_ptr(pawn, kCharacterCamera);
    const void* const controller = read_ptr(pawn, kPawnController);
    if (cam == nullptr || controller == nullptr) {
        return;
    }
    const Vec3 control = read_vec3(controller, kControllerControlRotation);
    const Vec3 before = component_pitch_yaw(cam);
    const double rotator[3] = {control.x, control.y, control.z};
    g_set_world_rotation(cam, rotator, false, nullptr, 0);
    double moved = control.x - before.x; // ControlRotation pitch is 0..360, the component's -180..180
    moved = moved > 180.0 ? moved - 360.0 : (moved < -180.0 ? moved + 360.0 : moved);
    if ((moved > 1.0 || moved < -1.0) && note_event_cap(g_c21_camera_logs, kC21CameraLogs, "card021 camera sync")) {
        const Vec3 after = component_pitch_yaw(cam);
        log::line("card021: %s server-copy camera: ControlRotation P %.2f Y %.2f; camera before P %.2f Y %.2f, "
                  "after P %.2f Y %.2f",
                  side(), control.x, control.y, before.x, before.y, after.x, after.y);
    }
}

// Card 021 step 5. UADAttributeSetHealth has no CPF_Net attribute, no OnRep_ and no GetLifetimeReplicatedProps;
// with Net.AutoRegisterReplicatedProperties on (1 in this build), FRepLayout::InitFromClass registers any CPF_Net
// property on first network use of the class (UClass::SetUpRuntimeReplicationData caches it), so the flag must be
// set on both machines before any connection replicates a health set. The meta attributes (Damage, Healing*,
// DamageTaken*) stay server-only; OverkillThreshold goes too because the client's RefreshState reads it.
// Set layout (FGameplayAttributeData 0x10: BaseValue +0x8, CurrentValue +0xC): Health +0x30.
constexpr const char* kC21NetAttributes[] = {"Health", "MaxHealth", "Armour", "MaxArmour", "OverkillThreshold"};

constexpr uint32_t kC21HealthLogs = 12;
uint32_t g_c21_health_logs = 0;

StaticClassFn g_health_set_class = nullptr;
HealthChangedFn g_health_attribute_changed = nullptr;
HealthChangedFn g_armour_attribute_changed = nullptr; // card 170
bool g_c21_in_notify = false;    // game thread; true only inside card 021's own HealthAttributeChanged call
ULONGLONG g_c21_notify_ms = 0;   // GetTickCount64 of the last card 021 notify; 0 = none yet

bool c21_is_net_attribute(const char* name) {
    for (const char* const n : kC21NetAttributes) {
        if (strcmp(n, name) == 0) {
            return true;
        }
    }
    return false;
}

// A change of a pawn's Health since the last tick. Client-own: replication wrote it silently (no RepNotify), so
// run the game's own handler (RefreshState, then OnHealthAttributeChanged for the HUD) as the server's delegate does.
void c21_watch_health(const uint8_t* pawn, uint32_t s) {
    void* component = nullptr;
    const float now = c21_health(pawn, &component);
    const float last = g_c21_health_seen[s];
    if (now < 0.0f || now == last) {
        return;
    }
    g_c21_health_seen[s] = now;
    const bool notify = last >= 0.0f && s == kSideClientOwn && g_health_attribute_changed != nullptr;
    if (notify) {
        uint8_t data[kAttributeChangeBytes] = {};
        memcpy(data + kAttributeChangeNew, &now, sizeof(now));
        memcpy(data + kAttributeChangeOld, &last, sizeof(last));
        g_c21_notify_ms = GetTickCount64();
        g_c21_in_notify = true;
        g_health_attribute_changed(component, data);
        g_c21_in_notify = false;
    }
    if (note_event_cap(g_c21_health_logs, kC21HealthLogs, "card021 health")) {
        log::line("card021: %s %s Health %.1f -> %.1f%s", side(), kSprintSideText[s], last, now,
                  notify ? ", HealthAttributeChanged called" : "");
    }
}

// Card 170: the client-own pawn's Armour, as c21_watch_health does for Health. Replication writes Armour into the
// adopted set (card 021) silently, so a change runs the game's ArmourAttributeChanged (it broadcasts the component's
// armour delegate, Old +0x3C and New +0x38, for the HUD bar). The first value per pawn is only recorded, so a new
// pawn (map change, respawn) never sees the last pawn's value as its Old.
constexpr uint32_t kC170ArmourLogs = 12;
uint32_t g_c170_armour_logs = 0;
const void* g_c170_pawn = nullptr; // pawn whose Armour g_c170_armour holds; nullptr = none yet
float g_c170_armour = -1.0f;

void c170_watch_armour(const uint8_t* pawn) {
    void* component = nullptr;
    c21_health(pawn, &component);
    const float now = c48_armour(component).armour;
    const float last = pawn == g_c170_pawn ? g_c170_armour : -1.0f;
    if (now < 0.0f || now == last) {
        return;
    }
    g_c170_pawn = pawn;
    g_c170_armour = now;
    const bool notify = last >= 0.0f && g_armour_attribute_changed != nullptr;
    if (notify) {
        uint8_t data[kAttributeChangeBytes] = {};
        memcpy(data + kAttributeChangeNew, &now, sizeof(now));
        memcpy(data + kAttributeChangeOld, &last, sizeof(last));
        g_armour_attribute_changed(component, data);
    }
    if (note_event_cap(g_c170_armour_logs, kC170ArmourLogs, "card170 armour")) {
        log::line("card170: %s client-own t=%llu Armour %.1f -> %.1f%s", side(),
                  static_cast<unsigned long long>(GetTickCount64()), last, now,
                  notify ? ", ArmourAttributeChanged called" : "");
    }
}

// Card 021 step 5 diagnosis: which health set the replicated values land in. Logs the component's set and every
// health set in the pawn's ASC SpawnedAttributes (component +0x128 is the ASC), with Health, 1 Hz, capped.
constexpr uint32_t kC21SetLogs = 10;

uint32_t g_c21_set_logs = 0;

// The health set in the ASC's SpawnedAttributes other than `own` with own's class (the replicated subobject on a
// client), or nullptr. `found` counts the health sets in SpawnedAttributes.
const void* c21_spawned_health_set(const void* asc, const void* own, int32_t* found) {
    const void* const* const items = static_cast<const void* const*>(read_ptr(asc, kAscSpawnedAttributes));
    const int32_t num = read_i32(asc, kAscSpawnedAttributesNum);
    const void* const cls = read_ptr(own, kObjectClass);
    const void* other = nullptr;
    *found = 0;
    for (int32_t i = 0; items != nullptr && i < num && i < kC21MaxSpawned; ++i) {
        if (items[i] != nullptr && read_ptr(items[i], kObjectClass) == cls) {
            ++*found;
            other = items[i] != own ? items[i] : other;
        }
    }
    return other;
}

void c21_dump_health_sets(const uint8_t* pawn, uint32_t s) {
    const void* const component = c21_character_ptr(pawn, kCharacterHealthComponent);
    const void* const asc = component != nullptr ? read_ptr(component, kHealthComponentAsc) : nullptr;
    const void* const own = component != nullptr ? read_ptr(component, kHealthComponentSet) : nullptr;
    if (asc == nullptr || own == nullptr || !note_event_cap(g_c21_set_logs, kC21SetLogs, "card021 health sets")) {
        return;
    }
    int32_t found = 0;
    const void* const other = c21_spawned_health_set(asc, own, &found);
    log::line("card021: %s %s health sets: component set %p Health %.1f/%.1f (base/current); ASC %p SpawnedAttributes "
              "%d, health sets %d, other health set %p Health %.1f",
              side(), kSprintSideText[s], own, read_f32(own, kHealthSetHealth + kAttrDataBase),
              read_f32(own, kHealthSetHealth + kAttrDataCurrent), asc, read_i32(asc, kAscSpawnedAttributesNum), found,
              other, other != nullptr ? read_f32(other, kHealthSetHealth + kAttrDataCurrent) : -1.0f);
}

// Card 021 step 5 fix, client-own pawn only. The client's health component builds its own local health set
// (card020: the client re-init is skipped), so HealthAttributeSet is not the set the server replicates into the
// ASC's SpawnedAttributes; the HUD and RefreshState read the local one and never see damage. Point the component at
// the replicated set once it exists. The local set stays referenced by nothing and is collected.
uint32_t g_c21_adopt_logs = 0;

void c21_adopt_replicated_set(uint8_t* pawn) {
    uint8_t* const component = static_cast<uint8_t*>(c21_character_ptr(pawn, kCharacterHealthComponent));
    const void* own = nullptr;
    const void* const other = c21_adopt_set(component, &own);
    if (other != nullptr && note_event_cap(g_c21_adopt_logs, 4, "card021 adopt")) {
        log::line("card021: %s client-own health component %p now uses replicated set %p (Health %.1f) instead of "
                  "local set %p (Health %.1f)", side(), component, other,
                  read_f32(other, kHealthSetHealth + kAttrDataCurrent), own,
                  read_f32(own, kHealthSetHealth + kAttrDataCurrent));
    }
}

// Card 021 step 5 test: one hit on the server copy, kC21HitDelayMs after it is first seen, written straight into
// the health set (base and current Health minus kC21HitAmount). This is a stand-in for a shot: it exercises the
// replication path, not the damage pipeline (no effect, no OnHealthAttributeChanged on the host).
constexpr ULONGLONG kC21HitDelayMs = 30000;
constexpr float kC21HitAmount = 25.0f;

void c21_test_hit(uint8_t* pawn, ULONGLONG now) {
    if (g_c21_grant.damaged) {
        return;
    }
    if (g_c21_grant.damage_at_ms == 0) {
        g_c21_grant.damage_at_ms = now + kC21HitDelayMs;
        return;
    }
    void* component = nullptr;
    const float before = c21_health(pawn, &component);
    if (now < g_c21_grant.damage_at_ms || before <= kC21HitAmount) {
        return;
    }
    g_c21_grant.damaged = true;
    uint8_t* const set = static_cast<uint8_t*>(read_ptr(component, kHealthComponentSet));
    const float after = before - kC21HitAmount;
    memcpy(set + kHealthSetHealth + kAttrDataBase, &after, sizeof(after));
    memcpy(set + kHealthSetHealth + kAttrDataCurrent, &after, sizeof(after));
    log::line("card021: %s test hit on server-copy %p: Health %.1f -> %.1f (set %p)", side(), pawn, before, after, set);
}

void c21_parity_step(void* engine) {
    void* const world = g_get_game_world(engine);
    for (int i = 0; i < kC17Slots && world != nullptr; ++i) {
        const void* const pc = g_get_player_controller(world, i);
        uint8_t* const pawn = pc != nullptr ? static_cast<uint8_t*>(read_ptr(pc, kControllerPawn)) : nullptr;
        const uint32_t s = sprint_side(pawn);
        if (s == kSideOther || !is_a(pawn, g_player_mi_class())) {
            continue;
        }
        if (s == kSideServerCopy && g_set_world_rotation != nullptr) {
            c21_sync_camera(pawn);
        }
        if (s == kSideClientOwn) {
            c21_adopt_replicated_set(pawn);
            c170_watch_armour(pawn);
        }
        c21_watch_health(pawn, s);
    }
}

// Card 021 step 3: one line per (machine, handle, result) the first time it is seen. Host: each client
// request InternalServerTryActivateAbility handled, with whether the server has a spec for that handle; client:
// each local activation. The card 016/019 caps are spent by sprint/jump/crouch before the fire steps.
constexpr int32_t kC21SeenMax = 48;
struct C21Seen {
    int32_t key[kC21SeenMax];
    int32_t count;
};
C21Seen g_c21_seen{};

bool c21_first_seen(int32_t key) {
    for (int32_t i = 0; i < g_c21_seen.count; ++i) {
        if (g_c21_seen.key[i] == key) {
            return false;
        }
    }
    if (g_c21_seen.count >= kC21SeenMax) {
        return false;
    }
    g_c21_seen.key[g_c21_seen.count++] = key;
    return true;
}

constexpr ULONGLONG kC50PollMs = 250;       // host: list compare period per copy

constexpr size_t kC50ReplicatesByte = 0x5B; // AActor::bReplicates is bit 3 of this byte
constexpr uint8_t kC50ReplicatesBit = 0x08;

constexpr uint32_t kC50AuditLines = 120; // card 050 audit detour lines
constexpr uint32_t kC50Shots = 4;        // client HUD shots after an apply
constexpr uint32_t kC50PickupBlockLines = 40; // card 140: client pickup block lines per session

C50ClassFn g_orig_c50_remove_by_class = nullptr;
C50ClassFn g_orig_c50_equip_by_class = nullptr;
C50IndexFn g_orig_c50_equip_by_index = nullptr;
C50SelfFn g_orig_c50_unequip = nullptr;
C50SelfFn g_orig_c50_pickup = nullptr;
C50SelfFn g_orig_c50_disable_pickup = nullptr;
C50PickupWeaponFn g_orig_c50_pickup_weapon = nullptr;
C50SaveFn g_orig_c50_save_checkpoint = nullptr;
StaticClassFn g_c50_weapon_pickup_class = nullptr;   // ASTWeaponPickup
StaticClassFn g_c56_pickup_class = nullptr;          // AADPickupBase (card 056)
C50SelfFn g_orig_c145_update_status = nullptr;          // AADPickupBase::OnUpdatePickupStatus (card 145)
C145SetEnabledFn g_c145_set_enabled = nullptr;           // AADPickupBase::SetPickupEnabled (card 145)
C50OverlapFn g_c50_overlapping_character = nullptr; // AADPickupBase::GetOverlappingCharacter (card 140)
StaticClassFn g_c50_item_pickup_class = nullptr;     // ASTItemPickup
StaticClassFn g_c50_tactical_pickup_class = nullptr; // ASTTacticalSupportPickup
StaticClassFn g_c50_weapon_class = nullptr;          // AADWeapon
StaticClassFn g_c50_interactable_class = nullptr;    // UADInteractableInterface
StaticClassFn g_c54_switch_class = nullptr;          // ASTSwitch
StaticClassFn g_c54_zipline_class = nullptr;         // ASTZiplinePoint
StaticClassFn g_c54_satchel_site_class = nullptr;    // ASTSatchelPlacementSite
StaticClassFn g_c54_secret_class = nullptr;          // ASTSecretPickup
StaticClassFn g_c54_limited_class = nullptr;         // ASTLimitedInteractionActor

C50State g_c50{}; // game thread

struct C50Want {
    void* cls[kC50Weapons];  // null where a path did not resolve
    int32_t slot[kC50Weapons]; // card 312: the slot (Items position) each class has on the sender
    int32_t count;
    int32_t equip;      // index into cls of the equipped weapon, -1 = none
    int32_t equip_slot; // card 312: the sender's equipped slot, -1 = none
    bool all; // every path resolved
};

// "<outer package>.<class>" for a definition class (StaticFindObject / StaticLoadObject path), or -1.
int32_t c50_class_path(const void* cls, char* out, int32_t cap) {
    const void* const outer = cls != nullptr ? read_ptr(cls, kObjectOuter) : nullptr;
    const int32_t a = outer != nullptr ? c50_name(outer, out, cap) : -1;
    if (a < 0 || a + 2 >= cap) {
        out[0] = '\0';
        return -1;
    }
    out[a] = '.';
    const int32_t b = c50_name(cls, out + a + 1, cap - a - 1);
    if (b < 0) {
        out[0] = '\0';
        return -1;
    }
    return a + 1 + b;
}

// The side comes from the counters, not side(): the client's first arm after a mission runs on the transit map,
// before its next join, where side() says "host" (c448b205a). Only a client receives Weapons frames.
void c50_totals(const char* what) {
    log::line("card050: %s %s: Weapons sent %u, received %u, applies %u, relayed presses %u (name match %u, mismatch "
              "%u), client gives outside an apply %u",
              g_c50.recvs != 0 ? "client" : "host", what, g_c50.sends, g_c50.recvs, g_c50.applies, g_c50.relays,
              g_c50.matches, g_c50.mismatches, g_c50.unguarded_gives);
}

// Host: a server copy PC's Pong. The first one per map means its client takes frames: it is marked ponged, which
// opens the card 139 send gate, and the next poll sends the list. True only for that first Pong.
bool c50_first_pong(void* pc) {
    if (sprint_side(pc) != kSideServerCopy) {
        return false;
    }
    C50Copy* const c = c50_copy(object_index(pc), true);
    if (c == nullptr || c->ponged) {
        return false;
    }
    c->ponged = true;
    c->force = "first Pong";
    c50_peer_force(); // card 050 b205: the new client gets every other player's list
    return true;
}

// Host: compare one copy's list with what its client last got; send on a change or a forced reason.
// Card 327: one row per roster slot (a server copy), logged on a change on the copy poll's cadence: the life state
// and bleedout seconds (card 043 table), the mech seat (card 075 pilot slots; the turret seat has no per-player
// source on the host, so it reads "none" there), and a 16-bit hash of the weapon list, the ammo map and the
// tactical charges. Capped per map.
constexpr uint32_t kC327Rows = 64;

uint16_t c327_hash(const char* const* parts, int32_t n) {
    uint32_t h = 2166136261u;
    for (int32_t p = 0; p < n; ++p) {
        for (const char* s = parts[p]; s != nullptr && *s != '\0'; ++s) {
            h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
        }
        h = (h ^ 0x7Cu) * 16777619u; // part separator
    }
    return static_cast<uint16_t>(h ^ (h >> 16));
}

// The hash of one on-foot soldier's weapon list (c50_text of its weapon inventory), ammo map (c72_map_text of its
// ammunition inventory, a different object) and the tactical charge text `charge` (null or empty: none). The texts
// go to `weapons` and `ammo` (kChanTextMax + 1 each). kC327NoHash without a soldier or a weapon inventory.
// Batch 3 run 8: both texts were read from the weapon inventory; the ammo read took whatever lay at its + kAmmoMap
// and crashed the client after a checkpoint load.
uint32_t c327_inventory_hash(const void* soldier, const char* charge, char* weapons, char* ammo) {
    const bool mi = soldier != nullptr && is_a(soldier, g_player_mi_class());
    void* const inventory = mi ? read_ptr(soldier, kMiWeaponInventory) : nullptr;
    if (inventory == nullptr) {
        return kC327NoHash;
    }
    if (c50_text(inventory, weapons, kChanTextMax + 1) <= 0) {
        weapons[0] = '\0';
    }
    if (c72_map_text(read_ptr(soldier, kMiAmmoInventory), ammo, kChanTextMax + 1) <= 0) {
        ammo[0] = '\0';
    }
    // The ammo map is a TMap: host and client hold the same pairs in different orders (b21 run 4: host Morita,
    // UnderslungShotgun, Laser; client Laser, Morita, UnderslungShotgun; PW WARN with equal values). Each
    // "<tag> <count>" pair is hashed alone and the pair hashes are summed, so the order does not count.
    uint32_t pairs = 0;
    const char* pair = ammo;
    constexpr int32_t kC327AmmoPairs = 64; // more pairs than a kChanTextMax text can hold are not hashed
    for (int32_t i = 0; i < kC327AmmoPairs && *pair != '\0'; ++i) {
        const char* const count = strchr(pair, ' ');
        const char* const next = count != nullptr ? strchr(count + 1, ' ') : nullptr;
        const char* const end = next != nullptr ? next : pair + strlen(pair);
        uint32_t h = 2166136261u;
        for (const char* c = pair; c < end; ++c) {
            h = (h ^ static_cast<uint8_t>(*c)) * 16777619u;
        }
        pairs += h;
        pair = next != nullptr ? next + 1 : end;
    }
    char ammo_sum[12];
    snprintf(ammo_sum, sizeof(ammo_sum), "%08x", pairs);
    const char* const parts[] = {weapons, ammo_sum, charge};
    return c327_hash(parts, 3);
}

// The soldier behind a PC's pawn: the PC's pawn is the mech while it pilots one (the soldier stays in the pilot slot).
const void* c327_soldier(const void* pawn, const void** mech) {
    // Card 450: the slots are never cleared at a leave and hold raw pointers, so only a live mech may match one
    // (a pawn allocated at a destroyed mech's address would otherwise hand back that mech's freed soldier).
    const bool is_mech = pawn != nullptr && g_mech_class != nullptr && is_a(pawn, g_mech_class());
    const void* const piloting = is_mech ? c75_soldier_of(pawn) : nullptr;
    *mech = piloting != nullptr ? pawn : nullptr;
    return piloting != nullptr ? piloting : pawn;
}

void c327_row(void* pc, C50Copy& c, ULONGLONG now) {
    const void* mech = nullptr;
    const void* const soldier = c327_soldier(read_ptr(pc, kControllerPawn), &mech);
    static char weapons[kChanTextMax + 1];
    static char ammo[kChanTextMax + 1];
    const uint32_t hash = c327_inventory_hash(soldier, c.charge, weapons, ammo);
    if (hash == kC327NoHash) {
        return;
    }
    const int32_t e = c43_find(object_index(soldier));
    const C43Life life = e >= 0 ? g_c43.e[e].state : C43Life::Up;
    const int32_t secs = e >= 0 && life != C43Life::Up ? static_cast<int32_t>(g_c43.e[e].secs_left) : 0;
    char row[sizeof(c.row)];
    snprintf(row, sizeof(row), "%s %d s, seat %s %s, inventory %04x", kC43LifeText[static_cast<size_t>(life)], secs,
             mech != nullptr ? "mech" : "none", mech != nullptr ? object_name(mech).text : "-", hash);
    if (strcmp(row, c.row) == 0) {
        return;
    }
    memcpy(c.row, row, sizeof(row));
    c440_keep(pc, weapons, ammo, c.charge); // cards 440, 445
    if (note_event_cap(g_c50.c327_rows, kC327Rows, "card327 player row")) {
        // The texts behind the hash, so a client PW WARN (which prints its own three) names the part that differs.
        log::line("card327: host t=%llu player row slot %d (copy pc %d, %s): %s; weapons %.300s; ammo %.300s; charge "
                  "%.100s", static_cast<unsigned long long>(now),
                  e >= 0 ? static_cast<int32_t>(g_c43.e[e].slot) : -1, c.pc_id, object_name(pc).text, row, weapons,
                  ammo, c.charge);
    }
}

// Card 327 PW, host: the inventory hash of the remote PC's soldier (its server copy), with the charge text last sent
// to it (the text the client applied). kC327NoHash when the PC has no copy or the soldier no inventory.
uint32_t c327_host_hash(void* pc) {
    C50Copy* const c = pc != nullptr ? c50_copy(object_index(pc), false) : nullptr;
    const void* mech = nullptr;
    const void* const soldier = c != nullptr ? c327_soldier(read_ptr(pc, kControllerPawn), &mech) : nullptr;
    static char weapons[kChanTextMax + 1];
    static char ammo[kChanTextMax + 1];
    return c327_inventory_hash(soldier, c != nullptr ? c->charge : nullptr, weapons, ammo);
}

// Card 327 PW, client: the same hash of its own soldier on foot, with its own charge text; the texts go to `t`.
// kC327NoHash while it pilots a mech (its pawn is the mech; the client has no pilot table) or has no inventory.
uint32_t c327_own_hash(const void* pawn, C327Texts& t) {
    // b181: the mech's own inventory (MechRazorback) is not what the host hashes (its pilot soldier, c327_soldier);
    // c423b179a/b printed WARN card327 PW differs for the whole ride. The caller skips the round on kC327NoHash.
    if (pawn != nullptr && g_mech_class != nullptr && is_a(pawn, g_mech_class())) {
        return kC327NoHash;
    }
    if (c104_pawn_charge_text(pawn, t.charge, sizeof(t.charge)) <= 0) {
        t.charge[0] = '\0';
    }
    return c327_inventory_hash(pawn, t.charge, t.weapons, t.ammo);
}

void c50_host_poll(void* pc, C50Copy& c, ULONGLONG now) {
    c327_row(pc, c, now); // card 327: before the on-foot check (a mech pilot's pawn is the mech)
    void* const pawn = read_ptr(pc, kControllerPawn);
    void* const inventory = pawn != nullptr && is_a(pawn, g_player_mi_class()) ? read_ptr(pawn, kMiWeaponInventory)
                                                                               : nullptr;
    if (inventory == nullptr) {
        return;
    }
    const int32_t pawn_id = object_index(pawn);
    if (pawn_id != c.pawn_id) {
        c.force = c.force != nullptr ? c.force : "new copy pawn";
        c.pawn_id = pawn_id;
        c.seed = true;
        c.ammo[0] = '\0';
        c.charge[0] = '\0'; // card 104: the new pawn's charges go out on this poll
    }
    if (pawn_id == g_c50.asc_pawn_id) {
        c.force = "LoadAbilitySystemComponent";
        g_c50.asc_pawn_id = -1;
        c.seed = true;
    }
    c.seed = c.seed && !c72_seed(pc, pawn); // card 072: retried each poll until the copy's AmmoMap has entries
    if (c.ammo[0] != '\0') {
        c72_mirror_apply(pc, c.ammo, "AmmoMirror re-applied"); // card 072: the copy's own reloads lower it
    }
    c104_charge_poll(pc, pawn, c.charge, sizeof(c.charge)); // card 104
    static char text[kChanTextMax + 1];
    const int32_t len = c50_text(inventory, text, sizeof(text));
    const bool changed = strcmp(text, c.sent) != 0;
    if (len <= 0 || (!changed && c.force == nullptr)) {
        return;
    }
    static wchar_t wire[kChanTextFrame];
    const WireString w = chan_text_frame(ChanOp::Weapons, text, wire);
    const bool sent = w.num > 0 && chan_send_client(pc, w);
    static char slots[kChanTextMax + 1];
    c50_slot_text(inventory, slots, sizeof(slots));
    log::line("card050: host t=%llu Weapons to %s (copy %s, %s): %s, %d chars: %s; slots %s",
              static_cast<unsigned long long>(now), object_name(pc).text, object_name(pawn).text,
              c.force != nullptr ? c.force : "list changed", sent ? "sent" : "WARN not sent", len, text, slots);
    if (sent) {
        memcpy(c.sent, text, static_cast<size_t>(len) + 1);
        ++g_c50.sends;
    }
    c.force = nullptr;
}

void c50_host_step(void* world, ULONGLONG now) {
    for (int s = 0; s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        C50Copy* const c = pc != nullptr && sprint_side(pc) == kSideServerCopy ? c50_copy(object_index(pc), false)
                                                                               : nullptr;
        if (c == nullptr || !c->ponged || now < c->next_ms) {
            continue;
        }
        c->next_ms = now + kC50PollMs;
        c50_host_poll(pc, *c, now);
    }
}

// The text's equipped slot, and each entry's slot and class (c50_text format, "<equipped slot>|<slot>=<path>|...";
// a class is null where its path did not resolve or the entry is malformed, logged).
C50Want c50_want(const char* text) {
    C50Want out{};
    out.all = true;
    out.equip = -1;
    char* end = nullptr;
    out.equip_slot = static_cast<int32_t>(strtol(text, &end, 10));
    const char* p = end;
    for (int32_t k = 0; p != nullptr && *p == '|' && k < kC50Weapons; ++k) {
        const char* const start = p + 1;
        const char* const bar = strchr(start, '|');
        char* eq = nullptr;
        const long slot = strtol(start, &eq, 10);
        const bool slot_ok = eq != start && *eq == '=' && slot >= 0 && slot < kC50Items;
        const char* const path_at = slot_ok ? eq + 1 : start;
        const size_t n = bar != nullptr ? static_cast<size_t>(bar - path_at) : strlen(path_at);
        char path[kC50PathMax + 1] = {};
        if (slot_ok && n > 0 && n <= static_cast<size_t>(kC50PathMax)) {
            memcpy(path, path_at, n);
            out.cls[k] = c50_resolve(path);
        }
        out.slot[k] = slot_ok ? static_cast<int32_t>(slot) : -1;
        if (out.cls[k] == nullptr) {
            out.all = false;
            log::line("WARN card050: %s weapon class %d (slot %d, %s) did not resolve", side(), k, out.slot[k], path);
        }
        out.equip = slot_ok && out.slot[k] == out.equip_slot ? k : out.equip;
        out.count = k + 1;
        p = bar;
    }
    return out;
}

// Card 312: the Items position (weapon slot) of the inventory's weapon of definition class cls, or -1.
int32_t c50_slot_of(void* inventory, const void* cls) {
    const auto* const items = static_cast<const uint8_t*>(read_ptr(inventory, kInventoryItems));
    const int32_t num = items != nullptr ? read_i32(inventory, kInventoryItems + kArrayNum) : 0;
    for (int32_t i = 0; i < num && i < kC50Items; ++i) {
        const void* const w = c50_weapon(read_ptr(items, static_cast<size_t>(i) * kC50ItemStride));
        if (w != nullptr && cls != nullptr && read_ptr(w, kWeaponDefinitionClass) == cls) {
            return i;
        }
    }
    return -1;
}

// Card 312: the weapon in Items position slot, or null.
void* c50_at_slot(void* inventory, int32_t slot) {
    const auto* const items = static_cast<const uint8_t*>(read_ptr(inventory, kInventoryItems));
    const int32_t num = items != nullptr ? read_i32(inventory, kInventoryItems + kArrayNum) : 0;
    return slot >= 0 && slot < num ? read_ptr(items, static_cast<size_t>(slot) * kC50ItemStride) : nullptr;
}

bool c50_wanted(const C50Want& want, const void* cls) {
    for (int32_t k = 0; k < want.count; ++k) {
        if (want.cls[k] != nullptr && want.cls[k] == cls) {
            return true;
        }
    }
    return false;
}

bool c50_has(const C50List& l, const void* cls) {
    for (int32_t i = 0; i < l.count; ++i) {
        if (read_ptr(l.weapon[i], kWeaponDefinitionClass) == cls) {
            return true;
        }
    }
    return false;
}

// The inventory holds exactly the wanted classes, with the wanted one equipped; with slots set (card 312), each in
// the sender's slot and the equipped slot the sender's.
bool c50_same(void* inventory, const C50Want& want, bool slots) {
    const C50List l = c50_list(inventory);
    for (int32_t i = 0; i < l.count; ++i) {
        if (!c50_wanted(want, read_ptr(l.weapon[i], kWeaponDefinitionClass))) {
            return false;
        }
    }
    for (int32_t k = 0; k < want.count; ++k) {
        if (want.cls[k] == nullptr || !c50_has(l, want.cls[k])) {
            return false;
        }
    }
    const void* const eq = l.equipped >= 0 ? read_ptr(l.weapon[l.equipped], kWeaponDefinitionClass) : nullptr;
    const void* const want_eq = want.equip >= 0 && want.equip < want.count ? want.cls[want.equip] : nullptr;
    if (eq != want_eq) {
        return false;
    }
    for (int32_t k = 0; slots && k < want.count; ++k) {
        if (c50_slot_of(inventory, want.cls[k]) != want.slot[k]) {
            return false;
        }
    }
    return !slots || want.equip < 0 || c50_equipped_slot(inventory) == want.equip_slot;
}

// Removes the weapons whose class is not wanted (unequipping first when it is the equipped one).
int32_t c50_remove_extras(void* inventory, const C50Want& want) {
    const C50List l = c50_list(inventory);
    int32_t removed = 0;
    for (int32_t i = 0; i < l.count; ++i) {
        const void* const def = read_ptr(l.weapon[i], kWeaponDefinitionClass);
        if (c50_wanted(want, def)) {
            continue;
        }
        if (i == l.equipped) {
            g_orig_c50_unequip(inventory);
        }
        log::line("card050: %s removes %s (%s)", side(), object_name(l.weapon[i]).text, object_name(def).text);
        g_remove_weapon(inventory, l.weapon[i]);
        ++removed;
    }
    return removed;
}

int32_t c50_give_missing(void* inventory, const C50Want& want) {
    int32_t given = 0;
    for (int32_t k = 0; k < want.count; ++k) {
        void* const cls = want.cls[k];
        if (cls == nullptr || c50_has(c50_list(inventory), cls)) {
            continue;
        }
        const bool ok = g_orig_give_by_class(inventory, &cls, -1, false);
        log::line("card050: %s gives %s: GiveWeaponByClass = %d", side(), object_name(cls).text, ok ? 1 : 0);
        given += ok ? 1 : 0;
    }
    return given;
}

void c50_equip(void* inventory, const C50Want& want) {
    void* const cur = g_get_equipped_weapon(inventory);
    const void* const cur_def = cur != nullptr ? read_ptr(cur, kWeaponDefinitionClass) : nullptr;
    void* const cls = want.equip >= 0 && want.equip < want.count ? want.cls[want.equip] : nullptr;
    if (cls != nullptr && cur_def != cls) {
        log::line("card050: %s equips %s (was %s)", side(), object_name(cls).text, object_name(cur_def).text);
        g_orig_c50_equip_by_class(inventory, &cls);
    } else if (want.equip < 0 && cur != nullptr) {
        log::line("card050: %s unequips %s (list has none equipped)", side(), object_name(cur).text);
        g_orig_c50_unequip(inventory);
    }
}

int32_t c141_state(const void* weapon);

// Card 312: a card 050 apply waits this long after the client's own switch request (Equip or Switch): past the swap
// animation (0.20 s) and the host's card 141 check (kC141CheckMs).
constexpr ULONGLONG kC50SwitchHoldMs = 600;

// Card 312: one weapon to move to the sender's slot.
struct C50Move {
    void* weapon;
    int32_t from;
    int32_t to;
    bool equip;
    int32_t underslung; // c141_state before the move
};

// Card 312: puts each wanted weapon in the sender's slot. RemoveWeaponByInstance does not destroy the weapon
// (UADWeaponInventory::RemoveItem: UnequipCurrentItem when it is the equipped one, the Items entry nulled, then
// AADWeapon::RemoveOwnerInInventoryFeatures, which removes the abilities and effects it granted the owner); the
// underslung state is a loose tag on the weapon's own ASC and the owner's, which neither touches. All misplaced
// weapons leave first, so a target slot held by another misplaced weapon is free when it is given back. Returns the
// moves made; a failed move is a WARN (the weapon is given back to any free slot rather than left out).
int32_t c50_move(void* inventory, const C50Want& want) {
    C50Move moves[kC50Weapons] = {};
    int32_t n = 0;
    for (int32_t k = 0; k < want.count && want.all; ++k) {
        const int32_t from = c50_slot_of(inventory, want.cls[k]);
        if (from < 0 || want.slot[k] < 0 || from == want.slot[k]) {
            continue;
        }
        void* const w = c50_at_slot(inventory, from);
        moves[n++] = C50Move{w, from, want.slot[k], k == want.equip, c141_state(w)};
    }
    for (int32_t m = 0; m < n; ++m) {
        g_remove_weapon(inventory, moves[m].weapon);
    }
    for (int32_t m = 0; m < n; ++m) {
        const C50Move& mv = moves[m];
        const bool free = c50_at_slot(inventory, mv.to) == nullptr;
        const bool given = free && g_give_by_instance(inventory, mv.weapon, mv.to, mv.equip);
        const bool ok = given && c50_at_slot(inventory, mv.to) == mv.weapon;
        const void* const def = read_ptr(mv.weapon, kWeaponDefinitionClass);
        const bool back = ok || c50_slot_of(inventory, def) >= 0 ||
                          g_give_by_instance(inventory, mv.weapon, -1, mv.equip);
        log::line("%scard050: client moves %s slot %d -> %d (equip %d): %s; underslung %d -> %d; now in slot %d%s",
                  ok ? "" : "WARN ", object_name(mv.weapon).text, mv.from, mv.to, mv.equip ? 1 : 0,
                  ok ? "ok" : "failed", mv.underslung, c141_state(mv.weapon), c50_slot_of(inventory, def),
                  ok ? "" : !free ? " (target slot held)" : back ? " (given back to a free slot)"
                                                                 : " (NOT back in the inventory)");
    }
    return n;
}

void c50_apply(void* pawn, ULONGLONG now) {
    void* const inventory = read_ptr(pawn, kMiWeaponInventory);
    if (inventory == nullptr) {
        log::line("WARN card050: client apply skipped: %s has no weapon inventory", object_name(pawn).text);
        return;
    }
    static char before[kChanTextMax + 1];
    static char after[kChanTextMax + 1];
    c50_text(inventory, before, sizeof(before));
    static char slots_before[kChanTextMax + 1];
    c50_slot_text(inventory, slots_before, sizeof(slots_before));
    const C50Want want = c50_want(g_c50.text);
    g_c50.applying = true;
    const int32_t removed = want.all ? c50_remove_extras(inventory, want) : 0; // keep everything on a lookup failure
    const int32_t given = c50_give_missing(inventory, want);
    const int32_t moved = c50_move(inventory, want); // card 312
    c50_equip(inventory, want);
    g_c50.applying = false;
    c50_text(inventory, after, sizeof(after));
    ++g_c50.applies;
    const bool same = c50_same(inventory, want, true);
    static char slots_after[kChanTextMax + 1];
    c50_slot_text(inventory, slots_after, sizeof(slots_after));
    log::line("card050: client t=%llu apply #%u on %s: removed %d, given %d, moved %d, %s; before %s; after %s; host %s; "
              "slots before %s, after %s",
              static_cast<unsigned long long>(now), g_c50.applies, object_name(pawn).text, removed, given, moved,
              same ? "matches the host" : "WARN differs from the host", before, after, g_c50.text, slots_before,
              slots_after);
    if (g_c50.shots < kC50Shots) {
        ++g_c50.shots;
        c43_shot(now + 1500, "card050 client HUD after a weapon apply", kC44ShotUi);
    }
}

void c50_receive(void* pc, const WireString* s) {
    (void)pc;
    const int32_t len = chan_text(s, g_c50.text, sizeof(g_c50.text));
    ++g_c50.recvs;
    g_c50.have_text = len > 0;
    g_c50.pending = len > 0;
    log::line("card050: client t=%llu Weapons received (#%u, %d chars): %s",
              static_cast<unsigned long long>(GetTickCount64()), g_c50.recvs, len, g_c50.text);
}

void c50_client_step(void* pc, ULONGLONG now) {
    void* const own = const_cast<void*>(c23_own_pawn(pc));
    if (own == nullptr || !is_a(own, g_player_mi_class())) {
        return;
    }
    const int32_t id = object_index(own);
    if (id != g_c50.own_id) {
        log::line("card050: client t=%llu own pawn %s on foot%s", static_cast<unsigned long long>(now),
                  object_name(own).text, g_c50.have_text ? ": the held list is applied again" : "");
        g_c50.own_id = id;
        g_c50.pending = g_c50.pending || g_c50.have_text;
    }
    // Card 312: no apply (its slot moves unequip and re-equip) while the client's own switch request is in flight.
    if (!g_c50.pending || c43_is_down(own) || now < g_c50.switch_due_ms) {
        return;
    }
    g_c50.pending = false;
    c50_apply(own, now);
}

// Addendum 3: the client's AssignStartingInventories, timed against the Chapter frame; the held list goes again.
void c50_after_assign(const void* pawn) {
    if (!g_client_session) {
        c143_host_assign(pawn); // card 143: a new server copy's starting inventory is in; a held Restore may apply
        c278_note_assign(pawn); // card 278: its script loadout replay waits for the step
        return;
    }
    if (!is_a(pawn, g_player_mi_class())) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_c50.chapter_ms != 0) {
        log::line("card050: client t=%llu AssignStartingInventories on %s (%s): %lld ms after the Chapter frame; "
                  "list %s",
                  static_cast<unsigned long long>(now), object_name(pawn).text, kSprintSideText[sprint_side(pawn)],
                  static_cast<long long>(now - g_c50.chapter_ms), g_c50.have_text ? "held, applied again" : "none yet");
    } else {
        log::line("card050: client t=%llu AssignStartingInventories on %s (%s): no Chapter frame yet this map; list %s",
                  static_cast<unsigned long long>(now), object_name(pawn).text, kSprintSideText[sprint_side(pawn)],
                  g_c50.have_text ? "held, applied again" : "none yet");
    }
    g_c50.pending = g_c50.pending || g_c50.have_text;
}

// --- Card 050 b205: every other player's weapons on each client (senior b205 item 1, card 323 Pl:wp) ---
// The Weapons list above reaches only the client's own pawn, and card 463 skips the client's own level script weapon
// edits for every pawn, so the client's copy of the host's pawn (and of any other client's) kept its spawn loadout
// (c448b204a: the client's guid 8e tree held only BP_Weapon_Unarmed_C while the host's pawn held the Morita).
// Host: each player soldier's c50_text, with its controller slot and NetGUID, goes to every client as one PeerWeapons
// text frame "<slot> <guid hex> <list>" on a change, on a new pawn in that slot, and all again on a client's first
// Pong (polled every kC50PollMs). Client: each slot's last list is checked on the copy its guid resolves to (never the
// own pawn, which card 050 owns) and applied (extras removed, missing ones given, the equipped one equipped) when it
// differs; kC50PeerTries applies per list and copy, then the row stops until the next list or a new copy.
constexpr int32_t kC50PeerTries = 3;
constexpr uint32_t kC50PeerLines = 64; // card 050 b205 event rows per map
struct C50PeerHost {
    int32_t pawn_id;
    char sent[kChanTextMax + 1];
};
struct C50PeerClient {
    uint64_t guid;
    int32_t pawn_id; // the copy the row last checked
    int32_t tries;
    bool done;
    char text[kChanTextMax + 1];
};
struct C50Peers {
    C50PeerHost host[kC17Slots];
    C50PeerClient client[kC17Slots];
    ULONGLONG next_ms;
    bool force;
    uint32_t sends, recvs, applies, lines;
};
C50Peers g_c50_peers{}; // game thread

// Card 495 (c448b240a, user: "CTF door opens before both players have laser gun"): the level script's GiveLaser
// (pickup -> RemovePlayersWeapons, AddWeaponByTag, card 267 mirror) ran at 681540486 and its door opened at once
// (client DoorState 0 -> 2 at 518), but the copy's Weapons compare ran on the 250 ms poll (sent at 657, client
// equip at 690) and the client's view of the host pawn on the peer poll (790). Every host give, add, removal or
// pickup now pulls both compares to the next step, so the list leaves in the same frame as the script's door row.
// The poll stays the fallback for changes no detour sees.
void c50_host_nudge() {
    if (g_client_session) {
        return;
    }
    for (int32_t i = 0; i < g_c50.copies && i < kC17Slots; ++i) {
        g_c50.copy[i].next_ms = 0;
    }
    g_c50_peers.next_ms = 0;
}

bool c50_peer_line() {
    return note_event_cap(g_c50_peers.lines, kC50PeerLines, "card050 peer weapons");
}

void c50_peer_force() {
    g_c50_peers.force = true;
}

void c50_peer_arm_map() {
    if (g_c50_peers.sends + g_c50_peers.recvs != 0) {
        log::line("card050: %s peer weapon map totals: sent %u, received %u, applies %u", // side: c50_totals
                  g_c50_peers.recvs != 0 ? "client" : "host", g_c50_peers.sends, g_c50_peers.recvs,
                  g_c50_peers.applies);
    }
    g_c50_peers = C50Peers{};
    for (C50PeerHost& h : g_c50_peers.host) {
        h.pawn_id = -1;
    }
}

// Host: one slot's soldier list to every client when it changed (or force).
void c50_peer_host_slot(void* world, int32_t slot, bool force) {
    void* const pc = g_get_player_controller(world, slot);
    const void* mech = nullptr;
    const void* const soldier = pc != nullptr ? c327_soldier(read_ptr(pc, kControllerPawn), &mech) : nullptr;
    const bool mi = soldier != nullptr && is_a(soldier, g_player_mi_class());
    void* const inventory = mi ? read_ptr(soldier, kMiWeaponInventory) : nullptr;
    const uint64_t guid = inventory != nullptr ? c41_guid(soldier) : 0;
    if (guid == 0) {
        return;
    }
    C50PeerHost& h = g_c50_peers.host[slot];
    const int32_t id = object_index(soldier);
    static char text[kChanTextMax + 1];
    if (c50_text(inventory, text, sizeof(text)) <= 0 || (!force && id == h.pawn_id && strcmp(text, h.sent) == 0)) {
        return;
    }
    static char frame[kChanTextMax + 1];
    const int n = snprintf(frame, sizeof(frame), "%d %llx %s", slot, static_cast<unsigned long long>(guid), text);
    if (n <= 0 || n >= static_cast<int>(sizeof(frame))) {
        log::line("WARN card050: host peer weapons of slot %d not sent: %d chars past the frame", slot, n);
        return;
    }
    const C95Fanout f = c95_send_all(world, ChanOp::PeerWeapons, frame);
    if (c50_peer_line()) {
        log::line("card050: host t=%llu peer weapons slot %d %s guid %llx to %d of %d client(s) (%s): %s",
                  static_cast<unsigned long long>(GetTickCount64()), slot, object_name(soldier).text,
                  static_cast<unsigned long long>(guid), f.sent, f.to,
                  force ? "forced" : (id != h.pawn_id ? "new pawn" : "list changed"), text);
    }
    if (f.sent > 0) {
        h.pawn_id = id;
        snprintf(h.sent, sizeof(h.sent), "%s", text);
        ++g_c50_peers.sends;
    }
}

void c50_peer_host_step(void* world, ULONGLONG now) {
    if (now < g_c50_peers.next_ms || c95_ponged_count(world) == 0) {
        return;
    }
    g_c50_peers.next_ms = now + kC50PollMs;
    const bool force = g_c50_peers.force;
    g_c50_peers.force = false;
    for (int32_t s = 0; s < kC17Slots; ++s) {
        c50_peer_host_slot(world, s, force);
    }
}

void c50_peer_receive(void* pc, const WireString* s) {
    (void)pc;
    static char text[kChanTextMax + 1];
    const int32_t len = chan_text(s, text, sizeof(text));
    char* end = nullptr;
    const long slot = len > 0 ? strtol(text, &end, 10) : -1;
    char* list = nullptr;
    const unsigned long long guid = end != nullptr && *end == ' ' ? strtoull(end + 1, &list, 16) : 0;
    if (slot < 0 || slot >= kC17Slots || guid == 0 || list == nullptr || *list != ' ') {
        log::line("WARN card050: client peer weapons frame malformed (%d chars): %s", len, len > 0 ? text : "");
        return;
    }
    C50PeerClient& r = g_c50_peers.client[slot];
    r.guid = guid;
    r.pawn_id = -1;
    r.tries = 0;
    r.done = false;
    snprintf(r.text, sizeof(r.text), "%s", list + 1);
    ++g_c50_peers.recvs;
    g_c50_peers.next_ms = 0; // card 495: applied on the next step, not the next 250 ms poll
    if (c50_peer_line()) {
        log::line("card050: client t=%llu peer weapons received slot %ld guid %llx: %s",
                  static_cast<unsigned long long>(GetTickCount64()), slot, guid, r.text);
    }
}

// Client: one held row on the copy its guid resolves to.
void c50_peer_apply_row(void* pc, const void* own, C50PeerClient& r, ULONGLONG now) {
    void* const pawn = r.guid != 0 ? c41_resolve_player(pc, r.guid) : nullptr;
    void* const inventory = pawn != nullptr && pawn != own && is_a(pawn, g_player_mi_class())
                                ? read_ptr(pawn, kMiWeaponInventory)
                                : nullptr;
    if (inventory == nullptr) {
        return;
    }
    const int32_t id = object_index(pawn);
    if (id != r.pawn_id) {
        r.pawn_id = id;
        r.tries = 0;
        r.done = false;
    }
    if (r.done) {
        return;
    }
    const C50Want want = c50_want(r.text);
    if (c50_same(inventory, want, false)) {
        r.done = true;
        return;
    }
    if (r.tries >= kC50PeerTries) {
        r.done = true;
        log::line("WARN card050: client peer weapons on %s guid %llx still differ after %d applies; host %s",
                  object_name(pawn).text, static_cast<unsigned long long>(r.guid), kC50PeerTries, r.text);
        return;
    }
    ++r.tries;
    static char before[kChanTextMax + 1];
    static char after[kChanTextMax + 1];
    c50_text(inventory, before, sizeof(before));
    g_c50.applying = true;
    const int32_t removed = want.all ? c50_remove_extras(inventory, want) : 0; // keep everything on a lookup failure
    const int32_t given = c50_give_missing(inventory, want);
    c50_equip(inventory, want);
    g_c50.applying = false;
    c50_text(inventory, after, sizeof(after));
    ++g_c50_peers.applies;
    r.done = c50_same(inventory, want, false);
    if (c50_peer_line()) {
        log::line("card050: client t=%llu peer weapons apply on %s guid %llx (try %d): removed %d, given %d, %s; "
                  "before %s; after %s; host %s",
                  static_cast<unsigned long long>(now), object_name(pawn).text,
                  static_cast<unsigned long long>(r.guid), r.tries, removed, given,
                  r.done ? "matches the host" : "differs from the host", before, after, r.text);
    }
}

void c50_peer_client_step(void* pc, ULONGLONG now) {
    if (now < g_c50_peers.next_ms) {
        return;
    }
    g_c50_peers.next_ms = now + kC50PollMs;
    const void* const own = c23_own_pawn(pc);
    for (C50PeerClient& r : g_c50_peers.client) {
        c50_peer_apply_row(pc, own, r, now);
    }
}

// Counted when the client's own inventory gets a weapon outside a card 050 apply (a pickup, a starting loadout).
void c50_note_give(const void* inventory, const void* cls, bool ok) {
    const void* const owner = inventory != nullptr ? read_ptr(inventory, kComponentOwner) : nullptr;
    if (!g_client_session || g_c50.applying || sprint_side(owner) != kSideClientOwn) {
        return;
    }
    ++g_c50.unguarded_gives;
    if (c50_line()) {
        log::line("card050: client gives %s outside an apply (#%u) = %d; the next host list decides",
                  object_name(cls).text, g_c50.unguarded_gives, ok ? 1 : 0);
    }
}

// --- Card 050 audit detours (log only) ---
bool c50_audit_line() {
    return note_event_cap(g_c50.audit_lines, kC50AuditLines, "card050 audit");
}

void c50_pickup_line(const char* fn, const void* pickup, const void* character) {
    if (!c50_audit_line()) {
        return;
    }
    char chain[96];
    caller_chain(chain, sizeof(chain));
    const auto* const p = static_cast<const uint8_t*>(pickup);
    log::line("card050: %s %s on %s (%s), placed %d, bReplicates %d, roles local %u remote %u; character %s (%s); "
              "callers %s",
              side(), fn, object_name(pickup).text, class_name(pickup).text, c50_placed(pickup) ? 1 : 0,
              (p[kC50ReplicatesByte] & kC50ReplicatesBit) != 0 ? 1 : 0, p[kActorLocalRole], p[kActorRemoteRole],
              object_name(character).text, kSprintSideText[sprint_side(character)], chain);
}

static bool c495_refuse(void* pickup, void* character); // card 495, below

// Always installed (card 495 needs every take); the card 050 row stays a diag row.
void detour_c50_pickup_weapon(void* pickup, void* character) {
    if (g_gate_diag) {
        c50_pickup_line("ASTWeaponPickup::PickupWeapon", pickup, character);
    }
    if (c495_refuse(pickup, character)) {
        return; // this player already took it: no second credit, and the base call that follows is skipped
    }
    g_orig_c50_pickup_weapon(pickup, character);
}

void detour_c50_pickup(void* pickup) {
    c50_pickup_line("AADPickupBase::Pickup", pickup, nullptr);
    g_orig_c50_pickup(pickup);
}

// Client, card 140 run 16: a placed pickup is the host's (it does not replicate; card 056 PickupGone carries its state),
// so the game's own pickup path does not disable it here. c140o: the client's overlap took BP_GrenadeAmmoPickup_Frag_C_5,
// which the host never took, and the digest read "pickup host 1 client 0" to map end. c56_receive calls the original
// directly, so the host's row still disables it. Only an overlap-driven disable (a character on the pickup) is blocked:
// c140p showed the game's load-time disable of collected BP_DevLogPickup_C runs with no overlapper on both sides, and the
// host's PickupGone for it went out before the client joined (sent to 0 of 0), so blocking it left "pickup host 0
// client 1" in every round. True when blocked.
// Card 145: the host's state now comes as an 'E' row (placed) or an 'N' row (a card 104 copy), applied through
// SetPickupEnabled, which this detour does not see. A card 104 copy is the host's too: the playtest client's overlap
// disabled its copy of a rack pickup (Pickup_Implementation from ASTItemPickup::Pickup_Implementation from
// OnBeginOverlap) and the copy stayed off, so a copy's overlap-driven disable is blocked the same way.
// Card 254: the test alone, no side effects. The overlapping character when the client must not run the game's own
// take of this pickup (a placed pickup or a card 104 host copy with a character on it, in a client session), else null.
const void* c50_client_pickup_overlapper(void* pickup) {
    uint32_t host_id = 0;
    // Card 239: a card 104 copy enters g_c104_copies only after its spawn returns, and a copy spawned on top of a
    // character overlaps it inside SpawnActor (c234-c5m1: the client's pawn on a killed crate took the host's drop
    // copy, 6 "no spawn" rows). The actor being spawned under g_c107_copy_spawn is the host's copy by construction.
    const bool hosts = c50_placed(pickup) || g_c107_copy_spawn || c104_copy_host_id(pickup, host_id);
    if (!g_client_session || !hosts || g_c50_overlapping_character == nullptr) {
        return nullptr;
    }
    return g_c50_overlapping_character(pickup);
}

bool c50_client_pickup_blocked(void* pickup, uint64_t caller) {
    const void* const who = c50_client_pickup_overlapper(pickup);
    if (who == nullptr) {
        return false;
    }
    ++g_c50.pickup_blocks;
    if (note_event_cap(g_c50.pickup_block_lines, kC50PickupBlockLines, "card140 pickup block")) {
        char name[kChanNameMax + 1];
        if (c50_name(pickup, name, sizeof(name)) <= 0) {
            snprintf(name, sizeof(name), "%s", object_name(pickup).text);
        }
        log::line("card140: client blocked the game's DisablePickup on %s %s (%s), caller exe+0x%llX: overlapping "
                  "character %s (%s, %s, guid 0x%llx); %u blocked this session",
                  c50_placed(pickup) ? "placed" : "host copy", name, class_name(pickup).text,
                  static_cast<unsigned long long>(caller), object_name(who).text,
                  class_name(who).text, kSprintSideText[sprint_side(who)],
                  static_cast<unsigned long long>(c41_guid(who)), g_c50.pickup_blocks);
    }
    return true;
}

void detour_c50_disable_pickup(void* pickup) {
    c50_pickup_line("AADPickupBase::DisablePickup", pickup, nullptr);
    if (c50_client_pickup_blocked(pickup, exe_rva(_ReturnAddress()))) {
        return;
    }
    g_orig_c50_disable_pickup(pickup);
    c137_client_change(pickup, "pickup enabled", 1, 0, exe_rva(_ReturnAddress())); // card 137 2a
    // Card 145: the host's change is sent by detour_c145_update_status, which the original ran. A relayed pickup's copy
    // stays until the actor's own Destroy (card 105 sent 'D' here, so a drop the game disables at spawn and enables
    // when it lands, BP_GrenadeAmmoPickup_Incendiary_C, never came back on the client).
}

// Card 254: client. Every pickup subclass ends its take in AADPickupBase::Pickup_Implementation: FX, DisablePickup, then
// the OnPickup broadcast that level scripts bind objective chains to. The host is the only source of a take's
// consequences: the client's own take also overlaps the pickup with its server copy on the host, and the host's FX
// (card 136), enabled state (card 145) and objective rows reach the client. So a take the card 140 test blocks skips
// the whole base call. The subclass credits (ASTAmmoPickup AddAmmunitions, ASTWeaponPickup PickupWeapon and
// AddAmmunitionTypeToInventory) run before their call of the base and are not changed here.
C50SelfFn g_orig_c254_pickup_implementation = nullptr;
static constexpr uint32_t kC254Lines = 100;
static uint32_t g_c254_lines = 0; // client, game thread

// Card 245 (b190): host/standalone. A placed pickup's OnPickup bindings (the level script chains card 254 names), the
// taker and the level values the C4M2 handlers read ("PlayerHasControl", and "Satchels": the Bridge satchel handler
// adds 1 per pickup and completes Bridge_SO2 at 4, ubergraph 0x4cb9), before and after the base call. Pickup_Implementation broadcasts OnPickup right after DisablePickup (0x4C61EE3, 0x4C61EEE), so
// the "after" row sees the handler's own changes. "-" is a value the bound object's class does not have.
static constexpr uint32_t kC245PickupLines = 40;
static constexpr int32_t kC245BindMax = 4;
static constexpr size_t kC245BindEntry = 0x10; // FScriptDelegate: FWeakObjectPtr + FName
static constexpr size_t kC245BindName = 0x8;
static uint32_t g_c245_pickup_lines = 0; // host, game thread

static void c245_pickup_row(void* pickup, const char* when) {
    if (g_client_session || !c50_placed(pickup) || g_weak_get == nullptr) {
        return;
    }
    const uint8_t* const prop = prop_find_prop(read_ptr(pickup, kObjectClass), "OnPickup");
    if (prop == nullptr) {
        return;
    }
    const auto off = static_cast<size_t>(read_i32(prop, kPropertyOffset));
    const auto* const list = static_cast<const uint8_t*>(read_ptr(pickup, off));
    const int32_t num = list != nullptr ? read_i32(pickup, off + kArrayNum) : 0;
    if (num <= 0 || !note_event_cap(g_c245_pickup_lines, kC245PickupLines, "card245 pickup")) {
        return;
    }
    char binds[320] = "";
    size_t n = 0;
    const void* level = nullptr;
    for (int32_t i = 0; i < num && i < kC245BindMax && n < sizeof(binds); ++i) {
        const uint8_t* const entry = list + static_cast<size_t>(i) * kC245BindEntry;
        void* const bound = g_weak_get(const_cast<uint8_t*>(entry));
        char fn[160];
        if (fname_into(entry + kC245BindName, fn, sizeof(fn)) == 0) {
            snprintf(fn, sizeof(fn), "?");
        }
        const int w = snprintf(binds + n, sizeof(binds) - n, "%s%s.%s", i > 0 ? ", " : "",
                               bound != nullptr ? object_name(bound).text : "(gone)", fn);
        n += w > 0 ? static_cast<size_t>(w) : 0;
        level = level != nullptr ? level : bound;
    }
    char has_control[64] = "-";
    char satchels[32] = "-";
    if (level != nullptr) {
        c22_prop_text(level, "PlayerHasControl", has_control, sizeof(has_control));
        c22_prop_text(level, "Satchels", satchels, sizeof(satchels));
    }
    const void* const taker = g_c50_overlapping_character != nullptr ? g_c50_overlapping_character(pickup) : nullptr;
    log::line("card245: %s t=%llu pickup %s (%s) %s: OnPickup bindings %d [%s]; taker %s (%s); level %s "
              "PlayerHasControl %s, Satchels %s",
              side(), static_cast<unsigned long long>(GetTickCount64()), object_name(pickup).text,
              class_name(pickup).text, when, num, binds, object_name(taker).text,
              kSprintSideText[sprint_side(taker)], object_name(level).text, has_control, satchels);
}

// Card 495 (b243): host. A level-script weapon pickup that every living player takes for themselves before the
// level's OnPickup chain runs. FED_C1M1's handlers for these pickups run RemovePlayersWeapons, then GiveLaser or
// GiveMorita on player 0 (card 267 mirrors both to every other pawn), then the objective step: LaserMorita_C_0 opens
// door C_1 and completes CaptureTheFlag_SO1 (ubergraph 6572, 5408), MoritaMk1_C_0 starts the shooting range
// (ShootingRange_SO1, 9740), MoritaMk1_C_1 skips tactical support 1 and 2 (TacSupport_SO3, 1634). User rule
// 2026-11-18: host and client each pick up the laser, then the door opens; the mirror give does not count.
// Each take credits its own player (ASTWeaponPickup::PickupWeapon, which runs before the base call); the base
// AADPickupBase::Pickup_Implementation (DisablePickup, then the OnPickup broadcast) is held until every living roster
// player is in the pickup's taker set (containment, not a count: a taker who left never stands in for a non-taker).
// Takers are keyed by PlayerController, so a respawned pawn is the same taker and a player who joins while a base is
// held is a non-taker. A solo host covers the set with its first take, so the base runs at once as unmodded.
// Not on this table: the satchel pickups (one carried charge per site: C4M2 counts 4 pickups for Bridge_SO2, C2M1
// completes LOS_O03_SO02a/b/c per pickup, C1M1's CTF satchel is the flag that ends the course) and the other maps'
// level-bound weapon pickups (their handlers switch off a flare marker or set a flag; no give, no objective step).
// Card 553: a take that does not cover every living player arms its taker at once with the site's weapon (the
// script's own AddWeapon(definition default object, equip 1), on the taker's pawn only), as single player does; only
// the base call (the script's objective step and its give to every player) waits. Before, the first taker had no
// weapon until the last player took (host log 2026-11-20: 9.5 s and 12.4 s empty-handed).
struct C495Site {
    const char* pickup_class;
    const char* level_class;      // the OnPickup binding's object class: the site is this map's scripted pickup only
    const wchar_t* definition;    // card 553: the weapon definition class the script's AddWeapon gives
};
static constexpr C495Site kC495Sites[] = {
    {"BP_WeaponPickup_LaserMorita_C", "FED_C1M1_Scripting_C", L"BP_WeaponDefinition_LaserMorita_C"},
    {"BP_WeaponPickup_MoritaMk1_C", "FED_C1M1_Scripting_C", L"BP_WeaponDefinition_MoritaMk1_C"},
};
static constexpr int32_t kC495Max = 32;         // per-player pickups with takers at once (C1M1: 3 weapons, card 570
                                                 // ammo and grenade pickups a player has not taken yet)
static constexpr ULONGLONG kC495RecheckMs = 500; // the held-base re-check period, as card 227's
static constexpr uint32_t kC495Lines = 60;       // held and released rows
static constexpr uint32_t kC495RefuseLines = 30; // refused rows: their own cap, so repeated presses never hide a release

struct C495Entry {
    C31Weak pickup;
    C31Weak takers[kC43Max]; // PlayerControllers that took this pickup
    int32_t takers_n;
    bool held; // a base call was held and has not run since
};
struct C495Table {
    C495Entry e[kC495Max];
    int32_t count;
    const void* refused; // the pickup whose PickupWeapon was just refused: its base call is skipped once
    void* taker;         // card 553: the character PickupWeapon just credited (its base call follows), else null
    ULONGLONG recheck_ms;
    uint32_t lines;
    uint32_t refuse_lines;
    uint32_t take_lines; // card 570 take rows
};
static C495Table g_c495{}; // host, game thread

static bool c495_line() {
    return note_event_cap(g_c495.lines, kC495Lines, "card495 per-player pickup");
}

// True when a placed pickup of a kC495Sites class has an OnPickup binding on that row's level script object.
static bool c495_site(const void* pickup) {
    if (g_client_session || pickup == nullptr || !c50_placed(pickup) || g_weak_get == nullptr) {
        return false;
    }
    const char* level_class = nullptr;
    const ObjectName cls = class_name(pickup);
    for (const C495Site& s : kC495Sites) {
        level_class = strcmp(cls.text, s.pickup_class) == 0 ? s.level_class : level_class;
    }
    const uint8_t* const prop = level_class != nullptr ? prop_find_prop(read_ptr(pickup, kObjectClass), "OnPickup")
                                                       : nullptr;
    if (prop == nullptr) {
        return false;
    }
    const auto off = static_cast<size_t>(read_i32(prop, kPropertyOffset));
    const auto* const list = static_cast<const uint8_t*>(read_ptr(pickup, off));
    const int32_t num = list != nullptr ? read_i32(pickup, off + kArrayNum) : 0;
    for (int32_t i = 0; i < num && i < kC245BindMax; ++i) {
        void* const bound = g_weak_get(const_cast<uint8_t*>(list + static_cast<size_t>(i) * kC245BindEntry));
        if (bound != nullptr && strcmp(class_name(bound).text, level_class) == 0) {
            return true;
        }
    }
    return false;
}

static C495Entry* c495_find(const void* pickup) {
    for (int32_t i = 0; i < g_c495.count && i < kC495Max; ++i) {
        if (c31_weak_get(g_c495.e[i].pickup) == pickup) {
            return &g_c495.e[i];
        }
    }
    return nullptr;
}

static void c495_drop(C495Entry* e) {
    const auto i = static_cast<int32_t>(e - g_c495.e);
    g_c495.e[i] = g_c495.e[g_c495.count - 1];
    g_c495.e[--g_c495.count] = C495Entry{};
}

static bool c495_has(const C495Entry& e, const void* pc) {
    for (int32_t t = 0; t < e.takers_n && t < kC43Max; ++t) {
        if (pc != nullptr && c31_weak_get(e.takers[t]) == pc) {
            return true;
        }
    }
    return false;
}

struct C495Cover {
    int32_t living;         // living roster players (a pawn, not Downed, Out or Dead: card 227's rule)
    int32_t taken;          // of them, the ones in the taker set
    void* taker_pawn;       // one living taker's pawn, null when none
};

static C495Cover c495_cover(const C495Entry& e, const void* world) {
    C495Cover c{};
    const C43Roster r = c43_roster(world);
    for (int32_t s = 0; s < kC43Max; ++s) {
        void* const pawn = r.p[s].pawn;
        if (pawn == nullptr || c43_is_down(pawn)) {
            continue;
        }
        ++c.living;
        const bool took = c495_has(e, r.p[s].pc);
        c.taken += took ? 1 : 0;
        c.taker_pawn = took && c.taker_pawn == nullptr ? pawn : c.taker_pawn;
    }
    return c;
}

// The pickup's instance name (BP_WeaponPickup_MoritaMk1_C_0), as the card 254 rows name it; the scenario waits on it.
struct C495Name {
    char text[kChanNameMax + 1];
};
static C495Name c495_name(const void* pickup) {
    C495Name n{};
    if (c50_name(pickup, n.text, sizeof(n.text)) <= 0) {
        snprintf(n.text, sizeof(n.text), "%s", object_name(pickup).text);
    }
    return n;
}

// detour_c50_pickup_weapon: true when this take is refused (the character's player already took this pickup), so the
// weapon is not credited again and the base call that follows is skipped. Otherwise the player joins the taker set.
static bool c495_join(void* pickup, void* character, bool weapon);

static bool c495_refuse(void* pickup, void* character) {
    g_c495.refused = nullptr;
    g_c495.taker = nullptr;
    return c495_site(pickup) && c495_join(pickup, character, true);
}

// A take of a tracked per-player pickup (a card 495 weapon site, or a card 570 ammo or grenade pickup): true when the
// character's player already took it (refused, logged), else the player joins the taker set. `weapon`: card 553 arms
// the taker when the base call is held.
static bool c495_join(void* pickup, void* character, bool weapon) {
    const void* const pc = character != nullptr ? read_ptr(character, kPawnController) : nullptr;
    C495Entry* e = c495_find(pickup);
    if (e == nullptr && pc != nullptr && g_c495.count < kC495Max) { // no entry for an untracked take: it never holds
        e = &g_c495.e[g_c495.count++];
        c31_weak_set(e->pickup, pickup);
    }
    if (e == nullptr || pc == nullptr) {
        log::line("WARN card495: host t=%llu take of %s by %s not tracked: %s", static_cast<unsigned long long>(
                  GetTickCount64()), c495_name(pickup).text, object_name(character).text,
                  pc == nullptr ? "the character has no controller" : "the table is full");
        return false;
    }
    if (c495_has(*e, pc)) {
        g_c495.refused = pickup;
        if (note_event_cap(g_c495.refuse_lines, kC495RefuseLines, "card495 refused take")) {
            log::line("card495: host t=%llu refused %s on %s by %s (%s, %s): this player already took it",
                      static_cast<unsigned long long>(GetTickCount64()), weapon ? "PickupWeapon" : "Pickup_Implementation",
                      c495_name(pickup).text,
                      object_name(character).text, object_name(pc).text, kSprintSideText[sprint_side(character)]);
        }
        return true;
    }
    if (e->takers_n < kC43Max) {
        c31_weak_set(e->takers[e->takers_n++], const_cast<void*>(pc));
    }
    g_c495.taker = weapon ? character : nullptr; // card 553: armed by c495_hold when the base call is held
    return false;
}

// Card 553, host: the held take's taker gets the site's weapon now (AddWeapon, equipped), on its own pawn only. The
// release's script chain later removes every player's weapons and gives the same weapon to all, so nothing doubles.
static void c553_arm(const void* pickup, void* character) {
    const ObjectName cls = class_name(pickup);
    const wchar_t* definition = nullptr;
    for (const C495Site& s : kC495Sites) {
        definition = strcmp(cls.text, s.pickup_class) == 0 ? s.definition : definition;
    }
    const void* const def_class = definition != nullptr ? g_find_first_object(nullptr, definition, 0, 0, nullptr) : nullptr;
    void* const def = def_class != nullptr ? read_ptr(def_class, kClassDefaultObject) : nullptr;
    const bool added = def != nullptr && character != nullptr && g_orig_add_weapon != nullptr &&
                       g_orig_add_weapon(character, def, true);
    // Native PickupWeapon (card021 row) may have added the weapon unequipped in the same ms, so AddWeapon returns 0
    // (already owned). Then equip the held copy by its definition class (c551b370a: taker stayed unarmed 2.6 s).
    void* const inventory = !added && def_class != nullptr && character != nullptr
                                ? const_cast<void*>(c24_inventory(character))
                                : nullptr;
    const C50List list = inventory != nullptr ? c50_list(inventory) : C50List{};
    int32_t held = -1;
    for (int32_t i = 0; i < list.count; ++i) {
        const bool match = list.weapon[i] != nullptr && read_ptr(list.weapon[i], kWeaponDefinitionClass) == def_class;
        held = held < 0 && match ? i : held;
    }
    if (held >= 0 && list.equipped != held) {
        void* cls = const_cast<void*>(def_class);
        g_orig_c50_equip_by_class(inventory, &cls);
    }
    const int32_t now = held >= 0 ? c50_list(inventory).equipped : -1;
    const bool ok = added || (held >= 0 && now == held);
    if (ok) {
        c50_host_nudge(); // the taker's weapon list goes out now
    }
    const unsigned long long t = GetTickCount64();
    if (held >= 0) {
        log::line("%scard553: host t=%llu held take of %s armed its taker %s (%s): already held, equipped (index %d) = %d",
                  ok ? "" : "WARN ", t, c495_name(pickup).text, object_name(character).text,
                  kSprintSideText[sprint_side(character)], held, ok ? 1 : 0);
        return;
    }
    log::line("%scard553: host t=%llu held take of %s armed its taker %s (%s): AddWeapon(%s, equip 1) = %d",
              ok ? "" : "WARN ", t, c495_name(pickup).text, object_name(character).text,
              kSprintSideText[sprint_side(character)], object_name(def).text, ok ? 1 : 0);
}

// detour_c254_pickup_implementation, host: true when the base call is held (or skipped after a refused take).
// `by` names the caller for the release row: "take" or "re-check".
static bool c495_hold(void* pickup, const char* by) {
    void* const taker = g_c495.taker;
    g_c495.taker = nullptr;
    if (g_c495.refused == pickup) {
        g_c495.refused = nullptr;
        return true; // the refuse row is logged
    }
    C495Entry* const e = c495_find(pickup); // entries are made only by c495_join, for a per-player pickup
    if (e == nullptr) {
        return false; // not a per-player pickup, or a take the table does not track (WARN logged)
    }
    const C495Cover c = c495_cover(*e, g_actor_get_world(pickup));
    const ULONGLONG now = GetTickCount64();
    if (c.living > 0 && c.taken == c.living && c.taker_pawn != nullptr) {
        if (c495_line()) {
            log::line("card495: host t=%llu released Pickup_Implementation on %s by %s: taken by %d of %d living "
                      "players (%d takers), living taker %s", static_cast<unsigned long long>(now),
                      c495_name(pickup).text, by, c.taken, c.living, e->takers_n, object_name(c.taker_pawn).text);
        }
        c495_drop(e);
        return false;
    }
    if (c495_line()) {
        log::line("card495: host t=%llu held Pickup_Implementation on %s (%s): taken by %d of %d living players (%d "
                  "takers)%s", static_cast<unsigned long long>(now), c495_name(pickup).text, by, c.taken, c.living,
                  e->takers_n, c.living > 0 && c.taken == c.living ? "; no living taker pawn" : "");
    }
    e->held = true;
    if (taker != nullptr) {
        c553_arm(pickup, taker); // card 553: a take only; a re-check has no taker
    }
    return true;
}

// Card 570: the ammo or grenade take that detour_c570_ammo/item runs now. `who` is the character the take credits
// (GetOverlappingCharacter answers it for `pickup` during the call); `base` is set when the subclass reached its base
// call (the game credited the take: a take with full ammo returns before it).
struct C570Take {
    void* pickup;
    void* who;
    bool base;
};
static C570Take g_c570{};
static constexpr int32_t kC570ClientMax = 64; // client: the host pickups the own pawn took this map
struct C570Client {
    C31Weak taken[kC570ClientMax];
    int32_t count;
    uint32_t lines;
};
static C570Client g_c570_client{};
static constexpr uint32_t kC570Lines = 64;

// Host/standalone base call of a placed pickup, unless card 495 holds it. `by` names the caller for the card 495 rows.
static void c254_host_base(void* pickup, const char* by) {
    g_c570.base = g_c570.base || (g_c570.pickup != nullptr && pickup == g_c570.pickup);
    if (c495_hold(pickup, by)) {
        return;
    }
    c245_pickup_row(pickup, "before the base call");
    g_orig_c254_pickup_implementation(pickup);
    c50_host_nudge(); // card 495: a satchel or ammo pickup changes the charge or ammo text
    c245_pickup_row(pickup, "after the base call");
}

// Host, C330 step: a held base whose taker set now covers every living player (a non-taker went down, died or left)
// runs once through the detour, which logs the release row.
void c495_recheck(void* world, ULONGLONG now) {
    if (g_client_session || g_c495.count == 0 || now < g_c495.recheck_ms || world == nullptr) {
        return;
    }
    g_c495.recheck_ms = now + kC495RecheckMs;
    g_c495.refused = nullptr; // a refuse is consumed by the base call in the same take, never by a re-check
    for (int32_t i = g_c495.count - 1; i >= 0; --i) {
        C495Entry& e = g_c495.e[i];
        void* const pickup = c31_weak_get(e.pickup);
        if (pickup == nullptr) {
            c495_drop(&e);
            continue; // the pickup went with its level
        }
        if (!e.held || g_actor_get_world(pickup) != world) {
            continue;
        }
        const C495Cover c = c495_cover(e, world);
        if (c.living <= 0 || c.taken < c.living) {
            continue;
        }
        if (c.taker_pawn == nullptr) {
            if (c495_line()) {
                log::line("card495: host t=%llu held Pickup_Implementation on %s (re-check): no living taker pawn, "
                          "still held", static_cast<unsigned long long>(now), c495_name(pickup).text);
            }
            continue;
        }
        c254_host_base(pickup, "re-check");
    }
}

void c495_map_end() {
    if (g_c495.count > 0) {
        log::line("card495: host map end: %d per-player pickup(s) still tracked", g_c495.count);
    }
    g_c495 = C495Table{};
    g_c570_client = C570Client{};
}
void detour_c254_pickup_implementation(void* pickup) {
    const void* const who = c50_client_pickup_overlapper(pickup);
    g_c570.base = g_c570.base || (who != nullptr && g_c570.pickup != nullptr && pickup == g_c570.pickup);
    if (who == nullptr) {
        c254_host_base(pickup, "take");
        return;
    }
    if (note_event_cap(g_c254_lines, kC254Lines, "card254")) {
        char name[kChanNameMax + 1];
        if (c50_name(pickup, name, sizeof(name)) <= 0) {
            snprintf(name, sizeof(name), "%s", object_name(pickup).text);
        }
        log::line("card254: client skipped Pickup_Implementation on %s (%s), overlapper %s (own pawn %d)", name,
                  class_name(pickup).text, object_name(who).text, sprint_side(who) == kSideClientOwn ? 1 : 0);
    }
}

// Card 570: ammo, grenade, armour, health and tactical support pickups are per player in co-op (user 2026-11-21,
// "Per player", then "all per player"): each living player gets
// the pickup's grant once, and the pickup goes only when every living player took it (card 495's hold and re-check).
// Placed pickups only: a pickup a bug drops stays first come (bounded table). The satchel item pickup stays first come.
C50SelfFn g_orig_c570_ammo_pickup = nullptr; // ASTAmmoPickup::Pickup_Implementation
C50SelfFn g_orig_c570_item_pickup = nullptr; // ASTItemPickup::Pickup_Implementation
C50SelfFn g_orig_c570_armour_pickup = nullptr; // ASTArmourPickup::Pickup_Implementation (user: all per player)
C50SelfFn g_orig_c570_health_pickup = nullptr; // ASTHealthPickup::Pickup_Implementation
C50SelfFn g_orig_c570_tactical_pickup = nullptr; // ASTTacticalSupportPickup::Pickup_Implementation
C50OverlapFn g_orig_c570_overlapping = nullptr; // AADPickupBase::GetOverlappingCharacter (hooked)

void* detour_c570_overlapping(const void* pickup) {
    if (g_c570.who != nullptr && pickup == g_c570.pickup) {
        return g_c570.who;
    }
    return g_orig_c570_overlapping(pickup);
}

static bool c570_item_kind(const void* pickup) {
    return strncmp(class_name(pickup).text, "BP_GrenadeAmmoPickup_", 21) == 0;
}

// Host: the character this take credits. The game's own answer, unless that player already took this pickup and
// another living non-taker overlaps it (both players stand on the pickup).
static void* c570_host_taker(void* pickup, void* first) {
    const C495Entry* const e = c495_find(pickup);
    const void* const first_pc = first != nullptr ? read_ptr(first, kPawnController) : nullptr;
    if (e == nullptr || first_pc == nullptr || !c495_has(*e, first_pc) || g_c227_is_overlapping == nullptr) {
        return first;
    }
    const C43Roster r = c43_roster(g_actor_get_world(pickup));
    for (int32_t s = 0; s < kC43Max; ++s) {
        void* const pawn = r.p[s].pawn;
        if (pawn != nullptr && pawn != first && !c43_is_down(pawn) && !c495_has(*e, r.p[s].pc) &&
            g_c227_is_overlapping(pickup, pawn)) {
            return pawn;
        }
    }
    return first;
}

// Host: undo a join whose take the game did not credit (no base call), so the player can take the pickup later.
static void c570_unjoin(void* pickup, const void* pc, int32_t takers_before) {
    C495Entry* const e = c495_find(pickup);
    if (e == nullptr || e->takers_n != takers_before + 1 || c31_weak_get(e->takers[takers_before]) != pc) {
        return;
    }
    e->takers[--e->takers_n] = C31Weak{};
    if (e->takers_n == 0 && !e->held) {
        c495_drop(e);
    }
}

static void c570_host_take(C50SelfFn orig, void* pickup) {
    void* const first = g_orig_c570_overlapping(pickup);
    if (first == nullptr || g_c570.pickup != nullptr) {
        orig(pickup); // nothing to credit, or a nested take: the game's own path
        return;
    }
    void* const who = c570_host_taker(pickup, first);
    const void* const pc = read_ptr(who, kPawnController);
    const C495Entry* const before = c495_find(pickup);
    const int32_t takers_before = before != nullptr ? before->takers_n : 0;
    g_c495.refused = nullptr;
    g_c495.taker = nullptr;
    if (c495_join(pickup, who, false)) {
        g_c495.refused = nullptr; // refused: no credit and no base call
        return;
    }
    g_c570 = C570Take{pickup, who, false};
    orig(pickup);
    const bool base = g_c570.base;
    g_c570 = C570Take{};
    if (!base) {
        c570_unjoin(pickup, pc, takers_before);
    }
    if (note_event_cap(g_c495.take_lines, kC570Lines, "card570 take")) {
        log::line("card570: host t=%llu take of %s credited %s (%s)%s, base call %d", static_cast<unsigned long long>(
                  GetTickCount64()), c495_name(pickup).text, object_name(who).text, kSprintSideText[sprint_side(who)],
                  who != first ? " instead of the first overlapper" : "", base ? 1 : 0);
    }
}

static int32_t c570_client_find(const void* pickup) {
    for (int32_t i = 0; i < g_c570_client.count && i < kC570ClientMax; ++i) {
        if (c31_weak_get(g_c570_client.taken[i]) == pickup) {
            return i;
        }
    }
    return -1;
}

// Client: the own pawn when it overlaps the host's pickup, else null (another character's overlap runs as before).
static void* c570_client_own(void* pickup) {
    if (c50_client_pickup_overlapper(pickup) == nullptr || g_c227_is_overlapping == nullptr) {
        return nullptr;
    }
    const C43Roster r = c43_roster(g_actor_get_world(pickup));
    for (int32_t s = 0; s < kC43Max; ++s) {
        void* const pawn = r.p[s].pawn;
        if (pawn != nullptr && sprint_side(pawn) == kSideClientOwn && g_c227_is_overlapping(pickup, pawn)) {
            return pawn;
        }
    }
    return nullptr;
}

// Client, own pawn on a host pickup: the local subclass run stays for the first take (card 073 binds the grenade
// input from it, card 384 pairs the local ammo credit with the host frame); a repeat take is skipped whole, as the
// host refuses it.
static void c570_client_take(C50SelfFn orig, void* pickup, void* own) {
    const bool repeat = c570_client_find(pickup) >= 0;
    if (!repeat && g_c570.pickup == nullptr) {
        g_c570 = C570Take{pickup, own, false};
        orig(pickup);
        const bool base = g_c570.base;
        g_c570 = C570Take{};
        if (base && g_c570_client.count < kC570ClientMax) {
            c31_weak_set(g_c570_client.taken[g_c570_client.count++], pickup);
        }
        if (base && g_c570_client.count >= kC570ClientMax) {
            log::line("WARN card570: client taken set full (%d): later repeat takes are not skipped", kC570ClientMax);
        }
        return;
    }
    if (repeat && note_event_cap(g_c570_client.lines, kC570Lines, "card570 client repeat")) {
        log::line("card570: client t=%llu skipped repeat take of %s by own pawn %s", static_cast<unsigned long long>(
                  GetTickCount64()), c495_name(pickup).text, object_name(own).text);
    }
    if (!repeat) {
        orig(pickup); // nested take: the game's own path
    }
}

static void c570_take(C50SelfFn orig, void* pickup) {
    void* const own = g_client_session ? c570_client_own(pickup) : nullptr;
    if (own != nullptr) {
        c570_client_take(orig, pickup, own);
        return;
    }
    if (g_client_session || !c50_placed(pickup)) {
        orig(pickup);
        return;
    }
    c570_host_take(orig, pickup);
}

void detour_c570_ammo_pickup(void* pickup) {
    c570_take(g_orig_c570_ammo_pickup, pickup);
}

void detour_c570_armour_pickup(void* pickup) {
    c570_take(g_orig_c570_armour_pickup, pickup);
}

void detour_c570_health_pickup(void* pickup) {
    c570_take(g_orig_c570_health_pickup, pickup);
}

void detour_c570_tactical_pickup(void* pickup) {
    c570_take(g_orig_c570_tactical_pickup, pickup);
}

void detour_c570_item_pickup(void* pickup) {
    if (!c570_item_kind(pickup)) {
        g_orig_c570_item_pickup(pickup);
        return;
    }
    c570_take(g_orig_c570_item_pickup, pickup);
}

// The two scene components OnUpdatePickupStatus sets from IsPickupEnabled: the mesh +0x2F0 (SetVisibility at
// 0x04C618AF) and +0x330 (SetVisibility at 0x04C618DA, then its vtable +0x410 with the same value).
static constexpr size_t kC145PickupMesh = 0x2F0;
static constexpr size_t kC145PickupFx = 0x330;

// bVisible of the scene component at `offset` of `pickup`; 2 when the pointer is null.
static uint32_t c145_component_visible(const void* pickup, size_t offset) {
    const auto* const c = static_cast<const uint8_t*>(read_ptr(pickup, offset));
    return c == nullptr ? 2u : ((c[kSceneVisibleByte] & kSceneVisibleBit) != 0 ? 1u : 0u);
}

// Card 145: every bEnabled change (DisablePickup, SetPickupEnabled, every override's call of the base).
// The state is sent at entry as well as after the original. DisablePickup and SetPickupEnabled write the byte before
// they call this, and the original's SetActorEnableCollision can run a nested take synchronously: a respawn under a
// standing player enables, overlaps, and disables again inside one call (c157a, BP_AmmoPickup_MoritaSmall_C_0 at
// 897601). Checked only after the original, the 0 -> 1 -> 0 matched the journal's 0 and no 'E' row went, while the
// client's own respawn left it at 1. At entry the outer call sends 'E 1' and the nested call 'E 0'.
//
// The original reads IsPickupEnabled once, at entry, and sets the mesh and the effect component from that value after
// the collision call (0x04C61880 then SetVisibility 0x03AF7060 at +0x3F). A nested take inside the collision call hides
// them, and the outer call then shows them again with bEnabled 0: on the host a taken pickup stays visible while the
// client hid it (c168-r3 round 2, BP_AmmoPickup_MoritaSmall_C_0 "mesh visible host 1 client 0"). When bEnabled changed
// during the call, the original runs once more, so the visuals follow the state the nested call left.
// Card 221: client. The client's own respawn timer (armed by an earlier 'E 0') enables a pickup while the host's
// respawn was taken at once by a standing player; that local enable is set back to 0 before the original, which then
// hides the pickup. Only a pickup whose last host row was 0 is set back (c221_host_disabled, world_c98.cpp): the
// Blueprint side enables every placed pickup at map load through this call, and the host sends no row for a pickup
// whose state never changed, so a set back without that row hid every pickup on the client for the whole map
// (c224-c1m1-b4, 750 card 132 mismatches).
static uint32_t g_c221_set_back = 0; // client, game thread
static constexpr uint32_t kC221Lines = 100;
static uint32_t g_c221_lines = 0;

static void c221_client_set_back(void* pickup) {
    uint32_t host_id = 0;
    const bool hosts = c50_placed(pickup) || c104_copy_host_id(pickup, host_id);
    if (g_c221_host_enable || !hosts || c145_enabled(pickup) == 0 || !c221_host_disabled(pickup)) {
        return;
    }
    static_cast<uint8_t*>(pickup)[kPickupEnabledByte] = 0;
    ++g_c221_set_back;
    if (note_event_cap(g_c221_lines, kC221Lines, "card221")) {
        char name[kChanNameMax + 1];
        if (c50_name(pickup, name, sizeof(name)) <= 0) {
            snprintf(name, sizeof(name), "%s", object_name(pickup).text);
        }
        log::line("card221: client pickup %s local enable set back (only the host enables; %u so far)", name,
                  g_c221_set_back);
    }
}

void detour_c145_update_status(void* pickup) {
    if (!g_client_session) {
        c145_host_status(pickup);
    } else if (pickup != nullptr) {
        c221_client_set_back(pickup);
    }
    const uint32_t entry = pickup != nullptr ? c145_enabled(pickup) : 0u;
    g_orig_c145_update_status(pickup);
    if (pickup != nullptr && c145_enabled(pickup) != entry) {
        const uint32_t mesh_was = c145_component_visible(pickup, kC145PickupMesh);
        const uint32_t fx_was = c145_component_visible(pickup, kC145PickupFx);
        g_orig_c145_update_status(pickup);
        const void* const fx = read_ptr(pickup, kC145PickupFx);
        log::line("card145: %s t=%llu pickup %s: bEnabled %u -> %u inside OnUpdatePickupStatus (nested take); status "
                  "run again for the visuals: mesh visible %u -> %u, %s visible %u -> %u",
                  side(), static_cast<unsigned long long>(GetTickCount64()), object_name(pickup).text, entry,
                  c145_enabled(pickup), mesh_was, c145_component_visible(pickup, kC145PickupMesh),
                  fx != nullptr ? class_name(fx).text : "(no component)", fx_was,
                  c145_component_visible(pickup, kC145PickupFx));
    }
    if (!g_client_session) {
        c145_host_status(pickup);
    }
}

// Card 457 (diagnostic, host): the GEngine chain at each checkpoint save, read before the game's own save runs. A null
// link prints as null and ends the chain; the save always goes on.
constexpr size_t kC457GEngineRva = 0x93FF208;
void c457_save_row() {
    const void* const engine =
        g_exe_base != nullptr ? *reinterpret_cast<void* const*>(g_exe_base + kC457GEngineRva) : nullptr;
    const void* const a = engine != nullptr ? read_ptr(engine, 0xC10) : nullptr;
    const void* const b = a != nullptr ? read_ptr(a, 0xF8) : nullptr;
    const void* const c = b != nullptr ? read_ptr(b, 0x90) : nullptr;
    log::line("card457: host save: engine %p, +0xC10 %p, +0xF8 %p, +0x90 %p", engine, a, b, c);
}

void detour_c50_save_checkpoint(void* context, void* data, bool flag) {
    if (!g_client_session) {
        c457_save_row();
    }
    if (c50_audit_line()) {
        log::line("card050: %s USTSaveSystemLibrary::Save_CheckPoint(context %s (%s), flag %d), caller exe+0x%llX",
                  side(), object_name(context).text, class_name(context).text, flag ? 1 : 0,
                  exe_rva(_ReturnAddress())); // card 147: exe+0x521F166 is ASTCheckPointSaveZone::TrySave
    }
    g_orig_c50_save_checkpoint(context, data, flag);
}

void c50_inventory_line(const char* fn, const void* inventory, const void* cls, int32_t index) {
    if (!c50_audit_line()) {
        return;
    }
    char where[160];
    char chain[96];
    c21_inventory_text(inventory, where, sizeof(where));
    caller_chain(chain, sizeof(chain));
    log::line("card050: %s UADWeaponInventory::%s(%s, index %d) by %s on %s; callers %s", side(), fn,
              object_name(cls).text, index, g_c50.applying ? "card 050 apply" : "game", where, chain);
}

void detour_c50_remove_by_class(void* inventory, void* const* cls) {
    c50_inventory_line("RemoveWeaponByClass", inventory, cls != nullptr ? *cls : nullptr, -1);
    g_orig_c50_remove_by_class(inventory, cls);
}

void detour_c50_equip_by_class(void* inventory, void* const* cls) {
    c50_inventory_line("EquipWeaponByClass", inventory, cls != nullptr ? *cls : nullptr, -1);
    g_orig_c50_equip_by_class(inventory, cls);
}

void detour_c50_equip_by_index(void* inventory, int32_t index) {
    c50_inventory_line("EquipWeaponByIndex", inventory, nullptr, index);
    g_orig_c50_equip_by_index(inventory, index);
}

void detour_c50_unequip(void* inventory) {
    c50_inventory_line("UnequipCurrentWeapon", inventory, nullptr, -1);
    g_orig_c50_unequip(inventory);
}

// --- Card 057 audit detours (log only): the weapon switch on each pawn ---
constexpr uint32_t kC57Lines = 160;

C57EquipFn g_orig_c57_equip_weapon = nullptr;
C57ActivateFn g_orig_c57_activate = nullptr;
C57CanActivateFn g_orig_c57_can_activate = nullptr;
C50SelfFn g_orig_c57_begin_switch = nullptr;
C57RequestFn g_orig_c57_request = nullptr;
C57CycleFn g_orig_c57_cycle = nullptr;
uint32_t g_c57_lines = 0;               // game thread
const void* g_c57_last_avatar = nullptr; // the pawn of the last switch ActivateAbility (log text only)

bool c57_line() {
    return note_event_cap(g_c57_lines, kC57Lines, "card057 audit");
}

// The switch ability's CDO NetExecutionPolicy (engine default LocalPredicted). A LocalPredicted switch on the client
// sends ServerTryActivateAbilityWithEventData, whose STGameplayAbilityTargetData_WeaponSwitch has no NetSerialize: a
// LowLevelFatalError (card 057 run x). On the host the server copy is not locally controlled, so LocalPredicted would
// bounce the switch to the client. So: the client runs it LocalOnly, the host ServerOnly; the Equip frame keeps the
// two in step. Set before the first activation (InternalTryActivateAbility reads Spec->Ability, the CDO).
constexpr uint8_t kC57PolicyLocalOnly = 1;
constexpr uint8_t kC57PolicyServerOnly = 3;
constexpr wchar_t kC57SwitchCdo[] = L"/Game/GAS/Weapons/GA_WeaponSwitchAbility.Default__GA_WeaponSwitchAbility_C";
void* g_c57_cdo = nullptr; // game thread; the CDO lives for the whole process, so it is looked up once

// Card 208: a bug pawn's switch key runs GA_BugForm_SwitchAbility, which sends WeaponSystem.EquipWeapon to
// GA_BugForm_EquipAbility (EquipWeaponByInstance). Neither Blueprint sets NetExecutionPolicy, so both keep the native
// default and both can take the card057x predicted-RPC path; they get the same policy as the MI switch ability. They
// live in /Game/GAS/BugForms, which is only loaded with a bug pawn, so they are looked up on every call (not kept
// across a map that may unload them) and a miss is not a WARN.
constexpr const wchar_t* kC208PolicyCdos[] = {
    L"/Game/GAS/BugForms/GA_BugForm_SwitchAbility.Default__GA_BugForm_SwitchAbility_C",
    L"/Game/GAS/BugForms/GA_BugForm_EquipAbility.Default__GA_BugForm_EquipAbility_C",
};
constexpr const char* kC208PolicyNames[] = {"GA_BugForm_SwitchAbility_C", "GA_BugForm_EquipAbility_C"};
static_assert(sizeof(kC208PolicyCdos) / sizeof(kC208PolicyCdos[0]) ==
              sizeof(kC208PolicyNames) / sizeof(kC208PolicyNames[0]));

// Card 208: the bug form abilities' CDO policies, when their package is loaded.
void c208_set_policy(uint8_t want) {
    for (size_t k = 0; k < sizeof(kC208PolicyCdos) / sizeof(kC208PolicyCdos[0]) && g_static_find_object != nullptr;
         ++k) {
        void* const cdo = g_static_find_object(nullptr, nullptr, kC208PolicyCdos[k], false);
        uint8_t* const policy = cdo != nullptr ? static_cast<uint8_t*>(cdo) + kC57NetExecutionPolicy : nullptr;
        if (policy != nullptr && *policy != want) {
            log::line("card057: %s %s NetExecutionPolicy %u -> %u", side(), kC208PolicyNames[k], *policy, want);
            *policy = want;
        }
    }
}

// Sets the policy on every call: a process can host and later join (or the reverse) through the co-op panel, and
// a policy left from the other role brings back the card057x crash or a server copy that never switches.
void c57_set_policy(uint8_t want) {
    c208_set_policy(want);
    if (g_c57_cdo == nullptr && g_static_find_object != nullptr) {
        g_c57_cdo = g_static_find_object(nullptr, nullptr, kC57SwitchCdo, false);
    }
    if (g_c57_cdo == nullptr) {
        log::line("WARN card057: %s switch ability CDO %ls not found: policy left as is", side(), kC57SwitchCdo);
        return;
    }
    uint8_t* const policy = static_cast<uint8_t*>(g_c57_cdo) + kC57NetExecutionPolicy;
    if (*policy != want) {
        log::line("card057: %s switch ability CDO NetExecutionPolicy %u -> %u", side(), *policy, want);
        *policy = want;
    }
}

// Client: the client's switch ability does not activate on its own pawn and never reaches the server copy (card
// 057 audit), so the slot its switch key asked for goes to the host as an Equip frame. Card 312: the frame is text,
// "<client slot>" then " <definition class path>" of the weapon (MI) or form (bug) in that slot, so the host resolves
// the slot on its copy by class; an empty slot or the Unarmed weapon sends no path.
// Card 439 (client): the last round of a weapon that drops when empty runs CheckToAutoSwitch inside Fire, at the
// ammo commit, so the Equip left before the Round of the same Fire (c423b138a: Equip sent 501086430, Round
// 501086431, both applied by the host in one tick, the Equip first: it cancelled the copy's fire ability and the
// owed Fire could not be fired, `client rounds 1, copy rounds 0, refused 1, unused 1`). An Equip sent inside the
// own Fire is held and sent right after that Fire's Round. The pawn pointer lives only across that one call.
struct C439Held {
    bool in_fire;
    const void* pawn; // null: no Equip held
    int32_t index;
    const void* sw_pawn; // null: no Switch held (the underslung toggle, EquipWeapon of the equipped slot)
    int32_t sw_a;
    int32_t sw_b;
    const void* sw_actor;
};
static C439Held g_c439{}; // game thread only
constexpr uint32_t kC439HeldLines = 32;

void c57_send_equip(const void* pawn, int32_t index);
void c141_send_switch(const void* pawn, int32_t a, int32_t b, const void* actor);

void c439_fire_begin() {
    g_c439 = C439Held{};
    g_c439.in_fire = true;
}

void c439_fire_end() {
    const C439Held h = g_c439;
    g_c439 = C439Held{};
    static uint32_t lines = 0;
    if (h.pawn != nullptr) {
        if (note_event_cap(lines, kC439HeldLines, "card439 held equip")) {
            log::line("card439: client t=%llu Equip of slot %d was sent inside the own Fire: held, sent after its "
                      "Round",
                      static_cast<unsigned long long>(GetTickCount64()), h.index);
        }
        c57_send_equip(h.pawn, h.index);
    }
    if (h.sw_pawn != nullptr) {
        if (note_event_cap(lines, kC439HeldLines, "card439 held switch")) {
            log::line("card439: client t=%llu Switch (%d, %d) was sent inside the own Fire: held, sent after its "
                      "Round",
                      static_cast<unsigned long long>(GetTickCount64()), h.sw_a, h.sw_b);
        }
        c141_send_switch(h.sw_pawn, h.sw_a, h.sw_b, h.sw_actor);
    }
}

void c57_send_equip(const void* pawn, int32_t index) {
    if (g_c439.in_fire) { // card 439: goes after this Fire's Round (c439_fire_end)
        g_c439.pawn = pawn;
        g_c439.index = index;
        return;
    }
    void* const pc = read_ptr(pawn, kPawnController);
    void* const inventory = const_cast<void*>(c24_inventory(pawn));
    void* const entry = inventory != nullptr ? c50_at_slot(inventory, index) : nullptr;
    const void* const weapon = c50_weapon(entry);
    const void* const def = weapon != nullptr ? read_ptr(weapon, kWeaponDefinitionClass) : nullptr;
    char path[kC50PathMax + 1] = {};
    // -1 = an entry that is not a weapon, or a weapon whose class path does not resolve: not sent (no slot fallback
    // for an occupied slot). An empty slot or the Unarmed weapon sends no path: the host uses the slot itself.
    const int32_t resolved = entry == nullptr ? 0 : def != nullptr ? c50_class_path(def, path, sizeof(path)) : -1;
    const bool unarmed = resolved > 0 && strstr(path, "Unarmed") != nullptr;
    const int32_t p = unarmed ? 0 : resolved;
    char text[kC50PathMax + 16];
    const int n = index >= 0 && p >= 0 ? snprintf(text, sizeof(text), p > 0 ? "%d %s" : "%d", index, path) : -1;
    wchar_t wire[kChanTextFrame];
    const WireString w = n > 0 && pc != nullptr ? chan_text_frame(ChanOp::Equip, text, wire) : WireString{};
    const bool sent = w.num > 0 && chan_send_host(pc, w);
    g_c50.switch_due_ms = GetTickCount64() + kC50SwitchHoldMs; // card 312: card 050 slot moves wait
    log::line("%scard057: client t=%llu switch to weapon %d (%s): Equip %s", sent ? "" : "WARN ",
              static_cast<unsigned long long>(GetTickCount64()), index,
              unarmed ? "Unarmed, no path" : p > 0 ? path : p == 0 ? "empty slot" : "class path did not resolve",
              sent ? "sent" : "NOT sent");
}

void* c208_inventory(const void* pawn);
void c210_copy_equip(void* pc, void* pawn, uint32_t index);

// Card 312: the copy slot an Equip frame names. With a class path, the copy's slot of the weapon of that class (-1
// when the copy has none: no equip); with no path (an empty slot or Unarmed on the client), the client slot itself.
int32_t c57_copy_slot(void* inventory, int32_t client_slot, const char* path) {
    if (path[0] == '\0') {
        return client_slot;
    }
    const auto* const items = inventory != nullptr ? static_cast<const uint8_t*>(read_ptr(inventory, kInventoryItems))
                                                   : nullptr;
    const int32_t num = items != nullptr ? read_i32(inventory, kInventoryItems + kArrayNum) : 0;
    for (int32_t i = 0; i < num && i < kC50Items; ++i) {
        const void* const w = c50_weapon(read_ptr(items, static_cast<size_t>(i) * kC50ItemStride));
        char have[kC50PathMax + 1];
        if (w != nullptr && c50_class_path(read_ptr(w, kWeaponDefinitionClass), have, sizeof(have)) > 0 &&
            strcmp(have, path) == 0) {
            return i;
        }
    }
    return -1;
}

// Host: an Equip frame from the client that owns pc; the switch runs on that PC's server copy, on the copy's slot of
// the client's weapon class (card 312).
void c57_receive_equip(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    const int32_t len = chan_text(s, text, sizeof(text));
    char* end = nullptr;
    const long client_slot = len > 0 ? strtol(text, &end, 10) : -1;
    const bool parsed = end != nullptr && end != text && client_slot >= 0 && client_slot < kC50Items &&
                        (*end == '\0' || *end == ' ');
    const char* const path = parsed && *end == ' ' ? end + 1 : "";
    void* const pawn = read_ptr(pc, kControllerPawn);
    // Card 208: an MI or a bug server copy (c196_inventory_at names the pawn's weapon or forms inventory).
    const bool copy = pawn != nullptr && c196_inventory_at(pawn) != 0 && sprint_side(pawn) == kSideServerCopy;
    void* const inventory = copy ? const_cast<void*>(c24_inventory(pawn)) : nullptr;
    const int32_t slot = parsed ? c57_copy_slot(inventory, static_cast<int32_t>(client_slot), path) : -1;
    const bool equip = copy && slot >= 0;
    if (equip) {
        c439_before_equip(pawn); // card 439: a reported round the copy has not fired yet goes first
    }
    log::line("%scard057: host t=%llu Equip from %s: client slot %ld, class %s, copy slot %d, used %s: %s",
              equip ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), object_name(pc).text,
              client_slot, path[0] != '\0' ? path : "- (empty slot or Unarmed)", slot,
              path[0] != '\0' ? "the copy slot of the class" : "the client slot",
              !parsed ? "dropped (malformed frame)" : !copy ? "dropped (no server copy pawn)"
              : slot < 0 ? "dropped (the copy has no weapon of that class)"
              : c208_inventory(pawn) != nullptr ? "card 210 EquipWeapon on the bug server copy"
                                                : "EquipWeapon on the server copy");
    if (!equip) {
        return;
    }
    // Card 210: a bug copy's switch ability needs BugForms.Can.Hopper/Tanker, owner-local loose tags the copy lacks
    // (card 208 Trace 2); the copy holds the owner's tag for the duration of the EquipWeapon call.
    if (c208_inventory(pawn) != nullptr) {
        c210_copy_equip(pc, pawn, static_cast<uint32_t>(slot));
        return;
    }
    c57_set_policy(kC57PolicyServerOnly);
    g_equip_weapon(pawn, slot);
}

// --- Card 208: a bug form switch reaches the other machine (host -> client) ---
// Bug forms do not replicate (card 132: STBugForm mechanism=none), so each machine's FormsInventory keeps its own
// equipped form. The host watches each PC's bug pawn (its own and the server copies) and sends a changed equipped
// index as BugForm (card 209: v0, v1 = the pawn's NetGUID; v2 = the index into c50_list) to every remote PC. The
// client resolves the GUID to any player pawn (its own, the host's or another client's copy) and equips that form
// through the inventory's own EquipWeaponByClass (OnEquip runs: abilities, mesh/anim set), under g_c50.applying so
// nothing is sent back; a GUID that does not resolve yet is held (c209_hold). A pawn whose GUID cannot be sent is
// skipped and counted. Rows hold object ids, never pointers.
constexpr int32_t kC208Rows = kC17Slots + 1; // host: one pawn per PC; client: its own pawn and every other one
constexpr uint32_t kC208Lines = 80;          // per run
constexpr uint32_t kC208Warns = 8;           // per run
constexpr int32_t kC208Unseen = -2;
constexpr ULONGLONG kC208StepMs = 200;
constexpr size_t kC208AscOwnedTags = 0x1020; // attributes.cpp kAscOwnedTags: the owned tags' ExplicitTags TArray
constexpr int32_t kC208MaxTags = 64;
constexpr char kC208TagPrefix[] = "BugForms.";
// Card 210: the server copy's form switch through the game's own path, and the form look diag.
constexpr int32_t kC210NoWant = -1;
// The Equip's switch is latent (USTWeaponSwitchAbility::ActivateAbility: WaitDelay for CancelGracePeriod, then
// SwitchOut waits for the unequip tag); c208-r2 host: key Two at 121452579 ms, equipped seen at 121453063 ms (484 ms).
constexpr ULONGLONG kC210EquipLateMs = 1500;
constexpr ULONGLONG kC210LookAgainMs = 1000;
constexpr uint32_t kC210Lines = 60; // per run
constexpr uint32_t kC210Warns = 8;  // per run
constexpr size_t kC210CharacterMesh = 0x328;   // ACharacter::Mesh (hit_c132.cpp kCharacterMesh)
constexpr size_t kC210FirstPersonMesh = 0x848; // ASTPlayerCharacter::FirstPersonMesh (remote_body.cpp)

struct C208Row {
    int32_t pawn_id;
    int32_t equipped; // host: last sent; client: last seen
    int32_t want;     // card 210, host: the index of an Equip on this server copy, kC210NoWant when none is open
    ULONGLONG want_by; // card 210: the first c208_track step at or after this time checks `want`
    ULONGLONG look_at; // card 210: the "+1 s" form look line is due at this time (0: none due)
};
struct C208State {
    C208Row row[kC208Rows];
    int32_t rows;
    int32_t peak;
    ULONGLONG next_ms;
    uint32_t lines, warns, skipped;
};
C208State g_c208{}; // game thread

bool c208_line() {
    return note_event_cap(g_c208.lines, kC208Lines, "card208");
}

bool c208_warn() {
    return note_event_cap(g_c208.warns, kC208Warns, "card208 WARN");
}

// The bug pawn's FormsInventory, or nullptr when `pawn` is not a bug pawn.
void* c208_inventory(const void* pawn) {
    const bool bug = pawn != nullptr && c196_inventory_at(pawn) == kBugFormsInventory;
    return bug ? const_cast<void*>(c24_inventory(pawn)) : nullptr;
}

// Appends `text` to out (',' between items), clamped to cap.
void c208_append(char* out, size_t cap, size_t& used, const char* text) {
    const int n = snprintf(out + used, cap - used, "%s%s", used != 0 ? "," : "", text);
    used += n > 0 ? static_cast<size_t>(n) : 0;
    used = used < cap ? used : cap - 1;
}

// The BugForms.* tags among the pawn ASC's owned tags, or "none".
void c208_tags(const void* pawn, char* out, size_t cap) {
    const void* const asc = g_get_asc != nullptr ? g_get_asc(pawn, true) : nullptr;
    const int32_t num = asc != nullptr ? read_i32(asc, kC208AscOwnedTags + sizeof(void*)) : 0;
    const int32_t max = asc != nullptr ? read_i32(asc, kC208AscOwnedTags + sizeof(void*) + sizeof(int32_t)) : 0;
    const auto* const data = asc != nullptr ? static_cast<const uint64_t*>(read_ptr(asc, kC208AscOwnedTags)) : nullptr;
    const bool bad = num < 0 || num > max;
    snprintf(out, cap, "%s", asc == nullptr ? "(no ASC)" : (bad ? "(bad TArray)" : "none"));
    const int32_t n = data == nullptr || bad ? 0 : (num < kC208MaxTags ? num : kC208MaxTags);
    size_t used = 0;
    for (int32_t i = 0; i < n; ++i) {
        const ObjectName tag = fname_text(&data[i]);
        if (strncmp(tag.text, kC208TagPrefix, sizeof(kC208TagPrefix) - 1) == 0) {
            c208_append(out, cap, used, tag.text);
        }
    }
}

// "<slot>=<definition class>" per form of the list.
void c208_forms(const C50List& list, char* out, size_t cap) {
    snprintf(out, cap, "%s", "none");
    size_t used = 0;
    for (int32_t k = 0; k < list.count; ++k) {
        char item[96];
        snprintf(item, sizeof(item), "%d=%s", k, object_name(read_ptr(list.weapon[k], kWeaponDefinitionClass)).text);
        c208_append(out, cap, used, item);
    }
}

C208Row* c208_row(int32_t pawn_id) {
    for (int32_t i = 0; i < g_c208.rows; ++i) {
        if (g_c208.row[i].pawn_id == pawn_id) {
            return &g_c208.row[i];
        }
    }
    if (g_c208.rows >= kC208Rows) {
        if (c208_warn()) {
            log::line("WARN card208: %s bug form table full (%d rows): pawn id %d not tracked", side(), kC208Rows,
                      pawn_id);
        }
        return nullptr;
    }
    C208Row* const row = &g_c208.row[g_c208.rows++];
    *row = C208Row{pawn_id, kC208Unseen, kC210NoWant, 0, 0};
    g_c208.peak = g_c208.rows > g_c208.peak ? g_c208.rows : g_c208.peak;
    return row;
}

// The first tick a bug pawn's FormsInventory holds forms: its tags, forms and equipped index.
void c208_pawn_line(const void* pawn, const C50List& list) {
    char tags[256];
    char forms[320];
    c208_tags(pawn, tags, sizeof(tags));
    c208_forms(list, forms, sizeof(forms));
    if (c208_line()) {
        log::line("card208: %s bug forms of %s (%s): tags %s, forms %s, equipped %d", side(), object_name(pawn).text,
                  kSprintSideText[sprint_side(pawn)], tags, forms, list.equipped);
    }
}

// Host: one BugForm row for `pawn` to each remote PC that can address it (only_pc: that PC alone, or every one).
void c208_send(void* world, const void* pawn, int32_t index, const void* only_pc, const char* why) {
    if (pawn == nullptr || world == nullptr) {
        return; // card 208 c208-r1: a slot with no PC reached here with a null pawn
    }
    const uint64_t guid = c41_guid(pawn);
    uint32_t lo = 0;
    uint32_t hi = 0;
    if (!c41_guid_args(guid, lo, hi)) {
        ++g_c208.skipped;
        if (c208_warn()) {
            log::line("WARN card209: host BugForm %d of %s (%s) %s not sent: guid 0x%llx not sendable", index,
                      object_name(pawn).text, kSprintSideText[sprint_side(pawn)], why,
                      static_cast<unsigned long long>(guid));
        }
        return;
    }
    const ChanArgs args{{lo, hi, static_cast<uint32_t>(index)}};
    uint32_t sent = 0;
    for (int32_t s = 0; s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        if (pc == nullptr || !c26_is_remote_pc(pc) || (only_pc != nullptr && pc != only_pc)) {
            continue;
        }
        sent += chan_to_client_args(pc, ChanOp::BugForm, args) ? 1u : 0u;
    }
    if (c208_line()) {
        log::line("card208: host t=%llu form %d of %s (%s, guid 0x%llx) %s: BugForm sent to %u client(s)",
                  static_cast<unsigned long long>(GetTickCount64()), index, object_name(pawn).text,
                  kSprintSideText[sprint_side(pawn)], static_cast<unsigned long long>(guid), why, sent);
    }
}

void c210_track(const void* pawn, C208Row& row, int32_t equipped, ULONGLONG now);
void c210_form_look(const void* pawn, int32_t equipped, const char* when);

// Both sides: one bug pawn's row; on the host a changed equipped index goes out.
void c208_track(void* world, const void* pawn) {
    void* const inventory = c208_inventory(pawn);
    const C50List list = inventory != nullptr ? c50_list(inventory) : C50List{};
    C208Row* const row = inventory != nullptr && list.count > 0 ? c208_row(object_index(pawn)) : nullptr;
    if (row == nullptr) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    c210_track(pawn, *row, list.equipped, now);
    if (row->equipped == list.equipped) {
        return;
    }
    if (row->equipped == kC208Unseen) {
        c208_pawn_line(pawn, list);
    }
    row->equipped = list.equipped;
    row->look_at = now + kC210LookAgainMs;
    c210_form_look(pawn, list.equipped, "on change");
    if (!g_client_session && list.equipped >= 0) {
        c208_send(world, pawn, list.equipped, nullptr, "equipped");
    }
}

// Both sides, from c50_step: the host's PC pawns, or the client's own pawn and every other player pawn (card 209).
void c208_step(void* world, void* pc, ULONGLONG now) {
    if (world == nullptr || now < g_c208.next_ms) {
        return;
    }
    g_c208.next_ms = now + kC208StepMs;
    if (g_client_session) {
        c208_track(world, pc != nullptr ? c23_own_pawn(pc) : nullptr);
        void* others[kC43Max];
        const int32_t n = c43_client_other_pawns(world, others);
        for (int32_t i = 0; i < n; ++i) {
            c208_track(world, others[i]);
        }
        return;
    }
    for (int32_t s = 0; s < kC17Slots; ++s) {
        const void* const p = g_get_player_controller(world, s);
        c208_track(world, p != nullptr ? read_ptr(p, kControllerPawn) : nullptr);
    }
}

// Host, the registry's "bugform" join replay (sync_join_replay): the current form of every bug pawn this PC can
// address.
void c208_join_replay(void* pc) {
    void* const world = g_actor_get_world(pc);
    for (int32_t s = 0; s < kC17Slots && world != nullptr; ++s) {
        const void* const p = g_get_player_controller(world, s);
        const void* const pawn = p != nullptr ? read_ptr(p, kControllerPawn) : nullptr;
        void* const inventory = c208_inventory(pawn);
        const C50List list = inventory != nullptr ? c50_list(inventory) : C50List{};
        if (inventory != nullptr && list.count > 0 && list.equipped >= 0) { // C50List{} has equipped 0
            c208_send(world, pawn, list.equipped, pc, "join replay");
        }
    }
}

// Client: one BugForm row (card 209: v0, v1 = the pawn's NetGUID, v2 = the form index). An unresolved GUID is held.
void c208_receive(void* pc, const ChanArgs& args) {
    const uint64_t guid = c41_args_guid(args.v[0], args.v[1]);
    const void* const pawn = c41_resolve_player(pc, guid);
    if (pawn == nullptr && guid != 0) {
        c209_hold(C209Op::BugForm, guid, args.v[2]);
        return;
    }
    c208_apply(pc, pawn, guid, static_cast<int32_t>(args.v[2]));
}

// Client: equips form `index` on `pawn` (the pawn a BugForm GUID resolved to) unless it is equipped already.
void c208_apply(void* pc, const void* pawn, uint64_t guid, int32_t index) {
    void* const inventory = c208_inventory(pawn);
    const C50List list = inventory != nullptr ? c50_list(inventory) : C50List{};
    const char* const who = pawn != nullptr && pawn == c23_own_pawn(pc) ? "own pawn" : "other pawn";
    const unsigned long long t = GetTickCount64();
    const bool in_range = inventory != nullptr && index >= 0 && index < list.count; // index is signed, from the wire
    void* definition = in_range && list.weapon[index] != nullptr
                           ? read_ptr(list.weapon[index], kWeaponDefinitionClass)
                           : nullptr;
    if (definition == nullptr) {
        if (c208_warn()) {
            log::line("WARN card208: client t=%llu BugForm %d for the %s (guid 0x%llx) dropped: %s (%d forms)", t,
                      index, who, static_cast<unsigned long long>(guid),
                      inventory == nullptr ? "no bug pawn" : (in_range ? "null form" : "index out of range"),
                      list.count);
        }
        return;
    }
    if (index == list.equipped) {
        if (c208_line()) {
            log::line("card208: client t=%llu BugForm %d for the %s %s: already equipped", t, index, who,
                      object_name(pawn).text);
        }
        return;
    }
    g_c50.applying = true;
    g_orig_c50_equip_by_class(inventory, &definition);
    g_c50.applying = false;
    const int32_t now_equipped = c50_list(inventory).equipped;
    if (c208_line()) {
        log::line("card208: client t=%llu BugForm %d for the %s %s: EquipWeaponByClass(%s), equipped %d -> %d", t,
                  index, who, object_name(pawn).text, object_name(definition).text, list.equipped, now_equipped);
    }
}

// Card 210: the BugForms.Can.* loose tag GA_BugForm_SwitchAbility's CanSwapToWeapon tests per form index (index 0,
// the Warrior, needs none). The owner's client sends Equip only after its own CanSwapToWeapon passed, so the host
// lends the copy that tag for the EquipWeapon call: the test (ShouldAbilityRespondToEvent, the only CanSwapToWeapon
// caller) runs inside the call (HandleGameplayEvent -> InternalTryActivateAbility, card 210 "Check A").
constexpr const wchar_t* kC210CanTag[] = {nullptr, L"BugForms.Can.Hopper", L"BugForms.Can.Tanker"};
constexpr uint32_t kC210CanTags = sizeof(kC210CanTag) / sizeof(kC210CanTag[0]);
constexpr const wchar_t kC210Invulnerable[] = L"HealthAndDamageSystem.Invulnerable"; // the equip ability's window
enum C210Tag : uint8_t { kC210TagNone, kC210TagHeld, kC210TagAdded, kC210TagFailed };
constexpr const char* kC210TagText[] = {"none needed", "already held", "added and removed", "NOT added (no ASC)"};
struct C210State {
    uint64_t can_tag[kC210CanTags]; // FName cache per kC210CanTag row
    uint64_t invulnerable;          // FName cache of kC210Invulnerable
    uint32_t lines, warns;          // per run (kept across maps)
};
C210State g_c210{}; // game thread

bool c210_line() {
    return note_event_cap(g_c210.lines, kC210Lines, "card210");
}

bool c210_warn() {
    return note_event_cap(g_c210.warns, kC210Warns, "card210 WARN");
}

// True when the ASC's owned tags (ExplicitTags) hold `tag`.
bool c210_has_tag(const void* asc, uint64_t tag) {
    const int32_t num = read_i32(asc, kC208AscOwnedTags + sizeof(void*));
    const int32_t max = read_i32(asc, kC208AscOwnedTags + sizeof(void*) + sizeof(int32_t));
    const auto* const data = static_cast<const uint64_t*>(read_ptr(asc, kC208AscOwnedTags));
    const int32_t n = data == nullptr || num < 0 || num > max ? 0 : (num < kC208MaxTags ? num : kC208MaxTags);
    for (int32_t i = 0; i < n; ++i) {
        if (data[i] == tag) {
            return true;
        }
    }
    return false;
}

// One loose tag count change on the ASC (delta +1 or -1), as UAbilitySystemComponent::UpdateTagMap_Internal does it:
// the count container, then the tag-changed notify when the tag's existence changed. True when it changed.
bool c210_tag_update(void* asc, const uint64_t* tag, int32_t delta) {
    if (g_c131_update_tag_map == nullptr ||
        !g_c131_update_tag_map(static_cast<uint8_t*>(asc) + kTagCountContainer, tag, delta)) {
        return false;
    }
    void* const* const vtable = *static_cast<void* const* const*>(asc);
    reinterpret_cast<TagChangedFn>(vtable[kTagChangedVfunc / sizeof(void*)])(asc, tag, delta > 0 ? 1 : 0);
    return true;
}

// Host: an Equip frame for a bug server copy, run as the MI path runs it (EquipWeapon on the copy), with the form's
// Can tag lent to the copy's ASC for the call. c208_track checks that the equipped index follows (row->want).
void c210_copy_equip(void* pc, void* pawn, uint32_t index) {
    void* const inventory = c208_inventory(pawn);
    void* const asc = g_get_asc != nullptr ? g_get_asc(pawn, true) : nullptr;
    const bool needs = index < kC210CanTags && kC210CanTag[index] != nullptr;
    const uint64_t tag = needs ? fname_cached(g_c210.can_tag[index], kC210CanTag[index]) : 0;
    C210Tag state = !needs ? kC210TagNone : (asc == nullptr ? kC210TagFailed : kC210TagHeld);
    if (state == kC210TagHeld && !c210_has_tag(asc, tag)) {
        state = c210_tag_update(asc, &tag, 1) ? kC210TagAdded : kC210TagFailed;
    }
    const int32_t before = c50_list(inventory).equipped;
    c57_set_policy(kC57PolicyServerOnly);
    g_equip_weapon(pawn, static_cast<int32_t>(index));
    const bool removed = state != kC210TagAdded || c210_tag_update(asc, &tag, -1);
    const unsigned long long t = GetTickCount64();
    C208Row* const row = c208_row(object_index(pawn));
    if (row != nullptr) {
        row->want = static_cast<int32_t>(index);
        row->want_by = t + kC210EquipLateMs;
    }
    const bool bad = state == kC210TagFailed || !removed;
    if (bad ? c210_warn() : c210_line()) {
        log::line("%scard210: host t=%llu Equip %u from %s: tag %s %s%s, EquipWeapon on the server copy, equipped %d "
                  "-> %d",
                  bad ? "WARN " : "", t, index, object_name(pc).text, needs ? fname_text(&tag).text : "-",
                  kC210TagText[state], removed ? "" : " (NOT removed)", before, c50_list(inventory).equipped);
    }
}

// The anim instance's CurrentForm (ABP_PlayerBug's byte variable, found by name through the class's reflected
// properties) of the pawn's third person mesh, else its first person mesh; -1 when neither has one.
int32_t c210_current_form(const void* pawn, uint32_t& offset) {
    constexpr size_t kMeshes[] = {kC210CharacterMesh, kC210FirstPersonMesh};
    for (size_t m = 0; m < sizeof(kMeshes) / sizeof(kMeshes[0]) && g_c169_anim_instance != nullptr; ++m) {
        const void* const mesh = read_ptr(pawn, kMeshes[m]);
        const void* const anim = mesh != nullptr ? g_c169_anim_instance(mesh) : nullptr;
        const void* const cls = anim != nullptr ? read_ptr(anim, kObjectClass) : nullptr;
        const uint8_t* const prop = cls != nullptr ? prop_find_prop(cls, "CurrentForm") : nullptr;
        if (prop != nullptr && read_i32(prop, kPropertyElementSize) == 1) {
            offset = static_cast<uint32_t>(read_i32(prop, kPropertyOffset));
            return static_cast<const uint8_t*>(anim)[offset];
        }
    }
    return -1;
}

// Both sides: the form a bug pawn shows (anim CurrentForm) next to its equipped index and BugForms.* tags.
void c210_form_look(const void* pawn, int32_t equipped, const char* when) {
    char tags[256];
    c208_tags(pawn, tags, sizeof(tags));
    uint32_t offset = 0;
    const int32_t form = c210_current_form(pawn, offset);
    const void* const asc = g_get_asc != nullptr ? g_get_asc(pawn, true) : nullptr;
    const uint64_t invulnerable = fname_cached(g_c210.invulnerable, kC210Invulnerable);
    const int held = asc != nullptr ? (c210_has_tag(asc, invulnerable) ? 1 : 0) : -1;
    if (c210_line()) {
        log::line("card210: %s form look of %s (%s): equipped %d, CurrentForm %d, tags %s, Invulnerable %d (%s, "
                  "CurrentForm at +0x%X)",
                  side(), object_name(pawn).text, kSprintSideText[sprint_side(pawn)], equipped, form, tags, held, when,
                  offset);
    }
}

// Both sides, per c208_track step: the "+1 s" form look, and on the host an open Equip whose check time has come.
void c210_track(const void* pawn, C208Row& row, int32_t equipped, ULONGLONG now) {
    if (row.look_at != 0 && now >= row.look_at) {
        row.look_at = 0;
        c210_form_look(pawn, equipped, "+1 s");
    }
    if (row.want == kC210NoWant || now < row.want_by) {
        return;
    }
    if (equipped != row.want && c210_warn()) {
        char tags[256];
        c208_tags(pawn, tags, sizeof(tags));
        log::line("WARN card210: host t=%llu Equip %d on %s (server-copy): equipped still %d after %llu ms, tags %s",
                  static_cast<unsigned long long>(now), row.want, object_name(pawn).text, equipped,
                  static_cast<unsigned long long>(kC210EquipLateMs), tags);
    }
    row.want = kC210NoWant;
}

// Both sides, at LoadMap enter (pl_map_end): the rows name the old world's pawns.
void c208_map_end() {
    if (g_c208.peak > 0 || g_c208.skipped > 0) {
        log::line("card208: %s map end: bug form rows %d, peak %d of %d, rows skipped %u", side(), g_c208.rows,
                  g_c208.peak, kC208Rows, g_c208.skipped);
    }
    const uint32_t lines = g_c208.lines;
    const uint32_t warns = g_c208.warns;
    g_c208 = C208State{};
    g_c208.lines = lines; // the caps are per run
    g_c208.warns = warns;
}

int32_t c208_live() {
    return g_c208.rows;
}

void detour_c57_equip_weapon(void* pawn, int32_t index) {
    if (c57_line()) {
        char chain[96];
        caller_chain(chain, sizeof(chain));
        log::line("card057: %s ASTPlayerCharacter::EquipWeapon(%d) on %s (%s); %s; callers %s", side(), index,
                  object_name(pawn).text, kSprintSideText[sprint_side(pawn)],
                  g_c50.applying ? "card 050 apply" : "game", chain);
    }
    g_orig_c57_equip_weapon(pawn, index);
}

void detour_c57_activate(void* ability, int32_t handle, const void* actor_info, const void* activation_info,
                         const void* event_data) {
    const void* const avatar = avatar_of(actor_info);
    g_c57_last_avatar = avatar;
    if (sprint_side(avatar) == kSideHostOwn && g_weak_assign != nullptr) {
        void* object = ability;
        g_weak_assign(&g_c62_instance_weak, &object); // read in input_diag.cpp; the instance can be collected mid-map
    }
    if (c57_line()) {
        char chain[96];
        caller_chain(chain, sizeof(chain));
        log::line("card057: %s USTWeaponSwitchAbility::ActivateAbility(%s, handle %d) on %s (%s), event data %d; "
                  "callers %s",
                  side(), class_name(ability).text, handle, object_name(avatar).text,
                  kSprintSideText[sprint_side(avatar)], event_data != nullptr ? 1 : 0, chain);
    }
    g_orig_c57_activate(ability, handle, actor_info, activation_info, event_data);
}

bool detour_c57_can_activate(const void* ability, int32_t handle, const void* actor_info, const void* source_tags,
                             const void* target_tags, void* relevant_tags) {
    const bool can = g_orig_c57_can_activate(ability, handle, actor_info, source_tags, target_tags, relevant_tags);
    if (c57_line()) {
        const void* const avatar = avatar_of(actor_info);
        log::line("card057: %s USTWeaponSwitchAbility::CanActivateAbility(%s, handle %d) on %s (%s) = %d, "
                  "NetExecutionPolicy %u; caller exe+0x%llX",
                  side(), class_name(ability).text, handle, object_name(avatar).text,
                  kSprintSideText[sprint_side(avatar)], can ? 1 : 0, static_cast<const uint8_t*>(ability)[kC57NetExecutionPolicy],
                  exe_rva(_ReturnAddress()));
    }
    return can;
}

void detour_c57_begin_switch(void* ability) {
    if (c57_line()) {
        log::line("card057: %s USTWeaponSwitchAbility::BeginWeaponSwitch(%s), last activate on %s (%s); caller "
                  "exe+0x%llX",
                  side(), class_name(ability).text, object_name(g_c57_last_avatar).text,
                  kSprintSideText[sprint_side(g_c57_last_avatar)], exe_rva(_ReturnAddress()));
    }
    g_orig_c57_begin_switch(ability);
}

void c141_send_switch(const void* pawn, int32_t a, int32_t b, const void* actor);

void* detour_c57_request(const void* pawn, void* ret, int32_t a, int32_t b, const void* actor) {
    if (c57_line()) {
        char chain[96];
        caller_chain(chain, sizeof(chain));
        log::line("card057: %s ASTPlayerCharacter::ConstructWeaponSwitchRequest(%d, %d, %s) on %s (%s); callers %s",
                  side(), a, b, object_name(actor).text, object_name(pawn).text, kSprintSideText[sprint_side(pawn)],
                  chain);
    }
    if (g_client_session) {
        c57_set_policy(kC57PolicyLocalOnly);
    }
    if (g_client_session && !g_c50.applying && sprint_side(pawn) == kSideClientOwn) {
        if (actor == pawn && a >= 0) {
            c57_send_equip(pawn, a);
        } else {
            c141_send_switch(pawn, a, b, actor);
        }
    }
    return g_orig_c57_request(pawn, ret, a, b, actor);
}

void detour_c57_cycle(void* inventory, bool forward) {
    if (c57_line()) {
        char where[160];
        char chain[96];
        c21_inventory_text(inventory, where, sizeof(where));
        caller_chain(chain, sizeof(chain));
        log::line("card057: %s UADWeaponInventory::CycleEquip(%d) on %s; callers %s", side(), forward ? 1 : 0, where,
                  chain);
    }
    g_orig_c57_cycle(inventory, forward);
}

// --- Card 141: the client's underslung toggle and next/previous switch on its server copy ---
// ASTPlayerCharacterMI::SendWeaponSwitchRequest(i, b) (0x051DB390) builds ConstructWeaponSwitchRequest(i, 0, the
// equipped weapon) when b == 0 and i is the equipped index (or one weapon in the inventory in the underslung state):
// the underslung toggle, a gameplay event to the weapon. Any other request has actor = the pawn: a slot (i >= 0,
// relayed by card 057's Equip) or next/previous (i = -1, b = +1/-1). Q is bound to both IA_SwitchWeapon_Next (tap) and
// IA_SwitchWeapon_Underslung (InputTriggerHold). The Switch frame carries the request; the host replays it with
// SendWeaponSwitchRequest on the server copy, so the copy takes the same branch from its own state. want is the
// underslung state the toggle asks for: the host skips a toggle the copy already has.
// Packed value: (i + 1) << 4 | (b + 1) << 2 | weapon branch << 1 | want.
constexpr int32_t kC141IndexMax = 1000; // (kC141IndexMax + 1) << 4 stays below kChanArgMax
constexpr uint32_t kC141Lines = 64;

C141SwitchFn g_c141_send_switch = nullptr;
C141UnderslungStateFn g_c141_underslung_state = nullptr;
C57ActivateFn g_orig_c141_activate = nullptr;
C141ReceiveEventFn g_orig_c141_receive = nullptr;
uint32_t g_c141_lines = 0; // game thread

// Host: the last underslung request replayed on a server copy, waiting for c141_step's check (pc null: none).
struct C141Check {
    void* pc = nullptr;  // compare only; c141_step reads it through pc_weak (card 331: the PC can leave in 600 ms)
    C31Weak pc_weak{};
    int32_t want = 0;
    ULONGLONG due_ms = 0;
};
constexpr ULONGLONG kC141CheckMs = 600; // past the swap animation (0.20 s) and a frame or two
C141Check g_c141_check{};               // game thread

int32_t c141_state(const void* weapon) {
    return weapon != nullptr && g_c141_underslung_state != nullptr ? (g_c141_underslung_state(weapon) ? 1 : 0) : -1;
}

const void* c141_equipped(const void* pawn) {
    const void* const inventory = pawn != nullptr ? read_ptr(pawn, kMiWeaponInventory) : nullptr;
    return inventory != nullptr ? g_get_equipped_weapon(inventory) : nullptr;
}

// The equipped slot index of a pawn's weapon inventory (c50_equipped_slot), -1 when the pawn has none.
int32_t c141_equipped_index(const void* pawn) {
    return c50_equipped_slot(pawn != nullptr ? read_ptr(pawn, kMiWeaponInventory) : nullptr);
}

void c141_send_switch(const void* pawn, int32_t a, int32_t b, const void* actor) {
    if (g_c439.in_fire) { // card 439: goes after this Fire's Round (c439_fire_end)
        g_c439.sw_pawn = pawn;
        g_c439.sw_a = a;
        g_c439.sw_b = b;
        g_c439.sw_actor = actor;
        return;
    }
    const bool weapon = actor != pawn;
    const int32_t state = weapon ? c141_state(actor) : 0;
    void* const pc = read_ptr(pawn, kPawnController);
    const bool ok = a >= -1 && a < kC141IndexMax && b >= -1 && b <= 1 && state >= 0 && pc != nullptr;
    const uint32_t packed = ok ? static_cast<uint32_t>(a + 1) << 4 | static_cast<uint32_t>(b + 1) << 2 |
                                     (weapon ? 2u : 0u) | (state == 0 ? 1u : 0u)
                               : 0;
    const bool sent = ok && chan_to_host(pc, ChanOp::Switch, packed);
    g_c50.switch_due_ms = GetTickCount64() + kC50SwitchHoldMs; // card 312: card 050 slot moves wait
    log::line("%scard141: client t=%llu switch request (%d, %d, %s), underslung %d: Switch %s", sent ? "" : "WARN ",
              static_cast<unsigned long long>(GetTickCount64()), a, b, object_name(actor).text, state,
              sent ? "sent" : "NOT sent");
}

// Host: a Switch frame from the client that owns pc, replayed on that PC's server copy.
void c141_receive_switch(void* pc, uint32_t packed) {
    const int32_t a = static_cast<int32_t>(packed >> 4) - 1;
    const int32_t b = static_cast<int32_t>((packed >> 2) & 3u) - 1;
    const bool weapon = (packed & 2u) != 0;
    const int32_t want = static_cast<int32_t>(packed & 1u);
    void* const pawn = read_ptr(pc, kControllerPawn);
    const bool copy = pawn != nullptr && is_a(pawn, g_player_mi_class()) && sprint_side(pawn) == kSideServerCopy &&
                      g_c141_send_switch != nullptr;
    const void* const gun = copy ? c141_equipped(pawn) : nullptr;
    const int32_t before = c141_state(gun);
    const bool skip = weapon && before == want;
    // Card 312: the weapon branch is "toggle underslung on the equipped weapon". The client's index is its own slot
    // layout, which can differ from the copy's (b14: client slot 1, copy slot 0, so the copy took the slot branch).
    // The copy's own equipped index with b == 0 always takes the weapon branch of SendWeaponSwitchRequest.
    const int32_t copy_index = copy && weapon ? c141_equipped_index(pawn) : a;
    const bool send = copy && !skip && copy_index >= -1 && !(weapon && copy_index < 0);
    if (send) {
        c439_before_equip(pawn); // card 439: a reported round the copy has not fired yet goes first
        c57_set_policy(kC57PolicyServerOnly);
        g_c141_send_switch(pawn, copy_index, b);
    }
    // The weapon enters or leaves the underslung state when its swap animation ends (about 190 ms, c141a), so the
    // result is checked by c141_step, not here.
    if (send && weapon) {
        g_c141_check = C141Check{pc, C31Weak{}, want, GetTickCount64() + kC141CheckMs};
        c31_weak_set(g_c141_check.pc_weak, pc);
    }
    log::line("%scard141: host t=%llu Switch (%d, %d, %s, want %d) from %s: %s (%d, %d) on the server copy; client "
              "index %d; copy weapon %s underslung %d",
              send || (copy && skip) ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), a, b,
              weapon ? "underslung" : "pawn", want, object_name(pc).text,
              !copy  ? "dropped (no server copy pawn)"
              : skip ? "skipped (already in that state)"
              : send ? "SendWeaponSwitchRequest"
                     : "dropped (the copy has no equipped index)",
              copy_index, b, a, object_name(gun).text, before);
}

// Host, every tick: one pending underslung request's result, read kC141CheckMs after it was replayed. The pawn and
// weapon are read again through the PC, so a copy destroyed meanwhile reads as no weapon.
void c141_step(void* /*engine*/) {
    if (g_c141_check.pc == nullptr || GetTickCount64() < g_c141_check.due_ms) {
        return;
    }
    const C141Check c = g_c141_check;
    g_c141_check = C141Check{};
    void* const pc = c31_weak_get(c.pc_weak) == c.pc ? c.pc : nullptr;
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    const void* const gun = pawn != nullptr && is_a(pawn, g_player_mi_class()) ? c141_equipped(pawn) : nullptr;
    const int32_t now_state = c141_state(gun);
    log::line("%scard141: host t=%llu copy weapon %s underslung %d (want %d) %llu ms after the Switch",
              now_state == c.want ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()),
              object_name(gun).text, now_state, c.want, static_cast<unsigned long long>(kC141CheckMs));
}

// The player pawn behind an ability's avatar: the avatar itself, or the owner of a weapon avatar.
const void* c141_pawn_of(const void* avatar) {
    return avatar == nullptr || is_a(avatar, g_player_mi_class()) ? avatar : weapon_pawn(avatar);
}

void detour_c141_activate(void* ability, int32_t handle, const void* actor_info, const void* activation_info,
                          const void* event_data) {
    if (note_event_cap(g_c141_lines, kC141Lines, "card141 underslung")) {
        const void* const avatar = avatar_of(actor_info);
        const void* const pawn = c141_pawn_of(avatar);
        log::line("card141: %s t=%llu USTWeaponUnderslungAbility::ActivateAbility(%s %p, handle %d) on %s of %s (%s), "
                  "NetExecutionPolicy %u, event data %d",
                  side(), static_cast<unsigned long long>(GetTickCount64()), class_name(ability).text, ability,
                  handle, object_name(avatar).text, object_name(pawn).text, kSprintSideText[sprint_side(pawn)],
                  static_cast<const uint8_t*>(ability)[kC57NetExecutionPolicy], event_data != nullptr ? 1 : 0);
    }
    g_orig_c141_activate(ability, handle, actor_info, activation_info, event_data);
}

void detour_c141_receive(void* ability, const void* payload) {
    if (note_event_cap(g_c141_lines, kC141Lines, "card141 underslung")) {
        log::line("card141: %s t=%llu USTWeaponUnderslungAbility::ReceiveEvent(%s %p)", side(),
                  static_cast<unsigned long long>(GetTickCount64()), class_name(ability).text, ability);
    }
    g_orig_c141_receive(ability, payload);
}

// --- Card 062 audit (log only, UBWCOOP_C50_TEST): why the host's own switch is refused before CanActivateAbility ---
// InternalTryActivateAbility calls the instance's (else the CDO's) ShouldAbilityRespondToEvent before
// CanActivateAbility when it has event data. USTWeaponSwitchAbility's (0x05137FB0) returns false when the weapon
// inventory (+0x3D0) has no weapon at the asked index, the index is the equipped one, or CanSwapToWeapon says no.
constexpr uint32_t kC62Lines = 64;

C62RespondFn g_orig_c62_respond = nullptr;
SprintEndFn g_orig_c62_end = nullptr;
uint32_t g_c62_lines = 0; // game thread

bool c62_line() {
    return g_c50_test && note_event_cap(g_c62_lines, kC62Lines, "card062 audit");
}

bool c62_is_switch(const void* ability) {
    return ability != nullptr && strcmp(class_name(ability).text, "GA_WeaponSwitchAbility_C") == 0;
}

bool detour_c62_respond(const void* ability, const void* actor_info, const void* event_data) {
    const bool ok = g_orig_c62_respond(ability, actor_info, event_data);
    if (c62_line()) {
        const void* const avatar = avatar_of(actor_info);
        char inv[320];
        c48_inventory_text(avatar, inv, sizeof(inv));
        log::line("card062: %s t=%llu USTWeaponSwitchAbility::ShouldAbilityRespondToEvent(%s %p) on %s (%s) = %d; %s",
                  side(), static_cast<unsigned long long>(GetTickCount64()), class_name(ability).text, ability,
                  object_name(avatar).text, kSprintSideText[sprint_side(avatar)], ok ? 1 : 0, inv);
    }
    return ok;
}

void detour_c62_end(void* ability, int32_t handle, const void* actor_info, const void* activation_info,
                    bool replicate, bool was_cancelled) {
    if (c62_line()) {
        const void* const avatar = avatar_of(actor_info);
        log::line("card062: %s t=%llu USTWeaponSwitchAbility::EndAbility(%s %p, handle %d, bReplicate %d, "
                  "bWasCancelled %d) on %s (%s); caller exe+0x%llX",
                  side(), static_cast<unsigned long long>(GetTickCount64()), class_name(ability).text, ability, handle,
                  replicate ? 1 : 0, was_cancelled ? 1 : 0, object_name(avatar).text,
                  kSprintSideText[sprint_side(avatar)], exe_rva(_ReturnAddress()));
    }
    g_orig_c62_end(ability, handle, actor_info, activation_info, replicate, was_cancelled);
}

// --- Card 050 test driver (UBWCOOP_C50_TEST=1) ---
// Host: once its client has the list, the server copy is moved next to the nearest placed weapon pickup (a client
// teleport is corrected back by the server). Client: once the own pawn is near a placed weapon pickup, it aims at it,
// presses Use (through the interact detour, so UseName and Use go to the host) and releases.
constexpr ULONGLONG kC50TestSettleMs = 3000;  // host: after the first Weapons send, before the move
constexpr ULONGLONG kC50TestPollMs = 500;     // client: proximity check period
constexpr ULONGLONG kC50TestGiveUpMs = 40000; // client: no pickup near this long after the first apply
constexpr ULONGLONG kC50TestPressMs = 1500;   // client: after the pickup came near
constexpr ULONGLONG kC50TestReleaseMs = 1900;
constexpr ULONGLONG kC50TestShotMs = 3500;
constexpr ULONGLONG kC50TestDoneMs = 8000;
constexpr double kC50TestBack = 120.0; // host: uu from the pickup
constexpr double kC50TestUp = 50.0;
constexpr double kC50TestNear = 300.0; // client: uu
constexpr int32_t kC50TestListed = 8;

C50Test g_c50t{}; // game thread; once per process

constexpr C54Step kC50Steps[] = {{&g_c50_weapon_pickup_class, nullptr, "weapon pickup"}};
constexpr C54Step kC54Steps[] = {
    {&g_c54_switch_class, "BP_PanelSwitch_WithoutScreen", "panel switch"},
    {&g_c50_item_pickup_class, "BP_SatchelItemPickup_CTF", "satchel pickup"},
    {&g_c54_satchel_site_class, "BP_SatchelPlacementSite_Flag", "satchel placement site"},
    {&g_c54_zipline_class, nullptr, "zipline point"},
};
constexpr uint32_t kC54StepCount = static_cast<uint32_t>(sizeof(kC54Steps) / sizeof(kC54Steps[0]));
struct C54Run {
    uint32_t step;           // index into this side's step list
    uint32_t relays_at_move; // host: g_c50.relays when the copy was moved for this step
    ULONGLONG relay_ms;      // host: first relayed Use of this step; 0 = none yet
};
C54Run g_c54t{}; // game thread; each machine walks its own copy of the steps

// The current step, or null once all are done.
const C54Step* c54_step() {
    if (g_c54_test) {
        return g_c54t.step < kC54StepCount ? &kC54Steps[g_c54t.step] : nullptr;
    }
    return g_c54t.step < 1 ? &kC50Steps[0] : nullptr;
}

bool c54_step_match(const C54Step& st, const void* actor) {
    const char* const prefix = st.class_prefix;
    return c50_placed(actor) && (prefix == nullptr || strncmp(class_name(actor).text, prefix, strlen(prefix)) == 0);
}

// The switch enabled byte of `target` (-1 when it is not a switch or has no interface).
int32_t c54_enabled_of(const void* target) {
    const C54Relay* const row = c54_relay_row(target);
    void* const object = const_cast<void*>(target);
    const void* const iface = row != nullptr ? g_get_interface_address(object, g_c50_interactable_class()) : nullptr;
    return iface != nullptr ? c54_switch_enabled(row, iface) : -1;
}

// The nearest placed target of the current step to `from` (logs up to kC50TestListed candidates when `list_them`).
const C23Enemy* c50_test_pick(const void* world, const void* from, bool list_them) {
    const C54Step* const st = c54_step();
    static C23List list;
    if (st == nullptr) {
        return nullptr;
    }
    c23_collect(world, from, (*st->cls)(), list);
    const C23Enemy* best = nullptr;
    for (uint32_t i = 0; i < list.count; ++i) {
        const C23Enemy& e = list.e[i];
        const bool match = c54_step_match(*st, e.actor) &&
                           (!g_c60_mk1_only || strstr(class_name(e.actor).text, "MoritaMk1") != nullptr);
        if (list_them && i < static_cast<uint32_t>(kC50TestListed)) {
            log::line("card050: %s test %s candidate %u: %s (%s), placed %d, step match %d, %.0f uu", side(),
                      st->label, i, object_name(e.actor).text, class_name(e.actor).text, c50_placed(e.actor) ? 1 : 0,
                      match ? 1 : 0, e.dist);
        }
        if (match && (best == nullptr || e.dist < best->dist)) {
            best = &e;
        }
    }
    return best;
}

void* c50_test_copy(const void* world) {
    void* copy = nullptr;
    for (int s = 0; copy == nullptr && s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        copy = pc != nullptr && sprint_side(pc) == kSideServerCopy ? read_ptr(pc, kControllerPawn) : nullptr;
    }
    return copy;
}

// Host: the server copy moved next to the current step's target (a client teleport is corrected back).
void c50_test_move(const void* world, void* copy, ULONGLONG now) {
    const C54Step* const st = c54_step();
    g_c50t.host_moved = true;
    g_c50t.at_ms = now;
    g_c54t.relays_at_move = g_c50.relays;
    g_c54t.relay_ms = 0;
    const C23Enemy* const e = c50_test_pick(world, copy, true);
    if (e == nullptr) {
        log::line("WARN card050: host test: no placed %s to move the server copy to; next step", st->label);
        ++g_c54t.step;
        g_c50t.host_moved = false;
        g_c50t.at_ms = now - kC50TestSettleMs;
        return;
    }
    const Vec3 to{e->loc.x + kC50TestBack, e->loc.y, e->loc.z + kC50TestUp};
    const bool moved = g_set_actor_location(copy, &to, false, nullptr, kTeleportPhysics);
    log::line("card050: host t=%llu test step %u (%s) moves server copy %s next to %s: moved %d, %.0f uu away, "
              "switch enabled %d",
              static_cast<unsigned long long>(now), g_c54t.step, st->label, object_name(copy).text,
              object_name(e->actor).text, moved ? 1 : 0, vec_dist(actor_location(copy), e->loc),
              c54_enabled_of(e->actor));
}

// Host: the step ends kC50TestDoneMs after its first relayed Use, or when the client gave up on it.
void c54_host_advance(ULONGLONG now) {
    if (g_c54t.relay_ms == 0 && g_c50.relays > g_c54t.relays_at_move) {
        g_c54t.relay_ms = now;
        if (g_c54_test) {
            c43_shot(now + kC50TestShotMs, "card054 host screen after the relayed Use", kC44ShotUi);
        }
    }
    const bool done = g_c54t.relay_ms != 0 ? now - g_c54t.relay_ms >= kC50TestDoneMs
                                           : now - g_c50t.at_ms >= kC50TestGiveUpMs + kC50TestDoneMs;
    if (!done) {
        return;
    }
    log::line("%scard054: host t=%llu test step %u (%s) ends: %s", g_c54t.relay_ms != 0 ? "" : "WARN ",
              static_cast<unsigned long long>(now), g_c54t.step, c54_step()->label,
              g_c54t.relay_ms != 0 ? "a Use was relayed" : "no relayed Use came");
    ++g_c54t.step;
    g_c50t.host_moved = false;
    g_c50t.at_ms = now - kC50TestSettleMs;
}

void c50_test_host(const void* world, ULONGLONG now) {
    if (c54_step() == nullptr || g_c50.sends == 0) {
        g_c50t.at_ms = g_c50.sends == 0 ? now : g_c50t.at_ms;
        return;
    }
    void* const copy = c50_test_copy(world);
    if (copy == nullptr) {
        return;
    }
    if (g_c50t.host_moved) {
        c54_host_advance(now);
    } else if (now - g_c50t.at_ms >= kC50TestSettleMs) {
        c50_test_move(world, copy, now);
    }
}

// Client: the next step, or done after the last one.
void c54_client_next(ULONGLONG now) {
    ++g_c54t.step;
    const C54Step* const st = c54_step();
    if (st == nullptr) {
        g_c50t.stage = C50Stage::Done;
        c50_totals("test totals");
        log::line("card050: client test done");
        return;
    }
    log::line("card054: %s t=%llu test step %u (%s) begins", side(), static_cast<unsigned long long>(now),
              g_c54t.step, st->label);
    g_c50t.stage = C50Stage::Look;
    g_c50t.at_ms = now;
    g_c50t.poll_ms = now;
    g_c50t.pickup = nullptr;
    g_c50t.pickup_weak = C31Weak{};
}

// Client: waits for the current step's target within kC50TestNear of the own pawn.
void c50_test_look(const void* world, const void* own, ULONGLONG now) {
    if (now < g_c50t.poll_ms) {
        return;
    }
    g_c50t.poll_ms = now + kC50TestPollMs;
    const C23Enemy* const e = c50_test_pick(world, own, false);
    if (e != nullptr && e->dist <= kC50TestNear) {
        log::line("card050: client t=%llu test step %u (%s): %s is %.0f uu away, switch enabled %d",
                  static_cast<unsigned long long>(now), g_c54t.step, c54_step()->label, object_name(e->actor).text,
                  e->dist, c54_enabled_of(e->actor));
        c43_shot(now + 500,
                 g_c54_test ? "card054 client next to the step target" : "card050 client next to the weapon pickup",
                 kC44ShotUi);
        g_c50t.pickup = e->actor;
        c31_weak_set(g_c50t.pickup_weak, e->actor);
        g_c50t.stage = C50Stage::Aim;
        g_c50t.at_ms = now;
    } else if (now - g_c50t.at_ms >= kC50TestGiveUpMs) {
        log::line("WARN card050: client test step %u (%s): no placed target came within %.0f uu; skipped",
                  g_c54t.step, c54_step()->label, kC50TestNear);
        c54_client_next(now);
    }
}

// Client: aim, press Use through the interact detour (so UseName and Use go to the host), release, next step.
void c50_test_client(const void* world, void* pc, ULONGLONG now) {
    void* const own = const_cast<void*>(c23_own_pawn(pc));
    void* const interactor = pawn_interactor(own);
    if (interactor == nullptr || g_c50t.stage == C50Stage::Done) {
        return;
    }
    const ULONGLONG t = now - g_c50t.at_ms;
    const bool live = g_c50t.pickup != nullptr && c31_weak_get(g_c50t.pickup_weak) == g_c50t.pickup;
    if (g_c50t.stage == C50Stage::Wait && g_c50.applies > 0) {
        g_c50t = C50Test{C50Stage::Look, now, now, nullptr, {}, false};
    } else if (g_c50t.stage == C50Stage::Look) {
        c50_test_look(world, own, now);
    } else if (g_c50t.stage == C50Stage::Aim && live) {
        c44_aim(pc, actor_location(own), actor_location(g_c50t.pickup));
        if (t >= kC50TestPressMs) {
            log::line("card050: client t=%llu test press (step %s), own target %s",
                      static_cast<unsigned long long>(now), c54_step()->label, object_name(interact_target(interactor)).text);
            g_c50t.stage = C50Stage::Pressed;
            detour_interact(interactor);
            if (g_c54_test && is_a(g_c50t.pickup, g_c54_zipline_class())) {
                c54_track(own, "client own pawn after the zipline Use");
            }
        }
    } else if (g_c50t.stage == C50Stage::Pressed && t >= kC50TestReleaseMs) {
        g_c50t.stage = C50Stage::Released;
        detour_release_interaction(interactor);
        c43_shot(g_c50t.at_ms + kC50TestShotMs,
                 g_c54_test ? "card054 client screen after the Use" : "card050 client HUD after the pickup Use",
                 kC44ShotUi);
    } else if (g_c50t.stage == C50Stage::Released && t >= kC50TestDoneMs) {
        log::line("card054: client t=%llu test step %u (%s) ends: the client sees its target %s, switch enabled %d",
                  static_cast<unsigned long long>(now), g_c54t.step, c54_step()->label,
                  live ? object_name(g_c50t.pickup).text : "(gone)", live ? c54_enabled_of(g_c50t.pickup) : -1);
        c54_client_next(now);
    }
}

// Card 057 solo (UBWCOOP_C57_SOLO=1 with UBWCOOP_C50_TEST=1, host without a client): once the own pawn has been on
// foot for kC57SoloSettleMs, it is moved next to the nearest placed weapon pickup, then the card 050 client stages
// aim, press Use and release on the host's own pawn.
constexpr ULONGLONG kC57SoloSettleMs = 5000;

void c57_test_solo(const void* world, void* pc, ULONGLONG now) {
    if (g_c50t.stage != C50Stage::Wait) {
        c50_test_client(world, pc, now);
        return;
    }
    void* const own = const_cast<void*>(c23_own_pawn(pc));
    if (own == nullptr || g_c50t.at_ms == 0) {
        g_c50t.at_ms = own == nullptr ? 0 : now; // on-foot start
        return;
    }
    if (now - g_c50t.at_ms < kC57SoloSettleMs) {
        return;
    }
    const C23Enemy* const e = c50_test_pick(world, own, true);
    if (e == nullptr) {
        log::line("WARN card057: solo test: no placed weapon pickup; solo test done");
        g_c50t.stage = C50Stage::Done;
        return;
    }
    const Vec3 to{e->loc.x + kC50TestBack, e->loc.y, e->loc.z + kC50TestUp};
    const bool moved = g_set_actor_location(own, &to, false, nullptr, kTeleportPhysics);
    log::line("card057: host t=%llu solo test moves own pawn %s next to %s: moved %d", static_cast<unsigned long long>(now),
              object_name(own).text, object_name(e->actor).text, moved ? 1 : 0);
    g_c50t = C50Test{C50Stage::Look, now, now, nullptr, {}, true};
}

// Card 060 (UBWCOOP_C60_HOST_OWN=1 with UBWCOOP_C50_TEST=1 and a client): after the host test's last step and once
// the switch ability CDO is ServerOnly (the client's first switch), the card 057 solo stages run on the host's own
// pawn, then the host presses 2 once. g_c50t and g_c54t are shared, so the solo stages start from a fresh state.
constexpr ULONGLONG kC60KeyDelayMs = 2000; // after the solo Done, before the 2 press
constexpr ULONGLONG kC60KeyHoldMs = 150;
constexpr ULONGLONG kC60StartDelayMs = 60000; // after the start: card060b's 2 press came while the post-brief video played

C60Test g_c60t{}; // game thread; once per process

bool c60_policy_server_only() {
    return g_c57_cdo != nullptr && static_cast<const uint8_t*>(g_c57_cdo)[kC57NetExecutionPolicy] == kC57PolicyServerOnly;
}

void c60_test_host_own(const void* world, void* pc, ULONGLONG now) {
    if (!g_c60t.started) {
        if (c54_step() != nullptr || !c60_policy_server_only()) {
            return;
        }
        g_c60t.started = true;
        g_c60_mk1_only = true;
        g_c60t.start_ms = now;
        g_c50t = C50Test{};
        g_c54t.step = 0;
        log::line("card060: host t=%llu host test done and switch CDO policy %u: solo stages start on the own pawn",
                  static_cast<unsigned long long>(now), kC57PolicyServerOnly);
    }
    if (now - g_c60t.start_ms < kC60StartDelayMs) {
        return;
    }
    if (g_c50t.stage != C50Stage::Done) {
        c57_test_solo(world, pc, now);
        return;
    }
    c60_press_two(pc, now);
}

// Card 060/062: 2 s after the solo stages are Done, one 2 press on the own pawn, held kC60KeyHoldMs, then a shot.
void c60_press_two(void* pc, ULONGLONG now) {
    if (g_c60t.done_ms == 0) {
        g_c60t.done_ms = now;
    } else if (g_c60t.down_ms == 0 && now - g_c60t.done_ms >= kC60KeyDelayMs) {
        g_c60t.down_ms = now;
        const bool sent = autoinput::key_event(pc, L"Two", true);
        log::line("%scard060: host t=%llu presses 2 on its own pawn %s: %s, switch CDO policy %u (255 = not looked up)",
                  sent ? "" : "WARN ",
                  static_cast<unsigned long long>(now), object_name(c23_own_pawn(pc)).text, sent ? "sent" : "NOT sent",
                  g_c57_cdo != nullptr ? static_cast<const uint8_t*>(g_c57_cdo)[kC57NetExecutionPolicy] : 255u);
        c43_shot(now + kC50TestShotMs, "card060 host screen after its own 2 press", kC44ShotUi);
    } else if (g_c60t.down_ms != 0 && !g_c60t.released && now - g_c60t.down_ms >= kC60KeyHoldMs) {
        g_c60t.released = true;
        autoinput::key_event(pc, L"Two", false);
    }
}

void c21_mark_health_net() {
    const void* const cls = g_health_set_class != nullptr ? g_health_set_class() : nullptr;
    if (cls == nullptr) {
        log::line("WARN card021: UADAttributeSetHealth class not found; health stays server-only");
        return;
    }
    int marked = 0;
    int props = 0;
    void* field = read_ptr(cls, kStructChildProperties);
    for (; props < kC21MaxSetProps && field != nullptr; ++props, field = read_ptr(field, kFieldNext)) {
        const ObjectName name = fname_text(static_cast<const uint8_t*>(field) + kFieldName);
        if (c21_is_net_attribute(name.text)) {
            const uint64_t flags = read_u64(field, kPropertyFlags) | kCpfNet;
            memcpy(static_cast<uint8_t*>(field) + kPropertyFlags, &flags, sizeof(flags));
            ++marked;
            log::line("card021: %s CPF_Net set on UADAttributeSetHealth.%s (offset 0x%X, flags now 0x%016llX)", side(),
                      name.text, read_i32(field, kPropertyOffset), static_cast<unsigned long long>(flags));
        }
    }
    log::line("%scard021: %s health replication: %d of %d attributes marked CPF_Net (%d properties walked)",
              marked == 5 ? "" : "WARN ", side(), marked, 5, props);
}

// Card 143: host. A Restore weapon list (c50_text format) on a new server copy's inventory: the unwanted removed, the
// missing given, the wanted one equipped. True when the inventory then matches the list.
// Card 463: a Restore holds the copy's list as it was at state_ms (the load's Reload, or the card 440 keep); the
// map's script loadout rows recorded after that (run 523857607: RemovePlayersWeapons, GiveUnarmedWeapon) are applied
// over it, so the Restore never undoes a newer script edit. True: the inventory then matches the Restore list.
bool c143_apply_weapons(void* inventory, const char* text, ULONGLONG state_ms) {
    c278_note_restored(inventory); // card 278: this pawn's script loadout replay is skipped
    const C50Want want = c50_want(text);
    const int32_t removed = want.all ? c50_remove_extras(inventory, want) : 0; // keep everything on a lookup failure
    const int32_t given = c50_give_missing(inventory, want);
    c50_equip(inventory, want);
    const bool same = c50_same(inventory, want, false); // card 312: the copy's slots are its own; its client follows
    log::line("%scard143: host weapons from Restore: removed %d, given %d, %s", same ? "" : "WARN ", removed, given,
              same ? "matches the list" : "differs from the list");
    c463_replay_newer(inventory, state_ms);
    return same;
}

// Host: send the copy's counts to its client once its AmmoMap has entries. True when sent.
bool c72_seed(void* pc, void* pawn) {
    const void* const inventory = read_ptr(pawn, kMiAmmoInventory);
    static char text[kChanTextMax + 1];
    const int32_t len = c72_map_text(inventory, text, sizeof(text));
    if (len <= 0) {
        return false; // not reset yet (or holes): the next poll tries again
    }
    static wchar_t wire[kChanTextFrame];
    const WireString w = chan_text_frame(ChanOp::AmmoSet, text, wire);
    const bool sent = w.num > 0 && chan_send_client(pc, w);
    log::line("card072: host t=%llu AmmoSet to %s (copy %s): %s: %s", static_cast<unsigned long long>(GetTickCount64()),
              object_name(pc).text, object_name(pawn).text, sent ? "sent" : "WARN not sent", text);
    return sent;
}

} // namespace ubw::hooks
