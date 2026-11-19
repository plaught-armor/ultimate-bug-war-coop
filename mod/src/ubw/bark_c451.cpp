// Card 451 part 2 (always on, both sides): trooper bark subtitles on the client.
//
// WB_Secondary_Subtitles (Gameplay_HUD) binds the bark subsystem's OnBarkStarted / OnBarkFinished (FBarkDelegate,
// one FADActiveBark parameter) and draws a line per started bark until the same struct comes back finished. NPC logic
// runs on the host only, so UADBarkSubsystem::PlaySound, the one Broadcast of OnBarkStarted, never runs on the client;
// card 136 relays the sound alone.
//
// FADActiveBark (0x28 bytes, StructParams SizeOf) has three reflected members, read from the exe's PropPointers:
//   Speaker  (TWeakObjectPtr<AActor>, gen flag 0x0E, +0x00), BarkType (UADBarkEventData*, 0x52, +0x08),
//   LineChoice (int32, +0x10)
// b230 run c448b230a: Speaker read as a raw pointer crashed the host (0x67AE0001E107 = serial 0x67AE, index 0x1E107).
// The rest (+0x14 to +0x28, copied by FBarkDelegate::Broadcast) is not reflected, so no Blueprint reads it.
//
// Host: the detour of FBarkDelegate::Broadcast(delegate, bark) tells OnBarkStarted from OnBarkFinished by the
// delegate's address in the speaker's bark subsystem (property offsets by name) and sends ChanOp::Bark
//   "<s|f> <speaker NetGUID hex> <line> <class path of BarkType>"
// to every ponged client. A player pawn speaker or one with no NetGUID is not sent.
// Client: the speaker by NetGUID, BarkType as the default object of the class, LineChoice; the original Broadcast on
// its own subsystem's delegate of the same name. The client plays no sound (card 136 plays it). Start and finish are
// built from the same fields, so the widget's finish match holds.
//
// Rows (kC451RelayRows per side per process, then counted only):
//   card451: host t=<ms> bark <started|finished> <class> line <n> speaker <name> guid 0x<g> sent to <k>
//   card451: client t=<ms> bark <started|finished> <class> line <n> speaker <name> (relay)

#include "ubw/hooks_internal.h"

#include <stdio.h>
#include <string.h>

namespace ubw::hooks {

C451BroadcastFn g_orig_c451_broadcast = nullptr;

namespace {

constexpr size_t kC451BarkBytes = 0x28;     // FADActiveBark SizeOf (StructParams)
constexpr size_t kC451Speaker = 0x00;       // TWeakObjectPtr<AActor> Speaker
constexpr size_t kC451BarkType = 0x08;      // UADBarkEventData* BarkType
constexpr size_t kC451LineChoice = 0x10;    // int32 LineChoice
constexpr int32_t kC451MaxLine = 1 << 12;   // a line index past this is not a line
constexpr uint32_t kC451RelayRows = 60;     // rows per side per process
constexpr const char* kC451Delegates[2] = {"OnBarkStarted", "OnBarkFinished"};
constexpr char kC451Kinds[2] = {'s', 'f'};
constexpr const char* kC451KindNames[2] = {"started", "finished"};
constexpr wchar_t kC451SubsystemClass[] = L"/Script/ADBarkSystem.ADBarkSubsystem";

struct C451Relay {
    uint32_t rows;        // rows written
    uint32_t sent[2];     // host: rows sent per kind
    uint32_t skipped;     // host: player or unaddressable speakers
    uint32_t relayed[2];  // client: broadcasts per kind
    uint32_t dropped;     // client: rows not applied
    uint32_t own;         // client: its own broadcasts (expected 0)
};
C451Relay g_c451r{};

void* c451_speaker(const uint8_t* bark) {
    C31Weak w{};
    memcpy(&w, bark + kC451Speaker, sizeof(w));
    return c31_weak_get(w);
}

bool c451_row_ok() {
    if (g_c451r.rows >= kC451RelayRows) {
        return false;
    }
    ++g_c451r.rows;
    return true;
}

// The bark subsystem of `ctx`'s world, or null.
void* c451_subsystem(void* ctx) {
    void* const cls = find_class(kC451SubsystemClass);
    return cls != nullptr ? c322_world_subsystem(ctx, cls) : nullptr;
}

// 0 for OnBarkStarted, 1 for OnBarkFinished, -1 when `delegate` is neither of `sub`.
int c451_kind_of(const void* sub, const void* delegate) {
    const void* const cls = read_ptr(sub, kObjectClass);
    for (int k = 0; cls != nullptr && k < 2; ++k) {
        const uint8_t* const f = prop_find_prop(cls, kC451Delegates[k]);
        if (f != nullptr && static_cast<const uint8_t*>(sub) + read_i32(f, kPropertyOffset) == delegate) {
            return k;
        }
    }
    return -1;
}

bool c451_is_player_pawn(const void* world, const void* actor) {
    for (int32_t s = 0; world != nullptr && s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(const_cast<void*>(world), s);
        if (pc != nullptr && read_ptr(pc, kControllerPawn) == actor) {
            return true;
        }
    }
    return false;
}

// Host: one ChanOp::Bark row for a broadcast of `kind`.
void c451_host_send(int kind, const uint8_t* bark) {
    void* const speaker = c451_speaker(bark);
    const void* const type = read_ptr(bark, kC451BarkType);
    const void* const world = speaker != nullptr ? g_actor_get_world(speaker) : nullptr;
    const uint64_t guid = speaker != nullptr ? c41_guid(speaker) : 0;
    char path[kC50PathMax + 1];
    const bool addressable = world != nullptr && guid != 0 && type != nullptr &&
                             c50_class_path(read_ptr(type, kObjectClass), path, sizeof(path)) > 0;
    if (!addressable || c451_is_player_pawn(world, speaker)) {
        ++g_c451r.skipped;
        return;
    }
    const int32_t line = read_i32(bark, kC451LineChoice);
    char text[kC50PathMax + 48];
    snprintf(text, sizeof(text), "%c %llx %d %s", kC451Kinds[kind], static_cast<unsigned long long>(guid), line, path);
    const C95Fanout f = c95_send_all(world, ChanOp::Bark, text);
    g_c451r.sent[kind] += f.sent > 0 ? 1u : 0u;
    if (c451_row_ok()) {
        log::line("%scard451: host t=%llu bark %s %s line %d speaker %s guid 0x%llX sent to %d of %d",
                  f.sent == f.to ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()),
                  kC451KindNames[kind], object_name(type).text, line, object_name(speaker).text,
                  static_cast<unsigned long long>(guid), f.sent, f.to);
    }
}

} // namespace

void detour_c451_broadcast(void* delegate, const void* bark) {
    const auto* const b = static_cast<const uint8_t*>(bark);
    void* const speaker = b != nullptr ? c451_speaker(b) : nullptr;
    if (speaker != nullptr && g_client_session) {
        ++g_c451r.own; // a client-side bark of its own; counted at map end by c451_relay_summary
    }
    if (speaker != nullptr && !g_client_session) {
        void* const sub = c451_subsystem(speaker);
        const int kind = sub != nullptr ? c451_kind_of(sub, delegate) : -1;
        if (kind >= 0) {
            c451_host_send(kind, b);
        }
    }
    g_orig_c451_broadcast(delegate, bark);
}

// Client: ChanOp::Bark "<s|f> <guid hex> <line> <class path>".
void c451_receive(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    chan_text(s, text, sizeof(text));
    char kind_c = '\0';
    unsigned long long guid = 0;
    int32_t line = -1;
    char path[kC50PathMax + 1] = "";
    const bool parsed = sscanf_s(text, "%c %llx %d %160s", &kind_c, 1, &guid, &line, path,
                                 static_cast<unsigned>(sizeof(path))) == 4 &&
                        (kind_c == 's' || kind_c == 'f') && line >= 0 && line < kC451MaxLine;
    const int kind = kind_c == 'f' ? 1 : 0;
    wchar_t w[kC50PathMax + 1];
    size_t n = 0;
    for (; parsed && path[n] != '\0' && n < static_cast<size_t>(kC50PathMax); ++n) {
        w[n] = static_cast<wchar_t>(path[n]);
    }
    w[n] = L'\0';
    void* const cls = parsed ? find_class(w) : nullptr; // find only: a bark class the client never loaded is dropped
    void* const type = cls != nullptr ? read_ptr(cls, kClassDefaultObject) : nullptr;
    void* const speaker = type != nullptr ? c41_object_from_guid(pc, guid) : nullptr;
    void* const sub = speaker != nullptr ? c451_subsystem(pc) : nullptr;
    const uint8_t* const f = sub != nullptr ? prop_find_prop(read_ptr(sub, kObjectClass), kC451Delegates[kind]) : nullptr;
    const ULONGLONG now = GetTickCount64();
    C31Weak weak{};
    c31_weak_set(weak, speaker);
    if (f == nullptr || weak.serial == 0) {
        ++g_c451r.dropped;
        log::line("WARN card451: client t=%llu bark row dropped (parsed %d, class %d, speaker %d, subsystem %d): %s",
                  static_cast<unsigned long long>(now), parsed ? 1 : 0, cls != nullptr ? 1 : 0,
                  speaker != nullptr ? 1 : 0, sub != nullptr ? 1 : 0, text);
        return;
    }
    alignas(16) uint8_t bark[kC451BarkBytes] = {};
    memcpy(bark + kC451Speaker, &weak, sizeof(weak));
    memcpy(bark + kC451BarkType, &type, sizeof(type));
    memcpy(bark + kC451LineChoice, &line, sizeof(line));
    g_orig_c451_broadcast(static_cast<uint8_t*>(sub) + read_i32(f, kPropertyOffset), bark);
    ++g_c451r.relayed[kind];
    if (c451_row_ok()) {
        log::line("card451: client t=%llu bark %s %s line %d speaker %s (relay)", static_cast<unsigned long long>(now),
                  kC451KindNames[kind], object_name(type).text, line, object_name(speaker).text);
    }
}

// Both sides, at LoadMap enter: the process tally. One row with every count: g_client_session is not yet set at the
// first loads of a client (c448b230a: its pre-join tally read "host").
void c451_relay_summary() {
    const C451Relay& r = g_c451r;
    log::line("card451: %s relay tally: sent started %u finished %u, not sent %u; relayed started %u finished %u, "
              "dropped %u, own broadcasts %u",
              g_client_session ? "client" : "host or pre-join", r.sent[0], r.sent[1], r.skipped, r.relayed[0],
              r.relayed[1], r.dropped, r.own);
}

} // namespace ubw::hooks
