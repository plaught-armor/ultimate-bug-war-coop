// Card 493 (host): the FED_C5M2 cave-in blast hits every player, not only player 0.
//
// FED_C5M2_Scripting event `Cave in` (ExecuteUbergraph @31328) reads GetSTPlayerCharacter(self), spawns a Niagara
// burst at its location, applies a 20 damage, 1500 uu radius blast there (ApplyDamageAtLocation) and re-arms itself
// with CallFunctionWithDelay(30.0, 'Cave in') @32214. On the host player 0 is the host pawn, so the blast tracked the
// host only.
//
// Host: the first CallFunctionWithDelay(..., 'Cave in') arms the ProcessEvent gate (g_c493_pe). Each later `Cave in`
// the timer runs through ProcessEvent is run once more, after the original, for every other living player inside a
// card 102 acting scope for that player's server copy (an index-0 GetPlayerController returns the copy, so
// GetSTPlayerCharacter is that pawn). During a rerun:
//   - CallFunctionWithDelay is skipped (each call adds one USTSavableTimer, so a rerun's re-arm would add a timer per
//     player per blast); the by-value FString is freed here, as the callee does (MSVC x64 callee-destroyed argument);
//   - no rerun reruns again (g_c493_rerun).
// The client keeps its own `Cave in` loop: its burst at its own pawn; its damage does nothing (damage is host only).
//
// Rows (kC493Rows per map, then counted):
//   card493: host t=<ms> armed: CallFunctionWithDelay <s> s 'Cave in'
//   card493: host t=<ms> cave in rerun for <pawn> at <x y z>, <d> uu from the host
//   card493: host map tally: reruns <n>, dead skipped <n>, re-arms skipped <n>
// b251 diagnostic (UBWCOOP_DIAG, both sides, kC493DiagRows per map each): c493b250a never armed. The probe logs
// every CallFunctionWithDelay name and every ProcessEvent of `Cave in`, to show whether anything starts the loop.
//   card493: <side> t=<ms> CallFunctionWithDelay <s> s '<name>' on <object>
//   card493: <side> t=<ms> ProcessEvent 'Cave in' on <object> (<class>)

#include "ubw/hooks_internal.h"

#include <math.h>
#include <wchar.h>

namespace ubw::hooks {

bool g_c493_pe = false;
C493DelayFn g_orig_c493_delay = nullptr;

namespace {

constexpr wchar_t kC493Event[] = L"Cave in";
constexpr int32_t kC493EventNum = static_cast<int32_t>(sizeof(kC493Event) / sizeof(kC493Event[0])); // with the null
constexpr uint32_t kC493Rows = 40;
constexpr uint32_t kC493DiagRows = 60; // b251 probe rows per kind per map
constexpr int32_t kC493NameMax = 64;   // narrowed CallFunctionWithDelay name

struct C493FString {
    wchar_t* data;
    int32_t num; // characters with the terminating null
    int32_t max;
};

struct C493State {
    uint32_t rows;
    uint32_t reruns;
    uint32_t dead;
    uint32_t rearms_skipped;
    uint64_t name_cache; // FName of kC493Event
    bool armed;          // host: a CallFunctionWithDelay(..., 'Cave in') was seen (the rerun gate)
    uint32_t delay_rows; // b251 probe
    uint32_t pe_rows;    // b251 probe
};
C493State g_c493{};
bool g_c493_rerun = false; // inside a rerun of `Cave in`

bool c493_row_ok() {
    return note_event_cap(g_c493.rows, kC493Rows, "card493 rows");
}

const char* c493_side() {
    return g_client_session ? "client" : "host";
}

// b251 probe: one row per CallFunctionWithDelay (UBWCOOP_DIAG only).
void c493_delay_row(const void* self, float delay, const C493FString* s) {
    if (!g_gate_diag || !note_event_cap(g_c493.delay_rows, kC493DiagRows, "card493 delay probe")) {
        return;
    }
    char name[kC493NameMax] = {};
    for (int32_t i = 0; s != nullptr && s->data != nullptr && i < s->num && i < kC493NameMax - 1; ++i) {
        const wchar_t ch = s->data[i];
        name[i] = ch > 0 && ch < 0x7F ? static_cast<char>(ch) : (ch == 0 ? '\0' : '?');
    }
    log::line("card493: %s t=%llu CallFunctionWithDelay %.1f s '%s' on %s", c493_side(),
              static_cast<unsigned long long>(GetTickCount64()), static_cast<double>(delay), name,
              object_name(self).text);
}

bool c493_is_event(const C493FString* s) {
    return s != nullptr && s->data != nullptr && s->num == kC493EventNum && wmemcmp(s->data, kC493Event, kC493EventNum) == 0;
}

// One rerun of `function` on `object` for `pawn`.
void c493_rerun_for(void* object, void* function, void* pawn, const Vec3& host_at) {
    bool killable = false;
    if (c98_dead_now(pawn, killable)) {
        ++g_c493.dead;
        return;
    }
    const C102Scope saved = c102_enter(pawn, kC102Damage);
    g_orig_process_event(object, function, nullptr);
    c102_leave(saved);
    ++g_c493.reruns;
    const Vec3 at = actor_location(pawn);
    const double dx = at.x - host_at.x;
    const double dy = at.y - host_at.y;
    const double dz = at.z - host_at.z;
    if (c493_row_ok()) {
        log::line("card493: host t=%llu cave in rerun for %s at %.0f %.0f %.0f, %.0f uu from the host",
                  static_cast<unsigned long long>(GetTickCount64()), object_name(pawn).text, at.x, at.y, at.z,
                  sqrt(dx * dx + dy * dy + dz * dz));
    }
}

} // namespace

void detour_c493_delay(void* self, float delay, void* fstring) {
    const auto* const s = static_cast<const C493FString*>(fstring);
    c493_delay_row(self, delay, s);
    const bool cave = !g_client_session && c493_is_event(s);
    if (cave && g_c493_rerun) {
        ++g_c493.rearms_skipped;
        g_memory_free(s->data); // the callee owns its by-value FString
        return;
    }
    if (cave && !g_c493.armed) {
        g_c493.armed = true;
        g_c493_pe = true;
        if (c493_row_ok()) {
            log::line("card493: host t=%llu armed: CallFunctionWithDelay %.1f s 'Cave in'",
                      static_cast<unsigned long long>(GetTickCount64()), static_cast<double>(delay));
        }
    }
    g_orig_c493_delay(self, delay, fstring);
}

// Host, after each ProcessEvent while armed: a `Cave in` run by the timer is run again for each other player.
// UBWCOOP_DIAG keeps the gate open on both sides from the map start for the b251 probe row.
void c493_after_pe(void* object, const void* function) {
    if (g_c493_rerun || function == nullptr ||
        read_u64(function, kObjectName) != fname_cached(g_c493.name_cache, kC493Event)) {
        return;
    }
    if (g_gate_diag && note_event_cap(g_c493.pe_rows, kC493DiagRows, "card493 ProcessEvent probe")) {
        log::line("card493: %s t=%llu ProcessEvent 'Cave in' on %s (%s)", c493_side(),
                  static_cast<unsigned long long>(GetTickCount64()), object_name(object).text,
                  class_name(object).text);
    }
    if (g_client_session || !g_c493.armed) {
        return;
    }
    void* const pc0 = g_get_player_controller(object, 0);
    void* const host = pc0 != nullptr ? read_ptr(pc0, kControllerPawn) : nullptr;
    if (host == nullptr) {
        return;
    }
    void* others[kC17Slots] = {};
    const int32_t n = c267_other_players(host, others, kC17Slots);
    const Vec3 host_at = actor_location(host);
    g_c493_rerun = true;
    for (int32_t i = 0; i < n; ++i) {
        c493_rerun_for(object, const_cast<void*>(function), others[i], host_at);
    }
    g_c493_rerun = false;
}

// LoadMap return: the old map's tally, then disarm.
void c493_arm_map() {
    if (g_c493.armed || g_c493.reruns != 0) {
        log::line("card493: host map tally: reruns %u, dead skipped %u, re-arms skipped %u", g_c493.reruns,
                  g_c493.dead, g_c493.rearms_skipped);
    }
    const uint64_t cache = g_c493.name_cache;
    g_c493 = C493State{};
    g_c493.name_cache = cache;
    g_c493_pe = g_gate_diag; // b251 probe: every `Cave in` ProcessEvent is seen, armed or not
    g_c493_rerun = false;
}

} // namespace ubw::hooks
