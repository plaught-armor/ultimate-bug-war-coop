#pragma once

// UE UMG widgets and FText: the engine function pointers and their typedefs (split S4b). The pointers are
// resolved by the kCallsUe table in ue/hooktable.cpp.

#include "ue/reflect.h"

#include <stdint.h>

namespace ue::widget {

// --- Engine function types and pointers (defined as nullptr in widget.cpp, resolved by kCallsUe) -----------

using FTextFromStringFn = void* (*)(void* out_text, const wchar_t* text);
using FTextToStringFn = const ue::reflect::FStringRaw* (*)(const void* text);
using WidgetVisibilityFn = void (*)(void* widget, uint8_t visibility);
using WidgetFn = void (*)(void* widget);
using CreateWidgetInstanceFn = void* (*)(void* pc, void* const* cls, uint64_t name);
using AddToViewportFn = void (*)(void* widget, int32_t z_order);
using PanelAddChildFn = void* (*)(void* panel, void* child);
using UiOnlyExFn = void (*)(void* pc, void* widget, uint8_t lock_mode, bool flush_input);

extern FTextFromStringFn g_ftext_from_string;
extern FTextToStringFn g_ftext_to_string;
extern ue::reflect::StaticClassFn g_text_block_class;        // UTextBlock
extern ue::reflect::StaticClassFn g_editable_text_box_class; // UEditableTextBox
extern WidgetVisibilityFn g_widget_set_visibility;
extern WidgetFn g_widget_set_keyboard_focus;
extern WidgetFn g_widget_remove_from_parent;
extern CreateWidgetInstanceFn g_create_widget_instance;
extern AddToViewportFn g_add_to_viewport;
extern PanelAddChildFn g_panel_add_child;
extern UiOnlyExFn g_ui_only_ex;

} // namespace ue::widget
