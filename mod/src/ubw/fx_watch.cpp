// Card 130 (items 5, 9, 15a): proof that a Niagara effect the mod spawns on the receiving side is shown, not only
// spawned. "spawned" in a log is a non-null component; this checks the component kFxWatchFirstMs and kFxWatchLateMs
// later: not complete early, active, visible, not hidden in game, where it is and how far from the own pawn. A
// component whose object index changed (destroyed and reused) is logged as gone. Log only, no state changes.

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

NiagaraBoolFn g_niagara_is_complete = nullptr;
NiagaraStateFn g_niagara_requested_state = nullptr;

namespace {

constexpr const char* kFxActive[] = {"bIsActive", nullptr};
constexpr const char* kFxVisible[] = {"bVisible", nullptr};
constexpr const char* kFxHidden[] = {"bHiddenInGame", nullptr};

struct FxWatchState {
    FxWatch row[kFxWatchMax];
    uint32_t count;
    uint32_t peak;
    bool full_warned;
};
FxWatchState g_fx;

Vec3 fx_own_pawn_at(void* engine) {
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    const void* const pawn = pc != nullptr ? c23_own_pawn(pc) : nullptr;
    const void* const root = pawn != nullptr ? read_ptr(pawn, kActorRootComponent) : nullptr;
    return root != nullptr ? read_vec3(root, kComponentTranslation) : Vec3{};
}

// One check of a watched component; late is the second (last) one.
void fx_check(const FxWatch& w, ULONGLONG now, const Vec3& own, bool late) {
    const unsigned long long age = static_cast<unsigned long long>(now - w.born_ms);
    const void* const comp = c31_weak_get(w.comp);
    if (comp == nullptr) {
        log::line("WARN card130: %s fx %s +%llu ms: component gone (destroyed before the check)", side(), w.what, age);
        return;
    }
    const int complete = g_niagara_is_complete != nullptr ? (g_niagara_is_complete(comp) ? 1 : 0) : -1;
    const int state = g_niagara_requested_state != nullptr ? g_niagara_requested_state(comp) : -1;
    const int active = c29_bool(comp, kFxActive);
    const int visible = c29_bool(comp, kFxVisible);
    const int hidden = c29_bool(comp, kFxHidden);
    const Vec3 at = read_vec3(comp, kComponentTranslation);
    const double dx = at.x - own.x, dy = at.y - own.y, dz = at.z - own.z;
    // Early: shown means running, active, visible and not hidden. Late: complete is the normal end of a burst, and a
    // completed pooled component is made invisible (hb65: 31 of 69 PumpShotgun_Hit_Concrete bursts ended inside 1 s,
    // complete 1, requested state 3, visible 0), so a late check passes on complete alone. Card 136 run 6: the early
    // check passes on complete too (hb66: 5 grenade explosions had ended inside 100 ms, complete 1 at +100 ms).
    const bool running = visible == 1 && hidden == 0;
    const bool shown = late ? (running || complete == 1) : ((running && active == 1) || complete == 1);
    if (shown && !pl_allow(PlClass::FxWatch)) {
        return; // card 155: shown results are rate limited; every WARN is written
    }
    log::line("%scard130: %s fx %s +%llu ms: complete %d, requested state %d, active %d, visible %d, hidden %d, "
              "at (%.1f, %.1f, %.1f), %.0f uu from the own pawn%s",
              shown ? "" : "WARN ", side(), w.what, age, complete, state, active, visible, hidden, at.x, at.y, at.z,
              sqrt(dx * dx + dy * dy + dz * dz), shown ? "" : ": not shown");
}

} // namespace

void fx_watch_add(const void* comp, const char* what) {
    if (comp == nullptr) {
        return; // the spawn's own log line already says none
    }
    if (g_fx.count >= kFxWatchMax) {
        if (!g_fx.full_warned) {
            g_fx.full_warned = true;
            log::line("WARN card130: %s fx watch full (%u): further effects are not checked", side(), kFxWatchMax);
        }
        return;
    }
    FxWatch& w = g_fx.row[g_fx.count++];
    w = FxWatch{{}, GetTickCount64(), 0, {}};
    c31_weak_set(w.comp, const_cast<void*>(comp));
    snprintf(w.what, sizeof(w.what), "%s", what);
    if (g_fx.count > g_fx.peak) {
        g_fx.peak = g_fx.count;
        log::line("card130: %s fx watch peak %u of %u", side(), g_fx.peak, kFxWatchMax);
    }
}

void fx_watch_step(void* engine) {
    if (g_fx.count == 0) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    const Vec3 own = fx_own_pawn_at(engine);
    uint32_t kept = 0;
    for (uint32_t i = 0; i < g_fx.count; ++i) {
        FxWatch& w = g_fx.row[i];
        const ULONGLONG due = w.born_ms + (w.checks == 0 ? kFxWatchFirstMs : kFxWatchLateMs);
        if (now >= due) {
            fx_check(w, now, own, w.checks == 1);
            ++w.checks;
        }
        if (w.checks < 2) {
            g_fx.row[kept++] = w;
        }
    }
    g_fx.count = kept;
}

// why: "the map change", or "the respawn in place" (an in-place load destroys the watched components with no map
// load; their checks then read "component gone", a false WARN in every such run since hb57).
void fx_watch_map(const char* why) {
    if (g_fx.count != 0) {
        log::line("card130: %s fx watch: %u checks dropped at %s", side(), g_fx.count, why);
    }
    g_fx.count = 0;
    g_fx.full_warned = false;
}

} // namespace ubw::hooks
