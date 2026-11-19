// UBWCoop: dwmapi.dll proxy injected into StarshipTroopers_UBW-Win64-Shipping.exe.
// Load path: the exe statically imports dwmapi.dll; with WINEDLLOVERRIDES=dwmapi=n,b the copy
// next to the exe (this DLL) is loaded, which forwards to <system dir>\dwmapi.dll.

#include "ue/config.h"
#include "ubw/flags.h"
#include "ubw/init.h"
#include "ue/log.h"
#include "galaxycfg.h"
#include "movieskip.h"
#include "upnp.h"
#include "proxy.h"

#include <windows.h>

namespace {

DWORD WINAPI worker(LPVOID) {
    ue::log::line("worker: started");
    const ue::config::Travel travel = ue::config::read_travel();
    const bool client_game_only = ubw::config::read_client_game_only();
    const ubw::config::AutoInput autoinput = ubw::config::read_autoinput();
    const bool auto_dropship = ubw::config::read_auto_dropship();
    const ubw::config::GrantWeapon grant = ubw::config::read_grant_weapon();
    const bool kill_trooper = ubw::config::read_kill_trooper();
    const bool dump_sprite_anims = ubw::config::read_dump_sprite_anims();
    const bool audit_029 = ubw::config::read_audit_029();
    const bool skip_movies = ubw::config::read_test_skip();
    const bool ok = ubw::init(travel, client_game_only, autoinput, auto_dropship, grant, kill_trooper,
                                        dump_sprite_anims, audit_029);
    ue::log::line(ok ? "worker: hooks installed" : "worker: FAILED, hooks not (fully) installed -- see above");
    if (ok && skip_movies) {
        // Only after install: it verified the exe is the build movieskip's global RVA belongs to.
        ubw::movieskip::start(reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr)));
    }
    return ok ? 0 : 1;
}

BOOL on_attach(HMODULE self) {
    const ue::config::LogTag tag = ue::config::read_log_tag();
    if (!ue::log::open_next_to_exe(tag.status == ue::config::TagStatus::Ok ? tag.text : nullptr)) {
        OutputDebugStringA("UBWCoop: cannot open the log file next to the exe\n");
    }
    ue::log::line("UBWCoop attach: proxy module %p", static_cast<void*>(self));
    if (tag.status == ue::config::TagStatus::Ok) {
        ue::log::line("attach: log tag \"%ls\"", tag.text);
    } else if (tag.status == ue::config::TagStatus::Invalid) {
        ue::log::line("WARN attach: UBWCOOP_LOG_TAG ignored (want 1-%zu chars of [A-Za-z0-9_-])",
                       ue::config::kMaxTagLen);
    }
    if (!DisableThreadLibraryCalls(self)) {
        // Non-fatal: only costs thread attach/detach notifications.
        ue::log::line("WARN attach: DisableThreadLibraryCalls failed, err=%lu", GetLastError());
    }
    if (!ubw::proxy::load_real_dll(self)) {
        // Without the real dwmapi the game cannot work; refuse to load so the failure is obvious.
        return FALSE;
    }
    ubw::galaxycfg::start(); // before PreInit loads steam_api64.dll; logs its own failure
    const HANDLE t = CreateThread(nullptr, 0, &worker, nullptr, 0, nullptr);
    if (t == nullptr) {
        ue::log::line("ERROR attach: CreateThread failed, err=%lu -- no hooks", GetLastError());
        return TRUE; // proxy still forwards; game runs unmodded
    }
    CloseHandle(t);
    return TRUE;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        return on_attach(inst);
    }
    if (reason == DLL_PROCESS_DETACH) {
        ue::log::line("UBWCoop detach");
        ubw::upnp::exit_cleanup(); // card 153: remove the port mapping this mod added (short timeouts)
        ubw::playlog_session_end(); // card 155: crash_line, no lock wait
        ubw::sync_detach();         // card 332: the teardown row, if one is open
    }
    return TRUE;
}
