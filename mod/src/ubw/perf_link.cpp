// Card 578: the co-op side of the ubwperf.dll boundary (perf/ubwperf_api.h). The performance cards moved into
// ubwperf.dll; this file loads it, hands it the co-op pieces it reads, and forwards the three co-op call sites
// (c42_step's tick, the card 461 detour, the two card 568 significance sites). With no ubwperf.dll every site runs
// the original path every frame, as before those cards.

#include "perf/ubwperf_api.h"
#include "ubw/hooks_internal.h"

// A co-op dwmapi.dll in the process: ubwperf.dll loaded as winhttp.dll sees this export and stays passive.
extern "C" __declspec(dllexport) const uint32_t ubwcoop_marker = kUbwPerfVersion;

namespace ubw::hooks {

namespace {

const UbwPerfApi* g_perf = nullptr; // set once in perf_link_init (worker, before any hook); read-only afterwards
// Set once install() succeeded (worker). Before it, ubwperf has no call rows and no card 461 trampoline, so the
// game-thread sites run the original path; they read it each call (a late true only delays the cuts one frame).
volatile bool g_perf_live = false;

UbwPerfInitFn perf_init_fn() {
    // Single player plus co-op in one process: the winhttp.dll copy is already loaded; use that one.
    HMODULE m = GetModuleHandleW(L"winhttp.dll");
    FARPROC p = m != nullptr ? GetProcAddress(m, kUbwPerfInitName) : nullptr;
    if (p == nullptr) {
        m = LoadLibraryW(L"ubwperf.dll");
        p = m != nullptr ? GetProcAddress(m, kUbwPerfInitName) : nullptr;
    }
    return reinterpret_cast<UbwPerfInitFn>(p);
}

} // namespace

void perf_link_init() {
    const UbwPerfInitFn init = perf_init_fn();
    if (init == nullptr) {
        log::line("WARN card578: no ubwperf.dll next to the exe (err=%lu); perf cuts off", GetLastError());
        return;
    }
    const UbwPerfHost host{kUbwPerfVersion, &ue::log::write_raw, &own_pc,
                           reinterpret_cast<void* const*>(&g_orig_c461_move)};
    g_perf = init(&host);
    if (g_perf == nullptr || g_perf->version != kUbwPerfVersion) {
        log::line("WARN card578: ubwperf_init refused (api %p); perf cuts off", static_cast<const void*>(g_perf));
        g_perf = nullptr;
        return;
    }
    *g_perf->client_session = g_client_session;
    log::line("card578: ubwperf linked, card 461 row %s", g_perf->wants_c461_move ? "wanted" : "not wanted");
}

bool perf_wants_c461_move() {
    return g_perf != nullptr && g_perf->wants_c461_move;
}

void perf_link_install() {
    if (g_perf == nullptr) {
        return;
    }
    if (!g_perf->install()) {
        log::line("WARN card578: ubwperf install failed; the co-op sites keep the original path (see lines above)");
        return;
    }
    g_perf_live = true; // its rows resolved, its hooks in, its card 461 trampoline copy taken
}

void set_client_session(bool on) {
    g_client_session = on;
    pe_gate_set(kPeGate_client, on); // card 573 b463: detour_process_event reads the mirror bit
    if (g_perf != nullptr) {
        *g_perf->client_session = on;
    }
}

void perf_link_tick(void* engine, ULONGLONG now) {
    if (g_perf_live) {
        g_perf->tick(engine, now);
    }
}

bool perf_link_c461_move(void* comp, const void* delta, const void* rotation, bool sweep, void* hit, int32_t flags,
                         uint8_t teleport) {
    return g_perf_live && g_perf->wants_c461_move
               ? g_perf->c539_move(comp, delta, rotation, sweep, hit, flags, teleport)
               : g_orig_c461_move(comp, delta, rotation, sweep, hit, flags, teleport);
}

bool perf_link_c568_due(int32_t slot, const void* world) {
    return !g_perf_live || g_perf->c568_due(slot, world);
}

} // namespace ubw::hooks
