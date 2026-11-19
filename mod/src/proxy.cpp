#include "proxy.h"

#include "ue/log.h"

#include <stdint.h>

namespace {

struct ExportRow {
    uint16_t ordinal;
    const char* name; // nullptr: ordinal-only export
};

constexpr ExportRow kExports[] = {
#define UBW_EXPORT(idx, ord, name) {ord, name},
#include "proxy_exports.generated.inc"
#undef UBW_EXPORT
};
constexpr size_t kExportCount = sizeof(kExports) / sizeof(kExports[0]);

// The generated index column (used by the stubs' jump offsets) must equal the array position.
#define UBW_EXPORT(idx, ord, name) \
    static_assert(idx < kExportCount && kExports[idx].ordinal == (ord), "export index/ordinal mismatch");
#include "proxy_exports.generated.inc"
#undef UBW_EXPORT

// Routed to when an export is missing from the real DLL. All dwmapi exports return HRESULT
// or BOOL; E_NOTIMPL is non-zero (BOOL TRUE) which is wrong for BOOL, but every such entry
// is logged at attach so a missing export is visible before it matters.
HRESULT WINAPI missing_export() {
    return E_NOTIMPL;
}

constexpr wchar_t kRealName[] = L"\\dwmapi.dll";

} // namespace

// Jump table read by the naked stubs below. Filled once in DllMain(PROCESS_ATTACH); the
// importing exe's code and later-initialised DLLs only call our exports after that returns.
extern "C" void* ubw_real[kExportCount] = {};

// One stub per export: `jmp [ubw_real + 8*idx]`. Naked, so arguments/stack pass through untouched.
#define UBW_EXPORT(idx, ord, name)                                                  \
    extern "C" __attribute__((naked, used)) void ubw_stub_##ord() {                 \
        __asm__ volatile("jmpq *ubw_real+8*" #idx "(%rip)");                        \
    }
#include "proxy_exports.generated.inc"
#undef UBW_EXPORT

namespace ubw::proxy {
namespace log = ue::log;

bool load_real_dll(HMODULE self) {
    for (size_t i = 0; i < kExportCount; ++i) {
        ubw_real[i] = reinterpret_cast<void*>(&missing_export);
    }

    wchar_t path[MAX_PATH];
    const UINT dir_len = GetSystemDirectoryW(path, MAX_PATH);
    const size_t name_len = sizeof(kRealName) / sizeof(kRealName[0]); // includes NUL
    if (dir_len == 0 || dir_len + name_len > MAX_PATH) {
        log::line("FATAL proxy: GetSystemDirectoryW failed (ret=%u, err=%lu)", dir_len, GetLastError());
        return false;
    }
    for (size_t i = 0; i < name_len; ++i) {
        path[dir_len + i] = kRealName[i];
    }

    const HMODULE real = LoadLibraryW(path);
    if (real == nullptr) {
        log::line("FATAL proxy: LoadLibraryW(%ls) failed, err=%lu", path, GetLastError());
        return false;
    }
    if (real == self) {
        log::line("FATAL proxy: LoadLibraryW(%ls) returned the proxy itself", path);
        return false;
    }
    log::line("proxy: real dwmapi %ls at %p", path, static_cast<void*>(real));

    size_t missing = 0;
    for (size_t i = 0; i < kExportCount; ++i) {
        const ExportRow& e = kExports[i];
        const LPCSTR key = e.name != nullptr ? e.name : MAKEINTRESOURCEA(e.ordinal);
        const FARPROC p = GetProcAddress(real, key);
        if (p == nullptr) {
            ++missing;
            log::line("ERROR proxy: export %s (ordinal %u) missing in real DLL -> E_NOTIMPL stub",
                      e.name != nullptr ? e.name : "<noname>", e.ordinal);
            continue;
        }
        ubw_real[i] = reinterpret_cast<void*>(p);
    }
    log::line("proxy: resolved %zu/%zu exports (%zu missing)", kExportCount - missing, kExportCount, missing);
    return true;
}

} // namespace ubw::proxy
