// Card 166: the shooting client's hit marker and kill marker.
// The game raises them on the shooter only: the receiver's health code calls the instigator's
// IADDamageCauserInterface::OnDamageCaused / OnDeathCaused, and ASTCharacter's versions broadcast
// OnCharacterCausedDamage (+0x670) and OnCharacterKilledNPC (+0x660), which WB_Hit_Marker binds on
// GetSTPlayerCharacter. Damage runs only on the host, so for a client's claimed hit (card 157) those calls run on
// the host's server copy of the client's pawn and the client's marker never plays. The game has no client RPC
// for them (single-player authority code).
//
// Host: the ASTPlayerCharacter detours (OnDamageCaused, card 157; OnDeathCaused, this card) send, for a server
// copy only, one Marker text frame to that copy's PC: "<D|K> <npc guid hex> <original> <final> <health> <armour>
// <value map count> <damage type count> <receiver tag count>", then the value map as "<tag> <value>" pairs, then the
// damage type and receiver tag names (the FADDamageEvent fields the widget reads).
// Client: the frame rebuilds the event (Instigator the client's own pawn, Causer its equipped weapon, Receiver the NPC
// copy and its health component) and calls the base ASTCharacter::OnDamageCaused or OnDeathCaused on the own pawn, which only
// broadcast the delegate. The ASTPlayerCharacter versions also update the receiver's tag counts and faction state,
// which is host game state, so the client never calls them.
//
// FADDamageEvent (0x1E0): Instigator +0x00, Causer +0x08, Receiver +0x10, ReceiverHealthComponent +0x18,
// InstigatorTags +0x20, ReceiverTags +0x40 (FGameplayTagContainer, 0x20), HitInfo +0x60, DamageProcessResult +0x160:
// DamageTypes +0x160, OriginalDamage +0x180, FinalDamage +0x184, HealthDamage +0x188, ArmourDamage +0x18C,
// ValueMap +0x190 (TMap<FGameplayTag, float>, layout in hit_c132.cpp).
//
// Card 168: WB_Hit_Marker's OnDamageCaused drops the marker when DamageData.Instigator == DamageData.Causer
// (EqualEqual_ObjectObject), so the rebuilt event's Causer is the own pawn's equipped weapon (the PC with no weapon),
// never the pawn. The marker's visible change is UUserWidget::PlayAnimation (Play Hit: Hit_Regular, Hit_Weakpoint,
// Hit_StrongPoint, Hit_NoDamage, Hit_Kill); detour_c168_play_animation logs it on both sides.

#include <stdio.h>
#include <string.h>

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

uint64_t c41_guid(const void* actor);            // enemies.cpp
void* c41_resolve_guid(void* pc, uint64_t guid); // enemies.cpp

C157DamageCausedFn g_orig_c166_death_caused = nullptr;
C168PlayAnimationFn g_orig_c168_play_animation = nullptr;

namespace {

constexpr size_t kEvInstigator = 0x00;
constexpr size_t kEvCauser = 0x08;
constexpr size_t kEvReceiver = 0x10;
constexpr size_t kEvReceiverHealth = 0x18;
constexpr size_t kEvReceiverTags = 0x40;
constexpr size_t kEvDamageTypes = 0x160;
constexpr size_t kEvOriginal = 0x180;
constexpr size_t kEvMap = 0x190;
constexpr size_t kEventSize = 0x400; // past the event's 0x1E0
constexpr size_t kMapNum = 0x08;
constexpr size_t kMapMax = 0x0C;
constexpr size_t kMapBitsInline = 0x10;
constexpr size_t kMapBitsHeap = 0x20;
constexpr size_t kMapNumBits = 0x28;
constexpr size_t kMapMaxBits = 0x2C;
constexpr size_t kMapFirstFree = 0x30;
constexpr size_t kMapNumFree = 0x34;
constexpr size_t kMapHashInline = 0x38;
constexpr size_t kMapHashSize = 0x48;
constexpr size_t kElemStride = 20;
constexpr size_t kElemValue = 0x8;
constexpr size_t kElemHashNext = 0xC;
constexpr size_t kElemHashIndex = 0x10;
constexpr int32_t kMapScan = 64;   // value map elements read on the host
constexpr int32_t kMaxValues = 8;  // value map entries sent
constexpr int32_t kMaxTags = 8;    // tags sent per container
constexpr int32_t kTagChars = 96;  // one tag name
constexpr uint32_t kLines = 40;    // per side per map

struct MarkerRow {
    char kind; // 'D' damage caused, 'K' death caused
    uint64_t guid;
    float dmg[4]; // original, final, health, armour
    int32_t values, types, rtags;
    char value_tag[kMaxValues][kTagChars];
    float value[kMaxValues];
    char type[kMaxTags][kTagChars];
    char rtag[kMaxTags][kTagChars];
};

struct MarkerState {
    uint32_t sent, not_sent, no_pc, no_guid;                        // host
    uint32_t received, applied, no_pawn, no_npc, bad, kills, damage; // client
    uint32_t lines;
    uint32_t anim_lines, anims; // card 168: WB_Hit_Marker PlayAnimation calls, both sides
};
MarkerState g_mk{}; // game thread

// An FName's text as one wire token: printable, no spaces or ';', "None" otherwise.
void name_token(const void* fname, char* out, size_t cap) {
    const ObjectName n = fname_text(fname);
    size_t len = strlen(n.text);
    bool ok = len > 0 && len < cap && strcmp(n.text, "?") != 0; // card 238: "?" = FName refused by the pool guard
    for (size_t i = 0; ok && i < len; ++i) {
        ok = n.text[i] > ' ' && n.text[i] <= '~' && n.text[i] != ';';
    }
    snprintf(out, cap, "%s", ok ? n.text : "None");
}

// Host: up to kMaxTags names of one FGameplayTagContainer's GameplayTags.
int32_t read_tags(const uint8_t* container, char (*out)[kTagChars]) {
    const auto* const data = static_cast<const uint8_t*>(read_ptr(container, 0));
    const int32_t num = read_i32(container, 8);
    int32_t n = 0;
    for (int32_t i = 0; data != nullptr && i < num && n < kMaxTags; ++i) {
        name_token(data + static_cast<size_t>(i) * 8, out[n], kTagChars);
        n += strcmp(out[n], "None") != 0 ? 1 : 0;
    }
    return n;
}

// Host: up to kMaxValues allocated entries of the value map.
int32_t read_values(const uint8_t* event, MarkerRow& r) {
    const uint8_t* const map = event + kEvMap;
    const auto* const data = static_cast<const uint8_t*>(read_ptr(map, 0));
    const int32_t max = read_i32(map, kMapNum);
    const void* const heap = read_ptr(map, kMapBitsHeap);
    const auto* const bits = static_cast<const uint32_t*>(heap != nullptr ? heap : map + kMapBitsInline);
    int32_t n = 0;
    for (int32_t i = 0; data != nullptr && i < max && i < kMapScan && n < kMaxValues; ++i) {
        const uint8_t* const elem = data + static_cast<size_t>(i) * kElemStride;
        if ((bits[i / 32] >> (i % 32) & 1u) == 0) {
            continue;
        }
        name_token(elem, r.value_tag[n], kTagChars);
        memcpy(&r.value[n], elem + kElemValue, sizeof(float));
        n += strcmp(r.value_tag[n], "None") != 0 ? 1 : 0;
    }
    return n;
}

int32_t row_text(const MarkerRow& r, char* out, size_t cap) {
    int32_t used = snprintf(out, cap, "%c %llx %.3f %.3f %.3f %.3f %d %d %d", r.kind,
                            static_cast<unsigned long long>(r.guid), r.dmg[0], r.dmg[1], r.dmg[2], r.dmg[3], r.values,
                            r.types, r.rtags);
    for (int32_t i = 0; i < r.values && used > 0 && static_cast<size_t>(used) < cap; ++i) {
        used += snprintf(out + used, cap - static_cast<size_t>(used), " %s %.4f", r.value_tag[i], r.value[i]);
    }
    for (int32_t i = 0; i < r.types && used > 0 && static_cast<size_t>(used) < cap; ++i) {
        used += snprintf(out + used, cap - static_cast<size_t>(used), " %s", r.type[i]);
    }
    for (int32_t i = 0; i < r.rtags && used > 0 && static_cast<size_t>(used) < cap; ++i) {
        used += snprintf(out + used, cap - static_cast<size_t>(used), " %s", r.rtag[i]);
    }
    return used;
}

// Host: a server copy's OnDamageCaused ('D') or OnDeathCaused ('K'), sent to that copy's PC.
void host_relay(void* iface, const uint8_t* event, char kind) {
    void* const pawn = static_cast<uint8_t*>(iface) - kCauserIface;
    if (g_client_session || sprint_side(pawn) != kSideServerCopy) {
        return;
    }
    void* const pc = read_ptr(pawn, kPawnController);
    const void* const npc = read_ptr(event, kEvReceiver);
    static MarkerRow r;
    r = MarkerRow{};
    r.kind = kind;
    r.guid = npc != nullptr ? c41_guid(npc) : 0;
    g_mk.no_pc += pc == nullptr ? 1 : 0;
    g_mk.no_guid += r.guid == 0 ? 1 : 0;
    if (pc == nullptr || r.guid == 0) {
        return;
    }
    memcpy(r.dmg, event + kEvOriginal, sizeof(r.dmg));
    r.values = read_values(event, r);
    r.types = read_tags(event + kEvDamageTypes, r.type);
    r.rtags = read_tags(event + kEvReceiverTags, r.rtag);
    static char text[kChanTextMax + 1];
    const int32_t len = row_text(r, text, sizeof(text));
    static wchar_t wire[kChanTextFrame];
    const WireString w = len > 0 && static_cast<size_t>(len) < sizeof(text) ? chan_text_frame(ChanOp::Marker, text, wire)
                                                                             : WireString{};
    const bool sent = w.num > 0 && chan_send_client(pc, w);
    g_mk.sent += sent ? 1 : 0;
    g_mk.not_sent += sent ? 0 : 1;
    if (!sent || note_event_cap(g_mk.lines, kLines, "card166 marker")) {
        log::line("%scard166: host t=%llu %s on server copy %s by %s to %s (%s): %s",
                  sent ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()),
                  kind == 'K' ? "OnDeathCaused" : "OnDamageCaused", object_name(pawn).text,
                  object_name(read_ptr(event, kEvCauser)).text, object_name(pc).text, sent ? "sent" : "not sent", text);
    }
}

// Client: an FName from a wire token, through the game's FName constructor.
uint64_t make_name(const char* text) {
    wchar_t w[kTagChars];
    size_t i = 0;
    for (; text[i] != '\0' && i + 1 < kTagChars; ++i) {
        w[i] = static_cast<wchar_t>(text[i]);
    }
    w[i] = L'\0';
    uint64_t name = 0;
    g_fname_ctor(&name, w, kFNameAdd);
    return name;
}

// Client: a tag container over `names` (stack storage; the base OnDamageCaused only reads the event).
void build_tags(uint8_t* container, uint64_t* names, const char (*text)[kTagChars], int32_t n) {
    for (int32_t i = 0; i < n; ++i) {
        names[i] = make_name(text[i]);
    }
    void* const data = n > 0 ? names : nullptr;
    memcpy(container, &data, sizeof(data));
    memcpy(container + 8, &n, sizeof(n));
    memcpy(container + 12, &n, sizeof(n));
}

// Client: the value map, all elements chained in hash bucket 0 (HashSize 1).
void build_map(uint8_t* event, uint8_t* elems, const MarkerRow& r) {
    const int32_t none = -1, zero = 0, one = 1, bits_max = 128;
    for (int32_t i = 0; i < r.values; ++i) {
        uint8_t* const e = elems + static_cast<size_t>(i) * kElemStride;
        const uint64_t tag = make_name(r.value_tag[i]);
        const int32_t next = i + 1 < r.values ? i + 1 : none;
        memcpy(e, &tag, sizeof(tag));
        memcpy(e + kElemValue, &r.value[i], sizeof(float));
        memcpy(e + kElemHashNext, &next, sizeof(next));
        memcpy(e + kElemHashIndex, &zero, sizeof(zero));
    }
    if (r.values == 0) {
        return; // an empty TMap is all zero
    }
    uint8_t* const map = event + kEvMap;
    void* const data = elems;
    const uint32_t bits = (1u << r.values) - 1u; // kMaxValues <= 31
    memcpy(map, &data, sizeof(data));
    memcpy(map + kMapNum, &r.values, sizeof(int32_t));
    memcpy(map + kMapMax, &r.values, sizeof(int32_t));
    memcpy(map + kMapBitsInline, &bits, sizeof(bits));
    memcpy(map + kMapNumBits, &r.values, sizeof(int32_t));
    memcpy(map + kMapMaxBits, &bits_max, sizeof(bits_max));
    memcpy(map + kMapFirstFree, &none, sizeof(none));
    memcpy(map + kMapNumFree, &zero, sizeof(zero));
    memcpy(map + kMapHashInline, &zero, sizeof(zero));
    memcpy(map + kMapHashSize, &one, sizeof(one));
}

// Client: one token of `text` at `*at` into out; false at the end.
bool next_token(const char*& at, char* out, size_t cap) {
    while (*at == ' ') {
        ++at;
    }
    size_t n = 0;
    while (at[n] != '\0' && at[n] != ' ') {
        ++n;
    }
    if (n == 0 || n >= cap) {
        return false;
    }
    memcpy(out, at, n);
    out[n] = '\0';
    at += n;
    return true;
}

bool parse_row(const char* text, MarkerRow& r) {
    unsigned long long guid = 0;
    int consumed = 0;
    const int got = sscanf_s(text, "%c %llx %f %f %f %f %d %d %d%n", &r.kind, 1, &guid, &r.dmg[0], &r.dmg[1],
                             &r.dmg[2], &r.dmg[3], &r.values, &r.types, &r.rtags, &consumed);
    r.guid = guid;
    if (got != 9 || (r.kind != 'D' && r.kind != 'K') || guid == 0 || r.values < 0 || r.values > kMaxValues ||
        r.types < 0 || r.types > kMaxTags || r.rtags < 0 || r.rtags > kMaxTags) {
        return false;
    }
    const char* at = text + consumed;
    char value[32];
    bool ok = true;
    for (int32_t i = 0; ok && i < r.values; ++i) {
        ok = next_token(at, r.value_tag[i], kTagChars) && next_token(at, value, sizeof(value)) &&
             sscanf_s(value, "%f", &r.value[i]) == 1;
    }
    for (int32_t i = 0; ok && i < r.types; ++i) {
        ok = next_token(at, r.type[i], kTagChars);
    }
    for (int32_t i = 0; ok && i < r.rtags; ++i) {
        ok = next_token(at, r.rtag[i], kTagChars);
    }
    return ok;
}

// Client diag (c166b: log lines present, no marker drawn in the shot): the own pawn's two marker delegates'
// invocation lists (TMulticastScriptDelegate at +0x670 OnCharacterCausedDamage and +0x660 OnCharacterKilledNPC:
// TArray of FScriptDelegate {FWeakObjectPtr object, FName function}), so the run shows whether WB_Hit_Marker is
// bound to this pawn.
void log_marker_bindings(const uint8_t* pawn) {
    constexpr size_t kDelegates[2] = {0x670, 0x660};
    constexpr int32_t kMaxShown = 8;
    for (size_t d : kDelegates) {
        const auto* const list = static_cast<const uint8_t*>(read_ptr(pawn, d));
        int32_t num = 0;
        memcpy(&num, pawn + d + 8, sizeof(num));
        char out[512];
        int used = snprintf(out, sizeof(out), "%d bound:", num);
        for (int32_t i = 0; list != nullptr && i < num && i < kMaxShown && used > 0 &&
                            used < static_cast<int>(sizeof(out)); ++i) {
            const uint8_t* const entry = list + static_cast<size_t>(i) * 16;
            void* const obj = g_weak_get != nullptr ? g_weak_get(entry) : nullptr;
            used += snprintf(out + used, sizeof(out) - static_cast<size_t>(used), " %s.%s",
                             obj != nullptr ? object_name(obj).text : "(gone)", fname_text(entry + 8).text);
        }
        log::line("card166: client t=%llu own pawn %s delegate +0x%zx %s", static_cast<unsigned long long>(GetTickCount64()),
                  object_name(pawn).text, d, out);
    }
}

} // namespace

void detour_c166_death_caused(void* self, const void* event) {
    g_orig_c166_death_caused(self, event);
    if (self != nullptr && event != nullptr) {
        host_relay(self, static_cast<const uint8_t*>(event), 'K');
    }
}

// Host, from detour_c157_damage_caused after the original.
void c166_host_damage_caused(void* self, const void* event) {
    if (self != nullptr && event != nullptr) {
        host_relay(self, static_cast<const uint8_t*>(event), 'D');
    }
}

// Client: one Marker frame; the own pawn's base ASTCharacter call broadcasts the marker delegate.
void c166_receive(void* pc, const WireString* s) {
    static char text[kChanTextMax + 1];
    static MarkerRow r;
    r = MarkerRow{};
    ++g_mk.received;
    if (chan_text(s, text, sizeof(text)) <= 0 || !parse_row(text, r)) {
        ++g_mk.bad;
        log::line("WARN card166: client t=%llu Marker frame not parsed (%u bad): %s",
                  static_cast<unsigned long long>(GetTickCount64()), g_mk.bad, text);
        return;
    }
    auto* const pawn = static_cast<uint8_t*>(pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr);
    void* const npc = c41_resolve_guid(pc, r.guid);
    g_mk.no_pawn += pawn == nullptr ? 1 : 0;
    g_mk.no_npc += npc == nullptr ? 1 : 0;
    using CausedFn = void (*)(void* self, const void* event);
    const auto caused = reinterpret_cast<CausedFn>(const_cast<uint8_t*>(
        g_exe_base + (r.kind == 'K' ? offsets::ASTCharacter_OnDeathCaused.rva : offsets::ASTCharacter_OnDamageCaused.rva)));
    if (pawn == nullptr || g_fname_ctor == nullptr || g_exe_base == nullptr) {
        log::line("WARN card166: client t=%llu Marker %c on guid 0x%llx not applied: %s",
                  static_cast<unsigned long long>(GetTickCount64()), r.kind, static_cast<unsigned long long>(r.guid),
                  pawn == nullptr ? "no own pawn" : "FName constructor not resolved");
        return;
    }
    void* health = nullptr;
    if (npc != nullptr) {
        c21_health(static_cast<const uint8_t*>(npc), &health);
    }
    alignas(16) static uint8_t event[kEventSize];
    alignas(8) static uint8_t elems[kMaxValues * kElemStride + 4];
    static uint64_t types[kMaxTags], rtags[kMaxTags];
    memset(event, 0, sizeof(event));
    void* const self_pawn = pawn;
    const void* const inventory = c24_inventory(pawn);
    void* const weapon = inventory != nullptr ? g_get_equipped_weapon(inventory) : nullptr;
    void* const causer = weapon != nullptr ? weapon : pc; // card 168: the widget drops Causer == Instigator
    memcpy(event + kEvInstigator, &self_pawn, sizeof(self_pawn));
    memcpy(event + kEvCauser, &causer, sizeof(causer));
    memcpy(event + kEvReceiver, &npc, sizeof(npc));
    memcpy(event + kEvReceiverHealth, &health, sizeof(health));
    build_tags(event + kEvReceiverTags, rtags, r.rtag, r.rtags);
    build_tags(event + kEvDamageTypes, types, r.type, r.types);
    memcpy(event + kEvOriginal, r.dmg, sizeof(r.dmg));
    build_map(event, elems, r);
    if (g_mk.applied < 2) {
        log_marker_bindings(pawn);
    }
    caused(pawn + kCauserIface, event);
    ++g_mk.applied;
    g_mk.kills += r.kind == 'K' ? 1 : 0;
    g_mk.damage += r.kind == 'D' ? 1 : 0;
    if (note_event_cap(g_mk.lines, kLines, "card166 marker")) {
        log::line("card166: client t=%llu %s on own pawn %s causer %s for %s guid 0x%llx damage %.2f (final), %d "
                  "values, %d damage types, %d receiver tags: %s", static_cast<unsigned long long>(GetTickCount64()),
                  r.kind == 'K' ? "OnDeathCaused" : "OnDamageCaused", object_name(pawn).text, object_name(causer).text,
                  npc != nullptr ? object_name(npc).text : "(NPC not mapped)", static_cast<unsigned long long>(r.guid),
                  static_cast<double>(r.dmg[1]), r.values, r.types, r.rtags, text);
    }
}

// Both sides: WB_Hit_Marker's Play Hit, the marker's visible change (card 168 proof).
void* detour_c168_play_animation(void* self, void* ret, void* animation, float start, int32_t loops, uint8_t mode,
                                 float speed, bool restore) {
    void* const handle = g_orig_c168_play_animation(self, ret, animation, start, loops, mode, speed, restore);
    if (self != nullptr && strcmp(class_name(self).text, "WB_Hit_Marker_C") == 0) {
        ++g_mk.anims;
        if (note_event_cap(g_mk.anim_lines, kLines, "card168 marker animation")) {
            log::line("card168: %s t=%llu WB_Hit_Marker PlayAnimation %s (%u this map)", side(),
                      static_cast<unsigned long long>(GetTickCount64()),
                      animation != nullptr ? object_name(animation).text : "(null)", g_mk.anims);
        }
    }
    return handle;
}

void c166_arm_map() {
    if (g_mk.sent != 0 || g_mk.not_sent != 0 || g_mk.received != 0 || g_mk.anims != 0) {
        // Labelled by the counters, not side(): the client arms its map before g_client_session is set (card 168).
        const bool client = g_mk.received != 0 || g_mk.applied != 0;
        log::line("card166: %s marker relay map totals: %u sent, %u not sent, %u without PC, %u without guid; %u "
                  "received, %u applied (%u damage, %u kills), %u without own pawn, %u NPC not mapped, %u bad; "
                  "card168: %u WB_Hit_Marker animations",
                  client ? "client" : "host", g_mk.sent, g_mk.not_sent, g_mk.no_pc, g_mk.no_guid, g_mk.received, g_mk.applied,
                  g_mk.damage, g_mk.kills, g_mk.no_pawn, g_mk.no_npc, g_mk.bad, g_mk.anims);
    }
    g_mk = MarkerState{};
}

} // namespace ubw::hooks
