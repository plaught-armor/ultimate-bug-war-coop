// Grenades: where a client's grenade lives (card 073) and each side's grenade throws (card 074).

#include "ubw/hooks_internal.h"

#include <math.h>
#include <string.h>

namespace ubw::hooks {

constexpr const char* kC73HookNames[kC73Hooks] = {"AddAbilityToActor", "AddInventoryItem_Internal",
                                                  "RemoveItemByDefinition", "ConsumeItem"};
constexpr uint32_t kC73Logs = 30;
constexpr size_t kItemInvItems = 0xD8;
constexpr size_t kItemInvNum = 0xE0;
constexpr size_t kItemInvEntryBytes = 0x10;
constexpr size_t kBindingAbility = 0x0;
constexpr size_t kBindingTags = 0x8;
constexpr size_t kBindingTagNum = 0x10;
constexpr size_t kSpecLevel = 0x18;
constexpr size_t kSpecInputId = 0x1C;
constexpr int32_t kC73MaxItems = 8;
constexpr size_t kC73ItemText = 2 * kNameChars + 16;
constexpr int32_t kC73MaxSpecs = 64;

constexpr int kC73MaxSupers = 12;
constexpr int kC73DumpFields = 32;

C73AddAbilityFn g_orig_c73_ability = nullptr;
C73AddItemFn g_orig_c73_add = nullptr;
C73RemoveItemFn g_orig_c73_remove = nullptr;
C73ConsumeFn g_orig_c73_consume = nullptr;

C73State g_c73{};

constexpr size_t kSpecReplicatedInstances = 0x78; // FGameplayAbilitySpec::GetPrimaryInstance: TArray +0x78 / Num +0x80
constexpr size_t kSpecReplicatedNum = 0x80;
constexpr size_t kSpecLocalInstances = 0x88;      // NonReplicatedInstances +0x88 / Num +0x90
constexpr size_t kSpecLocalNum = 0x90;
constexpr size_t kItemOwningInventory = 0x48;     // USTInventoryItem::OwningInventory (reflected, card 073 dump)
C73Grant g_c73_wait[kC73Waits]{}; // client, game thread
uint32_t g_c73_wait_peak = 0;     // card 210: most waits held at once this process, logged at each new peak
C73Pending g_c73_pending[kC73Pending]{}; // client, game thread
uint32_t g_c73_pending_count = 0;
uint32_t g_c73_pending_peak = 0;

C73FindItemFn g_c73_find_item = nullptr; // USTItemInventory::FindItemByDefinition (const)
C73AddItemFn g_c73_add_item = nullptr;   // USTItemInventory::AddItemByDefinition: same shape as AddInventoryItem_Internal

C73UnbindFn g_c73_unbind = nullptr;      // UADAbilitySystemComponent::RemoveInputTagBindingForAbility

bool c73_note(C73Hook h) {
    ++g_c73.calls[h];
    return g_c73.calls[h] <= kC73Logs;
}

// True when `cls` or a super is named `name` (the UClass name, no U/A prefix).
bool c73_class_is(const void* cls, const char* name) {
    for (int depth = 0; depth < kC73MaxSupers && cls != nullptr; ++depth, cls = read_ptr(cls, kStructSuper)) {
        if (strcmp(object_name(cls).text, name) == 0) {
            return true;
        }
    }
    return false;
}

// An item's Quantity (reflected), or -1.
int32_t c73_quantity(const void* item) {
    const uint8_t* const prop = item != nullptr ? prop_find_prop(read_ptr(item, kObjectClass), "Quantity") : nullptr;
    return prop != nullptr ? read_i32(item, static_cast<size_t>(read_i32(prop, kPropertyOffset))) : -1;
}

// The pawn's USTItemInventory: its reflected ItemInventory object property, or null.
void* c73_item_inventory(const void* pawn) {
    const uint8_t* const prop = pawn != nullptr ? prop_find_prop(read_ptr(pawn, kObjectClass), "ItemInventory") : nullptr;
    return prop != nullptr ? read_ptr(pawn, static_cast<size_t>(read_i32(prop, kPropertyOffset))) : nullptr;
}

// Once per process per class: every reflected property of `object` (own and inherited) as name/kind/offset.
void c73_dump_props(const char* what, const void* object) {
    const void* cls = read_ptr(object, kObjectClass);
    int shown = 0;
    for (int depth = 0; depth < kC73MaxSupers && cls != nullptr; ++depth, cls = read_ptr(cls, kStructSuper)) {
        const void* f = read_ptr(cls, kStructChildProperties);
        for (int i = 0; i < kPropMaxFields && f != nullptr && shown < kC73DumpFields; ++i, f = read_ptr(f, kFieldNext)) {
            ++shown;
            log::line("card073: %s %s %s.%s %s offset 0x%X size %d", side_text(), what, object_name(cls).text,
                      fname_text(static_cast<const uint8_t*>(f) + kFieldName).text,
                      fname_text(read_ptr(f, kFieldClassPrivate)).text, read_i32(f, kPropertyOffset),
                      read_i32(f, kPropertyElementSize));
        }
    }
}

// "<binding ability class> tags [a b]" of an FADAbilityToInputTags.
void c73_binding_text(const void* binding, char* out, size_t cap) {
    const auto* const tags = static_cast<const uint8_t*>(read_ptr(binding, kBindingTags));
    const int32_t n = read_i32(binding, kBindingTagNum);
    int w = snprintf(out, cap, "%s tags %d [", object_name(read_ptr(binding, kBindingAbility)).text, n);
    size_t used = w > 0 ? static_cast<size_t>(w) : cap;
    for (int32_t i = 0; i < n && i < kC73MaxTags && tags != nullptr && used < cap; ++i) {
        w = snprintf(out + used, cap - used, " %s", fname_text(tags + static_cast<size_t>(i) * sizeof(uint64_t)).text);
        used += w > 0 ? static_cast<size_t>(w) : cap;
    }
    if (used < cap) {
        snprintf(out + used, cap - used, " ]");
    }
}

void c73_log_ability(const int32_t* ret, void* actor, const void* binding, void* source, const void* ra) {
    char text[192];
    c73_binding_text(binding, text, sizeof(text));
    log::line("card073: %s AddAbilityToActor #%u actor %s (%s), ability %s, source %s (%s), handle %d, caller exe+0x%llX",
              side_text(), g_c73.calls[kC73Ability], object_name(actor).text, class_name(actor).text, text,
              object_name(source).text, class_name(source).text, ret != nullptr ? read_i32(ret, 0) : -2, exe_rva(ra));
}

// The spec's primary ability instance (GetPrimaryInstance without its instancing policy test), or null.
void* c73_spec_instance(const uint8_t* spec) {
    if (read_i32(spec, kSpecReplicatedNum) > 0) {
        return read_ptr(read_ptr(spec, kSpecReplicatedInstances), 0);
    }
    return read_i32(spec, kSpecLocalNum) > 0 ? read_ptr(read_ptr(spec, kSpecLocalInstances), 0) : nullptr;
}

// Client: CanActivateAbility of an item ability (USTInventoryItemGameplayAbility) needs its SourceItem live with
// Quantity > 0. OnGiveAbility sets it from the spec's SourceObject, which on the client is null (items do not
// replicate), so our own item goes there. A UPROPERTY: GC keeps the item alive and clears the reference.
// False while the spec has no instance yet and its ability class has a SourceItem (the caller keeps waiting); true once
// set or when nothing can be set.
bool c73_point_item(void* pawn, const uint8_t* spec, void* item) {
    auto* const instance = static_cast<uint8_t*>(c73_spec_instance(spec));
    if (instance == nullptr) {
        // Card 210: an instanced-per-execution ability (a bug form's Swoop) has no instance until it runs; its class
        // tells whether a SourceItem is ever needed. Without one it binds now; with one it keeps waiting.
        const void* const cdo = read_ptr(spec, kSpecAbility);
        return cdo != nullptr && prop_find_prop(read_ptr(cdo, kObjectClass), "SourceItem") == nullptr;
    }
    const uint8_t* const prop = prop_find_prop(read_ptr(instance, kObjectClass), "SourceItem");
    if (prop == nullptr) {
        return true; // card 196: not an item ability (e.g. a bug form's owner-equipped feature); its tags bind as is
    }
    const bool ours = item != nullptr && read_ptr(item, kItemOwningInventory) == c73_item_inventory(pawn);
    if (!ours) {
        log::line("WARN card073: client t=%llu %s: the grant's source is not our item, SourceItem not set",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(instance).text);
        return true;
    }
    const size_t at = static_cast<size_t>(read_i32(prop, kPropertyOffset));
    memcpy(instance + at, &item, sizeof(item));
    log::line("card073: client t=%llu %s SourceItem (+0x%zX) = %s qty %d",
              static_cast<unsigned long long>(GetTickCount64()), object_name(instance).text, at,
              object_name(item).text, c73_quantity(item));
    return true;
}

// Client: the grant's input tags bound to the replicated spec of its ability class on the pawn's ASC. False (nothing
// bound) while that spec has not replicated yet; `out` (when given) gets the spec's handle.
bool c73_bind(void* pawn, const C73Grant& g, int32_t* out) {
    void* const asc = pawn != nullptr ? g_get_asc(pawn, true) : nullptr;
    void* cls = g.ability;
    const auto* const spec =
        asc != nullptr && cls != nullptr ? static_cast<const uint8_t*>(g_find_spec_from_class(asc, &cls)) : nullptr;
    if (spec == nullptr || !c73_point_item(pawn, spec, g.item)) {
        return false;
    }
    for (int32_t t = 0; t < g.tags_num; ++t) {
        detour_bind_input_tag(asc, &g.tags[t], spec + kSpecHandle); // logs each bind (card 017 cap)
    }
    if (out != nullptr) {
        *out = read_i32(spec, kSpecHandle);
    }
    c158_note_ability(read_ptr(spec, kSpecAbility)); // card 158: the client spawns its own held grenade
    log::line("card073: client t=%llu grant %s bound to replicated spec h%d (%d tags), input tag map Num %d",
              static_cast<unsigned long long>(GetTickCount64()), object_name(cls).text, read_i32(spec, kSpecHandle),
              g.tags_num, read_i32(asc, kAscInputTagMapNum));
    return true;
}

// Card 351: true while the inventory's item array holds `item` (pointer compare only: a removed item may be gone).
bool c73_inventory_holds(const void* inventory, const void* item) {
    const auto* const data = inventory != nullptr ? static_cast<const uint8_t*>(read_ptr(inventory, kItemInvItems)) : nullptr;
    const int32_t n = data != nullptr ? read_i32(inventory, kItemInvNum) : 0;
    for (int32_t i = 0; i < n && i < kC73MaxSpecs; ++i) {
        if (read_ptr(data, static_cast<size_t>(i) * kItemInvEntryBytes) == item) {
            return true;
        }
    }
    return false;
}

// Client: a free wait slot; when all kC73Waits are held, the oldest wait's slot, its grant dropped with a WARN (card
// 210: a bug form grants five abilities at once, before their specs replicate). Each new peak of held waits is logged.
C73Grant* c73_wait_slot() {
    C73Grant* free_slot = nullptr;
    C73Grant* oldest = nullptr;
    uint32_t held = 0;
    for (C73Grant& w : g_c73_wait) {
        if (w.ability == nullptr) {
            free_slot = free_slot != nullptr ? free_slot : &w;
            continue;
        }
        ++held;
        oldest = oldest == nullptr || w.since_ms < oldest->since_ms ? &w : oldest;
    }
    if (free_slot == nullptr) {
        log::line("WARN card073: client t=%llu wait table full (%d): grant %s dropped, input not bound",
                  static_cast<unsigned long long>(GetTickCount64()), kC73Waits, object_name(oldest->ability).text);
        *oldest = C73Grant{};
        return oldest;
    }
    if (held + 1 > g_c73_wait_peak) {
        g_c73_wait_peak = held + 1;
        log::line("card073: client grant waits peak %u of %d", g_c73_wait_peak, kC73Waits);
    }
    return free_slot;
}

// Client own pawn: GiveAbility is server-only ("GiveAbility called on ability ... on the client, not allowed!"), so the
// original is not called. The server copy's grant replicates as a spec; its input tags are bound to that spec here,
// or once it arrives (c73_client_step). The handle is INDEX_NONE until then.
int32_t* c73_client_grant(int32_t* out, void* actor, const void* binding, void* source) {
    C73Grant g{};
    g.ability = read_ptr(binding, kBindingAbility);
    g.item = source;
    const auto* const tags = static_cast<const uint8_t*>(read_ptr(binding, kBindingTags));
    const int32_t n = tags != nullptr ? read_i32(binding, kBindingTagNum) : 0;
    g.tags_num = n < 0 ? 0 : (n < kC73MaxTags ? n : kC73MaxTags);
    memcpy(g.tags, tags, static_cast<size_t>(g.tags_num) * sizeof(uint64_t));
    g.since_ms = GetTickCount64();
    *out = -1;
    if (g.ability == nullptr || c73_bind(actor, g, out)) {
        return out;
    }
    *c73_wait_slot() = g;
    log::line("card073: client t=%llu grant %s waits for its replicated spec", static_cast<unsigned long long>(g.since_ms),
              object_name(g.ability).text);
    return out;
}

// Always detoured (kHooksUbw). Logs only with UBWCOOP_C73_LOG=1.
int32_t* detour_c73_ability(int32_t* out, void* actor, const void* binding, void* source) {
    const bool own_client = g_client_session && binding != nullptr && sprint_side(actor) == kSideClientOwn;
    int32_t* const ret = own_client ? c73_client_grant(out, actor, binding, source) : g_orig_c73_ability(out, actor, binding, source);
    if (g_c73_log && c73_note(kC73Ability)) {
        c73_log_ability(ret, actor, binding, source, _ReturnAddress());
    }
    return ret;
}

void c73_log_item(C73Hook h, const void* inventory, void* const* definition, int32_t amount, const void* item,
                  const void* ra) {
    const void* const owner = inventory != nullptr ? read_ptr(inventory, kComponentOwner) : nullptr;
    log::line("card073: %s %s #%u inventory %p owner %s (%s), definition %s, amount %d, item %s quantity %d, "
              "caller exe+0x%llX",
              side_text(), kC73HookNames[h], g_c73.calls[h], inventory, object_name(owner).text, class_name(owner).text,
              object_name(definition != nullptr ? *definition : nullptr).text, amount, class_name(item).text,
              c73_quantity(item), exe_rva(ra));
}

// The inventory's quantity of the definition (FindItemByDefinition), 0 when it holds none.
int32_t c73_count(void* inventory, void* definition) {
    const void* const item = inventory != nullptr && definition != nullptr ? g_c73_find_item(inventory, &definition)
                                                                           : nullptr;
    const int32_t q = c73_quantity(item);
    return q > 0 ? q : 0;
}

// One GrenadeItem text frame "<definition class path> <count>" to pc; `text` gets the frame text, `whole` false when a
// name would be cut (nothing sent then).
bool c73_send_item(void* pc, void* definition, int32_t count, char (&text)[kC73ItemText], bool* whole) {
    const ObjectName package = object_name(read_ptr(definition, kObjectOuter));
    const ObjectName cls = object_name(definition);
    snprintf(text, sizeof(text), "%s.%s %d", package.text, cls.text, count);
    *whole = strlen(package.text) + 1 < kNameChars && strlen(cls.text) + 1 < kNameChars;
    static wchar_t wire[kChanTextFrame];
    const WireString w = *whole && pc != nullptr ? chan_text_frame(ChanOp::GrenadeItem, text, wire) : WireString{wire, 0, 0};
    return w.num > 0 && chan_send_client(pc, w);
}

// Host: after an item change on a remote player's server copy, that client gets one GrenadeItem text frame,
// "<definition class path> <quantity>", the copy's quantity after the change (0 = none left).
void c73_relay(void* inventory, void* const* definition, const char* what) {
    void* const owner = inventory != nullptr ? read_ptr(inventory, kComponentOwner) : nullptr;
    if (g_client_session || definition == nullptr || *definition == nullptr || sprint_side(owner) != kSideServerCopy) {
        return;
    }
    void* const pc = read_ptr(owner, kPawnController);
    char text[kC73ItemText];
    bool whole = false;
    const int32_t count = c73_count(inventory, *definition);
    c446_note_item(pc, owner, *definition, count); // card 446
    const bool sent = c73_send_item(pc, *definition, count, text, &whole);
    // Card 351: the copy's spec count after the change, so a grant its item's removal took back shows on the host.
    void* const asc = g_get_asc(owner, true);
    log::line("%scard073: host t=%llu %s on server copy %s (%s): GrenadeItem %s: %s, ActivatableAbilities %d",
              sent ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), what, object_name(owner).text,
              pc != nullptr ? object_name(pc).text : "no PC", sent ? "sent" : (whole ? "not sent" : "name cut, not sent"),
              text, asc != nullptr ? read_i32(asc, kAscAbilityNum) : -1);
}

// Card 446, host: a server copy's count of one definition set to the count its gone controller's copy had. The raise
// goes through AddItemByDefinition (detour_c73_add relays it). The lower calls the game's remove direct and relays
// here: detour_c73_remove would take it for a level script's removal and apply it to every player (card 310).
void c446_set_count(void* inventory, void* definition, int32_t want) {
    const int32_t before = c73_count(inventory, definition);
    if (want > before && g_c73_add_item != nullptr) {
        g_c73_add_item(inventory, &definition, want - before);
    } else if (want < before && g_orig_c73_remove != nullptr) {
        g_orig_c73_remove(inventory, &definition, before - want, want == 0);
        c73_relay(inventory, &definition, "card 446 kept count");
    }
    const int32_t after = c73_count(inventory, definition);
    log::line("%scard446: host t=%llu kept item count %s %d on %s: %d to %d", after == want ? "" : "WARN ",
              static_cast<unsigned long long>(GetTickCount64()), object_name(definition).text, want,
              object_name(read_ptr(inventory, kComponentOwner)).text, before, after);
}

// Card 353: in a client session the host's GrenadeItem frame is the only writer of our own pawn's item counts. The
// client's own grenade throw (USTGrenadeThrowGameplayAbility::OnDoAction_Implementation, caller exe+0x5163343) and
// satchel placement (ASTSatchelPlacementSite::OnInteraction, caller exe+0x5163EBB) consume one item locally too, and
// the server copy runs the same consume and sends its absolute count; a frame landing before the local consume made
// the throw count twice (2 to 1 by the frame, 1 to 0 locally, the next item equipped). Both callers discard the
// result, and the consume is their only inventory write (the throw goes on to FireProjectile, card 286 releases that
// launch; its "own count" is card 132's launch table, not the inventory), so the local removal is not run.
constexpr uint32_t kC353Lines = 40;
constexpr uint64_t kC353ThrowCaller = 0x5163343;   // USTGrenadeThrowGameplayAbility::OnDoAction_Implementation +0xb3
constexpr uint64_t kC353SatchelCaller = 0x5163EBB; // ASTSatchelPlacementSite::OnInteraction +0xbb
uint32_t g_c353_lines = 0;
uint32_t g_c353_skipped = 0;
bool g_c353_host_write = false; // client, game thread: c73_apply's lowering or raise (card 354) is the host frame's write

// Client: true when the removal is our own pawn's local consume (skipped); logs one row per skip.
bool c353_skip_local(void* inventory, void* const* definition, int32_t amount, const void* ra) {
    const void* const owner = inventory != nullptr ? read_ptr(inventory, kComponentOwner) : nullptr;
    if (!g_client_session || g_c353_host_write || owner == nullptr || sprint_side(owner) != kSideClientOwn) {
        return false;
    }
    ++g_c353_skipped;
    // The two traced callers discard the result. Any other caller is skipped by the same rule (one writer), but its use
    // of the result and whether the server copy runs it too are not traced: a WARN row names it.
    const uint64_t rva = exe_rva(ra);
    const bool traced = rva == kC353ThrowCaller || rva == kC353SatchelCaller;
    if (note_event_cap(g_c353_lines, kC353Lines, "card353 local consume skips")) {
        log::line("%scard353: client t=%llu own %s RemoveItemByDefinition amount %d (caller exe+0x%llX%s): local consume "
                  "not run, count stays %d; the host frame writes it, %u skipped so far",
                  traced ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()),
                  object_name(definition != nullptr ? *definition : nullptr).text, amount, rva,
                  traced ? "" : ", caller not traced",
                  definition != nullptr ? c73_count(inventory, *definition) : -1, g_c353_skipped);
    }
    return true;
}

void* detour_c73_remove(void* inventory, void* const* definition, int32_t amount, bool flag) {
    if (c353_skip_local(inventory, definition, amount, _ReturnAddress())) {
        return definition != nullptr ? g_c73_find_item(inventory, definition) : nullptr; // the item, still held
    }
    void* const item = g_orig_c73_remove(inventory, definition, amount, flag);
    if (g_c73_log && c73_note(kC73Remove)) {
        c73_log_item(kC73Remove, inventory, definition, amount, item, _ReturnAddress());
    }
    c73_relay(inventory, definition, "RemoveItemByDefinition");
    c310_note_item_remove(inventory, definition, amount, flag); // card 310: a level script's removal on every player
    return item;
}

// Card 354: the same one-writer rule for adds. AddItemByDefinition's direct callers: ASTItemPickup::Pickup_Implementation
// (exe+0x51D3FA4) and ASTPlayerCharacterMI::ResetInventory, the loadout loop (exe+0x51DA8EE), both of which the server
// copy also runs and sends (card073 host rows 6-12 ms after the client's), and execAddItemByDefinition (exe+0x5072667),
// a Blueprint grant not traced to the server copy. The two traced callers discard the result and are not run locally;
// any other caller keeps running and logs a WARN naming it. A frame landing before the local add left the count 1 over.
constexpr uint32_t kC354Lines = 40;
constexpr uint64_t kC354PickupCaller = 0x51D3FA4; // ASTItemPickup::Pickup_Implementation +0xb4
constexpr uint64_t kC354LoadoutCaller = 0x51DA8EE; // ASTPlayerCharacterMI::ResetInventory +0x4be
constexpr uint64_t kC354ByDefinitionCaller = 0x514AE7B; // AddItemByDefinition's own call of AddInventoryItem_Internal
uint32_t g_c354_lines = 0;
uint32_t g_c354_skipped = 0;
C73AddItemFn g_orig_c354_add = nullptr;

// Client: our own pawn's inventory, an add the host frame does not write.
bool c354_own_local(void* inventory) {
    const void* const owner = inventory != nullptr ? read_ptr(inventory, kComponentOwner) : nullptr;
    return g_client_session && !g_c353_host_write && owner != nullptr && sprint_side(owner) == kSideClientOwn;
}

void c354_log(void* inventory, void* const* definition, int32_t amount, uint64_t rva, bool skipped, const char* what) {
    if (note_event_cap(g_c354_lines, kC354Lines, "card354 local add rows")) {
        log::line("%scard354: client t=%llu own %s %s amount %d (caller exe+0x%llX%s): %s, count %d%s",
                  skipped ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()),
                  object_name(definition != nullptr ? *definition : nullptr).text, what, amount, rva,
                  skipped ? "" : ", caller not traced", skipped ? "local add not run" : "local add run",
                  definition != nullptr ? c73_count(inventory, *definition) : -1,
                  skipped ? "; the host frame writes it" : "");
    }
}

// Always detoured (kHooksUbw). Client: the pickup and loadout adds on our own pawn are not run.
void* detour_c354_add(void* inventory, void* const* definition, int32_t amount) {
    if (c354_own_local(inventory)) {
        const uint64_t rva = exe_rva(_ReturnAddress());
        const bool traced = rva == kC354PickupCaller || rva == kC354LoadoutCaller;
        if (traced) {
            ++g_c354_skipped;
            c354_log(inventory, definition, amount, rva, true, "AddItemByDefinition");
            return definition != nullptr ? g_c73_find_item(inventory, definition) : nullptr; // both callers discard it
        }
        c354_log(inventory, definition, amount, rva, false, "AddItemByDefinition");
    }
    return g_orig_c354_add(inventory, definition, amount);
}

// Always detoured (kHooksUbw): the host relays a server copy's change; logs only with UBWCOOP_C73_LOG=1.
void* detour_c73_add(void* inventory, void* const* definition, int32_t amount) {
    if (c354_own_local(inventory)) {
        const uint64_t rva = exe_rva(_ReturnAddress());
        if (rva != kC354ByDefinitionCaller) { // card 354: AddItemByTag or LoadInventory, not traced to a host frame
            c354_log(inventory, definition, amount, rva, false, "AddInventoryItem_Internal");
        }
    }
    void* const item = g_orig_c73_add(inventory, definition, amount);
    if (g_c73_log && c73_note(kC73Add)) {
        c73_log_item(kC73Add, inventory, definition, amount, item, _ReturnAddress());
    }
    c73_relay(inventory, definition, "AddInventoryItem_Internal");
    return item;
}

// Client: the host's GrenadeItem frame. Our own pawn's quantity of that definition is raised (AddItemByDefinition) or
// lowered (RemoveItemByDefinition, removing the item at 0) to the server copy's. Absolute, so a throw this client
// already counted locally is not taken twice. The item's own equip grants its ability: c73_client_grant binds it.
// Our pawn's count of one definition set to the host's (raise by AddItemByDefinition, lower by RemoveItemByDefinition).
void c73_apply(void* inventory, void* definition, int32_t want, const char* what) {
    const int32_t before = c73_count(inventory, definition);
    if (want > before) {
        g_c353_host_write = true; // card 354: detour_c354_add passes the host frame's raise
        g_c73_add_item(inventory, &definition, want - before);
        g_c353_host_write = false;
    } else if (want < before) {
        g_c353_host_write = true;
        detour_c73_remove(inventory, &definition, before - want, want == 0);
        g_c353_host_write = false;
    }
    const int32_t after = c73_count(inventory, definition);
    log::line("card073: client t=%llu GrenadeItem %s %d%s: our count %d to %d%s",
              static_cast<unsigned long long>(GetTickCount64()), object_name(definition).text, want, what, before,
              after, after != before ? ", writer the host frame" : "");
}

// A frame that came while our pawn had no item inventory (the snapshot replay right after a respawn in place): the
// latest wanted count per definition, applied by c73_pending_step once the new pawn's inventory exists.
void c73_defer(void* definition, int32_t want) {
    const ULONGLONG now = GetTickCount64();
    uint32_t i = 0;
    while (i < g_c73_pending_count && g_c73_pending[i].definition != definition) {
        ++i;
    }
    if (i == g_c73_pending_count) {
        if (g_c73_pending_count >= kC73Pending) {
            log::line("WARN card073: client t=%llu GrenadeItem %s %d: pending table full (%u), frame dropped",
                      static_cast<unsigned long long>(now), object_name(definition).text, want, kC73Pending);
            return;
        }
        ++g_c73_pending_count;
        if (g_c73_pending_count > g_c73_pending_peak) {
            g_c73_pending_peak = g_c73_pending_count;
            log::line("card073: client pending GrenadeItem peak %u of %u", g_c73_pending_peak, kC73Pending);
        }
        g_c73_pending[i].since_ms = now;
    }
    g_c73_pending[i].definition = definition;
    g_c73_pending[i].want = want;
    log::line("card073: client t=%llu GrenadeItem %s %d: no item inventory on our pawn yet, applied when it has one",
              static_cast<unsigned long long>(now), object_name(definition).text, want);
}

// Client tick: pending counts applied once our pawn has its item inventory; after kC73WaitMs one is dropped (WARN).
void c73_pending_step(void* pc, ULONGLONG now) {
    if (g_c73_pending_count == 0) {
        return;
    }
    void* const inventory = pc != nullptr ? c73_item_inventory(read_ptr(pc, kControllerPawn)) : nullptr;
    uint32_t kept = 0;
    for (uint32_t i = 0; i < g_c73_pending_count; ++i) {
        const C73Pending p = g_c73_pending[i];
        if (inventory != nullptr) {
            char what[48];
            snprintf(what, sizeof(what), " (deferred %llu ms)", static_cast<unsigned long long>(now - p.since_ms));
            c73_apply(inventory, p.definition, p.want, what);
        } else if (now - p.since_ms >= kC73WaitMs) {
            log::line("WARN card073: client t=%llu GrenadeItem %s %d: still no item inventory on our pawn after %llu "
                      "ms, count not applied", static_cast<unsigned long long>(now), object_name(p.definition).text,
                      p.want, static_cast<unsigned long long>(kC73WaitMs));
        } else {
            g_c73_pending[kept++] = p;
        }
    }
    g_c73_pending_count = kept;
}

void c73_receive(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    char path[2 * kNameChars + 2];
    int32_t want = -1;
    if (chan_text(s, text, sizeof(text)) <= 0 ||
        sscanf_s(text, "%129s %d", path, static_cast<unsigned>(sizeof(path)), &want) != 2 || want < 0) {
        log::line("WARN card073: client: bad GrenadeItem frame");
        return;
    }
    wchar_t wide[sizeof(path)];
    for (size_t i = 0; i < sizeof(path); ++i) {
        wide[i] = static_cast<wchar_t>(path[i]);
    }
    void* const definition = find_class(wide);
    if (definition == nullptr) {
        log::line("WARN card073: client t=%llu GrenadeItem %s: no such class",
                  static_cast<unsigned long long>(GetTickCount64()), text);
        return;
    }
    void* const inventory = c73_item_inventory(read_ptr(pc, kControllerPawn));
    if (inventory == nullptr) {
        c73_defer(definition, want);
        return;
    }
    for (uint32_t i = 0; i < g_c73_pending_count; ++i) {
        if (g_c73_pending[i].definition == definition) { // this newer count supersedes the deferred one
            g_c73_pending[i] = g_c73_pending[--g_c73_pending_count];
            break;
        }
    }
    c73_apply(inventory, definition, want, "");
}

int32_t detour_c73_consume(void* ability, int32_t amount) {
    const int32_t ret = g_orig_c73_consume(ability, amount);
    if (c73_note(kC73Consume)) {
        log::line("card073: %s ConsumeItem #%u ability %s (%s), amount %d, returned %d, caller exe+0x%llX", side_text(),
                  g_c73.calls[kC73Consume], object_name(ability).text, class_name(ability).text, amount, ret,
                  exe_rva(_ReturnAddress()));
    }
    return ret;
}

// " <item class>=<quantity>" per item of the item inventory; the property dumps once per process.
size_t c73_items_text(const void* inventory, char* out, size_t cap) {
    const auto* const data = static_cast<const uint8_t*>(read_ptr(inventory, kItemInvItems));
    const int32_t n = read_i32(inventory, kItemInvNum);
    int w = snprintf(out, cap, "items %d [", n);
    size_t used = w > 0 ? static_cast<size_t>(w) : cap;
    for (int32_t i = 0; i < n && i < kC73MaxItems && data != nullptr && used < cap; ++i) {
        const void* const item = read_ptr(data, static_cast<size_t>(i) * kItemInvEntryBytes);
        if (item != nullptr && !g_c73.dumped_item) {
            g_c73.dumped_item = true;
            c73_dump_props("item property", item);
        }
        w = snprintf(out + used, cap - used, " %s=%d", class_name(item).text, c73_quantity(item));
        used += w > 0 ? static_cast<size_t>(w) : cap;
    }
    w = used < cap ? snprintf(out + used, cap - used, " ]") : 0;
    return used + (w > 0 ? static_cast<size_t>(w) : 0);
}

// " <ability class> handle/level/input/active" per ASC spec deriving from USTGrenadeThrowGameplayAbility.
void c73_specs_text(const void* asc, char* out, size_t cap) {
    out[0] = '\0';
    const auto* const data = asc != nullptr ? static_cast<const uint8_t*>(read_ptr(asc, kAscSpecs)) : nullptr;
    const int32_t n = asc != nullptr ? read_i32(asc, kAscAbilityNum) : 0;
    size_t used = 0;
    for (int32_t i = 0; i < n && i < kC73MaxSpecs && data != nullptr && used < cap; ++i) {
        const uint8_t* const spec = data + static_cast<size_t>(i) * kSpecBytes;
        const void* const ability = read_ptr(spec, kSpecAbility);
        if (ability == nullptr || !c73_class_is(read_ptr(ability, kObjectClass), "STGrenadeThrowGameplayAbility")) {
            continue;
        }
        const int w = snprintf(out + used, cap - used, " %s h%d/L%d/in%d/act%d", class_name(ability).text,
                               read_i32(spec, kSpecHandle), read_i32(spec, kSpecLevel), read_i32(spec, kSpecInputId),
                               static_cast<int>(spec[kSpecActiveCount]));
        used += w > 0 ? static_cast<size_t>(w) : cap;
    }
}

// One pawn's items and grenade specs, logged as "<who> 1s ..." when the text changed from `last`.
void c73_step_pawn(void* pawn, const char* who, char* last) {
    void* const inventory = c73_item_inventory(pawn);
    if (inventory == nullptr) {
        return;
    }
    if (!g_c73.dumped_inventory) {
        g_c73.dumped_inventory = true;
        c73_dump_props("inventory property", inventory);
    }
    char items[256];
    char specs[256];
    c73_items_text(inventory, items, sizeof(items));
    c73_specs_text(g_get_asc(pawn, true), specs, sizeof(specs));
    char line[kC73Line];
    snprintf(line, sizeof(line), "pawn %s, %s, grenade specs [%s ]", object_name(pawn).text, items, specs);
    if (strcmp(line, last) != 0) {
        memcpy(last, line, strlen(line) + 1);
        log::line("card073: %s 1s %s, calls ability %u add %u remove %u consume %u", who, line,
                  g_c73.calls[kC73Ability], g_c73.calls[kC73Add], g_c73.calls[kC73Remove], g_c73.calls[kC73Consume]);
    }
}

enum C74Hook : uint32_t {
    kC74StartAnims, kC74StartThrow, kC74DoAction, kC74Activate, kC74Spawn, kC74Spawned, kC74Init, kC74Explode,
    kC74Damage, kC74Zone, kC74AnimActivate, kC74AnimDo, kC74AnimEnd, kC74Life, kC74Consume, kC74Release, kC74Hooks
};
const char* const kC74HookNames[kC74Hooks] = {
    "StartAnimations", "StartThrowingAnimation", "OnDoAction_Implementation", "FireProjectile task Activate",
    "SpawnProjectile", "OnGrenadeSpawned", "InitGrenade", "Explode", "ApplyDamageFromImpactHitResult",
    "TrySpawnDamageZone", "PlayAnimation task Activate", "PlayAnimation task DoAction",
    "PlayAnimation task AnimationEnded", "AADProjectile LifeSpanExpired", "AADProjectile ConsumeProjectile",
    "AADProjectile ReleaseToPool"};
constexpr uint32_t kC74Logs = 40;              // lines per hook
constexpr size_t kTaskProjectile = 0x90;       // UADAbilityTask_FireProjectile: the spawned AADProjectile* (Activate)
constexpr size_t kTaskTransform = 0xA0;        // its FTransform<double> (Activate passes it to SpawnProjectile)
constexpr size_t kTaskInitData = 0x100;        // its TSubclassOf<UADProjectileInitData>
constexpr size_t kProjMovement = 0x370;        // AADProjectile: UProjectileMovementComponent* (UpdateInitialMovementVelocity)

C74AbilityFn g_orig_c74_start_anims = nullptr;
C74AbilityFn g_orig_c74_start_throw = nullptr;
C74AbilityFn g_orig_c74_do_action = nullptr;
C74TaskFn g_orig_c74_activate = nullptr;
C74SpawnFn g_orig_c74_spawn = nullptr;
C74GrenadeSpawnedFn g_orig_c74_spawned = nullptr;
C74InitGrenadeFn g_orig_c74_init = nullptr;
C74ExplodeFn g_orig_c74_explode = nullptr;
C74ApplyDamageFn g_orig_c74_damage = nullptr;
C74ZoneFn g_orig_c74_zone = nullptr;
C74TaskFn g_orig_c74_anim[3] = {};             // PlayAnimation task Activate, DoAction, AnimationEnded
C74ExplodeFn g_orig_c74_proj[3] = {};          // AADProjectile LifeSpanExpired, ConsumeProjectile, ReleaseToPool
uint32_t g_c74_calls[kC74Hooks] = {};

// Counts every call; true for the first kC74Logs, and only with UBWCOOP_C74_LOG (card 076 installs these detours
// for its own lines too).
bool c74_note(C74Hook h) {
    ++g_c74_calls[h];
    return g_c74_log && g_c74_calls[h] <= kC74Logs;
}

Vec3 c74_actor_loc(const void* actor) {
    const void* const root = actor != nullptr ? read_ptr(actor, kActorRootComponent) : nullptr;
    return root != nullptr ? read_vec3(root, kComponentTranslation) : Vec3{};
}

Vec3 c74_velocity(const void* projectile) {
    const void* const move = projectile != nullptr ? read_ptr(projectile, kProjMovement) : nullptr;
    return move != nullptr ? read_vec3(move, kMoveVelocity) : Vec3{};
}

// The ability's avatar and its sprint side text.
void c74_log_ability(C74Hook h, const void* ability, const void* ra) {
    const void* const avatar = avatar_of(read_ptr(ability, kAbilityActorInfo));
    log::line("card074: %s t=%llu %s #%u %s on %s (%s), caller exe+0x%llX", side_text(),
              static_cast<unsigned long long>(GetTickCount64()), kC74HookNames[h], g_c74_calls[h],
              class_name(ability).text, object_name(avatar).text, kSprintSideText[sprint_side(avatar)], exe_rva(ra));
}

void detour_c74_start_anims(void* ability) {
    if (c74_note(kC74StartAnims)) {
        c74_log_ability(kC74StartAnims, ability, _ReturnAddress());
    }
    g_orig_c74_start_anims(ability);
}

void detour_c74_start_throw(void* ability) {
    if (c74_note(kC74StartThrow)) {
        c74_log_ability(kC74StartThrow, ability, _ReturnAddress());
    }
    g_orig_c74_start_throw(ability);
}

void detour_c74_do_action(void* ability) {
    if (c74_note(kC74DoAction)) {
        c74_log_ability(kC74DoAction, ability, _ReturnAddress());
    }
    g_orig_c74_do_action(ability);
}

// --- Card 074 steps 2-3: the host's grenade throws are shown on every client, as cosmetic copies ---
// Step 1 found: the client's own throw runs StartAnimations, the PlayAnimation task and its DoAction broadcast, and the
// broadcast's ProcessEvent of the ability Blueprint's OnDoAction (flags 0x08080800: BlueprintEvent | Protected | Event,
// not authority-only or net), but that graph never calls the native OnDoAction_Implementation on the client, so no
// FireProjectile runs there; and the host player's grenade spawns nothing on the client. So the host sends every
// grenade, the thrower's own client included. The client's copy comes from the bare UADProjectilePooler::SpawnProjectile:
// the damage is bound later by OnGrenadeSpawned (its lambdas call ApplyDamageFromImpactHitResult and
// TrySpawnDamageZone), which the copy never gets, so the copy deals no damage.
C74SpawnFn g_c74_spawn_fn = nullptr; // UADProjectilePooler::SpawnProjectile (static), direct call on the client
void* g_c74_grenade = nullptr;       // host: the projectile OnGrenadeSpawned just saw, until its task's Activate returns
uint32_t g_c74_relayed = 0;          // host: GrenadeSpawn throws sent
// A grenade in flight: on the host its projectile and throw number until LifeSpanExpired; on the client the copy of that
// throw until the host's GrenadeExplode (or the copy's own 10 s life span) ends it. Pooled projectiles are reused, so an
// entry leaves at its projectile's expiry. A full table drops the oldest throw: its copy then keeps its own life span.
struct C74Live {
    void* proj;
    uint32_t seq;
    const void* skip_pc; // host, a round: the PC its explosion frame skips too (a vehicle's pilot); else null; compare only
    C31Weak proj_weak;   // card 331: proj's weak, set by c74_table_add; a lookup by number checks it before proj is used
    const void* cls;     // host, a throw: its init data class (a UClass, never freed), for the card 323 join replay
};
constexpr uint32_t kC74Live = 8;
C74Live g_c74_live[kC74Live] = {};
constexpr uint32_t kC476DamageLines = 20; // card 476: freed damage instigator rows
uint32_t g_c476_damage_lines = 0;

// Card 132: a relayed round (a player's, vehicle's or turret's round, or an AI round) in flight. On the host its
// projectile, round number and skipped PC until its OnHit, ReleaseToPool or LifeSpanExpired; on the client the copy of
// that round until the same. Round numbers carry kC132RoundBit, so a GrenadeExplode frame names its table. Rounds fly
// many at once (a Morita burst, a bug swarm's spit), so the table is larger; a full table drops the oldest round, whose
// impact then is not relayed (counted).
constexpr uint32_t kC132RoundBit = 0x80000000u;
constexpr uint32_t kC132Rounds = 256;
C74Live g_c132_rounds[kC132Rounds] = {};
uint32_t g_c132_round_n = 0;      // host: rounds numbered
uint32_t g_c132_round_dropped = 0; // rounds pushed out of a full table

// Adds e to table t of n entries (replacing its projectile's old entry); a full table drops its lowest number.
// Returns true when a live entry was dropped.
bool c74_table_add(C74Live* t, uint32_t n, const C74Live& e) {
    uint32_t slot = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (t[i].proj == nullptr || t[i].proj == e.proj) {
            t[i] = e;
            c31_weak_set(t[i].proj_weak, e.proj);
            return false;
        }
        slot = t[i].seq < t[slot].seq ? i : slot;
    }
    t[slot] = e;
    c31_weak_set(t[slot].proj_weak, e.proj);
    return true;
}

// Removes and returns the entry of table t whose projectile (proj != nullptr) or number matches; {} when none does.
// Card 331: a lookup by number (the client's GrenadeExplode) has no live pointer of its own, so the entry's
// projectile is returned only while its weak still names it; a copy destroyed meanwhile reads as "already ended".
C74Live c74_table_take(C74Live* t, uint32_t n, const void* proj, uint32_t seq) {
    for (uint32_t i = 0; i < n; ++i) {
        if (t[i].proj != nullptr && (proj != nullptr ? t[i].proj == proj : t[i].seq == seq)) {
            C74Live got = t[i];
            t[i] = C74Live{};
            got.proj = proj != nullptr || c31_weak_get(got.proj_weak) == got.proj ? got.proj : nullptr;
            return got;
        }
    }
    return C74Live{};
}

void c74_live_add(void* proj, uint32_t seq, const void* cls) {
    c74_table_add(g_c74_live, kC74Live, C74Live{proj, seq, nullptr, C31Weak{}, cls});
}

C74Live c74_live_take(const void* proj, uint32_t seq) {
    return c74_table_take(g_c74_live, kC74Live, proj, seq);
}

void c132_round_add(void* proj, uint32_t seq, const void* skip_pc) {
    const bool dropped = c74_table_add(g_c132_rounds, kC132Rounds, C74Live{proj, seq, skip_pc, C31Weak{}, nullptr});
    g_c132_round_dropped += dropped ? 1u : 0u;
}

C74Live c132_round_take(const void* proj, uint32_t seq) {
    return c74_table_take(g_c132_rounds, kC132Rounds, proj, seq);
}

// Host: the next round number (never 0, the old card 076 number a client may still treat as "no explosion frame").
uint32_t c132_round_next() {
    g_c132_round_n = (g_c132_round_n + 1u) & ~kC132RoundBit;
    g_c132_round_n += g_c132_round_n == 0 ? 1u : 0u;
    return kC132RoundBit | g_c132_round_n;
}

// Host: sends one frame to every remote PC that has sent its first Pong, except skip_pc (may be null); returns sent,
// *to = how many it went to.
int32_t c74_send_all(const void* actor, const WireString& w, int32_t* to, const void* skip_pc) {
    void* const world = g_actor_get_world(actor);
    int32_t sent = 0;
    *to = 0;
    for (int s = 0; world != nullptr && s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        const C50Copy* const c = pc != nullptr && sprint_side(pc) == kSideServerCopy ? c50_copy(object_index(pc), false)
                                                                                     : nullptr;
        if (c != nullptr && c->ponged && pc != skip_pc) {
            ++*to;
            sent += w.num > 0 && chan_send_client(pc, w) ? 1 : 0;
        }
    }
    return sent;
}
constexpr uint32_t kC74PathChars = 160; // the init data class's package path, with the terminator

// The frame text: "<throw number> <init data class path> x y z qx qy qz qw vx vy vz", the spawn transform (scale 1)
// and the projectile's velocity now. False when a name is too long to send whole.
bool c74_transform_text(uint32_t seq, const void* transform, const void* proj, const void* cls, char* text, size_t cap) {
    double q[4] = {};
    memcpy(q, transform, sizeof(q));
    const Vec3 at = read_vec3(transform, kTransformTranslation);
    const Vec3 v = c74_velocity(proj);
    char package[kC74PathChars];
    char name[kNameChars];
    const uint32_t package_len = object_name_into(read_ptr(cls, kObjectOuter), package, sizeof(package));
    const uint32_t name_len = object_name_into(cls, name, sizeof(name));
    snprintf(text, cap, "%u %s.%s %.1f %.1f %.1f %.6f %.6f %.6f %.6f %.1f %.1f %.1f", seq, package, name, at.x, at.y,
             at.z, q[0], q[1], q[2], q[3], v.x, v.y, v.z);
    return package_len > 0 && package_len < sizeof(package) && name_len > 0 && name_len < sizeof(name);
}

// The task's spawn transform, with the projectile's velocity after OnGrenadeSpawned.
bool c74_spawn_text(uint32_t seq, const void* task, const void* proj, const void* cls, char* text, size_t cap) {
    return c74_transform_text(seq, static_cast<const uint8_t*>(task) + kTaskTransform, proj, cls, text, cap);
}

// Card 076: a mech's rounds. The host's own mech fire spawns nothing on the client, and a client's relayed mech fire
// spawns on the host only (both as with grenades). So each round the host fires from a mech goes as one GrenadeSpawn
// frame with a round number (kC132RoundBit set) to every remote PC except the mech's pilot, whose client already
// shows its own predicted rounds. Card 132: its impact goes as a GrenadeExplode frame with the same number
// (detour_c132_on_hit); the copy deals no damage and never runs its own OnHit; only the host's round does.
// FireProjectile returns its task before the task's Activate, so the mark is set in time.
// Card 125: a mounted turret's rounds take this relay too (c76_mark_task); the log line names the vehicle's class.
constexpr uint32_t kC76MechSeq = 0;
constexpr uint32_t kC76MechLines = 24;

C76Mech g_c76_mech{};

void c76_relay(const void* task, void* proj, const void* cls) {
    g_c76_mech.task = nullptr;
    const void* const vehicle = c31_weak_get(g_c76_mech.vehicle);   // card 331: null once the shooter is gone
    const void* const pilot_pc = c31_weak_get(g_c76_mech.pilot_pc);
    // Card 157: a vehicle's or turret's round takes this relay, not a hit claim. Card 165: a player's own rounds take
    // this relay too (card 132 step 2b, c76_mark_task) and keep their noted round: forgetting them here, right after
    // c157_launch noted them, left every client claim with no round (c159c: 9 of 9 dropped).
    if (vehicle == nullptr || !is_a(vehicle, g_player_mi_class())) {
        c157_forget(proj, "the card 076 relay");
    }
    if (proj == nullptr || cls == nullptr || net_mode(proj) != kNetModeListenServer) {
        return;
    }
    char text[kC74PathChars + kNameChars + 160];
    const uint32_t seq = c132_round_next();
    const bool whole = c74_spawn_text(seq, task, proj, cls, text, sizeof(text));
    static wchar_t wire[kChanTextFrame];
    const WireString w = whole ? chan_text_frame(ChanOp::GrenadeSpawn, text, wire) : WireString{wire, 0, 0};
    int32_t to = 0;
    const int32_t sent = c74_send_all(proj, w, &to, pilot_pc);
    if (sent > 0) {
        c132_round_add(proj, seq, pilot_pc); // card 132: its impact goes as a GrenadeExplode frame
    }
    ++g_c76_mech.relayed;
    if ((g_c76_log || sent != to || w.num == 0) &&
        note_event_cap(g_c76_mech.lines, kC76MechLines, "card076 vehicle round relay")) {
        log::line("%scard076: host t=%llu %s round #%u %s: sent to %d of %d clients (pilot pc %s skipped)%s",
                  sent == to ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()),
                  class_name(vehicle).text, g_c76_mech.relayed, text, sent, to, object_name(pilot_pc).text,
                  w.num > 0 ? "" : (whole ? " (frame not built)" : " (name too long)"));
    }
}

// Host (listen server), after a FireProjectile task's Activate: when its projectile is the one OnGrenadeSpawned just
// saw (a grenade ability's throw, on the host's own pawn or a server copy), every remote PC that has sent its first
// Pong gets one GrenadeSpawn frame, and the throw stays in g_c74_live until its explosion. A solo game has no remote PC,
// so it sends nothing.
void c74_relay(const void* task) {
    void* const proj = read_ptr(task, kTaskProjectile);
    const void* const cls = read_ptr(task, kTaskInitData);
    const bool grenade = !g_client_session && proj != nullptr && proj == g_c74_grenade;
    if (!g_client_session && task == g_c76_mech.task && !grenade) {
        c76_relay(task, proj, cls);
        return;
    }
    if (!grenade) {
        return;
    }
    g_c74_grenade = nullptr;
    c157_forget(proj, "the card 074 relay"); // card 157: a grenade, not a hit-claim round
    g_c76_mech.task = nullptr; // a player's throw was marked for the card 076 relay too (card 132 step 2b)
    if (cls == nullptr || net_mode(proj) != kNetModeListenServer) {
        return;
    }
    char text[kC74PathChars + kNameChars + 160];
    ++g_c74_relayed;
    const bool whole = c74_spawn_text(g_c74_relayed, task, proj, cls, text, sizeof(text));
    static wchar_t wire[kChanTextFrame];
    const WireString w = whole ? chan_text_frame(ChanOp::GrenadeSpawn, text, wire) : WireString{wire, 0, 0};
    int32_t to = 0;
    const int32_t sent = c74_send_all(proj, w, &to, nullptr);
    // Card 323: every built throw stays in g_c74_live, sent or not, so a client that joins while it flies gets it
    // (c74_join_replay); its explosion then goes to every client that has sent its first Pong.
    if (w.num > 0) {
        c74_live_add(proj, g_c74_relayed, cls);
    }
    log::line("%scard074: host t=%llu GrenadeSpawn #%u %s: sent to %d of %d clients%s", sent == to ? "" : "WARN ",
              static_cast<unsigned long long>(GetTickCount64()), g_c74_relayed, text, sent, to,
              w.num > 0 ? "" : (whole ? " (frame not built)" : " (name too long)"));
}

// Card 132: the host's pooled launches made outside a FireProjectile task's Activate. Every AI round (troopers' Morita
// rounds, AI grenades, bug spit) comes from Blueprint through USTCombatUtilityLibrary::SpawnPooledProjectile
// (0x05250240, return exe+0x52502AC). The client's own Blueprints run that path too (card 417: mortar zones, troopers,
// turrets); c417_launch releases those launches, so the client shows only the host's copies. Each goes as one
// GrenadeSpawn frame with a round number (kC132RoundBit set) to every remote PC, the card 076 path: the client
// spawns a copy from
// the same init data class and transform, which flies on its own and deals no damage; only the host's round does. A
// FireProjectile task's launch (player weapons, grenades, mechs and turrets) is left to c74_relay, so no round goes
// twice.
bool g_c74_activating = false; // host: inside a FireProjectile task's Activate
uint32_t g_c132_relayed = 0;   // host: AI rounds sent
uint32_t g_c132_relay_lines = 0;
constexpr uint32_t kC132RelayLines = 24;

void c132_relay(const void* cls, const double* transform, void* proj) {
    if (g_client_session || g_c74_activating || proj == nullptr || cls == nullptr ||
        net_mode(proj) != kNetModeListenServer) {
        return;
    }
    char text[kC74PathChars + kNameChars + 160];
    const uint32_t seq = c132_round_next();
    const bool whole = c74_transform_text(seq, transform, proj, cls, text, sizeof(text));
    static wchar_t wire[kChanTextFrame];
    const WireString w = whole ? chan_text_frame(ChanOp::GrenadeSpawn, text, wire) : WireString{wire, 0, 0};
    int32_t to = 0;
    const int32_t sent = c74_send_all(proj, w, &to, nullptr);
    if (sent > 0) {
        c132_round_add(proj, seq, nullptr);
    }
    ++g_c132_relayed;
    if ((sent != to || w.num == 0 || g_c132_relay_lines < 4) &&
        note_event_cap(g_c132_relay_lines, kC132RelayLines, "card132 AI round relay")) {
        log::line("%scard132: host t=%llu AI round #%u %s: sent to %d of %d clients%s", sent == to ? "" : "WARN ",
                  static_cast<unsigned long long>(GetTickCount64()), g_c132_relayed, text, sent, to,
                  w.num > 0 ? "" : (whole ? " (frame not built)" : " (name too long)"));
    }
}

// Client: the host's GrenadeSpawn frame. A copy is spawned from the same init data class and transform through the
// pooler, its movement takes the host projectile's velocity, and it waits in g_c74_live for the host's GrenadeExplode.
void c74_receive(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    char path[kC74PathChars + kNameChars + 2]; // %225s below
    alignas(16) double t[12] = {}; // FTransform<double>: quat, translation (+pad), scale (+pad)
    Vec3 v{};
    uint32_t seq = 0;
    if (chan_text(s, text, sizeof(text)) <= 0 ||
        sscanf_s(text, "%u %225s %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf", &seq, path,
                 static_cast<unsigned>(sizeof(path)), &t[4], &t[5], &t[6], &t[0], &t[1], &t[2], &t[3], &v.x, &v.y,
                 &v.z) != 12) {
        log::line("WARN card074: client: bad GrenadeSpawn frame");
        return;
    }
    t[8] = t[9] = t[10] = 1.0;
    void* cls = c50_resolve(path); // found, or loaded: a mech round's init data class need not be loaded here yet
    const void* const world = g_actor_get_world(pc);
    c132_proj_copying(true);
    void* const proj = cls != nullptr && world != nullptr && g_c74_spawn_fn != nullptr ? g_c74_spawn_fn(&cls, t, world)
                                                                                       : nullptr;
    c132_proj_copying(false);
    void* const move = proj != nullptr ? read_ptr(proj, kProjMovement) : nullptr;
    const Vec3 spawned = c74_velocity(proj);
    // A zero velocity is an AI round sent before its Blueprint caller set one (card 132): the copy keeps the init
    // data's launch velocity.
    if (move != nullptr && (v.x != 0.0 || v.y != 0.0 || v.z != 0.0)) {
        memcpy(static_cast<uint8_t*>(move) + kMoveVelocity, &v, sizeof(v));
    }
    const bool round = seq == kC76MechSeq || (seq & kC132RoundBit) != 0;
    if (proj != nullptr && (seq & kC132RoundBit) != 0) {
        c132_round_add(proj, seq, nullptr); // card 132: the copy ends on the host's impact frame
    } else if (proj != nullptr && seq != kC76MechSeq) {
        c74_live_add(proj, seq, nullptr);
    }
    if (round && ((!g_c76_log && move != nullptr) ||
                               !note_event_cap(g_c76_mech.copy_lines, kC76MechLines, "card076 mech round copies"))) {
        return; // a mech round's copy logs with UBWCOOP_C76_LOG=1, or when it failed
    }
    log::line("%scard074: client t=%llu GrenadeSpawn %s -> %s %s (%p), velocity at spawn (%.1f, %.1f, %.1f)%s",
              move != nullptr ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), text,
              cls != nullptr ? "copy" : "no such class", class_name(proj).text, proj, spawned.x, spawned.y, spawned.z,
              move != nullptr ? ", set to the host's" : ", no movement component");
}

void c132_early_hit_send(const void* task); // below, after the round OnHit detour
void c132_sent_drop(const void* proj);      // below: the round is back in the pool
void c132_rounds_forget(const void* proj);  // below: the round's old table entries

// Card 286: the client's own grenade throw now reaches FireProjectile (since the client's ability spawns its held
// grenade, cards 158/169), so the client launched its own grenade and the host's GrenadeSpawn copy arrived about 49 ms
// later: two grenades. The host's copy is the one kept (one grenade, the host's place, ended by the host's
// GrenadeExplode). So the own launch goes back to the pool at once, the card 157 pattern. ReleaseToPool (exe+0x4A361E0)
// clears the projectile's hit delegates +0x2B0/+0x2C0 and the delegate pairs +0x2D8/+0x2E8, +0x300/+0x310 and
// +0x328/+0x338, zeroes +0x388, turns collision off, hides the actor and stops its movement component. OnGrenadeSpawned
// (exe+0x5163A70) binds its damage lambdas to +0x2C0 and +0x338, so both are gone. ReleaseToPool does not clear the
// actor's life span timer, and a grenade's explosion is its LifeSpanExpired, so the life span is set to 0 first
// (AActor::SetLifeSpan clears the timer for 0; the client spawned this actor, so it has authority over it).
constexpr uint32_t kC286Lines = 16;
uint32_t g_c286_lines = 0;
uint32_t g_c286_released = 0;

void c286_client_release(const void* task) {
    void* const proj = read_ptr(task, kTaskProjectile);
    if (!g_client_session || proj == nullptr || proj != g_c74_grenade) {
        return;
    }
    g_c74_grenade = nullptr;
    const void* const cls = read_ptr(task, kTaskInitData);
    const bool shot = pl_shot_undo();
    const bool counted = c132_proj_unown(cls);
    c157_forget(proj, "card 286 release");
    if (g_set_life_span != nullptr) {
        g_set_life_span(proj, 0.0f);
    }
    g_orig_c74_proj[2](proj);
    ++g_c286_released;
    if (note_event_cap(g_c286_lines, kC286Lines, "card286 own grenade release")) {
        log::line("%scard286: client t=%llu own %s launch released (the host's copy is shown); shot row %s, own count "
                  "%s, life span %s, %u so far", shot && counted && g_set_life_span != nullptr ? "" : "WARN ",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(cls).text,
                  shot ? "taken back" : "not open", counted ? "taken back" : "not found",
                  g_set_life_span != nullptr ? "cleared" : "not cleared (no SetLifeSpan)", g_c286_released);
    }
}

// Card 417: the client's own launches through the Blueprint pooled-spawn path (SpawnPooledProjectile, return
// exe+0x52502AC: MortarZone, AIMoritaRound, turret rounds) doubled the host's card 132 copies. Each is hidden at once
// and released to the pool on the next frame step (c417_step), the card 286 way: the Blueprint caller binds
// BP_OnProjectileImpact and sets the velocity on the returned round after the call, and ReleaseToPool clears those
// bindings only when it runs after them. Host, mortar only: BP_MortarZone_C's GetMortarLocation keeps clear of the
// host's own player alone, so a host mortar within the zone's SafeDistance of a remote player's pawn is released the
// same way and not relayed (the Blueprint drops another after its next delay).
namespace {

constexpr unsigned long long kC417LaunchRa = 0x52502AC;
constexpr int32_t kC417Ring = 32;
constexpr uint32_t kC417Lines = 16;

struct C417Pending {
    C31Weak weak;
    const void* init_data; // a class object: lives while the map does
    char context[kNameChars];
    bool done; // released by the step, or pooled by the game first: entries never move while the step walks them
};
struct C417State {
    C417Pending ring[kC417Ring];
    int32_t n;
    uint32_t released, gone, overflow, lines, host_released, host_checked;
    uint32_t pooled_early; // the game's own ReleaseToPool reached a queued round before the step (entry dropped)
    uint32_t hit_early;    // a queued round's OnHit before the step (original not run: no own impact)
    const void* zone_class; // the class whose SafeDistance offset is cached
    int32_t safe_at;        // its offset, -1 absent
    bool safe_double;       // DoubleProperty (else FloatProperty)
};

C417State g_c417{};

// The mortar zone's SafeDistance (float or double property), cached per class. Negative when absent.
double c417_safe_distance(const void* zone) {
    const void* const cls = read_ptr(zone, kObjectClass);
    if (cls != g_c417.zone_class) {
        g_c417.zone_class = cls;
        const uint8_t* const f = prop_find_prop(cls, "SafeDistance");
        g_c417.safe_double = f != nullptr && prop_kind_is(f, "DoubleProperty");
        g_c417.safe_at = f != nullptr && (g_c417.safe_double || prop_kind_is(f, "FloatProperty"))
                             ? read_i32(f, kPropertyOffset)
                             : -1;
        if (g_c417.safe_at < 0) {
            log::line("WARN card417: host %s has no float SafeDistance: its mortars are not checked",
                      object_name(cls).text);
        }
    }
    if (g_c417.safe_at < 0) {
        return -1.0;
    }
    const uint8_t* const p = static_cast<const uint8_t*>(zone) + g_c417.safe_at;
    double v = 0.0;
    if (g_c417.safe_double) {
        memcpy(&v, p, sizeof(v));
    } else {
        v = read_f32(p, 0);
    }
    return v;
}

// Host: true when the mortar's XY is within the zone's SafeDistance of a remote player's pawn (logged).
bool c417_host_mortar_close(const void* zone, const double* transform) {
    if (zone == nullptr || strcmp(class_name(zone).text, "BP_MortarZone_C") != 0) {
        return false;
    }
    const double safe = c417_safe_distance(zone);
    const void* const world = g_actor_get_world(zone);
    const Vec3 at = read_vec3(transform, kTransformTranslation);
    ++g_c417.host_checked;
    for (int32_t s = 0; safe > 0.0 && world != nullptr && s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        const void* const pawn = pc != nullptr && c26_is_remote_pc(pc) ? read_ptr(pc, kControllerPawn) : nullptr;
        if (pawn == nullptr) {
            continue;
        }
        const Vec3 p = actor_location(pawn);
        const double d = sqrt((p.x - at.x) * (p.x - at.x) + (p.y - at.y) * (p.y - at.y));
        if (d < safe) {
            log::line("card417: host t=%llu mortar at (%.0f, %.0f) released: %.0f uu from remote player %s (SafeDistance "
                      "%.0f)", static_cast<unsigned long long>(GetTickCount64()), at.x, at.y, d,
                      object_name(pawn).text, safe);
            return true;
        }
    }
    return false;
}

// Both sides: the round is hidden now and queued for the next frame step's release.
void c417_queue(void* proj, const void* init_data, const void* context) {
    if (g_c417.n >= kC417Ring) {
        ++g_c417.overflow;
        log::line("WARN card417: %s t=%llu release ring full (%d): %s from %s left launched",
                  side_text(), static_cast<unsigned long long>(GetTickCount64()), kC417Ring, object_name(init_data).text,
                  class_name(context).text);
        return;
    }
    C417Pending& e = g_c417.ring[g_c417.n++];
    c31_weak_set(e.weak, proj);
    e.init_data = init_data;
    e.done = false;
    snprintf(e.context, sizeof(e.context), "%s", context != nullptr ? class_name(context).text : "-");
    if (g_orig_c98_hidden != nullptr) {
        g_orig_c98_hidden(proj, true); // the original: no hb46 row for a pooled round
    }
}

// The ring index of a queued round, -1 when not queued. Linear: 32 entries, only while some are queued.
int32_t c417_find(const void* proj) {
    for (int32_t i = 0; proj != nullptr && i < g_c417.n && i < kC417Ring; ++i) {
        if (!g_c417.ring[i].done && c31_weak_get(g_c417.ring[i].weak) == proj) {
            return i;
        }
    }
    return -1;
}

// Both sides, launch detour: true when this launch is released next frame (the host does not relay it).
bool c417_launch(void* proj, const void* init_data, const double* transform, const void* context, const void* ra) {
    if (proj == nullptr || exe_rva(ra) != kC417LaunchRa) {
        return false;
    }
    const bool own = g_client_session && !c132_proj_is_copying();
    if (!own && (g_client_session || !c417_host_mortar_close(context, transform))) {
        return false;
    }
    g_c417.host_released += own ? 0u : 1u;
    c417_queue(proj, init_data, context);
    return true;
}

} // namespace

// Both sides, each frame: the queued rounds go back to the pool (life span cleared first, card 286). A release can
// re-enter c417_pooled (another queued round sent back) or c417_queue (a launch): entries are marked done in place,
// never swapped, and a round queued during the walk waits for the next step.
void c417_step() {
    const int32_t start = g_c417.n < kC417Ring ? g_c417.n : kC417Ring;
    for (int32_t i = 0; i < start; ++i) {
        C417Pending& e = g_c417.ring[i];
        if (e.done) {
            continue;
        }
        e.done = true;
        void* const proj = c31_weak_get(e.weak);
        if (proj == nullptr) {
            ++g_c417.gone;
            continue;
        }
        const bool counted = !g_client_session || c132_proj_unown(e.init_data);
        c157_forget(proj, "card 417 release");
        c132_sent_drop(proj);
        if (g_set_life_span != nullptr) {
            g_set_life_span(proj, 0.0f);
        }
        g_orig_c74_proj[2](proj);
        ++g_c417.released;
        if (g_client_session && note_event_cap(g_c417.lines, kC417Lines, "card417 own launch release")) {
            log::line("%scard417: client t=%llu own %s launch released (context %s), own count %s, %u so far",
                      counted ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()),
                      object_name(e.init_data).text, e.context, counted ? "taken back" : "not found", g_c417.released);
        }
    }
    int32_t kept = 0;
    for (int32_t i = start; i < g_c417.n && i < kC417Ring; ++i) {
        if (!g_c417.ring[i].done) {
            g_c417.ring[kept++] = g_c417.ring[i];
        }
    }
    g_c417.n = kept;
}

// Both sides, first in detour_c74_release: a queued round the game sends back to the pool before the step leaves
// the ring (own count taken back here), so the step never releases it twice or releases a reused pool object.
void c417_pooled(const void* proj) {
    const int32_t i = g_c417.n != 0 ? c417_find(proj) : -1;
    if (i < 0) {
        return;
    }
    if (g_client_session) {
        c132_proj_unown(g_c417.ring[i].init_data);
    }
    g_c417.ring[i].done = true; // in place: c417_step may be walking the ring (it compacts at its end)
    ++g_c417.pooled_early;
}

// Both sides, first in detour_c132_on_hit: true for a queued round (its OnHit does not run; the step releases it).
bool c417_hit(const void* proj) {
    if (g_c417.n == 0 || c417_find(proj) < 0) {
        return false;
    }
    ++g_c417.hit_early;
    return true;
}

void c417_arm_map() {
    if (g_c417.released != 0 || g_c417.overflow != 0 || g_c417.host_checked != 0 || g_c417.pooled_early != 0 ||
        g_c417.hit_early != 0) {
        log::line("%scard417: %s map totals: %u released (%u host mortars near a remote player, %u mortars checked), %u "
                  "gone before the step, %u ring full, %u pooled by the game before the step, %u hits before the step "
                  "not run",
                  g_c417.overflow != 0 ? "WARN " : "", side_text(), g_c417.released, g_c417.host_released,
                  g_c417.host_checked, g_c417.gone, g_c417.overflow, g_c417.pooled_early, g_c417.hit_early);
    }
    g_c417 = C417State{};
}

// Always detoured (kHooksUbw): the host relays a grenade throw; logs only with UBWCOOP_C74_LOG=1.
// Card 286: the client releases its own grenade launch (c286_client_release).
void detour_c74_activate(void* task) {
    c132_proj_activating(task);
    g_c74_activating = true;
    g_orig_c74_activate(task);
    g_c74_activating = false;
    c132_proj_activating(nullptr);
    c74_relay(task);
    c132_early_hit_send(task);
    c286_client_release(task); // card 286: last, after every reader of the task's projectile
    if (!g_c74_log || !c74_note(kC74Activate)) {
        return;
    }
    const void* const proj = read_ptr(task, kTaskProjectile);
    const Vec3 at = read_vec3(task, kTaskTransform + kTransformTranslation);
    const Vec3 v = c74_velocity(proj);
    log::line("card074: %s t=%llu task Activate #%u: init data %s at (%.1f, %.1f, %.1f), projectile %s (%p) "
              "velocity (%.1f, %.1f, %.1f)", side_text(), static_cast<unsigned long long>(GetTickCount64()),
              g_c74_calls[kC74Activate], object_name(read_ptr(task, kTaskInitData)).text, at.x, at.y, at.z,
              class_name(proj).text, proj, v.x, v.y, v.z);
}

void* detour_c74_spawn(void* const* init_data_class, const double* transform, const void* world) {
    void* const proj = g_orig_c74_spawn(init_data_class, transform, world);
    // A pooled round handed out again is a new shot: it leaves the sent ring here, since a round can return to the pool
    // without passing ReleaseToPool (hb57: 17 false "already went" WARNs from reused ADProjectiles).
    c132_sent_drop(proj);
    c132_rounds_forget(proj);
    c132_proj_launch(init_data_class != nullptr ? *init_data_class : nullptr, transform, world, proj, _ReturnAddress());
    // Card 417: the client's own Blueprint pooled launch, or a host mortar near a remote player, is released next frame.
    if (!c417_launch(proj, init_data_class != nullptr ? *init_data_class : nullptr, transform, world,
                     _ReturnAddress())) {
        c132_relay(init_data_class != nullptr ? *init_data_class : nullptr, transform, proj);
    }
    if (g_c76_log) {
        c76_spawn(init_data_class != nullptr ? *init_data_class : nullptr, transform, proj);
    }
    if (c74_note(kC74Spawn)) {
        const Vec3 at = read_vec3(transform, kTransformTranslation);
        log::line("card074: %s t=%llu SpawnProjectile #%u: init data %s at (%.1f, %.1f, %.1f) -> %s (%p), "
                  "caller exe+0x%llX", side_text(), static_cast<unsigned long long>(GetTickCount64()),
                  g_c74_calls[kC74Spawn], object_name(init_data_class != nullptr ? *init_data_class : nullptr).text,
                  at.x, at.y, at.z, class_name(proj).text, proj, exe_rva(_ReturnAddress()));
    }
    return proj;
}

// Always detoured (kHooksUbw): marks the projectile for c74_relay; logs only with UBWCOOP_C74_LOG=1.
void detour_c74_spawned(void* ability, void* projectile) {
    g_orig_c74_spawned(ability, projectile);
    g_c74_grenade = projectile;
    c157_forget(projectile, "OnGrenadeSpawned"); // card 157: a grenade is not a hit-claim round
    if (pl_allow(PlClass::Grenade)) { // card 155: every throw this side spawns (host: all players'; client: none)
        const Vec3 at = c74_actor_loc(projectile);
        const Vec3 v = c74_velocity(projectile);
        log::line("playlog: %s t=%llu grenade spawn %s by %s at (%.0f, %.0f, %.0f) velocity (%.0f, %.0f, %.0f)",
                  g_client_session ? "client" : "host", static_cast<unsigned long long>(GetTickCount64()),
                  class_name(projectile).text, object_name(read_ptr(projectile, kActorInstigator)).text, at.x, at.y,
                  at.z, v.x, v.y, v.z);
    }
    if (!g_client_session) {
        c158_host_throw(ability, projectile, c74_velocity(projectile)); // card 158: server copy aim vs LaunchAngle
    }
    if (g_c74_log && c74_note(kC74Spawned)) {
        const Vec3 at = c74_actor_loc(projectile);
        const Vec3 v = c74_velocity(projectile);
        c74_log_ability(kC74Spawned, ability, _ReturnAddress());
        log::line("card074: %s   projectile %s (%p) at (%.1f, %.1f, %.1f) velocity (%.1f, %.1f, %.1f)", side_text(),
                  class_name(projectile).text, projectile, at.x, at.y, at.z, v.x, v.y, v.z);
    }
}

void detour_c74_init(void* projectile, const void* params) {
    g_orig_c74_init(projectile, params);
    if (c74_note(kC74Init)) {
        log::line("card074: %s t=%llu InitGrenade #%u on %s (%p), caller exe+0x%llX", side_text(),
                  static_cast<unsigned long long>(GetTickCount64()), g_c74_calls[kC74Init], class_name(projectile).text,
                  projectile, exe_rva(_ReturnAddress()));
    }
}

void c74_log_projectile(C74Hook h, const void* projectile, const void* ra) {
    const Vec3 at = c74_actor_loc(projectile);
    log::line("card074: %s t=%llu %s #%u %s (%p) at (%.1f, %.1f, %.1f), caller exe+0x%llX", side_text(),
              static_cast<unsigned long long>(GetTickCount64()), kC74HookNames[h], g_c74_calls[h],
              class_name(projectile).text, projectile, at.x, at.y, at.z, exe_rva(ra));
}

void detour_c74_explode(void* projectile) {
    if (c74_note(kC74Explode)) {
        c74_log_projectile(kC74Explode, projectile, _ReturnAddress());
    }
    c132_fx_probe("Explode", class_name(projectile).text, nullptr, projectile, _ReturnAddress());
    g_orig_c74_explode(projectile);
}

// Where a projectile's flight ends on each side: ConsumeProjectile and ReleaseToPool (log only; LifeSpanExpired, the
// explosion, is detoured always for the relay).
void detour_c74_consume(void* projectile) {
    if (c74_note(kC74Consume)) {
        c74_log_projectile(kC74Consume, projectile, _ReturnAddress());
    }
    c132_fx_probe("ConsumeProjectile", class_name(projectile).text, nullptr, projectile, _ReturnAddress());
    g_orig_c74_proj[1](projectile);
}

// Always detoured (kHooksUbw, card 132): a pooled round that ends leaves the round table on either side.
void detour_c74_release(void* projectile) {
    c417_pooled(projectile); // card 417: before the original, so the step's ring no longer holds it
    c132_round_take(projectile, 0);
    c157_forget(projectile, "ReleaseToPool");
    c132_sent_drop(projectile);
    if (g_c74_log && c74_note(kC74Release)) {
        c74_log_projectile(kC74Release, projectile, _ReturnAddress());
    }
    c132_fx_probe("ReleaseToPool", class_name(projectile).text, nullptr, projectile, _ReturnAddress());
    g_orig_c74_proj[2](projectile);
}

struct C74Fx {
    const void* system; // the first Niagara system the explosion spawned, and where
    Vec3 system_at;
    const void* sound;  // the first sound it played (at the grenade)
    void* zone;         // the damage zone it spawned (the incendiary's fire), live until LifeSpanExpired returns
    Vec3 system_rot;    // card 130 item 9: the host's own spawn arguments, logged beside the client's
    Vec3 system_scale;
    bool auto_destroy;
    bool auto_activate;
    int32_t pool;
    bool pre_cull;
};
bool g_c74_fx = false;  // host: inside a relayed grenade's LifeSpanExpired, or a relayed round's OnHit
C74Fx g_c74_fx_got{};

// Host: whether an effect spawn now is the relayed explosion's. A round's impact cue (GameplayCue.Impacts.*) runs
// inside its OnHit on the host and never on the client (hb56: the client ran 0 Impacts cues, the host 2353), so what
// the cue spawns is captured like the rest: a nuke round that hits a bug spawns its explosion only inside the cue, and
// skipping it relayed the bug's death blood instead (hb56 #1979). A grenade's explosion is its cue (card 074).
bool c74_capturing() {
    return g_c74_fx;
}

bool g_c98_fx_quiet = false;  // client: inside a journal replay's one-shot apply (c98_fx_quiet)
uint32_t g_c98_fx_muted = 0;  // effects that apply spawned nothing for, per process

// True (and counted) when an effect spawn now is muted.
bool c98_fx_mute(const char* what, const void* asset) {
    if (!g_c98_fx_quiet) {
        return false;
    }
    ++g_c98_fx_muted;
    log::line("card098: client t=%llu journal replay: %s %s muted (%u so far)",
              static_cast<unsigned long long>(GetTickCount64()), what, object_name(asset).text, g_c98_fx_muted);
    return true;
}

void c98_fx_quiet(bool on) {
    g_c98_fx_quiet = on && g_client_session;
}

uint32_t c98_fx_muted() {
    return g_c98_fx_muted;
}
SpawnNiagaraFn g_orig_c74_niagara = nullptr;
C74StNiagaraFn g_orig_c74_st_niagara = nullptr;
C74PlaySoundFn g_orig_c74_play_sound = nullptr;
C74SpawnSoundFn g_orig_c74_spawn_sound = nullptr;
C74CueFn g_orig_c74_cue = nullptr;
C132OnHitFn g_orig_c132_on_hit = nullptr;

C74SetLifeSpanFn g_set_life_span = nullptr;
StaticClassFn g_niagara_system_class = nullptr; // UNiagaraSystem
StaticClassFn g_sound_base_class = nullptr;     // USoundBase

void c74_log_fx(const char* what, const char* asset, const Vec3* at, const void* ra) {
    const Vec3 p = at != nullptr ? *at : Vec3{};
    log::line("card074: %s t=%llu   FX %s %s at (%.1f, %.1f, %.1f), caller exe+0x%llX", side_text(),
              static_cast<unsigned long long>(GetTickCount64()), what, asset, p.x, p.y, p.z, exe_rva(ra));
}

// Always detoured (kHooksUbw), like PlaySoundAtLocation below: the catch is one branch outside a relayed explosion.
void* detour_c74_st_niagara(void* manager, void* context, void* system, const Vec3* location, const Vec3* rotation,
                            const Vec3* scale, bool auto_destroy, bool auto_activate, int32_t pool, bool pre_cull) {
    if (c98_fx_mute("STSpawnSystemAtLocation", system)) {
        return nullptr;
    }
    const bool first = c74_capturing() && g_c74_fx_got.system == nullptr && location != nullptr;
    if (first) {
        g_c74_fx_got.system = system;
        g_c74_fx_got.system_at = *location;
        g_c74_fx_got.system_rot = rotation != nullptr ? *rotation : Vec3{};
        g_c74_fx_got.system_scale = scale != nullptr ? *scale : Vec3{};
        g_c74_fx_got.auto_destroy = auto_destroy;
        g_c74_fx_got.auto_activate = auto_activate;
        g_c74_fx_got.pool = pool;
        g_c74_fx_got.pre_cull = pre_cull;
    }
    if (g_c74_fx && g_c74_log) {
        c74_log_fx("STSpawnSystemAtLocation", object_name(system).text, location, _ReturnAddress());
    }
    c132_fx_probe("STSpawnSystemAtLocation", object_name(system).text, location, nullptr, _ReturnAddress());
    if (g_c76_log) {
        c76_fx("STSpawnSystemAtLocation", system, location);
    }
    void* const comp = g_orig_c74_st_niagara(manager, context, system, location, rotation, scale, auto_destroy,
                                             auto_activate, pool, pre_cull);
    if (first) {
        // Card 130 item 9: the host's own explosion, checked like the client's replay for a side-by-side.
        fx_watch_add(comp, "grenade explosion (host's own)");
    }
    return comp;
}

// Card 132 item 4a. USTCommonUtilityLibrary::STSpawnNiagraSystemAtLocation 0x0524C580 spawns through the GameMode's
// USTParticleEffectManager (GameMode+0x388, STSpawnSystemAtLocation 0x051DAF10). A client has no GameMode, so the
// original returns null there and the effect is lost: barrels, the bug spawner, grenade and footstep impact cues
// (USTImpactGameplayCueNotify::CreateImpactPFX), gore parts. The client spawns the same system with the same arguments
// through UNiagaraFunctionLibrary::SpawnSystemAtLocation, which STSpawnSystemAtLocation wraps (card 130: it adds only the
// manager's array entry and an OnSystemFinished bind). The host keeps the original.
C132UtilNiagaraFn g_orig_c132_util_niagara = nullptr;
uint32_t g_c132_util_fallbacks = 0; // client spawns made here, per process
uint32_t g_c132_util_culled = 0;    // of those, nulls from the pre-cull check, per process
uint32_t g_c132_util_lines = 0;
int64_t g_c252_ticks[static_cast<size_t>(C252Class::Count)] = {}; // card 252 step 1b
constexpr uint32_t kC132UtilLines = 48;

// Item 4a bound check (UBWCOOP_C132_FX): the manager the client lacks has no cap and no pool of its own (card 132
// report), so the fallback adds none either. This counts the fallback components still alive (same object index, not
// complete) at each spawn and keeps the peak, and c132_util_step logs the peak and the frame time per window on both
// sides, so the biggest fight shows whether the fallback stays bounded and what it costs.
namespace {

constexpr int32_t kUtilLiveMax = 512;
constexpr ULONGLONG kUtilWindowMs = 10000;

struct UtilLive {
    uint64_t weak[kUtilLiveMax];  // FWeakObjectPtr (index, serial): a component freed by a GC or a map change reads
                                  // back null, where a raw pointer read faulted (hb57: a reconnect freed them)
    int32_t n;
    int32_t peak_window, peak_map;
    uint32_t untracked;
};

struct FrameWindow {
    LARGE_INTEGER last;
    ULONGLONG start_ms;
    double max_ms, sum_ms;
    uint32_t frames, over_50ms;
};

UtilLive g_util_live{};  // client, game thread only
FrameWindow g_frame{};   // both sides, game thread only
int64_t g_frame_log_seen = 0; // card 252 step 1b: ue::log line ticks at the last frame window row

// Card 252 step 1b: milliseconds of one class's ticks this window.
double c252_ms(C252Class c, double freq) {
    return static_cast<double>(g_c252_ticks[static_cast<size_t>(c)]) * 1000.0 / freq;
}

void c132_util_sweep() {
    int32_t w = 0;
    for (int32_t i = 0; i < g_util_live.n; ++i) {
        const void* const c = g_weak_get(&g_util_live.weak[i]);
        const bool alive = c != nullptr && (g_niagara_is_complete == nullptr || !g_niagara_is_complete(c));
        if (alive) {
            g_util_live.weak[w] = g_util_live.weak[i];
            ++w;
        }
    }
    g_util_live.n = w;
}

void c132_util_track(const void* comp) {
    if (!g_c132_fx_log || comp == nullptr || g_weak_get == nullptr || g_weak_assign == nullptr) {
        return;
    }
    // Card 263: the table is swept once per engine tick (c132_util_step), not on every spawn (quadratic in a big fight);
    // here only when it is full, then the spawn counts as untracked if it is still full.
    if (g_util_live.n >= kUtilLiveMax) {
        c132_util_sweep();
    }
    if (g_util_live.n >= kUtilLiveMax) {
        ++g_util_live.untracked;
        return;
    }
    void* object = const_cast<void*>(comp);
    g_weak_assign(&g_util_live.weak[g_util_live.n], &object);
    ++g_util_live.n;
    g_util_live.peak_window = g_util_live.n > g_util_live.peak_window ? g_util_live.n : g_util_live.peak_window;
    g_util_live.peak_map = g_util_live.n > g_util_live.peak_map ? g_util_live.n : g_util_live.peak_map;
}

} // namespace

// Both sides, every engine tick while the effect audit is on: frame time per kUtilWindowMs window and, on a client, the
// live fallback component count.
void c132_util_step() {
    if (!g_c132_fx_log) {
        return;
    }
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    const ULONGLONG ms = GetTickCount64();
    if (g_frame.frames != 0 || g_frame.last.QuadPart != 0) {
        const double dt = static_cast<double>(now.QuadPart - g_frame.last.QuadPart) * 1000.0 /
                          static_cast<double>(freq.QuadPart);
        g_frame.max_ms = dt > g_frame.max_ms ? dt : g_frame.max_ms;
        g_frame.sum_ms += dt;
        g_frame.over_50ms += dt > 50.0 ? 1u : 0u;
        ++g_frame.frames;
    } else {
        g_frame.start_ms = ms;
    }
    g_frame.last = now;
    if (g_client_session) {
        c132_util_sweep(); // card 263: once per tick, so the peaks are per-tick values
    }
    if (ms - g_frame.start_ms < kUtilWindowMs) {
        return;
    }
    const double f = static_cast<double>(freq.QuadPart);
    int64_t log_ticks = 0;
    int64_t log_lines = 0;
    log::line_totals(&log_ticks, &log_lines);
    const double log_ms = static_cast<double>(log_ticks - g_frame_log_seen) * 1000.0 / f; // card 252 step 1b
    g_frame_log_seen = log_ticks;
    log::line("card132: %s t=%llu frame window %llu ms: %u frames, mean %.1f ms, max %.1f ms, %u over 50 ms; util "
              "fallback live %d, window peak %d, map peak %d (peaks per tick), untracked %u; mod ms: chan %.0f fx %.0f "
              "audit %.0f log %.0f pose %.0f digest %.0f other %.0f",
              side(), static_cast<unsigned long long>(ms), static_cast<unsigned long long>(ms - g_frame.start_ms),
              g_frame.frames, g_frame.frames != 0 ? g_frame.sum_ms / g_frame.frames : 0.0, g_frame.max_ms,
              g_frame.over_50ms, g_util_live.n, g_util_live.peak_window, g_util_live.peak_map, g_util_live.untracked,
              c252_ms(C252Class::Chan, f), c252_ms(C252Class::FxApply, f), c252_ms(C252Class::Audit, f), log_ms,
              c252_ms(C252Class::PoseApply, f) + c252_ms(C252Class::PoseStep, f),
              c252_ms(C252Class::DigestRecv, f) + c252_ms(C252Class::DigestStep, f),
              c252_ms(C252Class::Tick, f) - c252_ms(C252Class::PoseStep, f) - c252_ms(C252Class::DigestStep, f));
    memset(g_c252_ticks, 0, sizeof(g_c252_ticks));
    const LARGE_INTEGER last = g_frame.last;
    g_frame = FrameWindow{};
    g_frame.last = last;
    g_frame.start_ms = ms;
    g_util_live.peak_window = g_util_live.n;
}

void* detour_c132_util_niagara(void* context, void* system, const Vec3* location, const Vec3* rotation,
                               const Vec3* scale, bool auto_destroy, bool auto_activate, int32_t pool, bool pre_cull) {
    if (c98_fx_mute("STSpawnNiagraSystemAtLocation", system)) {
        return nullptr;
    }
    if (!g_client_session || system == nullptr || location == nullptr || rotation == nullptr || scale == nullptr) {
        return g_orig_c132_util_niagara(context, system, location, rotation, scale, auto_destroy, auto_activate, pool,
                                        pre_cull);
    }
    void* const comp = g_spawn_niagara(context, system, location, rotation, scale, auto_destroy, auto_activate, pool,
                                       pre_cull);
    if (comp == nullptr && c136_blocked()) {
        return nullptr; // card 136 step 3: blocked inside the SpawnSystemAtLocation detour; the host sends this effect
    }
    ++g_c132_util_fallbacks;
    c132_util_track(comp);
    // A null with pre_cull set is the engine's pre-cull answer for this player's view (card 132 Trace 4): counted,
    // not a WARN. A null without pre_cull is a failed spawn.
    const bool culled = comp == nullptr && pre_cull;
    g_c132_util_culled += culled ? 1u : 0u;
    if ((comp == nullptr && !pre_cull) ||
        note_event_cap(g_c132_util_lines, kC132UtilLines, "card132 client util niagara lines")) {
        log::line("%scard132: client t=%llu util niagara %s at (%.0f, %.0f, %.0f) pre_cull %d: %s (client spawn %u, "
                  "culled %u), caller exe+0x%llX",
                  comp != nullptr || culled ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()),
                  object_name(system).text, location->x, location->y, location->z, pre_cull ? 1 : 0,
                  comp != nullptr ? "spawned" : (culled ? "culled" : "none"), g_c132_util_fallbacks,
                  g_c132_util_culled, exe_rva(_ReturnAddress()));
    }
    return comp;
}

void detour_c74_play_sound(const void* context, void* sound, const Vec3* location, const Vec3* rotation,
                           float volume, float pitch, float start, void* attenuation, void* concurrency,
                           const void* owner, const void* params) {
    if (c98_fx_mute("PlaySoundAtLocation", sound)) {
        return;
    }
    if (c74_capturing() && g_c74_fx_got.sound == nullptr) {
        g_c74_fx_got.sound = sound;
    }
    if (g_c74_fx && g_c74_log) {
        c74_log_fx("PlaySoundAtLocation", object_name(sound).text, location, _ReturnAddress());
    }
    c132_fx_probe("PlaySoundAtLocation", object_name(sound).text, location, nullptr, _ReturnAddress());
    C136Call c{C136Point::PlaySound};
    c.asset = sound;
    c.context = context;
    c.owner = owner;
    c.at = location != nullptr ? *location : Vec3{};
    c.rot = rotation != nullptr ? *rotation : Vec3{};
    c.look[0] = volume;
    c.look[1] = pitch;
    c.look[2] = start;
    c.atten = attenuation;
    c.conc = concurrency;
    c.located = location != nullptr;
    const uint32_t id = c136_fx_enter(c);
    // Card 162: a remote player's sound with no attenuation plays with its fallback (the row keeps the call's own).
    void* const atten = c136_blocked() ? attenuation : c162_call_atten(c);
    if (location != nullptr) {
        pl_sound(owner, sound, *location, atten, -1); // card 155: a remote player's sound (pointer compares first)
    }
    if (!c136_blocked()) { // card 136 step 3: a blocked client effect is the host's to send
        g_orig_c74_play_sound(context, sound, location, rotation, volume, pitch, start, atten, concurrency, owner,
                              params);
    }
    c136_fx_leave(c.point, id, nullptr);
}

// Always detoured (kHooksUbw, card 136 capture points 2 and 1); the card 074, 076 and 132 lines stay behind their flags.
void* detour_c74_niagara(const void* context, void* system, const Vec3* location, const Vec3* rotation,
                         const Vec3* scale, bool auto_destroy, bool auto_activate, int32_t pool, bool pre_cull) {
    if (g_c98_fx_quiet) { // log only, as in detour_c74_spawn_sound
        log::line("WARN card098: client journal replay: SpawnSystemAtLocation %s not muted (no mute on this hook yet)",
                  object_name(system).text);
    }
    if (g_c74_fx && g_c74_log) {
        c74_log_fx("SpawnSystemAtLocation", object_name(system).text, location, _ReturnAddress());
    }
    c132_fx_probe("SpawnSystemAtLocation", object_name(system).text, location, nullptr, _ReturnAddress());
    if (g_c76_log) {
        c76_fx("SpawnSystemAtLocation", system, location);
    }
    C136Call c{C136Point::Niagara};
    c.asset = system;
    c.context = context;
    c.at = location != nullptr ? *location : Vec3{};
    c.rot = rotation != nullptr ? *rotation : Vec3{};
    c.scale = scale != nullptr ? *scale : Vec3{1.0, 1.0, 1.0};
    c.flags = static_cast<uint8_t>((auto_destroy ? kC136FlagAutoDestroy : 0) | (auto_activate ? kC136FlagAutoActivate : 0));
    const uint32_t id = c136_fx_enter(c);
    void* const comp = c136_blocked() ? nullptr // card 136 step 3: the host sends it
                                      : g_orig_c74_niagara(context, system, location, rotation, scale, auto_destroy,
                                                           auto_activate, pool, pre_cull);
    c136_fx_leave(c.point, id, comp);
    return comp;
}

void* detour_c74_spawn_sound(const void* context, void* sound, const Vec3* location, const Vec3* rotation,
                             float volume, float pitch, float start, void* attenuation, void* concurrency,
                             bool auto_destroy) {
    if (g_c98_fx_quiet) { // log only: the play build has no mute on this hook, so a leak here is a play-build leak too
        log::line("WARN card098: client journal replay: SpawnSoundAtLocation %s not muted (no mute on this hook yet)",
                  object_name(sound).text);
    }
    if (g_c74_fx && g_c74_log) {
        c74_log_fx("SpawnSoundAtLocation", object_name(sound).text, location, _ReturnAddress());
    }
    c132_fx_probe("SpawnSoundAtLocation", object_name(sound).text, location, nullptr, _ReturnAddress());
    C136Call c{C136Point::SpawnSound};
    c.asset = sound;
    c.context = context;
    c.at = location != nullptr ? *location : Vec3{};
    c.rot = rotation != nullptr ? *rotation : Vec3{};
    c.look[0] = volume;
    c.look[1] = pitch;
    c.look[2] = start;
    c.flags = auto_destroy ? kC136FlagAutoDestroy : 0;
    c.atten = attenuation;
    c.conc = concurrency;
    c.located = location != nullptr;
    const uint32_t id = c136_fx_enter(c);
    void* const audio = c136_blocked() ? nullptr // card 136 step 3: the host sends it
                                       : g_orig_c74_spawn_sound(context, sound, location, rotation, volume, pitch,
                                                                start, c162_call_atten(c), concurrency, auto_destroy);
    c136_fx_leave(c.point, id, nullptr);
    return audio;
}

// Always detoured (kHooksUbw, card 132): the cue probe (UBWCOOP_C132_FX=1); card 136 capture point 4.
void detour_c74_cue(void* asc, uint64_t tag, const void* parameters) {
    if (g_c74_fx && g_c74_log) {
        c74_log_fx("ExecuteGameplayCue", fname_text(&tag).text, nullptr, _ReturnAddress());
    }
    c132_fx_probe("ExecuteGameplayCue", fname_text(&tag).text, nullptr, nullptr, _ReturnAddress());
    C136Call c{C136Point::Cue};
    c.tag = tag;
    c.owner = asc != nullptr ? read_ptr(asc, kComponentOwner) : nullptr;
    c.cue_params = parameters;
    const uint32_t id = c136_fx_enter(c);
    g_orig_c74_cue(asc, tag, parameters);
    c136_fx_leave(c.point, id, nullptr);
}

// An asset's path, "<package>.<name>" (a top-level asset's outer is its package, named by its full path), or "-".
// A path that does not fit is "-" too (logged): a cut path would load nothing.
C74Path c74_asset_path(const void* asset) {
    C74Path out{};
    snprintf(out.text, sizeof(out.text), "-");
    if (asset == nullptr) {
        return out; // the explosion did not use one
    }
    const void* const outer = read_ptr(asset, kObjectOuter);
    char package[sizeof(out.text)];
    const uint32_t len = outer != nullptr ? object_name_into(outer, package, sizeof(package)) : 0;
    const ObjectName name = object_name(asset);
    if (len == 0 || len + 1 + strlen(name.text) >= sizeof(out.text)) {
        log::line("WARN card074: host asset %s: no path that fits (package length %u)", name.text, len);
        return out;
    }
    snprintf(out.text, sizeof(out.text), "%s.%s", package, name.text);
    return out;
}

// Host: a relayed throw's explosion, to every client that got the throw: GrenadeExplode "<throw number> x y z
// <system path> <sound path> sx sy sz <zone class path> zx zy zz zyaw <zone life span s>" (an asset or zone the
// explosion did not make is "-"; then its numbers are 0).
void c74_relay_explode(uint32_t seq, const void* proj, const Vec3& at, const C74Fx& fx, const void* skip_pc) {
    const Vec3 zone_at = c74_actor_loc(fx.zone);
    const void* const zone_root = fx.zone != nullptr ? read_ptr(fx.zone, kActorRootComponent) : nullptr;
    const double zone_yaw = zone_root != nullptr ? component_yaw(zone_root) : 0.0;
    const float zone_life = fx.zone != nullptr ? g_life_span(fx.zone) : 0.0f;
    char text[3 * kAssetPathChars + 160];
    snprintf(text, sizeof(text), "%u %.1f %.1f %.1f %s %s %.1f %.1f %.1f %s %.1f %.1f %.1f %.1f %.2f", seq, at.x, at.y,
             at.z, c74_asset_path(fx.system).text, c74_asset_path(fx.sound).text, fx.system_at.x, fx.system_at.y,
             fx.system_at.z, c74_asset_path(fx.zone != nullptr ? read_ptr(fx.zone, kObjectClass) : nullptr).text,
             zone_at.x, zone_at.y, zone_at.z, zone_yaw, zone_life);
    static wchar_t wire[kChanTextFrame];
    const WireString w = chan_text_frame(ChanOp::GrenadeExplode, text, wire);
    int32_t to = 0;
    const int32_t sent = c74_send_all(proj, w, &to, skip_pc);
    const bool round = (seq & kC132RoundBit) != 0;
    if (sent != to || !round || g_c76_log || g_c132_fx_log) {
        log::line("%scard%s: host t=%llu GrenadeExplode %s (%s): sent to %d of %d clients", sent == to ? "" : "WARN ",
                  round ? "132" : "074", static_cast<unsigned long long>(GetTickCount64()), text,
                  round ? class_name(proj).text : "grenade", sent, to);
    }
    if (round) {
        return;
    }
    log::line("card130: host grenade %u effect spawn: rotation (%.1f, %.1f, %.1f), scale (%.2f, %.2f, %.2f), auto destroy "
              "%d, auto activate %d, pool %d, pre-cull %d (the client replays rotation 0, scale 1, 1 1 0 1)",
              seq, fx.system_rot.x, fx.system_rot.y, fx.system_rot.z, fx.system_scale.x, fx.system_scale.y,
              fx.system_scale.z, fx.auto_destroy ? 1 : 0, fx.auto_activate ? 1 : 0, fx.pool, fx.pre_cull ? 1 : 0);
}

// An asset by path: loaded already (StaticFindObject), else loaded now (StaticLoadObject, once: later finds hit);
// nullptr for "-" or a path neither finds.
void* c74_find_asset(const char* path, void* cls) {
    wchar_t wide[kAssetPathChars];
    if (strcmp(path, "-") == 0 || swprintf(wide, kAssetPathChars, L"%hs", path) <= 0) {
        return nullptr;
    }
    void* const found = g_static_find_object(cls, nullptr, wide, false);
    return found != nullptr ? found
                            : g_static_load_object(cls, nullptr, wide, nullptr, kC29LoadFlags, nullptr, true, nullptr);
}

// Client: the host's GrenadeExplode frame. A grenade's copy expires now (its LifeSpanExpired: the pool release), a
// round's copy goes back to the pool, where its own flight put it; the host's effect plays where the host's exploded
// either way. A copy that already ended (its own life span, a round's own hit) leaves the effect to play in the
// receiving PC's world. The log gives how far the copy was from the host's.
void c74_receive_explode(void* /*pc*/, const WireString* s) {
    char text[kChanTextMax + 1];
    static_assert(kAssetPathChars == 192, "the %191s widths below");
    char system[kAssetPathChars] = {};
    char sound[kAssetPathChars] = {};
    char zone[kAssetPathChars] = {};
    uint32_t seq = 0;
    Vec3 host{};
    Vec3 fx_at{};
    Vec3 zone_at{};
    double zone_yaw = 0.0;
    float zone_life = 0.0f;
    if (chan_text(s, text, sizeof(text)) <= 0 ||
        sscanf_s(text, "%u %lf %lf %lf %191s %191s %lf %lf %lf %191s %lf %lf %lf %lf %f", &seq, &host.x, &host.y,
                 &host.z, system, static_cast<unsigned>(sizeof(system)), sound, static_cast<unsigned>(sizeof(sound)),
                 &fx_at.x, &fx_at.y, &fx_at.z, zone, static_cast<unsigned>(sizeof(zone)), &zone_at.x, &zone_at.y,
                 &zone_at.z, &zone_yaw, &zone_life) != 15) {
        log::line("WARN card074: client: bad GrenadeExplode frame");
        return;
    }
    const bool round = (seq & kC132RoundBit) != 0;
    const C74Live e = round ? c132_round_take(nullptr, seq) : c74_live_take(nullptr, seq);
    const Vec3 at = c74_actor_loc(e.proj);
    const double dx = at.x - host.x, dy = at.y - host.y, dz = at.z - host.z;
    const bool log_it = !round || g_c76_log || g_c132_fx_log;
    if (log_it) {
        // Card 132 hb53 note 2: the number pairs this line with the copy's "round copy #<n> ... hit" line, so a
        // round whose copy hit elsewhere shows both points; the effect plays at the host's.
        log::line("card%s: client t=%llu GrenadeExplode #%u %s -> %s (%p) at (%.1f, %.1f, %.1f), %.1f uu from the "
                  "host's (%.1f, %.1f, %.1f)",
                  round ? "132" : "074", static_cast<unsigned long long>(GetTickCount64()), seq & ~kC132RoundBit, text,
                  e.proj != nullptr ? "copy" : "copy already ended, effect in the PC's world", e.proj, at.x, at.y,
                  at.z, e.proj != nullptr ? sqrt(dx * dx + dy * dy + dz * dz) : 0.0, host.x, host.y, host.z);
    }
    // Card 136 step 5: the explosion's effects (system, sound, damage zone fire) reach the client through the effects
    // manager like every other host effect; the frame still names them for this line.
    if (e.proj != nullptr) {
        (round ? g_orig_c74_proj[2] : g_orig_c74_proj[0])(e.proj);
    }
}

// Card 132: a relayed round's OnHit. Host: what the hit spawns outside a cue (the nuke's fireball and impact sound, a
// round's damage zone) goes to every client that got the round, as a GrenadeExplode frame with the round's number. The
// client: a round copy skips its own OnHit (no second effect, sound, zone or damage; the host's frame plays the impact
// at the host's point) and goes back to the pool. Other projectiles (the client's own predicted rounds) run as always.
uint32_t g_c132_hit_lines = 0;
constexpr uint32_t kC132HitLines = 60000; // card 132 4: every copy hit, for the distribution
uint32_t g_c132_copy_hits = 0; // client: round copies whose OnHit was skipped

// Host: a round that hit inside its own FireProjectile task's Activate (point blank: the spawn's first sweep hits),
// before c74_relay numbered it and put it in the round table. Its capture waits here; c132_early_hit_send sends it as
// that round's GrenadeExplode frame right after the round's GrenadeSpawn frame. hb55: a point-blank nuke's fireball
// was lost this way, and the round's later OnHit sent the gore it spawned in its place. One slot: one task at a time.
struct C132EarlyHit {
    const void* proj;
    Vec3 at;
    C74Fx fx;
};
C132EarlyHit g_c132_early{};
uint32_t g_c132_early_sent = 0;
uint32_t g_c132_early_lost = 0; // early hits with no slot (one already waiting) or no matching task: not relayed

// Host: rounds whose GrenadeExplode went out, until they go back to the pool. One round gives one GrenadeExplode; a later
// OnHit of one of these (its own gameplay still runs) is a WARN, counted, never a second frame.
constexpr int32_t kC132SentRounds = 32;
struct C132Sent {
    const void* proj[kC132SentRounds];
    int32_t next;      // ring: the oldest entry is overwritten (more live rounds than this: a later hit is not caught)
    uint32_t repeats;  // OnHits of a round already sent, per process
    uint32_t repeat_lines; // card 188: repeat lines with no effect captured (notes, capped per map)
};
// Card 188: a repeat OnHit that captured no effect has nothing the client misses; its note is capped per map.
constexpr uint32_t kC132RepeatLines = 24;
C132Sent g_c132_sent{};

void c132_sent_add(const void* proj) {
    g_c132_sent.proj[g_c132_sent.next] = proj;
    g_c132_sent.next = (g_c132_sent.next + 1) % kC132SentRounds;
}

bool c132_sent_has(const void* proj) {
    for (int32_t i = 0; i < kC132SentRounds && proj != nullptr; ++i) {
        if (g_c132_sent.proj[i] == proj) {
            return true;
        }
    }
    return false;
}

void c132_sent_drop(const void* proj) {
    for (int32_t i = 0; i < kC132SentRounds && proj != nullptr; ++i) {
        g_c132_sent.proj[i] = g_c132_sent.proj[i] == proj ? nullptr : g_c132_sent.proj[i];
    }
}

// A pooled projectile handed out again: the entries it left from its last flight go, since it can return to the pool
// without passing OnHit, ReleaseToPool or LifeSpanExpired. A stale entry would give this flight the old number and
// skipped PC (the caller adds the new entry after this).
void c132_rounds_forget(const void* proj) {
    if (proj == nullptr) {
        return;
    }
    c74_table_take(g_c132_rounds, kC132Rounds, proj, 0);
    c74_table_take(g_c74_live, kC74Live, proj, 0);
    g_c132_early = g_c132_early.proj == proj ? C132EarlyHit{} : g_c132_early;
}

// Map load (ubw_load_map_enter, both sides): every projectile these tables name belongs to the world being torn down,
// so a later lookup by number (the client's GrenadeExplode) or a class_name read would touch a freed object (hb57's
// crash class). All of them start empty on the new map.
void c132_rounds_map_end() {
    memset(g_c74_live, 0, sizeof(g_c74_live));
    memset(g_c132_rounds, 0, sizeof(g_c132_rounds));
    g_c132_early = C132EarlyHit{};
    g_c132_sent = C132Sent{};
    g_c74_grenade = nullptr;
}

// Host: runs a round's OnHit with the effect capture on; returns what it spawned, its impact cue's spawns included.
C74Fx c132_capture_hit(void* self, void* hit_comp, void* other, void* other_comp, const Vec3* impulse,
                       const void* hit) {
    const bool saved_fx = g_c74_fx;
    const C74Fx saved_got = g_c74_fx_got;
    g_c74_fx_got = C74Fx{};
    g_c74_fx = true;
    g_orig_c132_on_hit(self, hit_comp, other, other_comp, impulse, hit);
    const C74Fx got = g_c74_fx_got;
    g_c74_fx = saved_fx;
    g_c74_fx_got = saved_got;
    return got;
}

bool c132_fx_any(const C74Fx& fx) {
    return fx.system != nullptr || fx.sound != nullptr || fx.zone != nullptr;
}

void c132_early_hit_send(const void* task) {
    const void* const proj = read_ptr(task, kTaskProjectile);
    if (g_c132_early.proj != nullptr && g_c132_early.proj != proj) {
        ++g_c132_early_lost;
        log::line("WARN card132: host t=%llu point-blank hit of %s waits, but the task launched %s: not relayed (%u lost)",
                  static_cast<unsigned long long>(GetTickCount64()), class_name(g_c132_early.proj).text,
                  class_name(proj).text, g_c132_early_lost);
    }
    if (g_c132_early.proj == nullptr || g_c132_early.proj != proj) {
        g_c132_early = C132EarlyHit{};
        return;
    }
    const C132EarlyHit h = g_c132_early;
    g_c132_early = C132EarlyHit{};
    const C74Live e = c132_round_take(proj, 0); // relayed now: its copy ends on this frame
    if (e.proj == nullptr) {
        log::line("card132: host t=%llu point-blank hit of %s inside its launch: not in the round table (no remote PC, or not a round)",
                  static_cast<unsigned long long>(GetTickCount64()), class_name(proj).text);
        return;
    }
    ++g_c132_early_sent;
    log::line("card132: host t=%llu point-blank hit of round #%u %s inside its launch: sent after its spawn frame "
              "(%u so far)", static_cast<unsigned long long>(GetTickCount64()), e.seq & ~kC132RoundBit,
              class_name(proj).text, g_c132_early_sent);
    c74_relay_explode(e.seq, proj, h.at, h.fx, e.skip_pc);
    c132_sent_add(proj);
}

void detour_c132_on_hit(void* self, void* hit_comp, void* other, void* other_comp, const Vec3* impulse,
                        const void* hit) {
    if (c417_hit(self)) {
        return; // card 417: a queued own launch makes no impact of its own; the next frame step releases it
    }
    const C74Live e = c132_round_take(self, 0);
    if (e.proj == nullptr && c157_on_hit(self, hit_comp, other, other_comp, impulse, hit)) {
        return; // card 157: a remote player's round waits for its claim (host)
    }
    if (e.proj == nullptr && !g_client_session && g_c74_activating && g_c132_early.proj != nullptr &&
        g_c132_early.proj != self) {
        ++g_c132_early_lost;
        log::line("WARN card132: host t=%llu second point-blank hit in one launch (%s, %s already waits): not relayed "
                  "(%u lost)", static_cast<unsigned long long>(GetTickCount64()), class_name(self).text,
                  class_name(g_c132_early.proj).text, g_c132_early_lost);
    }
    if (e.proj == nullptr && !g_client_session && g_c74_activating && g_c132_early.proj == nullptr) {
        const Vec3 at = c74_actor_loc(self);
        const C74Fx got = c132_capture_hit(self, hit_comp, other, other_comp, impulse, hit);
        if (c132_fx_any(got)) {
            g_c132_early = C132EarlyHit{self, at, got};
        }
        return;
    }
    if (e.proj == nullptr) {
        if (!g_client_session && c132_sent_has(self)) {
            // Card 167: the repeat runs with the effect capture on, so the line says what it hit and whether it
            // spawned an effect the client never gets (no second frame). Card 188: a WARN only when it captured an
            // effect; with none it is a note under kC132RepeatLines. Card 193: a note in both cases (since card
            // 136 step 5 the effects reach the client by the effects manager, not by a GrenadeExplode frame).
            ++g_c132_sent.repeats;
            const Vec3 at = c74_actor_loc(self);
            const C74Fx got = c132_capture_hit(self, hit_comp, other, other_comp, impulse, hit);
            if (!note_event_cap(g_c132_sent.repeat_lines, kC132RepeatLines, "card132 repeat hits")) {
                return;
            }
            log::line("card132: host t=%llu OnHit of %s after its GrenadeExplode: effects go by the effects manager "
                      "(%u so far); hit %s (%s) at (%.0f, %.0f, %.0f), effects system %s sound %s zone %s",
                      static_cast<unsigned long long>(GetTickCount64()), class_name(self).text, g_c132_sent.repeats,
                      other != nullptr ? object_name(other).text : "null",
                      other_comp != nullptr ? object_name(other_comp).text : "null", at.x, at.y, at.z,
                      got.system != nullptr ? "yes" : "no", got.sound != nullptr ? "yes" : "no",
                      got.zone != nullptr ? "yes" : "no");
            return;
        }
        g_orig_c132_on_hit(self, hit_comp, other, other_comp, impulse, hit);
        return;
    }
    if (g_client_session) {
        ++g_c132_copy_hits;
        if ((g_c76_log || g_c132_fx_log) && note_event_cap(g_c132_hit_lines, kC132HitLines, "card132 copy hits")) {
            const Vec3 at = c74_actor_loc(self);
            log::line("card132: client t=%llu round copy #%u %s hit %s at (%.1f, %.1f, %.1f): own OnHit skipped "
                      "(%u so far), back to the pool", static_cast<unsigned long long>(GetTickCount64()),
                      e.seq & ~kC132RoundBit, class_name(self).text, class_name(other).text, at.x, at.y, at.z,
                      g_c132_copy_hits);
        }
        g_orig_c74_proj[2](self);
        return;
    }
    const Vec3 at = c74_actor_loc(self);
    const C74Fx got = c132_capture_hit(self, hit_comp, other, other_comp, impulse, hit);
    if (c132_fx_any(got)) {
        c74_relay_explode(e.seq, self, at, got, e.skip_pc);
        c132_sent_add(self);
    }
}

// Always detoured (kHooksUbw): a projectile's LifeSpanExpired is its explosion (the grenade's damage and fire zone are
// bound to it by OnGrenadeSpawned). Host: a relayed throw's goes to the clients. Client: a copy that ends on its own
// leaves g_c74_live. Logs only with UBWCOOP_C74_LOG=1.
void detour_c74_life(void* projectile) {
    if (g_c74_log && c74_note(kC74Life)) {
        c74_log_projectile(kC74Life, projectile, _ReturnAddress());
    }
    c132_fx_probe("LifeSpanExpired", class_name(projectile).text, nullptr, projectile, _ReturnAddress());
    C74Live e = c74_live_take(projectile, 0);
    const bool round = e.proj == nullptr;
    e = round ? c132_round_take(projectile, 0) : e; // card 132: a round that ends in the air
    if (e.proj == nullptr || g_client_session) {
        g_orig_c74_proj[0](projectile);
        return;
    }
    const Vec3 at = c74_actor_loc(projectile);
    g_c74_fx_got = C74Fx{};
    g_c74_fx = true;
    g_c74_pe = g_c74_log; // log only: the cue's ProcessEvents
    g_orig_c74_proj[0](projectile);
    g_c74_fx = false;
    g_c74_pe = false;
    if (!round || g_c74_fx_got.system != nullptr || g_c74_fx_got.sound != nullptr || g_c74_fx_got.zone != nullptr) {
        c74_relay_explode(e.seq, projectile, at, g_c74_fx_got, e.skip_pc);
    }
}

// Card 476: a round thrown by a client that left before it exploded keeps the freed pawn as its Instigator. The game's
// ApplyDamageFromImpactHitResult reads the instigator's ability system through its vtable unchecked (exe+0x522DC72,
// the struct field +0x28, when the causer has none), so a freed or null instigator crashes there. The causer (the
// round itself) stands in: the game reads it first and finds no ability system, then reads it again in that slot.
// b179: only a really freed instigator is replaced; a destroyed shooter that is still the array entry (flagged
// unreachable or garbage, not yet freed) keeps the game's own damage attribution.
static void* c476_live_instigator(void* instigator, void* causer) {
    if (instigator == nullptr || object_present(instigator) || !object_listed(causer)) {
        return instigator;
    }
    if (note_event_cap(g_c476_damage_lines, kC476DamageLines, "card476 freed damage instigators")) {
        log::line("WARN card476: host t=%llu damage by %s: its instigator is freed, the causer stands in",
                  static_cast<unsigned long long>(GetTickCount64()), class_name(causer).text);
    }
    return causer;
}

void* detour_c74_damage(void* results, void* instigator, void* causer, const void* damage, const void* tags,
                        const void* hit, const void* tag) {
    instigator = c476_live_instigator(instigator, causer);
    const C102Scope saved = c102_enter(instigator != nullptr ? instigator : causer, kC102Damage); // card 102
    void* const ret = g_orig_c74_damage(results, instigator, causer, damage, tags, hit, tag);
    c102_leave(saved);
    // Card 476: the rows below name only live objects (a freed one reads as null).
    const void* const live_instigator = object_listed(instigator) ? instigator : nullptr;
    const void* const live_causer = object_listed(causer) ? causer : nullptr;
    if (g_c76_log) {
        c76_damage(results, live_instigator, live_causer);
    }
    if (c74_note(kC74Damage)) {
        log::line("card074: %s t=%llu ApplyDamageFromImpactHitResult #%u: instigator %s, causer %s, %d result(s), "
                  "caller exe+0x%llX", side_text(), static_cast<unsigned long long>(GetTickCount64()),
                  g_c74_calls[kC74Damage], object_name(live_instigator).text, class_name(live_causer).text,
                  read_i32(results, kAttackResultsNum), exe_rva(_ReturnAddress()));
    }
    return ret;
}

void* detour_c74_zone(void* context, const void* zone, const void* hit) {
    void* const ret = g_orig_c74_zone(context, zone, hit);
    if (c74_capturing() && g_c74_fx_got.zone == nullptr) {
        g_c74_fx_got.zone = ret;
    }
    if (ret != nullptr) {
        c132_fx_probe("TrySpawnDamageZone", class_name(ret).text, nullptr, ret, _ReturnAddress()); // card 132 step 2e
    }
    if (g_c74_log && c74_note(kC74Zone)) {
        log::line("card074: %s t=%llu TrySpawnDamageZone #%u: context %s -> %s (%p), caller exe+0x%llX", side_text(),
                  static_cast<unsigned long long>(GetTickCount64()), g_c74_calls[kC74Zone], object_name(context).text,
                  class_name(ret).text, ret, exe_rva(_ReturnAddress()));
    }
    return ret;
}

// The throw's animation task: Activate binds the animator's delegates, which call DoAction (the notify that runs
// OnDoAction) and AnimationEnded.
void c74_log_anim(C74Hook h, void* task, const void* ra) {
    if (c74_note(h)) {
        log::line("card074: %s t=%llu %s #%u task %p, caller exe+0x%llX", side_text(),
                  static_cast<unsigned long long>(GetTickCount64()), kC74HookNames[h], g_c74_calls[h], task,
                  exe_rva(ra));
    }
}

void detour_c74_anim_activate(void* task) {
    c74_log_anim(kC74AnimActivate, task, _ReturnAddress());
    g_orig_c74_anim[0](task);
}

void detour_c74_anim_do(void* task) {
    c74_log_anim(kC74AnimDo, task, _ReturnAddress());
    g_c74_pe = g_c74_calls[kC74AnimDo] <= kC74Logs;
    g_orig_c74_anim[1](task);
    g_c74_pe = false;
}

void detour_c74_anim_end(void* task) {
    c74_log_anim(kC74AnimEnd, task, _ReturnAddress());
    g_orig_c74_anim[2](task);
}

// Card 074: the object's FName as ASCII into out (cap chars with the terminator), for names longer than ObjectName
// holds. Returns the name's full length; the name is whole in out only when that is below cap. 0 when unknown.
uint32_t object_name_into(const void* object, char* out, uint32_t cap) {
    if (object == nullptr) {
        out[0] = '\0';
        return 0;
    }
    return fname_into(static_cast<const uint8_t*>(object) + kObjectName, out, cap);
}

uint32_t fname_into(const void* fname, char* out, uint32_t cap) {
    const uint8_t* const entry = fname_entry_checked(fname); // card 238
    uint16_t header = 0;
    if (entry != nullptr) {
        memcpy(&header, entry, sizeof(header));
    }
    const bool wide = (header & 1u) != 0;
    const uint32_t len = static_cast<uint32_t>(header >> 6);
    const uint32_t n = len < cap - 1 ? len : cap - 1;
    for (uint32_t i = 0; i < n; ++i) {
        uint16_t c = entry[2 + i];
        if (wide) {
            memcpy(&c, entry + 2 + 2 * static_cast<size_t>(i), sizeof(c));
        }
        out[i] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
    }
    out[n] = '\0';
    return len;
}

// Once a second: the local pawn's line, and on the host the first remote player's server copy's.
void c73_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    if (now < g_c73.next_ms) {
        return;
    }
    g_c73.next_ms = now + kC73LineMs;
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? g_get_player_controller(world, 0) : nullptr;
    void* const copy_pc = world != nullptr && !g_client_session ? g_get_player_controller(world, 1) : nullptr;
    if (pc != nullptr) {
        c73_step_pawn(read_ptr(pc, kControllerPawn), side_text(), g_c73.last);
    }
    if (copy_pc != nullptr) {
        c73_step_pawn(read_ptr(copy_pc, kControllerPawn), "host server copy", g_c73.last_copy);
    }
}

// Installed after MH_Initialize, before the hook tables, and only with UBWCOOP_C73_LOG=1 (the ConsumeItem log detour).
bool c73_install(uint8_t* base, const IMAGE_NT_HEADERS64* nt) {
    const HookSpec hooks[] = {
        {&offsets::USTInventoryItemGameplayAbility_ConsumeItem, reinterpret_cast<void*>(&detour_c73_consume),
         reinterpret_cast<void**>(&g_orig_c73_consume)},
    };
    for (const HookSpec& h : hooks) {
        void* const target = code_address(base, nt, *h.sym);
        if (target == nullptr || !create_and_enable(target, h.detour, h.original, h.sym->name)) {
            return false;
        }
    }
    log::line("hooks: card 073 grenade log on (%zu log detour; the item and ability detours are always on)",
              sizeof(hooks) / sizeof(hooks[0]));
    return true;
}

// Installed after MH_Initialize, before the hook tables, and only with UBWCOOP_C74_LOG=1 or UBWCOOP_C76_LOG=1 (the
// FireProjectile task's Activate, SpawnProjectile, OnGrenadeSpawned and AADProjectile::LifeSpanExpired are in
// kHooksUbw).
bool c74_install(uint8_t* base, const IMAGE_NT_HEADERS64* nt) {
    const HookSpec hooks[] = {
        {&offsets::USTGrenadeThrowGameplayAbility_StartAnimations, reinterpret_cast<void*>(&detour_c74_start_anims),
         reinterpret_cast<void**>(&g_orig_c74_start_anims)},
        {&offsets::USTGrenadeThrowGameplayAbility_StartThrowingAnimation,
         reinterpret_cast<void*>(&detour_c74_start_throw), reinterpret_cast<void**>(&g_orig_c74_start_throw)},
        {&offsets::USTGrenadeThrowGameplayAbility_OnDoAction_Implementation,
         reinterpret_cast<void*>(&detour_c74_do_action), reinterpret_cast<void**>(&g_orig_c74_do_action)},
        {&offsets::ASTGrenadeProjectileBase_InitGrenade, reinterpret_cast<void*>(&detour_c74_init),
         reinterpret_cast<void**>(&g_orig_c74_init)},
        {&offsets::ASTGrenadeProjectileBase_Explode, reinterpret_cast<void*>(&detour_c74_explode),
         reinterpret_cast<void**>(&g_orig_c74_explode)},
        {&offsets::USTAbilityTask_PlayAnimation_Activate, reinterpret_cast<void*>(&detour_c74_anim_activate),
         reinterpret_cast<void**>(&g_orig_c74_anim[0])},
        // DoAction and AnimationEnded are installed always (card 104, kHooksUbw); its detours call ours when on.
        {&offsets::AADProjectile_ConsumeProjectile, reinterpret_cast<void*>(&detour_c74_consume),
         reinterpret_cast<void**>(&g_orig_c74_proj[1])},
        // SpawnSystemAtLocation and SpawnSoundAtLocation are installed always (card 136, kHooksUbw).
    };
    for (const HookSpec& h : hooks) {
        void* const target = code_address(base, nt, *h.sym);
        if (target == nullptr || !create_and_enable(target, h.detour, h.original, h.sym->name)) {
            return false;
        }
    }
    log::line("hooks: card 074 log detours installed (%zu; card 074 log %s, card 076 log %s)",
              sizeof(hooks) / sizeof(hooks[0]), g_c74_log ? "on" : "off", g_c76_log ? "on" : "off");
    return true;
}

// Both sides, from the card 074 pooler detour: a projectile spawned near a mech.
void c76_spawn(const void* init_data, const double* transform, const void* proj) {
    const Vec3 at = read_vec3(transform, kTransformTranslation);
    if (!c76_near_mech(&at) || !c76_note(kC76Spawn)) {
        return;
    }
    log::line("card076: %s t=%llu SpawnProjectile near a mech (%.0f uu): init data %s at (%.1f, %.1f, %.1f) -> %s, "
              "remote role %u", side(), c76_now(), c76_mech_dist(at), object_name(init_data).text, at.x, at.y, at.z,
              class_name(proj).text, proj != nullptr ? static_cast<const uint8_t*>(proj)[kActorRemoteRole] : 0u);
}

// Both sides, from the card 074 damage detour: impact damage a mech instigated (other damage is not counted).
void c76_damage(const void* results, const void* instigator, const void* causer) {
    if (g_c76.mechs == 0 || !is_a(instigator, g_mech_class()) || !c76_note(kC76Damage)) {
        return;
    }
    log::line("card076: %s t=%llu ApplyDamageFromImpactHitResult: instigator %s (%s), causer %s, %d result(s)",
              side(), c76_now(), object_name(instigator).text, class_name(instigator).text, class_name(causer).text,
              read_i32(results, kAttackResultsNum));
}

// Both sides, from the Niagara detours: a system spawned near a mech (muzzle, tracer or impact).
void c76_fx(const char* what, const void* system, const Vec3* at) {
    if (!c76_near_mech(at) || !c76_note(kC76Fx)) {
        return;
    }
    log::line("card076: %s t=%llu %s %s at (%.0f, %.0f, %.0f), %.0f uu from a mech", side(), c76_now(), what,
              object_name(system).text, at->x, at->y, at->z, c76_mech_dist(*at));
}

// Card 326: the grenades in flight (the registry's lost-rows check).
int32_t c74_live() {
    int32_t n = 0;
    for (uint32_t i = 0; i < kC74Live; ++i) {
        n += g_c74_live[i].proj != nullptr ? 1 : 0;
    }
    return n;
}

// Card 356 (host): every item of pc's server copy's item inventory, definition and quantity, as GrenadeItem frames.
// The spawn-time ResetInventory adds the loadout before the copy is possessed (sprint_side reads host-own there), so
// c73_relay sends nothing for it; this replay at the first Pong after each arrive (the first spawn and every respawn
// in place, which reconnects) is the loadout's only path to the client. The client applies each through c73_receive.
void c73_send_all(void* pc) {
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    void* const inventory = c73_item_inventory(pawn);
    const uint32_t side = sprint_side(pawn);
    if (inventory == nullptr || side != kSideServerCopy) {
        // No item inventory: a front end PC (no pawn, or a menu pawn); nothing to send. One that is not the server
        // copy has no client to own its counts.
        log::line("%scard356: host t=%llu loadout replay to %s: none sent (pawn %s, item inventory %s, side %u)",
                  inventory != nullptr ? "WARN " : "", static_cast<unsigned long long>(GetTickCount64()),
                  object_name(pc).text, class_name(pawn).text, inventory != nullptr ? "yes" : "no", side);
        return;
    }
    const auto* const data = static_cast<const uint8_t*>(read_ptr(inventory, kItemInvItems));
    const int32_t n = read_i32(inventory, kItemInvNum);
    char list[512];
    size_t used = 0;
    list[0] = '\0';
    int32_t sent = 0;
    int32_t failed = 0;
    for (int32_t i = 0; i < n && i < kC73MaxItems && data != nullptr; ++i) {
        void* const item = read_ptr(data, static_cast<size_t>(i) * kItemInvEntryBytes);
        const uint8_t* const prop = item != nullptr ? prop_find_prop(read_ptr(item, kObjectClass), "ItemDefinition")
                                                    : nullptr;
        void* const definition = prop != nullptr ? read_ptr(item, static_cast<size_t>(read_i32(prop, kPropertyOffset)))
                                                 : nullptr;
        char text[kC73ItemText];
        bool whole = false;
        const bool ok = definition != nullptr && c73_send_item(pc, definition, c73_quantity(item), text, &whole);
        sent += ok ? 1 : 0;
        failed += ok ? 0 : 1;
        const int w = used < sizeof(list) ? snprintf(list + used, sizeof(list) - used, " %s%s", ok ? "" : "(not sent) ",
                                                     definition != nullptr ? text : "no definition")
                                          : 0;
        used += w > 0 ? static_cast<size_t>(w) : 0;
    }
    log::line("%scard356: host t=%llu loadout replay to %s, server copy %s: %d of %d items sent:%s", failed > 0 ? "WARN " : "",
              static_cast<unsigned long long>(GetTickCount64()), object_name(pc).text, object_name(pawn).text, sent, n,
              list);
}

// Card 323 "grenades" join replay (host): every throw still in flight goes to the joining PC alone as one GrenadeSpawn
// frame with its own throw number, built from the projectile's transform and velocity now (not the throw's), so the
// joiner's copy appears where the host's grenade is and ends on the same GrenadeExplode. A throw whose projectile is
// gone (weak no longer names it) or whose class was not kept is skipped and counted.
void c74_join_replay(void* pc) {
    c73_send_all(pc); // card 356: the joiner's own loadout counts first
    int32_t sent = 0;
    int32_t gone = 0;
    for (uint32_t i = 0; i < kC74Live; ++i) {
        C74Live& e = g_c74_live[i];
        if (e.proj == nullptr) {
            continue;
        }
        const void* const root = c31_weak_get(e.proj_weak) == e.proj ? read_ptr(e.proj, kActorRootComponent) : nullptr;
        if (root == nullptr || e.cls == nullptr) {
            // A projectile destroyed with no explosion (c335-range-load on b25: thrown 4 s before a checkpoint load,
            // which destroys every actor that is not placed). The joiner's map copy is fresh, so there is nothing to
            // end there; the row is freed.
            // Only a gone projectile frees its row: a live one with no kept class still needs its row for the
            // explosion relay to the clients that already have its copy.
            ++gone;
            e.proj = root == nullptr ? nullptr : e.proj;
            continue;
        }
        char text[kC74PathChars + kNameChars + 160];
        const bool whole = c74_transform_text(e.seq, static_cast<const uint8_t*>(root) + kComponentRotation, e.proj,
                                              e.cls, text, sizeof(text));
        static wchar_t wire[kChanTextFrame];
        const WireString w = whole ? chan_text_frame(ChanOp::GrenadeSpawn, text, wire) : WireString{wire, 0, 0};
        sent += w.num > 0 && chan_send_client(pc, w) ? 1 : 0;
    }
    log::line("card323: host t=%llu grenades join replay to %s: %d in flight sent, %d gone (row freed)",
              static_cast<unsigned long long>(GetTickCount64()), object_name(pc).text, sent, gone);
}

// b234 (user: "they are trying to shoot the bug nests... just give them enough grenades to kill nests"), test only,
// host: `grenade fill <n>` raises every roster player's BP_GrenadeItem_Frag_C count to n through AddItemByDefinition,
// so detour_c73_add relays a server copy's new count to its client (card 073) as for a pickup. A count already at n
// or over is left. One row per player. Null when run.
const char* c234_grenade_fill(void* world, int32_t want, uint32_t seq) {
    if (g_client_session || world == nullptr || g_c73_add_item == nullptr) {
        return "grenade fill runs on the host with AddItemByDefinition";
    }
    void* const definition = find_class(L"/Game/Blueprints/Inventory/BP_GrenadeItem_Frag.BP_GrenadeItem_Frag_C");
    if (definition == nullptr) {
        return "BP_GrenadeItem_Frag_C is not loaded";
    }
    const C43Roster r = c43_roster(world);
    int32_t filled = 0;
    for (int32_t s = 0; s < kC43Max; ++s) {
        void* const inventory = r.p[s].pawn != nullptr ? c73_item_inventory(r.p[s].pawn) : nullptr;
        if (inventory == nullptr) {
            continue; // no player in the slot, or a mech (no item inventory)
        }
        const int32_t before = c73_count(inventory, definition);
        if (before < want) {
            void* def = definition;
            g_c73_add_item(inventory, &def, want - before);
            ++filled;
        }
        log::line("cmd: %u grenade fill: %s %s %d to %d", seq, object_name(r.p[s].pawn).text,
                  object_name(definition).text, before, c73_count(inventory, definition));
    }
    log::line("cmd: %u grenade fill: %d of %u players raised to %d", seq, filled, r.players, want);
    return nullptr;
}

} // namespace ubw::hooks
