// Lobby UI: the lobby overlay render test and its script (card 031), and the co-op lobby panels at the
// main menu and in game with their test driver (card 052).

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

void c52_step(void* engine); // card 052: co-op lobby panel at the main menu

using C31KeyStateFn = SHORT(WINAPI*)(int vk);
using C31ForegroundFn = HWND(WINAPI*)();
using C31WindowPidFn = DWORD(WINAPI*)(HWND window, DWORD* pid);
using C31SendInputFn = UINT(WINAPI*)(UINT count, INPUT* inputs, int size);
using C31EnumWindowsFn = BOOL(WINAPI*)(WNDENUMPROC proc, LPARAM param);
using C31SetForegroundFn = BOOL(WINAPI*)(HWND window);
using C31WindowVisibleFn = BOOL(WINAPI*)(HWND window);
ScoParamsCtorFn g_sco_params_ctor = nullptr;
ScoInternalFn g_sco_internal = nullptr;
StaticClassFn g_user_widget_class = nullptr;
StaticClassFn g_canvas_panel_class = nullptr;
StaticClassFn g_widget_tree_class = nullptr;
WidgetFn g_panel_clear_children = nullptr;
PanelCountFn g_panel_child_count = nullptr;
PanelChildAtFn g_panel_child_at = nullptr;
SlotSetPositionFn g_slot_set_position = nullptr;
SlotSetAutoSizeFn g_slot_set_auto_size = nullptr;
TextBlockSetTextFn g_text_block_set_text = nullptr;
InputModeCtorFn g_input_mode_game_and_ui_ctor = nullptr;
InputModeCtorFn g_input_mode_ui_only_ctor = nullptr;

constexpr size_t kClassFlags = 0xD4;
constexpr uint32_t kClassAbstract = 0x1;
constexpr size_t kFTextBytes = 0x10;
constexpr size_t kInputModeBytes = 0x20; // the largest of the three (UIOnly / GameAndUI end at +0x1A)

constexpr uint8_t kVisCollapsed = 1;
constexpr uint8_t kVisSelfHitTestInvisible = 4; // the canvas lets clicks through; its children still hit-test
constexpr int32_t kC31ZOrder = 100;
constexpr uint32_t kC31Logs = 32;
constexpr int32_t kC31MaxChildren = 16;
constexpr wchar_t kC31ClassPath[] =
    L"/Game/Blueprints/UI_Aloft/Gameplay_HUD/WB_TutorialPrompt.WB_TutorialPrompt_C";
constexpr wchar_t kC31Label[] = L"UBW co-op: H host, J join";
constexpr wchar_t kC31MissionUrl[] = L"/Game/Maps/FED/FED_C1M1";

struct C31Key {
    int vk;
    const char* name;
};
constexpr C31Key kC31Keys[] = {{0x77, "F8"}, {0x0D, "Enter"}, {0x48, "H"}, {0x4A, "J"}}; // VK_F8, VK_RETURN
constexpr uint32_t kC31KeyF8 = 0;
constexpr uint32_t kC31KeyEnter = 1;

struct C31User32 {
    C31KeyStateFn key_state;
    C31ForegroundFn foreground;
    C31WindowPidFn window_pid;
    C31SendInputFn send_input;
    C31EnumWindowsFn enum_windows;     // test input only (c31_take_foreground); null: the test waits for a click
    C31SetForegroundFn set_foreground;
    C31WindowVisibleFn window_visible;
    bool tried;
    bool ok;
};

config::OverlayTest g_c31_mode = config::OverlayTest::Off; // written in ubw::init before any hook
C31State g_c31{};
C31User32 g_c31_user32{};

bool c31_line() {
    return note_event_cap(g_c31.logs, kC31Logs, "card031");
}

void* c31_weak_get(const C31Weak& w) {
    return g_weak_get != nullptr ? g_weak_get(&w) : nullptr; // unresolved: no object (callers treat it as gone)
}

void c31_weak_set(C31Weak& w, void* object) {
    if (object == nullptr || g_weak_assign == nullptr) {
        w = C31Weak{};
        return;
    }
    g_weak_assign(&w, &object);
}

FARPROC c31_user32_proc(HMODULE user32, const char* name) {
    const FARPROC p = GetProcAddress(user32, name);
    if (p == nullptr) {
        log::line("ERROR card031: GetProcAddress(user32, %s) failed, err=%lu", name, GetLastError());
    }
    return p;
}

// Once per process. False (logged once) when user32 or an export is missing: no key polling then.
bool c31_resolve_user32() {
    C31User32& u = g_c31_user32;
    if (u.tried) {
        return u.ok;
    }
    u.tried = true;
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        log::line("ERROR card031: GetModuleHandleW(user32.dll) failed, err=%lu -- no key polling", GetLastError());
        return false;
    }
    u.key_state = reinterpret_cast<C31KeyStateFn>(reinterpret_cast<void*>(c31_user32_proc(user32, "GetAsyncKeyState")));
    u.foreground = reinterpret_cast<C31ForegroundFn>(reinterpret_cast<void*>(c31_user32_proc(user32, "GetForegroundWindow")));
    u.window_pid =
        reinterpret_cast<C31WindowPidFn>(reinterpret_cast<void*>(c31_user32_proc(user32, "GetWindowThreadProcessId")));
    u.send_input = reinterpret_cast<C31SendInputFn>(reinterpret_cast<void*>(c31_user32_proc(user32, "SendInput")));
    u.enum_windows = reinterpret_cast<C31EnumWindowsFn>(reinterpret_cast<void*>(c31_user32_proc(user32, "EnumWindows")));
    u.set_foreground =
        reinterpret_cast<C31SetForegroundFn>(reinterpret_cast<void*>(c31_user32_proc(user32, "SetForegroundWindow")));
    u.window_visible =
        reinterpret_cast<C31WindowVisibleFn>(reinterpret_cast<void*>(c31_user32_proc(user32, "IsWindowVisible")));
    u.ok = u.key_state != nullptr && u.foreground != nullptr && u.window_pid != nullptr && u.send_input != nullptr;
    log::line("card031: user32 %s (GetAsyncKeyState, GetForegroundWindow, GetWindowThreadProcessId, SendInput)",
              u.ok ? "resolved" : "INCOMPLETE -- no key polling");
    return u.ok;
}

// True when the foreground window belongs to this process (keys typed elsewhere are ignored).
bool c31_own_foreground() {
    const HWND fg = g_c31_user32.foreground();
    DWORD pid = 0;
    if (fg != nullptr && g_c31_user32.window_pid(fg, &pid) == 0) {
        return false; // GetWindowThreadProcessId failed: treated as not ours (polled every tick: not logged)
    }
    return fg != nullptr && pid == GetCurrentProcessId();
}

// EnumWindows callback: the first visible top-level window of this process, into *param.
static BOOL CALLBACK c31_own_window_cb(HWND window, LPARAM param) {
    DWORD pid = 0;
    if (g_c31_user32.window_pid(window, &pid) != 0 && pid == GetCurrentProcessId() &&
        g_c31_user32.window_visible(window)) {
        *reinterpret_cast<HWND*>(param) = window;
        return FALSE; // found: stop
    }
    return TRUE;
}

// Test input only (card 031 script, card 052 test): input is sent with SendInput, which goes to the foreground
// window, so a test that waits for input brings its own game window to the foreground itself (both lane games share
// one Wine desktop; no click needed). Returns true when it asked; the caller retries the input on a later tick.
// `logged` caps the line at one per call site.
static bool c31_take_foreground(const char* card, bool& logged) {
    const C31User32& u = g_c31_user32;
    HWND own = nullptr;
    if (u.enum_windows != nullptr && u.set_foreground != nullptr && u.window_visible != nullptr) {
        u.enum_windows(&c31_own_window_cb, reinterpret_cast<LPARAM>(&own));
    }
    const BOOL ok = own != nullptr ? u.set_foreground(own) : FALSE;
    if (!logged) {
        logged = true;
        log::line("%s%s: test: game window not in the foreground -- %s", own != nullptr && ok ? "" : "WARN ", card,
                  own == nullptr ? "own window not found, input held until it is foreground"
                  : ok           ? "SetForegroundWindow on the own window, input on a later tick"
                                 : "SetForegroundWindow failed, input held until it is foreground");
    }
    return ok != FALSE;
}

uint32_t c31_class_flags(const void* cls) {
    uint32_t flags = 0;
    memcpy(&flags, static_cast<const uint8_t*>(cls) + kClassFlags, sizeof(flags));
    return flags;
}

void c31_log_class_flags() {
    void* const classes[] = {g_user_widget_class(), g_canvas_panel_class(), g_text_block_class(),
                             g_editable_text_box_class(), g_widget_tree_class()};
    for (void* const cls : classes) {
        const uint32_t flags = c31_class_flags(cls);
        log::line("card031: class %s %p ClassFlags 0x%08X (%s)", object_name(cls).text, cls, flags,
                  (flags & kClassAbstract) != 0 ? "abstract" : "concrete");
    }
}

// The overlay's widget class: found in memory if loaded, else loaded. Logs which path found it.
void* c31_find_widget_class() {
    void* const meta = g_uclass_class();
    void* cls = g_static_find_object(meta, nullptr, kC31ClassPath, false);
    const char* how = "StaticFindObject (already loaded)";
    if (cls == nullptr) {
        cls = g_static_load_object(meta, nullptr, kC31ClassPath, nullptr, kC29LoadFlags, nullptr, true, nullptr);
        how = "StaticLoadObject";
    }
    if (cls == nullptr) {
        log::line("ERROR card031: widget class %ls not found (StaticFindObject and StaticLoadObject null)",
                  kC31ClassPath);
        return nullptr;
    }
    const uint32_t flags = c31_class_flags(cls);
    const bool user_widget = is_a(cls, meta);
    log::line("card031: widget class %s %p via %s, ClassFlags 0x%08X (%s)%s", object_name(cls).text, cls, how, flags,
              (flags & kClassAbstract) != 0 ? "abstract" : "concrete", user_widget ? "" : " -- NOT a UClass");
    return (flags & kClassAbstract) == 0 && user_widget ? cls : nullptr;
}

// A reflected object property's value (nullptr when the property is missing or not an object one).
void* c31_object_prop(const void* object, const char* name) {
    const uint8_t* prop = nullptr;
    return const_cast<void*>(prop_object_field(object, name, &prop));
}

bool c31_is_panel(const void* widget) {
    return widget != nullptr && prop_find_prop(read_ptr(widget, kObjectClass), "Slots") != nullptr;
}

// One line per widget: the panel itself, then its children (and one more level for child panels).
void c31_log_panel(const char* what, const void* panel, int depth) {
    const int32_t n = g_panel_child_count(panel);
    log::line("card031: %s %*s%s %s: %d children", what, depth * 2, "", class_name(panel).text,
              object_name(panel).text, n);
    const int32_t shown = n < kC31MaxChildren ? n : kC31MaxChildren;
    for (int32_t i = 0; i < shown; ++i) {
        const void* const child = g_panel_child_at(panel, i);
        if (depth == 0 && c31_is_panel(child)) {
            c31_log_panel(what, child, 1); // depth 1 does not recurse again
            continue;
        }
        log::line("card031: %s %*s  [%d] %s %s", what, depth * 2, "", i, class_name(child).text,
                  object_name(child).text);
    }
}

void c31_log_tree(const char* what, const void* widget) {
    const void* const tree = c31_object_prop(widget, "WidgetTree");
    const void* const root = tree != nullptr ? c31_object_prop(tree, "RootWidget") : nullptr;
    log::line("card031: %s: widget %s, WidgetTree %s %p, RootWidget %s %s", what, object_name(widget).text,
              class_name(tree).text, tree, class_name(root).text, object_name(root).text);
    if (c31_is_panel(root)) {
        c31_log_panel(what, root, 0);
    }
}

// FText::FromString into a 16-byte buffer; SetText takes it by value and releases it.
void c31_set_text(void* text_block, const wchar_t* text) {
    alignas(8) uint8_t ftext[kFTextBytes] = {};
    g_ftext_from_string(ftext, text);
    g_text_block_set_text(text_block, ftext); // consumed: no ~FText here
}

// Canvas slot LayoutData.Offsets (FMargin floats): Left/Top = position (SetPosition), Right/Bottom =
// size when the anchors are not stretched. Written before Slate builds the slot (no SetSize export).
void c31_slot_layout(void* slot, double x, double y, float w, float h) {
    const double pos[2] = {x, y};
    g_slot_set_position(slot, pos);
    g_slot_set_auto_size(slot, w <= 0.0f);
    const uint8_t* const prop = prop_find_prop(read_ptr(slot, kObjectClass), "LayoutData");
    if (prop == nullptr) {
        log::line("ERROR card031: %s has no LayoutData property -- size not set", class_name(slot).text);
        return;
    }
    auto* const margin = static_cast<uint8_t*>(slot) + read_i32(prop, kPropertyOffset);
    float m[4] = {};
    memcpy(m, margin, sizeof(m));
    const bool pos_ok = m[0] == static_cast<float>(x) && m[1] == static_cast<float>(y);
    if (pos_ok && w > 0.0f) {
        memcpy(margin + 8, &w, sizeof(w));
        memcpy(margin + 12, &h, sizeof(h));
    }
    log::line("card031: slot %s LayoutData.Offsets after SetPosition (%.0f, %.0f): %.1f %.1f %.1f %.1f%s",
              object_name(slot).text, x, y, m[0], m[1], m[2], m[3],
              pos_ok ? (w > 0.0f ? ", size written" : ", auto size") : " -- MISMATCH, size not written");
}

// Adds a new widget of `cls` to `canvas`; returns the widget (nullptr logged).
void* c31_add(void* tree, void* canvas, void* cls, double x, double y, float w, float h) {
    void* const widget = c31_construct(cls, tree);
    void* const slot = widget != nullptr ? g_panel_add_child(canvas, widget) : nullptr;
    if (slot == nullptr) {
        log::line("ERROR card031: AddChild(%s) failed (widget %p)", object_name(cls).text, widget);
        return nullptr;
    }
    log::line("card031: built %s %s (outer %s), slot %s %s", class_name(widget).text, object_name(widget).text,
              object_name(read_ptr(widget, kObjectOuter)).text, class_name(slot).text, object_name(slot).text);
    c31_slot_layout(slot, x, y, w, h);
    return widget;
}

// The tree's root as a cleared canvas: ClearChildren when it is one, else a new UCanvasPanel root.
void* c31_root_canvas(void* tree) {
    void* const root = c31_object_prop(tree, "RootWidget");
    if (root != nullptr && is_a(root, g_canvas_panel_class())) {
        g_panel_clear_children(root);
        log::line("card031: root %s is a CanvasPanel: ClearChildren", object_name(root).text);
        return root;
    }
    const uint8_t* const prop = prop_find_prop(read_ptr(tree, kObjectClass), "RootWidget");
    void* const canvas = prop != nullptr ? c31_construct(g_canvas_panel_class(), tree) : nullptr;
    if (canvas == nullptr) {
        log::line("ERROR card031: no CanvasPanel root (root %s, RootWidget property %p)", class_name(root).text,
                  static_cast<const void*>(prop));
        return nullptr;
    }
    memcpy(static_cast<uint8_t*>(tree) + read_i32(prop, kPropertyOffset), &canvas, sizeof(canvas));
    log::line("card031: root %s replaced by new CanvasPanel %s", class_name(root).text, object_name(canvas).text);
    return canvas;
}

// Creates the overlay for `pc`, replaces its tree and adds it to the viewport. True when built.
bool c31_build(void* pc) {
    c31_log_class_flags();
    void* const cls = c31_find_widget_class();
    void* const widget = cls != nullptr ? g_create_widget_instance(pc, &cls, 0) : nullptr;
    void* const tree = widget != nullptr ? c31_object_prop(widget, "WidgetTree") : nullptr;
    if (tree == nullptr) {
        log::line("ERROR card031: CreateWidgetInstance(pc=%p, %s) gave widget %p, WidgetTree null", pc,
                  object_name(cls).text, widget);
        return false;
    }
    log::line("card031: CreateWidgetInstance -> %s %s, outer %s %s", class_name(widget).text,
              object_name(widget).text, class_name(read_ptr(widget, kObjectOuter)).text,
              object_name(read_ptr(widget, kObjectOuter)).text);
    c31_log_tree("tree before", widget);
    void* const canvas = c31_root_canvas(tree);
    void* const label = canvas != nullptr ? c31_add(tree, canvas, g_text_block_class(), 40.0, 40.0, 0.0f, 0.0f) : nullptr;
    void* const box = label != nullptr ? c31_add(tree, canvas, g_editable_text_box_class(), 40.0, 110.0, 480.0f, 48.0f)
                                       : nullptr;
    if (box == nullptr) {
        return false;
    }
    c31_set_text(label, kC31Label);
    c31_log_tree("tree after", widget);
    g_add_to_viewport(widget, kC31ZOrder);
    g_widget_set_visibility(widget, kVisCollapsed);
    c31_weak_set(g_c31.widget, widget);
    c31_weak_set(g_c31.box, box);
    log::line("card031: AddToViewport(%s, z %d) done, built collapsed", object_name(widget).text, kC31ZOrder);
    return true;
}

// Card 345b: a one-line text overlay for `pc`: a new widget of the overlay class with one UTextBlock, added to the
// viewport. Returns the widget (the caller removes it); nullptr is logged.
void* c31_text_overlay(void* pc, const wchar_t* text, double x, double y) {
    void* cls = c31_find_widget_class();
    void* const widget = cls != nullptr ? g_create_widget_instance(pc, &cls, 0) : nullptr;
    void* const tree = widget != nullptr ? c31_object_prop(widget, "WidgetTree") : nullptr;
    void* const canvas = tree != nullptr ? c31_root_canvas(tree) : nullptr;
    void* const label = canvas != nullptr ? c31_add(tree, canvas, g_text_block_class(), x, y, 0.0f, 0.0f) : nullptr;
    if (label == nullptr) {
        log::line("ERROR card345b: text overlay not built (widget %p, WidgetTree %p, canvas %p)", widget, tree, canvas);
        return nullptr;
    }
    c31_set_text(label, text);
    g_add_to_viewport(widget, kC31ZOrder);
    return widget;
}

// The overlay to show: reused when alive and built in this world, else rebuilt for `pc`.
void* c31_widget_for(void* pc) {
    void* const widget = c31_weak_get(g_c31.widget);
    void* const box = c31_weak_get(g_c31.box);
    if (widget != nullptr && box != nullptr && g_c31.built_world == g_c31.world_id) {
        return widget;
    }
    if (widget != nullptr) {
        log::line("card031: overlay %s alive from an earlier world (outer %s %s): RemoveFromParent, rebuild",
                  object_name(widget).text, class_name(read_ptr(widget, kObjectOuter)).text,
                  object_name(read_ptr(widget, kObjectOuter)).text);
        g_widget_remove_from_parent(widget);
    }
    if (!c31_build(pc)) {
        return nullptr;
    }
    g_c31.built_world = g_c31.world_id;
    return c31_weak_get(g_c31.widget);
}

// The game's own input mode, as last recorded: UIOnly through the native UIOnlyEx with the recorded
// focus object, the others through their ctor and SetInputMode. Never forces GameOnly.
void c31_restore_mode(void* pc) {
    alignas(8) uint8_t mode[kInputModeBytes] = {};
    void* const focus = c31_weak_get(g_c31.game_focus);
    g_c31.own_mode_call = true;
    switch (g_c31.game_kind) {
    case C31Kind::UIOnly:
        g_ui_only_ex(pc, is_a(focus, g_user_widget_class()) ? focus : nullptr, g_c31.game_lock, false);
        break;
    case C31Kind::GameAndUI:
        g_input_mode_game_and_ui_ctor(mode);
        mode[kInputModeLock] = g_c31.game_lock;
        mode[kInputModeHideCursor] = g_c31.game_flag;
        g_orig_set_input_mode(pc, mode);
        break;
    case C31Kind::GameOnly:
        g_input_mode_game_only_ctor(mode);
        mode[kInputModeConsumeMouse] = g_c31.game_flag;
        g_orig_set_input_mode(pc, mode);
        break;
    case C31Kind::None:
        break;
    }
    g_c31.own_mode_call = false;
    if (c31_line()) {
        log::line("card031: restored the game's input mode %s (lock %u, flag %u, focus %s %s)",
                  kC31KindText[static_cast<uint8_t>(g_c31.game_kind)], g_c31.game_lock, g_c31.game_flag,
                  class_name(focus).text, object_name(focus).text);
    }
}

void c31_toggle(void* pc) {
    if (g_c31.shown) {
        void* const widget = c31_weak_get(g_c31.widget);
        if (widget != nullptr) {
            g_widget_set_visibility(widget, kVisCollapsed);
        }
        g_c31.shown = false;
        g_c31.focus_pending = false;
        if (c31_line()) {
            log::line("card031: F8: overlay hidden (widget %s)", widget != nullptr ? "alive" : "GONE");
        }
        c31_restore_mode(pc);
        return;
    }
    void* const widget = c31_widget_for(pc);
    if (widget == nullptr) {
        return; // c31_build logged why
    }
    g_widget_set_visibility(widget, kVisSelfHitTestInvisible);
    g_c31.shown = true;
    g_c31.focus_pending = true;
    if (c31_line()) {
        log::line("card031: F8: overlay shown (%s); game input mode on record %s", object_name(widget).text,
                  kC31KindText[static_cast<uint8_t>(g_c31.game_kind)]);
    }
}

// One tick after showing: UIOnly with the box as focus, then UWidget::SetKeyboardFocus on it.
void c31_focus(void* pc) {
    g_c31.focus_pending = false;
    void* const box = c31_weak_get(g_c31.box);
    if (box == nullptr) {
        log::line("ERROR card031: text box gone before focus");
        return;
    }
    g_c31.own_mode_call = true;
    g_ui_only_ex(pc, box, 0, false); // EMouseLockMode::DoNotLock, no input flush
    g_c31.own_mode_call = false;
    g_widget_set_keyboard_focus(box);
    if (c31_line()) {
        log::line("card031: SetInputMode_UIOnlyEx(pc=%p, %s, DoNotLock) + SetKeyboardFocus", pc, object_name(box).text);
    }
}

void c31_log_box_text() {
    const void* const box = c31_weak_get(g_c31.box);
    const uint8_t* const prop = box != nullptr ? prop_find_prop(read_ptr(box, kObjectClass), "Text") : nullptr;
    if (prop == nullptr) {
        log::line("ERROR card031: Enter: text box %p, Text property %p", box, static_cast<const void*>(prop));
        return;
    }
    const FStringRaw* const s =
        g_ftext_to_string(static_cast<const uint8_t*>(box) + read_i32(prop, kPropertyOffset));
    const bool ok = s != nullptr && s->data != nullptr && s->num > 0 && s->num <= kMaxLoggedFStringNum;
    log::line("card031: Enter: text box \"%.*ls\" (%d chars)", ok ? s->num - 1 : 0, ok ? s->data : L"",
              ok ? s->num - 1 : 0);
}

// Edge-detects kC31Keys; returns the bits that went down since the previous poll.
uint8_t c31_poll_keys() {
    uint8_t now = 0;
    if (c31_own_foreground()) {
        for (uint32_t i = 0; i < sizeof(kC31Keys) / sizeof(kC31Keys[0]); ++i) {
            now |= (g_c31_user32.key_state(kC31Keys[i].vk) & 0x8000) != 0 ? static_cast<uint8_t>(1u << i) : 0u;
        }
    }
    const uint8_t pressed = static_cast<uint8_t>(now & ~g_c31.keys);
    g_c31.keys = now;
    return pressed;
}

void c31_handle_keys(void* pc, uint8_t pressed) {
    for (uint32_t i = 0; i < sizeof(kC31Keys) / sizeof(kC31Keys[0]); ++i) {
        const bool hit = (pressed & (1u << i)) != 0;
        if (hit && i != kC31KeyF8 && note_event_cap(g_c31.key_logs, kC31Logs, "card031 key")) {
            log::line("card031: key %s down (overlay %s)", kC31Keys[i].name, g_c31.shown ? "shown" : "hidden");
        }
    }
    if ((pressed & (1u << kC31KeyEnter)) != 0 && g_c31.shown) {
        c31_log_box_text();
    }
    if ((pressed & (1u << kC31KeyF8)) != 0) {
        c31_toggle(pc);
    }
}

// Per world: the map's package name and whether the overlay survived the travel.
void c31_new_world(void* world, const void* pc) {
    ++g_c31.worlds;
    const void* const package = read_ptr(world, kObjectOuter);
    void* const widget = c31_weak_get(g_c31.widget);
    const bool was_shown = g_c31.shown;
    g_c31.shown = false; // the game sets its own input mode on the new map
    g_c31.focus_pending = false;
    log::line("card031: world #%u %s (package %s, net mode %u): overlay widget %s%s%s", g_c31.worlds,
              object_name(world).text, object_name(package).text, net_mode(pc), g_c31.widget.serial == 0 ? "never built" : (widget != nullptr ? "ALIVE" : "gone"),
              widget != nullptr ? ", outer " : "", widget != nullptr ? object_name(read_ptr(widget, kObjectOuter)).text : "");
    if (was_shown) {
        log::line("card031: overlay was shown at travel: marked hidden");
    }
}

// --- Card 031 script (UBWCOOP_OVERLAY_TEST=script): drives the test with SendInput ----------
// Rows run in order; `at_ms` counts from the start of their phase (phase 0 = the menu world the
// game settles on, 1 = the mission, 2 = the menu again). Keys are separate down/up rows so the
// per-tick GetAsyncKeyState poll sees them. Input is only sent while this process owns the
// foreground window, so it can never land in another program.
enum class C31Act : uint8_t { KeyDown, KeyUp, Type, Shot, TravelMission, TravelMenu, Done };
struct C31Row {
    uint8_t phase;
    uint32_t at_ms;
    C31Act act;
    uint16_t vk;
};
constexpr uint16_t kC31VkF8 = 0x77;
constexpr uint16_t kC31VkEnter = 0x0D;
constexpr uint16_t kC31VkDown = 0x28;
constexpr wchar_t kC31Typed[] = L"127.0.0.1:7777";
constexpr C31Row kC31Script[] = {
    {0, 8000, C31Act::KeyDown, kC31VkF8},     {0, 8150, C31Act::KeyUp, kC31VkF8},
    {0, 9500, C31Act::Shot, 0},               {0, 10000, C31Act::Type, 0},
    {0, 11500, C31Act::Shot, 0},              {0, 12000, C31Act::KeyDown, kC31VkEnter},
    {0, 12150, C31Act::KeyUp, kC31VkEnter},   {0, 13000, C31Act::KeyDown, kC31VkF8},
    {0, 13150, C31Act::KeyUp, kC31VkF8},      {0, 14000, C31Act::Shot, 0},
    {0, 15000, C31Act::KeyDown, kC31VkDown},  {0, 15150, C31Act::KeyUp, kC31VkDown},
    {0, 17000, C31Act::KeyDown, kC31VkDown},  {0, 17150, C31Act::KeyUp, kC31VkDown},
    {0, 18000, C31Act::Shot, 0},              {0, 19000, C31Act::TravelMission, 0},
    {1, 20000, C31Act::TravelMenu, 0},        {2, 8000, C31Act::KeyDown, kC31VkF8},
    {2, 8150, C31Act::KeyUp, kC31VkF8},       {2, 9500, C31Act::Shot, 0},
    {2, 10000, C31Act::KeyDown, kC31VkF8},    {2, 10150, C31Act::KeyUp, kC31VkF8},
    {2, 11000, C31Act::Shot, 0},              {2, 11500, C31Act::Done, 0},
};
constexpr uint32_t kC31ScriptRows = sizeof(kC31Script) / sizeof(kC31Script[0]);
constexpr const char* kC31ActText[] = {"key down", "key up", "type", "shot", "travel to mission", "travel to menu",
                                       "done"};
constexpr uint32_t kC31TypeMax = 32; // INPUT pairs per Type row
constexpr uint32_t kC31PathChars = 128;

struct C31Script {
    uint32_t step;
    uint8_t phase;
    bool waiting_travel;   // a travel row ran; the next world starts the next phase
    bool foreground_logged;
    ULONGLONG phase_t0;
    wchar_t menu_path[kC31PathChars];
};
C31Script g_c31_script{};

// One keyboard INPUT: a virtual key (wVk) or, with KEYEVENTF_UNICODE, a character (wScan).
INPUT c31_key_input(uint16_t vk, wchar_t ch, bool up) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = static_cast<WORD>(ch);
    in.ki.dwFlags = (ch != 0 ? KEYEVENTF_UNICODE : 0u) | (up ? KEYEVENTF_KEYUP : 0u);
    return in;
}

bool c31_send(INPUT* inputs, uint32_t count) {
    const UINT sent = g_c31_user32.send_input(count, inputs, static_cast<int>(sizeof(INPUT)));
    if (sent != count) {
        log::line("ERROR card031: SendInput sent %u of %u inputs, err=%lu", sent, count, GetLastError());
        return false;
    }
    return true;
}


bool c31_type(const wchar_t* text) {
    INPUT inputs[2 * kC31TypeMax] = {};
    uint32_t n = 0;
    for (uint32_t i = 0; i < kC31TypeMax && text[i] != L'\0'; ++i) {
        inputs[n++] = c31_key_input(0, text[i], false);
        inputs[n++] = c31_key_input(0, text[i], true);
    }
    return c31_send(inputs, n);
}

void c31_travel(void* engine, void* world, const wchar_t* url) {
    g_set_client_travel(engine, world, url, kTravelAbsolute);
    g_c31_script.waiting_travel = true;
    log::line("card031: script: SetClientTravel(%ls)", url);
}

// Runs one row; false when it must be retried on a later tick (input while not in the foreground).
bool c31_run_row(const C31Row& row, void* engine, void* world, void* pc) {
    const bool input = row.act == C31Act::KeyDown || row.act == C31Act::KeyUp || row.act == C31Act::Type;
    if (input && !c31_own_foreground()) {
        c31_take_foreground("card031", g_c31_script.foreground_logged);
        return false;
    }
    INPUT key = c31_key_input(row.vk, 0, row.act == C31Act::KeyUp);
    switch (row.act) {
    case C31Act::KeyDown:
    case C31Act::KeyUp:
        c31_send(&key, 1);
        break;
    case C31Act::Type:
        c31_type(kC31Typed);
        break;
    case C31Act::Shot:
        request_shot(pc, true);
        break;
    case C31Act::TravelMission:
        c31_travel(engine, world, kC31MissionUrl);
        break;
    case C31Act::TravelMenu:
        c31_travel(engine, world, g_c31_script.menu_path);
        break;
    case C31Act::Done:
        log::line("card031: script done");
        break;
    }
    return true;
}

void c31_script_step(void* engine, void* world, void* pc) {
    C31Script& s = g_c31_script;
    if (s.step >= kC31ScriptRows || s.waiting_travel) {
        return;
    }
    const C31Row& row = kC31Script[s.step];
    const ULONGLONG at = s.phase_t0 + row.at_ms;
    if (row.phase != s.phase || GetTickCount64() < at) {
        return;
    }
    if (!c31_run_row(row, engine, world, pc)) {
        return;
    }
    log::line("card031: script row %u (phase %u, %u ms): %s vk 0x%02X", s.step, row.phase, row.at_ms,
              kC31ActText[static_cast<uint8_t>(row.act)], row.vk);
    ++s.step;
}

// A new world: before the first row it restarts phase 0 (and records the menu path for the trip
// back); after a travel row it starts the next phase.
void c31_script_world(void* world) {
    C31Script& s = g_c31_script;
    if (s.step == 0) {
        const ObjectName package = object_name(read_ptr(world, kObjectOuter));
        uint32_t i = 0;
        for (; i < kC31PathChars - 1 && package.text[i] != '\0'; ++i) {
            s.menu_path[i] = static_cast<wchar_t>(package.text[i]);
        }
        s.menu_path[i] = L'\0';
        s.phase_t0 = GetTickCount64();
        log::line("card031: script: phase 0 (re)starts on %s; menu path %ls", package.text, s.menu_path);
        return;
    }
    if (s.waiting_travel) {
        s.waiting_travel = false;
        ++s.phase;
        s.phase_t0 = GetTickCount64();
        log::line("card031: script: phase %u starts", s.phase);
    }
}

void c31_step(void* engine) {
    if (g_c31_mode == config::OverlayTest::Off) {
        c52_step(engine); // card 052: the lobby panel replaces the card 031 overlay test
        return;
    }
    if (!c31_resolve_user32()) {
        return;
    }
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    if (pc == nullptr) {
        return;
    }
    if (reinterpret_cast<uintptr_t>(world) != g_c31.world_id) {
        g_c31.world_id = reinterpret_cast<uintptr_t>(world);
        c31_new_world(world, pc);
        c31_script_world(world);
    }
    if (net_mode(pc) != kNetModeStandalone) {
        return; // single player only; c31_new_world logged the world
    }
    const uint8_t pressed = c31_poll_keys();
    if (g_c31.focus_pending) {
        c31_focus(pc);
    }
    c31_handle_keys(pc, pressed);
    if (g_c31_mode == config::OverlayTest::Script) {
        c31_script_step(engine, world, pc);
    }
}

StaticClassFn g_border_class = nullptr;
StaticClassFn g_vbox_class = nullptr;
StaticClassFn g_sizebox_class = nullptr;
C52WrapperFn g_border_set_brush_color = nullptr;
C52WrapperFn g_border_set_padding = nullptr;
C52WrapperFn g_sizebox_set_width = nullptr;
C52WrapperFn g_sizebox_set_height = nullptr;
C52WrapperFn g_box_set_text = nullptr;
FTextDtorFn g_ftext_dtor = nullptr;
TransitionToLevelFn g_transition_to_level = nullptr;
LoadLatestSaveFn g_load_latest_save = nullptr;
SaveQuicksaveFn g_save_quicksave = nullptr; // card 143
UpdateSignificanceFn g_update_significance = nullptr; // card 132
SignificanceGetFn g_significance_get = nullptr;        // card 132
AddSignificanceLocationFn g_add_significance_location = nullptr; // card 132
NetworkFailureFn g_orig_handle_network_failure = nullptr;
TravelFailureFn g_orig_handle_travel_failure = nullptr;
C290NotifyFn g_orig_c290_notify = nullptr; // card 290

constexpr ULONGLONG kC52BuildDelayMs = 1500; // after the menu world appears
constexpr size_t kC52TextChars = 512; // card 153: room for the internet, port and LAN lines
constexpr size_t kC153LineChars = 112;

constexpr float kC52BoxWidth = 300.0f;
constexpr float kC52BoxHeight = 40.0f;
constexpr char kC52MenuMap[] = "3D_Frontend";

constexpr const char* kC52NetworkFailures[] = {
    "NetDriverAlreadyExists", "NetDriverCreateFailure", "NetDriverListenFailure", "ConnectionLost",
    "ConnectionTimeout",      "FailureReceived",        "OutdatedClient",         "OutdatedServer",
    "PendingConnectionFailure", "NetGuidMismatch",      "NetChecksumMismatch"};
constexpr const char* kC52TravelFailures[] = {
    "NoLevel",      "LoadMapFailure", "InvalidURL",     "PackageMissing",           "PackageVersion",   "NoDownload",
    "TravelFailure", "CheatCommands", "PendingNetGameCreateFailure", "CloudSaveFailure", "ServerTravelFailure",
    "ClientTravelFailure"};

enum class C52Key : uint8_t { F8, Enter, H, J, I, Esc, Count };
constexpr int kC52Vk[] = {0x77, 0x0D, 0x48, 0x4A, 0x49, 0x1B}; // VK_F8, VK_RETURN, H, J, I (card 490), VK_ESCAPE
static_assert(sizeof(kC52Vk) / sizeof(kC52Vk[0]) == static_cast<size_t>(C52Key::Count), "one VK per C52Key");

C52State g_c52{};
config::C52Test g_c52_test = config::C52Test::Off; // written in ubw::init before any hook

bool c52_key_bit(uint8_t bits, C52Key k) {
    return (bits & (1u << static_cast<uint8_t>(k))) != 0;
}

// The three panel lines above the box.
// Card 153: no router lookup and no port mapping in test runs (UBWCOOP_TRAVEL, or the card 063 test runner).
bool c153_gated() {
    return g_c52.env_travel || g_c63_on;
}

// The internet line: the external address with the listen port, or why there is none.
void c153_internet_line(const upnp::Result& r, wchar_t (&out)[kC153LineChars]) {
    if (c153_gated()) {
        swprintf(out, kC153LineChars, L"Internet: not looked up in test runs");
    } else if (r.state == upnp::State::Looking) {
        swprintf(out, kC153LineChars, L"Internet: looking up...");
    } else if (r.external[0] == '\0') {
        swprintf(out, kC153LineChars, L"Internet: address not found");
    } else {
        const bool by_hand = r.external_from_web && r.state != upnp::State::Mapped;
        swprintf(out, kC153LineChars, L"Internet: %hs:%u%ls", r.external, g_c52.port,
                 by_hand ? L" (external, port not opened automatically)" : L"");
    }
}

// The port-mapping line; empty while looking up or in test runs.
void c153_port_line(const upnp::Result& r, wchar_t (&out)[kC153LineChars]) {
    out[0] = L'\0';
    if (c153_gated()) {
        return;
    }
    switch (r.state) {
    case upnp::State::Mapped:
        swprintf(out, kC153LineChars, L"\n  UDP port %u opened by UPnP", g_c52.port);
        break;
    case upnp::State::NoUpnp:
        swprintf(out, kC153LineChars, L"\n  UPnP not available: forward UDP port %u by hand", g_c52.port);
        break;
    case upnp::State::Failed:
        swprintf(out, kC153LineChars, L"\n  UPnP failed (code %d): forward UDP port %u by hand", r.code, g_c52.port);
        break;
    case upnp::State::Off:
    case upnp::State::Looking:
        break;
    }
}

void c153_host_lines(wchar_t (&out)[kC52TextChars]) {
    const upnp::Result r = upnp::poll();
    wchar_t net[kC153LineChars];
    wchar_t port[kC153LineChars];
    c153_internet_line(r, net);
    c153_port_line(r, port);
    const lobby::Ips& ips = g_c52.ips;
    swprintf(out, kC52TextChars, L"\nPlayers join at:\n  %ls%ls\n  LAN: %hs%hs%hs%hs%hs (port %u)\n"
             L"Now start a mission from the menu.", net, port, ips.count > 0 ? ips.ip[0] : "(no LAN address found)",
             ips.count > 1 ? ", " : "", ips.count > 1 ? ips.ip[1] : "", ips.count > 2 ? ", " : "",
             ips.count > 2 ? ips.ip[2] : "", g_c52.port);
}

// Hosting turned on (H, or saved on at start): look up the router and open the port; off: remove the mapping.
void c153_host_changed() {
    if (c153_gated()) {
        log::line("card153: UPnP off in test runs (UBWCOOP_TRAVEL or UBWCOOP_CMD): no router lookup");
        return;
    }
    if (!g_c52.settings.host) {
        upnp::stop();
        return;
    }
    log::line("card153: listen port %u (-port= on the command line, else %u): router lookup started", g_c52.port,
              lobby::kDefaultPort);
    upnp::start(g_c52.port);
}

void c52_info_text(wchar_t (&out)[kC52TextChars]) {
    if (g_c52.env_travel) {
        swprintf(out, kC52TextChars, L"UBW CO-OP\nUBWCOOP_TRAVEL is set: panel keys are off");
        return;
    }
    wchar_t hosting[kC52TextChars] = L"";
    if (g_c52.settings.host) {
        c153_host_lines(hosting);
    }
    swprintf(out, kC52TextChars, L"UBW CO-OP   (F8 hides)\n[H] Host co-op: %ls%ls\n[I] Infinite stamina when hosting: %ls"
             L"\n[J] Join:%ls", g_c52.settings.host ? L"ON" : L"OFF", hosting, g_c52.settings.stamina ? L"ON" : L"OFF",
             g_c52.typing ? L" type a.b.c.d or a.b.c.d:port, Enter joins, Esc cancels" : L"");
}

void c52_status_text(wchar_t (&out)[kC52TextChars]) {
    switch (g_c52.status) {
    case C52Status::Connecting:
        swprintf(out, kC52TextChars, L"Connecting to %ls... %us (Esc cancels)", g_travel.url, g_c52.shown_sec);
        break;
    case C52Status::Failed:
        swprintf(out, kC52TextChars, L"Could not connect: %hs", g_c52.reason);
        break;
    case C52Status::Lost:
        swprintf(out, kC52TextChars, L"Connection lost: %hs", g_c52.reason);
        break;
    case C52Status::Invalid:
        swprintf(out, kC52TextChars, L"Invalid address");
        break;
    case C52Status::Cancelled:
        swprintf(out, kC52TextChars, L"Join cancelled");
        break;
    case C52Status::NoConfirm: // card 344
        swprintf(out, kC52TextChars, L"The other player left or did not finish loading: mission not started");
        break;
    case C52Status::None:
        swprintf(out, kC52TextChars, L"%ls", g_c52.env_travel ? g_travel.url : L"");
        break;
    }
}

void c52_render() {
    g_c52.dirty = false;
    void* const widget = c31_weak_get(g_c52.widget);
    void* const info = c31_weak_get(g_c52.info);
    void* const status = c31_weak_get(g_c52.status_line);
    void* const box = c31_weak_get(g_c52.box);
    if (widget == nullptr || info == nullptr || status == nullptr || box == nullptr) {
        log::line("WARN card052: panel widgets gone before render (%p %p %p %p)", widget, info, status, box);
        return;
    }
    wchar_t text[kC52TextChars];
    c52_info_text(text);
    c31_set_text(info, text);
    c52_status_text(text);
    c31_set_text(status, text);
    g_widget_set_visibility(box, g_c52.env_travel ? kVisCollapsed : kVisSelfHitTestInvisible);
    g_widget_set_visibility(widget, g_c52.user_hidden ? kVisCollapsed : kVisSelfHitTestInvisible);
}

// A reflected native-bool property of `object` set to true (logged when missing or not a plain bool).
void c52_set_bool(void* object, const char* name) {
    const uint8_t* const prop = prop_find_prop(read_ptr(object, kObjectClass), name);
    if (prop == nullptr || !prop_kind_is(prop, "BoolProperty") || !prop_bool_ok(prop)) {
        log::line("WARN card052: %s.%s not a usable BoolProperty (%p): left as is", class_name(object).text, name,
                  static_cast<const void*>(prop));
        return;
    }
    auto* const byte = static_cast<uint8_t*>(object) + read_i32(prop, kPropertyOffset) + prop[kBoolByteOffset];
    *byte = static_cast<uint8_t>(*byte | prop[kBoolFieldMask]);
}

float c52_read_float(const void* object, const char* name) {
    const uint8_t* const prop = prop_find_prop(read_ptr(object, kObjectClass), name);
    float v = -1.0f;
    if (prop != nullptr && prop_kind_is(prop, "FloatProperty")) {
        memcpy(&v, static_cast<const uint8_t*>(object) + read_i32(prop, kPropertyOffset), sizeof(v));
    }
    return v;
}

// The box's text colour. The engine's default is light grey on the light-grey box (card052h shots: the address
// was hard to read). UEditableTextBox::SetForegroundColor has no native wrapper in this build, only its exec
// thunk, so it goes through ProcessEvent with its one FLinearColor parameter.
void c52_box_colour(void* box) {
    static uint64_t name = 0;
    void* const fn = g_find_function != nullptr && g_orig_process_event != nullptr &&
                             fname_cached(name, L"SetForegroundColor") != 0
                         ? g_find_function(box, name)
                         : nullptr;
    if (fn == nullptr) {
        log::line("WARN card052: UEditableTextBox SetForegroundColor not found (FindFunction %p): box text keeps "
                  "the default colour",
                  reinterpret_cast<void*>(g_find_function));
        return;
    }
    alignas(16) float parms[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // FLinearColor black
    g_orig_process_event(box, fn, parms);
    log::line("card052: box text colour set (SetForegroundColor, black)");
}

// Backing colour and padding, the box size (SizeBox overrides, read back for the log) and the prefill.
void c52_style(void* border, void* size, void* box) {
    const float backing[4] = {0.0f, 0.0f, 0.0f, 0.7f}; // FLinearColor
    const float padding[4] = {12.0f, 8.0f, 12.0f, 8.0f}; // FMargin
    g_border_set_brush_color(border, backing);
    g_border_set_padding(border, padding);
    g_sizebox_set_width(size, &kC52BoxWidth);
    g_sizebox_set_height(size, &kC52BoxHeight);
    log::line("card052: SizeBox WidthOverride %.0f, HeightOverride %.0f after the setters (asked %.0f x %.0f)",
              c52_read_float(size, "WidthOverride"), c52_read_float(size, "HeightOverride"), kC52BoxWidth,
              kC52BoxHeight);
    c52_set_bool(box, "SelectAllTextWhenFocused");
    c52_set_bool(box, "RevertTextOnEscape");
    c52_box_colour(box);
    if (g_c52.settings.address[0] != L'\0') {
        alignas(8) uint8_t ftext[kFTextBytes] = {};
        g_ftext_from_string(ftext, g_c52.settings.address);
        g_box_set_text(box, ftext); // copies: ours is destroyed below
        g_ftext_dtor(ftext);
    }
}

// Border -> VerticalBox -> {info, SizeBox -> box, status}; nullptr members mean a construct failed (logged).
struct C52Tree {
    void* border;
    void* vbox;
    void* info;
    void* size;
    void* box;
    void* status;
};

C52Tree c52_construct_tree(void* tree) {
    C52Tree t{};
    t.border = c31_construct(g_border_class(), tree);
    t.vbox = t.border != nullptr ? c31_construct(g_vbox_class(), tree) : nullptr;
    t.info = t.vbox != nullptr ? c31_construct(g_text_block_class(), tree) : nullptr;
    t.size = t.info != nullptr ? c31_construct(g_sizebox_class(), tree) : nullptr;
    t.box = t.size != nullptr ? c31_construct(g_editable_text_box_class(), tree) : nullptr;
    t.status = t.box != nullptr ? c31_construct(g_text_block_class(), tree) : nullptr;
    return t;
}

bool c52_link_tree(void* canvas, const C52Tree& t) {
    void* const slot = g_panel_add_child(canvas, t.border);
    const bool ok = slot != nullptr && g_panel_add_child(t.border, t.vbox) != nullptr &&
                    g_panel_add_child(t.vbox, t.info) != nullptr && g_panel_add_child(t.vbox, t.size) != nullptr &&
                    g_panel_add_child(t.size, t.box) != nullptr && g_panel_add_child(t.vbox, t.status) != nullptr;
    if (!ok) {
        log::line("ERROR card052: AddChild failed while linking the panel tree (canvas slot %p)", slot);
        return false;
    }
    c31_slot_layout(slot, 16.0, 16.0, 0.0f, 0.0f); // auto size: the border wraps its content
    return true;
}

bool c52_build(void* pc) {
    void* cls = c31_find_widget_class();
    void* const widget = cls != nullptr ? g_create_widget_instance(pc, &cls, 0) : nullptr;
    void* const tree = widget != nullptr ? c31_object_prop(widget, "WidgetTree") : nullptr;
    void* const canvas = tree != nullptr ? c31_root_canvas(tree) : nullptr;
    if (canvas == nullptr) {
        log::line("ERROR card052: panel not built: widget %p, WidgetTree %p, canvas %p", widget, tree, canvas);
        return false;
    }
    const C52Tree t = c52_construct_tree(tree);
    if (t.status == nullptr || !c52_link_tree(canvas, t)) {
        return false;
    }
    c52_style(t.border, t.size, t.box);
    g_add_to_viewport(widget, kC31ZOrder);
    c31_weak_set(g_c52.widget, widget);
    c31_weak_set(g_c52.info, t.info);
    c31_weak_set(g_c52.box, t.box);
    c31_weak_set(g_c52.status_line, t.status);
    g_c52.dirty = true;
    c31_log_tree("card052 panel", widget);
    log::line("card052: panel built (%s), %s", object_name(widget).text, g_c52.user_hidden ? "hidden (F8)" : "shown");
    return true;
}

void c52_drop_panel() {
    void* const widget = c31_weak_get(g_c52.widget);
    if (widget != nullptr) {
        g_widget_remove_from_parent(widget);
    }
    g_c52.widget = C31Weak{};
    g_c52.info = C31Weak{};
    g_c52.box = C31Weak{};
    g_c52.status_line = C31Weak{};
}

void c52_new_world(void* world, const void* pc, ULONGLONG now) {
    const ObjectName package = object_name(read_ptr(world, kObjectOuter));
    const unsigned mode = net_mode(pc);
    c52_drop_panel();
    g_c52.world_id = reinterpret_cast<uintptr_t>(world);
    g_c52.world_ms = now;
    // Card 154: the host's hangar is a listen server once a session went through it; the panel stays on there.
    g_c52.menu_map = strstr(package.text, kC52MenuMap) != nullptr;
    g_c52.at_frontend = g_c52.menu_map && mode != kNetModeClient;
    g_c52.build_failed = false;
    g_c52.typing = false; // the game sets its own input mode on the new map
    g_c52.focus_pending = false;
    if (g_c52.status == C52Status::NoConfirm && !g_c52.at_frontend) {
        g_c52.status = C52Status::None; // card 344: the reason is for the menu the timeout led to, not a later one
    }
    if (g_c52.status == C52Status::Failed || g_c52.status == C52Status::Lost || g_c52.status == C52Status::NoConfirm) {
        g_c52.user_hidden = false; // the reason must be seen
    }
    log::line("card052: t=%llu world %s (package %s, net mode %u): panel %s, status %u%s",
              static_cast<unsigned long long>(now), object_name(world).text, package.text, mode,
              g_c52.at_frontend ? "on" : "off", static_cast<unsigned>(g_c52.status),
              g_c52.join_pending ? ", join pending" : "");
}

// --- Card 052 test driver (UBWCOOP_C52_TEST): drives the panel with real keys, like a player. ---
// Phases: 0 = the first menu world, 1 and 2 = the first and second non-menu worlds, 10 = a later menu world
// (back from a failed join). Row times are ms after the phase's world appeared.

enum class C52Act : uint8_t { Shot, Down, Up, Type, HostOn, HostOff, Transition, Next, Level, Done, Fail };
struct C52Row {
    uint8_t phase;
    uint32_t at_ms;
    C52Act act;
    uint16_t vk;
};
constexpr uint8_t kC52PhaseMenuAgain = 10;
constexpr uint16_t kC52VkCtrl = 0x11;
constexpr uint16_t kC52VkA = 0x41;
constexpr uint16_t kC52VkH = 0x48;
constexpr uint16_t kC52VkJ = 0x4A;
constexpr uint16_t kC52VkEnter = 0x0D;
constexpr uint16_t kC52VkF8 = 0x77;
constexpr wchar_t kC52Typed[] = L"127.0.0.1";
constexpr const char* kC52ActText[] = {"shot", "key down", "key up", "type", "host on", "host off", "TransitionToLevel",
                                       "TransitionToLevel next", "log level", "done", "timeout"};

constexpr C52Row kC52HostRows[] = {
    {0, 3000, C52Act::Shot, 0},        {0, 4000, C52Act::HostOn, 0},      {0, 4150, C52Act::Up, kC52VkH},
    {0, 6000, C52Act::Shot, 0},        {0, 8000, C52Act::Transition, 0},  {1, 2000, C52Act::Level, 0},
    {1, 70000, C52Act::Shot, 0},       {1, 90000, C52Act::Shot, 0},       {1, 100000, C52Act::Next, 0},
    {2, 2000, C52Act::Level, 0},       {2, 20000, C52Act::Shot, 0},       {2, 21000, C52Act::Done, 0},
};
constexpr C52Row kC52SoloRows[] = {
    {0, 3000, C52Act::Shot, 0},        {0, 4000, C52Act::HostOff, 0},     {0, 4150, C52Act::Up, kC52VkH},
    {0, 6000, C52Act::Shot, 0},        {0, 8000, C52Act::Transition, 0},  {1, 2000, C52Act::Level, 0},
    {1, 15000, C52Act::Shot, 0},       {1, 20000, C52Act::Done, 0},
};
// Played-path scenarios (card 319, UBWCOOP_C52_TEST=play / join): the same host start and client join, then the panel
// driver stops in the first mission and the scenario drives play. No Next row (the host stays on its first mission)
// and no Fail row (a scenario's own waits time the join out).
constexpr C52Row kC52PlayRows[] = {
    {0, 3000, C52Act::Shot, 0},        {0, 4000, C52Act::HostOn, 0},      {0, 4150, C52Act::Up, kC52VkH},
    {0, 6000, C52Act::Shot, 0},        {0, 8000, C52Act::Transition, 0},  {1, 2000, C52Act::Level, 0},
    {1, 2100, C52Act::Done, 0},
};
constexpr C52Row kC52JoinRows[] = {
    {0, 3000, C52Act::Shot, 0},          {0, 4000, C52Act::HostOff, 0},          {0, 4150, C52Act::Up, kC52VkH},
    {0, 5000, C52Act::Down, kC52VkJ},    {0, 5150, C52Act::Up, kC52VkJ},         {0, 6000, C52Act::Down, kC52VkCtrl},
    {0, 6050, C52Act::Down, kC52VkA},    {0, 6100, C52Act::Up, kC52VkA},         {0, 6150, C52Act::Up, kC52VkCtrl},
    {0, 6500, C52Act::Type, 0},          {0, 7500, C52Act::Shot, 0},             {0, 8500, C52Act::Down, kC52VkEnter},
    {0, 8650, C52Act::Up, kC52VkEnter},  {0, 10000, C52Act::Shot, 0},            {1, 2000, C52Act::Level, 0},
    {1, 2100, C52Act::Done, 0},
};
// The client and fail runs type the same join; the fail run has no host and comes back to the menu. It ends with
// host mode ON, so the next run's "lobby settings read" line shows the host flag surviving a restart.
constexpr C52Row kC52ClientRows[] = {
    {0, 3000, C52Act::Shot, 0},          {0, 4000, C52Act::HostOff, 0},          {0, 4150, C52Act::Up, kC52VkH},
    {0, 5000, C52Act::Down, kC52VkJ},    {0, 5150, C52Act::Up, kC52VkJ},         {0, 6000, C52Act::Down, kC52VkCtrl},
    {0, 6050, C52Act::Down, kC52VkA},    {0, 6100, C52Act::Up, kC52VkA},         {0, 6150, C52Act::Up, kC52VkCtrl},
    {0, 6500, C52Act::Type, 0},          {0, 7500, C52Act::Shot, 0},             {0, 8500, C52Act::Down, kC52VkEnter},
    {0, 8650, C52Act::Up, kC52VkEnter},  {0, 10000, C52Act::Shot, 0},            {1, 15000, C52Act::Shot, 0},
    {1, 30000, C52Act::Shot, 0},         {1, 150000, C52Act::Fail, 0},           {2, 20000, C52Act::Shot, 0},
    {2, 21000, C52Act::Done, 0},
};
constexpr C52Row kC52FailRows[] = {
    {0, 3000, C52Act::Shot, 0},          {0, 4000, C52Act::HostOff, 0},          {0, 4150, C52Act::Up, kC52VkH},
    {0, 5000, C52Act::Down, kC52VkJ},    {0, 5150, C52Act::Up, kC52VkJ},         {0, 6000, C52Act::Down, kC52VkCtrl},
    {0, 6050, C52Act::Down, kC52VkA},    {0, 6100, C52Act::Up, kC52VkA},         {0, 6150, C52Act::Up, kC52VkCtrl},
    {0, 6500, C52Act::Type, 0},          {0, 7500, C52Act::Shot, 0},             {0, 8500, C52Act::Down, kC52VkEnter},
    {0, 8650, C52Act::Up, kC52VkEnter},  {0, 10000, C52Act::Shot, 0},            {0, 90000, C52Act::Shot, 0},
    {0, 150000, C52Act::Fail, 0},        {10, 4000, C52Act::Shot, 0},            {10, 5000, C52Act::HostOn, 0},
    {10, 5150, C52Act::Up, kC52VkH},     {10, 6500, C52Act::Shot, 0},            {10, 9000, C52Act::Down, kC52VkF8},
    {10, 9150, C52Act::Up, kC52VkF8},    {10, 10000, C52Act::Shot, 0},           {10, 10500, C52Act::Down, kC52VkF8},
    {10, 10650, C52Act::Up, kC52VkF8},   {10, 11500, C52Act::Shot, 0},           {10, 12000, C52Act::Done, 0},
};

struct C52Script {
    const C52Row* rows;
    uint32_t count;
    uint32_t step;
    uint8_t phase;
    uint32_t menus; // menu worlds seen
    uint32_t others; // non-menu worlds seen
    ULONGLONG t0;
    bool foreground_logged;
};
C52Script g_c52_script{};

const char* c52_test_name() {
    constexpr const char* kNames[] = {"off", "host", "client", "fail", "solo", "play", "join"};
    return kNames[static_cast<uint8_t>(g_c52_test)];
}

void c52_test_rows() {
    C52Script& s = g_c52_script;
    switch (g_c52_test) {
    case config::C52Test::Host:
        s.rows = kC52HostRows;
        s.count = sizeof(kC52HostRows) / sizeof(kC52HostRows[0]);
        break;
    case config::C52Test::Client:
        s.rows = kC52ClientRows;
        s.count = sizeof(kC52ClientRows) / sizeof(kC52ClientRows[0]);
        break;
    case config::C52Test::Fail:
        s.rows = kC52FailRows;
        s.count = sizeof(kC52FailRows) / sizeof(kC52FailRows[0]);
        break;
    case config::C52Test::Solo:
        s.rows = kC52SoloRows;
        s.count = sizeof(kC52SoloRows) / sizeof(kC52SoloRows[0]);
        break;
    case config::C52Test::Play:
        s.rows = kC52PlayRows;
        s.count = sizeof(kC52PlayRows) / sizeof(kC52PlayRows[0]);
        break;
    case config::C52Test::Join:
        s.rows = kC52JoinRows;
        s.count = sizeof(kC52JoinRows) / sizeof(kC52JoinRows[0]);
        break;
    case config::C52Test::Off:
        s.count = 0;
        break;
    }
}

// Called from c52_step's new-world branch (after c52_new_world set at_frontend).
void c52_test_world(ULONGLONG now) {
    C52Script& s = g_c52_script;
    if (s.rows == nullptr) {
        c52_test_rows();
    }
    if (g_c52.at_frontend) {
        // After a mission the menu is the card 045 reconnect's stop on the way to the host's next map: the
        // phase stays, so the next mission's rows still run. Only a menu before any mission is phase 10.
        s.phase = s.menus == 0 ? 0 : (s.others == 0 ? kC52PhaseMenuAgain : s.phase);
        ++s.menus;
    } else {
        ++s.others;
        s.phase = static_cast<uint8_t>(s.others < 2 ? s.others : 2);
    }
    s.t0 = now;
    log::line("card052: test %s: phase %u starts (menu worlds %u, other worlds %u)", c52_test_name(), s.phase, s.menus,
              s.others);
}

void c52_test_transition(void* world, bool next) {
    void* const gi = g_get_st_game_instance(world);
    if (gi == nullptr) {
        log::line("ERROR card052: test: no ST game instance for TransitionToLevel");
        return;
    }
    const auto* const g = static_cast<const uint8_t*>(gi);
    const int32_t chapter = next ? read_i32(g, kGameInstanceChapter) : 0;
    const int32_t level = next ? read_i32(g, kGameInstanceLevel) + 1 : 0;
    FStringRaw empty{}; // passed by value: the callee destroys it (empty, nothing to free)
    log::line("card052: test: TransitionToLevel(chapter %d, level %d), as the menu or mission end would", chapter, level);
    g_transition_to_level(gi, chapter, level, &empty);
}

void c52_test_level(void* world) {
    const auto* const gi = static_cast<const uint8_t*>(g_get_st_game_instance(world));
    const ObjectName package = object_name(read_ptr(world, kObjectOuter));
    log::line("card052: test: %s world %s: game instance chapter %d, level %d, net mode %u", side(), package.text,
              gi != nullptr ? read_i32(gi, kGameInstanceChapter) : -1,
              // Card 349: any PC in the world will do (net mode only); not the local PC.
              gi != nullptr ? read_i32(gi, kGameInstanceLevel) : -1, net_mode(g_world_first_pc(world)));
}

// Input rows wait for the foreground; HostOn/HostOff press H only when the saved flag differs.
// Every hold logs one WARN row and its end one "foreground back" row, so a harness that activates the window from
// outside (SetForegroundWindow fails under Wine on Wayland: run_pair.sh MENU=1) sees each hold.
bool c52_test_input(const C52Row& row) {
    if (!c31_own_foreground()) {
        c31_take_foreground("card052", g_c52_script.foreground_logged);
        return false;
    }
    if (g_c52_script.foreground_logged) {
        g_c52_script.foreground_logged = false;
        log::line("card052: test: foreground back, input resumes");
    }
    const bool want_host = row.act == C52Act::HostOn;
    INPUT key = c31_key_input(row.act == C52Act::HostOn || row.act == C52Act::HostOff ? kC52VkH : row.vk, 0,
                              row.act == C52Act::Up);
    if (row.act == C52Act::Type) {
        c31_type(kC52Typed);
    } else if ((row.act != C52Act::HostOn && row.act != C52Act::HostOff) || g_c52.settings.host != want_host) {
        c31_send(&key, 1);
    } else {
        log::line("card052: test: host mode already %s, H not pressed", want_host ? "ON" : "OFF");
    }
    return true;
}

// Runs one row; false when it must be retried on a later tick.
bool c52_test_run(const C52Row& row, void* world, void* pc) {
    switch (row.act) {
    case C52Act::Shot:
        request_shot(pc, true);
        return true;
    case C52Act::Transition:
    case C52Act::Next:
        c52_test_transition(world, row.act == C52Act::Next);
        return true;
    case C52Act::Level:
        c52_test_level(world);
        return true;
    case C52Act::Done:
        log::line("card052: %s test done", c52_test_name());
        return true;
    case C52Act::Fail:
        log::line("ERROR card052: %s test FAILED: phase %u still waiting after %u ms", c52_test_name(), row.phase,
                  row.at_ms);
        return true;
    default:
        return c52_test_input(row);
    }
}

// Rows of an earlier phase than the current one are skipped; rows of a later phase wait.
void c52_test_step(void* engine, void* world, void* pc, ULONGLONG now) {
    (void)engine;
    C52Script& s = g_c52_script;
    if (s.rows == nullptr || s.step >= s.count) {
        return;
    }
    const C52Row& row = s.rows[s.step];
    if (row.phase < s.phase) {
        log::line("card052: test: row %u (phase %u, %s) skipped: phase %u reached", s.step, row.phase,
                  kC52ActText[static_cast<uint8_t>(row.act)], s.phase);
        ++s.step;
        return;
    }
    if (row.phase > s.phase || now - s.t0 < row.at_ms || !c52_test_run(row, world, pc)) {
        return;
    }
    log::line("card052: test row %u (phase %u, %u ms): %s vk 0x%02X", s.step, row.phase, row.at_ms,
              kC52ActText[static_cast<uint8_t>(row.act)], row.vk);
    ++s.step;
}

// Edge-detects the panel keys while this process owns the foreground window.
uint8_t c52_poll_keys() {
    uint8_t now = 0;
    if (c31_own_foreground()) {
        for (uint32_t i = 0; i < static_cast<uint32_t>(C52Key::Count); ++i) {
            now |= (g_c31_user32.key_state(kC52Vk[i]) & 0x8000) != 0 ? static_cast<uint8_t>(1u << i) : 0u;
        }
    }
    const uint8_t pressed = static_cast<uint8_t>(now & ~g_c52.keys);
    g_c52.keys = now;
    return pressed;
}

void c52_save() {
    lobby::save(g_c52.settings);
}

// Card 154: H in the standalone hangar makes the hangar itself listen (UWorld::Listen on the current world with a
// `listen` URL of the menu map), so a client can join there and follow the host into the mission. Its own
// mission Browse then keeps `listen` through c45_host_browse (net mode 2). A failed Listen leaves the old path:
// the mission Browse gets `listen` from c52_host_browse.
constexpr size_t kC154FurlBytes = 0x100; // FURL is 0x68 (card 045 layout check); room to spare

void c154_listen_here(void* pc) {
    void* const level = read_ptr(pc, kObjectOuter);
    void* const world = level != nullptr ? read_ptr(level, kObjectOuter) : nullptr;
    if (world == nullptr || strcmp(class_name(world).text, "World") != 0 || g_c45.layout != 1) {
        log::line("WARN card154: H: the hangar does not listen: world %s, FURL layout %d",
                  world != nullptr ? class_name(world).text : "null", g_c45.layout);
        return;
    }
    alignas(16) uint8_t u[kC154FurlBytes] = {};
    g_furl_ctor(u, nullptr, L"/Game/Maps/3D_Frontend/3D_Frontend?listen", kTravelAbsolute);
    const bool ok = ue::net::g_listen(world, u);
    log::line("%scard154: H: hangar UWorld::Listen on %s, port %d: %s", ok ? "" : "WARN ", object_name(world).text,
              read_i32(u, kFurlPort), ok ? "listening" : "FAILED (the mission Browse still gets listen)");
    g_furl_dtor(u);
}

// Card 344: host, from the card 320 hold's timeout, before its travel to the menu.
void c52_note_no_confirm() {
    g_c52.status = C52Status::NoConfirm;
    g_c52.dirty = true;
}

void c52_toggle_host() {
    g_c52.settings.host = !g_c52.settings.host;
    if (g_c52.settings.host && !g_c52.ips_read) {
        g_c52.ips = lobby::local_ipv4();
        g_c52.ips_read = true;
    }
    g_c52.status = g_c52.join_pending ? g_c52.status : C52Status::None;
    g_c52.dirty = true;
    log::line("card052: H: host mode %s", g_c52.settings.host ? "ON" : "OFF");
    c52_save();
    c153_host_changed();
}

// Card 490: I flips the host's infinite stamina option, saved; a session this process hosts uses it at once.
void c52_toggle_stamina(const void* pc) {
    g_c52.settings.stamina = !g_c52.settings.stamina;
    g_c52.dirty = true;
    log::line("card052: I: infinite stamina %s", g_c52.settings.stamina ? "ON" : "OFF");
    c52_save();
    c490_set_host(pc, g_c52.settings.stamina, "panel");
}

// One tick after J: UIOnly with the box as focus (the mod's own call: not recorded as the game's mode).
void c52_focus(void* pc) {
    g_c52.focus_pending = false;
    void* const box = c31_weak_get(g_c52.box);
    if (box == nullptr) {
        log::line("ERROR card052: text box gone before focus");
        g_c52.typing = false;
        return;
    }
    g_c31.own_mode_call = true;
    g_ui_only_ex(pc, box, 0, false); // EMouseLockMode::DoNotLock, no input flush
    g_c31.own_mode_call = false;
    g_widget_set_keyboard_focus(box);
    log::line("card052: J: box focused (UIOnlyEx + SetKeyboardFocus); game mode on record %s",
              kC31KindText[static_cast<uint8_t>(g_c31.game_kind)]);
}

void c52_end_typing(void* pc, const char* why) {
    g_c52.typing = false;
    g_c52.focus_pending = false;
    g_c52.dirty = true;
    log::line("card052: %s: box released", why);
    c31_restore_mode(pc);
}

// The box's Text as typed (empty when unreadable; logged).
void c52_box_text(wchar_t (&out)[kC52TextChars]) {
    out[0] = L'\0';
    const void* const box = c31_weak_get(g_c52.box);
    const uint8_t* const prop = box != nullptr ? prop_find_prop(read_ptr(box, kObjectClass), "Text") : nullptr;
    const FStringRaw* const s =
        prop != nullptr ? g_ftext_to_string(static_cast<const uint8_t*>(box) + read_i32(prop, kPropertyOffset))
                        : nullptr;
    if (s == nullptr || s->data == nullptr || s->num <= 0 || static_cast<size_t>(s->num) > kC52TextChars) {
        log::line("WARN card052: box text unreadable (box %p, Text property %p, string %p)", box,
                  static_cast<const void*>(prop), static_cast<const void*>(s));
        return;
    }
    swprintf(out, kC52TextChars, L"%.*ls", s->num - 1, s->data);
}

// The env-var client path (ubw::init order), on the game thread: travel_step fires on the next tick.
void c52_join(const wchar_t (&address)[lobby::kAddrChars]) {
    swprintf(g_travel.url, ue::config::kMaxUrlLen + 1, L"%ls", address);
    g_travel.enabled = true;
    g_travel.delay_ms = 0;
    g_client_game_only = true;
    g_travel_armed = true;
    g_c52.arm_ms = GetTickCount64();
    g_c52.join_pending = true;
    g_c52.status = C52Status::Connecting;
    g_c52.shown_sec = 0;
    g_c52.dirty = true;
    log::line("card052: t=%llu join armed: g_travel.url \"%ls\", enabled 1, delay 0 ms, client game-only 1, armed 1",
              static_cast<unsigned long long>(g_c52.arm_ms), g_travel.url);
    wcscpy_s(g_c52.settings.address, lobby::kAddrChars, address);
    const bool was_host = g_c52.settings.host;
    g_c52.settings.host = false; // a joining player is not the host
    c52_save();
    if (was_host) {
        c153_host_changed(); // card 153: remove the port mapping
    }
}

void c52_submit(void* pc) {
    wchar_t typed[kC52TextChars];
    c52_box_text(typed);
    wchar_t address[lobby::kAddrChars];
    const bool ok = lobby::parse_address(typed, address);
    log::line("card052: Enter: box \"%ls\" -> %ls", typed, ok ? address : L"invalid address");
    c52_end_typing(pc, "Enter");
    if (!ok) {
        g_c52.status = C52Status::Invalid;
        return;
    }
    c52_join(address);
}

void c52_console(void* pc, const wchar_t* text) {
    const int32_t chars = static_cast<int32_t>(wcslen(text)) + 1;
    const FStringRaw cmd{text, chars, chars};
    FStringRaw out{};
    g_pc_console_command(pc, &out, &cmd, false);
    if (out.data != nullptr) {
        g_memory_free(const_cast<wchar_t*>(out.data));
    }
}

void c52_cancel(void* pc) {
    log::line("card052: Esc: join to %ls cancelled after %llu ms: console \"disconnect\"", g_travel.url,
              static_cast<unsigned long long>(GetTickCount64() - g_c52.arm_ms));
    g_c52.join_pending = false;
    g_travel_armed = false; // when Esc came before travel_step fired
    g_c52.status = C52Status::Cancelled;
    g_c52.dirty = true;
    c52_console(pc, L"disconnect");
}

void c52_handle_keys(void* pc, uint8_t pressed) {
    if (c52_key_bit(pressed, C52Key::F8)) {
        g_c52.user_hidden = !g_c52.user_hidden;
        g_c52.dirty = true;
        log::line("card052: F8: panel %s", g_c52.user_hidden ? "hidden" : "shown");
        if (g_c52.user_hidden && g_c52.typing) {
            c52_end_typing(pc, "F8");
        }
        return;
    }
    if (g_c52.user_hidden || g_c52.env_travel) {
        return;
    }
    if (g_c52.typing) { // H and J are text while the box has focus
        if (c52_key_bit(pressed, C52Key::Enter)) {
            c52_submit(pc);
        } else if (c52_key_bit(pressed, C52Key::Esc)) {
            c52_end_typing(pc, "Esc");
        }
        return;
    }
    if (c52_key_bit(pressed, C52Key::H)) {
        c52_toggle_host();
        if (g_c52.settings.host && net_mode(pc) == kNetModeStandalone) {
            c154_listen_here(pc);
        }
    } else if (c52_key_bit(pressed, C52Key::I)) {
        c52_toggle_stamina(pc);
    } else if (c52_key_bit(pressed, C52Key::J) && !g_c52.join_pending) {
        g_c52.typing = true;
        g_c52.focus_pending = true;
        g_c52.dirty = true;
    } else if (c52_key_bit(pressed, C52Key::Esc) && g_c52.join_pending) {
        c52_cancel(pc);
    }
}

void c52_failure(const char* kind, int32_t type, const char* name, const void* message) {
    c45_pending_failed(kind, name); // card 130 item 15c: the client follow's pending try ended
    if (!g_c52.join_pending && !g_client_session) {
        return; // not a client join: the host's own net errors are not the panel's
    }
    const bool pending = g_c52.join_pending;
    log::line("card052: t=%llu %s failure %d (%s) %s, %llu ms after the join arm",
              static_cast<unsigned long long>(GetTickCount64()), kind, type, name,
              pending ? "while the join was pending" : "in session",
              static_cast<unsigned long long>(GetTickCount64() - g_c52.arm_ms));
    log_fstring("card052: failure message", message);
    snprintf(g_c52.reason, kC52ReasonChars, "%s", name);
    // Card 290: a failure the host's travel row explains is no lost connection on the panel either.
    const bool travel = !pending && strcmp(kind, "network") == 0 && c290_travel_failure(type);
    g_c52.status = pending ? C52Status::Failed : (travel ? C52Status::None : C52Status::Lost);
    g_c52.join_pending = false;
    g_c52.user_hidden = false;
    g_c52.dirty = true;
}

void detour_handle_network_failure(void* engine, void* world, void* driver, int32_t type, const void* message) {
    g_orig_handle_network_failure(engine, world, driver, type, message);
    c337_uncover("network failure"); // card 337
    const int32_t n = static_cast<int32_t>(sizeof(kC52NetworkFailures) / sizeof(kC52NetworkFailures[0]));
    c52_failure("network", type, type >= 0 && type < n ? kC52NetworkFailures[type] : "unknown", message);
}

// Card 290: the game's failure dialog. Skipped for a failure the host's travel row explains (the row is logged by
// c290_travel_failure); the engine's HandleDisconnect after it still runs, so the client leaves the dead map.
void detour_c290_notify(void* engine, void* world, void* driver, int32_t type) {
    if (g_client_session && c290_travel_failure(type)) {
        log::line("card290: client t=%llu failure %d within %llu ms of the host travel row: dialog skipped, follow runs",
                  static_cast<unsigned long long>(GetTickCount64()), type,
                  static_cast<unsigned long long>(kC290TravelMs));
        return;
    }
    g_orig_c290_notify(engine, world, driver, type);
}

void detour_handle_travel_failure(void* engine, void* world, int32_t type, const void* message) {
    g_orig_handle_travel_failure(engine, world, type, message);
    const int32_t n = static_cast<int32_t>(sizeof(kC52TravelFailures) / sizeof(kC52TravelFailures[0]));
    c52_failure("travel", type, type >= 0 && type < n ? kC52TravelFailures[type] : "unknown", message);
}

// A join's own LoadMap (pending net game) ends the wait; a failed one is reported by the failure hooks.
void c52_load_map_return(bool ok, const void* pending) {
    if (pending == nullptr || !g_c52.join_pending || !ok) {
        return;
    }
    g_c52.join_pending = false;
    g_c52.status = C52Status::None;
    log::line("card052: joined %ls: LoadMap of the host's map succeeded %llu ms after the join arm", g_travel.url,
              static_cast<unsigned long long>(GetTickCount64() - g_c52.arm_ms));
}

void c52_step(void* engine) {
    if (!c31_resolve_user32()) {
        return;
    }
    void* const world = g_get_game_world(engine);
    void* const pc = world != nullptr ? own_pc(world) : nullptr;
    if (pc == nullptr) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (reinterpret_cast<uintptr_t>(world) != g_c52.world_id) {
        c52_new_world(world, pc, now);
        if (g_c52_test != config::C52Test::Off) {
            c52_test_world(now);
        }
    }
    if (g_c52_test != config::C52Test::Off) {
        c52_test_step(engine, world, pc, now);
    }
    if (!g_c52.at_frontend) {
        return;
    }
    if (c31_weak_get(g_c52.widget) == nullptr) {
        if (g_c52.build_failed || now - g_c52.world_ms < kC52BuildDelayMs) {
            return;
        }
        g_c52.build_failed = !c52_build(pc);
        return;
    }
    if (g_c52.focus_pending) {
        c52_focus(pc);
    }
    c52_handle_keys(pc, c52_poll_keys());
    const uint32_t sec = static_cast<uint32_t>((now - g_c52.arm_ms) / 1000);
    if (g_c52.status == C52Status::Connecting && sec != g_c52.shown_sec) {
        g_c52.shown_sec = sec;
        g_c52.dirty = true;
    }
    const upnp::State upnp_now = upnp::poll().state; // card 153: redraw when the worker answers
    if (upnp_now != g_c52.upnp_shown) {
        g_c52.upnp_shown = upnp_now;
        g_c52.dirty = true;
    }
    if (g_c52.dirty) {
        c52_render();
    }
}

// ubw::init: the saved settings, and whether UBWCOOP_TRAVEL owns the session.
void c52_init(bool env_travel) {
    g_c52.settings = lobby::load();
    g_c52.env_travel = env_travel;
    c490_set_host(nullptr, g_c52.settings.stamina, "ini"); // card 490: no client yet at load
    g_c52.port = lobby::listen_port();
    if (g_c52.settings.host) {
        g_c52.ips = lobby::local_ipv4();
        g_c52.ips_read = true;
        c153_host_changed();
    }
    log::line("card052: lobby panel on at the main menu (F8 toggles); UBWCOOP_TRAVEL %s", env_travel ? "set: status only" : "not set");
}

// Host mode: the menu's mission Browse (net mode 0, local map other than the menu) gets `listen`.
void c52_host_browse(void* engine, uint8_t* url) {
    if (!g_c52.settings.host || g_c52.env_travel || g_client_session || fs_len(url + kFurlHost) != 0) {
        return;
    }
    void* const world = g_get_game_world(engine);
    // Card 349: any PC in the world will do (net mode only); not the local PC.
    void* const pc = world != nullptr ? g_world_first_pc(world) : nullptr;
    const unsigned mode = pc != nullptr ? net_mode(pc) : 0xFFu;
    const wchar_t* const map = fs_text(url + kFurlMap);
    if (mode != kNetModeStandalone || fs_len(url + kFurlMap) == 0 || wcsstr(map, kC52MenuMapW) != nullptr ||
        c45_has_option(url, L"listen")) {
        log::line("card052: host mode: Browse to \"%ls\" left alone (net mode %u, has listen %d)", map, mode,
                  c45_has_option(url, L"listen") ? 1 : 0);
        return;
    }
    const auto* const op = reinterpret_cast<const FArrayRaw*>(url + kFurlOp);
    log::line("card052: host mode: Browse URL before: map \"%ls\", %d options", map, op->num);
    const bool added = c45_add_option(url, L"listen");
    log::line("card052: host mode: Browse URL after: map \"%ls\", %d options, listen %s", map, op->num,
              added ? "added" : "NOT ADDED");
    c45_log_url("card052 host-mode Browse URL", url);
    g_c52.host_browse_ms = added ? GetTickCount64() : 0;
}

void c52_listen(bool ok) {
    if (g_c52.host_browse_ms == 0) {
        return;
    }
    log::line("card052: UWorld::Listen returned %s %llu ms after the host-mode Browse", ok ? "true" : "false",
              static_cast<unsigned long long>(GetTickCount64() - g_c52.host_browse_ms));
    g_c52.host_browse_ms = 0;
}

} // namespace ubw::hooks
