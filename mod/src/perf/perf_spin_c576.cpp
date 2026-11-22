// Card 576 (card 573 finding B): shorter idle spin for the UE LowLevelTasks workers.
//
// LowLevelTasks::Private::FScheduler::WorkerLoop (exe 0x14117cc00) calls
// FWaitingQueue::CommitWait(this, event, out_of_work, SpinCycles, WaitCycles) with SpinCycles a fixed 53:
// `mov r9d, 0x35` at 0x14117d0d7 (41 B9 35 00 00 00). Each spin round checks the event, then waits WaitCycles TSC
// ticks (587 to 1597, from the CreateWorker table indexed by WorkerId & 7) in a pause + rdtsc loop (no waitpkg on
// the test CPU), and only then sleeps on the event. In run c569b390b this spin was about 3 CPU s per 20 s per side
// (10% of process CPU) in quiet and combat alike. No console variable sets it.
//
// The patch writes a smaller SpinCycles into that imm32 once, at start-up, before MH_Initialize. The task results
// are the same (a worker that stops spinning sleeps on its event and is woken by the same signal); only the wake
// latency of an idle worker changes, which the A/B checks from frame counts and worker CPU.
// UBWCOOP_C576_OFF=1 leaves the exe unchanged. UBWCOOP_C576_SPIN=N (0 to 53) sets the count; default kC576Spin.
#include "ubw/hooks_internal.h"

#include <string.h>

namespace ubw::hooks {

namespace {

constexpr uintptr_t kC576SiteRva = 0x117d0d7; // mov r9d, imm32 in WorkerLoop
constexpr uint8_t kC576Original[6] = {0x41, 0xB9, 0x35, 0x00, 0x00, 0x00};
constexpr uint32_t kC576Spin = 8;    // default patched SpinCycles
constexpr uint32_t kC576MaxSpin = 53; // the exe's own value; higher is never written

uint32_t c576_spin_from_env() {
    wchar_t buf[8] = {};
    const DWORD n = GetEnvironmentVariableW(L"UBWCOOP_C576_SPIN", buf, 8);
    if (n == 0 || n >= 8) {
        return kC576Spin;
    }
    uint32_t value = 0;
    for (DWORD i = 0; i < n; ++i) {
        if (buf[i] < L'0' || buf[i] > L'9') {
            log::line("card576: UBWCOOP_C576_SPIN is not a number, default %u used", kC576Spin);
            return kC576Spin;
        }
        value = value * 10 + static_cast<uint32_t>(buf[i] - L'0');
    }
    if (value > kC576MaxSpin) {
        log::line("card576: UBWCOOP_C576_SPIN %u over %u, default %u used", value, kC576MaxSpin, kC576Spin);
        return kC576Spin;
    }
    return value;
}

} // namespace

void c576_patch_worker_spin(uint8_t* base) {
    if (ue::config::read_switch(L"UBWCOOP_C576_OFF", "card 576 task worker spin patch off (A/B)")) {
        return;
    }
    uint8_t* const site = base + kC576SiteRva;
    if (memcmp(site, kC576Original, sizeof(kC576Original)) != 0) {
        log::line("WARN card576: WorkerLoop SpinCycles site bytes differ (%02X %02X %02X), not patched", site[0],
                  site[1], site[2]);
        return;
    }
    const uint32_t spin = c576_spin_from_env();
    DWORD old_protect = 0;
    if (!VirtualProtect(site + 2, 1, PAGE_EXECUTE_READWRITE, &old_protect)) {
        log::line("WARN card576: VirtualProtect RWX failed, err=%lu, not patched", GetLastError());
        return;
    }
    site[2] = static_cast<uint8_t>(spin); // spin <= 53: the upper imm32 bytes stay 0
    DWORD unused = 0;
    if (!VirtualProtect(site + 2, 1, old_protect, &unused)) {
        log::line("WARN card576: VirtualProtect restore failed, err=%lu", GetLastError());
    }
    FlushInstructionCache(GetCurrentProcess(), site, sizeof(kC576Original));
    log::line("card576: task worker SpinCycles 53 -> %u at exe+0x%llx", spin,
              static_cast<unsigned long long>(kC576SiteRva));
}

} // namespace ubw::hooks
