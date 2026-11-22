#pragma once

// Card 578: the boundary between the co-op dll (dwmapi.dll) and the performance mod (ubwperf.dll). Plain C types
// only; both dlls are built from this tree in one build, so kUbwPerfVersion is the whole compatibility check.
//
// One binary, two load paths:
// - Co-op: the dwmapi.dll worker, in its after_resolve step, takes ubwperf_init from an already loaded winhttp.dll
//   or from LoadLibraryW(L"ubwperf.dll"), calls it with UbwPerfHost (nothing hooked yet), sets its card 461 row gate
//   from wants_c461_move, and calls install() once its own hooks are in. The co-op detours call tick, c539_move and
//   c568_due; with no ubwperf they run the original every frame.
// - Single player: the game loads the same file as winhttp.dll. With no co-op dwmapi.dll in the process, its own
//   worker installs alone: its own UGameEngine::Tick row runs tick, its own UpdateSignificanceManager row runs the
//   card 568 throttle, and the log is ubwperf-YYYYMMDD-HHMMSS.log next to the exe.

#include <stddef.h>
#include <stdint.h>

constexpr uint32_t kUbwPerfVersion = 1;

// The co-op dll's side, read by ubwperf_init. Every pointer is required.
struct UbwPerfHost {
    uint32_t version;                                   // kUbwPerfVersion
    void (*log_write)(const char* text, size_t n);      // ue::log::write_raw of the co-op dll
    void* (*own_pc)(const void* world);                 // the local player controller (co-op own_pc)
    void* const* orig_c461_move;                        // slot of the card 461 MoveComponentImpl trampoline
};

// ubwperf's side, returned by ubwperf_init (static storage; never freed).
struct UbwPerfApi {
    uint32_t version;                                   // kUbwPerfVersion
    bool* client_session;                               // the host writes it where it writes g_client_session
    bool wants_c461_move;                               // cards 539/542 on: the host installs its card 461 row
    bool (*install)();                                  // after the host's own install; once
    void (*tick)(void* engine, unsigned long long now); // game thread, once per frame (co-op c42_step)
    bool (*c539_move)(void* comp, const void* delta, const void* rotation, bool sweep, void* hit, int32_t flags,
                      uint8_t teleport);                // the card 461 detour's pass-through
    bool (*c568_due)(int32_t slot, const void* world);  // card 568: slot 0 host game mode, 1 client step
};

using UbwPerfInitFn = const UbwPerfApi* (*)(const UbwPerfHost* host);

// Export of ubwperf.dll (defined in perf_main.cpp; the host takes it by GetProcAddress). Null on a version mismatch
// or a missing host pointer; the host then runs without it.
inline constexpr const char kUbwPerfInitName[] = "ubwperf_init";
