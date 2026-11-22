// Objectives: co-op objectives on the client (card 049), the host snapshot after a checkpoint, the
// journal compaction (card 066), the client replay steps, the actors of each replayed Add (card 070),
// the client objective map and list rebuilt to the host's (card 053), the level trigger log
// (card 065) and the client minimap icons (card 068).

#include "ubw/hooks_internal.h"
#include "ue/config.h"

#include <cmath>
#include <string.h>

namespace ubw::hooks {

// --- Card 049: co-op objectives on the client ---
// Objectives are host-only state (card 048): the level Blueprint calls the static USTObjectiveUtilityLibrary
// functions on the host only, and USTLevelObjectiveSubsystem (one per world) replicates nothing. Every library
// function that changes objectives is detoured on both sides; the original always runs first, unchanged.
// Step 1 (audit, log only): one line per call with the ids as text, the bools in signature order, the count, the
// actor-array Num, the nesting depth of library calls and the callers. The reflected parameter names of every
// function are logged once, and each side logs its objective subsystem and ASTLevelObjectiveManager after a map load.
enum class C49Kind : uint32_t {
    Add,
    AddSub,
    AddCount,
    AddCountSubs,
    AddCountSub,
    AddCountSubSubs,
    Complete,
    CompleteAny,
    CompleteSub,
    Increment,
    Visibility,
    Callout,
    SetCount,      // mirror only (snapshot): Increment up to the host's current count
    SnapshotBegin, // mirror only
    SnapshotEnd,   // mirror only; count = the host's objectives
    Chapter,       // mirror only; count = the host's game instance chapter, flags = its level (no flag bits)
    HostSet,       // mirror only (card 499): name[0] = an objective the host completed natively in the next group
    Count
};
constexpr size_t kC49LibraryKinds = 12; // the kinds before SetCount are library functions
constexpr const wchar_t* kC49Function[] = {
    L"AddObjective",         L"AddSubObjective",          L"AddCountObjective", L"AddCountObjective_SubObjectives",
    L"AddCountSubObjective", L"AddCountSubObjective_SubObjectives", L"CompleteObjective", L"CompleteAnyObjective",
    L"CompleteSubObjective", L"IncrementObjectiveCount",  L"SetObjectiveVisibility", L"CalloutObjective"};
constexpr const char* kC49KindText[] = {
    "AddObjective",         "AddSubObjective",          "AddCountObjective", "AddCountObjective_SubObjectives",
    "AddCountSubObjective", "AddCountSubObjective_SubObjectives", "CompleteObjective", "CompleteAnyObjective",
    "CompleteSubObjective", "IncrementObjectiveCount",  "SetObjectiveVisibility", "CalloutObjective",
    "SetCount", "SnapshotBegin", "SnapshotEnd", "Chapter", "HostSet"};
constexpr int32_t kC49Bools[] = {4, 5, 4, 4, 5, 5, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0}; // bool parameters per kind
static_assert(sizeof(kC49Function) / sizeof(kC49Function[0]) == kC49LibraryKinds);
static_assert(static_cast<size_t>(C49Kind::SetCount) == kC49LibraryKinds);
static_assert(sizeof(kC49KindText) / sizeof(kC49KindText[0]) == static_cast<size_t>(C49Kind::Count));
static_assert(sizeof(kC49Bools) / sizeof(kC49Bools[0]) == static_cast<size_t>(C49Kind::Count));

constexpr int32_t kC49ListedObjectives = 32; // per world check
constexpr int32_t kC49MaxParams = 16;
constexpr uint32_t kC49AuditLines = 240;

C49AddFn g_orig_c49_add = nullptr;
C49AddSubFn g_orig_c49_add_sub = nullptr;
C49AddCountFn g_orig_c49_add_count = nullptr;
C49AddCountSubsFn g_orig_c49_add_count_subs = nullptr;
C49AddCountSubFn g_orig_c49_add_count_sub = nullptr;
C49AddCountSubSubsFn g_orig_c49_add_count_sub_subs = nullptr;
C49NameFn g_orig_c49_complete = nullptr;
C49NameFn g_orig_c49_complete_any = nullptr;
C49Name2Fn g_orig_c49_complete_sub = nullptr;
C49NameFn g_orig_c49_increment = nullptr;
C49VisibilityFn g_orig_c49_visibility = nullptr;
C49NameFn g_orig_c49_callout = nullptr;
C49CheckAllFn g_orig_c49_check_all = nullptr;
C49GateFn g_orig_c49_gate = nullptr;
C49SaveFn g_orig_c49_save = nullptr;
C49GetSubsystemFn g_c49_get_subsystem = nullptr;
C49ExistsFn g_c49_exists = nullptr;
StaticClassFn g_c49_library_class = nullptr;
StaticClassFn g_c49_objective_class = nullptr;
StaticClassFn g_c49_count_class = nullptr;
StaticClassFn g_c49_manager_class = nullptr;
struct C49Actors { // TArray<AActor*> header: data, Num, Max
    void* const* data;
    int32_t num;
    int32_t max;
};

// One library call: what the detour saw, enough to replay it.
struct C49Call {
    C49Kind kind;
    uint64_t name[2];   // FName values (this process); name[1] is 0 for the one-name functions
    uint32_t flags;     // bit i = the i-th bool parameter in signature order
    int32_t count;      // the int32 parameter; 0 when there is none
    const void* actors; // the const TArray<AActor*>& (null for the functions without one)
};

C49State g_c49{}; // game thread

uint32_t c49_bits(bool b0, bool b1, bool b2, bool b3, bool b4) {
    return (b0 ? 1u : 0u) | (b1 ? 2u : 0u) | (b2 ? 4u : 0u) | (b3 ? 8u : 0u) | (b4 ? 16u : 0u);
}

// The objective subsystem of the world `ctx` lives in, or null.
void* c49_subsystem(const void* ctx) {
    const void* const world = ctx != nullptr ? g_actor_get_world(ctx) : nullptr;
    return world != nullptr ? g_c49_get_subsystem(world) : nullptr;
}

// The subsystem's objective map, or false.
bool c49_objectives(const void* subsystem, SparseView& v) {
    return subsystem != nullptr &&
           sparse_view(static_cast<const uint8_t*>(subsystem) + kSubsystemObjectives, kC49MaxObjectives, v);
}

// The number of objectives in the subsystem, or -1.
int32_t c49_objective_count(const void* subsystem) {
    SparseView v{};
    if (!c49_objectives(subsystem, v)) {
        return -1;
    }
    int32_t n = 0;
    for (int32_t i = 0; i < v.count; ++i) {
        n += sparse_allocated(v, i) ? 1 : 0;
    }
    return n;
}

// One function's reflected parameters as "name:type@offset ...".
void c49_param_text(const void* function, char* out, size_t cap) {
    size_t used = 0;
    out[0] = '\0';
    const void* field = read_ptr(function, kStructChildProperties);
    for (int32_t i = 0; i < kC49MaxParams && field != nullptr && used < cap; ++i) {
        const void* const field_class = read_ptr(field, sizeof(void*)); // FField::ClassPrivate; FFieldClass::Name +0
        const int w = snprintf(out + used, cap - used, " %s:%s@0x%X",
                               fname_text(static_cast<const uint8_t*>(field) + kFieldName).text,
                               field_class != nullptr ? fname_text(field_class).text : "?",
                               static_cast<unsigned>(read_i32(field, kPropertyOffset)));
        used += w > 0 ? static_cast<size_t>(w) : cap;
        field = read_ptr(field, kFieldNext);
    }
}

// Once per process: every library function's reflected parameter list (UFunction FProperty chain).
void c49_log_params() {
    if (g_c49.params_logged) {
        return;
    }
    g_c49.params_logged = true;
    const void* const cls = g_c49_library_class();
    const void* const cdo = cls != nullptr ? read_ptr(cls, kClassDefaultObject) : nullptr;
    for (size_t k = 0; cdo != nullptr && k < kC49LibraryKinds; ++k) {
        uint64_t name = 0;
        g_fname_ctor(&name, kC49Function[k], kFNameAdd);
        const void* const function = g_find_function(cdo, name);
        char text[512];
        if (function != nullptr) {
            c49_param_text(function, text, sizeof(text));
        }
        log::line("card049: %s UFunction %s %p params:%s", side(), kC49KindText[k], function,
                  function != nullptr ? text : " (not found)");
    }
    if (cdo == nullptr) {
        log::line("WARN card049: %s no USTObjectiveUtilityLibrary default object; parameter names not logged", side());
    }
}

// The bools of a call as "1010" (signature order).
void c49_bool_text(const C49Call& c, char* out) {
    const int32_t n = kC49Bools[static_cast<size_t>(c.kind)];
    for (int32_t i = 0; i < n; ++i) {
        out[i] = (c.flags >> i) & 1u ? '1' : '0';
    }
    out[n] = '\0';
}

// Steps 2-5: the mirror. The host sends each library call (after its original ran) as one ChanOp::Objective frame
// to every remote PC that already has this map's journal; the client replays it through the same native function
// (the trampoline, so its own detour stays quiet) with its own PC as the world context and an empty actor array.
// The client never sends (echo guard: g_client_session). The host journals every frame since the map load; a
// remote PC's first Pong of the map gets the whole journal once, and only then the live frames, so no frame reaches
// a client twice (a repeated Increment would double a count). After a checkpoint (SetObjectiveMapPostSave) the host
// rebuilds the journal from its objective map and sends it as a snapshot. Field offsets: see kObjective* above
// (static, from the getters named there) and the count fields below; the world check logs each objective's own id
// next to its map key, which confirmed the element stride and the id, parent, marker, complete and list offsets
// (card049a, 14 of 14 objectives).
constexpr uint32_t kC49HostCompleteBit = 1u << 5; // snapshot Add frames: the objective is complete on the host
constexpr uint32_t kC49SnapshotBit = 1u << 6;     // the frame is part of a checkpoint snapshot
// Card 305 (b9): live frames: the call's target (or a sub-objective Add's parent) existed on the host when the call
// ran. Without it the host's own call did nothing either, so a client lacking the id is in step, not behind.
constexpr uint32_t kC49HostExistsBit = 1u << 7;

constexpr int32_t kC70MaxActors = 32; // card 070: actor names per ObjectiveActors frame
constexpr int32_t kC49MaxIncrements = 64; // per snapshot count frame
constexpr int32_t kC49MaxSupers = 4;
constexpr int32_t kC49MaxBound = 8; // bound entries listed per delegate
constexpr uint32_t kC49MirrorLines = 240;

constexpr ULONGLONG kC49LoadShotMs = 6000;   // after the first objective of a map
constexpr ULONGLONG kC49CompleteMs = 10000;  // test: after the first objective of the map, in test phase 1

constexpr size_t kC49ObjectFlags = 0x8;           // UObjectBase::ObjectFlags (the library's Complete tests bit 30)

static_assert(kChanNamedFrame <= kChanTextFrame); // card 070: one C49Frame holds either kind

C49Journal g_c49_journal{}; // host, game thread
C49Journal g_c66_saved{};   // card 066: the journal before a compaction, put back when the snapshot does not fit
int32_t g_c49_cap = kC49JournalCap; // card 066: frames the journal holds (UBWCOOP_C66_CAP test cap, at most the array)
static_assert(kC49JournalCap == 512); // config::read_c66_cap() returns at most 512
C49Frame g_c49_wire{};      // the live frame being sent
C49Mirror g_c49m{};
bool g_c49_test = false;            // UBWCOOP_C49_TEST set
wchar_t g_c49_test_id[config::kMaxWeaponLen + 1] = {}; // the objective the host test completes
ULONGLONG g_c53_event_ms = 0; // host test: when the post-checkpoint completion runs (0 = none due); kept over map arms
bool g_c53_rebuilt = false;    // client: card 053's list rebuild ran in this process (test dumps follow it)
ULONGLONG g_c53_after_ms = 0;  // client test: when the dump after a post-rebuild event runs (0 = none due)

bool c49_line() {
    return note_event_cap(g_c49m.lines, kC49MirrorLines, "card049 mirror");
}

// Card 097: the completion and count lines are never capped; every other kind takes c49_line().
bool c49_kind_line(C49Kind k) {
    return k == C49Kind::Complete || k == C49Kind::CompleteAny || k == C49Kind::CompleteSub ||
           k == C49Kind::Increment || c49_line();
}

void c49_shot(ULONGLONG at_ms, const char* what) {
    if (!g_c49_test || g_c49m.shots >= kC49Shots) {
        return;
    }
    g_c49m.shot[g_c49m.shots++] = C49Shot{at_ms + (g_client_session ? kC49ClientShotLagMs : 0), what};
}

void c49_shot_step(void* pc, ULONGLONG now) {
    for (int32_t i = g_c49m.shots - 1; i >= 0; --i) {
        if (now >= g_c49m.shot[i].at_ms) {
            log::line("card049: %s t=%llu screenshot (showui): %s", side(), static_cast<unsigned long long>(now),
                      g_c49m.shot[i].what);
            request_shot(pc, true);
            g_c49m.shot[i] = g_c49m.shot[--g_c49m.shots];
            return; // one shot per tick
        }
    }
}

// The FName as wire text: the entry text plus "_<Number - 1>" when Number is set (FName layout: comparison index,
// then Number). Returns the length, or -1 for a name over kChanNameMax chars or not printable ASCII (fname_text
// maps those chars to '?' and cuts the entry at kNameChars - 1).
int32_t c49_name_text(uint64_t id, char (&out)[kChanNameMax + 1]) {
    const ObjectName base = fname_text(&id);
    const uint32_t number = static_cast<uint32_t>(id >> 32);
    const int w = number != 0 ? snprintf(out, sizeof(out), "%s_%u", base.text, number - 1)
                              : snprintf(out, sizeof(out), "%s", base.text);
    if (w <= 0 || w > kChanNameMax || strlen(base.text) >= kNameChars - 1 || strchr(out, '?') != nullptr) {
        return -1;
    }
    return w;
}

// The Objective frame for c in f: kind, flags, count, the two name lengths (each value + 1), then the name chars
// (each + 1). False, logged, when a name or a value does not fit.
bool c49_frame(const C49Call& c, C49Frame& f) {
    char text[2][kChanNameMax + 1] = {};
    const int32_t l1 = c.name[0] != 0 ? c49_name_text(c.name[0], text[0]) : 0;
    const int32_t l2 = c.name[1] != 0 ? c49_name_text(c.name[1], text[1]) : 0;
    const int32_t limit = static_cast<int32_t>(kChanArgMax) - 1;
    if (l1 < 0 || l2 < 0 || c.count < 0 || c.count >= limit || c.flags >= static_cast<uint32_t>(limit)) {
        log::line("WARN card049: %s %s(%s, %s) not sent: name length %d/%d (max %d, ASCII only), count %d, flags 0x%X",
                  side(), kC49KindText[static_cast<size_t>(c.kind)], fname_text(&c.name[0]).text,
                  fname_text(&c.name[1]).text, l1, l2, kChanNameMax, c.count, c.flags);
        return false;
    }
    const uint32_t head[kChanNamedHead] = {static_cast<uint32_t>(c.kind), c.flags, static_cast<uint32_t>(c.count),
                                               static_cast<uint32_t>(l1), static_cast<uint32_t>(l2)};
    memcpy(f.w, kChanPrefix, kChanPrefixLen * sizeof(wchar_t));
    f.w[kChanOpAt] = static_cast<wchar_t>(ChanOp::Objective);
    for (int32_t i = 0; i < kChanNamedHead; ++i) {
        f.w[kChanOpAt + 1 + i] = static_cast<wchar_t>(head[i] + 1);
    }
    wchar_t* const chars = f.w + kChanOpAt + 1 + kChanNamedHead;
    for (int32_t i = 0; i < l1; ++i) {
        chars[i] = static_cast<wchar_t>(text[0][i] + 1);
    }
    for (int32_t i = 0; i < l2; ++i) {
        chars[l1 + i] = static_cast<wchar_t>(text[1][i] + 1);
    }
    f.num = kChanOpAt + 1 + kChanNamedHead + l1 + l2 + 1;
    f.w[f.num - 1] = L'\0';
    return true;
}

bool c49_synced(const void* pc) {
    const int32_t index = object_index(pc);
    for (int32_t i = 0; pc != nullptr && i < g_c49m.n_synced; ++i) {
        if (g_c49m.synced[i] == index) {
            return true;
        }
    }
    return false;
}

// Host: one frame to every remote PC that already has the journal. Returns the PCs sent to.
uint32_t c49_send(const void* world_context, const C49Frame& f) {
    const WireString s{f.w, f.num, f.num};
    uint32_t sent = 0;
    for (int32_t i = 0; i < kC17Slots; ++i) {
        void* const pc = g_get_player_controller(world_context, i);
        sent += (c49_synced(pc) && chan_send_client(pc, s)) ? 1 : 0;
    }
    return sent;
}

void c49_record(const C49Frame& f) {
    if (g_c49_journal.n < g_c49_cap) {
        g_c49_journal.frame[g_c49_journal.n++] = f;
        g_c49_journal.peak = g_c49_journal.n > g_c49_journal.peak ? g_c49_journal.n : g_c49_journal.peak;
        return;
    }
    ++g_c49_journal.dropped;
    if (!g_c49_journal.full_logged && !g_c49_journal.record_only) { // a compaction that overflows logs its own WARN
        g_c49_journal.full_logged = true;
        log::line("WARN card049: %s journal full (%d frames): later frames are sent live but not recorded", side(),
                  g_c49_cap);
    }
}

// The journal starts over (a map load or a snapshot). Card 066's compact_off is the caller's to clear.
void c49_journal_reset() {
    g_c49_journal.n = 0;
    g_c49_journal.dropped = 0;
    g_c49_journal.full_logged = false;
}

bool c70_names_frame(const C49Call& c, C49Frame& f); // card 070
bool c66_compact(const void* world_context, const C49Call& c); // card 066

// Host: journal one call and send it live. An Add with actors is preceded by its card 070 ObjectiveActors frame,
// so the client holds the names when it replays the Add. Card 066: a library call that would not fit first compacts
// the journal; the compaction covers the call, so it is then sent live only. During a compaction nothing is sent.
void c49_emit(const void* world_context, const C49Call& c) {
    if (c332_in_leaving_world(world_context)) { // card 332: the old world's teardown calls are not journalled or sent
        c332_journal_skipped();
        return;
    }
    const bool covered = c66_compact(world_context, c); // before g_c49_wire: the compaction emits through here too
    if (!c49_frame(c, g_c49_wire)) {
        return;
    }
    static C49Frame names{};
    const bool live = !g_c49_journal.record_only;
    if (c70_names_frame(c, names)) {
        if (!covered && g_c49_journal.n + 1 < g_c49_cap) {
            c49_record(names); // only with room for its Objective frame too, so the journal never ends on one
        }
        if (live) {
            c49_send(world_context, names);
        }
    }
    if (!covered) {
        c49_record(g_c49_wire);
    }
    if (!live) {
        return;
    }
    const uint32_t sent = c49_send(world_context, g_c49_wire);
    if (sent > 0 && c49_kind_line(c.kind)) {
        log::line("card049: host sent %s(%s%s%s) flags 0x%X count %d to %u pc(s); journal %d", 
                  kC49KindText[static_cast<size_t>(c.kind)], fname_text(&c.name[0]).text, c.name[1] != 0 ? ", " : "",
                  c.name[1] != 0 ? fname_text(&c.name[1]).text : "", c.flags, c.count, sent, g_c49_journal.n);
    }
}

// Host: the game instance chapter and level, the first frame of each map's journal and of each snapshot.
// The objective texts come from the chapter's DataTable (USTLevelObjectiveSubsystem::GetLevelObjectiveDataTable
// 0x0515A4C0 reads the game instance +0x410/+0x414); a net client's game instance keeps -1/-1, so without this
// frame every replayed objective has empty text and the client panel shows no rows.
void c49_emit_chapter(const void* world_context) {
    const auto* const gi = static_cast<const uint8_t*>(g_get_st_game_instance(world_context));
    const int32_t chapter = gi != nullptr ? read_i32(gi, kGameInstanceChapter) : -1;
    const int32_t level = gi != nullptr ? read_i32(gi, kGameInstanceLevel) : -1;
    if (chapter < 0 || level < 0) {
        log::line("WARN card049: host chapter frame not sent: game instance %p, chapter %d, level %d", gi, chapter,
                  level);
        return;
    }
    log::line("card049: host chapter frame: chapter %d level %d", chapter, level);
    c49_emit(world_context, C49Call{C49Kind::Chapter, {0, 0}, static_cast<uint32_t>(level), chapter, nullptr});
}

const uint8_t* c49_find(const void* subsystem, uint64_t id);

// Card 253: a completion's objective (name[1] for CompleteSub, else name[0]) when it is a count objective, else null.
bool c253_completion(C49Kind kind) {
    return kind == C49Kind::Complete || kind == C49Kind::CompleteAny || kind == C49Kind::CompleteSub;
}
uint8_t* c253_count_objective(const void* subsystem, const C49Call& c) {
    const uint8_t* const o = c253_completion(c.kind)
                                 ? c49_find(subsystem, c.name[c.kind == C49Kind::CompleteSub ? 1 : 0])
                                 : nullptr;
    return o != nullptr && is_a(o, g_c49_count_class()) ? const_cast<uint8_t*>(o) : nullptr;
}

constexpr uint32_t kC253Lines = 40; // client, per map
uint32_t g_c253_lines = 0;          // reset on the map's first Objective frame (g_c49m.frames 0)

// Card 253, client, before a live completion's replay: the host's count (the frame's count field) when higher than
// ours goes into kObjectiveCountCurrent, so the game's own completion events read it. Never lowered here.
void c253_take_host_count(const void* subsystem, const C49Call& c) {
    uint8_t* const o = (c.flags & kC49SnapshotBit) == 0 ? c253_count_objective(subsystem, c) : nullptr;
    const int32_t have = o != nullptr ? read_i32(o, kObjectiveCountCurrent) : 0;
    if (o == nullptr || c.count <= have) {
        return;
    }
    memcpy(o + kObjectiveCountCurrent, &c.count, sizeof(c.count));
    if (note_event_cap(g_c253_lines, kC253Lines, "card253 client count")) {
        log::line("card253: client count %s %d to %d before %s",
                  fname_text(&c.name[c.kind == C49Kind::CompleteSub ? 1 : 0]).text, have, c.count,
                  kC49KindText[static_cast<size_t>(c.kind)]);
    }
}

// Host, after each library call's original. The client never sends.
void c49_mirror(void* ctx, const C49Call& c) {
    if (g_client_session) {
        return;
    }
    C49Call e = c;
    const uint8_t* const counted = c253_count_objective(c49_subsystem(ctx), c);
    if (counted != nullptr) { // card 253: a counted objective's completion carries the host's current count
        e.count = read_i32(counted, kObjectiveCountCurrent);
    }
    if (g_c49m.first_add_ms == 0 && e.kind <= C49Kind::AddCountSubSubs) {
        g_c49m.first_add_ms = GetTickCount64();
        c49_shot(g_c49m.first_add_ms + kC49LoadShotMs, "objectives after the map load (host)");
        c49_emit_chapter(ctx);
    }
    c49_emit(ctx, e);
}

bool c49_decode_name(const wchar_t* chars, int32_t len, uint64_t& out);

// Card 435 (run c423b131a: a client that rejoined 220 s into C2M1 got 137 journal frames and showed the banners
// "Touchdown / Proceed out of the LZ" and "Touchdown / Secure the LZ" for objectives long complete): the journal holds
// each call with its popup bools (bShouldPopupOnAdd, bShouldPopupOnCompletion: bits 2-3 of a root Add, 3-4 of a
// sub-objective Add) and each CalloutObjective, so a late joiner replayed every banner of the mission so far.
// Journal frame f as a late joiner gets it, in out: an Add of an objective complete on the host now loses its popup
// bools (its Complete frame follows in the same replay); an objective still open keeps them. False = not sent (a
// CalloutObjective: a one-time banner, not state). Card 070 actors frames pass unchanged.
// Run c423b158a: the client's own level script still called out LOS_O03_SO03 ("Cut Me Some Flak" banner, 3 s after
// the rejoin): its OnObjectiveComplete handler runs inside the replayed CompleteSubObjective(LOS_O03, LOS_O03_SO02)
// and calls CalloutObjective there, where card 140 lets it through. So every library-call frame of the join replay
// carries kC435JoinBit, and the client skips a CalloutObjective made while it replays such a frame (c435_skip_callout).
constexpr uint32_t kC435JoinBit = 1u << 8; // the frame is part of a late joiner's journal replay (below kChanArgMax)
struct C435Client {
    uint32_t join;    // join-replay frames being replayed now (nesting)
    uint32_t skipped; // per process: callouts skipped inside them
    uint32_t lines;
};
C435Client g_c435{}; // game thread
constexpr uint32_t kC435Lines = 24;

// Client, in the CalloutObjective detour: true (logged, capped) when the call comes from inside a join-replay frame.
bool c435_skip_callout(uint64_t id, const void* ret) {
    if (!g_client_session || g_c435.join == 0) {
        return false;
    }
    ++g_c435.skipped;
    if (note_event_cap(g_c435.lines, kC435Lines, "card435 join replay callouts")) {
        log::line("card435: client t=%llu skipped CalloutObjective(%s) inside the join replay: a one-time banner of "
                  "the mission so far (%u skips so far); caller exe+0x%llX",
                  static_cast<unsigned long long>(GetTickCount64()), fname_text(&id).text, g_c435.skipped,
                  exe_rva(ret));
    }
    return true;
}
struct C49JoinTally {
    int32_t quiet;    // Adds sent with their popup bools cleared
    int32_t callouts; // CalloutObjective frames not sent
    int32_t marked;   // frames sent with kC435JoinBit
    size_t used;      // chars in names
    char names[256];  // b135: the objective name of each callout not sent, for the replay row
};

// The callout's objective name, appended to t.names for the replay row.
void c435_note_callout(const wchar_t* head, C49JoinTally& t) {
    ++t.callouts;
    const int32_t len = head[3] - 1; // chars are each + 1, printable ASCII (c49_decode_name)
    const wchar_t* const chars = head + kChanNamedHead;
    if (t.used + 2 >= sizeof(t.names)) {
        return;
    }
    t.names[t.used++] = ' ';
    for (int32_t i = 0; i < len && i < kChanNameMax && t.used + 1 < sizeof(t.names); ++i) {
        const wchar_t c = static_cast<wchar_t>(chars[i] - 1);
        t.names[t.used++] = c >= 0x20 && c <= 0x7E ? static_cast<char>(c) : '?';
    }
    t.names[t.used] = '\0';
}

// An Add frame's flags with the popup bools cleared when its objective is complete on the host now; else unchanged.
uint32_t c435_add_flags(const void* pc, C49Kind kind, const wchar_t* head, uint32_t flags, C49JoinTally& t) {
    const bool sub = kind == C49Kind::AddSub || kind == C49Kind::AddCountSub || kind == C49Kind::AddCountSubSubs;
    const int32_t l1 = head[3] - 1;
    const int32_t l2 = head[4] - 1;
    const wchar_t* const chars = head + kChanNamedHead;
    uint64_t id = 0;
    const bool named = sub ? c49_decode_name(chars + l1, l2, id) : c49_decode_name(chars, l1, id);
    const uint8_t* const o = named && id != 0 ? c49_find(c49_subsystem(pc), id) : nullptr;
    const uint32_t popups = sub ? 0x18u : 0xCu;
    if (o == nullptr || o[kObjectiveComplete] == 0 || (flags & popups) == 0) {
        return flags;
    }
    ++t.quiet;
    return flags & ~popups;
}

bool c49_join_frame(const void* pc, const C49Frame& f, C49Frame& out, C49JoinTally& t) {
    out = f;
    if (f.w[kChanOpAt] != static_cast<wchar_t>(ChanOp::Objective)) {
        return true;
    }
    const wchar_t* const head = f.w + kChanOpAt + 1; // kind, flags, count, the two name lengths (each + 1)
    const C49Kind kind = static_cast<C49Kind>(head[0] - 1);
    if (kind == C49Kind::Callout) {
        c435_note_callout(head, t);
        return false;
    }
    if (kind >= C49Kind::SetCount) { // mirror-only kinds: Chapter carries the level in its flags
        return true;
    }
    uint32_t flags = static_cast<uint32_t>(head[1] - 1);
    if (kind <= C49Kind::AddCountSubSubs) {
        flags = c435_add_flags(pc, kind, head, flags, t);
    }
    ++t.marked;
    out.w[kChanOpAt + 2] = static_cast<wchar_t>((flags | kC435JoinBit) + 1);
    return true;
}

// Host: a remote PC's first Pong of this map (card 326: the registry's "objective" join replay, from
// sync_join_replay). It gets the whole journal once, then the live frames.
void c49_join_replay(void* pc) {
    if (g_client_session || pc == nullptr || sprint_side(pc) != kSideServerCopy || c49_synced(pc)) {
        return;
    }
    if (g_c49m.n_synced >= kC17Slots) {
        log::line("WARN card049: host journal replay to %s skipped: %d PCs synced already", object_name(pc).text,
                  g_c49m.n_synced);
        return;
    }
    g_c49m.synced[g_c49m.n_synced++] = object_index(pc);
    c114_send(pc); // card 114: the host's difficulty values, on the same once-per-map-per-PC gate
    c490_send(pc); // card 490: the host infinite stamina option, same gate
    c321_level_send(pc); // card 321: the host's combat level, same gate
    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    int32_t sent = 0;
    C49JoinTally tally{};
    static C49Frame out{}; // game thread only
    for (int32_t i = 0; i < g_c49_journal.n; ++i) {
        if (c49_join_frame(pc, g_c49_journal.frame[i], out, tally)) { // card 435
            sent += chan_send_client(pc, WireString{out.w, out.num, out.num}) ? 1 : 0;
        }
    }
    QueryPerformanceCounter(&t1);
    log::line("card049: host t=%llu journal replay to %s (first Pong of the map): %d of %d frames in %.3f ms; card435: "
              "%d Add(s) of complete objectives sent without popups, %d frame(s) marked as join replay (the client skips "
              "its own callouts inside them), %d callout frame(s) not sent:%s",
              static_cast<unsigned long long>(GetTickCount64()), object_name(pc).text, sent, g_c49_journal.n,
              freq.QuadPart != 0 ? 1000.0 * static_cast<double>(t1.QuadPart - t0.QuadPart) / static_cast<double>(freq.QuadPart) : -1.0,
              tally.quiet, tally.marked, tally.callouts, tally.used > 0 ? tally.names : " none");
}

// The objective with this id in the subsystem's map, or null.
const uint8_t* c49_find(const void* subsystem, uint64_t id) {
    SparseView v{};
    if (!c49_objectives(subsystem, v)) {
        return nullptr;
    }
    for (int32_t i = 0; i < v.count; ++i) {
        const uint8_t* const element = v.data + static_cast<size_t>(i) * kObjectiveElementBytes;
        if (sparse_allocated(v, i) && read_u64(element, 0) == id) {
            return static_cast<const uint8_t*>(read_ptr(element, kObjectiveElementValue));
        }
    }
    return nullptr;
}

// --- Host snapshot after a checkpoint (step 5) ---
// The Add that recreates objective o: its parent decides Add or AddSub, its class Add or AddCount, and a count
// that counts its sub-objectives (kObjectiveCountSubs) the _SubObjectives kind, which alone creates a count that
// accepts children (card 305: the plain kind dropped Beach_SO1_A/B/C and Resort_SO3_A/B/C on a joining client).
// The bools are the stored marker and list flags; the popups are off (they are not stored where this card reads).
// Both count kinds take the marker and list bools in the creator's same two slots (bits 0-1 for a root, 1-2 for a
// sub-objective, as c49_replay_add_count_subs / _sub_subs pass them).
C49Call c49_snapshot_add(const uint8_t* o) {
    const void* const parent = read_ptr(o, kObjectiveParent);
    const bool count = is_a(o, g_c49_count_class());
    const bool count_subs = count && o[kObjectiveCountSubs] != 0;
    const bool marker = o[kObjectiveMarker] != 0;
    const bool list = o[kObjectiveInList] != 0;
    C49Call c{};
    c.flags = kC49SnapshotBit | (o[kObjectiveComplete] != 0 ? kC49HostCompleteBit : 0);
    c.count = count ? read_i32(o, kObjectiveCountTarget) : 0;
    c.name[0] = read_u64(o, kObjectiveId);
    c.actors = o + kObjectiveActors; // card 070
    if (parent == nullptr) {
        c.kind = count_subs ? C49Kind::AddCountSubs : count ? C49Kind::AddCount : C49Kind::Add;
        c.flags |= c49_bits(marker, list, false, false, false);
    } else {
        c.kind = count_subs ? C49Kind::AddCountSubSubs : count ? C49Kind::AddCountSub : C49Kind::AddSub;
        c.name[1] = c.name[0];
        c.name[0] = read_u64(parent, kObjectiveId);
        c.flags |= c49_bits(false, marker, list, false, false); // bUseParentsData off
    }
    return c;
}

const uint8_t* c49_slot(const SparseView& v, int32_t i) {
    return sparse_allocated(v, i) ? static_cast<const uint8_t*>(read_ptr(
                                        v.data + static_cast<size_t>(i) * kObjectiveElementBytes, kObjectiveElementValue))
                                  : nullptr;
}

// Adds (roots, then children), then the completions, counts and visibility of every objective.
int32_t c49_snapshot_frames(const void* world, const SparseView& v) {
    int32_t n = 0;
    for (int32_t pass = 0; pass < 2; ++pass) {
        for (int32_t i = 0; i < v.count; ++i) {
            const uint8_t* const o = c49_slot(v, i);
            if (o != nullptr && (read_ptr(o, kObjectiveParent) == nullptr) == (pass == 0)) {
                c49_emit(world, c49_snapshot_add(o));
                ++n;
            }
        }
    }
    for (int32_t i = 0; i < v.count; ++i) {
        const uint8_t* const o = c49_slot(v, i);
        if (o != nullptr && o[kObjectiveComplete] != 0) {
            // A sub-objective completes only through CompleteSubObjective(parent, id): the library's CompleteObjective
            // returns early for one (see c49_test_step), so a joining client kept it incomplete (card 143 run 2).
            const void* const parent = read_ptr(o, kObjectiveParent);
            const uint64_t id = read_u64(o, kObjectiveId);
            c49_emit(world, parent != nullptr
                                ? C49Call{C49Kind::CompleteSub, {read_u64(parent, kObjectiveId), id}, kC49SnapshotBit, 0, nullptr}
                                : C49Call{C49Kind::Complete, {id, 0}, kC49SnapshotBit, 0, nullptr});
        }
        if (o != nullptr && is_a(o, g_c49_count_class()) && o[kObjectiveCountSubs] == 0) {
            c49_emit(world, C49Call{C49Kind::SetCount, {read_u64(o, kObjectiveId), 0}, kC49SnapshotBit,
                                    read_i32(o, kObjectiveCountCurrent), nullptr});
        }
        if (o != nullptr) {
            const uint32_t bits = c49_bits(o[kObjectiveMarker] != 0, o[kObjectiveInList] != 0, false, false, false);
            c49_emit(world, C49Call{C49Kind::Visibility, {read_u64(o, kObjectiveId), 0}, bits | kC49SnapshotBit, 0,
                                    nullptr});
        }
    }
    // Card 305: a count of sub-objectives counts up as its children complete, so its SetCount comes after every
    // completion above; sent before a child's CompleteSub, the child's own increment would count twice.
    for (int32_t i = 0; i < v.count; ++i) {
        const uint8_t* const o = c49_slot(v, i);
        if (o != nullptr && is_a(o, g_c49_count_class()) && o[kObjectiveCountSubs] != 0) {
            c49_emit(world, C49Call{C49Kind::SetCount, {read_u64(o, kObjectiveId), 0}, kC49SnapshotBit,
                                    read_i32(o, kObjectiveCountCurrent), nullptr});
        }
    }
    return n;
}

// Host: the journal becomes the snapshot of objective map v: SnapshotBegin, the chapter frame, the objectives,
// SnapshotEnd. Returns the objectives. The checkpoint snapshot and card 066's compaction both build it here.
int32_t c49_snapshot_journal(const void* world, const SparseView& v) {
    c49_journal_reset();
    g_c49_journal.building = true;
    c49_emit(world, C49Call{C49Kind::SnapshotBegin, {0, 0}, kC49SnapshotBit, 0, nullptr});
    c49_emit_chapter(world);
    const int32_t n = c49_snapshot_frames(world, v);
    c49_emit(world, C49Call{C49Kind::SnapshotEnd, {0, 0}, kC49SnapshotBit, n, nullptr});
    g_c49_journal.building = false;
    return n;
}

// --- Card 066: compaction of a full journal ---
// Card 066, the run-time check of the ordering: the triggering call's objective as the snapshot recorded it, next
// to the call's own count. c49_mirror runs after the original, so an Increment is in the snapshot's count already.
void c66_log_trigger(const void* subsystem, const C49Call& c) {
    const uint64_t id = c.name[1] != 0 ? c.name[1] : c.name[0]; // the sub objective for the two-name kinds
    const uint8_t* const o = c49_find(subsystem, id);
    const bool count = o != nullptr && is_a(o, g_c49_count_class());
    log::line("card066: host trigger %s(%s) count %d; in the snapshot: %s complete %d, count %d of %d",
              kC49KindText[static_cast<size_t>(c.kind)], fname_text(&id).text, c.count,
              o != nullptr ? "found," : "not found,", o != nullptr ? o[kObjectiveComplete] : 0,
              count ? read_i32(o, kObjectiveCountCurrent) : -1, count ? read_i32(o, kObjectiveCountTarget) : -1);
}

// The journal before the compaction comes back, and compaction stays off for this map (card 066's WARN case).
void c66_restore(const char* why, int32_t frames, int32_t objectives) {
    log::line("WARN card066: host journal compaction failed (%s: %d frames, cap %d, %d objectives); the journal keeps "
              "its %d old frames and records nothing more until the next checkpoint or map",
              why, frames, g_c49_cap, objectives, g_c66_saved.n);
    memcpy(g_c49_journal.frame, g_c66_saved.frame, sizeof(C49Frame) * static_cast<size_t>(g_c66_saved.n));
    g_c49_journal.n = g_c66_saved.n;
    g_c49_journal.dropped = g_c66_saved.dropped;
    g_c49_journal.full_logged = g_c66_saved.full_logged;
    g_c49_journal.compact_off = true;
}

// Host, from c49_emit before a library call's frames: when they would not fit (a names frame and its Objective
// frame), the journal is rebuilt in place as a snapshot of the host's objective map, recorded only (connected
// clients have this state already). Returns true when it was: the snapshot covers the call. The building flag keeps
// the snapshot's own frames from starting a second compaction; a snapshot that does not fit puts the old journal back.
bool c66_compact(const void* world_context, const C49Call& c) {
    C49Journal& j = g_c49_journal;
    if (g_client_session || j.building || j.compact_off || c.kind >= C49Kind::SetCount || j.n + 2 <= g_c49_cap) {
        return false;
    }
    const void* const subsystem = c49_subsystem(world_context);
    SparseView v{};
    if (!c49_objectives(subsystem, v)) {
        j.compact_off = true;
        log::line("WARN card066: host journal full and no objective map (subsystem %p); no compaction", subsystem);
        return false;
    }
    const int32_t before = j.n;
    memcpy(g_c66_saved.frame, j.frame, sizeof(C49Frame) * static_cast<size_t>(j.n));
    g_c66_saved.n = j.n;
    g_c66_saved.dropped = j.dropped;
    g_c66_saved.full_logged = j.full_logged;
    j.record_only = true;
    const int32_t objectives = c49_snapshot_journal(world_context, v);
    j.record_only = false;
    if (j.dropped > 0 || j.n + 2 > g_c49_cap) {
        c66_restore(j.dropped > 0 ? "the snapshot overflowed" : "no room after the snapshot", j.n + j.dropped,
                    objectives);
        return false;
    }
    log::line("card066: host t=%llu journal compacted at %s: %d frames before, %d after (%d objectives, cap %d)",
              static_cast<unsigned long long>(GetTickCount64()), kC49KindText[static_cast<size_t>(c.kind)], before,
              j.n, objectives, g_c49_cap);
    c66_log_trigger(subsystem, c);
    return true;
}

// --- Client replay (steps 3-5) ---
bool c49_bit(const C49Call& c, int32_t i) {
    return ((c.flags >> i) & 1u) != 0;
}

void c49_replay_add(void* pc, const C49Call& c) {
    g_orig_c49_add(pc, c.name[0], c.actors, c49_bit(c, 0), c49_bit(c, 1), c49_bit(c, 2), c49_bit(c, 3));
}
void c49_replay_add_sub(void* pc, const C49Call& c) {
    g_orig_c49_add_sub(pc, c.name[0], c.name[1], c49_bit(c, 0), c.actors, c49_bit(c, 1), c49_bit(c, 2),
                       c49_bit(c, 3), c49_bit(c, 4));
}
void c49_replay_add_count(void* pc, const C49Call& c) {
    g_orig_c49_add_count(pc, c.name[0], c.actors, c.count, c49_bit(c, 0), c49_bit(c, 1), c49_bit(c, 2),
                         c49_bit(c, 3));
}
void c49_replay_add_count_subs(void* pc, const C49Call& c) {
    g_orig_c49_add_count_subs(pc, c.name[0], c.actors, c49_bit(c, 0), c49_bit(c, 1), c.count, c49_bit(c, 2),
                              c49_bit(c, 3));
}
void c49_replay_add_count_sub(void* pc, const C49Call& c) {
    g_orig_c49_add_count_sub(pc, c.name[0], c.name[1], c49_bit(c, 0), c.actors, c.count, c49_bit(c, 1),
                             c49_bit(c, 2), c49_bit(c, 3), c49_bit(c, 4));
}
void c49_replay_add_count_sub_subs(void* pc, const C49Call& c) {
    g_orig_c49_add_count_sub_subs(pc, c.name[0], c.name[1], c49_bit(c, 0), c.actors, c49_bit(c, 1),
                                  c49_bit(c, 2), c.count, c49_bit(c, 3), c49_bit(c, 4));
}
void c49_replay_complete(void* pc, const C49Call& c) {
    g_orig_c49_complete(pc, c.name[0]);
}
void c49_replay_complete_any(void* pc, const C49Call& c) {
    g_orig_c49_complete_any(pc, c.name[0]);
}
void c49_replay_complete_sub(void* pc, const C49Call& c) {
    g_orig_c49_complete_sub(pc, c.name[0], c.name[1]);
}
void c49_replay_increment(void* pc, const C49Call& c) {
    g_orig_c49_increment(pc, c.name[0]);
}
void c49_replay_visibility(void* pc, const C49Call& c) {
    g_orig_c49_visibility(pc, c.name[0], c49_bit(c, 0), c49_bit(c, 1));
}
void c49_replay_callout(void* pc, const C49Call& c) {
    g_orig_c49_callout(pc, c.name[0]);
}

// Snapshot count: Increment up to the host's current count. A lower host count (a checkpoint restored before
// increments the client still holds, card 058) is written straight into the count; the card053 list rebuild that
// follows the snapshot then shows it.
void c49_replay_set_count(void* pc, const C49Call& c) {
    const uint8_t* const o = c49_find(c49_subsystem(pc), c.name[0]);
    if (o == nullptr || !is_a(o, g_c49_count_class())) {
        log::line("WARN card049: client snapshot count for %s: no count objective (%p)", fname_text(&c.name[0]).text, o);
        return;
    }
    const int32_t have = read_i32(o, kObjectiveCountCurrent);
    if (have != c.count) {
        log::line("card049: client snapshot count %s: host %d, client %d%s", fname_text(&c.name[0]).text, c.count,
                  have, have > c.count ? " (client higher: lowered)" : "");
    }
    if (have > c.count && c.count >= 0) {
        *reinterpret_cast<int32_t*>(const_cast<uint8_t*>(o) + kObjectiveCountCurrent) = c.count;
        return;
    }
    for (int32_t i = have; i < c.count && i - have < kC49MaxIncrements; ++i) {
        g_orig_c49_increment(pc, c.name[0]);
    }
}

// Chapter frame: the client's game instance takes the host's chapter and level (a net client's stays -1/-1;
// a later map of another chapter must not keep the old one).
void c49_replay_chapter(void* pc, const C49Call& c) {
    auto* const gi = static_cast<uint8_t*>(g_get_st_game_instance(pc));
    if (gi == nullptr) {
        log::line("WARN card049: client chapter frame: no game instance");
        return;
    }
    auto* const chapter = reinterpret_cast<int32_t*>(gi + kGameInstanceChapter);
    auto* const level = reinterpret_cast<int32_t*>(gi + kGameInstanceLevel);
    const int32_t was_chapter = *chapter;
    const int32_t was_level = *level;
    *chapter = c.count;
    *level = static_cast<int32_t>(c.flags);
    c50_chapter_frame(); // card 050 Addendum 3
    log::line("card049: client game instance chapter %d level %d, host %d %u: %s", was_chapter, was_level, c.count,
              c.flags, was_chapter == c.count && was_level == static_cast<int32_t>(c.flags) ? "same" : "set");
}

void c49_replay_snapshot_begin(void* pc, const C49Call& c) {
    (void)pc;
    (void)c;
    g_c49m.n_snapshot = 0;
}

bool c53_same_order(const void* subsystem); // card 053

// Snapshot end: every client objective the host's snapshot lacks is logged, then card 053 rebuilds the client's
// map and list when its set or order differs from the snapshot.
void c49_replay_snapshot_end(void* pc, const C49Call& c) {
    SparseView v{};
    if (!c49_objectives(c49_subsystem(pc), v)) {
        return;
    }
    int32_t extra = 0;
    for (int32_t i = 0; i < v.count; ++i) {
        const uint8_t* const o = c49_slot(v, i);
        const uint64_t id = o != nullptr ? read_u64(o, kObjectiveId) : 0;
        bool found = o == nullptr;
        for (int32_t k = 0; !found && k < g_c49m.n_snapshot; ++k) {
            found = g_c49m.snapshot_ids[k] == id;
        }
        if (!found) {
            ++extra;
            log::line("card049: client objective %s is not in the host's snapshot (kept)", fname_text(&id).text);
        }
    }
    const ULONGLONG now = GetTickCount64();
    log::line("card049: client t=%llu snapshot end: host %d objectives, received %d ids, %d client-only",
              static_cast<unsigned long long>(now), c.count, g_c49m.n_snapshot, extra);
    if (c53_same_order(c49_subsystem(pc))) {
        log::line("card053: client objective map matches the snapshot (set and order): no rebuild");
        return;
    }
    g_c49m.c53_due_ms = g_c49_test ? now + kC53TestDelayMs : now; // test: the before shot comes first
    c49_shot(now, "card053 client panel before the rebuild");
}

// --- Card 499 (b258): the client completes what the host's native code completed, nothing more ---
// c245b257a BrainBug_O1: the host's CompleteSubObjective(BrainBug_O1, BrainBug_SO2) runs SO2's OnCompleted handler,
// which adds BrainBug_SO3 before the parent's own check (OnSubObjectiveComplete 0x05165560), so the host's O1 stays
// open. The client replays the outer row first and the nested Add after it (card 491/497 order: children before
// parents, every nested row after the outer one), so its check found 4 of 4 and completed BrainBug_O1 natively
// (caller exe+0x51655C9). Reordering breaks Research_O1 again (card 497), so the client mirrors the host's completion
// set instead: during the outermost complete-kind library call (and, sibling, the outermost native call card 097
// queues) the host notes every objective its native CompleteObjective completes. Before the outer row it sends one
// HostSet frame per id, then the outer row with kC499GroupBit and the nested rows with kC499NestedBit (b259 card 491:
// nested adds go before the outer row, and the group's first row, add or outer, carries kC499GroupBit). The client
// arms the set at the group row, keeps it through the nested rows and drops it at any other row; while it replays a
// row under an armed set, a native completion of an objective outside the set is skipped (one card499 row each). The
// journal holds the same frames, so a join replay filters the same way. A set over kC499Set ids: WARN, and the rows
// go without the bits (no filter, the old behavior).
constexpr uint32_t kC499GroupBit = 1u << 9;   // the outer row of a grouped call: the HostSet ids before it apply
constexpr uint32_t kC499NestedBit = 1u << 10; // a nested row of the same grouped call: the set stays armed
constexpr int32_t kC499Set = 32;
constexpr uint32_t kC499Lines = 64; // card499 rows per process, each side
constexpr size_t kC499Text = 480;   // the host row's id list
struct C499Host {
    int32_t n;
    bool over;
    uint32_t lines;
    uint64_t id[kC499Set];
};
struct C499Client {
    int32_t pending_n; // HostSet frames since the last other row
    bool pending_over;
    bool armed;
    int32_t n; // the armed set
    uint32_t blocked;
    uint32_t lines;
    C49Call outer; // the group row the set came with (kind and names, for the log)
    uint64_t pending[kC499Set];
    uint64_t id[kC499Set];
};

namespace {
C499Host g_c499h{};   // host, game thread
C499Client g_c499c{}; // client, game thread
} // namespace

// Host, at the entry of an outermost complete-kind library call: a new set.
void c499_host_reset() {
    g_c499h.n = 0;
    g_c499h.over = false;
}

// Host, from the native CompleteObjective inside a complete-kind library call: the objective it completes.
void c499_host_note(uint64_t id) {
    if (g_c499h.n < kC499Set) {
        g_c499h.id[g_c499h.n++] = id;
        return;
    }
    g_c499h.over = true;
}

// Host, before a grouped call's outer row: one HostSet frame per id, and a card499 row naming them. False (WARN) when
// the set overflowed; the caller then sends its rows without the group bits.
bool c499_send_set(void* ctx, const uint64_t* ids, int32_t n, bool over, const C49Call& outer) {
    const char* const kind = kC49KindText[static_cast<size_t>(outer.kind)];
    const uint64_t* const name = &outer.name[outer.name[1] != 0 ? 1 : 0];
    if (over) {
        log::line("WARN card499: host %s(%s) completed over %d objectives natively: rows sent without the set", kind,
                  fname_text(name).text, kC499Set);
        return false;
    }
    char text[kC499Text] = {};
    size_t len = 0;
    for (int32_t i = 0; i < n && i < kC499Set; ++i) {
        c49_mirror(ctx, C49Call{C49Kind::HostSet, {ids[i], 0}, 0, 0, nullptr});
        const int w = len < sizeof(text) ? snprintf(text + len, sizeof(text) - len, " %s", fname_text(&ids[i]).text)
                                         : 0;
        len += w > 0 ? static_cast<size_t>(w) : 0;
    }
    if (note_event_cap(g_c499h.lines, kC499Lines, "card499 host sets")) {
        log::line("card499: host t=%llu %s(%s) native set %d:%s", static_cast<unsigned long long>(GetTickCount64()),
                  kind, fname_text(name).text, n, text);
    }
    return true;
}

// Client, a HostSet frame: one id of the next group row's set.
void c49_replay_host_set(void*, const C49Call& c) {
    if (g_c499c.pending_n < kC499Set) {
        g_c499c.pending[g_c499c.pending_n++] = c.name[0];
        return;
    }
    g_c499c.pending_over = true;
}

// Client, before replaying any Objective frame: a group row arms the HostSet ids before it, a nested row keeps the
// set, any other row (HostSet frames aside) drops it.
void c499_client_frame(const C49Call& c) {
    if (c.kind == C49Kind::HostSet) {
        return;
    }
    if ((c.flags & kC499GroupBit) != 0) {
        g_c499c.armed = !g_c499c.pending_over;
        g_c499c.n = g_c499c.pending_n;
        memcpy(g_c499c.id, g_c499c.pending, sizeof(g_c499c.id));
        g_c499c.outer = c;
        if (g_c499c.pending_over) {
            log::line("WARN card499: client set for %s(%s) over %d ids: not armed",
                      kC49KindText[static_cast<size_t>(c.kind)], fname_text(&c.name[c.name[1] != 0 ? 1 : 0]).text,
                      kC499Set);
        }
    } else if ((c.flags & kC499NestedBit) == 0) {
        g_c499c.armed = false;
    } else if (!c253_completion(g_c499c.outer.kind) && c253_completion(c.kind)) {
        g_c499c.outer = c; // b259 card 491: the group led with a nested add; the block rows name the complete
    }
    g_c499c.pending_n = 0;
    g_c499c.pending_over = false;
}

// Client, a native CompleteObjective inside a host replay: true (one card499 row) when an armed set lacks it.
bool c499_block(const uint8_t* o, const void* ret) {
    if (!g_client_session || g_c49.replay == 0 || !g_c499c.armed) {
        return false;
    }
    const uint64_t id = read_u64(o, kObjectiveId);
    for (int32_t i = 0; i < g_c499c.n && i < kC499Set; ++i) {
        if (g_c499c.id[i] == id) {
            return false;
        }
    }
    ++g_c499c.blocked;
    if (note_event_cap(g_c499c.lines, kC499Lines, "card499 client blocks")) {
        const C49Call& outer = g_c499c.outer;
        log::line("card499: client t=%llu blocked native complete %s (not in host set of %d, outer %s(%s)); %u blocked "
                  "so far; caller exe+0x%llX", static_cast<unsigned long long>(GetTickCount64()), fname_text(&id).text,
                  g_c499c.n, kC49KindText[static_cast<size_t>(outer.kind)],
                  fname_text(&outer.name[outer.name[1] != 0 ? 1 : 0]).text, g_c499c.blocked, exe_rva(ret));
    }
    return true;
}

using C49ReplayFn = void (*)(void* pc, const C49Call& c);
constexpr C49ReplayFn kC49Replay[] = {
    c49_replay_add,         c49_replay_add_sub,      c49_replay_add_count,      c49_replay_add_count_subs,
    c49_replay_add_count_sub, c49_replay_add_count_sub_subs, c49_replay_complete, c49_replay_complete_any,
    c49_replay_complete_sub, c49_replay_increment,   c49_replay_visibility,     c49_replay_callout,
    c49_replay_set_count,   c49_replay_snapshot_begin, c49_replay_snapshot_end, c49_replay_chapter,
    c49_replay_host_set};
static_assert(sizeof(kC49Replay) / sizeof(kC49Replay[0]) == static_cast<size_t>(C49Kind::Count));

// The id an Add kind creates: the second name for the sub-objective Adds.
uint64_t c49_added_id(const C49Call& c) {
    const bool sub = c.kind == C49Kind::AddSub || c.kind == C49Kind::AddCountSub || c.kind == C49Kind::AddCountSubSubs;
    return sub ? c.name[1] : c.name[0];
}

// Client, an Add frame: records snapshot ids; true (logged, skipped) when the id exists already.
bool c49_skip_add(const void* subsystem, const C49Call& c) {
    const uint64_t id = c49_added_id(c);
    if ((c.flags & kC49SnapshotBit) != 0 && g_c49m.n_snapshot < kC49MaxObjectives) {
        g_c49m.snapshot_ids[g_c49m.n_snapshot++] = id;
    }
    if (!g_c49_exists(subsystem, id)) {
        return false;
    }
    const uint8_t* const o = c49_find(subsystem, id);
    const bool host_complete = (c.flags & kC49HostCompleteBit) != 0;
    const bool own_complete = o != nullptr && o[kObjectiveComplete] != 0;
    if (c49_line()) {
        log::line("card049: client ignored %s for %s: the id exists already%s", kC49KindText[static_cast<size_t>(c.kind)],
                  fname_text(&id).text,
                  (c.flags & kC49SnapshotBit) != 0 && own_complete && !host_complete
                      ? "; complete here, not on the host (a client cannot un-complete)" : "");
    }
    return true;
}

// Client, once per process before the first replayed Complete: the multicast delegates of the objective and the
// number of functions bound to each (reflection: the class chain's FProperty lists; an inline multicast delegate is
// a TArray of bindings, Num at +8). These are what USTLevelObjective::CompleteObjective broadcasts.
// The objects and functions bound to one inline multicast delegate (FScriptDelegate entries), capped.
void c49_bound_lines(const uint8_t* o, size_t offset) {
    const auto* const data = static_cast<const uint8_t*>(read_ptr(o, offset));
    const int32_t num = read_i32(o, offset + kArrayNum);
    for (int32_t i = 0; data != nullptr && i < num && i < kC49MaxBound; ++i) {
        const uint8_t* const d = data + static_cast<size_t>(i) * kScriptDelegateBytes;
        const void* const target = g_weak_get(d);
        log::line("card049: %s   [%d] -> %s (%s).%s", side(), i, object_name(target).text, class_name(target).text,
                  fname_text(d + kScriptDelegateName).text);
    }
}

void c49_delegate_lines(const uint8_t* o) {
    const void* cls = read_ptr(o, kObjectClass);
    for (int32_t depth = 0; cls != nullptr && depth < kC49MaxSupers; ++depth) {
        const void* field = read_ptr(cls, kStructChildProperties);
        for (int32_t i = 0; field != nullptr && i < kC49MaxParams * 4; ++i) {
            const void* const field_class = read_ptr(field, sizeof(void*));
            const ObjectName type = field_class != nullptr ? fname_text(field_class) : ObjectName{};
            const size_t offset = static_cast<size_t>(read_i32(field, kPropertyOffset));
            if (strstr(type.text, "Multicast") != nullptr) {
                const bool inline_list = strstr(type.text, "Inline") != nullptr;
                log::line("card049: %s delegate %s.%s (%s @0x%zX): %d bound%s", side(), class_name(o).text,
                          fname_text(static_cast<const uint8_t*>(field) + kFieldName).text, type.text, offset,
                          inline_list ? read_i32(o, offset + kArrayNum) : -1, inline_list ? "" : " (sparse: not read)");
                if (inline_list) {
                    c49_bound_lines(o, offset);
                }
            }
            field = read_ptr(field, kFieldNext);
        }
        cls = read_ptr(cls, kStructSuper);
    }
}

// One reflected property of o with its value, for the scalar, object and container types only (struct and other
// types print nothing). Diagnosis of the client's empty OBJECTIVES panel.
void c49_prop_line(const uint8_t* o, const void* field, const char* type) {
    const size_t off = static_cast<size_t>(read_i32(field, kPropertyOffset));
    const ObjectName name = fname_text(static_cast<const uint8_t*>(field) + kFieldName);
    char value[96] = {};
    if (strstr(type, "ArrayProperty") != nullptr || strstr(type, "MapProperty") != nullptr ||
        strstr(type, "SetProperty") != nullptr) {
        snprintf(value, sizeof(value), "num %d", read_i32(o, off + kArrayNum));
    } else if (strcmp(type, "ObjectProperty") == 0) {
        const void* const v = read_ptr(o, off);
        snprintf(value, sizeof(value), "%s (%s)", object_name(v).text, class_name(v).text);
    } else if (strcmp(type, "BoolProperty") == 0 || strcmp(type, "ByteProperty") == 0 ||
               strcmp(type, "EnumProperty") == 0) {
        snprintf(value, sizeof(value), "0x%02X", o[off]);
    } else if (strcmp(type, "FloatProperty") == 0) {
        snprintf(value, sizeof(value), "%.3f", static_cast<double>(read_f32(o, off)));
    } else if (strcmp(type, "IntProperty") == 0) {
        snprintf(value, sizeof(value), "%d", read_i32(o, off));
    } else if (strcmp(type, "DoubleProperty") == 0) {
        double d = 0.0;
        memcpy(&d, o + off, sizeof(d));
        snprintf(value, sizeof(value), "%.3f", d);
    } else {
        return;
    }
    log::line("card049: %s     %s.%s (%s @0x%zX) = %s", side(), class_name(o).text, name.text, type, off, value);
}

// The properties of o and its supers (kC49MaxSupers classes); returns the object held by the property named
// `child` (nullptr when there is none).
const uint8_t* c49_prop_lines(const uint8_t* o, const char* child) {
    const uint8_t* found = nullptr;
    const void* cls = o != nullptr ? read_ptr(o, kObjectClass) : nullptr;
    for (int32_t depth = 0; cls != nullptr && depth < kC49MaxSupers; ++depth) {
        const void* field = read_ptr(cls, kStructChildProperties);
        for (int32_t i = 0; field != nullptr && i < kC49MaxParams * 4; ++i) {
            const void* const field_class = read_ptr(field, sizeof(void*));
            const ObjectName type = field_class != nullptr ? fname_text(field_class) : ObjectName{};
            c49_prop_line(o, field, type.text);
            if (child != nullptr && strcmp(fname_text(static_cast<const uint8_t*>(field) + kFieldName).text, child) == 0) {
                found = static_cast<const uint8_t*>(read_ptr(o, static_cast<size_t>(read_i32(field, kPropertyOffset))));
            }
            field = read_ptr(field, kFieldNext);
        }
        cls = read_ptr(cls, kStructSuper);
    }
    return found;
}

// The OBJECTIVES panel widget (WB_Objective_List_C) bound to the subsystem's OnLevelObjectiveAdded (@0x30), or null.
const uint8_t* c49_panel(const uint8_t* subsystem) {
    constexpr size_t kAddedDelegate = 0x30;
    const auto* const data = static_cast<const uint8_t*>(read_ptr(subsystem, kAddedDelegate));
    const int32_t num = read_i32(subsystem, kAddedDelegate + kArrayNum);
    for (int32_t i = 0; data != nullptr && i < num && i < kC49MaxBound; ++i) {
        const auto* const target = static_cast<const uint8_t*>(g_weak_get(data + static_cast<size_t>(i) * kScriptDelegateBytes));
        if (target != nullptr && strcmp(class_name(target).text, "WB_Objective_List_C") == 0) {
            return target;
        }
    }
    return nullptr;
}

// The panel's properties and those of its list box.
void c49_panel_lines(const uint8_t* subsystem) {
    const uint8_t* const panel = c49_panel(subsystem);
    if (panel == nullptr) {
        return;
    }
    log::line("card049: %s panel %s %p:", side(), object_name(panel).text, static_cast<const void*>(panel));
    const uint8_t* const box = c49_prop_lines(panel, "ObjectivesVerticleBox");
    if (box == nullptr) {
        return;
    }
    c49_prop_lines(box, nullptr);
    constexpr size_t kPanelSlots = 0x168; // UPanelWidget::Slots (card049x4 dump)
    const auto* const slots = static_cast<const uint8_t* const*>(read_ptr(box, kPanelSlots));
    const int32_t num = read_i32(box, kPanelSlots + kArrayNum);
    for (int32_t i = 0; slots != nullptr && i < num && i < kC49MaxBound; ++i) {
        log::line("card049: %s panel slot %d:", side(), i);
        const uint8_t* const item = c49_prop_lines(slots[i], "Content");
        c49_prop_lines(item, nullptr);
    }
}

// One line per list item widget (WB_Objective_List_Item_Fed_C) in a VerticalBox: its objective id and the
// Visibility and RenderOpacity of its child widgets. Offsets from the card049x6 reflection dump. depth 0 also
// logs each item's sub-objective items (depth 1).
void c49_item_lines(const uint8_t* box, int32_t depth) {
    constexpr size_t kPanelSlots = 0x168;
    constexpr size_t kSlotContent = 0x30;
    constexpr size_t kItemSubBox = 0x348;
    constexpr size_t kItemChildren[] = {0x360, 0x368, 0x370, 0x378, 0x380, 0x388};
    constexpr size_t kItemObjective = 0x398;
    constexpr size_t kObjectiveId = 0x58;
    const auto* const slots = static_cast<const uint8_t* const*>(box != nullptr ? read_ptr(box, kPanelSlots) : nullptr);
    const int32_t num = box != nullptr ? read_i32(box, kPanelSlots + kArrayNum) : 0;
    for (int32_t i = 0; slots != nullptr && i < num && i < kC49MaxBound; ++i) {
        const auto* const item = static_cast<const uint8_t*>(slots[i] != nullptr ? read_ptr(slots[i], kSlotContent) : nullptr);
        if (item == nullptr || strcmp(class_name(item).text, "WB_Objective_List_Item_Fed_C") != 0) {
            log::line("card049: %s %*sitem %d: %s (%s), not read", side(), depth * 2, "", i, object_name(item).text,
                      class_name(item).text);
            continue;
        }
        const auto* const objective = static_cast<const uint8_t*>(read_ptr(item, kItemObjective));
        char text[160] = {};
        size_t used = 0;
        for (const size_t off : kItemChildren) {
            const auto* const w = static_cast<const uint8_t*>(read_ptr(item, off));
            used += static_cast<size_t>(snprintf(text + used, sizeof(text) - used, " %s %u/%.2f",
                                                 object_name(w).text, w != nullptr ? w[0xDC] : 9u,
                                                 w != nullptr ? static_cast<double>(read_f32(w, 0xE0)) : -1.0));
            if (used >= sizeof(text)) {
                break;
            }
        }
        const auto* const sub = static_cast<const uint8_t*>(read_ptr(item, kItemSubBox));
        log::line("card049: %s %*sitem %d %s: vis %u/%.2f, sub slots %d;%s", side(), depth * 2, "", i,
                  objective != nullptr ? fname_text(objective + kObjectiveId).text : "null", item[0xDC],
                  static_cast<double>(read_f32(item, 0xE0)), sub != nullptr ? read_i32(sub, kPanelSlots + kArrayNum) : -1,
                  text);
        if (depth == 0) {
            c49_item_lines(sub, 1);
        }
    }
}

// Card053x3, client: the panel's ObjectiveListItems count, box slot count and box items, with `when` in the line.
void c53_dump(const void* subsystem, const char* when) {
    const uint8_t* const panel = c49_panel(static_cast<const uint8_t*>(subsystem));
    const uint8_t* const box = panel != nullptr ? static_cast<const uint8_t*>(read_ptr(panel, 0x358)) : nullptr;
    if (box == nullptr) {
        log::line("WARN card053: client panel %p has no box %s", static_cast<const void*>(panel), when);
        return;
    }
    log::line("card053: client t=%llu panel %s: ObjectiveListItems %d, box slots %d, objectives %d",
              static_cast<unsigned long long>(GetTickCount64()), when, read_i32(panel, 0x370 + kArrayNum),
              read_i32(box, 0x168 + kArrayNum), c49_objective_count(subsystem));
    c49_item_lines(box, 0);
}

// Diagnosis, both sides, every kC49PanelReadMs: one line whenever the panel's state changes. Offsets from the
// card049x4 reflection dump of WB_Objective_List_C (Visible @0x3C0, ObjectiveListItems @0x370,
// ObjectivesVerticleBox @0x358) and UWidget (Visibility @0xDC, RenderOpacity @0xE0); VerticalBox Slots @0x168.
void c49_panel_watch(void* pc, ULONGLONG now) {
    constexpr ULONGLONG kC49PanelReadMs = 250;
    if (now < g_c49.panel_next_ms) {
        return;
    }
    g_c49.panel_next_ms = now + kC49PanelReadMs;
    void* const world = g_actor_get_world(pc);
    const auto* const subsystem = static_cast<const uint8_t*>(world != nullptr ? g_c49_get_subsystem(world) : nullptr);
    const uint8_t* const panel = subsystem != nullptr ? c49_panel(subsystem) : nullptr;
    if (panel == nullptr) {
        return;
    }
    const uint8_t* const box = static_cast<const uint8_t*>(read_ptr(panel, 0x358));
    const int32_t slots = box != nullptr ? read_i32(box, 0x168 + kArrayNum) : -1;
    const int32_t items = read_i32(panel, 0x370 + kArrayNum);
    const float opacity = read_f32(panel, 0xE0);
    const uint64_t sig = (static_cast<uint64_t>(panel[0x3C0]) << 56) | (static_cast<uint64_t>(panel[0xDC]) << 48) |
                         (static_cast<uint64_t>(static_cast<uint8_t>(slots)) << 40) |
                         (static_cast<uint64_t>(static_cast<uint8_t>(items)) << 32) |
                         static_cast<uint64_t>(static_cast<uint32_t>(opacity * 100.0f));
    if (g_c49.items_ms != 0 && now >= g_c49.items_ms) {
        g_c49.items_ms = 0;
        log::line("card049: %s t=%llu panel items 1.5 s after the change:", side(), static_cast<unsigned long long>(now));
        c49_item_lines(box, 0);
    }
    if (sig == g_c49.panel_sig) {
        return;
    }
    g_c49.panel_sig = sig;
    g_c49.items_ms = now + 1500;
    log::line("card049: %s t=%llu panel %p: Visible %u, Visibility %u, opacity %.2f, list items %d, box slots %d",
              side(), static_cast<unsigned long long>(now), static_cast<const void*>(panel), panel[0x3C0], panel[0xDC],
              static_cast<double>(opacity), items, slots);
}

void c49_log_delegates(const uint8_t* o) {
    if (g_c49m.delegates_logged || o == nullptr) {
        return;
    }
    g_c49m.delegates_logged = true;
    c49_delegate_lines(o);
}

// Client: one name from the frame's chars (each - 1, printable ASCII) as an FName; false for anything else.
bool c49_decode_name(const wchar_t* chars, int32_t len, uint64_t& out) {
    wchar_t w[kChanNameMax + 1] = {};
    for (int32_t i = 0; i < len && i < kChanNameMax; ++i) {
        const wchar_t c = static_cast<wchar_t>(chars[i] - 1);
        if (c < 0x20 || c > 0x7E) {
            return false;
        }
        w[i] = c;
    }
    out = 0;
    if (len > 0) {
        g_fname_ctor(&out, w, kFNameAdd);
    }
    return len == 0 || out != 0;
}

// Client: the call a parsed Objective frame carries; false (logged) for a bad kind or name.
bool c49_decode(const WireString* s, const ChanArgs& args, C49Call& c) {
    const int32_t l1 = static_cast<int32_t>(args.v[3]);
    const int32_t l2 = static_cast<int32_t>(args.v[4]);
    const wchar_t* const chars = s->data + kChanOpAt + 1 + kChanNamedHead;
    c = C49Call{static_cast<C49Kind>(args.v[0]), {0, 0}, args.v[1], static_cast<int32_t>(args.v[2]), nullptr};
    if (args.v[0] >= static_cast<uint32_t>(C49Kind::Count) || !c49_decode_name(chars, l1, c.name[0]) ||
        !c49_decode_name(chars + l1, l2, c.name[1])) {
        log::line("WARN card049: client dropped an Objective frame: kind %u, lengths %d/%d", args.v[0], l1, l2);
        return false;
    }
    return true;
}

bool c49_is_add(C49Kind k) {
    return k <= C49Kind::AddCountSubSubs;
}

// --- Card 070: the actors of each replayed Add ---
// The host's Add gets the level actors, and the game makes one ASTLevelObjectiveMarker per actor
// (CreateASTLevelObjectiveMarker 0x05150E20); the minimap icon comes from that marker. Level-placed actors have the
// same name on both machines, so the host sends the names in an ObjectiveActors text frame
// ("<added id> <actor> <actor> ...") right before the Add's Objective frame, live and in the journal. A checkpoint
// snapshot reads them from the objective (+0x88). The client keeps the one pending frame, resolves its names with
// c64_find_actor when the Add replays, and passes the actors it found. Runs in every co-op session.
struct C70Pending {
    char text[kChanTextMax + 1]; // the last ObjectiveActors frame, empty once an Objective frame took it
};
C70Pending g_c70{}; // client, game thread

// Appends " name" to text (len chars). The one place a name's bytes are counted against kChanTextMax.
bool c70_append(char* text, size_t& len, const char* name) {
    const size_t n = strlen(name);
    if (n == 0 || len + 1 + n > static_cast<size_t>(kChanTextMax)) {
        return false;
    }
    text[len] = ' ';
    memcpy(text + len + 1, name, n + 1);
    len += 1 + n;
    return true;
}

// Host: the ObjectiveActors frame for an Add call with actors in f. False when c is no Add or has no actors.
bool c70_names_frame(const C49Call& c, C49Frame& f) {
    const int32_t total = c49_is_add(c.kind) && c.actors != nullptr ? read_i32(c.actors, kArrayNum) : 0;
    const auto* const data = total > 0 ? static_cast<void* const*>(read_ptr(c.actors, 0)) : nullptr;
    char id[kChanNameMax + 1];
    if (data == nullptr || c49_name_text(c49_added_id(c), id) < 0) {
        return false;
    }
    char text[kChanTextMax + 1];
    size_t len = strlen(id);
    memcpy(text, id, len + 1);
    int32_t names = 0;
    for (int32_t i = 0; i < total && i < kC70MaxActors; ++i) {
        const ObjectName name = data[i] != nullptr ? prop_name(static_cast<const uint8_t*>(data[i]) + kObjectName, 8)
                                                   : ObjectName{};
        if (data[i] != nullptr && !c70_append(text, len, name.text)) {
            log::line("WARN card070: host %s: names frame full at %s (%d of %d actors sent)", id, name.text, names, total);
            break;
        }
        names += data[i] != nullptr ? 1 : 0;
    }
    if (total > kC70MaxActors) {
        log::line("WARN card070: host %s has %d actors; only the first %d are sent", id, total, kC70MaxActors);
    }
    const WireString w = names > 0 ? chan_text_frame(ChanOp::ObjectiveActors, text, f.w) : WireString{f.w, 0, 0};
    f.num = w.num;
    if (c49_line()) {
        log::line("card070: host t=%llu %s%s actors %d: %s%s", static_cast<unsigned long long>(GetTickCount64()),
                  (c.flags & kC49SnapshotBit) != 0 ? "snapshot " : "", id, total, text,
                  w.num > 0 ? "" : " (WARN no frame: not printable ASCII)");
    }
    return w.num > 0;
}

// Client: keep the frame until the next Objective frame.
void c70_receive(const WireString* s) {
    if (g_c70.text[0] != '\0') {
        log::line("WARN card070: client: names frame \"%.80s\" was not taken by an Add (dropped)", g_c70.text);
    }
    if (chan_text(s, g_c70.text, sizeof(g_c70.text)) < 0) {
        log::line("WARN card070: client: bad ObjectiveActors frame");
    }
}

// Client, at LoadMap enter (c49_map_end; b12 run 2): a names frame taken just before a travel whose Add never came
// would otherwise meet the new session's first Objective frame (or a later Add of the same id).
void c70_map_end() {
    if (g_c70.text[0] != '\0') {
        char id[kChanNameMax + 1] = {};
        const char* const sp = strchr(g_c70.text, ' ');
        const size_t n = sp != nullptr ? static_cast<size_t>(sp - g_c70.text) : strlen(g_c70.text);
        memcpy(id, g_c70.text, n < sizeof id ? n : sizeof id - 1);
        log::line("card070: client pending names frame for %s cleared at map end", id);
    }
    g_c70.text[0] = '\0';
}

// Card 432 step 2: the placed actor with this object name, from card 407's name index (c64_find_actor walked every
// level actor, about 3.8 ms a find on a mission map). The index's pick is the walk's: the lowest (level, slot). With
// no index answer the walk runs (a runtime spawn, a class name). The first answers are compared with the walk.
void* c432_find_named(const void* world, const char* name) {
    constexpr uint32_t kChecks = 16; // index answers compared with the walk, per process
    static uint32_t checks = 0;
    const C50NameRef ref = c50_name_ref(name, kC95ActorNameMax);
    C402New hit[kC407Hits];
    const int32_t n = c407_named(world, ref, hit, kC407Hits);
    int32_t best = -1;
    for (int32_t i = 0; i < n; ++i) {
        const bool before = best >= 0 && (hit[i].level < hit[best].level ||
                                          (hit[i].level == hit[best].level && hit[i].index < hit[best].index));
        best = best < 0 || before ? i : best;
    }
    if (best < 0) {
        return c64_find_actor(world, name, Vec3{});
    }
    if (checks < kChecks) {
        ++checks;
        void* const walked = c64_find_actor(world, name, Vec3{});
        if (walked != hit[best].actor) {
            log::line("WARN card432: name index gave %p for %s, the walk %p (walk kept)", hit[best].actor, name, walked);
            return walked;
        }
    }
    return hit[best].actor;
}

// Client, on every Objective frame: the pending names, resolved for this Add (null for any other frame), into out.
// Returns the actors found and clears the pending frame. A name that does not resolve is left out with a WARN.
int32_t c70_take(void* pc, const C49Call* add, void** out) {
    char id[kChanNameMax + 1] = {};
    char* ctx = nullptr;
    char* tok = g_c70.text[0] != '\0' ? strtok_s(g_c70.text, " ", &ctx) : nullptr;
    const bool match = add != nullptr && tok != nullptr && c49_name_text(c49_added_id(*add), id) > 0 && strcmp(tok, id) == 0;
    if (tok != nullptr && !match) {
        log::line("WARN card070: client: names frame for %s, but the next frame is %s(%s): dropped", tok,
                  add != nullptr ? kC49KindText[static_cast<size_t>(add->kind)] : "not an Add", id);
    }
    void* const world = match ? g_actor_get_world(pc) : nullptr;
    int32_t names = 0;
    int32_t found = 0;
    for (tok = world != nullptr ? strtok_s(nullptr, " ", &ctx) : nullptr; tok != nullptr && names < kC70MaxActors;
         tok = strtok_s(nullptr, " ", &ctx)) {
        ++names;
        void* const actor = c432_find_named(world, tok);
        out[found] = actor;
        found += actor != nullptr ? 1 : 0;
        if (actor == nullptr) {
            log::line("WARN card070: client %s: actor %s not found", id, tok);
        }
    }
    if (match) {
        log::line("card070: client t=%llu %s(%s): names sent %d, actors found %d%s",
                  static_cast<unsigned long long>(GetTickCount64()), kC49KindText[static_cast<size_t>(add->kind)], id,
                  names, found, world == nullptr ? " (WARN no world)" : "");
    }
    g_c70.text[0] = '\0';
    return found;
}

// Card 305: the objective a replayed frame acts on (the child of CompleteSub, else the first name) and, for the
// sub-objective Adds, the parent it goes under. Kinds without one (snapshot markers, chapter) return 0.
uint64_t c305_target(const C49Call& c) {
    switch (c.kind) {
    case C49Kind::CompleteSub: return c.name[1];
    case C49Kind::Complete: case C49Kind::CompleteAny: case C49Kind::Increment: case C49Kind::Visibility:
    case C49Kind::Callout: return c.name[0];
    default: return 0;
    }
}

// Card 305 (b9): the host held the frame's target (or a sub-Add's parent) when its call ran. Snapshot frames are
// built from the host's own objective map, so they always name objectives the host holds.
bool c305_host_had(const C49Call& c) {
    return (c.flags & (kC49HostExistsBit | kC49SnapshotBit)) != 0;
}

constexpr uint32_t kC305Lines = 24; // client, per map: the plain "absent on the host too" rows
uint32_t g_c305_lines = 0;          // reset on the map's first Objective frame (g_c49m.frames 0)

// The plain row for a frame whose id the host lacked too: the host's call did nothing, so the client is in step.
//   card305: client replayed <kind> <parent>/<id>: absent on the host too
void c305_absent_line(const C49Call& c, uint64_t parent, uint64_t id) {
    if (note_event_cap(g_c305_lines, kC305Lines, "card305 absent on the host too")) {
        log::line("card305: client replayed %s %s/%s: absent on the host too",
                  kC49KindText[static_cast<size_t>(c.kind)], parent != 0 ? fname_text(&parent).text : "-",
                  fname_text(&id).text);
    }
}

// Client, before a replay: a completion, count, visibility or callout whose objective does not exist here does
// nothing in the game (the library returns early), so the client's HUD silently stays behind the host's. It WARNs
// only when the host held the objective (c305_host_had); else the host's call did nothing too (b8 trace: the C2M1
// script's own calls on ids its host never added). SetCount has its own WARN card049 row.
void c305_missing_line(const void* subsystem, const C49Call& c) {
    const uint64_t id = c305_target(c);
    if (id == 0 || g_c49_exists(subsystem, id)) {
        return;
    }
    const bool sub = c.kind == C49Kind::CompleteSub;
    if (!c305_host_had(c)) {
        c305_absent_line(c, sub ? c.name[0] : 0, id);
        return;
    }
    log::line("WARN card305: client replayed %s %s/%s: objective missing", kC49KindText[static_cast<size_t>(c.kind)],
              sub ? fname_text(&c.name[0]).text : "-", fname_text(&id).text);
}

// Client, after a replayed sub-objective Add: the child must exist now. A missing parent, or a parent that does
// not accept children (a count created without kObjectiveCountSubs), makes AddSubObjective return early. It WARNs
// only when the host held the parent; else the host's Add did nothing too.
// Parent kind: none, plain, count, count-subs.
void c305_add_line(const void* subsystem, const C49Call& c) {
    const bool sub = c.kind == C49Kind::AddSub || c.kind == C49Kind::AddCountSub || c.kind == C49Kind::AddCountSubSubs;
    if (!sub || g_c49_exists(subsystem, c.name[1])) {
        return;
    }
    if (!c305_host_had(c)) {
        c305_absent_line(c, c.name[0], c.name[1]);
        return;
    }
    const uint8_t* const parent = c49_find(subsystem, c.name[0]);
    const bool count = parent != nullptr && is_a(parent, g_c49_count_class());
    const char* const kind = parent == nullptr ? "none" : !count ? "plain" : parent[kObjectiveCountSubs] != 0
                                                                                  ? "count-subs" : "count";
    log::line("WARN card305: client %s add %s under %s failed (parent kind %s)",
              (c.flags & kC49SnapshotBit) != 0 ? "snapshot" : "replayed", fname_text(&c.name[1]).text,
              fname_text(&c.name[0]).text, kind);
}

// Card 432, client: time spent inside the game's objective function per replayed row. A row over kC432SlowMs gets a
// line (capped); every kC432Every rows one totals line, so the handler's own share is the card330 recv time less this.
constexpr double kC432SlowMs = 2.0;
constexpr uint32_t kC432SlowLines = 32;
constexpr uint32_t kC432Every = 64;
struct C432Replay {
    uint32_t rows = 0;
    uint32_t slow_lines = 0;
    double total_ms = 0.0;
    double max_ms = 0.0;
};
C432Replay g_c432; // game thread

void c432_note_replay(const C49Call& c, double ms) {
    ++g_c432.rows;
    g_c432.total_ms += ms;
    g_c432.max_ms = ms > g_c432.max_ms ? ms : g_c432.max_ms;
    if (ms >= kC432SlowMs && note_event_cap(g_c432.slow_lines, kC432SlowLines, "card432 slow replay")) {
        log::line("card432: client replay %s(%s) flags 0x%X took %.2f ms in the game's call",
                  kC49KindText[static_cast<size_t>(c.kind)], fname_text(&c.name[0]).text, c.flags, ms);
    }
    if (g_c432.rows % kC432Every == 0) {
        log::line("card432: client objective replays: %u rows, %.1f ms in the game's calls, max %.2f ms",
                  g_c432.rows, g_c432.total_ms, g_c432.max_ms);
    }
}

// Client: one Objective frame from the host, replayed through the same library function on its own world.
int32_t c70_take(void* pc, const C49Call* add, void** out); // card 070

void c49_receive(void* pc, const WireString* s, const ChanArgs& args) {
    C49Call c{};
    void* const subsystem = c49_subsystem(pc);
    void* found[kC70MaxActors] = {};
    const bool ok = c49_decode(s, args, c);
    g_c253_lines = g_c49m.frames == 0 ? 0 : g_c253_lines; // card 253: the line cap is per map
    g_c305_lines = g_c49m.frames == 0 ? 0 : g_c305_lines; // card 305: per map too
    const C49Actors list{found, c70_take(pc, ok && c49_is_add(c.kind) ? &c : nullptr, found), kC70MaxActors};
    c.actors = &list; // card 070: the host's actors for an Add, resolved here; empty for every other frame
    if (!ok || subsystem == nullptr || (c49_is_add(c.kind) && c49_skip_add(subsystem, c))) {
        return;
    }
    const size_t k = static_cast<size_t>(c.kind);
    if (c.kind == C49Kind::Complete || c.kind == C49Kind::CompleteAny) {
        c49_log_delegates(c49_find(subsystem, c.name[0]));
    }
    const int32_t before = c49_objective_count(subsystem);
    if (g_c53_rebuilt && (c.flags & kC49SnapshotBit) == 0) {
        c53_dump(subsystem, "before replaying the event");
        g_c53_after_ms = GetTickCount64() + 1500;
    }
    c253_take_host_count(subsystem, c); // card 253: before the replay, live completions only
    c305_missing_line(subsystem, c);
    LARGE_INTEGER freq{}, t0{}, t1{}; // card 432: the game's own call, apart from this handler's checks and lines
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    // Card 435: the client's own callouts inside a join replay frame, and (card 435 sibling) inside a checkpoint
    // snapshot frame: a snapshot restates state the host restored without its script (popups off, no Callout frame),
    // so a banner its replayed Complete or SetCount sets off on the client has no host counterpart.
    const uint32_t join = (c.flags & (kC435JoinBit | kC49SnapshotBit)) != 0 ? 1u : 0u;
    g_c435.join += join;
    c499_client_frame(c); // card 499: arm, keep or drop the host's completion set
    ++g_c49.replay; // card 100: completions inside a host replay apply on the client
    kC49Replay[k](pc, c);
    --g_c49.replay;
    g_c435.join -= join;
    QueryPerformanceCounter(&t1);
    c432_note_replay(c, freq.QuadPart != 0 ? 1000.0 * static_cast<double>(t1.QuadPart - t0.QuadPart) /
                                                 static_cast<double>(freq.QuadPart) : 0.0);
    c305_add_line(subsystem, c);
    const int32_t after = c49_objective_count(subsystem);
    if (g_c49m.frames++ == 0) {
        c49_shot(GetTickCount64() + kC49LoadShotMs, "objectives after the map load (client)");
    }
    if ((c.kind == C49Kind::Complete || c.kind == C49Kind::CompleteSub) && (c.flags & kC49SnapshotBit) == 0) {
        c49_shot(GetTickCount64() + kC49ShotAfterMs, "after the completion (client)");
    }
    if (c49_kind_line(c.kind)) {
        log::line("card049: client t=%llu replayed %s(%s%s%s) flags 0x%X count %d: objectives %d -> %d",
                  static_cast<unsigned long long>(GetTickCount64()), kC49KindText[k], fname_text(&c.name[0]).text,
                  c.name[1] != 0 ? ", " : "", c.name[1] != 0 ? fname_text(&c.name[1]).text : "", c.flags, c.count,
                  before, after);
    }
}

// Host test (UBWCOOP_C49_TEST=<id>), restart phase 1: complete the test objective once, through the detour so it is
// mirrored like a level-script call.
void c49_test_step(void* pc, ULONGLONG now) {
    if (!g_c49_test || g_client_session || g_c45_phase != 1 || g_c49m.completed || g_c49m.first_add_ms == 0 ||
        now < g_c49m.first_add_ms + kC49CompleteMs) {
        return;
    }
    g_c49m.completed = true;
    uint64_t id = 0;
    g_fname_ctor(&id, g_c49_test_id, kFNameAdd);
    // The library's CompleteObjective returns early for a sub-objective (its virtual check at +0x2B0 is
    // IsSubobjective 0x0515FCD0, not IsComplete), so a sub-objective goes through CompleteSubObjective(parent, id).
    const uint8_t* const o = c49_find(c49_subsystem(pc), id);
    const void* const parent = o != nullptr ? read_ptr(o, kObjectiveParent) : nullptr;
    const uint64_t parent_id = parent != nullptr ? read_u64(parent, kObjectiveId) : 0;
    log::line("card049: host t=%llu test: %s(%s%s%s)", static_cast<unsigned long long>(now),
              parent_id != 0 ? "CompleteSubObjective" : "CompleteObjective", parent_id != 0 ? fname_text(&parent_id).text : "",
              parent_id != 0 ? ", " : "", fname_text(&id).text);
    if (parent_id != 0) {
        detour_c49_complete_sub(pc, parent_id, id);
    } else {
        detour_c49_complete(pc, id);
    }
    log::line("card049: host test: %s %p flags 0x%X, complete %d", fname_text(&id).text, o,
              o != nullptr ? static_cast<uint32_t>(read_i32(o, kC49ObjectFlags)) : 0u, o != nullptr ? o[kObjectiveComplete] : -1);
    c49_shot(now + kC49ShotAfterMs, "after the completion (host)");
}

// Audit line for one finished library call (step 1). ret is the detour's return address.
void c49_audit(const C49Call& c, const void* ctx, const void* ret) {
    c49_log_params();
    if (!note_event_cap(g_c49.audit_lines, kC49AuditLines, "card049 audit")) {
        return;
    }
    char bools[8];
    c49_bool_text(c, bools);
    char chain[96];
    caller_chain(chain, sizeof(chain), kStackFrames + 1);
    const int32_t actors = c.actors != nullptr ? read_i32(c.actors, kArrayNum) : -1;
    log::line("card049: %s t=%llu %s(%s%s%s) bools %s, count %d, actors %d, depth %u, ctx %s, objectives now %d; "
              "caller exe+0x%llX, chain %s",
              side(), static_cast<unsigned long long>(GetTickCount64()), kC49KindText[static_cast<size_t>(c.kind)],
              fname_text(&c.name[0]).text, c.name[1] != 0 ? ", " : "", c.name[1] != 0 ? fname_text(&c.name[1]).text : "",
              bools, c.count, actors, g_c49.depth, class_name(ctx).text, c49_objective_count(c49_subsystem(ctx)),
              exe_rva(ret), chain);
}

// Every detour, after its original: the audit line (later steps add the mirror here).
void c49_after(void* ctx, const C49Call& c, const void* ret) {
    --g_c49.depth;
    c49_audit(c, ctx, ret);
    c49_mirror(ctx, c);
}

// b226 card 491 (c245b225a BrainBug_SO2: host +10000, client +25000): the host's CompleteSubObjective(SO2, SO2_R2)
// completes the parent inside itself (CompleteSubObjective(O1, SO2) at library depth 1). c49_after mirrors after the
// original returns, so the parent's row went out first; the client replayed it with R2 still open, the engine
// cascaded R2 inside it and scored it. Host only: a complete-kind call nested in a complete-kind call keeps a slot in
// call entry order, and its row goes out right after the outermost complete's row, so children reach the client
// before their parents.
// b247 card 497 (c247b245a Research_O1: client +20000 over the host): CompleteObjective(Research_O1) runs its
// OnCompleted handler inside the call, which adds Research_SO2/Marker2. The adds went out at once (journal 125, 127),
// the outer complete only on return (128); the client added them first and its cascade completed them. Now every
// mirrored kind (adds, visibility, callout, increment, completes) called inside a complete takes a slot at entry, and
// only complete kinds count as a complete in progress, so the outermost complete's row goes first, then every nested
// row in entry order.
// b259 card 491 (c245b258a BrainBug_SO2): the reward of a sub complete depends on the open subs of its parent at that
// moment. The host's SO2 OnCompleted adds SO3 inside the call, before SO2's reward, so SO2 scored +10000 and SO3,
// later the parent's last open sub, +25000; the client replayed Complete SO2 before the nested Add SO3 and scored
// +25000 at SO2, +10000 at SO3. Now the nested add-kind rows (c491_add_kind) go before the outer row, as the host ran
// them, and every other nested row after it. The first row of the group carries kC499GroupBit, so the card 499 set
// is armed through the adds and the outer complete: the cascade that card 497 saw (Research_SO2/Marker2 added before
// Research_O1's complete, then completed by its cascade) is blocked by the set, which lacks them.
constexpr int32_t kC491Nested = 64;
constexpr uint32_t kC497Lines = 40;
struct C491Row {
    void* ctx; // null until the nested call finishes (or a call that sends nothing, e.g. unchanged visibility)
    C49Call call;
};
struct C491State {
    uint32_t completes; // host: complete-kind library calls in progress
    int32_t n;
    uint32_t c497_lines;
    C491Row rows[kC491Nested];
};
C491State g_c491{}; // game thread

void c492_objective_trigger(const C49Call& c); // card 492 fix 6, after c227_count

// Before a mirrored original: the slot for a call nested in a complete, or -1 (outermost, client, queue full).
int32_t c491_enter(const C49Call& c) {
    if (g_client_session) {
        return -1;
    }
    c492_objective_trigger(c); // card 492 fix 6: an objective whose OnCompleted closes doors sets the card 288 trigger
    int32_t slot = -1;
    if (g_c491.completes > 0 && g_c491.n < kC491Nested) {
        slot = g_c491.n++;
        g_c491.rows[slot] = C491Row{nullptr, c};
    } else if (g_c491.completes > 0) {
        log::line("WARN card491: host nested row queue full (%d): %s %s sent in finish order", kC491Nested,
                  kC49KindText[static_cast<size_t>(c.kind)], fname_text(&c.name[c.name[1] != 0 ? 1 : 0]).text);
    }
    if (g_c491.completes == 0 && c253_completion(c.kind)) {
        c499_host_reset(); // card 499: the outermost complete's native completion set
    }
    g_c491.completes += c253_completion(c.kind) ? 1u : 0u;
    return slot;
}

// b259 card 491: the kinds that create an objective; nested in a complete, their rows go before the outer row.
bool c491_add_kind(C49Kind kind) {
    return kind == C49Kind::Add || kind == C49Kind::AddSub || kind == C49Kind::AddCount ||
           kind == C49Kind::AddCountSubs || kind == C49Kind::AddCountSub || kind == C49Kind::AddCountSubSubs;
}

// The outermost complete: sends the filled nested rows of one part in entry order, adds (`adds`, before the outer
// row) or the rest (after it). Card 499: when grouped, the group's first row (`lead`) carries kC499GroupBit and every
// other kC499NestedBit. Returns the rows sent.
int32_t c491_send_part(bool adds, bool grouped, bool lead) {
    int32_t sent = 0;
    for (int32_t i = 0; i < g_c491.n && i < kC491Nested; ++i) {
        const C491Row& r = g_c491.rows[i];
        if (r.ctx != nullptr && c491_add_kind(r.call.kind) == adds) {
            C49Call row = r.call;
            row.flags |= !grouped ? 0u : (lead && sent == 0 ? kC499GroupBit : kC499NestedBit);
            c49_mirror(r.ctx, row);
            ++sent;
        }
    }
    return sent;
}

// The outermost complete, after its own row: the card 497 row when nested rows went with it, then the slots clear.
void c491_flush(const C49Call& outer, int32_t before, int32_t after) {
    g_c491.n = 0;
    if (before + after > 0 && note_event_cap(g_c491.c497_lines, kC497Lines, "card497 nested rows")) {
        log::line("card497: host t=%llu %s(%s) sent after %d nested add rows and before %d other nested rows",
                  static_cast<unsigned long long>(GetTickCount64()), kC49KindText[static_cast<size_t>(outer.kind)],
                  fname_text(&outer.name[outer.name[1] != 0 ? 1 : 0]).text, before, after);
    }
}

// After a mirrored original: a nested call fills its slot; any other call sends its row, and the outermost complete
// then sends the slots in order.
void c491_after(void* ctx, const C49Call& c, const void* ret, int32_t slot) {
    if (g_client_session) {
        c49_after(ctx, c, ret);
        return;
    }
    const bool complete = c253_completion(c.kind);
    --g_c49.depth;
    g_c491.completes -= complete ? 1u : 0u;
    c49_audit(c, ctx, ret);
    if (slot >= 0) {
        g_c491.rows[slot] = C491Row{ctx, c};
        return;
    }
    // Card 499: the outermost complete sends its native completion set, then (b259 card 491) its nested adds, its
    // row, and its other nested rows; the group's first row carries the group bit.
    const bool outermost = complete && g_c491.completes == 0;
    const bool grouped = outermost && c499_send_set(ctx, g_c499h.id, g_c499h.n, g_c499h.over, c);
    const int32_t before = outermost ? c491_send_part(true, grouped, true) : 0;
    C49Call row = c;
    row.flags |= !grouped ? 0u : (before == 0 ? kC499GroupBit : kC499NestedBit);
    c49_mirror(ctx, row);
    if (outermost) {
        c491_flush(c, before, c491_send_part(false, grouped, false));
    }
}

// Card 258: only the host adds objectives. The client's own level script also runs Add calls outside any host replay
// (in c241 run 8 a presence it alone fired added LOS_O05_SO03 43 s before the host: one STLevelObjective more on the
// client for two digest rounds). As card 100 does for completions, such a call is skipped on the client; the host's
// journal row or checkpoint snapshot is then the only creator. c49_skip_add stays the guard for replayed rows that
// still find the id. Counted, and logged up to kC258LocalLines lines.
constexpr uint32_t kC258LocalLines = 24;
uint32_t g_c258_local_skips = 0; // game thread
uint32_t g_c258_local_lines = 0;

bool c258_skip_local(C49Kind kind, uint64_t parent, uint64_t id, const void* ret) {
    if (!g_client_session || g_c49.replay != 0) {
        return false;
    }
    ++g_c258_local_skips;
    if (note_event_cap(g_c258_local_lines, kC258LocalLines, "card258 local objective adds")) {
        log::line("card258: client skipped local %s %s%s%s (only the host adds; %u skips so far); caller exe+0x%llX",
                  kC49KindText[static_cast<size_t>(kind)], parent != 0 ? fname_text(&parent).text : "",
                  parent != 0 ? "/" : "", fname_text(&id).text, g_c258_local_skips, exe_rva(ret));
    }
    return true;
}

// Card 305 (b9), host, before a detour's original: kC49HostExistsBit when the call's target (c305_target) or, for a
// sub-objective Add, its parent exists in the host's objective map now. 0 on the client and for kinds with neither.
uint32_t c305_exists_bit(const void* ctx, const C49Call& c) {
    if (g_client_session || g_c49_exists == nullptr) {
        return 0;
    }
    const bool sub_add = c.kind == C49Kind::AddSub || c.kind == C49Kind::AddCountSub ||
                         c.kind == C49Kind::AddCountSubSubs;
    const uint64_t id = sub_add ? c.name[0] : c305_target(c);
    void* const subsystem = id != 0 ? c49_subsystem(ctx) : nullptr;
    return subsystem != nullptr && g_c49_exists(subsystem, id) ? kC49HostExistsBit : 0;
}

void detour_c49_add(void* ctx, uint64_t id, const void* actors, bool b0, bool b1, bool b2, bool b3) {
    if (c258_skip_local(C49Kind::Add, 0, id, _ReturnAddress())) {
        return;
    }
    const C49Call call{C49Kind::Add, {id, 0}, c49_bits(b0, b1, b2, b3, false), 0, actors};
    const int32_t slot = c491_enter(call);
    ++g_c49.depth;
    g_orig_c49_add(ctx, id, actors, b0, b1, b2, b3);
    c491_after(ctx, call, _ReturnAddress(), slot);
}

void detour_c49_add_sub(void* ctx, uint64_t parent, uint64_t id, bool b0, const void* actors, bool b1, bool b2,
                        bool b3, bool b4) {
    if (c258_skip_local(C49Kind::AddSub, parent, id, _ReturnAddress())) {
        return;
    }
    const uint32_t had = c305_exists_bit(ctx, C49Call{C49Kind::AddSub, {parent, id}, 0, 0, nullptr});
    const C49Call call{C49Kind::AddSub, {parent, id}, c49_bits(b0, b1, b2, b3, b4) | had, 0, actors};
    const int32_t slot = c491_enter(call);
    ++g_c49.depth;
    g_orig_c49_add_sub(ctx, parent, id, b0, actors, b1, b2, b3, b4);
    c491_after(ctx, call, _ReturnAddress(), slot);
}

void detour_c49_add_count(void* ctx, uint64_t id, const void* actors, int32_t count, bool b0, bool b1, bool b2,
                          bool b3) {
    if (c258_skip_local(C49Kind::AddCount, 0, id, _ReturnAddress())) {
        return;
    }
    const C49Call call{C49Kind::AddCount, {id, 0}, c49_bits(b0, b1, b2, b3, false), count, actors};
    const int32_t slot = c491_enter(call);
    ++g_c49.depth;
    g_orig_c49_add_count(ctx, id, actors, count, b0, b1, b2, b3);
    c491_after(ctx, call, _ReturnAddress(), slot);
}

void detour_c49_add_count_subs(void* ctx, uint64_t id, const void* actors, bool b0, bool b1, int32_t count, bool b2,
                               bool b3) {
    if (c258_skip_local(C49Kind::AddCountSubs, 0, id, _ReturnAddress())) {
        return;
    }
    const C49Call call{C49Kind::AddCountSubs, {id, 0}, c49_bits(b0, b1, b2, b3, false), count, actors};
    const int32_t slot = c491_enter(call);
    ++g_c49.depth;
    g_orig_c49_add_count_subs(ctx, id, actors, b0, b1, count, b2, b3);
    c491_after(ctx, call, _ReturnAddress(), slot);
}

void detour_c49_add_count_sub(void* ctx, uint64_t parent, uint64_t id, bool b0, const void* actors, int32_t count,
                              bool b1, bool b2, bool b3, bool b4) {
    if (c258_skip_local(C49Kind::AddCountSub, parent, id, _ReturnAddress())) {
        return;
    }
    const uint32_t had = c305_exists_bit(ctx, C49Call{C49Kind::AddCountSub, {parent, id}, 0, 0, nullptr});
    const C49Call call{C49Kind::AddCountSub, {parent, id}, c49_bits(b0, b1, b2, b3, b4) | had, count, actors};
    const int32_t slot = c491_enter(call);
    ++g_c49.depth;
    g_orig_c49_add_count_sub(ctx, parent, id, b0, actors, count, b1, b2, b3, b4);
    c491_after(ctx, call, _ReturnAddress(), slot);
}

void detour_c49_add_count_sub_subs(void* ctx, uint64_t parent, uint64_t id, bool b0, const void* actors, bool b1,
                                   bool b2, int32_t count, bool b3, bool b4) {
    if (c258_skip_local(C49Kind::AddCountSubSubs, parent, id, _ReturnAddress())) {
        return;
    }
    const uint32_t had = c305_exists_bit(ctx, C49Call{C49Kind::AddCountSubSubs, {parent, id}, 0, 0, nullptr});
    const C49Call call{C49Kind::AddCountSubSubs, {parent, id}, c49_bits(b0, b1, b2, b3, b4) | had, count, actors};
    const int32_t slot = c491_enter(call);
    ++g_c49.depth;
    g_orig_c49_add_count_sub_subs(ctx, parent, id, b0, actors, b1, b2, count, b3, b4);
    c491_after(ctx, call, _ReturnAddress(), slot);
}

void detour_c49_complete(void* ctx, uint64_t id) {
    const uint32_t had = c305_exists_bit(ctx, C49Call{C49Kind::Complete, {id, 0}, 0, 0, nullptr});
    const C49Call call{C49Kind::Complete, {id, 0}, had, 0, nullptr};
    const int32_t slot = c491_enter(call);
    ++g_c49.depth;
    g_orig_c49_complete(ctx, id);
    c491_after(ctx, call, _ReturnAddress(), slot);
}

void detour_c49_complete_any(void* ctx, uint64_t id) {
    const uint32_t had = c305_exists_bit(ctx, C49Call{C49Kind::CompleteAny, {id, 0}, 0, 0, nullptr});
    const C49Call call{C49Kind::CompleteAny, {id, 0}, had, 0, nullptr};
    const int32_t slot = c491_enter(call);
    ++g_c49.depth;
    g_orig_c49_complete_any(ctx, id);
    c491_after(ctx, call, _ReturnAddress(), slot);
}

void detour_c49_complete_sub(void* ctx, uint64_t a, uint64_t b) {
    const uint32_t had = c305_exists_bit(ctx, C49Call{C49Kind::CompleteSub, {a, b}, 0, 0, nullptr});
    const C49Call call{C49Kind::CompleteSub, {a, b}, had, 0, nullptr};
    const int32_t slot = c491_enter(call);
    ++g_c49.depth;
    g_orig_c49_complete_sub(ctx, a, b);
    c491_after(ctx, call, _ReturnAddress(), slot);
}

void detour_c49_increment(void* ctx, uint64_t id) {
    const uint32_t had = c305_exists_bit(ctx, C49Call{C49Kind::Increment, {id, 0}, 0, 0, nullptr});
    const C49Call call{C49Kind::Increment, {id, 0}, had, 0, nullptr};
    const int32_t slot = c491_enter(call);
    ++g_c49.depth;
    g_orig_c49_increment(ctx, id);
    c491_after(ctx, call, _ReturnAddress(), slot);
}

// Card 140 run 7: only the host shows and calls out objectives. The client's own level script also calls
// SetObjectiveVisibility and CalloutObjective outside any host replay (in c140g its pawn's warp into the bombing-run
// pickup area ran a 28-call refresh that hid TacSupport_SO2 and SO41, which the host had shown: marker and in-list
// host 1 client 0 in every digest round). As card 100 does for completions, such a call is skipped on the client;
// the host's frame brings the same change through the replay (g_c49.replay), where the client's own BP handlers that
// the replay fires still run. Counted, and logged up to kC140LocalLines lines.
constexpr uint32_t kC140LocalLines = 24;
uint32_t g_c140_local_skips = 0; // game thread
uint32_t g_c140_local_lines = 0;

bool c140_skip_local(C49Kind kind, uint64_t id, const void* ret) {
    if (!g_client_session || g_c49.replay != 0) {
        return false;
    }
    ++g_c140_local_skips;
    if (note_event_cap(g_c140_local_lines, kC140LocalLines, "card140 local objective skips")) {
        log::line("card140: client t=%llu skipped local %s(%s): only the host shows and calls out objectives (%u skips "
                  "so far); caller exe+0x%llX",
                  static_cast<unsigned long long>(GetTickCount64()), kC49KindText[static_cast<size_t>(kind)],
                  fname_text(&id).text, g_c140_local_skips, exe_rva(ret));
    }
    return true;
}

// Card 432, host: true when the objective exists and already holds this marker and list state, so the call changes
// nothing a client replay would need (FED_C2M1 scripting repeats whole visibility bursts: 27 of 95 rows in c423b127b,
// each about 1 ms of client replay).
constexpr uint32_t kC432SameLines = 8;
uint32_t g_c432_same = 0; // game thread; per map (c432_map_end), printed with each logged skip
uint32_t g_c432_sent = 0; // game thread; per map: host visibility calls that changed something and went to c49_after
uint32_t g_c432_same_lines = 0; // per process

// Host, at LoadMap enter (c260_map_end): the full not-sent / sent split the capped rows leave out.
void c432_map_end() {
    if (g_c432_same + g_c432_sent > 0) {
        log::line("card432: host map totals: SetObjectiveVisibility not sent %u (changed nothing), sent %u", g_c432_same,
                  g_c432_sent);
    }
    g_c432_same = 0;
    g_c432_sent = 0;
}

bool c432_visibility_same(const void* ctx, uint64_t id, bool b0, bool b1) {
    if (g_client_session) {
        return false;
    }
    const void* const subsystem = c49_subsystem(ctx);
    const uint8_t* const o = subsystem != nullptr ? c49_find(subsystem, id) : nullptr;
    return o != nullptr && (o[kObjectiveMarker] != 0) == b0 && (o[kObjectiveInList] != 0) == b1;
}

void detour_c49_visibility(void* ctx, uint64_t id, bool b0, bool b1) {
    if (c140_skip_local(C49Kind::Visibility, id, _ReturnAddress())) {
        return;
    }
    const uint32_t had = c305_exists_bit(ctx, C49Call{C49Kind::Visibility, {id, 0}, 0, 0, nullptr});
    const bool same = c432_visibility_same(ctx, id, b0, b1);
    const C49Call call{C49Kind::Visibility, {id, 0}, c49_bits(b0, b1, false, false, false) | had, 0, nullptr};
    const int32_t slot = c491_enter(call);
    ++g_c49.depth;
    g_orig_c49_visibility(ctx, id, b0, b1);
    if (same && c432_visibility_same(ctx, id, b0, b1)) { // unchanged before and after: no row, no journal entry
        --g_c49.depth;
        ++g_c432_same;
        if (note_event_cap(g_c432_same_lines, kC432SameLines, "card432 unchanged visibility")) {
            log::line("card432: host SetObjectiveVisibility(%s) marker %d list %d changed nothing: not sent (%u so far)",
                      fname_text(&id).text, b0 ? 1 : 0, b1 ? 1 : 0, g_c432_same);
        }
        return;
    }
    if (!g_client_session) {
        ++g_c432_sent;
    }
    c491_after(ctx, call, _ReturnAddress(), slot);
}

void detour_c49_callout(void* ctx, uint64_t id) {
    if (c435_skip_callout(id, _ReturnAddress()) || c140_skip_local(C49Kind::Callout, id, _ReturnAddress())) {
        return;
    }
    const uint32_t had = c305_exists_bit(ctx, C49Call{C49Kind::Callout, {id, 0}, 0, 0, nullptr});
    const C49Call call{C49Kind::Callout, {id, 0}, had, 0, nullptr};
    const int32_t slot = c491_enter(call);
    ++g_c49.depth;
    g_orig_c49_callout(ctx, id);
    c491_after(ctx, call, _ReturnAddress(), slot);
}

// Card 260: USTGameInstance::EndLevel shows WB_TrooperLevelEnd for the local player. Maps that end by boarding the
// transport dropship call it on the host only (EnterDropship runs there), so the host relays it. A client runs it once
// per map, from the relay only.
// Card 290 part B: only the host ends a level. A client's own-script EndLevel is skipped always (a rejoin onto a
// host on its level-end screen ran a second level end); the host replays its EndLevel to a client that joins after
// it (c260_join_replay, after the first Pong).
EndLevelFn g_orig_end_level = nullptr;
namespace {
struct C260State {
    bool ended = false; // client: EndLevel ran on this map (own script or relay)
    bool relay = false; // client: the current EndLevel call is the relay's
    bool host_ended = false; // host: EndLevel ran on this map (card 290: replayed to a late joiner)
};
C260State g_c260;

// Card 345b: the level-end Next button's handler on WB_TrooperLevelEnd and the client's waiting line.
constexpr const char* kC345bNextPrefix = "BndEvt__TrooperLevelEnd_WB_Button_Next";
constexpr const wchar_t* kC345bWaitText = L"Waiting for the host to continue";
constexpr double kC345bWaitX = 40.0;
constexpr double kC345bWaitY = 40.0;
constexpr uint32_t kC345bHeldLines = 8;
constexpr size_t kC345bParmsBytes = 256;
struct C345bState {
    void* game_instance = nullptr; // host: EndLevel's, for the remote PCs
    C31Weak widget;                // the WB_TrooperLevelEnd that has the Next handler
    const void* fn = nullptr;      // its Next handler (a UFunction of the widget class)
    C31Weak wait;                  // client: the waiting line overlay
    bool want_wait = false;        // client: own Next held, the line is not up yet
    bool relayed = false;          // host: the Next sent; client: the host's Next arrived
    bool ran = false;              // client: the own Next handler ran for the host's Next
    uint32_t held = 0;             // client: own Next calls held (log cap)
};
C345bState g_c345b;

void c345b_arm(void* game_instance) {
    g_c345b = C345bState{};
    g_c345b.game_instance = game_instance;
    pe_gate_set(kPeGate_c345b_pe, true);
}

void c345b_drop_wait() {
    void* const wait = c31_weak_get(g_c345b.wait);
    if (wait != nullptr) {
        g_widget_remove_from_parent(wait);
    }
    g_c345b.wait = C31Weak{};
    g_c345b.want_wait = false;
}

// Client: the own Next handler for the host's Next. The direct ProcessEvent passes the hold in detour_process_event.
void c345b_run_next() {
    void* const widget = c31_weak_get(g_c345b.widget);
    if (widget == nullptr || g_c345b.fn == nullptr) {
        return; // c345b_step runs it once the widget is found
    }
    c345b_drop_wait();
    g_c345b.ran = true;
    log::line("card345b: client t=%llu host's level-end Next: own Next handler %s run on %s",
              static_cast<unsigned long long>(GetTickCount64()), object_name(g_c345b.fn).text,
              object_name(widget).text);
    alignas(16) static uint8_t parms[kC345bParmsBytes];
    memset(parms, 0, sizeof(parms));
    g_orig_process_event(widget, const_cast<void*>(g_c345b.fn), parms);
}

void c345b_host_relay() {
    g_c345b.relayed = true;
    uint32_t sent = 0;
    uint32_t flushed = 0;
    for (int32_t s = 0; g_c345b.game_instance != nullptr && s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(g_c345b.game_instance, s);
        if (pc != nullptr && c26_is_remote_pc(pc)) {
            // The host's own Next handler runs next and blocks this tick for the video's load (2014 ms in playb68r3):
            // the frame is flushed now so the client's video starts with the host's, not one load later.
            const bool ok = chan_to_client(pc, ChanOp::EndLevel, 1);
            sent += ok ? 1u : 0u;
            flushed += ok && net_flush_pc(pc) ? 1u : 0u;
        }
    }
    log::line("card345b: host t=%llu level-end Next: relayed to %u, flushed %u",
              static_cast<unsigned long long>(GetTickCount64()), sent, flushed);
}
} // namespace


bool c345b_pe(void* object, void* function) {
    if (function != g_c345b.fn || g_c345b.fn == nullptr || object != c31_weak_get(g_c345b.widget)) {
        return false;
    }
    if (!g_client_session) {
        if (!g_c345b.relayed) {
            c345b_host_relay();
        }
        return false;
    }
    if (g_c345b.held < kC345bHeldLines) {
        log::line("card345b: client t=%llu own level-end Next not run: waiting for the host",
                  static_cast<unsigned long long>(GetTickCount64()));
    }
    ++g_c345b.held;
    g_c345b.want_wait = c31_weak_get(g_c345b.wait) == nullptr;
    return true;
}

void c345b_step(void* pc) {
    if (!pe_gate(kPeGate_c345b_pe)) {
        return;
    }
    if (g_c345b.fn == nullptr || c31_weak_get(g_c345b.widget) == nullptr) {
        void* widget = nullptr;
        int32_t num = 0;
        int32_t index = -1;
        g_c345b.fn = c121_ui_find(kC345bNextPrefix, &widget, &num, &index);
        if (g_c345b.fn == nullptr) {
            return;
        }
        c31_weak_set(g_c345b.widget, widget);
        log::line("card345b: %s t=%llu level-end Next handler %s on %s (stack element %d of %d)", side(),
                  static_cast<unsigned long long>(GetTickCount64()), object_name(g_c345b.fn).text,
                  object_name(widget).text, index, num);
    }
    if (g_client_session && g_c345b.relayed && !g_c345b.ran) {
        c345b_run_next();
        return;
    }
    if (g_c345b.want_wait && !g_c345b.ran) {
        g_c345b.want_wait = false;
        void* const wait = c31_text_overlay(pc, kC345bWaitText, kC345bWaitX, kC345bWaitY);
        c31_weak_set(g_c345b.wait, wait);
        log::line("card345b: client t=%llu waiting line %s", static_cast<unsigned long long>(GetTickCount64()),
                  wait != nullptr ? "shown" : "NOT shown");
    }
}

void detour_end_level(void* game_instance) {
    const unsigned long long t = GetTickCount64();
    if (!g_client_session) {
        g_orig_end_level(game_instance);
        g_c260.host_ended = true; // card 290
        c345b_arm(game_instance);
        uint32_t sent = 0;
        for (int32_t s = 0; s < kC17Slots; ++s) {
            void* const pc = g_get_player_controller(game_instance, s);
            if (pc != nullptr && c26_is_remote_pc(pc)) {
                sent += chan_to_client(pc, ChanOp::EndLevel, 0) ? 1u : 0u;
            }
        }
        log::line("card260: host EndLevel, sent to %u (t=%llu, caller exe+0x%llX)", sent, t,
                  exe_rva(_ReturnAddress()));
        return;
    }
    if (!g_c260.relay) {
        // Card 290: the relay is the only path; a second EndLevel would stack a second level-end screen.
        log::line("card260: client EndLevel (own script) skipped: only the host ends a level (t=%llu, caller exe+0x%llX)",
                  t, exe_rva(_ReturnAddress()));
        return;
    }
    g_c260.ended = true;
    g_orig_end_level(game_instance);
    const bool relayed_next = g_c345b.relayed; // card 345b: the host's Next can arrive before the EndLevel ran
    c345b_arm(game_instance);
    g_c345b.relayed = relayed_next;
    log::line("card260: client EndLevel (%s) (t=%llu, caller exe+0x%llX)", g_c260.relay ? "relay" : "own script", t,
              exe_rva(_ReturnAddress()));
}

void c260_receive(void* pc, const ChanArgs& args) {
    if (args.v[0] == 1) { // card 345b: the host's level-end Next
        g_c345b.relayed = true;
        log::line("card345b: client t=%llu host's level-end Next arrived (EndLevel %s)",
                  static_cast<unsigned long long>(GetTickCount64()), g_c260.ended ? "ran" : "not run yet");
        c345b_run_next();
        return;
    }
    if (g_c260.ended) {
        log::line("card260: client relay dropped, already ended (t=%llu)",
                  static_cast<unsigned long long>(GetTickCount64()));
        return;
    }
    void* const gi = g_get_st_game_instance(pc);
    if (gi == nullptr) {
        log::line("WARN card260: client relay dropped: no game instance from %s", object_name(pc).text);
        return;
    }
    g_c260.relay = true;
    detour_end_level(gi);
    g_c260.relay = false;
}

void c260_map_end() {
    c432_map_end();
    g_c260 = C260State{};
    c345b_drop_wait();
    g_c345b = C345bState{};
    pe_gate_set(kPeGate_c345b_pe, false);
}

// Card 290 part B, host: a client that joins after this map's EndLevel gets it once it has loaded (first Pong).
void c260_join_replay(void* pc) {
    if (!g_c260.host_ended) {
        return;
    }
    const bool sent = chan_to_client(pc, ChanOp::EndLevel, 0);
    log::line("%scard290: host t=%llu EndLevel replayed to late joiner %s: %s", sent ? "" : "WARN ",
              static_cast<unsigned long long>(GetTickCount64()), object_name(pc).text, sent ? "sent" : "NOT sent");
}

// Both sides, log only: the level-end checks and saves (the client's replay must not cause any).
bool detour_c49_check_all(void* ctx) {
    const bool all = g_orig_c49_check_all(ctx);
    if (note_event_cap(g_c49.audit_lines, kC49AuditLines, "card049 audit")) {
        log::line("card049: %s t=%llu CheckAllObjectivesComplete(ctx %s) = %d, depth %u, caller exe+0x%llX", side(),
                  static_cast<unsigned long long>(GetTickCount64()), class_name(ctx).text, all ? 1 : 0, g_c49.depth,
                  exe_rva(_ReturnAddress()));
    }
    return all;
}

bool detour_c49_gate(void* ctx, uint64_t id, void* ids) {
    const bool open = g_orig_c49_gate(ctx, id, ids);
    if (note_event_cap(g_c49.audit_lines, kC49AuditLines, "card049 audit")) {
        log::line("card049: %s t=%llu ObjectivesCompletedGate(%s, %d ids) = %d, depth %u, caller exe+0x%llX", side(),
                  static_cast<unsigned long long>(GetTickCount64()), fname_text(&id).text,
                  ids != nullptr ? read_i32(ids, kArrayNum) : -1, open ? 1 : 0, g_c49.depth, exe_rva(_ReturnAddress()));
    }
    return open;
}

void detour_c49_save(void* engine, void* metadata, void* delegate, bool flag) {
    char chain[96];
    caller_chain(chain, sizeof(chain));
    log::line("card049: %s t=%llu ASTSaveSystemEngine::Save(flag %d); callers %s", side(),
              static_cast<unsigned long long>(GetTickCount64()), flag ? 1 : 0, chain);
    g_orig_c49_save(engine, metadata, delegate, flag);
}

// The number of ASTLevelObjectiveManager actors in the world.
int32_t c49_manager_count(const void* world) {
    const void* const cls = g_c49_manager_class();
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = read_i32(world, kWorldLevels + kArrayNum);
    int32_t n = 0;
    for (int32_t l = 0; levels != nullptr && l < num_levels && l < kMaxLevels; ++l) {
        const void* const level = levels[l];
        const auto* const actors = level != nullptr ? static_cast<void* const*>(read_ptr(level, kLevelActors)) : nullptr;
        const int32_t num = level != nullptr ? read_i32(level, kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; actors != nullptr && a < num && a < kMaxLevelActors; ++a) {
            n += (actors[a] != nullptr && is_a(actors[a], cls)) ? 1 : 0;
        }
    }
    return n;
}

// One objective of the subsystem map: key, the object's own id (layout check), class and state bytes.
void c49_objective_line(int32_t i, const uint8_t* element) {
    const void* const objective = read_ptr(element, kObjectiveElementValue);
    if (objective == nullptr) {
        log::line("card049: %s   [%d] %s -> null", side(), i, fname_text(element).text);
        return;
    }
    const void* const parent = read_ptr(objective, kObjectiveParent);
    const auto* const o = static_cast<const uint8_t*>(objective);
    log::line("card049: %s   [%d] key %s, object %s (%s), id %s, parent %s, marker %u, complete %u, in list %u%s",
              side(), i, fname_text(element).text, object_name(objective).text, class_name(objective).text,
              fname_text(o + kObjectiveId).text, parent != nullptr ? fname_text(static_cast<const uint8_t*>(parent) +
                                                                                kObjectiveId).text : "none",
              o[kObjectiveMarker], o[kObjectiveComplete], o[kObjectiveInList],
              is_a(objective, g_c49_count_class()) ? ", count objective" : "");
}

// Both sides, once per map load (+kC49WorldCheckMs): the subsystem, its objectives, the manager actors.
void c49_world_check(void* pc) {
    void* const world = g_actor_get_world(pc);
    if (g_gate_diag) {
        // Card 412: this pass walks every actor in play (14 to 20 ms on one host frame, playb69/b71/b72); the map
        // load and map end passes run behind a loading screen and stay in every build.
        c132_inventory(world, "+90 s"); // card 132 step 1: host only
    }
    void* const subsystem = world != nullptr ? g_c49_get_subsystem(world) : nullptr;
    SparseView v{};
    const bool read = c49_objectives(subsystem, v);
    log::line("card049: %s t=%llu world check: subsystem %s %p, %d objectives (%d slots), %d ASTLevelObjectiveManager",
              side(), static_cast<unsigned long long>(GetTickCount64()), class_name(subsystem).text, subsystem,
              c49_objective_count(subsystem), read ? v.count : -1, world != nullptr ? c49_manager_count(world) : -1);
    if (subsystem != nullptr) {
        c49_delegate_lines(static_cast<const uint8_t*>(subsystem)); // what a HUD panel could bind to
        c49_panel_lines(static_cast<const uint8_t*>(subsystem));
    }
    for (int32_t i = 0, listed = 0; read && i < v.count && listed < kC49ListedObjectives; ++i) {
        if (sparse_allocated(v, i)) {
            c49_objective_line(i, v.data + static_cast<size_t>(i) * kObjectiveElementBytes);
            ++listed;
        }
    }
}


// --- Card 053: the client's objective map and list rebuilt to the host's checkpoint snapshot ---
// Layouts: docs/cards/053-objective-list-rebuild.md Addendum 3 (TMap<FName, USTLevelObjective*>, 0x50 bytes) and
// Static facts (SetObjectiveMapPostSave frees the passed map's element array and hash itself).
constexpr int32_t kC53MaxElements = 128; // the inline allocation-flag bits
constexpr size_t kC53MapBytes = 0x50;
constexpr size_t kC53Elements = 0x00, kC53ArrayNum = 0x08, kC53ArrayMax = 0x0C, kC53FlagBits = 0x10;
constexpr size_t kC53NumBits = 0x28, kC53MaxBits = 0x2C, kC53FirstFree = 0x30; // NumFreeIndices (+0x34) stays 0
constexpr size_t kC53HashInline = 0x38, kC53HashHeap = 0x40, kC53HashSize = 0x48;
constexpr size_t kC53ElemNext = 0x10, kC53ElemBucket = 0x14;
constexpr size_t kC53PanelBox = 0x358; // WB_Objective_List_C ObjectivesVerticleBox (card049x4 dump)
constexpr size_t kC53PanelItems = 0x370; // WB_Objective_List_C ObjectiveListItems (TMap, 0x50 bytes)
constexpr size_t kC53BitsHeap = 0x20;    // allocation bits past the inline 128: heap pointer

// GetTypeHash(FName) as the game's inlined Find computes it (port of 0x01338A90 plus Number).
uint32_t c53_fname_hash(uint64_t name) {
    const uint32_t id = static_cast<uint32_t>(name);
    const uint32_t number = static_cast<uint32_t>(name >> 32);
    const uint32_t offset = id & 0xFFFFu;
    const uint32_t block = id >> 16;
    return (block << 19) + block + (offset << 16) + offset + (offset >> 4) + number;
}

// UE's default bucket count: 1 below 4 elements, else the power of two at or above n/2 + 8.
int32_t c53_hash_size(int32_t n) {
    if (n < 4) {
        return 1;
    }
    int32_t size = 1;
    for (int32_t i = 0; i < 30 && size < n / 2 + 8; ++i) {
        size <<= 1;
    }
    return size;
}

// True when the client's objective ids, in map slot order, equal the snapshot's ids (true when unreadable: no
// rebuild without a map).
bool c53_same_order(const void* subsystem) {
    SparseView v{};
    if (!c49_objectives(subsystem, v)) {
        return true;
    }
    int32_t k = 0;
    for (int32_t i = 0; i < v.count; ++i) {
        const uint8_t* const o = c49_slot(v, i);
        if (o == nullptr) {
            continue;
        }
        if (k >= g_c49m.n_snapshot || read_u64(o, kObjectiveId) != g_c49m.snapshot_ids[k]) {
            return false;
        }
        ++k;
    }
    return k == g_c49m.n_snapshot;
}

// The snapshot ids the client has, with their objects, in the host's order; ids the client lacks are logged.
int32_t c53_collect(const void* subsystem, uint64_t (&ids)[kC53MaxElements], void* (&objs)[kC53MaxElements]) {
    int32_t n = 0;
    for (int32_t k = 0; k < g_c49m.n_snapshot && n < kC53MaxElements; ++k) {
        const uint8_t* const o = c49_find(subsystem, g_c49m.snapshot_ids[k]);
        if (o == nullptr) {
            log::line("WARN card053: client lacks the host's objective %s: left out of the rebuilt map",
                      fname_text(&g_c49m.snapshot_ids[k]).text);
            continue;
        }
        ids[n] = g_c49m.snapshot_ids[k];
        objs[n] = const_cast<uint8_t*>(o);
        ++n;
    }
    return n;
}

// The bucket array of a built map: the heap one, or the inline one when the heap pointer is null.
int32_t* c53_buckets(uint8_t* map) {
    auto* const heap = static_cast<int32_t*>(read_ptr(map, kC53HashHeap));
    return heap != nullptr ? heap : reinterpret_cast<int32_t*>(map + kC53HashInline);
}

// Writes the element array, allocation bits and hash chains of `map` (zeroed, element array allocated).
void c53_fill(uint8_t* map, uint8_t* elems, const uint64_t* ids, void* const* objs, int32_t n, int32_t hash_size) {
    int32_t* const buckets = c53_buckets(map);
    for (int32_t b = 0; b < hash_size; ++b) {
        buckets[b] = -1;
    }
    auto* const bits = reinterpret_cast<uint32_t*>(map + kC53FlagBits);
    for (int32_t i = 0; i < n; ++i) {
        uint8_t* const e = elems + static_cast<size_t>(i) * kObjectiveElementBytes;
        const int32_t b = static_cast<int32_t>(c53_fname_hash(ids[i]) & static_cast<uint32_t>(hash_size - 1));
        memcpy(e, &ids[i], sizeof(uint64_t));
        memcpy(e + kObjectiveElementValue, &objs[i], sizeof(void*));
        memcpy(e + kC53ElemNext, &buckets[b], sizeof(int32_t));
        memcpy(e + kC53ElemBucket, &b, sizeof(int32_t));
        buckets[b] = i;
        bits[i / 32] |= 1u << (i % 32);
    }
    const int32_t minus_one = -1;
    const int32_t max_bits = kC53MaxElements;
    memcpy(map + kC53Elements, &elems, sizeof(void*));
    memcpy(map + kC53ArrayNum, &n, sizeof(int32_t));
    memcpy(map + kC53ArrayMax, &n, sizeof(int32_t));
    memcpy(map + kC53NumBits, &n, sizeof(int32_t));
    memcpy(map + kC53MaxBits, &max_bits, sizeof(int32_t));
    memcpy(map + kC53FirstFree, &minus_one, sizeof(int32_t));
    memcpy(map + kC53HashSize, &hash_size, sizeof(int32_t));
}

// Allocates (engine allocator) and fills `map`. False (logged, nothing left allocated) on a failed allocation.
bool c53_build(uint8_t* map, const uint64_t* ids, void* const* objs, int32_t n) {
    const int32_t hash_size = c53_hash_size(n);
    memset(map, 0, kC53MapBytes);
    auto* const elems = static_cast<uint8_t*>(g_memory_malloc(static_cast<size_t>(n) * kObjectiveElementBytes, 8));
    void* const hash = hash_size > 1 && elems != nullptr
                           ? g_memory_malloc(static_cast<size_t>(hash_size) * sizeof(int32_t), 4) : nullptr;
    if (elems == nullptr || (hash_size > 1 && hash == nullptr)) {
        log::line("WARN card053: client FMemory::Malloc failed (elements %p, hash %p, %d buckets): no rebuild",
                  static_cast<void*>(elems), hash, hash_size);
        if (elems != nullptr) {
            g_memory_free(elems);
        }
        return false;
    }
    memcpy(map + kC53HashHeap, &hash, sizeof(void*));
    c53_fill(map, elems, ids, objs, n, hash_size);
    return true;
}

// Looks every id up through the built buckets and chains, as the game's Find does. False on the first miss.
bool c53_check(uint8_t* map, const uint64_t* ids, int32_t n) {
    const int32_t* const buckets = c53_buckets(map);
    const auto* const elems = static_cast<const uint8_t*>(read_ptr(map, kC53Elements));
    const uint32_t mask = static_cast<uint32_t>(read_i32(map, kC53HashSize) - 1);
    for (int32_t i = 0; i < n; ++i) {
        int32_t j = buckets[c53_fname_hash(ids[i]) & mask];
        bool found = false;
        for (int32_t step = 0; !found && j >= 0 && j < n && step <= n; ++step) {
            const uint8_t* const e = elems + static_cast<size_t>(j) * kObjectiveElementBytes;
            found = read_u64(e, 0) == ids[i];
            j = read_i32(e, kC53ElemNext);
        }
        if (!found) {
            log::line("WARN card053: client self-check: %s not found through its bucket: no rebuild",
                      fname_text(&ids[i]).text);
            return false;
        }
    }
    return true;
}

// Frees a built map that was not handed to the game.
void c53_free(uint8_t* map) {
    void* const hash = read_ptr(map, kC53HashHeap);
    if (hash != nullptr) {
        g_memory_free(hash);
    }
    g_memory_free(read_ptr(map, kC53Elements));
}

// True when the panel's top-level items show the snapshot's root objectives in the host's order.
bool c53_items_match(const uint8_t* panel) {
    const auto* const box = static_cast<const uint8_t*>(read_ptr(panel, kC53PanelBox));
    const auto* const slots = static_cast<const uint8_t* const*>(box != nullptr ? read_ptr(box, 0x168) : nullptr);
    const int32_t num = box != nullptr ? read_i32(box, 0x168 + kArrayNum) : -1;
    int32_t k = 0;
    for (int32_t i = 0; slots != nullptr && i < num && i < kC53MaxElements; ++i) {
        const uint8_t* const item =
            slots[i] != nullptr ? static_cast<const uint8_t*>(read_ptr(slots[i], 0x30)) : nullptr;
        const uint8_t* const o = item != nullptr ? static_cast<const uint8_t*>(read_ptr(item, 0x398)) : nullptr;
        if (o == nullptr) {
            return false;
        }
        for (; k < g_c49m.n_snapshot && g_c49m.snapshot_ids[k] != read_u64(o, kObjectiveId); ++k) {
        }
        if (k == g_c49m.n_snapshot) {
            return false; // an item out of the host's order, or not in the snapshot
        }
        ++k;
    }
    return num >= 0;
}

// One parameterless widget function through ProcessEvent (its reflected parameters logged). False when absent.
bool c53_call(void* panel, uint64_t& cache, const wchar_t* name) {
    void* const fn = fname_cached(cache, name) != 0 ? g_find_function(panel, cache) : nullptr;
    char params[160];
    params[0] = '\0';
    if (fn != nullptr) {
        c49_param_text(fn, params, sizeof(params));
    }
    log::line("%scard053: client t=%llu ProcessEvent(%s, %ls) %s; params:%s", fn != nullptr ? "" : "WARN ",
              static_cast<unsigned long long>(GetTickCount64()), object_name(panel).text, name,
              fn != nullptr ? "called" : "not found", params[0] != '\0' ? params : " none");
    if (fn == nullptr) {
        return false;
    }
    alignas(16) static uint8_t parms[256];
    memset(parms, 0, sizeof(parms));
    g_orig_process_event(panel, fn, parms);
    return true;
}

// Addendum 2 fallback (a): FirstInit back to true, then OnInitialized.
void c53_reinit(void* panel) {
    static uint64_t on_initialized = 0;
    const uint8_t* const prop = prop_find_prop(read_ptr(panel, kObjectClass), "FirstInit");
    if (prop == nullptr) {
        log::line("WARN card053: client panel has no FirstInit property: fallback (a) not run");
        return;
    }
    static_cast<uint8_t*>(panel)[read_i32(prop, kPropertyOffset)] = 1;
    c53_call(panel, on_initialized, L"OnInitialized");
}

// Empties the panel's ObjectiveListItems map (same 0x50 TMap layout as the subsystem's) back to the
// default-constructed state, freeing its allocations. False (map untouched) when its allocation bits are on the heap.
bool c53_empty_items(uint8_t* panel) {
    uint8_t* const items = panel + kC53PanelItems;
    const int32_t max_bits = read_i32(items, kC53MaxBits);
    if (max_bits > kC53MaxElements || read_ptr(items, kC53BitsHeap) != nullptr) {
        log::line("WARN card053: client ObjectiveListItems has %d allocation bits (inline %d): not emptied", max_bits,
                  kC53MaxElements);
        return false;
    }
    log::line("card053: client ObjectiveListItems emptied (%d entries)", read_i32(items, kC53ArrayNum));
    if (read_ptr(items, kC53Elements) != nullptr) {
        c53_free(items);
    } else if (read_ptr(items, kC53HashHeap) != nullptr) {
        g_memory_free(read_ptr(items, kC53HashHeap));
    }
    memset(items, 0, kC53MapBytes);
    return true;
}

// Step 2: the panel built at the HUD's re-creation shows the client's pre-snapshot map. Its box and item map are
// emptied, then fallback (a) re-adds every item from the rebuilt map. (WB_Objective_List has no `Clear` function:
// card053x.) Items dumped before and after.
void c53_widget(const void* subsystem) {
    static uint64_t clear_children = 0;
    auto* const panel = const_cast<uint8_t*>(c49_panel(static_cast<const uint8_t*>(subsystem)));
    void* const box = panel != nullptr ? read_ptr(panel, kC53PanelBox) : nullptr;
    if (box == nullptr || g_find_function == nullptr || g_orig_process_event == nullptr) {
        log::line("WARN card053: client objective panel %p or its box %p not found (FindFunction %p): list not rebuilt",
                  static_cast<void*>(panel), box, reinterpret_cast<void*>(g_find_function));
        return;
    }
    log::line("card053: client panel items before the list rebuild (match %d):", c53_items_match(panel));
    c49_item_lines(static_cast<const uint8_t*>(box), 0);
    if (c53_call(box, clear_children, L"ClearChildren") && c53_empty_items(panel)) {
        c53_reinit(panel);
    }
    log::line("%scard053: client panel items after ClearChildren and fallback (a) (match %d):",
              c53_items_match(panel) ? "" : "WARN ", c53_items_match(panel));
    c49_item_lines(static_cast<const uint8_t*>(read_ptr(panel, kC53PanelBox)), 0);
    c49_shot(GetTickCount64() + kC49ShotAfterMs, "card053 client panel after the rebuild");
}

// Step 1: the client's map rebuilt in the host's snapshot order through its own SetObjectiveMapPostSave.
void c53_rebuild(void* pc) {
    void* const subsystem = c49_subsystem(pc);
    uint64_t ids[kC53MaxElements] = {};
    void* objs[kC53MaxElements] = {};
    if (subsystem == nullptr || g_memory_malloc == nullptr || g_memory_free == nullptr ||
        g_orig_set_objective_map == nullptr || g_c49_exists == nullptr || g_c49m.n_snapshot > kC53MaxElements) {
        log::line("WARN card053: client rebuild not run: subsystem %p, %d snapshot ids (max %d)", subsystem,
                  g_c49m.n_snapshot, kC53MaxElements);
        return;
    }
    const int32_t n = c53_collect(subsystem, ids, objs);
    alignas(16) uint8_t map[kC53MapBytes];
    if (n == 0 || !c53_build(map, ids, objs, n)) {
        return;
    }
    if (!c53_check(map, ids, n)) {
        c53_free(map);
        return;
    }
    log::line("card053: client t=%llu SetObjectiveMapPostSave with %d objectives in the host's order (was %d), "
              "hash size %d", static_cast<unsigned long long>(GetTickCount64()), n, c49_objective_count(subsystem),
              read_i32(map, kC53HashSize));
    g_orig_set_objective_map(subsystem, map); // the callee frees the element array and the hash
    int32_t found = 0;
    for (int32_t i = 0; i < n; ++i) {
        found += g_c49_exists(subsystem, ids[i]) ? 1 : 0;
    }
    log::line("%scard053: client map after: %d objectives, DoesObjectiveExistForID true for %d of %d, order %s",
              found == n ? "" : "WARN ", c49_objective_count(subsystem), found, n,
              c53_same_order(subsystem) ? "matches" : "DIFFERS");
    c53_widget(subsystem);
    g_c53_rebuilt = true;
}

// Host test (card053x3), phase 2: once, kC53EventMs after the checkpoint snapshot (past the client's rebuild),
// complete the test objective's root (TacSupport_01 for TacSupport_SO0) through the detour so it is mirrored.
void c53_test_step(void* pc, ULONGLONG now) {
    if (g_client_session || g_c53_event_ms == 0 || now < g_c53_event_ms || g_c45_phase != 2) {
        return;
    }
    g_c53_event_ms = 0;
    uint64_t id = 0;
    g_fname_ctor(&id, g_c49_test_id, kFNameAdd);
    const uint8_t* const o = c49_find(c49_subsystem(pc), id);
    const void* const parent = o != nullptr ? read_ptr(o, kObjectiveParent) : nullptr;
    const uint64_t root = parent != nullptr ? read_u64(parent, kObjectiveId) : id;
    log::line("card053: host t=%llu test: CompleteObjective(%s) after the client's rebuild",
              static_cast<unsigned long long>(now), fname_text(&root).text);
    detour_c49_complete(pc, root);
    c49_shot(now + kC49ShotAfterMs, "card053 host after the post-rebuild completion");
}

void c53_step(void* pc, ULONGLONG now) {
    if (g_c53_after_ms != 0 && now >= g_c53_after_ms) {
        g_c53_after_ms = 0;
        void* const subsystem = c49_subsystem(pc);
        if (subsystem != nullptr) {
            c53_dump(subsystem, "1.5 s after the event");
        }
    }
    if (g_c49m.c53_due_ms != 0 && now >= g_c49m.c53_due_ms) {
        g_c49m.c53_due_ms = 0;
        c53_rebuild(pc);
    }
}

// Card 370: a pawn on the ground: CharacterMovement not Falling (a jump, a step off a ledge or a fall below the map
// all read Falling) and not under the kill height. In c143b55d both pawns fell below the map and each kill floor warp
// put one on the other, still falling, until both were down.
bool c46_grounded(const void* pawn) {
    const auto* const cmc = static_cast<const uint8_t*>(read_ptr(pawn, kCharacterMovement));
    return cmc != nullptr && cmc[kCmcMovementMode] != kMoveFalling &&
           actor_location(pawn).z >= static_cast<double>(c46_kill_z(pawn));
}

// The first player pawn that is Up and on the ground (card 370), `except` (an id) left out.
void* c46_up_target(const C43Roster& r, int32_t except) {
    for (int32_t s = 0; s < kC43Max; ++s) {
        void* const pawn = r.p[s].pawn;
        if (pawn != nullptr && r.p[s].id != except && is_a(pawn, g_player_character_class()) &&
            !c43_down_state(c43_life(pawn)) && c46_grounded(pawn)) {
            return pawn;
        }
    }
    return nullptr;
}

// Host: moves `pawn` (roster slot `slot`) to `target`, tells a copy's client, queues the follow-up check.
void c46_warp(void* pawn, void* target, const C43Roster& r, int32_t slot, ULONGLONG now) {
    const int32_t id = object_index(pawn);
    const int32_t target_slot = c43_slot(r, object_index(target));
    c46_mark_warp(id, now);
    ++g_c46.warp_count;
    const Vec3 from = actor_location(pawn);
    const Vec3 t = actor_location(target);
    Vec3 spot{};
    const bool room = c46_side_spot(pawn, target, &spot);
    const int32_t impart = c461_warp_clear(pawn, slot, now); // card 461
    const bool moved = c46_move_to(pawn, spot);
    c461_warp_watch(pawn, slot, now, impart);
    const Vec3 to = actor_location(pawn);
    const bool copy = sprint_side(pawn) == kSideServerCopy;
    uint32_t lo = 0;
    uint32_t hi = 0;
    const bool addressable = c41_guid_args(c41_guid(target), lo, hi); // card 209: the target pawn by NetGUID
    // Card 461: the host's spot as an offset from the target, so the client lands where the host's copy did.
    const ChanArgs args{{lo, hi, c46_offset_arg(spot.x - t.x), c46_offset_arg(spot.y - t.y),
                         c46_offset_arg(spot.z - t.z)}};
    const bool sent = copy && addressable && chan_to_client_args(r.p[slot].pc, ChanOp::Warp, args);
    bool sent_to = false; // card 389: a target with no sendable NetGUID (a host just off the dropship): by position
    if (copy && !addressable) {
        static wchar_t wire[kChanTextFrame];
        char text[kChanTextMax + 1];
        // Card 399: the target's yaw as a fourth field, so a ride landing faces the host's way (a reader of three
        // fields ignores it).
        const void* const root = read_ptr(target, kActorRootComponent);
        const double yaw = root != nullptr ? component_yaw(root) : 0.0;
        snprintf(text, sizeof(text), "%.0f %.0f %.0f %.1f", to.x, to.y, to.z, yaw);
        sent_to = chan_send_client(r.p[slot].pc, chan_text_frame(ChanOp::WarpTo, text, wire));
    }
    log::line("card046: host t=%llu kill floor warp %u: %s (%s) at (%.0f, %.0f, %.0f), KillZ %.0f, to %s (slot %d) "
              "at (%.0f, %.0f, %.0f), beside %s (%+.0f, %+.0f, %+.0f): now (%.0f, %.0f, %.0f), moved %d; Warp frame %s",
              static_cast<unsigned long long>(now), g_c46.warp_count, object_name(pawn).text,
              kSprintSideText[sprint_side(pawn)], from.x, from.y, from.z, static_cast<double>(c46_kill_z(pawn)),
              object_name(target).text, target_slot, t.x, t.y, t.z, room ? "free" : "no room, raised",
              spot.x - t.x, spot.y - t.y, spot.z - t.z, to.x, to.y, to.z, moved ? 1 : 0,
              !copy ? "not needed" : sent ? "sent" : addressable ? "NOT sent"
                  : sent_to ? "not sendable by guid: WarpTo sent" : "NOT sent (target guid not sendable)");
    c46_add_follow(id, object_index(target), now);
}

// Host: a server copy below the floor with nobody else Up goes Out and moves to the host's pawn.
void c46_copy_out(void* pawn, const C43Roster& r, int32_t slot, ULONGLONG now) {
    const int32_t id = object_index(pawn);
    const int32_t i = c43_find(id);
    if (i >= 0 && g_c43.e[i].state == C43Life::Up) {
        c43_remove(i); // a revived player still in its StandUp window
    }
    const int32_t j = c43_find(id);
    if (j < 0) {
        c43_enter_down(pawn, r, static_cast<uint32_t>(slot), C43Life::Out, now);
    } else if (g_c43.e[j].state == C43Life::Downed) {
        g_c43.e[j].state = C43Life::Out;
        g_c43.e[j].secs_left = 0.0f;
        c43_transition(r, g_c43.e[j], pawn, "kill floor: Out");
    }
    log::line("card046: host t=%llu kill floor: %s below with nobody else Up: %s, no StartDeath",
              static_cast<unsigned long long>(now), object_name(pawn).text,
              kC43LifeText[static_cast<size_t>(c43_life(pawn))]);
    if (r.p[0].pawn != nullptr) {
        c46_warp(pawn, r.p[0].pawn, r, slot, now);
    }
}

void c46_still_line(const void* pawn, int32_t w, ULONGLONG now) {
    C46Warp& row = g_c46.warp[w];
    if (row.still_lines < kC46StillLines) {
        ++row.still_lines;
        log::line("WARN card046: host %s still below the kill height %llu ms after its warp (z %.0f, KillZ %.0f): "
                  "no warp until %llu ms pass",
                  object_name(pawn).text, static_cast<unsigned long long>(now - row.ms), actor_location(pawn).z,
                  static_cast<double>(c46_kill_z(pawn)), static_cast<unsigned long long>(kC46CooldownMs));
    }
}

// Card 146, host: a server copy below the kill height while all players are down (no Up player to warp beside, and
// no StartDeath: the host's team wipe owns the end) goes back to c46_rescue_spot, and its client moves its own pawn
// there (WarpTo "x y z"). Before card 146 the gate was off here and the copy fell forever (playtest 2026-11-08).
// Card 146 / 370, host: moves `pawn` (roster slot `slot`) to c46_rescue_spot; a server copy's client moves its own
// pawn there (WarpTo "x y z"). `why` names the case in the rows. False when there is no spot (held below).
bool c46_return_to_spot(void* pawn, void* world, const C43Roster& r, int32_t slot, ULONGLONG now, const char* why) {
    const void* const cls = read_ptr(pawn, kObjectClass);
    const void* const cdo = cls != nullptr ? read_ptr(cls, kClassDefaultObject) : nullptr;
    Vec3 to{};
    const char* from = "none";
    if (cdo == nullptr || !c46_rescue_spot(world, cdo, r, &to, &from)) {
        log::line("WARN card046: host t=%llu kill floor %s: no spot for %s (class default %p, no PlayerStart): held "
                  "below", static_cast<unsigned long long>(now), why, object_name(pawn).text, cdo);
        return false;
    }
    ++g_c46.warp_count;
    const Vec3 at = actor_location(pawn);
    const bool moved = c46_move_to(pawn, to);
    const bool copy = sprint_side(pawn) == kSideServerCopy;
    bool sent = false;
    if (copy) {
        static wchar_t wire[kChanTextFrame];
        char text[kChanTextMax + 1];
        snprintf(text, sizeof(text), "%.0f %.0f %.0f", to.x, to.y, to.z);
        sent = chan_send_client(r.p[slot].pc, chan_text_frame(ChanOp::WarpTo, text, wire));
    }
    log::line("card146: host t=%llu kill floor %s: %s (%s) at (%.0f, %.0f, %.0f), KillZ %.0f, back to %s at (%.0f, "
              "%.0f, %.0f), moved %d; WarpTo frame %s",
              static_cast<unsigned long long>(now), why, object_name(pawn).text, kSprintSideText[sprint_side(pawn)],
              at.x, at.y, at.z, static_cast<double>(c46_kill_z(pawn)), from, to.x, to.y, to.z, moved ? 1 : 0,
              !copy ? "not needed" : sent ? "sent" : "NOT sent");
    return true;
}

bool c46_all_down_return(void* pawn, void* world, const C43Roster& r, int32_t slot) {
    const ULONGLONG now = GetTickCount64();
    const int32_t id = object_index(pawn);
    const int32_t w = c46_find_warp(id);
    if (w >= 0 && now < g_c46.warp[w].ms + kC46CooldownMs) {
        c46_still_line(pawn, w, now);
        return false;
    }
    c46_mark_warp(id, now);
    c46_return_to_spot(pawn, world, r, slot, now, "while all down");
    return false;
}

// Host: a player pawn below the kill height. Returns the detour's result.
bool c46_host_below(void* pawn) {
    const bool copy = sprint_side(pawn) == kSideServerCopy;
    void* const world = g_actor_get_world(pawn);
    const C43Roster r = world != nullptr ? c43_roster(world) : C43Roster{};
    const int32_t id = object_index(pawn);
    const int32_t slot = c43_slot(r, id);
    if (copy && g_c43.all_down && net_mode(pawn) == kNetModeListenServer && r.players >= 2 && slot >= 0) {
        return c46_all_down_return(pawn, world, r, slot);
    }
    if (net_mode(pawn) != kNetModeListenServer || r.players < 2 || g_c43.all_down || slot < 0) {
        ++g_c46.vanilla;
        if (note_event_cap(g_c46.gate_lines, kC46GateLines, "card046 gate off")) {
            log::line("card046: host kill floor on %s, gate off (net mode %u, players %u, all down %d, slot %d): %s",
                      object_name(pawn).text, net_mode(pawn), r.players, g_c43.all_down ? 1 : 0, slot,
                      copy ? "server copy held (false)" : "vanilla death");
        }
        return !copy;
    }
    const ULONGLONG now = GetTickCount64();
    const int32_t w = c46_find_warp(id);
    if (w >= 0 && now < g_c46.warp[w].ms + kC46CooldownMs) {
        c46_still_line(pawn, w, now);
        return false;
    }
    void* const target = c46_up_target(r, id);
    if (target != nullptr) {
        c46_warp(pawn, target, r, slot, now);
        c46_hurt(pawn);
        return false;
    }
    // Card 370: no grounded Up teammate (each other one is down, in a jump or below the map too). A pawn still Up goes
    // to the rescue anchor, not to the all-down path; with no checkpoint save yet on this map, the path below stands.
    if (!c43_down_state(c43_life(pawn))) {
        if (g_c46.anchor_world == world) {
            c46_mark_warp(id, now);
            if (c46_return_to_spot(pawn, world, r, slot, now, "with no grounded teammate Up")) {
                c46_hurt(pawn);
            }
            return false;
        }
        if (!g_c46.no_anchor_warned) {
            g_c46.no_anchor_warned = true;
            log::line("WARN card046: host t=%llu kill floor on %s (Up) with no grounded teammate Up and no "
                      "checkpoint save yet on this map: no rescue anchor, %s",
                      static_cast<unsigned long long>(now), object_name(pawn).text,
                      copy ? "the copy goes Out" : "all down, vanilla death");
        }
    }
    if (copy) {
        c46_copy_out(pawn, r, slot, now);
        return false;
    }
    c43_all_down(r, pawn, "all down: kill floor");
    ++g_c46.vanilla;
    log::line("card046: host t=%llu kill floor: own pawn %s below with nobody else Up: all down, vanilla death",
              static_cast<unsigned long long>(now), object_name(pawn).text);
    return true;
}

// Card 389, host: FED_C5M1 and FED_C5M2 have no ground at the PlayerStart and no kill floor call in reach (run
// c389b72: the client's copy, spawned on foot there while the host rode the dropship, fell from z -437 to -583819 in
// 150 s, no card 046 row, and never came back). Once a second: an Up player pawn, not on a dropship, that read
// Falling at every sample for kC389FallMs goes beside a grounded Up player who is not on a dropship (the kill floor
// warp, no damage). With no such player yet (the host still rides) it keeps falling and is warped at the first
// sample after one lands. Rows are keyed by pawn id, so a map change starts them over.
// Card 554: a player who jumps every 1-3 s reads Falling at most once-a-second samples, so the time test alone warped
// the playtest host 8 times at normal height. The warp also needs the pawn kC389DropUu below the z of its first
// Falling sample: a real fall off the map drops that far in well under kC389FallMs, a run of jumps never does.
constexpr ULONGLONG kC389FallMs = 6000;
constexpr double kC389DropUu = 5000.0;
constexpr ULONGLONG kC389StepMs = 1000;
struct C389Fall {
    int32_t id;        // the slot's pawn at the last sample (0: none)
    ULONGLONG since;   // first Falling sample of the run (0: not falling)
    double z0;         // card 554: the pawn's z at that first sample
    bool waited;       // the one "no grounded Up player" row of this fall is out
};
C389Fall g_c389[kC43Max]{};
ULONGLONG g_c389_next_ms = 0;

bool c389_on_dropship(const void* pawn) {
    const void* const parent = g_get_attach_parent_actor(pawn);
    return parent != nullptr && text_contains(class_name(parent).text, "Dropship");
}

// The first grounded Up player pawn that is not `pawn` and not on a dropship.
void* c389_target(const C43Roster& r, const void* pawn) {
    for (int32_t s = 0; s < kC43Max; ++s) {
        void* const t = r.p[s].pawn;
        if (t != nullptr && t != pawn && is_a(t, g_player_character_class()) && !c43_down_state(c43_life(t)) &&
            c46_grounded(t) && !c389_on_dropship(t)) {
            return t;
        }
    }
    return nullptr;
}

void c389_step(void* world, ULONGLONG now) {
    if (g_client_session || now < g_c389_next_ms || world == nullptr) {
        return;
    }
    g_c389_next_ms = now + kC389StepMs;
    const C43Roster r = c43_roster(world);
    for (int32_t s = 0; s < kC43Max; ++s) {
        C389Fall& f = g_c389[s];
        void* const pawn = r.p[s].pawn;
        const auto* const cmc = pawn != nullptr && is_a(pawn, g_player_character_class())
                                    ? static_cast<const uint8_t*>(read_ptr(pawn, kCharacterMovement)) : nullptr;
        const bool falling = cmc != nullptr && cmc[kCmcMovementMode] == kMoveFalling &&
                             !c43_down_state(c43_life(pawn)) && !c389_on_dropship(pawn);
        const int32_t id = falling ? object_index(pawn) : 0;
        // Card 395: under the card 320 load hold the world is paused (z stays), so the fall count starts over.
        if (id == 0 || id != f.id || c320_holding()) {
            f = C389Fall{id, id != 0 ? now : 0, id != 0 ? actor_location(pawn).z : 0.0, false};
            continue;
        }
        if (now - f.since < kC389FallMs || f.z0 - actor_location(pawn).z < kC389DropUu) {
            continue;
        }
        void* const target = c389_target(r, pawn);
        if (target == nullptr) {
            if (!f.waited) {
                f.waited = true;
                log::line("WARN card389: host t=%llu %s (slot %d) Falling for %llu ms at z %.0f, no grounded Up player "
                          "off a dropship yet: warp when one lands", static_cast<unsigned long long>(now),
                          object_name(pawn).text, s, static_cast<unsigned long long>(now - f.since),
                          actor_location(pawn).z);
            }
            continue;
        }
        log::line("card389: host t=%llu %s (slot %d) Falling for %llu ms at z %.0f (from z %.0f): kill floor warp "
                  "beside %s", static_cast<unsigned long long>(now), object_name(pawn).text, s,
                  static_cast<unsigned long long>(now - f.since), actor_location(pawn).z, f.z0,
                  object_name(target).text);
        c46_warp(pawn, target, r, s, now);
        f = C389Fall{};
    }
}

// Client: false for every player pawn; the first suppression per pawn is logged.
bool c46_client_below(const void* pawn) {
    const int32_t id = object_index(pawn);
    for (int32_t i = 0; i < g_c46.quiets; ++i) {
        if (g_c46.quiet[i] == id) {
            return false;
        }
    }
    if (g_c46.quiets < kC46Rows) {
        g_c46.quiet[g_c46.quiets++] = id;
        log::line("card046: client t=%llu kill floor suppressed on %s (%s) at z %.0f, KillZ %.0f",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(pawn).text,
                  kSprintSideText[sprint_side(pawn)], actor_location(pawn).z, static_cast<double>(c46_kill_z(pawn)));
    }
    return false;
}

bool detour_is_below_kill_threshold(void* actor) {
    const bool below = g_orig_is_below_kill_threshold(actor);
    if (!below) {
        return false;
    }
    if (actor == nullptr || !is_a(actor, g_player_character_class())) {
        if (note_event_cap(g_c46.npc_lines, kC46NpcLines, "card046 npc")) {
            log::line("card046: %s kill floor on non-player %s at z %.0f: vanilla", side(), object_name(actor).text,
                      actor != nullptr ? actor_location(actor).z : 0.0);
        }
        return true;
    }
    return g_client_session ? c46_client_below(actor) : c46_host_below(actor);
}

// Client: ChanOp::Warp (card 209: v0, v1 = the target pawn's NetGUID, any player; card 461: v2..v4 = the host's spot
// as an offset from the target). An unresolved target is held.
void c46_receive_warp(void* pc, const ChanArgs& args) {
    ++g_c46.warp_count;
    g_c46.warp_offset[g_c46.warp_count % kC46Rows] = Vec3{c46_offset_uu(args.v[2]), c46_offset_uu(args.v[3]),
                                                          c46_offset_uu(args.v[4])}; // card 461
    const uint64_t guid = c41_args_guid(args.v[0], args.v[1]);
    void* const target = c41_resolve_player(pc, guid);
    if (target == nullptr && guid != 0) {
        c209_hold(C209Op::Warp, guid, g_c46.warp_count);
        return;
    }
    c46_warp_apply(pc, target, guid, g_c46.warp_count);
}

// Client: moves the own pawn to `target` (the pawn a Warp GUID resolved to) and queues the follow-up check. `warp` is
// the Warp's own number: a held Warp applied later (card 209) logs the number it arrived with, not the newest count.
void c46_warp_apply(void* pc, void* target, uint64_t guid, uint32_t warp) {
    void* own = const_cast<void*>(c23_own_pawn(pc));
    const bool landing = own == nullptr && target != nullptr && (own = c399_client_land(pc)) != nullptr;
    if (own == nullptr || target == nullptr) {
        log::line("WARN card046: client Warp %u to guid 0x%llx not applied: own pawn %p, target %p", warp,
                  static_cast<unsigned long long>(guid), static_cast<const void*>(own), target);
        return;
    }
    const Vec3 from = actor_location(own);
    const Vec3 t = actor_location(target);
    const Vec3 off = g_c46.warp_offset[warp % kC46Rows]; // card 461: the host's spot, as its offset from the target
    const ULONGLONG now = GetTickCount64();
    const int32_t impart = c461_warp_clear(own, 0, now); // card 461: own pawn, slot 0
    const bool moved = c46_move_to(own, Vec3{t.x + off.x, t.y + off.y, t.z + off.z});
    c461_warp_watch(own, 0, now, impart);
    const Vec3 to = actor_location(own);
    log::line("card046: client t=%llu Warp %u to guid 0x%llx (%s at (%.0f, %.0f, %.0f)), host offset (%+.0f, %+.0f, "
              "%+.0f): own %s (%.0f, %.0f, %.0f) -> (%.0f, %.0f, %.0f), moved %d",
              static_cast<unsigned long long>(GetTickCount64()), warp,
              static_cast<unsigned long long>(guid), object_name(target).text, t.x, t.y, t.z, off.x, off.y, off.z,
              object_name(own).text, from.x, from.y, from.z, to.x, to.y, to.z, moved ? 1 : 0);
    const void* const root = landing ? read_ptr(target, kActorRootComponent) : nullptr;
    if (root != nullptr) {
        c399_client_face(pc, component_yaw(root), "host pawn by guid");
    } else if (landing) {
        log::line("WARN card399: client landing facing not set: host pawn %s has no root", object_name(target).text);
    }
    c46_add_follow(c43_client_id(own), c43_client_id(target), GetTickCount64());
}

// Client: ChanOp::WarpTo "x y z [yaw]" (card 146), the host's all-down kill floor return of this client's server copy.
// The yaw comes with a warp beside a host pawn that has no sendable NetGUID (card 389, card 399's release).
void c46_receive_warp_to(void* pc, const WireString* s) {
    ++g_c46.warp_count;
    char text[kChanTextMax + 1];
    Vec3 to{};
    void* own = const_cast<void*>(c23_own_pawn(pc));
    const bool got = chan_text(s, text, sizeof(text)) > 0;
    char* end = text;
    double yaw = 0.0;
    double* const axes[4] = {&to.x, &to.y, &to.z, &yaw}; // the yaw (card 399) is optional
    int32_t read = 0;
    for (int32_t i = 0; got && i < 4; ++i) {
        char* const start = end;
        *axes[i] = strtod(start, &end);
        read += end != start ? 1 : 0;
    }
    const bool parsed = read >= 3;
    const bool landing = own == nullptr && parsed && (own = c399_client_land(pc)) != nullptr; // the release warp
    // while this pawn still rides (card 399)
    if (own == nullptr || !parsed) {
        log::line("WARN card146: client WarpTo %u not applied: own pawn %p, parsed %d",
                  g_c46.warp_count, static_cast<const void*>(own), parsed ? 1 : 0);
        return;
    }
    const Vec3 from = actor_location(own);
    const ULONGLONG now = GetTickCount64();
    const int32_t impart = c461_warp_clear(own, 0, now); // card 461: own pawn, slot 0
    const bool moved = c46_move_to(own, to);
    c461_warp_watch(own, 0, now, impart);
    const Vec3 now_at = actor_location(own);
    log::line("card146: client t=%llu WarpTo %u (%.0f, %.0f, %.0f): own %s (%.0f, %.0f, %.0f) -> (%.0f, %.0f, %.0f), "
              "moved %d", static_cast<unsigned long long>(GetTickCount64()), g_c46.warp_count, to.x, to.y, to.z,
              object_name(own).text, from.x, from.y, from.z, now_at.x, now_at.y, now_at.z, moved ? 1 : 0);
    if (landing && read == 4) {
        c399_client_face(pc, yaw, "host yaw in WarpTo");
    } else if (landing) {
        log::line("WARN card399: client landing facing not set: WarpTo without a yaw");
    }
}

// A follow-up row's pawn: the client resolves ids as NetGUIDs (card 209, any player); the host by roster.
void* c46_resolve(const void* world, void* pc, int32_t id) {
    if (g_client_session) {
        return id > 0 ? c41_resolve_player(pc, static_cast<uint64_t>(id)) : nullptr;
    }
    const C43Roster r = c43_roster(world);
    const int32_t slot = c43_slot(r, id);
    return slot >= 0 ? r.p[slot].pawn : nullptr;
}

// The position check kC46FollowMs after each warp.
void c46_follow_step(const void* world, void* pc, ULONGLONG now) {
    for (int32_t i = g_c46.follows - 1; i >= 0; --i) {
        const C46Follow f = g_c46.follow[i];
        if (now < f.at_ms) {
            continue;
        }
        g_c46.follow[i] = g_c46.follow[--g_c46.follows];
        const void* const pawn = c46_resolve(world, pc, f.id);
        const void* const target = c46_resolve(world, pc, f.target_id);
        if (pawn == nullptr || target == nullptr) {
            log::line("WARN card046: %s warp +0.5 s check: pawn %p, target %p", side(), pawn, target);
            continue;
        }
        void* component = nullptr;
        const float health = c21_health(static_cast<const uint8_t*>(pawn), &component);
        const Vec3 a = actor_location(pawn);
        const Vec3 b = actor_location(target);
        const double gap = vec_dist(a, b);
        log::line("%scard046: %s t=%llu warp +0.5 s: %s at (%.0f, %.0f, %.0f), %s at (%.0f, %.0f, %.0f): gap %.0f uu "
                  "(limit %.0f), Health %.1f",
                  gap <= kC46NearUu ? "" : "WARN ", side(), static_cast<unsigned long long>(now),
                  object_name(pawn).text, a.x, a.y, a.z, object_name(target).text, b.x, b.y, b.z, gap, kC46NearUu,
                  static_cast<double>(health));
    }
}

constexpr ULONGLONG kC46StartMs = 20000;   // on foot to the first drop
constexpr ULONGLONG kC46GapMs = 8000;      // between tests
constexpr ULONGLONG kC46WaitMs = 10000;    // a drop's result
constexpr ULONGLONG kC46OutWaitMs = 45000; // test (c): the bleedout to Out
constexpr ULONGLONG kC46OverMs = 15000;    // game over to the done line
constexpr double kC46DropBelow = 500.0;    // uu under KillZ
constexpr double kC46JumpUu = 1000.0;      // client: a host pawn rise this large in one tick is a warp
// Client shots run 1 s behind the host's: both games write one Screenshots folder, and two shots in the same
// millisecond land in one file.
constexpr ULONGLONG kC46ClientShotLag = 1000;
constexpr const char* kC46ClientWarpShot[] = {"client view +2.5 s after Warp 1 (test a)",
                                              "client view +2.5 s after Warp 2 (test c)",
                                              "client view +2.5 s after Warp 3 (test c, Health 5 retry)",
                                              "client view +2.5 s after Warp 4 (test c, Health 1 retry)",
                                              "client view +2.5 s after a later Warp"};
// Test (c) Health tries, highest first (the senior's 20, then 5; 1 as the last before forcing Downed).
constexpr float kC46LowHealth[] = {20.0f, 5.0f, 1.0f};
constexpr uint32_t kC46LowTries = sizeof(kC46LowHealth) / sizeof(kC46LowHealth[0]);
constexpr uint32_t kC46ClientWarpShots = sizeof(kC46ClientWarpShot) / sizeof(kC46ClientWarpShot[0]);

C46Test g_c46t{}; // not reset per map: one test map per run

void c46_next(C46Stage stage, ULONGLONG at_ms) {
    g_c46t.stage = stage;
    g_c46t.at_ms = at_ms;
    g_c46t.drop_ms = 0;
}

void c46_drop(void* pawn, const char* what, ULONGLONG now) {
    const float kill_z = c46_kill_z(pawn);
    const Vec3 at = actor_location(pawn);
    const Vec3 to{at.x, at.y, static_cast<double>(kill_z) - kC46DropBelow};
    const bool moved = g_set_actor_location(pawn, &to, false, nullptr, kTeleportPhysics);
    void* component = nullptr;
    const float health = c21_health(static_cast<const uint8_t*>(pawn), &component);
    log::line("card046: host t=%llu test drop (%s): %s (%s) z %.0f -> %.0f (KillZ %.0f - %.0f), moved %d, Health %.1f, "
              "state %s",
              static_cast<unsigned long long>(now), what, object_name(pawn).text, kSprintSideText[sprint_side(pawn)],
              at.z, to.z, static_cast<double>(kill_z), kC46DropBelow, moved ? 1 : 0, static_cast<double>(health),
              kC43LifeText[static_cast<size_t>(c43_life(pawn))]);
}

// One drop: `pawn` goes below the kill height, then the stage waits until `counter` moves (or kC46WaitMs).
// True = the stage is over.
bool c46_drop_step(void* pawn, const char* what, const uint32_t& counter, ULONGLONG now) {
    if (g_c46t.drop_ms == 0) {
        g_c46t.drop_ms = now;
        g_c46t.count_at = counter;
        c46_drop(pawn, what, now);
        return false;
    }
    const bool seen = counter != g_c46t.count_at;
    if (!seen && now < g_c46t.drop_ms + kC46WaitMs) {
        return false;
    }
    log::line("%scard046: host test (%s): %s %llu ms after the drop; StartDeath calls this map %u, state %s",
              seen ? "" : "WARN ", what, seen ? "handled" : "NOTHING HAPPENED",
              static_cast<unsigned long long>(now - g_c46t.drop_ms), g_c46.start_deaths,
              kC43LifeText[static_cast<size_t>(c43_life(pawn))]);
    return true;
}

void c46_low_copy(void* copy, ULONGLONG now) {
    const float want = kC46LowHealth[g_c46t.low_try];
    c43_set_health(copy, want);
    void* component = nullptr;
    log::line("card046: host t=%llu test (c): %s Health set to %.0f, now %.1f", static_cast<unsigned long long>(now),
              object_name(copy).text, static_cast<double>(want),
              static_cast<double>(c21_health(static_cast<const uint8_t*>(copy), &component)));
    c46_next(C46Stage::DropLow, now + 1000);
}

// Test (c) result. Still Up: retry at the next lower Health; after the last try, force Downed through card 043
// so test (d) still runs with the copy Out (armour can absorb the whole warp hit).
void c46_after_low(void* world, void* copy, ULONGLONG now) {
    const float tried = kC46LowHealth[g_c46t.low_try];
    if (c43_life(copy) == C43Life::Up && g_c46t.low_try + 1 < kC46LowTries) {
        g_c46t.low_try += 1;
        log::line("card046: host test (c): copy still Up after the warp hit at Health %.0f; retry at Health %.0f",
                  static_cast<double>(tried), static_cast<double>(kC46LowHealth[g_c46t.low_try]));
        c46_next(C46Stage::LowCopy, now + 3000);
        return;
    }
    if (c43_life(copy) == C43Life::Up) {
        const C43Roster r = c43_roster(world);
        const int32_t slot = c43_slot(r, object_index(copy));
        log::line("WARN card046: host test (c): copy still Up after the warp hit at Health %.0f; forcing Downed "
                  "(slot %d) so test (d) runs", static_cast<double>(tried), slot);
        if (slot >= 0) {
            c43_enter_down(copy, r, static_cast<uint32_t>(slot), C43Life::Downed, now);
        }
    }
    c43_shot(now + 1500, "host view +1.5 s after the low copy's warp (test c)", kC44ShotAim);
    c46_next(C46Stage::WaitOut, now + 1000);
}

void c46_after_all(ULONGLONG now) {
    c43_shot(now + 5000, "host game over +5 s (test d / solo)", kC44ShotUi);
    c43_shot(now + 9000, "host game over +9 s (test d / solo)", kC44ShotUi);
    c46_next(C46Stage::Over, now + kC46OverMs);
}

void c46_test_pair(void* world, void* host, void* copy, ULONGLONG now) {
    switch (g_c46t.stage) {
    case C46Stage::WaitFoot:
        if (c30_find(world).host_on_foot && c30_find(world).copy_on_foot) {
            log::line("card046: host test: both on foot; first drop in %llu ms",
                      static_cast<unsigned long long>(kC46StartMs));
            c46_next(C46Stage::DropCopy, now + kC46StartMs);
        }
        return;
    case C46Stage::DropCopy:
        if (c46_drop_step(copy, "a: copy", g_c46.warp_count, now)) {
            c43_shot(now + 1500, "host view +1.5 s after the copy's warp (test a)", kC44ShotAim);
            c46_next(C46Stage::DropHost, now + kC46GapMs);
        }
        return;
    case C46Stage::DropHost:
        if (c46_drop_step(host, "b: host", g_c46.warp_count, now)) {
            c43_shot(now + 1500, "host view +1.5 s after its own warp (test b)", kC44ShotAim);
            c46_next(C46Stage::LowCopy, now + kC46GapMs);
        }
        return;
    case C46Stage::LowCopy:
        c46_low_copy(copy, now);
        return;
    case C46Stage::DropLow:
        if (c46_drop_step(copy, "c: low copy", g_c46.warp_count, now)) {
            c46_after_low(world, copy, now);
        }
        return;
    default:
        return;
    }
}

void c46_test_end(void* world, void* host, void* copy, ULONGLONG now) {
    if (g_c46t.stage == C46Stage::WaitOut) {
        const bool out = c43_life(copy) == C43Life::Out;
        if (out || now >= g_c46t.at_ms + kC46OutWaitMs) {
            log::line("%scard046: host test: copy %s; test (d) drop next", out ? "" : "WARN ",
                      kC43LifeText[static_cast<size_t>(c43_life(copy))]);
            c46_next(C46Stage::DropAll, now + 3000);
        }
    } else if (g_c46t.stage == C46Stage::DropAll) {
        if (c46_drop_step(host, copy != nullptr ? "d: host, copy Out" : "solo: host", g_c46.vanilla, now)) {
            c46_after_all(now);
        }
    } else if (g_c46t.stage == C46Stage::Over) {
        log::line("card046: host test done: warps %u, vanilla %u, StartDeath %u, all down %d",
                  g_c46.warp_count, g_c46.vanilla, g_c46.start_deaths, g_c43.all_down ? 1 : 0);
        g_c46t.stage = C46Stage::Done;
    } else if (copy == nullptr && g_c46t.stage == C46Stage::WaitFoot && c30_find(world).host_on_foot) {
        log::line("card046: host test: solo, on foot; drop in %llu ms", static_cast<unsigned long long>(kC46StartMs));
        c46_next(C46Stage::DropAll, now + kC46StartMs);
    }
}

void c46_test_host(void* world, ULONGLONG now) {
    if (g_c46t.stage == C46Stage::Done || now < g_c46t.at_ms) {
        return;
    }
    const C43Roster r = c43_roster(world);
    void* const host = r.p[0].pawn;
    void* const copy = c44_other_pawn(world);
    if (host == nullptr || (copy == nullptr && r.players != 1)) {
        return;
    }
    if (copy != nullptr && g_c46t.stage < C46Stage::WaitOut) {
        c46_test_pair(world, host, copy, now);
    } else {
        c46_test_end(world, host, copy, now);
    }
}

// Client: a shot after each Warp frame and after the host's pawn rises (its own warp), then the death screen.
void c46_test_client(void* world, ULONGLONG now) {
    const void* const host = c43_client_host_pawn(world);
    const double z = host != nullptr ? actor_location(host).z : 0.0;
    if (host != nullptr && g_c46t.host_z_set && z > g_c46t.host_z + kC46JumpUu) {
        log::line("card046: client t=%llu host pawn %s rose z %.0f -> %.0f in one tick (its warp)",
                  static_cast<unsigned long long>(now), object_name(host).text, g_c46t.host_z, z);
        c43_shot(now + 1500 + kC46ClientShotLag, "client view +2.5 s after the host's warp (test b)", kC44ShotAim);
    }
    g_c46t.host_z = z;
    g_c46t.host_z_set = host != nullptr;
    if (g_c46.warp_count > g_c46t.warps_seen) {
        const uint32_t k = g_c46t.warps_seen < kC46ClientWarpShots ? g_c46t.warps_seen : kC46ClientWarpShots - 1;
        g_c46t.warps_seen = g_c46.warp_count;
        c43_shot(now + 1500 + kC46ClientShotLag, kC46ClientWarpShot[k], kC44ShotAim);
    }
    const int32_t h = c43_find_pawn(host);
    if (!g_c46t.over_shot && h >= 0 && g_c43.e[h].state == C43Life::Dead) {
        g_c46t.over_shot = true;
        c43_shot(now + 5000 + kC46ClientShotLag, "client death screen +6 s (test d)", kC44ShotUi);
        c43_shot(now + 9000 + kC46ClientShotLag, "client death screen +10 s (test d)", kC44ShotUi);
        g_c46t.done_ms = now + kC46OverMs;
    }
    if (g_c46t.done_ms != 0 && now >= g_c46t.done_ms) {
        g_c46t.done_ms = 0;
        g_c46t.stage = C46Stage::Done;
        log::line("card046: client test done: Warp frames %u, StartDeath %u", g_c46.warp_count, g_c46.start_deaths);
    }
}

constexpr uint32_t kC65ActivateLogs = 96; // Activate log lines per map: every target's components at load, then pops
constexpr int32_t kC65NameMax = 64;        // chars per relayed object name (sscanf widths below)
constexpr size_t kC65Reactions = 0xC0;     // TArray<UADLevelReactionBase*> Reactions {data @0xC0, int32 num @0xC8}
constexpr size_t kC65ReactionCount = 0xC8; // (Trigger 0x04A41A60 walks 8-byte entries and calls each live one)
// Card 227: the trigger log and the presence BeginPlay dump share g_c65_logs (reset per
// map in sprint.cpp). FED_C1M1 has 23 BP_PlayerPresence_C and 10 BP_TutorialPromptTrigger_C, each firing on both
// machines, so the note_event default (16) ran out before the capture-the-flag arena.
constexpr uint32_t kC65TriggerLogs = 240;
constexpr int32_t kC227ReactionsShown = 6;   // reaction classes printed per trigger component
constexpr size_t kC227ReactionText = 200;    // chars of that list
// ASTPlayerPresence (OnTriggerEntered 0x050FA160 / OnTriggerExited 0x050FA1A0): broadcast the level script's
// delegate, call the LT component's Trigger (vtable +0x4E0), then clear the enabled byte when the once flag is set.
constexpr size_t kPresenceEnterScript = 0x2C8; // multicast delegate (TArray<FScriptDelegate>) broadcast on enter
constexpr size_t kPresenceExitScript = 0x2D8;  // the same, on exit
constexpr size_t kPresenceLtEnter = 0x2E8;     // UADLevelTriggerComponent* LTOnEnter
constexpr size_t kPresenceLtExit = 0x2F0;      // UADLevelTriggerComponent* LTOnExit
constexpr size_t kPresenceOnceEnter = 0x308;   // bool: enter disables further trigger events
constexpr size_t kPresenceOnceExit = 0x309;    // bool: exit disables further trigger events
constexpr size_t kPresenceEnabled = 0x30A;     // bool: trigger events enabled (SetTriggerEventsEnabled 0x05103780)
constexpr size_t kScriptDelegateFunction = 0x8; // FScriptDelegate {FWeakObjectPtr Object, FName FunctionName}
// The presence edge lines (the c227 scenario's wait targets) have their own counter, so trigger and dump lines
// cannot use up the lines a scenario waits on. c227_map_end resets it per map.
static uint32_t g_c227_edge_logs = 0;
static uint32_t g_c288_client_logs = 0; // card 288: client "door reactions left to the host" lines, reset per map
// Card 227 step 3, the gate: a presence whose LTOnEnter reactions hold one of these classes fires only when every
// living player overlaps it (condition table, matched by class name at BeginPlay).
constexpr const char* kC227GateClasses[] = {"LR_Door_Close_C", "LR_Door_Open_C", "LR_Door_Toggle_C", "LR_SpawnAI_C"};
constexpr int32_t kC227GatedMax = 32;      // gated presences per map (FED_C1M1: 1)
constexpr int32_t kC227ReactionsRead = 64; // reaction entries read per presence
constexpr uint32_t kC227GateLogs = 64;     // "gated", "held" and "fired after hold" lines per map
constexpr int32_t kC227PlayersMax = 2 * kC43Max; // the roster's pawns plus, on the client, every other player pawn
constexpr ULONGLONG kC227RecheckMs = 250; // card 228 item 1: re-check period while some entry is held
struct C227Gated {
    uint64_t name;  // the presence's FName (raw 8 bytes at kObjectName): unique per level actor
    uint64_t weak;  // card 228: FWeakObjectPtr to the presence, resolved and is_a checked at the re-check
    uint32_t holds; // entries held so far
    bool fired;     // the original OnTriggerEntered ran once: both edges pass through from then on
    bool closure;   // card 300: a one-way closure presence (kC300Closures): host decides over the group, client
                    // runs its edge only from the host's 'F' row
    uint8_t site;   // card 492: the closure's site (C300Closure::site), 0 when not a closure
};
struct C227Gate {
    C227Gated e[kC227GatedMax];
    int32_t count;
    uint32_t logs;
    ULONGLONG recheck_ms; // card 228: the next re-check, 0 = none yet this map
    bool full_warned;
};
static C227Gate g_c227{};
C227OverlapFn g_c227_is_overlapping = nullptr;
StaticClassFn g_c227_presence_class = nullptr; // ASTPlayerPresence
C227BeginPlayFn g_orig_c227_begin_play = nullptr;
C227EdgeFn g_orig_c227_entered = nullptr;
C227EdgeFn g_orig_c227_exited = nullptr;
StaticClassFn g_c65_trigger_class = nullptr; // UADLevelTriggerComponent
C65TriggerFn g_orig_c65_trigger = nullptr;
C65TriggerFn g_orig_c65_stateful_trigger = nullptr;
ExecThunkFn g_orig_c65_exec_trigger = nullptr;
ExecThunkFn g_orig_c65_exec_activate = nullptr;
C65ActivateFn g_orig_c65_activate = nullptr;
const void* g_c65_node = nullptr;   // the UFunction running execTrigger, during that call only
const void* g_c65_object = nullptr; // that frame's object (the Blueprint instance), during that call only

// Card 227: "<num>" or "<num>: <class>, <class>, ..." (the first kC227ReactionsShown) of a level-trigger
// component's reaction array, into out.
void c227_reaction_text(const void* component, char* out, size_t size) {
    if (component == nullptr) {
        snprintf(out, size, "no component");
        return;
    }
    const auto* const data = static_cast<void* const*>(read_ptr(component, kC65Reactions));
    const int32_t num = read_i32(component, kC65ReactionCount);
    int used = snprintf(out, size, "%d", num);
    for (int32_t i = 0; data != nullptr && i < num && i < kC227ReactionsShown; ++i) {
        if (used < 0 || static_cast<size_t>(used) >= size) {
            return;
        }
        used += snprintf(out + used, size - static_cast<size_t>(used), "%s%s", i == 0 ? ": " : ", ",
                         class_name(data[i]).text);
    }
}

void c65_log_trigger(const char* which, const void* component, unsigned long long caller) {
    if (!note_event_cap(g_c65_logs, kC65TriggerLogs, "c65 level trigger")) {
        return;
    }
    const void* const owner = read_ptr(component, kComponentOwner);
    const void* const node = g_c65_node;
    char reactions[kC227ReactionText];
    c227_reaction_text(component, reactions, sizeof(reactions));
    log::line("c65: %s %s::Trigger %s (%s), owner %s (%s), reactions %d, caller exe+0x%llX, blueprint %s.%s, "
              "self %s; card227: owner name %s, reaction classes %s",
              side_text(), which, object_name(component).text, class_name(component).text, object_name(owner).text,
              class_name(owner).text, read_i32(component, kC65ReactionCount), caller,
              object_name(node != nullptr ? read_ptr(node, kObjectOuter) : nullptr).text, object_name(node).text,
              object_name(g_c65_object).text,
              owner != nullptr ? prop_name(static_cast<const uint8_t*>(owner) + kObjectName, 8).text : "null",
              reactions);
}

// Card 227: "<num>" or "<num> <first bound function>" of a presence's level-script delegate at `off`.
void c227_script_text(const void* presence, size_t off, char* out, size_t size) {
    const auto* const data = static_cast<const uint8_t*>(read_ptr(presence, off));
    const int32_t num = read_i32(presence, off + kArrayNum);
    if (data == nullptr || num <= 0) {
        snprintf(out, size, "%d", num);
        return;
    }
    snprintf(out, size, "%d %s", num, prop_name(data + kScriptDelegateFunction, 8).text);
}

// Card 227 (UBWCOOP_C65_LOG): one line per ASTPlayerPresence at its BeginPlay, so the event/marker list does not
// depend on walking into every volume.
void c227_dump_presence(const void* presence) {
    if (!note_event_cap(g_c65_logs, kC65TriggerLogs, "c65 level trigger")) {
        return;
    }
    char enter[kC227ReactionText];
    char exit_r[kC227ReactionText];
    char enter_s[kNameChars + 16];
    char exit_s[kNameChars + 16];
    c227_reaction_text(read_ptr(presence, kPresenceLtEnter), enter, sizeof(enter));
    c227_reaction_text(read_ptr(presence, kPresenceLtExit), exit_r, sizeof(exit_r));
    c227_script_text(presence, kPresenceEnterScript, enter_s, sizeof(enter_s));
    c227_script_text(presence, kPresenceExitScript, exit_s, sizeof(exit_s));
    const auto* const p = static_cast<const uint8_t*>(presence);
    const Vec3 at = actor_location(presence);
    log::line("card227: %s presence %s at %.0f %.0f %.0f BeginPlay: LTOnEnter reactions %s; script binds %s; "
              "LTOnExit reactions %s; script binds %s; once enter %u exit %u, triggers enabled %u",
              side_text(), prop_name(p + kObjectName, 8).text, at.x, at.y, at.z, enter, enter_s, exit_r, exit_s,
              p[kPresenceOnceEnter], p[kPresenceOnceExit], p[kPresenceEnabled]);
}

uint64_t c227_name_key(const void* actor) {
    uint64_t key = 0;
    memcpy(&key, static_cast<const uint8_t*>(actor) + kObjectName, sizeof(key));
    return key;
}

int32_t c227_find(const void* presence) {
    const uint64_t key = c227_name_key(presence);
    for (int32_t i = 0; i < g_c227.count && i < kC227GatedMax; ++i) {
        if (g_c227.e[i].name == key) {
            return i;
        }
    }
    return -1;
}

// The first kC227GateClasses entry among a presence's LTOnEnter reaction classes, or nullptr.
const char* c227_gate_class(const void* presence) {
    const void* const component = read_ptr(presence, kPresenceLtEnter);
    if (component == nullptr) {
        return nullptr;
    }
    const auto* const data = static_cast<void* const*>(read_ptr(component, kC65Reactions));
    const int32_t num = read_i32(component, kC65ReactionCount);
    for (int32_t i = 0; data != nullptr && i < num && i < kC227ReactionsRead; ++i) {
        if (data[i] == nullptr) {
            continue;
        }
        const auto cls = class_name(data[i]);
        for (const char* const gate : kC227GateClasses) {
            if (strcmp(cls.text, gate) == 0) {
                return gate;
            }
        }
    }
    return nullptr;
}

// Card 300: one-way closures. A script-bound presence (no door or spawn reaction, so not in kC227GateClasses) whose
// entry closes the way behind the player. The presences of one site (card 492: C300Closure::site) are one group: a
// player is past the closure when it overlaps any presence of that site, and the close spends that site only. Matched at BeginPlay by actor name and the name of the world its level
// belongs to (the streamed level's package). FED_C2M1: after EthanFound, any pawn in C_78/79/81 runs
// HiveEntrance_C_1 `LD: Destroy` (the cave collapses) and BP_HordeZone_C_3 Begin KillEmAll (the arena wall).
struct C300Closure {
    const char* level;
    const char* presence;
    uint8_t site; // card 492: one closure place; 1 and up, unique across the table
};
constexpr C300Closure kC300Closures[] = {
    {"FED_C2M1_Scripting", "BP_PlayerPresence_C_78", 1},
    {"FED_C2M1_Scripting", "BP_PlayerPresence_C_79", 1},
    {"FED_C2M1_Scripting", "BP_PlayerPresence_C_81", 1},
    {"FED_C3M2_Scripting", "BP_PlayerPresence_C_52", 2},  // card 492: Setup Objective 3_02 closes Gigantic C_8/C_7/C_10
    {"FED_C4M2_Scripting", "BP_PlayerPresence_C_73", 3},  // card 492: @9746 closes Plaza Medium C_1..C_4
    {"FED_C4M1_Scripting", "BP_PlayerPresence_C_17", 4},  // card 492: metro, closes Large C_3/C_2, Medium C_3/C_4
    {"FED_C4M1_Scripting", "BP_PlayerPresence_C_255", 5}, // card 492: closes bridge Large C_0
    {"FED_C4M1_Scripting", "BP_PlayerPresence_C_70", 6},  // card 492: EndLevel (class E): waits for every living player
};
constexpr uint32_t kC300Logs = 32; // card300 "waiting" and client "left to the host" lines per map
static uint32_t g_c300_logs = 0;

// The presence's actor name and the name of the world its level belongs to (the streamed level's package); false
// when either is unreadable.
bool c300_presence_names(const void* presence, char (&name)[kAssetPathChars], char (&level)[kAssetPathChars]) {
    const void* const outer = read_ptr(presence, kObjectOuter);
    const void* const world = outer != nullptr ? read_ptr(outer, kObjectOuter) : nullptr;
    return c50_name(presence, name, static_cast<int32_t>(sizeof(name))) > 0 &&
           c50_name(world, level, static_cast<int32_t>(sizeof(level))) > 0;
}

// The closure site of `presence`, 0 when it is not in kC300Closures.
uint8_t c300_closure_site(const void* presence) {
    char name[kAssetPathChars];
    char level[kAssetPathChars];
    if (!c300_presence_names(presence, name, level)) {
        return 0;
    }
    for (const C300Closure& c : kC300Closures) {
        if (strcmp(name, c.presence) == 0 && strcmp(level, c.level) == 0) {
            return c.site;
        }
    }
    return 0;
}

// Card 492 fix 6 (FED_C4M1 stadium): an objective whose OnCompleted callback closes doors inside the host's
// Complete, CompleteSub or Increment call. Stadium_SO1's (the count of 4 panel switches Stadium_SO1_A..D, @6135) is
// `Add Stadium_SO2` @6586: it adds Stadium_SO2, turns FallZones C_6/C_7 on and closes Medium C_2, C_1 and Gigantic
// C_1 (@6976-7048) in the same call. Host, before the original of any completion or Increment call on that objective
// (c491_enter, see c492_objective_trigger for the match): when a living player is inside the row's area
// presence, card 288's trigger is set from that pawn, so the closes are held until every living player is on the
// inner side. No player inside: no trigger (the closes run as unmodded). Not held: the fall zones going on.
struct C492ObjectiveTrigger {
    const char* level;
    const wchar_t* objective;
    const char* presence; // the area volume: inside = every living player must be in it before the closes
};
constexpr C492ObjectiveTrigger kC492ObjectiveTriggers[] = {
    {"FED_C4M1_Scripting", L"Stadium_SO1", "BP_PlayerPresence_C_5"}, // enter @5729 sets inStadium, exit @5937 clears
};
constexpr int32_t kC492Triggers = static_cast<int32_t>(sizeof(kC492ObjectiveTriggers) / sizeof(kC492ObjectiveTriggers[0]));
constexpr uint32_t kC492TriggerRows = 24; // per map
struct C492TriggerState {
    C31Weak presence[kC492Triggers]; // seen at BeginPlay, per row
    uint64_t name_cache[kC492Triggers];
    uint32_t rows;
};
static C492TriggerState g_c492_obj{}; // host and client (BeginPlay); used on the host only, game thread

// BeginPlay: a presence named by a kC492ObjectiveTriggers row is kept for that row.
void c492_note_presence(const void* presence, const char* name, const char* level) {
    for (int32_t i = 0; i < kC492Triggers; ++i) {
        const C492ObjectiveTrigger& t = kC492ObjectiveTriggers[i];
        if (strcmp(name, t.presence) == 0 && strcmp(level, t.level) == 0) {
            c31_weak_set(g_c492_obj.presence[i], const_cast<void*>(presence));
        }
    }
}

// Card 300: the pawn overlaps some presence of closure site `site` (it is past that closure). Card 492: only the
// same site counts; a presence of another closure on the map is not past this one.
bool c300_past(const void* pawn, uint8_t site) {
    for (int32_t i = 0; site != 0 && i < g_c227.count && i < kC227GatedMax && g_weak_get != nullptr; ++i) {
        C227Gated& e = g_c227.e[i];
        void* const p = e.closure && e.site == site && e.weak != 0 ? g_weak_get(&e.weak) : nullptr;
        if (p != nullptr && g_c227_is_overlapping(p, pawn)) {
            return true;
        }
    }
    return false;
}

// BeginPlay: a presence with a door or spawn reaction, or a card 300 closure, joins the gated table (once per name).
void c227_add_gated(const void* presence) {
    const uint8_t site = c300_closure_site(presence);
    const bool closure = site != 0;
    const char* const gate = closure ? "none, card 300 closure" : c227_gate_class(presence);
    if (gate == nullptr || c227_find(presence) >= 0) {
        return;
    }
    const auto name = prop_name(static_cast<const uint8_t*>(presence) + kObjectName, 8);
    if (g_c227.count >= kC227GatedMax) {
        if (!g_c227.full_warned) {
            g_c227.full_warned = true;
            log::line("WARN card227: %s presence %s not gated: table full (%d of %d)", side_text(), name.text,
                      g_c227.count, kC227GatedMax);
        }
        return;
    }
    C227Gated& e = g_c227.e[g_c227.count++];
    e = C227Gated{c227_name_key(presence), 0, 0, false, closure, site};
    if (g_weak_assign != nullptr) {
        void* const obj = const_cast<void*>(presence);
        g_weak_assign(&e.weak, &obj); // card 228: the re-check's reference (fails once the level unloads)
    }
    if (note_event_cap(g_c227.logs, kC227GateLogs, "card227 gate")) {
        log::line("card227: %s presence %s gated (LTOnEnter %s), table %d of %d", side_text(), name.text, gate,
                  g_c227.count, kC227GatedMax);
    }
}

// Card 220: a BP_TutorialPromptTrigger_C prompt is per player. ASTPlayerPresence::OnBeginOverlap / OnEndOverlap
// (0x50F6880, 0x50F7200) have no player filter and every machine holds every player's pawn, so each machine's copy
// fires for any player's pawn and a once trigger is spent for the others. Each machine passes only its own pawn's
// edges, as in single player; its enabled byte (+0x30A) is then per-player state, left out of the card 132 digest and
// 'X' rows (c132_actor_ext). The class is matched by name at BeginPlay, once per map.
constexpr const char* kC220PromptClass = "BP_TutorialPromptTrigger_C";
constexpr uint32_t kC220Logs = 128; // edge lines per map (b190: 4 widget rows per check)
static const void* g_c220_class = nullptr; // this map's BP_TutorialPromptTrigger_C UClass, null until one begins play
static uint32_t g_c220_logs = 0;

bool c220_per_player(const void* actor) {
    return g_c220_class != nullptr && actor != nullptr && read_ptr(actor, kObjectClass) == g_c220_class;
}

// Card 220 (b189): the trigger Blueprint's gate variables, logged at each edge.
constexpr const char* kC220Vars[] = {"Enabled?", "Condition", "Displaying", "ThisBox?", "PlayerInside?"};
constexpr size_t kC220VarChars = 48;

// "<name>=<value>" for one Blueprint variable of the trigger: bool, integer, float or object name; "<name>=absent"
// when the class has no such property, "<name>=<kind>" for any other kind.
void c220_var_text(const void* presence, const char* name, char* out, size_t cap) {
    const uint8_t* const f = prop_find_prop(read_ptr(presence, kObjectClass), name);
    if (f == nullptr) {
        snprintf(out, cap, "%s=absent", name);
        return;
    }
    const uint8_t* const addr = static_cast<const uint8_t*>(presence) + read_i32(f, kPropertyOffset);
    const int32_t size = read_i32(f, kPropertyElementSize);
    ObjectName kind{};
    const PropKind k = prop_kind(f, kind);
    float fl = 0.0f;
    double d = 0.0;
    int64_t i = 0;
    switch (k) {
    case PropKind::Bool:
        snprintf(out, cap, "%s=%d", name, prop_bool_ok(f) ? ((addr[f[kBoolByteOffset]] & f[kBoolFieldMask]) != 0) : -1);
        return;
    case PropKind::Object: snprintf(out, cap, "%s=%s", name, object_name(read_ptr(addr, 0)).text); return;
    case PropKind::Float: memcpy(&fl, addr, sizeof(fl)); snprintf(out, cap, "%s=%.2f", name, fl); return;
    case PropKind::Double: memcpy(&d, addr, sizeof(d)); snprintf(out, cap, "%s=%.2f", name, d); return;
    case PropKind::SInt:
    case PropKind::UInt:
        memcpy(&i, addr, size > 0 && size <= 8 ? static_cast<size_t>(size) : 1);
        snprintf(out, cap, "%s=%lld", name, static_cast<long long>(i));
        return;
    default: snprintf(out, cap, "%s=<%s>", name, kind.text); return;
    }
}

// Card 220 (b189): the trigger's gate variables, its colliding bounds and where the pawn stood at the edge.
void c220_edge_state(const char* edge, const void* presence, const void* actor) {
    char vars[5 * kC220VarChars] = "";
    size_t used = 0;
    for (const char* name : kC220Vars) {
        char one[kC220VarChars];
        c220_var_text(presence, name, one, sizeof(one));
        const int w = snprintf(vars + used, sizeof(vars) - used, "%s%s", used > 0 ? ", " : "", one);
        used = w > 0 ? used + static_cast<size_t>(w) : used;
        used = used < sizeof(vars) ? used : sizeof(vars) - 1;
    }
    Vec3 centre{}, extent{};
    if (g_actor_bounds != nullptr) {
        g_actor_bounds(presence, true, &centre, &extent, false);
    }
    const Vec3 at = actor != nullptr ? actor_location(actor) : Vec3{};
    log::line("card220: %s prompt trigger %s %s state: %s; colliding bounds centre (%.0f, %.0f, %.0f) extent (%.0f, "
              "%.0f, %.0f); pawn at (%.0f, %.0f, %.0f)", side_text(),
              prop_name(static_cast<const uint8_t*>(presence) + kObjectName, 8).text, edge, vars, centre.x, centre.y,
              centre.z, extent.x, extent.y, extent.z, at.x, at.y, at.z);
}

// Card 220: true when the edge may run the original (not a prompt trigger, or this machine's own pawn).
bool c220_pass(const char* edge, const void* presence, const void* actor) {
    if (!c220_per_player(presence)) {
        return true;
    }
    const bool own = actor != nullptr && sprint_side(actor) == (g_client_session ? kSideClientOwn : kSideHostOwn);
    if (note_event_cap(g_c220_logs, kC220Logs, "card220 prompt edge")) {
        log::line("card220: %s prompt trigger %s %s by %s: %s", side_text(),
                  prop_name(static_cast<const uint8_t*>(presence) + kObjectName, 8).text, edge,
                  actor != nullptr ? prop_name(static_cast<const uint8_t*>(actor) + kObjectName, 8).text : "null",
                  own ? "own pawn, passed" : "other player, skipped");
        c220_edge_state(edge, presence, actor);
    }
    return own;
}

// Card 220 (b187, log only): what the HUD's prompt line widget holds after an own pawn's enter edge, and again about
// 1 s later. The trigger's Turn On Prompt runs GetAllWidgetsOfClass(WB_Tutorial_Prompt_Line_C) and ShowTutorialPrompt
// (Final Prompt Message) on each; the widget adds the text to its list, shows the newest and sets itself visible.
// Rows tell the cases apart: 0 widgets (the HUD has none), widgets with an empty trigger text, or a visible widget
// with text set (the shot is the problem).
constexpr const wchar_t* kC220WidgetPath = L"/Game/Blueprints/UI_Aloft/Gameplay_HUD/WB_Tutorial_Prompt_Line.WB_Tutorial_Prompt_Line_C";
// b190: 4, not 2. b189 found 3 objects of the class and logged the first 2, both "list 0, Collapsed" while the
// trigger's Final Prompt Message held the _9 text. Turn On Prompt sets that text (SetText) inside its loop over
// GetAllWidgetsOfClass, so the loop ran and ShowTutorialPrompt reached a widget: the 2 logged were templates (not in
// the world) and the live one went unlogged. Each row now carries the widget's object flags and outer.
constexpr int32_t kC220Widgets = 4;     // widgets logged per check
constexpr int32_t kC220Pending = 4;     // rechecks waiting at once (_0 and _7 fire together)
constexpr ULONGLONG kC220RecheckMs = 1000;
constexpr size_t kC220TextChars = 160;
constexpr const char* kC220VisNames[] = {"Visible", "Collapsed", "Hidden", "HitTestInvisible", "SelfHitTestInvisible"};
struct C220Pending { const void* presence; ULONGLONG due; };
static C220Pending g_c220_pending[kC220Pending]{};
bool g_c220_due = false;

// The first ArrayProperty the widget's own Blueprint class declares (the prompt list), or null.
static const uint8_t* c220_list_prop(const void* cls) {
    const uint8_t* f = static_cast<const uint8_t*>(read_ptr(cls, kStructChildProperties));
    for (int32_t i = 0; f != nullptr && i < kPropMaxFields; ++i) {
        if (prop_kind_is(f, "ArrayProperty")) {
            return f;
        }
        f = static_cast<const uint8_t*>(read_ptr(f, kFieldNext));
    }
    return nullptr;
}

// "list <num> (<name>), newest '<text>'" for the widget's prompt list; the newest only when its elements are FText.
static void c220_list_text(const void* widget, char* out, size_t cap) {
    const uint8_t* const prop = c220_list_prop(read_ptr(widget, kObjectClass));
    if (prop == nullptr) {
        snprintf(out, cap, "list - (no array property), newest ''");
        return;
    }
    const uint8_t* const arr = static_cast<const uint8_t*>(widget) + read_i32(prop, kPropertyOffset);
    const int32_t num = read_i32(arr, kArrayNum);
    const auto* const inner = static_cast<const uint8_t*>(read_ptr(prop, kArrayPropInner));
    char newest[kC220TextChars] = "";
    const auto* const data = static_cast<const uint8_t*>(read_ptr(arr, kArrayData));
    const bool text = inner != nullptr && prop_kind_is(inner, "TextProperty");
    if (text && data != nullptr && num > 0 && num <= 1024) {
        c114_ftext(data + static_cast<size_t>(num - 1) * static_cast<size_t>(read_i32(inner, kPropertyElementSize)),
                   newest, sizeof(newest));
    }
    ObjectName kind{};
    if (inner != nullptr) {
        prop_kind(inner, kind);
    }
    snprintf(out, cap, "list %d (%s of %s), newest '%s'", num, fname_text(prop + kFieldName).text,
             inner != nullptr ? kind.text : "-", text ? newest : "<not text>");
}

// One row per prompt line widget (at most kC220Widgets), with the trigger's Final Prompt Message.
static void c220_widget_rows(const void* presence, const char* when) {
    static const void* cls = nullptr;
    if (cls == nullptr) {
        cls = find_class(kC220WidgetPath);
    }
    const void* widgets[kC220Widgets]{};
    const int32_t n = cls != nullptr ? objects_of_class(cls, widgets, kC220Widgets) : 0;
    char message[kC220TextChars] = "-";
    const uint8_t* mp = prop_find_prop(read_ptr(presence, kObjectClass), "Final Prompt Message");
    if (mp == nullptr) {
        mp = prop_find_prop(read_ptr(presence, kObjectClass), "FinalPromptMessage");
    }
    if (mp != nullptr && prop_kind_is(mp, "TextProperty")) {
        c114_ftext(static_cast<const uint8_t*>(presence) + read_i32(mp, kPropertyOffset), message, sizeof(message));
    }
    const ObjectName trigger_name = prop_name(static_cast<const uint8_t*>(presence) + kObjectName, 8);
    const char* const trigger = trigger_name.text;
    // b188: Turn On Prompt runs from the trigger's OnTriggerEnter_Event, which the Blueprint binds to the enter
    // script delegate (+0x2C8, broadcast by OnTriggerEntered 0x50FA160) in its STSave FullyLoaded handler. Bindings 0
    // here means that handler never ran, so no prompt is shown and Final Prompt Message keeps its class default.
    char mk[kC220TextChars] = "-";
    const uint8_t* const mkp = prop_find_prop(read_ptr(presence, kObjectClass), "Prompt Message M&K");
    if (mkp != nullptr && prop_kind_is(mkp, "TextProperty")) {
        c114_ftext(static_cast<const uint8_t*>(presence) + read_i32(mkp, kPropertyOffset), mk, sizeof(mk));
    }
    if (note_event_cap(g_c220_logs, kC220Logs, "card220 prompt edge")) {
        log::line("card220: %s prompt trigger %s (%s): OnTriggerEnter script bindings %d, Prompt Message M&K '%s'",
                  side_text(), trigger, when,
                  read_i32(static_cast<const uint8_t*>(presence) + kPresenceEnterScript, kArrayNum), mk);
    }
    if (n == 0 && note_event_cap(g_c220_logs, kC220Logs, "card220 prompt edge")) {
        log::line("card220: %s prompt widgets 0 (%s, %s): class %s, trigger %s Final Prompt Message '%s'", side_text(),
                  when, cls != nullptr ? "class loaded" : "class not loaded", "WB_Tutorial_Prompt_Line_C", trigger,
                  message);
    }
    for (int32_t i = 0; i < n && i < kC220Widgets; ++i) {
        const uint8_t* const vp = prop_find_prop(read_ptr(widgets[i], kObjectClass), "Visibility");
        const int32_t vis = vp != nullptr ? *(static_cast<const uint8_t*>(widgets[i]) + read_i32(vp, kPropertyOffset)) : -1;
        char list[2 * kC220TextChars];
        c220_list_text(widgets[i], list, sizeof(list));
        if (note_event_cap(g_c220_logs, kC220Logs, "card220 prompt edge")) {
            const void* const outer = read_ptr(widgets[i], kObjectOuter);
            log::line("card220: %s prompt widgets %d (%s): %s flags 0x%X outer %s (%s) visibility %d %s, %s; trigger %s "
                      "Final Prompt Message '%s'", side_text(), n, when, object_name(widgets[i]).text,
                      static_cast<unsigned>(read_i32(widgets[i], kObjectFlags)), object_name(outer).text,
                      class_name(outer).text, vis, vis >= 0 && vis < 5 ? kC220VisNames[vis] : "?", list, trigger,
                      message);
        }
    }
}

void c220_after_enter(const void* presence) {
    if (!c220_per_player(presence)) {
        return;
    }
    c220_widget_rows(presence, "at the enter");
    for (C220Pending& p : g_c220_pending) {
        if (p.presence == nullptr) {
            p = C220Pending{presence, GetTickCount64() + kC220RecheckMs};
            g_c220_due = true;
            return;
        }
    }
}

void c220_step() {
    const ULONGLONG now = GetTickCount64();
    bool left = false;
    for (C220Pending& p : g_c220_pending) {
        if (p.presence == nullptr) {
            continue;
        }
        if (now < p.due) {
            left = true;
            continue;
        }
        if (object_listed(p.presence)) {
            c220_widget_rows(p.presence, "1 s after the enter");
        }
        p = C220Pending{};
    }
    g_c220_due = left;
}

static uint32_t g_c245_rows = 0; // card 245 (b192): player-body overlap rows this map (detour_c245_overlap)
static uint32_t g_c301_rows = 0; // card 301 (b212): enable rows this map (detour_c301_exec_enable)
static uint32_t g_c244_presence_rows = 0; // card 244 (b212): presence BeginPlay rows this map (host)

void c227_map_end() {
    if (g_c227.count > 0) {
        log::line("card227: %s map end: gated presences peak %d of %d", side_text(), g_c227.count, kC227GatedMax);
    }
    g_c227 = C227Gate{};
    g_c227_edge_logs = 0;
    g_c245_rows = 0; // card 245 b192
    g_c301_rows = 0; // card 301 b212
    g_c244_presence_rows = 0; // card 244 b212
    g_c288_client_logs = 0;
    g_c300_logs = 0;
    for (C31Weak& w : g_c492_obj.presence) {
        w = C31Weak{}; // card 492 fix 6: seen again at the next map's BeginPlay
    }
    g_c492_obj.rows = 0;
    g_c220_class = nullptr; // card 220: the class is looked up again in the next map
    g_c220_logs = 0;
    for (C220Pending& p : g_c220_pending) {
        p = C220Pending{};
    }
    g_c220_due = false;
}

// Card 244 (b212): one host row per player presence at its BeginPlay (name, location, enabled byte), so a step whose
// presence is never seen fire (FED_C4M1 LZ_SO2's C_257) can be placed from the log, with no table in the scenario.
constexpr uint32_t kC244PresenceRows = 384; // presence rows per map (host)

void c244_presence_row(const void* presence) {
    if (g_client_session || !note_event_cap(g_c244_presence_rows, kC244PresenceRows, "card244 presence")) {
        return;
    }
    const auto* const p = static_cast<const uint8_t*>(presence);
    const Vec3 at = actor_location(presence);
    log::line("card244: host presence %s %s at %.0f %.0f %.0f: enabled %u, once enter %u", class_name(presence).text,
              prop_name(p + kObjectName, 8).text, at.x, at.y, at.z, p[kPresenceEnabled], p[kPresenceOnceEnter]);
}

// Shared with ASTAnimatedLight, ASTTeleporterVolumeBase and ASTJumpPadBase::BeginPlay (one folded body).
void detour_c227_begin_play(void* actor) {
    g_orig_c227_begin_play(actor);
    if (g_c227_presence_class != nullptr && is_a(actor, g_c227_presence_class())) {
        c227_add_gated(actor);
        char name[kAssetPathChars];
        char level[kAssetPathChars];
        if (c300_presence_names(actor, name, level)) {
            c492_note_presence(actor, name, level); // card 492 fix 6
        }
        if (g_c220_class == nullptr && strcmp(class_name(actor).text, kC220PromptClass) == 0) {
            g_c220_class = read_ptr(actor, kObjectClass); // card 220
        }
        if (g_c65_log) {
            c227_dump_presence(actor);
        }
        c244_presence_row(actor);
    }
}

struct C227Count {
    int32_t inside;          // living players the presence overlaps (or the entering actor)
    int32_t living;          // living player pawns in the world
    const void* first_inside; // card 228: one living pawn counted inside, null when none
    int32_t down_outside;    // card 300: downed, out or dead player pawns not inside
    const void* first_down_outside; // card 300: one of them, null when none
    const void* mech;        // card 300 (b180): one roster pawn that is a mech, null when none
    const void* pilot_body;  // card 300 (b180): the soldier tested for that mech, null when none was found
};

// Living players on this machine's view: the roster's pawns (the host: every PlayerController; the client: its own)
// plus, on the client, every other player pawn (card 209). A pawn that is Downed, Out or Dead (c43_is_down) is not
// counted. Card 228: a roster pawn that is a mech is the piloting player (a mech has no down entry, so it is always
// living). Card 300 (b180): the game reports the overlap for the soldier riding inside, not for the mech
// (c423b179b), so a mech's player is inside when the mech or its pilot body (c75_pilot_body, host pilot slots) is
// the entering actor or overlaps / is past. Not covered: the client's view of another player's mech (its other-pawn
// list holds soldiers only); the client's own mech has no pilot body (the slots are the host's), so it is tested as
// the mech only. Inside: the game's own overlap list (AActor::IsOverlappingActor).
C227Count c227_count(const void* presence, const void* actor) {
    C227Count c{};
    const void* const world = g_actor_get_world(presence);
    if (world == nullptr || g_player_character_class == nullptr || g_c227_is_overlapping == nullptr) {
        return c;
    }
    const void* const cls = g_player_character_class();
    const void* const mech_cls = g_mech_class != nullptr ? g_mech_class() : nullptr; // card 228
    const void* pawns[kC227PlayersMax] = {};
    const C43Roster r = c43_roster(world);
    for (int32_t s = 0; s < kC43Max; ++s) {
        pawns[s] = r.p[s].pawn;
    }
    void* others[kC43Max] = {};
    const int32_t n = g_client_session ? c43_client_other_pawns(world, others) : 0;
    for (int32_t i = 0; i < n && kC43Max + i < kC227PlayersMax; ++i) {
        pawns[kC43Max + i] = others[i]; // card 209: every other player pawn on the client
    }
    const int32_t g = c227_find(presence);
    const uint8_t site = g >= 0 && g_c227.e[g].closure ? g_c227.e[g].site : 0; // card 300: inside = past, the site
    const bool closure = site != 0;
    for (const void* const pawn : pawns) {
        if (pawn == nullptr || (!is_a(pawn, cls) && (mech_cls == nullptr || !is_a(pawn, mech_cls)))) {
            continue;
        }
        const void* const body = c75_pilot_body(pawn); // card 300 (b180): null unless a piloted mech
        const bool is_mech = mech_cls != nullptr && is_a(pawn, mech_cls);
        c.mech = is_mech && c.mech == nullptr ? pawn : c.mech;
        c.pilot_body = body != nullptr && c.pilot_body == nullptr ? body : c.pilot_body;
        const bool in = pawn == actor || (body != nullptr && body == actor) ||
                        (closure ? c300_past(pawn, site) || (body != nullptr && c300_past(body, site))
                                 : g_c227_is_overlapping(presence, pawn) ||
                                       (body != nullptr && g_c227_is_overlapping(presence, body)));
        if (c43_is_down(pawn)) {
            c.down_outside += in ? 0 : 1;
            c.first_down_outside = !in && c.first_down_outside == nullptr ? pawn : c.first_down_outside;
            continue;
        }
        ++c.living;
        c.inside += in ? 1 : 0;
        c.first_inside = in && c.first_inside == nullptr ? pawn : c.first_inside;
    }
    if (g_client_session && n == 0) {
        ++c.living; // card 288: the host's pawn copy is out of net relevancy (gone): the host is living and outside
    }
    return c;
}

// Card 492 fix 6: see kC492ObjectiveTriggers (with the card 300 closures).
// The row's objective is the count container: c244b237a closed the stadium doors inside the 4th
// CompleteSubObjective(Stadium_SO1, Stadium_SO1_<last>) (Add Stadium_SO2 at depth 1, then the closes), with no
// Complete or Increment of Stadium_SO1 of its own. So the match is name[0] (the parent of a CompleteSub, the objective
// of a Complete or Increment), on the completion and Increment kinds only (Visibility and Callout close nothing).
void c492_objective_trigger(const C49Call& c) {
    const bool kind_ok = c253_completion(c.kind) || c.kind == C49Kind::Increment;
    const uint64_t id = kind_ok ? c.name[0] : 0;
    for (int32_t i = 0; id != 0 && i < kC492Triggers; ++i) {
        if (id != fname_cached(g_c492_obj.name_cache[i], kC492ObjectiveTriggers[i].objective)) {
            continue;
        }
        const void* const presence = c31_weak_get(g_c492_obj.presence[i]);
        const C227Count n = presence != nullptr ? c227_count(presence, nullptr) : C227Count{};
        if (n.first_inside != nullptr) {
            c288_trigger_at(actor_location(presence), true); // fix 6 + card 498 (one hold group)
        }
        if (note_event_cap(g_c492_obj.rows, kC492TriggerRows, "card492 objective trigger")) {
            log::line("card492: host t=%llu objective %s %s trigger from %s: %d of %d inside, %s",
                      static_cast<unsigned long long>(GetTickCount64()), kC49KindText[static_cast<size_t>(c.kind)],
                      fname_text(&id).text, kC492ObjectiveTriggers[i].presence, n.inside, n.living,
                      presence == nullptr      ? "presence not seen this map, no trigger"
                      : n.first_inside != nullptr ? "card 288 trigger set"
                                                  : "no player inside, no trigger");
        }
    }
}

// Card 300: a closure fired on this machine (host: its gate released; client: the host's 'F' row). Every presence of
// its site is spent (the C2M1 script disables all three after the close), so no later entry or re-check runs it
// again. Card 492: other sites of the map stay open.
// The host sends its row (journalled: a joining or reconnecting client gets the close).
void c300_closed(const void* presence, const C227Count& c) {
    const int32_t g = c227_find(presence);
    const uint8_t site = g >= 0 && g_c227.e[g].closure ? g_c227.e[g].site : 0;
    for (int32_t i = 0; site != 0 && i < g_c227.count && i < kC227GatedMax; ++i) {
        g_c227.e[i].fired = g_c227.e[i].fired || (g_c227.e[i].closure && g_c227.e[i].site == site);
    }
    const auto name = prop_name(static_cast<const uint8_t*>(presence) + kObjectName, 8);
    log::line("card300: %s t=%llu closure %s closed: players past %d of %d", side_text(),
              static_cast<unsigned long long>(GetTickCount64()), name.text, c.inside, c.living);
    if (c.down_outside > 0) {
        log::line("WARN card300: %s closure %s closed with player %s not past it (%d downed players not past)",
                  side_text(), name.text, object_name(c.first_down_outside).text, c.down_outside);
    }
    if (!g_client_session) {
        c300_host_send(presence); // world_c98.cpp
    }
}

// Card 300 (client): the host's 'F' row for presence (matched by name and place). The original entry edge runs on
// the client's copy with the client's own pawn, as the host's did; the presence's own entries never run it.
void c300_client_close(void* presence, void* own_pawn) {
    const int32_t g = c227_find(presence);
    if (g >= 0 && g_c227.e[g].fired) {
        log::line("card300: client t=%llu closure %s already closed", static_cast<unsigned long long>(GetTickCount64()),
                  prop_name(static_cast<const uint8_t*>(presence) + kObjectName, 8).text);
        return;
    }
    C227Count c{};
    c.inside = 1;
    c.living = 1;
    c300_closed(presence, c);
    c227_run_edge(g_orig_c227_entered, g, presence, presence, own_pawn != nullptr ? own_pawn : presence,
                  kPresenceLtEnter);
}

// Gated presence g, not fired yet: true when the entry may run the original (every living player inside, or none
// counted: behaves as the unmodded game), false when it is held.
bool c227_release(int32_t g, const void* presence, const void* actor) {
    C227Gated& e = g_c227.e[g];
    const C227Count c = c227_count(presence, actor);
    const auto name = prop_name(static_cast<const uint8_t*>(presence) + kObjectName, 8);
    const ObjectName by = actor != nullptr ? prop_name(static_cast<const uint8_t*>(actor) + kObjectName, 8)
                                           : object_name(nullptr);
    if (c.living > 0 && c.inside < c.living) {
        ++e.holds;
        if (note_event_cap(g_c227.logs, kC227GateLogs, "card227 gate")) {
            char pilot[2 * kNameChars + 32] = ""; // card 300 (b180): which body was tested for a mech's player
            if (c.mech != nullptr) {
                snprintf(pilot, sizeof(pilot), ", mech %s pilot body %s", object_name(c.mech).text,
                         c.pilot_body != nullptr ? object_name(c.pilot_body).text : "none");
            }
            log::line("card227: %s presence %s held: %d of %d players inside (entered by %s, hold %u%s)", side_text(),
                      name.text, c.inside, c.living, by.text, e.holds, pilot);
        }
        if (e.closure && note_event_cap(g_c300_logs, kC300Logs, "card300 closure")) {
            log::line("card300: host t=%llu closure %s wanted by entry of %s: players past %d of %d, waiting",
                      static_cast<unsigned long long>(GetTickCount64()), name.text, by.text, c.inside, c.living);
        }
        return false;
    }
    e.fired = true;
    if (e.closure) {
        c300_closed(presence, c);
    }
    if (e.holds > 0 && note_event_cap(g_c227.logs, kC227GateLogs, "card227 gate")) {
        log::line("card227: %s presence %s fired after hold: %d of %d players inside, entered by %s (%u held)",
                  side_text(), name.text, c.inside, c.living, by.text, e.holds);
    }
    return true;
}

// Card 228 item 1: a held presence fires once every living player is inside without a new enter edge (a player
// downed, dead or gone). Both sides, from c43_step, every kC227RecheckMs while some entry is held. The original
// OnTriggerEntered reads only rcx (0x50FA160: the script broadcast at +0x2C8, the LTOnEnter reactions at +0x2E8,
// the once-enter disable); its caller in OnBeginOverlap (0x50F68E1) passes the presence as rcx and rdx and the
// other actor as r8, so the re-check passes the presence twice and one living pawn inside.
void c227_recheck(void* world, ULONGLONG now) {
    if (g_c227.count == 0 || now < g_c227.recheck_ms || world == nullptr || g_weak_get == nullptr ||
        g_c227_presence_class == nullptr || g_orig_c227_entered == nullptr) {
        return;
    }
    g_c227.recheck_ms = now + kC227RecheckMs;
    for (int32_t i = 0; i < g_c227.count && i < kC227GatedMax; ++i) {
        C227Gated& e = g_c227.e[i];
        void* const presence = e.holds > 0 && !e.fired && e.weak != 0 ? g_weak_get(&e.weak) : nullptr;
        if (presence == nullptr || !is_a(presence, g_c227_presence_class()) || g_actor_get_world(presence) != world) {
            continue; // nothing held, or the presence went with its level
        }
        const C227Count c = c227_count(presence, nullptr);
        if (c.living <= 0 || c.inside < c.living || c.first_inside == nullptr) {
            continue;
        }
        e.fired = true;
        if (e.closure) {
            c300_closed(presence, c);
        }
        log::line("card227: %s presence %s fired by re-check: %d of %d players inside (%u held)", side_text(),
                  prop_name(static_cast<const uint8_t*>(presence) + kObjectName, 8).text, c.inside, c.living,
                  e.holds);
        c288_presence_fired(c.first_inside); // card 288: a door close it starts is a player-trigger close
        c227_run_edge(g_orig_c227_entered, i, presence, presence, const_cast<void*>(c.first_inside),
                      kPresenceLtEnter);
    }
}

// Card 227: the edge around OnTriggerEntered / OnTriggerExited. What the level script does for this presence
// (objective, door, spawn lines) logs between the "begin" and "end" lines.
void c227_log_edge(const char* edge, const char* phase, const void* presence, const void* actor) {
    if (!note_event_cap(g_c227_edge_logs, kC65TriggerLogs, "card227 presence edge")) {
        return;
    }
    const Vec3 at = actor_location(actor);
    log::line("card227: %s t=%llu presence %s %s by %s (%s) at %.0f %.0f %.0f: %s, triggers enabled %u", side_text(),
              static_cast<unsigned long long>(GetTickCount64()),
              prop_name(static_cast<const uint8_t*>(presence) + kObjectName, 8).text, edge,
              actor != nullptr ? prop_name(static_cast<const uint8_t*>(actor) + kObjectName, 8).text : "null",
              class_name(actor).text, at.x, at.y, at.z, phase, static_cast<const uint8_t*>(presence)[kPresenceEnabled]);
}

// Card 288 (client): the door a presence's reactions move is host owned; the host's DoorOp (card 161) is the only
// source of its state on the client. The client's copy of a gated presence runs its original edge (script broadcast,
// the other reactions, the once-enter disable) with the door reactions left out of the trigger component's list for
// that call: the TArray's data and num are pointed at a filtered copy and set back after. Build b6 cause (client log
// 183002, t=255625647): the host's pawn copy was gone from the client (out of net relevancy; the next copy was made at
// 255631932), so the client's gate counted 1 of 1 inside, released with no row, and its LR_Door_Toggle_C started the
// close the host had held.
constexpr const char* kC288DoorReactions[] = {"LR_Door_Close_C", "LR_Door_Open_C", "LR_Door_Toggle_C"};
constexpr uint32_t kC288ClientLogs = 32; // "door reactions left to the host" lines per map

bool c288_door_reaction(const void* reaction) {
    const auto cls = class_name(reaction);
    for (const char* const door : kC288DoorReactions) {
        if (strcmp(cls.text, door) == 0) {
            return true;
        }
    }
    return false;
}

// Client, gated presence: the original edge with the door reactions of the component at lt_off left out.
void c288_client_edge(C227EdgeFn orig, void* presence, void* self, void* actor, size_t lt_off) {
    auto* const component = static_cast<uint8_t*>(read_ptr(presence, lt_off));
    void** const data = component != nullptr ? static_cast<void**>(read_ptr(component, kC65Reactions)) : nullptr;
    const int32_t num = component != nullptr ? read_i32(component, kC65ReactionCount) : 0;
    if (data == nullptr || num <= 0 || num > kC227ReactionsRead) {
        orig(presence, self, actor); // nothing to filter (or more entries than the copy holds: logged below)
        if (num > kC227ReactionsRead) {
            log::line("WARN card288: client presence %s: %d reactions, over %d: door reactions not filtered",
                      prop_name(static_cast<const uint8_t*>(presence) + kObjectName, 8).text, num,
                      kC227ReactionsRead);
        }
        return;
    }
    void* kept[kC227ReactionsRead] = {};
    int32_t n = 0;
    for (int32_t i = 0; i < num; ++i) {
        if (data[i] == nullptr || !c288_door_reaction(data[i])) {
            kept[n++] = data[i];
        }
    }
    if (n == num) {
        orig(presence, self, actor);
        return;
    }
    void* const kept_data = kept;
    memcpy(component + kC65Reactions, &kept_data, sizeof(kept_data));
    memcpy(component + kC65ReactionCount, &n, sizeof(n));
    orig(presence, self, actor);
    memcpy(component + kC65Reactions, static_cast<const void*>(&data), sizeof(data));
    memcpy(component + kC65ReactionCount, &num, sizeof(num));
    if (note_event_cap(g_c288_client_logs, kC288ClientLogs, "card288 client door reactions")) {
        log::line("card288: client t=%llu presence %s %s by %s: %d door reactions left to the host's DoorOp, %d run",
                  static_cast<unsigned long long>(GetTickCount64()),
                  prop_name(static_cast<const uint8_t*>(presence) + kObjectName, 8).text,
                  lt_off == kPresenceLtEnter ? "entered" : "exited", object_name(actor).text, num - n, n);
    }
}

// The original edge: on the client a gated presence's door reactions are left out (c288_client_edge).
void c227_run_edge(C227EdgeFn orig, int32_t g, void* presence, void* self, void* actor, size_t lt_off) {
    if (g_client_session && g >= 0) {
        c288_client_edge(orig, presence, self, actor, lt_off);
        return;
    }
    orig(presence, self, actor);
}

// Card 245 (b192): ASTPlayerPresence::OnBeginOverlap (0x050F6880) runs OnTriggerEntered (vtable +0x758) only past
// six gates, read from its disassembly in this order:
//   G1 the enabled byte +0x30A (SetTriggerEventsEnabled; cleared by a once-enter presence after its first entry);
//   G2 (byte +0x5D & 0x18) == 0x10 (an AActor bitfield byte);
//   G3 byte +0x2C0 (the bool at +0x18 of the struct at +0x2A8);
//   G4 USTSaveSystemLibrary::IsLoadInProgress(presence) is false (0x05209180: the save subsystem's byte +0x3C4);
//   G5 the other component is not null and not garbage (object flags bit 30, RF_MirroredGarbage);
//   G6 the other component's byte +0x97 & 0x20.
// For a player body (trooper or mech) the row names the first gate that fails, or "passed"; c245_entered_row then
// says what OnTriggerEntered did with it. FED_C4M2's Plaza_O1 is completed only by BP_PlayerPresence_C_54.
C338OverlapFn g_orig_c245_overlap = nullptr;
C245LoadFn g_c245_load_in_progress = nullptr;
constexpr uint32_t kC245OverlapRows = 96; // player-body overlap rows per map (both sides)
static const void* g_c245_in = nullptr; // the presence whose traced overlap is on the stack (game thread only)

const char* c245_gate(const uint8_t* p, const uint8_t* other_comp, int& load) {
    load = g_c245_load_in_progress != nullptr ? (g_c245_load_in_progress(p) ? 1 : 0) : -1;
    if (p[kPresenceEnabled] == 0) {
        return "G1 enabled byte 0";
    }
    if ((p[0x5D] & 0x18) != 0x10) {
        return "G2 actor byte +0x5D";
    }
    if (p[0x2C0] == 0) {
        return "G3 byte +0x2C0 0";
    }
    if (load != 0) {
        return load > 0 ? "G4 save load in progress" : "G4 not read (no IsLoadInProgress)";
    }
    if (other_comp == nullptr || (static_cast<uint32_t>(read_i32(other_comp, 0x8)) & 0x40000000u) != 0) {
        return "G5 other component null or garbage";
    }
    return (other_comp[0x97] & 0x20) == 0 ? "G6 other component byte +0x97 bit 0x20 clear" : "passed";
}

// The outcome of a traced overlap's OnTriggerEntered call (one row per traced overlap that reached it).
void c245_entered_row(const void* presence, const void* actor, const char* what) {
    if (g_c245_in != presence) {
        return;
    }
    g_c245_in = nullptr;
    log::line("card245: %s t=%llu presence %s OnTriggerEntered by %s: %s", side_text(),
              static_cast<unsigned long long>(GetTickCount64()),
              prop_name(static_cast<const uint8_t*>(presence) + kObjectName, 8).text,
              prop_name(static_cast<const uint8_t*>(actor) + kObjectName, 8).text, what);
}

void detour_c245_overlap(void* presence, void* comp, void* other, void* other_comp, int32_t body, bool sweep,
                         const void* hit) {
    if (!c321_player_body(other)) {
        g_orig_c245_overlap(presence, comp, other, other_comp, body, sweep, hit);
        return;
    }
    const void* const parent = g_get_attach_parent_actor != nullptr ? g_get_attach_parent_actor(other) : nullptr;
    // Card 581: the attachment owns that body's place, the host the entry. Card 220 prompt triggers stay per machine.
    if (g_client_session && parent != nullptr && !c220_per_player(presence)) {
        c581_note_reject(presence, other, parent);
        return;
    }
    if (g_c245_rows >= kC245OverlapRows) {
        g_orig_c245_overlap(presence, comp, other, other_comp, body, sweep, hit);
        return;
    }
    ++g_c245_rows;
    const auto* const p = static_cast<const uint8_t*>(presence);
    const auto* const oc = static_cast<const uint8_t*>(other_comp);
    int load = -1;
    const char* const gate = c245_gate(p, oc, load);
    // Card 581: where the body is and what it rides (c582b414b: the client's copy of the host's seated trooper
    // entered BP_PlayerPresence_C_8, which the host's trooper never entered).
    const Vec3 at = actor_location(other);
    log::line("card245: %s t=%llu presence %s begin overlap by %s comp %s at (%.0f, %.0f, %.0f) attached to %s: "
              "enabled %u, +0x5D 0x%02x, +0x2C0 %u, load in progress %d, other comp byte +0x97 0x%02x: gate %s "
              "(row %u of %u)",
              side_text(), static_cast<unsigned long long>(GetTickCount64()),
              prop_name(p + kObjectName, 8).text, prop_name(static_cast<const uint8_t*>(other) + kObjectName, 8).text,
              oc != nullptr ? prop_name(oc + kObjectName, 8).text : "null", at.x, at.y, at.z,
              parent != nullptr ? object_name(parent).text : "none", p[kPresenceEnabled], p[0x5D], p[0x2C0], load,
              oc != nullptr ? oc[0x97] : 0u, gate, g_c245_rows, kC245OverlapRows);
    const bool passed = gate[0] == 'p';
    g_c245_in = passed ? presence : nullptr;
    g_orig_c245_overlap(presence, comp, other, other_comp, body, sweep, hit);
    if (g_c245_in == presence) { // gates read as passed but OnTriggerEntered was not reached: the gate list is wrong
        g_c245_in = nullptr;
        log::line("WARN card245: %s presence %s: gates passed but OnTriggerEntered not called (trace out of date)",
                  side_text(), prop_name(p + kObjectName, 8).text);
    }
}

// Card 301 gap G (b212): SetTriggerEventsEnabled (0x05103780) only stores the enabled byte +0x30A, so a pawn that
// already overlaps a presence when the script enables it never enters (OnBeginOverlap fired earlier and failed G1).
// Single-player scripts cover that with a player-0 test after the enable (FED_C4M2 @20514 C_176 enable, @20547
// GetPlayerCharacter(0), @20573 TriggerArea.IsOverlappingActor); a client standing inside never counts. Host, with a
// client: after the thunk turns the byte 0 -> 1, every other living player body that overlaps the presence goes
// through the card 245 OnBeginOverlap path (the game's own gates, then OnTriggerEntered and the card 227 gate), with
// the pawn's root component as the other component (OnBeginOverlap reads only the actor and that component). Player 0
// is left to the script's own test (GetPlayerController(world, 0) answers the same pawn the script reads, card 102
// scope included), so it never enters twice. A once-enter presence clears the byte on its first entry: the loop stops.
ExecFn g_orig_c301_exec_enable = nullptr;
constexpr uint32_t kC301EnableRows = 32; // card 301 enable rows per map (host)

int32_t c301_enter_inside(void* world, void* presence, int32_t& inside) {
    const auto* const p = static_cast<const uint8_t*>(presence);
    void* const pc0 = g_get_player_controller(world, 0);
    const void* const p0 = pc0 != nullptr ? read_ptr(pc0, kControllerPawn) : nullptr;
    const C43Roster r = c43_roster(world);
    int32_t sent = 0;
    for (int32_t s = 0; s < kC43Max && p[kPresenceEnabled] != 0; ++s) {
        void* const pawn = r.p[s].pawn;
        if (pawn == nullptr || pawn == p0 || c43_is_down(pawn) || !c321_player_body(pawn)) {
            continue;
        }
        const void* const body = c75_pilot_body(pawn); // card 300: the overlap is reported for the riding soldier
        void* const actor = g_c227_is_overlapping(presence, pawn) ? pawn
                            : body != nullptr && g_c227_is_overlapping(presence, body) ? const_cast<void*>(body)
                                                                                         : nullptr;
        if (actor == nullptr) {
            continue;
        }
        ++inside;
        void* const root = read_ptr(actor, kActorRootComponent);
        if (root == nullptr) {
            continue;
        }
        detour_c245_overlap(presence, nullptr, actor, root, 0, false, nullptr);
        ++sent;
    }
    return sent;
}

void detour_c301_exec_enable(void* ctx, void* frame, void* result) {
    const auto* const p = static_cast<const uint8_t*>(ctx);
    const uint8_t before = ctx != nullptr ? p[kPresenceEnabled] : 0;
    g_orig_c301_exec_enable(ctx, frame, result);
    if (ctx == nullptr || g_client_session || before != 0 || p[kPresenceEnabled] == 0 ||
        g_c227_presence_class == nullptr || g_c227_is_overlapping == nullptr || !is_a(ctx, g_c227_presence_class()) ||
        c220_per_player(ctx)) {
        return;
    }
    void* const world = g_actor_get_world(ctx);
    if (world == nullptr || c95_ponged_count(world) == 0) {
        return;
    }
    int32_t inside = 0;
    const int32_t sent = c301_enter_inside(world, ctx, inside);
    if (inside > 0 && note_event_cap(g_c301_rows, kC301EnableRows, "card301 enable")) {
        log::line("card301: host t=%llu presence %s enabled with %d other player(s) inside: %d sent through "
                  "OnBeginOverlap, enabled byte now %u",
                  static_cast<unsigned long long>(GetTickCount64()), prop_name(p + kObjectName, 8).text, inside, sent,
                  p[kPresenceEnabled]);
    }
}

// The gate: a gated presence that has not fired runs the original only on the entry that completes the set of
// living players inside. Held entries leave the presence as it was (enabled, nothing triggered: the original does
// the script broadcast, the reactions and the once-enter disable).
void detour_c227_entered(void* presence, void* self, void* actor) {
    if (!c220_pass("entered", presence, actor)) {
        c245_entered_row(presence, actor, "refused by card 220 (prompt trigger, another player's pawn)");
        return; // card 220: another player's pawn in a prompt trigger
    }
    const int32_t g = c227_find(presence);
    if (g >= 0 && g_client_session && g_c227.e[g].closure && !g_c227.e[g].fired) {
        c245_entered_row(presence, actor, "client closure, left to the host's row (card 300)");
        if (note_event_cap(g_c300_logs, kC300Logs, "card300 closure")) {
            log::line("card300: client t=%llu closure %s entered by %s: left to the host's row",
                      static_cast<unsigned long long>(GetTickCount64()),
                      prop_name(static_cast<const uint8_t*>(presence) + kObjectName, 8).text, object_name(actor).text);
        }
        return;
    }
    if (g >= 0 && !g_c227.e[g].fired && !c227_release(g, presence, actor)) {
        c245_entered_row(presence, actor, "held by the card 227 gate (not every living player inside)");
        if (g_c65_log) {
            c227_log_edge("entered", "held", presence, actor);
        }
        return;
    }
    if (g_c65_log) {
        c227_log_edge("entered", "begin", presence, actor);
    }
    c288_presence_fired(actor); // card 288: a door close it starts is a player-trigger close
    c245_entered_row(presence, actor, g >= 0 ? "original runs (gated presence, released)" : "original runs (script broadcast, reactions)");
    c227_run_edge(g_orig_c227_entered, g, presence, self, actor, kPresenceLtEnter);
    c220_after_enter(presence); // card 220 (b187): the prompt widget rows, after Turn On Prompt ran
    if (g_c65_log) {
        c227_log_edge("entered", "end", presence, actor);
    }
}

// Card 596: OnTriggerExited (0x050FA1A0) broadcasts the exit script delegate (+0x2D8), runs the LTOnExit reactions and,
// for a once-exit presence (+0x309), clears the enabled byte. It never asks whether another player is still inside, so
// on a presence card 227 does not gate the first player out wins (FED_C3M1 C_19 inMonorailArea, FED_C4M1 C_191
// atMetro, FED_C5M2 C_402 atWhiskey: each holds back its horde while false). Host: an exit by a player body runs the
// original only when no other living, not downed player body still overlaps the presence; a held exit leaves the
// presence as it was (once-exit included), and the last player's exit runs it once.
constexpr int32_t kC596Held = 16;      // presences with a held exit at once
constexpr uint32_t kC596Lines = 64;    // card 596 rows per session
struct C596Held {
    C31Weak presence;
    uint32_t held; // exits held since the last one that ran
};
C596Held g_c596[kC596Held]{};
uint32_t g_c596_lines = 0;

int32_t c596_find(const void* presence) {
    for (int32_t i = 0; i < kC596Held; ++i) {
        if (g_c596[i].presence.serial != 0 && c31_weak_get(g_c596[i].presence) == presence) {
            return i;
        }
    }
    return -1;
}

// The other living player bodies that overlap `presence` (the leaving actor and its own pawn left out). With
// `comp` set (card 600), the test is that component's overlap instead of the actor's.
int32_t c596_others_inside(void* presence, const void* comp, const void* actor) {
    const C43Roster r = c43_roster(g_actor_get_world(presence));
    int32_t inside = 0;
    for (int32_t s = 0; s < kC43Max; ++s) {
        void* const pawn = r.p[s].pawn;
        const void* const body = pawn != nullptr ? c75_pilot_body(pawn) : nullptr; // card 300: the riding soldier
        if (pawn == nullptr || pawn == actor || body == actor || c43_is_down(pawn) || !c321_player_body(pawn)) {
            continue;
        }
        const bool in = comp != nullptr ? g_c321_prim_overlapping(comp, pawn) ||
                                              (body != nullptr && g_c321_prim_overlapping(comp, body))
                                        : g_c227_is_overlapping(presence, pawn) ||
                                              (body != nullptr && g_c227_is_overlapping(presence, body));
        inside += in ? 1 : 0;
    }
    return inside;
}

// Host, an exit on a presence card 227 does not gate: true when it is held (another living player is inside).
bool c596_hold_exit(void* presence, void* actor) {
    if (g_client_session || g_c227_is_overlapping == nullptr || !c321_player_body(actor)) {
        return false;
    }
    const auto* const p = static_cast<const uint8_t*>(presence);
    const ULONGLONG now = GetTickCount64();
    const int32_t inside = c596_others_inside(presence, nullptr, actor);
    int32_t i = c596_find(presence);
    if (inside == 0) {
        if (i >= 0 && note_event_cap(g_c596_lines, kC596Lines, "card596")) {
            log::line("card596: host t=%llu exit on %s by %s runs: last living player out after %u held exit(s), "
                      "once-exit %u, enabled %u",
                      static_cast<unsigned long long>(now), prop_name(p + kObjectName, 8).text, object_name(actor).text,
                      g_c596[i].held, p[kPresenceOnceExit], p[kPresenceEnabled]);
        }
        if (i >= 0) {
            g_c596[i] = C596Held{};
        }
        return false;
    }
    for (int32_t k = 0; i < 0 && k < kC596Held; ++k) {
        i = g_c596[k].presence.serial == 0 || c31_weak_get(g_c596[k].presence) == nullptr ? k : -1;
    }
    if (i >= 0) {
        if (g_c596[i].presence.serial == 0 || c31_weak_get(g_c596[i].presence) != presence) {
            g_c596[i] = C596Held{};
            c31_weak_set(g_c596[i].presence, presence);
        }
        ++g_c596[i].held;
    }
    if (note_event_cap(g_c596_lines, kC596Lines, "card596")) {
        log::line("%scard596: host t=%llu exit held on %s: %s left, %d player%s inside (once-exit %u, enabled %u)",
                  i >= 0 ? "" : "WARN ", static_cast<unsigned long long>(now), prop_name(p + kObjectName, 8).text,
                  object_name(actor).text, inside, inside == 1 ? "" : "s", p[kPresenceOnceExit], p[kPresenceEnabled]);
    }
    return true;
}

// A gated presence that has not fired gives no exit edge (its exit reactions would act on an event that has not
// started); after it fires, every exit passes through. An ungated presence's exit waits for the last player out
// (card 596).
void detour_c227_exited(void* presence, void* self, void* actor) {
    if (!c220_pass("exited", presence, actor)) {
        return; // card 220
    }
    const int32_t g = c227_find(presence);
    const bool held = g >= 0 && !g_c227.e[g].fired;
    if (g_c65_log) {
        c227_log_edge("exited", held ? "held" : "begin", presence, actor);
    }
    if (held) {
        return;
    }
    if (g < 0 && c596_hold_exit(presence, actor)) {
        return; // card 596: another living player is still inside
    }
    c227_run_edge(g_orig_c227_exited, g, presence, self, actor, kPresenceLtExit);
    if (g_c65_log) {
        c227_log_edge("exited", "end", presence, actor);
    }
}

// Card 600: placed spawners and encounter prefabs bind a PlayerDetection OnComponentEndOverlap custom event that
// tests only IsPlayer(OtherActor), then pauses the encounter (BP_DestructibleSpawner_Base PlayerExit @2929 ->
// PauseReinforcementsInAllWaves when ActiveOnlyWhenPlayerNear; BP_TentSpawner_HumanGameplay PlayerExit @1210 ->
// PauseReinforcementsInAllWaves; BP_EM_PrefabBase Exit @1250 -> isPlayerOverlapping = false). The first player out
// pauses it for a player still inside. Host, the card 596 rule: the event runs only when no other living, not downed
// player body still overlaps the component. Both events take (OverlappedComponent, OtherActor, OtherComp,
// OtherBodyIndex). BP_MortarZone_C is an ASTPlayerPresence, so detour_c227_exited already holds its exit (card 596).
constexpr const char* kC600Classes[] = {"BP_EM_PrefabBase_C", "BP_DestructibleSpawner_Base_C",
                                        "BP_TentSpawner_HumanGameplay_C"};
constexpr uint32_t kC600Lines = 64;      // card 600 rows per session
constexpr size_t kC600ParmComp = 0x00;   // OverlappedComponent
constexpr size_t kC600ParmActor = 0x08;  // OtherActor
uint32_t g_c600_lines = 0;

bool c600_exit_pe(const void* object, const void* function, const void* parms, uint64_t name) {
    if (!pe_name_is(name, g_pe.names.exit) && !pe_name_is(name, g_pe.names.player_exit)) {
        return false; // card 573 b463: `name` is the detour's one read of function's FName
    }
    const void* const cls = read_ptr(object, kObjectClass);
    bool listed = false;
    for (const char* name : kC600Classes) {
        listed = listed || c73_class_is(cls, name);
    }
    const void* const comp = listed && parms != nullptr ? read_ptr(parms, kC600ParmComp) : nullptr;
    const void* const actor = comp != nullptr ? read_ptr(parms, kC600ParmActor) : nullptr;
    if (actor == nullptr || g_c321_prim_overlapping == nullptr || !c321_player_body(actor)) {
        return false;
    }
    const int32_t inside = c596_others_inside(const_cast<void*>(object), comp, actor);
    if (note_event_cap(g_c600_lines, kC600Lines, "card600")) {
        log::line("card600: host t=%llu %s on %s (%s) by %s %s: %d other player%s inside the %s",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(function).text, object_name(object).text,
                  class_name(object).text, object_name(actor).text, inside > 0 ? "held" : "runs (last living player out)",
                  inside, inside == 1 ? "" : "s", object_name(comp).text);
    }
    return inside > 0;
}

void detour_c65_trigger(void* component) {
    c65_log_trigger("Reactions", component, exe_rva(_ReturnAddress()));
    g_orig_c65_trigger(component);
}

void detour_c65_stateful_trigger(void* component) {
    c65_log_trigger("StatefulReactions", component, exe_rva(_ReturnAddress()));
    g_orig_c65_stateful_trigger(component);
}

// The card 014 pattern on a shared exec thunk: the Blueprint frame is kept for the call only when the context is a
// level-trigger component; any other context (the thunk's other users) passes straight through.
void c65_exec_with_frame(ExecThunkFn original, void* context, void* frame, void* result) {
    if (!is_a(context, g_c65_trigger_class())) {
        original(context, frame, result);
        return;
    }
    const void* const outer_node = g_c65_node;
    const void* const outer_object = g_c65_object;
    g_c65_node = read_ptr(frame, kFrameNode);
    g_c65_object = read_ptr(frame, kFrameObject);
    original(context, frame, result);
    g_c65_node = outer_node;
    g_c65_object = outer_object;
}

void detour_c65_exec_trigger(void* context, void* frame, void* result) {
    c65_exec_with_frame(g_orig_c65_exec_trigger, context, frame, result);
}

// Shared with UWidget::execSetIsEnabled; run c65c saw the range targets' ADLevelTriggerReactions activated from here.
void detour_c65_exec_activate(void* context, void* frame, void* result) {
    c65_exec_with_frame(g_orig_c65_exec_activate, context, frame, result);
}

// Card 065: a component of a range target (its owner's class name has "RangeTarget").
bool c65_range_owned(const void* component) {
    const void* const owner = component != nullptr ? read_ptr(component, kComponentOwner) : nullptr;
    return owner != nullptr && strstr(class_name(owner).text, "RangeTarget") != nullptr;
}

void c65_log_activate(const void* component, bool reset, unsigned long long caller) {
    if (!note_event_cap(g_c65_activate_logs, kC65ActivateLogs, "c65 RangeTarget Activate")) {
        return;
    }
    const void* const owner = read_ptr(component, kComponentOwner);
    const void* const node = g_c65_node;
    const Vec3 at = actor_location(owner);
    log::line("c65: %s Activate(%d) %s (%s), owner %s (%s) at %.0f %.0f %.0f, caller exe+0x%llX, blueprint %s.%s, "
              "self %s",
              side_text(), reset ? 1 : 0, object_name(component).text, class_name(component).text,
              prop_name(static_cast<const uint8_t*>(owner) + kObjectName, 8).text, class_name(owner).text, at.x, at.y,
              at.z, caller, object_name(node != nullptr ? read_ptr(node, kObjectOuter) : nullptr).text,
              object_name(node).text, object_name(g_c65_object).text);
}

// Host: "<owner> <component> <reset>" (full object names) to every remote PC. Returns the PCs sent to.
uint32_t c65_send_activate(const void* component, bool reset) {
    const void* const owner = read_ptr(component, kComponentOwner);
    char text[2 * kC65NameMax + 8];
    snprintf(text, sizeof(text), "%s %s %d", prop_name(static_cast<const uint8_t*>(owner) + kObjectName, 8).text,
             prop_name(static_cast<const uint8_t*>(component) + kObjectName, 8).text, reset ? 1 : 0);
    static wchar_t wire[kChanTextFrame];
    const WireString w = chan_text_frame(ChanOp::Activate, text, wire);
    uint32_t sent = 0;
    for (int32_t i = 0; w.num > 0 && i < kC17Slots; ++i) {
        sent += chan_send_client(g_get_player_controller(component, i), w) ? 1 : 0;
    }
    log::line("card065: host t=%llu Activate(%d) %s relayed to %u client(s)",
              static_cast<unsigned long long>(GetTickCount64()), reset ? 1 : 0, text, sent);
    if (owner != nullptr && strstr(class_name(owner).text, "KillableBuilding") != nullptr) {
        c310_collision_row(owner, "activate"); // c310 range: compare with the client's look row
    }
    return sent;
}

// Installed always: on the host a range target's level-trigger Activate is relayed to the clients after it runs
// (the client's own Blueprint never fires it, run c65f). UBWCOOP_C65_LOG=1 logs every range-target component's
// Activate, on both machines: each target's ability-system component activates once at load (run c65a).
void detour_c65_activate(void* component, bool reset) {
    const bool owned = c65_range_owned(component);
    if (g_c65_log && (owned || is_a(component, g_c65_trigger_class()))) {
        c65_log_activate(component, reset, exe_rva(_ReturnAddress()));
    }
    g_orig_c65_activate(component, reset);
    if (owned && !g_client_session && is_a(component, g_c65_trigger_class())) {
        c65_send_activate(component, reset);
        c311_host_activate(component, reset); // card 311: journalled for a client that loads the map again
    }
}

// Card 235: "owner component reset", where the component name may hold a space (the game allows one): the owner is
// the first field, the reset flag the last, and the component everything between.
static bool c65_activate_parse(const char* text, char (&owner)[kC65NameMax + 1], char (&comp)[kC65NameMax + 1], int& reset) {
    int at = 0;
    if (sscanf_s(text, "%64s %n", owner, static_cast<unsigned>(sizeof(owner)), &at) != 1 || at <= 0) {
        return false;
    }
    const char* const rest = text + at;
    const char* const last = strrchr(rest, ' ');
    const size_t n = last != nullptr ? static_cast<size_t>(last - rest) : 0;
    if (n == 0 || n >= sizeof(comp)) {
        return false;
    }
    memcpy(comp, rest, n);
    comp[n] = '\0';
    char tail = 0;
    return sscanf_s(last + 1, "%d%c", &reset, &tail, 1u) == 1;
}

// Client: the host's range-target Activate, on our own copy of that component (found by owner and component name).
void c65_receive_activate(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    char owner_name[kC65NameMax + 1];
    char comp_name[kC65NameMax + 1];
    int reset = 0;
    if (chan_text(s, text, sizeof(text)) < 0 || !c65_activate_parse(text, owner_name, comp_name, reset)) {
        log::line("WARN card065: client: bad Activate frame \"%.160s\"", text); // card 235: the refused text
        return;
    }
    void* const world = g_actor_get_world(pc);
    void* const owner = world != nullptr ? c432_find_named(world, owner_name) : nullptr;
    wchar_t w[kC65NameMax + 1];
    const size_t n = strlen(comp_name);
    for (size_t i = 0; i <= n; ++i) {
        w[i] = static_cast<wchar_t>(comp_name[i]);
    }
    void* const component = owner != nullptr ? g_static_find_object(g_c65_trigger_class(), owner, w, false) : nullptr;
    log::line("%scard065: client t=%llu Activate(%d) %s.%s: %s", component != nullptr ? "" : "WARN ",
              static_cast<unsigned long long>(GetTickCount64()), reset, owner_name, comp_name,
              component != nullptr ? "activated" : (owner != nullptr ? "no such component" : "no such actor"));
    if (component != nullptr) {
        g_orig_c65_activate(component, reset != 0);
    }
}

constexpr uint32_t kC68Logs = 400;              // event lines per map
constexpr size_t kMarkerVisibleCache = 0x2D8;   // ASTLevelObjectiveMarker: cached IsMarkerVisible (a bool)
constexpr size_t kMinimapActiveCount = 0x100;   // USTMinimapComponent::ActiveMarkers {data @0xF8, int32 num @0x100}

C68MarkerFn g_orig_c68_objective_updated = nullptr;
C68ShowFn g_orig_c68_show = nullptr;
C68IndexFn g_orig_c68_add = nullptr;
C68IndexFn g_orig_c68_remove = nullptr;
C68WidgetFn g_orig_c68_init = nullptr;
C68UpdateFn g_orig_c68_update = nullptr;
uint32_t g_c68_updates = 0;   // UpdateMarkerLocation calls since the last 1 s line
ULONGLONG g_c68_next_ms = 0;

int32_t c68_marker_count(const void* minimap) {
    return minimap != nullptr ? read_i32(minimap, kMinimapActiveCount) : -1;
}

void detour_c68_objective_updated(void* marker, const void* objective) {
    const unsigned long long caller = exe_rva(_ReturnAddress());
    const int before = static_cast<const uint8_t*>(marker)[kMarkerVisibleCache];
    g_orig_c68_objective_updated(marker, objective);
    if (note_event_cap(g_c68_logs, kC68Logs, "card068 minimap")) {
        log::line("card068: %s ObjectiveUpdated marker %p %s (%s), objective %s id %s, visible cache %d -> %d, caller "
                  "exe+0x%llX",
                  side_text(), marker, object_name(marker).text, class_name(marker).text, object_name(objective).text,
                  objective != nullptr ? fname_text(static_cast<const uint8_t*>(objective) + kObjectiveId).text : "-",
                  before, static_cast<const uint8_t*>(marker)[kMarkerVisibleCache], caller);
    }
}

void detour_c68_show(void* minimap, const void* actor) {
    const int32_t before = c68_marker_count(minimap);
    g_orig_c68_show(minimap, actor);
    if (note_event_cap(g_c68_logs, kC68Logs, "card068 minimap")) {
        log::line("card068: %s ShowOnMinimap minimap %p, actor %s (%s), ActiveMarkers %d -> %d, origin %s, caller "
                  "exe+0x%llX",
                  side_text(), minimap, object_name(actor).text, class_name(actor).text, before,
                  c68_marker_count(minimap), class_name(read_ptr(minimap, kMinimapOrigin)).text,
                  exe_rva(_ReturnAddress()));
    }
}

void c68_log_index(const char* which, const void* widget, int32_t index, unsigned long long caller) {
    if (note_event_cap(g_c68_logs, kC68Logs, "card068 minimap")) {
        log::line("card068: %s %s widget %p (%s), index %d, caller exe+0x%llX", side_text(), which, widget,
                  class_name(widget).text, index, caller);
    }
}

void detour_c68_add(void* widget, int32_t index) {
    c68_log_index("AddNewMarker", widget, index, exe_rva(_ReturnAddress()));
    g_orig_c68_add(widget, index);
}

void detour_c68_remove(void* widget, int32_t index) {
    c68_log_index("RemoveMarker", widget, index, exe_rva(_ReturnAddress()));
    g_orig_c68_remove(widget, index);
}

void detour_c68_update(void* widget, void* marker_data) {
    ++g_c68_updates;
    g_orig_c68_update(widget, marker_data);
}

// The widget's world's player controller 0 (the original reaches it through GetWorld, vtable +0x180, and the world's
// first player controller) and that PC's +0x7B0 component, before and after the original.
void c68_log_init(const char* when, const void* widget) {
    void* const pc = g_get_player_controller(widget, 0);
    void* const minimap = is_a(pc, g_st_pc_class()) ? read_ptr(pc, kPcMinimap) : nullptr;
    log::line("card068: %s NativeOnInitialized %s: widget %p (%s), player controller 0 %p (%s), minimap %p, "
              "ActiveMarkers %d",
              side_text(), when, widget, class_name(widget).text, pc, class_name(pc).text, minimap,
              c68_marker_count(minimap));
}

void detour_c68_init(void* widget) {
    c68_log_init("before", widget);
    g_orig_c68_init(widget);
    c68_log_init("after", widget);
}

int32_t c46_find_warp(int32_t id) {
    for (int32_t i = 0; i < g_c46.warps; ++i) {
        if (g_c46.warp[i].id == id) {
            return i;
        }
    }
    return -1;
}

// The cooldown row of `id`, reused or added; a full table reuses the oldest row.
void c46_mark_warp(int32_t id, ULONGLONG now) {
    int32_t i = c46_find_warp(id);
    if (i < 0 && g_c46.warps < kC46Rows) {
        i = g_c46.warps++;
    }
    for (int32_t k = 0; i < 0 && k < g_c46.warps; ++k) {
        i = k == 0 || g_c46.warp[k].ms < g_c46.warp[i].ms ? k : i;
    }
    g_c46.warp[i] = C46Warp{id, now, 0};
}

void c46_add_follow(int32_t id, int32_t target_id, ULONGLONG now) {
    if (g_c46.follows >= kC46Rows) {
        log::line("WARN card046: %s follow-up check dropped (table full)", side());
        return;
    }
    g_c46.follow[g_c46.follows++] = C46Follow{id, target_id, now + kC46FollowMs};
}

// Card 461 follow-up: the spot kC46RejoinSideUu to the right of `target` (its root's yaw) at its height, cleared by
// FindTeleportSpot for `pawn`'s class; kC46WarpUp above that side point when there is no room. A warp onto the
// target's head stood the pawn on it (c236 depenetration rows). True when FindTeleportSpot found room.
bool c46_side_spot(const void* pawn, const void* target, Vec3* out) {
    const Vec3 t = actor_location(target);
    const void* const root = read_ptr(target, kActorRootComponent);
    const double yaw = (root != nullptr ? component_yaw(root) : 0.0) / kRadToDeg;
    const Vec3 side{t.x - sin(yaw) * kC46RejoinSideUu, t.y + cos(yaw) * kC46RejoinSideUu, t.z}; // UE right vector
    Vec3 spot = side;
    const Vec3 rotation{};
    void* const world = g_actor_get_world(pawn);
    const void* const cdo = read_ptr(read_ptr(pawn, kObjectClass), kClassDefaultObject);
    const bool room = world != nullptr && cdo != nullptr && g_c46_teleport_spot != nullptr &&
                      g_c46_teleport_spot(world, cdo, &spot, &rotation);
    *out = room ? spot : Vec3{side.x, side.y, side.z + kC46WarpUp};
    return room;
}

// Moves `pawn` to `to`, zeroes its velocity and puts it back to Walking if it is Flying.
bool c46_move_to(void* pawn, const Vec3& to) {
    const bool moved = g_set_actor_location(pawn, &to, false, nullptr, kTeleportPhysics);
    auto* const cmc = static_cast<uint8_t*>(read_ptr(pawn, kCharacterMovement));
    if (cmc == nullptr) {
        log::line("WARN card046: %s %s has no CharacterMovement", side(), object_name(pawn).text);
        return moved;
    }
    char base[160]; // card 461: the movement base after the warp, for the row
    c461_base_text(pawn, base, sizeof(base));
    const Vec3 zero{};
    memcpy(cmc + kMoveVelocity, &zero, sizeof(zero));
    const uint8_t mode = cmc[kCmcMovementMode];
    if (mode == kMoveFlying) {
        const auto* const vt = static_cast<void* const*>(read_ptr(cmc, 0));
        reinterpret_cast<SetMovementModeFn>(vt[kVtSetMovementMode / sizeof(void*)])(cmc, kMoveWalking, 0);
    }
    if (c46_line()) {
        log::line("card046: %s %s movement mode %u -> %u, base %s", side(), object_name(pawn).text, mode,
                  cmc[kCmcMovementMode], base);
    }
    return moved;
}

// Kill-floor hit of kC46DamageFraction of MaxHealth (self-causer, armour ignored); Health is never written directly.
// Falls back to card 046's USTCombatUtilityLibrary::DealDamage when the tagged call cannot be made.
void c46_hurt(void* pawn) {
    void* component = nullptr;
    const float before = c21_health(static_cast<const uint8_t*>(pawn), &component);
    const float max_health = c43_max_health(component);
    if (max_health <= 0.0f) {
        log::line("WARN card046: host no warp damage on %s: MaxHealth %.1f", object_name(pawn).text,
                  static_cast<double>(max_health));
        return;
    }
    const C48Armour a0 = c48_armour(component);
    const float amount = kC46DamageFraction * max_health;
    const bool tagged = c48_deal_ignore_armour(pawn, amount);
    if (!tagged) {
        g_deal_damage(pawn, pawn, amount, true);
    }
    const float after = c21_health(static_cast<const uint8_t*>(pawn), &component);
    const C48Armour a1 = c48_armour(component);
    log::line("card048: host warp damage %.1f (%s) on %s: Health %.1f -> %.1f (change %.1f), Armour %.1f -> %.1f "
              "(MaxArmour %.1f), state %s",
              static_cast<double>(amount), tagged ? "ignore armour and modifiers" : "card 046 fallback", object_name(pawn).text,
              static_cast<double>(before), static_cast<double>(after), static_cast<double>(before - after),
              static_cast<double>(a0.armour), static_cast<double>(a1.armour), static_cast<double>(a1.max_armour),
              kC43LifeText[static_cast<size_t>(c43_life(pawn))]);
}

void c50_chapter_frame() {
    g_c50.chapter_ms = GetTickCount64();
}

// Once a second: the local PC's minimap component, its OriginActor, ActiveMarkers and MinimapRange.
void c68_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    if (now < g_c68_next_ms) {
        return;
    }
    g_c68_next_ms = now + kC68LineMs;
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? g_get_player_controller(world, 0) : nullptr;
    if (!is_a(pc, g_st_pc_class())) {
        return; // +0x7B0 is an ASTPlayerController field: no other controller class has it
    }
    void* const minimap = read_ptr(pc, kPcMinimap);
    const void* const origin = minimap != nullptr ? read_ptr(minimap, kMinimapOrigin) : nullptr;
    log::line("card068: %s 1s pc %p minimap %p origin %p (%s), ActiveMarkers %d, MinimapRange %.0f, "
              "UpdateMarkerLocation %u",
              side_text(), pc, minimap, origin, class_name(origin).text, c68_marker_count(minimap),
              minimap != nullptr ? read_f32(minimap, kMinimapRange) : -1.0f, g_c68_updates);
    g_c68_updates = 0;
}

// Installed after MH_Initialize, before the hook tables, and only with UBWCOOP_C68_LOG=1.
bool c68_install(uint8_t* base, const IMAGE_NT_HEADERS64* nt) {
    const HookSpec hooks[] = {
        {&offsets::ASTLevelObjectiveMarker_ObjectiveUpdated, reinterpret_cast<void*>(&detour_c68_objective_updated),
         reinterpret_cast<void**>(&g_orig_c68_objective_updated)},
        {&offsets::USTMinimapComponent_ShowOnMinimap, reinterpret_cast<void*>(&detour_c68_show),
         reinterpret_cast<void**>(&g_orig_c68_show)},
        {&offsets::USTMinimapWidget_AddNewMarker, reinterpret_cast<void*>(&detour_c68_add),
         reinterpret_cast<void**>(&g_orig_c68_add)},
        {&offsets::USTMinimapWidget_RemoveMarker, reinterpret_cast<void*>(&detour_c68_remove),
         reinterpret_cast<void**>(&g_orig_c68_remove)},
        {&offsets::USTMinimapWidget_NativeOnInitialized, reinterpret_cast<void*>(&detour_c68_init),
         reinterpret_cast<void**>(&g_orig_c68_init)},
        {&offsets::USTMinimapWidget_UpdateMarkerLocation, reinterpret_cast<void*>(&detour_c68_update),
         reinterpret_cast<void**>(&g_orig_c68_update)},
    };
    for (const HookSpec& h : hooks) {
        void* const target = code_address(base, nt, *h.sym);
        if (target == nullptr || !create_and_enable(target, h.detour, h.original, h.sym->name)) {
            return false;
        }
    }
    log::line("hooks: card 068 minimap log on (%zu detours)", sizeof(hooks) / sizeof(hooks[0]));
    return true;
}

// Card 097: the native completion choke point. USTLevelObjective::CompleteObjective (0x05150C70) returns at once
// for a complete objective; otherwise it sets +0x69, completes every sub-objective (vtable +0x2B8), broadcasts, and
// for a sub-objective calls the parent's OnSubObjectiveComplete (+0x2D8), which may complete the parent.
// USTLevelObjectiveCount::IncrementObjectiveCount (0x0515D9B0) completes the objective at its target. Level script
// and native code reach both without the card 049 library functions, so those completions never reached the client.
// Host, outside a mirrored library call: each objective completed and each count changed in one outermost call is
// queued in entry order and sent after that call returns. Inside a library call the card 049 mirror already sends
// the call and the client's replay runs the same cascade. Every completion is logged on both sides.
C97ObjectiveFn g_orig_c97_complete = nullptr;
C97ObjectiveFn g_orig_c97_increment = nullptr;
C97SubsystemCompleteFn g_orig_c97_subsystem_complete = nullptr;
uint32_t g_c491_window = 0; // card 491 b227: subsystem ObjectiveComplete calls in progress (nesting depth)

// Card 579 (c397b398a): a checkpoint snapshot or join replay frame restates completions the host already paid for
// and restored from its save, so the client's replayed ObjectiveComplete must not pay again (9 x 10000 and a stack of
// "+10,000 Objective Complete" toasts after the reload). Live completions still pay on both sides.
uint32_t g_c579_dropped = 0; // game thread
bool c579_drop_award(uint64_t tag, int32_t amount) {
    if (!g_client_session || g_c491_window == 0 || g_c435.join == 0) {
        return false;
    }
    ++g_c579_dropped;
    log::line("card579: client t=%llu dropped replayed award %s %d inside a snapshot or join frame (%u dropped)",
              static_cast<unsigned long long>(GetTickCount64()), fname_text(&tag).text, amount, g_c579_dropped);
    return true;
}

namespace {

constexpr int32_t kC97Queue = 32;      // completions and counts per outermost call
bool g_c491_warned = false; // card 491: the no-world WARN, once per session
constexpr size_t kC491IsSubobjective = 0x2B0; // USTLevelObjective vtable: IsSubobjective (0x0515FCD0)
constexpr uint32_t kC97Lines = 1200;   // native completion lines per process (diagnosis; card 097 uncaps these)
constexpr size_t kUObjectOuter = 0x20; // UObjectBase::OuterPrivate
constexpr int32_t kC97OuterHops = 6;   // objective -> subsystem -> world

struct C97Entry {
    const uint8_t* objective;
    C49Kind kind;   // Complete (CompleteSub when it has a parent) or Increment
    int32_t before; // Increment: the count before the call
};
struct C97State {
    uint32_t depth; // native calls in progress
    int32_t n;
    bool full; // card 499: an entry was dropped, so the completion set is not whole
    C97Entry queue[kC97Queue];
    uint32_t lines;
};
C97State g_c97{}; // game thread

bool c97_line() {
    return note_event_cap(g_c97.lines, kC97Lines, "card097 native objective");
}

// The world the objective lives in (its outer chain), or null.
const void* c97_world(const uint8_t* objective) {
    const void* o = objective;
    for (int32_t i = 0; i < kC97OuterHops && o != nullptr; ++i) {
        o = read_ptr(o, kUObjectOuter);
        if (o != nullptr && strcmp(class_name(o).text, "World") == 0) {
            return o;
        }
    }
    return nullptr;
}

void c97_push(const uint8_t* objective, C49Kind kind, int32_t before) {
    if (g_c97.n >= kC97Queue) {
        log::line("WARN card097: host queue full (%d): %s %s not sent", kC97Queue, kC49KindText[static_cast<size_t>(kind)],
                  fname_text(&*reinterpret_cast<const uint64_t*>(objective + kObjectiveId)).text);
        g_c97.full = true;
        return;
    }
    g_c97.queue[g_c97.n++] = C97Entry{objective, kind, before};
}

// The frame for one queued entry, or false when there is nothing to send.
bool c97_call(const C97Entry& e, C49Call& c) {
    const uint64_t id = read_u64(e.objective, kObjectiveId);
    if (e.kind == C49Kind::Increment) {
        c = C49Call{C49Kind::Increment, {id, 0}, kC49HostExistsBit, 0, nullptr}; // card 305: a live objective
        return read_i32(e.objective, kObjectiveCountCurrent) != e.before;
    }
    const void* const parent = read_ptr(e.objective, kObjectiveParent);
    c = parent != nullptr
            ? C49Call{C49Kind::CompleteSub, {read_u64(parent, kObjectiveId), id}, kC49HostExistsBit, 0, nullptr}
            : C49Call{C49Kind::Complete, {id, 0}, kC49HostExistsBit, 0, nullptr}; // card 305: a live objective
    return e.objective[kObjectiveComplete] != 0;
}

// Host, after the outermost native call: send the queue in entry order through the card 049 mirror.
// Card 499 sibling: the queue's completions are every native completion of that call, so they go first as the
// HostSet, the first row carries kC499GroupBit and the rest kC499NestedBit; the client's cascade of the first row
// completes nothing the host did not.
void c97_flush(const uint8_t* objective) {
    const int32_t n = g_c97.n;
    const bool full = g_c97.full;
    g_c97.n = 0;
    g_c97.full = false;
    if (n == 0) {
        return;
    }
    const void* const world = c97_world(objective);
    // Card 349: any PC in the world will do (the world context object only); not the local PC.
    void* const pc = world != nullptr ? g_world_first_pc(const_cast<void*>(world)) : nullptr;
    if (pc == nullptr) {
        log::line("WARN card097: host has no world or player controller for %d queued objective frame(s) (world %p)", n,
                  world);
        return;
    }
    C49Call rows[kC97Queue] = {};
    uint64_t ids[kC97Queue] = {};
    int32_t r = 0;
    int32_t m = 0;
    for (int32_t i = 0; i < n && i < kC97Queue; ++i) {
        if (!c97_call(g_c97.queue[i], rows[r])) {
            continue;
        }
        if (c253_completion(rows[r].kind)) {
            ids[m++] = rows[r].name[rows[r].kind == C49Kind::CompleteSub ? 1 : 0];
        }
        ++r;
    }
    const bool grouped = r > 0 && c499_send_set(pc, ids, m, full, rows[0]);
    for (int32_t i = 0; i < r; ++i) {
        rows[i].flags |= !grouped ? 0u : (i == 0 ? kC499GroupBit : kC499NestedBit);
        c49_mirror(pc, rows[i]);
    }
}

void c97_note(const char* what, const uint8_t* o, bool send, const void* ret) {
    if (!c97_line()) {
        return;
    }
    const void* const parent = read_ptr(o, kObjectiveParent);
    const uint64_t parent_id = parent != nullptr ? read_u64(parent, kObjectiveId) : 0;
    log::line("card097: %s t=%llu native %s %s%s%s (count %d/%d), depth %u, library depth %u, %s; caller exe+0x%llX",
              side(), static_cast<unsigned long long>(GetTickCount64()), what,
              parent_id != 0 ? fname_text(&parent_id).text : "", parent_id != 0 ? "/" : "",
              fname_text(reinterpret_cast<const uint64_t*>(o + kObjectiveId)).text,
              is_a(o, g_c49_count_class()) ? read_i32(o, kObjectiveCountCurrent) : -1,
              is_a(o, g_c49_count_class()) ? read_i32(o, kObjectiveCountTarget) : -1, g_c97.depth, g_c49.depth,
              g_client_session ? "client" : (send ? "queued" : "mirrored by the library call"), exe_rva(ret));
}

// Card 100: only the host completes objectives. On the client, a native completion or count outside a host replay
// (its own level script, a local overlap or pickup) is skipped; the host's frame completes it later through the
// replay, which also fires the client's OnLevelObjectiveComplete events in the host's order. Logged once per objective.
constexpr int32_t kC100Skipped = 48;
struct C100State {
    int32_t n;
    uint64_t id[kC100Skipped];
    uint32_t skips;
};
C100State g_c100{}; // game thread

bool c100_skip_local(const uint8_t* o, const char* what, const void* ret) {
    if (!g_client_session || g_c49.replay != 0) {
        return false;
    }
    ++g_c100.skips;
    const uint64_t id = read_u64(o, kObjectiveId);
    for (int32_t i = 0; i < g_c100.n; ++i) {
        if (g_c100.id[i] == id) {
            return true;
        }
    }
    if (g_c100.n < kC100Skipped) {
        g_c100.id[g_c100.n++] = id;
        log::line("card100: client t=%llu skipped local %s %s (only the host completes; %u skips so far); caller exe+0x%llX",
                  static_cast<unsigned long long>(GetTickCount64()), what, fname_text(&id).text, g_c100.skips,
                  exe_rva(ret));
    }
    return true;
}

} // namespace

void detour_c97_complete(void* objective) {
    const auto* const o = static_cast<const uint8_t*>(objective);
    if (o[kObjectiveComplete] == 0 && c100_skip_local(o, "CompleteObjective", _ReturnAddress())) {
        return;
    }
    if (o[kObjectiveComplete] == 0 && c499_block(o, _ReturnAddress())) { // card 499: outside the host's set
        return;
    }
    const bool send = !g_client_session && g_c49.depth == 0;
    if (o[kObjectiveComplete] == 0) {
        if (!g_client_session && g_c491.completes > 0) {
            c499_host_note(read_u64(o, kObjectiveId)); // card 499: inside a complete-kind library call
        }
        c97_note("CompleteObjective", o, send, _ReturnAddress());
        if (send) {
            c97_push(o, C49Kind::Complete, 0);
        }
    }
    ++g_c97.depth;
    g_orig_c97_complete(objective);
    if (--g_c97.depth == 0 && send) {
        c97_flush(o);
    }
}

void detour_c97_increment(void* objective) {
    const auto* const o = static_cast<const uint8_t*>(objective);
    if (c100_skip_local(o, "IncrementObjectiveCount", _ReturnAddress())) {
        return;
    }
    const bool send = !g_client_session && g_c49.depth == 0;
    const int32_t before = read_i32(o, kObjectiveCountCurrent);
    if (send) {
        c97_push(o, C49Kind::Increment, before); // before the completions it triggers, as the client replays them
    }
    ++g_c97.depth;
    g_orig_c97_increment(objective);
    c97_note(read_i32(o, kObjectiveCountCurrent) != before ? "IncrementObjectiveCount" : "IncrementObjectiveCount (no change)",
             o, send, _ReturnAddress());
    if (--g_c97.depth == 0 && send) {
        c97_flush(o);
    }
}

// Diagnosis only: the subsystem's completion broadcast (OnLevelObjectiveComplete).
void detour_c97_subsystem_complete(void* subsystem, const void* objective) {
    if (objective != nullptr && c97_line()) {
        log::line("card097: %s t=%llu subsystem ObjectiveComplete(%s); caller exe+0x%llX", side(),
                  static_cast<unsigned long long>(GetTickCount64()),
                  fname_text(reinterpret_cast<const uint64_t*>(static_cast<const uint8_t*>(objective) + kObjectiveId)).text,
                  exe_rva(_ReturnAddress()));
    }
    // Card 491: the FedScore this completion paid. The subsystem pays only for a sub-objective (vtable +0x2B0,
    // IsSubobjective) shown in the list (kObjectiveInList), reward x GetFedscoreMultiplier.
    const uint8_t* const o = static_cast<const uint8_t*>(objective);
    // b224 (c245b224a: every row -1 on both sides, cause not yet logged): the world comes from the subsystem's outer
    // (a world subsystem) first, then the objective's outer chain; the WARN below names both outers if neither works.
    const bool reader = o != nullptr && g_c131_score_subsystem != nullptr && g_c131_fed_score != nullptr;
    const void* found = reader && subsystem != nullptr ? c97_world(static_cast<const uint8_t*>(subsystem)) : nullptr;
    found = found == nullptr && reader ? c97_world(o) : found;
    void* const world = const_cast<void*>(found);
    const void* const sub = world != nullptr ? g_c131_score_subsystem(world) : nullptr;
    if (sub == nullptr && o != nullptr && !g_c491_warned) {
        g_c491_warned = true;
        const void* const outer = read_ptr(o, kUObjectOuter);
        const void* const sub_outer = subsystem != nullptr ? read_ptr(subsystem, kUObjectOuter) : nullptr;
        log::line("WARN card491: %s no score subsystem (reader %d, world %s): objective outer %s, subsystem outer %s",
                  side(), reader ? 1 : 0, world != nullptr ? "found" : "null",
                  outer != nullptr ? class_name(outer).text : "null",
                  sub_outer != nullptr ? class_name(sub_outer).text : "null");
    }
    const int32_t before = sub != nullptr ? g_c131_fed_score(sub) : -1;
    ++g_c491_window; // b227: every AddFedScore inside logs, apart from the card 099 score budget
    g_orig_c97_subsystem_complete(subsystem, objective);
    --g_c491_window;
    if (o == nullptr) {
        return;
    }
    using IsSubFn = bool (*)(const void* objective);
    const auto* const vtable = *reinterpret_cast<void* const* const*>(o);
    const bool is_sub = reinterpret_cast<IsSubFn>(vtable[kC491IsSubobjective / sizeof(void*)])(o);
    const bool inlist = o[kObjectiveInList] != 0;
    const int32_t after = sub != nullptr ? g_c131_fed_score(sub) : -1;
    if ((after != before || inlist) && c97_line()) {
        log::line("card491: %s t=%llu ObjectiveComplete(%s) sub %d inlist %d score %d -> %d", side(),
                  static_cast<unsigned long long>(GetTickCount64()),
                  fname_text(reinterpret_cast<const uint64_t*>(o + kObjectiveId)).text, is_sub ? 1 : 0,
                  inlist ? 1 : 0, before, after);
    }
}

OverlapMultiFn g_orig_c46_overlap_multi = nullptr;
EncroachingFn g_c46_encroaching = nullptr;
TeleportSpotFn g_c46_teleport_spot = nullptr;

namespace {

constexpr unsigned long long kC46SpawnPawnCall = 0x3C6EC9B; // AGameModeBase::SpawnDefaultPawnAtTransform_Implementation's
                                                            // SpawnActor call (return address)
constexpr double kC46OriginUu = 1.0;       // a spawn this close to (0, 0, 0) is FindPlayerStart's WorldSettings fallback
// The sides of the Up player tried in order (x, y unit steps), each resolved by FindTeleportSpot.
constexpr double kC46Sides[4][2] = {{1.0, 0.0}, {-1.0, 0.0}, {0.0, 1.0}, {0.0, -1.0}};
// FOverlapResult as ComponentEncroachesBlockingGeometry_WithAdjustment (0x143DB6AE0) reads it: 0x48 bytes per
// result, the overlapped component a TWeakObjectPtr at +0x20 (read through FWeakObjectPtr::Get), and the owner
// actor the FActorInstanceHandle's TWeakObjectPtr<AActor> at +0x0.
constexpr size_t kOverlapStride = 0x48;
constexpr size_t kOverlapComponent = 0x20;
constexpr size_t kPrimResponseToChannel = 0x5D0; // UPrimitiveComponent vtable: GetCollisionResponseToChannel
constexpr int32_t kResponseBlock = 2;            // ECR_Block
constexpr int32_t kC46MaxOverlaps = 32;

// While `on`, detour_c46_overlap_multi logs the overlaps it returns (the PlayerStart probe below, and card 590's
// stuck spot probe): `tag` starts each row, `where` names the spot, at most `cap` overlap rows.
struct C46Probe {
    bool on = false;
    Vec3 start{};
    char tag[48] = "card046: host  ";
    const char* where = "the PlayerStart";
    int32_t cap = kC46MaxOverlaps;
};
C46Probe g_c46_probe;

int32_t c46_response(const void* comp, int32_t channel) {
    using ResponseFn = int32_t (*)(const void* comp, int32_t channel);
    const auto* const vtable = *static_cast<void* const* const*>(comp);
    return reinterpret_cast<ResponseFn>(vtable[kPrimResponseToChannel / sizeof(void*)])(comp, channel);
}

// Diagnosis: runs the engine's own encroachment test for the pawn class at the PlayerStart (with an adjustment, so
// it takes the OverlapMultiByChannel path and the detour lists every overlap), then FindTeleportSpot from there,
// the two calls ChoosePlayerStart makes, and logs both results.
void c46_probe_start(void* world, const void* cdo, const void* start_actor) {
    const Vec3 start = actor_location(start_actor);
    const Vec3 rotation{};
    Vec3 at = start;
    Vec3 adjustment{};
    g_c46_probe.on = true;
    g_c46_probe.start = start;
    snprintf(g_c46_probe.tag, sizeof(g_c46_probe.tag), "card046: host  ");
    g_c46_probe.where = "the PlayerStart";
    g_c46_probe.cap = kC46MaxOverlaps;
    const bool encroached = g_c46_encroaching(world, cdo, &at, &rotation, &adjustment);
    g_c46_probe.on = false;
    Vec3 spot = start;
    const bool teleport = g_c46_teleport_spot(world, cdo, &spot, &rotation);
    log::line("card046: host PlayerStart %s at (%.0f, %.0f, %.0f): EncroachingBlockingGeometry %d (adjustment (%.0f, "
              "%.0f, %.0f)), FindTeleportSpot %d at (%.0f, %.0f, %.0f)",
              object_name(start_actor).text, start.x, start.y, start.z, encroached ? 1 : 0, adjustment.x,
              adjustment.y, adjustment.z, teleport ? 1 : 0, spot.x, spot.y, spot.z);
}

void c46_probe_starts(void* world, const void* cdo) {
    const auto* const levels = static_cast<void* const*>(world != nullptr ? read_ptr(world, kWorldLevels) : nullptr);
    const int32_t num_levels = levels != nullptr ? read_i32(world, kWorldLevels + kArrayNum) : 0;
    for (int32_t l = 0; l < num_levels && l < kMaxLevels; ++l) {
        auto* const* const actors = levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = actors != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; a < num && a < kMaxLevelActors; ++a) {
            if (actors[a] != nullptr && strcmp(class_name(actors[a]).text, "PlayerStart") == 0) {
                c46_probe_start(world, cdo, actors[a]);
            }
        }
    }
}

// A free spot beside `target` for a pawn of `cdo`'s class: each side in kC46Sides, resolved by the engine's
// FindTeleportSpot (the same check ChoosePlayerStart uses). False when no side has room.
bool c46_free_spot(void* world, const void* cdo, const Vec3& target, Vec3* out) {
    const Vec3 rotation{};
    for (const auto& side : kC46Sides) {
        const Vec3 tried{target.x + side[0] * kC46RejoinSideUu, target.y + side[1] * kC46RejoinSideUu, target.z};
        Vec3 spot = tried;
        const bool ok = g_c46_teleport_spot(world, cdo, &spot, &rotation);
        log::line("card046: host   FindTeleportSpot beside the Up player at (%.0f, %.0f, %.0f): %d -> (%.0f, %.0f, %.0f)",
                  tried.x, tried.y, tried.z, ok ? 1 : 0, spot.x, spot.y, spot.z);
        if (ok) {
            *out = spot;
            return true;
        }
    }
    return false;
}

// Card 146: the first PlayerStart moved by the engine's encroachment adjustment, then cleared by FindTeleportSpot
// (kC46WarpUp above the adjusted point when FindTeleportSpot finds no room). False when the map has no PlayerStart.
bool c46_start_spot(void* world, const void* cdo, Vec3* out, const char** from) {
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = levels != nullptr ? read_i32(world, kWorldLevels + kArrayNum) : 0;
    for (int32_t l = 0; l < num_levels && l < kMaxLevels; ++l) {
        auto* const* const actors = levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = actors != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; a < num && a < kMaxLevelActors; ++a) {
            if (actors[a] == nullptr || strcmp(class_name(actors[a]).text, "PlayerStart") != 0) {
                continue;
            }
            const Vec3 start = actor_location(actors[a]);
            const Vec3 rotation{};
            Vec3 at = start;
            Vec3 adjustment{};
            g_c46_encroaching(world, cdo, &at, &rotation, &adjustment);
            Vec3 spot{start.x + adjustment.x, start.y + adjustment.y, start.z + adjustment.z};
            const bool ok = g_c46_teleport_spot(world, cdo, &spot, &rotation);
            if (!ok) {
                spot = Vec3{start.x + adjustment.x, start.y + adjustment.y, start.z + adjustment.z + kC46WarpUp};
            }
            *out = spot;
            *from = ok ? "the PlayerStart (adjusted, FindTeleportSpot 1)" : "above the PlayerStart (adjusted, FindTeleportSpot 0)";
            return true;
        }
    }
    return false;
}

constexpr double kC46ZoneNearUu = 5000.0; // the nearest zone this close to the saver is the one it saved in

// The nearest BP_CheckPointSaveZone_C to `at` on `world`, or nullptr.
const void* c46_nearest_zone(const void* world, const Vec3& at, double* dist) {
    const void* best = nullptr;
    const auto* const levels = static_cast<void* const*>(read_ptr(world, kWorldLevels));
    const int32_t num_levels = levels != nullptr ? read_i32(world, kWorldLevels + kArrayNum) : 0;
    for (int32_t l = 0; l < num_levels && l < kMaxLevels; ++l) {
        auto* const* const actors = levels[l] != nullptr ? static_cast<void* const*>(read_ptr(levels[l], kLevelActors)) : nullptr;
        const int32_t num = actors != nullptr ? read_i32(levels[l], kLevelActors + kArrayNum) : 0;
        for (int32_t a = 0; a < num && a < kMaxLevelActors; ++a) {
            if (actors[a] == nullptr || strcmp(class_name(actors[a]).text, "BP_CheckPointSaveZone_C") != 0) {
                continue;
            }
            const double d = vec_dist(actor_location(actors[a]), at);
            if (best == nullptr || d < *dist) {
                best = actors[a];
                *dist = d;
            }
        }
    }
    return best;
}

} // namespace

// Host, at each Save_CheckPoint: the saver is the first Up player (else the host's pawn); its nearest zone within
// kC46ZoneNearUu is the checkpoint, else the saver's own spot (a lever's TriggerSave far from any zone).
void c46_note_checkpoint(void* world) {
    const C43Roster r = c43_roster(world);
    const void* saver = c46_up_target(r, -1);
    saver = saver != nullptr ? saver : r.p[0].pawn;
    if (saver == nullptr) {
        log::line("WARN card146: host checkpoint save with no player pawn: rescue anchor not moved");
        return;
    }
    const Vec3 at = actor_location(saver);
    if (at.z < static_cast<double>(c46_kill_z(saver))) { // card 372: no anchor under the world
        log::line("WARN card146: host checkpoint save with the saver below the kill floor (z %.0f): rescue anchor not "
                  "moved", at.z);
        return;
    }
    double d = 0.0;
    const void* const zone = c46_nearest_zone(world, at, &d);
    const bool in_zone = zone != nullptr && d <= kC46ZoneNearUu;
    g_c46.anchor_world = world;
    g_c46.anchor = in_zone ? actor_location(zone) : at;
    log::line("card146: host t=%llu checkpoint save by %s at (%.0f, %.0f, %.0f): rescue anchor %s at (%.0f, %.0f, "
              "%.0f)", static_cast<unsigned long long>(GetTickCount64()), object_name(saver).text, at.x, at.y, at.z,
              in_zone ? object_name(zone).text : "the saver's spot", g_c46.anchor.x, g_c46.anchor.y,
              g_c46.anchor.z);
}

// Card 146 r2: a rescue spot this close (2D) to a player's pawn, alive or dead, is on top of it. FindTeleportSpot
// does not move off a dead pawn (its capsule no longer blocks), and the new pawn slid 300 uu off the host's corpse.
constexpr double kC146PawnClearUu = 100.0;

double c146_dist2d(const Vec3& a, const Vec3& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

const void* c146_pawn_at(const C43Roster& r, const Vec3& spot) {
    for (const auto& p : r.p) {
        if (p.pawn != nullptr && c146_dist2d(actor_location(p.pawn), spot) < kC146PawnClearUu) {
            return p.pawn;
        }
    }
    return nullptr;
}

bool c46_rescue_spot(void* world, const void* cdo, const C43Roster& r, Vec3* out, const char** from) {
    const Vec3 rotation{};
    if (g_c46.anchor_world == world) {
        Vec3 spot{g_c46.anchor.x, g_c46.anchor.y, g_c46.anchor.z + kC46WarpUp};
        const bool ok = g_c46_teleport_spot(world, cdo, &spot, &rotation);
        log::line("card146: host   FindTeleportSpot at the last checkpoint (%.0f, %.0f, %.0f): %d -> (%.0f, %.0f, "
                  "%.0f)", g_c46.anchor.x, g_c46.anchor.y, g_c46.anchor.z, ok ? 1 : 0, spot.x, spot.y,
                  spot.z);
        const void* const on = ok ? c146_pawn_at(r, spot) : nullptr;
        if (on != nullptr) {
            log::line("card146: host   the checkpoint spot is %.0f uu from %s's pawn (alive or dead): a side spot "
                      "is taken", c146_dist2d(actor_location(on), spot), object_name(on).text);
            if (c46_free_spot(world, cdo, spot, out)) {
                *from = "beside the last checkpoint";
                return true;
            }
        } else if (ok) {
            *out = spot;
            *from = "the last checkpoint";
            return true;
        }
    }
    // Card 372: a host pawn below the kill floor is no rescue spot (it may be the pawn being rescued).
    const void* const host = r.p[0].pawn;
    if (host != nullptr && actor_location(host).z >= static_cast<double>(c46_kill_z(host)) &&
        c46_free_spot(world, cdo, actor_location(host), out)) {
        *from = "beside the host's pawn";
        return true;
    }
    return c46_start_spot(world, cdo, out, from);
}

bool detour_c46_overlap_multi(const void* world, void* out, const Vec3* location, const void* rotation,
                              int32_t channel, const void* shape, const void* params, const void* response) {
    const bool hit = g_orig_c46_overlap_multi(world, out, location, rotation, channel, shape, params, response);
    if (!g_c46_probe.on || out == nullptr) {
        return hit;
    }
    const auto* const data = static_cast<const uint8_t*>(read_ptr(out, 0));
    const int32_t num = data != nullptr ? read_i32(out, kArrayNum) : 0;
    log::line("%s overlaps at %s, channel %d: %d (blocking hit %d)", g_c46_probe.tag, g_c46_probe.where, channel,
              num, hit ? 1 : 0);
    for (int32_t i = 0; i < num && i < g_c46_probe.cap; ++i) {
        const uint8_t* const r = data + static_cast<size_t>(i) * kOverlapStride;
        // The owner is the component's outer: +0 of FOverlapResult is an FActorInstanceHandle, not a plain weak
        // actor pointer (hb33: reading it as one crashed the host).
        const void* const comp = g_weak_get(r + kOverlapComponent);
        const void* const outer = comp != nullptr ? read_ptr(comp, kObjectOuter) : nullptr;
        const void* const actor = outer != nullptr && g_actor_class != nullptr && is_a(outer, g_actor_class()) ? outer : nullptr;
        char text[256];
        c39_comp_text(comp, text, sizeof(text));
        const double d = comp != nullptr ? vec_dist(read_vec3(comp, kComponentTranslation), g_c46_probe.start) : -1.0;
        log::line("%s overlap %d: owner %s %s, %.0f uu; response %d (block %d); %s", g_c46_probe.tag, i,
                  actor != nullptr ? class_name(actor).text : "none", actor != nullptr ? object_name(actor).text : "-",
                  d, comp != nullptr ? c46_response(comp, channel) : -1,
                  comp != nullptr && c46_response(comp, channel) == kResponseBlock ? 1 : 0, text);
    }
    return hit;
}

// Card 590: the engine's encroachment test for the live pawn at its stuck spot (the pawn's own capsule and collision
// profile, the pawn itself ignored); the detour logs each overlap, owner class and name, and whether it blocks the
// test channel. kC590MaxRows per call, kC590MaxCalls per run, then silent.
namespace {
constexpr int32_t kC590MaxRows = 8;
constexpr int32_t kC590MaxCalls = 12;
int32_t g_c590_calls = 0;
} // namespace
void c590_stuck_overlaps(void* world, const void* pawn, const Vec3& at, uint32_t seq) {
    if (world == nullptr || pawn == nullptr || g_c46_encroaching == nullptr || g_c590_calls >= kC590MaxCalls) {
        return;
    }
    ++g_c590_calls;
    const Vec3 rotation{};
    Vec3 spot = at;
    Vec3 adjustment{};
    g_c46_probe.on = true;
    g_c46_probe.start = at;
    snprintf(g_c46_probe.tag, sizeof(g_c46_probe.tag), "card590: %s cmd %u", side(), seq);
    g_c46_probe.where = "the stuck spot";
    g_c46_probe.cap = kC590MaxRows;
    const bool encroached = g_c46_encroaching(world, pawn, &spot, &rotation, &adjustment);
    g_c46_probe.on = false;
    log::line("card590: %s cmd %u stuck spot (%.0f, %.0f, %.0f): EncroachingBlockingGeometry %d, adjustment (%.1f, "
              "%.1f, %.1f), probe %d of %d", side(), seq, at.x, at.y, at.z, encroached ? 1 : 0, adjustment.x,
              adjustment.y, adjustment.z, g_c590_calls, kC590MaxCalls);
}

// Card 146 test (UBWCOOP_C146_NO_START=1): a player spawn while 2+ players are present and nobody is Up is taken as
// FindPlayerStart's WorldSettings fallback (every PlayerStart encroached), the playtest 2026-11-08 rejoin with the
// host dead, so the origin branch below runs on a map whose PlayerStart is free. Off: false.
namespace {
bool c146_test_no_start(const void* world, const void* up_target, const Vec3& at) {
    static int8_t on = -1;
    if (on < 0) {
        on = ue::config::read_switch(L"UBWCOOP_C146_NO_START", "card 146 test: a rejoin spawn with nobody Up is "
                                                               "taken as FindPlayerStart's origin fallback") ? 1 : 0;
    }
    if (on == 0 || up_target != nullptr || c43_roster(world).players < 2) {
        return false;
    }
    log::line("card146: host test: player spawn at (%.0f, %.0f, %.0f) with nobody Up taken as the origin fallback "
              "(UBWCOOP_C146_NO_START)", at.x, at.y, at.z);
    return true;
}
} // namespace

// Card 307: host. A non-host player pawn spawning at a free PlayerStart goes beside the Up player too (join, rejoin,
// respawn after a host restart or load), unless: the host's own pawn is the one spawning (the host's slot has no
// pawn: map start, restart, load), nobody is Up (card 146 handles the origin case only), the Up player is within
// kC307KeepUu of the PlayerStart, or the Up player rides a dropship (card 159 snapshot path). Logs a keep row.
constexpr double kC307KeepUu = 1500.0;
bool c307_beside_up(const void* world, const void* target, const Vec3& at) {
    const C43Roster r = c43_roster(world);
    const char* keep = nullptr;
    const void* const parent = target != nullptr ? g_get_attach_parent_actor(target) : nullptr;
    if (r.p[0].pawn == nullptr) {
        keep = "the host's own pawn spawns";
    } else if (target == nullptr) {
        keep = "nobody Up";
    } else if (vec_dist(at, actor_location(target)) <= kC307KeepUu) {
        keep = "within 1500 uu";
    } else if (parent != nullptr && text_contains(class_name(parent).text, "Dropship")) {
        keep = "dropship ride";
    }
    if (keep != nullptr) {
        log::line("card307: host t=%llu player spawn at (%.0f, %.0f, %.0f) kept PlayerStart: %s",
                  static_cast<unsigned long long>(GetTickCount64()), at.x, at.y, at.z, keep);
    }
    return keep == nullptr;
}

// Card 046 (late join): RestartPlayer -> FindPlayerStart_Implementation 0x143C4D000 -> ChoosePlayerStart 0x143C41CE0
// returns null when every PlayerStart is encroached (EncroachingBlockingGeometry true) and FindTeleportSpot finds no
// room; FindPlayerStart then returns the WorldSettings actor (FUN_144342B30), so SpawnDefaultPawnAtTransform spawns
// the player at (0, 0, 0), kilometres above nothing on FED_C2M1 (hb30, hb31: z -22031 before the kill floor warp).
// The host spawns that pawn beside an Up player instead, at a spot FindTeleportSpot clears, before it ever ticks.
// Returns `transform`, or `out` (12 doubles, an FTransform) with the translation moved.
const double* c46_spawn_spot(const void* world, const void* cls, const double* transform, unsigned long long caller,
                             double* out) {
    if (g_client_session || caller != kC46SpawnPawnCall || cls == nullptr) {
        return transform;
    }
    const void* const cdo = read_ptr(cls, kClassDefaultObject);
    if (cdo == nullptr || !is_a(cdo, g_player_character_class())) {
        return transform;
    }
    void* const target = c46_up_target(c43_roster(world), -1);
    const Vec3 at = read_vec3(transform, kTransformTranslation);
    const bool origin = vec_dist(at, Vec3{}) <= kC46OriginUu || c146_test_no_start(world, target, at);
    if (!origin && !c307_beside_up(world, target, at)) {
        return transform;
    }
    void* const w = const_cast<void*>(world);
    c46_probe_starts(w, cdo);
    if (target == nullptr) {
        Vec3 spot{};
        const char* from = "none";
        if (!c46_rescue_spot(w, cdo, c43_roster(world), &spot, &from)) {
            log::line("WARN card046: host player spawn at the origin (no PlayerStart free, FindPlayerStart fell back "
                      "to WorldSettings), no player Up and no PlayerStart: left at the origin");
            return transform;
        }
        memcpy(out, transform, 12 * sizeof(double));
        memcpy(reinterpret_cast<uint8_t*>(out) + kTransformTranslation, &spot, sizeof(spot));
        log::line("card146: host player spawn at the origin with no player Up: spawns at %s (%.0f, %.0f, %.0f) "
                  "instead", from, spot.x, spot.y, spot.z);
        return out;
    }
    const Vec3 t = actor_location(target);
    Vec3 to{};
    if (!c46_free_spot(w, cdo, t, &to)) {
        to = Vec3{t.x + kC46RejoinSideUu, t.y, t.z + kC46WarpUp};
        log::line("WARN card046: host FindTeleportSpot found no room on any side of %s: the rejoined pawn spawns "
                  "%.0f uu above the +x side at (%.0f, %.0f, %.0f)", object_name(target).text, kC46WarpUp, to.x, to.y,
                  to.z);
    }
    memcpy(out, transform, 12 * sizeof(double));
    memcpy(reinterpret_cast<uint8_t*>(out) + kTransformTranslation, &to, sizeof(to));
    log::line("card046: host player spawn %s: spawns beside %s at (%.0f, %.0f, %.0f) instead; PlayerStart at (%.0f, "
              "%.0f, %.0f) %.0f uu from %s",
              origin ? "at the origin (no PlayerStart free, FindPlayerStart fell back to WorldSettings)"
                     : "at the PlayerStart (card 307)",
              object_name(target).text, to.x, to.y, to.z, at.x, at.y, at.z, vec_dist(at, t), object_name(target).text);
    if (!origin) {
        log::line("card307: host t=%llu player spawn: PlayerStart (%.0f, %.0f, %.0f) is %.0f uu from Up player %s: "
                  "spawns beside at (%.0f, %.0f, %.0f)", static_cast<unsigned long long>(GetTickCount64()), at.x, at.y,
                  at.z, vec_dist(at, t), object_name(target).text, to.x, to.y, to.z);
    }
    return out;
}

} // namespace ubw::hooks
