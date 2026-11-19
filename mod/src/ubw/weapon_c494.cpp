// Card 494 (diag, always on, client): the own pawn's drawn weapon state.
//
// Playtest 2026-11-18: after some client grenade throws (b231a cmd 22, b234a cmd 41; other throws fine) the client
// stands with an empty glove hand while the HUD still shows MORITA MK1 60/240. card158/card169/card324 rows show the
// FirstPersonMesh visible and not hidden, and card 323's digest reads bVisible only, so none of them sees this.
// One row per call names what the own pawn holds:
//   - the inventory's equipped weapon (name, class, actor bHidden), WeaponSystem.Blocked count (card 352);
//   - the FirstPersonMesh montage and position;
//   - each component of the equipped weapon in the pawn's attach tree (breadth first from RootComponent, as card 323):
//     bVisible, bHiddenInGame, attach parent and socket, RelativeLocation, and for a skeletal mesh its
//     VisibilityBasedAnimTickOption, bPauseAnims, bNoSkeletonUpdate, anim instance montage and position;
//   - the other weapon actors in the tree with a visible component.
// Called at each scenario shot (testrun.cpp, Op::Shot) and kC494AfterMs after each own grenade OnDoAction (card 158).
//   card494: <client|host> t=<ms> <why>: equipped <w> (<class>) hidden <0|1> blocked <n>; arms montage <m> at <s>; ...
//
// Fix (b237, client): USTGrenadeThrowGameplayAbility::StartThrowingAnimation calls ASTWeapon::HideWeapon (mesh to
// CollisionCylinder, actor hidden) and binds the throw task's OnAnimationEnded to StartEquippingWeaponAnimation, the
// one ASTWeapon::ShowWeapon of the throw. c244b236a throw 5: that end never came (no ability end row, host no cancel),
// and the weapon stayed hidden on CollisionCylinder for the rest of the run (40 rows). The animator has silent paths
// that drop a bound end without a broadcast (StartAnimationSequence with no anim instance clears it; SetActiveAnimation
// with interrupt 0 replaces the active montage, so the old montage's CallAnimationEnded no longer matches); which one
// ran is not logged. The grenade throw is the only HideWeapon caller paired with a later ShowWeapon (OnUnequip and
// SpawnWeaponInstance hide for good), so the guard covers that class: at the after-throw check, an equipped ASTWeapon
// still hidden and off the arms with no arms montage playing gets ShowWeapon (the call OnEquip makes). An arms montage
// still playing (a next throw, an equip) re-checks kC494RetryMs later, kC494Retries times.
//   card494: client t=<ms> weapon <w> hidden on <parent> after own throw, no arms montage: ShowWeapon (fix <n>)

#include "ubw/hooks_internal.h"

#include <algorithm>
#include <stdio.h>
#include <string.h>

namespace ubw::hooks {

namespace {

constexpr int32_t kC494Tree = 64;           // components walked (card 323's bound)
constexpr int32_t kC494CompsShown = 6;      // equipped weapon components per row
constexpr int32_t kC494OthersShown = 4;     // other visible weapon actors per row
constexpr uint32_t kC494Rows = 120;         // rows per session
constexpr ULONGLONG kC494AfterMs = 2000;    // the check after an own OnDoAction
constexpr ULONGLONG kC494RetryMs = 500;     // the re-check while an arms montage plays
constexpr int32_t kC494Retries = 8;         // re-checks per throw
constexpr size_t kC494FirstPersonMesh = 0x848; // ASTPlayerCharacter::FirstPersonMesh (remote_body.cpp)
constexpr const char* kC494Visible[] = {"bVisible", nullptr};
constexpr const char* kC494HiddenInGame[] = {"bHiddenInGame", nullptr};
constexpr const char* kC494Pause[] = {"bPauseAnims", nullptr};
constexpr const char* kC494NoSkel[] = {"bNoSkeletonUpdate", nullptr};

uint32_t g_c494_rows = 0;
ULONGLONG g_c494_due = 0; // the next after-throw check, 0 none
int32_t g_c494_retries = 0; // re-checks left for the last throw
uint32_t g_c494_fixes = 0;  // ShowWeapon calls this process
// Card 495 (senior, c448b241a proof): the user's "have laser gun" means in hand, not only in the inventory. A client
// door opening from closed logs the drawn weapon on the next frame and again kC495AfterMs after the door row.
constexpr ULONGLONG kC495AfterMs = 100;
constexpr int32_t kC495Rows = 2; // the next frame, then +kC495AfterMs
ULONGLONG g_c495_door_ms = 0;    // the door row's time, 0 none due
int32_t g_c495_done = 0;         // rows written for that door
char g_c495_door[64] = "";

// A reflected byte property of `object`, -1 when absent.
int32_t c494_byte(const void* object, const char* name) {
    const uint8_t* const f = prop_find_prop(read_ptr(object, kObjectClass), name);
    return f != nullptr ? static_cast<const uint8_t*>(object)[read_i32(f, kPropertyOffset)] : -1;
}

// " montage <name> at <s>" for a skeletal mesh component's anim instance; " no anim" without one.
int c494_montage(const void* mesh, char* out, size_t cap) {
    const void* const anim = mesh != nullptr && g_c169_anim_instance != nullptr ? g_c169_anim_instance(mesh) : nullptr;
    if (anim == nullptr || g_c169_active_montage == nullptr || g_c169_montage_position == nullptr) {
        return snprintf(out, cap, " no anim");
    }
    const void* const m = g_c169_active_montage(anim);
    return snprintf(out, cap, " anim %s montage %s at %.2f", class_name(anim).text,
                    m != nullptr ? object_name(m).text : "none", m != nullptr ? g_c169_montage_position(anim, m) : -1.0f);
}

// One equipped weapon component as "; <name> (<class>) vis V hig H on <parent>/<socket> rel X Y Z[ skel ...]".
int c494_comp(const void* comp, char* out, size_t cap) {
    char socket[64] = "?";
    const uint8_t* const sf = prop_find_prop(read_ptr(comp, kObjectClass), "AttachSocketName");
    if (sf != nullptr) {
        fname_into(static_cast<const uint8_t*>(comp) + read_i32(sf, kPropertyOffset), socket, sizeof(socket));
    }
    const uint8_t* const lf = prop_find_prop(read_ptr(comp, kObjectClass), "RelativeLocation");
    const Vec3 rel = lf != nullptr ? read_vec3(comp, static_cast<size_t>(read_i32(lf, kPropertyOffset))) : Vec3{0, 0, 0};
    int used = snprintf(out, cap, "; %s (%s) vis %d hig %d on %s/%s rel %.1f %.1f %.1f", object_name(comp).text,
                        class_name(comp).text, c29_bool(comp, kC494Visible), c29_bool(comp, kC494HiddenInGame),
                        object_name(c140_attach_parent(comp)).text, socket, rel.x, rel.y, rel.z);
    const int32_t tick = c494_byte(comp, "VisibilityBasedAnimTickOption");
    if (tick >= 0 && used > 0 && static_cast<size_t>(used) < cap) {
        used += snprintf(out + used, cap - used, " tick %d pause %d noskel %d", tick, c29_bool(comp, kC494Pause),
                         c29_bool(comp, kC494NoSkel));
        used += used > 0 && static_cast<size_t>(used) < cap ? c494_montage(comp, out + used, cap - used) : 0;
    }
    return used;
}

// The attach tree of `pawn`: equipped weapon components into `comps`, other weapon actors with a visible component
// into `others` (distinct). Returns the equipped component count.
int32_t c494_tree(const void* pawn, const void* equipped, const void** comps, const void** others, int32_t* n_others) {
    const void* const weapon_cls = g_c50_weapon_class != nullptr ? g_c50_weapon_class() : nullptr;
    const void* queue[kC494Tree] = {read_ptr(pawn, kActorRootComponent)};
    int32_t tail = queue[0] != nullptr ? 1 : 0;
    int32_t n = 0;
    for (int32_t head = 0; weapon_cls != nullptr && head < tail; ++head) {
        const void* const comp = queue[head];
        const uint8_t* const prop = prop_find_prop(read_ptr(comp, kObjectClass), "AttachChildren");
        const size_t off = prop != nullptr ? static_cast<size_t>(read_i32(prop, kPropertyOffset)) : 0;
        const auto* const kids = prop != nullptr ? static_cast<void* const*>(read_ptr(comp, off)) : nullptr;
        const int32_t num = kids != nullptr ? read_i32(comp, off + kArrayNum) : 0;
        for (int32_t i = 0; i < num && tail < kC494Tree; ++i) {
            queue[tail] = kids[i];
            tail += kids[i] != nullptr ? 1 : 0;
        }
        const void* const owner = read_ptr(comp, kComponentOwner);
        if (owner == pawn || owner == nullptr || !is_a(owner, weapon_cls)) {
            continue;
        }
        if (owner == equipped && n < kC494CompsShown) {
            comps[n++] = comp;
            continue;
        }
        const bool seen = std::find(others, others + *n_others, owner) != others + *n_others;
        if (owner != equipped && !seen && *n_others < kC494OthersShown && c29_bool(comp, kC494Visible) == 1) {
            others[(*n_others)++] = owner;
        }
    }
    return n;
}

// The own pawn's equipped weapon when it is an ASTWeapon, else null.
void* c494_equipped(const void* pawn) {
    void* const inventory = pawn != nullptr ? read_ptr(pawn, kMiWeaponInventory) : nullptr;
    void* const w = inventory != nullptr && g_get_equipped_weapon != nullptr ? g_get_equipped_weapon(inventory) : nullptr;
    return w != nullptr && g_c494_st_weapon_class != nullptr && is_a(w, g_c494_st_weapon_class()) ? w : nullptr;
}

// The first component of `weapon` in the attach tree of `pawn`, null when none.
const void* c494_weapon_comp(const void* pawn, const void* weapon) {
    const void* comps[kC494CompsShown] = {};
    const void* others[kC494OthersShown] = {};
    int32_t n_others = 0;
    return c494_tree(pawn, weapon, comps, others, &n_others) > 0 ? comps[0] : nullptr;
}

// Card 494 fix: after an own throw, ShowWeapon when the equipped weapon is still hidden off the arms and no arms
// montage plays.
enum class C494Check { Done, Fixed, Again };
C494Check c494_restore(void* pc) {
    void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    bool killable = false;
    void* const weapon = pawn != nullptr && !c98_dead_now(pawn, killable) ? c494_equipped(pawn) : nullptr;
    if (weapon == nullptr || g_c494_show_weapon == nullptr ||
        (static_cast<const uint8_t*>(weapon)[kActorHiddenByte] & kActorHiddenBit) == 0) {
        return C494Check::Done;
    }
    const void* const arms = read_ptr(pawn, kC494FirstPersonMesh);
    const void* const comp = c494_weapon_comp(pawn, weapon);
    const void* const parent = comp != nullptr ? c140_attach_parent(comp) : nullptr;
    if (arms == nullptr || parent == arms) {
        return C494Check::Done;
    }
    const void* const anim = g_c169_anim_instance != nullptr ? g_c169_anim_instance(arms) : nullptr;
    if (anim != nullptr && g_c169_active_montage != nullptr && g_c169_active_montage(anim) != nullptr) {
        return C494Check::Again;
    }
    ++g_c494_fixes;
    log::line("WARN card494: client t=%llu weapon %s hidden on %s after own throw, no arms montage: ShowWeapon (fix %u)",
              static_cast<unsigned long long>(GetTickCount64()), object_name(weapon).text,
              parent != nullptr ? object_name(parent).text : "none", g_c494_fixes);
    g_c494_show_weapon(weapon);
    return C494Check::Fixed;
}

} // namespace

C29SelfFn g_c494_show_weapon = nullptr;
StaticClassFn g_c494_st_weapon_class = nullptr;

// Both sides: one card494 row for the own pawn of `pc` (card 495: the host's scenario shots log their row too).
void c494_weapon_row(void* pc, const char* why) {
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    if (pawn == nullptr || g_player_mi_class == nullptr || !is_a(pawn, g_player_mi_class()) ||
        !note_event_cap(g_c494_rows, kC494Rows, "card494 rows")) {
        return;
    }
    void* const inventory = read_ptr(pawn, kMiWeaponInventory);
    const void* const equipped = inventory != nullptr && g_get_equipped_weapon != nullptr ? g_get_equipped_weapon(inventory)
                                                                                          : nullptr;
    const void* comps[kC494CompsShown] = {};
    const void* others[kC494OthersShown] = {};
    int32_t n_others = 0;
    const int32_t n = c494_tree(pawn, equipped, comps, others, &n_others);
    char text[1600];
    int used = snprintf(text, sizeof(text), "equipped %s (%s) hidden %d blocked %d; arms",
                        equipped != nullptr ? object_name(equipped).text : "none",
                        equipped != nullptr ? class_name(equipped).text : "-",
                        equipped != nullptr && (static_cast<const uint8_t*>(equipped)[kActorHiddenByte] & kActorHiddenBit) != 0
                            ? 1 : 0,
                        c352_blocked(pawn, kFirePrimary));
    const void* const arms = read_ptr(pawn, kC494FirstPersonMesh);
    for (int32_t step = 0; step < 1 + n + n_others && used > 0 && static_cast<size_t>(used) < sizeof(text); ++step) {
        char* const at = text + used;
        const size_t cap = sizeof(text) - static_cast<size_t>(used);
        used += step == 0      ? c494_montage(arms, at, cap)
                : step <= n    ? c494_comp(comps[step - 1], at, cap)
                               : snprintf(at, cap, "; other visible weapon %s (%s)", object_name(others[step - 1 - n]).text,
                                          class_name(others[step - 1 - n]).text);
    }
    log::line("%scard494: %s t=%llu %s: %s; %d equipped component(s) in the tree", n == 0 ? "WARN " : "", side(),
              static_cast<unsigned long long>(GetTickCount64()), why, text, n);
}

// Client own grenade OnDoAction (card 158): check the drawn weapon kC494AfterMs later.
void c494_after_throw() {
    g_c494_due = GetTickCount64() + kC494AfterMs;
    g_c494_retries = kC494Retries;
}

// Client, each frame: the due after-throw check.
// Client, each frame: the card 495 in-hand rows due after a door opened.
void c495_door_step(void* pc) {
    const ULONGLONG now = GetTickCount64();
    if (g_c495_door_ms == 0 || (g_c495_done > 0 && now < g_c495_door_ms + kC495AfterMs)) {
        return;
    }
    char why[128];
    snprintf(why, sizeof(why), "card495 door %s opened %llu ms ago", g_c495_door,
             static_cast<unsigned long long>(now - g_c495_door_ms));
    c494_weapon_row(pc, why);
    ++g_c495_done;
    g_c495_door_ms = g_c495_done >= kC495Rows ? 0 : g_c495_door_ms;
}

void c494_client_step(void* pc) {
    c495_door_step(pc);
    if (g_c494_due == 0 || GetTickCount64() < g_c494_due) {
        return;
    }
    const bool first = g_c494_retries == kC494Retries;
    g_c494_due = 0;
    if (first) {
        c494_weapon_row(pc, "2 s after own grenade OnDoAction");
    }
    const C494Check c = c494_restore(pc);
    if (c == C494Check::Fixed) {
        c494_weapon_row(pc, "after card 494 ShowWeapon");
    }
    if (c == C494Check::Again && g_c494_retries <= 0) {
        log::line("WARN card494: client t=%llu arms montage still playing after %d re-checks: weapon left as is",
                  static_cast<unsigned long long>(GetTickCount64()), kC494Retries);
        c494_weapon_row(pc, "card 494 re-checks spent");
    }
    g_c494_due = c == C494Check::Again && g_c494_retries > 0 ? GetTickCount64() + kC494RetryMs : 0;
    --g_c494_retries;
}

// Card 495: client door `door` went from closed to open (card 161 state row); a newer door replaces a pending one.
void c495_door_opened(const char* door) {
    if (!g_client_session) {
        return;
    }
    snprintf(g_c495_door, sizeof(g_c495_door), "%s", door);
    g_c495_door_ms = GetTickCount64();
    g_c495_done = 0;
}

} // namespace ubw::hooks
