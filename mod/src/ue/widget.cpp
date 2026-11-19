#include "ue/widget.h"

namespace ue::widget {

using ue::reflect::StaticClassFn;

FTextFromStringFn g_ftext_from_string = nullptr;
FTextToStringFn g_ftext_to_string = nullptr;
StaticClassFn g_text_block_class = nullptr;
StaticClassFn g_editable_text_box_class = nullptr;
WidgetVisibilityFn g_widget_set_visibility = nullptr;
WidgetFn g_widget_set_keyboard_focus = nullptr;
WidgetFn g_widget_remove_from_parent = nullptr;
CreateWidgetInstanceFn g_create_widget_instance = nullptr;
AddToViewportFn g_add_to_viewport = nullptr;
PanelAddChildFn g_panel_add_child = nullptr;
UiOnlyExFn g_ui_only_ex = nullptr;

} // namespace ue::widget
