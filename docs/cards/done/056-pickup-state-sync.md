# Card 056: Pickups taken on the host disappear on the client

Status: sent 2026-11-05 05:44 to junior ultimate-bug-war-coop-bb, after card 073 was accepted. Line numbers refreshed 2026-11-05 04:50. Card 071 ammo pickups and the grenade pickups taken by the client's copy are covered too: the host calls `DisablePickup` on them (`logs/c72c-host.log`).

Read `docs/STATE.md` and the card 050 Report and Senior review. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

Placed pickups have `bReplicates` 0 (card 050 audit). The host takes a pickup for itself or, through the card 050 relay, for the client's server copy. The host then calls `AADPickupBase::DisablePickup` 0x04C61170 (hooked by card 050 as `detour_c50_disable_pickup`) on its own copy only. The client's copy stays, so the client sees a weapon, ammo or health it cannot take.

## Static facts (senior, 2026-11-04)

- Card 050 already has the pieces:
  - the placed-actor name text (`c50_name`, `c50_placed`, RF_WasLoaded);
  - a name lookup over placed actors of given classes (`c50_find_placed`, which collects with `c23_collect`);
  - text ops (`chan_text_frame`, `chan_text`, `kChanArgsText`);
  - the per-copy first-Pong hook (`c50_pong`).
- On both machines, `DisablePickup` also runs at map load for `BP_DevLogPickup_C`, called from the level's own setup. Those calls happen on both machines anyway, and sending them is harmless, but the journal must not grow from them without a bound.
- `AADPickupBase` has `RespawnPickup` and `SetRespawningEnabled`, but no FED_C1M1 sublevel (`FED_C1M1`, `_Scripting`, `_Art`, `_Audio`, `_Blockout`, `_Lighting`) names either one; the only "respawn" string is `RespawnPoint`. No `PickupBack` op is needed for this card.
- Code to reuse, exact (hooks.cpp):
  - `void* c50_find_placed(const void* world, const char* name)` (hooks.cpp:21885 on 2026-11-05) loops a fixed `cls[kC50RelayClasses]` array (weapon, item, tactical pickup classes) through `c23_collect` and matches `c50_placed` plus `c50_name`. Change it to take `(const void* world, const StaticClassFn* cls, int32_t n, const char* name)`; the card 050 caller passes its existing array, and this card passes `{AADPickupBase}` (one class covers ammo and health pickups too). The symbol exists: `offsets::AADPickupBase_StaticClass` 0x04C5F520 (offsets.generated.h:791); resolve it like `g_c50_weapon_pickup_class` (hooks.cpp:21304).
  - `detour_c50_disable_pickup` (hooks.cpp:22056) is two lines: a log and the original. Add the send after the original.
  - `c50_pong(pc)` (hooks.cpp:21535) is the first-Pong hook to extend with the journal replay.
- The FED_C1M1 level script binds the pickups' `OnPickup` (`Bind Health Pickup`, `Bind Tac Pickup`, `PickedUpHealthPickup`) to drive tutorial objectives. `DisablePickup` on the client does not fire `OnPickup`, so the client's level script sees nothing. That is wanted: the host's objective sync (card 049) carries the result.

## Do

1. **Op.** Add `ChanOp::PickupGone` before `Count` in the `ChanOp` enum (hooks.cpp:6408); text is the placed actor's name, host to client, with `kChanArgsText`.
2. **Host.**
   - In `detour_c50_disable_pickup`, after the original runs, act only on a listen server, with at least one client, for a placed pickup (`c50_placed`).
   - Add the name to a per-map journal: a fixed array of names with a cap of 128. When the cap is hit, log it and drop the name.
   - Send `PickupGone` to every server-copy client that has sent a Pong.
   - On a copy's first Pong of a map, send the whole journal, one frame per name, so a joining or rejoining client is filled.
3. **Client.**
   - Find the placed `AADPickupBase` with that name with the generalized `c50_find_placed` (Static facts).
   - Call the original `DisablePickup` on it. A miss is logged and skipped.
   - A name already disabled on the client is a no-op, and is logged once.
4. **Runs.** Script both runs with the card 063/064 runner (`SCENARIO=tools/scenarios/c56-*.txt`, `goto actor` to each pickup, `key` for Use). Use no teleport and no xdotool, and put the scenario files in the Report.
   - `card056x`: two players, FED_C1M1. The host picks up a weapon. The client takes another one through the card 050 relay. Then the client leaves and rejoins (`run_pair.sh` reconnect, or the card 052 panel). Take shots of both cases on the client before and after; both must be gone after the rejoin too.
   - `card056s`: solo. No frames are sent, and the pickup behaviour is unchanged.

## Done when

- A pickup taken on the host, whether by the host's player or by the relay for the client, disappears on the client within a second, and stays gone across a rejoin.
- rc 0 on all runs, no new dump, the build is clean with `/W4 /WX`, and every function is 60 lines or fewer.

## Report

Junior, 2026-11-05. Done: a pickup taken on the host is disabled on the client within about 10 ms, and stays disabled across a client rejoin.

### What changed

- `ChanOp::PickupGone` is a new text op, host to client. The client's `detour_server_change_name` leaves it out of `c26_host_receive`. `detour_client_message` passes it to `c56_receive` while `g_client_session` is set.
- **Host (`c56_host_disabled`).** It runs from `detour_c50_disable_pickup`, after the original call, and acts only on a listen server and only on a placed pickup (`c50_placed`).
  - The name (`c50_name`, with its number suffix) goes into a per-map journal of 128 names. A name already in the journal is not sent again. When the journal is full, the name is dropped and one WARN is logged.
  - The name is sent to each server-copy PC whose copy row has `ponged` set. The log line is `card056: host t=.. PickupGone <name> (<class>): sent to N of M clients`, with a WARN prefix when N < M.
- **Replay (`c56_replay`).** It is called from `c50_pong` on a copy's first Pong of the map, and sends the whole journal: `PickupGone journal replay to <pc> (first Pong of the map): N of M names`.
- **Client (`c56_receive`).**
  - A bad frame gives a WARN.
  - A name the client has already disabled is skipped, and logged once per name.
  - Otherwise the pickup is found with `c50_find_placed(world, {&g_c56_pickup_class}, 1, name)`, where `AADPickupBase_StaticClass` is resolved into `g_c56_pickup_class`. A miss gives a WARN and is skipped. A hit calls `g_orig_c50_disable_pickup` and logs `... disabled`.
  - The client's own disables, such as the map-load DevLog pickups, enter its journal through `c56_client_disabled`, so a later frame for one of them is a no-op.
- `c56_arm_map` (called first in `c50_arm_map`) logs the map totals and resets the journal.
- Tests: the runner has a new `<role> disconnect` command: `cmdfile::Op::Disconnect`, and the console command `disconnect` through `g_pc_console_command`. The card 045 follow then reconnects the client. `tools/drive.py` lists it in COMMANDS and in its docstring.
- Build `/W4 /WX` clean. fnlen: 1337 functions, 0 over 60.

### Deviations

1. `c50_find_placed` now takes an array of class pointers. The new wrapper `c50_find_relayable` builds that array from `kC54Relays`, and `c50_pick_target` calls it. Card 056 passes `{&g_c56_pickup_class}` (`AADPickupBase`), so ammo and grenade pickups are covered as well as weapons.
2. The journal is kept on any listen server, even with no client connected. Pickups taken while the client was away are therefore replayed on rejoin. In solo the same lines read `sent to 0 of 0 clients`, and no frame is built.
3. The host's map-load disables of the three `BP_DevLogPickup_C` journal entries run before `c50_arm_map`, so the arm reset drops them. That is harmless, because the client disables them itself at map load.
4. When the client leaves the session, its local map's arm line still reads `card056: host map totals`, because `side()` is no longer "client". This only affects logging.
5. The client pickup is a walk-over, not E. The client walks onto the frag grenade pickup, and its server copy takes it on the host. FED_C1M1's other placed weapon pickup (`BP_WeaponPickup_MoritaMk1_C_1`) is the TacSupport_SO2 objective actor. The walk also took the incendiary and two small Morita ammo pickups next to it, and all of them synced.

### Runs

- **card056x** (`DEPLOY=1`, two players, FED_C1M1, `tools/scenarios/c56-rejoin.txt`): scenario passed; host rc 0, client rc 0; FATAL/ERROR 0, WARN 0 on both; no new dumps.
  - Frag: host send at t=28645601, client disabled at t=28645609 (8 ms).
  - Morita, taken by the host with E: send at t=28667291, client disabled at t=28667301 (10 ms).
  - The client `disconnect` led to `card045: client ... reconnected (1): try 1, 5769 ms after the drop`. The host logged `journal replay ... 5 of 5 names`. The client logged all 5 as `disabled` in the same tick (t=28677954): Frag_C_0, Incendiary_C_0, AmmoPickup_MoritaSmall_C_0 and _C_1, and WeaponPickup_MoritaMk1_C_0. The client had 0 misses.
  - Client shots, all in `prefix/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/Screenshots/Windows/`:
    - `client-card056x-before-morita.png`: the "COLLECT 1.12m" prompt is on the Morita.
    - `client-card056x-after-morita.png`: no prompt, and the objective has moved on to "Enter the target range to begin".
    - `client-card056x-rejoin-morita.png`: the crate is open with no weapon and no prompt.
    - `before-frag`, `after-frag` and `rejoin-frag` are in the same directory. There is also the host's `host-card056x-after-morita.png`.
- **card056s** (`SOLO=1`, `tools/scenarios/c56-solo.txt`): scenario passed; host rc 0; FATAL/ERROR 0; no new dumps.
  - The one WARN is the usual solo `autodropship: 1 PlayerController(s)` line, which also appears in c70f, c71solo and c72f.
  - The Morita E gives the weapon (`card021: host GiveWeaponByClass(BP_WeaponDefinition_MoritaMk1_C ...) = 1 ... (host-own)`), and the pickup is disabled as before.
  - Every `card056` line reads `sent to 0 of 0 clients`, so no frame was sent.

### Limits

- Pickups only go away. A pickup that respawns or is re-enabled is not synced, because there is no re-enable op.
- A name that is not in the client's world is logged and skipped, and is not retried. It did not happen here (0 misses).

### Scenario files

`tools/scenarios/c56-rejoin.txt`:

```
# Card 056: pickups taken on the host disappear on the client, and stay gone across a rejoin.
# The client walks over the frag grenade pickup (taken on the host by its server copy); the host takes the Morita
# Mk1 weapon pickup with E. Client shots before and after each, then the client disconnects (console "disconnect";
# card 045's follow reconnects it), gets the host's PickupGone journal at its first Pong, and is shot at both spots.
# (FED_C1M1's other placed weapon pickup, BP_WeaponPickup_MoritaMk1_C_1, is the TacSupport_SO2 objective's actor.)
# Run: SCENARIO=tools/scenarios/c56-rejoin.txt HOST_ENV="UBWCOOP_AUTODROPSHIP=1" run65.sh <tag> 520
host wait state pawn=host-own timeout=240
client wait state pawn=client-own timeout=120
host ui continue
sleep 3
client goto actor BP_GrenadeAmmoPickup_Frag_C 400
client look actor BP_GrenadeAmmoPickup_Frag_C
sleep 1
client shot before-frag
client goto actor BP_GrenadeAmmoPickup_Frag_C 80
host wait log "card056: host t=\d+ PickupGone BP_GrenadeAmmoPickup_Frag_C\S* .*sent to 1 of 1" timeout=15
client wait log "card056: client t=\d+ PickupGone BP_GrenadeAmmoPickup_Frag_C\S* .*disabled" timeout=2
client goto actor BP_WeaponPickup_MoritaMk1_C_0 500
client look actor BP_GrenadeAmmoPickup_Frag_C
sleep 1
client shot after-frag
client look actor BP_WeaponPickup_MoritaMk1_C_0
sleep 1
client shot before-morita
host goto actor BP_WeaponPickup_MoritaMk1_C_0 120
host look actor BP_WeaponPickup_MoritaMk1_C_0
host key E
host wait log "card056: host t=\d+ PickupGone BP_WeaponPickup_MoritaMk1_C_0 .*sent to 1 of 1" timeout=15
client wait log "card056: client t=\d+ PickupGone BP_WeaponPickup_MoritaMk1_C_0 .*disabled" timeout=2
sleep 1
client shot after-morita
host shot after-morita
# Rejoin.
client disconnect
client wait log "card045: client t=\d+ reconnected" timeout=120
client wait state pawn=client-own timeout=120
host wait log "card056: host t=\d+ PickupGone journal replay to .*: [1-9]\d* of" timeout=60
client wait log "card056: client t=\d+ PickupGone BP_WeaponPickup_MoritaMk1_C_0 .*disabled" count=2 timeout=10
client wait log "card056: client t=\d+ PickupGone BP_GrenadeAmmoPickup_Frag_C\S* .*disabled" count=2 timeout=10
client goto actor BP_WeaponPickup_MoritaMk1_C_0 500
client look actor BP_WeaponPickup_MoritaMk1_C_0
sleep 1
client shot rejoin-morita
client look actor BP_GrenadeAmmoPickup_Frag_C
sleep 1
client shot rejoin-frag
sleep 2
client quit
sleep 2
host quit
```

`tools/scenarios/c56-solo.txt`:

```
# Card 056 solo (SOLO=1): the host takes the Morita Mk1 pickup; no PickupGone frame is sent and the pickup behaves
# as before (the weapon is given, the pickup is disabled).
# Run: SCENARIO=tools/scenarios/c56-solo.txt SOLO=1 HOST_ENV="UBWCOOP_AUTODROPSHIP=1" run65.sh <tag> 300
host wait state pawn=host-own timeout=240
host ui continue
sleep 3
host goto actor BP_WeaponPickup_MoritaMk1_C_0 120
host look actor BP_WeaponPickup_MoritaMk1_C_0
host key E
host wait log "card021: host GiveWeaponByClass\(BP_WeaponDefinition_MoritaMk1_C.*host-own" timeout=15
host wait log "card050: host AADPickupBase::DisablePickup on BP_WeaponPickup_MoritaMk1_C" timeout=5
sleep 2
host shot after-morita
host quit
```

## Senior review (2026-11-05 05:58): accepted

- Checked card056x and card056s myself.
  - Both drives end with `scenario passed`.
  - logsum gives ERROR/FATAL 0. The only WARN is the known solo `autodropship` line.
- Both runs started after the DLL deploy at 05:47:11.
- No new dump: the newest crash folder is still the 05:22 one from card 073 run c73g, which ran a build from before that card's fix.
- fnlen: 1337 functions, 0 over 60 lines.
- Rejoin, in card056x:
  - The client logged `card045: ... reconnected (1): try 1, 5769 ms after the drop`.
  - The host logged `PickupGone journal replay ...: 5 of 5 names`.
  - The client has 10 `PickupGone ... disabled` lines: 5 live and 5 replayed.
  - In `client-card056x-rejoin-morita`, the crate is open with no weapon and no prompt.
- All five deviations are accepted. Two of them are improvements:
  - the generalized `c50_find_placed`;
  - keeping the journal while no client is connected.
- The new runner command `disconnect` is a useful tool for later rejoin cards.
