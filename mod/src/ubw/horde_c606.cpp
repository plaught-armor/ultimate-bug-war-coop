// Card 606: BP_HordeZone (the "kill them all" arena wall) has no replicated state. Each machine runs its own copy,
// and the copy follows only its own level script's `Begin KillEmAll` / `End KillEmAll` calls. An End that comes from
// a host-only encounter delegate (C4M1 SetWaves C_11, C5M1 SetWaves C_0, C4M2 plaza) never ran on the client, so the
// client kept the wall's 71 collision meshes (collidable and visible), the HordeSystem Niagara, the hazard zones and
// the end sound undone (c502b277a: card 098 zone collision tally 213 host, 142 client).
// Host: each Begin / End that runs on a zone (BP VM or ProcessEvent: both reach ProcessLocalScriptFunction) goes to
// every ready client as ChanOp::HordeZone "<zone level key> B|E <ShowUI 0|1>".
// Client: its own calls of those two events are refused (the host is the one source, so a switch-sourced begin does
// not run twice); a host row runs the copy's event once through ProcessEvent with the gate bypassed.
// Join and in-place reload (the first Pong of a load, sync registry "horde"): every host zone with Active = true
// gets one begin row.

#include "ubw/hooks_internal.h"

#include <stdio.h>
#include <string.h>

namespace ubw::hooks {

bool g_c606_vm = false; // the map's zone functions are resolved: detour_c507_local_script calls c606_vm

namespace {

constexpr wchar_t kC606Class[] = L"/Game/Blueprints/HordeMode/BP_HordeZone.BP_HordeZone_C";
constexpr ULONGLONG kC606ScanMs = 5000; // the class lookup repeats this often until a zone has loaded
constexpr size_t kC606ParmsMax = 16;    // Begin KillEmAll: one bool (ShowUI); End KillEmAll: none
constexpr size_t kC606FrameLocals = 0x28; // FFrame::Locals (the parms struct), as rack_c556.cpp and claim_c157.cpp
constexpr int32_t kC606Begin = 0;
constexpr int32_t kC606End = 1;
constexpr int32_t kC606Fns = 2;
constexpr const wchar_t* kC606FnText[kC606Fns] = {L"Begin KillEmAll", L"End KillEmAll"};
constexpr char kC606Op[kC606Fns] = {'B', 'E'};
constexpr const char* kC606OpText[kC606Fns] = {"begin", "end"};

struct C606State {
    const void* cls;
    void* fn[kC606Fns];
    int32_t active_at; // the zone's Active bool: byte offset into the actor; -1 not read
    uint8_t active_mask;
    bool calling; // client: the mod's own run of a host row (the gate lets it through)
    ULONGLONG scan_at;
    uint32_t sends, refused, applied, missed;
};
C606State g_c606{nullptr, {nullptr, nullptr}, -1, 0, false, 0, 0, 0, 0, 0}; // game thread
uint64_t g_c606_fname[kC606Fns] = {};

int32_t c606_index(const void* node) {
    return node == g_c606.fn[kC606Begin] ? kC606Begin : node == g_c606.fn[kC606End] ? kC606End : -1;
}

// The class's two event functions and the Active offset, from any loaded zone. True when both functions resolved.
bool c606_resolve(const void* world) {
    g_c606.cls = g_c606.cls != nullptr ? g_c606.cls : find_class(kC606Class);
    if (g_c606.cls == nullptr) {
        return false; // no zone loaded on this map yet
    }
    static C23List list;
    c23_collect(world, nullptr, g_c606.cls, list);
    void* const zone = list.count > 0 ? list.e[0].actor : nullptr;
    for (int32_t f = 0; zone != nullptr && f < kC606Fns; ++f) {
        g_c606.fn[f] = fname_cached(g_c606_fname[f], kC606FnText[f]) != 0 ? g_find_function(zone, g_c606_fname[f])
                                                                           : nullptr;
    }
    const uint8_t* const active = prop_find_prop(g_c606.cls, "Active");
    if (active != nullptr && prop_bool_ok(active)) {
        g_c606.active_at = read_i32(active, kPropertyOffset) + active[kBoolByteOffset];
        g_c606.active_mask = active[kBoolFieldMask];
    }
    const bool ok = g_c606.fn[kC606Begin] != nullptr && g_c606.fn[kC606End] != nullptr;
    if (zone != nullptr) {
        log::line("%scard606: %s t=%llu BP_HordeZone_C: %u zones, begin %d, end %d, Active offset %d",
                  ok && g_c606.active_at >= 0 ? "" : "WARN ", side(), static_cast<unsigned long long>(GetTickCount64()),
                  list.count, g_c606.fn[kC606Begin] != nullptr, g_c606.fn[kC606End] != nullptr, g_c606.active_at);
    }
    return ok;
}

// Host: one row to one client copy. True when sent.
bool c606_send_one(void* pc, const void* zone, int32_t f, int show_ui, const char* why) {
    char key[kChanNameMax + 1];
    char text[kChanNameMax + 8];
    if (c50_level_key(zone, key, sizeof(key)) <= 0) {
        log::line("WARN card606: host zone %s has no sendable level key, %s not sent", object_name(zone).text, why);
        return false;
    }
    snprintf(text, sizeof(text), "%s %c %d", key, kC606Op[f], show_ui);
    return c95_send_one(pc, ChanOp::HordeZone, text);
}

// Host: a zone event ran here; every ready client gets the row.
void c606_host_send(const void* zone, int32_t f, int show_ui) {
    const void* const world = g_actor_get_world(zone);
    int32_t to = 0;
    int32_t sent = 0;
    for (int s = 0; world != nullptr && s < kC17Slots; ++s) {
        void* const pc = g_get_player_controller(world, s);
        const C50Copy* const c = pc != nullptr && sprint_side(pc) == kSideServerCopy ? c50_copy(object_index(pc), false)
                                                                                     : nullptr;
        if (c == nullptr || !c->ponged) {
            continue; // not a ready client: its join replay reads Active
        }
        ++to;
        sent += c606_send_one(pc, zone, f, show_ui, kC606OpText[f]) ? 1 : 0;
    }
    ++g_c606.sends;
    log::line("%scard606: host t=%llu %s %s showui=%d sent to %d of %d clients", sent == to ? "" : "WARN ",
              static_cast<unsigned long long>(GetTickCount64()), object_name(zone).text, kC606OpText[f], show_ui, sent,
              to);
}

} // namespace

// Hot path (detour_c507_local_script, while g_c606_vm): true refuses this script function call. Two pointer compares
// on a miss.
bool c606_vm(const void* context, const void* stack) {
    const int32_t f = c606_index(read_ptr(stack, kFrameNode));
    if (f < 0) {
        return false;
    }
    if (!g_client_session) {
        const auto* const locals = static_cast<const uint8_t*>(read_ptr(stack, kC606FrameLocals));
        const int show_ui = f == kC606Begin && locals != nullptr && locals[0] != 0 ? 1 : 0;
        c606_host_send(context, f, show_ui);
        return false;
    }
    if (g_c606.calling) {
        return false;
    }
    ++g_c606.refused;
    log::line("card606: client t=%llu %s own %s refused (the host's row runs it)",
              static_cast<unsigned long long>(GetTickCount64()), object_name(context).text, kC606OpText[f]);
    return true;
}

// Client: ChanOp::HordeZone "<zone level key> B|E <ShowUI>". Runs the copy's event once.
void c606_receive(void* pc, const WireString* s) {
    char text[kChanNameMax + 8];
    char key[kChanNameMax + 1] = {};
    char op = 0;
    int show_ui = 0;
    const ULONGLONG now = GetTickCount64();
    static_assert(kChanNameMax == 64, "the %64s width below");
    const bool read = chan_text(s, text, sizeof(text)) > 0 &&
                      sscanf_s(text, "%64s %c %d", key, static_cast<unsigned>(sizeof(key)), &op, 1, &show_ui) == 3;
    const int32_t f = op == 'B' ? kC606Begin : op == 'E' ? kC606End : -1;
    void* const world = g_actor_get_world(pc);
    if (read && f >= 0 && g_c606.fn[f] == nullptr && world != nullptr) {
        g_c606_vm = c606_resolve(world); // a join row ahead of the periodic scan
    }
    void* const zone = read && f >= 0 ? c50_find_placed_any(world, key) : nullptr;
    void* const fn = f >= 0 ? g_c606.fn[f] : nullptr;
    const uint16_t size =
        fn != nullptr ? *reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(fn) + kFunctionParmsSize) : 0;
    if (zone == nullptr || fn == nullptr || size > kC606ParmsMax || g_orig_process_event == nullptr) {
        ++g_c606.missed;
        log::line("WARN card606: client t=%llu host row \"%s\": %s", static_cast<unsigned long long>(now),
                  read ? text : "(unread)",
                  zone == nullptr ? "no placed zone has that key" : fn == nullptr ? "zone event not resolved"
                                                                                : "parms larger than the buffer");
        return;
    }
    alignas(16) uint8_t parms[kC606ParmsMax] = {};
    parms[0] = f == kC606Begin && show_ui != 0 ? 1 : 0; // Begin KillEmAll's ShowUI is its first parameter
    g_c606.calling = true;
    g_orig_process_event(zone, fn, parms);
    g_c606.calling = false;
    ++g_c606.applied;
    log::line("card606: client t=%llu %s %s showui=%d from the host: own copy's event ran",
              static_cast<unsigned long long>(now), key, kC606OpText[f], show_ui);
}

// Host join replay (sync registry "horde"): one begin row per zone whose Active is true.
void c606_join_replay(void* pc) {
    const void* const world = g_actor_get_world(pc);
    if (g_c606.cls == nullptr || g_c606.active_at < 0 || world == nullptr) {
        return; // no zone resolved on this map
    }
    static C23List list;
    c23_collect(world, nullptr, g_c606.cls, list);
    int32_t active = 0;
    int32_t sent = 0;
    for (uint32_t i = 0; i < list.count; ++i) {
        const auto* const zone = static_cast<const uint8_t*>(list.e[i].actor);
        if (!c50_placed(zone) || (zone[g_c606.active_at] & g_c606.active_mask) == 0) {
            continue;
        }
        ++active;
        sent += c606_send_one(pc, zone, kC606Begin, 0, "join begin") ? 1 : 0;
    }
    log::line("%scard606: host t=%llu horde join replay to %s: %d of %d active zones sent (%u zones)",
              sent == active ? "" : "WARN ", static_cast<unsigned long long>(GetTickCount64()), object_name(pc).text,
              sent, active, list.count);
}

// Both sides, every tick: resolve the zone functions once a zone has loaded (sublevels stream in).
void c606_step(void* engine) {
    const ULONGLONG now = GetTickCount64();
    if (g_c606_vm || now < g_c606.scan_at) {
        return;
    }
    g_c606.scan_at = now + kC606ScanMs;
    void* const world = engine != nullptr ? g_get_game_world(engine) : nullptr;
    g_c606_vm = world != nullptr && c606_resolve(world);
}

// Map end (sync registry): totals, then the class and functions are looked up again (they unload with the map).
void c606_map_end(void*) {
    if (g_c606.sends + g_c606.refused + g_c606.applied + g_c606.missed != 0) {
        log::line("card606: %s map totals: %u sends, %u own calls refused, %u host rows run, %u missed", side(),
                  g_c606.sends, g_c606.refused, g_c606.applied, g_c606.missed);
    }
    c606_arm_map();
}

void c606_arm_map() {
    g_c606 = C606State{nullptr, {nullptr, nullptr}, -1, 0, false, 0, 0, 0, 0, 0};
    g_c606_vm = false;
}

} // namespace ubw::hooks
