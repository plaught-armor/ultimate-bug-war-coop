// Card 350: no WARNING (photosensitivity) screen after the card 344 return to the menu, on the host or the client.
//
// The frontend HUD (BP_HUD_3DFrontend BeginPlay, "Get Setup from Options") reads GetGameMode -> OptionsString with
// HasOption(FromLevelComplete / FromLevelQuit) and GetIntOption(ChapterID / LevelID); with neither option it runs the
// first-start FTUE widgets, WB_FTUE_PhotoSensitivity first. Two gaps reached it:
//  - host: card 344 called USTGameInstance::ReturnToMainMenu, whose OpenLevel ("MainMenu_InteriorScene", no options)
//    gave a valid-0 URL and the engine's "?closed" fallback. The pause menu QUIT (WB_Pause_Menu ConfirmQuitToMainMenu)
//    opens /Game/Maps/3D_Frontend/3D_Frontend with "FromLevelQuit?ChapterID=<c>?LevelID=<l>"; card 350 does the same.
//  - client: no GameMode, so its options string is always empty. The host's seamless ClientTravel URL to the frontend
//    carries the options; the client keeps them and answers the HUD's empty-string HasOption / ParseOption for the four
//    frontend setup keys only.

#include "ubw/hooks_internal.h"

namespace ubw::hooks {

OpenLevelFn g_c350_open_level = nullptr;
HasOptionFn g_orig_c350_has_option = nullptr;
ParseOptionFn g_orig_c350_parse_option = nullptr;
ClientTravelInternalFn g_orig_c350_client_travel = nullptr;

namespace {

constexpr size_t kC350OptionChars = 256;
constexpr uint32_t kC350LogLines = 24;
constexpr const wchar_t* kC350Frontend = L"/Game/Maps/3D_Frontend/3D_Frontend";
constexpr const wchar_t* kC350Keys[] = {L"FromLevelQuit", L"FromLevelComplete", L"ChapterID", L"LevelID"};

struct C350State {
    wchar_t options[kC350OptionChars]; // client: the host's frontend travel options ("" off the frontend)
    uint64_t frontend_name;            // FName cache
    uint32_t lines;
};
C350State g_c350{};

bool c350_key_listed(const FStringRaw* key) {
    const int len = fs_len(key);
    for (const wchar_t* const k : kC350Keys) {
        if (static_cast<size_t>(len) == wcslen(k) && _wcsnicmp(fs_text(key), k, static_cast<size_t>(len)) == 0) {
            return true;
        }
    }
    return false;
}

// The stored option `key` (UGameplayStatics rules: "?Key" or "?Key=Value", case-insensitive): its value and length,
// or null when absent. The store holds the URL text after its first '?', options separated by '?'.
const wchar_t* c350_option(const FStringRaw* key, size_t* value_len) {
    const size_t n = static_cast<size_t>(fs_len(key));
    const wchar_t* p = g_c350.options;
    for (size_t guard = 0; *p != L'\0' && guard < kC350OptionChars; ++guard) {
        const wchar_t* const end = wcschr(p, L'?');
        const size_t len = end != nullptr ? static_cast<size_t>(end - p) : wcslen(p);
        if (n > 0 && len >= n && _wcsnicmp(p, fs_text(key), n) == 0 && (len == n || p[n] == L'=')) {
            *value_len = len > n ? len - n - 1 : 0;
            return len > n ? p + n + 1 : p + n;
        }
        if (end == nullptr) {
            break;
        }
        p = end + 1;
    }
    return nullptr;
}

bool c350_client_answers(const FStringRaw* options, const FStringRaw* key) {
    return g_client_session && fs_len(options) == 0 && g_c350.options[0] != L'\0' && c350_key_listed(key);
}

} // namespace

bool c350_host_return_to_menu(void* context, const void* game_instance) {
    if (g_c350_open_level == nullptr || g_memory_malloc == nullptr ||
        fname_cached(g_c350.frontend_name, kC350Frontend) == 0) {
        log::line("WARN card350: host OpenLevel, FMemory::Malloc or the frontend FName unresolved: no return");
        return false;
    }
    wchar_t text[kC350OptionChars];
    const int chars = swprintf(text, kC350OptionChars, L"FromLevelQuit?ChapterID=%d?LevelID=%d",
                               read_i32(game_instance, kGameInstanceChapter),
                               read_i32(game_instance, kGameInstanceLevel));
    auto* const buf =
        chars > 0 ? static_cast<wchar_t*>(g_memory_malloc(static_cast<size_t>(chars + 1) * sizeof(wchar_t), 0)) : nullptr;
    if (buf == nullptr) {
        log::line("WARN card350: host FMemory::Malloc of the OpenLevel options failed: no return");
        return false;
    }
    memcpy(buf, text, static_cast<size_t>(chars + 1) * sizeof(wchar_t));
    FStringRaw options{buf, chars + 1, chars + 1}; // by value: OpenLevel frees it
    log::line("card350: host t=%llu return to the menu: OpenLevel(\"%ls\", absolute, \"%ls\"), as the pause menu QUIT",
              static_cast<unsigned long long>(GetTickCount64()), kC350Frontend, text);
    g_c350_open_level(context, g_c350.frontend_name, true, &options);
    return true;
}

void c350_client_session_end() {
    if (g_c350.options[0] != L'\0' && note_event_cap(g_c350.lines, kC350LogLines, "card350")) {
        log::line("card350: client t=%llu session ended: frontend options \"%ls\" cleared",
                  static_cast<unsigned long long>(GetTickCount64()), g_c350.options);
    }
    g_c350.options[0] = L'\0';
}

void detour_c350_client_travel(void* pc, const FStringRaw* url, int32_t type, bool seamless, const void* guid) {
    if (g_client_session) {
        const wchar_t* const text = fs_text(url);
        const wchar_t* const q = wcschr(text, L'?');
        const size_t map_len = q != nullptr ? static_cast<size_t>(q - text) : wcslen(text);
        const bool frontend = map_len == wcslen(kC350Frontend) && _wcsnicmp(text, kC350Frontend, map_len) == 0;
        swprintf(g_c350.options, kC350OptionChars, L"%ls", frontend && q != nullptr ? q + 1 : L"");
        if (note_event_cap(g_c350.lines, kC350LogLines, "card350")) {
            log::line("card350: client t=%llu ClientTravel \"%ls\" (seamless %d): frontend options \"%ls\"",
                      static_cast<unsigned long long>(GetTickCount64()), text, seamless ? 1 : 0, g_c350.options);
        }
    }
    g_orig_c350_client_travel(pc, url, type, seamless, guid);
}

bool detour_c350_has_option(FStringRaw* options, const FStringRaw* key) {
    const bool answer = c350_client_answers(options, key); // read before the call: it destroys `options`
    const bool found = g_orig_c350_has_option(options, key);
    if (!answer || found) {
        return found;
    }
    size_t len = 0;
    const bool stored = c350_option(key, &len) != nullptr;
    if (note_event_cap(g_c350.lines, kC350LogLines, "card350")) {
        log::line("card350: client t=%llu HasOption(\"\", %ls) from the host's travel options: %d",
                  static_cast<unsigned long long>(GetTickCount64()), fs_text(key), stored ? 1 : 0);
    }
    return stored;
}

FStringRaw* detour_c350_parse_option(FStringRaw* out, FStringRaw* options, const FStringRaw* key) {
    const bool answer = c350_client_answers(options, key);
    FStringRaw* const r = g_orig_c350_parse_option(out, options, key);
    size_t len = 0;
    const wchar_t* const value = answer && fs_len(r) == 0 ? c350_option(key, &len) : nullptr;
    if (value == nullptr || len == 0 || g_memory_malloc == nullptr) {
        return r;
    }
    auto* const buf = static_cast<wchar_t*>(g_memory_malloc((len + 1) * sizeof(wchar_t), 0));
    if (buf == nullptr) {
        log::line("WARN card350: client FMemory::Malloc of the ParseOption value failed");
        return r;
    }
    memcpy(buf, value, len * sizeof(wchar_t));
    buf[len] = L'\0';
    if (r->data != nullptr && g_memory_free != nullptr) {
        g_memory_free(const_cast<wchar_t*>(r->data));
    }
    *r = FStringRaw{buf, static_cast<int32_t>(len + 1), static_cast<int32_t>(len + 1)};
    if (note_event_cap(g_c350.lines, kC350LogLines, "card350")) {
        log::line("card350: client t=%llu ParseOption(\"\", %ls) from the host's travel options: \"%ls\"",
                  static_cast<unsigned long long>(GetTickCount64()), fs_text(key), buf);
    }
    return r;
}

} // namespace ubw::hooks
