// Card 104: ability animations on the host. USTAbilityTask_PlayAnimation::Activate (0x050D9580) finds the avatar's
// UADAnimatorBaseComponent and starts the sequence through a virtual call (vtable +0x4E8 StartAnimationSequence, or
// +0x4E0 StartAnimationSequenceFixedTime), then binds DoAction (0x050E3BC0) to the animator's +0xC0 delegate and
// AnimationEnded (0x050DBCB0) to +0xD0. The animator fires those from its montage notifies, so an animator whose mesh
// does not advance its montage never fires DoAction, and an ability that waits on it (the tactical support throw)
// stays open. Log: each start on the animator (key, static-mesh mode, mesh, VisibilityBasedAnimTickOption) and each
// DoAction / AnimationEnded with the time since the owner's last start, per owner side.

#include "ubw/hooks_internal.h"

#include "ue/config.h"

#include <stdio.h>
#include <string.h>

namespace ubw::hooks {

C104StartFn g_orig_c104_start = nullptr;
C104StartFn g_orig_c104_start_fixed = nullptr;
C104FlareFn g_orig_c104_flare[kC104FlareFns] = {};
C104FlareTimerFn g_orig_c104_flare_timer = nullptr;
C104LzFn g_orig_c104_lz = nullptr;
C104FlareFn g_orig_c104_flare_begin = nullptr; // card 249
C104UsedFn g_orig_c104_used = nullptr;
C569WrapperNotifyFn g_orig_c569_wrapper_notify = nullptr; // card 569

namespace {

constexpr uint32_t kC104Lines = 60;          // log lines per run
constexpr size_t kC104TaskActor = 0xA0;      // USTAbilityTask_PlayAnimation: the avatar actor (Activate reads it)
constexpr size_t kC104TaskKey = 0xA8;        // the animation key FName
constexpr size_t kC104AnimMesh = 0xF8;       // UADSkeletalAnimatorComponent: SetSkeletalMesh writes it
constexpr size_t kC104AnimStaticMode = 0x158; // USTSkeletalAnimatorComponent: StartAnimationSequence branches on it
constexpr size_t kC104StructSize = 0x58;      // UStruct::PropertiesSize (difficulty.cpp)
constexpr uint8_t kC104AlwaysTickPose = 1;    // EVisibilityBasedAnimTickOption::AlwaysTickPose

const char* const kC104SideText[] = {"host own", "server copy", "client own", "other"};
static_assert(sizeof(kC104SideText) / sizeof(kC104SideText[0]) == kSprintSides, "one text per side");

struct C104State {
    uint32_t lines;
    uint32_t starts[kSprintSides], actions[kSprintSides], ends[kSprintSides], forced;
    ULONGLONG start_ms[kSprintSides];
    int8_t fix; // -1 unread, 0 off, 1 on
};
C104State g_c104{0, {}, {}, {}, 0, {}, -1};

// Card 569: the AD anim notify running now (game thread), so a DoAction can name the sequence that fired it.
constexpr size_t kC569WrapperInner = 0x38;   // UADAnimNotifyWrapper: its UADAnimNotify (Notify reads it)
constexpr uint32_t kC569Lines = 48;          // DoAction source rows per run
struct C569Notify {
    const void* inner;    // UADAnimNotify, null outside a notify
    const void* sequence; // UAnimSequenceBase
    uint32_t lines, outside;
};
C569Notify g_c569{nullptr, nullptr, 0, 0};

bool c104_line() {
    return g_c104.lines < kC104Lines && ++g_c104.lines > 0;
}

uint32_t c104_side(const void* actor) {
    const uint32_t s = actor != nullptr ? sprint_side(actor) : kSideOther;
    return s < kSprintSides ? s : kSideOther;
}

// The mesh's VisibilityBasedAnimTickOption byte, or -1 when the mesh or the property is absent.
int32_t c104_tick_option(const void* mesh, uint8_t** addr) {
    const uint8_t* const prop =
        mesh != nullptr ? prop_find_prop(read_ptr(mesh, kObjectClass), "VisibilityBasedAnimTickOption") : nullptr;
    *addr = prop != nullptr ? static_cast<uint8_t*>(const_cast<void*>(mesh)) + read_i32(prop, kPropertyOffset)
                            : nullptr;
    return *addr != nullptr ? **addr : -1;
}

bool c104_fix_on() {
    if (g_c104.fix < 0) {
        g_c104.fix = ue::config::read_switch(L"UBWCOOP_C104_TICK", "card 104 server copy mesh AlwaysTickPose") ? 1 : 0;
    }
    return g_c104.fix == 1;
}

// The animator's static-mesh mode byte, or -1 when its class ends before it. The fixed-time start hooks the
// UADSkeletalAnimatorComponent base, whose objects can be smaller than the USTSkeletalAnimatorComponent field (run
// c207b152b: a host crash reading animator +0x158 across the end of its allocation).
int32_t c104_static_mode(const void* animator) {
    const void* const cls = read_ptr(animator, kObjectClass);
    const int32_t size = cls != nullptr ? read_i32(cls, kC104StructSize) : 0;
    if (size <= static_cast<int32_t>(kC104AnimStaticMode)) {
        return -1;
    }
    return *(static_cast<const uint8_t*>(animator) + kC104AnimStaticMode);
}

// One start on an animator: counted per owner side, logged with the mesh state. With UBWCOOP_C104_TICK=1, a server
// copy's mesh that does not always tick its pose is set to AlwaysTickPose first.
void c104_note_start(void* animator, uint64_t key, float value, const char* fn) {
    const void* const owner = read_ptr(animator, kComponentOwner);
    const uint32_t s = c104_side(owner);
    ++g_c104.starts[s];
    g_c104.start_ms[s] = GetTickCount64();
    if (s != kSideServerCopy && s != kSideHostOwn) {
        return;
    }
    void* const mesh = read_ptr(animator, kC104AnimMesh);
    uint8_t* opt = nullptr;
    const int32_t before = c104_tick_option(mesh, &opt);
    const bool force = s == kSideServerCopy && opt != nullptr && before > kC104AlwaysTickPose && c104_fix_on();
    if (force) {
        *opt = kC104AlwaysTickPose;
        ++g_c104.forced;
    }
    if (c104_line()) {
        log::line("card104: %s t=%llu %s %s(%s, %.2f) on %s (%s): static-mesh mode %d, mesh %s (%s) "
                  "VisibilityBasedAnimTickOption %d%s; starts %u",
                  side(), static_cast<unsigned long long>(g_c104.start_ms[s]), kC104SideText[s], fn,
                  fname_text(&key).text, static_cast<double>(value), object_name(owner).text,
                  object_name(animator).text, c104_static_mode(animator),
                  object_name(mesh).text, class_name(mesh).text, before,
                  force ? " -> 1 (AlwaysTickPose, card 104)" : "", g_c104.starts[s]);
    }
}

void c104_note_task(void* task, uint32_t* counts, const char* what) {
    const void* const actor = read_ptr(task, kC104TaskActor);
    const uint32_t s = c104_side(actor);
    ++counts[s];
    if ((s == kSideServerCopy || s == kSideHostOwn) && c104_line()) {
        const ULONGLONG now = GetTickCount64();
        log::line("card104: %s t=%llu %s %s, key %s, actor %s, %llu ms after its last start; %s count %u", side(),
                  static_cast<unsigned long long>(now), kC104SideText[s], what,
                  fname_text(static_cast<const uint8_t*>(task) + kC104TaskKey).text, object_name(actor).text,
                  static_cast<unsigned long long>(now - g_c104.start_ms[s]), what, counts[s]);
    }
}

const char* const kC104FlareText[] = {"RunLZQuery", "ActivateFlare", "ExpireFlare"};
static_assert(sizeof(kC104FlareText) / sizeof(kC104FlareText[0]) == kC104FlareFns, "one text per flare function");

void c104_note_flare(const void* flare, const char* what) {
    if (c104_line()) {
        log::line("card104: %s t=%llu flare %s %s", side(), static_cast<unsigned long long>(GetTickCount64()),
                  object_name(flare).text, what);
    }
}

constexpr int32_t kC104MaxSpecs = 64;   // specs walked per call
// Card 101: spec handles a client has activated on the host through ServerTryActivateAbility (handles come from one
// process-wide counter, so they are unique across pawns). The event path skips them: the client predicts those
// abilities and sends its own request, so a second, event-path activation on the server copy doubles them.
// Card 105: a ring of the most recent distinct handles, so a handle of any value is kept. A pawn has about 30 specs;
// the ring holds several respawns' worth, and the oldest handles (dead specs) go first.
constexpr int32_t kC101SentHandles = 256;
struct C101Sent {
    int32_t handle[kC101SentHandles];
    int32_t n;    // filled entries, up to kC101SentHandles
    int32_t next; // the slot the next new handle overwrites
};
C101Sent g_c101_sent{}; // game thread

bool c101_client_sent(int32_t handle) {
    for (int32_t i = 0; i < g_c101_sent.n; ++i) {
        if (g_c101_sent.handle[i] == handle) {
            return true;
        }
    }
    return false;
}
static_assert(sizeof(C104Policy::was) == kC104MaxSpecs, "one saved byte per walked spec");
constexpr uint8_t kC104LocalOnly = 1;   // EGameplayAbilityNetExecutionPolicy: LocalPredicted 0, LocalOnly 1
constexpr uint8_t kC104ServerOnly = 3;  // ServerOnly
int32_t g_c104_policy_off = -1;          // UGameplayAbility::NetExecutionPolicy, by reflection on first use

uint8_t* c104_policy_byte(void* ability) {
    if (g_c104_policy_off < 0 && ability != nullptr) {
        const uint8_t* const prop = prop_find_prop(read_ptr(ability, kObjectClass), "NetExecutionPolicy");
        g_c104_policy_off = prop != nullptr ? read_i32(prop, kPropertyOffset) : 0;
    }
    return ability != nullptr && g_c104_policy_off > 0 ? static_cast<uint8_t*>(ability) + g_c104_policy_off
                                                       : nullptr;
}

} // namespace

// Card 296: client-own (or any side's) task callback counts, for the spool row.
void c104_side_counts(uint32_t side, uint32_t* actions, uint32_t* ends) {
    *actions = side < kSprintSides ? g_c104.actions[side] : 0;
    *ends = side < kSprintSides ? g_c104.ends[side] : 0;
}

// Card 104: GAS starts a LocalPredicted or LocalOnly ability only where its avatar is locally controlled: from a
// gameplay event (HasNetworkAuthorityToActivateTriggeredAbility) and in InternalTryActivateAbility without a prediction
// key. The host's server copy of a client's pawn is not locally controlled, so on it the tactical support throw (an
// event-triggered ability) and the bombing run (given and activated once by the slot) never start. For one call on a
// server copy's ability system component, those specs' ability objects read ServerOnly; c104_policy_restore puts the
// bytes back. handle < 0: every spec; else the one spec with that handle.
void c104_policy_set(void* asc, int32_t handle, const char* what, C104Policy* p) {
    p->n = 0;
    const void* const owner = asc != nullptr ? read_ptr(asc, kComponentOwner) : nullptr;
    if (owner == nullptr || sprint_side(owner) != kSideServerCopy) {
        return;
    }
    const auto* const data = static_cast<const uint8_t*>(read_ptr(asc, kAscSpecs));
    const int32_t num = data != nullptr ? read_i32(asc, kAscAbilityNum) : 0;
    for (int32_t i = 0; i < num && i < kC104MaxSpecs; ++i) {
        const uint8_t* const spec = data + static_cast<size_t>(i) * kSpecBytes;
        const int32_t h = read_i32(spec, kSpecHandle);
        const bool pick = handle < 0 ? !c101_client_sent(h) : h == handle;
        uint8_t* const addr = pick ? c104_policy_byte(read_ptr(spec, kSpecAbility)) : nullptr;
        if (addr != nullptr && *addr <= kC104LocalOnly) {
            p->addr[p->n] = addr;
            p->was[p->n] = *addr;
            *addr = kC104ServerOnly;
            ++p->n;
        }
    }
    if (p->n > 0 && c104_line()) {
        log::line("card104: %s t=%llu server copy %s %s: %d spec(s) LocalPredicted/LocalOnly read ServerOnly for the "
                  "call", side(), static_cast<unsigned long long>(GetTickCount64()), object_name(owner).text, what,
                  p->n);
    }
}

void c101_note_client_sent(int32_t handle) {
    if (handle < 0 || c101_client_sent(handle)) {
        return;
    }
    g_c101_sent.handle[g_c101_sent.next] = handle;
    g_c101_sent.next = (g_c101_sent.next + 1) % kC101SentHandles;
    g_c101_sent.n = g_c101_sent.n < kC101SentHandles ? g_c101_sent.n + 1 : kC101SentHandles;
}

// Card 105 step 5: one line per successful activation on a server copy, with its path (the client's own request or a
// host-side call such as HandleGameplayEvent), so a run can count activations per client action. Host only, capped.
void c105_activation(const void* asc, int32_t handle, const void* ability, bool ok, bool client_request) {
    static uint32_t lines = 0;
    if (g_client_session || !ok || asc == nullptr) {
        return;
    }
    const void* const owner = read_ptr(asc, kComponentOwner);
    if (sprint_side(owner) != kSideServerCopy || !note_event_cap(lines, 400, "card105 activation")) {
        return;
    }
    log::line("card105: host server-copy activation handle %d ability %s path %s, client-sent %d", handle,
              class_name(ability).text, client_request ? "client request" : "host call",
              c101_client_sent(handle) ? 1 : 0);
}

void c104_policy_restore(const C104Policy& p) {
    for (int32_t i = p.n - 1; i >= 0; --i) {
        *p.addr[i] = p.was[i];
    }
}

void detour_c104_flare_run(void* flare) {
    c104_note_flare(flare, kC104FlareText[0]);
    g_orig_c104_flare[0](flare);
}

// Card 249: host. A thrown (not level-placed) flare is relayed from its BeginPlay, so the client sees it in flight
// ('M' rows), at rest and at an early destroy ('D' row), not only after ActivateFlare.
void detour_c104_flare_begin(void* flare) {
    g_orig_c104_flare_begin(flare);
    if (!g_client_session && !c50_placed(flare)) {
        c104_relay_spawn(flare, actor_location(flare));
    }
}

void detour_c104_flare_activate(void* flare) {
    c104_note_flare(flare, kC104FlareText[1]);
    g_orig_c104_flare[1](flare);
    if (!c104_is_relayed(flare)) {
        c104_relay_spawn(flare, actor_location(flare)); // host: a level-placed flare's copy goes out when it activates
    }
}

void detour_c104_flare_expire(void* flare) {
    c104_note_flare(flare, kC104FlareText[2]);
    g_orig_c104_flare[2](flare);
}

// `result` points at the by-value TSharedPtr<FEnvQueryResult>; its object's Items.Num is at +0x8 and its status at
// +0x28 (1 = success; LZQueryFinished's head tests the same int).
void detour_c104_flare_lz(void* flare, void* result) {
    const void* const query = result != nullptr ? read_ptr(result, 0) : nullptr;
    if (c104_line()) {
        char status[16] = "null";
        if (query != nullptr) {
            snprintf(status, sizeof(status), "%d", read_i32(query, kC249LzStatus));
        }
        log::line("card104: %s t=%llu flare %s LZQueryFinished status %s items %d", side(),
                  static_cast<unsigned long long>(GetTickCount64()), object_name(flare).text, status,
                  query != nullptr ? read_i32(query, kC249LzItemsNum) : -1); // card 249
    }
    g_orig_c104_lz(flare, result);
}

void detour_c104_flare_timer(void* flare, float delay) {
    if (c104_line()) {
        log::line("card104: %s t=%llu flare %s StartActivationTimer(%.2f)", side(),
                  static_cast<unsigned long long>(GetTickCount64()), object_name(flare).text,
                  static_cast<double>(delay));
    }
    g_orig_c104_flare_timer(flare, delay);
}

// USTTacticalSupportComponent::OnAbilityUsed: the slot's charge before and after.
void detour_c104_used(void* component, uint64_t tag, int32_t slot) {
    const int32_t before = g_c100_charge != nullptr ? g_c100_charge(component, slot) : -1;
    g_orig_c104_used(component, tag, slot);
    if (c104_line()) {
        log::line("card104: %s t=%llu OnAbilityUsed(%s, slot %d) on %s: charge %d -> %d", side(),
                  static_cast<unsigned long long>(GetTickCount64()), fname_text(&tag).text, slot,
                  object_name(read_ptr(component, kObjectOuter)).text, before,
                  g_c100_charge != nullptr ? g_c100_charge(component, slot) : -1);
    }
}

void detour_c104_start(void* animator, uint64_t key, float rate) {
    c104_note_start(animator, key, rate, "StartAnimationSequence");
    g_orig_c104_start(animator, key, rate);
}

void detour_c104_start_fixed(void* animator, uint64_t key, float total) {
    c104_note_start(animator, key, total, "StartAnimationSequenceFixedTime");
    g_orig_c104_start_fixed(animator, key, total);
}

// Card 569: which notify and sequence fired this task's DoAction (the animator delegate fires every bound task).
void c569_note_source(const void* task) {
    const void* const actor = read_ptr(task, kC104TaskActor);
    if (g_c569.inner == nullptr) {
        ++g_c569.outside;
    }
    // Only pawn tasks (the grenade ability's avatar). Weapon tasks (fire, pump, swap) have the weapon as actor: on the
    // client a weapon reads as Authority, so it was labelled "host own", and its rows used up the cap before a throw
    // (run c569b390b: 48 of 48 rows were Morita weapon tasks).
    if (actor == nullptr || g_c569.lines >= kC569Lines || g_player_character_class == nullptr ||
        !is_a(actor, g_player_character_class())) {
        return;
    }
    const uint32_t s = c104_side(actor);
    if (s == kSideOther) {
        return;
    }
    // Grenade tasks only. On the host the server copy's weapon tasks (MoritaMk1_Fire, MoritaMK1_Underslung_*) carry
    // the pawn as actor, so the pawn filter keeps them (run c569b392a: 48 of 48 host rows were those).
    const auto key = fname_text(static_cast<const uint8_t*>(task) + kC104TaskKey);
    if (strncmp(key.text, "Grenade", 7) != 0) {
        return;
    }
    ++g_c569.lines;
    log::line("card569: %s t=%llu %s DoAction, task key %s, actor %s: notify %s (%s) in sequence %s; %u outside a "
              "notify", side(), static_cast<unsigned long long>(GetTickCount64()), kC104SideText[s],
              key.text, object_name(actor).text,
              object_name(g_c569.inner).text, class_name(g_c569.inner).text, object_name(g_c569.sequence).text,
              g_c569.outside);
}

void detour_c569_wrapper_notify(void* wrapper, void* mesh, void* sequence) {
    const C569Notify saved = g_c569;
    g_c569.inner = wrapper != nullptr ? read_ptr(wrapper, kC569WrapperInner) : nullptr;
    g_c569.sequence = sequence;
    g_orig_c569_wrapper_notify(wrapper, mesh, sequence);
    g_c569.inner = saved.inner;
    g_c569.sequence = saved.sequence;
}

// Card 569 fix: the animator's +0xC0 DoAction delegate fires every bound task, so a weapon fire task's DoAction (on
// the host server copy: outside any notify, rows c569b392a) can release a bound grenade throw early, at the start aim.
// A grenade task's DoAction runs only inside a notify of a grenade sequence; the real one then releases it (7 of 7
// throws in c569b392a/b came from ADAnimNotify_DoAction in a *Grenade* montage). UBWCOOP_C569_OFF=1 turns it off.
bool g_c569_gate = true; // written in ubw::init before any hook
static uint32_t g_c569_skips = 0;

static bool c569_skip(const void* task) {
    if (!g_c569_gate) {
        return false;
    }
    const auto key = fname_text(static_cast<const uint8_t*>(task) + kC104TaskKey);
    if (strncmp(key.text, "Grenade", 7) != 0) {
        return false;
    }
    if (g_c569.inner != nullptr && g_c569.sequence != nullptr &&
        strstr(object_name(g_c569.sequence).text, "Grenade") != nullptr) {
        return false;
    }
    ++g_c569_skips;
    log::line("card569: %s t=%llu skip %s DoAction (notify %s, sequence %s): not its grenade sequence; %u skips",
              side(), static_cast<unsigned long long>(GetTickCount64()), key.text, object_name(g_c569.inner).text,
              object_name(g_c569.sequence).text, g_c569_skips);
    return true;
}

void detour_c104_do_action(void* task) {
    c104_note_task(task, g_c104.actions, "DoAction");
    c569_note_source(task);
    if (c569_skip(task)) {
        return;
    }
    if (g_c74_log || g_c76_log) {
        detour_c74_anim_do(task);
        return;
    }
    g_orig_c74_anim[1](task);
}

void detour_c104_anim_ended(void* task) {
    c104_note_task(task, g_c104.ends, "AnimationEnded");
    if (g_c74_log || g_c76_log) {
        detour_c74_anim_end(task);
        return;
    }
    g_orig_c74_anim[2](task);
}

// Test command `objectives` and map change: the per-side counts.
void c104_dump() {
    log::line("card104: %s animation tally (host own / server copy / client own / other): starts %u/%u/%u/%u, "
              "DoAction %u/%u/%u/%u, AnimationEnded %u/%u/%u/%u, meshes forced to AlwaysTickPose %u",
              side(), g_c104.starts[0], g_c104.starts[1], g_c104.starts[2], g_c104.starts[3], g_c104.actions[0],
              g_c104.actions[1], g_c104.actions[2], g_c104.actions[3], g_c104.ends[0], g_c104.ends[1], g_c104.ends[2],
              g_c104.ends[3], g_c104.forced);
}


C104ChargeFn g_c104_update_charge = nullptr;
C104ReadyFn g_c104_set_ready = nullptr;
C104DestroyFn g_c104_destroy_slot = nullptr; // card 333

namespace {

constexpr int32_t kC104ChargeSlots = 4;        // slots mirrored (the HUD's four)
constexpr size_t kC104CompSlots = 0x1A0;       // USTTacticalSupportComponent: TArray<USTTacticalSupportSlot*>
constexpr size_t kC104SlotAbility = 0x30;      // USTTacticalSupportSlot: the slot's ability object (GetAbilityCharge)
constexpr size_t kC104AbilityReady = 0x29;     // on it: the ready flag (UpdateAbilityCharge tests it)
constexpr size_t kC104AbilityCharge = 0x2C;    // on it: the charge (GetAbilityCharge returns it)
constexpr size_t kC104ObjectFlags = 0x8;       // UObjectBase flags; bit 30 set = being destroyed (the game's check)
constexpr uint32_t kC104DyingBit = 30;
constexpr uint32_t kC104ChargeLines = 40;      // charge mirror lines per run
uint32_t g_c104_charge_lines = 0;              // game thread

bool c104_charge_line() {
    return note_event_cap(g_c104_charge_lines, kC104ChargeLines, "card104 charge mirror");
}

void* c104_tac_component(const void* pawn) {
    const uint8_t* prop = nullptr;
    return pawn != nullptr ? const_cast<void*>(prop_object_field(pawn, "TacticalSupportComponent", &prop)) : nullptr;
}

// The slot's live ability object, as GetAbilityCharge and UpdateAbilityCharge find it, or null.
uint8_t* c104_slot_ability(void* comp, int32_t i, void** slot_out) {
    void* const* const slots = comp != nullptr ? static_cast<void* const*>(read_ptr(comp, kC104CompSlots)) : nullptr;
    const int32_t num = slots != nullptr ? read_i32(comp, kC104CompSlots + kArrayNum) : 0;
    void* const slot = i < num ? slots[i] : nullptr;
    auto* const ability = slot != nullptr ? static_cast<uint8_t*>(read_ptr(slot, kC104SlotAbility)) : nullptr;
    const bool live = ability != nullptr &&
                      ((static_cast<uint32_t>(read_i32(ability, kC104ObjectFlags)) >> kC104DyingBit) & 1u) == 0;
    *slot_out = slot;
    return live ? ability : nullptr;
}

// "charge ready" per slot; a slot without a live ability is "-1 0".
int32_t c104_charge_text(void* comp, char* out, size_t cap) {
    int32_t len = 0;
    for (int32_t i = 0; i < kC104ChargeSlots && len >= 0 && static_cast<size_t>(len) < cap; ++i) {
        void* slot = nullptr;
        const uint8_t* const a = c104_slot_ability(comp, i, &slot);
        const int32_t charge = a != nullptr ? read_i32(a, kC104AbilityCharge) : -1;
        const int ready = a != nullptr ? a[kC104AbilityReady] : 0;
        len += snprintf(out + len, cap - static_cast<size_t>(len), i == 0 ? "%d %d" : " %d %d", charge, ready);
    }
    return len > 0 && static_cast<size_t>(len) < cap ? len : -1;
}

} // namespace

// Card 327 PW: the charge text of `pawn`'s tactical support component; 0 without one.
int32_t c104_pawn_charge_text(const void* pawn, char* out, size_t cap) {
    void* const comp = c104_tac_component(pawn);
    return comp != nullptr ? c104_charge_text(comp, out, cap) : 0;
}

// Host: one server copy's charges, sent on a change (the copy poll's period bounds the rate while a slot recharges).
void c104_charge_poll(void* pc, const void* pawn, char* sent, size_t cap) {
    char text[kC104ChargeText];
    void* const comp = c104_tac_component(pawn);
    if (comp == nullptr || c104_charge_text(comp, text, sizeof(text)) <= 0 || strcmp(text, sent) == 0) {
        return;
    }
    static wchar_t wire[kChanTextFrame];
    const WireString w = chan_text_frame(ChanOp::TacCharge, text, wire);
    const bool ok = w.num > 0 && chan_send_client(pc, w);
    if (ok) {
        snprintf(sent, cap, "%s", text);
    }
    if (c104_charge_line()) {
        log::line("%scard104: host t=%llu TacCharge to %s (copy %s): %s", ok ? "" : "WARN ",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(pc).text, object_name(pawn).text,
                  text);
    }
}

// Card 333, client: slot `i` holds an ability on one side only. Ours and not the host's, and the host's slot held one
// in the previous frame for this component (`was`: the host removed it, e.g. a spent bombing run): the game's own
// DestroyTacticalSupportAbility on our slot. A host frame from before its copy had a loadout never had the slot, so it
// removes nothing. The host's and not ours, once we hold any slot (`loaded`): nothing here can create it, so a capped
// WARN names the slot.
constexpr uint32_t kC333Warns = 8;
struct C333Host {
    const void* comp;             // the component the flags are for; another one (new pawn) starts from none
    bool live[kC104ChargeSlots];  // the host's slot held an ability in the last frame
};
static C333Host g_c333_host{}; // game thread, client
// Map change: the old map's component may be freed and a new one allocated at its address, so the flags start over.
void c333_arm_map() {
    g_c333_host = C333Host{};
}
static void c333_slot_presence(void* slot, int32_t i, bool ours, bool was, bool loaded) {
    if (ours && was && slot != nullptr && g_c104_destroy_slot != nullptr) {
        g_c104_destroy_slot(slot);
        log::line("card333: client t=%llu slot %d: the host's slot went empty, DestroyTacticalSupportAbility on ours",
                  static_cast<unsigned long long>(GetTickCount64()), i);
        return;
    }
    if (ours ? !was : !loaded) {
        return; // the host's copy has no loadout yet, or ours has none
    }
    static uint32_t warns = 0;
    if (warns < kC333Warns) {
        ++warns;
        log::line("WARN card333: client: slot %d ability on %s only, not mirrored (slot %p)", i,
                  ours ? "the client" : "the host", slot);
    }
}

// Client: the host's charges onto our own pawn's slots: UpdateAbilityCharge(host - ours), then the ready flag.
void c104_receive_charge(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    int32_t v[2 * kC104ChargeSlots] = {};
    const int n = chan_text(s, text, sizeof(text)) > 0
                      ? sscanf_s(text, "%d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7])
                      : 0;
    void* const comp = c104_tac_component(read_ptr(pc, kControllerPawn));
    if (n < 2 || (n & 1) != 0 || comp == nullptr || g_c104_update_charge == nullptr || g_c104_set_ready == nullptr) {
        log::line("WARN card104: client: TacCharge frame not applied (%d values, component %s)", n,
                  object_name(comp).text);
        return;
    }
    char applied[kC104ChargeText];
    const int32_t before = c104_charge_text(comp, applied, sizeof(applied));
    // A frame with fewer than all slots (a host copy without its full loadout) starts the flags over too: a slot
    // it omits would otherwise keep a stale "held" flag and a later empty row would destroy ours.
    if (g_c333_host.comp != comp || n < 2 * kC104ChargeSlots) {
        g_c333_host = C333Host{};
        g_c333_host.comp = comp;
    }
    bool loaded = false;
    for (int32_t i = 0; i < kC104ChargeSlots; ++i) {
        void* slot = nullptr;
        loaded = loaded || c104_slot_ability(comp, i, &slot) != nullptr;
    }
    for (int32_t i = 0; i < kC104ChargeSlots && 2 * i < n; ++i) {
        void* slot = nullptr;
        const uint8_t* const a = c104_slot_ability(comp, i, &slot);
        const bool was = g_c333_host.live[i];
        g_c333_host.live[i] = v[2 * i] >= 0;
        if ((a == nullptr) != (v[2 * i] < 0)) {
            c333_slot_presence(slot, i, a != nullptr, was, loaded);
            continue;
        }
        if (a == nullptr) {
            continue;
        }
        const int32_t delta = v[2 * i] - read_i32(a, kC104AbilityCharge);
        if (delta != 0) {
            g_c104_update_charge(slot, delta);
        }
        if ((a[kC104AbilityReady] != 0) != (v[2 * i + 1] != 0)) {
            g_c104_set_ready(slot, v[2 * i + 1] != 0);
        }
    }
    char after[kC104ChargeText];
    c104_charge_text(comp, after, sizeof(after));
    if (c104_charge_line()) {
        log::line("card104: client t=%llu TacCharge %s: own slots %s -> %s", static_cast<unsigned long long>(GetTickCount64()),
                  text, before > 0 ? applied : "-", after);
    }
}

// Card 106 test (`charge <slot> <value> <ready>`): sets one slot of the pawn's tactical support charge the way the
// client mirror does, through UpdateAbilityCharge (the delta) and SetIsAbilityReadyForUse. Null when set.
const char* c106_set_charge(void* pawn, int32_t slot_index, int32_t value, bool ready) {
    void* const comp = c104_tac_component(pawn);
    void* slot = nullptr;
    const uint8_t* const a = comp != nullptr && slot_index < kC104ChargeSlots ? c104_slot_ability(comp, slot_index, &slot)
                                                                              : nullptr;
    if (a == nullptr || g_c104_update_charge == nullptr || g_c104_set_ready == nullptr) {
        return "no TacticalSupportComponent slot ability or charge functions";
    }
    char before[kC104ChargeText];
    const int32_t had = c104_charge_text(comp, before, sizeof(before));
    const int32_t delta = value - read_i32(a, kC104AbilityCharge);
    if (delta != 0) {
        g_c104_update_charge(slot, delta);
    }
    if ((a[kC104AbilityReady] != 0) != ready) {
        g_c104_set_ready(slot, ready);
    }
    char after[kC104ChargeText];
    c104_charge_text(comp, after, sizeof(after));
    log::line("card106: %s t=%llu charge slot %d to %d ready %d on %s: %s -> %s", side(),
              static_cast<unsigned long long>(GetTickCount64()), slot_index, value, ready ? 1 : 0, object_name(pawn).text,
              had > 0 ? before : "-", after);
    return nullptr;
}

} // namespace ubw::hooks
