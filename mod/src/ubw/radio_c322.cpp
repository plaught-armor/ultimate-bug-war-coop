// Card 322 step 1 (diagnostic, log only, both sides): which mission radio, music, time dilation and out-of-bounds
// starts each process runs on its own. No mod code touches these; the client runs its own level script copy
// (card 280), so the client may start a line once, twice, late or never.
//
// One row per call:
//   card322: <side> t=<ms> <conversation|music|dilation|out of bounds> <name>, caller <caller>
// The name is the conversation data class (TSubclassOf<USTConversationData>, the first element for a list), the music
// trigger type value (ESTMusicTriggerType, a uint8: the PDB has no enum names), the dilation handle index, or the
// out-of-bounds component owner's class.
//
// The native functions take no caller object, so the Blueprint thunks (exec*) of the same functions are hooked too:
// each stores FFrame::Object (the calling Blueprint object) for the duration of its call, and the native row names
// that object's class. A call with no thunk around it is native (TickMusicModeManager, TimeDilationStart, ...): the
// row names `native exe+0x..` (the return address). The music manager's own tick calls TriggerMusic often, so rows
// are capped per map (kC322Rows) and the map-end tally counts every call by kind and name. The step 1 rows print
// only under the diag gate (g_gate_c62_diag); the conversation hooks themselves are always on (part A).
//
// Card 322 part A (always on, both sides): the host owns every conversation start.
// - Host: StartConversationFromList picks the index itself (min(num - 1, pick)), writes the picked class to data[0]
//   and sets the count to 1, so the original's own pick is index 0 (the original still frees the data). Each
//   start or queue is sent as ChanOp::Radio `<start|queue> <class path> <a> <b> <c>` to every remote PC.
// - Client: its own script's calls of the three are skipped (the list call frees the list's data as the original
//   would). The relay finds the class by full path (find only, no load) and calls the original StartConversation
//   or AddConversationToQueue on the client's subsystem, taken through USubsystemBlueprintLibrary (ProcessEvent on
//   its default object: GetWorldSubsystem, then GetGameInstanceSubsystem).
// - Map end, client: a skipped name with no relay of the same name on that map is a WARN (a client-only line lost).
//   A skipped list counts a skip for every element in one group; a relay of any element clears the group (the host
//   picked one variant).
// Rows:
//   card322: host t=<ms> radio <start|queue> <class> (picked <i> of <n>) sent to <k>
//   card322: client t=<ms> radio <start|queue> <class> started (relay)
//   card322: client t=<ms> radio <class> own start skipped, caller <class>
//   card322: client t=<ms> radio <first class> (list of <n>) own start skipped, caller <class>
//   card322: client t=<ms> radio subsystem from <GetWorldSubsystem|GetGameInstanceSubsystem> (once per map)
//
// Card 360 (always on, both sides): the out-of-bounds expiry kill. OnOutOfBoundsEntered sets a one-shot timer (handle
// component+0xC8, delay USTOutOfBoundsSettings+0x38) bound through the vcall thunk to KillOwningActor (vtable +0x4E0),
// which InstaKills the owning pawn. Every process runs its own overlap (card 322 part B), so without a guard a client
// kills its own copy of a pawn in a volume by itself. The host's kill of its own pawn and of a server copy reaches the
// client by the player death sync (HandleDeathEvent, c43_death, the Down frame), so a client never kills a player
// (card 361 skips it at InstaKillTarget).
// Rows (capped per map, kC360Rows):
//   card360: <side> t=<ms> out of bounds expiry on <pawn> (<side>): KillOwningActor runs
//   (card 361: since b46 the client skip is the InstaKillTarget detour's, row "card361: client ... by KillOwningActor")
//   card360: <side> t=<ms> out of bounds countdown <s> s on <pawn> (<side>)   (diag only, after Entered)

#include "ubw/hooks_internal.h"

#include <intrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ubw::hooks {

C322StartConversationFn g_orig_c322_start_conversation = nullptr;
C322AddConversationFn g_orig_c322_add_conversation = nullptr;
C322StartFromListFn g_orig_c322_start_from_list = nullptr;
C322TriggerMusicFn g_orig_c322_trigger_music = nullptr;
C322PlayDilationFn g_orig_c322_play_dilation = nullptr;
C322ThisFn g_orig_c322_dilation_start = nullptr;
C322ThisFn g_orig_c322_oob_entered = nullptr;
C322ThisFn g_orig_c322_oob_exited = nullptr;
C322ExecFn g_orig_c322_exec[kC322Execs] = {};
const void* g_c322_bp_caller = nullptr;
C322ThisFn g_orig_c360_kill_owning_actor = nullptr;
C360TimeLeftFn g_c360_time_left = nullptr;

namespace {

constexpr uint32_t kC322Rows = 200;       // card322 rows per map, per side
constexpr uint32_t kC360Rows = 64;        // card360 rows per map, per side
uint32_t g_c360_rows = 0;                 // card360 rows this map
constexpr int32_t kC322Names = 96;        // distinct (kind, name) pairs tallied per map
constexpr size_t kC322NameChars = 96;     // a tallied name, truncated
constexpr size_t kTArrayData = 0x0;       // TArray::AllocatorInstance data pointer
constexpr size_t kTArrayNum = 0x8;        // TArray::ArrayNum
constexpr size_t kDilationHandleIndex = 0x8; // FADTimeDilationHandle index (PlayTimeDilation+: movq 0x8(%rdx),%r8)

struct C322Tally {
    char kind[16];
    char name[kC322NameChars];
    uint32_t calls;
    ULONGLONG first_ms;
};

constexpr int32_t kC322Radios = 32;       // distinct radio names with a client skip or relay per map
constexpr uint16_t kC322LibParms = 32;    // (UObject*, TSubclassOf, UObject* return): 24 bytes, padded

// Client, per map: own-script skips and relays by conversation class name (the map-end lost-line check).
struct C322Radio {
    char name[kC322NameChars];
    uint32_t skips, relays;
    int32_t group; // client list skip: the rows of one skipped list share it, and a relay of any clears all (0: none)
};

struct C322Map {
    uint32_t rows;
    int32_t radio_n;
    int32_t group_n; // list skip groups handed out this map
    bool subsystem_logged; // client: the subsystem source row printed this map
    C322Radio radio[kC322Radios];
    int32_t n;
    uint32_t dropped; // calls whose name did not fit the tally
    bool music_any;   // a TriggerMusic seen this map
    uint8_t music_last; // its trigger type
    C322Tally names[kC322Names];
};
C322Map g_c322{};

// The Blueprint object of the innermost hooked exec thunk on the game thread, null outside one.

bool g_c322_relay = false; // client: the current conversation call is the relay's (passes the own-call skip)
uint64_t g_c322_lib_names[2] = {}; // FName caches: GetWorldSubsystem, GetGameInstanceSubsystem

const char* c322_side() {
    return g_client_session ? "client" : "host";
}

void c322_tally(const char* kind, const char* name, ULONGLONG now) {
    for (int32_t i = 0; i < g_c322.n; ++i) {
        C322Tally& t = g_c322.names[i];
        if (strcmp(t.kind, kind) == 0 && strcmp(t.name, name) == 0) {
            ++t.calls;
            return;
        }
    }
    if (g_c322.n >= kC322Names) {
        ++g_c322.dropped;
        return;
    }
    C322Tally& t = g_c322.names[g_c322.n++];
    strncpy_s(t.kind, kind, _TRUNCATE);
    strncpy_s(t.name, name, _TRUNCATE);
    t.calls = 1;
    t.first_ms = now;
}

// One tally count, and a row unless `quiet`. `ret` is the native function's return address (used when no thunk is
// around it).
void c322_row(const char* kind, const char* name, const void* ret, bool quiet = false) {
    if (!g_gate_c62_diag) {
        return; // part A: the conversation hooks run in the play build; the step 1 rows stay diagnostic
    }
    const ULONGLONG now = GetTickCount64();
    c322_tally(kind, name, now);
    if (quiet || !note_event_cap(g_c322.rows, kC322Rows, "card322")) {
        return;
    }
    if (g_c322_bp_caller != nullptr) {
        log::line("card322: %s t=%llu %s %s, caller %s (%s)", c322_side(), static_cast<unsigned long long>(now), kind,
                  name, class_name(g_c322_bp_caller).text, object_name(g_c322_bp_caller).text);
        return;
    }
    char chain[160];
    caller_chain(chain, sizeof chain, 3);
    log::line("card322: %s t=%llu %s %s, caller native exe+0x%llX (%s)", c322_side(), static_cast<unsigned long long>(now),
              kind, name, exe_rva(ret), chain);
}

// `data_class` is the native function's TSubclassOf argument: a pointer to the class pointer (x64 by value).
void* c322_class_of(void* data_class) {
    return data_class != nullptr ? *static_cast<void**>(data_class) : nullptr;
}

void c322_class_row(const char* kind, void* data_class, const void* ret) {
    const void* const cls = c322_class_of(data_class);
    c322_row(kind, cls != nullptr ? object_name(cls).text : "null", ret);
}

C322Radio* c322_radio(const char* name) {
    for (int32_t i = 0; i < g_c322.radio_n; ++i) {
        if (strcmp(g_c322.radio[i].name, name) == 0) {
            return &g_c322.radio[i];
        }
    }
    if (g_c322.radio_n >= kC322Radios) {
        log::line("WARN card322: client radio table full (%d), %s not counted", kC322Radios, name);
        return nullptr;
    }
    C322Radio& r = g_c322.radio[g_c322.radio_n++];
    strncpy_s(r.name, name, _TRUNCATE);
    return &r;
}

// Host: one start or queue to every remote PC; returns the PCs sent to.
uint32_t c322_send(void* self, const char* kind, const void* cls, int32_t a, int32_t b, bool c) {
    char path[kC50PathMax + 1];
    if (c50_class_path(cls, path, sizeof(path)) <= 0) {
        log::line("WARN card322: host radio %s %s: no class path, not sent", kind, object_name(cls).text);
        return 0;
    }
    char text[kC50PathMax + 64];
    snprintf(text, sizeof(text), "%s %s %d %d %d", kind, path, a, b, c ? 1 : 0);
    uint32_t sent = 0;
    for (int32_t s = 0; s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(self, s);
        if (pc != nullptr && c26_is_remote_pc(pc)) {
            sent += c95_send_one(pc, ChanOp::Radio, text) ? 1u : 0u;
        }
    }
    return sent;
}

void c322_host_row(const char* kind, const void* cls, int32_t pick, int32_t num, uint32_t sent) {
    log::line("card322: host t=%llu radio %s %s (picked %d of %d) sent to %u", static_cast<unsigned long long>(GetTickCount64()),
              kind, object_name(cls).text, pick, num, sent);
}

// Client, own script call skipped: the row names the Blueprint caller (or the native return address).
void c322_skip_log(const char* name, const void* ret) {
    if (g_c322_bp_caller != nullptr) {
        log::line("card322: client t=%llu radio %s own start skipped, caller %s", static_cast<unsigned long long>(GetTickCount64()),
                  name, class_name(g_c322_bp_caller).text);
        return;
    }
    log::line("card322: client t=%llu radio %s own start skipped, caller native exe+0x%llX",
              static_cast<unsigned long long>(GetTickCount64()), name, exe_rva(ret));
}

// Client, own single start or queue skipped: one skip for its class.
void c322_skip_row(const void* cls, const void* ret) {
    const ObjectName on = object_name(cls);
    const char* const name = cls != nullptr ? on.text : "null";
    C322Radio* const r = c322_radio(name);
    if (r != nullptr) {
        ++r->skips;
    }
    c322_skip_log(name, ret);
}

// Client, own list start skipped: the host picks one element, so every element counts a skip in one group, and a
// relay of any element clears the group at map end. A list sharing an element with an earlier list joins its group.
void c322_skip_list(void* const* items, int32_t num, const void* ret) {
    const int32_t n = items != nullptr ? (num < kC322Radios ? num : kC322Radios) : 0;
    int32_t group = 0;
    for (int32_t i = 0; i < n && group == 0; ++i) {
        const C322Radio* const r = items[i] != nullptr ? c322_radio(object_name(items[i]).text) : nullptr;
        group = r != nullptr ? r->group : 0;
    }
    group = group != 0 ? group : ++g_c322.group_n;
    for (int32_t i = 0; i < n; ++i) {
        C322Radio* const r = items[i] != nullptr ? c322_radio(object_name(items[i]).text) : nullptr;
        if (r != nullptr) {
            ++r->skips;
            r->group = group;
        }
    }
    char name[kC322NameChars];
    snprintf(name, sizeof name, "%s (list of %d)", n > 0 && items[0] != nullptr ? object_name(items[0]).text : "null", num);
    c322_skip_log(name, ret);
}

// A relay reached this row, or (a list skip's row) any row of its group.
bool c322_relayed(const C322Radio& r) {
    for (int32_t i = 0; i < g_c322.radio_n && r.relays == 0 && r.group != 0; ++i) {
        if (g_c322.radio[i].group == r.group && g_c322.radio[i].relays > 0) {
            return true;
        }
    }
    return r.relays > 0;
}

// USubsystemBlueprintLibrary::<fn>(ctx, cls) through ProcessEvent on the library's default object, or null when
// the function or its (UObject*, TSubclassOf, return) layout is not the expected.
void* c322_lib_call(void* cdo, uint64_t& cache, const wchar_t* fn_name, void* ctx, void* cls) {
    void* const fn = fname_cached(cache, fn_name) != 0 ? g_find_function(cdo, cache) : nullptr;
    uint16_t size = 0;
    if (fn != nullptr) {
        memcpy(&size, static_cast<const uint8_t*>(fn) + kFunctionParmsSize, sizeof(size));
    }
    const void* const p0 = fn != nullptr ? read_ptr(fn, kStructChildProperties) : nullptr;
    const void* const p1 = p0 != nullptr ? read_ptr(p0, kFieldNext) : nullptr;
    const void* const p2 = p1 != nullptr ? read_ptr(p1, kFieldNext) : nullptr;
    const int32_t ret_at = p2 != nullptr ? read_i32(p2, kPropertyOffset) : -1;
    const bool ok = p2 != nullptr && size <= kC322LibParms && read_i32(p0, kPropertyOffset) == 0 &&
                    read_i32(p1, kPropertyOffset) == 8 && ret_at == 16;
    if (!ok) {
        return nullptr;
    }
    alignas(16) uint8_t parms[kC322LibParms] = {};
    memcpy(parms, &ctx, sizeof(ctx));
    memcpy(parms + 8, &cls, sizeof(cls));
    g_orig_process_event(cdo, fn, parms);
    void* out = nullptr;
    memcpy(&out, parms + ret_at, sizeof(out));
    return out;
}

// Client: the conversation subsystem for `ctx` (the own PC), or null (WARN by the caller).
void* c322_subsystem(void* ctx) {
    static const wchar_t* const kFns[2] = {L"GetWorldSubsystem", L"GetGameInstanceSubsystem"};
    void* const lib = find_class(L"/Script/Engine.SubsystemBlueprintLibrary");
    void* const cdo = lib != nullptr ? read_ptr(lib, kClassDefaultObject) : nullptr;
    void* const cls = find_class(L"/Script/Aloft.STConversationSubsystem");
    if (cdo == nullptr || cls == nullptr || g_find_function == nullptr || g_orig_process_event == nullptr) {
        log::line("WARN card322: client radio subsystem lookup: library %p, class %p", cdo, cls);
        return nullptr;
    }
    for (int32_t i = 0; i < 2; ++i) {
        void* const sub = c322_lib_call(cdo, g_c322_lib_names[i], kFns[i], ctx, cls);
        if (sub == nullptr) {
            continue;
        }
        if (!g_c322.subsystem_logged) {
            g_c322.subsystem_logged = true;
            log::line("card322: client t=%llu radio subsystem from %ls", static_cast<unsigned long long>(GetTickCount64()),
                      kFns[i]);
        }
        return sub;
    }
    return nullptr;
}

} // namespace

// Card 451: the world subsystem of class `cls` for `ctx`'s world through GetWorldSubsystem, or null.
void* c322_world_subsystem(void* ctx, void* cls) {
    void* const lib = find_class(L"/Script/Engine.SubsystemBlueprintLibrary");
    void* const cdo = lib != nullptr ? read_ptr(lib, kClassDefaultObject) : nullptr;
    if (cdo == nullptr || cls == nullptr || g_find_function == nullptr || g_orig_process_event == nullptr) {
        return nullptr;
    }
    return c322_lib_call(cdo, g_c322_lib_names[0], L"GetWorldSubsystem", ctx, cls);
}

// One exec thunk: FFrame::Object is the calling Blueprint object while the native function runs.
template <int I>
void detour_c322_exec(void* context, void* stack, void* result) {
    const void* const prev = g_c322_bp_caller;
    g_c322_bp_caller = stack != nullptr ? *reinterpret_cast<void* const*>(static_cast<uint8_t*>(stack) + kFrameObject)
                                        : nullptr;
    g_orig_c322_exec[I](context, stack, result);
    g_c322_bp_caller = prev;
}

template void detour_c322_exec<0>(void*, void*, void*);
template void detour_c322_exec<1>(void*, void*, void*);
template void detour_c322_exec<2>(void*, void*, void*);
template void detour_c322_exec<3>(void*, void*, void*);
template void detour_c322_exec<4>(void*, void*, void*);
template void detour_c322_exec<5>(void*, void*, void*);
template void detour_c322_exec<6>(void*, void*, void*);

void detour_c322_start_conversation(void* self, void* data_class, int32_t a, int32_t b, bool c) {
    c322_class_row("conversation", data_class, _ReturnAddress());
    void* const cls = c322_class_of(data_class);
    if (g_client_session && !g_c322_relay) {
        c322_skip_row(cls, _ReturnAddress());
        return;
    }
    g_orig_c322_start_conversation(self, data_class, a, b, c);
    if (!g_client_session && cls != nullptr) {
        c322_host_row("start", cls, 0, 1, c322_send(self, "start", cls, a, b, c));
    }
}

void detour_c322_add_conversation(void* self, void* data_class, int32_t a, int32_t b) {
    c322_class_row("conversation queued", data_class, _ReturnAddress());
    void* const cls = c322_class_of(data_class);
    if (g_client_session && !g_c322_relay) {
        c322_skip_row(cls, _ReturnAddress());
        return;
    }
    g_orig_c322_add_conversation(self, data_class, a, b);
    if (!g_client_session && cls != nullptr) {
        c322_host_row("queue", cls, 0, 1, c322_send(self, "queue", cls, a, b, false));
    }
}

// The TArray by value is the caller's temporary copy (x64: passed by pointer, destroyed by the callee): read before
// the original runs.
void detour_c322_start_from_list(void* self, void* list, int32_t a, int32_t b, bool c) {
    const auto* const p = static_cast<const uint8_t*>(list);
    const int32_t num = p != nullptr ? *reinterpret_cast<const int32_t*>(p + kTArrayNum) : 0;
    const auto* const data = p != nullptr ? *reinterpret_cast<void* const* const*>(p + kTArrayData) : nullptr;
    char name[kC322NameChars];
    snprintf(name, sizeof name, "%s (list of %d)",
             num > 0 && data != nullptr && data[0] != nullptr ? object_name(data[0]).text : "null", num);
    c322_row("conversation list", name, _ReturnAddress());
    void** const items = const_cast<void**>(data);
    if (g_client_session) {
        // Own script call: skipped. The original owns the by-value list and frees its data; so does this skip.
        c322_skip_list(items, num, _ReturnAddress());
        if (items != nullptr && g_memory_free != nullptr) {
            g_memory_free(items);
        }
        return;
    }
    // Host: the pick is the host's own, so the relay names the line the host plays. A list of 1 makes the
    // original's rand() pick index 0.
    const int32_t pick = num > 0 ? static_cast<int32_t>((static_cast<int64_t>(rand() & 0x7FFF) * num) >> 15) : 0;
    const int32_t at = pick < num - 1 ? pick : num - 1;
    void* const cls = num > 0 && items != nullptr ? items[at] : nullptr;
    if (cls != nullptr) {
        items[0] = cls;
        *reinterpret_cast<int32_t*>(static_cast<uint8_t*>(list) + kTArrayNum) = 1;
    }
    g_orig_c322_start_from_list(self, list, a, b, c);
    if (cls != nullptr) {
        c322_host_row("start", cls, at, num, c322_send(self, "start", cls, a, b, c));
    }
}

// Client: ChanOp::Radio `<start|queue> <class path> <a> <b> <c>` from the host.
void c322_receive(void* pc, const WireString* s) {
    char text[kChanTextMax + 1];
    chan_text(s, text, sizeof(text));
    char kind[8] = "";
    char path[kC50PathMax + 1] = "";
    int32_t a = 0;
    int32_t b = 0;
    int32_t c = 0;
    const ULONGLONG now = GetTickCount64();
    if (sscanf_s(text, "%7s %160s %d %d %d", kind, static_cast<unsigned>(sizeof(kind)), path,
                 static_cast<unsigned>(sizeof(path)), &a, &b, &c) != 5) {
        log::line("WARN card322: client t=%llu radio row not parsed: %s", static_cast<unsigned long long>(now), text);
        return;
    }
    wchar_t w[kC50PathMax + 1];
    size_t n = 0;
    for (; path[n] != '\0' && n < static_cast<size_t>(kC50PathMax); ++n) {
        w[n] = static_cast<wchar_t>(path[n]);
    }
    w[n] = L'\0';
    void* cls = find_class(w); // find only: a line whose class is not loaded is not loaded for it
    void* const sub = cls != nullptr ? c322_subsystem(pc) : nullptr;
    if (cls == nullptr || sub == nullptr) {
        log::line("WARN card322: client t=%llu radio %s %s dropped: %s", static_cast<unsigned long long>(now), kind, path,
                  cls == nullptr ? "class not found" : "no conversation subsystem");
        return;
    }
    const bool queue = strcmp(kind, "queue") == 0;
    g_c322_relay = true;
    if (queue) {
        g_orig_c322_add_conversation(sub, &cls, a, b);
    } else {
        g_orig_c322_start_conversation(sub, &cls, a, b, c != 0);
    }
    g_c322_relay = false;
    C322Radio* const r = c322_radio(object_name(cls).text);
    if (r != nullptr) {
        ++r->relays;
    }
    log::line("card322: client t=%llu radio %s %s started (relay)", static_cast<unsigned long long>(now),
              queue ? "queue" : "start", object_name(cls).text);
}

void detour_c322_trigger_music(void* self, uint8_t type, float value, bool flag) {
    char name[32];
    snprintf(name, sizeof name, "trigger %u", static_cast<unsigned>(type));
    // A repeat of the last trigger type (the music manager's own tick) is tallied, not logged: it would use up the cap.
    const bool repeat = g_c322.music_any && g_c322.music_last == type;
    g_c322.music_any = true;
    g_c322.music_last = type;
    c322_row("music", name, _ReturnAddress(), repeat);
    g_orig_c322_trigger_music(self, type, value, flag);
}

void detour_c322_play_dilation(void* self, void* handle) {
    char name[32];
    snprintf(name, sizeof name, "handle %lld",
             handle != nullptr
                 ? static_cast<long long>(*reinterpret_cast<const int64_t*>(static_cast<uint8_t*>(handle) +
                                                                            kDilationHandleIndex))
                 : -1LL);
    c322_row("dilation", name, _ReturnAddress());
    g_orig_c322_play_dilation(self, handle);
}

void detour_c322_dilation_start(void* ability) {
    c322_row("dilation ability", class_name(ability).text, _ReturnAddress());
    g_orig_c322_dilation_start(ability);
}

void detour_c322_oob_entered(void* component) {
    const void* const owner = *reinterpret_cast<void* const*>(static_cast<uint8_t*>(component) + kComponentOwner);
    c322_row("out of bounds entered", owner != nullptr ? class_name(owner).text : "null", _ReturnAddress());
    g_orig_c322_oob_entered(component);
    // Card 360: the countdown the original just started (diag only; this detour is installed under the diag gate).
    if (g_c360_time_left != nullptr && owner != nullptr && note_event_cap(g_c360_rows, kC360Rows, "card360")) {
        log::line("card360: %s t=%llu out of bounds countdown %.2f s on %s (%s)", c322_side(),
                  static_cast<unsigned long long>(GetTickCount64()), static_cast<double>(g_c360_time_left(component)),
                  object_name(owner).text, kSprintSideText[sprint_side(owner)]);
    }
}

void detour_c322_oob_exited(void* component) {
    const void* const owner = *reinterpret_cast<void* const*>(static_cast<uint8_t*>(component) + kComponentOwner);
    c322_row("out of bounds exited", owner != nullptr ? class_name(owner).text : "null", _ReturnAddress());
    g_orig_c322_oob_exited(component);
}

// Card 360, always on, log only: the expiry timer's kill runs on both sides; card 361's InstaKill detour skips a
// client's kill of a player-class pawn and names this caller by g_c361_oob_expiry.
void detour_c360_kill_owning_actor(void* component) {
    const void* const owner = *reinterpret_cast<void* const*>(static_cast<uint8_t*>(component) + kComponentOwner);
    const bool log_row = note_event_cap(g_c360_rows, kC360Rows, "card360");
    const unsigned long long now = static_cast<unsigned long long>(GetTickCount64());
    const ObjectName name = object_name(owner); // held: .text lives in the struct
    const char* const pawn = owner != nullptr ? name.text : "null";
    const char* const side = owner != nullptr ? kSprintSideText[sprint_side(owner)] : "none";
    if (log_row) {
        log::line("card360: %s t=%llu out of bounds expiry on %s (%s): KillOwningActor runs", c322_side(), now, pawn,
                  side);
    }
    g_c361_oob_expiry = true; // card 361: the original tail-jumps to InstaKillTarget; its caller row names the expiry
    g_orig_c360_kill_owning_actor(component);
    g_c361_oob_expiry = false;
}

// Both sides, at LoadMap enter: one tally row per (kind, name), then the table starts over.
void c322_map_end() {
    for (int32_t i = 0; i < g_c322.n; ++i) {
        const C322Tally& t = g_c322.names[i];
        log::line("card322: %s map end: %s %s x%u, first t=%llu", c322_side(), t.kind, t.name, t.calls,
                  static_cast<unsigned long long>(t.first_ms));
    }
    for (int32_t i = 0; i < g_c322.radio_n; ++i) {
        const C322Radio& r = g_c322.radio[i];
        if (r.skips > 0 && !c322_relayed(r)) {
            log::line("WARN card322: client map end: radio %s own start skipped x%u, no relay of it on this map", r.name,
                      r.skips);
        }
    }
    if (g_c322.dropped > 0) {
        log::line("WARN card322: %s map end: %u calls not tallied (table of %d full)", c322_side(), g_c322.dropped,
                  kC322Names);
    }
    g_c322 = C322Map{};
    g_c360_rows = 0;
    g_c322_bp_caller = nullptr;
}

} // namespace ubw::hooks
