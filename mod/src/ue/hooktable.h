#pragma once

// The hook-table machinery: PE checks, symbol resolution by RVA, and MinHook install over sets of table
// rows. The engine set (kHooksUe, kCallsUe) lives in hooktable.cpp; the game set is passed to ue::install.

#include "offsets.generated.h"

#include <windows.h>

#include <span>
#include <stddef.h>
#include <stdint.h>

namespace ue::hooktable {

// A hooked engine function: its symbol, the detour, the slot that receives the trampoline, and an
// optional install gate. The trampoline of a gated-off row stays null: only its own detour calls it. Code
// outside the detour calls the target through a CallSpec row (b16: the card 312 crash called a diag-gated one).
struct HookSpec {
    const ubw::offsets::Symbol* sym;
    void* detour;
    void** original;
    const bool* gate = nullptr; // nullptr = always installed; else only while *gate (set before install)
};

// An engine function the mod calls directly (not hooked): its symbol and the slot its address goes to.
struct CallSpec {
    const ubw::offsets::Symbol* sym;
    void** slot;
};

// The exe's base address: set once by the installer before any hook is enabled, read-only afterwards.
extern const uint8_t* g_exe_base;

// The exe's PE64 headers, or nullptr when the image has none.
const IMAGE_NT_HEADERS64* nt_headers(const uint8_t* base);
// True if the image's CodeView record matches the PDB identity (guid, age) the offsets came from.
bool image_matches_pdb(const uint8_t* base, const IMAGE_NT_HEADERS64* nt, const uint8_t (&guid)[16], uint32_t age);
// Resolves an RVA to an address, requiring it to land inside an executable section.
void* code_address(uint8_t* base, const IMAGE_NT_HEADERS64* nt, const ubw::offsets::Symbol& sym);
// MH_CreateHook then MH_EnableHook; logs each failure, and the hooked address on success.
bool create_and_enable(void* target, void* detour, void** original, const char* name);
// Exe-relative address of `p`, or 0 when it lies outside the exe image (e.g. in this mod).
unsigned long long exe_rva(const void* p);

// One install set: hook rows and call rows that resolve and install together.
struct InstallSet {
    std::span<const HookSpec> hooks;
    std::span<const CallSpec> calls;
};

// The caller's steps inside ue::install. Each returns false to stop the install.
struct InstallSteps {
    // After every row of every set resolved and g_exe_base set, before MH_Initialize.
    bool (*after_resolve)(uint8_t* base, const IMAGE_NT_HEADERS64* nt);
    // After MH_Initialize, before the first table row is hooked.
    bool (*before_hooks)(uint8_t* base, const IMAGE_NT_HEADERS64* nt);
};

// Gate of the UWorld::Listen diagnostic row (UBWCOOP_DIAG); set by the caller before ue::install.
extern bool g_gate_diag;

bool hook_gated_off(const HookSpec& h);
// Resolves every hook target (into targets, one per hook row, sets in order) and every call slot before
// anything is patched.
bool resolve_all(uint8_t* base, const IMAGE_NT_HEADERS64* nt, std::span<const InstallSet> sets,
                 std::span<void*> targets);
// One line with the count of gated-off hooks over all sets, then their names, wrapped to fit a log line.
void log_skipped_hooks(std::span<const InstallSet> sets, uint32_t skipped);
// Creates and enables every row whose gate is on, sets in order; the rest stay resolved but unhooked.
bool install_hooks(std::span<const InstallSet> sets, std::span<void* const> targets);

} // namespace ue::hooktable

namespace ue {

// Verifies the host exe is the build the offsets were generated for (guid, age), resolves the engine set
// then `game`, runs steps.after_resolve, starts MinHook, runs steps.before_hooks, and hooks the engine set
// then `game`. Logs every step; on any failure logs FATAL/ERROR and installs nothing more.
// Must not run under the loader lock (MinHook suspends threads).
bool install(const hooktable::InstallSet& game, const uint8_t (&guid)[16], uint32_t age,
             const hooktable::InstallSteps& steps);

} // namespace ue
