// Card 604: placed audio components and level-script music that host-only level script chains start (encounter
// delegates, the custom events they call, pickups the host runs) reach the client.
//
// Host capture (one class, two parts):
//   - a UAudioComponent play (PlayInternal, from detour_pl_play_internal) or Stop whose owner is a placed actor that
//     does not replicate, outside every card 136 capture point, when the component itself was loaded with the level or
//     is a default subobject (object flags RF_WasLoaded or RF_DefaultSubObject). A component made at run time
//     (SpawnSoundAttached, a Blueprint's own AddComponent) has a generated name the client cannot find, and its spawn
//     is card 136 / 455's row: it is not relayed, only counted by sound asset (one summary per 60 s);
//   - TriggerSpecificMusic, EndMusic, ClearMusicTriggers, PlaySound2DFName and PlaySound2D called with a level script
//     actor (*_Scripting_C) as the world context.
// Each capture is one ChanOp::LevelAudio text row to every ready client:
//   P <owner> <component> <volume %> <start s>   component play
//   S <owner> <component>                        component stop (only when the component plays)
//   T <0|1> <music name>                         TriggerSpecificMusic
//   E <0|1> <fade s>                             EndMusic
//   C <0|1>                                      ClearMusicTriggers
//   F <volume %> <pitch %> <start s> <name>      PlaySound2DFName
//   2 <volume %> <pitch %> <asset path>          PlaySound2D
//
// Client pairing: most of these calls also run on the client (BeginPlay, objective events the client replays, presences,
// switch presses), so a host row must not play a second time. The client keeps a small ring of recent calls, its own
// and the host's, keyed by kind and object (component, music name, asset). A call whose twin from the other side is in
// the ring and younger than kC604PairMs consumes it and does not run; any other call runs and is remembered. Stops
// are idempotent and never paired.
//
// No row goes while the host runs an in-place load with pawns (g_c121_in_place_load): the card 290 travel row went
// first, so the client reloads the map and its own BeginPlay plays run there (c604b459a: 1073 destructible LoopingSFX
// plays inside one death-screen load). Those calls are counted, not sent.
//
// Rows are rare (63 music sites over the eight FED maps, most once per map), so text keeps them readable at about 20 to
// 60 B a row; a per-second cap bounds a looping retrigger. The per-map tally (rows, bytes, pairs, misses) prints at
// map end with the card 099 totals.

#include "ubw/hooks_internal.h"

#include <stdio.h>
#include <string.h>

namespace ubw::hooks {

namespace {
// UActorComponent::bIsActive, byte +0x97 bit 6 (read by UActorComponent::execIsActive at exe+0x3A60C85).
// UAudioComponent::IsPlaying returns IsActive(). The pdb address for IsPlaying (exe+0x3A35C20) is a folded,
// this-adjusted body that reads [rcx-0x1AA]; calling it with a component pointer crashed c604b457a.
constexpr size_t kComponentActiveByte = 0x97;
constexpr uint8_t kComponentActiveBit = 0x40;
bool c604_active(const void* comp) {
    return (static_cast<const uint8_t*>(comp)[kComponentActiveByte] & kComponentActiveBit) != 0;
}
} // namespace

C604StopFn g_orig_c604_stop = nullptr;
C604PlayFn g_c604_play = nullptr;
C604TriggerFn g_orig_c604_trigger = nullptr;
C604EndFn g_orig_c604_end = nullptr;
C604ClearFn g_orig_c604_clear = nullptr;
C604Sound2DNameFn g_orig_c604_sound_name = nullptr;
C604SubmixSetFn g_orig_c604_submix_set = nullptr;
C604SubmixClearFn g_orig_c604_submix_clear = nullptr;

namespace {

constexpr ULONGLONG kC604PairMs = 5000; // a both-machines call lands on the client within this of the host's row
constexpr uint32_t kC604Ring = 32;      // pairing ring entries (oldest replaced)
constexpr uint32_t kC604RowsPerSec = 16; // host: rows per second over all kinds; more are dropped and counted
constexpr uint32_t kC604Lines = 80;      // log rows per map per side (the tally covers the rest)
constexpr float kC604MaxStart = 600.0f;  // a start time or fade past this is a bad read: sent as 0
constexpr int32_t kC604MaxPercent = 400; // volume or pitch scale ceiling, percent
constexpr ULONGLONG kC604SkipMs = 60000;  // host: runtime-component skip summary period
constexpr uint32_t kC604SkipAssets = 32;  // host: skip rows by sound asset (more are counted in one overflow)
constexpr uint32_t kRfDefaultSubObject = 0x40000; // EObjectFlags: RF_DefaultSubObject (RF_WasLoaded is kC50WasLoaded)

enum C604Kind : uint8_t { kPlay, kStop, kTrigger, kEnd, kClear, kName2D, kAsset2D, kKinds };
constexpr char kC604Op[kKinds] = {'P', 'S', 'T', 'E', 'C', 'F', '2'};
constexpr const char* kC604KindText[kKinds] = {"component play", "component stop", "TriggerSpecificMusic", "EndMusic",
                                              "ClearMusicTriggers", "PlaySound2DFName", "PlaySound2D"};

struct C604Pair {
    uint64_t key; // 0: empty
    ULONGLONG t;
    uint8_t kind;
    bool host; // true: a host row played; false: the client's own call ran
};

struct C604Tally {
    uint32_t rows[kKinds];  // host: rows sent (any client); client: host rows received
    uint32_t bytes[kKinds]; // text chars, host side
    uint32_t paired[kKinds]; // client: calls skipped as the twin of the other side's
    uint32_t missed;         // client: host rows whose component, name or asset is not here
    uint32_t dropped[kKinds]; // host: rows over the per-second cap
    uint32_t submix_set;     // both: SetSubmixEffectChainOverride calls (card 604 check)
    uint32_t submix_clear;   // both: ClearSubmixEffectChainOverride calls
    uint32_t load_skipped;   // host: play and stop calls inside an in-place load (the client reloads)
};

// Runtime audio components not relayed, by sound asset, since the last summary (host: skipped; client: its own).
struct C604Skip {
    const void* asset; // nullptr: empty; a compare key, never read through (card 608)
    ObjectName name;   // copied at insert: the asset can be freed before the summary. Accepted: an asset loaded at a
                       // freed one's address in the same period is counted in that one's row, under that one's name.
    uint32_t plays;
    uint32_t stops;
    char owner[kChanNameMax + 1]; // first owner seen, with its number (matches card 136 / 455 rows)
};
struct C604Skips {
    C604Skip row[kC604SkipAssets];
    uint32_t overflow; // calls whose asset found no free row
    ULONGLONG ms0;     // start of this summary period (0: none yet)
};
C604Skips g_c604_skips{};

struct C604State {
    C604Pair ring[kC604Ring];
    uint32_t next;
    C604Tally tally;
    uint32_t lines;
    ULONGLONG sec_ms;
    uint32_t sec_rows;
    int32_t vol_at;   // UAudioComponent::VolumeMultiplier offset, from detour_pl_play_internal (-1 until read)
    int32_t sound_at; // UAudioComponent::Sound offset, from the same (-1 until read)
};
C604State g_c604{{}, 0, {}, 0, 0, 0, -1, -1};
thread_local bool t_c604_row = false; // client: a host row runs (its own calls are neither paired nor remembered)

bool c604_line_ok() {
    return g_c604.lines < kC604Lines && ++g_c604.lines > 0;
}

float c604_clamp_time(float s) {
    return s >= 0.0f && s <= kC604MaxStart ? s : 0.0f;
}

int32_t c604_percent(float v) {
    const int32_t p = static_cast<int32_t>(v * 100.0f + 0.5f);
    return p >= 0 && p <= kC604MaxPercent ? p : 100;
}

// Client: true when this call is the twin of a remembered call from the other side (consumed, the call is skipped);
// otherwise it is remembered and runs.
bool c604_pair(C604Kind kind, uint64_t key, bool host) {
    const ULONGLONG now = GetTickCount64();
    for (C604Pair& p : g_c604.ring) {
        if (p.key == key && p.kind == kind && p.host != host && now - p.t <= kC604PairMs) {
            p.key = 0;
            ++g_c604.tally.paired[kind];
            return true;
        }
    }
    g_c604.ring[g_c604.next] = C604Pair{key, now, kind, host};
    g_c604.next = (g_c604.next + 1) % kC604Ring;
    return false;
}

// Host: one row to every ready client. The caller built `text`; `what` names it in the log.
void c604_send(void* world, C604Kind kind, const char* text, int len, const char* what) {
    if (g_c121_in_place_load) {
        ++g_c604.tally.load_skipped; // the client reloads the map and runs its own calls
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (now - g_c604.sec_ms >= 1000) {
        g_c604.sec_ms = now;
        g_c604.sec_rows = 0;
    }
    if (g_c604.sec_rows >= kC604RowsPerSec) {
        if (g_c604.sec_rows++ == kC604RowsPerSec && c604_line_ok()) { // the first drop of each second names its row
            log::line("WARN card604: host t=%llu %s %s over %u rows/s, dropped: \"%s\"",
                      static_cast<unsigned long long>(now), kC604KindText[kind], what, kC604RowsPerSec, text);
        }
        ++g_c604.tally.dropped[kind];
        return;
    }
    int32_t to = 0;
    int32_t sent = 0;
    for (int s = 0; world != nullptr && s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        const C50Copy* const c = pc != nullptr && sprint_side(pc) == kSideServerCopy ? c50_copy(object_index(pc), false)
                                                                                     : nullptr;
        if (c == nullptr || !c->ponged) {
            continue;
        }
        ++to;
        sent += c95_send_one(pc, ChanOp::LevelAudio, text) ? 1 : 0;
    }
    if (to == 0) {
        return; // no ready client: nothing counted
    }
    ++g_c604.sec_rows;
    ++g_c604.tally.rows[kind];
    g_c604.tally.bytes[kind] += static_cast<uint32_t>(len);
    if (sent != to || c604_line_ok()) {
        log::line("%scard604: host t=%llu %s %s sent to %d of %d clients: \"%s\"", sent == to ? "" : "WARN ",
                  static_cast<unsigned long long>(now), kC604KindText[kind], what, sent, to, text);
    }
}

// The component's placed, unreplicated owner, or null when the call is not one this card relays.
const void* c604_placed_owner(const void* comp) {
    const void* const owner = comp != nullptr ? read_ptr(comp, kComponentOwner) : nullptr;
    if (owner == nullptr || !c50_placed(owner) || c98_replicates(owner) || c136_depth() != 0) {
        return nullptr;
    }
    return owner;
}

// True when the component exists on both machines under the same name: loaded with the level (placed actor's
// serialized components) or a default subobject. A run-time AudioComponent_<n> has neither flag.
bool c604_level_component(const void* comp) {
    const uint32_t flags = static_cast<uint32_t>(read_i32(comp, kC50ObjectFlags));
    return (flags & (kC50WasLoaded | kRfDefaultSubObject)) != 0;
}

// Both sides: the skip summary, one row per asset seen since the last one, then a fresh period.
void c604_skip_summary(ULONGLONG now, const char* when) {
    uint32_t assets = 0;
    uint32_t calls = g_c604_skips.overflow;
    for (const C604Skip& r : g_c604_skips.row) {
        if (r.asset == nullptr) {
            continue;
        }
        ++assets;
        calls += r.plays + r.stops;
        log::line("card604: %s t=%llu runtime component not relayed (%s): %s on %s: %u plays, %u stops", side_text(),
                  static_cast<unsigned long long>(now), when, r.name.text, r.owner, r.plays, r.stops);
    }
    if (assets != 0 || calls != 0) {
        log::line("card604: %s t=%llu runtime component skip summary (%s): %u calls, %u assets, %u over the table of "
                  "%u",
                  side_text(), static_cast<unsigned long long>(now), when, calls, assets, g_c604_skips.overflow,
                  kC604SkipAssets);
    }
    g_c604_skips = C604Skips{};
    g_c604_skips.ms0 = now;
}

// Both sides (stops: host only): one runtime-component play or stop, counted by its sound asset.
void c604_skip(const void* owner, const void* comp, bool play) {
    const ULONGLONG now = GetTickCount64();
    if (g_c604_skips.ms0 == 0) {
        g_c604_skips.ms0 = now;
    } else if (now - g_c604_skips.ms0 >= kC604SkipMs) {
        c604_skip_summary(now, "60 s");
    }
    const void* const asset = g_c604.sound_at >= 0 ? read_ptr(comp, static_cast<size_t>(g_c604.sound_at)) : nullptr;
    for (C604Skip& r : g_c604_skips.row) {
        if (r.asset != nullptr && r.asset != asset) {
            continue;
        }
        if (r.asset == nullptr) {
            if (asset == nullptr) {
                break; // no sound: overflow row
            }
            r.asset = asset;
            r.name = object_name(asset);
            if (c50_name(owner, r.owner, sizeof(r.owner)) <= 0) {
                snprintf(r.owner, sizeof(r.owner), "%s", object_name(owner).text);
            }
        }
        ++(play ? r.plays : r.stops);
        return;
    }
    ++g_c604_skips.overflow;
}


// Host: the owner and component names as the client's finds match them (c50_find_placed_any, c95_component), with
// the FName number suffix. object_name drops the number: c604b458a sent "BP_KillableCrate_05_C" for every crate and
// the client found none of the 94 rows' owners.
struct C604Names {
    char owner[kChanNameMax + 1];
    char comp[kChanNameMax + 1];
};
bool c604_names(const void* owner, const void* comp, C604Names& out) {
    // Card 605: the owner goes as a level key. NiagaraActor_<n> repeats across sublevels; a plain name found the
    // first level's copy, which has no Audio component (C1M1 "P NiagaraActor_0 Audio": 6 misses).
    const bool ok =
        c50_level_key(owner, out.owner, sizeof(out.owner)) > 0 && c50_name(comp, out.comp, sizeof(out.comp)) > 0;
    if (!ok && c604_line_ok()) {
        log::line("WARN card604: host t=%llu %s/%s: a name does not fit %d chars, row not sent",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(owner).text, object_name(comp).text,
                  kChanNameMax);
    }
    return ok;
}

uint64_t c604_name_key(uint64_t fname) {
    return fname != 0 ? fname : 1; // NAME_None still pairs with itself
}

uint64_t c604_fname(const char* text) {
    wchar_t wide[kChanNameMax + 1] = {};
    for (int32_t i = 0; i < kChanNameMax && text[i] != 0; ++i) {
        wide[i] = static_cast<wchar_t>(static_cast<unsigned char>(text[i]));
    }
    uint64_t name = 0;
    if (g_fname_ctor != nullptr) {
        g_fname_ctor(&name, wide, 0); // FNAME_Find: a name this client never made is not played
    }
    return name;
}

// Client: true when the client's own call of this kind and key must not run (a host row played it).
bool c604_client_own(C604Kind kind, uint64_t key) {
    return g_client_session && !t_c604_row && c604_pair(kind, key, false);
}

void c604_receive_component(void* world, char op, const char* text) {
    char owner_name[kChanNameMax + 1] = {};
    char comp_name[kChanNameMax + 1] = {};
    int32_t volume = 100;
    float start = 0.0f;
    static_assert(kChanNameMax == 64, "the %64s widths below");
    const int fields = sscanf_s(text + 2, "%64s %64s %d %f", owner_name, static_cast<unsigned>(sizeof(owner_name)),
                                comp_name, static_cast<unsigned>(sizeof(comp_name)), &volume, &start);
    const bool play = op == 'P';
    void* const owner = fields >= 2 ? c50_find_placed_any(world, owner_name) : nullptr;
    void* const comp = owner != nullptr ? c95_component(owner, comp_name) : nullptr;
    if (comp == nullptr || (play && (fields != 4 || g_c604_play == nullptr)) || (!play && g_orig_c604_stop == nullptr)) {
        ++g_c604.tally.missed;
        char probe[kChanNameMax + 32] = "owner found";
        if (owner == nullptr && fields >= 2) {
            c50_name_probe(world, owner_name, probe, sizeof(probe)); // card 605: unplaced copy, or none at all
        }
        log::line("WARN card604: client t=%llu host row \"%s\": no placed %s with component %s here (fields %d; "
                  "name probe: %s)",
                  static_cast<unsigned long long>(GetTickCount64()), text, owner_name, comp_name, fields, probe);
        return;
    }
    if (!play) {
        g_orig_c604_stop(comp);
        return;
    }
    if (c604_pair(kPlay, reinterpret_cast<uintptr_t>(comp), true)) {
        return; // the client's own call played it already
    }
    if (g_c604.vol_at >= 0 && volume >= 0 && volume <= kC604MaxPercent) {
        const float v = static_cast<float>(volume) / 100.0f;
        memcpy(static_cast<uint8_t*>(comp) + g_c604.vol_at, &v, sizeof(v)); // Edit Volume: the host's level
    }
    t_c604_row = true;
    g_c604_play(comp, c604_clamp_time(start));
    t_c604_row = false;
}

void c604_receive_music(void* pc, char op, const char* text) {
    int flag = 0;
    float a = 0.0f;
    int32_t vol = 100;
    int32_t pitch = 100;
    char name[kAssetPathChars] = {};
    static_assert(kAssetPathChars == 192, "the %191s widths below");
    bool ok = false;
    uint64_t key = 1;
    C604Kind kind = kKinds;
    if (op == 'T') {
        ok = sscanf_s(text + 2, "%d %64s", &flag, name, static_cast<unsigned>(kChanNameMax + 1)) == 2;
        key = c604_name_key(ok ? c604_fname(name) : 0);
        kind = kTrigger;
    } else if (op == 'E') {
        ok = sscanf_s(text + 2, "%d %f", &flag, &a) == 2;
        kind = kEnd;
    } else if (op == 'C') {
        ok = sscanf_s(text + 2, "%d", &flag) == 1;
        kind = kClear;
    } else if (op == 'F') {
        ok = sscanf_s(text + 2, "%d %d %f %64s", &vol, &pitch, &a, name, static_cast<unsigned>(kChanNameMax + 1)) == 4;
        key = c604_name_key(ok ? c604_fname(name) : 0);
        kind = kName2D;
    } else if (op == '2') {
        ok = sscanf_s(text + 2, "%d %d %191s", &vol, &pitch, name, static_cast<unsigned>(sizeof(name))) == 3;
        kind = kAsset2D;
    }
    void* const asset = kind == kAsset2D && ok && g_sound_base_class != nullptr
                            ? c74_find_asset(name, g_sound_base_class())
                            : nullptr;
    key = kind == kAsset2D ? reinterpret_cast<uintptr_t>(asset) : key;
    if (!ok || kind == kKinds || (kind == kAsset2D && asset == nullptr) ||
        ((kind == kTrigger || kind == kName2D) && key == 1)) {
        ++g_c604.tally.missed;
        log::line("WARN card604: client t=%llu host row \"%s\" not played (parsed %d, asset or name here %d)",
                  static_cast<unsigned long long>(GetTickCount64()), text, ok ? 1 : 0, key != 1 && key != 0 ? 1 : 0);
        return;
    }
    if (c604_pair(kind, key, true)) {
        return;
    }
    const float v = static_cast<float>(vol < 0 || vol > kC604MaxPercent ? 100 : vol) / 100.0f;
    const float p = static_cast<float>(pitch <= 0 || pitch > kC604MaxPercent ? 100 : pitch) / 100.0f;
    t_c604_row = true;
    if (kind == kTrigger && g_orig_c604_trigger != nullptr) {
        g_orig_c604_trigger(pc, key, flag != 0);
    } else if (kind == kEnd && g_orig_c604_end != nullptr) {
        g_orig_c604_end(pc, flag != 0, c604_clamp_time(a));
    } else if (kind == kClear && g_orig_c604_clear != nullptr) {
        g_orig_c604_clear(pc, flag != 0);
    } else if (kind == kName2D && g_orig_c604_sound_name != nullptr) {
        g_orig_c604_sound_name(pc, key, v, p, c604_clamp_time(a), nullptr, nullptr);
    } else if (kind == kAsset2D && g_orig_c99_sound_2d != nullptr) {
        g_orig_c99_sound_2d(pc, asset, v, p, 0.0f, nullptr, nullptr, false);
    }
    t_c604_row = false;
}

// Host: a level-script music or 2D row, when the context is a level script actor.
void c604_host_music(const void* ctx, C604Kind kind, const char* text, int len) {
    if (len <= 0 || len >= kChanTextMax) {
        log::line("WARN card604: host %s row not sent (%d chars)", kC604KindText[kind], len);
        return;
    }
    c604_send(g_actor_get_world(ctx), kind, text, len, class_name(ctx).text);
}

bool c604_level_ctx(const void* ctx) {
    return ctx != nullptr && c582_level_script(ctx);
}

} // namespace

// From detour_pl_play_internal before the original (both sides). True: the client skips the original (a host row
// played this component). `request`: FPlayInternalRequestData, StartTime first.
bool c604_component_play(void* comp, const void* request, int32_t vol_at, int32_t sound_at) {
    g_c604.vol_at = vol_at >= 0 ? vol_at : g_c604.vol_at;
    g_c604.sound_at = sound_at >= 0 ? sound_at : g_c604.sound_at;
    const void* const owner = c604_placed_owner(comp);
    if (owner == nullptr) {
        return false;
    }
    if (!c604_level_component(comp)) {
        c604_skip(owner, comp, true); // both sides: the client's count shows the owner's own chain runs there
        return false;                 // no host row comes for it: the client's own call runs unpaired
    }
    if (g_client_session) {
        return c604_client_own(kPlay, reinterpret_cast<uintptr_t>(comp));
    }
    float start = 0.0f;
    if (request != nullptr) {
        memcpy(&start, request, sizeof(start));
    }
    const float volume = vol_at >= 0 ? read_f32(comp, static_cast<size_t>(vol_at)) : 1.0f;
    C604Names n{};
    if (!c604_names(owner, comp, n)) {
        return false;
    }
    char text[2 * kChanNameMax + 32];
    const int len = snprintf(text, sizeof(text), "P %s %s %d %.1f", n.owner, n.comp, c604_percent(volume),
                             static_cast<double>(c604_clamp_time(start)));
    if (len > 0 && static_cast<size_t>(len) < sizeof(text)) {
        c604_send(g_actor_get_world(owner), kPlay, text, len, n.owner);
    }
    return false;
}

void detour_c604_stop(void* comp) {
    const void* const owner = !g_client_session ? c604_placed_owner(comp) : nullptr;
    C604Names n{};
    if (owner != nullptr && c604_active(comp) && !c604_level_component(comp)) {
        c604_skip(owner, comp, false);
    } else if (owner != nullptr && c604_active(comp) && c604_names(owner, comp, n)) {
        char text[2 * kChanNameMax + 8];
        const int len = snprintf(text, sizeof(text), "S %s %s", n.owner, n.comp);
        if (len > 0 && static_cast<size_t>(len) < sizeof(text)) {
            c604_send(g_actor_get_world(owner), kStop, text, len, n.owner);
        }
    }
    g_orig_c604_stop(comp);
}

void detour_c604_trigger(void* ctx, uint64_t name, bool flag) {
    if (c604_level_ctx(ctx)) {
        if (c604_client_own(kTrigger, c604_name_key(name))) {
            return;
        }
        if (!g_client_session) {
            char text[kChanNameMax + 8];
            c604_host_music(ctx, kTrigger, text,
                            snprintf(text, sizeof(text), "T %d %s", flag ? 1 : 0, fname_text(&name).text));
        }
    }
    g_orig_c604_trigger(ctx, name, flag);
}

void detour_c604_end(void* ctx, bool flag, float fade) {
    if (c604_level_ctx(ctx)) {
        if (c604_client_own(kEnd, 1)) {
            return;
        }
        if (!g_client_session) {
            char text[32];
            c604_host_music(ctx, kEnd, text, snprintf(text, sizeof(text), "E %d %.1f", flag ? 1 : 0,
                                                      static_cast<double>(c604_clamp_time(fade))));
        }
    }
    g_orig_c604_end(ctx, flag, fade);
}

void detour_c604_clear(void* ctx, bool flag) {
    if (c604_level_ctx(ctx)) {
        if (c604_client_own(kClear, 1)) {
            return;
        }
        if (!g_client_session) {
            char text[8];
            c604_host_music(ctx, kClear, text, snprintf(text, sizeof(text), "C %d", flag ? 1 : 0));
        }
    }
    g_orig_c604_clear(ctx, flag);
}

void detour_c604_sound_name(void* ctx, uint64_t name, float volume, float pitch, float start, void* concurrency,
                            void* owner) {
    if (c604_level_ctx(ctx) && owner == nullptr) {
        if (c604_client_own(kName2D, c604_name_key(name))) {
            return;
        }
        if (!g_client_session) {
            char text[kChanNameMax + 32];
            c604_host_music(ctx, kName2D, text,
                            snprintf(text, sizeof(text), "F %d %d %.1f %s", c604_percent(volume), c604_percent(pitch),
                                     static_cast<double>(c604_clamp_time(start)), fname_text(&name).text));
        }
    }
    g_orig_c604_sound_name(ctx, name, volume, pitch, start, concurrency, owner);
}

// From detour_c99_sound_2d after its card 160 gate (both sides). True: the client skips the original.
bool c604_level_2d(const void* ctx, const void* owner, const void* sound, float volume, float pitch) {
    if (!c604_level_ctx(ctx) || owner != nullptr || sound == nullptr) {
        return false;
    }
    if (g_client_session) {
        return c604_client_own(kAsset2D, reinterpret_cast<uintptr_t>(sound));
    }
    char text[kAssetPathChars + 24];
    c604_host_music(ctx, kAsset2D, text,
                    snprintf(text, sizeof(text), "2 %d %d %s", c604_percent(volume), c604_percent(pitch),
                             c74_asset_path(sound).text));
    return false;
}

// Card 604 check (log only): a submix override on either side. Card 604 expects both sides to run them (C4M1 Dream
// submix: ReceiveLoaded, ReceiveLevelStart, BP_SatchelBuilding_Rubble_C_2 BuildingDestroyed).
void detour_c604_submix_set(const void* ctx, void* submix, void* presets, float fade) {
    ++g_c604.tally.submix_set;
    if (c604_line_ok()) {
        log::line("card604: %s t=%llu SetSubmixEffectChainOverride %s from %s, fade %.2f", side_text(),
                  static_cast<unsigned long long>(GetTickCount64()), object_name(submix).text, class_name(ctx).text,
                  static_cast<double>(fade));
    }
    g_orig_c604_submix_set(ctx, submix, presets, fade);
}

void detour_c604_submix_clear(const void* ctx, void* submix, float fade) {
    ++g_c604.tally.submix_clear;
    if (c604_line_ok()) {
        log::line("card604: %s t=%llu ClearSubmixEffectChainOverride %s from %s, fade %.2f", side_text(),
                  static_cast<unsigned long long>(GetTickCount64()), object_name(submix).text, class_name(ctx).text,
                  static_cast<double>(fade));
    }
    g_orig_c604_submix_clear(ctx, submix, fade);
}

// Client: ChanOp::LevelAudio.
void c604_receive(void* pc, const WireString* s) {
    static char text[kChanTextMax + 1];
    if (chan_text(s, text, sizeof(text)) < 2 || text[1] != ' ') {
        ++g_c604.tally.missed;
        log::line("WARN card604: client t=%llu unreadable LevelAudio row", static_cast<unsigned long long>(GetTickCount64()));
        return;
    }
    const char op = text[0];
    int32_t kind = 0;
    while (kind < kKinds && kC604Op[kind] != op) {
        ++kind;
    }
    if (kind == kKinds) {
        ++g_c604.tally.missed;
        log::line("WARN card604: client unknown LevelAudio row \"%s\"", text);
        return;
    }
    ++g_c604.tally.rows[kind];
    if (c604_line_ok()) {
        log::line("card604: client t=%llu host row %s: \"%s\"", static_cast<unsigned long long>(GetTickCount64()),
                  kC604KindText[kind], text);
    }
    if (op == 'P' || op == 'S') {
        c604_receive_component(g_actor_get_world(pc), op, text);
    } else {
        c604_receive_music(pc, op, text);
    }
}

// Map end and quit (c99_map_totals): the tally, then a fresh map.
void c604_totals(const char* when) {
    const C604Tally& t = g_c604.tally;
    uint32_t dropped = 0;
    for (int32_t k = 0; k < kKinds; ++k) {
        dropped += t.dropped[k];
        if (t.rows[k] != 0 || t.paired[k] != 0 || t.dropped[k] != 0) {
            log::line("card604: %s %s: %s %u rows, %u B (%.1f B/row), %u paired calls skipped, %u dropped over the cap",
                      side_text(), when, kC604KindText[k], t.rows[k], t.bytes[k],
                      t.rows[k] != 0 ? static_cast<double>(t.bytes[k]) / t.rows[k] : 0.0, t.paired[k], t.dropped[k]);
        }
    }
    log::line("%scard604: %s %s: %u host rows missed here, %u dropped over %u/s, %u not sent in an in-place load, "
              "submix set %u clear %u",
              t.missed != 0 || dropped != 0 ? "WARN " : "", side_text(), when, t.missed, dropped, kC604RowsPerSec,
              t.load_skipped, t.submix_set, t.submix_clear);
    c604_skip_summary(GetTickCount64(), when);
    const int32_t vol_at = g_c604.vol_at;
    const int32_t sound_at = g_c604.sound_at;
    g_c604 = C604State{{}, 0, {}, 0, 0, 0, vol_at, sound_at};
}

} // namespace ubw::hooks
