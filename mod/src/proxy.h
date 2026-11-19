#pragma once

#include <windows.h>

// dwmapi.dll proxy: every export of the real (system-dir) dwmapi.dll is re-exported with the
// same ordinal/name by a stub that tail-jumps through a table filled at attach time.
// Export list: proxy_exports.generated.inc / dwmapi.def (tools/gen_proxy_exports.py).

namespace ubw::proxy {

// Loads <system dir>\dwmapi.dll and resolves every export into the jump table.
// Called from DllMain (loader lock held). Returns false only if the real DLL cannot be
// loaded; individual missing exports are logged and routed to an E_NOTIMPL stub.
bool load_real_dll(HMODULE self);

} // namespace ubw::proxy
