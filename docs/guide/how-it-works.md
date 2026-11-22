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
- **Performance.** `ubwperf.dll` holds the changes made to cut CPU work. See [Performance module](#performance-module).
- **Port mapping.** `mod/src/upnp.cpp` is a small SSDP and SOAP client that asks the router for the UDP mapping
  and the public address, on a worker thread. Without UPnP, it reads the public address from `api.ipify.org`
  over HTTPS. It is skipped in test runs.

## Performance module

`ubwperf.dll` reduces the game's CPU work, mainly to leave the game thread more headroom in large fights. It does
not change graphics settings. Its source is `mod/src/perf/`.

`dwmapi.dll` loads it at start-up, both when hosting and when joining. If the file is missing or has another
interface version, the mod logs a warning and runs without these changes.

| Change | Where | What can differ from the base game | Off switch |
|---|---|---|---|
| Save ID lookups use the component cache | Both | Meant to be identical. | `UBWCOOP_C531_OFF` |
| Component lookups are cached until the actor's components change | Both | Meant to be identical. Sampled hits are checked against the original. | `UBWCOOP_C536_OFF` |
| A repeated target condition check in the same frame reuses its result | Both | Meant to be identical. Sampled hits are checked against the original. | `UBWCOOP_C540_OFF` |
| The physics tree pads moving objects by 30 cm instead of 5 cm, so it is rebuilt less | Both | Queries test a few more candidates, then apply the same exact test. | `UBWCOOP_C544_OFF` |
| The significance manager runs 30 times a second instead of every frame | Both | Enemy update rate and detail level can change up to about 33 ms later. | `UBWCOOP_C568_OFF` |
| Idle engine worker threads spin 8 rounds before sleeping instead of 53 | Both | An idle worker can wake slightly later. | `UBWCOOP_C576_OFF` |
| Enemy capsules stop colliding with their own collision type when they move | Client | Enemies can overlap on the client while they follow the host's positions. They still collide with players and the world. | `UBWCOOP_C542_OFF` |
| Runtime navmesh rebuilds are skipped | Client | The client's navmesh stays as it was loaded. Enemy AI runs on the host. | `UBWCOOP_C577_OFF` |
| From 30 s after a map's first rebuild, navmeshes that no agent has used are not rebuilt | Host | The skipped area is rebuilt when such a navmesh is first used. | `UBWCOOP_C593_HOLD_OFF` |

Set a switch to `1` in the environment to turn that change off. `UBWCOOP_C536_OFF` also makes save ID lookups run the
original, and `UBWCOOP_C577_OFF` also turns off the host's navmesh hold.

The module also contains counters that write timing and call counts to the mod log. They change nothing in the game.

The same binary has a second load path as `winhttp.dll`, for use without co-op. It was run once during development
(b425). The launcher does not install it that way and it is not a supported way to play.

`docs/SYNC_AUDIT.md` reviews each kind of synced state and how it is carried, as of the date on each entry.
