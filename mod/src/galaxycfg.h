#pragma once

// Card 130 item 17: the game's steam_api64.dll is the GOG Galaxy Steam wrapper. At SteamAPI_Init it reads
// ./GalaxyConfig.json, which has no "auth_timeout" key, so SignInGalaxy waits the wrapper's default 15 s (it
// fails offline either way) before the engine goes on. Mod-side fix, game files untouched: steam_api64.dll's
// own KERNEL32 CreateFileW import is patched when the module loads, and a read open of a path ending in
// GalaxyConfig.json is sent to UBWCoop-GalaxyConfig.json next to it: the real JSON with "auth_timeout": 1
// added when absent.

namespace ubw::galaxycfg {

// Call once from DllMain attach (before steam_api64.dll loads during PreInit). Patches at once when the
// module is already loaded, else registers an ntdll DLL load notification. Logs and returns false when
// neither works; the game then runs with the 15 s wait.
bool start();

} // namespace ubw::galaxycfg
