// Card 130 item 17 (see galaxycfg.h). The copy is shared by both instances of a test pair (one prefix, one
// game dir). It is written only when absent or different (tmp file + MoveFileExW, the cmdfile pattern), and
// the redirected open adds FILE_SHARE_READ | FILE_SHARE_DELETE so the other instance can still read or
// replace it (card 114 err 32). Every failure falls back to the real file with a WARN: the open never fails
// because of the mod.

#include "galaxycfg.h"

#include "ue/log.h"

#include <windows.h>
#include <winternl.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

namespace ubw::galaxycfg {

namespace {

constexpr wchar_t kModule[] = L"steam_api64.dll";
constexpr wchar_t kConfigName[] = L"GalaxyConfig.json";
constexpr wchar_t kCopyName[] = L"UBWCoop-GalaxyConfig.json";
constexpr char kKey[] = "\"auth_timeout\"";
constexpr char kInsert[] = "\"auth_timeout\": 1";  // seconds: SignInGalaxy fails offline after 1 s, not 15
constexpr DWORD kMaxJson = 64 * 1024;
constexpr uint32_t kMaxImports = 64;
constexpr uint32_t kMaxThunks = 4096;
constexpr uint32_t kWriteTries = 10;
constexpr DWORD kWriteRetryMs = 50;

using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

struct State {
    CreateFileWFn orig;            // the IAT's previous CreateFileW
    INIT_ONCE once;                // prepares the copy on the first matching open
    bool ready;                    // copy holds the wanted content: redirect
    wchar_t copy[MAX_PATH];
    volatile LONG logged;          // the first redirect line
    volatile LONG patched;         // the IAT patch ran
    void* cookie;                  // LdrRegisterDllNotification cookie (kept: unregistering from the callback is unsafe)
};
State g{nullptr, INIT_ONCE_STATIC_INIT, false, {}, 0, 0, nullptr};

struct Buf {
    char data[kMaxJson + 64];
    DWORD len;
};
Buf g_real;   // the real JSON
Buf g_want;   // the wanted copy
Buf g_have;   // the copy on disk

bool ends_with_ci(const wchar_t* s, const wchar_t* tail) {
    const size_t n = wcslen(s), t = wcslen(tail);
    return n >= t && _wcsicmp(s + n - t, tail) == 0 && (n == t || s[n - t - 1] == L'\\' || s[n - t - 1] == L'/');
}

// Whole file into `b` (bounded). False on any failure or a file over kMaxJson.
bool read_file(const wchar_t* path, Buf& b) {
    const HANDLE f = g.orig(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        return false;
    }
    const bool ok = ReadFile(f, b.data, kMaxJson + 1, &b.len, nullptr) != 0 && b.len <= kMaxJson;
    CloseHandle(f);
    return ok;
}

// g_want from g_real: "auth_timeout" inserted before the last '}'. False when the JSON has no '}'.
bool build_copy() {
    DWORD close = g_real.len;
    for (DWORD i = g_real.len; i > 0; --i) {
        if (g_real.data[i - 1] == '}') {
            close = i - 1;
            break;
        }
    }
    if (close == g_real.len) {
        return false;
    }
    DWORD last = close;  // the last non-space byte before '}'
    for (DWORD i = close; i > 0 && last == close; --i) {
        const char c = g_real.data[i - 1];
        last = c == ' ' || c == '\t' || c == '\r' || c == '\n' ? close : i - 1;
    }
    const bool empty = last == close || g_real.data[last] == '{';
    const DWORD head = last == close ? close : last + 1;
    const int n = snprintf(g_want.data, sizeof(g_want.data), "%.*s%s\n    %s\n%.*s", static_cast<int>(head),
                           g_real.data, empty ? "" : ",", kInsert, static_cast<int>(g_real.len - close),
                           g_real.data + close);
    g_want.len = n > 0 ? static_cast<DWORD>(n) : 0;
    return n > 0 && static_cast<size_t>(n) < sizeof(g_want.data);
}

bool copy_matches() {
    return read_file(g.copy, g_have) && g_have.len == g_want.len && memcmp(g_have.data, g_want.data, g_want.len) == 0;
}

// One tmp write + replace. Logs its own failure.
bool write_copy(uint32_t attempt) {
    wchar_t tmp[MAX_PATH + 32];
    swprintf(tmp, sizeof(tmp) / sizeof(tmp[0]), L"%ls.%lu.tmp", g.copy, GetCurrentProcessId());
    const HANDLE f = g.orig(tmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        ue::log::line("galaxycfg: try %u: cannot create %ls, err=%lu", attempt, tmp, GetLastError());
        return false;
    }
    DWORD wrote = 0;
    const bool ok = WriteFile(f, g_want.data, g_want.len, &wrote, nullptr) != 0 && wrote == g_want.len;
    const DWORD werr = GetLastError();
    CloseHandle(f);
    if (ok && MoveFileExW(tmp, g.copy, MOVEFILE_REPLACE_EXISTING)) {
        return true;
    }
    ue::log::line("galaxycfg: try %u: %ls failed, err=%lu", attempt, ok ? L"MoveFileExW" : L"WriteFile",
                  ok ? GetLastError() : werr);
    DeleteFileW(tmp);
    return false;
}

// The copy next to `real` holds g_want: already there (the other instance, or a past run) or written now.
bool ensure_copy() {
    for (uint32_t i = 1; i <= kWriteTries; ++i) {
        if (copy_matches() || write_copy(i)) {
            return true;
        }
        Sleep(kWriteRetryMs);
    }
    return copy_matches();
}

BOOL CALLBACK prepare(PINIT_ONCE, PVOID param, PVOID*) {
    const wchar_t* const real = static_cast<const wchar_t*>(param);
    wchar_t full[MAX_PATH];
    wchar_t* file = nullptr;
    const DWORD n = GetFullPathNameW(real, MAX_PATH, full, &file);
    if (n == 0 || n >= MAX_PATH || file == nullptr || !read_file(full, g_real)) {
        ue::log::line("WARN galaxycfg: cannot read %ls (err=%lu): not redirected, Galaxy sign-in waits its default 15 s",
                      real, GetLastError());
        return TRUE;
    }
    g_real.data[g_real.len] = '\0';
    if (strstr(g_real.data, kKey) != nullptr) {
        ue::log::line("galaxycfg: %ls already has auth_timeout: not redirected", full);
        return TRUE;
    }
    *file = L'\0';
    swprintf(g.copy, MAX_PATH, L"%ls%ls", full, kCopyName);
    if (!build_copy()) {
        ue::log::line("WARN galaxycfg: %ls%ls has no closing '}': not redirected", full, kConfigName);
        return TRUE;
    }
    g.ready = ensure_copy();
    if (!g.ready) {
        ue::log::line("WARN galaxycfg: cannot write %ls after %u tries: not redirected", g.copy, kWriteTries);
    }
    return TRUE;
}

HANDLE WINAPI hook_create_file_w(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp,
                                 DWORD flags, HANDLE tmpl) {
    const bool read_only = (access & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA)) == 0;
    if (path == nullptr || !read_only || !ends_with_ci(path, kConfigName)) {
        return g.orig(path, access, share, sa, disp, flags, tmpl);
    }
    InitOnceExecuteOnce(&g.once, &prepare, const_cast<wchar_t*>(path), nullptr);
    if (!g.ready) {
        return g.orig(path, access, share, sa, disp, flags, tmpl);
    }
    const HANDLE h = g.orig(g.copy, access, share | FILE_SHARE_READ | FILE_SHARE_DELETE, sa, disp, flags, tmpl);
    const DWORD err = GetLastError();
    if (h == INVALID_HANDLE_VALUE) {
        ue::log::line("WARN galaxycfg: open of %ls failed, err=%lu: real %ls opened instead", g.copy, err, path);
        return g.orig(path, access, share, sa, disp, flags, tmpl);
    }
    if (InterlockedExchange(&g.logged, 1) == 0) {
        ue::log::line("galaxycfg: steam_api64 open of %ls redirected to %ls (auth_timeout 1 s added; default 15 s)",
                      path, g.copy);
    }
    SetLastError(err);
    return h;
}

// The FirstThunk slot of KERNEL32!CreateFileW in `base`'s import table, or nullptr.
void** find_slot(uint8_t* base) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    const auto* imp = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (uint32_t i = 0; dir.VirtualAddress != 0 && i < kMaxImports && imp[i].Name != 0; ++i) {
        if (_stricmp(reinterpret_cast<const char*>(base + imp[i].Name), "KERNEL32.dll") != 0 ||
            imp[i].OriginalFirstThunk == 0) {
            continue;
        }
        const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + imp[i].OriginalFirstThunk);
        auto* slots = reinterpret_cast<void**>(base + imp[i].FirstThunk);
        for (uint32_t k = 0; k < kMaxThunks && names[k].u1.AddressOfData != 0; ++k) {
            if (IMAGE_SNAP_BY_ORDINAL64(names[k].u1.Ordinal)) {
                continue;
            }
            const auto* by = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names[k].u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char*>(by->Name), "CreateFileW") == 0) {
                return &slots[k];
            }
        }
    }
    return nullptr;
}

void patch(uint8_t* base, const char* when) {
    if (InterlockedExchange(&g.patched, 1) != 0) {
        return;
    }
    void** const slot = find_slot(base);
    DWORD old = 0;
    if (slot == nullptr || !VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) {
        ue::log::line("WARN galaxycfg: %ls at %p: no KERNEL32 CreateFileW import slot (or VirtualProtect failed, "
                      "err=%lu): not patched", kModule, static_cast<void*>(base), GetLastError());
        return;
    }
    g.orig = reinterpret_cast<CreateFileWFn>(*slot);
    *slot = reinterpret_cast<void*>(&hook_create_file_w);
    VirtualProtect(slot, sizeof(*slot), old, &old);
    ue::log::line("galaxycfg: %ls at %p (%s): CreateFileW import slot %p patched (was %p)", kModule,
                  static_cast<void*>(base), when, static_cast<void*>(slot), reinterpret_cast<void*>(g.orig));
}

// ntdll LDR_DLL_NOTIFICATION_DATA (loaded and unloaded share the layout).
struct LdrNotify {
    ULONG flags;
    const UNICODE_STRING* full_name;
    const UNICODE_STRING* base_name;
    void* base;
    ULONG size;
};
constexpr ULONG kLdrLoaded = 1;
using LdrNotifyFn = VOID(CALLBACK*)(ULONG, const LdrNotify*, PVOID);
using LdrRegisterFn = LONG(NTAPI*)(ULONG, LdrNotifyFn, PVOID, PVOID*);

VOID CALLBACK on_dll(ULONG reason, const LdrNotify* data, PVOID) {
    if (reason != kLdrLoaded || data == nullptr || data->base_name == nullptr || data->base_name->Buffer == nullptr) {
        return;
    }
    const UNICODE_STRING& n = *data->base_name;
    const size_t want = wcslen(kModule);
    if (n.Length / sizeof(wchar_t) == want && _wcsnicmp(n.Buffer, kModule, want) == 0) {
        patch(static_cast<uint8_t*>(data->base), "load notification");
    }
}

} // namespace

bool start() {
    const HMODULE loaded = GetModuleHandleW(kModule);
    if (loaded != nullptr) {
        patch(reinterpret_cast<uint8_t*>(loaded), "already loaded at attach");
        return true;
    }
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto reg = ntdll != nullptr
                         ? reinterpret_cast<LdrRegisterFn>(GetProcAddress(ntdll, "LdrRegisterDllNotification"))
                         : nullptr;
    const LONG st = reg != nullptr ? reg(0, &on_dll, nullptr, &g.cookie) : -1;
    if (st != 0) {
        ue::log::line("WARN galaxycfg: LdrRegisterDllNotification unavailable (status 0x%08lX): %ls not patched, Galaxy "
                      "sign-in waits its default 15 s", static_cast<unsigned long>(st), kModule);
        return false;
    }
    ue::log::line("galaxycfg: waiting for %ls to load (DLL load notification registered)", kModule);
    return true;
}

} // namespace ubw::galaxycfg
