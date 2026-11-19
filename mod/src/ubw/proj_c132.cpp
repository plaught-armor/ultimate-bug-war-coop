// Card 132, bug projectile check: every pooled projectile launch on each side, at UADProjectilePooler::SpawnProjectile
// (0x04A363F0; every pooled projectile goes through it: the FireProjectile task's Activate, the Blueprint
// execSpawnProjectile and USTCombatUtilityLibrary::SpawnPooledProjectile). Each launch is logged (capped per map) with
// its class, shooter, location, direction and time, and counted per init data class per side (every pooled
// projectile is an ADProjectile; the init data class names the round: BP_ProjectileInitData_AIMoritaRound_C and so on);
// the counts print with the card 132 digest round and reset. A launch made by the client's card 074/076 relay copy (c74_receive) counts as a copy, apart
// from the side's own launches, so a class with own launches on the host and none (own or copy) on the client is a gap.
// The shooter is the FireProjectile task's (detour_fire_projectile), else the pooler's context object: SpawnPooledProjectile
// 0x05250240 passes its UObject* context (rcx) through as SpawnProjectile's third argument (+0x5C: mov r8, rsi).
// Lines:
//   card132: <side> t=<ms> launch #<n> <own|copy> <projectile class> init data <class> shooter <name> (<class>)
//            context <name> (<class>) at (x, y, z) P <pitch> Y <yaw>, caller exe+0x<rva>
//   card132: <side> round <r> launches (init data class:own/copy): <class>:<own>/<copy> ...      (WARN when the table or the log cap overflowed)

#include "ubw/hooks_internal.h"

#include <stdio.h>
#include <string.h>

namespace ubw::hooks {

namespace {

constexpr int32_t kC132ProjClasses = 32;   // projectile classes tracked per round
constexpr int32_t kC132ProjTasks = 32;     // FireProjectile task -> shooter ring

// A row keeps the init data's name, copied at the launch: the init data object can be gone by the round line
// (hb28: the client's round line read a freed object's FName and crashed).
constexpr size_t kC132ProjName = 96;
struct C132ProjRow {
    char name[kC132ProjName]; // the launch's init data name
    uint32_t own;    // launches by this side's game code
    uint32_t copy;   // launches made by the card 074/076 relay copy (client)
};

struct C132ProjTask {
    const void* task;  // identity only (compared, never read)
    C31Weak shooter;   // weak: a task address outlives its shooter across a checkpoint load (c156-r1 host crash)
};

struct C132Proj {
    C132ProjRow row[kC132ProjClasses];
    int32_t n;
    uint32_t overflow;  // launches of a class past the table this round
    uint32_t launches;  // this map
    uint32_t lines;     // this map
    C132ProjTask task[kC132ProjTasks];
    uint32_t task_next;
    const void* shooter; // the shooter of the FireProjectile task now activating, or null
    bool copying;        // c74_receive is spawning a relay copy
};

C132Proj g_c132p{};

// The newest ring entry for the task: a freed task's address is reused by a later one, so an older entry with the
// same address names an older shooter. The weak handle gives null when that shooter is gone.
const void* c132_proj_shooter_of(const void* task) {
    const C132Proj& p = g_c132p;
    for (uint32_t k = 1; task != nullptr && k <= static_cast<uint32_t>(kC132ProjTasks) && k <= p.task_next; ++k) {
        const C132ProjTask& t = p.task[(p.task_next - k) % kC132ProjTasks];
        if (t.task == task) {
            return c31_weak_get(t.shooter);
        }
    }
    return nullptr;
}

void c132_proj_count(const void* init_data, bool copy) {
    C132Proj& p = g_c132p;
    const auto name = object_name(init_data);
    for (int32_t i = 0; i < p.n; ++i) {
        if (strcmp(p.row[i].name, name.text) == 0) {
            ++(copy ? p.row[i].copy : p.row[i].own);
            return;
        }
    }
    if (p.n >= kC132ProjClasses) {
        ++p.overflow;
        return;
    }
    C132ProjRow& r = p.row[p.n];
    snprintf(r.name, sizeof(r.name), "%s", name.text);
    r.own = copy ? 0u : 1u;
    r.copy = copy ? 1u : 0u;
    ++p.n;
}

} // namespace

// Both sides, from detour_fire_projectile: the task's shooter, for its launch line.
void c132_proj_task(const void* task, const void* shooter) {
    C132Proj& p = g_c132p;
    C132ProjTask& t = p.task[p.task_next % kC132ProjTasks];
    t.task = task;
    c31_weak_set(t.shooter, const_cast<void*>(shooter));
    ++p.task_next;
}

// Both sides, around the FireProjectile task's Activate (detour_c74_activate).
void c132_proj_activating(const void* task) {
    g_c132p.shooter = c132_proj_shooter_of(task);
}

// Client, around c74_receive's spawn call: the launch is a relay copy.
void c132_proj_copying(bool on) {
    g_c132p.copying = on;
}

bool c132_proj_is_copying() { // card 417: the launch in progress is a host relay copy
    return g_c132p.copying;
}

// User playtest (hb52): the client saw no fireball where the host's nuke hit. Log only: for kC132FxWindowMs after a
// NukeLauncher launch (own or relay copy), every effect spawn, gameplay cue and projectile end on that side, so the
// host call that makes the explosion, and what the client's copy does at its end, are known before a relay is built.
namespace {

constexpr ULONGLONG kC132FxWindowMs = 6000;
constexpr uint32_t kC132FxLines = 64;
ULONGLONG g_c132_fx_until = 0; // game thread only
const void* g_c132_fx_proj = nullptr;
uint32_t g_c132_fx_lines = 0;

void c132_fx_window(const void* init_data, const void* proj) {
    if (strstr(object_name(init_data).text, "NukeLauncher") == nullptr) {
        return;
    }
    g_c132_fx_until = GetTickCount64() + kC132FxWindowMs;
    g_c132_fx_proj = proj;
}

} // namespace

// Card 132 step 2e (UBWCOOP_C132_FX=1): every effect spawn on this side, from every chokepoint, for the
// host-against-client pairing (tools/fx_pair.py). Capped per asset (kC132AuditPerAsset lines each) and per map.
// Line: card132: <side> t=<ms> fx audit: <what> <asset> at (x, y, z) owner <name> (<class>) frames <chain>
namespace {

constexpr int32_t kC132AuditAssets = 256;    // assets with a line count
constexpr uint32_t kC132AuditPerAsset = 200000; // lines per asset per map (card 132 4: every spawn, not 16)
constexpr uint32_t kC132AuditLines = 400000;    // lines per map (a bound, not a filter; hb54 had about 3000)
constexpr size_t kC132AuditName = 96;
struct C132AuditAsset {
    char name[kC132AuditName];
    uint32_t n;
};
struct C132Audit {
    C132AuditAsset asset[kC132AuditAssets];
    int32_t n;
    uint32_t lines;
    uint32_t over; // spawns of an asset past the table
};
C132Audit g_c132_audit{}; // game thread only

// Counts a spawn of asset; true while the asset has lines left.
bool c132_audit_note(const char* asset) {
    C132Audit& a = g_c132_audit;
    for (int32_t i = 0; i < a.n; ++i) {
        if (strcmp(a.asset[i].name, asset) == 0) {
            return ++a.asset[i].n <= kC132AuditPerAsset;
        }
    }
    if (a.n >= kC132AuditAssets) {
        return note_event_cap(a.over, 1, "card132 fx audit asset table");
    }
    C132AuditAsset& r = a.asset[a.n++];
    snprintf(r.name, sizeof(r.name), "%s", asset);
    r.n = 1;
    return true;
}

void c132_fx_audit_line(const char* what, const char* asset, const Vec3& p, const void* actor) {
    if (!c132_audit_note(asset) ||
        !note_event_cap(g_c132_audit.lines, kC132AuditLines, "card132 fx audit lines")) {
        return;
    }
    const C252Timer timer(C252Class::Audit); // card 252 step 1b
    char chain[160];
    caller_chain(chain, sizeof(chain), 8); // card 132 4b: deep enough to name the bark callers
    log::line("card132: %s t=%llu fx audit: %s %s at (%.0f, %.0f, %.0f) owner %s (%s) frames %s", side_text(),
              static_cast<unsigned long long>(GetTickCount64()), what, asset, p.x, p.y, p.z, object_name(actor).text,
              class_name(actor).text, chain);
}

} // namespace

void c132_fx_probe(const char* what, const char* asset, const Vec3* at, const void* actor, const void* ra) {
    const Vec3 p = at != nullptr ? *at : (actor != nullptr ? actor_location(actor) : Vec3{});
    if (g_c132_fx_log) {
        c132_fx_audit_line(what, asset, p, actor);
    }
    if (GetTickCount64() >= g_c132_fx_until ||
        !note_event_cap(g_c132_fx_lines, kC132FxLines, "card132 nuke fx probe lines")) {
        return;
    }
    log::line("card132: %s t=%llu nuke fx probe: %s %s at (%.0f, %.0f, %.0f)%s, caller exe+0x%llX", side_text(),
              static_cast<unsigned long long>(GetTickCount64()), what, asset, p.x, p.y, p.z,
              actor != nullptr && actor == g_c132_fx_proj ? " (the nuke projectile)" : "", exe_rva(ra));
}

// The audit's passthrough detours on the spawn chokepoints the card 074 detours do not cover. Every hooked function
// takes integer or pointer arguments in its first four slots (FVector and FRotator go by reference), so a uint64_t
// passthrough of twelve slots forwards each call unchanged (slots past the real arguments are the caller's stack).
namespace {

// 13 slots: SpawnSoundAttached has 13 arguments (bAutoDestroy is the 13th); the others ignore the extra slots.
using C132PassFn = void* (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                             uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
constexpr int32_t kC132PassSlots = 13;
constexpr size_t kFxParamsSystem = 0x08;   // FFXSystemSpawnParameters::SystemTemplate
constexpr size_t kFxParamsLocation = 0x10; // FFXSystemSpawnParameters::Location
constexpr size_t kFxParamsAttach = 0x58;   // FFXSystemSpawnParameters::AttachToComponent

enum C132PassHook : int32_t {
    kPassNiagaraParams,
    kPassNiagaraAttachedParams,
    kPassNiagaraAttached,
    kPassNiagaraAttachedScale,
    kPassEmitter,
    kPassEmitterAttached,
    kPassEmitterAttachedScale,
    kPassSoundAttached,
    kPassHooks
};
C132PassFn g_c132_pass[kPassHooks] = {};

const void* c132_owner_of(const void* comp) {
    return comp != nullptr ? read_ptr(comp, kComponentOwner) : nullptr;
}

// The card 132 lines are off: skip the name lookups (these detours are always installed since card 136).
bool c132_audit_off() {
    return !g_c132_fx_log && GetTickCount64() >= g_c132_fx_until;
}

// An attached spawn: the component's world location and its owner.
void c132_audit_attached(const char* what, uint64_t asset, uint64_t comp) {
    if (c132_audit_off()) {
        return;
    }
    const void* const c = reinterpret_cast<const void*>(comp);
    const Vec3 at = c != nullptr ? read_vec3(c, kComponentTranslation) : Vec3{};
    c132_fx_probe(what, object_name(reinterpret_cast<const void*>(asset)).text, &at, c132_owner_of(c), nullptr);
}

void c132_audit_params(const char* what, uint64_t params, bool attached) {
    const void* const p = reinterpret_cast<const void*>(params);
    if (p == nullptr || c132_audit_off()) {
        return;
    }
    const void* const comp = attached ? read_ptr(p, kFxParamsAttach) : nullptr;
    const Vec3 at = comp != nullptr ? read_vec3(comp, kComponentTranslation) : read_vec3(p, kFxParamsLocation);
    c132_fx_probe(what, object_name(read_ptr(p, kFxParamsSystem)).text, &at, c132_owner_of(comp), nullptr);
}

#define C132_PASS_ARGS uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6, uint64_t a7, \
                       uint64_t a8, uint64_t a9, uint64_t a10, uint64_t a11, uint64_t a12, uint64_t a13
#define C132_PASS_CALL(h) g_c132_pass[h](a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13)

constexpr size_t kFxParamsContext = 0x00;  // FFXSystemSpawnParameters::WorldContextObject
constexpr size_t kFxParamsRotation = 0x28; // FFXSystemSpawnParameters::Rotation
constexpr size_t kFxParamsScale = 0x40;    // FFXSystemSpawnParameters::Scale
constexpr size_t kFxParamsSocket = 0x60;   // FFXSystemSpawnParameters::AttachPointName
// The switches, read by SpawnSystemAtLocationWithParams (0x04840900): 0x69 goes to CreateNiagaraSystem as
// bAutoDestroy, 0x6A gates the Activate call, 0x6B is the pooling method, 0x6C bPreCullCheck, 0x6D bIsPlayerEffect.
constexpr size_t kFxParamsLocationType = 0x68; // EAttachLocation (attached entry only)
constexpr size_t kFxParamsAutoDestroy = 0x69;
constexpr size_t kFxParamsAutoActivate = 0x6A;

Vec3 c136_vec(uint64_t slot, Vec3 fallback) {
    const Vec3* const v = reinterpret_cast<const Vec3*>(slot);
    return v != nullptr ? *v : fallback;
}

// A float argument in a stack slot: its low 32 bits.
float c136_slot_float(uint64_t slot) {
    const uint32_t bits = static_cast<uint32_t>(slot);
    float f = 0.0f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

// Card 136 capture point 2 or 3 or 1, from a FFXSystemSpawnParameters (the WithParams entries).
C136Call c136_from_params(C136Point point, uint64_t params) {
    C136Call c{point};
    const void* const p = reinterpret_cast<const void*>(params);
    if (p == nullptr) {
        return c;
    }
    const uint8_t* const b = static_cast<const uint8_t*>(p);
    c.asset = read_ptr(p, kFxParamsSystem);
    c.context = read_ptr(p, kFxParamsContext);
    c.at = read_vec3(p, kFxParamsLocation);
    c.rot = read_vec3(p, kFxParamsRotation);
    c.scale = read_vec3(p, kFxParamsScale);
    c.flags = static_cast<uint8_t>((b[kFxParamsAutoDestroy] != 0 ? kC136FlagAutoDestroy : 0) |
                                   (b[kFxParamsAutoActivate] != 0 ? kC136FlagAutoActivate : 0));
    if (point == C136Point::NiagaraAttachedParams) {
        c.attach = read_ptr(p, kFxParamsAttach);
        memcpy(&c.socket, b + kFxParamsSocket, sizeof(c.socket));
        const uint8_t loc = static_cast<uint8_t>(b[kFxParamsLocationType] & kC136FlagLocationMask);
        c.flags = static_cast<uint8_t>(c.flags | (loc << kC136FlagLocationShift));
    }
    return c;
}

// The attached entries: (asset a1, component a2, socket FName a3, location* a4, rotation* a5[, scale* a6], ...).
C136Call c136_from_attached(C136Point point, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5,
                            uint64_t scale) {
    C136Call c{point};
    c.flags = kC136FlagAutoDestroy | kC136FlagAutoActivate; // the attached entries' defaults (c136_attached_flags)
    c.asset = reinterpret_cast<const void*>(a1);
    c.attach = reinterpret_cast<const void*>(a2);
    c.socket = a3;
    c.at = c136_vec(a4, Vec3{});
    c.rot = c136_vec(a5, Vec3{});
    c.scale = c136_vec(scale, Vec3{1.0, 1.0, 1.0});
    return c;
}

// The attached spawn's own switches: EAttachLocation, bAutoDestroy and bAutoActivate in their slots (the low byte).
C136Call c136_attached_flags(C136Call c, uint64_t location, uint64_t destroy, uint64_t activate) {
    const uint8_t loc = static_cast<uint8_t>(location & kC136FlagLocationMask);
    c.flags = static_cast<uint8_t>(((destroy & 0xFF) != 0 ? kC136FlagAutoDestroy : 0) |
                                   ((activate & 0xFF) != 0 ? kC136FlagAutoActivate : 0) |
                                   (loc << kC136FlagLocationShift));
    return c;
}

// Calls the original between c136_fx_enter and c136_fx_leave; the result is the spawned component (or null). On the
// client a blocked effect (c136_blocked: the host sends it) returns null without the original.
#define C136_PASS(h, call)                                                                                           \
    do {                                                                                                             \
        const C136Call c136 = (call);                                                                                \
        const uint32_t id = c136_fx_enter(c136);                                                                     \
        if (c136_blocked()) {                                                                                        \
            c136_fx_leave(c136.point, id, nullptr);                                                                  \
            return nullptr;                                                                                          \
        }                                                                                                            \
        void* const made = C132_PASS_CALL(h);                                                                        \
        c136_fx_leave(c136.point, id, made);                                                                         \
        return made;                                                                                                 \
    } while (false)

// The attached spawns return a component their callers use at once without a null check (hb63: the WarriorBug's
// NS_Compute_DamageAndRadius, exe+0x4BBA724 sets a variable on the result). On the client a blocked attached spawn
// runs the original with its bAutoActivate slot cleared: the caller gets a real, never activated component (it dies
// with its parent; a later Activate of it is blocked as point 6), and the host's row plays the visible effect.
#define C136_PASS_INACTIVE(h, call, activate)                                                                         \
    do {                                                                                                             \
        const C136Call c136 = (call);                                                                                \
        const uint32_t id = c136_fx_enter(c136);                                                                     \
        if (c136_blocked()) {                                                                                        \
            (activate) = 0;                                                                                          \
        }                                                                                                            \
        void* const made = C132_PASS_CALL(h);                                                                        \
        c136_fx_leave(c136.point, id, made);                                                                         \
        return made;                                                                                                 \
    } while (false)

void* detour_c132_niagara_params(C132_PASS_ARGS) {
    c132_audit_params("SpawnSystemAtLocationWithParams", a1, false);
    C136_PASS(kPassNiagaraParams, c136_from_params(C136Point::NiagaraParams, a1));
}
void* detour_c132_niagara_attached_params(C132_PASS_ARGS) {
    c132_audit_params("SpawnSystemAttachedWithParams", a1, true);
    const C136Call c136 = c136_from_params(C136Point::NiagaraAttachedParams, a1);
    const uint32_t id = c136_fx_enter(c136);
    // Blocked on the client: the original with the params' bAutoActivate cleared, as C136_PASS_INACTIVE.
    uint8_t* const params = reinterpret_cast<uint8_t*>(a1);
    const bool quiet = params != nullptr && c136_blocked();
    const uint8_t activate = params != nullptr ? params[kFxParamsAutoActivate] : 0;
    if (quiet) {
        params[kFxParamsAutoActivate] = 0;
    }
    void* const made = C132_PASS_CALL(kPassNiagaraAttachedParams);
    if (quiet) {
        params[kFxParamsAutoActivate] = activate;
    }
    c136_fx_leave(c136.point, id, made);
    return made;
}
void* detour_c132_niagara_attached(C132_PASS_ARGS) {
    c132_audit_attached("SpawnSystemAttached", a1, a2);
    C136_PASS_INACTIVE(kPassNiagaraAttached,
                       c136_attached_flags(c136_from_attached(C136Point::NiagaraAttached, a1, a2, a3, a4, a5, 0), a6,
                                           a7, a8),
                       a8);
}
// SpawnSystemAttached with Scale: a6 Scale, a7 EAttachLocation, a8 bAutoDestroy, a9 ENCPoolMethod, a10 bAutoActivate
// (the overload without Scale: a6 EAttachLocation, a7 bAutoDestroy, a8 bAutoActivate; tools/symbols.txt).
void* detour_c132_niagara_attached_scale(C132_PASS_ARGS) {
    c132_audit_attached("SpawnSystemAttachedScale", a1, a2);
    C136_PASS_INACTIVE(kPassNiagaraAttachedScale,
                       c136_attached_flags(c136_from_attached(C136Point::NiagaraAttachedScale, a1, a2, a3, a4, a5, a6),
                                           a7, a8, a10),
                       a10);
}
C136Call c136_from_emitter(uint64_t world, uint64_t templ, uint64_t at, uint64_t rot, uint64_t scale) {
    C136Call c{C136Point::Emitter};
    c.context = reinterpret_cast<const void*>(world);
    c.asset = reinterpret_cast<const void*>(templ);
    c.at = c136_vec(at, Vec3{});
    c.rot = c136_vec(rot, Vec3{});
    c.scale = c136_vec(scale, Vec3{1.0, 1.0, 1.0});
    return c;
}
void* detour_c132_emitter(C132_PASS_ARGS) {
    if (!c132_audit_off()) {
        const Vec3* const at = reinterpret_cast<const Vec3*>(a3);
        c132_fx_probe("InternalSpawnEmitterAtLocation", object_name(reinterpret_cast<const void*>(a2)).text, at,
                      nullptr, nullptr);
    }
    C136_PASS(kPassEmitter, c136_from_emitter(a1, a2, a3, a4, a5));
}
void* detour_c132_emitter_attached(C132_PASS_ARGS) {
    c132_audit_attached("SpawnEmitterAttached", a1, a2);
    // SpawnEmitterAttached: a6 EAttachLocation, a7 bAutoDestroy, a8 EPSCPoolMethod, a9 bAutoActivate.
    C136_PASS_INACTIVE(kPassEmitterAttached,
                       c136_attached_flags(c136_from_attached(C136Point::EmitterAttached, a1, a2, a3, a4, a5, 0), a6,
                                           a7, a9),
                       a9);
}
void* detour_c132_emitter_attached_scale(C132_PASS_ARGS) {
    c132_audit_attached("SpawnEmitterAttachedScale", a1, a2);
    // SpawnEmitterAttached with Scale: a6 Scale, a7 EAttachLocation, a8 bAutoDestroy, a9 EPSCPoolMethod, a10
    // bAutoActivate.
    C136_PASS_INACTIVE(kPassEmitterAttachedScale,
                       c136_attached_flags(c136_from_attached(C136Point::EmitterAttachedScale, a1, a2, a3, a4, a5, a6),
                                           a7, a8, a10),
                       a10);
}
// SpawnSoundAttached: a6 EAttachLocation, a7 bStopWhenAttachedToDestroyed, a8 to a10 volume, pitch, start, a11
// attenuation, a12 concurrency, a13 bAutoDestroy.
C136Call c136_from_sound_attached(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6,
                                  uint64_t a7, uint64_t a8, uint64_t a9, uint64_t a10, uint64_t a11, uint64_t a12,
                                  uint64_t a13) {
    C136Call c = c136_attached_flags(c136_from_attached(C136Point::SoundAttached, a1, a2, a3, a4, a5, 0), a6, a13, 1);
    c.flags = static_cast<uint8_t>(c.flags | ((a7 & 0xFF) != 0 ? kC136FlagStopWithParent : 0));
    c.look[0] = c136_slot_float(a8);
    c.look[1] = c136_slot_float(a9);
    c.look[2] = c136_slot_float(a10);
    c.atten = reinterpret_cast<const void*>(a11);
    c.conc = reinterpret_cast<const void*>(a12);
    return c;
}
void* detour_c132_sound_attached(C132_PASS_ARGS) {
    c132_audit_attached("SpawnSoundAttached", a1, a2);
    // C136_PASS written out: card 162 sets the attenuation slot once the call is known not blocked.
    const C136Call c136 = c136_from_sound_attached(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13);
    const uint32_t id = c136_fx_enter(c136);
    if (c136_blocked()) {
        c136_fx_leave(c136.point, id, nullptr);
        return nullptr;
    }
    // Card 162: a remote player's sound with no attenuation plays with its fallback (the row keeps the call's own).
    a11 = reinterpret_cast<uint64_t>(c162_call_atten(c136));
    void* const made = C132_PASS_CALL(kPassSoundAttached);
    c136_fx_leave(c136.point, id, made);
    return made;
}

#undef C136_PASS
#undef C136_PASS_INACTIVE
#undef C132_PASS_CALL
#undef C132_PASS_ARGS

constexpr C132PassHook kC136PassOf[static_cast<int>(C136Point::Count)] = {
    kPassHooks, kPassHooks, kPassSoundAttached, kPassHooks, kPassNiagaraAttached, kPassNiagaraAttachedScale,
    kPassNiagaraParams, kPassNiagaraAttachedParams, kPassHooks, kPassEmitterAttached, kPassEmitterAttachedScale,
    kPassHooks, kPassHooks, kPassHooks, kPassHooks, kPassHooks, kPassHooks, kPassHooks, kPassHooks,
};

} // namespace

// Card 136 step 3, client: the original of an attached point, with the 13 slots its detour would forward (null when
// the point has no pass hook or its original is not installed).
void* c136_pass_play(C136Point point, const uint64_t* slots) {
    const int32_t p = static_cast<int32_t>(point);
    if (p < 0 || p >= static_cast<int32_t>(C136Point::Count) || slots == nullptr) {
        return nullptr;
    }
    const C132PassHook h = kC136PassOf[p];
    if (h == kPassHooks || g_c132_pass[h] == nullptr) {
        return nullptr;
    }
    const uint64_t* const a = slots;
    static_assert(kC132PassSlots == 13);
    return g_c132_pass[h](a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12]);
}

// Installed always, after MH_Initialize (card 136 capture points 1, 2 and 3; the card 132 lines stay behind
// UBWCOOP_C132_FX=1).
bool c132_fx_install(uint8_t* base, const IMAGE_NT_HEADERS64* nt) {
    const HookSpec hooks[kPassHooks] = {
        {&offsets::UNiagaraFunctionLibrary_SpawnSystemAtLocationWithParams,
         reinterpret_cast<void*>(&detour_c132_niagara_params), reinterpret_cast<void**>(&g_c132_pass[0])},
        {&offsets::UNiagaraFunctionLibrary_SpawnSystemAttachedWithParams,
         reinterpret_cast<void*>(&detour_c132_niagara_attached_params), reinterpret_cast<void**>(&g_c132_pass[1])},
        {&offsets::UNiagaraFunctionLibrary_SpawnSystemAttached, reinterpret_cast<void*>(&detour_c132_niagara_attached),
         reinterpret_cast<void**>(&g_c132_pass[2])},
        {&offsets::UNiagaraFunctionLibrary_SpawnSystemAttachedScale,
         reinterpret_cast<void*>(&detour_c132_niagara_attached_scale), reinterpret_cast<void**>(&g_c132_pass[3])},
        {&offsets::UGameplayStatics_InternalSpawnEmitterAtLocation, reinterpret_cast<void*>(&detour_c132_emitter),
         reinterpret_cast<void**>(&g_c132_pass[4])},
        {&offsets::UGameplayStatics_SpawnEmitterAttached, reinterpret_cast<void*>(&detour_c132_emitter_attached),
         reinterpret_cast<void**>(&g_c132_pass[5])},
        {&offsets::UGameplayStatics_SpawnEmitterAttachedScale,
         reinterpret_cast<void*>(&detour_c132_emitter_attached_scale), reinterpret_cast<void**>(&g_c132_pass[6])},
        {&offsets::UGameplayStatics_SpawnSoundAttached, reinterpret_cast<void*>(&detour_c132_sound_attached),
         reinterpret_cast<void**>(&g_c132_pass[7])},
    };
    for (const HookSpec& h : hooks) {
        void* const target = code_address(base, nt, *h.sym);
        if (target == nullptr || !create_and_enable(target, h.detour, h.original, h.sym->name)) {
            return false;
        }
    }
    log::line("hooks: card 132 effect audit detours installed (%d)", static_cast<int>(kPassHooks));
    return true;
}

// Both sides, from detour_c74_spawn after the pooler returned.
void c132_proj_launch(const void* init_data, const double* transform, const void* context, const void* proj,
                      const void* ra) {
    C132Proj& p = g_c132p;
    c132_fx_window(init_data, proj);
    c132_proj_count(init_data, p.copying);
    ++p.launches;
    const void* shooter = p.shooter;
    if (shooter == nullptr && proj != nullptr) {
        shooter = read_ptr(proj, kActorInstigator);
    }
    const Vec3 at = read_vec3(transform, kTransformTranslation);
    const Vec3 rot = quat_pitch_yaw(transform);
    // Card 155: a player's launches are one playlog shot line per burst; the rest (AI) are rate limited, play build on.
    const bool shot_line = pl_shot(shooter, at, rot, p.copying);
    if (!p.copying) {
        // Card 157: after pl_shot, so the client notes this shot's number. Card 281: the host keeps the init data.
        c157_launch(const_cast<void*>(proj), shooter, init_data);
    }
    if (shot_line || !pl_allow(PlClass::Launch)) {
        return;
    }
    log::line("card132: %s t=%llu launch #%u %s %s init data %s shooter %s (%s) context %s (%s) at (%.1f, %.1f, %.1f) "
              "P %.2f Y %.2f, caller exe+0x%llX",
              side_text(), static_cast<unsigned long long>(GetTickCount64()), p.launches, p.copying ? "copy" : "own",
              class_name(proj).text, object_name(init_data).text, object_name(shooter).text, class_name(shooter).text,
              object_name(context).text, class_name(context).text, at.x, at.y, at.z, rot.x, rot.y, exe_rva(ra));
}

// Client, card 286: the own grenade launch released after its task's Activate is not an own launch: it leaves its
// class's own count. False when the class has no own launch counted this round.
bool c132_proj_unown(const void* init_data) {
    C132Proj& p = g_c132p;
    const auto name = object_name(init_data);
    for (int32_t i = 0; i < p.n; ++i) {
        if (strcmp(p.row[i].name, name.text) == 0 && p.row[i].own > 0) {
            --p.row[i].own; // p.launches stays: it numbers launch lines
            return true;
        }
    }
    return false;
}

// Both sides, with the card 132 digest round line: the round's launch counts per class, then reset.
void c132_proj_round(uint32_t round) {
    C132Proj& p = g_c132p;
    char text[1400];
    size_t at = 0;
    text[0] = '\0';
    for (int32_t i = 0; i < p.n && at < sizeof(text); ++i) {
        const int w = snprintf(text + at, sizeof(text) - at, "%s%s:%u/%u", i == 0 ? "" : " ",
                               p.row[i].name, p.row[i].own, p.row[i].copy);
        at += w > 0 ? static_cast<size_t>(w) : 0;
    }
    log::line("%scard132: %s round %u launches (init data class:own/copy): %s; %u past the table",
              p.overflow != 0 || at >= sizeof(text) ? "WARN " : "", side_text(), round, p.n != 0 ? text : "-",
              p.overflow);
    p.n = 0;
    p.overflow = 0;
}

// Both sides, at map load (c132_arm_map).
void c132_proj_map() {
    g_c132_audit.n = 0;
    g_c132_audit.lines = 0;
    C132Proj& p = g_c132p;
    p.launches = 0;
    p.lines = 0;
}

} // namespace ubw::hooks
