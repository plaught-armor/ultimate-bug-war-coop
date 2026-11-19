// NPC audit: the NPC list and a test kill of the nearest NPC trooper (card 025), and the Morita trooper
// sprite animation dump (card 027).

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

StaticClassFn g_npc_class = nullptr; // ASTNonPlayableCharacterBase
InstaKillFn g_insta_kill = nullptr;
ActorBoundsFn g_actor_bounds = nullptr; // card 131
NpcDeathEventFn g_orig_npc_death_event = nullptr;
NpcStartDyingFn g_orig_npc_start_dying = nullptr;

GameplayAttributeCtorFn g_gameplay_attribute_ctor = nullptr;
// Built once by the game's ctor and kept for the process: its FString/TArray buffers are engine-owned and never freed.
GameplayAttributeRaw g_c25_health_attr{};
bool g_c25_health_attr_built = false;
NpcHealthChangedFn g_orig_npc_health_changed = nullptr;
NpcRevivedFn g_orig_npc_revived = nullptr;

constexpr uint32_t kC25TickLines = 32;
constexpr uint32_t kC25Ticks = 13;       // t = 0, 5, ..., 60 s
constexpr ULONGLONG kC25TickMs = 5000;
constexpr ULONGLONG kC25PreShotMs = 1000;   // after the move: the camera settles, then the shot of the live target
constexpr ULONGLONG kC25KillMs = 1300;      // after the move: the kill, a few frames after the pre-kill shot
constexpr ULONGLONG kC25ReviveGapMs = 500;  // after the last kill shot: the revive

constexpr double kC25MoveOkDistance = 700.0; // 1 s after the move: farther = snapped back
constexpr double kC25MoveOkDrop = 300.0;     // 1 s after the move: more than this below the target = fell through
constexpr float kC25ReviveHealth = 50.0f;
constexpr size_t kNpcAsc = 0x680;            // ASTNonPlayableCharacterBase ASC (logged next to the component's)
constexpr uint32_t kC25DeathLines = 16;      // death-path lines for NPCs other than the target
constexpr uint32_t kC25TargetLines = 16;

// Game-window screenshots: the console command "shot" (UGameViewportClient's own screenshot of the next frame,
// saved under Saved/Screenshots/Windows). X11 capture of the window is black under XWayland.
constexpr ULONGLONG kC25KillShotMs[] = {100, 300, 600, 1000, 1500, 2500, 5000, 15000};
constexpr ULONGLONG kC25ReviveShotMs[] = {200, 600, 1200, 2500, 5000};

constexpr C25Series kC25KillSeries{"", kC25KillShotMs, sizeof(kC25KillShotMs) / sizeof(kC25KillShotMs[0])};
constexpr C25Series kC25ReviveSeries{"r", kC25ReviveShotMs, sizeof(kC25ReviveShotMs) / sizeof(kC25ReviveShotMs[0])};

bool g_c25_kill = false; // UBWCOOP_KILL_TROOPER; written in ubw::init before any hook
C25State g_c25{};        // game thread; reset per map by c25_arm_map


C25Npc c25_measure(void* actor, const Vec3& pawn_loc, const ViewPoint& view) {
    const Vec3 at = actor_location(actor);
    const Vec3 p{at.x - pawn_loc.x, at.y - pawn_loc.y, at.z - pawn_loc.z};
    const Vec3 v{at.x - view.loc.x, at.y - view.loc.y, at.z - view.loc.z};
    const double vlen = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    const double dot = v.x * view.dir.x + v.y * view.dir.y + v.z * view.dir.z;
    return C25Npc{actor, at, sqrt(p.x * p.x + p.y * p.y + p.z * p.z), vlen > 1.0 ? dot / vlen : 1.0};
}

void c25_log_tick(const C25List& list) {
    log::line("card025: tick %u (t=%llu s): %u NPCs in the world, %u listed", g_c25.ticks,
              static_cast<unsigned long long>(g_c25.ticks * kC25TickMs / 1000), list.total,
              list.count < kC25TickLines ? list.count : kC25TickLines);
    for (uint32_t i = 0; i < list.count && i < kC25TickLines; ++i) {
        const C25Npc& n = list.npc[i];
        float health = 0.0f;
        const int dead = c25_dead(n.actor, &health);
        log::line("card025:   %s %s at (%.0f, %.0f, %.0f) dist %.0f front %.2f Health %.1f dead %d",
                  class_name(n.actor).text, object_name(n.actor).text, n.loc.x, n.loc.y, n.loc.z, n.dist, n.front,
                  health, dead);
    }
}

// Index of the nearest live NPC whose class name holds "Trooper", or -1.
int32_t c25_pick(const C25List& list) {
    int32_t best = -1;
    for (uint32_t i = 0; i < list.count; ++i) {
        const C25Npc& n = list.npc[i];
        float health = 0.0f;
        const bool live = c25_dead(n.actor, &health) == 0;
        if (!live || strstr(class_name(n.actor).text, "Trooper") == nullptr) {
            continue;
        }
        if (best < 0 || n.dist < list.npc[best].dist) {
            best = static_cast<int32_t>(i);
        }
    }
    return best;
}

// Index of the chosen target in `list`, or -1.
int32_t c25_find(const C25List& list) {
    for (uint32_t i = 0; g_c25.target != 0 && i < list.count; ++i) {
        if (read_u64(list.npc[i].actor, kObjectName) == g_c25.target) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

bool c25_is_target(const void* npc) {
    return g_c25.target != 0 && read_u64(npc, kObjectName) == g_c25.target;
}

// After the first tick: picks the target, moves the pawn to it and schedules the pre-kill shot and the kill.
void c25_choose(const C25List& list, void* pawn, void* pc, ULONGLONG now) {
    const int32_t i = c25_pick(list);
    if (i < 0) {
        log::line("WARN card025: no live NPC with \"Trooper\" in its class name among %u listed -- no kill", list.count);
        return;
    }
    const C25Npc& n = list.npc[i];
    g_c25.target = read_u64(n.actor, kObjectName);
    log::line("card025: target %s %s at (%.0f, %.0f, %.0f) dist %.0f", class_name(n.actor).text,
              object_name(n.actor).text, n.loc.x, n.loc.y, n.loc.z, n.dist);
    if (!c25_move(pawn, pc, n.loc)) {
        log::line("WARN card025: the move failed -- stop, no kill");
        return;
    }
    g_c25.pre_at_ms = now + kC25PreShotMs;
    g_c25.kill_at_ms = now + kC25KillMs;
}

// 1 s after the move: checks the pawn held its spot, faces the target again and shoots the live target.
void c25_pre(const C25List& list, const void* pawn, void* pc) {
    g_c25.pre_at_ms = 0;
    const int32_t i = c25_find(list);
    const Vec3 at = actor_location(pawn);
    const Vec3 t = i >= 0 ? list.npc[i].loc : Vec3{};
    const double d = vec_dist(at, t);
    const bool ok = i >= 0 && d <= kC25MoveOkDistance && at.z >= t.z - kC25MoveOkDrop;
    log::line("card025: %spawn 1 s after the move at (%.0f, %.0f, %.0f), %.0f from the target at (%.0f, %.0f, %.0f) "
              "-- %s",
              ok ? "" : "WARN ", at.x, at.y, at.z, d, t.x, t.y, t.z,
              ok ? "move held" : "move failed (snapped back, fell through or target gone); stop, no kill");
    if (!ok) {
        g_c25.kill_at_ms = 0;
        return;
    }
    c25_face(pc, at, t);
    request_shot(pc, false);
    float health = 0.0f;
    const int dead = c25_dead(list.npc[i].actor, &health);
    log::line("card025: shot card025b-pre requested; target Health %.1f dead %d", health, dead);
}

void c25_start_series(const C25Series* series) {
    g_c25.series = series;
    g_c25.series_ms = GetTickCount64();
    g_c25.shots = 0;
}

void c25_kill(const C25List& list) {
    g_c25.kill_at_ms = 0;
    const int32_t i = c25_find(list);
    if (i < 0) {
        log::line("WARN card025: the target is no longer among the NPCs -- no kill");
        return;
    }
    void* const actor = list.npc[i].actor;
    float before = 0.0f;
    const int dead_before = c25_dead(actor, &before);
    const uint32_t lines_before = g_c25.target_lines;
    log::line("card025: KILL %s %s dist %.0f Health %.1f dead %d via USTCombatUtilityLibrary::InstaKillTarget"
              "(bIsOverkill=false)",
              class_name(actor).text, object_name(actor).text, list.npc[i].dist, before, dead_before);
    g_insta_kill(actor, false);
    c25_start_series(&kC25KillSeries);
    float after = 0.0f;
    const int dead_after = c25_dead(actor, &after);
    log::line("card025: after InstaKillTarget Health %.1f dead %d; death handling %s", after, dead_after,
              g_c25.target_lines > lines_before ? "ran inside the call (lines above)" : "not seen inside the call");
}

// Card 019/075: FGameplayAttributes of the movement set's properties, each built once by its ctor (the attribute
// delegate map compares weak owner + path, so the key must come from the ctor). Game thread.
struct C19AttrCache {
    const void* field[kC18MaxProps];
    GameplayAttributeRaw attr[kC18MaxProps];
    int n;
};
C19AttrCache g_c19_attrs{};

const GameplayAttributeRaw* c19_attribute(void* field) {
    for (int i = 0; i < g_c19_attrs.n; ++i) {
        if (g_c19_attrs.field[i] == field) {
            return &g_c19_attrs.attr[i];
        }
    }
    if (g_c19_attrs.n >= kC18MaxProps) {
        return nullptr;
    }
    const int i = g_c19_attrs.n++;
    g_c19_attrs.field[i] = field;
    g_gameplay_attribute_ctor(&g_c19_attrs.attr[i], field);
    return &g_c19_attrs.attr[i];
}

// The revive: the target's Health base 0 -> 50 through its health component's ASC, then the revive shots.
void c25_revive(const C25List& list) {
    g_c25.revive_at_ms = 0;
    const int32_t i = c25_find(list);
    void* const actor = i >= 0 ? list.npc[i].actor : nullptr;
    void* const component = c21_character_ptr(actor, kCharacterHealthComponent);
    void* const asc = component != nullptr ? read_ptr(component, kHealthComponentAsc) : nullptr;
    const GameplayAttributeRaw* const attr = asc != nullptr ? c25_health_attribute() : nullptr;
    if (attr == nullptr) {
        log::line("WARN card025: revive skipped (target %d, health component %p, ASC %p, Health attribute missing "
                  "or not looked up)", i, component, asc);
        return;
    }
    float before = 0.0f;
    const int dead_before = c25_dead(actor, &before);
    const uint32_t revived_before = g_c25.revived_calls;
    log::line("card025: REVIVE %s Health %.1f dead %d via UAbilitySystemComponent::SetNumericAttributeBase(Health, "
              "%.0f) on the health component's ASC %p (NPC +0x680 ASC %p)",
              object_name(actor).text, before, dead_before, kC25ReviveHealth, asc, read_ptr(actor, kNpcAsc));
    g_set_numeric_attribute_base(asc, attr, kC25ReviveHealth);
    c25_start_series(&kC25ReviveSeries);
    float after = 0.0f;
    const int dead_after = c25_dead(actor, &after);
    log::line("card025: after revive Health %.1f dead %d; BP_OnRevived %s", after, dead_after,
              g_c25.revived_calls > revived_before ? "ran inside the call" : "not seen inside the call");
}

// Requests the next due shot of the current series; the end of the kill series schedules the revive.
void c25_series_step(const C25List& list, void* pc, ULONGLONG now) {
    const C25Series* const s = g_c25.series;
    if (s == nullptr || now < g_c25.series_ms + s->ms[g_c25.shots]) {
        return;
    }
    request_shot(pc, false);
    const int32_t i = c25_find(list);
    float health = -1.0f;
    const int dead = i >= 0 ? c25_dead(list.npc[i].actor, &health) : -1;
    log::line("card025: shot card025b-%s%.1f requested at +%llu ms (planned +%llu ms); target Health %.1f dead %d",
              s->tag, static_cast<double>(s->ms[g_c25.shots]) / 1000.0,
              static_cast<unsigned long long>(now - g_c25.series_ms),
              static_cast<unsigned long long>(s->ms[g_c25.shots]), health, dead);
    if (++g_c25.shots < s->count) {
        return;
    }
    g_c25.series = nullptr;
    if (s == &kC25KillSeries) {
        g_c25.revive_at_ms = now + kC25ReviveGapMs;
    }
}

bool c25_due(ULONGLONG at, ULONGLONG now) {
    return at != 0 && now >= at;
}

bool c25_event_due(ULONGLONG now) {
    const C25Series* const s = g_c25.series;
    return c25_due(g_c25.pre_at_ms, now) || c25_due(g_c25.kill_at_ms, now) || c25_due(g_c25.revive_at_ms, now) ||
           (s != nullptr && now >= g_c25.series_ms + s->ms[g_c25.shots]);
}

// Engine tick: the list every kC25TickMs and the step-6 events. Starts when the first PC's character is on foot.
void c25_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    if (!g_c25.armed || (now < g_c25.next_ms && !c25_event_due(now))) {
        return;
    }
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    auto* const pawn = pc != nullptr ? static_cast<uint8_t*>(read_ptr(pc, kControllerPawn)) : nullptr;
    ViewPoint view{};
    if (pawn == nullptr || !is_a(pawn, g_player_character_class()) || holding_dropship(pawn) != nullptr ||
        !c24_view_point(pawn, view)) {
        return;
    }
    C25List list;
    c25_collect(world, actor_location(pawn), view, list);
    if (c25_due(g_c25.pre_at_ms, now)) {
        c25_pre(list, pawn, pc);
    }
    if (c25_due(g_c25.kill_at_ms, now)) {
        c25_kill(list);
    }
    if (c25_due(g_c25.revive_at_ms, now)) {
        c25_revive(list);
    }
    c25_series_step(list, pc, now);
    if (now < g_c25.next_ms) {
        return;
    }
    c25_log_tick(list);
    g_c25.next_ms = now + kC25TickMs;
    if (++g_c25.ticks == 1) {
        c25_choose(list, pawn, pc, now);
    }
    if (g_c25.ticks >= kC25Ticks) {
        g_c25.armed = false;
        log::line("card025: NPC list done (%u ticks)", g_c25.ticks);
    }
}

// Log-only death-path evidence while UBWCOOP_KILL_TROOPER=1; the target's lines have their own cap.
void c25_note_death(const void* npc, const char* what) {
    const bool target = c25_is_target(npc);
    uint32_t& lines = target ? g_c25.target_lines : g_c25.death_lines;
    if (note_event_cap(lines, target ? kC25TargetLines : kC25DeathLines, "card025 NPC death path")) {
        log::line("card025: %s %s %s%s", what, class_name(npc).text, object_name(npc).text,
                  target ? " (target)" : "");
    }
}

void detour_npc_death_event(void* npc, const void* damage_event) {
    if (g_c25_kill) {
        c25_note_death(npc, "HandleDeathEvent");
    }
    c37_note_npc("HandleDeathEvent", npc, 0.0f, 0.0f, false);
    c38_note_npc(true, npc);
    g_orig_npc_death_event(npc, damage_event);
}

void detour_npc_start_dying(void* npc) {
    if (g_c25_kill) {
        c25_note_death(npc, "StartDying");
    }
    c37_note_npc("StartDying", npc, 0.0f, 0.0f, false);
    c38_note_npc(false, npc);
    g_orig_npc_start_dying(npc);
}

// Log-only, target only: the health-changed handler's two float arguments (the revive path).
void detour_npc_health_changed(void* npc, float a, float b) {
    if (g_c25_kill && c25_is_target(npc) &&
        note_event_cap(g_c25.target_lines, kC25TargetLines, "card025 NPC death path")) {
        log::line("card025: HandleHealthValueChangedEvent(%.1f, %.1f) %s (target)", a, b, object_name(npc).text);
    }
    c37_note_npc("HandleHealthValueChangedEvent", npc, a, b, true);
    g_orig_npc_health_changed(npc, a, b);
}

void detour_npc_revived(void* npc) {
    if (g_c25_kill && c25_is_target(npc)) {
        ++g_c25.revived_calls;
        log::line("card025: BP_OnRevived %s (target)", object_name(npc).text);
    }
    c37_note_npc("BP_OnRevived", npc, 0.0f, 0.0f, false);
    g_orig_npc_revived(npc);
}

// ---- Card 027: Morita trooper sprite animation dump (UBWCOOP_DUMP_SPRITE_ANIMS=1) ----
// Host/standalone only, kC27DelayMs after the local character is first on foot, once per map: the nearest
// BP_AI_Trooper_Morita_C's AnimatorComponent.AnimationData (followed by reflected property name) is logged, then
// BP_CharacterAnimations_MoritaTrooper and _FemaleMoritaTrooper (StaticFindFirstObject by name; a generated class
// gives its CDO) are walked by reflection into card027-<asset>.txt next to the exe, one line per leaf.
// FProperty subclass layouts (exe disassembly; the FProperty base is 0x70 bytes):
//   FStructProperty::Struct +0x70 (GetMinAlignment); FArrayProperty::Inner +0x78 (SameType 0x014F1F30);
//   FMapProperty KeyProp +0x70, ValueProp +0x78, MapLayout ValueOffset +0x80, HashNextIdOffset +0x84,
//   HashIndexOffset +0x88, Size +0x8C, SparseArrayLayout.Size +0x94 (SameType 0x015189C0, LinkInternal
//   0x015167C0, which also sets ValueProp's Offset_Internal to ValueOffset); FSetProperty ElementProp +0x70,
//   SetLayout HashNextIdOffset +0x78, HashIndexOffset +0x7C, Size +0x80, SparseArrayLayout.Size +0x88;
//   FObjectPropertyBase::PropertyClass +0x70; FBoolProperty FieldSize +0x70, ByteOffset +0x71, FieldMask +0x73
//   (Identical 0x015101E0). Each is checked on a known property before any walk (c27_verify), and every
//   container is checked again where it is met; a failed check stops the walk below that property.
StaticClassFn g_sprite_data_class = nullptr; // UADSpriteAnimationData
StaticClassFn g_npc_data_class = nullptr;    // USTNPCAnimationData
bool g_c27_dump = false;                     // UBWCOOP_DUMP_SPRITE_ANIMS; written in ubw::init before any hook

constexpr size_t kFieldOwner = 0x10;          // FField::Owner (FFieldVariant; bit 0 = UObject owner)
constexpr size_t kPropertyArrayDim = 0x30;
constexpr size_t kStructPropertiesSize = 0x58; // UStruct::PropertiesSize
constexpr size_t kStructMinAlignment = 0x5C;   // UStruct::MinAlignment (int16)

constexpr size_t kSetPropElement = 0x70;
constexpr size_t kSetHashNext = 0x78;
constexpr size_t kSetHashIndex = 0x7C;
constexpr size_t kSetSize = 0x80;
constexpr size_t kSetSparseSize = 0x88;

constexpr uint32_t kC27MaxDepth = 10;     // container nesting

constexpr int32_t kC27MaxArrayDim = 64;
constexpr uint32_t kC27MaxQueue = 8;      // objects per file: the asset, then sprite data it references
constexpr uint32_t kC27MaxSurvey = 16;    // distinct AnimationData objects logged from the NPC list
constexpr ULONGLONG kC27DelayMs = 5000;   // after the pawn is first on foot: the NPCs have spawned

constexpr const char* kC27Assets[] = {"BP_CharacterAnimations_MoritaTrooper",
                                      "BP_CharacterAnimations_FemaleMoritaTrooper"};
constexpr const char* kC27MoritaClass = "BP_AI_Trooper_Morita_C";

C27State g_c27{};

struct C27Walk {
    C27Out* out;
    char path[512];
    size_t len;
    const void* queue[kC27MaxQueue];
    uint32_t queued;
    uint32_t dropped; // sprite data references past kC27MaxQueue
    uint32_t refused; // containers whose layout check failed
};

// True when `child` is a field owned by the field `parent` (an inner, key, value or element property).
bool c27_owned(const void* child, const void* parent) {
    return child != nullptr && (read_u64(child, kFieldOwner) & ~1ull) == reinterpret_cast<uint64_t>(parent);
}

bool c27_struct_ok(const uint8_t* field, const void* s) {
    if (s == nullptr) {
        return false;
    }
    const ObjectName cls = class_name(s);
    const int32_t props = read_i32(s, kStructPropertiesSize);
    int16_t align = 0;
    memcpy(&align, static_cast<const uint8_t*>(s) + kStructMinAlignment, sizeof(align));
    if ((strcmp(cls.text, "ScriptStruct") != 0 && strcmp(cls.text, "UserDefinedStruct") != 0) || props <= 0 ||
        align <= 0 || align > 64 || (align & (align - 1)) != 0) {
        return false;
    }
    return ((props + align - 1) & ~(align - 1)) == read_i32(field, kPropertyElementSize);
}

bool c27_set_ok(const uint8_t* field) {
    const auto* const element = static_cast<const uint8_t*>(read_ptr(field, kSetPropElement));
    const int32_t size = read_i32(field, kSetSize);
    return c27_owned(element, field) && read_i32(field, kSetHashIndex) == read_i32(field, kSetHashNext) + 4 &&
           size >= read_i32(element, kPropertyElementSize) && read_i32(field, kSetSparseSize) >= size;
}

void c27_emit(C27Out& out, const char* text, size_t len) {
    DWORD written = 0;
    if (!WriteFile(out.file, text, static_cast<DWORD>(len), &written, nullptr) || written != len) {
        ++out.write_fails;
    }
}

// Appends to the walk path; returns the length to restore with c27_pop. Truncates at the buffer end.
size_t c27_push(C27Walk& w, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
size_t c27_push(C27Walk& w, const char* fmt, ...) {
    const size_t old = w.len;
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(w.path + w.len, sizeof(w.path) - w.len, fmt, ap);
    va_end(ap);
    const size_t grown = n > 0 ? w.len + static_cast<size_t>(n) : w.len;
    w.len = grown < sizeof(w.path) - 1 ? grown : sizeof(w.path) - 1;
    return old;
}

void c27_pop(C27Walk& w, size_t old) {
    w.len = old;
    w.path[old] = '\0';
}

void c27_refuse(C27Walk& w, const char* what) {
    ++w.refused;
    c27_write(*w.out, "%s = <%s layout check failed; not walked>", w.path, what);
}

long long c27_sint(const uint8_t* addr, int32_t size) {
    int8_t a = 0;
    int16_t b = 0;
    int32_t c = 0;
    int64_t d = 0;
    switch (size) {
    case 1: memcpy(&a, addr, 1); return a;
    case 2: memcpy(&b, addr, 2); return b;
    case 4: memcpy(&c, addr, 4); return c;
    default: memcpy(&d, addr, 8); return d;
    }
}

unsigned long long c27_uint(const uint8_t* addr, int32_t size) {
    unsigned long long v = 0;
    memcpy(&v, addr, size > 0 && size < 8 ? static_cast<size_t>(size) : 8); // little-endian
    return v;
}

void c27_str(const uint8_t* addr, char* buf, size_t cap) {
    FStringRaw s{};
    memcpy(&s, addr, sizeof(s));
    size_t n = 0;
    const int32_t chars = s.data != nullptr && s.num > 1 && s.num <= kMaxLoggedFStringNum ? s.num - 1 : 0;
    buf[n++] = '"';
    for (int32_t i = 0; i < chars && n + 2 < cap; ++i) {
        const wchar_t c = s.data[i];
        buf[n++] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
    }
    buf[n++] = '"';
    buf[n] = '\0';
}

void c27_hex(const char* kind, const uint8_t* addr, int32_t size, char* buf, size_t cap) {
    int w = snprintf(buf, cap, "<%s, %d bytes>", kind, size);
    for (int32_t i = 0; i < size && i < 16 && w > 0 && static_cast<size_t>(w) + 4 < cap; ++i) {
        w += snprintf(buf + w, cap - static_cast<size_t>(w), " %02X", addr[i]);
    }
}

void c27_enqueue(C27Walk& w, const void* object) {
    if (!c27_is_sprite_data(object)) {
        return;
    }
    for (uint32_t i = 0; i < w.queued; ++i) {
        if (w.queue[i] == object) {
            return;
        }
    }
    if (w.queued < kC27MaxQueue) {
        w.queue[w.queued++] = object;
    } else {
        ++w.dropped;
    }
}

void c27_value(C27Walk& w, const uint8_t* field, const uint8_t* addr, uint32_t depth);

// One property of a struct or object at `base`, each static-array element on its own path.
void c27_field(C27Walk& w, const uint8_t* f, const uint8_t* base, int32_t limit, uint32_t depth) {
    const int32_t dim = read_i32(f, kPropertyArrayDim);
    const int32_t size = read_i32(f, kPropertyElementSize);
    const int32_t off = read_i32(f, kPropertyOffset);
    const ObjectName name = fname_text(f + kFieldName);
    if (dim < 1 || dim > kC27MaxArrayDim || size <= 0 || off < 0 || static_cast<int64_t>(off) + dim * size > limit) {
        c27_write(*w.out, "%s%s%s = <bad property: dim %d size %d offset %d, struct size %d>", w.path,
                  w.len != 0 ? "." : "", name.text, dim, size, off, limit);
        return;
    }
    for (int32_t k = 0; k < dim && !w.out->capped; ++k) {
        const size_t old = dim == 1 ? c27_push(w, "%s%s", w.len != 0 ? "." : "", name.text)
                                    : c27_push(w, "%s%s[%d]", w.len != 0 ? "." : "", name.text, k);
        c27_value(w, f, base + off + static_cast<ptrdiff_t>(k) * size, depth);
        c27_pop(w, old);
    }
}

// Every property of `ustruct` and its supers (base first) at `base`.
void c27_fields(C27Walk& w, const void* ustruct, const uint8_t* base, uint32_t depth) {
    const void* chain[kPropMaxSupers] = {};
    int n = 0;
    for (const void* s = ustruct; s != nullptr && n < kPropMaxSupers; s = read_ptr(s, kStructSuper)) {
        chain[n++] = s;
    }
    const int32_t limit = read_i32(ustruct, kStructPropertiesSize);
    for (int c = n - 1; c >= 0 && !w.out->capped; --c) {
        const auto* f = static_cast<const uint8_t*>(read_ptr(chain[c], kStructChildProperties));
        for (int i = 0; i < kPropMaxFields && f != nullptr && !w.out->capped;
             ++i, f = static_cast<const uint8_t*>(read_ptr(f, kFieldNext))) {
            c27_field(w, f, base, limit, depth);
        }
    }
}

void c27_array(C27Walk& w, const uint8_t* field, const uint8_t* addr, uint32_t depth) {
    const auto* const inner = static_cast<const uint8_t*>(read_ptr(field, kArrayPropInner));
    const int32_t stride = inner != nullptr ? read_i32(inner, kPropertyElementSize) : 0;
    const auto* const data = static_cast<const uint8_t*>(read_ptr(addr, 0));
    const int32_t num = read_i32(addr, kArrayNum);
    if (!c27_owned(inner, field) || stride <= 0 || num < 0 || num > read_i32(addr, kArrayMax) ||
        (num > 0 && data == nullptr)) {
        c27_refuse(w, "array");
        return;
    }
    c27_write(*w.out, "%s.Num = %d", w.path, num);
    for (int32_t i = 0; i < num && i < kC27MaxElements && !w.out->capped; ++i) {
        const size_t old = c27_push(w, "[%d]", i);
        c27_value(w, inner, data + static_cast<ptrdiff_t>(i) * stride, depth);
        c27_pop(w, old);
    }
}

// One map pair: a leaf key becomes the path index (Animations[StandUp]); any other key is walked as .Key.
void c27_pair(C27Walk& w, const uint8_t* field, const uint8_t* elem, int32_t slot, uint32_t depth) {
    const auto* const key = static_cast<const uint8_t*>(read_ptr(field, kMapPropKey));
    const auto* const value = static_cast<const uint8_t*>(read_ptr(field, kMapPropValue));
    const uint8_t* const v = elem + read_i32(field, kMapValueOffset);
    ObjectName kind_name{};
    const PropKind kind = prop_kind(key, kind_name);
    if (prop_is_leaf(kind)) {
        char text[kC27TextChars];
        c27_text(kind, kind_name.text, key, elem, text, sizeof(text));
        const size_t old = c27_push(w, "[%s]", text);
        c27_value(w, value, v, depth);
        c27_pop(w, old);
        return;
    }
    size_t old = c27_push(w, "[#%d].Key", slot);
    c27_value(w, key, elem, depth);
    c27_pop(w, old);
    old = c27_push(w, "[#%d].Value", slot);
    c27_value(w, value, v, depth);
    c27_pop(w, old);
}

void c27_map(C27Walk& w, const uint8_t* field, const uint8_t* addr, uint32_t depth) {
    C27Sparse s{};
    if (!c27_map_ok(field) || !c27_sparse(addr, s)) {
        c27_refuse(w, "map");
        return;
    }
    const int32_t stride = read_i32(field, kMapSparseSize);
    c27_write(*w.out, "%s.Num = %d", w.path, s.live);
    for (int32_t i = 0; i < s.slots && i < kC27MaxElements && !w.out->capped; ++i) {
        if (c27_bit(s, i)) {
            c27_pair(w, field, s.data + static_cast<ptrdiff_t>(i) * stride, i, depth);
        }
    }
}

void c27_set(C27Walk& w, const uint8_t* field, const uint8_t* addr, uint32_t depth) {
    C27Sparse s{};
    if (!c27_set_ok(field) || !c27_sparse(addr, s)) {
        c27_refuse(w, "set");
        return;
    }
    const auto* const element = static_cast<const uint8_t*>(read_ptr(field, kSetPropElement));
    const int32_t stride = read_i32(field, kSetSparseSize);
    c27_write(*w.out, "%s.Num = %d", w.path, s.live);
    for (int32_t i = 0; i < s.slots && i < kC27MaxElements && !w.out->capped; ++i) {
        if (c27_bit(s, i)) {
            const size_t old = c27_push(w, "{%d}", i);
            c27_value(w, element, s.data + static_cast<ptrdiff_t>(i) * stride, depth);
            c27_pop(w, old);
        }
    }
}

// One value: containers and structs recurse (depth-capped), leaves become one line.
void c27_value(C27Walk& w, const uint8_t* field, const uint8_t* addr, uint32_t depth) {
    if (w.out->capped) {
        return;
    }
    ObjectName kind_name{};
    const PropKind kind = prop_kind(field, kind_name);
    if (!prop_is_leaf(kind) && depth >= kC27MaxDepth) {
        c27_write(*w.out, "%s = <depth cap %u>", w.path, kC27MaxDepth);
        return;
    }
    const void* const s = kind == PropKind::Struct ? read_ptr(field, kStructPropStruct) : nullptr;
    switch (kind) {
    case PropKind::Struct:
        if (!c27_struct_ok(field, s)) {
            c27_refuse(w, "struct");
        } else {
            c27_fields(w, s, addr, depth + 1);
        }
        return;
    case PropKind::Array: c27_array(w, field, addr, depth + 1); return;
    case PropKind::Map: c27_map(w, field, addr, depth + 1); return;
    case PropKind::Set: c27_set(w, field, addr, depth + 1); return;
    default: break;
    }
    if (kind == PropKind::Object) {
        c27_enqueue(w, read_ptr(addr, 0));
    }
    char text[kC27TextChars];
    c27_text(kind, kind_name.text, field, addr, text, sizeof(text));
    c27_write(*w.out, "%s = %s", w.path, text);
}

// One asset into card027-<tag>.txt: the asset, then the sprite data objects it references (kC27MaxQueue).
void c27_dump(const char* tag, const void* asset) {
    C27Out out{};
    if (!c27_open("card027-", tag, out)) {
        return;
    }
    C27Walk w{};
    w.out = &out;
    w.queue[0] = asset;
    w.queued = 1;
    for (uint32_t q = 0; q < w.queued && q < kC27MaxQueue && !out.capped; ++q) {
        const void* const object = w.queue[q];
        const void* const cls = read_ptr(object, kObjectClass);
        c27_write(out, "== %s (class %s, super %s, outer %s) ==", object_name(object).text, object_name(cls).text,
                  object_name(read_ptr(cls, kStructSuper)).text, object_name(read_ptr(object, kObjectOuter)).text);
        c27_pop(w, 0);
        c27_fields(w, cls, static_cast<const uint8_t*>(object), 0);
    }
    const uint32_t lines = out.lines;
    const BOOL closed = CloseHandle(out.file);
    log::line("card027: wrote card027-%s.txt: %u lines%s, %u objects (%u references past the cap), %u layout "
              "refusals, %u write failures, close %d",
              tag, lines, out.capped ? " (capped)" : "", w.queued, w.dropped, w.refused, out.write_fails,
              closed ? 1 : 0);
}

// Known-value check of FObjectPropertyBase::PropertyClass: a class the value is an instance of.
bool c27_object_prop_ok(const uint8_t* prop, const void* value) {
    const void* const cls = prop != nullptr ? read_ptr(prop, kObjectPropClass) : nullptr;
    return cls != nullptr && value != nullptr && strstr(class_name(cls).text, "Class") != nullptr && is_a(value, cls);
}

struct C27Npc {
    const void* npc;
    const void* component;
    const void* data;
    bool path_ok; // both object property checks passed
};

// npc.AnimatorComponent -> component.AnimationData, by reflected property name.
C27Npc c27_follow(const void* npc) {
    C27Npc r{npc, nullptr, nullptr, false};
    const uint8_t* comp_prop = nullptr;
    const uint8_t* data_prop = nullptr;
    r.component = prop_object_field(npc, "AnimatorComponent", &comp_prop);
    r.data = prop_object_field(r.component, "AnimationData", &data_prop);
    r.path_ok = c27_object_prop_ok(comp_prop, r.component) && c27_object_prop_ok(data_prop, r.data);
    return r;
}

void c27_log_path(const C27Npc& n, double dist) {
    const uint8_t* comp_prop = nullptr;
    const uint8_t* data_prop = nullptr;
    prop_object_field(n.npc, "AnimatorComponent", &comp_prop);
    prop_object_field(n.component, "AnimationData", &data_prop);
    log::line("card027: nearest %s %s (dist %.0f): .AnimatorComponent (+0x%X, PropertyClass %s) -> %s %s "
              ".AnimationData (+0x%X, PropertyClass %s) -> %s %s; PropertyClass checks %s",
              class_name(n.npc).text, object_name(n.npc).text, dist,
              comp_prop != nullptr ? read_i32(comp_prop, kPropertyOffset) : -1,
              object_name(comp_prop != nullptr ? read_ptr(comp_prop, kObjectPropClass) : nullptr).text,
              class_name(n.component).text, object_name(n.component).text,
              data_prop != nullptr ? read_i32(data_prop, kPropertyOffset) : -1,
              object_name(data_prop != nullptr ? read_ptr(data_prop, kObjectPropClass) : nullptr).text,
              class_name(n.data).text, object_name(n.data).text, n.path_ok ? "ok" : "FAILED");
}

// Index of the nearest BP_AI_Trooper_Morita_C in the card 025 NPC list, or -1.
int32_t c27_nearest(const C25List& list) {
    int32_t best = -1;
    for (uint32_t i = 0; i < list.count; ++i) {
        if (strcmp(class_name(list.npc[i].actor).text, kC27MoritaClass) == 0 &&
            (best < 0 || list.npc[i].dist < list.npc[best].dist)) {
            best = static_cast<int32_t>(i);
        }
    }
    return best;
}

// The distinct AnimationData objects of the listed NPCs, with counts (first kC27MaxSurvey).
void c27_survey(const C25List& list) {
    const void* data[kC27MaxSurvey] = {};
    const void* example[kC27MaxSurvey] = {};
    uint32_t count[kC27MaxSurvey] = {};
    uint32_t distinct = 0;
    uint32_t over = 0;
    for (uint32_t i = 0; i < list.count; ++i) {
        const void* const d = c27_follow(list.npc[i].actor).data;
        uint32_t k = 0;
        while (k < distinct && data[k] != d) {
            ++k;
        }
        if (k == distinct && distinct == kC27MaxSurvey) {
            ++over;
            continue;
        }
        if (k == distinct) {
            data[distinct] = d;
            example[distinct++] = list.npc[i].actor;
        }
        ++count[k];
    }
    log::line("card027: %u NPCs listed (%u in the world), %u distinct AnimationData (%u NPCs past the cap)",
              list.count, list.total, distinct, over);
    for (uint32_t k = 0; k < distinct; ++k) {
        log::line("card027:   %s %s x%u (e.g. %s)", class_name(data[k]).text, object_name(data[k]).text, count[k],
                  class_name(example[k]).text);
    }
}

// The asset `name`: a loaded sprite data object of that name, else the CDO of the generated class `name`_C.
const void* c27_find_asset(const char* name) {
    wchar_t wide[160];
    if (swprintf(wide, 160, L"%hs", name) <= 0) {
        return nullptr;
    }
    const void* const object = g_find_first_object(nullptr, wide, 0, 0, nullptr);
    log::line("card027: find %s -> %s (%s)", name, object_name(object).text, class_name(object).text);
    if (c27_is_sprite_data(object)) {
        return object;
    }
    if (swprintf(wide, 160, L"%hs_C", name) <= 0) {
        return nullptr;
    }
    const void* const cls = g_find_first_object(nullptr, wide, 0, 0, nullptr);
    const void* const cdo =
        cls != nullptr && strstr(class_name(cls).text, "Class") != nullptr ? read_ptr(cls, kClassDefaultObject) : nullptr;
    log::line("card027: find %s_C -> %s (%s, super %s), CDO %s (%s)", name, object_name(cls).text,
              class_name(cls).text, object_name(cls != nullptr ? read_ptr(cls, kStructSuper) : nullptr).text,
              object_name(cdo).text, class_name(cdo).text);
    return c27_is_sprite_data(cdo) ? cdo : nullptr;
}

// The lookups, the checks, then one file per distinct asset (male, female, and the NPC's if it is neither).
void c27_run(const void* world, const void* pawn, const ViewPoint& view) {
    C25List list;
    c25_collect(world, actor_location(pawn), view, list);
    const int32_t m = c27_nearest(list);
    C27Npc npc{};
    if (m >= 0) {
        npc = c27_follow(list.npc[m].actor);
        c27_log_path(npc, list.npc[m].dist);
    } else {
        log::line("card027: no %s among %u listed NPCs", kC27MoritaClass, list.count);
    }
    c27_survey(list);
    const void* const male = c27_find_asset(kC27Assets[0]);
    const void* const female = c27_find_asset(kC27Assets[1]);
    log::line("card027: male %s, female %s, nearest Morita's AnimationData is %s", male != nullptr ? "found" : "NOT found",
              female != nullptr ? "found" : "NOT loaded (only the male is dumped)",
              npc.data == nullptr ? "none" : npc.data == male ? "the male asset" : npc.data == female ? "the female asset"
                                                                                                    : "another object");
    const void* const first = male != nullptr ? male : female != nullptr ? female : npc.data;
    if (first == nullptr || !c27_is_sprite_data(first) || !c27_verify(first)) {
        log::line("ERROR card027: no asset passed the offset checks; nothing dumped");
        return;
    }
    if (male != nullptr) {
        c27_dump(kC27Assets[0], male);
    }
    if (female != nullptr) {
        c27_dump(kC27Assets[1], female);
    }
    if (c27_is_sprite_data(npc.data) && npc.data != male && npc.data != female) {
        c27_dump(object_name(npc.data).text, npc.data);
    }
}

void c27_step(void* engine) {
    if (!g_c27.armed) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_c27.due_ms != 0 && now < g_c27.due_ms) {
        return;
    }
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    auto* const pawn = pc != nullptr ? static_cast<uint8_t*>(read_ptr(pc, kControllerPawn)) : nullptr;
    ViewPoint view{};
    if (pawn == nullptr || !is_a(pawn, g_player_character_class()) || holding_dropship(pawn) != nullptr ||
        !c24_view_point(pawn, view)) {
        return;
    }
    if (g_c27.due_ms == 0) {
        g_c27.due_ms = now + kC27DelayMs;
        log::line("card027: on foot; dump in %llu ms", static_cast<unsigned long long>(kC27DelayMs));
        return;
    }
    g_c27.armed = false;
    c27_run(world, pawn, view);
    c29_sets();
    log::line("card027: done");
}

// A UADAttributeSetHealth property (Health, Armour) as an FGameplayAttribute built by FGameplayAttribute(FProperty*);
// false when the class or property is missing.
bool c25_build_attribute(const char* name, GameplayAttributeRaw* out) {
    void* const cls = g_health_set_class != nullptr ? g_health_set_class() : nullptr;
    void* field = cls != nullptr ? read_ptr(cls, kStructChildProperties) : nullptr;
    for (int32_t n = 0; n < kC21MaxSetProps && field != nullptr; ++n, field = read_ptr(field, kFieldNext)) {
        if (strcmp(fname_text(static_cast<const uint8_t*>(field) + kFieldName).text, name) == 0) {
            g_gameplay_attribute_ctor(out, field);
            log::line("card025: %s attribute built by its ctor: field %p, path num %d, owner %p", name, out->field,
                      out->path.num, out->owner);
            return true;
        }
    }
    return false;
}

// UADAttributeSetHealth.Health, built on first use; nullptr when the class or property is missing.
const GameplayAttributeRaw* c25_health_attribute() {
    g_c25_health_attr_built = g_c25_health_attr_built || c25_build_attribute("Health", &g_c25_health_attr);
    return g_c25_health_attr_built ? &g_c25_health_attr : nullptr;
}

bool c27_is_sprite_data(const void* object) {
    return object != nullptr && (is_a(object, g_sprite_data_class()) || is_a(object, g_npc_data_class()));
}

// card027-<tag>.txt next to the exe, created or truncated. False (logged) on failure.
// Creates <exe dir>/<prefix><tag>.txt for writing.
bool c27_open(const char* prefix, const char* tag, C27Out& out) {
    out = C27Out{INVALID_HANDLE_VALUE, 0, 0, false};
    wchar_t path[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        log::line("ERROR card027: GetModuleFileNameW failed (len %lu, err %lu)", len, GetLastError());
        return false;
    }
    DWORD cut = len;
    while (cut > 0 && path[cut - 1] != L'\\' && path[cut - 1] != L'/') {
        --cut;
    }
    const int n = swprintf(path + cut, MAX_PATH - cut, L"%hs%hs.txt", prefix, tag);
    if (n <= 0) {
        log::line("ERROR card027: output path too long for %s", tag);
        return false;
    }
    out.file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out.file == INVALID_HANDLE_VALUE) {
        log::line("ERROR card027: CreateFileW card027-%s.txt failed, err %lu", tag, GetLastError());
        return false;
    }
    return true;
}

// The first property of `ustruct` named in `names` (null-terminated list), or null.
const uint8_t* c27_find_any(const void* ustruct, const char* const* names) {
    for (int i = 0; names[i] != nullptr && i < 8; ++i) {
        const uint8_t* const p = prop_find_prop(ustruct, names[i]);
        if (p != nullptr) {
            return p;
        }
    }
    return nullptr;
}

void c29_sets() {
    C27Out out{};
    if (g_static_load_object == nullptr || !c27_open("card029-", "sets", out)) {
        return;
    }
    C29Layout layout{};
    bool have_layout = false;
    for (const C29Set& set : kC29Sets) {
        c29_dump_set(out, set, layout, have_layout);
    }
    const uint32_t lines = out.lines;
    const BOOL closed = CloseHandle(out.file);
    log::line("card029: wrote card029-sets.txt: %u lines%s, %u write failures, close %d", lines,
              out.capped ? " (capped)" : "", out.write_fails, closed ? 1 : 0);
}

// Both sides with the audit: the NPC death path and health handler calls of non-trooper enemies. On the client an
// Old <= 0, New > 0 handler call is the revive branch and is counted.
void c37_note_npc(const char* what, const void* npc, float old_health, float new_health, bool values) {
    if (!g_c37.audit || c23_trooper(npc)) {
        return;
    }
    const bool revive = values && g_client_session && old_health <= 0.0f && new_health > 0.0f;
    g_c37.revive_calls += revive ? 1u : 0u;
    if (!note_event_cap(g_c37.event_lines, kC37EventLines, "card037 NPC events")) {
        return;
    }
    if (values) {
        log::line("%scard037: %s NPC %s %s %s(Old %.1f, New %.1f)%s", revive ? "WARN " : "", side(), what,
                  class_name(npc).text, object_name(npc).text, old_health, new_health,
                  g_c21_in_notify ? " [mod notify]" : " [game]");
        return;
    }
    log::line("card037: %s NPC %s %s %s%s", side(), what, class_name(npc).text, object_name(npc).text,
              g_c21_in_notify ? " [mod notify]" : " [game]");
}

// Both sides, from the card 025 NPC detours. Host: HandleDeathEvent starts the clock for the visual-native lines.
void c38_note_npc(bool death_event, const void* npc) {
    if (death_event && !g_client_session) {
        g_c38.death_ms = GetTickCount64();
        c39_track(npc);
    }
    c38_logic(death_event ? kC38NpcDeathEvent : kC38NpcStartDying, npc);
}

// Card 293 diagnostic: once per second on each machine, walk the local player's USTMinimapComponent ActiveMarkers
// (TArray<FSTMinimapMarkerData> at +0xF8, 0x38 bytes each; MinimapSource AActor* at +0x20, from the
// FSTMinimapMarkerData property params) and log each NPC marker that no player could see: hidden, mesh not
// visible, no collision, health at or below 0, or more than 300 uu below the first visibility hit straight down from
// 2000 uu above it. A marker is logged again only when its reason set changes; rows are capped per map.
constexpr size_t kMinimapActiveMarkers = 0xF8;  // USTMinimapComponent::ActiveMarkers (data; Num at +0x100)
constexpr size_t kMinimapMarkerSize = 0x38;     // sizeof(FSTMinimapMarkerData)
constexpr size_t kMinimapMarkerSource = 0x20;   // FSTMinimapMarkerData::MinimapSource (AActor*)
constexpr int32_t kC293MaxMarkers = 256;
constexpr uint32_t kC293Seen = 64;
constexpr uint32_t kC293RowCap = 300;
constexpr ULONGLONG kC293TickMs = 1000;
constexpr double kC293BelowUu = 300.0;
constexpr double kC293TraceUp = 2000.0;
constexpr size_t kC293CharacterMesh = 0x328; // ACharacter::Mesh (claim_c157.cpp kCharacterMesh)
enum : uint32_t {
    kC293Hidden = 1u,
    kC293MeshOff = 2u,
    kC293NoCollision = 4u,
    kC293NoHealth = 8u,
    kC293Below = 16u,
    kC293ReAdded = 32u, // step 2: IsDead 1, dropped from ActiveMarkers and listed again
    kC293NoGuid = 64u,  // step 2: host, a live NPC marker with mod guid 0
};
// Card 293 step 2: one dwell entry per suspect marker (an NPC marker with any reason), from its first suspect sample
// to map end. `listed` is whether it was in ActiveMarkers at the last sample; its first leave logs one row, and
// map end logs one more row for each entry still listed or re-added. Sampled every tick while a suspect marker is
// listed, once per second otherwise.
constexpr uint32_t kC293Dwell = 64;
constexpr uint32_t kC293DwellRowCap = 200;
struct C293Dwell {
    C31Weak actor;
    ObjectName cls;      // kept for a row after the actor is destroyed
    uint64_t guid;
    ULONGLONG first_ms;
    ULONGLONG last_ms;   // the last sample it was listed in
    uint32_t samples;    // samples it was listed in
    uint32_t reasons;    // union over its samples
    uint32_t re_adds;
    float health;
    int dead;
    bool listed;
    bool seen;           // listed in the current sample
    bool left_logged;
};
struct C293 {
    ULONGLONG next_ms;
    uint32_t seen;    // marker samples this map (an NPC marker counted once per second)
    uint32_t suspect; // suspect samples this map
    uint32_t rows;
    uint32_t n;
    int32_t id[kC293Seen];    // object_index of an NPC last logged
    uint32_t mask[kC293Seen]; // its reason set at that row
    C293Dwell dwell[kC293Dwell];
    uint32_t dwell_n;
    uint32_t dwell_rows;
    uint32_t no_guid;          // no-guid samples this map
    ULONGLONG longest_ms;      // longest dwell this map
    bool suspect_listed;       // a suspect marker was listed at the last sample: sample every tick
};
C293 g_c293{};

uint32_t c293_reasons(const void* pawn, const void* npc, float* health, int* dead) {
    const auto* const a = static_cast<const uint8_t*>(npc);
    const void* const mesh = read_ptr(npc, kC293CharacterMesh);
    uint32_t m = (a[kActorHiddenByte] & kActorHiddenBit) != 0 ? kC293Hidden : 0u;
    m |= mesh != nullptr && (static_cast<const uint8_t*>(mesh)[kSceneVisibleByte] & kSceneVisibleBit) == 0 ? kC293MeshOff
                                                                                                           : 0u;
    m |= (a[kActorCollisionFlags] & kActorEnableCollisionBit) == 0 ? kC293NoCollision : 0u;
    *dead = c25_dead(npc, health);
    m |= *health <= 0.0f ? kC293NoHealth : 0u;
    const Vec3 at = actor_location(npc);
    const void* hit_actor = nullptr; // the NPC's own capsule or mesh is not ground
    const Vec3 hit = trace_channel(pawn, Vec3{at.x, at.y, at.z + kC293TraceUp}, at, kEccVisibility, &hit_actor);
    m |= hit_actor != npc && hit.z > at.z + kC293BelowUu ? kC293Below : 0u;
    m |= !g_client_session && *health > 0.0f && c41_guid(npc) == 0 ? kC293NoGuid : 0u;
    return m;
}
// True when this NPC's reason set differs from its last row (and records it); false when unchanged or table full.
bool c293_changed(int32_t id, uint32_t mask) {
    for (uint32_t i = 0; i < g_c293.n; ++i) {
        if (g_c293.id[i] == id) {
            const bool changed = g_c293.mask[i] != mask;
            g_c293.mask[i] = mask;
            return changed;
        }
    }
    if (g_c293.n >= kC293Seen) {
        return false;
    }
    g_c293.id[g_c293.n] = id;
    g_c293.mask[g_c293.n++] = mask;
    return true;
}
// The reason set as text, each reason with a leading space.
void c293_reason_text(uint32_t m, char* out, size_t size) {
    snprintf(out, size, "%s%s%s%s%s%s%s", (m & kC293Hidden) != 0 ? " hidden" : "",
             (m & kC293MeshOff) != 0 ? " mesh-off" : "", (m & kC293NoCollision) != 0 ? " no-collision" : "",
             (m & kC293NoHealth) != 0 ? " health<=0" : "", (m & kC293Below) != 0 ? " below-ground" : "",
             (m & kC293ReAdded) != 0 ? " re-added" : "", (m & kC293NoGuid) != 0 ? " no-guid" : "");
}
void c293_row(const void* npc, uint32_t m, float health, int dead) {
    const Vec3 at = actor_location(npc);
    char reasons[128];
    c293_reason_text(m, reasons, sizeof(reasons));
    log::line("card293: %s t=%llu minimap marker %s guid %llu at (%.0f, %.0f, %.0f): suspect%s; vfunc 9E0 %d, "
              "health %.1f, hidden %u, mesh visible %u",
              g_client_session ? "client" : "host", static_cast<unsigned long long>(GetTickCount64()),
              class_name(npc).text, static_cast<unsigned long long>(c41_guid(npc)), at.x, at.y, at.z, reasons, dead,
              health, (m & kC293Hidden) != 0 ? 1u : 0u, (m & kC293MeshOff) != 0 ? 0u : 1u);
}
// Step 2: the entry for this actor, or a new one when `create` (null when absent or the table is full).
C293Dwell* c293_dwell_find(void* npc, bool create) {
    C31Weak key{};
    c31_weak_set(key, npc);
    for (uint32_t i = 0; i < g_c293.dwell_n; ++i) {
        C293Dwell& d = g_c293.dwell[i];
        if (d.actor.index == key.index && d.actor.serial == key.serial) {
            return &d;
        }
    }
    if (!create || g_c293.dwell_n >= kC293Dwell) {
        return nullptr;
    }
    C293Dwell& d = g_c293.dwell[g_c293.dwell_n++];
    d = C293Dwell{};
    d.actor = key;
    d.cls = class_name(npc);
    d.guid = c41_guid(npc);
    d.first_ms = GetTickCount64();
    return &d;
}
// Step 2: one listed suspect (or a known entry listed again) at this sample.
void c293_dwell_sample(void* npc, uint32_t m, float health, int dead, ULONGLONG now) {
    C293Dwell* const d = c293_dwell_find(npc, m != 0);
    if (d == nullptr) {
        return;
    }
    if (!d->listed && d->samples != 0 && dead == 1) {
        ++d->re_adds; // dropped at an earlier sample, listed again while IsDead
        d->reasons |= kC293ReAdded;
    }
    d->listed = true;
    d->seen = true;
    d->last_ms = now;
    ++d->samples;
    d->reasons |= m;
    d->health = health;
    d->dead = dead;
}
void c293_dwell_row(const C293Dwell& d, const char* what) {
    const ULONGLONG ms = d.last_ms - d.first_ms;
    g_c293.longest_ms = ms > g_c293.longest_ms ? ms : g_c293.longest_ms;
    if (g_c293.dwell_rows >= kC293DwellRowCap) {
        return;
    }
    ++g_c293.dwell_rows;
    char reasons[128];
    c293_reason_text(d.reasons, reasons, sizeof(reasons));
    log::line("card293: %s marker %s guid %llu %s after %llu ms, %u samples, reasons%s, IsDead %d, health %.1f, "
              "actor %s, re-added %u",
              g_client_session ? "client" : "host", d.cls.text, static_cast<unsigned long long>(d.guid), what,
              static_cast<unsigned long long>(ms), d.samples, reasons, d.dead, d.health,
              c31_weak_get(d.actor) != nullptr ? "alive" : "destroyed", d.re_adds);
}
// Step 2: entries listed at the last sample and not at this one have left; the first leave logs its row.
void c293_dwell_leaves() {
    bool any = false;
    for (uint32_t i = 0; i < g_c293.dwell_n; ++i) {
        C293Dwell& d = g_c293.dwell[i];
        if (d.listed && !d.seen) {
            d.listed = false;
            if (!d.left_logged) {
                d.left_logged = true;
                c293_dwell_row(d, "left");
            }
        }
        any = any || (d.listed && d.reasons != 0);
        d.seen = false;
    }
    g_c293.suspect_listed = any;
}
void c293_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    const bool due = now >= g_c293.next_ms || g_c293.suspect_listed; // step 2: every tick while a suspect is listed
    if (!due || g_npc_class == nullptr || g_st_pc_class == nullptr) {
        return;
    }
    const bool second = now >= g_c293.next_ms; // the 1 Hz sample: counted in seen/suspect, as step 1
    if (second) {
        g_c293.next_ms = now + kC293TickMs;
    }
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    const void* const pawn = pc != nullptr ? read_ptr(pc, kControllerPawn) : nullptr;
    const void* const minimap = pawn != nullptr && is_a(pc, g_st_pc_class()) ? read_ptr(pc, kPcMinimap) : nullptr;
    if (minimap == nullptr) {
        g_c293.suspect_listed = false;
        return;
    }
    const auto* const data = static_cast<const uint8_t*>(read_ptr(minimap, kMinimapActiveMarkers));
    const int32_t num = read_i32(minimap, kMinimapActiveMarkers + sizeof(void*));
    const int32_t count = data != nullptr && num > 0 ? (num < kC293MaxMarkers ? num : kC293MaxMarkers) : 0;
    for (int32_t i = 0; i < count; ++i) {
        void* const npc = read_ptr(data + static_cast<size_t>(i) * kMinimapMarkerSize, kMinimapMarkerSource);
        if (npc == nullptr || !is_a(npc, g_npc_class())) {
            continue;
        }
        float health = 0.0f;
        int dead = -1;
        const uint32_t m = c293_reasons(pawn, npc, &health, &dead);
        c293_dwell_sample(npc, m, health, dead, now);
        if (!second) {
            continue;
        }
        ++g_c293.seen;
        g_c293.suspect += m != 0 ? 1u : 0u;
        g_c293.no_guid += (m & kC293NoGuid) != 0 ? 1u : 0u;
        if (c293_changed(object_index(npc), m) && m != 0 && g_c293.rows < kC293RowCap) {
            ++g_c293.rows;
            c293_row(npc, m, health, dead);
        }
    }
    c293_dwell_leaves();
}
void c293_map_end() {
    uint32_t re_added = 0;
    for (uint32_t i = 0; i < g_c293.dwell_n; ++i) {
        const C293Dwell& d = g_c293.dwell[i];
        re_added += d.re_adds;
        if (d.listed || d.re_adds != 0) {
            c293_dwell_row(d, d.listed ? "still listed at map end" : "re-added, last left");
        }
    }
    if (g_c293.seen != 0) {
        log::line("card293: %s minimap markers %u seen, %u suspect (NPC marker samples, one per second), longest "
                  "dwell %llu ms, re-added %u, no-guid %u",
                  g_client_session ? "client" : "host", g_c293.seen, g_c293.suspect,
                  static_cast<unsigned long long>(g_c293.longest_ms), re_added, g_c293.no_guid);
    }
    g_c293 = C293{};
}

// Card 319 step 1 (diagnostic, both sides, read only): why the client minimap shows no bug blips. Every kC319EveryMs
// while the local pawn exists (the PC minimap's OriginActor, which card 058 keeps current), one row with each gate of
// USTMinimapComponent::TickComponent and ASTHUDGameplay::GatherMinimapMarkers (exe disassembly; offsets below), then a
// `missed` row for up to kC319Missed live NPCs in range that the HUD's list lacks (once per NPC per map), and a tally
// at map end. "Live npc in range" is the mod's own NPC list (c23_collect over the NPC class, health above 0, within
// the largest scan range of the local pawn), not the game's list. Nothing here calls GatherMinimapMarkers or any
// other state-changing game function; FindLargestMinimapScanningRange only reads the PC's minimap components.
constexpr size_t kHudGathered = 0x3D0;     // ASTHUDGameplay: TArray<TWeakObjectPtr<AActor>> (Gather+0x52 returns it)
constexpr size_t kHudObjectTypes = 0x3E0;  // TArray<TEnumAsByte<EObjectTypeQuery>> (Gather+0x1A5, SphereOverlapActors)
constexpr size_t kHudGatherCached = 0x3F0; // byte: return the cached list (Gather+0x48), set after a gather (+0x329)
constexpr size_t kHudGatherEvery = 0x400;  // float: the SetMinimapMarkersReadyForUpdate timer (Gather+0x33E)
constexpr size_t kMinimapScanGate = 0xD8;  // float: TickComponent+0x5D skips the scan unless it is above 0
constexpr size_t kMinimapDrawRange = 0xDC; // float: TickComponent+0x199, the marker range
constexpr size_t kNpcMinimapIcon = 0x838;  // ShouldShowOnMinimap_Implementation: this (NPC+0x7A0) + 0x98
constexpr size_t kNpcIsDeadVfunc = 0x9E0;  // ASTCharacter::IsDead on the AActor vtable; ShouldShowOnMinimap needs false
constexpr ULONGLONG kC319EveryMs = 5000;
constexpr uint32_t kC319Missed = 3;
constexpr int32_t kC319MaxTypes = 8;
constexpr int32_t kC319MaxList = 512;
using IsDeadFn = bool (*)(const void* npc);
struct C319 {
    ULONGLONG next_ms;
    uint32_t samples;
    uint32_t live;
    uint32_t gathered;
    uint32_t markers;
    uint32_t hostile;          // card 319: the live NPCs that are enemies (c37_enemy: ASTEnemyBase, not a trooper)
    uint32_t hostile_gathered;
    uint32_t hostile_markers;
    uint32_t pawn_not_same;
    uint32_t hud_not_gameplay;
    uint32_t missed_n;
    int32_t missed_id[kC319Missed];
};
C319 g_c319{};
GetHudFn g_c319_get_hud = nullptr;
ScanRangeFn g_c319_scan_range = nullptr;
StaticClassFn g_c319_hud_gameplay_class = nullptr;
ObjectTypeChannelFn g_c319_type_channel = nullptr;
ResponseToChannelFn g_c319_response = nullptr;

const char* c319_side() {
    return g_client_session ? "client" : "host";
}
// The HUD's overlap object types as "query/channel" pairs; their channels go to `channels` (count returned).
int32_t c319_object_types(const void* hud, int32_t* channels, char* out, size_t size) {
    const auto* const data = hud != nullptr ? static_cast<const uint8_t*>(read_ptr(hud, kHudObjectTypes)) : nullptr;
    const int32_t num = hud != nullptr ? read_i32(hud, kHudObjectTypes + sizeof(void*)) : 0;
    // b18 run 3: [0:] on both sides while the gather found NPCs. Gather+0x1A5 passes HUD+0x3E0 as
    // SphereOverlapActors' ObjectTypes (r9), so the read is right and the array is empty: the overlap then has no
    // object types and runs the engine's default query, so the row says that instead of an empty list.
    int w = snprintf(out, size, num == 0 && hud != nullptr ? "%d: empty, default query" : "%d:", num);
    int32_t n = 0;
    for (int32_t i = 0; data != nullptr && i < num && i < kC319MaxTypes && w > 0 && static_cast<size_t>(w) < size; ++i) {
        const int32_t channel = g_c319_type_channel != nullptr ? g_c319_type_channel(data[i]) : -1;
        channels[n++] = channel;
        w += snprintf(out + w, size - static_cast<size_t>(w), " %u/ch%d", data[i], channel);
    }
    return n;
}
// The HUD's gathered list, resolved; pointers go to `out` (count returned, capped).
int32_t c319_gathered(const void* hud, const void** out) {
    const auto* const data = static_cast<const C31Weak*>(read_ptr(hud, kHudGathered));
    const int32_t num = read_i32(hud, kHudGathered + sizeof(void*));
    int32_t n = 0;
    for (int32_t i = 0; data != nullptr && i < num && i < kC319MaxList; ++i) {
        out[n++] = c31_weak_get(data[i]);
    }
    return n;
}
int32_t c319_markers(const void* minimap, const void** out) {
    const auto* const data = static_cast<const uint8_t*>(read_ptr(minimap, kMinimapActiveMarkers));
    const int32_t num = read_i32(minimap, kMinimapActiveMarkers + sizeof(void*));
    int32_t n = 0;
    for (int32_t i = 0; data != nullptr && i < num && i < kC319MaxList; ++i) {
        out[n++] = read_ptr(data + static_cast<size_t>(i) * kMinimapMarkerSize, kMinimapMarkerSource);
    }
    return n;
}
bool c319_listed(const void* actor, const void* const* list, int32_t n) {
    for (int32_t i = 0; i < n; ++i) {
        if (list[i] == actor) {
            return true;
        }
    }
    return false;
}
// True the first time this NPC is missed this map while a row slot is left (and takes the slot).
bool c319_missed_once(int32_t id) {
    for (uint32_t i = 0; i < g_c319.missed_n; ++i) {
        if (g_c319.missed_id[i] == id) {
            return false;
        }
    }
    if (g_c319.missed_n >= kC319Missed) {
        return false;
    }
    g_c319.missed_id[g_c319.missed_n++] = id;
    return true;
}
void c319_missed_row(const void* npc, double dist, const int32_t* channels, int32_t channel_n) {
    const void* const capsule = read_ptr(npc, kActorRootComponent);
    const void* const mesh = read_ptr(npc, kC293CharacterMesh);
    char responses[96];
    int w = snprintf(responses, sizeof(responses), "%s", channel_n == 0 ? "none" : "");
    for (int32_t i = 0; i < channel_n && w > 0 && static_cast<size_t>(w) < sizeof(responses); ++i) {
        const int r = capsule != nullptr && g_c319_response != nullptr && channels[i] >= 0
                          ? g_c319_response(capsule, channels[i])
                          : -1;
        w += snprintf(responses + w, sizeof(responses) - static_cast<size_t>(w), "%sch%d=%d", i == 0 ? "" : " ",
                      channels[i], r);
    }
    const auto is_dead = reinterpret_cast<IsDeadFn>((*static_cast<void* const* const*>(npc))[kNpcIsDeadVfunc / 8]);
    const int cap_on = capsule != nullptr && g_collision_enabled != nullptr ? g_collision_enabled(capsule) : -1;
    const int cap_type = capsule != nullptr && g_collision_type != nullptr ? g_collision_type(capsule) : -1;
    const int mesh_on = mesh != nullptr && g_collision_enabled != nullptr ? g_collision_enabled(mesh) : -1;
    const int mesh_type = mesh != nullptr && g_collision_type != nullptr ? g_collision_type(mesh) : -1;
    log::line("card319: %s missed %s guid %llu at %.0f: capsule collision %d object type %d responses %s, mesh "
              "collision %d object type %d, vfunc 9E0 %d, icon %d",
              c319_side(), class_name(npc).text, static_cast<unsigned long long>(c41_guid(npc)), dist, cap_on, cap_type,
              responses, mesh_on, mesh_type, is_dead(npc) ? 1 : 0, read_ptr(npc, kNpcMinimapIcon) != nullptr ? 1 : 0);
}
struct C319Live {
    uint32_t live;
    uint32_t gathered;
    uint32_t markers;
    uint32_t hostile; // the subset that are enemies (c37_enemy), so friendly troopers do not satisfy a bug wait
    uint32_t hostile_gathered;
    uint32_t hostile_markers;
};
// Live NPCs within `range` of `local`: how many the HUD gathered and how many have a marker, all and hostile only;
// missed rows.
C319Live c319_live(void* world, const void* local, float range, const void* const* gathered, int32_t gathered_n,
                   const void* const* markers, int32_t marker_n, const int32_t* channels, int32_t channel_n) {
    static C23List list; // game thread only; too large for the stack
    c23_collect(world, local, g_npc_class(), list);
    C319Live out{};
    for (uint32_t i = 0; i < list.count; ++i) {
        const void* const npc = list.e[i].actor;
        float health = 0.0f;
        c25_dead(npc, &health);
        if (health <= 0.0f || list.e[i].dist < 0.0 || list.e[i].dist > range) {
            continue;
        }
        ++out.live;
        const bool in_list = c319_listed(npc, gathered, gathered_n);
        const bool marked = c319_listed(npc, markers, marker_n);
        out.gathered += in_list ? 1u : 0u;
        out.markers += marked ? 1u : 0u;
        const bool hostile = c37_enemy(npc);
        out.hostile += hostile ? 1u : 0u;
        out.hostile_gathered += hostile && in_list ? 1u : 0u;
        out.hostile_markers += hostile && marked ? 1u : 0u;
        if (!in_list && c319_missed_once(object_index(npc))) {
            c319_missed_row(npc, list.e[i].dist, channels, channel_n);
        }
    }
    return out;
}
void c319_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    if (now < g_c319.next_ms || g_c319_get_hud == nullptr || g_c319_scan_range == nullptr ||
        g_c319_hud_gameplay_class == nullptr || g_npc_class == nullptr || g_st_pc_class == nullptr) {
        return;
    }
    g_c319.next_ms = now + kC319EveryMs;
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    void* const minimap = pc != nullptr && is_a(pc, g_st_pc_class()) ? read_ptr(pc, kPcMinimap) : nullptr;
    const void* const local = minimap != nullptr ? read_ptr(minimap, kMinimapOrigin) : nullptr;
    if (local == nullptr) {
        return;
    }
    const void* const pc_pawn = read_ptr(pc, kControllerPawn);
    void* const hud = g_c319_get_hud(world);
    const bool gameplay = hud != nullptr && is_a(hud, g_c319_hud_gameplay_class());
    const float range = g_c319_scan_range(pc);
    static const void* gathered[kC319MaxList]; // game thread only
    static const void* markers[kC319MaxList];
    const int32_t gathered_n = gameplay ? c319_gathered(hud, gathered) : 0;
    const int32_t marker_n = c319_markers(minimap, markers);
    int32_t channels[kC319MaxTypes];
    char types[128];
    const int32_t channel_n = c319_object_types(gameplay ? hud : nullptr, channels, types, sizeof(types));
    const C319Live live = c319_live(world, local, range, gathered, gathered_n, markers, marker_n, channels, channel_n);
    const char* const pawn_text = pc_pawn == nullptr ? "null" : (pc_pawn == local ? "same" : "other");
    const double pawn_uu = pc_pawn != nullptr ? vec_dist(actor_location(pc_pawn), actor_location(local)) : -1.0;
    ++g_c319.samples;
    g_c319.live += live.live;
    g_c319.gathered += live.gathered;
    g_c319.markers += live.markers;
    g_c319.hostile += live.hostile;
    g_c319.hostile_gathered += live.hostile_gathered;
    g_c319.hostile_markers += live.hostile_markers;
    g_c319.pawn_not_same += pc_pawn != local ? 1u : 0u;
    g_c319.hud_not_gameplay += gameplay ? 0u : 1u;
    log::line("card319: %s t=%llu hud %s gameplay %d, pc pawn %s (%.0f from the local pawn), range %.0f scan %.1f draw "
              "%.0f, object types [%s], cached %d every %.2f s, gathered %d, live npc in range %u: gathered %u, in "
              "ActiveMarkers %u, hostile %u: gathered %u, in ActiveMarkers %u",
              c319_side(), static_cast<unsigned long long>(now), hud != nullptr ? class_name(hud).text : "null",
              gameplay ? 1 : 0, pawn_text, pawn_uu, range, read_f32(minimap, kMinimapScanGate),
              read_f32(minimap, kMinimapDrawRange), types,
              gameplay ? static_cast<const uint8_t*>(hud)[kHudGatherCached] : -1,
              gameplay ? read_f32(hud, kHudGatherEvery) : -1.0f, gathered_n, live.live, live.gathered, live.markers, live.hostile, live.hostile_gathered,
              live.hostile_markers);
}
// b238 (card 319 next diag): at a scenario `shot`, every live hostile NPC within the scan range of OriginActor, with
// its NetGUID (the join key across the host and client logs), its distance and whether the HUD gathered it and the
// minimap marked it. Both sides. Bounded by the collected list (C23List).
void c319_shot_hostiles(void* world, const char* shot, const void* origin, float range, const void* const* gathered,
                        int32_t gathered_n, const void* const* markers, int32_t marker_n) {
    if (origin == nullptr || g_npc_class == nullptr || range <= 0.0f) {
        log::line("card319: %s shot %s hostiles: none listed (origin %d, range %.0f)", c319_side(), shot,
                  origin != nullptr ? 1 : 0, range);
        return;
    }
    static C23List list; // game thread only; too large for the stack
    c23_collect(world, origin, g_npc_class(), list);
    uint32_t n = 0;
    for (uint32_t i = 0; i < list.count; ++i) {
        const void* const npc = list.e[i].actor;
        float health = 0.0f;
        c25_dead(npc, &health);
        if (health <= 0.0f || list.e[i].dist < 0.0 || list.e[i].dist > range || !c37_enemy(npc)) {
            continue;
        }
        ++n;
        log::line("card319: %s shot %s hostile guid %llu %s at %.0f uu: gathered %d, marked %d", c319_side(), shot,
                  static_cast<unsigned long long>(c41_guid(npc)), object_name(npc).text, list.e[i].dist,
                  c319_listed(npc, gathered, gathered_n) ? 1 : 0, c319_listed(npc, markers, marker_n) ? 1 : 0);
    }
    log::line("card319: %s shot %s hostiles: %u live within %.0f uu", c319_side(), shot, n, range);
}
// Card 319 reopen (b249, user: the bug display is the middle HUD, USTBugProximityAreaWidget). Its NativeOnInitialized
// stores the PC's minimap component at widget+0x358 and AddDynamic-binds OnNewMarker to comp+0xB8 and OnRemoveMarker
// to comp+0x180, once, and binds nothing when the PC or the component is null. These rows say, per side at each shot,
// whether the widget holds that component, how many binds the two delegates carry, how often the widget's handlers ran
// since the last shot, and how many icon widgets are live and shown.
constexpr size_t kC319AreaComp = 0x358;     // USTBugProximityAreaWidget: USTMinimapComponent* (NativeOnInitialized)
constexpr size_t kC319CompNewList = 0xB8;   // USTMinimapComponent: the OnNewMarker delegate's invocation TArray
constexpr int32_t kC319Widgets = 8;         // area widget rows per shot
constexpr int32_t kC319Icons = 256;         // icon widgets scanned per shot
constexpr uint8_t kSlateVisible = 0;        // ESlateVisibility::Visible
constexpr uint8_t kSlateHitTestInvisible = 3;
constexpr uint8_t kSlateSelfHitTestInvisible = 4;
C319MarkerFn g_orig_c319_new_marker = nullptr;
C319MarkerFn g_orig_c319_remove_marker = nullptr;
StaticClassFn g_c319_area_class = nullptr;
StaticClassFn g_minimap_class = nullptr; // card 319 b251: sync_client_minimap_origin
StaticClassFn g_c319_icon_class = nullptr;
struct C319Calls {
    uint32_t added;   // OnNewMarker calls since the last shot row
    uint32_t removed; // OnRemoveMarker calls since the last shot row
};
C319Calls g_c319_calls{}; // game thread

void detour_c319_new_marker(void* widget, int32_t index) {
    ++g_c319_calls.added;
    g_orig_c319_new_marker(widget, index);
}
void detour_c319_remove_marker(void* widget, int32_t index) {
    ++g_c319_calls.removed;
    g_orig_c319_remove_marker(widget, index);
}

// A UWidget's Visibility byte, -1 when the class has no such property.
int32_t c319_visibility(const void* widget) {
    const uint8_t* const prop = prop_find_prop(read_ptr(widget, kObjectClass), "Visibility");
    return prop != nullptr ? static_cast<const uint8_t*>(widget)[static_cast<size_t>(read_i32(prop, kPropertyOffset))]
                           : -1;
}
bool c319_shown(int32_t visibility) {
    return visibility == kSlateVisible || visibility == kSlateHitTestInvisible ||
           visibility == kSlateSelfHitTestInvisible;
}

// pc_comp: the own PC's minimap component (pc+kPcMinimap), null when there is none.
void c319_widget_rows(const void* pc_comp, const char* shot) {
    static const void* areas[kC319Widgets]; // game thread only
    static const void* icons[kC319Icons];
    const int32_t area_n = g_c319_area_class != nullptr ? objects_of_class(g_c319_area_class(), areas, kC319Widgets, true)
                                                        : -1;
    const int32_t icon_n = g_c319_icon_class != nullptr ? objects_of_class(g_c319_icon_class(), icons, kC319Icons, true)
                                                        : -1;
    int32_t shown = 0;
    for (int32_t i = 0; i < icon_n && i < kC319Icons; ++i) {
        shown += c319_shown(c319_visibility(icons[i])) ? 1 : 0;
    }
    const unsigned long long now = GetTickCount64();
    log::line("card319: %s t=%llu shot %s widgets %d calls new %u rm %u icons %d shown %d%s", c319_side(), now, shot,
              area_n, g_c319_calls.added, g_c319_calls.removed, icon_n, shown,
              area_n > kC319Widgets || icon_n > kC319Icons ? " (rows capped)" : "");
    for (int32_t i = 0; i < area_n && i < kC319Widgets; ++i) {
        const void* const comp = read_ptr(areas[i], kC319AreaComp);
        log::line("card319: %s t=%llu shot %s widget %d %s vis %d comp %s %p same_as_pc %s (pc comp %p, owner %s) "
                  "binds new %d origin %p (%s) calls new %u rm %u icons %d shown %d",
                  c319_side(), now, shot, i, object_name(areas[i]).text, c319_visibility(areas[i]),
                  comp != nullptr ? class_name(comp).text : "null", comp, comp != nullptr && comp == pc_comp ? "y" : "n",
                  pc_comp, comp != nullptr ? object_name(read_ptr(comp, kObjectOuter)).text : "null",
                  comp != nullptr ? read_i32(comp, kC319CompNewList + kArrayNum) : -1,
                  comp != nullptr ? read_ptr(comp, kMinimapOrigin) : nullptr,
                  comp != nullptr && read_ptr(comp, kMinimapOrigin) != nullptr
                      ? class_name(read_ptr(comp, kMinimapOrigin)).text : "null", g_c319_calls.added,
                  g_c319_calls.removed, icon_n, shown);
    }
    g_c319_calls = C319Calls{};
}

// b232 (card 319 reopened, user: host radar shows bugs, client radar none): one row at every scenario `shot`, both
// sides, no cap: each gate the two minimap functions read, then every gathered actor with the ShouldShowOnMinimap
// inputs (IsDead on the vtable at +0x9E0, the icon at NPC+0x838) and its distance to OriginActor.
void c319_shot_row(void* world, const char* shot) {
    void* const pc = world != nullptr && g_st_pc_class != nullptr ? own_pc(world) : nullptr;
    void* const minimap = pc != nullptr && is_a(pc, g_st_pc_class()) ? read_ptr(pc, kPcMinimap) : nullptr;
    const void* const origin = minimap != nullptr ? read_ptr(minimap, kMinimapOrigin) : nullptr;
    void* const hud = g_c319_get_hud != nullptr ? g_c319_get_hud(world) : nullptr;
    const bool gameplay = hud != nullptr && g_c319_hud_gameplay_class != nullptr && is_a(hud, g_c319_hud_gameplay_class());
    static const void* gathered[kC319MaxList]; // game thread only
    static const void* markers[kC319MaxList];
    const int32_t gathered_n = gameplay ? c319_gathered(hud, gathered) : 0;
    const int32_t marker_n = minimap != nullptr ? c319_markers(minimap, markers) : 0;
    log::line("card319: %s t=%llu shot %s: origin %s, range %.0f scan %.1f draw %.0f, cached %d, gathered %d, "
              "ActiveMarkers %d",
              c319_side(), static_cast<unsigned long long>(GetTickCount64()), shot,
              origin != nullptr ? object_name(origin).text : "null",
              pc != nullptr && g_c319_scan_range != nullptr ? g_c319_scan_range(pc) : -1.0f,
              minimap != nullptr ? read_f32(minimap, kMinimapScanGate) : -1.0f,
              minimap != nullptr ? read_f32(minimap, kMinimapDrawRange) : -1.0f,
              gameplay ? static_cast<const uint8_t*>(hud)[kHudGatherCached] : -1, gathered_n, marker_n);
    for (int32_t i = 0; i < gathered_n; ++i) { // bounded by kC319MaxList (c319_gathered)
        const void* const a = gathered[i];
        const bool npc = a != nullptr && g_npc_class != nullptr && is_a(a, g_npc_class());
        const auto is_dead =
            npc ? reinterpret_cast<IsDeadFn>((*static_cast<void* const* const*>(a))[kNpcIsDeadVfunc / 8]) : nullptr;
        log::line("card319: %s shot %s gathered %d: %s npc %d dead %d icon %d, %.0f uu from origin, marked %d",
                  c319_side(), shot, i, a != nullptr ? object_name(a).text : "null", npc ? 1 : 0,
                  is_dead != nullptr && is_dead(a) ? 1 : 0, npc && read_ptr(a, kNpcMinimapIcon) != nullptr ? 1 : 0,
                  a != nullptr && origin != nullptr ? vec_dist(actor_location(a), actor_location(origin)) : -1.0,
                  c319_listed(a, markers, marker_n) ? 1 : 0);
    }
    c319_shot_hostiles(world, shot, origin, pc != nullptr && g_c319_scan_range != nullptr ? g_c319_scan_range(pc) : -1.0f,
                       gathered, gathered_n, markers, marker_n);
    c319_widget_rows(minimap, shot);
}
void c319_map_end() {
    if (g_c319.samples != 0) {
        log::line("card319: %s samples %u, live npc in range %u, gathered %u, in ActiveMarkers %u, hostile %u: "
                  "gathered %u, in ActiveMarkers %u, samples with pc pawn not same %u, samples with hud not gameplay %u",
                  c319_side(), g_c319.samples, g_c319.live, g_c319.gathered, g_c319.markers, g_c319.hostile,
                  g_c319.hostile_gathered, g_c319.hostile_markers, g_c319.pawn_not_same,
                  g_c319.hud_not_gameplay);
    }
    g_c319 = C319{};
}

} // namespace ubw::hooks
