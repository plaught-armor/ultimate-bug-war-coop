// Interact: the down player's interact focus trace (card 081), the client's Use relayed to its server
// copy's Interact (card 050 step 3a) and placed pickups disabled on the client too (card 056).

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

TraceInteractableFn g_orig_trace_interactable = nullptr;

namespace {

constexpr const wchar_t* kC130ReviveText = L"Revive";
constexpr size_t kC130PromptText = 0x48; // FText in the interface subobject (GetInteractionPrompt_Implementation)

// This machine's revive interactable. The world is compared only; the actor is read only while its world is current.
struct C130Revive {
    const void* world;
    void* actor;
    void* iface;
    C31Weak weak; // the actor's liveness (card 319 run 1 class fix: no object_index on a maybe-freed pointer)
    const void* shown_for; // the teammate the prompt was last shown for (compared only), for the log
};
C130Revive g_c130r{}; // game thread

// The interactable for this world, spawned on first use with the revive prompt set. Null when it cannot be made.
void* c130_revive_actor(void* world, const void* mate) {
    if (g_c130r.world == world && g_c130r.actor != nullptr && c31_weak_get(g_c130r.weak) == g_c130r.actor) {
        return g_c130r.iface;
    }
    g_c130r = C130Revive{world, nullptr, nullptr, {}, nullptr};
    if (g_c54_limited_class == nullptr || g_c50_interactable_class == nullptr || g_get_interface_address == nullptr) {
        return nullptr;
    }
    alignas(16) uint8_t params[kSpawnParamsSize] = {};
    g_spawn_params_ctor(params);
    params[kSpawnCollisionOverride] = kSpawnAdjustAlways;
    const Vec3 at = actor_location(mate);
    const Vec3 rot{};
    void* const actor = g_world_spawn_actor(world, g_c54_limited_class(), &at, &rot, params);
    void* const iface = actor != nullptr ? g_get_interface_address(actor, g_c50_interactable_class()) : nullptr;
    if (iface == nullptr) {
        log::line("WARN card130: %s revive interactable: %s", side(), actor == nullptr ? "no spawn" : "no interface");
        return nullptr;
    }
    auto* const prompt = static_cast<uint8_t*>(iface) + kC130PromptText;
    g_ftext_dtor(prompt);
    g_ftext_from_string(prompt, kC130ReviveText);
    g_c130r = C130Revive{world, actor, iface, {}, nullptr};
    c31_weak_set(g_c130r.weak, actor);
    log::line("card130: %s revive interactable %s spawned (interface +0x%llX), prompt \"%ls\"", side(),
              object_name(actor).text,
              static_cast<unsigned long long>(static_cast<uint8_t*>(iface) - static_cast<uint8_t*>(actor)),
              kC130ReviveText);
    return iface;
}

} // namespace

// Card 130 item 11: the revive interactable's interface while the own pawn `own` is in revive range of a downed
// teammate, else null. Logged when the teammate shown for changes.
void* c130_revive_iface(const void* own) {
    const void* const mate = c43_revivable_near(own);
    void* const iface = mate != nullptr ? c130_revive_actor(g_actor_get_world(own), mate) : nullptr;
    if (mate != g_c130r.shown_for) {
        log::line("card130: %s t=%llu revive prompt %s for %s", side(), static_cast<unsigned long long>(GetTickCount64()),
                  mate != nullptr ? (iface != nullptr ? "shown" : "WARN not shown (no interactable)") : "hidden",
                  object_name(mate != nullptr ? mate : g_c130r.shown_for).text);
        g_c130r.shown_for = mate;
    }
    return iface;
}

bool c130_is_revive_target(const void* target) {
    return target != nullptr && target == g_c130r.actor;
}

// Card 081: TickComponent's only trace. While this machine's own pawn is down it finds nothing, so TickComponent
// clears the focus target (+0x100) and broadcasts the change as when the player walks away: the "E ENTER" prompt
// hides. After the stand-up the next trace finds the target again and the prompt comes back natively.
// Returns the IADInteractableInterface pointer TickComponent stores at +0x100, not a UObject.
void* detour_trace_interactable(void* interactor) {
    void* const found = g_orig_trace_interactable(interactor);
    const void* const owner = read_ptr(interactor, kComponentOwner);
    const uint32_t s = sprint_side(owner);
    // Card 563: a client's trooper left unpossessed in a seat (Door Gunner, mech, missile) has remote role simulated
    // on the host and reads host-own. Only the pawn a local PC drives is this machine's own; otherwise the two
    // troopers flipped g_c81.trace_hidden every frame and the downed host's trace came back live.
    const void* const ctrl = owner != nullptr ? read_ptr(owner, kPawnController) : nullptr;
    if ((s != kSideHostOwn && s != kSideClientOwn) || ctrl == nullptr || !is_local_pc(ctrl)) {
        c456_note_trace(interactor, found, found);
        return found;
    }
    const bool hide = c43_is_down(owner);
    if (hide != g_c81.trace_hidden && note_event_cap(g_c81.trace_lines, kC81Lines, "card081 trace")) {
        log::line("card081: %s t=%llu own interact trace %s: %s is %s, trace found %s", side(),
                  static_cast<unsigned long long>(GetTickCount64()), hide ? "hidden" : "restored",
                  object_name(owner).text, hide ? c43_life_text(owner) : "Up", object_name(iface_object(found)).text);
    }
    g_c81.trace_hidden = hide;
    void* const revive = hide ? nullptr : c130_revive_iface(owner);
    void* const used = hide ? nullptr : (revive != nullptr ? revive : found);
    c456_note_trace(interactor, found, used); // card 456: an interrupt in this tick names it
    return used;
}

// "name x y z" (card 133: the client target's location).
void c50_receive_use_name(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    const int32_t len = chan_text(s, text, sizeof(text));
    Vec3 at{};
    static_assert(sizeof(g_c50.use_name) == 130, "card 230: the %129s width below");
    const int got = len > 0 ? sscanf_s(text, "%129s %lf %lf %lf", g_c50.use_name,
                                       static_cast<unsigned>(sizeof(g_c50.use_name)), &at.x, &at.y, &at.z)
                            : 0;
    g_c50.use_pc_id = object_index(pc);
    g_c50.use_name_ms = GetTickCount64();
    g_c50.use_at = at;
    g_c50.use_at_ok = got == 4;
    if (c50_line() || got != 4) {
        log::line("%scard050: host t=%llu UseName from %s: %s (%d chars)%s", got == 4 ? "" : "WARN ",
                  static_cast<unsigned long long>(g_c50.use_name_ms), object_name(pc).text,
                  len > 0 ? text : "(invalid)", len, got == 4 ? "" : ": not \"name x y z\", ignored");
    }
    if (got != 4) {
        g_c50.use_name[0] = '\0';
    }
}

namespace {
constexpr int32_t kC426Tail = 9; // "@" and 8 hex digits

// Card 426: FNV-1a of the actor's level's name (the level's outer, as the card 132 digest names levels).
uint32_t c50_level_hash(const void* actor) {
    const void* const level = actor != nullptr ? read_ptr(actor, kObjectOuter) : nullptr;
    const ObjectName n = object_name(level != nullptr ? read_ptr(level, kObjectOuter) : nullptr);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(n.text) && n.text[i] != '\0'; ++i) {
        h = (h ^ static_cast<uint8_t>(n.text[i])) * 16777619u;
    }
    return h;
}

// Card 426: "<name>@<8 hex>" into name and level hash: true. False: not a level key (a plain name, or a malformed
// tail, which the caller matches whole, so it finds no actor); name is then not written.
bool c50_key_split(const char* key, char (&name)[kChanNameMax + 1], uint32_t& level) {
    const char* const at = strchr(key, '@');
    const size_t len = at != nullptr ? static_cast<size_t>(at - key) : 0;
    char* end = nullptr;
    const unsigned long h = at != nullptr && strlen(at) == kC426Tail ? strtoul(at + 1, &end, 16) : 0;
    if (at == nullptr || end != at + kC426Tail || len == 0 || len > kChanNameMax) {
        return false;
    }
    memcpy(name, key, len);
    name[len] = '\0';
    level = static_cast<uint32_t>(h);
    return true;
}
} // namespace

int32_t c50_level_key(const void* actor, char* out, int32_t cap) {
    const int32_t len = cap > kC426Tail + 1 ? c50_name(actor, out, cap - kC426Tail) : -1;
    if (len <= 0) {
        return -1;
    }
    snprintf(out + len, kC426Tail + 1, "@%08x", c50_level_hash(actor));
    return len + kC426Tail;
}

// The placed actor of one of the n classes with this name, or null (card 056: also the client, for AADPickupBase).
// Card 426: a level key ("<name>@<hash>") matches only the copy in that level.
void* c50_find_placed(const void* world, const StaticClassFn* const* cls, int32_t n, const char* key) {
    static C23List list;
    char name[kChanNameMax + 1];
    uint32_t level = 0;
    const bool by_level = c50_key_split(key, name, level);
    C50NameRef ref = c50_name_ref(by_level ? name : key, kChanNameMax); // card 405
    for (int32_t c = 0; world != nullptr && c < n; ++c) {
        c23_collect(world, nullptr, (*cls[c])(), list);
        for (uint32_t i = 0; i < list.count; ++i) {
            void* const a = list.e[i].actor;
            if (c50_name_match(a, ref) && c50_placed(a) && (!by_level || c50_level_hash(a) == level)) { // 404, 405
                return a;
            }
        }
    }
    return nullptr;
}

// Card 236: the placed actor of any class with this name, or null. Walks every level's actor list directly (no
// C23List, so no kC23Max cut); bounded by kMaxLevels x kMaxLevelActors. Callers keep it off the per-tick path.
// Card 605: `name` may be a level key (c50_level_key): then only the copy in that level matches (NiagaraActor_<n>
// names repeat across sublevels).
void* c50_find_placed_any(const void* world, const char* key) {
    char name[kChanNameMax + 1];
    uint32_t level = 0;
    const bool by_level = c50_key_split(key, name, level);
    C50NameRef ref = c50_name_ref(by_level ? name : key, kChanNameMax); // card 405
    const auto* const levels = static_cast<void* const*>(world != nullptr ? read_ptr(world, kWorldLevels) : nullptr);
    const int32_t num_levels = levels != nullptr ? read_i32(world, kWorldLevels + kArrayNum) : 0;
    for (int32_t l = 0; l < num_levels && l < kMaxLevels; ++l) {
        auto* const* const actors =
            levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = actors != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; a < num && a < kMaxLevelActors; ++a) {
            if (actors[a] != nullptr && c50_name_match(actors[a], ref) && c50_placed(actors[a]) &&
                (!by_level || c50_level_hash(actors[a]) == level)) { // cards 404, 405, 605
                return actors[a];
            }
        }
    }
    return nullptr;
}

// Card 605, client diagnostic for a card604 miss: the first actor of any level with this name, placed or not, as
// "<level> flags 0x<flags>", or "none in <n> levels". Off the per-tick path (one call per missed host row).
void c50_name_probe(const void* world, const char* key, char* out, size_t cap) {
    char name[kChanNameMax + 1];
    uint32_t level = 0;
    C50NameRef ref = c50_name_ref(c50_key_split(key, name, level) ? name : key, kChanNameMax);
    const auto* const levels = static_cast<void* const*>(world != nullptr ? read_ptr(world, kWorldLevels) : nullptr);
    const int32_t num_levels = levels != nullptr ? read_i32(world, kWorldLevels + kArrayNum) : 0;
    for (int32_t l = 0; l < num_levels && l < kMaxLevels; ++l) {
        auto* const* const actors =
            levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = actors != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; a < num && a < kMaxLevelActors; ++a) {
            if (actors[a] != nullptr && c50_name_match(actors[a], ref)) {
                snprintf(out, cap, "%s flags 0x%x", object_name(read_ptr(levels[l], kObjectOuter)).text,
                         static_cast<uint32_t>(read_i32(actors[a], kC50ObjectFlags)));
                return;
            }
        }
    }
    snprintf(out, cap, "none in %d levels", num_levels);
}

// Card 559, host: the live actor of the class named `cls` nearest `at`, or null. A full level walk, bounded by
// kMaxLevels x kMaxLevelActors; runs once per relayed Use press only.
static void* c559_nearest_of_class(const void* world, const char* cls, const Vec3& at) {
    const auto* const levels = static_cast<void* const*>(world != nullptr ? read_ptr(world, kWorldLevels) : nullptr);
    const int32_t num_levels = levels != nullptr ? read_i32(world, kWorldLevels + kArrayNum) : 0;
    void* best = nullptr;
    double best_d = 0.0;
    for (int32_t l = 0; l < num_levels && l < kMaxLevels; ++l) {
        auto* const* const actors =
            levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = actors != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; a < num && a < kMaxLevelActors; ++a) {
            if (actors[a] == nullptr || strcmp(class_name(actors[a]).text, cls) != 0) {
                continue;
            }
            const double d = vec_dist(actor_location(actors[a]), at);
            if (best == nullptr || d < best_d) {
                best = actors[a];
                best_d = d;
            }
        }
    }
    return best;
}

// Host: the copy's own trace target when no fresh name came, else the client's target: the placed actor with the
// client's name nearest the client's location (card 133), refused above kC50UseRangeUu or outside kC54Relays.
// Logs both names and the distance.
void* c50_pick_target(void* pawn, void* interactor) {
    void* const host_t = const_cast<void*>(interact_target(interactor));
    char host_name[kChanNameMax + 1];
    c50_name(host_t, host_name, sizeof(host_name));
    const void* const pc = read_ptr(pawn, kPawnController);
    const ULONGLONG now = GetTickCount64();
    const bool fresh = g_c50.use_name[0] != '\0' && g_c50.use_pc_id == object_index(pc) &&
                       now - g_c50.use_name_ms <= kC50UseNameMs;
    if (!fresh) {
        log::line("card050: host t=%llu relay for %s: copy target %s (%s), no client name (not a placed pickup)",
                  static_cast<unsigned long long>(now), object_name(pawn).text,
                  host_name[0] != '\0' ? host_name : object_name(host_t).text, class_name(host_t).text);
        return host_t;
    }
    // Card 306: "#id" names a relayed runtime copy by the host's id (c50_send_use_name).
    // Card 559: "$class" names a client-local pickup by class; the host's own nearest live actor of that class.
    const char lead = g_c50.use_name[0];
    void* const found = lead == '#'   ? c104_relayed_by_id(static_cast<uint32_t>(strtoul(g_c50.use_name + 1, nullptr, 10)))
                        : lead == '$' ? c559_nearest_of_class(g_actor_get_world(pawn), g_c50.use_name + 1, g_c50.use_at)
                                      : c95_find_nearest(g_actor_get_world(pawn), g_c50.use_name, g_c50.use_at);
    if (found == nullptr && g_c50.use_name[0] == '#') {
        log::line("WARN card306: host t=%llu relay for %s: client copy %s is not a live relayed actor; the copy's own "
                  "target %s is used", static_cast<unsigned long long>(now), object_name(pawn).text, g_c50.use_name,
                  object_name(host_t).text);
        g_c50.use_name[0] = '\0';
        return host_t;
    }
    // Card 133: a child actor's wire location is its parent's (c95_wire_target).
    const double dist = found != nullptr ? vec_dist(actor_location(c95_anchor(found)), g_c50.use_at) : -1.0;
    const bool relayable = found != nullptr && c54_relay_row(found) != nullptr;
    const bool in_range = found != nullptr && dist <= kC50UseRangeUu;
    void* const t = relayable && in_range ? found : nullptr;
    const bool match = t != nullptr && t == host_t;
    ++(match ? g_c50.matches : g_c50.mismatches);
    log::line("%scard050: host t=%llu relay for %s: copy target before %s, client target %s at (%.0f, %.0f, %.0f), "
              "found %.0f uu from it: %s",
              t != nullptr ? "" : "WARN ", static_cast<unsigned long long>(now), object_name(pawn).text,
              host_t != nullptr ? host_name : "(none)", g_c50.use_name, g_c50.use_at.x, g_c50.use_at.y,
              g_c50.use_at.z, dist,
              t == nullptr ? (found == nullptr ? (lead == '$' ? "refused, no live actor of the client's class (card 559)"
                                                              : "refused, no placed actor has the client's name")
                              : !relayable     ? "refused, the named actor is not a card 054 relay class"
                                               : "refused, farther than 500 uu from the client's location")
              : match ? "match, the copy's own target is used"
                      : "mismatch, the client's target found by name and location is used");
    g_c50.use_name[0] = '\0';
    return t;
}

C54Track g_c54_track{nullptr, {}, nullptr, 0, 0}; // game thread

// Host: the target set as the copy interactor's +0x100 (its interface address), then Interact.
void c50_interact(void* pawn, void* interactor, void* target, const C54Relay* row) {
    void* const iface = g_get_interface_address(target, g_c50_interactable_class());
    if (iface == nullptr) {
        log::line("WARN card050: host relay skipped: %s has no IADInteractableInterface", object_name(target).text);
        return;
    }
    if (c133_is_timed(iface)) {
        log::line("card133: host t=%llu %s Use press on %s (%s, row %s): timed, card 054 hold not started; the client's "
                  "finished hold (HoldDone) is the event",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(pawn).text, object_name(target).text,
                  class_name(target).text, row->label);
        return; // relayed_id stays unset: no release is relayed either
    }
    void* const inventory = const_cast<void*>(c24_inventory(pawn)); // card 196: MI weapons or bug forms
    static char before[kChanTextMax + 1];
    static char after[kChanTextMax + 1];
    c50_text(inventory, before, sizeof(before));
    const int32_t enabled = c54_switch_enabled(row, iface);
    const void* const was = read_ptr(interactor, kInteractorTarget);
    memcpy(static_cast<uint8_t*>(interactor) + kInteractorTarget, &iface, sizeof(iface));
    c102_interact(interactor);
    g_c50.relayed_id = object_index(pawn);
    ++g_c50.relays;
    c50_text(inventory, after, sizeof(after));
    log::line("card054: host t=%llu relayed Use #%u: %s Interact on %s (%s, row %s), +0x100 %s; switch enabled %d -> "
              "%d; weapons before %s, after %s",
              static_cast<unsigned long long>(GetTickCount64()), g_c50.relays, object_name(pawn).text,
              object_name(target).text, class_name(target).text, row->label,
              was == iface ? "already set" : "set by card 050", enabled, c54_switch_enabled(row, iface), before, after);
    if (row->kind == C54Kind::Zipline) {
        c54_track(pawn, "host server copy after the relayed zipline Use");
    }
}

} // namespace ubw::hooks
