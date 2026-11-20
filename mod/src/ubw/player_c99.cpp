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
constexpr uint32_t kC99Callers = 48;       // distinct remapped callers counted per map
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
constexpr const char* kC102EntryText[kC102Entries] = {"context", "overlap", "damage", "interaction", "enemy"};
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
const void* c99_copy_of(const void* ctx) {
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
        o = owner != nullptr ? owner : read_ptr(o, kObjectOuter);
    }
    return nullptr;
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
    return pc != nullptr && is_a(pc, g_player_controller_class()) ? pc : nullptr;
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

void* detour_c99_get_pc(const void* ctx, int32_t index) {
    void* const pc = g_get_player_controller(ctx, index); // the trampoline (the hook row's original slot)
    // A client has no server copy (sprint_side: local Authority with an AutonomousProxy remote role is host only), no
    // acting scope and no enemy remap (c103_enemy_pc), so its answer is always the original: no context walk.
    if (index != 0 || ctx == nullptr || pc == nullptr || g_client_session) {
        return pc;
    }
    const unsigned long long rva = exe_rva(_ReturnAddress());
    const void* copy = c99_copy_of(ctx);
    uint8_t entry = kC102None;
    if (copy == nullptr && g_c102.acting != nullptr && !c102_excluded(rva)) {
        copy = g_c102.acting;
        entry = g_c102.entry;
    }
    void* own = copy != nullptr ? c99_copy_pc(copy) : nullptr;
    if (copy == nullptr && !c102_excluded(rva)) {
        own = c103_enemy_pc(ctx, rva); // card 103: the enemy's player; null for the host's own
        copy = own != nullptr ? read_ptr(own, kControllerPawn) : nullptr;
        entry = own != nullptr ? static_cast<uint8_t>(kC102Enemy) : entry;
    }
    if (own == nullptr || own == pc) {
        return pc;
    }
    if (g_is_in_game_thread != nullptr && g_is_in_game_thread()) {
        c99_count(ctx, copy, own, rva, entry); // the tally is game-thread state; render-thread callers skip it
    }
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
    if (g_client_session) {
        c138_note_begin_play(actor);
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
    c492_switch_press(target, read_ptr(interactor, kComponentOwner)); // card 492: a switch close is a player trigger
    g_orig_interact(interactor);
    c102_leave(saved);
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
    c103_arm_map();
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
    log::line("card099: map: player 0 remapped %u times by %u callers (%u callers not counted)", g_c99.total,
              g_c99.used, g_c99.dropped);
    for (uint32_t i = 0; i < g_c99.used; ++i) {
        log::line("card099:   caller exe+0x%llX, context %s, %u calls", g_c99.row[i].rva, g_c99.row[i].cls,
                  g_c99.row[i].count);
    }
    g_c99 = C99Callers{};
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
    void* const pc = c99_copy_pc(copy);
    char text[kAssetPathChars + 32];
    const int len = snprintf(text, sizeof(text), "%s %.3f %.3f", c74_asset_path(sound).text, volume, pitch);
    // c423b162a: a rejoining client's copy was hit 233 ms before its first Pong; card 139 holds every frame to a PC
    // until then (its join replay follows), so that refusal is expected and not a WARN.
    const C50Copy* const c = pc != nullptr ? c50_copy(object_index(pc), false) : nullptr;
    const bool not_ready = pc != nullptr && (c == nullptr || !c->ponged); // a null PC stays a WARN below
    const bool sent = pc != nullptr && !not_ready && len > 0 && static_cast<size_t>(len) < sizeof(text) &&
                      c95_send_one(pc, ChanOp::HurtSound, text);
    if (c455_line_ok()) { // the not-sent WARN shares the 60-row cap
        log::line("%scard455: host t=%llu relayed hurt sound %s to %s (%s): %s", sent || not_ready ? "" : "WARN ",
                  static_cast<unsigned long long>(GetTickCount64()), name.text, object_name(pc).text,
                  sent ? "sent" : not_ready ? "not sent: client not ready" : "not sent", text);
    }
}

// Client: ChanOp::HurtSound. Played through the original UGameplayStatics::PlaySound2D on the own pawn.
void c455_receive(void* pc, const WireString* s) {
    static char text[kChanTextMax + 1];
    static_assert(kAssetPathChars == 192, "the %191s width below");
    char path[kAssetPathChars] = {};
    float volume = 1.0f;
    float pitch = 1.0f;
    const bool parsed = chan_text(s, text, sizeof(text)) > 0 && sscanf_s(text, "%191s %f %f", path, static_cast<unsigned>(sizeof(path)), &volume, &pitch) == 3 &&
                        volume >= 0.0f && volume <= kC455MaxScale && pitch > 0.0f && pitch <= kC455MaxScale;
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
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
} // namespace

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

} // namespace ubw::hooks
