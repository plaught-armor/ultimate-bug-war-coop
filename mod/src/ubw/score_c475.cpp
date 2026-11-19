// Card 475: the client's FedScore after a join or rejoin.
//
// USTFedScoreSubsystem is a world subsystem on each machine. AddFedScore(tag, text, amount) (exe+0x51BC960) adds the
// delta to the int32 total at +0x44, broadcasts OnFedScoreGained (the inline multicast delegate at +0x30: the HUD
// counter, the WB_Fed_Score_Log popup line and USTTacticalSupportComponent::HandleFedScoreUpdate), and, for a tag
// that is not None, appends {FGameplayTag, int32 delta} (12 bytes) to the FedScoreHistory TArray at +0x50, which the
// mission-end list reads (USTFedScoreUtilityLibrary::GetFedscoreHistory). Load (exe+0x51CDAF0) copies the total and
// the history from the save, so the history is the game's own per-tag journal of the total on every path (adds,
// map start, in-place checkpoint load); untagged adds are the total minus the history sum.
//
// Host, at a client's first Pong of the map (the registry's "score" join replay, after "objective"): its history
// summed per tag, plus the untagged rest as tag "-", in ChanOp::Score text frames "R <tag>=<sum>/<count>;..." and
// a closing "E <total> <rows>". The client, per row, applies the host's sum minus its own sum of that tag when
// positive: the total, one history element (not for "-") and OnFedScoreGained to every bound function except the
// WB_Fed_Score_Log popup (any broadcast draws a "+N Reason" line there, empty text included). The objective replay
// frames come first on the same reliable channel and their completes add score synchronously on receipt, so the
// client's own sums already hold them when the rows arrive.

#include "ubw/hooks_internal.h"

#include <algorithm>
#include <stdio.h>
#include <string.h>

namespace ubw::hooks {

FTextGetEmptyFn g_c475_text_empty = nullptr; // FText::GetEmpty

namespace {

constexpr size_t kC475Delegate = 0x30;         // OnFedScoreGained (exe+0x51BCAD5: lea rcx,[rsi+0x30])
constexpr size_t kC475Total = 0x44;            // the total (GetFedScore: mov eax,[rcx+0x44])
constexpr size_t kC475History = 0x50;          // FedScoreHistory TArray {data, Num, Max}
constexpr size_t kC475EntryBytes = 12;         // FSTFedScoreHistoryEntry {FGameplayTag (FName, 8), int32 amount}
constexpr size_t kC475EntryAmount = 8;
constexpr int32_t kC475MaxHistory = 1 << 16;   // history elements read
constexpr int32_t kC475Tags = 96;              // tags per table
constexpr int32_t kC475MaxBound = 32;          // OnFedScoreGained bindings called
constexpr uint16_t kC475Parms = 0x18;          // {int32 FedScoreAmount, pad, FText RewardReason (0x10)}
constexpr size_t kC475ParmText = 0x8;
constexpr size_t kC475TextBytes = 0x10;
constexpr char kC475Untagged[] = "-";          // the wire name of the untagged rest
constexpr char kC475PopupClass[] = "WB_Fed_Score_Log_C";

struct C475Row {
    uint64_t tag;         // FName on this side; 0 for the untagged rest
    char name[kNameChars];
    int64_t sum;
    int32_t count;
};
struct C475Table {
    C475Row row[kC475Tags];
    int32_t n;
    int32_t dropped;      // tags past kC475Tags
    int64_t tagged;       // the history sum
};

// Client: one join replay in progress (between the first row and "E").
struct C475Client {
    bool open;
    int32_t before;       // the total at the first row
    int32_t rows;         // rows received
    int32_t applied;      // rows that added score
    int32_t not_applied;  // rows at or below the client's own sum
    int32_t bad;          // rows that did not parse
    int32_t calls;        // OnFedScoreGained functions called
    int32_t skipped;      // popup bindings skipped
};
C475Client g_c475c{};

C475Table g_c475t{}; // game thread only; filled per call

void* c475_subsystem(const void* world) {
    return world != nullptr && g_c131_score_subsystem != nullptr ? g_c131_score_subsystem(const_cast<void*>(world))
                                                                 : nullptr;
}

int32_t c475_total_of(const void* sub) {
    return read_i32(sub, kC475Total);
}

C475Row* c475_row(C475Table& t, uint64_t tag) {
    for (int32_t i = 0; i < t.n; ++i) {
        if (t.row[i].tag == tag) {
            return &t.row[i];
        }
    }
    if (t.n >= kC475Tags) {
        ++t.dropped;
        return nullptr;
    }
    C475Row& r = t.row[t.n++];
    r = C475Row{};
    r.tag = tag;
    if (tag == 0) {
        snprintf(r.name, sizeof(r.name), "%s", kC475Untagged);
    } else {
        snprintf(r.name, sizeof(r.name), "%s", fname_text(&r.tag).text);
    }
    return &r;
}

// The history of `sub` summed per tag, then the untagged rest (total minus the history sum) as tag 0 when not 0.
void c475_build(const void* sub, C475Table& t) {
    t.n = 0;
    t.dropped = 0;
    t.tagged = 0;
    const auto* const data = static_cast<const uint8_t*>(read_ptr(sub, kC475History));
    const int32_t num = read_i32(sub, kC475History + kArrayNum);
    for (int32_t i = 0; data != nullptr && i < num && i < kC475MaxHistory; ++i) {
        const uint8_t* const e = data + static_cast<size_t>(i) * kC475EntryBytes;
        const int32_t amount = read_i32(e, kC475EntryAmount);
        t.tagged += amount;
        C475Row* const r = c475_row(t, read_u64(e, 0));
        if (r != nullptr) {
            r->sum += amount;
            ++r->count;
        }
    }
    const int64_t rest = static_cast<int64_t>(c475_total_of(sub)) - t.tagged;
    C475Row* const r = rest != 0 ? c475_row(t, 0) : nullptr;
    if (r != nullptr) {
        r->sum += rest;
        r->count = 1;
    }
}

// This side's sum of one tag (0: the untagged rest).
int64_t c475_own_sum(const void* sub, uint64_t tag) {
    c475_build(sub, g_c475t);
    for (int32_t i = 0; i < g_c475t.n; ++i) {
        if (g_c475t.row[i].tag == tag) {
            return g_c475t.row[i].sum;
        }
    }
    return 0;
}

int32_t c475_slot(const void* world, const void* pc) {
    for (int32_t s = 0; world != nullptr && s < kC17Slots; ++s) {
        if (g_get_player_controller(const_cast<void*>(world), s) == pc) {
            return s;
        }
    }
    return -1;
}

// Client: one history element {tag, amount} appended with the game's allocator (as the TArray grows it).
bool c475_append_history(void* sub, uint64_t tag, int32_t amount) {
    auto* const arr = static_cast<uint8_t*>(sub) + kC475History;
    void* data = read_ptr(arr, 0);
    const int32_t num = read_i32(arr, kArrayNum);
    const int32_t max = read_i32(arr, kArrayMax);
    if (num < 0 || num >= kC475MaxHistory || g_memory_realloc == nullptr) {
        return false;
    }
    if (num + 1 > max) {
        const int32_t grown = max * 2 > num + 1 ? max * 2 : num + 16;
        data = g_memory_realloc(data, static_cast<size_t>(grown) * kC475EntryBytes, 0);
        if (data == nullptr) {
            return false;
        }
        memcpy(arr, &data, sizeof(data));
        memcpy(arr + kArrayMax, &grown, sizeof(grown));
    }
    uint8_t* const e = static_cast<uint8_t*>(data) + static_cast<size_t>(num) * kC475EntryBytes;
    memcpy(e, &tag, sizeof(tag));
    memcpy(e + kC475EntryAmount, &amount, sizeof(amount));
    const int32_t n = num + 1;
    memcpy(arr + kArrayNum, &n, sizeof(n));
    return true;
}

// Client: OnFedScoreGained(amount, empty text) to every bound function except the popup log. The bindings are
// copied first (a called function may bind or unbind).
void c475_broadcast(const void* sub, int32_t amount) {
    uint8_t bound[kC475MaxBound][kScriptDelegateBytes];
    const auto* const data = static_cast<const uint8_t*>(read_ptr(sub, kC475Delegate));
    const int32_t num = read_i32(sub, kC475Delegate + kArrayNum);
    int32_t n = 0;
    for (int32_t i = 0; data != nullptr && i < num && n < kC475MaxBound; ++i) {
        memcpy(bound[n++], data + static_cast<size_t>(i) * kScriptDelegateBytes, kScriptDelegateBytes);
    }
    const void* const empty = g_c475_text_empty != nullptr ? g_c475_text_empty() : nullptr;
    for (int32_t i = 0; i < n && empty != nullptr; ++i) {
        void* const target = g_weak_get(bound[i]);
        if (target == nullptr) {
            continue;
        }
        if (strcmp(class_name(target).text, kC475PopupClass) == 0) {
            ++g_c475c.skipped;
            continue;
        }
        void* const fn = g_find_function(target, read_u64(bound[i], kScriptDelegateName));
        const uint16_t parms = fn != nullptr ? *reinterpret_cast<const uint16_t*>(
                                                   static_cast<const uint8_t*>(fn) + kFunctionParmsSize)
                                             : 0;
        if (parms != kC475Parms) {
            log::line("WARN card475: client OnFedScoreGained binding %s (%s).%s not called: parms %u, expected %u",
                      object_name(target).text, class_name(target).text,
                      fname_text(bound[i] + kScriptDelegateName).text, parms, kC475Parms);
            continue;
        }
        // The FText is a bitwise copy of the static empty text, not destroyed here: a callee that keeps it takes
        // its own reference, so the count is unchanged when the call returns.
        alignas(16) uint8_t p[kC475Parms] = {};
        memcpy(p, &amount, sizeof(amount));
        memcpy(p + kC475ParmText, empty, kC475TextBytes);
        const auto process_event = reinterpret_cast<ProcessEventFn>(read_ptr(read_ptr(target, 0), kVtProcessEvent));
        process_event(target, fn, p);
        ++g_c475c.calls;
    }
    c491_pawn_rows(sub, "after client broadcast");
}

// Client: one "tag=sum/count" row.
void c475_apply_row(void* sub, const char* item) {
    char name[kNameChars] = {};
    long long sum = 0;
    int count = 0;
    const char* const eq = strchr(item, '=');
    const size_t len = eq != nullptr ? static_cast<size_t>(eq - item) : 0;
    if (len == 0 || len >= sizeof(name) || sscanf_s(eq + 1, "%lld/%d", &sum, &count) != 2) {
        ++g_c475c.bad;
        return;
    }
    memcpy(name, item, len);
    ++g_c475c.rows;
    uint64_t tag = 0;
    if (strcmp(name, kC475Untagged) != 0) {
        wchar_t wide[kNameChars] = {};
        for (size_t i = 0; i < len; ++i) {
            wide[i] = static_cast<wchar_t>(static_cast<unsigned char>(name[i]));
        }
        if (g_fname_ctor == nullptr) {
            ++g_c475c.bad;
            return;
        }
        g_fname_ctor(&tag, wide, kFNameAdd);
    }
    const int64_t diff = static_cast<int64_t>(sum) - c475_own_sum(sub, tag);
    if (diff <= 0 || diff > INT32_MAX) {
        ++g_c475c.not_applied;
        return;
    }
    const int32_t add = static_cast<int32_t>(diff);
    const int32_t total = c475_total_of(sub) + add;
    memcpy(static_cast<uint8_t*>(sub) + kC475Total, &total, sizeof(total));
    if (tag != 0 && !c475_append_history(sub, tag, add)) {
        log::line("WARN card475: client history element %s %d not appended (total set)", name, add);
    }
    ++g_c475c.applied;
    c475_broadcast(sub, add);
}

void c475_close(const void* sub, const char* text) {
    long long host_total = 0;
    int host_rows = 0;
    if (sscanf_s(text, "E %lld %d", &host_total, &host_rows) != 2) {
        ++g_c475c.bad;
    }
    const int32_t now = c475_total_of(sub);
    c475_build(sub, g_c475t);
    log::line("card475: client t=%llu score %d -> %d (%d rows applied, %d tags; %d at or below the own sum, %d bad, "
              "%d of %d host rows; OnFedScoreGained calls %d, popup bindings skipped %d)",
              static_cast<unsigned long long>(GetTickCount64()), g_c475c.before, now, g_c475c.applied, g_c475t.n,
              g_c475c.not_applied, g_c475c.bad, g_c475c.rows, host_rows, g_c475c.calls, g_c475c.skipped);
    if (static_cast<long long>(now) != host_total) {
        log::line("WARN card475: client t=%llu total %d differs from the host's %lld after the replay",
                  static_cast<unsigned long long>(GetTickCount64()), now, host_total);
    }
    g_c475c = C475Client{};
}

// Card 323: the host's total after the join replay. The host compares its total each tick and sends "T <total>" on
// every change. c423b188a: host-only adds after the rejoin left the client 1000 short in digest rounds 14-15.
// b221: a "T" above the client's own total is written at once; the difference is held as an unbroadcast raise. The
// client's own adds for the same events (objective completes on receipt, mirrored kills) land 9-35 ms after the
// host's send (c245b220a, c244b220a: 9 samples, max 35, p95 35) and broadcast OnFedScoreGained themselves, so within
// kC323SettleMs (max + 45 ms) each own add is taken back out of the raise instead of counting twice. At the settle
// the rest of the raise is broadcast once (host-only adds, no history element: the tag is not on the wire) and a
// total above the host's is set down (no broadcast). The settle starts at the first frame and is never restarted
// (c245b199a: a frame every 100 ms pushed the apply out for about 10 s). The digest reads the host total for at most
// kC323DigestGraceMs past the due time, so a snapshot inside the settle compares the value the settle will hold, and
// a stuck settle still shows as a mismatch.
// b222: a total above the host's at the settle is held one more kC323SettleMs before the set-down. c245b221a: an
// objective complete relay (Sewers_SO3 + Sewers_O1, +33500) was applied 8 ms before the "T" sent in the same host
// tick, the settle in between set the total down 33500 for one frame, and the raise that "T" made was broadcast on
// top of the game's own popup (two popups). The other two set-downs: 1500 back 8 ms later (a kill "T" in flight), and
// 15000 over the host for good (kept: the hold delays it 80 ms).
constexpr ULONGLONG kC323SettleMs = 80;
constexpr ULONGLONG kC323DigestGraceMs = 500;
struct C323Host {
    bool armed;      // last holds this map's total (the first tick of a map takes it without a send)
    int32_t last;
};
C323Host g_c323h{};
struct C323Client {
    void* pc;           // the PC the newest "T" frame came on (cleared at map end)
    int32_t host;       // the newest host total
    ULONGLONG due;      // the settle end (the first pending frame + kC323SettleMs); 0: nothing pending
    ULONGLONG first;    // when the first pending frame arrived
    int32_t frames;     // frames in this settle
    int32_t own_seen;   // the total last seen by this code (after its own writes)
    int32_t raise_left; // written at once, not yet broadcast, not yet covered by an own add
    bool held;          // the set-down was held once for a "T" in flight (b222)
};
C323Client g_c323c{};

void c323_write_total(void* sub, int32_t total) {
    memcpy(static_cast<uint8_t*>(sub) + kC475Total, &total, sizeof(total));
}

void c323_host_step(void* engine) {
    void* const world = engine != nullptr ? g_get_game_world(engine) : nullptr;
    const void* const sub = c475_subsystem(world);
    if (sub == nullptr) {
        return;
    }
    const int32_t total = c475_total_of(sub);
    if (!g_c323h.armed || total == g_c323h.last) {
        g_c323h.armed = true;
        g_c323h.last = total;
        return;
    }
    char text[32];
    snprintf(text, sizeof(text), "T %d", total);
    const C95Fanout f = c95_send_all(world, ChanOp::Score, text);
    log::line("%scard323: host t=%llu FedScore %d -> %d (delta %d) sent to %d of %d clients",
              f.sent == f.to ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), g_c323h.last, total,
              total - g_c323h.last, f.sent, f.to);
    g_c323h.last = total;
}

// An own add while a raise is held: the part the raise already wrote is taken back out.
void c323_absorb(void* sub, ULONGLONG now) {
    const int32_t cur = c475_total_of(sub);
    const int32_t d = cur - g_c323c.own_seen;
    if (d <= 0 || g_c323c.raise_left <= 0) {
        g_c323c.own_seen = cur;
        return;
    }
    const int32_t take = d < g_c323c.raise_left ? d : g_c323c.raise_left;
    g_c323c.raise_left -= take;
    g_c323c.own_seen = cur - take;
    c323_write_total(sub, g_c323c.own_seen);
    log::line("card323: client t=%llu own add %d covered %d of the raise (%d left), %llu ms after the first frame",
              static_cast<unsigned long long>(now), d, take, g_c323c.raise_left,
              static_cast<unsigned long long>(now - g_c323c.first));
}

void c323_client_step(ULONGLONG now) {
    if (g_c323c.due == 0) {
        return;
    }
    void* const sub = g_c323c.pc != nullptr ? c475_subsystem(g_actor_get_world(g_c323c.pc)) : nullptr;
    if (sub != nullptr && c475_total_of(sub) != g_c323c.own_seen) {
        c323_absorb(sub, now);
    }
    if (now < g_c323c.due) {
        return;
    }
    if (sub != nullptr && !g_c323c.held && c475_total_of(sub) > g_c323c.host) {
        g_c323c.held = true;
        g_c323c.due = now + kC323SettleMs;
        log::line("card323: client t=%llu host FedScore %d, own %d: set-down held %llu ms for a frame in flight",
                  static_cast<unsigned long long>(now), g_c323c.host, c475_total_of(sub),
                  static_cast<unsigned long long>(kC323SettleMs));
        return;
    }
    g_c323c.due = 0;
    if (sub == nullptr) {
        log::line("WARN card323: client t=%llu host FedScore %d not settled: no subsystem",
                  static_cast<unsigned long long>(now), g_c323c.host);
        return;
    }
    const int32_t own = c475_total_of(sub);
    const int64_t diff = static_cast<int64_t>(g_c323c.host) - own;
    if (diff != 0) {
        c323_write_total(sub, g_c323c.host);
    }
    if (diff > 0) {
        g_c323c.raise_left += static_cast<int32_t>(diff);
    }
    const int32_t broadcast = g_c323c.raise_left;
    if (broadcast > 0) {
        c475_broadcast(sub, broadcast);
    }
    g_c323c.raise_left = 0;
    log::line("card323: client t=%llu settle: host FedScore %d, own %d: %s; OnFedScoreGained %d; %llu ms after the "
              "first of %d frame(s)",
              static_cast<unsigned long long>(now), g_c323c.host, own,
              diff > 0 ? "raised" : diff < 0 ? "set down" : "equal", broadcast,
              static_cast<unsigned long long>(now - g_c323c.first), g_c323c.frames);
}

} // namespace

// ---- Card 491 step 2 (diag, UBWCOOP_C99_LOG=1): the resupply state of each pawn bound to OnFedScoreGained ----
// After each broadcast (host: AddFedScore(tag, text, amount); client: c475_broadcast), one row per distinct
// BP_PlayerCharacterMI_C binding: own (player controller 0's pawn) or proxy, NetGUID, local role, the
// USTDifficulty::GetCurrentDifficultyLevel the pawn sees, and every own property of the pawn class whose name has
// "Resupply" or "FEDScoreThreshold" (ints and int arrays as values). c245b228a: the client paid one extra
// ResupplyUpgraded inside BrainBug_SO2; these rows show which pawn copy and with which level and thresholds.
namespace {
constexpr uint32_t kC491PawnRows = 1500;    // b238: before and after every award (was 600, after only)
constexpr int32_t kC491Pawns = 8;            // distinct pawns per broadcast
constexpr int32_t kC491ArrayShown = 8;       // threshold elements shown
constexpr char kC491PawnClass[] = "BP_PlayerCharacterMI_C";
// b232: wider than Resupply/FEDScoreThreshold. b254: "Resupply" again (strstr is case-sensitive, so "Supply" missed
// ResupplyLevel and every other Resupply* property from b232 to b253).
constexpr const char* kC491Parts[] = {"Score", "Supply", "Resupply", "Threshold"};
uint32_t g_c491_pawn_rows = 0;               // per map (c475_map_end)

bool c491_wanted(const char* name) {
    for (const char* p : kC491Parts) {
        if (strstr(name, p) != nullptr) {
            return true;
        }
    }
    return false;
}

int64_t c491_int(const uint8_t* at, int32_t size) {
    if (size == 1) {
        return at[0];
    }
    int64_t v = 0;
    memcpy(&v, at, size == 8 ? 8 : 4);
    return size == 8 ? v : static_cast<int64_t>(static_cast<int32_t>(v));
}

// One scalar of `kind` and `size` bytes at `at` as text; -1 for a kind that is not a number.
int c491_scalar(PropKind kind, const uint8_t* at, int32_t size, char* out, size_t cap) {
    if (kind == PropKind::SInt || kind == PropKind::UInt) {
        return snprintf(out, cap, "%lld", static_cast<long long>(c491_int(at, size)));
    }
    if (kind == PropKind::Float) {
        float v = 0.0f;
        memcpy(&v, at, sizeof(v));
        return snprintf(out, cap, "%g", static_cast<double>(v));
    }
    if (kind == PropKind::Double) {
        double v = 0.0;
        memcpy(&v, at, sizeof(v));
        return snprintf(out, cap, "%g", v);
    }
    return -1;
}

// b238: one struct element as "{m=v,...}": its number members (bounded), "{}" when it has none.
int c491_struct_text(const void* st, const uint8_t* at, char* out, size_t cap) {
    int used = snprintf(out, cap, "{");
    const void* f = read_ptr(st, kStructChildProperties);
    int shown = 0;
    for (int i = 0; f != nullptr && i < kPropMaxFields && used > 0 && static_cast<size_t>(used) < cap;
         ++i, f = read_ptr(f, kFieldNext)) {
        const auto* const m = static_cast<const uint8_t*>(f);
        ObjectName mk{};
        const PropKind kind = prop_kind(m, mk);
        char v[40];
        if (c491_scalar(kind, at + read_i32(m, kPropertyOffset), read_i32(m, kPropertyElementSize), v, sizeof(v)) < 0) {
            continue;
        }
        used += snprintf(out + used, cap - used, "%s%s=%s", shown++ == 0 ? "" : ",", fname_text(m + kFieldName).text, v);
    }
    if (used > 0 && static_cast<size_t>(used) < cap) {
        used += snprintf(out + used, cap - used, "}");
    }
    return used;
}

// " name=v" (numbers), " name=n[a,b,...]" (number arrays), " name=n[{m=v,...},...]" (b238: struct arrays), or
// " name=<kind of inner>" for one property at `at`.
int c491_prop_text(const uint8_t* f, const uint8_t* at, char* out, size_t cap) {
    ObjectName k{};
    const PropKind kind = prop_kind(f, k);
    const auto field_name = fname_text(f + kFieldName);
    const char* const name = field_name.text;
    int used = snprintf(out, cap, " %s=", name);
    if (used <= 0 || static_cast<size_t>(used) >= cap) {
        return used;
    }
    const int scalar = c491_scalar(kind, at, read_i32(f, kPropertyElementSize), out + used, cap - used);
    if (scalar >= 0) {
        return used + scalar;
    }
    const uint8_t* const inner = kind == PropKind::Array ? static_cast<const uint8_t*>(read_ptr(f, kArrayPropInner)) : nullptr;
    ObjectName ik{};
    const PropKind inner_kind = inner != nullptr ? prop_kind(inner, ik) : PropKind::Other;
    const int32_t esize = inner != nullptr ? read_i32(inner, kPropertyElementSize) : 0;
    const bool number = inner_kind == PropKind::SInt || inner_kind == PropKind::UInt || inner_kind == PropKind::Float ||
                        inner_kind == PropKind::Double;
    const void* const st = inner_kind == PropKind::Struct ? read_ptr(inner, kStructPropStruct) : nullptr;
    if (!number && st == nullptr) {
        // b232: the inner kind (and struct name) of a non-number array, so a threshold table in structs shows
        return used + snprintf(out + used, cap - used, "<%s%s%s%s>", k.text, inner != nullptr ? " of " : "",
                               inner != nullptr ? ik.text : "", st != nullptr ? object_name(st).text : "");
    }
    const auto* const data = static_cast<const uint8_t*>(read_ptr(at, kArrayData));
    const int32_t num = read_i32(at, kArrayNum);
    used += snprintf(out + used, cap - used, "%d[", num);
    for (int32_t i = 0; data != nullptr && i < num && i < kC491ArrayShown && used > 0 && static_cast<size_t>(used) < cap; ++i) {
        used += snprintf(out + used, cap - used, "%s", i == 0 ? "" : ",");
        const uint8_t* const e = data + static_cast<size_t>(i) * esize;
        const int n = static_cast<size_t>(used) >= cap ? 0
                      : st != nullptr                   ? c491_struct_text(st, e, out + used, cap - used)
                                                        : c491_scalar(inner_kind, e, esize, out + used, cap - used);
        used += n > 0 ? n : 0;
    }
    if (used > 0 && static_cast<size_t>(used) < cap) {
        used += snprintf(out + used, cap - used, "]");
    }
    return used;
}

// Every wanted property of the pawn's class and its supers (b232: a threshold declared on a native super showed none).
void c491_props(const uint8_t* pawn, char* out, size_t cap) {
    out[0] = '\0';
    size_t used = 0;
    const void* cls = read_ptr(pawn, kObjectClass);
    for (int s = 0; cls != nullptr && s < kPropMaxSupers; ++s, cls = read_ptr(cls, kStructSuper)) {
        const void* f = read_ptr(cls, kStructChildProperties);
        for (int i = 0; f != nullptr && i < kPropMaxFields && used + 1 < cap; ++i, f = read_ptr(f, kFieldNext)) {
            const auto* const field = static_cast<const uint8_t*>(f);
            if (!c491_wanted(fname_text(field + kFieldName).text)) {
                continue;
            }
            const int n = c491_prop_text(field, pawn + read_i32(field, kPropertyOffset), out + used, cap - used);
            used = n > 0 ? std::min(cap - 1, used + static_cast<size_t>(n)) : used;
        }
    }
}

void c491_pawn_row(const uint8_t* pawn, const char* what, int32_t total) {
    const void* const world = g_actor_get_world(pawn);
    const void* const pc0 = world != nullptr && g_get_player_controller != nullptr
                                ? g_get_player_controller(const_cast<void*>(world), 0)
                                : nullptr;
    const bool own = pc0 != nullptr && read_ptr(pc0, kControllerPawn) == pawn;
    const void* const d = g_c114_current != nullptr ? g_c114_current(pawn) : nullptr;
    const int level = d != nullptr && g_c491_difficulty_level != nullptr ? g_c491_difficulty_level(d) : -1;
    char props[760]; // log::line holds 1024 bytes
    c491_props(pawn, props, sizeof(props));
    log::line("card491: %s t=%llu %s (total %d): pawn %s %s guid 0x%llX role %u, difficulty %d:%s",
              g_client_session ? "client" : "host", static_cast<unsigned long long>(GetTickCount64()), what, total,
              object_name(pawn).text, own ? "own" : "proxy", static_cast<unsigned long long>(c41_guid(pawn)),
              static_cast<unsigned>(pawn[kActorLocalRole]), level, props[0] != '\0' ? props : " none");
}

// The distinct BP_PlayerCharacterMI_C functions bound to `sub`'s OnFedScoreGained (every pawn copy on this side), at
// most kC491Pawns; returns the count.
int32_t c491_bound_pawns(const void* sub, const uint8_t* (&out)[kC491Pawns]) {
    int32_t n = 0;
    if (sub == nullptr || g_weak_get == nullptr) {
        return 0;
    }
    const auto* const data = static_cast<const uint8_t*>(read_ptr(sub, kC475Delegate));
    const int32_t num = read_i32(sub, kC475Delegate + kArrayNum);
    for (int32_t i = 0; data != nullptr && i < num && i < kC475MaxBound && n < kC491Pawns; ++i) {
        const auto* const target =
            static_cast<const uint8_t*>(g_weak_get(data + static_cast<size_t>(i) * kScriptDelegateBytes));
        if (target == nullptr || strcmp(class_name(target).text, kC491PawnClass) != 0 ||
            std::find(out, out + n, target) != out + n) {
            continue;
        }
        out[n++] = target;
    }
    return n;
}
} // namespace

void c491_pawn_rows(const void* sub, const char* what) {
    if (!g_c99_log) {
        return;
    }
    const uint8_t* pawns[kC491Pawns] = {};
    const int32_t n = c491_bound_pawns(sub, pawns);
    for (int32_t i = 0; i < n; ++i) {
        if (note_event_cap(g_c491_pawn_rows, kC491PawnRows, "card491 pawn rows")) {
            c491_pawn_row(pawns[i], what, c475_total_of(sub));
        }
    }
}

// Card 491 b260 (c245b259a): the host pays every ResupplyUpgraded.
//
// BP_PlayerCharacterMI_C's FedScoreGained (bound to OnFedScoreGained on every pawn copy on every machine) compares the
// total with its own FEDScoreThresholdsArray[ResupplyLevel]; on a crossing it steps ResupplyLevel (a Blueprint int
// with no native symbol: its offset is fixed when the class links, so it is found by name), plays the upgrade sound,
// steps the tier and calls AddFedScore(ResupplyUpgraded) for +15000. Each copy holds its own level, so a copy behind
// the other machine's paid where that one did not (c245b259a: SO2 the client's proxy 0x14 at 0, SO3 the host's
// rejoin proxy 0xDB2 at 0). The host's awards are the score:
// - client: every native AddFedScore tagged ResupplyUpgraded is dropped, one row each;
// - host: after each one, "U <guid hex> <level> 1" to every client: the client sets that pawn's ResupplyLevel and
//   adds the award through the original AddFedScore (total, history, broadcast, popup line), not through the detour;
// - the registry's "resupply" join replay (before "objective"): "U <guid> <level> 0" per bound pawn, levels only, so
//   the objective replay's score does not step a client copy the host already stepped.
// The host's frames go on the same reliable channel as the "T" totals (card 323), after the award they carry.
namespace {
constexpr char kC491ResupplyTag[] = "FedScoreSystem.Reward.ResupplyUpgraded";
constexpr char kC491LevelName[] = "ResupplyLevel";
constexpr uint32_t kC491ResupplyRows = 400; // per map, per side

struct C491Resupply {
    const void* cls;   // the class `offset` was resolved on this map; null: not resolved yet
    int32_t offset;    // ResupplyLevel, -1 when the class has none
    int32_t paid;      // host: ResupplyUpgraded awards sent with "U ... 1"
    int32_t dropped;   // client: native ResupplyUpgraded calls dropped
    int32_t applied;   // client: "U ... 1" awards added
    uint32_t rows;     // log budget
};
constexpr C491Resupply kC491ResupplyNone{nullptr, -1, 0, 0, 0, 0};
C491Resupply g_c491r = kC491ResupplyNone;

// b262, client: each bound pawn's last seen ResupplyLevel, so a change no "U" row made logs once, with its time
// (c245b260a: proxy 0x14 went 1 -> 0 between two awards, by no Blueprint write).
struct C491Watch {
    const void* pawn[kC491Pawns];
    int32_t level[kC491Pawns];
    int32_t n;
    int32_t refused; // FedScoreGained calls refused (c491_pe)
};
C491Watch g_c491w{};
} // namespace
// Game thread: set just before AddFedScore(tag)'s original runs; its first AddFedScoreText (the same award) clears it
// and passes the detour (c245b260a: paid twice on the host). One shot, not a depth: an award a Blueprint makes inside
// the broadcast is a new one (c245b261a: a depth gate passed the step's ResupplyUpgraded, host paid 0).
bool g_c491_inner_text = false;
namespace {
uint64_t g_c491_resupply_fname = 0; // the tag's FName, made once (FNames live for the process)

bool c491_is_resupply(uint64_t tag) {
    return strcmp(fname_text(&tag).text, kC491ResupplyTag) == 0;
}

uint64_t c491_resupply_fname() {
    if (g_c491_resupply_fname == 0 && g_fname_ctor != nullptr) {
        wchar_t wide[sizeof(kC491ResupplyTag)] = {};
        for (size_t i = 0; i + 1 < sizeof(kC491ResupplyTag); ++i) {
            wide[i] = static_cast<wchar_t>(kC491ResupplyTag[i]);
        }
        g_fname_ctor(&g_c491_resupply_fname, wide, kFNameAdd);
    }
    return g_c491_resupply_fname;
}

// ResupplyLevel's offset on `pawn`'s class or a super, -1 when none; resolved once per map, one row.
int32_t c491_level_offset(const uint8_t* pawn) {
    const void* const cls = pawn != nullptr ? read_ptr(pawn, kObjectClass) : nullptr;
    if (cls == nullptr || strcmp(class_name(pawn).text, kC491PawnClass) != 0) {
        return -1;
    }
    if (cls == g_c491r.cls) {
        return g_c491r.offset;
    }
    int32_t off = -1;
    const void* c = cls;
    for (int s = 0; c != nullptr && s < kPropMaxSupers && off < 0; ++s, c = read_ptr(c, kStructSuper)) {
        const void* f = read_ptr(c, kStructChildProperties);
        for (int i = 0; f != nullptr && i < kPropMaxFields && off < 0; ++i, f = read_ptr(f, kFieldNext)) {
            const auto* const field = static_cast<const uint8_t*>(f);
            ObjectName k{};
            const bool match = strcmp(fname_text(field + kFieldName).text, kC491LevelName) == 0 &&
                               prop_kind(field, k) == PropKind::SInt && read_i32(field, kPropertyElementSize) == 4;
            off = match ? read_i32(field, kPropertyOffset) : -1;
        }
    }
    g_c491r.cls = cls;
    g_c491r.offset = off;
    log::line("%scard491: %s t=%llu ResupplyLevel offset 0x%X on %s%s", off < 0 ? "WARN " : "",
              g_client_session ? "client" : "host", static_cast<unsigned long long>(GetTickCount64()),
              static_cast<unsigned>(off), kC491PawnClass, off < 0 ? " not found (no level sync this map)" : "");
    return off;
}

int32_t c491_level_of(const uint8_t* pawn) {
    const int32_t off = c491_level_offset(pawn);
    return off >= 0 ? read_i32(pawn, off) : -1;
}

// The innermost Blueprint caller when it is a player pawn copy, else null.
const uint8_t* c491_caller_pawn() {
    const auto* const p = static_cast<const uint8_t*>(g_c322_bp_caller);
    return p != nullptr && strcmp(class_name(p).text, kC491PawnClass) == 0 ? p : nullptr;
}

// The watch entry of `pawn` set to `level` (a write the mod made, or a first sight); added when absent.
void c491_watch_set(const void* pawn, int32_t level) {
    for (int32_t i = 0; i < g_c491w.n; ++i) {
        if (g_c491w.pawn[i] == pawn) {
            g_c491w.level[i] = level;
            return;
        }
    }
    if (g_c491w.n < kC491Pawns) {
        g_c491w.pawn[g_c491w.n] = pawn;
        g_c491w.level[g_c491w.n] = level;
        ++g_c491w.n;
    }
}

constexpr char kC491PromptClass[] =
    "/Game/Blueprints/UI_Aloft/Gameplay_HUD/WB_Tactical_Support_Prompt_Default_v2.WB_Tactical_Support_Prompt_Default_v2_C";
constexpr int32_t kC491Levels = 3;      // ResupplyLevel 0..3 (the Blueprint steps while below 3)
constexpr size_t kC491ParmsBytes = 64;  // GetAllWidgetsOfClass parms (context 8, array 16, class 8, bool)
constexpr size_t kC491ArrayBytes = 16;  // TArray: data, num, max

// The parameter `name`'s offset in `fn`'s parms, -1 when absent or past kC491ParmsBytes.
int32_t c491_parm(const void* fn, const char* name, size_t bytes) {
    const uint8_t* const p = prop_find_prop(fn, name);
    const int32_t off = p != nullptr ? read_i32(p, kPropertyOffset) : -1;
    return off >= 0 && static_cast<size_t>(off) + bytes <= kC491ParmsBytes ? off : -1;
}

// Client, a "U ... 1" row for the own pawn: what the Blueprint's step does on the own screen, the level write aside
// (the row made it): Upgrade() on each WB_Tactical_Support_Prompt_Default_v2 (UWidgetBlueprintLibrary::
// GetAllWidgetsOfClass through ProcessEvent on its CDO, as the Blueprint calls it) and the level's Supply_Upgrade sound
// through the original PlaySound2D. Returns the widgets upgraded, -1 when the lookup failed; `played` the sound.
int32_t c491_own_step(void* pawn, int32_t level, bool* played) {
    *played = false;
    if (level >= 1 && level <= kC491Levels && g_sound_base_class != nullptr && g_orig_c99_sound_2d != nullptr) {
        char path[kAssetPathChars];
        snprintf(path, sizeof(path),
                 "/Game/Audio/Assets/UI/FED/MS_UI_FED_FedScore_Supply_Upgrade_0%d.MS_UI_FED_FedScore_Supply_Upgrade_0%d",
                 level, level);
        void* const sound = c74_find_asset(path, g_sound_base_class());
        if (sound != nullptr) {
            g_orig_c99_sound_2d(pawn, sound, 1.0f, 1.0f, 0.0f, nullptr, pawn, false);
            *played = true;
        }
    }
    static void* cdo = nullptr; // the CDO lives for the process
    static uint64_t get_name = 0;
    static uint64_t upgrade_name = 0;
    cdo = cdo != nullptr ? cdo : g_static_find_object(nullptr, nullptr, L"/Script/UMG.Default__WidgetBlueprintLibrary", false);
    void* const get = cdo != nullptr && fname_cached(get_name, L"GetAllWidgetsOfClass") != 0
                          ? g_find_function(cdo, get_name)
                          : nullptr;
    void* const cls = g_uclass_class != nullptr ? c74_find_asset(kC491PromptClass, g_uclass_class()) : nullptr;
    const int32_t o_ctx = get != nullptr ? c491_parm(get, "WorldContextObject", sizeof(void*)) : -1;
    const int32_t o_found = get != nullptr ? c491_parm(get, "FoundWidgets", kC491ArrayBytes) : -1;
    const int32_t o_cls = get != nullptr ? c491_parm(get, "WidgetClass", sizeof(void*)) : -1;
    const int32_t o_top = get != nullptr ? c491_parm(get, "TopLevelOnly", 1) : -1;
    if (cls == nullptr || o_ctx < 0 || o_found < 0 || o_cls < 0 || o_top < 0 || g_orig_process_event == nullptr) {
        return -1;
    }
    alignas(16) uint8_t parms[kC491ParmsBytes] = {};
    memcpy(parms + o_ctx, &pawn, sizeof(pawn));
    memcpy(parms + o_cls, &cls, sizeof(cls));
    parms[o_top] = 0; // nested widgets too (the prompt sits inside the HUD)
    g_orig_process_event(cdo, get, parms);
    void* const* const data = static_cast<void* const*>(read_ptr(parms, static_cast<size_t>(o_found) + kArrayData));
    const int32_t num = read_i32(parms, static_cast<size_t>(o_found) + kArrayNum);
    int32_t upgraded = 0;
    for (int32_t i = 0; data != nullptr && i < num && i < kC491Pawns; ++i) {
        void* const up = data[i] != nullptr && fname_cached(upgrade_name, L"Upgrade") != 0
                             ? g_find_function(data[i], upgrade_name)
                             : nullptr;
        if (up != nullptr) {
            alignas(16) uint8_t none[kC491ParmsBytes] = {};
            g_orig_process_event(data[i], up, none);
            ++upgraded;
        }
    }
    if (data != nullptr && g_memory_free != nullptr) {
        g_memory_free(const_cast<void**>(data)); // the array the call filled is ours
    }
    return upgraded;
}

// Client, ChanOp::Score "U <guid hex> <level> <award 0|1>".
void c491_resupply_apply(void* pc, void* sub, const char* text) {
    unsigned long long guid = 0;
    int level = -1;
    int award = 0;
    if (sscanf_s(text, "U %llx %d %d", &guid, &level, &award) != 3 || award < 0 || award > 1) {
        log::line("WARN card491: client resupply frame '%s' did not parse", text);
        return;
    }
    auto* const pawn = guid != 0 ? static_cast<uint8_t*>(c41_resolve_player(pc, guid)) : nullptr;
    const int32_t off = pawn != nullptr ? c491_level_offset(pawn) : -1;
    const int32_t was = off >= 0 ? read_i32(pawn, off) : -1;
    const bool set = off >= 0 && level >= 0;
    if (set) {
        const int32_t v = level;
        memcpy(pawn + off, &v, sizeof(v));
        c491_watch_set(pawn, v);
    }
    // b262: the own pawn's step effects on an award row only (a join replay row, award 0, sets the level silently)
    const bool own = set && award == 1 && pc != nullptr && read_ptr(pc, kControllerPawn) == pawn;
    bool played = false;
    const int32_t upgraded = own ? c491_own_step(pawn, level, &played) : -2;
    const int32_t before = c475_total_of(sub);
    const uint64_t tag = award == 1 ? c491_resupply_fname() : 0;
    const bool paid = tag != 0 && g_orig_c99_score != nullptr;
    if (paid) {
        ++g_c491r.applied;
        g_c491_inner_text = true; // its inner AddFedScoreText passes the detour (b262)
        g_orig_c99_score(sub, tag); // the original: its pawn Blueprints' own ResupplyUpgraded calls are dropped
        g_c491_inner_text = false;
    }
    const bool ok = (level < 0 || set) && (award == 0 || paid) && (!own || (played && upgraded >= 0));
    if (!ok || note_event_cap(g_c491r.rows, kC491ResupplyRows, "card491 resupply rows")) {
        log::line("%scard491: client t=%llu U pawn %s guid 0x%llX level %d -> %d%s; award %d (applied %d), total %d -> "
                  "%d; own step %s (sound %s, prompts upgraded %d)", ok ? "" : "WARN ",
                  static_cast<unsigned long long>(GetTickCount64()),
                  pawn != nullptr ? object_name(pawn).text : "unresolved", guid, was, level, set ? "" : " not set",
                  award, g_c491r.applied, before, c475_total_of(sub), own ? "run" : "none",
                  played ? "played" : "none", upgraded);
    }
}
} // namespace

// Both sides, both AddFedScore detours: ResupplyLevel's offset once per map, before the first award runs.
void c491_resupply_arm(const void* sub) {
    if (g_c491r.cls != nullptr) {
        return;
    }
    const uint8_t* pawns[kC491Pawns] = {};
    if (c491_bound_pawns(sub, pawns) > 0) {
        c491_level_offset(pawns[0]);
    }
}

// Client, both AddFedScore detours: true for a native ResupplyUpgraded, which the caller drops.
bool c491_resupply_drop(const void* sub, uint64_t tag) {
    if (!g_client_session || !c491_is_resupply(tag)) {
        return false;
    }
    ++g_c491r.dropped;
    const uint8_t* const pawn = c491_caller_pawn();
    if (note_event_cap(g_c491r.rows, kC491ResupplyRows, "card491 resupply rows")) {
        log::line("card491: client t=%llu suppressed ResupplyUpgraded #%d: pawn %s guid 0x%llX level %d, total %d",
                  static_cast<unsigned long long>(GetTickCount64()), g_c491r.dropped,
                  pawn != nullptr ? object_name(pawn).text : "none",
                  static_cast<unsigned long long>(pawn != nullptr ? c41_guid(pawn) : 0),
                  pawn != nullptr ? c491_level_of(pawn) : -1, c475_total_of(sub));
    }
    return true;
}

// Card 491 b262, client, every ProcessEvent (detour_process_event): true refuses a player pawn's FedScoreGained, the
// Blueprint's own resupply step (ubergraph @7554: level + 1, AddFedScore(ResupplyUpgraded), Upgrade() on the support
// prompts, the Supply_Upgrade sound). ReceiveBeginPlay binds it on every pawn, proxies included, so a client stepped
// on its own score (c245b260a FAIL 2); the host's "U" row runs the step (c491_resupply_apply). A pawn's
// ReceiveBeginPlay logs its level before the Blueprint runs. Two FName compares per call; the class only on a match.
bool c491_pe(void* object, const void* function) {
    static uint64_t gained_name = 0;
    static uint64_t begin_name = 0;
    const uint64_t name = read_u64(function, kObjectName);
    const bool gained = name == fname_cached(gained_name, L"FedScoreGained") && gained_name != 0;
    const bool begin = !gained && name == fname_cached(begin_name, L"ReceiveBeginPlay") && begin_name != 0;
    if ((!gained && !begin) || strcmp(class_name(object).text, kC491PawnClass) != 0) {
        return false;
    }
    const auto* const pawn = static_cast<const uint8_t*>(object);
    g_c491w.refused += gained ? 1 : 0;
    if (note_event_cap(g_c491r.rows, kC491ResupplyRows, "card491 resupply rows")) {
        log::line("card491: client t=%llu %s pawn %s guid 0x%llX level %d (refused %d)",
                  static_cast<unsigned long long>(GetTickCount64()),
                  gained ? "refused FedScoreGained on" : "ReceiveBeginPlay of", object_name(pawn).text,
                  static_cast<unsigned long long>(c41_guid(pawn)), c491_level_of(pawn), g_c491w.refused);
    }
    return gained;
}

// Card 491 b262, client, every tick: a bound pawn's ResupplyLevel change that no "U" row made, one row each.
void c491_level_watch(const void* sub) {
    const uint8_t* pawns[kC491Pawns] = {};
    const int32_t n = g_c491r.cls != nullptr ? c491_bound_pawns(sub, pawns) : 0;
    for (int32_t i = 0; i < n; ++i) {
        const int32_t level = c491_level_of(pawns[i]);
        int32_t k = 0;
        for (; k < g_c491w.n && g_c491w.pawn[k] != pawns[i]; ++k) {
        }
        if (k == g_c491w.n) {
            c491_watch_set(pawns[i], level); // first sight
            continue;
        }
        if (g_c491w.level[k] != level) {
            log::line("WARN card491: client t=%llu pawn %s guid 0x%llX ResupplyLevel %d -> %d, not by a U row",
                      static_cast<unsigned long long>(GetTickCount64()), object_name(pawns[i]).text,
                      static_cast<unsigned long long>(c41_guid(pawns[i])), g_c491w.level[k], level);
            g_c491w.level[k] = level;
        }
    }
}

// Host, both AddFedScore detours after the original: a ResupplyUpgraded award and its pawn's level to every client.
void c491_resupply_paid(const void* sub, uint64_t tag) {
    if (g_client_session || sub == nullptr || !c491_is_resupply(tag)) {
        return;
    }
    ++g_c491r.paid;
    const uint8_t* const pawn = c491_caller_pawn();
    const uint64_t guid = pawn != nullptr ? c41_guid(pawn) : 0;
    const int32_t level = pawn != nullptr ? c491_level_of(pawn) : -1;
    char text[64];
    snprintf(text, sizeof(text), "U %llX %d 1", static_cast<unsigned long long>(guid), level);
    const C95Fanout f = c95_send_all(read_ptr(sub, kObjectOuter), ChanOp::Score, text); // a world subsystem: outer world
    const bool ok = f.sent == f.to && guid != 0 && level >= 0;
    if (!ok || note_event_cap(g_c491r.rows, kC491ResupplyRows, "card491 resupply rows")) {
        log::line("%scard491: host t=%llu ResupplyUpgraded #%d: pawn %s guid 0x%llX level %d, total %d; U sent to %d of "
                  "%d clients", ok ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), g_c491r.paid,
                  pawn != nullptr ? object_name(pawn).text : "none", static_cast<unsigned long long>(guid), level,
                  c475_total_of(sub), f.sent, f.to);
    }
}

// Host, the registry's "resupply" join replay (before "objective"): every bound pawn's level to one PC.
void c491_resupply_join_replay(void* pc) {
    if (g_client_session || pc == nullptr || sprint_side(pc) != kSideServerCopy) {
        return;
    }
    const void* const sub = c475_subsystem(g_actor_get_world(pc));
    const uint8_t* pawns[kC491Pawns] = {};
    const int32_t n = c491_bound_pawns(sub, pawns);
    char list[kC491Pawns * 28 + 1] = {};
    size_t used = 0;
    int32_t sent = 0;
    for (int32_t i = 0; i < n; ++i) {
        const uint64_t guid = c41_guid(pawns[i]);
        const int32_t level = c491_level_of(pawns[i]);
        char text[64];
        snprintf(text, sizeof(text), "U %llX %d 0", static_cast<unsigned long long>(guid), level);
        const bool ok = guid != 0 && level >= 0 && c95_send_one(pc, ChanOp::Score, text);
        sent += ok ? 1 : 0;
        const int w = snprintf(list + used, sizeof(list) - used, " 0x%llX=%d%s", static_cast<unsigned long long>(guid),
                               level, ok ? "" : "(not sent)");
        used = w > 0 ? std::min(sizeof(list) - 1, used + static_cast<size_t>(w)) : used;
    }
    log::line("%scard491: host t=%llu resupply join replay to %s: %d of %d pawn levels sent:%s",
              sent == n ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), object_name(pc).text, sent, n,
              n > 0 ? list : " none bound");
}

// Host, the registry's "score" join replay (after "objective").
void c475_join_replay(void* pc) {
    if (g_client_session || pc == nullptr || sprint_side(pc) != kSideServerCopy) {
        return;
    }
    const void* const world = g_actor_get_world(pc);
    const void* const sub = c475_subsystem(world);
    if (sub == nullptr) {
        log::line("WARN card475: host t=%llu no FedScore subsystem, no score rows to %s",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(pc).text);
        return;
    }
    c475_build(sub, g_c475t);
    char text[kChanTextMax + 1];
    size_t len = 0;
    int32_t frames = 0;
    bool ok = true;
    for (int32_t i = 0; i <= g_c475t.n && ok; ++i) {
        char item[kNameChars + 40] = {};
        const int w = i < g_c475t.n ? snprintf(item, sizeof(item), "%s=%lld/%d;", g_c475t.row[i].name,
                                               static_cast<long long>(g_c475t.row[i].sum), g_c475t.row[i].count)
                                    : 0;
        if (len > 0 && (i == g_c475t.n || len + static_cast<size_t>(w) >= static_cast<size_t>(kChanTextMax))) {
            ok = c95_send_one(pc, ChanOp::Score, text);
            ++frames;
            len = 0;
        }
        if (w > 0 && static_cast<size_t>(w) < sizeof(item)) {
            len += static_cast<size_t>(snprintf(text + len, sizeof(text) - len, "%s%s", len == 0 ? "R " : "", item));
        }
    }
    const int32_t total = c475_total_of(sub);
    snprintf(text, sizeof(text), "E %d %d", total, g_c475t.n);
    ok = ok && c95_send_one(pc, ChanOp::Score, text);
    log::line("%scard475: host t=%llu score rows %d, total %d sent to slot %d (%s, %d frames; history sum %lld, %d tags "
              "dropped)",
              ok ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), g_c475t.n, total,
              c475_slot(world, pc), object_name(pc).text, frames + 1, static_cast<long long>(g_c475t.tagged),
              g_c475t.dropped);
}

// Client: ChanOp::Score from the host.
void c475_receive(void* pc, const WireString* s) {
    static char text[kChanTextMax + 1];
    void* const sub = pc != nullptr ? c475_subsystem(g_actor_get_world(pc)) : nullptr;
    if (chan_text(s, text, sizeof(text)) <= 0 || sub == nullptr) {
        log::line("WARN card475: client t=%llu score frame dropped (subsystem %d)",
                  static_cast<unsigned long long>(GetTickCount64()), sub != nullptr ? 1 : 0);
        return;
    }
    if (text[0] == 'U') {
        c491_resupply_apply(pc, sub, text); // card 491 b260
        return;
    }
    if (text[0] == 'T') {
        long long host_total = 0;
        if (sscanf_s(text, "T %lld", &host_total) != 1 || host_total < INT32_MIN || host_total > INT32_MAX) {
            log::line("WARN card323: client score frame '%s' did not parse", text);
            return;
        }
        const ULONGLONG now = GetTickCount64();
        if (g_c323c.due == 0) {
            g_c323c.due = now + kC323SettleMs;
            g_c323c.first = now;
            g_c323c.frames = 0;
            g_c323c.raise_left = 0;
            g_c323c.held = false;
        }
        g_c323c.pc = pc;
        g_c323c.host = static_cast<int32_t>(host_total);
        ++g_c323c.frames;
        const int32_t own = c475_total_of(sub);
        if (g_c323c.host > own) {
            c323_write_total(sub, g_c323c.host);
            g_c323c.raise_left += g_c323c.host - own;
            log::line("card323: client t=%llu host FedScore %d, own %d: raised at once (%d held for the settle)",
                      static_cast<unsigned long long>(now), g_c323c.host, own, g_c323c.raise_left);
        }
        g_c323c.own_seen = c475_total_of(sub);
        return;
    }
    if (!g_c475c.open) {
        g_c475c = C475Client{};
        g_c475c.open = true;
        g_c475c.before = c475_total_of(sub);
    }
    if (text[0] == 'E') {
        c475_close(sub, text);
        return;
    }
    if (text[0] != 'R' || text[1] != ' ') {
        ++g_c475c.bad;
        return;
    }
    char* next = nullptr;
    char* item = strtok_s(text + 2, ";", &next);
    for (int32_t guard = 0; item != nullptr && guard < kC475Tags; ++guard) {
        c475_apply_row(sub, item);
        item = strtok_s(nullptr, ";", &next);
    }
}

// Both sides, map end: a replay cut by the map change is dropped.
void c475_map_end(void*) {
    if (g_c491r.paid + g_c491r.dropped + g_c491r.applied != 0) {
        log::line("card491: %s t=%llu map end: ResupplyUpgraded paid %d (host), dropped %d, applied %d (client)",
                  g_client_session ? "client" : "host", static_cast<unsigned long long>(GetTickCount64()), g_c491r.paid,
                  g_c491r.dropped, g_c491r.applied);
    }
    g_c491r = kC491ResupplyNone; // card 491 b260: the "resupply" join replay has no map end of its own
    g_c491w = C491Watch{};        // b262
    g_c475c = C475Client{};
    g_c323h = C323Host{};
    g_c323c = C323Client{};
    g_c491_pawn_rows = 0;
}

// Card 323, both sides, every tick: the host sends its total on each change; the client applies a settled one.
void c475_step(void* engine) {
    if (g_client_session) {
        c323_client_step(GetTickCount64());
        void* const world = engine != nullptr ? g_get_game_world(engine) : nullptr;
        c491_level_watch(c475_subsystem(world)); // b262
    } else {
        c323_host_step(engine);
    }
}

// The digest's "Score:" value: this side's total in `world`, or -1. Client: the pending host total while a "T" waits
// for its apply, up to kC323DigestGraceMs past the due time.
int32_t c475_digest_total(const void* world) {
    const void* const sub = c475_subsystem(world);
    if (sub == nullptr) {
        return -1;
    }
    const bool pending = g_client_session && g_c323c.due != 0 &&
                         GetTickCount64() < g_c323c.due + kC323DigestGraceMs;
    return pending ? g_c323c.host : c475_total_of(sub);
}

} // namespace ubw::hooks
