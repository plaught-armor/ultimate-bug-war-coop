# Technical overview

[Back to the README](../../README.md)

Paths on this page are relative to the repository root.

## How it works

The game is built on a custom branch of Unreal Engine 5. Its native code replicates nothing: none of its
properties or RPC functions are marked for networking, so stock Unreal multiplayer cannot carry the campaign.
The mod supplies the multiplayer itself.

- **Loading.** The mod is `dwmapi.dll`, placed next to the game executable. Windows loads it instead of the system
  `dwmapi.dll`; it forwards every real export to the system DLL and then installs its hooks. Its only load-time
  import is `kernel32`.
- **Hooks.** Game functions are found by their addresses (RVAs) in the 1.2.0 executable, taken from the game's
  Shipping PDB and checked in as `mod/src/offsets.generated.h`. [MinHook](https://github.com/TsudaKageyu/minhook)
  detours them.
- **Session.** The host runs the mission as an Unreal listen server and is authoritative: enemy AI, objectives,
  damage and pickups are decided there.
- **Mod channel.** Everything the engine does not replicate travels on the mod's own channel: compact binary
  frames packed into strings and carried by two engine RPCs that are reliable and ordered (`ServerChangeName`
  from client to host, `ClientMessage` from host to client). See `mod/src/ue/chan.h` for the frame format.
- **Performance.** `ubwperf.dll` holds changes that only cut CPU work. `dwmapi.dll` loads it from the same folder.
- **Port mapping.** `mod/src/upnp.cpp` is a small SSDP and SOAP client that asks the router for the UDP mapping
  and the public address, on a worker thread. Without UPnP, it reads the public address from `api.ipify.org`
  over HTTPS. It is skipped in test runs.

`docs/SYNC_AUDIT.md` reviews each kind of synced state and how it is carried, as of the date on each entry.
