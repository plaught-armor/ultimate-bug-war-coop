// Ammo: why the client had no reserve ammo and could not reload (card 069), the ammo a pickup credits to a
// client's server copy (card 071) and the client-owned ammunition counts mirrored on its server copy
// (card 072).

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

constexpr const char* kC69HookNames[kC69Hooks] = {"ResetInventory", "AddAmmo",       "SetAmmoCount",
                                                  "RequestAmmo",    "PerformReload", "DoAction(reload)"};
constexpr uint32_t kC69Logs = 20;               // logged calls per hook per map; the rest are counted

constexpr size_t kMapNum = 0x8;                 // TSparseArray Data.Num, from the map's start
constexpr size_t kMapNumFree = 0x34;            // TSparseArray NumFreeIndices (after Data 0x10, bits 0x20, first free)
constexpr size_t kAmmoElementBytes = 0x14;      // TSetElement<TPair<FGameplayTag, int32>>: tag 8 (align 4), count, hash links
constexpr int32_t kC69MapShown = 4;

C69VoidFn g_orig_c69_reset = nullptr;
C69AmmoFn g_orig_c69_add = nullptr;
C69AmmoFn g_orig_c69_set = nullptr;
C69RequestFn g_orig_c69_request = nullptr;
C69VoidFn g_orig_c69_perform = nullptr;
C69VoidFn g_orig_c69_do_action = nullptr;
C69GetAmmoFn g_c69_get_ammo = nullptr;
C69GetMaxFn g_c69_get_max = nullptr;
uint32_t g_c69_calls[kC69Hooks] = {};
ULONGLONG g_c69_next_ms = 0;

// Counts the call; true while it is one of the first kC69Logs of its hook this map.
bool c69_note(C69Hook h) {
    ++g_c69_calls[h];
    return g_c69_calls[h] <= kC69Logs;
}

void c69_log_ammo(C69Hook h, const void* inventory, uint64_t tag, int32_t amount, int32_t ret, const void* ra) {
    const void* const owner = inventory != nullptr ? read_ptr(inventory, kComponentOwner) : nullptr;
    log::line("card069: %s %s #%u inventory %p owner %s (%s), tag %s, amount %d, returned %d, caller exe+0x%llX",
              side_text(), kC69HookNames[h], g_c69_calls[h], inventory, object_name(owner).text,
              class_name(owner).text, fname_text(&tag).text, amount, ret, exe_rva(ra));
}

void c69_log_self(C69Hook h, const void* self, const char* when, const void* ra) {
    log::line("card069: %s %s #%u %s: %p %s (%s), caller exe+0x%llX", side_text(), kC69HookNames[h], g_c69_calls[h],
              when, self, object_name(self).text, class_name(self).text, exe_rva(ra));
}

void detour_c69_reset(void* pawn) {
    const bool log_it = c69_note(kC69Reset);
    if (log_it) {
        c69_log_self(kC69Reset, pawn, "before", _ReturnAddress());
    }
    g_orig_c69_reset(pawn);
    if (log_it) {
        c69_log_self(kC69Reset, pawn, "after", _ReturnAddress());
    }
}

// Card 333: ResetInventory's loadout AddAmmo (exe+0x51DA71D, ASTPlayerCharacterMI::ResetInventory +0x2ed; the caller
// overwrites eax, so the result is discarded). The client's own reset adds its own loadout's ammo, which after a
// seamless travel is still the last map's (b44: the FED_C2M1 reset added AmmunitionType.Laser 300, the C1M1 training
// rifle's, while the server copy's reset added Morita 240 and UnderslungShotgun 8). The ammo map has no removal, so
// the stale tag stayed and the AmmoMirror then put it on the copy. The server copy runs the same reset and its
// AmmoSet (c72_seed) writes our counts, so in a client session the local add is not run.
constexpr uint64_t kC333LoadoutAmmoCaller = 0x51DA71D;
constexpr uint32_t kC333Lines = 20;
uint32_t g_c333_ammo_lines = 0;

// Always detoured (kHooksUbw, card 333); logs the card 069 rows only with UBWCOOP_C69_LOG=1.
int32_t detour_c69_add(void* inventory, uint64_t tag, int32_t amount) {
    if (c354_own_local(inventory) && exe_rva(_ReturnAddress()) == kC333LoadoutAmmoCaller) {
        const int32_t count = g_c69_get_ammo(inventory, tag, false);
        if (note_event_cap(g_c333_ammo_lines, kC333Lines, "card333 loadout ammo rows")) {
            log::line("card333: client t=%llu own loadout AddAmmo %s %d (caller exe+0x%llX): local add not run, count "
                      "%d; the host's AmmoSet writes it",
                      static_cast<unsigned long long>(GetTickCount64()), fname_text(&tag).text, amount,
                      static_cast<unsigned long long>(kC333LoadoutAmmoCaller), count);
        }
        return count;
    }
    const int32_t ret = g_orig_c69_add(inventory, tag, amount);
    if (g_c69_log && c69_note(kC69Add)) {
        c69_log_ammo(kC69Add, inventory, tag, amount, ret, _ReturnAddress());
    }
    return ret;
}

int32_t detour_c69_set(void* inventory, uint64_t tag, int32_t amount) {
    const int32_t ret = g_orig_c69_set(inventory, tag, amount);
    if (c69_note(kC69Set)) {
        c69_log_ammo(kC69Set, inventory, tag, amount, ret, _ReturnAddress());
    }
    return ret;
}

int32_t detour_c69_request(void* inventory, uint64_t tag, int32_t amount, bool flag) {
    const int32_t ret = g_orig_c69_request(inventory, tag, amount, flag);
    if (c69_note(kC69Request)) {
        c69_log_ammo(kC69Request, inventory, tag, amount, ret, _ReturnAddress());
    }
    return ret;
}

void detour_c69_perform(void* action) {
    if (c69_note(kC69Perform)) {
        c69_log_self(kC69Perform, action, "enter", _ReturnAddress());
    }
    g_orig_c69_perform(action);
}

void detour_c69_do_action(void* action) {
    if (c69_note(kC69DoAction)) {
        c69_log_self(kC69DoAction, action, "enter", _ReturnAddress());
    }
    g_orig_c69_do_action(action);
}

// The live entry count of a TMap at `map` (Data.Num minus the free list), or -1 without an inventory.
int32_t c69_map_count(const void* inventory, size_t map) {
    if (inventory == nullptr) {
        return -1;
    }
    return read_i32(inventory, map + kMapNum) - read_i32(inventory, map + kMapNumFree);
}

// The first AmmoMap entries as "tag=count/GetAmmoCount/GetMaxAmmo"; only when the sparse array has no holes (a free
// slot holds no tag).
void c69_map_text(const void* inventory, char* out, size_t cap) {
    out[0] = '\0';
    if (inventory == nullptr || read_i32(inventory, kAmmoMap + kMapNumFree) != 0) {
        return;
    }
    const auto* const data = static_cast<const uint8_t*>(read_ptr(inventory, kAmmoMap));
    const int32_t n = read_i32(inventory, kAmmoMap + kMapNum);
    size_t used = 0;
    for (int32_t i = 0; i < n && i < kC69MapShown && data != nullptr && used < cap; ++i) {
        const uint8_t* const e = data + static_cast<size_t>(i) * kAmmoElementBytes;
        const uint64_t tag = read_u64(e, 0);
        const int w = snprintf(out + used, cap - used, " %s=%d/%d/%d", fname_text(e).text, read_i32(e, sizeof(tag)),
                               g_c69_get_ammo(inventory, tag, false), g_c69_get_max(inventory, tag));
        used += w > 0 ? static_cast<size_t>(w) : cap;
    }
}

constexpr int32_t kC71MaxTags = 8;     // tags per AddAmmunitions call that are measured and sent
constexpr int32_t kFNameFind = 0;      // EFindName::FNAME_Find: no new names
C71AddAmmunitionsFn g_orig_c71_add_ammunitions = nullptr;
C69AmmoFn g_c71_add_ammo = nullptr;    // USTAmmunitionInventory::AddAmmo (through the card 069 log detour when on)

struct C71Credit {
    int32_t n;                       // tags measured (at most kC71MaxTags)
    uint64_t tag[kC71MaxTags];
    int32_t asked[kC71MaxTags];      // the pickup's amount for the tag
    int32_t before[kC71MaxTags];
    int32_t after[kC71MaxTags];
};

// Host: the tags and requested amounts of one AddAmmunitions call, with the inventory's counts before it.
C71Credit c71_measure(const void* inventory, const void* tags, const void* amounts) {
    C71Credit c{};
    const auto* const t = static_cast<const uint64_t*>(read_ptr(tags, 0));
    const auto* const a = static_cast<const int32_t*>(read_ptr(amounts, 0));
    const int32_t n = read_i32(tags, kArrayNum);
    const int32_t na = read_i32(amounts, kArrayNum);
    c.n = t == nullptr || n < 0 ? 0 : (n < kC71MaxTags ? n : kC71MaxTags);
    for (int32_t i = 0; i < c.n; ++i) {
        c.tag[i] = t[i];
        c.asked[i] = a != nullptr && i < na ? a[i] : 0;
        c.before[i] = inventory != nullptr ? g_c69_get_ammo(inventory, c.tag[i], false) : 0;
    }
    return c;
}

// Host: the credit as the frame text (tags with a gain only) and as a log text (every tag, with the copy's max).
int32_t c71_texts(const void* inventory, const C71Credit& c, char* frame, size_t frame_cap, char* log_text,
                  size_t log_cap) {
    size_t used = 0;
    size_t logged = 0;
    frame[0] = '\0';
    log_text[0] = '\0';
    for (int32_t i = 0; i < c.n; ++i) {
        const ObjectName name = fname_text(&c.tag[i]);
        const int32_t gain = c.after[i] - c.before[i];
        const int32_t max = inventory != nullptr ? g_c69_get_max(inventory, c.tag[i]) : -1;
        const int lw = snprintf(log_text + logged, log_cap - logged, " %s asked %d, %d to %d (max %d)%s", name.text,
                                c.asked[i], c.before[i], c.after[i], max, c.after[i] >= max ? " AT MAX" : "");
        logged = lw > 0 && static_cast<size_t>(lw) < log_cap - logged ? logged + static_cast<size_t>(lw) : logged;
        if (gain <= 0) {
            continue;
        }
        const int fw = snprintf(frame + used, frame_cap - used, "%s%s %d", used > 0 ? " " : "", name.text, gain);
        if (fw <= 0 || static_cast<size_t>(fw) >= frame_cap - used) {
            break;
        }
        used += static_cast<size_t>(fw);
    }
    return static_cast<int32_t>(used);
}

// Host: after the copy's credit, send the gain to the copy's client; log every AddAmmunitions on a player pawn.
void c71_after_credit(void* actor, const void* inventory, C71Credit& c, int32_t result) {
    for (int32_t i = 0; i < c.n; ++i) {
        c.after[i] = inventory != nullptr ? g_c69_get_ammo(inventory, c.tag[i], false) : 0;
    }
    static char frame[kChanTextMax + 1];
    static char log_text[512];
    const int32_t len = c71_texts(inventory, c, frame, sizeof(frame), log_text, sizeof(log_text));
    const bool copy = sprint_side(actor) == kSideServerCopy;
    void* const pc = copy ? read_ptr(actor, kPawnController) : nullptr;
    static wchar_t wire[kChanTextFrame];
    const WireString w = len > 0 && pc != nullptr ? chan_text_frame(ChanOp::Ammo, frame, wire) : WireString{wire, 0, 0};
    const bool sent = w.num > 0 && chan_send_client(pc, w);
    const char* const what = !copy ? "own pawn, not sent" : (len == 0 ? "nothing gained, not sent"
                                                                    : (sent ? "Ammo sent" : "WARN Ammo not sent"));
    log::line("card071: host t=%llu AddAmmunitions on %s %s (%s), returned %d:%s; %s%s%s",
              static_cast<unsigned long long>(GetTickCount64()), copy ? "server copy" : "pawn", object_name(actor).text,
              pc != nullptr ? object_name(pc).text : "no PC", result, log_text, what, sent ? ": " : "",
              sent ? frame : "");
    C50Copy* const row = sent ? c50_copy(object_index(pc), false) : nullptr;
    if (row != nullptr) {
        row->ammo[0] = '\0'; // card 072: the client's next AmmoMirror carries the gain; until then nothing re-applies
    }
}

// Card 384: one take, one credit. A pickup the client's own pawn overlaps credits it locally (the subclass
// Pickup_Implementation runs before card 254's cut on the base) and the host credits the server copy and sends the
// same amount on an Ammo frame, so the client gained twice (c276b67: one MoritaSmall of 120 took the client from 0
// to 240 while the copy went 0 to 120). Both credits of one take land within the frame's travel time, so each
// side's gain is kept per tag for kC384PairMs and the later one is reduced by the earlier one. A local credit with
// no host frame (or a frame with no local credit) is left whole. Client, game thread.
constexpr ULONGLONG kC384PairMs = 2000;
constexpr int32_t kC384Rows = 8;
constexpr uint32_t kC384Lines = 40;
struct C384Gain {
    uint64_t tag;
    int32_t gain;     // not yet paired; 0 = free row
    ULONGLONG at;
    bool host;        // true: applied from the host's frame; false: our own local AddAmmunitions
};
C384Gain g_c384[kC384Rows]{};
uint32_t g_c384_lines = 0;

// The part of `gain` already credited by the other side for this tag (taken off its rows); the rest is noted as ours.
int32_t c384_pair(uint64_t tag, int32_t gain, bool host, ULONGLONG now) {
    int32_t paired = 0;
    C384Gain* free_row = nullptr;
    for (int32_t i = 0; i < kC384Rows; ++i) {
        C384Gain& r = g_c384[i];
        if (r.gain > 0 && now - r.at > kC384PairMs) {
            r.gain = 0;
        }
        if (r.gain > 0 && r.tag == tag && r.host != host && paired < gain) {
            const int32_t take = r.gain < gain - paired ? r.gain : gain - paired;
            r.gain -= take;
            paired += take;
        }
        free_row = r.gain == 0 && free_row == nullptr ? &r : free_row;
    }
    if (paired < gain && free_row != nullptr) {
        *free_row = C384Gain{tag, gain - paired, now, host};
    }
    return paired;
}

// Client: our own pawn's local AddAmmunitions. A gain the host's frame already gave is taken back off the count.
int32_t c384_local_add(void* actor, const void* tags, const void* amounts) {
    void* const inventory = read_ptr(actor, kMiAmmoInventory);
    C71Credit c = c71_measure(inventory, tags, amounts);
    const int32_t result = g_orig_c71_add_ammunitions(actor, tags, amounts);
    const ULONGLONG now = GetTickCount64();
    for (int32_t i = 0; i < c.n && inventory != nullptr; ++i) {
        c.after[i] = g_c69_get_ammo(inventory, c.tag[i], false);
        const int32_t gain = c.after[i] - c.before[i];
        const int32_t paired = gain > 0 ? c384_pair(c.tag[i], gain, false, now) : 0;
        if (paired > 0) {
            g_c72_set_ammo(inventory, c.tag[i], c.after[i] - paired);
        }
        if (gain > 0 && note_event_cap(g_c384_lines, kC384Lines, "card384 rows")) {
            log::line("card384: client t=%llu local AddAmmunitions %s %d to %d: %d already given by the host's frame, "
                      "count now %d", static_cast<unsigned long long>(now), fname_text(&c.tag[i]).text, c.before[i],
                      c.after[i], paired, g_c69_get_ammo(inventory, c.tag[i], false));
        }
    }
    return result;
}

// Installed always (kHooksUbw). The host's player pawns are measured and a client's own pawn is paired (card 384);
// everything else passes straight through.
int32_t detour_c71_add_ammunitions(void* actor, const void* tags, const void* amounts) {
    if (tags == nullptr || amounts == nullptr || !is_a(actor, g_player_mi_class())) {
        return g_orig_c71_add_ammunitions(actor, tags, amounts);
    }
    if (g_client_session) {
        return sprint_side(actor) == kSideClientOwn ? c384_local_add(actor, tags, amounts)
                                                    : g_orig_c71_add_ammunitions(actor, tags, amounts);
    }
    const void* const inventory = read_ptr(actor, kMiAmmoInventory);
    C71Credit c = c71_measure(inventory, tags, amounts);
    const int32_t result = g_orig_c71_add_ammunitions(actor, tags, amounts);
    c71_after_credit(actor, inventory, c, result);
    return result;
}

// An existing FName for the text (the find flag: no new names); 0 when there is none.
uint64_t c71_find_tag(const char* tag_text) {
    wchar_t w[kChanNameMax + 1];
    const size_t n = strlen(tag_text);
    for (size_t i = 0; i <= n && i <= static_cast<size_t>(kChanNameMax); ++i) {
        w[i] = static_cast<wchar_t>(tag_text[i]);
    }
    w[kChanNameMax] = L'\0';
    uint64_t tag = 0;
    g_fname_ctor(&tag, w, kFNameFind);
    return tag;
}

// Client: one "<tag> <amount>" pair onto our own inventory. False when the pair does not parse.
bool c71_apply_pair(void* inventory, const char* tag_text, int32_t amount) {
    const uint64_t tag = c71_find_tag(tag_text);
    if (tag == 0 || inventory == nullptr || amount <= 0) {
        log::line("WARN card071: client t=%llu Ammo %s %d: %s", static_cast<unsigned long long>(GetTickCount64()),
                  tag_text, amount, tag == 0 ? "no such name" : (inventory == nullptr ? "no ammunition inventory"
                                                                                       : "bad amount"));
        return false;
    }
    const int32_t before = g_c69_get_ammo(inventory, tag, false);
    // Card 384: only what the frame adds on top of our own local credit of the same take is applied and noted.
    const int32_t local = c384_pair(tag, amount, true, GetTickCount64());
    const int32_t ret = local < amount ? g_c71_add_ammo(inventory, tag, amount - local) : 0;
    const int32_t after = g_c69_get_ammo(inventory, tag, false);
    log::line("card071: client t=%llu Ammo %s +%d (card 384: %d already credited locally): %d to %d, AddAmmo "
              "returned %d", static_cast<unsigned long long>(GetTickCount64()), tag_text, amount, local, before,
              after, ret);
    return true;
}

// Client: the host's Ammo frame, added to our own pawn's USTAmmunitionInventory.
void c71_receive(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    if (chan_text(s, text, sizeof(text)) <= 0) {
        log::line("WARN card071: client: bad Ammo frame");
        return;
    }
    void* const pawn = read_ptr(pc, kControllerPawn);
    void* const inventory = is_a(pawn, g_player_mi_class()) ? read_ptr(pawn, kMiAmmoInventory) : nullptr;
    const char* p = text;
    for (int32_t i = 0; i < kC71MaxTags && *p != '\0'; ++i) {
        char tag_text[kChanNameMax + 1];
        int32_t amount = 0;
        int used = 0;
        if (sscanf_s(p, "%64s %d%n", tag_text, static_cast<unsigned>(sizeof(tag_text)), &amount, &used) != 2) {
            log::line("WARN card071: client: bad Ammo pair in \"%s\"", text);
            return;
        }
        c71_apply_pair(inventory, tag_text, amount);
        p += used;
        p += *p == ' ' ? 1 : 0;
    }
}

C69AmmoFn g_c72_set_ammo = nullptr;     // USTAmmunitionInventory::SetAmmoCount (through the card 069 log detour when on)

constexpr ULONGLONG kC72HeldSetMs = 10000; // card 073: a held AmmoSet waits this long for the own pawn's AmmoMap

C72Client g_c72{}; // client, game thread

// The inventory's AmmoMap as "<tag> <count> ..." (at most kC71MaxTags entries). -1 when the sparse array has holes
// (a free slot holds no tag; card 069 reads it the same way), else the text length (0 = an empty map).
int32_t c72_map_text(const void* inventory, char* out, size_t cap) {
    out[0] = '\0';
    if (inventory == nullptr) {
        return 0;
    }
    if (read_i32(inventory, kAmmoMap + kMapNumFree) != 0) {
        return -1;
    }
    const auto* const data = static_cast<const uint8_t*>(read_ptr(inventory, kAmmoMap));
    const int32_t n = read_i32(inventory, kAmmoMap + kMapNum);
    size_t used = 0;
    for (int32_t i = 0; i < n && i < kC71MaxTags && data != nullptr; ++i) {
        const uint8_t* const e = data + static_cast<size_t>(i) * kAmmoElementBytes;
        const int w = snprintf(out + used, cap - used, "%s%s %d", used > 0 ? " " : "", fname_text(e).text,
                               read_i32(e, sizeof(uint64_t)));
        if (w <= 0 || static_cast<size_t>(w) >= cap - used) {
            break;
        }
        used += static_cast<size_t>(w);
    }
    out[used] = '\0';
    return static_cast<int32_t>(used);
}

// One "<tag> <count>" pair set on the inventory, clamped to 0 and (when max_cap) to GetMaxAmmo; appends
// " <tag> before to after (max M)" to the log text when the count changed. False when the tag does not resolve.
bool c72_set_pair(void* inventory, const char* tag_text, int32_t count, bool max_cap, char* log_text, size_t& logged,
                  size_t log_cap) {
    const uint64_t tag = c71_find_tag(tag_text);
    if (tag == 0) {
        return false;
    }
    const int32_t max = g_c69_get_max(inventory, tag);
    const int32_t want = count < 0 ? 0 : (max_cap && count > max ? (max > 0 ? max : 0) : count);
    const int32_t before = g_c69_get_ammo(inventory, tag, false);
    if (before == want) {
        return true;
    }
    const int32_t ret = g_c72_set_ammo(inventory, tag, want);
    const int32_t after = g_c69_get_ammo(inventory, tag, false);
    const int w = snprintf(log_text + logged, log_cap - logged, " %s %d to %d (asked %d, max %d, ret %d)", tag_text,
                           before, after, count, max, ret);
    logged = w > 0 && static_cast<size_t>(w) < log_cap - logged ? logged + static_cast<size_t>(w) : logged;
    return true;
}

// Both sides: every pair of an AmmoSet / AmmoMirror text onto the inventory; the pairs that changed a count go
// in the log text; a bad pair or an unknown tag is a WARN naming it.
void c72_apply(void* inventory, const char* text, bool max_cap, const char* what, char* log_text, size_t log_cap) {
    size_t logged = 0;
    log_text[0] = '\0';
    const char* p = text;
    for (int32_t i = 0; i < kC71MaxTags + kC274Behaviours && *p != '\0'; ++i) {
        char tag_text[kChanNameMax + 1];
        int32_t count = 0;
        int used = 0;
        if (sscanf_s(p, "%64s %d%n", tag_text, static_cast<unsigned>(sizeof(tag_text)), &count, &used) != 2) {
            log::line("WARN card072: %s %s: bad pair in \"%s\"", side_text(), what, text);
            return;
        }
        if (tag_text[0] == kC274MagazineMark) {
            // card 274: a magazine pair; c274_magazine_apply sets it on the copy's weapon, not on the AmmoMap
        } else if (!c72_set_pair(inventory, tag_text, count, max_cap, log_text, logged, log_cap)) {
            log::line("WARN card072: %s %s %s %d: no such name", side_text(), what, tag_text, count);
        }
        p += used;
        p += *p == ' ' ? 1 : 0;
    }
}

// --- Card 274: the server copy's magazine follows the client's ---
GetEquippedAmmoCountFn g_c274_magazine_count = nullptr; // USTWeaponUtilityLibrary::GetEquippedAmmunitionCount
GetEquippedAmmoTypeFn g_c274_magazine_type = nullptr;   // USTWeaponUtilityLibrary::GetEquippedAmmunitionType

constexpr uint32_t kC274Lines = 200;
static uint32_t g_c274_lines = 0; // host, game thread

// The ammunition tag of the weapon's `behaviour` (1 primary, 2 secondary) when that ammunition uses a magazine, else 0.
static uint64_t c274_magazine_tag(const void* weapon, uint8_t behaviour) {
    if (weapon == nullptr || g_c274_magazine_type == nullptr || g_ammo_uses_magazine == nullptr) {
        return 0;
    }
    uint64_t tag = 0;
    g_c274_magazine_type(&tag, weapon, behaviour);
    return tag != 0 && g_ammo_uses_magazine(weapon, &tag, behaviour) ? tag : 0;
}

// The equipped weapon of a player pawn, null without one.
static void* c274_weapon(const void* pawn) {
    const void* const weapons = is_a(pawn, g_player_mi_class()) ? read_ptr(pawn, kMiWeaponInventory) : nullptr;
    return weapons != nullptr && g_get_equipped_weapon != nullptr ? g_get_equipped_weapon(weapons) : nullptr;
}

// Client, every frame: the sum of the equipped weapon's magazine counts, with no text built (c274_magazine_text
// wrote the pairs each frame only for this sum: `c72_client_step` 14 us mean, run c325b101a).
int32_t c274_magazine_rounds(const void* pawn) {
    int32_t rounds = 0;
    const void* const weapon = c274_weapon(pawn);
    for (uint8_t b = 1; b <= kC274Behaviours && g_c274_magazine_count != nullptr; ++b) {
        rounds += c274_magazine_tag(weapon, b) != 0 ? g_c274_magazine_count(weapon, b) : 0;
    }
    return rounds;
}

// Client: the equipped weapon's magazine counts as " @<ammunition tag> <count>" pairs (one per behaviour that uses a
// magazine), appended to `out` at `used`; *rounds gets their sum. Returns the new length.
size_t c274_magazine_text(const void* pawn, char* out, size_t used, size_t cap, int32_t* rounds) {
    *rounds = 0;
    const void* const weapon = c274_weapon(pawn);
    for (uint8_t b = 1; b <= kC274Behaviours && g_c274_magazine_count != nullptr; ++b) {
        const uint64_t tag = c274_magazine_tag(weapon, b);
        if (tag == 0) {
            continue;
        }
        const int32_t count = g_c274_magazine_count(weapon, b);
        const int w = snprintf(out + used, cap - used, "%s%c%s %d", used > 0 ? " " : "", kC274MagazineMark,
                               fname_text(&tag).text, count);
        if (w <= 0 || static_cast<size_t>(w) >= cap - used) {
            break;
        }
        used += static_cast<size_t>(w);
        *rounds += count;
    }
    out[used] = '\0';
    return used;
}

// Host: each magazine pair of a client's AmmoMirror text onto the equipped weapon of its server copy, when that weapon
// holds the same ammunition type. Only raises: the copy lags the client by the rounds in flight, so a lower count is
// the client's newer state the copy has not fired yet.
static int32_t c274_magazine_set(const void* pawn, void* weapon, const char* text, bool raise_only, const char* what);
void c274_magazine_apply(const void* pawn, const char* text, const char* what) {
    void* const weapon = sprint_side(pawn) == kSideServerCopy ? c274_weapon(pawn) : nullptr;
    (void)c274_magazine_set(pawn, weapon, text, true, what);
}

// Each magazine pair of `text` onto `weapon` where a behaviour holds the same ammunition type: raise-only (the host
// copy, card 274) or exact (card 570: the client's own weapon after a checkpoint rejoin). Returns the pairs matched.
static int32_t c274_magazine_set(const void* pawn, void* weapon, const char* text, bool raise_only, const char* what) {
    int32_t matched = 0;
    if (weapon == nullptr || g_set_equipped_ammo == nullptr) {
        return 0;
    }
    const char* p = text;
    for (int32_t i = 0; i < kC71MaxTags + kC274Behaviours && *p != '\0'; ++i) {
        char tag_text[kChanNameMax + 1];
        int32_t want = 0;
        int used = 0;
        if (sscanf_s(p, "%64s %d%n", tag_text, static_cast<unsigned>(sizeof(tag_text)), &want, &used) != 2) {
            return matched; // c72_apply logged the bad pair
        }
        p += used;
        p += *p == ' ' ? 1 : 0;
        if (tag_text[0] != kC274MagazineMark) {
            continue;
        }
        const uint64_t tag = c71_find_tag(tag_text + 1);
        for (uint8_t b = 1; b <= kC274Behaviours && tag != 0 && g_c274_magazine_count != nullptr; ++b) {
            if (c274_magazine_tag(weapon, b) != tag) {
                continue;
            }
            const int32_t before = g_c274_magazine_count(weapon, b);
            ++matched;
            if (want > before || (!raise_only && want != before)) {
                g_set_equipped_ammo(weapon, tag, want, b);
                if (note_event_cap(g_c274_lines, kC274Lines, "card274")) {
                    log::line("card274: %s magazine %s %d to %d (%s, %s, behaviour %u, %s)",
                              raise_only ? "host copy" : "client own", tag_text + 1,
                              before, g_c274_magazine_count(weapon, b), object_name(pawn).text,
                              object_name(weapon).text, b, what);
                }
            }
            break;
        }
    }
    return matched;
}

// Card 570, client: the own weapon's magazine at a Reload, set again on the new pawn after the rejoin (the Reload
// text carries the reserve map, not the magazine, and a respawned weapon starts full). Only the equipped weapon's.
struct C570Magazine {
    char text[kChanTextMax + 1];
    int32_t pawn_id;    // the pawn the text came from; the set waits for a different one
    ULONGLONG until_ms; // the set gives up then (WARN)
};
static C570Magazine g_c570_magazine{};
static C570Magazine g_c570_noted{}; // the own magazine at its last change (until_ms unused): a Reload can come after
                                    // the own pawn is gone (run c570b386a: "no own pawn" 6 ms before the Reload)
static constexpr ULONGLONG kC570MagazineMs = 60000;

// Client, from c72_client_step when the magazine sum changes (a shot or a reload, not every frame).
// A frame with no magazine weapon (a dying pawn's weapon goes first) keeps the last note.
void c570_magazine_note(const void* pawn) {
    int32_t rounds = 0;
    char text[sizeof(g_c570_noted.text)];
    if (pawn != nullptr && c274_magazine_text(pawn, text, 0, sizeof(text), &rounds) > 0) {
        memcpy(g_c570_noted.text, text, strlen(text) + 1);
        g_c570_noted.pawn_id = object_index(pawn);
    }
}

void c570_magazine_keep(const void* pawn, ULONGLONG now) {
    int32_t rounds = 0;
    g_c570_magazine = C570Magazine{};
    if (pawn != nullptr && c274_magazine_text(pawn, g_c570_magazine.text, 0, sizeof(g_c570_magazine.text), &rounds) > 0) {
        g_c570_magazine.pawn_id = object_index(pawn);
    } else if (g_c570_noted.text[0] != '\0') {
        g_c570_magazine = g_c570_noted; // the pawn went first: its last noted magazine
    }
    if (g_c570_magazine.text[0] != '\0') {
        g_c570_magazine.until_ms = now + kC570MagazineMs;
    }
    log::line("card570: client t=%llu Reload keeps own magazine: %s", static_cast<unsigned long long>(now),
              g_c570_magazine.text[0] != '\0' ? g_c570_magazine.text : "(none)");
}

void c570_magazine_step(const void* pawn, ULONGLONG now) {
    if (g_c570_magazine.text[0] == '\0') {
        return;
    }
    if (now >= g_c570_magazine.until_ms) {
        log::line("WARN card570: client t=%llu kept magazine \"%s\" not set: no matching weapon on a new pawn in %llu "
                  "ms", static_cast<unsigned long long>(now), g_c570_magazine.text,
                  static_cast<unsigned long long>(kC570MagazineMs));
        g_c570_magazine = C570Magazine{};
        return;
    }
    // c397b398a: a pawn held in the old world before the travel took the set, and the new map's pawn kept 60.
    if (pawn == nullptr || object_index(pawn) == g_c570_magazine.pawn_id || c294_restart_pending()) {
        return;
    }
    if (c274_magazine_set(pawn, c274_weapon(pawn), g_c570_magazine.text, false, "card570 Reload") > 0) {
        log::line("card570: client t=%llu kept magazine set on %s: %s", static_cast<unsigned long long>(now),
                  object_name(pawn).text, g_c570_magazine.text);
        c574_weapon_status_refresh(pawn, read_ptr(pawn, kMiWeaponInventory));
        g_c570_magazine = C570Magazine{};
    }
}

// Client: the host's AmmoSet. Held, then set on our own pawn by c72_take_set: at a respawn the frame can arrive while
// the controller has no pawn yet (card 070 run c70b), so it waits for the new pawn (card 073).
void c72_receive_set(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    if (chan_text(s, text, sizeof(text)) <= 0) {
        log::line("WARN card072: client: AmmoSet bad frame");
        return;
    }
    if (g_c72.held[0] != '\0') {
        log::line("card073: client: AmmoSet \"%s\" replaced by a newer one before it was applied", g_c72.held);
    }
    memcpy(g_c72.held, text, strlen(text) + 1);
    g_c72.held_ms = GetTickCount64();
    c72_take_set(pc, g_c72.held_ms);
}

// Client: the held AmmoSet onto our own pawn once it has an ammunition inventory. Its AmmoMap may still be empty:
// the first spawn's ResetInventory adds no entry, and SetAmmoCount adds it (card 073 run c73a). Dropped with a WARN
// after kC72HeldSetMs. True while it is still held.
bool c72_take_set(void* pc, ULONGLONG now) {
    if (g_c72.held[0] == '\0') {
        return false;
    }
    void* const pawn = read_ptr(pc, kControllerPawn);
    void* const inventory = is_a(pawn, g_player_mi_class()) ? read_ptr(pawn, kMiAmmoInventory) : nullptr;
    if (inventory == nullptr) {
        if (now - g_c72.held_ms < kC72HeldSetMs) {
            return true;
        }
        log::line("WARN card072: client: AmmoSet \"%s\" dropped: own pawn has no ammunition inventory after %llu ms",
                  g_c72.held, static_cast<unsigned long long>(now - g_c72.held_ms));
        g_c72.held[0] = '\0';
        return false;
    }
    char changes[512];
    c72_apply(inventory, g_c72.held, false, "AmmoSet", changes, sizeof(changes));
    log::line("card072: client t=%llu AmmoSet \"%s\" on %s (held %llu ms):%s", static_cast<unsigned long long>(now),
              g_c72.held, object_name(pawn).text, static_cast<unsigned long long>(now - g_c72.held_ms),
              changes[0] != '\0' ? changes : " no count changed");
    g_c72.held[0] = '\0';
    return false;
}

// Host: a client's AmmoMirror frame. Applied now, and kept on its card 050 copy row so each poll re-applies it: the
// copy runs its own reload on the relayed fire (SetAmmoCount callers 0x524BF76 / 0x524BFB6), which lowers it again.
void c72_receive_mirror(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    if (chan_text(s, text, sizeof(text)) <= 0) {
        log::line("WARN card072: host: bad AmmoMirror frame from %s", object_name(pc).text);
        return;
    }
    // Card 444: a magazine-only frame (starts with the mark) carries no reserve map: nothing to apply to the
    // inventory, and the kept text (the full map, re-applied on each poll) stays.
    if (text[0] == kC274MagazineMark) {
        c274_magazine_apply(read_ptr(pc, kControllerPawn), text, "AmmoMirror");
        return;
    }
    c72_mirror_apply(pc, text, "AmmoMirror");
    // Card 274: on the frame only, not on the card 050 poll's re-apply: a stale count would raise a copy that has
    // since fired the client's newer rounds.
    c274_magazine_apply(read_ptr(pc, kControllerPawn), text, "AmmoMirror");
    C50Copy* const c = c50_copy(object_index(pc), false);
    if (c != nullptr) {
        memcpy(c->ammo, text, strlen(text) + 1);
    }
}

// Once a second: the local pawn's equipped weapon and its type tag, the reserve of its ammunition type
// (GetAmmoCountOfEquippedAmmunitionType), the AmmoMap entries with GetAmmoCount/GetMaxAmmo of each ammunition tag
// (the map is keyed by ammunition type, not weapon type), and the hook call counts so far this map.
void c69_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    if (now < g_c69_next_ms) {
        return;
    }
    g_c69_next_ms = now + kC69LineMs;
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? g_get_player_controller(world, 0) : nullptr;
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    if (!is_a(pawn, g_player_mi_class())) {
        return; // the ammunition inventory is an ASTPlayerCharacterMI field
    }
    const void* const ammo = read_ptr(pawn, kMiAmmoInventory);
    const void* const weapons = read_ptr(pawn, kMiWeaponInventory);
    void* const weapon = weapons != nullptr ? g_get_equipped_weapon(weapons) : nullptr;
    const uint64_t tag = weapon != nullptr ? read_u64(weapon, kWeaponTypeTag) : 0;
    char entries[160];
    c69_map_text(ammo, entries, sizeof(entries));
    log::line("card069: %s 1s pawn %p %s weapon %s tag %s, equipped type count %d, AmmoMap %d [%s ] (count/"
              "GetAmmoCount/GetMaxAmmo), MaxAmmoMap %d, calls reset %u add %u set %u request %u perform %u doaction %u",
              side_text(), pawn, class_name(pawn).text, class_name(weapon).text, fname_text(&tag).text,
              weapon != nullptr ? g_get_ammo_count(weapon) : -1,
              c69_map_count(ammo, kAmmoMap), entries, c69_map_count(ammo, kAmmoMaxMap), g_c69_calls[kC69Reset],
              g_c69_calls[kC69Add], g_c69_calls[kC69Set], g_c69_calls[kC69Request], g_c69_calls[kC69Perform],
              g_c69_calls[kC69DoAction]);
}

// Installed after MH_Initialize, before the hook tables, and only with UBWCOOP_C69_LOG=1.
bool c69_install(uint8_t* base, const IMAGE_NT_HEADERS64* nt) {
    const HookSpec hooks[] = {
        {&offsets::ASTPlayerCharacterMI_ResetInventory, reinterpret_cast<void*>(&detour_c69_reset),
         reinterpret_cast<void**>(&g_orig_c69_reset)},
        // AddAmmo is in kHooksUbw (card 333), always detoured.
        {&offsets::USTAmmunitionInventory_SetAmmoCount, reinterpret_cast<void*>(&detour_c69_set),
         reinterpret_cast<void**>(&g_orig_c69_set)},
        {&offsets::USTAmmunitionInventory_RequestAmmo, reinterpret_cast<void*>(&detour_c69_request),
         reinterpret_cast<void**>(&g_orig_c69_request)},
        {&offsets::USTAttackActionReload_PerformReload, reinterpret_cast<void*>(&detour_c69_perform),
         reinterpret_cast<void**>(&g_orig_c69_perform)},
        {&offsets::USTAttackActionReload_DoAction, reinterpret_cast<void*>(&detour_c69_do_action),
         reinterpret_cast<void**>(&g_orig_c69_do_action)},
    };
    for (const HookSpec& h : hooks) {
        void* const target = code_address(base, nt, *h.sym);
        if (target == nullptr || !create_and_enable(target, h.detour, h.original, h.sym->name)) {
            return false;
        }
    }
    log::line("hooks: card 069 ammo log on (%zu detours)", sizeof(hooks) / sizeof(hooks[0]));
    return true;
}

} // namespace ubw::hooks
