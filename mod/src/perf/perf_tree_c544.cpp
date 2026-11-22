// Card 544 (performance mod family, card 527): pad the Chaos scene-query dynamic tree so moving bodies reinsert less.
//
// c263L364 host GameThread (10374 samples): Chaos::TAABBTree::FindBestSibling 4.3%, RotateNode 0.9%,
// UpdateElement 0.5% and UpdateAncestorBounds 0.5%, 6.2% in all. That is dynamic tree maintenance, apart from the
// queries themselves (QueryImp 9.2%). A dynamic tree leaf stores the body's bounds grown by
// p.aabbtree.DynamicTreeBoundingBoxPadding (engine default 5 cm). A move that stays inside the padded box only
// updates the payload. A move that leaves it removes the leaf and reinserts it (FindBestSibling, RotateNode,
// UpdateAncestorBounds). A bug running at several metres per second leaves a 5 cm pad almost every frame.
//
// Once per process, the first frame a local PC exists, this sets the padding to kC544PaddingCm through the PC's
// console command and logs the value read back before and after. Query results do not change: the padding only
// widens broad-phase boxes, and each candidate still passes the exact narrow-phase test. The cost moves to a few more
// broad-phase candidates per query, so the run must show the maintenance drop beating any QueryImp rise.
// Off switch: UBWCOOP_C544_OFF=1.

#include "ubw/hooks_internal.h"

#include <wchar.h>

namespace ubw::hooks {

bool g_c544_on = true; // written in ubw::init before any hook (UBWCOOP_C544_OFF=1 turns it off)

namespace {

constexpr wchar_t kC544Var[] = L"p.aabbtree.DynamicTreeBoundingBoxPadding";
constexpr wchar_t kC544Set[] = L"p.aabbtree.DynamicTreeBoundingBoxPadding 30";
constexpr int kC544PaddingCm = 30; // must match kC544Set
constexpr size_t kC544OutChars = 160;

bool g_c544_done = false; // game thread

// Runs `cmd` as the PC's console command and copies its output (the cvar readback) into `out`.
void c544_exec(void* pc, const wchar_t* cmd, wchar_t (&out)[kC544OutChars]) {
    const int32_t len = static_cast<int32_t>(wcslen(cmd)) + 1; // FString Num counts the terminator
    const ue::reflect::FStringRaw text{cmd, len, len};
    ue::reflect::FStringRaw result{};
    ue::input::g_pc_console_command(pc, &result, &text, false);
    out[0] = L'\0';
    if (result.data == nullptr) {
        return;
    }
    size_t n = 0;
    for (; n + 1 < kC544OutChars && result.data[n] != L'\0'; ++n) {
        const wchar_t c = result.data[n];
        out[n] = (c == L'\r' || c == L'\n') ? L' ' : c;
    }
    out[n] = L'\0';
    ue::reflect::g_memory_free(const_cast<wchar_t*>(result.data));
}

} // namespace

// Game thread, per frame (c42_step).
void c544_tick(void* engine) {
    if (!g_c544_on || g_c544_done || ue::input::g_pc_console_command == nullptr) {
        return;
    }
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    if (pc == nullptr) {
        return;
    }
    g_c544_done = true;
    wchar_t before[kC544OutChars];
    wchar_t set[kC544OutChars];
    wchar_t after[kC544OutChars];
    c544_exec(pc, kC544Var, before);
    c544_exec(pc, kC544Set, set);
    c544_exec(pc, kC544Var, after);
    // No side label: this runs at the main menu, before the session flag is set (c263L365).
    log::line("card544: t=%llu dynamic tree padding set to %d cm; before \"%ls\", set \"%ls\", after \"%ls\"",
              static_cast<unsigned long long>(GetTickCount64()),
              kC544PaddingCm, before, set, after);
}

} // namespace ubw::hooks
