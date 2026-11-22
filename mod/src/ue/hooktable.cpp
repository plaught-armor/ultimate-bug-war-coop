#include "ue/hooktable.h"

#include "ue/log.h"

#include <MinHook.h>

#include <iterator>
#include <stdio.h>
#include <string.h>

namespace ue::hooktable {
namespace {

// CodeView debug record (RSDS) as stored in the image.
struct CvInfoPdb70 {
    DWORD signature; // 'RSDS'
    uint8_t guid[16];
    DWORD age;
    // char pdb_path[] follows
};
constexpr DWORD kRsds = 0x53445352; // "RSDS" little-endian
constexpr DWORD kMaxDebugEntries = 64;
} // namespace

const uint8_t* g_exe_base = nullptr; // set once in ue::install before any hook is enabled
bool g_gate_diag = false;

const IMAGE_NT_HEADERS64* nt_headers(const uint8_t* base) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return nullptr;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return nullptr;
    }
    return nt;
}

bool image_matches_pdb(const uint8_t* base, const IMAGE_NT_HEADERS64* nt, const uint8_t (&guid)[16], uint32_t age) {
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    const DWORD count = dir.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
    if (dir.VirtualAddress == 0 || count == 0 || count > kMaxDebugEntries) {
        log::line("FATAL hooks: debug directory missing or implausible (rva=0x%lx size=0x%lx)",
                  dir.VirtualAddress, dir.Size);
        return false;
    }
    const auto* entries = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(base + dir.VirtualAddress);
    for (DWORD i = 0; i < count; ++i) {
        const IMAGE_DEBUG_DIRECTORY& e = entries[i];
        if (e.Type != IMAGE_DEBUG_TYPE_CODEVIEW || e.AddressOfRawData == 0 || e.SizeOfData < sizeof(CvInfoPdb70)) {
            continue;
        }
        const auto* cv = reinterpret_cast<const CvInfoPdb70*>(base + e.AddressOfRawData);
        if (cv->signature != kRsds) {
            continue;
        }
        const bool same = memcmp(cv->guid, guid, sizeof(cv->guid)) == 0 && cv->age == age;
        if (!same) {
            log::line("FATAL hooks: exe CodeView GUID/age differs from offsets.generated.h -- wrong game build");
        }
        return same;
    }
    log::line("FATAL hooks: no RSDS CodeView record in exe");
    return false;
}

void* code_address(uint8_t* base, const IMAGE_NT_HEADERS64* nt, const ubw::offsets::Symbol& sym) {
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const IMAGE_SECTION_HEADER& s = sec[i];
        if (sym.rva >= s.VirtualAddress && sym.rva < s.VirtualAddress + s.Misc.VirtualSize) {
            if ((s.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
                log::line("FATAL hooks: %s rva 0x%08X is in non-executable section", sym.name, sym.rva);
                return nullptr;
            }
            return base + sym.rva;
        }
    }
    log::line("FATAL hooks: %s rva 0x%08X is outside every section", sym.name, sym.rva);
    return nullptr;
}

bool create_and_enable(void* target, void* detour, void** original, const char* name) {
    MH_STATUS st = MH_CreateHook(target, detour, original);
    if (st != MH_OK) {
        log::line("ERROR hooks: MH_CreateHook(%s @ %p) = %s", name, target, MH_StatusToString(st));
        return false;
    }
    st = MH_EnableHook(target);
    if (st != MH_OK) {
        log::line("ERROR hooks: MH_EnableHook(%s @ %p) = %s", name, target, MH_StatusToString(st));
        return false;
    }
    log::line("hooks: %s hooked at %p", name, target);
    return true;
}

unsigned long long exe_rva(const void* p) {
    const IMAGE_NT_HEADERS64* const nt = nt_headers(g_exe_base);
    const auto* const q = static_cast<const uint8_t*>(p);
    if (nt == nullptr || q < g_exe_base || q >= g_exe_base + nt->OptionalHeader.SizeOfImage) {
        return 0;
    }
    return static_cast<unsigned long long>(q - g_exe_base);
}

bool hook_gated_off(const HookSpec& h) {
    return h.gate != nullptr && !*h.gate;
}

size_t hook_count(std::span<const InstallSet> sets) {
    size_t n = 0;
    for (const InstallSet& s : sets) {
        n += s.hooks.size();
    }
    return n;
}

bool resolve_all(uint8_t* base, const IMAGE_NT_HEADERS64* nt, std::span<const InstallSet> sets) {
    for (const InstallSet& s : sets) {
        for (const HookSpec& h : s.hooks) {
            if (code_address(base, nt, *h.sym) == nullptr) {
                return false;
            }
        }
    }
    for (const InstallSet& s : sets) {
        for (const CallSpec& c : s.calls) {
            void* const fn = code_address(base, nt, *c.sym);
            if (fn == nullptr) {
                return false;
            }
            *c.slot = fn;
        }
    }
    return true;
}

void log_skipped_hooks(std::span<const InstallSet> sets, uint32_t skipped) {
    const size_t total = hook_count(sets);
    log::line("hooks: %u of %zu hooks not installed (gate off), %zu installed", skipped, total, total - skipped);
    char text[768] = {};
    size_t used = 0;
    for (const InstallSet& s : sets) {
        for (const HookSpec& h : s.hooks) {
            if (!hook_gated_off(h)) {
                continue;
            }
            const char* const name = h.sym->name;
            if (used + strlen(name) + 3 > sizeof(text)) {
                log::line("hooks: skipped: %s", text);
                used = 0;
            }
            const int n = snprintf(text + used, sizeof(text) - used, "%s%s", used != 0 ? ", " : "", name);
            used += n > 0 ? static_cast<size_t>(n) : 0;
            used = used < sizeof(text) ? used : sizeof(text) - 1;
        }
    }
    if (used != 0) {
        log::line("hooks: skipped: %s", text);
    }
}

// b16: every call slot and every trampoline of an installed row must be set once install ends. One WARN row names
// each null one; no row when all are set. A trampoline of a gated-off row stays null by design, so code outside its
// detour never calls one: it calls the target through a call row (CallSpec) instead, which this pass covers.
void log_null_slots(std::span<const InstallSet> sets) {
    char text[768] = {};
    size_t used = 0;
    uint32_t nulls = 0;
    auto note = [&](const char* name) {
        ++nulls;
        const int n = snprintf(text + used, sizeof(text) - used, "%s%s", used != 0 ? ", " : "", name);
        used += n > 0 ? static_cast<size_t>(n) : 0;
        used = used < sizeof(text) ? used : sizeof(text) - 1;
    };
    for (const InstallSet& s : sets) {
        for (const CallSpec& c : s.calls) {
            if (*c.slot == nullptr) {
                note(c.sym->name);
            }
        }
        for (const HookSpec& h : s.hooks) {
            if (!hook_gated_off(h) && *h.original == nullptr) {
                note(h.sym->name);
            }
        }
    }
    if (nulls != 0) {
        log::line("WARN hooks: %u null pointers after install (call rows and installed trampolines): %s", nulls, text);
    }
}

namespace {

// Card 571: per-hook call counter, on only with UBWCOOP_C571=1 (off: no stub, no cost). Each installed row hooks a
// 32-byte stub instead of its detour: `mov rax, &count; inc qword [rax]; mov rax, detour; jmp rax`. RAX is volatile
// and carries no argument on Win64, and the stub jumps (no call), so the detour sees the same stack and arguments and
// no unwind data is needed. The inc is not locked: a race between threads can lose a count, which a ranking accepts.
// Both blocks are sized from the row count with VirtualAlloc once at install and kept for the process life.
constexpr size_t kC571Stub = 32;
constexpr uint32_t kC571Top = 20;
struct C571Counts {
    uint8_t* stubs = nullptr;              // rows * kC571Stub, executable after install
    uint64_t* count = nullptr;             // rows, written by the stubs
    uint64_t* seen = nullptr;              // rows, count at the last window row
    const HookSpec** row = nullptr;        // rows, the spec each counter belongs to
    uint32_t rows = 0;                     // rows with a counter so far
    uint32_t cap = 0;                      // rows the blocks hold
};
C571Counts g_c571;

bool c571_on() {
    wchar_t buf[4] = {};
    return GetEnvironmentVariableW(L"UBWCOOP_C571", buf, 4) == 1 && buf[0] == L'1';
}

bool c571_alloc(size_t rows) {
    void* const code = VirtualAlloc(nullptr, rows * kC571Stub, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    void* const data = VirtualAlloc(nullptr, rows * (2 * sizeof(uint64_t) + sizeof(HookSpec*)), MEM_COMMIT | MEM_RESERVE,
                                    PAGE_READWRITE);
    if (code == nullptr || data == nullptr) {
        log::line("WARN card571: VirtualAlloc for %zu hook counters failed (%lu); hooks install uncounted", rows,
                  GetLastError());
        if (code != nullptr) {
            VirtualFree(code, 0, MEM_RELEASE);
        }
        if (data != nullptr) {
            VirtualFree(data, 0, MEM_RELEASE);
        }
        return false;
    }
    g_c571.stubs = static_cast<uint8_t*>(code);
    g_c571.count = static_cast<uint64_t*>(data);
    g_c571.seen = g_c571.count + rows;
    g_c571.row = reinterpret_cast<const HookSpec**>(g_c571.seen + rows);
    g_c571.cap = static_cast<uint32_t>(rows);
    return true;
}

// The stub for one row, or the detour itself when the counter is off or full.
void* c571_stub(const HookSpec& h) {
    if (g_c571.stubs == nullptr || g_c571.rows >= g_c571.cap) {
        return h.detour;
    }
    const uint32_t i = g_c571.rows++;
    uint8_t* const p = g_c571.stubs + static_cast<size_t>(i) * kC571Stub;
    const uint64_t counter = reinterpret_cast<uint64_t>(&g_c571.count[i]);
    const uint64_t detour = reinterpret_cast<uint64_t>(h.detour);
    p[0] = 0x48; // mov rax, imm64
    p[1] = 0xB8;
    memcpy(p + 2, &counter, sizeof(counter));
    p[10] = 0x48; // inc qword [rax]
    p[11] = 0xFF;
    p[12] = 0x00;
    p[13] = 0x48; // mov rax, imm64
    p[14] = 0xB8;
    memcpy(p + 15, &detour, sizeof(detour));
    p[23] = 0xFF; // jmp rax
    p[24] = 0xE0;
    memset(p + 25, 0xCC, kC571Stub - 25);
    g_c571.row[i] = &h;
    return p;
}

// Makes the written stubs executable; any failure leaves them unhooked, so it runs before the first MH_CreateHook.
bool c571_seal() {
    DWORD old = 0;
    if (!VirtualProtect(g_c571.stubs, static_cast<size_t>(g_c571.cap) * kC571Stub, PAGE_EXECUTE_READ, &old)) {
        log::line("WARN card571: VirtualProtect of the hook stubs failed (%lu); hooks install uncounted",
                  GetLastError());
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), g_c571.stubs, static_cast<size_t>(g_c571.cap) * kC571Stub);
    return true;
}

// Writes every stub (one per row whose gate is on), then seals them. Returns false to install uncounted.
bool c571_prepare(std::span<const InstallSet> sets) {
    if (!c571_on() || !c571_alloc(hook_count(sets))) {
        return false;
    }
    for (const InstallSet& s : sets) {
        for (const HookSpec& h : s.hooks) {
            if (!hook_gated_off(h)) {
                (void)c571_stub(h);
            }
        }
    }
    if (!c571_seal()) {
        g_c571.stubs = nullptr;
        return false;
    }
    log::line("card571: hook call counter on, %u rows counted", g_c571.rows);
    return true;
}

} // namespace

void log_hook_counts(const char* side, unsigned long long now, unsigned long long window_ms) {
    if (g_c571.stubs == nullptr || g_c571.rows == 0) {
        return;
    }
    uint32_t top[kC571Top] = {};
    uint64_t top_n[kC571Top] = {};
    uint32_t used = 0;
    uint64_t total = 0;
    for (uint32_t i = 0; i < g_c571.rows; ++i) {
        const uint64_t d = g_c571.count[i] - g_c571.seen[i];
        g_c571.seen[i] += d;
        total += d;
        uint32_t at = used < kC571Top ? used++ : kC571Top;
        if (at == kC571Top && d <= top_n[kC571Top - 1]) {
            continue;
        }
        at = at == kC571Top ? kC571Top - 1 : at;
        for (; at > 0 && top_n[at - 1] < d; --at) {
            top[at] = top[at - 1];
            top_n[at] = top_n[at - 1];
        }
        top[at] = i;
        top_n[at] = d;
    }
    char text[1400];
    int len = 0;
    for (uint32_t k = 0; k < used && len >= 0 && static_cast<size_t>(len) < sizeof(text); ++k) {
        len += snprintf(text + len, sizeof(text) - static_cast<size_t>(len), "%s%s %llu", k == 0 ? "" : ", ",
                        g_c571.row[top[k]]->sym->name, static_cast<unsigned long long>(top_n[k]));
    }
    const double per_s = window_ms != 0 ? static_cast<double>(total) * 1000.0 / static_cast<double>(window_ms) : 0.0;
    log::line("card571: %s t=%llu hook calls in %llu ms: %llu total (%.0f/s) over %u rows; top %u: %s", side, now,
              window_ms, static_cast<unsigned long long>(total), per_s, g_c571.rows, used, used != 0 ? text : "none");
}

bool install_hooks(uint8_t* base, const IMAGE_NT_HEADERS64* nt, std::span<const InstallSet> sets) {
    const bool counted = c571_prepare(sets);
    uint32_t counted_row = 0;
    uint32_t skipped = 0;
    for (const InstallSet& s : sets) {
        for (const HookSpec& h : s.hooks) {
            if (hook_gated_off(h)) {
                ++skipped;
                continue;
            }
            // The address again: base + RVA inside a code section, which resolve_all checked for every row.
            void* const detour = counted && counted_row < g_c571.rows
                                     ? g_c571.stubs + static_cast<size_t>(counted_row++) * kC571Stub
                                     : h.detour;
            if (!create_and_enable(code_address(base, nt, *h.sym), detour, h.original, h.sym->name)) {
                return false;
            }
        }
    }
    log_skipped_hooks(sets, skipped);
    log_null_slots(sets);
    return true;
}

// Base and PE64 headers of the host exe, or false (logged FATAL).
bool exe_image(uint8_t*& base, const IMAGE_NT_HEADERS64*& nt) {
    base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    if (base == nullptr) {
        log::line("FATAL hooks: GetModuleHandleW(nullptr) failed, err=%lu", GetLastError());
        return false;
    }
    nt = nt_headers(base);
    if (nt == nullptr) {
        log::line("FATAL hooks: exe at %p has no valid PE64 headers", static_cast<void*>(base));
        return false;
    }
    return true;
}

} // namespace ue::hooktable
