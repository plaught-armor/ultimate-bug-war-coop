// Card 575 step 1 (diagnostic): who registers detour crowd agents, on each side.
//
// Exe scan (2026-11-21): dtCrowd's update steps are called only from UCrowdManager::Tick. The b395 row on
// UCrowdFollowingComponent::RegisterCrowdAgent saw 0 calls on both sides in run c569b395a, so agents come through
// another caller of UCrowdManager::RegisterAgent (SetCrowdSimulationState, or a call the rel32 scan does not see).
// b397 hooks UCrowdManager::RegisterAgent itself and names the caller by exe RVA. The agent is an
// ICrowdAgentInterface pointer inside its object, not an object start, so it is not named.
// UBWCOOP_C575_LOG=1 installs the row; otherwise it is not installed.
#include "ubw/hooks_internal.h"

#include <intrin.h>

namespace ubw::hooks {

C575RegisterFn g_orig_c575_register = nullptr;
bool g_c575_log = false; // written in ubw::init before any hook

namespace {

constexpr uint32_t kC575Rows = 24;    // every registration logged up to here
constexpr uint32_t kC575Every = 64;   // then every Nth one
uint32_t g_c575_count = 0;            // game thread only (agent registration)

} // namespace

void detour_c575_register(void* manager, void* agent) {
    ++g_c575_count;
    if (g_c575_count <= kC575Rows || g_c575_count % kC575Every == 0) {
        log::line("card575: %s t=%llu UCrowdManager::RegisterAgent #%u agent %p from exe+0x%llx, manager %s", side(),
                  static_cast<unsigned long long>(GetTickCount64()), g_c575_count, agent,
                  exe_rva(_ReturnAddress()), object_name(manager).text);
    }
    g_orig_c575_register(manager, agent);
}

} // namespace ubw::hooks
