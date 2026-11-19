// Card 118: always-on crash trace on both sides.
// The shipping game writes no log and, in the first internet playtest, left no dump for a client "Fatal error".
// A vectored exception handler writes one block per fatal exception to the mod log and flushes it: the code, the
// faulting address as exe+0x... (or module+offset), the access for an access violation, a few registers and a
// return-address stack of at most kMaxFrames frames. It always returns EXCEPTION_CONTINUE_SEARCH, so the game's
// own handling (UE's __except -> ReportCrash -> GError->HandleError -> "Fatal error" dialog) is unchanged.
// The ReportCrash and FWindowsErrorOutputDevice::HandleError hooks log UE's own error text (GErrorHist,
// GErrorExceptionDescription), which also covers fatal errors that are not hardware exceptions (check, fatal log).
// No heap use anywhere on this path: stack buffers, Rtl unwind calls and direct reads of mapped PE headers only.

#include "ubw/hooks_internal.h"

#include <windows.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace ubw::hooks {
namespace {

constexpr LONG kMaxBlocks = 4;      // a crash cascade (fault in a handler) must not flood the log
constexpr uint32_t kMaxFrames = 32;
constexpr uint32_t kFramesPerLine = 4;
constexpr size_t kAddrCap = 64;     // "<module>+0x<offset>" text
constexpr size_t kMaxWideChunk = 400;
constexpr uint32_t kMaxWideChunks = 4; // UE error text logged: at most 1600 characters per buffer
constexpr size_t kErrorBufChars = 16384; // GErrorHist / GErrorExceptionDescription are TCHAR[16384]

volatile LONG g_blocks = 0;
// Set when UE's ReportCrash starts. Its crash reporting thread then walks every thread's stack
// (FCrashReportingThread::HandleCrashInternal -> CaptureAllThreadContexts -> CaptureStackTraceInternal) and takes
// first-chance access violations inside ntdll that it handles itself (run c118c: three of them used up the block
// cap); nothing after that point is the game's crash, so the handler stands down.
volatile LONG g_reporting = 0;
PVOID g_veh = nullptr;

struct CodeName {
    DWORD code;
    const char* name;
};

// Only these codes are fatal for the card; the rest (C++ throws, breakpoints, guard pages, debugger
// notifications) pass through untouched.
constexpr CodeName kFatalCodes[] = {
    {EXCEPTION_ACCESS_VIOLATION, "EXCEPTION_ACCESS_VIOLATION"},
    {EXCEPTION_ILLEGAL_INSTRUCTION, "EXCEPTION_ILLEGAL_INSTRUCTION"},
    {EXCEPTION_STACK_OVERFLOW, "EXCEPTION_STACK_OVERFLOW"},
    {EXCEPTION_INT_DIVIDE_BY_ZERO, "EXCEPTION_INT_DIVIDE_BY_ZERO"},
};

const char* fatal_name(DWORD code) {
    for (const CodeName& c : kFatalCodes) {
        if (c.code == code) {
            return c.name;
        }
    }
    return nullptr;
}

// The export-directory name of a mapped PE image ("ntdll.dll"), or nullptr. Reads only the image's own headers,
// which stay mapped for a loaded module; takes no loader lock (GetModuleFileName could, from a crashing thread).
const char* image_name(const uint8_t* base) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return nullptr;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return nullptr;
    }
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    const DWORD size = nt->OptionalHeader.SizeOfImage;
    if (dir.VirtualAddress == 0 || dir.VirtualAddress + sizeof(IMAGE_EXPORT_DIRECTORY) > size) {
        return nullptr;
    }
    const auto* exp = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
    return exp->Name != 0 && exp->Name < size ? reinterpret_cast<const char*>(base + exp->Name) : nullptr;
}

// Writes pc as "exe+0x...", "ubwcoop+0x...", "<export name>+0x..." or a bare address.
void describe(uint64_t pc, char* out, size_t cap) {
    PVOID base = nullptr;
    RtlPcToFileHeader(reinterpret_cast<PVOID>(pc), &base);
    const auto* b = static_cast<const uint8_t*>(base);
    if (b == nullptr) {
        snprintf(out, cap, "0x%llX", static_cast<unsigned long long>(pc));
        return;
    }
    const unsigned long long off = pc - reinterpret_cast<uint64_t>(b);
    if (b == g_exe_base) {
        snprintf(out, cap, "exe+0x%llX", off);
    } else if (b == reinterpret_cast<const uint8_t*>(&__ImageBase)) {
        snprintf(out, cap, "ubwcoop+0x%llX", off);
    } else {
        const char* name = image_name(b);
        snprintf(out, cap, "%.24s+0x%llX", name != nullptr ? name : "module", off);
    }
}

// True when [p, p + 8) lies inside the current thread's stack.
bool on_stack(uint64_t p) {
    const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const auto lo = reinterpret_cast<uint64_t>(tib->StackLimit);
    const auto hi = reinterpret_cast<uint64_t>(tib->StackBase);
    return p >= lo && p + 8 <= hi;
}

// One unwind step on ctx. A frame without unwind data is treated as a leaf (or a call through a bad pointer):
// its return address is at [rsp]. False when the walk cannot continue.
bool unwind_step(CONTEXT* ctx) {
    DWORD64 image = 0;
    PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(ctx->Rip, &image, nullptr);
    if (fe == nullptr) {
        if (!on_stack(ctx->Rsp)) {
            return false;
        }
        ctx->Rip = *reinterpret_cast<const DWORD64*>(ctx->Rsp);
        ctx->Rsp += 8;
    } else {
        PVOID handler_data = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, ctx->Rip, fe, ctx, &handler_data, &establisher, nullptr);
    }
    return ctx->Rip != 0 && on_stack(ctx->Rsp);
}

// Logs the return-address stack starting at the faulting frame, kFramesPerLine frames per line.
void log_stack(LONG n, const CONTEXT& fault) {
    CONTEXT ctx = fault;
    char line[kFramesPerLine * kAddrCap + 32];
    size_t pos = 0;
    for (uint32_t i = 0; i < kMaxFrames; ++i) {
        char addr[kAddrCap];
        describe(ctx.Rip, addr, sizeof(addr));
        const int w = snprintf(line + pos, sizeof(line) - pos, " #%u %s", i, addr);
        pos += w > 0 ? static_cast<size_t>(w) : 0;
        const bool more = unwind_step(&ctx);
        if ((i + 1) % kFramesPerLine == 0 || !more || i + 1 == kMaxFrames) {
            log::crash_line("CRASH %ld: stack%s", n, line);
            pos = 0;
            line[0] = '\0';
        }
        if (!more) {
            break;
        }
    }
}

void log_block(LONG n, const EXCEPTION_RECORD& rec, const CONTEXT& ctx, const char* name) {
    char where[kAddrCap];
    describe(reinterpret_cast<uint64_t>(rec.ExceptionAddress), where, sizeof(where));
    log::crash_line("CRASH %ld/%ld: %s (0x%08lX) at %s thread %lu (first chance; the game's handling continues)", n,
                    kMaxBlocks, name, rec.ExceptionCode, where, GetCurrentThreadId());
    if (rec.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec.NumberParameters >= 2) {
        const ULONG_PTR kind = rec.ExceptionInformation[0];
        const char* access = kind == 0 ? "reading" : kind == 1 ? "writing" : kind == 8 ? "executing" : "accessing";
        log::crash_line("CRASH %ld: %s 0x%llX", n, access,
                        static_cast<unsigned long long>(rec.ExceptionInformation[1]));
    }
    log::crash_line("CRASH %ld: rax=%llX rbx=%llX rcx=%llX rdx=%llX rsi=%llX rdi=%llX", n, ctx.Rax, ctx.Rbx,
                    ctx.Rcx, ctx.Rdx, ctx.Rsi, ctx.Rdi);
    log::crash_line("CRASH %ld: r8=%llX r9=%llX r10=%llX r11=%llX rsp=%llX rbp=%llX", n, ctx.R8, ctx.R9, ctx.R10,
                    ctx.R11, ctx.Rsp, ctx.Rbp);
    log_stack(n, ctx);
    log::crash_line("CRASH %ld: end", n);
    log::flush();
}

LONG CALLBACK on_exception(PEXCEPTION_POINTERS info) {
    const EXCEPTION_RECORD& rec = *info->ExceptionRecord;
    const char* name = fatal_name(rec.ExceptionCode);
    if (name == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (g_reporting != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const LONG n = InterlockedIncrement(&g_blocks);
    if (n <= kMaxBlocks) {
        log_block(n, rec, *info->ContextRecord, name);
    }
    return EXCEPTION_CONTINUE_SEARCH; // never swallow: the game (or the OS) handles it as before
}

// Logs a UE TCHAR error buffer as ASCII, in chunks of kMaxWideChunk characters (non-ASCII -> '?').
void log_wide(const char* tag, const wchar_t* text) {
    if (text[0] == L'\0') {
        log::crash_line("CRASH ue: %s empty", tag);
        return;
    }
    size_t i = 0;
    for (uint32_t chunk = 0; chunk < kMaxWideChunks && i < kErrorBufChars && text[i] != L'\0'; ++chunk) {
        char out[kMaxWideChunk + 1];
        size_t n = 0;
        for (; n < kMaxWideChunk && i < kErrorBufChars && text[i] != L'\0'; ++n, ++i) {
            const wchar_t c = text[i];
            out[n] = c == L'\r' || c == L'\n' ? ' ' : (c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '?');
        }
        out[n] = '\0';
        log::crash_line("CRASH ue: %s[%u] %s", tag, chunk, out);
    }
}

void log_ue_error_text() {
    log_wide("GErrorHist", reinterpret_cast<const wchar_t*>(g_exe_base + offsets::GErrorHist.rva));
    log_wide("GErrorExceptionDescription",
             reinterpret_cast<const wchar_t*>(g_exe_base + offsets::GErrorExceptionDescription.rva));
}

} // namespace

// Card 303: the frame watchdog's samples are described the same way as a crash stack.
void c118_describe_pc(uint64_t pc, char* out, size_t cap) {
    describe(pc, out, cap);
}

ReportCrashFn g_orig_report_crash = nullptr;
HandleErrorFn g_orig_handle_error = nullptr;

void install_crash_trace() {
    g_veh = AddVectoredExceptionHandler(1, &on_exception);
    if (g_veh == nullptr) {
        log::line("ERROR c118: AddVectoredExceptionHandler failed -- no crash trace");
        return;
    }
    log::line("c118: crash trace installed (vectored handler, at most %ld blocks, %u frames each)", kMaxBlocks,
              kMaxFrames);
}

int detour_report_crash(EXCEPTION_POINTERS* info) {
    const DWORD code = info != nullptr && info->ExceptionRecord != nullptr ? info->ExceptionRecord->ExceptionCode : 0;
    char where[kAddrCap] = "?";
    if (info != nullptr && info->ExceptionRecord != nullptr) {
        describe(reinterpret_cast<uint64_t>(info->ExceptionRecord->ExceptionAddress), where, sizeof(where));
    }
    InterlockedExchange(&g_reporting, 1);
    log::crash_line("CRASH ue: ReportCrash code 0x%08lX at %s thread %lu", code, where, GetCurrentThreadId());
    // Logged before the call: the original writes the dump and ends the process without returning (run c118c).
    // A check or fatal log has filled GErrorHist by now; for a hardware exception it is still empty.
    log_ue_error_text();
    log::flush();
    const int r = g_orig_report_crash(info);
    log::crash_line("CRASH ue: ReportCrash returned %d", r);
    log::flush();
    return r;
}

void detour_handle_error(void* device) {
    log::crash_line("CRASH ue: FWindowsErrorOutputDevice::HandleError thread %lu", GetCurrentThreadId());
    log_ue_error_text();
    log::flush();
    g_orig_handle_error(device);
}

} // namespace ubw::hooks
