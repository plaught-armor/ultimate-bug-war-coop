// Card 136 step 1, the effects manager's host capture layer. Every visible effect passes one of the capture points
// (card 136 "Capture points (final)"):
//   1. UGameplayStatics PlaySoundAtLocation, SpawnSoundAtLocation, SpawnSoundAttached;
//   2. UNiagaraFunctionLibrary SpawnSystemAtLocation, SpawnSystemAttached (both overloads), the two WithParams entries;
//   3. UGameplayStatics InternalSpawnEmitterAtLocation, SpawnEmitterAttached (both overloads) (Cascade);
//   4. UAbilitySystemComponent::ExecuteGameplayCue (both overloads), UGameplayCueManager::ExecuteGameplayCue_NonReplicated;
//   5. USTDecalManager SpawnDecalAtLocation, SpawnDecalAttached;
//   6. UNiagaraComponent Activate, Deactivate, DeactivateImmediate.
// The detours of those functions (grenades.cpp, proj_c132.cpp, enemies.cpp and the point 4 and 6 detours here) call
// c136_fx_enter before the original and c136_fx_leave after it, on both sides, so the per-thread depth stays exact.
// The points nest (a cue runs a notify that spawns a system, which activates a new component): only the outermost
// captured call on a thread becomes an event. An inner capture is skipped and logged with its parent's event id.
// A component a point 2 call returned is remembered with that call's event id; a later outermost Activate or Deactivate
// of it is an event that names the spawn it restarts or stops ("of <id>"), the stop event card 136 step 4 journals.
//
// Step 1 is the host's capture and log. Steps 2 to 4 (fx_c136_net.cpp) send each event (c136_net_queue here, at the
// outermost enter; c136_net_done at its leave) and journal the live ones. Step 3, the client: the outermost captured
// call on a thread decides the block (c136_client_own: the client's own effects are kept, the host sends the rest),
// its inner calls inherit it, and the effect points' detours skip their original while c136_blocked() is true. A
// host row the client manager plays runs with `playing` set, so nothing it calls is blocked.
//
// The event: the point, the asset (or the cue tag), the transform (world, or relative to the attach component), the
// attach component and socket, the look parameters (sound volume, pitch, start; decal size, life; scale), the
// component's RandomSeedOffset (Niagara spawns, read after the spawn), the host time, the owner, the instigator, and the
// key the host chose for the owner from its own flags (card 136 "Actor keys"):
//   guid   the owner replicates (step 2 puts its NetGUID in the frame);
//   placed the owner is placed and does not replicate (step 2: the placed name, child actors parent/component);
//   world  neither, or no owner: the client uses the event's world transform.
// The owner is the explicit owner argument, else the attach component's owner, else the context object's first actor
// outer (a component's or anim instance's outer chain ends at its actor), else, inside an anim notify, the notifying
// anim instance's first actor outer (UAnimNotify_PlayNiagaraEffect and UAnimNotify_PlaySound pass the world as the
// context: the Morita casing and fire sound get the weapon or pawn whose mesh plays the montage). The instigator is the cue parameters'
// instigator (point 4), else the owner if it is a pawn, else the owner's AActor::Instigator.
//
// Lines (host; each kind capped per process, the cap logged once):
//   card136: host fx <id> t=<ms> <point> <asset path or cue tag> at (x, y, z) rot (p, y, r) scale (x, y, z)
//            attach <component>/<socket> owner <name> (<class>) instigator <name> (<class>) key <guid|placed|world>
//            look <a> <b> <c>[ of <id>]
//   card136: host fx <id> seed <RandomSeedOffset> component <name>
//   card136: host fx skip <point> <asset or tag> in <parent id>

#include "ubw/hooks_internal.h"

#include <stdio.h>

namespace ubw::hooks {

C136CueContextFn g_orig_c136_cue_context = nullptr;
C136CueLocalFn g_orig_c136_cue_local = nullptr;
C136CueHandleFn g_orig_c136_cue_handle = nullptr;
StaticClassFn g_projectile_class = nullptr;
StaticClassFn g_gore_part_class = nullptr;
C136ActivateFn g_orig_c136_activate = nullptr;
ExecThunkFn g_orig_c409_spawn_thunk = nullptr;
ExecThunkFn g_orig_c418_sound_thunk = nullptr;
C409DestroyFn g_scene_destroy_component = nullptr;
C409ReleaseFn g_c409_release_to_pool = nullptr;
C136ComponentFn g_orig_c136_deactivate = nullptr;
C136ComponentFn g_orig_c136_deactivate_now = nullptr;
C136CueActorFn g_c136_cue_instigator = nullptr;
C136CueParamsFn g_c136_cue_params_ctor = nullptr;
C136CueParamsFn g_c136_cue_params_dtor = nullptr;
C136CueFromContextFn g_c136_cue_params_from_context = nullptr;
C136AnimNotifyFn g_orig_c136_anim_notify = nullptr;

namespace {

constexpr size_t kNiagaraSeedOffset = 0x52C; // UNiagaraComponent::RandomSeedOffset (execGetRandomSeedOffset 0x04747535)
constexpr int32_t kOwnerOuters = 6;          // outer chain steps from a context object to its actor
constexpr uint32_t kC136EventLines = 200000; // event lines per process (fx_pair pairs every one)
constexpr uint32_t kC136SkipLines = 20000;   // inner-skip lines per process
constexpr int32_t kC136Live = 4096;          // live point 2 components (step 7 measures the peak)
constexpr uint32_t kC136FullLines = 20;      // live-set-full WARN lines per process
constexpr uint32_t kC136OwnLines = 20000;    // client kept-own lines per map (run 6 fix H)
constexpr uint32_t kC136CueLines = 20000;    // client engine cue copy lines per map (run 6 fix B)
constexpr uint64_t kCueEventExecuted = 2;    // EGameplayCueEvent::Executed (OnActive 0, WhileActive 1, Removed 3)
constexpr size_t kC136CueParamsSize = 0x200; // FGameplayCueParameters (about 0xD0) with room to spare

struct C136PointRow {
    const char* name;
    bool niagara_spawn; // point 2: the call returns the UNiagaraComponent it spawned
    bool component;     // point 6: the call acts on an existing UNiagaraComponent
};
constexpr C136PointRow kC136Points[static_cast<int>(C136Point::Count)] = {
    {"PlaySoundAtLocation", false, false},
    {"SpawnSoundAtLocation", false, false},
    {"SpawnSoundAttached", false, false},
    {"SpawnSystemAtLocation", true, false},
    {"SpawnSystemAttached", true, false},
    {"SpawnSystemAttachedScale", true, false},
    {"SpawnSystemAtLocationWithParams", true, false},
    {"SpawnSystemAttachedWithParams", true, false},
    {"InternalSpawnEmitterAtLocation", false, false},
    {"SpawnEmitterAttached", false, false},
    {"SpawnEmitterAttachedScale", false, false},
    {"ExecuteGameplayCue", false, false},
    {"ExecuteGameplayCueContext", false, false},
    {"ExecuteGameplayCue_NonReplicated", false, false},
    {"SpawnDecalAtLocation", false, false},
    {"SpawnDecalAttached", false, false},
    {"NiagaraActivate", false, true},
    {"NiagaraDeactivate", false, true},
    {"NiagaraDeactivateImmediate", false, true},
    {"HandleGameplayCue", false, false}, // run 6: a scope only, never an event (never sent)
};

struct C136Thread {
    int32_t depth;     // captured calls open on this thread
    uint32_t outer_id; // the outermost one's event id (0: none, or not an event)
    bool blocked;      // client: the last outermost call's block decision (inner calls inherit it)
    bool playing;      // client: the manager is playing a host row
    // Host, run 6: inside an executed cue that reached UGameplayCueManager::HandleGameplayCue outside every capture
    // point, the cue's instigator, which the events it draws carry (their spawns have a world context, no owner).
    bool cue_scope;
    const void* cue_instigator;
};
thread_local C136Thread t_c136{};
thread_local const void* t_c136_notify = nullptr; // the anim instance whose notify is running (nested: saved, restored)

// A component a point 2 call returned, while it lives: the pointer is matched, the weak pointer proves the object at
// that address is still the one spawned (a freed and reused address fails the serial check).
struct C136Spawned {
    const void* component;
    C31Weak weak;
    uint32_t id;
};
struct C136Live {
    int32_t n;
    int32_t peak;
    C136Spawned rows[kC136Live];
};
struct C136Counts { // per map; logged by c136_report (map end, map arm, quit)
    uint32_t events[static_cast<int>(C136Point::Count)];
    uint32_t skips;
    uint32_t full;    // point 2 components not remembered: the live set was full of live components
    uint32_t dropped; // remembered components found dead (destroyed or garbage collected) and removed
    uint32_t blocked_calls; // client: effect calls skipped (c136_blocked true)
    uint32_t blocked_outer; // client: outermost calls decided blocked
    uint32_t kept_outer;    // client: outermost calls decided kept (own, or a host row playing)
    uint32_t cue_dropped;   // client, run 6: engine copies of executed cues the host sends (blocked at depth 0)
    uint32_t cue_kept;      // client, run 6: executed cues at depth 0 the client's own pawn or PC instigated
    uint32_t cue_lines;     // client: UBWCOOP_C136_LOG lines of the two counts above
    uint32_t own_lines;     // client: UBWCOOP_C136_LOG lines of kept outermost calls
};
struct C136State {
    uint32_t next_id;
    uint32_t event_lines;
    uint32_t skip_lines;
    uint32_t full_lines;
    C136Counts counts;
    C136Live live;
    // Host side: the map's ENetMode, read once from the first outermost call with an owner actor (kC136ModeUnknown
    // until then; c136_arm_map starts it over). Capture runs only on a listen server: standalone (single player, the
    // menu, a client's own menu map) skips every event. A listen server with no client yet still captures, so the
    // journal holds the placed and looping rows a later join or a reconnect replays.
    uint32_t net_mode;
    uint32_t standalone_skips; // outermost calls skipped as standalone this map
};
constexpr uint32_t kC136ModeUnknown = 0xFF;
C136State g_c136{0, 0, 0, 0, C136Counts{}, C136Live{}, kC136ModeUnknown, 0};

const C136PointRow& c136_row(C136Point p) {
    return kC136Points[static_cast<int>(p)];
}

// The actor an effect belongs to: the explicit owner, the attach component's owner, else the context's first actor
// along its outer chain (an actor itself, a component, an anim instance under a mesh component).
const void* c136_first_actor(const void* o) {
    const void* const actor_class = g_actor_class();
    for (int32_t i = 0; i < kOwnerOuters && o != nullptr; ++i) {
        if (is_a(o, actor_class)) {
            return o;
        }
        o = read_ptr(o, kObjectOuter);
    }
    return nullptr;
}

const void* c136_owner(const C136Call& c) {
    if (c.owner != nullptr) {
        return c.owner;
    }
    if (c.attach != nullptr) {
        return read_ptr(c.attach, kComponentOwner);
    }
    const void* const actor = c136_first_actor(c.context);
    return actor != nullptr ? actor : c136_first_actor(t_c136_notify);
}

const void* c136_instigator(const C136Call& c, const void* owner) {
    if (c.cue_params != nullptr) {
        const void* const inst = g_c136_cue_instigator(c.cue_params);
        if (inst != nullptr) {
            return inst;
        }
    }
    if (owner == nullptr) {
        return nullptr;
    }
    return is_a(owner, g_pawn_class()) ? owner : read_ptr(owner, kActorInstigator);
}

// The instigator an event carries: the executed cue's (run 6 host scope) when one is open, else the call's own.
const void* c136_event_instigator(const C136Call& c, const void* owner) {
    const C136Thread& t = t_c136;
    return t.cue_scope && t.cue_instigator != nullptr ? t.cue_instigator : c136_instigator(c, owner);
}

const char* c136_key(const void* owner, const C136Call& c) {
    const bool on_actor = c.attach != nullptr || c.component != nullptr || c.cue_params != nullptr || c.cue_context;
    if (owner == nullptr || !on_actor) {
        return "world"; // an at-location effect: its world transform is the key
    }
    if (c98_replicates(owner)) {
        return "guid";
    }
    return c50_placed(owner) ? "placed" : "world";
}

bool c136_alive(const C136Spawned& s) {
    return g_weak_get != nullptr && g_weak_get(&s.weak) == s.component;
}

// The live-set row of `component`, or -1.
int32_t c136_find(const void* component) {
    C136Live& l = g_c136.live;
    for (int32_t i = 0; i < l.n; ++i) {
        if (l.rows[i].component == component) {
            return i;
        }
    }
    return -1;
}

void c136_remove_at(int32_t i) {
    C136Live& l = g_c136.live;
    l.rows[i] = l.rows[l.n - 1];
    --l.n;
}

// The event id of the live point 2 spawn that returned `component`, or 0 (a stale row is removed here).
uint32_t c136_spawned_id(const void* component) {
    const int32_t i = c136_find(component);
    if (i < 0) {
        return 0;
    }
    if (!c136_alive(g_c136.live.rows[i])) {
        ++g_c136.counts.dropped;
        c136_remove_at(i);
        return 0;
    }
    return g_c136.live.rows[i].id;
}

// Removes the rows whose component is gone (only when the set is full; a live row is never evicted).
void c136_sweep() {
    C136Live& l = g_c136.live;
    for (int32_t i = l.n - 1; i >= 0; --i) {
        if (!c136_alive(l.rows[i])) {
            ++g_c136.counts.dropped;
            c136_remove_at(i);
        }
    }
}

void c136_remember(const void* component, uint32_t id) {
    C136Live& l = g_c136.live;
    C31Weak weak{};
    if (g_weak_assign != nullptr) {
        void* const object = const_cast<void*>(component);
        g_weak_assign(&weak, &object);
    }
    const int32_t at = c136_find(component);
    if (at >= 0) { // a pooled component spawned again
        l.rows[at] = {component, weak, id};
        return;
    }
    if (l.n == kC136Live) {
        c136_sweep();
    }
    if (l.n == kC136Live) {
        ++g_c136.counts.full;
        if (g_c136.full_lines < kC136FullLines) {
            ++g_c136.full_lines;
            log::line("WARN card136: host fx %u: live set full (%d live, peak %d): component %s not remembered, its "
                      "stop will look like a placed component's", id, l.n, l.peak, object_name(component).text);
        }
        return;
    }
    l.rows[l.n] = {component, weak, id};
    ++l.n;
    l.peak = l.n > l.peak ? l.n : l.peak;
}

// A point 2 component stopped: it leaves the live set (a later restart goes through point 2 again).
void c136_forget(C136Point point, const void* component) {
    if (point != C136Point::NiagaraDeactivate && point != C136Point::NiagaraDeactivateImmediate) {
        return;
    }
    const int32_t i = c136_find(component);
    if (i >= 0) {
        c136_remove_at(i);
    }
}

ObjectName c136_asset_text(const C136Call& c) {
    if (c.asset == nullptr) {
        return fname_text(&c.tag); // a cue: its tag
    }
    return object_name(c.asset);
}

void c136_log_skip(const C136Call& c, uint32_t parent) {
    ++g_c136.counts.skips;
    if (!g_c136_log || !note_event_cap(g_c136.skip_lines, kC136SkipLines, "card136 host fx skip lines")) {
        return;
    }
    log::line("card136: host fx skip %s %s in %u", c136_row(c.point).name, c136_asset_text(c).text, parent);
}

void c136_log_event(uint32_t id, const C136Call& c, uint32_t of) {
    ++g_c136.counts.events[static_cast<int>(c.point)];
    if (!g_c136_log || !note_event_cap(g_c136.event_lines, kC136EventLines, "card136 host fx event lines")) {
        return;
    }
    const void* const owner = c136_owner(c);
    const void* const inst = c136_event_instigator(c, owner);
    C74Path asset{};
    snprintf(asset.text, sizeof(asset.text), "%s",
             c.asset != nullptr ? c74_asset_path(c.asset).text : fname_text(&c.tag).text); // the temporary lives here
    const void* const attach = c.attach != nullptr ? c.attach : c.component;
    char of_text[24] = "";
    if (of != 0) {
        snprintf(of_text, sizeof(of_text), " of %u", of);
    }
    // Run 4: the player pawn the event belongs to (owner, the owner's owner for a weapon, or instigator), so the gap
    // split counts the events filtered as that client's own directly.
    const void* const player = g_player_mi_class != nullptr ? g_player_mi_class() : nullptr;
    const void* const owner_owner = owner != nullptr ? weapon_pawn(owner) : nullptr;
    const void* pawn = nullptr;
    for (const void* const a : {owner, owner_owner, inst}) {
        pawn = pawn == nullptr && player != nullptr && a != nullptr && is_a(a, player) ? a : pawn;
    }
    char pawn_text[160] = "";
    if (pawn != nullptr) {
        // hb65: the object name is the class name for every pawn in this build, so the side, address and NetGUID
        // (0 when the pawn has none yet) name it.
        snprintf(pawn_text, sizeof(pawn_text), " pawn %s:%p guid %llu", kSprintSideText[sprint_side(pawn)], pawn,
                 static_cast<unsigned long long>(c41_guid(pawn)));
    }
    log::line("card136: host fx %u t=%llu %s %s at (%.1f, %.1f, %.1f) rot (%.1f, %.1f, %.1f) scale (%.2f, %.2f, %.2f) "
              "attach %s/%s owner %s (%s) instigator %s (%s) key %s look %.2f %.2f %.2f%s%s",
              id, static_cast<unsigned long long>(GetTickCount64()), c136_row(c.point).name, asset.text, c.at.x, c.at.y,
              c.at.z, c.rot.x, c.rot.y, c.rot.z, c.scale.x, c.scale.y, c.scale.z, object_name(attach).text,
              fname_text(&c.socket).text, object_name(owner).text, class_name(owner).text, object_name(inst).text,
              class_name(inst).text, c136_key(owner, c), c.look[0], c.look[1], c.look[2], of_text, pawn_text);
}

} // namespace

namespace {

// Client, run 6 (fix H): an outermost effect call the client keeps as its own, so a double with a host row can be
// measured (point, asset, place, owner). Capped per map.
void c136_log_own(const C136Call& c, const void* owner) {
    if (!g_c136_log || !note_event_cap(g_c136.counts.own_lines, kC136OwnLines, "card136 client fx own lines")) {
        return;
    }
    C74Path asset{};
    snprintf(asset.text, sizeof(asset.text), "%s",
             c.asset != nullptr ? c74_asset_path(c.asset).text : fname_text(&c.tag).text);
    log::line("card136: client fx own t=%llu %s %s at (%.1f, %.1f, %.1f) owner %s",
              static_cast<unsigned long long>(GetTickCount64()), c136_row(c.point).name, asset.text, c.at.x, c.at.y,
              c.at.z, owner != nullptr ? class_name(owner).text : "-");
}

// Card 518 (diagnostic): the client's blocked outermost calls by asset (a cue by its tag), with the first owner's class
// and how many ran inside an anim notify. Compared with the host's card329 rows per asset, it shows which effects the
// client already makes itself (footsteps from its own bug animations) and could keep instead of a host row.
// Step e: the host counts its own outermost calls in the same table, before any send filter, so the two sides' call
// counts per asset compare directly (c263L317: client AI Morita shot sound 6,920 blocked against 2,357 host rows).
// Card 518 step h: 128 rows were full in c263L322 (156 calls not counted), so the table holds 256.
constexpr int32_t kC518Blocked = 256;
constexpr int32_t kC518BlockedLines = 16;
struct C518Blocked {
    const void* asset;    // a compare key with tag, never read through (card 608: a reused address merges two assets)
    uint64_t asset_name;  // copied at insert
    uint64_t tag;
    uint64_t owner_class; // the first owner's class FName, copied at insert; 0 no owner
    uint32_t n;
    uint32_t in_notify;
};
struct C518BlockedTable {
    int32_t n;
    uint32_t unrowed;
    C518Blocked rows[kC518Blocked];
};
C518BlockedTable g_c518_blocked{}; // game thread only (the client's effect calls run there)

void c518_note_blocked(const C136Call& c, const void* owner) {
    C518BlockedTable& t = g_c518_blocked;
    if (g_is_in_game_thread != nullptr && !g_is_in_game_thread()) {
        ++t.unrowed; // the table is game thread only
        return;
    }
    int32_t i = 0;
    while (i < t.n && !(t.rows[i].asset == c.asset && t.rows[i].tag == c.tag)) {
        ++i;
    }
    if (i == t.n && t.n >= kC518Blocked) {
        ++t.unrowed;
        return;
    }
    if (i == t.n) {
        t.rows[t.n++] = C518Blocked{c.asset, object_fname(c.asset), c.tag,
                                     owner != nullptr ? object_fname(read_ptr(owner, kObjectClass)) : 0, 0, 0};
    }
    ++t.rows[i].n;
    t.rows[i].in_notify += t_c136_notify != nullptr ? 1u : 0u;
}

// The kC518BlockedLines largest rows, then the table starts over.
void c518_blocked_lines(const char* when) {
    C518BlockedTable& t = g_c518_blocked;
    for (int32_t line = 0; line < kC518BlockedLines && line < t.n; ++line) {
        int32_t best = line;
        for (int32_t j = line + 1; j < t.n; ++j) {
            best = t.rows[j].n > t.rows[best].n ? j : best;
        }
        const C518Blocked r = t.rows[best];
        t.rows[best] = t.rows[line];
        t.rows[line] = r;
        log::line("card518: %s (%s) %s: %u calls, %u in an anim notify, first owner %s",
                  g_client_session ? "client fx blocked" : "host fx calls", when,
                  r.asset != nullptr ? fname_text(&r.asset_name).text : fname_text(&r.tag).text, r.n, r.in_notify,
                  r.owner_class != 0 ? fname_text(&r.owner_class).text : "-");
    }
    if (t.unrowed != 0) {
        log::line("card518: %s (%s): %u calls not counted (table of %d full)",
                  g_client_session ? "client fx blocked" : "host fx calls", when, t.unrowed, kC518Blocked);
    }
    t = C518BlockedTable{};
}

} // namespace

// Both sides, before the capture point's original runs. Returns the event id (0: inner, client, or no event).
uint32_t c136_fx_enter(const C136Call& c) {
    C136Thread& t = t_c136;
    ++t.depth;
    if (g_client_session) {
        // Card 566: a decal drawn while a host row plays never reaches here: the spawn detours' c132_decal_suppress
        // drops every client decal not drawn by the card 132 relay (the host's journal brings it).
        if (t.depth == 1) { // step 3: the outermost call decides; its inner calls inherit
            const void* const owner = c136_owner(c);
            t.blocked = !t.playing && !c136_client_own(c, owner, c136_instigator(c, owner));
            ++(t.blocked ? g_c136.counts.blocked_outer : g_c136.counts.kept_outer);
            if (!t.blocked && !t.playing && c.point != C136Point::CueHandle) {
                c136_log_own(c, owner);
            }
            if (t.blocked) {
                c518_note_blocked(c, owner);
            }
        }
        return 0;
    }
    if (g_c136.net_mode != kNetModeListenServer && t.depth == 1) {
        if (g_c136.net_mode == kC136ModeUnknown) {
            const void* const first = c136_owner(c);
            g_c136.net_mode = first != nullptr ? net_mode(first) : kC136ModeUnknown;
        }
        if (g_c136.net_mode != kNetModeListenServer && g_c136.net_mode != kC136ModeUnknown) {
            ++g_c136.standalone_skips;
            return 0; // not a listen server: no event, no row
        }
    }
    if (t.depth > 1) {
        if (t.outer_id == 0) {
            return 0; // inside a standalone-skipped (or unknown-mode first) outermost call
        }
        c136_log_skip(c, t.outer_id);
        return 0;
    }
    const uint32_t of = c136_row(c.point).component ? c136_spawned_id(c.component) : 0u;
    const uint32_t id = ++g_c136.next_id;
    t.outer_id = id;
    c136_log_event(id, c, of);
    const void* const owner = c136_owner(c);
    c518_note_blocked(c, owner); // card 518 step e: the host's own calls per asset, before the send filters
    c136_net_queue(id, c, owner, c136_event_instigator(c, owner), c136_key(owner, c), of);
    return id;
}

const char* c136_point_name(C136Point point) {
    return c136_row(point).name;
}

int32_t c136_depth() {
    return t_c136.depth;
}

bool c136_blocked() {
    if (!g_client_session || !t_c136.blocked) {
        return false;
    }
    ++g_c136.counts.blocked_calls;
    return true;
}

void c136_set_playing(bool on) {
    t_c136.playing = on;
}

// Both sides, after the original returned (`made`: the component a point 2 call returned, else null).
void c136_fx_leave(C136Point point, uint32_t id, const void* made) {
    C136Thread& t = t_c136;
    if (t.depth <= 0) {
        log::line("WARN card136: %s fx leave %s without an enter (depth %d)", side_text(), c136_row(point).name,
                  t.depth);
        return;
    }
    --t.depth;
    if (t.depth == 0) {
        t.outer_id = 0;
    }
    if (id != 0) {
        c136_net_done(id, made); // host, outermost: the row is complete (the spawn's seed read now)
    }
    if (id == 0 || made == nullptr || !c136_row(point).niagara_spawn) {
        return;
    }
    c136_remember(made, id);
    if (g_c136_log && note_event_cap(g_c136.event_lines, kC136EventLines, "card136 host fx event lines")) {
        log::line("card136: host fx %u seed %d component %s", id,
                  *reinterpret_cast<const int32_t*>(static_cast<const uint8_t*>(made) + kNiagaraSeedOffset),
                  object_name(made).text);
    }
}

// --- Point 4 and 6 detours (kHooksUbw) ---------------------------------------------------------------------------

void detour_c136_cue_context(void* asc, uint64_t tag, const void* context) {
    C136Call c{C136Point::CueContext};
    c.tag = tag;
    c.owner = read_ptr(asc, kComponentOwner);
    c.cue_context = true;
    // Host: the cue's values (location, normal, physical material, instigator) as the engine builds them from the
    // context, so the event carries what ExecuteGameplayCue_NonReplicated needs on the client.
    alignas(16) uint8_t params[kC136CueParamsSize];
    const bool built = !g_client_session && context != nullptr && g_c136_cue_params_from_context != nullptr &&
                       g_c136_cue_params_dtor != nullptr;
    if (built) {
        g_c136_cue_params_from_context(params, context);
        c.cue_params = params;
    }
    const uint32_t id = c136_fx_enter(c);
    g_orig_c136_cue_context(asc, tag, context);
    c136_fx_leave(c.point, id, nullptr);
    if (built) {
        g_c136_cue_params_dtor(params);
    }
}

void detour_c136_cue_local(void* target, uint64_t tag, const void* parameters) {
    C136Call c{C136Point::CueNonReplicated};
    c.tag = tag;
    c.owner = target;
    c.cue_params = parameters;
    const uint32_t id = c136_fx_enter(c);
    g_orig_c136_cue_local(target, tag, parameters);
    c136_fx_leave(c.point, id, nullptr);
}

// Run 6 fix B (card item 3): every cue that runs on a side ends in UGameplayCueManager::HandleGameplayCue. An executed
// cue that arrives here at depth 0 passed no capture point on this side: on the client it is the engine's replicated
// copy (the NetMulticast InvokeGameplayCueExecuted variants, card 132 Trace 1) or a predicted cue flushed later. The
// instigator rule decides it: the client's own pawn or PC instigated it, kept; anything else, blocked, because the host
// sends it. The original always runs; the capture points inside inherit the decision. On the host the scope gives the
// events the cue draws the cue's instigator. Persistent cue events (OnActive, WhileActive, Removed) keep the capture
// point rules.
void detour_c136_cue_handle(void* manager, void* target, uint64_t tag, uint64_t event, const void* params,
                            uint64_t options) {
    C136Thread& t = t_c136;
    if (t.depth != 0 || (event & 0xFF) != kCueEventExecuted || params == nullptr) {
        g_orig_c136_cue_handle(manager, target, tag, event, params, options);
        return;
    }
    if (!g_client_session) {
        const bool was_scope = t.cue_scope; // a cue that runs another before any capture point: nested scopes
        const void* const was_instigator = t.cue_instigator;
        t.cue_scope = true;
        t.cue_instigator = g_c136_cue_instigator(params);
        g_orig_c136_cue_handle(manager, target, tag, event, params, options);
        t.cue_scope = was_scope;
        t.cue_instigator = was_instigator;
        return;
    }
    C136Call c{C136Point::CueHandle};
    c.tag = tag;
    c.owner = target;
    c.cue_params = params;
    const bool playing = t.playing;
    c136_fx_enter(c);
    if (!playing) {
        ++(t.blocked ? g_c136.counts.cue_dropped : g_c136.counts.cue_kept);
        if (g_c136_log && note_event_cap(g_c136.counts.cue_lines, kC136CueLines, "card136 client cue copy lines")) {
            const void* const inst = g_c136_cue_instigator(params);
            log::line("card136: client engine cue t=%llu %s %s target %s instigator %s",
                      static_cast<unsigned long long>(GetTickCount64()), t.blocked ? "dropped" : "kept",
                      fname_text(&tag).text, target != nullptr ? class_name(target).text : "-",
                      inst != nullptr ? class_name(inst).text : "-");
        }
    }
    g_orig_c136_cue_handle(manager, target, tag, event, params, options);
    c136_fx_leave(c.point, 0, nullptr);
}

namespace {

C136Call c136_component_call(C136Point point, void* component) {
    C136Call c{point};
    c.asset = component != nullptr ? read_ptr(component, kNiagaraAsset) : nullptr;
    c.component = component;
    c.owner = component != nullptr ? read_ptr(component, kComponentOwner) : nullptr;
    c.at = component != nullptr ? read_vec3(component, kComponentTranslation) : Vec3{};
    return c;
}

// Client, point 6 under a blocked decision: an owned component is skipped (the host sends the op, for a placed owner
// too); an ownerless one at the outermost call still runs, counted as an extra effect.
bool c136_component_skip(const C136Call& c) {
    if (!g_client_session || !t_c136.blocked) {
        return false;
    }
    if (t_c136.depth == 1 && c.owner == nullptr) {
        c136_client_extra(c.asset);
        return false;
    }
    return c136_blocked();
}

} // namespace

// The notify's effects (UAnimNotify_PlayNiagaraEffect, UAnimNotify_PlaySound) take the world as context; c136_owner
// reads the anim instance kept here while the notify runs.
void detour_c136_anim_notify(void* anim_instance, void* notify_ref) {
    const void* const saved = t_c136_notify;
    t_c136_notify = anim_instance;
    g_orig_c136_anim_notify(anim_instance, notify_ref);
    t_c136_notify = saved;
}

// Card 408: an effect a flak or plasma bug building's own code makes: the anim instance whose notify runs, or the
// ProcessEvent in progress, is the building (or its projectile), or the owner is the plasma projectile. Both sides make
// these themselves from the same synced turn and timeout, so the host does not send them and the client keeps its own.
// The building's death effects come from its ReceiveOnSatchelExploded, which both sides run (the client from the host's
// 'S' row), so they are local too. An effect the building only owns, with neither in progress (a player's bullet hit
// on it: run c408b115a logged NS_FX_Weapons_MoritaHit_Ground "by owner"), is not its own and is sent as usual.
bool c136_attack_local(const C136Call& c, const void* owner) {
    if (!pe_gate(kPeGate_c408_pe)) {
        return false;
    }
    const char* by = c408_projectile(owner)                             ? "projectile owner"
                     : c408_attack(c136_first_actor(t_c136_notify))     ? "notify"
                     : c408_attack(c136_first_actor(g_c128_pe_object)) ? "ProcessEvent"
                                                                         : nullptr;
    if (by == nullptr) {
        return false;
    }
    constexpr int32_t kSeen = 24;
    static const void* seen[kSeen];
    static int32_t n = 0;
    bool known = c.asset == nullptr;
    for (int32_t i = 0; i < n && !known; ++i) {
        known = seen[i] == c.asset;
    }
    if (!known && n < kSeen && !g_client_session) {
        seen[n++] = c.asset;
        log::line("card408: host t=%llu fx local %s %s by %s", static_cast<unsigned long long>(GetTickCount64()),
                  c136_row(c.point).name, object_name(c.asset).text, by);
    }
    return true;
}

// --- Cards 409 step 2 and 418: Blueprint at-location spawns through their script thunks ------------------------------
// UNiagaraFunctionLibrary::execSpawnSystemAtLocation and UGameplayStatics::execSpawnSoundAtLocation have the spawn
// body inlined: a Blueprint call never reaches the detoured SpawnSystemAtLocation / SpawnSoundAtLocation. Each thunk
// detour raises a per-thread mark around the original (always called: it reads its parameters from the script frame)
// and notes FFrame::Object, the calling Blueprint instance, as the event's context. The first Activate (409) or audio
// play (418) under the mark, outside every other capture point, is the spawn: it becomes the point's event, built from
// the component the body just made and placed. Client: the same call makes the own-or-host decision; a host-sent one
// is not activated or played, and its component is discarded after the thunk returns.
namespace {

constexpr size_t kComponentScale = 0x220;        // ComponentToWorld scale (remote_body.cpp)
constexpr uint8_t kNiagaraManualRelease = 2;     // ENCPoolMethod::ManualRelease
constexpr int32_t kC409Assets = 48;
constexpr int32_t kC418Classes = 16;

struct C409Mark {
    const void* context; // FFrame::Object: the Blueprint instance that called the node
    const void* component; // the component the converted Activate or play acted on
    const void* asset;     // its system or sound
    bool up;      // inside the thunk
    bool seen;    // its Activate or play ran (only the first one converts)
    bool blocked; // client: the host sends it (the component is discarded after the thunk)
};
thread_local C409Mark t_c409{}; // card 409: execSpawnSystemAtLocation
thread_local C409Mark t_c418{}; // card 418: execSpawnSoundAtLocation

struct C409Row {
    const void* asset;  // a compare key, never read through (card 608: a reused address merges two assets)
    uint64_t name;      // copied at insert
    uint32_t events;    // converted calls (host: events made; client: own-or-host decisions)
    uint32_t blocked;   // client: host-sent, component discarded
    uint32_t unseen;    // a component returned with no Activate or play under the mark (nothing sent)
};
struct C409Table {
    int32_t n;
    uint32_t unrowed; // calls not counted: the table was full
    C409Row rows[kC409Assets];
};
struct C418Null {
    const void* cls; // the calling Blueprint's class: a compare key, never read through (card 608: can merge two)
    uint64_t name;   // the class's, copied at insert
    uint32_t n;      // thunk calls that returned no component (FAudioDevice::CreateComponent: out of range, or no device)
};
struct C418Nulls {
    int32_t n;
    uint32_t unrowed;
    uint32_t nested; // card 418: plays inside another capture (t_c136.depth != 0), no own event
    C418Null rows[kC418Classes];
};
C409Table g_c409_assets{};
C409Table g_c418_assets{};
C418Nulls g_c418_nulls{};
uint32_t g_c418_ambient = 0; // BP_RandomEmitterVolume_C calls: per side, never an event

C409Row* c409_row(C409Table& t, const void* asset) {
    for (int32_t i = 0; i < t.n; ++i) {
        if (t.rows[i].asset == asset) {
            return &t.rows[i];
        }
    }
    if (t.n >= kC409Assets) {
        ++t.unrowed;
        return nullptr;
    }
    t.rows[t.n] = C409Row{asset, object_fname(asset), 0, 0, 0};
    return &t.rows[t.n++];
}

void c418_null(const void* cls) {
    for (int32_t i = 0; i < g_c418_nulls.n; ++i) {
        if (g_c418_nulls.rows[i].cls == cls) {
            ++g_c418_nulls.rows[i].n;
            return;
        }
    }
    if (g_c418_nulls.n >= kC418Classes) {
        ++g_c418_nulls.unrowed;
        return;
    }
    g_c418_nulls.rows[g_c418_nulls.n++] = C418Null{cls, object_fname(cls), 1};
    // The first null of a class this map is logged at once (the map line needs a map end, which a run may not reach).
    log::line("card418: %s t=%llu first SpawnSoundAtLocation by %s this map returned no component (out of audible "
              "range or no device)", side_text(), static_cast<unsigned long long>(GetTickCount64()),
              cls != nullptr ? object_name(cls).text : "-");
}

// The mark a thunk detour raises: saved by the caller and restored after the original (a nested thunk call keeps its
// own).
C409Mark c409_raise(const void* frame) {
    C409Mark m{};
    m.context = frame != nullptr ? read_ptr(frame, kFrameObject) : nullptr;
    m.up = true;
    return m;
}

// After the thunk: the WARN for a component made without its Activate or play, and the client's discard of a
// host-sent one (registered, auto destroy, never activated or played: it would stay until the map ends).
// Returns the component to discard, null when none.
void* c409_after(C409Table& t, const C409Mark& m, void* made, const void* asset, const char* card, const char* what,
                 const char* step) {
    C409Row* const row = made != nullptr ? c409_row(t, asset) : nullptr;
    if (row == nullptr) {
        return nullptr;
    }
    if (!m.seen) {
        if (row->unseen++ == 0) {
            log::line("WARN card%s: %s t=%llu %s returned %s with no %s under the mark (called by %s): nothing sent, "
                      "counted per map", card, side_text(), static_cast<unsigned long long>(GetTickCount64()),
                      what, asset != nullptr ? object_name(asset).text : "-", step,
                      m.context != nullptr ? class_name(m.context).text : "-");
        }
        return nullptr;
    }
    if (!g_client_session || !m.blocked || made != m.component) {
        return nullptr;
    }
    ++row->blocked;
    return made;
}

void c409_discard(void* comp) {
    uint8_t* const p = static_cast<uint8_t*>(comp);
    if (p[kNiagaraPoolingMethod] != 0) { // a pooled one (BP_ResupplyRack_Level1: AutoRelease) goes back to its pool
        p[kNiagaraPoolingMethod] = kNiagaraManualRelease;
        g_c409_release_to_pool(comp);
        return;
    }
    g_scene_destroy_component(comp, false);
}

void c409_table_line(C409Table& t, const char* card, const char* when, const char* step) {
    for (int32_t i = 0; i < t.n; ++i) {
        const C409Row& r = t.rows[i];
        log::line("card%s: %s map asset (%s) %s: %u converted, %u host-sent discarded, %u with no %s", card,
                  side_text(), when, r.asset != nullptr ? fname_text(&r.name).text : "-", r.events, r.blocked,
                  r.unseen, step);
    }
    if (t.unrowed != 0) {
        log::line("WARN card%s: %s map (%s): %u calls not counted (table of %d full)", card, side_text(), when,
                  t.unrowed, kC409Assets);
    }
    t = C409Table{};
}

} // namespace

// Card 526: the client's discard of a host-sent component (c409_discard), for the attached-spawn detours.
void c136_discard_component(void* comp) {
    c409_discard(comp);
}

// Card 409 step 2, from detour_c136_activate: true when this Activate was the thunk's spawn (handled here).
bool c409_activate_spawn(void* component, bool reset) {
    C409Mark& m = t_c409;
    if (!m.up || m.seen || component == nullptr) {
        return false;
    }
    m.seen = true;
    if (t_c136.depth != 0) {
        return false; // inside another capture point: that call's event covers it
    }
    C136Call c{C136Point::Niagara};
    c.asset = read_ptr(component, kNiagaraAsset);
    c.context = m.context;
    c.at = read_vec3(component, kComponentTranslation);
    c.rot = component_rotator(component);
    c.scale = read_vec3(component, kComponentScale);
    // Every Blueprint caller in the card's import sweep passes bAutoDestroy and bAutoActivate true.
    c.flags = static_cast<uint8_t>(kC136FlagAutoDestroy | kC136FlagAutoActivate);
    const uint32_t id = c136_fx_enter(c);
    m.blocked = c136_blocked();
    m.component = component;
    m.asset = c.asset;
    C409Row* const row = c409_row(g_c409_assets, c.asset);
    row != nullptr ? ++row->events : 0u;
    if (!m.blocked) {
        g_orig_c136_activate(component, reset);
    }
    c136_fx_leave(c.point, id, component); // host: the seed line, and the live entry (a pooled one replaced)
    return true;
}

void detour_c409_spawn_thunk(void* context, void* frame, void* result) {
    const C409Mark saved = t_c409;
    t_c409 = c409_raise(frame);
    g_orig_c409_spawn_thunk(context, frame, result);
    const C409Mark m = t_c409;
    t_c409 = saved;
    void* const made = result != nullptr ? *static_cast<void**>(result) : nullptr;
    void* const discard =
        c409_after(g_c409_assets, m, made, made != nullptr ? read_ptr(made, kNiagaraAsset) : nullptr, "409",
                   "SpawnSystemAtLocation", "Activate");
    if (discard != nullptr) {
        c409_discard(discard);
    }
}

// Card 418, from detour_pl_play_internal before its original: true when this play is the thunk's spawn. Then *id is
// the event id (0 on the client), the caller plays only when c136_blocked() is false and closes with c136_fx_leave.
bool c418_play_enter(void* comp, const void* sound, float volume, float pitch, const void* atten, uint32_t* id) {
    C409Mark& m = t_c418;
    if (!m.up || m.seen || comp == nullptr) {
        return false;
    }
    m.seen = true;
    if (t_c136.depth != 0) {
        if (g_c418_nulls.nested++ == 0) { // first this map at once, the count at map end
            log::line("card418: %s t=%llu first SpawnSoundAtLocation of %s this map inside another capture (depth %d): "
                      "no own event", side_text(), static_cast<unsigned long long>(GetTickCount64()),
                      object_name(sound).text, t_c136.depth);
        }
        return false;
    }
    C136Call c{C136Point::SpawnSound};
    c.asset = sound;
    c.context = m.context;
    c.at = read_vec3(comp, kComponentTranslation);
    c.rot = component_rotator(comp);
    c.look[0] = volume;
    c.look[1] = pitch;
    c.look[2] = 0.0f; // the start time is in the play request, not on the component; every caller seen passes 0
    c.flags = kC136FlagAutoDestroy;
    c.atten = atten;
    c.located = true;
    *id = c136_fx_enter(c);
    m.blocked = c136_blocked();
    m.component = comp;
    m.asset = sound;
    C409Row* const row = c409_row(g_c418_assets, sound);
    row != nullptr ? ++row->events : 0u;
    return true;
}

void detour_c418_sound_thunk(void* context, void* frame, void* result) {
    const void* const caller = frame != nullptr ? read_ptr(frame, kFrameObject) : nullptr;
    if (caller != nullptr && strcmp(class_name(caller).text, "BP_RandomEmitterVolume_C") == 0) {
        ++g_c418_ambient; // ambient one-shots: each side keeps its own
        g_orig_c418_sound_thunk(context, frame, result);
        return;
    }
    const C409Mark saved = t_c418;
    t_c418 = c409_raise(frame);
    g_orig_c418_sound_thunk(context, frame, result);
    const C409Mark m = t_c418;
    t_c418 = saved;
    void* const made = result != nullptr ? *static_cast<void**>(result) : nullptr;
    if (made == nullptr) {
        c418_null(caller != nullptr ? read_ptr(caller, kObjectClass) : nullptr);
        return;
    }
    void* const discard = c409_after(g_c418_assets, m, made, m.asset, "418", "SpawnSoundAtLocation", "play");
    if (discard != nullptr) { // never played: no Stop needed
        g_scene_destroy_component(discard, false);
    }
}

// Per map, from c136_report: the converted calls per asset on each side, the 418 null results and ambient count.
void c409_report(const char* when) {
    c409_table_line(g_c409_assets, "409", when, "Activate");
    c409_table_line(g_c418_assets, "418", when, "play");
    for (int32_t i = 0; i < g_c418_nulls.n; ++i) {
        log::line("card418: %s map (%s): %u SpawnSoundAtLocation calls by %s returned no component (out of audible "
                  "range or no device: no event, the other side hears nothing)", side_text(), when,
                  g_c418_nulls.rows[i].n,
                  g_c418_nulls.rows[i].cls != nullptr ? fname_text(&g_c418_nulls.rows[i].name).text : "-");
    }
    if (g_c418_nulls.nested != 0) {
        log::line("card418: %s map (%s): %u SpawnSoundAtLocation plays inside another capture (no own event)",
                  side_text(), when, g_c418_nulls.nested);
    }
    if (g_c418_ambient != 0) {
        log::line("card418: %s map (%s): %u BP_RandomEmitterVolume_C calls kept per side (0 events)", side_text(), when,
                  g_c418_ambient);
    }
    g_c418_nulls = C418Nulls{};
    g_c418_ambient = 0;
}

void detour_c136_activate(void* component, bool reset) {
    if (c409_activate_spawn(component, reset)) {
        return;
    }
    C136Call c = c136_component_call(C136Point::NiagaraActivate, component);
    c.flags = reset ? kC136FlagReset : 0;
    const uint32_t id = c136_fx_enter(c);
    if (!c136_component_skip(c)) {
        g_orig_c136_activate(component, reset);
    }
    c136_fx_leave(c.point, id, nullptr);
}

void detour_c136_deactivate(void* component) {
    const C136Call c = c136_component_call(C136Point::NiagaraDeactivate, component);
    const uint32_t id = c136_fx_enter(c);
    if (!c136_component_skip(c)) {
        g_orig_c136_deactivate(component);
    }
    c136_fx_leave(c.point, id, nullptr);
    if (!g_client_session) {
        c136_forget(c.point, component); // after the event, which named the spawn it stops
    }
}

void detour_c136_deactivate_now(void* component) {
    const C136Call c = c136_component_call(C136Point::NiagaraDeactivateImmediate, component);
    const uint32_t id = c136_fx_enter(c);
    if (!c136_component_skip(c)) {
        g_orig_c136_deactivate_now(component);
    }
    c136_fx_leave(c.point, id, nullptr);
    if (!g_client_session) {
        c136_forget(c.point, component);
    }
}

namespace {

// --- Step 7 measurement, UBWCOOP_C136_LOG=1 only: UNiagaraComponent::SetVariable* after the spawn -----------------
// Counted per (asset, variable) on the host and logged per map, so step 7 can list the assets whose user parameters
// change after a captured spawn and decide which of those calls change what a player sees.

constexpr int32_t kC136SetVarRows = 256;
constexpr int32_t kC136SetVarKinds = 12;
using C136SetVarFn = void (*)(void* component, uint64_t name, uint64_t value);
using C136SetFloatFn = void (*)(void* component, uint64_t name, float value);

struct C136SetVarRow {
    const void* asset;   // a compare key with name, never read through (card 608: a reused address merges two assets)
    uint64_t asset_name; // copied at insert
    uint64_t name; // the variable's FName
    uint32_t calls;
    bool live;     // at least one call was on a component a captured point 2 spawn returned
};
struct C136SetVars {
    int32_t n;
    uint32_t unrowed; // calls not counted: the table was full
    C136SetVarRow rows[kC136SetVarRows];
};
C136SetVars g_c136_setvars{};
C136SetVarFn g_c136_setvar[kC136SetVarKinds] = {};
C136SetFloatFn g_c136_set_float = nullptr;

void c136_setvar_note(const void* component, uint64_t name) {
    if (g_client_session || component == nullptr) {
        return;
    }
    const void* const asset = read_ptr(component, kNiagaraAsset);
    C136SetVars& s = g_c136_setvars;
    const bool live = c136_find(component) >= 0;
    for (int32_t i = 0; i < s.n; ++i) {
        if (s.rows[i].asset == asset && s.rows[i].name == name) {
            ++s.rows[i].calls;
            s.rows[i].live = s.rows[i].live || live;
            return;
        }
    }
    if (s.n == kC136SetVarRows) {
        ++s.unrowed;
        return;
    }
    s.rows[s.n] = {asset, object_fname(asset), name, 1u, live};
    ++s.n;
}

#define C136_SETVAR(k)                                                                                               \
    void detour_c136_setvar_##k(void* component, uint64_t name, uint64_t value) {                                    \
        c136_setvar_note(component, name);                                                                           \
        g_c136_setvar[k](component, name, value);                                                                    \
    }
C136_SETVAR(0)
C136_SETVAR(1)
C136_SETVAR(2)
C136_SETVAR(3)
C136_SETVAR(4)
C136_SETVAR(5)
C136_SETVAR(6)
C136_SETVAR(7)
C136_SETVAR(8)
C136_SETVAR(9)
C136_SETVAR(10)
#undef C136_SETVAR

void detour_c136_set_float(void* component, uint64_t name, float value) {
    c136_setvar_note(component, name);
    g_c136_set_float(component, name, value);
}

void c136_setvar_dump() {
    C136SetVars& s = g_c136_setvars;
    for (int32_t i = 0; i < s.n; ++i) {
        const C136SetVarRow& r = s.rows[i];
        log::line("card136: host fx setvar %s %s %u calls%s",
                  r.asset != nullptr ? fname_text(&r.asset_name).text : "null", fname_text(&r.name).text,
                  r.calls, r.live ? " (on a captured spawn)" : "");
    }
    if (s.n != 0 || s.unrowed != 0) {
        log::line("card136: host fx setvar map totals: %d rows, %u calls not counted (table full)", s.n, s.unrowed);
    }
    s = C136SetVars{};
}

} // namespace

// Installed with UBWCOOP_C136_LOG=1 only, after MH_Initialize.
bool c136_setvar_install(uint8_t* base, const IMAGE_NT_HEADERS64* nt) {
    const HookSpec hooks[] = {
        {&offsets::UNiagaraComponent_SetVariableBool, reinterpret_cast<void*>(&detour_c136_setvar_0),
         reinterpret_cast<void**>(&g_c136_setvar[0])},
        {&offsets::UNiagaraComponent_SetVariableInt, reinterpret_cast<void*>(&detour_c136_setvar_1),
         reinterpret_cast<void**>(&g_c136_setvar[1])},
        {&offsets::UNiagaraComponent_SetVariableLinearColor, reinterpret_cast<void*>(&detour_c136_setvar_2),
         reinterpret_cast<void**>(&g_c136_setvar[2])},
        {&offsets::UNiagaraComponent_SetVariableMatrix, reinterpret_cast<void*>(&detour_c136_setvar_3),
         reinterpret_cast<void**>(&g_c136_setvar[3])},
        {&offsets::UNiagaraComponent_SetVariablePosition, reinterpret_cast<void*>(&detour_c136_setvar_4),
         reinterpret_cast<void**>(&g_c136_setvar[4])},
        {&offsets::UNiagaraComponent_SetVariableQuat, reinterpret_cast<void*>(&detour_c136_setvar_5),
         reinterpret_cast<void**>(&g_c136_setvar[5])},
        {&offsets::UNiagaraComponent_SetVariableTextureRenderTarget, reinterpret_cast<void*>(&detour_c136_setvar_6),
         reinterpret_cast<void**>(&g_c136_setvar[6])},
        {&offsets::UNiagaraComponent_SetVariableTexture, reinterpret_cast<void*>(&detour_c136_setvar_7),
         reinterpret_cast<void**>(&g_c136_setvar[7])},
        {&offsets::UNiagaraComponent_SetVariableVec2, reinterpret_cast<void*>(&detour_c136_setvar_8),
         reinterpret_cast<void**>(&g_c136_setvar[8])},
        {&offsets::UNiagaraComponent_SetVariableVec3, reinterpret_cast<void*>(&detour_c136_setvar_9),
         reinterpret_cast<void**>(&g_c136_setvar[9])},
        {&offsets::UNiagaraComponent_SetVariableVec4, reinterpret_cast<void*>(&detour_c136_setvar_10),
         reinterpret_cast<void**>(&g_c136_setvar[10])},
        {&offsets::UNiagaraComponent_SetVariableFloat, reinterpret_cast<void*>(&detour_c136_set_float),
         reinterpret_cast<void**>(&g_c136_set_float)},
    };
    for (const HookSpec& h : hooks) {
        void* const target = code_address(base, nt, *h.sym);
        if (target == nullptr || !create_and_enable(target, h.detour, h.original, h.sym->name)) {
            return false;
        }
    }
    log::line("hooks: card 136 SetVariable count detours installed (%zu)", sizeof(hooks) / sizeof(hooks[0]));
    return true;
}

// The per-map totals (always logged, UBWCOOP_C136_LOG or not), then the live set and the counts start over: the old
// map's components go with its world. when: "map end" (LoadMap enter), "quit" (the quit command) or "map arm"
// (c98_arm_map: what came between the map end and the new map's arm, which is the whole LoadMap: its BeginPlay
// activates every placed Niagara component).
// Card 325: keep_state (the host's map arm) logs and resets the counts only. The live set, the effect journal, the
// asset and name tables, the peers, the queue and the pending row were built by this map's LoadMap and are what the
// join replay sends; they start over at "map end" and "quit" only.
void c136_report(const char* when, bool keep_state) {
    const C136Counts& k = g_c136.counts;
    uint32_t total = 0;
    char per_point[640] = "";
    size_t used = 0;
    for (int32_t i = 0; i < static_cast<int32_t>(C136Point::Count); ++i) {
        total += k.events[i];
        if (k.events[i] == 0 || used >= sizeof(per_point)) {
            continue;
        }
        const int n = snprintf(per_point + used, sizeof(per_point) - used, " %s %u", kC136Points[i].name,
                               k.events[i]);
        used += n > 0 ? static_cast<size_t>(n) : 0u;
    }
    if (!g_client_session) {
        log::line("card136: host fx map totals (%s): %u events (%s ), %u skips, live peak %d of %d, %u not remembered (set "
                  "full), %u found dead, net mode %u, %u standalone calls skipped", when, total, per_point, k.skips,
                  g_c136.live.peak, kC136Live, k.full, k.dropped, g_c136.net_mode, g_c136.standalone_skips);
        c518_blocked_lines(when);
    }
    if (g_client_session) {
        log::line("card136: client fx block map totals (%s): %u outermost calls blocked, %u kept, %u effect calls skipped; "
                  "engine cue copies %u dropped, %u kept (own)",
                  when, k.blocked_outer, k.kept_outer, k.blocked_calls, k.cue_dropped, k.cue_kept);
        c518_blocked_lines(when);
    }
    c136_setvar_dump();
    c409_report(when);
    c136_net_arm_map(when, keep_state);
    g_c136.counts = C136Counts{};
    g_c136.net_mode = kC136ModeUnknown;
    g_c136.standalone_skips = 0;
    if (keep_state) {
        return;
    }
    g_c136.live.n = 0;
    g_c136.live.peak = 0;
}

// Card 325: the host reports only (the load's placed effects stay journalled for the join replay). Card 326: the
// registry's "effects" map end (c136_report("map end")) empties the live set on both sides, so the arm reports and
// resets the counts only, on both sides: a client row applied between the map end and the arm is the new world's.
void c136_arm_map() {
    c136_report("map arm", true);
}

int32_t c136_live() {
    return g_c136.live.n;
}

// Card 162: a remote player's sound with no attenuation (no override and no USoundBase::AttenuationSettings of its own,
// as the player's own MetaSounds MoritaMK1Fire_MS and HumanMovement*_MS are) plays 2D at full volume on the other
// side. Both sides give such a sound the attenuation its AI twin plays with: a "Fire" sound gets the AI rifle's
// ATT_WPN_MoritaMK1, every other one ATT_FLY_Trooper_Footsteps. The sound is a remote player's when its owner (or the
// owner's Owner) is a remote pawn, or, with no player owner, when the nearest player pawn to it is a remote one within
// kC162NearUu (a client world row carries no owner). Each fallback is found or loaded once, held by the game instance
// and reached through a weak pointer.
//   card162: <side> attenuation <ATT> loaded: bAttenuate <0|1> bSpatialize <0|1> FalloffDistance <uu>
//   card162: <side> t=<ms> remote sound <sound> from <owner> at (x, y, z), <R> uu from the remote pawn: no
//            attenuation, plays with <ATT>
namespace {

constexpr double kC162NearUu = 300.0;
constexpr uint32_t kC162Lines = 60;
constexpr uint32_t kC173Lines = 80;
struct C162Fallback {
    const char* path;
    C31Weak weak;
    bool failed;
};
struct C162State {
    C162Fallback fallback[2]; // 0: "Fire" sounds, 1: the rest
    int32_t sound_atten_at;   // USoundBase::AttenuationSettings: -1 unresolved, -2 absent
    uint32_t lines;
    uint32_t asset_lines;
    uint32_t own_lines; // card 173
    uint32_t gi_lines;  // card 314: fallback calls without a game instance (never latched)
};
C162State g_c162 = {{{"/Game/Audio/Attenuation/ATT_WPN_MoritaMK1.ATT_WPN_MoritaMK1", C31Weak{}, false},
                     {"/Game/Audio/Attenuation/ATT_FLY_Trooper_Footsteps.ATT_FLY_Trooper_Footsteps", C31Weak{}, false}},
                    -1, 0, 0, 0, 0};

const char* c162_side() {
    return g_client_session ? "client" : "host";
}

// The sound's own attenuation: its AttenuationSettings, or (a USoundCue) its bOverrideAttenuation. True when it has one.
bool c162_own_atten(const void* sound) {
    const void* const cls = read_ptr(sound, kObjectClass);
    if (g_c162.sound_atten_at == -1) {
        const uint8_t* const prop = prop_find_prop(cls, "AttenuationSettings");
        g_c162.sound_atten_at = prop != nullptr ? read_i32(prop, kPropertyOffset) : -2;
        log::line("card162: %s USoundBase AttenuationSettings %s +0x%X", c162_side(),
                  prop != nullptr ? "at" : "absent (no fallback attenuation)", prop != nullptr ? g_c162.sound_atten_at : 0);
    }
    if (g_c162.sound_atten_at < 0) {
        return true; // unreadable: leave the sound as it is
    }
    if (read_ptr(sound, static_cast<size_t>(g_c162.sound_atten_at)) != nullptr) {
        return true;
    }
    const uint8_t* const over = prop_find_prop(cls, "bOverrideAttenuation");
    return over != nullptr && prop_bool_ok(over) && c29_flag(static_cast<const uint8_t*>(sound), over);
}

// Once per fallback asset: the settings it plays with, read by reflection from its FSoundAttenuationSettings.
void c162_log_asset(const void* att) {
    const uint8_t* const sp = prop_find_prop(read_ptr(att, kObjectClass), "Attenuation");
    const void* const st = sp != nullptr ? read_ptr(sp, kStructPropStruct) : nullptr;
    if (st == nullptr) {
        log::line("card162: %s attenuation %s loaded; its Attenuation struct is not readable", c162_side(),
                  object_name(att).text);
        return;
    }
    const uint8_t* const base = static_cast<const uint8_t*>(att) + read_i32(sp, kPropertyOffset);
    const uint8_t* const on = prop_find_prop(st, "bAttenuate");
    const uint8_t* const spat = prop_find_prop(st, "bSpatialize");
    const uint8_t* const fall = prop_find_prop(st, "FalloffDistance");
    log::line("card162: %s attenuation %s loaded: bAttenuate %d bSpatialize %d FalloffDistance %.0f", c162_side(),
              object_name(att).text, on != nullptr && prop_bool_ok(on) ? (c29_flag(base, on) ? 1 : 0) : -1,
              spat != nullptr && prop_bool_ok(spat) ? (c29_flag(base, spat) ? 1 : 0) : -1,
              fall != nullptr ? static_cast<double>(read_f32(base, static_cast<size_t>(read_i32(fall, kPropertyOffset))))
                              : -1.0);
}

// The fallback for `sound`, found or loaded on first use and held by the game instance of `context` (any object in
// a world: a PC, the world, an audio component). Card 314: only a missing class or asset latches the fallback off
// for the process; a missing game instance (a call without a world context) fails this call alone, with a WARN row.
void* c162_fallback(const void* context, const void* sound) {
    C162Fallback& f = g_c162.fallback[strstr(object_name(sound).text, "Fire") != nullptr ? 0 : 1];
    void* const held = c31_weak_get(f.weak);
    if (held != nullptr || f.failed) {
        return held;
    }
    static void* const cls = find_class(L"/Script/Engine.SoundAttenuation");
    void* const att = cls != nullptr ? c74_find_asset(f.path, cls) : nullptr;
    void* const gi = att != nullptr && context != nullptr && g_get_st_game_instance != nullptr
                         ? g_get_st_game_instance(context) : nullptr;
    if (att == nullptr || gi == nullptr || g_gi_register_ref == nullptr) {
        f.failed = att == nullptr || g_gi_register_ref == nullptr; // card 314: a missing game instance never latches
        if (f.failed || note_event_cap(g_c162.gi_lines, 8, "card162 game instance missing lines")) {
            log::line("WARN card162: %s fallback attenuation %s failed (class %s, asset %s, game instance %s, context "
                      "%s); %s", c162_side(), f.path, cls != nullptr ? "found" : "missing",
                      att != nullptr ? "found" : "missing", gi != nullptr ? "found" : "missing",
                      context != nullptr ? object_name(context).text : "null",
                      f.failed ? "its sounds stay unattenuated" : "this sound plays unattenuated, the next one retries");
        }
        return nullptr;
    }
    g_gi_register_ref(gi, att);
    c31_weak_set(f.weak, att);
    if (note_event_cap(g_c162.asset_lines, 8, "card162 attenuation asset lines")) {
        c162_log_asset(att);
    }
    return att;
}

// Card 173: bSpatialize of an attenuation asset's FSoundAttenuationSettings, -1 when unreadable. The properties are
// found once (every asset is a USoundAttenuation).
int c173_spatialize(const void* att) {
    static const uint8_t* settings = nullptr;
    static const uint8_t* spat = nullptr;
    if (settings == nullptr) {
        settings = prop_find_prop(read_ptr(att, kObjectClass), "Attenuation");
        const void* const st = settings != nullptr ? read_ptr(settings, kStructPropStruct) : nullptr;
        spat = st != nullptr ? prop_find_prop(st, "bSpatialize") : nullptr;
    }
    if (spat == nullptr || !prop_bool_ok(spat)) {
        return -1;
    }
    return c29_flag(static_cast<const uint8_t*>(att) + read_i32(settings, kPropertyOffset), spat) ? 1 : 0;
}

// Card 173: a remote player's sound with an attenuation of its own (the footsteps' ATT_FLY_Player). It keeps it when
// that spatializes; one that does not gets the card 162 fallback. A line per sound per second, capped:
//   card173: <side> t=<ms> remote sound <sound> from <owner> at (x, y, z), <L> uu from the listener, <R> uu from the
//            remote pawn: own attenuation <ATT> bSpatialize <-1|0|1>, plays with <ATT|its own>
void* c173_own_atten(const void* context, const void* sound, const void* owner, const Vec3& at, double d) {
    const void* const own = g_c162.sound_atten_at >= 0 ? read_ptr(sound, static_cast<size_t>(g_c162.sound_atten_at)) : nullptr;
    const int spatial = own != nullptr ? c173_spatialize(own) : -1;
    void* const att = spatial == 0 ? c162_fallback(context, sound) : nullptr;
    const ULONGLONG now = GetTickCount64();
    if (pl_once(reinterpret_cast<uintptr_t>(sound) ^ 0x173u, now, 1000) &&
        note_event_cap(g_c162.own_lines, kC173Lines, "card173 own attenuation lines")) {
        log::line("card173: %s t=%llu remote sound %s from %s at (%.0f, %.0f, %.0f), %.0f uu from the listener, %.0f uu "
                  "from the remote pawn: own attenuation %s bSpatialize %d, plays with %s", c162_side(),
                  static_cast<unsigned long long>(now), object_name(sound).text,
                  owner != nullptr ? object_name(owner).text : "no owner", at.x, at.y, at.z, pl_listener_uu(at), d,
                  own != nullptr ? object_name(own).text : "an override (no asset)", spatial,
                  att != nullptr ? object_name(att).text : "its own");
    }
    return att;
}

} // namespace

// Card 304: the receiver used to decide "a remote player's sound" only from the playlog's remote list
// (pl_remote_source), which is empty when no remote pawn was seen in the last kPlRemoteMs = 3000 ms; a far host's
// rifle then played with the sound's own first-person ATT_WPN_Player_Fire at near full level 500 m away. The
// sender now marks its own player's sounds (kC136FlagPlayerSource), and an owner that is another machine's player
// pawn (by class and role) counts without the list.
struct C304Tally {
    uint32_t rows;        // player-source audio rows received this map
    uint32_t unresolved;  // of them, a guid or placed key whose owner is not found here (a world key names none)
    uint32_t own_spatial; // of them, played with the sound's own attenuation, which spatializes (correct)
    uint32_t own_used;    // of them, played with an own attenuation that does not spatialize, no fallback (the defect)
    uint32_t warn_lines;
};
constexpr uint32_t kC304WarnLines = 32;
static C304Tally g_c304{};

const void* c304_player_pawn(const void* owner) {
    if (owner == nullptr || g_player_character_class == nullptr) {
        return nullptr;
    }
    const void* const cls = g_player_character_class();
    if (is_a(owner, cls)) {
        return owner;
    }
    // b9 (senior review of the b8 trace): only a weapon carries its pawn's player flag. weapon_pawn alone falls back
    // to any actor's Owner, which made a grenade or placed turret one Owner hop from the pawn a "player source".
    const void* const weapon_cls = g_c50_weapon_class != nullptr ? g_c50_weapon_class() : nullptr;
    if (weapon_cls == nullptr || !is_a(owner, weapon_cls)) {
        return nullptr;
    }
    const void* const up = weapon_pawn(owner);
    return up != nullptr && is_a(up, cls) ? up : nullptr;
}

// The owner is another machine's player: a server copy on the host, a simulated proxy on the client.
bool c304_other_player(const void* owner) {
    const void* const pawn = c304_player_pawn(owner);
    const uint32_t side = pawn != nullptr ? sprint_side(pawn) : kSideHostOwn;
    return side == kSideServerCopy || side == kSideOther;
}

// bSpatialize of the sound's own attenuation asset (card 173), -1 when it has none or it is unreadable.
int c304_own_spatial(const void* sound) {
    const void* const own =
        g_c162.sound_atten_at >= 0 ? read_ptr(sound, static_cast<size_t>(g_c162.sound_atten_at)) : nullptr;
    return own != nullptr ? c173_spatialize(own) : -1;
}

// key is the row's key kind ("guid", "placed" or "world"); a world key names no owner, so it is never unresolved.
// The WARN is only an own attenuation that does not spatialize with no fallback: the far first-person sound.
//   [WARN ]card304: client audio row <sound> key <kind> owner <resolved|unresolved|none>, played with <ATT|its own
//                   attenuation> (bSpatialize <-1|0|1>) at <uu> uu
void c304_row(const void* sound, bool player_source, const char* key, bool owner_resolved, const void* atten,
              const Vec3& at) {
    if (!player_source) {
        return;
    }
    const bool keyed = strcmp(key, "world") != 0;
    const bool unresolved = keyed && !owner_resolved;
    const int spatial = atten == nullptr ? c304_own_spatial(sound) : -1;
    const bool own_bad = atten == nullptr && spatial != 1;
    ++g_c304.rows;
    g_c304.unresolved += unresolved ? 1u : 0u;
    g_c304.own_spatial += atten == nullptr && spatial == 1 ? 1u : 0u;
    g_c304.own_used += own_bad ? 1u : 0u;
    if ((!unresolved && !own_bad) || !note_event_cap(g_c304.warn_lines, kC304WarnLines, "card304 row lines")) {
        return;
    }
    log::line("%scard304: client audio row %s key %s owner %s, played with %s (bSpatialize %d) at %.0f uu",
              own_bad ? "WARN " : "", object_name(sound).text, key,
              unresolved ? "unresolved" : (keyed ? "resolved" : "none"),
              atten != nullptr ? object_name(atten).text : "its own attenuation", spatial, pl_listener_uu(at));
}

void c304_map_end() {
    if (g_c304.rows > 0) {
        log::line("card304: audio rows %u, owner unresolved %u (guid or placed keys), own attenuation spatialized %u, "
                  "first-person attenuation used %u", g_c304.rows, g_c304.unresolved, g_c304.own_spatial,
                  g_c304.own_used);
    }
    g_c304 = C304Tally{};
}

void* c162_remote_atten(const void* context, const void* sound, const void* atten, const void* owner, const Vec3& at,
                        bool player_source) {
    double d = -1.0;
    if (atten != nullptr || sound == nullptr) {
        return const_cast<void*>(atten);
    }
    if (!player_source && !c304_other_player(owner) && !pl_remote_source(owner, at, kC162NearUu, d)) {
        return nullptr;
    }
    if (c162_own_atten(sound)) {
        return c173_own_atten(context, sound, owner, at, d);
    }
    void* const att = c162_fallback(context, sound);
    if (att != nullptr && note_event_cap(g_c162.lines, kC162Lines, "card162 remote sound lines")) {
        log::line("card162: %s t=%llu remote sound %s from %s at (%.0f, %.0f, %.0f), %.0f uu from the remote pawn: no "
                  "attenuation, plays with %s", c162_side(), static_cast<unsigned long long>(GetTickCount64()),
                  object_name(sound).text, owner != nullptr ? object_name(owner).text : "no owner", at.x, at.y, at.z,
                  d, object_name(att).text);
    }
    return att;
}

// The detours' form: the call's owner as card 136 names it; an attached sound's place is its parent component's.
// Card 314: SpawnSoundAttached has no world context (c.context null); its attach component is in the world and holds
// the fallback's game instance, else the owner.
void* c162_call_atten(const C136Call& c) {
    const Vec3 at = c.point == C136Point::SoundAttached && c.attach != nullptr ? read_vec3(c.attach, kComponentTranslation)
                                                                                : c.at;
    const void* const owner = c136_owner(c);
    const void* const context = c.context != nullptr ? c.context : c.attach != nullptr ? c.attach : owner;
    return c162_remote_atten(context, c.asset, c.atten, owner, at, false);
}

// Card 314: every audio component play of another machine's player (detour_pl_play_internal, both sides, before the
// original). The component's settings to apply (GetAttenuationSettingsToApply) decide: none is a 2D play (card 176
// mutes it on the host); bSpatialize 1 plays as it is; bSpatialize 0 (ATT_WPN_Player_Fire: the player's own gun)
// gets the card 162 fallback as the component's AttenuationSettings, and the getter is asked again to prove it took.
// A play with no settings (2D: the other player's own-ear sounds) is muted on both sides by VolumeMultiplier at
// `volume_at` unless card 176 (host, server copies only) already muted it; PlayInternal reads that field.
constexpr uint32_t kC314Sounds = 48; // a row per sound per map
struct C314Tally {
    uint32_t plays, re_attenuated, muted, unspatialized;
    const void* seen[kC314Sounds];
    uint32_t seen_n;
    uint32_t warn_lines;
    bool full_logged; // the "sound table full" row of this map is written
};
static C314Tally g_c314{};

// Card 314 review: a muted component's VolumeMultiplier stays 0 after its play, and the same component can later play a
// sound the local player should hear (a 3D play, or another machine's player no longer the owner). Each mute keeps the
// value it replaced, by weak pointer; c314_restore_volume puts it back before the next play's verdict, which mutes again
// only when that play is 2D. Not reset per map: an entry whose component is gone fails its weak test and is pruned.
constexpr int32_t kC314MutedMax = 64;
struct C314Muted {
    C31Weak weak;
    void* comp; // compared before the weak test; never dereferenced unless the weak test passes
    float volume;
};
static C314Muted g_c314_muted[kC314MutedMax];
static int32_t g_c314_muted_n = 0;
static uint32_t g_c314_muted_full = 0; // mutes with no free entry (their component stays at 0 after the play)

// Drops the entries whose component is gone.
static void c314_muted_prune() {
    for (int32_t i = g_c314_muted_n - 1; i >= 0; --i) {
        if (c31_weak_get(g_c314_muted[i].weak) != g_c314_muted[i].comp) {
            g_c314_muted[i] = g_c314_muted[--g_c314_muted_n];
        }
    }
}

// bSpatialize of an FSoundAttenuationSettings, -1 unreadable. The property is found once from USoundAttenuation.
static int c314_settings_spatial(const void* settings) {
    static const uint8_t* spat = nullptr;
    if (spat == nullptr) {
        static void* const cls = find_class(L"/Script/Engine.SoundAttenuation");
        const uint8_t* const prop = cls != nullptr ? prop_find_prop(cls, "Attenuation") : nullptr;
        const void* const st = prop != nullptr ? read_ptr(prop, kStructPropStruct) : nullptr;
        spat = st != nullptr ? prop_find_prop(st, "bSpatialize") : nullptr;
    }
    return spat != nullptr && prop_bool_ok(spat) ? (c29_flag(static_cast<const uint8_t*>(settings), spat) ? 1 : 0) : -1;
}

static bool c314_first(const void* sound) {
    for (uint32_t i = 0; i < g_c314.seen_n; ++i) {
        if (g_c314.seen[i] == sound) {
            return false;
        }
    }
    if (g_c314.seen_n < kC314Sounds) {
        g_c314.seen[g_c314.seen_n++] = sound;
        return true;
    }
    if (!g_c314.full_logged) {
        g_c314.full_logged = true;
        log::line("card314: %s sound table full (%u sounds): later new sounds get no row this map", c162_side(),
                  kC314Sounds);
    }
    return false;
}

void c314_mute(void* comp, int32_t volume_at) {
    if (comp == nullptr || volume_at < 0) {
        return;
    }
    auto* const field = reinterpret_cast<float*>(static_cast<uint8_t*>(comp) + volume_at);
    bool kept = false;
    for (int32_t i = 0; i < g_c314_muted_n && !kept; ++i) {
        kept = g_c314_muted[i].comp == comp && c31_weak_get(g_c314_muted[i].weak) == comp; // muted again: keep the first
    }
    if (!kept && g_c314_muted_n == kC314MutedMax) {
        c314_muted_prune();
    }
    if (!kept && g_c314_muted_n < kC314MutedMax) {
        C314Muted& m = g_c314_muted[g_c314_muted_n++];
        c31_weak_set(m.weak, comp);
        m.comp = comp;
        m.volume = *field;
    } else if (!kept && g_c314_muted_full++ == 0) {
        log::line("WARN card314: %s muted component table full (%d): a muted component stays at VolumeMultiplier 0",
                  c162_side(), kC314MutedMax);
    }
    *field = 0.0f;
}

void c314_restore_volume(void* comp, int32_t volume_at) {
    if (comp == nullptr || volume_at < 0) {
        return;
    }
    for (int32_t i = 0; i < g_c314_muted_n; ++i) {
        if (g_c314_muted[i].comp != comp) {
            continue;
        }
        if (c31_weak_get(g_c314_muted[i].weak) == comp) {
            *reinterpret_cast<float*>(static_cast<uint8_t*>(comp) + volume_at) = g_c314_muted[i].volume;
        }
        g_c314_muted[i] = g_c314_muted[--g_c314_muted_n];
        return;
    }
}

void c314_component_play(void* comp, const void* owner, const void* sound, int32_t atten_at, int32_t volume_at,
                         bool muted) {
    if (comp == nullptr || sound == nullptr || !c304_other_player(owner)) {
        return;
    }
    ++g_c314.plays;
    const void* const settings = c176_settings_to_apply(comp);
    if (settings == nullptr && !muted && volume_at >= 0) {
        c314_mute(comp, volume_at);
        muted = true;
    }
    const int before = settings != nullptr ? c314_settings_spatial(settings) : -1;
    void* applied = nullptr;
    if (settings != nullptr && before == 0 && atten_at >= 0) {
        applied = c162_fallback(comp, sound);
        if (applied != nullptr) {
            *reinterpret_cast<void**>(static_cast<uint8_t*>(comp) + atten_at) = applied;
        }
    }
    const void* const now = c176_settings_to_apply(comp);
    const int after = now != nullptr ? c314_settings_spatial(now) : -1;
    const bool bad = !muted && after != 1;
    g_c314.re_attenuated += applied != nullptr && after == 1 ? 1u : 0u;
    g_c314.muted += muted ? 1u : 0u;
    g_c314.unspatialized += bad ? 1u : 0u;
    const void* const set = atten_at >= 0 ? read_ptr(comp, static_cast<size_t>(atten_at)) : nullptr;
    if (!c314_first(sound) && !(bad && note_event_cap(g_c314.warn_lines, 16, "card314 unspatialized lines"))) {
        return;
    }
    log::line("%scard314: %s remote player sound %s from %s: applied %s bSpatialize %d, plays with %s%s",
              bad ? "WARN " : "", c162_side(), object_name(sound).text, object_name(owner).text,
              applied != nullptr ? object_name(applied).text : "none", before,
              muted ? "muted" : (set != nullptr ? object_name(set).text : "its own"),
              bad ? (after == 0 ? " (plays unspatialized)" : " (plays unspatialized: no settings)") : "");
}

void c314_map_end() {
    if (g_c314.plays > 0) {
        log::line("card314: %s remote player component sounds %u, re-attenuated %u, muted %u, unspatialized %u, muted "
                  "components held %d (table full %u)",
                  c162_side(), g_c314.plays, g_c314.re_attenuated, g_c314.muted, g_c314.unspatialized,
                  g_c314_muted_n, g_c314_muted_full);
    }
    g_c314 = C314Tally{};
}

} // namespace ubw::hooks
