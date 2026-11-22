// Card 310: a level script's per-player write on any player pawn reaches every player pawn, and each client where
// the client reads the value.
//
// Level scripts write per-player state on GetSTPlayerCharacter(self), player 0: the host's pawn, or inside a card 102
// scope the acting client's server copy. None of the state replicates. As card 267 does for weapons, the exec thunks
// mark the running call kind when the frame object is the host's level script (c267_level_script_frame); the impl
// detours then apply the same write to every other player pawn (c267_other_players) through the trampolines, with the
// kind cleared so a mirrored call never mirrors again.
//
//   SetFactionTag, SquadLeader SetEnabled   host AI reads them: host pawns only, no client row
//   SetCanTacticalSupportBeCharged          mirror, client row "C <0|1>"
//   RemoveItemByDefinition                  mirror through detour_c73_remove: card 073 relays each copy's count
//   AddAmmunition                           mirror through the trampoline: card 071's AddAmmunitions relays each gain
//   Add/RemoveLooseGameplayTags             count-based: every other pawn's count of each tag is set to the source's
//                                           count after the call; client row "T <tag> <count>"
//   DefaultTacticalSupportClass             a property write with no call: a 1 Hz host compare per player slot; the
//                                           changed pawn is the source; client row "D <class path>"
//
// Client rows are absolute (a count, a bool, a class), so a row applied twice or on top of the engine's own replication
// changes nothing. The host keeps the map's rows (one per tag, chargeable, default class); a server copy's new pawn
// (respawn or join) gets them applied on the host and sent to its client, as card 278 does for weapons. The client
// keeps the rows it got and applies them again on its own new pawn.
//
// The tables clear at map end (ubw_load_map_enter), not at the arm: a level script BeginPlay write made inside LoadMap
// lands between the two and must survive to the arm (the card 325 class, card 326). The old world is torn down inside
// LoadMap after map end, so a write from its level script (EndPlay) is dropped by the world gate in c310_exec.

#include "ubw/hooks_internal.h"
#include <stdio.h>
#include <string.h>

namespace ubw::hooks {

uint64_t c71_find_tag(const char* tag_text); // ammo.cpp

C310Kind g_c310_kind = C310Kind::None;
ExecFn g_orig_c310_exec_faction = nullptr;
ExecFn g_orig_c310_exec_squad = nullptr;
ExecFn g_orig_c310_exec_charge = nullptr;
ExecFn g_orig_c310_exec_item = nullptr;
ExecFn g_orig_c310_exec_ammo = nullptr;
ExecFn g_orig_c310_exec_tag_add = nullptr;
ExecFn g_orig_c310_exec_tag_remove = nullptr;
C310TagSetFn g_orig_c310_set_faction = nullptr;
C310BoolFn g_orig_c310_set_squad = nullptr;
C310BoolFn g_orig_c310_set_charge = nullptr;
AddAmmunitionFn g_orig_c310_add_ammo = nullptr;
C310CountUpdateFn g_orig_c310_count_update = nullptr;
C310AscUpdateFn g_orig_c310_asc_update = nullptr;
C310TagCountFn g_c310_tag_count = nullptr;

namespace {

// Return addresses (exe RVA) of the one tag-map call inside each loose-tag thunk (disassembly, card 310 (b)):
// execAddLooseGameplayTags calls FGameplayTagCountContainer::UpdateTagMap_Internal(ASC+0xF18, tag, 1) per tag;
// execRemoveLooseGameplayTags calls UAbilitySystemComponent::UpdateTagMap_Internal(ASC, container, -1) once.
constexpr uint64_t kC310AddCallRet = 0x49793D1;
constexpr uint64_t kC310RemoveCallRet = 0x498F06D;
// Card 439: the return address of the UpdateTagMap_Internal call in FMinimalReplicationTagCountMap::UpdateOwnerTagMap
// (call at exe 0x144A0790F), the receive path of a replicated tag map.
constexpr uint64_t kC439RepTagRet = 0x4A07914;
constexpr size_t kC310AscTagCounts = 0xF18; // UAbilitySystemComponent: its FGameplayTagCountContainer
constexpr int32_t kC310CaptureTags = 8;     // tags of one loose-tag call
constexpr int32_t kC310JournalTags = 16;    // distinct tags per map
constexpr ULONGLONG kC310PollMs = 1000;     // the DefaultTacticalSupportClass compare
constexpr size_t kC310ArgsText = 192;
constexpr size_t kC310PathText = 256;

// FGameplayTagContainer: TArray<FGameplayTag> GameplayTags, TArray<FGameplayTag> ParentTags (0x20 bytes).
struct C310TagContainer {
    const uint64_t* data;
    int32_t num;
    int32_t max;
    const void* parent_data;
    int32_t parent_num;
    int32_t parent_max;
};
static_assert(sizeof(C310TagContainer) == 0x20);

// The tags one loose-tag thunk changed, captured by the tag-map detours while the thunk runs.
struct C310Capture {
    void* asc;
    uint64_t tags[kC310CaptureTags];
    int32_t n;
};

// The map's rows: the host's synced values, the client's received ones.
struct C310Journal {
    uint64_t tag[kC310JournalTags];
    int32_t count[kC310JournalTags];
    int32_t tags;
    int32_t chargeable;  // -1 unset, else 0 or 1
    void* default_class; // null unset
    uint64_t default_name; // card 608: its FName, copied at each write; the map arm prints only this
};

// Host, per player slot: the pawn last seen and its DefaultTacticalSupportClass then.
struct C310Slot {
    int32_t pawn_id;
    void* default_class;
    const void* deferred_pc; // host: a server copy not ponged yet whose client rows wait for its first Pong; null none
};

struct C310State {
    C310Journal journal;
    C310Slot slots[kC17Slots];
    C310Capture capture;
    ULONGLONG next_poll_ms;
    int32_t own_pawn_id; // client: the own pawn the journal was last applied to
    uint32_t writes;     // host: level script writes on a player pawn
    uint32_t mirrored;   // host: applications to another player pawn
    uint32_t sent;       // host: client rows sent
    uint32_t not_sent;   // host: client rows a send refused
    uint32_t deferred;   // host: client rows held for a copy not ponged yet (its join replay sends the current set)
    uint32_t received;   // client: rows received
    uint32_t applied;    // client: rows applied on the own pawn
    uint32_t teardown;   // host: level script writes from the world being left, dropped (map end to arm)
};
C310State g_c310 = {};

ULONGLONG now_ms() {
    return GetTickCount64();
}

void* c310_tac_component(const void* pawn) {
    const uint8_t* prop = nullptr;
    return pawn != nullptr ? const_cast<void*>(prop_object_field(pawn, "TacticalSupportComponent", &prop)) : nullptr;
}

// The byte offset of DefaultTacticalSupportClass on the component's class, or -1. Only a ClassProperty (a raw
// TSubclassOf pointer) is read and written; any other kind is logged once per map and left alone.
int32_t c310_default_class_offset(const void* comp) {
    const uint8_t* const prop =
        comp != nullptr ? prop_find_prop(read_ptr(comp, kObjectClass), "DefaultTacticalSupportClass") : nullptr;
    if (prop != nullptr && !prop_kind_is(prop, "ClassProperty")) {
        static bool logged = false;
        if (!logged) {
            ObjectName kind = {};
            prop_kind(prop, kind);
            log::line("WARN card310: %s DefaultTacticalSupportClass is a %s, not a ClassProperty: not synced", side(),
                      kind.text);
            logged = true;
        }
        return -1;
    }
    return prop != nullptr ? read_i32(prop, kPropertyOffset) : -1;
}

void* c310_default_class(const void* pawn) {
    const void* const comp = c310_tac_component(pawn);
    const int32_t off = c310_default_class_offset(comp);
    return off >= 0 ? read_ptr(comp, static_cast<size_t>(off)) : nullptr;
}

// Writes the pawn's DefaultTacticalSupportClass. False when the pawn has no component or property.
bool c310_set_default_class(const void* pawn, void* cls) {
    void* const comp = c310_tac_component(pawn);
    const int32_t off = c310_default_class_offset(comp);
    if (off < 0) {
        return false;
    }
    *reinterpret_cast<void**>(static_cast<uint8_t*>(comp) + off) = cls;
    return true;
}

int32_t c310_count(const void* asc, uint64_t tag) {
    return asc != nullptr && g_c310_tag_count != nullptr ? g_c310_tag_count(asc, tag) : -1;
}

// Sets the pawn's count of tag to want through UAbilitySystemComponent::UpdateTagMap_Internal (OnTagUpdated runs
// for a tag that appears or goes). Returns the count before, or -1 without an ability system component.
int32_t c310_set_tag_count(const void* pawn, uint64_t tag, int32_t want) {
    void* const asc = pawn != nullptr && g_get_asc != nullptr ? g_get_asc(pawn, true) : nullptr;
    const int32_t have = c310_count(asc, tag);
    if (have < 0 || have == want || g_orig_c310_asc_update == nullptr) {
        return have;
    }
    const C310TagContainer one = {&tag, 1, 1, nullptr, 0, 0};
    g_orig_c310_asc_update(asc, &one, want - have);
    return have;
}

// The host player pawn whose ability system component is asc, or null (a bug, a turret, any other actor).
void* c310_asc_pawn(const void* world, const void* asc) {
    for (int s = 0; s < kC17Slots && asc != nullptr; ++s) {
        const void* const pc = g_get_player_controller(world, s);
        void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
        if (pawn != nullptr && c267_source(pawn) && g_get_asc(pawn, true) == asc) {
            return pawn;
        }
    }
    return nullptr;
}

// Host: true while the card 139 gate holds live frames to this copy's client (no first Pong yet on this map); the
// same C50Copy::ponged read as chan_ops.cpp c139_client_hold.
bool c310_held(const void* pc) {
    const C50Copy* const c = c50_copy(object_index(pc), false);
    return c == nullptr || !c->ponged;
}

// Host: one client row to every server copy's client. Returns the number of clients it went to. A copy not ponged
// yet gets nothing now: its slot is marked and its join replay at the first Pong sends the current set (b16).
int32_t c310_send_all(const void* world, const char* row) {
    static wchar_t wire[kChanTextFrame];
    const WireString w = chan_text_frame(ChanOp::ScriptWrite, row, wire);
    int32_t n = 0;
    for (int s = 0; s < kC17Slots && w.num > 0; ++s) {
        void* const pc = g_get_player_controller(world, s);
        if (pc == nullptr || sprint_side(pc) != kSideServerCopy) {
            continue;
        }
        if (c310_held(pc)) {
            g_c310.slots[s].deferred_pc = pc;
            ++g_c310.deferred;
            log::line("card310: host client row \"%s\" deferred: %s not ponged yet (card 139), sent at its first Pong",
                      row, object_name(pc).text);
            continue;
        }
        const bool ok = chan_send_client(pc, w);
        g_c310.sent += ok ? 1 : 0;
        g_c310.not_sent += ok ? 0 : 1;
        n += ok ? 1 : 0;
        if (!ok) {
            log::line("WARN card310: host client row \"%s\" not sent to %s", row, object_name(pc).text);
        }
    }
    return n;
}

void c310_host_row(const char* native, const char* args, const void* source, int32_t others, const char* client) {
    ++g_c310.writes;
    g_c310.mirrored += static_cast<uint32_t>(others);
    log::line("card310: host t=%llu level script %s(%s) on %s (%s) applied to %d other player pawn(s), client row %s",
              static_cast<unsigned long long>(now_ms()), native, args, object_name(source).text,
              kSprintSideText[sprint_side(source)], others, client);
}

// The journal row of tag, added when new; null when the table is full (WARN).
int32_t* c310_journal_tag(C310Journal& j, uint64_t tag) {
    for (int32_t i = 0; i < j.tags; ++i) {
        if (j.tag[i] == tag) {
            return &j.count[i];
        }
    }
    if (j.tags >= kC310JournalTags) {
        log::line("WARN card310: %s tag journal full (%d): %s not kept", side(), kC310JournalTags,
                  fname_text(&tag).text);
        return nullptr;
    }
    j.tag[j.tags] = tag;
    return &j.count[j.tags++];
}

} // namespace

// --- Exec thunks: the call kind while a host level script frame runs (saved and restored for nested calls) ---

void c310_exec(ExecFn original, C310Kind kind, void* context, void* frame, void* result) {
    const C310Kind outer = g_c310_kind;
    const C310Capture outer_capture = g_c310.capture;
    const bool script = !g_client_session && c267_level_script_frame(frame);
    // Card 332: the one teardown state (sync_registry.cpp) names the world being left.
    const bool old_world = script && c332_leaving(g_actor_get_world(read_ptr(frame, kFrameObject)));
    g_c310.teardown += old_world ? 1 : 0;
    g_c310_kind = script && !old_world ? kind : C310Kind::None;
    g_c310.capture.n = 0;
    g_c310.capture.asc = nullptr;
    original(context, frame, result);
    if (g_c310_kind == C310Kind::TagAdd || g_c310_kind == C310Kind::TagRemove) {
        c310_mirror_tags(read_ptr(frame, kFrameObject), g_c310_kind == C310Kind::TagAdd); // the level script
    }
    g_c310_kind = outer;
    g_c310.capture = outer_capture;
}

void detour_c310_exec_faction(void* context, void* frame, void* result) {
    c310_exec(g_orig_c310_exec_faction, C310Kind::Faction, context, frame, result);
}
void detour_c310_exec_squad(void* context, void* frame, void* result) {
    c310_exec(g_orig_c310_exec_squad, C310Kind::Squad, context, frame, result);
}
void detour_c310_exec_charge(void* context, void* frame, void* result) {
    c310_exec(g_orig_c310_exec_charge, C310Kind::Charge, context, frame, result);
}
void detour_c310_exec_item(void* context, void* frame, void* result) {
    c310_exec(g_orig_c310_exec_item, C310Kind::Item, context, frame, result);
}
void detour_c310_exec_ammo(void* context, void* frame, void* result) {
    c310_exec(g_orig_c310_exec_ammo, C310Kind::Ammo, context, frame, result);
}
void detour_c310_exec_tag_add(void* context, void* frame, void* result) {
    c310_exec(g_orig_c310_exec_tag_add, C310Kind::TagAdd, context, frame, result);
}
void detour_c310_exec_tag_remove(void* context, void* frame, void* result) {
    c310_exec(g_orig_c310_exec_tag_remove, C310Kind::TagRemove, context, frame, result);
}

// --- Impl detours: the write on the source pawn, then the same write on every other player pawn ---

// The same-class component of every other player pawn of source (the component's owner). Returns the count.
int32_t c310_other_components(const void* comp, const void* source, void** out, int32_t cap) {
    void* pawns[kC17Slots] = {};
    const int32_t m = c267_other_players(source, pawns, kC17Slots);
    void* const cls = read_ptr(comp, kObjectClass);
    int32_t n = 0;
    for (int32_t i = 0; i < m && n < cap; ++i) {
        void* const other = find_component_of(pawns[i], cls);
        if (other != nullptr) {
            out[n++] = other;
        } else {
            log::line("WARN card310: host %s has no %s: write not mirrored", object_name(pawns[i]).text,
                      object_name(cls).text);
        }
    }
    return n;
}

// The source pawn of a component write in the current kind, or null when this call is not a level script write on a
// player pawn.
const void* c310_component_source(const void* comp, C310Kind kind) {
    const void* const owner = comp != nullptr ? read_ptr(comp, kComponentOwner) : nullptr;
    return g_c310_kind == kind && c267_source(owner) ? owner : nullptr;
}

void detour_c310_set_faction(void* comp, const void* tag) {
    g_orig_c310_set_faction(comp, tag);
    const void* const source = c310_component_source(comp, C310Kind::Faction);
    if (source == nullptr || tag == nullptr) {
        return;
    }
    g_c310_kind = C310Kind::None;
    void* others[kC17Slots] = {};
    const int32_t n = c310_other_components(comp, source, others, kC17Slots);
    for (int32_t i = 0; i < n; ++i) {
        g_orig_c310_set_faction(others[i], tag);
    }
    g_c310_kind = C310Kind::Faction;
    c310_host_row("SetFactionTag", fname_text(tag).text, source, n, "not needed");
}

void c310_mirror_bool(C310BoolFn fn, C310Kind kind, const char* native, void* comp, bool on) {
    const void* const source = c310_component_source(comp, kind);
    if (source == nullptr) {
        return;
    }
    g_c310_kind = C310Kind::None;
    void* others[kC17Slots] = {};
    const int32_t n = c310_other_components(comp, source, others, kC17Slots);
    for (int32_t i = 0; i < n; ++i) {
        fn(others[i], on);
    }
    g_c310_kind = kind;
    const char* client = "not needed";
    if (kind == C310Kind::Charge) {
        g_c310.journal.chargeable = on ? 1 : 0;
        const int32_t sent = c310_send_all(source, on ? "C 1" : "C 0");
        client = sent > 0 ? "sent" : "sent to no client";
    }
    c310_host_row(native, on ? "true" : "false", source, n, client);
}

void detour_c310_set_squad(void* comp, bool on) {
    g_orig_c310_set_squad(comp, on);
    c310_mirror_bool(g_orig_c310_set_squad, C310Kind::Squad, "SquadLeader SetEnabled", comp, on);
}

void detour_c310_set_charge(void* comp, bool on) {
    g_orig_c310_set_charge(comp, on);
    c310_mirror_bool(g_orig_c310_set_charge, C310Kind::Charge, "SetCanTacticalSupportBeCharged", comp, on);
}

// From detour_c73_remove (grenades.cpp), after the source's removal and its card 073 relay.
void c310_note_item_remove(void* inventory, void* const* definition, int32_t amount, bool flag) {
    const void* const source = c310_component_source(inventory, C310Kind::Item);
    if (source == nullptr) {
        return;
    }
    g_c310_kind = C310Kind::None;
    void* others[kC17Slots] = {};
    const int32_t n = c310_other_components(inventory, source, others, kC17Slots);
    for (int32_t i = 0; i < n; ++i) {
        detour_c73_remove(others[i], definition, amount, flag); // card 073 relays a copy's new count
    }
    g_c310_kind = C310Kind::Item;
    char args[kC310ArgsText];
    snprintf(args, sizeof(args), "%s, %d, %d", object_name(definition != nullptr ? *definition : nullptr).text, amount,
             flag ? 1 : 0);
    c310_host_row("RemoveItemByDefinition", args, source, n, "carried by card 073");
}

int32_t detour_c310_add_ammo(void* actor, uint64_t tag, int32_t count) {
    const int32_t result = g_orig_c310_add_ammo(actor, tag, count);
    if (g_c310_kind != C310Kind::Ammo || !c267_source(actor)) {
        return result;
    }
    g_c310_kind = C310Kind::None;
    void* others[kC17Slots] = {};
    const int32_t n = c267_other_players(actor, others, kC17Slots);
    for (int32_t i = 0; i < n; ++i) {
        g_orig_c310_add_ammo(others[i], tag, count); // card 071's AddAmmunitions detour relays a copy's gain
    }
    g_c310_kind = C310Kind::Ammo;
    char args[kC310ArgsText];
    snprintf(args, sizeof(args), "%s, %d", fname_text(&tag).text, count);
    c310_host_row("AddAmmunition", args, actor, n, "carried by card 071");
    return result;
}

// --- Loose tags: the thunk's own tag-map call is captured; the mirror runs after the thunk ---

void c310_capture(void* asc, const uint64_t* tags, int32_t n) {
    C310Capture& c = g_c310.capture;
    if (c.asc != nullptr && c.asc != asc) {
        log::line("WARN card310: host one loose-tag call on two ability system components: %s not mirrored",
                  object_name(asc).text);
        return;
    }
    c.asc = asc;
    for (int32_t i = 0; i < n && c.n < kC310CaptureTags; ++i) {
        c.tags[c.n++] = tags[i];
    }
    if (n > kC310CaptureTags) {
        log::line("WARN card310: host loose-tag call with %d tags: the first %d mirrored", n, kC310CaptureTags);
    }
}

// Card 439 root (c423b140a): the host copy's weapon state tags reach the client's own pawn ASC through
// FMinimalReplicationTagCountMap::UpdateOwnerTagMap, which sets each count to the host's value. The client runs its
// own fire, spool, reload and recoil abilities, so the host's value overwrites a local count, and the copy's SpoolDown
// tag cancelled the client's own fire ability 1 ms before its own end (UADWeaponGameplayAbility::
// OnInventoryOwnerTagCountChange -> CancelAbility): Fire stayed on the pawn and the next press fired with no windup.
// True when this replicated change is for one of those tags on the client's own pawn: the caller drops it.
bool c439_own_state_tag(void* container, const uint64_t* tag, int32_t delta) {
    constexpr uint32_t kC439Lines = 40;
    static const wchar_t* const kNames[] = {L"WeaponSystem.EquippedWeapon.Fire", L"WeaponSystem.EquippedWeapon.SpoolUp",
                                            L"WeaponSystem.EquippedWeapon.SpoolDown",
                                            L"WeaponSystem.EquippedWeapon.Reload", L"WeaponSystem.Recoiling"};
    static const char* const kText[] = {"Fire", "SpoolUp", "SpoolDown", "Reload", "Recoiling"};
    constexpr int kTags = sizeof(kNames) / sizeof(kNames[0]);
    static uint64_t cache[kTags] = {};
    static uint32_t lines = 0;
    static uint32_t dropped = 0;
    if (!g_client_session || tag == nullptr || g_get_asc == nullptr) {
        return false;
    }
    int hit = -1;
    for (int i = 0; i < kTags && hit < 0; ++i) {
        hit = fname_cached(cache[i], kNames[i]) == *tag ? i : -1;
    }
    void* const asc = static_cast<uint8_t*>(container) - kC310AscTagCounts;
    const void* const owner = read_ptr(asc, kComponentOwner);
    if (owner == nullptr || sprint_side(owner) != kSideClientOwn || g_get_asc(owner, true) != asc) {
        return false;
    }
    // c439k diagnostic (c423b143a): one client fire cancel still came through this call (callers 0x4A07914) with
    // no dropped row, so a tag outside the list cancels too. Each one that passes on the own pawn is logged.
    static uint32_t passed = 0;
    if (hit < 0) {
        ++passed;
        if (passed <= kC439Lines) {
            log::line("card439: client t=%llu host tag map change of %s by %d on the own pawn passed (count before "
                      "%d, passed %u)", static_cast<unsigned long long>(GetTickCount64()), fname_text(tag).text,
                      delta, c310_count(asc, *tag), passed);
        }
        return false;
    }
    ++dropped;
    if (lines < kC439Lines) {
        ++lines;
        log::line("card439: client t=%llu host tag map change of %s by %d on the own pawn dropped (local count %d, "
                  "dropped %u)", static_cast<unsigned long long>(GetTickCount64()), kText[hit], delta,
                  c310_count(asc, *tag), dropped);
    }
    return true;
}

bool detour_c310_count_update(void* container, const void* tag, int32_t delta) {
    if (exe_rva(_ReturnAddress()) == kC439RepTagRet &&
        c439_own_state_tag(container, static_cast<const uint64_t*>(tag), delta)) {
        return false; // not changed: UpdateOwnerTagMap runs no tag event for it
    }
    if (g_c310_kind == C310Kind::TagAdd && exe_rva(_ReturnAddress()) == kC310AddCallRet && tag != nullptr) {
        c310_capture(static_cast<uint8_t*>(container) - kC310AscTagCounts, static_cast<const uint64_t*>(tag), 1);
    }
    return g_orig_c310_count_update(container, tag, delta);
}

void detour_c310_asc_update(void* asc, const void* container, int32_t delta) {
    if (g_c310_kind == C310Kind::TagRemove && exe_rva(_ReturnAddress()) == kC310RemoveCallRet && container != nullptr) {
        const auto* const c = static_cast<const C310TagContainer*>(container);
        c310_capture(asc, c->data, c->data != nullptr ? c->num : 0);
    }
    g_orig_c310_asc_update(asc, container, delta);
}

// After a host level script's Add/RemoveLooseGameplayTags: each captured tag's count on the source is set on every
// other player pawn and sent to every client.
void c310_mirror_tags(const void* world, bool add) {
    const C310Capture c = g_c310.capture;
    const void* const source = c.n > 0 ? c310_asc_pawn(world, c.asc) : nullptr;
    if (source == nullptr) {
        return; // not a player pawn's tags
    }
    g_c310_kind = C310Kind::None;
    void* others[kC17Slots] = {};
    const int32_t n = c267_other_players(source, others, kC17Slots);
    for (int32_t t = 0; t < c.n; ++t) {
        const int32_t count = c310_count(c.asc, c.tags[t]);
        int32_t* const kept = c310_journal_tag(g_c310.journal, c.tags[t]);
        if (kept != nullptr) {
            *kept = count;
        }
        for (int32_t i = 0; i < n; ++i) {
            c310_set_tag_count(others[i], c.tags[t], count);
        }
        char row[kC310ArgsText];
        snprintf(row, sizeof(row), "T %s %d", fname_text(&c.tags[t]).text, count);
        const int32_t sent = c310_send_all(world, row);
        char args[kC310ArgsText];
        snprintf(args, sizeof(args), "%s, count now %d", fname_text(&c.tags[t]).text, count);
        c310_host_row(add ? "AddLooseGameplayTags" : "RemoveLooseGameplayTags", args, source, n,
                      sent > 0 ? "sent" : "sent to no client");
    }
}

// --- DefaultTacticalSupportClass (no call): the host's 1 Hz compare, and the map's rows on a copy's new pawn ---

// The source's new default class onto every other player pawn and every client.
void c310_sync_default_class(const void* world, const void* source, void* cls) {
    g_c310.journal.default_class = cls;
    g_c310.journal.default_name = object_fname(cls);
    void* others[kC17Slots] = {};
    const int32_t n = c267_other_players(source, others, kC17Slots);
    for (int32_t i = 0; i < n; ++i) {
        c310_set_default_class(others[i], cls);
    }
    char path[kC310PathText];
    char row[kC310PathText + 4];
    const bool named = c50_class_path(cls, path, static_cast<int32_t>(sizeof(path))) > 0;
    snprintf(row, sizeof(row), "D %s", named ? path : "None");
    const int32_t sent = named || cls == nullptr ? c310_send_all(world, row) : 0;
    if (!named && cls != nullptr) {
        log::line("WARN card310: host default class %s has no path: client row not sent", object_name(cls).text);
    }
    c310_host_row("DefaultTacticalSupportClass =", named ? path : object_name(cls).text, source, n,
                  sent > 0 ? "sent" : "sent to no client");
}

// The byte SetCanTacticalSupportBeCharged(bool) writes on the component (the shipping exe's setter is one
// `mov [rcx+0x221], dl`). Read only, to take the host's own live value for a new copy pawn.
constexpr size_t kC310ChargeByte = 0x221;

// The host's own live MI pawn, or null (the host is down or between pawns).
void* c310_host_own_pawn(const void* world) {
    for (int s = 0; s < kC17Slots; ++s) {
        const void* const pc = g_get_player_controller(world, s);
        void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
        if (pawn != nullptr && sprint_side(pawn) == kSideHostOwn && c267_source(pawn) &&
            is_a(pawn, g_player_mi_class())) {
            return pawn;
        }
    }
    return nullptr;
}

// Host: one row to one client; a failed send is counted and logged like c310_send_all.
bool c310_send_one(void* pc, const char* row) {
    static wchar_t wire[kChanTextFrame];
    const bool ok = chan_send_client(pc, chan_text_frame(ChanOp::ScriptWrite, row, wire));
    g_c310.sent += ok ? 1 : 0;
    g_c310.not_sent += ok ? 0 : 1;
    if (!ok) {
        log::line("WARN card310: host client row \"%s\" not sent to %s", row, object_name(pc).text);
    }
    return ok;
}

// Host: the map's values for a copy: each from the host's own live MI pawn (a checkpoint load rolls the level script
// back without clearing the journal); the journal value only when the host has no live MI pawn.
struct C310Values {
    int32_t count[kC310JournalTags];
    int32_t chargeable; // -1 unset
    void* cls;          // null unset
    bool from_host;
};

C310Values c310_values(const void* world) {
    const C310Journal& j = g_c310.journal;
    void* const host = c310_host_own_pawn(world);
    const void* const host_asc = host != nullptr ? g_get_asc(host, true) : nullptr;
    const uint8_t* const host_comp = static_cast<const uint8_t*>(host != nullptr ? c310_tac_component(host) : nullptr);
    C310Values v = {};
    for (int32_t i = 0; i < j.tags; ++i) {
        const int32_t live = c310_count(host_asc, j.tag[i]);
        v.count[i] = live >= 0 ? live : j.count[i];
    }
    v.chargeable = host_comp != nullptr ? (host_comp[kC310ChargeByte] != 0 ? 1 : 0) : j.chargeable;
    v.cls = host != nullptr ? c310_default_class(host) : j.default_class;
    v.from_host = host != nullptr;
    return v;
}

// Host: the values on a server copy's new pawn (the host-side apply).
void c310_apply_values(void* pawn, const C310Values& v) {
    const C310Journal& j = g_c310.journal;
    for (int32_t i = 0; i < j.tags; ++i) {
        c310_set_tag_count(pawn, j.tag[i], v.count[i]);
    }
    void* const charge = v.chargeable >= 0 ? c310_tac_component(pawn) : nullptr;
    if (charge != nullptr) {
        g_orig_c310_set_charge(charge, v.chargeable == 1);
    }
    if (v.cls != nullptr) {
        c310_set_default_class(pawn, v.cls);
    }
}

// Host: the values as client rows to one copy's client. Counts rows sent and refused into sent/failed.
void c310_send_values(void* pc, const C310Values& v, int32_t& sent, int32_t& failed) {
    const C310Journal& j = g_c310.journal;
    char row[kC310PathText + 4];
    auto one = [&](const char* r) {
        const bool ok = c310_send_one(pc, r);
        sent += ok ? 1 : 0;
        failed += ok ? 0 : 1;
    };
    for (int32_t i = 0; i < j.tags; ++i) {
        snprintf(row, sizeof(row), "T %s %d", fname_text(&j.tag[i]).text, v.count[i]);
        one(row);
    }
    if (v.chargeable >= 0) {
        one(v.chargeable == 1 ? "C 1" : "C 0");
    }
    char path[kC310PathText];
    if (v.cls != nullptr && c50_class_path(v.cls, path, static_cast<int32_t>(sizeof(path))) > 0) {
        snprintf(row, sizeof(row), "D %s", path);
        one(row);
    }
}

// Host: the map's rows on a server copy's new pawn, and to its client. A client not ponged yet (card 139) gets its
// rows from c310_join_replay at its first Pong instead: the slot keeps its PC until then (b16).
void c310_apply_journal_to_copy(void* world, int s, void* pc, void* pawn) {
    const C310Values v = c310_values(world);
    c310_apply_values(pawn, v);
    const bool held = c310_held(pc);
    int32_t rows = 0;
    int32_t failed = 0;
    if (held) {
        g_c310.slots[s].deferred_pc = pc;
        ++g_c310.deferred;
    } else {
        c310_send_values(pc, v, rows, failed);
    }
    log::line("%scard310: host t=%llu map rows applied to the new copy pawn %s of %s from %s: %d tag(s), chargeable "
              "%d, default class %s; %s%d client row(s) sent, %d not sent",
              failed > 0 ? "WARN " : "", static_cast<unsigned long long>(now_ms()), object_name(pawn).text,
              object_name(pc).text, v.from_host ? "the host pawn" : "the journal (no live host pawn)",
              g_c310.journal.tags, v.chargeable, object_name(v.cls).text,
              held ? "client rows deferred: not ponged yet (card 139), sent at its first Pong; " : "", rows, failed);
}

// Host (card 139 join replay, from sync_join_replay): a copy whose client rows were held before its first Pong gets
// the current set now. A row still refused here is the WARN.
void c310_join_replay(void* pc) {
    void* const world = pc != nullptr ? g_actor_get_world(pc) : nullptr;
    for (int s = 0; s < kC17Slots && world != nullptr; ++s) {
        C310Slot& slot = g_c310.slots[s];
        if (slot.deferred_pc != pc) {
            continue;
        }
        slot.deferred_pc = nullptr;
        const C310Values v = c310_values(world);
        int32_t rows = 0;
        int32_t failed = 0;
        c310_send_values(pc, v, rows, failed);
        log::line("%scard310: host t=%llu join rows to %s at its first Pong (card 139): %d tag(s), chargeable %d, "
                  "default class %s; %d client row(s) sent, %d not sent",
                  failed > 0 ? "WARN " : "", static_cast<unsigned long long>(now_ms()), object_name(pc).text,
                  g_c310.journal.tags, v.chargeable, object_name(v.cls).text, rows, failed);
    }
}

// Host, one player slot per poll: a new pawn is noted (a copy's gets the map's rows); a changed default class on a
// known pawn is a level script write on it.
void c310_poll_slot(void* world, int s) {
    void* const pc = g_get_player_controller(world, s);
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    C310Slot& slot = g_c310.slots[s];
    if (pawn == nullptr || !c267_source(pawn)) {
        return;
    }
    const int32_t id = object_index(pawn);
    if (id != slot.pawn_id) {
        const C310Journal& j = g_c310.journal;
        const bool rows = j.tags > 0 || j.chargeable >= 0 || j.default_class != nullptr;
        if (rows && sprint_side(pawn) == kSideServerCopy) {
            c310_apply_journal_to_copy(world, s, pc, pawn);
        }
        slot.pawn_id = id;
        slot.default_class = c310_default_class(pawn);
        return;
    }
    void* const cls = c310_default_class(pawn);
    if (cls == slot.default_class) {
        return;
    }
    c310_sync_default_class(world, pawn, cls);
    for (int o = 0; o < kC17Slots; ++o) { // every known pawn now holds cls: the next poll sees no change on them
        g_c310.slots[o].default_class = o == s || g_c310.slots[o].pawn_id >= 0 ? cls : g_c310.slots[o].default_class;
    }
}

// Host, each tick from c50_step.
void c310_host_step(void* world, ULONGLONG now) {
    if (world == nullptr || now < g_c310.next_poll_ms) {
        return;
    }
    g_c310.next_poll_ms = now + kC310PollMs;
    for (int s = 0; s < kC17Slots; ++s) {
        c310_poll_slot(world, s);
    }
}

// --- Client: rows applied on the own pawn, kept, and applied again on a new own pawn ---

// One row on the own pawn. Returns false when it cannot apply (logged by the caller).
bool c310_client_apply(const void* pawn, char kind, const char* value, char* result, size_t cap) {
    if (kind == 'T') {
        char name[kC310ArgsText] = {};
        int32_t want = -1;
        const uint64_t tag = sscanf_s(value, "%191s %d", name, static_cast<unsigned>(sizeof(name)), &want) == 2
                                 ? c71_find_tag(name)
                                 : 0;
        const int32_t before = tag != 0 && want >= 0 ? c310_set_tag_count(pawn, tag, want) : -1;
        snprintf(result, cap, "%s count %d to %d", name, before, before >= 0 ? want : -1);
        return before >= 0;
    }
    if (kind == 'C') {
        void* const comp = c310_tac_component(pawn);
        if (comp != nullptr && g_orig_c310_set_charge != nullptr) {
            g_orig_c310_set_charge(comp, value[0] == '1');
        }
        snprintf(result, cap, "chargeable %c%s", value[0], comp != nullptr ? "" : ", no component");
        return comp != nullptr;
    }
    if (kind == 'D') {
        void* const cls = strcmp(value, "None") == 0 ? nullptr : c50_resolve(value);
        const bool ok = (cls != nullptr || strcmp(value, "None") == 0) && c310_set_default_class(pawn, cls);
        snprintf(result, cap, "default class %s%s", value, ok ? "" : " not set");
        return ok;
    }
    snprintf(result, cap, "unknown row kind %c", kind);
    return false;
}

// Keeps a received row in the client's journal (the same tables as the host's).
void c310_client_keep(char kind, const char* value) {
    C310Journal& j = g_c310.journal;
    if (kind == 'T') {
        char name[kC310ArgsText] = {};
        int32_t want = -1;
        const uint64_t tag = sscanf_s(value, "%191s %d", name, static_cast<unsigned>(sizeof(name)), &want) == 2
                                 ? c71_find_tag(name)
                                 : 0;
        int32_t* const kept = tag != 0 ? c310_journal_tag(j, tag) : nullptr;
        if (kept != nullptr) {
            *kept = want;
        }
    } else if (kind == 'C') {
        j.chargeable = value[0] == '1' ? 1 : 0;
    } else if (kind == 'D') {
        j.default_class = strcmp(value, "None") == 0 ? nullptr : c50_resolve(value);
        j.default_name = object_fname(j.default_class);
    }
}

void c310_receive(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    const int32_t len = chan_text(s, text, sizeof(text));
    ++g_c310.received;
    const void* const pawn = read_ptr(pc, kControllerPawn);
    if (len < 3 || text[1] != ' ') {
        log::line("WARN card310: client t=%llu ScriptWrite row not parsed", static_cast<unsigned long long>(now_ms()));
        return;
    }
    c310_client_keep(text[0], text + 2);
    char result[kC310PathText + 32];
    const bool ok = pawn != nullptr && c310_client_apply(pawn, text[0], text + 2, result, sizeof(result));
    g_c310.applied += ok ? 1 : 0;
    // Only a row that applied marks the pawn: on a failed apply c310_client_step tries the kept rows again.
    g_c310.own_pawn_id = ok ? object_index(pawn) : g_c310.own_pawn_id;
    log::line("%scard310: client t=%llu %s applied on the own pawn %s: %s", ok ? "" : "WARN ",
              static_cast<unsigned long long>(now_ms()), text, object_name(pawn).text,
              pawn != nullptr ? result : "no own pawn, kept for it");
}

// Client, each tick from c50_step: the kept rows on a new own pawn (respawn, or rows that came before the pawn).
void c310_client_step(void* pc) {
    const C310Journal& j = g_c310.journal;
    const void* const pawn = read_ptr(pc, kControllerPawn);
    const bool rows = j.tags > 0 || j.chargeable >= 0 || j.default_class != nullptr;
    if (pawn == nullptr || !rows || object_index(pawn) == g_c310.own_pawn_id || !is_a(pawn, g_player_mi_class())) {
        return;
    }
    g_c310.own_pawn_id = object_index(pawn);
    char result[kC310PathText + 32];
    int32_t ok = 0;
    for (int32_t i = 0; i < j.tags; ++i) {
        char value[kC310ArgsText];
        snprintf(value, sizeof(value), "%s %d", fname_text(&j.tag[i]).text, j.count[i]);
        ok += c310_client_apply(pawn, 'T', value, result, sizeof(result)) ? 1 : 0;
    }
    ok += j.chargeable >= 0 && c310_client_apply(pawn, 'C', j.chargeable == 1 ? "1" : "0", result, sizeof(result));
    char path[kC310PathText];
    const bool named =
        j.default_class != nullptr && c50_class_path(j.default_class, path, static_cast<int32_t>(sizeof(path))) > 0;
    ok += named && c310_client_apply(pawn, 'D', path, result, sizeof(result)) ? 1 : 0;
    const int32_t all = j.tags + (j.chargeable >= 0 ? 1 : 0) + (j.default_class != nullptr ? 1 : 0);
    // The pawn is marked either way (no retry each tick), so a row that did not apply must be seen.
    log::line("%scard310: client t=%llu kept rows applied on the new own pawn %s: %d of %d", ok == all ? "" : "WARN ",
              static_cast<unsigned long long>(now_ms()), object_name(pawn).text, ok, all);
}

// Map end, both sides (ubw_load_map_enter, before LoadMap tears the old world down): the totals, then a clean state
// for the next map; the old world's level script is gated from here on.
void c310_map_end(void* old_world) {
    const C310State& c = g_c310;
    if (c.writes + c.sent + c.received != 0) {
        const bool bad = g_client_session ? c.received != c.applied : c.not_sent != 0;
        log::line("%scard310: %s map totals: writes %u, mirrored %u, client rows sent %u (not sent %u, deferred to "
                  "a first Pong %u), received %u applied %u",
                  bad ? "WARN " : "", side(), c.writes, c.mirrored, c.sent, c.not_sent, c.deferred, c.received,
                  c.applied);
    }
    (void)old_world; // card 332: the teardown state is set by sync_map_end
    g_c310 = {};
    g_c310.journal.chargeable = -1;
    g_c310.own_pawn_id = -1;
    for (int s = 0; s < kC17Slots; ++s) {
        g_c310.slots[s].pawn_id = -1;
    }
}

// Map arm, both sides: what the new world's load journalled before the arm (the rows kept for the arm), and the old
// world's gate is lifted by the registry (sync_map_arm ends the card 332 teardown state). Clears no state table.
void c310_arm_map() {
    const C310Journal& j = g_c310.journal;
    log::line("card310: %s t=%llu map arm: journal from the load: %d tag(s), chargeable %d, default class %s; "
              "writes %u, dropped from the world being left %u",
              side(), static_cast<unsigned long long>(now_ms()), j.tags, j.chargeable,
              j.default_class != nullptr ? fname_text(&j.default_name).text : "null",
              g_c310.writes, g_c310.teardown);
    g_c310.teardown = 0; // counted only between map end and the arm
}

// Card 326: the map's journal rows (tags, the charge flag, the default class).
int32_t c310_live() {
    const C310Journal& j = g_c310.journal;
    return j.tags + (j.chargeable >= 0 ? 1 : 0) + (j.default_class != nullptr ? 1 : 0);
}

} // namespace ubw::hooks
