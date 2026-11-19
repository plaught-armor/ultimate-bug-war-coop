#include "autoinput.h"

#include "ue/log.h"

#include <windows.h>

#include <string.h>

namespace ubw::autoinput {
namespace log = ue::log;

bool g_running = false;

namespace {

// EInputEvent values (FInputKeyEventArgs ctor arg 4).
constexpr int32_t kEventPressed = 0;
constexpr int32_t kEventReleased = 1;
constexpr int32_t kEventAxis = 4;

// APlayerController vtable slot for InputKey(const FInputKeyEventArgs&): UGameViewportClient::InputKey
// +0x3CB..+0x3F4 and ::InputAxis +0x283..+0x2AC call [vtable + 0xDA8] on the local player's PC.
constexpr size_t kPcInputKeySlot = 0xDA8 / sizeof(void*);
// FKey: FName KeyName, then TSharedPtr<FKeyDetails> (object, controller) = 0x18 bytes; the
// FKey(const TCHAR*) ctor (0x015B71F0) writes the FName and zeroes +8 and +0x10.
constexpr size_t kFKeyBytes = 0x18;
// FInputKeyEventArgs: the ctor (0x03C9E340) writes up to +0x48 (timestamp at +0x40); the key copy at +0x10.
constexpr size_t kKeyArgsBytes = 0x48;
constexpr size_t kKeyArgsKey = 0x10;

constexpr ULONGLONG kExitDelayMs = 8000;

// Hold: Pressed at start, Released at end. Axis: one InputKey Axis event per tick. Mouse (card 016):
// one SendInput MOUSEEVENTF_MOVE per tick of `value` pixels on the axis named by key (L"X" or L"Y"),
// sent only while this process owns the foreground window. AxisOnce (card 020): one InputKey Axis
// event of `value` on the step's first tick; it goes through the PC like a real mouse delta, so it
// needs no foreground window (both instances run side by side, only one has focus).
enum class Kind : uint8_t { Hold, Axis, Mouse, AxisOnce };

struct Step {
    uint32_t start_ms; // after the script start
    uint32_t dur_ms;
    const wchar_t* key;
    Kind kind;
    float value; // Axis: the per-tick amount; Hold: the pressed amount; Mouse: pixels per tick
};

// Card 020 parity script, the same on the host's own pawn and on the client: idle, walk, look 30 right
// and 30 up, jump, sprint, crouch, look 60 down and fire 2 s at the ground, look 30 up and fire 2 s
// more (card 021), aim, interact, melee. Every hold is capped by dur_ms
// and its release is logged. Keys: IMC_Default_KBM / IMC_MI_KBM (cards 011-019); fire, aim and
// interact are the stock UE keys, checked against the card 020 input-map lines.
const Step kSteps[] = {
    {2000, 3000, L"W", Kind::Hold, 1.0f},
    {5500, 0, L"MouseX", Kind::AxisOnce, 30.0f},
    {6500, 0, L"MouseY", Kind::AxisOnce, 30.0f},
    {8000, 300, L"SpaceBar", Kind::Hold, 1.0f},
    {9500, 3000, L"LeftShift", Kind::Hold, 1.0f},
    {9500, 3000, L"W", Kind::Hold, 1.0f},
    {13500, 100, L"LeftControl", Kind::Hold, 1.0f},
    {15500, 0, L"MouseY", Kind::AxisOnce, -60.0f},
    {16000, 2000, L"LeftMouseButton", Kind::Hold, 1.0f},
    {18200, 0, L"MouseY", Kind::AxisOnce, 30.0f},
    {18500, 2000, L"LeftMouseButton", Kind::Hold, 1.0f},
    {21000, 1500, L"RightMouseButton", Kind::Hold, 1.0f},
    {23000, 600, L"E", Kind::Hold, 1.0f},
    {25000, 100, L"V", Kind::Hold, 1.0f},
};
constexpr uint32_t kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);

// Action labels for the card 020 rows: the action whose start is the latest one at or before t.
struct Action {
    uint32_t start_ms;
    const char* name;
};
const Action kActions[] = {
    {0, "idle"},      {2000, "walk"},   {5500, "look"},  {8000, "jump"},      {9500, "sprint"},
    {13500, "crouch"}, {15500, "aim-ground"}, {16000, "fire-ground"}, {18200, "aim-up"}, {18500, "fire-up"},
    {21000, "ads"},   {23000, "interact"}, {25000, "melee"},
};
constexpr uint32_t kActionCount = sizeof(kActions) / sizeof(kActions[0]);

struct Script {
    bool armed_this_map;
    bool net_client;
    bool done;
    bool slot_checked;
    bool slot_ok;
    bool no_pc_logged;
    ULONGLONG start_ms;
    ULONGLONG done_ms;
    uint32_t calls;
    uint32_t false_returns;
    bool mouse_state_logged;
    uint32_t action; // index into kActions of the last logged action
    bool action_logged;
    bool started[kStepCount];
    bool released[kStepCount];
    uint32_t mouse_sent[kStepCount];    // SendInput calls that returned 1
    uint32_t mouse_skipped[kStepCount]; // ticks not sent: this process did not own the foreground window
    uint32_t mouse_failed[kStepCount];  // SendInput calls that returned 0
};

// Card 016: user32 is resolved at runtime (GetModuleHandleW + GetProcAddress) so the DLL keeps
// KERNEL32-only imports. Resolved once per process on the first Mouse step.
using SendInputFn = UINT(WINAPI*)(UINT count, INPUT* inputs, int size);
using GetForegroundWindowFn = HWND(WINAPI*)();
using GetWindowThreadProcessIdFn = DWORD(WINAPI*)(HWND window, DWORD* pid);

struct User32 {
    bool resolved;
    bool ok;
    SendInputFn send_input;
    GetForegroundWindowFn foreground_window;
    GetWindowThreadProcessIdFn window_pid;
};

// ULocalPlayer::ViewportClient +0x78; UGameViewportClient capture mode +0x3BA, lock mode +0x3BC
// (see kLocalPlayerViewport / kViewportCaptureMode / kViewportLockMode in ubw/input_diag.cpp).
constexpr size_t kLocalPlayerViewport = 0x78;
constexpr size_t kViewportCaptureMode = 0x3BA;
constexpr size_t kViewportLockMode = 0x3BC;

Api g_api{};
bool g_enabled = false;
ULONGLONG g_start_delay_ms = 0; // set once by init()
bool g_injecting = false;
Script g_script{};
const Step* const g_steps = kSteps;
constexpr uint32_t g_step_count = kStepCount;
bool g_host_script = false;
User32 g_user32{};

using PcInputKeyFn = bool (*)(void* pc, const void* args);

// The PC's InputKey slot; on first use checks it against APlayerController::InputKey and logs once.
PcInputKeyFn pc_input_key(void* pc) {
    const auto fn = reinterpret_cast<PcInputKeyFn>((*static_cast<void* const* const*>(pc))[kPcInputKeySlot]);
    if (!g_script.slot_checked) {
        g_script.slot_checked = true;
        const auto* const p = reinterpret_cast<const uint8_t*>(fn);
        const unsigned long long rva = p >= g_api.exe_base ? static_cast<unsigned long long>(p - g_api.exe_base) : 0;
        g_script.slot_ok = rva == g_api.pc_input_key_rva;
        log::line("%sautoinput: PC vtable +0x%zX -> exe+0x%llX (APlayerController::InputKey exe+0x%X) %s",
                  g_script.slot_ok ? "" : "ERROR ", kPcInputKeySlot * sizeof(void*), rva, g_api.pc_input_key_rva,
                  g_script.slot_ok ? "match" : "MISMATCH -- script stopped, nothing sent");
    }
    return g_script.slot_ok ? fn : nullptr;
}

// One FInputKeyEventArgs through the PC's InputKey. Both FKeys are destroyed (refcount only).
void send(void* pc, const wchar_t* key_name, int32_t event, float amount) {
    const PcInputKeyFn input_key = pc_input_key(pc);
    if (input_key == nullptr) {
        g_running = false;
        return;
    }
    LARGE_INTEGER qpc{};
    if (!QueryPerformanceCounter(&qpc)) {
        log::line("WARN autoinput: QueryPerformanceCounter failed, err=%lu -- timestamp 0", GetLastError());
    }
    alignas(8) uint8_t key[kFKeyBytes] = {};
    alignas(8) uint8_t args[kKeyArgsBytes] = {};
    g_api.fkey_ctor(key, key_name);
    g_api.key_args_ctor(args, nullptr, 0, key, event, amount, false, static_cast<uint64_t>(qpc.QuadPart));
    g_injecting = true;
    const bool handled = input_key(pc, args);
    g_injecting = false;
    ++g_script.calls;
    g_script.false_returns += handled ? 0u : 1u;
    g_api.fkey_dtor(args + kKeyArgsKey);
    g_api.fkey_dtor(key);
}

FARPROC user32_proc(HMODULE user32, const char* name) {
    const FARPROC p = GetProcAddress(user32, name);
    if (p == nullptr) {
        log::line("ERROR autoinput: GetProcAddress(user32, %s) failed, err=%lu", name, GetLastError());
    }
    return p;
}

// Once per process. False (logged) when user32 or any export is missing: Mouse steps then send nothing.
bool resolve_user32() {
    if (g_user32.resolved) {
        return g_user32.ok;
    }
    g_user32.resolved = true;
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        log::line("ERROR autoinput: GetModuleHandleW(user32.dll) failed, err=%lu -- no SendInput", GetLastError());
        return false;
    }
    g_user32.send_input = reinterpret_cast<SendInputFn>(reinterpret_cast<void*>(user32_proc(user32, "SendInput")));
    g_user32.foreground_window =
        reinterpret_cast<GetForegroundWindowFn>(reinterpret_cast<void*>(user32_proc(user32, "GetForegroundWindow")));
    g_user32.window_pid = reinterpret_cast<GetWindowThreadProcessIdFn>(
        reinterpret_cast<void*>(user32_proc(user32, "GetWindowThreadProcessId")));
    g_user32.ok = g_user32.send_input != nullptr && g_user32.foreground_window != nullptr && g_user32.window_pid != nullptr;
    return g_user32.ok;
}

// True when the foreground window belongs to this process (the game window: the only top-level one).
bool own_foreground(DWORD* fg_pid) {
    const HWND fg = g_user32.foreground_window();
    DWORD pid = 0;
    if (fg != nullptr && g_user32.window_pid(fg, &pid) == 0) {
        log::line("WARN autoinput: GetWindowThreadProcessId failed, err=%lu", GetLastError());
    }
    *fg_pid = pid;
    return fg != nullptr && pid == GetCurrentProcessId();
}

// Once per map, at the first Mouse step: foreground owner and the viewport capture/lock modes.
void log_mouse_state(void* pc) {
    g_script.mouse_state_logged = true;
    DWORD fg_pid = 0;
    const bool fg = resolve_user32() && own_foreground(&fg_pid);
    const auto* const lp = static_cast<const uint8_t*>(g_api.get_local_player(pc));
    const uint8_t* viewport = nullptr;
    if (lp != nullptr) {
        memcpy(&viewport, lp + kLocalPlayerViewport, sizeof(viewport));
    }
    const unsigned capture = viewport != nullptr ? viewport[kViewportCaptureMode] : 0xFFu;
    // Card 018: SendInput mouse reaches the look only with the window in the foreground and capture mode 1.
    log::line("%sautoinput: mouse path: user32 %s, foreground window pid %lu, own pid %lu -> %s; viewport %p capture "
              "mode %u lock mode %u",
              fg && capture == 1u ? "" : "WARN ", g_user32.ok ? "resolved" : "MISSING", fg_pid, GetCurrentProcessId(),
              fg ? "foreground, SendInput on" : "NOT foreground, mouse steps send nothing",
              static_cast<const void*>(viewport), capture, viewport != nullptr ? viewport[kViewportLockMode] : 0xFFu);
}

// One relative MOUSEEVENTF_MOVE of the step's pixels, only while this process is in the foreground.
void send_mouse(uint32_t i) {
    DWORD fg_pid = 0;
    if (!resolve_user32() || !own_foreground(&fg_pid)) {
        ++g_script.mouse_skipped[i];
        return;
    }
    const Step& s = g_steps[i];
    const LONG px = static_cast<LONG>(s.value);
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = s.key[0] == L'X' ? px : 0;
    in.mi.dy = s.key[0] == L'Y' ? px : 0;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    if (g_user32.send_input(1, &in, static_cast<int>(sizeof(in))) == 1) {
        ++g_script.mouse_sent[i];
        return;
    }
    if (g_script.mouse_failed[i]++ == 0) {
        log::line("WARN autoinput: SendInput returned 0, err=%lu (step %u)", GetLastError(), i);
    }
}

// The world's first PC when it has a ULocalPlayer, else null.
void* local_pc(void* engine) {
    void* const world = engine != nullptr ? g_api.get_game_world(engine) : nullptr;
    void* const pc = world != nullptr ? g_api.world_own_pc(world) : nullptr;
    return (pc != nullptr && g_api.get_local_player(pc) != nullptr) ? pc : nullptr;
}

void step_one(void* pc, uint32_t i, ULONGLONG t) {
    const Step& s = g_steps[i];
    if (!g_script.started[i]) {
        if (t < s.start_ms) {
            return;
        }
        g_script.started[i] = true;
        static const char* const kKindText[] = {"hold", "axis", "mouse px", "axis once"};
        log::line("autoinput: step %u %ls %s %.1f for %u ms", i, s.key, kKindText[static_cast<uint8_t>(s.kind)],
                  static_cast<double>(s.value), s.dur_ms);
        if (s.kind == Kind::Mouse && !g_script.mouse_state_logged) {
            log_mouse_state(pc);
        }
        if (s.kind == Kind::Hold) {
            send(pc, s.key, kEventPressed, s.value);
        } else if (s.kind == Kind::AxisOnce) {
            send(pc, s.key, kEventAxis, s.value);
        }
    }
    if (g_script.released[i] || !g_running) {
        return;
    }
    if (t < static_cast<ULONGLONG>(s.start_ms) + s.dur_ms) {
        if (s.kind == Kind::Axis) {
            send(pc, s.key, kEventAxis, s.value);
        } else if (s.kind == Kind::Mouse) {
            send_mouse(i);
        }
        return;
    }
    g_script.released[i] = true;
    if (s.kind == Kind::Hold) {
        send(pc, s.key, kEventReleased, 0.0f);
        log::line("autoinput: step %u %ls released at its %u ms cap (t=%llu ms)", i, s.key, s.dur_ms,
                  static_cast<unsigned long long>(t));
    } else if (s.kind == Kind::Mouse) {
        log::line("autoinput: step %u mouse %ls %.0f px end: sent %u, skipped (not foreground) %u, failed %u", i, s.key,
                  static_cast<double>(s.value), g_script.mouse_sent[i], g_script.mouse_skipped[i],
                  g_script.mouse_failed[i]);
    }
}

bool all_released() {
    for (uint32_t i = 0; i < g_step_count; ++i) {
        if (!g_script.released[i]) {
            return false;
        }
    }
    return true;
}

void finish_step(ULONGLONG now) {
    if (!g_script.done) {
        g_script.done = true;
        g_script.done_ms = now;
        log::line("autoinput: script done (%u InputKey calls, %u returned false)%s", g_script.calls,
                  g_script.false_returns, g_script.net_client ? "; net client exits in 8000 ms" : "");
    }
    if (!g_script.net_client) {
        g_running = false;
        return;
    }
    if (now - g_script.done_ms < kExitDelayMs) {
        return;
    }
    g_running = false;
    log::line("autoinput: RequestEngineExit");
    g_api.request_exit(L"UBWCOOP_AUTOINPUT script done");
}

// The kActions index for script time t.
uint32_t action_at(ULONGLONG t) {
    uint32_t a = 0;
    for (uint32_t i = 1; i < kActionCount; ++i) {
        a = t >= kActions[i].start_ms ? i : a;
    }
    return a;
}

// One line per action change, so client action starts can be joined to host rows by tick time.
void log_action(ULONGLONG t) {
    const uint32_t a = action_at(t);
    if (a == g_script.action && g_script.action_logged) {
        return;
    }
    g_script.action = a;
    g_script.action_logged = true;
    log::line("autoinput: action %s (t=%llu ms)", kActions[a].name, static_cast<unsigned long long>(t));
}

} // namespace

const char* current_action() {
    if (!g_script.armed_this_map || GetTickCount64() < g_script.start_ms) {
        return "pre";
    }
    if (g_script.done) {
        return "done";
    }
    return kActions[action_at(GetTickCount64() - g_script.start_ms)].name;
}

void init(const Api& api, bool enabled, uint32_t start_delay_ms, bool host_sprint) {
    g_api = api;
    g_enabled = enabled;
    g_start_delay_ms = start_delay_ms;
    g_host_script = host_sprint;
}

bool key_event(void* pc, const wchar_t* key_name, bool pressed) {
    if (pc == nullptr || pc_input_key(pc) == nullptr) {
        return false;
    }
    send(pc, key_name, pressed ? kEventPressed : kEventReleased, pressed ? 1.0f : 0.0f);
    return true;
}

bool axis_event(void* pc, const wchar_t* key_name, float value) {
    if (pc == nullptr || pc_input_key(pc) == nullptr) {
        return false;
    }
    send(pc, key_name, kEventAxis, value);
    return true;
}

bool host_script() {
    return g_enabled && g_host_script;
}

bool enabled() {
    return g_enabled;
}

bool injecting() {
    return g_injecting;
}

bool arm(bool net_client) {
    if (!g_enabled || g_script.armed_this_map) {
        return false;
    }
    if (g_host_script && net_client) {
        log::line("WARN autoinput: UBWCOOP_HOST_SPRINT script is host-only; net client not armed");
        return false;
    }
    const Script previous = g_script;
    g_script = Script{};
    g_script.slot_checked = previous.slot_checked; // the vtable slot check is per process
    g_script.slot_ok = previous.slot_ok;
    g_script.armed_this_map = true;
    g_script.net_client = net_client;
    g_script.start_ms = GetTickCount64() + g_start_delay_ms;
    g_running = !previous.slot_checked || previous.slot_ok;
    log::line("autoinput: armed (%s); card 020 parity script of %u steps starts in %llu ms",
              g_host_script ? "host, after landing" : (net_client ? "net client" : "host/standalone"), g_step_count,
              static_cast<unsigned long long>(g_start_delay_ms));
    return true;
}

void tick(void* engine) {
    const ULONGLONG now = GetTickCount64();
    if (now < g_script.start_ms) {
        return;
    }
    if (g_script.done || all_released()) {
        finish_step(now);
        return;
    }
    void* const pc = local_pc(engine);
    if (pc == nullptr) {
        if (!g_script.no_pc_logged) {
            g_script.no_pc_logged = true;
            log::line("WARN autoinput: no local PlayerController this tick; script waits");
        }
        return;
    }
    const ULONGLONG t = now - g_script.start_ms;
    log_action(t);
    for (uint32_t i = 0; i < g_step_count && g_running; ++i) {
        step_one(pc, i, t);
    }
}

void load_map_enter(void* engine) {
    if (!g_enabled) {
        return;
    }
    void* const pc = g_running ? local_pc(engine) : nullptr;
    uint32_t released = 0;
    for (uint32_t i = 0; i < g_step_count && pc != nullptr; ++i) {
        if (g_script.started[i] && !g_script.released[i] && g_steps[i].kind == Kind::Hold) {
            send(pc, g_steps[i].key, kEventReleased, 0.0f);
            ++released;
        }
    }
    if (g_running || g_script.armed_this_map) {
        log::line("autoinput: LoadMap: released %u held keys (local PC %p), disarmed", released, pc);
    }
    g_running = false;
    g_script.armed_this_map = false;
}

} // namespace ubw::autoinput
