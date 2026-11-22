// Card 099: "the player" on the host is the pawn's own controller.
// The game is single-player code: every "the player" helper (GetSTPlayerController, GetSTPlayerCharacter, GetSTHUD,
// StartCameraShake, PlayForceFeedbackEffect, SpawnSoundAttachedToPlayer, the Blueprint Get Player nodes) resolves
// UGameplayStatics::GetPlayerController(context, 0). Since card 024 the host runs a client's fire ability, animations
// and cues on that client's server copy pawn, so its first-person feedback (shake, rumble, sound attached to "the
// player", HUD effects) went to the host's controller. The gate: on the host, index 0 with a context that belongs to a
// server copy (the copy itself, or reached through the outer chain or the Owner/Instigator chain) returns the copy's
// own controller. The mod's own lookups go through g_get_player_controller, which the hook row turns into the
// trampoline, so they do not change. With UBWCOOP_C99_LOG=1 the sound, shake and score calls are logged as well.

#include "ubw/hooks_internal.h"

#include <intrin.h>
#include <stdio.h>

namespace ubw::hooks {

bool g_c99_log = false; // UBWCOOP_C99_LOG: card 099 measure hooks installed
StaticClassFn g_pawn_class = nullptr;
C102OverlapFn g_orig_c102_overlap = nullptr;
C99Sound2DFn g_orig_c99_sound_2d = nullptr;
C99SpawnSound2DFn g_orig_c99_spawn_sound_2d = nullptr;
C99AdSound2DFn g_orig_c99_ad_sound_2d = nullptr;
C99SoundFNameFn g_orig_c99_sound_fname = nullptr;
C99ShakeFn g_orig_c99_shake = nullptr;
C99ScoreTextFn g_orig_c99_score_text = nullptr;
C99ScoreFn g_orig_c99_score = nullptr;
C160MpcScalarFn g_orig_c160_mpc_scalar = nullptr;
ExecThunkFn g_orig_c160_exec_create = nullptr;
CreateWidgetInstanceFn g_orig_c160_widget_world = nullptr;
C160SettingsFn g_c160_settings = nullptr;
C160BoolGetFn g_c160_toggle_kbm = nullptr;
C160BoolGetFn g_c160_toggle_ctrl = nullptr;
C160BoolSetFn g_c160_set_toggle_kbm = nullptr;
C160SubsystemFn g_c160_input_subsystem = nullptr;
C160BoolGetFn g_c160_gamepad = nullptr;
ExecThunkFn g_orig_c160_exec_toggle_kbm = nullptr;
ExecThunkFn g_orig_c160_exec_toggle_ctrl = nullptr;
ExecThunkFn g_orig_c160_exec_gamepad = nullptr;

namespace {

constexpr int32_t kC99Hops = 8;            // context -> component/ability -> ASC/weapon -> pawn -> controller ...
constexpr uint32_t kC99Callers = 96;       // distinct remapped callers counted per map (c582b411a filled 48)
constexpr uint32_t kC99Lines = 300;        // card 099 log lines per map
constexpr uint32_t kC99ShakeLines = 200;   // card 454: camera shake rows per map, apart from kC99Lines
constexpr uint32_t kC99ScoreLines = 200;   // card 491: AddFedScore rows per map, apart from kC99Lines
constexpr uint32_t kC476FreedLines = 20;   // card 476: freed-object copy walk rows per map
constexpr ULONG kC476Frames = 16;          // stack frames searched for the first exe caller

struct C99Caller {
    unsigned long long rva;
    uint32_t count;
    uint8_t entry; // C102Entry: kC102None for a context that belongs to the copy itself
    char cls[kNameChars];
};
struct C99Callers {
    C99Caller row[kC99Callers];
    uint32_t used, dropped, total, lines;
    uint32_t shake_lines; // card 454: rows of detour_c99_shake
    uint32_t score_lines; // card 491: rows of detour_c99_score_text and detour_c99_score
    uint32_t hurt_lines;  // card 455: rows of c455_host_relay or c455_receive
    uint32_t freed_lines; // card 476: copy walks that met a freed object
};
C99Callers g_c99{};

// Card 160: a server copy's local-only effects (2D sounds, global material parameters, a widget with no owning
// player) are the client's own feedback; the host skips them. Counted per map by kind.
// Card 176: kC160Mute is a 2D component sound the host plays muted, not skipped (the component's life stays the game's).
enum C160Kind : uint8_t { kC160Sound, kC160Mpc, kC160Widget, kC160Mute, kC160Kinds };
constexpr const char* kC160KindText[kC160Kinds] = {"2D sound", "material parameter", "widget", "2D component sound"};
constexpr const char* kC160Verb[kC160Kinds] = {"skipped", "skipped", "skipped", "muted"};
// Lines per map by group: the skips share one cap; the mutes have their own, so a burst of clicks spends no skip line.
enum C160Group : uint8_t { kC160Skips, kC160Mutes, kC160Groups };
constexpr uint8_t kC160GroupOf[kC160Kinds] = {kC160Skips, kC160Skips, kC160Skips, kC160Mutes};
constexpr uint32_t kC160Cap[kC160Groups] = {60, 8};
constexpr const char* kC160CapText[kC160Groups] = {"card160 skips", "card176 mutes"};
// Writes the line's note for ctx; called only for a line that is written.
using C160NoteFn = void (*)(char* out, size_t n, const void* ctx);
struct C160Skips {
    uint32_t count[kC160Kinds]; // complete: every effect is counted, logged or not
    uint32_t lines[kC160Groups];
    const void* widget_copy; // the server copy whose Blueprint is inside execCreate, else null
};
C160Skips g_c160{};

// Card 176: the last muted bAutoDestroy components, by weak pointer. A live entry is one the game has not destroyed
// yet, so a bounded live count over a burst shows the components do not pile up on the weapon.
using C176AttenFn = const void* (*)(const void* comp); // UAudioComponent::GetAttenuationSettingsToApply
constexpr uint32_t kC176Ring = 16;
struct C176Mutes {
    C176AttenFn atten; // resolved at the first host play, then fixed
    C31Weak ring[kC176Ring];
    uint32_t next;
    bool resolve_failed;
};
C176Mutes g_c176{};

// Card 102: the acting server copy for the extent of a host entry point (overlap, damage, interaction). Innermost
// scope wins; each detour restores the value it saved, so the scope ends with the call.
constexpr const char* kC102EntryText[kC102Entries] = {"context", "overlap", "damage", "interaction", "enemy", "place"};
C102Scope g_c102{};

// Return addresses (inside the game's helpers) that must not take the acting scope: they are camera or render code
// that asks for "the player" for its own view, not a check on who acted.
constexpr unsigned long long kC102Excluded[] = {
    0x523A1ACull, // USTCommonUtilityLibrary::GetCenterScreenWorldPostionDirection
    0x52023FDull, // USTSpriteRenderComponent::GetLocalToWorldForProxy
    0x523DF5Dull, // the player's PlayerCameraManager (PC+0x358) for gore and camera effects (card 103)
};

bool c102_excluded(unsigned long long rva) {
    for (const unsigned long long e : kC102Excluded) {
        if (e == rva) {
            return true;
        }
    }
    return false;
}

bool c99_line_ok() {
    if (g_c99.lines >= kC99Lines) {
        return false;
    }
    ++g_c99.lines;
    return true;
}

// Card 476: a copy walk met an object that is no longer a live GUObjectArray entry (a client's pawn destroyed by its
// disconnect, still named by a grenade's Instigator). One capped row with the first exe frame on the stack.
void c476_freed_row(int32_t hop) {
    if (!note_event_cap(g_c99.freed_lines, kC476FreedLines, "card476 freed copy walks")) {
        return;
    }
    void* frames[kC476Frames] = {};
    const USHORT n = RtlCaptureStackBackTrace(1, kC476Frames, frames, nullptr);
    unsigned long long caller = 0;
    for (USHORT i = 0; i < n && caller == 0; ++i) {
        caller = exe_rva(frames[i]);
    }
    log::line("WARN card476: host t=%llu copy walk met a freed object at hop %d, caller exe+0x%llX",
              static_cast<unsigned long long>(GetTickCount64()), hop, caller);
}

// The server copy (a client's pawn or PlayerController on the host) that ctx belongs to, or nullptr. Actors follow
// Instigator, then Owner, then outer; other objects follow the outer chain. Card 476: every object is checked live in
// GUObjectArray before it is read; a freed one ends the walk with nullptr (the effect stays the host's).
// Card 572 step 5: an actor with no Owner ends the walk (`full` false). Its outer is its ULevel, then the UWorld and the
// package: no actor above, so the full walk (`full` true, the step 5 check) can only spend 3 hops to answer nullptr.
const void* c99_copy_walk(const void* ctx, bool full) {
    const void* const actor_class = g_actor_class();
    const void* o = ctx;
    for (int32_t hop = 0; hop < kC99Hops && o != nullptr; ++hop) {
        if (!object_listed(o)) {
            c476_freed_row(hop);
            return nullptr;
        }
        if (!is_a(o, actor_class)) {
            o = read_ptr(o, kObjectOuter);
            continue;
        }
        if (sprint_side(o) == kSideServerCopy) {
            return o;
        }
        const void* const instigator = read_ptr(o, kActorInstigator);
        if (instigator != nullptr && instigator != o && !object_listed(instigator)) {
            c476_freed_row(hop);
            return nullptr;
        }
        if (instigator != nullptr && instigator != o && sprint_side(instigator) == kSideServerCopy) {
            return instigator;
        }
        const void* const owner = read_ptr(o, kActorOwner);
        if (owner == nullptr && !full) {
            return nullptr;
        }
        o = owner != nullptr ? owner : read_ptr(o, kObjectOuter);
    }
    return nullptr;
}

const void* c99_copy_of(const void* ctx) {
    return c99_copy_walk(ctx, false);
}

// The copy's own controller: the copy if it is one, else a pawn's Controller if that is a PlayerController.
void* c99_copy_pc(const void* copy) {
    if (is_a(copy, g_player_controller_class())) {
        return const_cast<void*>(copy);
    }
    if (!is_a(copy, g_pawn_class())) {
        return nullptr;
    }
    void* const pc = read_ptr(copy, kPawnController);
    if (pc == nullptr) {
        return c586_holder_pc(copy); // card 586: an unpossessed trooper held by its player's seat or missile
    }
    return is_a(pc, g_player_controller_class()) ? pc : nullptr;
}

// The actor an object belongs to: itself when it is an actor, else the first actor on its outer chain (a component's
// outer is its owner), or null.
const void* c160_actor_of(const void* o) {
    const void* const actor_class = g_actor_class();
    for (int32_t hop = 0; hop < kC99Hops && o != nullptr; ++hop) {
        if (!object_listed(o)) {
            return nullptr; // card 476: a freed object on the chain
        }
        if (is_a(o, actor_class)) {
            return o;
        }
        o = read_ptr(o, kObjectOuter);
    }
    return nullptr;
}

// Card 302, host: the acting server copy of the card 102 scope, for an effect whose context and owner resolve to no
// copy. Only an effect of the scope's site (the overlapped pickup, the interaction target) is the copy's: an effect
// of any other object inside the scope (the level script completing an objective on the copy's overlap, a marker or
// a stinger meant for every player) stays the host's. `rva` is the effect call's return address (c102_excluded).
const void* c302_acting_for(const void* ctx, const void* owner, unsigned long long rva) {
    if (g_c102.acting == nullptr || g_c102.site == nullptr || c102_excluded(rva)) {
        return nullptr;
    }
    const bool site = (ctx != nullptr && c160_actor_of(ctx) == g_c102.site) ||
                      (owner != nullptr && c160_actor_of(owner) == g_c102.site);
    return site ? g_c102.acting : nullptr;
}

// Card 302: the scope's acting copy and entry, for the effect rows ("acting copy null (none)" outside a scope).
struct C302Acting {
    const char* name;
    const char* entry;
};
C302Acting c302_acting_text() {
    static ObjectName name; // game thread
    name = object_name(g_c102.acting);
    return C302Acting{g_c102.acting != nullptr ? name.text : "null",
                      g_c102.entry < kC102Entries ? kC102EntryText[g_c102.entry] : "?"};
}

// Host: true when ctx (or owner) belongs to a server copy, so the effect is that client's own; counted and logged.
// Card 302: when neither does, the acting copy of the card 102 scope (c302_acting_for) owns an effect of its site.
// `note_fn` writes a note appended to the line (card 176's mute details), only when the line is written.
bool c160_copy_effect(uint8_t kind, const void* ctx, const void* owner, const void* item, unsigned long long rva,
                      C160NoteFn note_fn = nullptr) {
    if (g_client_session) {
        return false;
    }
    const void* copy = ctx != nullptr ? c99_copy_of(ctx) : nullptr;
    copy = copy == nullptr && owner != nullptr ? c99_copy_of(owner) : copy;
    copy = copy == nullptr ? c302_acting_for(ctx, owner, rva) : copy;
    if (copy == nullptr) {
        return false;
    }
    ++g_c160.count[kind];
    const uint8_t group = kC160GroupOf[kind];
    if (note_event_cap(g_c160.lines[group], kC160Cap[group], kC160CapText[group])) {
        char note[128] = "";
        if (note_fn != nullptr) {
            note_fn(note, sizeof(note), ctx);
        }
        const C302Acting acting = c302_acting_text();
        log::line("card160: host t=%llu %s server copy %s %s: context %s (%s), copy %s, acting copy %s (%s)%s",
                  static_cast<unsigned long long>(GetTickCount64()), kC160Verb[kind], kC160KindText[kind],
                  object_name(item).text, object_name(ctx).text, class_name(ctx).text, object_name(copy).text,
                  acting.name, acting.entry, note);
    }
    return true;
}

// Card 176: GetAttenuationSettingsToApply, resolved once; null when the symbol does not resolve (logged once).
C176AttenFn c176_atten() {
    if (g_c176.atten == nullptr && !g_c176.resolve_failed) {
        uint8_t* const base = const_cast<uint8_t*>(ue::hooktable::g_exe_base);
        void* const at = base != nullptr ? ue::hooktable::code_address(base, ue::hooktable::nt_headers(base),
                                                                       offsets::UAudioComponent_GetAttenuationSettingsToApply)
                                         : nullptr;
        g_c176.atten = reinterpret_cast<C176AttenFn>(at);
        g_c176.resolve_failed = at == nullptr;
        if (at == nullptr) {
            log::line("WARN card176: GetAttenuationSettingsToApply did not resolve; server copy 2D component sounds "
                      "play unmuted");
        }
    }
    return g_c176.atten;
}

uint32_t c176_live() {
    uint32_t live = 0;
    for (const C31Weak& w : g_c176.ring) {
        live += c31_weak_get(w) != nullptr ? 1u : 0u;
    }
    return live;
}

constexpr const char* kC176AutoDestroy[] = {"bAutoDestroy", nullptr};

// The mute line's note: the component, its bAutoDestroy and how many earlier muted ones the game has not destroyed.
void c176_note(char* out, size_t n, const void* comp) {
    snprintf(out, n, "; component %s, bAutoDestroy %d, earlier muted auto-destroy components live %u of %u",
             object_name(comp).text, c29_bool(comp, kC176AutoDestroy), c176_live(),
             g_c176.next < kC176Ring ? g_c176.next : kC176Ring);
}

} // namespace

// Card 176, host: a server copy's 2D play of `comp` (no attenuation to apply) is muted before PlayInternal runs, by
// VolumeMultiplier at `volume_at`; PlayInternal reads that field when it builds the active sound (exe+0x3A424A3). The
// original always runs, so the component completes and a bAutoDestroy one is destroyed by the game. A 3D play is
// untouched. The 2D test (one native getter) runs before the copy walk, which is the dearer check; the bAutoDestroy
// lookup and the note run only for a copy's play, the note only for a written line. True when muted.
bool c176_mute_copy_2d(void* comp, const void* owner, const void* sound, int32_t volume_at) {
    if (g_client_session || comp == nullptr || volume_at < 0) {
        return false;
    }
    const C176AttenFn atten = c176_atten();
    if (atten == nullptr || atten(comp) != nullptr) {
        return false;
    }
    if (!c160_copy_effect(kC160Mute, comp, owner, sound, 0, c176_note)) {
        return false;
    }
    c314_mute(comp, volume_at);
    if (c29_bool(comp, kC176AutoDestroy) == 1) {
        c31_weak_set(g_c176.ring[g_c176.next % kC176Ring], comp);
        ++g_c176.next;
    }
    return true;
}

// Card 314: the component's FSoundAttenuationSettings to apply, null for none (a 2D play) or an unresolved getter.
const void* c176_settings_to_apply(const void* comp) {
    const C176AttenFn atten = c176_atten();
    return atten != nullptr && comp != nullptr ? atten(comp) : nullptr;
}

namespace {

void c99_count(const void* ctx, const void* copy, const void* pc, unsigned long long rva, uint8_t entry) {
    ++g_c99.total;
    const ObjectName cls_name = class_name(ctx); // card 130 item 13: a row per class, so a marker is not hidden
    const char* const cls = cls_name.text;
    for (uint32_t i = 0; i < g_c99.used; ++i) {
        if (g_c99.row[i].rva == rva && g_c99.row[i].entry == entry &&
            strncmp(g_c99.row[i].cls, cls, sizeof(g_c99.row[i].cls) - 1) == 0) {
            ++g_c99.row[i].count;
            return;
        }
    }
    if (g_c99.used == kC99Callers) {
        ++g_c99.dropped;
        return;
    }
    C99Caller& r = g_c99.row[g_c99.used++];
    r.rva = rva;
    r.count = 1;
    r.entry = entry;
    snprintf(r.cls, sizeof(r.cls), "%s", cls);
    if (c99_line_ok()) {
        log::line("card099: player 0 remapped (%s): caller exe+0x%llX, context %s (%s), copy %s, pc %p",
                  kC102EntryText[entry], rva, r.cls, object_name(ctx).text, object_name(copy).text, pc);
    }
}

} // namespace

namespace {

// Card 572 step 2 diag: ticks since *mark into *part, only for a sampled body (parts non-null).
void c572_stamp(C572Parts* parts, uint64_t C572Parts::*part, uint64_t* mark) {
    if (parts == nullptr) {
        return;
    }
    const uint64_t now = __rdtsc();
    parts->*part += now - *mark;
    *mark = now;
}

// Card 582: placed world actors (no Instigator, no Owner) whose Blueprints read player 0 for a test that is about any
// player: the initial trigger overlap (G10), the mortar zone's PlayerInside and target (G1), the pylon's line-of-sight
// target compare (G5). For these, "player 0" on the host is the player nearest the actor, so a client alone near the
// actor is the one tested. Classes that bind, mark or show something per player (EM marker waves, pickup tracker) are
// not here: one nearest player would take the host's share; they need a fan-out (card 582 G2, G4).
constexpr const char* kC582Classes[] = {"BP_MortarZone_C", "BP_KillableBuilding_ElectricPylon_C", "BP_EM_PrefabBase_C",
                                        "BP_DestructibleSpawner_Base_C", "BP_TentSpawner_HumanGameplay_C"};
constexpr uint32_t kC582Cache = 64; // distinct contexts' classes seen per map; a full cache tests by name each time

struct C582Seen {
    const void* cls[kC582Cache];
    bool place[kC582Cache];
    uint32_t used;
};
C582Seen g_c582{}; // game thread

// True when cls (or a super) is one of kC582Classes. The name walk runs once per class; repeats are a pointer scan.
bool c582_place_class(const void* cls) {
    for (uint32_t i = 0; i < g_c582.used; ++i) {
        if (g_c582.cls[i] == cls) {
            return g_c582.place[i];
        }
    }
    bool place = false;
    for (const char* name : kC582Classes) {
        place = place || c73_class_is(cls, name);
    }
    if (g_c582.used < kC582Cache) {
        g_c582.cls[g_c582.used] = cls;
        g_c582.place[g_c582.used] = place;
        ++g_c582.used;
    }
    return place;
}

// The controller of the player nearest ctx when ctx is a card 582 class and that player is not the host, else null.
void* c582_place_pc(const void* ctx) {
    if (g_client_session || ctx == nullptr || !c582_place_class(read_ptr(ctx, kObjectClass))) {
        return nullptr;
    }
    const C43Roster r = c43_roster(ctx);
    const Vec3 at = actor_location(ctx);
    int32_t best = -1;
    double best_d = 0.0;
    for (int32_t s = 0; s < kC43Max; ++s) {
        if (r.p[s].pawn == nullptr || r.p[s].pc == nullptr) {
            continue;
        }
        const double d = vec_dist(actor_location(r.p[s].pawn), at);
        if (best < 0 || d < best_d) {
            best = s;
            best_d = d;
        }
    }
    return best > 0 ? r.p[best].pc : nullptr; // slot 0 is the host: the original result stands
}

// Card 582 G2: the Blueprint object of the innermost ModifyTargetThreat exec thunk, and the map's fan-out tally.
// A (component, client pawn) pair is mirrored once: on a host pawn change the Blueprint adds +delta to the new pawn
// and -delta to the old one, which is no longer slot 0, so a second mirror would stack +delta on the client's pawn.
// A client's new pawn is a new pair and gets the next marker's threat.
constexpr uint32_t kC582Pairs = 256; // marked enemies x client pawns per map
struct C582Pair {
    const void* component;
    const void* pawn;
};
struct C582Threat {
    const void* caller;
    uint32_t seen;     // EM calls for the host's pawn
    uint32_t mirrored; // repeats for client pawns
    uint32_t repeats;  // pairs already mirrored, skipped
    uint32_t full;     // pairs not mirrored: table full
    uint32_t used;
    C582Pair pair[kC582Pairs];
};
C582Threat g_c582_threat{}; // game thread

constexpr const char* kC582ThreatClasses[] = {"BP_EM_Dropship_C", "BP_EM_1FactionSetWaves_C"};

// True, and the pair noted, when (component, pawn) has not been mirrored this map. A full table mirrors nothing more.
bool c582_pair_new(const void* component, const void* pawn) {
    C582Threat& t = g_c582_threat;
    for (uint32_t i = 0; i < t.used; ++i) {
        if (t.pair[i].component == component && t.pair[i].pawn == pawn) {
            ++t.repeats;
            return false;
        }
    }
    if (t.used == kC582Pairs) {
        ++t.full;
        return false;
    }
    t.pair[t.used++] = C582Pair{component, pawn};
    return true;
}

bool c582_threat_class(const void* obj) {
    const void* const cls = obj != nullptr ? read_ptr(obj, kObjectClass) : nullptr;
    bool hit = false;
    for (const char* name : kC582ThreatClasses) {
        hit = hit || (cls != nullptr && c73_class_is(cls, name));
    }
    return hit;
}

// The work behind a host index 0 answer: the server copy ctx belongs to, else the acting scope's, else (game thread
// only) the enemy's player. own null: the original result stands.
struct C99Pick {
    const void* copy;
    void* own;
    uint8_t entry;
};

C99Pick c99_pick(const void* ctx, unsigned long long rva, bool excluded, bool game, C572Parts* parts, uint64_t* mark) {
    C99Pick p{c99_copy_of(ctx), nullptr, kC102None};
    c572_stamp(parts, &C572Parts::copy, mark);
    if (parts != nullptr && game) { // card 572 step 5 check: a sampled body also runs the full walk, untimed
        const uint64_t c0 = __rdtsc();
        g_c572_memo.walk_checked += 1;
        g_c572_memo.walk_differ += c99_copy_walk(ctx, true) != p.copy ? 1 : 0;
        *mark = __rdtsc();
        parts->check = *mark - c0;
    }
    if (p.copy == nullptr && g_c102.acting != nullptr && !excluded) {
        p.copy = g_c102.acting;
        p.entry = g_c102.entry;
    }
    p.own = p.copy != nullptr ? c99_copy_pc(p.copy) : nullptr;
    if (p.copy == nullptr && !excluded && game) {
        p.own = c103_enemy_pc(ctx, rva); // card 103: the enemy's player; null for the host's own
        p.copy = p.own != nullptr ? read_ptr(p.own, kControllerPawn) : nullptr;
        p.entry = p.own != nullptr ? static_cast<uint8_t>(kC102Enemy) : p.entry;
    }
    if (p.own == nullptr && !excluded && game) {
        p.own = c582_place_pc(ctx); // card 582: a placed world actor that asks for "the" player
        p.copy = p.own != nullptr ? read_ptr(p.own, kControllerPawn) : nullptr;
        p.entry = p.own != nullptr ? static_cast<uint8_t>(kC102Place) : p.entry;
    }
    c572_stamp(parts, &C572Parts::enemy, mark);
    return p;
}

// Card 572 step 4 (c540 pattern): the pick for a context already answered this frame on the game thread is the stored
// one. c573rb401a (b401): the copy walk and the enemy remap were 182-218 and 220-271 ns of every body, about 34-67 ms
// a window. Only calls with no acting scope and a caller that is not excluded use the memo, so a stored pick depends on
// ctx alone; the frame key (c99_frame_tick) drops every row each frame. One repeat in kC99MemoVerifyEvery runs the
// body anyway and counts a different answer (the memo would have been wrong).
constexpr uint32_t kC99MemoSlots = 256;       // power of two, direct mapped, 32 B rows (8 KB)
constexpr uint32_t kC99MemoVerifyEvery = 64;  // power of two

struct C99Memo {
    const void* ctx;
    void* own;
    const void* copy;
    uint32_t frame;
    uint8_t entry;
};
C99Memo g_c99_memo[kC99MemoSlots] = {};
uint32_t g_c99_frame = 1; // game thread
uint32_t g_c99_verify_tick = 0;

constexpr uint32_t kC99MemoWarnLines = 8; // since start
uint32_t g_c99_memo_warn_lines = 0;     // game thread

C99Memo& c99_memo_slot(const void* ctx) {
    const uint64_t key = reinterpret_cast<uintptr_t>(ctx) >> 4;
    return g_c99_memo[static_cast<uint32_t>(key * 0x9E3779B97F4A7C15ULL >> 40) & (kC99MemoSlots - 1)];
}

// Card 572 step 7: a verified repeat whose body answer differs from the stored one (the memo would have served a wrong
// player controller). The row names the case: context class, caller, stored and fresh own and copy.
void c99_memo_warn(const void* ctx, unsigned long long rva, const C99Memo& stored, const C99Pick& fresh) {
    if (g_c99_memo_warn_lines >= kC99MemoWarnLines) {
        return;
    }
    ++g_c99_memo_warn_lines;
    log::line("WARN card572: memo differs in frame %u: context %s %p, caller exe+0x%llX, stored own %p copy %p entry %u, "
              "fresh own %p copy %p entry %u", g_c99_frame, class_name(ctx).text, ctx, rva, stored.own, stored.copy,
              static_cast<unsigned>(stored.entry), fresh.own, fresh.copy, static_cast<unsigned>(fresh.entry));
}

C99Pick c99_memo_pick(const void* ctx, unsigned long long rva, C572Parts* parts, uint64_t* mark) {
    C99Memo& m = c99_memo_slot(ctx);
    const bool repeat = m.ctx == ctx && m.frame == g_c99_frame;
    if (repeat && (++g_c99_verify_tick & (kC99MemoVerifyEvery - 1)) != 0) {
        ++g_c572_memo.served;
        c572_stamp(parts, &C572Parts::enemy, mark);
        if (parts != nullptr) {
            parts->served = 1;
        }
        return C99Pick{m.copy, m.own, m.entry};
    }
    const C99Pick p = c99_pick(ctx, rva, false, true, parts, mark);
    g_c572_memo.verified += repeat ? 1 : 0;
    const bool differ = repeat && (p.own != m.own || p.copy != m.copy);
    g_c572_memo.differ += differ ? 1 : 0;
    if (differ) {
        c99_memo_warn(ctx, rva, m, p);
    }
    m = C99Memo{ctx, p.own, p.copy, g_c99_frame, p.entry};
    return p;
}

// Card 582 step 1 (UBWCOOP_C99_LOG, host): a player-0 read that kept the host's answer while a client is connected,
// one row per (context class, caller). Rows give the G6 to G9 verdicts: a context that should act for a client but
// reads the host. Keys are recorded only while a client is connected, so a key first seen alone logs later.
constexpr uint32_t kC582Kept = 64; // distinct keys per map
struct C582KeptKey {
    const void* cls;
    unsigned long long rva;
};
struct C582Kept {
    C582KeptKey key[kC582Kept];
    uint32_t used, full;
};
C582Kept g_c582_kept{};

void c582_kept_row(const void* ctx, unsigned long long rva) {
    const void* const cls = ctx != nullptr ? read_ptr(ctx, kObjectClass) : nullptr;
    for (uint32_t i = 0; cls != nullptr && i < g_c582_kept.used; ++i) {
        if (g_c582_kept.key[i].cls == cls && g_c582_kept.key[i].rva == rva) {
            return;
        }
    }
    if (cls == nullptr || c43_roster(ctx).players < 2) {
        return;
    }
    if (g_c582_kept.used >= kC582Kept) {
        ++g_c582_kept.full;
        return;
    }
    g_c582_kept.key[g_c582_kept.used++] = C582KeptKey{cls, rva};
    const bool actor = g_actor_class != nullptr && is_a(ctx, g_actor_class());
    const void* const owner = actor ? read_ptr(ctx, kActorOwner) : nullptr;
    const void* const instigator = actor ? read_ptr(ctx, kActorInstigator) : nullptr;
    log::line("card582: player 0 not remapped: context %s, caller exe+0x%llX, owner %s, instigator %s",
              class_name(ctx).text, rva, owner != nullptr ? class_name(owner).text : (actor ? "none" : "not an actor"),
              instigator != nullptr ? class_name(instigator).text : (actor ? "none" : "not an actor"));
}

// The host index 0 answer: the server copy's, the acting scope's or the enemy's player controller, else pc.
// `parts`: null, or a sampled body's part timings (card 572).
void* c99_answer(const void* ctx, void* pc, unsigned long long rva, C572Parts* parts) {
    uint64_t mark = parts != nullptr ? __rdtsc() : 0;
    const bool excluded = c102_excluded(rva);
    const bool game = g_is_in_game_thread != nullptr && g_is_in_game_thread();
    const C99Pick p = game && g_c572_memo_on && g_c102.acting == nullptr && !excluded
                          ? c99_memo_pick(ctx, rva, parts, &mark)
                          : c99_pick(ctx, rva, excluded, game, parts, &mark);
    if (p.own == nullptr || p.own == pc) {
        if (g_c99_log && game && !g_client_session) {
            c582_kept_row(ctx, rva); // card 582 step 1, diagnostic only
        }
        return pc;
    }
    if (game) {
        c99_count(ctx, p.copy, p.own, rva, p.entry); // the tally is game-thread state; render-thread callers skip it
    }
    c572_stamp(parts, &C572Parts::count, &mark);
    return p.own;
}

uint32_t g_c572_tick = 0; // card 572 sampler; a race on it only moves which call is sampled

} // namespace

bool g_c572_memo_on = true; // written in ubw::init before any hook (UBWCOOP_C572_MEMO_OFF=1 turns it off)
C572Memo g_c572_memo = {};

void c99_frame_tick() {
    ++g_c99_frame;
}

void c572_memo_forget(const void* actor) {
    if (actor == nullptr) {
        return;
    }
    C99Memo& m = c99_memo_slot(actor);
    m.frame = m.ctx == actor ? 0 : m.frame; // a later actor at this address starts with no stored pick
    const bool player_kind = (g_pawn_class != nullptr && is_a(actor, g_pawn_class())) ||
                             (g_player_controller_class != nullptr && is_a(actor, g_player_controller_class()));
    if (!player_kind) {
        return;
    }
    const C43Roster r = c43_roster(actor);
    bool held = false;
    for (int32_t s = 0; s < kC43Max; ++s) {
        held = held || r.p[s].pc == actor || r.p[s].pawn == actor;
    }
    if (held) {
        ++g_c99_frame; // every stored pick may name this controller or pawn: drop them all
        ++g_c572_memo.forget_bumps;
    }
}

PossessedByFn g_orig_c572_pawn_possessed = nullptr;
PawnFn g_orig_c572_pawn_unpossessed = nullptr;

void c572_input_bump(C572Input source) {
    ++g_c99_frame; // a stored pick may read the input that changed: drop them all
    ++g_c572_memo.input_bumps[source < kC572Inputs ? source : kC572InputPrimary];
}

namespace {

// Card 572 step 8: what a player pawn's (Un)PossessedBy may change that a memo pick reads (the server copy test reads
// the remote role, the context walk reads Owner and Controller).
struct C572PawnKey {
    uint8_t remote_role;
    const void* owner;
    const void* controller;
};

C572PawnKey c572_pawn_key(const void* pawn) {
    const auto* const a = static_cast<const uint8_t*>(pawn);
    return C572PawnKey{a[kActorRemoteRole], read_ptr(pawn, kActorOwner), read_ptr(pawn, kPawnController)};
}

bool c572_pawn_tracked(const void* pawn) {
    return !g_client_session && pawn != nullptr && g_pawn_class != nullptr && is_a(pawn, g_pawn_class());
}

void c572_pawn_note(const void* pawn, const C572PawnKey& before) {
    const C572PawnKey after = c572_pawn_key(pawn);
    if (after.remote_role != before.remote_role || after.owner != before.owner || after.controller != before.controller) {
        c572_input_bump(kC572InputRole);
    }
}

} // namespace

void detour_c572_pawn_possessed(void* pawn, void* controller) {
    if (!c572_pawn_tracked(pawn)) {
        g_orig_c572_pawn_possessed(pawn, controller);
        return;
    }
    const C572PawnKey before = c572_pawn_key(pawn);
    g_orig_c572_pawn_possessed(pawn, controller);
    c572_pawn_note(pawn, before);
}

void detour_c572_pawn_unpossessed(void* pawn) {
    if (!c572_pawn_tracked(pawn)) {
        g_orig_c572_pawn_unpossessed(pawn);
        return;
    }
    const C572PawnKey before = c572_pawn_key(pawn);
    g_orig_c572_pawn_unpossessed(pawn);
    c572_pawn_note(pawn, before);
}

void* detour_c99_get_pc(const void* ctx, int32_t index) {
    void* const pc = g_get_player_controller(ctx, index); // the trampoline (the hook row's original slot)
    // A client has no server copy (sprint_side: local Authority with an AutonomousProxy remote role is host only), no
    // acting scope and no enemy remap (c103_enemy_pc), so its answer is always the original: no context walk.
    if (index != 0 || ctx == nullptr || pc == nullptr || g_client_session) {
        return pc;
    }
    const unsigned long long rva = exe_rva(_ReturnAddress());
    if (!g_c572_on) {
        return c99_answer(ctx, pc, rva, nullptr);
    }
    c572_c99_call(); // card 572 step 1: time 1 body in 64
    if ((++g_c572_tick & 63u) != 0) {
        return c99_answer(ctx, pc, rva, nullptr);
    }
    C572Parts parts{};
    const uint64_t t0 = __rdtsc();
    void* const own = c99_answer(ctx, pc, rva, &parts);
    c572_c99_sample(__rdtsc() - t0, parts);
    return own;
}

C102Scope c102_enter(const void* actor, uint8_t entry, const void* site) {
    const C102Scope saved = g_c102;
    g_c102.acting = actor != nullptr ? c99_copy_of(actor) : nullptr;
    g_c102.entry = g_c102.acting != nullptr ? entry : kC102None;
    g_c102.site = g_c102.acting != nullptr ? site : nullptr;
    return saved;
}

void c102_leave(const C102Scope& saved) {
    g_c102 = saved;
}

// Card 582 G3 (b453): USTCombatUtilityLibrary::ApplyDamageAtLocation, the knife's damage (GA_HumanMelee DealDamage
// passes GetPlayerCharacter as both instigator and causer). (a) It had no card 102 scope, so a damaged world actor's
// "Causer == GetPlayerCharacter(0)" check (BP_KillableBuilding_Base's damaged sound) compared a server copy with the
// host pawn. (b) Inside it the host runs the knife's impact cue (GC_Impacts_PlayerMelee, MS_GORE_Knife_Attack_Impact)
// for the copy; the client never runs it itself, so card 136 sends those rows to the copy's own PC too.
C582DamageAtFn g_orig_c582_damage_at = nullptr;
uint32_t g_c582_melee_depth = 0; // game thread only: inside a server copy's self-caused ApplyDamageAtLocation

void* detour_c582_damage_at(void* results, void* instigator, void* causer, const void* tmpl, const void* loc) {
    const C102Scope saved = c102_enter(instigator != nullptr ? instigator : causer, kC102Damage);
    const uint32_t melee = g_c102.acting != nullptr && instigator == causer ? 1u : 0u;
    g_c582_melee_depth += melee;
    void* const ret = g_orig_c582_damage_at(results, instigator, causer, tmpl, loc);
    g_c582_melee_depth -= melee;
    c102_leave(saved);
    return ret;
}

bool c582_copy_melee() {
    return g_c582_melee_depth != 0;
}

// Host: an overlap acts for the server copy that owns either component (the other one is the FOverlapInfo's hit
// component, a weak pointer at +0xE0 = bFromSweep, padding, FHitResult::Component at +0xD8; BeginComponentOverlap+0x6B).
// Card 302: the site is the other side's owner (the pickup whose OnBeginOverlap -> TryPickup -> Pickup runs inside this
// call, AADPickupBase::OnBeginOverlap+0xF4 calls TryPickup).
void detour_c102_overlap(void* component, const void* info, bool notify) {
    if (g_client_session) { // no server copy on a client: no scope to enter
        g_orig_c102_overlap(component, info, notify);
        return;
    }
    const void* const owner = read_ptr(component, kComponentOwner);
    const void* copy = owner != nullptr ? c99_copy_of(owner) : nullptr;
    const void* site = owner;
    if (copy == nullptr) {
        const void* const other = g_weak_get(static_cast<const uint8_t*>(info) + kC102OverlapComponent);
        const void* const other_owner = other != nullptr ? read_ptr(other, kComponentOwner) : nullptr;
        copy = other_owner;
    } else {
        const void* const other = g_weak_get(static_cast<const uint8_t*>(info) + kC102OverlapComponent);
        site = other != nullptr ? read_ptr(other, kComponentOwner) : nullptr;
    }
    const C102Scope saved = c102_enter(copy, kC102Overlap, site);
    g_orig_c102_overlap(component, info, notify);
    c102_leave(saved);
}

// Card 130 item 13: a new actor's BeginPlay is the actor's own start, not part of the act that spawned it. An
// objective marker spawned while a client copy's overlap completes a step (ASTLevelObjectiveMarker::Create
// ASTLevelObjectiveMarker, a deferred spawn whose FinishSpawning dispatches BeginPlay) asked GetPlayerController(0)
// inside the scope and built its widget for the client's controller, so the host never saw it. An actor spawned for
// a copy still remaps through its own Instigator/Owner chain (c99_copy_of).
C99BeginPlayFn g_orig_c99_begin_play = nullptr;
namespace {
constexpr uint32_t kC99BeginPlayLogs = 16;
uint32_t g_c99_begin_play_suspended = 0;
} // namespace

void detour_c99_begin_play(void* actor, bool from_streaming) {
    c555_begin_play(actor); // card 555: both sides
    if (g_client_session) {
        c138_note_begin_play(actor);
    } else {
        c582_tracker_begin_play(actor); // card 582 G4
    }
    if (g_c102.acting == nullptr) {
        g_orig_c99_begin_play(actor, from_streaming);
        return;
    }
    if (g_c99_begin_play_suspended++ < kC99BeginPlayLogs) {
        log::line("card130: host BeginPlay of %s runs outside the acting scope of %s (entry %u, %u so far)",
                  object_name(actor).text, object_name(g_c102.acting).text, static_cast<unsigned>(g_c102.entry),
                  g_c99_begin_play_suspended);
    }
    const C102Scope saved = c102_enter(nullptr, kC102None);
    g_orig_c99_begin_play(actor, from_streaming);
    c102_leave(saved);
}

// Host: an interaction acts for the interactor's server copy owner. The player transport dropship is left out:
// EnterDropship possesses the dropship with "the player" controller and then ends the level for everyone.
void c102_interact(void* interactor) {
    const void* const target = interact_target(interactor);
    const bool dropship = target != nullptr && is_a(target, g_transport_dropship_class());
    const C102Scope saved =
        c102_enter(dropship ? nullptr : read_ptr(interactor, kComponentOwner), kC102Interaction, target); // card 302 site
    const void* const pawn = read_ptr(interactor, kComponentOwner);
    const bool press = c492_switch_press(target, pawn); // card 492: a switch close is a player trigger
    g_orig_interact(interactor);
    c102_leave(saved);
    if (press) {
        c602_send_press(target, pawn); // card 602: the other clients' copies broadcast too
    }
}

// Card 160: a material parameter collection is one per world, so a server copy's write (IronSights'
// PlayerCharacterFOV) would change the host's own first-person view.
void detour_c160_mpc_scalar(void* ctx, void* collection, uint64_t name, float value) {
    if (c160_copy_effect(kC160Mpc, ctx, nullptr, collection, exe_rva(_ReturnAddress()))) {
        return;
    }
    g_orig_c160_mpc_scalar(ctx, collection, name, value);
}

// Card 160: Create Widget with no owning player makes the widget for the world's first local player, the host. The
// Blueprint caller (Stack.Object) is in scope here; detour_c160_widget_world returns no widget for a server copy's.
void detour_c160_exec_create(void* ctx, void* frame, void* result) {
    const void* const saved = g_c160.widget_copy;
    const void* const caller = read_ptr(frame, kFrameObject);
    g_c160.widget_copy = !g_client_session && caller != nullptr ? c99_copy_of(caller) : nullptr;
    g_c160.widget_copy = !g_client_session && g_c160.widget_copy == nullptr ? c302_acting_for(caller, nullptr, 0)
                                                                            : g_c160.widget_copy; // card 302
    g_orig_c160_exec_create(ctx, frame, result);
    g_c160.widget_copy = saved;
}

void* detour_c160_widget_world(void* world, void* const* cls, uint64_t name) {
    if (g_c160.widget_copy != nullptr) {
        ++g_c160.count[kC160Widget];
        if (note_event_cap(g_c160.lines[kC160Skips], kC160Cap[kC160Skips], kC160CapText[kC160Skips])) {
            log::line("card160: host t=%llu skipped server copy widget %s for %s (Create with no owning player)",
                      static_cast<unsigned long long>(GetTickCount64()),
                      object_name(cls != nullptr ? *cls : nullptr).text, object_name(g_c160.widget_copy).text);
        }
        return nullptr;
    }
    return g_orig_c160_widget_world(world, cls, name);
}

// ---- Card 160: the server copy's IronSights reads its client's own ADS inputs ----
// BP_Weapon_IronSightsAbility picks hold or toggle from GetSTGameUserSettings' IsToggleADSKBMEnabled or
// IsToggleADSControllerEnabled, by UADInputDetectionSubsystem::CurrentInputGamepad. On the host those read the host's
// own settings and input device. The client puts its three values on every ADS edge (kFireAds* bits); the host keeps
// them per PlayerController and answers the three exec thunks with them when the Blueprint caller (Stack.Object)
// belongs to that PC's server copy. Test switch UBWCOOP_C160_TOGGLE_ADS=1 (client): SetToggleADSKBM(true) in memory
// before the first ADS edge (a field write; nothing is saved).

namespace {

enum C160Read : uint8_t { kC160ReadKbm, kC160ReadCtrl, kC160ReadPad, kC160Reads };
constexpr const char* kC160ReadText[kC160Reads] = {"IsToggleADSKBMEnabled", "IsToggleADSControllerEnabled",
                                                   "CurrentInputGamepad"};
constexpr uint32_t kC160ReadBit[kC160Reads] = {kFireAdsToggleKbm, kFireAdsToggleCtrl, kFireAdsGamepad};
constexpr size_t kC160PadState = 0x70;  // UADInputDetectionSubsystem state CurrentInputGamepad reads (+0x1A)
constexpr uint32_t kC160ReadLines = 24; // card160 override lines per session

struct C160Pc {
    int32_t pc_id; // UObject InternalIndex of the client's PlayerController; 0 = free row
    uint32_t bits; // the last ADS edge's kFireAds* bits
};
struct C160Inputs {
    C160Pc pc[kC17Slots];
    uint32_t client_sent; // client: the bits of the last logged edge (+1 so 0 means none yet)
    uint32_t lines;
    uint32_t overrides[kC160Reads];
    int8_t test; // client: UBWCOOP_C160_TOGGLE_ADS, -1 = not read yet
};
C160Inputs g_c160_in{{}, 0, 0, {}, -1};

const void* c160_world_of(const void* context) {
    return is_a(context, g_actor_class()) ? g_actor_get_world(context) : context;
}

// Host: the row for pc, made when `make`, else null.
C160Pc* c160_pc_row(const void* pc, bool make) {
    const int32_t id = object_index(pc);
    C160Pc* free_row = nullptr;
    for (C160Pc& r : g_c160_in.pc) {
        if (r.pc_id == id && id != 0) {
            return &r;
        }
        free_row = free_row == nullptr && r.pc_id == 0 ? &r : free_row;
    }
    if (make && free_row != nullptr) {
        free_row->pc_id = id;
        return free_row;
    }
    return nullptr;
}

// Host, after the original thunk wrote the host's value: a server copy caller gets its client's value instead.
void c160_answer_copy(uint8_t read, const void* frame, void* result) {
    const void* const caller = g_client_session || result == nullptr ? nullptr : read_ptr(frame, kFrameObject);
    const void* const copy = caller != nullptr ? c99_copy_of(caller) : nullptr;
    const void* const pc = copy != nullptr ? c99_copy_pc(copy) : nullptr;
    if (pc == nullptr) {
        return;
    }
    const C160Pc* const row = c160_pc_row(pc, false);
    bool* const out = static_cast<bool*>(result);
    const bool host_value = *out;
    if (row == nullptr || (row->bits & kFireAdsKnown) == 0) {
        if (note_event_cap(g_c160_in.lines, kC160ReadLines, "card160 ADS reads")) {
            log::line("WARN card160: host t=%llu server copy %s reads %s with no client value yet: host's own %d kept",
                      static_cast<unsigned long long>(GetTickCount64()), class_name(caller).text, kC160ReadText[read],
                      host_value ? 1 : 0);
        }
        return;
    }
    *out = (row->bits & kC160ReadBit[read]) != 0;
    ++g_c160_in.overrides[read];
    if (note_event_cap(g_c160_in.lines, kC160ReadLines, "card160 ADS reads")) {
        log::line("card160: host t=%llu server copy %s (%s) reads %s: the client's %d (host's own %d)",
                  static_cast<unsigned long long>(GetTickCount64()), class_name(caller).text, object_name(pc).text,
                  kC160ReadText[read], *out ? 1 : 0, host_value ? 1 : 0);
    }
}

} // namespace

void detour_c160_exec_toggle_kbm(void* ctx, void* frame, void* result) {
    g_orig_c160_exec_toggle_kbm(ctx, frame, result);
    c160_answer_copy(kC160ReadKbm, frame, result);
}

void detour_c160_exec_toggle_ctrl(void* ctx, void* frame, void* result) {
    g_orig_c160_exec_toggle_ctrl(ctx, frame, result);
    c160_answer_copy(kC160ReadCtrl, frame, result);
}

void detour_c160_exec_gamepad(void* ctx, void* frame, void* result) {
    g_orig_c160_exec_gamepad(ctx, frame, result);
    c160_answer_copy(kC160ReadPad, frame, result);
}

// Client: the kFireAds* bits for an ADS edge, read now from its own settings and input device (0 when unreadable).
uint32_t c160_client_ads_bits(const void* context) {
    void* const settings = g_c160_settings != nullptr ? g_c160_settings() : nullptr;
    if (g_c160_in.test < 0) {
        g_c160_in.test = ue::config::read_switch(L"UBWCOOP_C160_TOGGLE_ADS", "card 160 test: the client's toggle ADS "
                                                                             "(keyboard and mouse) on, in memory") ? 1 : 0;
        if (g_c160_in.test == 1 && settings != nullptr) {
            g_c160_set_toggle_kbm(settings, true);
            log::line("card160: client test: SetToggleADSKBM(true) in memory (UBWCOOP_C160_TOGGLE_ADS)");
        }
    }
    const void* const world = context != nullptr ? c160_world_of(context) : nullptr;
    const void* const gi = world != nullptr ? read_ptr(world, kWorldOwningGameInstance) : nullptr;
    const void* const pad = gi != nullptr ? g_c160_input_subsystem(gi) : nullptr;
    if (settings == nullptr || pad == nullptr || read_ptr(pad, kC160PadState) == nullptr) {
        log::line("WARN card160: client ADS edge without its settings (settings %p, input subsystem %p): the host "
                  "keeps its own", settings, pad);
        return 0;
    }
    const uint32_t bits = kFireAdsKnown | (g_c160_toggle_kbm(settings) ? kFireAdsToggleKbm : 0u) |
                          (g_c160_toggle_ctrl(settings) ? kFireAdsToggleCtrl : 0u) |
                          (g_c160_gamepad(pad) ? kFireAdsGamepad : 0u);
    if (bits + 1 != g_c160_in.client_sent) {
        g_c160_in.client_sent = bits + 1;
        log::line("card160: client t=%llu ADS edge carries toggle KBM %d, toggle controller %d, gamepad %d",
                  static_cast<unsigned long long>(GetTickCount64()), (bits & kFireAdsToggleKbm) != 0 ? 1 : 0,
                  (bits & kFireAdsToggleCtrl) != 0 ? 1 : 0, (bits & kFireAdsGamepad) != 0 ? 1 : 0);
    }
    return bits;
}

// Host: an ADS edge from pc; its bits are what pc's server copy reads from now on.
void c160_host_ads_bits(const void* pc, uint32_t arg) {
    C160Pc* const row = c160_pc_row(pc, true);
    if (row == nullptr) {
        log::line("WARN card160: host ADS settings table full (%d PCs): %s not kept", kC17Slots, object_name(pc).text);
        return;
    }
    const uint32_t bits = arg & (kFireAdsKnown | kFireAdsToggleKbm | kFireAdsToggleCtrl | kFireAdsGamepad);
    if (bits != row->bits && note_event_cap(g_c160_in.lines, kC160ReadLines, "card160 ADS reads")) {
        log::line("card160: host t=%llu %s ADS settings: known %d, toggle KBM %d, toggle controller %d, gamepad %d",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(pc).text,
                  (bits & kFireAdsKnown) != 0 ? 1 : 0, (bits & kFireAdsToggleKbm) != 0 ? 1 : 0,
                  (bits & kFireAdsToggleCtrl) != 0 ? 1 : 0, (bits & kFireAdsGamepad) != 0 ? 1 : 0);
    }
    row->bits = bits;
}

void c99_arm_map() {
    ++g_c99_frame; // card 572: no memo row outlives the map
    c103_arm_map();
    g_c582 = C582Seen{}; // card 582: class pointers are per map (a Blueprint class can unload with its level)
    c532_arm_map(); // card 532: tables, pending rows and world reset per map
    log::line("card160: map: host skipped server copy effects: %u 2D sounds, %u material parameters, %u widgets; "
              "muted %u 2D component sounds (%u muted auto-destroy components still live)",
              g_c160.count[kC160Sound], g_c160.count[kC160Mpc], g_c160.count[kC160Widget], g_c160.count[kC160Mute],
              c176_live());
    g_c160 = C160Skips{};
    for (C31Weak& w : g_c176.ring) {
        w = C31Weak{};
    }
    g_c176.next = 0;
    if (!g_client_session) {
        log::line("card160: map: host answered server copy ADS reads with the client's values: %u KBM toggle, %u "
                  "controller toggle, %u gamepad", g_c160_in.overrides[kC160ReadKbm], g_c160_in.overrides[kC160ReadCtrl],
                  g_c160_in.overrides[kC160ReadPad]);
        for (uint32_t& n : g_c160_in.overrides) {
            n = 0;
        }
        for (C160Pc& r : g_c160_in.pc) {
            r = C160Pc{}; // PlayerControllers are new each map; the next ADS edge fills the row again
        }
    }
    c99_map_totals("map");
    g_c99 = C99Callers{};
    c455_attached_map_reset(); // card 582 G3 diag rows
    g_c582_threat = C582Threat{};
    g_c582_kept = C582Kept{};
    c582_trace_map_reset(); // card 582 G3 b451 diag rows
    c582_tracker_map();
    c584_map();
}

void c582_level_totals(const char* when); // defined with the sound detours below

// Card 582: the card099 caller table and the card582 tallies, print only. At map arm (label "map") and at the test
// quit (label "quit"): a run that quits inside its last map has no next arm to print them (c582b410a).
void c99_map_totals(const char* when) {
    log::line("card099: %s: player 0 remapped %u times by %u callers (%u callers not counted)", when, g_c99.total,
              g_c99.used, g_c99.dropped);
    for (uint32_t i = 0; i < g_c99.used; ++i) {
        const uint8_t e = g_c99.row[i].entry;
        log::line("card099:   caller exe+0x%llX, context %s (%s), %u calls", g_c99.row[i].rva, g_c99.row[i].cls,
                  e < kC102Entries ? kC102EntryText[e] : "?", g_c99.row[i].count);
    }
    if (!g_client_session && (g_c582_threat.seen > 0 || g_c99_log)) {
        log::line("card582: %s: EM marker threat calls %u for the host pawn, mirrored %u times to client pawns, "
                  "%u repeat pairs skipped", when, g_c582_threat.seen, g_c582_threat.mirrored, g_c582_threat.repeats);
    }
    if (g_c582_threat.full > 0) { // the table peak rule: reported in the play build too
        log::line("WARN card582: %s: threat pair table full (%u pairs), %u mirrors not made", when, kC582Pairs,
                  g_c582_threat.full);
    }
    c582_tracker_totals(when);
    c582_level_totals(when);
    c604_totals(when);
    if (g_c582_kept.full > 0) { // the table peak rule
        log::line("WARN card582: %s: not-remapped key table full (%u keys), %u calls of other keys not logged", when,
                  kC582Kept, g_c582_kept.full);
    }
}

// Card 455: the player's hurt sounds are 2D sounds its Blueprint plays from the damage event, which runs on the host
// only; the ammo pickup sound comes from the host's overlap (acting copy, card 302). Card 160 skips them for a server copy; a sound named by a row below goes to the copy's PC instead. A sound
// the client plays itself (weapon swap, pickups) must never be a row: it would play twice.
constexpr const char* kC455Prefix[] = {"MS_GORE_Human_Player_", "MS_PLAYERBARK_Pain", "MS_ITEM_Pickup"};
constexpr uint32_t kC455Lines = 60;     // rows per side
constexpr float kC455MaxScale = 4.0f;   // volume and pitch ceiling a frame may ask for

bool c455_line_ok() {
    if (g_c99.hurt_lines >= kC455Lines) {
        return false;
    }
    ++g_c99.hurt_lines;
    return true;
}

// Host: one HurtSound frame to a copy's PC, with its card455 row.
void c455_send(void* pc, const char* text, int len, size_t cap, const char* kind, const char* name) {
    // c423b162a: a rejoining client's copy was hit 233 ms before its first Pong; card 139 holds every frame to a PC
    // until then (its join replay follows), so that refusal is expected and not a WARN.
    const C50Copy* const c = pc != nullptr ? c50_copy(object_index(pc), false) : nullptr;
    const bool not_ready = pc != nullptr && (c == nullptr || !c->ponged); // a null PC stays a WARN below
    const bool sent = pc != nullptr && !not_ready && len > 0 && static_cast<size_t>(len) < cap &&
                      c95_send_one(pc, ChanOp::HurtSound, text);
    if (c455_line_ok()) { // the not-sent WARN shares the 60-row cap
        log::line("%scard455: host t=%llu relayed %s %s to %s (%s): %s", sent || not_ready ? "" : "WARN ",
                  static_cast<unsigned long long>(GetTickCount64()), kind, name, object_name(pc).text,
                  sent ? "sent" : not_ready ? "not sent: client not ready" : "not sent", text);
    }
}

// Host: called for a 2D sound card 160 just skipped.
void c455_host_relay(const void* ctx, const void* owner, const void* sound, float volume, float pitch,
                     unsigned long long rva) {
    const auto name = object_name(sound);
    bool listed = false;
    for (const char* const prefix : kC455Prefix) {
        listed = listed || strncmp(name.text, prefix, strlen(prefix)) == 0;
    }
    const void* copy = listed && ctx != nullptr ? c99_copy_of(ctx) : nullptr;
    copy = listed && copy == nullptr && owner != nullptr ? c99_copy_of(owner) : copy;
    copy = listed && copy == nullptr ? c302_acting_for(ctx, owner, rva) : copy; // as c160_copy_effect
    if (copy == nullptr) {
        return;
    }
    char text[kAssetPathChars + 32];
    const int len = snprintf(text, sizeof(text), "%s %.3f %.3f", c74_asset_path(sound).text, volume, pitch);
    c455_send(c99_copy_pc(copy), text, len, sizeof(text), "hurt sound", name.text);
}

// Card 582 G3: USTCommonUtilityLibrary::SpawnSoundAttachedToPlayerWithAIAwareness (exe+0x5250F40) attaches its sound
// (a sound table row named by FName) to GetPlayerController(context, 0)'s pawn root (exe+0x523E0F0, pawn+0x1B8) through
// SpawnSoundAttachedToComponentWithAIAwareness (exe+0x5250A80, the card 052 detour). Card 099 hands a client's act the
// server copy, so the host played it on the copy and no one heard it. A sound named by a row below goes to the copy's PC
// instead, and the client attaches it to its own pawn. A sound the client plays itself must never be a row.
namespace {
constexpr const char* kC455AttachedPrefix[] = {"MS_EMIT_Human_Building_"};
constexpr uint32_t kC455AttachedDiagLines = 24; // UBWCOOP_C99_LOG rows per map
uint32_t g_c455_attached_diag = 0;
constexpr uint32_t kC455AttachedOtherLines = 8; // unlisted, not a server copy: the host's own sounds
uint32_t g_c455_attached_other = 0;
constexpr float kC455MaxStart = 60.0f; // start time ceiling a frame may ask for, s

} // namespace

void c455_attached_map_reset() {
    g_c455_attached_diag = 0;
    g_c455_attached_other = 0;
}

bool c455_attached_relay(const void* component, uint64_t sound_name, uint64_t attach_point, Vec3 location,
                         bool stop_when_destroyed, float volume, float pitch, float start_time, bool auto_destroy,
                         unsigned long long rva) {
    if (g_client_session || component == nullptr) {
        return false;
    }
    const void* const owner = read_ptr(component, kComponentOwner);
    const void* const copy = owner != nullptr ? c99_copy_of(owner) : nullptr;
    const auto name = fname_text(&sound_name);
    bool listed = false;
    for (const char* const prefix : kC455AttachedPrefix) {
        listed = listed || strncmp(name.text, prefix, strlen(prefix)) == 0;
    }
    // b450: only listed or server-copy rows count against kC455AttachedDiagLines; the host's own unlisted sounds have
    // their own small cap (run c582b449c: they spent all 24 before the step that needed them).
    const bool counted = listed || copy != nullptr;
    uint32_t& rows = counted ? g_c455_attached_diag : g_c455_attached_other;
    if (g_c99_log && rows < (counted ? kC455AttachedDiagLines : kC455AttachedOtherLines)) {
        ++rows;
        log::line("card455: host t=%llu attached sound %s on %s (%s, %s), caller exe+0x%llX",
                  static_cast<unsigned long long>(GetTickCount64()), name.text, object_name(owner).text,
                  copy != nullptr ? "server copy" : "not a server copy", listed ? "listed" : "not listed", rva);
    }
    if (copy == nullptr || !listed) {
        return false;
    }
    char text[2 * kNameChars + 160];
    const int len = snprintf(text, sizeof(text), "@%s %s %.1f %.1f %.1f %d %d %.3f %.3f %.3f", name.text,
                             fname_text(&attach_point).text, location.x, location.y, location.z,
                             stop_when_destroyed ? 1 : 0, auto_destroy ? 1 : 0, volume, pitch, start_time);
    c455_send(c99_copy_pc(copy), text, len, sizeof(text), "attached sound", name.text);
    return true; // skipped on the host even when not sent: the copy's sound is never heard there
}

namespace {

// Client: the attached form ("@<sound FName> <attach FName> <x> <y> <z> <stop> <auto> <volume> <pitch> <start>"),
// attached to the own pawn's root through the original SpawnSoundAttachedToComponentWithAIAwareness.
void c455_receive_attached(void* pawn, const char* text) {
    char sound[kNameChars] = {};
    char attach[kNameChars] = {};
    static_assert(kNameChars == 64, "the %63s widths below");
    Vec3 at{};
    int stop = 0;
    int autod = 0;
    float volume = 1.0f;
    float pitch = 1.0f;
    float start = 0.0f;
    const bool parsed =
        sscanf_s(text, "@%63s %63s %lf %lf %lf %d %d %f %f %f", sound, static_cast<unsigned>(sizeof(sound)), attach,
                 static_cast<unsigned>(sizeof(attach)), &at.x, &at.y, &at.z, &stop, &autod, &volume, &pitch,
                 &start) == 10 &&
        volume >= 0.0f && volume <= kC455MaxScale && pitch > 0.0f && pitch <= kC455MaxScale && start >= 0.0f &&
        start <= kC455MaxStart;
    wchar_t wide[kNameChars] = {};
    uint64_t sound_name = 0;
    uint64_t attach_name = 0;
    if (parsed && g_fname_ctor != nullptr) {
        for (size_t i = 0; i < kNameChars - 1 && sound[i] != 0; ++i) {
            wide[i] = static_cast<wchar_t>(static_cast<unsigned char>(sound[i]));
        }
        g_fname_ctor(&sound_name, wide, 0); // FNAME_Find: a name this client never made is not played
        memset(wide, 0, sizeof(wide));
        for (size_t i = 0; i < kNameChars - 1 && attach[i] != 0; ++i) {
            wide[i] = static_cast<wchar_t>(static_cast<unsigned char>(attach[i]));
        }
        g_fname_ctor(&attach_name, wide, 0); // "None" finds 0, NAME_None
    }
    void* const root = pawn != nullptr ? read_ptr(pawn, kActorRootComponent) : nullptr;
    const bool play = sound_name != 0 && root != nullptr && g_orig_spawn_sound_attached != nullptr;
    if (play) {
        g_orig_spawn_sound_attached(root, sound_name, attach_name, at, stop != 0, volume, pitch, start, nullptr,
                                    nullptr, autod != 0);
    }
    if (!play || c455_line_ok()) {
        log::line("%scard455: client t=%llu %s attached sound %s (parsed %d, own pawn %d, name %d)",
                  play ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()),
                  play ? "played" : "did not play", text, parsed ? 1 : 0, root != nullptr ? 1 : 0,
                  sound_name != 0 ? 1 : 0);
    }
}

} // namespace

// Client: ChanOp::HurtSound. Played through the original UGameplayStatics::PlaySound2D on the own pawn.
void c455_receive(void* pc, const WireString* s) {
    static char text[kChanTextMax + 1];
    static_assert(kAssetPathChars == 192, "the %191s width below");
    char path[kAssetPathChars] = {};
    float volume = 1.0f;
    float pitch = 1.0f;
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    const bool read = chan_text(s, text, sizeof(text)) > 0;
    if (read && text[0] == '@') { // card 582 G3: the attached form
        c455_receive_attached(pawn, text);
        return;
    }
    const bool parsed = read && sscanf_s(text, "%191s %f %f", path, static_cast<unsigned>(sizeof(path)), &volume, &pitch) == 3 &&
                        volume >= 0.0f && volume <= kC455MaxScale && pitch > 0.0f && pitch <= kC455MaxScale;
    void* const sound = parsed && g_sound_base_class != nullptr ? c74_find_asset(path, g_sound_base_class()) : nullptr;
    const bool play = pawn != nullptr && sound != nullptr && g_orig_c99_sound_2d != nullptr;
    if (play) {
        g_orig_c99_sound_2d(pawn, sound, volume, pitch, 0.0f, nullptr, pawn, false);
    }
    if (!play || c455_line_ok()) {
        log::line("%scard455: client t=%llu %s hurt sound %s (parsed %d, own pawn %d, asset %d)", play ? "" : "WARN ",
                  static_cast<unsigned long long>(GetTickCount64()), play ? "played" : "did not play", text,
                  parsed ? 1 : 0, pawn != nullptr ? 1 : 0, sound != nullptr ? 1 : 0);
    }
}

// Card 491 b232 (UBWCOOP_C99_LOG): every Supply_Upgrade sound, both sides, with its own budget, and whether the card
// 160 server copy gate skipped it.
namespace {
constexpr uint32_t kC491SoundRows = 40;
uint32_t g_c491_sound_rows = 0;
void c491_sound_row(const char* fn, const void* ctx, const void* owner, const void* sound, bool skipped) {
    if (!g_c99_log || strstr(object_name(sound).text, "Supply_Upgrade") == nullptr ||
        g_c491_sound_rows >= kC491SoundRows) {
        return;
    }
    ++g_c491_sound_rows;
    log::line("card491: %s t=%llu sound %s %s: context %s, owner %s, %s", side_text(),
              static_cast<unsigned long long>(GetTickCount64()), fn, object_name(sound).text, class_name(ctx).text,
              object_name(owner).text, skipped ? "skipped (server copy)" : "played");
}
// Card 582 step 1, level-script presentation (UBWCOOP_C99_LOG, both sides): a 2D sound whose context class ends in
// "_Scripting_C". The level script runs on each machine for its own player; the rows and the per-(call, asset) tally
// show whether the client plays what the host plays. Camera shakes are not covered here: the camera manager detour has
// no world context.
constexpr uint32_t kC582LevelRows = 40;  // rows per map
constexpr uint32_t kC582LevelKeys = 32;  // tally keys per map
struct C582LevelKey {
    const char* call;
    const void* asset;
    uint32_t count;
    char name[kNameChars];
};
struct C582Level {
    C582LevelKey key[kC582LevelKeys];
    uint32_t used, rows, full;
};
C582Level g_c582_level{};

} // namespace

bool c582_level_script(const void* ctx) { // card 604 also reads it (audio_c604.cpp)
    const ObjectName cls = class_name(ctx);
    const size_t n = strlen(cls.text);
    constexpr size_t kTail = sizeof("_Scripting_C") - 1;
    return n >= kTail && strcmp(cls.text + n - kTail, "_Scripting_C") == 0;
}

namespace {

void c582_level_row(const char* call, const void* ctx, const void* asset, float volume) {
    if (!g_c99_log || ctx == nullptr || !c582_level_script(ctx)) {
        return;
    }
    uint32_t i = 0;
    while (i < g_c582_level.used && (g_c582_level.key[i].call != call || g_c582_level.key[i].asset != asset)) {
        ++i;
    }
    if (i == g_c582_level.used && i < kC582LevelKeys) {
        C582LevelKey& k = g_c582_level.key[g_c582_level.used++];
        k = C582LevelKey{call, asset, 0, {}};
        snprintf(k.name, sizeof(k.name), "%s", object_name(asset).text);
    }
    if (i < kC582LevelKeys) {
        ++g_c582_level.key[i].count;
    } else {
        ++g_c582_level.full;
    }
    if (g_c582_level.rows < kC582LevelRows) {
        ++g_c582_level.rows;
        log::line("card582: %s t=%llu level script %s %s volume/scale %.2f (%s)", side_text(),
                  static_cast<unsigned long long>(GetTickCount64()), call, object_name(asset).text,
                  static_cast<double>(volume), class_name(ctx).text);
    }
}

} // namespace

// Card 582 step 1: the level-script tally, print only, then clear (map arm and test quit, from c99_map_totals).
void c582_level_totals(const char* when) {
    for (uint32_t i = 0; i < g_c582_level.used; ++i) {
        log::line("card582: %s %s: level script %s %s, %u plays", side_text(), when, g_c582_level.key[i].call,
                  g_c582_level.key[i].name, g_c582_level.key[i].count);
    }
    if (g_c582_level.full > 0) { // the table peak rule
        log::line("WARN card582: %s: level-script tally full (%u keys), %u plays of other keys not counted", when,
                  kC582LevelKeys, g_c582_level.full);
    }
    g_c582_level = C582Level{};
}

void detour_c99_sound_2d(const void* ctx, void* sound, float volume, float pitch, float start, void* concurrency,
                         const void* owner, bool ui) {
    if (c99_line_ok()) {
        const C302Acting acting = c302_acting_text();
        log::line("card099: %s PlaySound2D %s: context %s, owner %s (%s), caller exe+0x%llX, acting copy %s (%s)",
                  side_text(), object_name(sound).text, class_name(ctx).text, object_name(owner).text,
                  owner != nullptr && c99_copy_of(owner) != nullptr ? "server copy" : "not a server copy",
                  exe_rva(_ReturnAddress()), acting.name, acting.entry);
    }
    const bool skipped = c160_copy_effect(kC160Sound, ctx, owner, sound, exe_rva(_ReturnAddress()));
    c491_sound_row("PlaySound2D", ctx, owner, sound, skipped);
    c582_level_row("PlaySound2D", ctx, sound, volume);
    if (!skipped && c604_level_2d(ctx, owner, sound, volume, pitch)) {
        return; // card 604, client: a host LevelAudio row played this level-script 2D sound already
    }
    if (skipped) {
        c455_host_relay(ctx, owner, sound, volume, pitch, exe_rva(_ReturnAddress())); // card 455
        return;
    }
    g_orig_c99_sound_2d(ctx, sound, volume, pitch, start, concurrency, owner, ui);
}

void* detour_c99_spawn_sound_2d(const void* ctx, void* sound, float volume, float pitch, float start,
                                void* concurrency, bool persist, bool auto_destroy) {
    if (c99_line_ok()) {
        log::line("card099: %s SpawnSound2D %s: context %s (%s), caller exe+0x%llX", side_text(),
                  object_name(sound).text, class_name(ctx).text,
                  c99_copy_of(ctx) != nullptr ? "server copy" : "not a server copy", exe_rva(_ReturnAddress()));
    }
    // Card 173: BP_WeaponFireAbility's low-ammo click (SpawnSound2D of LowAmmoTick) runs on the server copy too. The
    // Blueprint's SetFloatParameter on the null return is an accessed-None warning, not a crash.
    const bool skipped = c160_copy_effect(kC160Sound, ctx, nullptr, sound, exe_rva(_ReturnAddress()));
    c491_sound_row("SpawnSound2D", ctx, nullptr, sound, skipped);
    c582_level_row("SpawnSound2D", ctx, sound, volume);
    if (skipped) {
        return nullptr;
    }
    return g_orig_c99_spawn_sound_2d(ctx, sound, volume, pitch, start, concurrency, persist, auto_destroy);
}

void detour_c99_ad_sound_2d(void* manager, const void* ctx, void* sound, float volume, float pitch, float start,
                            void* concurrency, const void* owner) {
    if (c99_line_ok()) {
        const C302Acting acting = c302_acting_text();
        log::line("card099: %s ADAudioManager::PlaySound2D %s: context %s (%s), owner %s, caller exe+0x%llX, "
                  "acting copy %s (%s)",
                  side_text(), object_name(sound).text, class_name(ctx).text,
                  ctx != nullptr && c99_copy_of(ctx) != nullptr ? "server copy" : "not a server copy",
                  object_name(owner).text, exe_rva(_ReturnAddress()), acting.name, acting.entry);
    }
    const bool skipped = c160_copy_effect(kC160Sound, ctx, owner, sound, exe_rva(_ReturnAddress()));
    c491_sound_row("ADAudioManager::PlaySound2D", ctx, owner, sound, skipped);
    c582_level_row("ADAudioManager::PlaySound2D", ctx, sound, volume);
    if (skipped) {
        c455_host_relay(ctx, owner, sound, volume, pitch, exe_rva(_ReturnAddress())); // card 455
        return;
    }
    g_orig_c99_ad_sound_2d(manager, ctx, sound, volume, pitch, start, concurrency, owner);
}

void detour_c99_sound_fname(void* ctx, uint64_t name, const Vec3* location, const void* rotation, float volume,
                            float pitch, float start, void* attenuation, void* concurrency, void* owner) {
    if (c99_line_ok()) {
        log::line("card099: %s PlaySoundAtLocationFName %s: context %s (%s), attenuation %s, caller exe+0x%llX",
                  side_text(), fname_text(&name).text, class_name(ctx).text,
                  ctx != nullptr && c99_copy_of(ctx) != nullptr ? "server copy" : "not a server copy",
                  object_name(attenuation).text, exe_rva(_ReturnAddress()));
    }
    g_orig_c99_sound_fname(ctx, name, location, rotation, volume, pitch, start, attenuation, concurrency, owner);
}

// TSubclassOf is passed as a pointer to the caller's copy (MSVC x64: class with user-defined constructors).
void* detour_c99_shake(void* manager, const void* const* shake_class, float scale, uint8_t space,
                       const void* rotation) {
    const void* const owner = read_ptr(manager, kActorOwner);
    const void* const cls = shake_class != nullptr ? *shake_class : nullptr;
    // Card 454: shakes have their own row budget; sound rows spent the shared one before the hits (c207b153a).
    if (g_c99.shake_lines < kC99ShakeLines) {
        ++g_c99.shake_lines;
        log::line("card099: %s CameraManager::StartCameraShake %s scale %.2f: camera of %s (%s), caller exe+0x%llX",
                  side_text(), object_name(cls).text, static_cast<double>(scale), object_name(owner).text,
                  owner != nullptr && sprint_side(owner) == kSideServerCopy ? "server copy" : "local",
                  exe_rva(_ReturnAddress()));
    }
    return g_orig_c99_shake(manager, shake_class, scale, space, rotation);
}

// Card 551: USTAnimNotify_ScreenShake::Notify_Implementation(Owner) calls USTCommonUtilityLibrary::StartCameraShake
// with Owner as the world context, and that shakes GetPlayerController(context, 0), this machine's own player. The
// notify has no locally controlled check (exe+0x50F6170), so a shake notify in a remote player's animation (the
// landing, playtest 2026-11-20: "we feel the recoil of the landing of whomever jumped") shook the local camera on
// both sides. Class rule: a shake notify whose owner (or the owner's Owner, for an actor such as a weapon) is a
// trooper or mech runs only when that pawn's controller is this machine's own player controller. NPC and world
// notifies are unchanged: each machine plays its own copy's shake, as single player does.
C551NotifyFn g_orig_c551_shake_notify = nullptr;
namespace {
constexpr uint32_t kC551Lines = 64; // skipped-notify rows per run (b382: 24 ran out before the jumps)
uint32_t g_c551_lines = 0;          // game thread

const void* c551_player_pawn(const void* actor) {
    for (int hop = 0; hop < 2 && actor != nullptr; ++hop) {
        if (is_a(actor, g_player_character_class()) || is_a(actor, g_mech_class())) {
            return actor;
        }
        actor = read_ptr(actor, kActorOwner);
    }
    return nullptr;
}
} // namespace

// Card 584 (diagnostic): the client's own landings run the heavy landing cue. ASTPlayerCharacter::Landed picks it
// when the air time at character+0x9C8 (summed by ASTPlayerCharacter::Tick while IsFalling) is over cfg+0x94
// HeavyLandingTimeThreshold, cfg = (character+0x7E0)+0x110. One row per own-trooper Landed on each side: air time,
// threshold, the movement mode at the call, and the Falling enters and leaves since the last Landed (a clean jump
// reads 1 enter and 1 leave: the previous landing's leave runs after its Landed).
C584LandedFn g_orig_c584_landed = nullptr;
namespace {
constexpr size_t kC584AirTime = 0x9C8;    // float, ASTPlayerCharacter::Landed comiss
constexpr size_t kC584CfgOwner = 0x7E0;   // ASTPlayerCharacter::Landed
constexpr size_t kC584Cfg = 0x110;        // on the +0x7E0 object
constexpr size_t kC584HeavyTime = 0x94;   // float HeavyLandingTimeThreshold on cfg
constexpr uint32_t kC584Lines = 48;
struct C584State {
    uint32_t enters;
    uint32_t leaves;
    uint32_t lines;
    ULONGLONG last_ms;
};
C584State g_c584{}; // game thread

bool c584_own_trooper(const void* pawn) {
    const void* const ctrl = read_ptr(pawn, kPawnController);
    return ctrl != nullptr && is_local_pc(ctrl) && is_a(pawn, g_player_character_class());
}
} // namespace

void c584_map() {
    const uint32_t lines = g_c584.lines; // the row cap is per run
    g_c584 = C584State{};
    g_c584.lines = lines;
}

void c584_note_mode(const void* cmc, uint8_t from, uint8_t to) {
    const void* const owner = read_ptr(cmc, kCmcCharacterOwner);
    if (owner == nullptr || !c584_own_trooper(owner)) {
        return;
    }
    g_c584.enters += to == kMoveFalling ? 1u : 0u;
    g_c584.leaves += from == kMoveFalling ? 1u : 0u;
}

void detour_c584_landed(void* pawn, const void* hit) {
    if (!c584_own_trooper(pawn)) {
        g_orig_c584_landed(pawn, hit);
        return;
    }
    const auto* const p = static_cast<const uint8_t*>(pawn);
    float air = 0.0f;
    memcpy(&air, p + kC584AirTime, sizeof(air));
    const void* const cfg_owner = read_ptr(pawn, kC584CfgOwner);
    const auto* const cfg = cfg_owner != nullptr ? static_cast<const uint8_t*>(read_ptr(cfg_owner, kC584Cfg)) : nullptr;
    float heavy = -1.0f;
    if (cfg != nullptr) {
        memcpy(&heavy, cfg + kC584HeavyTime, sizeof(heavy));
    }
    const auto* const cmc = static_cast<const uint8_t*>(read_ptr(pawn, kCharacterMovement));
    const ULONGLONG now = GetTickCount64();
    if (note_event_cap(g_c584.lines, kC584Lines, "card584 landed")) {
        log::line("card584: %s t=%llu own Landed %s: air time %.3f s, heavy threshold %.3f s (%s), mode %u, falling "
                  "enters %u leaves %u since the last Landed (%llu ms ago)",
                  side_text(), static_cast<unsigned long long>(now), object_name(pawn).text, static_cast<double>(air),
                  static_cast<double>(heavy), cfg == nullptr ? "no cfg" : air > heavy ? "heavy" : "normal",
                  cmc != nullptr ? cmc[kCmcMovementMode] : 0xFFu, g_c584.enters, g_c584.leaves,
                  static_cast<unsigned long long>(g_c584.last_ms != 0 ? now - g_c584.last_ms : 0));
    }
    g_c584.enters = 0;
    g_c584.leaves = 0;
    g_c584.last_ms = now;
    g_orig_c584_landed(pawn, hit);
}

void detour_c551_shake_notify(void* notify, void* owner) {
    const void* const pawn = c551_player_pawn(owner);
    const void* const ctrl = pawn != nullptr ? read_ptr(pawn, kPawnController) : nullptr;
    if (pawn == nullptr || (ctrl != nullptr && is_local_pc(ctrl))) {
        g_orig_c551_shake_notify(notify, owner);
        return;
    }
    if (note_event_cap(g_c551_lines, kC551Lines, "card551 skipped shake notify")) {
        log::line("card551: %s t=%llu skipped screen shake notify %s on %s (%s), controller %s",
                  side_text(), static_cast<unsigned long long>(GetTickCount64()), object_name(notify).text,
                  object_name(pawn).text, class_name(pawn).text, object_name(ctrl).text);
    }
}

// Card 551 b373: run c551b370a logged 0 skipped notifies, so the landing shake is not an anim notify. Landing path:
// ASTPlayerCharacter::Landed -> USTGASUtilityLibrary::ExecuteGameplayCueFromHit -> the impact cue's ExecuteImpact
// (Target, Parameters), which plays PlayScreenShake (USTCommonUtilityLibrary::StartCameraShake: this machine's own
// player camera, or StartCameraShakeAtLocation), PlayForceFeedback and ApplyScreenEffect on whatever machine runs
// the cue. The host runs the client pawn's landing (playtest 2026-11-21: "if client jumps the bounce will happen on
// host also when they land"). Class rule, the same as the notify above: inside ExecuteImpact whose Target is a trooper
// or mech not controlled by this machine's player, the three personal feedback calls are skipped; the impact's
// decal, particles and sound still play.
C551ImpactFn g_orig_c551_execute_impact = nullptr;
C551CueFn g_orig_c551_cue_shake = nullptr;
C551CueFn g_orig_c551_cue_feedback = nullptr;
C551ScreenFn g_orig_c551_cue_screen = nullptr;
uint32_t g_c551_relayed_player_cue = 0; // game thread

bool c551_is_player(const void* actor) {
    return c551_player_pawn(actor) != nullptr;
}
namespace {
const void* g_c551_impact_target = nullptr; // game thread: ExecuteImpact's Target while it runs (nesting restores)

bool c551_remote_player(const void* actor) {
    const void* const pawn = c551_player_pawn(actor);
    const void* const ctrl = pawn != nullptr ? read_ptr(pawn, kPawnController) : nullptr;
    return pawn != nullptr && !(ctrl != nullptr && is_local_pc(ctrl));
}

bool c551_skip_feedback(const void* notify, const void* target, const char* what) {
    if (g_c551_relayed_player_cue > 0) {
        if (note_event_cap(g_c551_lines, kC551Lines, "card551 skipped shake notify")) {
            log::line("card551: %s t=%llu skipped impact cue %s of %s: relayed cue of a remote player", side_text(),
                      static_cast<unsigned long long>(GetTickCount64()), what, object_name(notify).text);
        }
        return true;
    }
    if (target == nullptr || !c551_remote_player(target)) {
        return false;
    }
    if (note_event_cap(g_c551_lines, kC551Lines, "card551 skipped shake notify")) {
        log::line("card551: %s t=%llu skipped impact cue %s of %s on %s (%s)", side_text(),
                  static_cast<unsigned long long>(GetTickCount64()), what, object_name(notify).text,
                  object_name(target).text, class_name(target).text);
    }
    return true;
}
} // namespace

// Card 551 b380: run c551b378a logged 0 skips here and every landing shake ran from PlayScreenShake (exe+0x5133F6A),
// so a landing's Target is not the pawn (ExecuteGameplayCueFromHit passes the hit surface). The pawn that landed is the
// cue's instigator or effect causer (the game's own FGameplayCueParameters getters). The first player pawn among
// Target, instigator and effect causer is the cue's subject; the first rows with a player subject log all three.
C136CueActorFn g_c551_cue_causer = nullptr;
namespace {
constexpr uint32_t kC551ImpactLines = 64; // cue subject rows per run (b382: 16 ran out before the jumps)
uint32_t g_c551_impact_lines = 0;         // game thread

const void* c551_impact_subject(const void* target, const void* params, const char* where) {
    const void* const instigator =
        params != nullptr && g_c136_cue_instigator != nullptr ? g_c136_cue_instigator(params) : nullptr;
    const void* const causer = params != nullptr && g_c551_cue_causer != nullptr ? g_c551_cue_causer(params) : nullptr;
    const void* subject = c551_player_pawn(target);
    subject = subject != nullptr ? subject : c551_player_pawn(instigator);
    subject = subject != nullptr ? subject : c551_player_pawn(causer);
    if (g_c551_impact_lines < kC551ImpactLines) {
        ++g_c551_impact_lines;
        const char* const verdict =
            subject == nullptr ? "no player" : c551_remote_player(subject) ? "remote, feedback skipped" : "own";
        log::line("card551: %s t=%llu %s target %s (%s), instigator %s, effect causer %s: subject %s, %s",
                  side_text(), static_cast<unsigned long long>(GetTickCount64()), where, object_name(target).text,
                  class_name(target).text, object_name(instigator).text, object_name(causer).text,
                  object_name(subject).text, verdict);
    }
    return subject;
}
} // namespace

void detour_c551_execute_impact(const void* notify, void* target, const void* params) {
    const void* const saved = g_c551_impact_target;
    const void* const subject = c551_impact_subject(target, params, "ExecuteImpact");
    g_c551_impact_target = subject != nullptr ? subject : target;
    g_orig_c551_execute_impact(notify, target, params);
    g_c551_impact_target = saved;
}

// Card 551 b382: run c551b380a showed a client Fall shake from PlayScreenShake with no ExecuteImpact around it (the
// function is also Blueprint-callable). Outside ExecuteImpact, the subject comes from the shake's own cue parameters.
const void* c551_cue_subject(const void* params, const char* where) {
    return g_c551_impact_target != nullptr ? g_c551_impact_target : c551_impact_subject(nullptr, params, where);
}

void detour_c551_cue_shake(const void* notify, const void* params) {
    if (!c551_skip_feedback(notify, c551_cue_subject(params, "PlayScreenShake outside ExecuteImpact"), "screen shake")) {
        g_orig_c551_cue_shake(notify, params);
    }
}

void detour_c551_cue_feedback(const void* notify, const void* params) {
    if (!c551_skip_feedback(notify, c551_cue_subject(params, "PlayForceFeedback outside ExecuteImpact"),
                            "force feedback")) {
        g_orig_c551_cue_feedback(notify, params);
    }
}

void detour_c551_cue_screen(const void* notify, void* actor, const void* params) {
    if (!c551_skip_feedback(notify, actor != nullptr ? actor : g_c551_impact_target, "screen effect")) {
        g_orig_c551_cue_screen(notify, actor, params);
    }
}

// Card 551 b378: run c551b376a still shook the local camera on a remote landing (Shake_Generic_Fall_C, card099 caller
// exe+0x5251E77, inside USTCommonUtilityLibrary::StartCameraShake). That function shakes GetPlayerController(context,
// 0), this machine's own player, and has eight native callers plus the Blueprint thunk, so the gate sits on the
// function itself: a WorldContext that is an actor whose pawn (itself or its Owner) is a trooper or mech not
// controlled by this machine's player is skipped. A non-actor context (component, anim instance, widget) runs
// unchanged; a Fall or Land shake that runs logs its context class and caller, so a missed path shows in one run.
C551StartShakeFn g_orig_c551_start_shake = nullptr;
namespace {
constexpr uint32_t kC551RunLines = 64; // Fall/Land shakes that ran, per run (b382: was 24)
uint32_t g_c551_run_lines = 0;          // game thread

bool c551_land_shake(const void* shake_class) {
    const auto name = object_name(shake_class); // keep the buffer alive while strstr reads it
    return strstr(name.text, "Fall") != nullptr || strstr(name.text, "Land") != nullptr;
}
} // namespace

void* detour_c551_start_shake(void* context, void* shake_class_ref, float scale) {
    const void* const shake_class = shake_class_ref != nullptr ? read_ptr(shake_class_ref, 0) : nullptr;
    const bool actor = context != nullptr && is_a(context, g_actor_class());
    if (actor && c551_remote_player(context)) {
        if (note_event_cap(g_c551_lines, kC551Lines, "card551 skipped shake notify")) {
            log::line("card551: %s t=%llu skipped StartCameraShake %s for %s (%s), caller exe+0x%llX", side_text(),
                      static_cast<unsigned long long>(GetTickCount64()), object_name(shake_class).text,
                      object_name(context).text, class_name(context).text, exe_rva(_ReturnAddress()));
        }
        return nullptr;
    }
    if (shake_class != nullptr && g_c551_run_lines < kC551RunLines && c551_land_shake(shake_class)) {
        ++g_c551_run_lines;
        log::line("card551: %s t=%llu ran StartCameraShake %s scale %.2f for %s (%s, %s), caller exe+0x%llX",
                  side_text(), static_cast<unsigned long long>(GetTickCount64()), object_name(shake_class).text,
                  static_cast<double>(scale), object_name(context).text, class_name(context).text,
                  actor ? "actor" : "not an actor", exe_rva(_ReturnAddress()));
    }
    // Card 582: a level script's shake (the camera manager detour has no context); the volume column carries the scale.
    c582_level_row("StartCameraShake", context, shake_class, scale);
    return g_orig_c551_start_shake(context, shake_class_ref, scale);
}

// Card 491: score rows have their own budget, as the shakes do (card 454): sound rows spend the shared one before a
// late objective's score cascade (BrainBug_SO2 on C4M2).
namespace {
bool c99_score_line_ok() {
    // b227 (c245b226a: EggDestroyed spent the budget before the BrainBug_SO2 gap): a score call inside the card 491
    // ObjectiveComplete window always logs.
    if (g_c491_window != 0) {
        return true;
    }
    if (g_c99.score_lines >= kC99ScoreLines) {
        return false;
    }
    ++g_c99.score_lines;
    return true;
}
} // namespace

// Card 491 b260: both AddFedScore hooks are always installed (score_c475.cpp, the resupply section); the card 099
// and card 491 diag rows log under UBWCOOP_C99_LOG only.
void detour_c99_score_text(void* subsystem, uint64_t tag, void* text, int32_t amount) {
    if (g_c99_log && c99_score_line_ok()) {
        log::line("card099: %s AddFedScore(tag, text, %d) %s, caller exe+0x%llX", side_text(), amount,
                  fname_text(&tag).text, exe_rva(_ReturnAddress()));
    }
    // b232: the resupply state before and after each ResupplyUpgraded (the step the client paid twice). b238 (card 491
    // next diag): before and after every award, so ResupplyLevel and the thresholds show around each one.
    if (g_c491_inner_text) { // card 491 b262: AddFedScore(tag)'s own inner call, the award its detour handles
        g_c491_inner_text = false;
        g_orig_c99_score_text(subsystem, tag, text, amount);
        return;
    }
    if (c579_drop_award(tag, amount)) {
        return;
    }
    char what[96];
    c491_resupply_arm(subsystem);
    if (c491_resupply_drop(subsystem, tag)) {
        return;
    }
    snprintf(what, sizeof(what), "before %s", fname_text(&tag).text);
    c491_pawn_rows(subsystem, what);
    g_orig_c99_score_text(subsystem, tag, text, amount);
    snprintf(what, sizeof(what), "after %s", fname_text(&tag).text);
    c491_pawn_rows(subsystem, what);
    c491_resupply_paid(subsystem, tag);
}

void detour_c99_score(void* subsystem, uint64_t tag) {
    if (g_c99_log && c99_score_line_ok()) {
        log::line("card099: %s AddFedScore(tag) %s, caller exe+0x%llX, Blueprint %s", side_text(), fname_text(&tag).text,
                  exe_rva(_ReturnAddress()), g_c322_bp_caller != nullptr ? class_name(g_c322_bp_caller).text : "none");
    }
    // b232: AddFedScore(tag) gets the card 491 rows too (it also broadcasts OnFedScoreGained)
    if (c579_drop_award(tag, 0)) {
        return;
    }
    char what[96];
    c491_resupply_arm(subsystem);
    if (c491_resupply_drop(subsystem, tag)) {
        return;
    }
    snprintf(what, sizeof(what), "before %s (tag only)", fname_text(&tag).text); // b238: every award
    c491_pawn_rows(subsystem, what);
    g_c491_inner_text = true; // its first AddFedScoreText is this award (b262); a Blueprint's award inside the
    g_orig_c99_score(subsystem, tag); // broadcast is a new one and goes through the detours
    g_c491_inner_text = false;
    snprintf(what, sizeof(what), "after %s (tag only)", fname_text(&tag).text);
    c491_pawn_rows(subsystem, what);
    c491_resupply_paid(subsystem, tag);
}

C582ThreatFn g_orig_c582_threat = nullptr;
C582ExecFn g_orig_c582_exec_threat = nullptr;

// Card 582 G2: FFrame::Object is the calling Blueprint object while the native ModifyTargetThreat runs.
void detour_c582_exec_threat(void* context, void* stack, void* result) {
    const void* const prev = g_c582_threat.caller;
    g_c582_threat.caller =
        stack != nullptr ? *reinterpret_cast<void* const*>(static_cast<uint8_t*>(stack) + kFrameObject) : nullptr;
    g_orig_c582_exec_threat(context, stack, result);
    g_c582_threat.caller = prev;
}

// Card 582 G2: the original runs; on the host, an EM class's call for the host's pawn (roster slot 0) is repeated with
// the same delta for each other player pawn. A pawn change of the host gives +delta then -delta, so a client pawn's
// net stays the marker's +2000. Calls from native code (no Blueprint caller) and other actors pass unchanged.
void detour_c582_threat(void* component, void* actor, float delta) {
    g_orig_c582_threat(component, actor, delta);
    if (g_client_session || actor == nullptr || !c582_threat_class(g_c582_threat.caller)) {
        return;
    }
    const C43Roster r = c43_roster(component);
    if (r.p[0].pawn != actor) {
        return;
    }
    ++g_c582_threat.seen;
    for (int32_t s = 1; s < kC43Max; ++s) {
        // The native adds only to an existing target entry; a pawn the enemy has no entry for is tried again on the
        // next marker call instead of being noted as done.
        const bool entry = g_c103_find != nullptr && g_c103_find(component, r.p[s].pawn) != nullptr;
        if (r.p[s].pawn != nullptr && entry && c582_pair_new(component, r.p[s].pawn)) {
            g_orig_c582_threat(component, r.p[s].pawn, delta);
            ++g_c582_threat.mirrored;
        }
    }
}

// Card 582 G4: BP_PickupTracker binds its WeaponEquipped/WeaponUnequipped handlers on GetSTPlayerCharacter(self)'s
// weapon inventory only (Enable Delegates; Disable Delegates removes them). Pickups run on the host for every taker
// (card 254), so a client's nuke equip or unequip on its server copy never reached the tracker and the pickup was
// never respawned after the client used it. Host: each Enable/Disable Delegates body (card 507's local script hook)
// is repeated inside an acting scope for every roster pawn, and a tracker left enabled is bound again to a pawn
// that changed (a respawn). Binding is AddUnique and the handlers check the tracker's own Active flag, so a repeat
// or a stale bind is harmless.
namespace {
constexpr int32_t kC582Trackers = 16;
constexpr ULONGLONG kC582TrackerMs = 500; // rebind check period: a respawned pawn is bound within 0.5 s
struct C582Tracker {
    C31Weak actor;
    C31Weak pawn[kC43Max]; // the pawn each slot is bound for; a different live pawn needs a bind
    bool enabled;
};
struct C582Trackers {
    C582Tracker t[kC582Trackers];
    int32_t n;
    const void* fn_enable;
    const void* fn_disable;
    bool fanning; // our own ProcessEvent of a tracker function is running
    ULONGLONG due;
    uint32_t binds;
    uint32_t unbinds;
    uint32_t full;
};
C582Trackers g_c582_trk{}; // game thread
uint64_t g_c582_enable_name = 0;
uint64_t g_c582_disable_name = 0;

C582Tracker* c582_tracker_of(const void* actor) {
    for (int32_t i = 0; i < g_c582_trk.n; ++i) {
        if (c31_weak_get(g_c582_trk.t[i].actor) == actor) {
            return &g_c582_trk.t[i];
        }
    }
    return nullptr;
}

// Runs the tracker's parameterless function `fn` with GetSTPlayerCharacter answering slot s's pawn.
void c582_tracker_call(void* actor, const void* fn, int32_t s, const void* pawn) {
    const C102Scope saved = s == 0 ? c102_enter(nullptr, kC102None) : c102_enter(pawn, kC102Place);
    alignas(16) uint8_t parms[16] = {};
    g_c582_trk.fanning = true;
    g_orig_process_event(actor, const_cast<void*>(fn), parms);
    g_c582_trk.fanning = false;
    c102_leave(saved);
}

// Binds every roster pawn the tracker is not yet bound for.
void c582_tracker_bind(C582Tracker& t, void* actor, const char* why) {
    const C43Roster r = c43_roster(actor);
    for (int32_t s = 0; s < kC43Max; ++s) {
        void* const pawn = r.p[s].pawn;
        if (pawn == nullptr || c31_weak_get(t.pawn[s]) == pawn) {
            continue;
        }
        c582_tracker_call(actor, g_c582_trk.fn_enable, s, pawn);
        c31_weak_set(t.pawn[s], pawn);
        ++g_c582_trk.binds;
        log::line("card582: host t=%llu tracker %s bound for slot %d pawn %s (%s)",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(actor).text, s,
                  object_name(pawn).text, why);
    }
}

// Removes the handlers from every pawn still bound (the Blueprint's own call removed only player 0's).
void c582_tracker_unbind(C582Tracker& t, void* actor) {
    const C43Roster r = c43_roster(actor);
    for (int32_t s = 0; s < kC43Max; ++s) {
        void* const pawn = r.p[s].pawn;
        if (pawn != nullptr && c31_weak_get(t.pawn[s]) == pawn) {
            c582_tracker_call(actor, g_c582_trk.fn_disable, s, pawn);
            ++g_c582_trk.unbinds;
        }
        t.pawn[s] = C31Weak{};
    }
    log::line("card582: host t=%llu tracker %s delegates disabled for every pawn",
              static_cast<unsigned long long>(GetTickCount64()), object_name(actor).text);
}
} // namespace

// Host, from the BeginPlay detour (before the original runs Enable Delegates): notes a BP_PickupTracker and its two
// functions.
void c582_tracker_begin_play(void* actor) {
    if (g_client_session || !c73_class_is(read_ptr(actor, kObjectClass), "BP_PickupTracker_C")) {
        return;
    }
    if (g_c582_trk.n >= kC582Trackers) {
        ++g_c582_trk.full;
        log::line("WARN card582: host tracker table full (%d), %s not fanned out", kC582Trackers,
                  object_name(actor).text);
        return;
    }
    const uint64_t en = fname_cached(g_c582_enable_name, L"Enable Delegates");
    const uint64_t dis = fname_cached(g_c582_disable_name, L"Disable Delegates");
    const void* const fn_en = en != 0 ? g_find_function(actor, en) : nullptr;
    const void* const fn_dis = dis != 0 ? g_find_function(actor, dis) : nullptr;
    if (fn_en == nullptr || fn_dis == nullptr) {
        log::line("WARN card582: host tracker %s has no Enable/Disable Delegates (%p %p), not fanned out",
                  object_name(actor).text, fn_en, fn_dis);
        return;
    }
    g_c582_trk.fn_enable = fn_en;
    g_c582_trk.fn_disable = fn_dis;
    C582Tracker& t = g_c582_trk.t[g_c582_trk.n++];
    t = C582Tracker{};
    c31_weak_set(t.actor, actor);
}

// Host, card 507's local script detour after the original body: a tracker's own Enable or Disable Delegates call.
void c582_tracker_local(void* context, const void* node) {
    if (g_c582_trk.fanning || (node != g_c582_trk.fn_enable && node != g_c582_trk.fn_disable)) {
        return;
    }
    C582Tracker* const t = c582_tracker_of(context);
    if (t == nullptr) {
        return;
    }
    if (node == g_c582_trk.fn_disable) {
        t->enabled = false;
        c582_tracker_unbind(*t, context);
        return;
    }
    t->enabled = true;
    for (C31Weak& w : t->pawn) {
        w = C31Weak{}; // the Blueprint bound one pawn; every pawn gets the (AddUnique) bind again
    }
    c582_tracker_bind(*t, context, "Enable Delegates");
}

bool c582_tracker_armed() {
    return g_c582_trk.n > 0;
}

// Host, each tick (death.cpp c50_step): an enabled tracker is bound for a pawn that changed (a respawn, a join).
void c582_tracker_step(ULONGLONG now) {
    if (g_c582_trk.n == 0 || now < g_c582_trk.due) {
        return;
    }
    g_c582_trk.due = now + kC582TrackerMs;
    for (int32_t i = 0; i < g_c582_trk.n; ++i) {
        void* const actor = c31_weak_get(g_c582_trk.t[i].actor);
        if (actor != nullptr && g_c582_trk.t[i].enabled) {
            c582_tracker_bind(g_c582_trk.t[i], actor, "pawn changed");
        }
    }
}

// Both sides, at map arm, after c99_map_totals printed the tally: the table is cleared (trackers and their
// functions are per map).
void c582_tracker_map() {
    g_c582_trk = C582Trackers{};
}

// Card 582: the pickup tracker tally, print only (c99_map_totals: map arm and quit).
void c582_tracker_totals(const char* when) {
    if (!g_client_session && (g_c582_trk.n > 0 || g_c99_log)) {
        log::line("card582: %s: %d pickup trackers, %u pawn binds, %u pawn unbinds", when, g_c582_trk.n,
                  g_c582_trk.binds, g_c582_trk.unbinds);
    }
    if (g_c582_trk.full > 0) {
        log::line("WARN card582: %s: tracker table full (%d), %u trackers not fanned out", when, kC582Trackers,
                  g_c582_trk.full);
    }
}

// Card 582 G3 b451 diagnostic (g_c99_log only): GA_HumanMelee's DealDamage and TraceForTarget both run
// SphereTraceSingleForObjects(self, PCM location, PCM location + PCM forward * DamageOriginDistanceFromCam, 64, ...)
// with PCM = GameplayStatics::GetPlayerCameraManager(self, 0) (exec thunk, then GetPlayerController(ctx, 0)). A row
// gives the trace and its hit, the player 0 answer for the ability (what the thunk's GetPlayerController returns,
// through the card 099 gate), that controller's camera manager, the ability's outer chain, and both pawns' cameras.
C582TraceFn g_orig_c582_trace = nullptr;

namespace {
constexpr uint32_t kC582TraceLines = 24; // rows per map
constexpr size_t kC582HitFlags = 0xAD;   // FHitResult: bit 0 bBlockingHit
constexpr size_t kC582HitActor = 0xB8;   // FHitResult: HitObjectHandle (weak actor)
uint32_t g_c582_trace_lines = 0;         // game thread

bool c582_melee_ctx(const void* ctx) {
    return ctx != nullptr && strncmp(class_name(ctx).text, "GA_HumanMelee", 13) == 0;
}

void c582_trace_row(const void* ctx, const Vec3& start, const Vec3& end, float radius, bool hit, const void* out) {
    const auto* const h = static_cast<const uint8_t*>(out);
    const void* const actor = h != nullptr ? g_weak_get(h + kC582HitActor) : nullptr;
    const void* const outer = read_ptr(ctx, kObjectOuter);
    const void* const copy = c99_copy_of(ctx);
    void* const p0 = detour_c99_get_pc(ctx, 0); // the thunk's own answer for this context
    const void* const pcm = p0 != nullptr ? read_ptr(p0, kPcCameraManager) : nullptr;
    const Vec3 pcm_at = pcm != nullptr ? read_vec3(read_ptr(pcm, kActorRootComponent), kComponentTranslation) : Vec3{};
    const C43Roster r = c43_roster(ctx);
    const Vec3 host_cam = camera_location(r.p[0].pawn);
    const Vec3 copy_cam = camera_location(copy);
    log::line("card582: %s t=%llu melee trace %s: start (%.0f, %.0f, %.0f) end (%.0f, %.0f, %.0f) radius %.0f, hit %d "
              "(blocking %d) actor %s; outer %s (%s), copy %s; player 0 answer %s %p, camera manager %s at (%.0f, %.0f, "
              "%.0f); host camera (%.0f, %.0f, %.0f), copy camera (%.0f, %.0f, %.0f)",
              g_client_session ? "client" : "host", static_cast<unsigned long long>(GetTickCount64()),
              object_name(ctx).text, start.x, start.y, start.z, end.x, end.y, end.z, radius, hit ? 1 : 0,
              h != nullptr ? (h[kC582HitFlags] & 1) : -1, actor != nullptr ? object_name(actor).text : "none",
              object_name(outer).text, class_name(outer).text, copy != nullptr ? object_name(copy).text : "none",
              p0 != nullptr ? object_name(p0).text : "null", p0, pcm != nullptr ? object_name(pcm).text : "null",
              pcm_at.x, pcm_at.y, pcm_at.z, host_cam.x, host_cam.y, host_cam.z, copy_cam.x, copy_cam.y, copy_cam.z);
}
} // namespace

void c582_trace_map_reset() {
    g_c582_trace_lines = 0;
}

bool detour_c582_trace(const void* ctx, Vec3 start, Vec3 end, float radius, const void* types, bool complex,
                       const void* ignore, uint8_t draw, void* out_hit, bool ignore_self, C582Color color,
                       C582Color hit_color, float draw_time) {
    const bool hit = g_orig_c582_trace(ctx, start, end, radius, types, complex, ignore, draw, out_hit, ignore_self,
                                       color, hit_color, draw_time);
    if (g_c99_log && g_c582_trace_lines < kC582TraceLines && c582_melee_ctx(ctx)) {
        ++g_c582_trace_lines;
        c582_trace_row(ctx, start, end, radius, hit, out_hit);
    }
    return hit;
}

} // namespace ubw::hooks
