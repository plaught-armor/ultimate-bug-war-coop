# 306 Client crashes (fatal error) when it swaps one tactical support pickup for another

Playtest 2026-11-13 (user plays as client, FED_C3M1): "received fatal error".

## Trace (client log `UBWCoop-menu-20261113-184603.log`, lines 129253 to 129274)

| Client t (ms) | Row |
|---|---|
| 258795270 | client equips `Config_TacSupp_WhitePhosphorus_C` from a pickup (caller exe+0x51D2A9C) |
| 259408308 | interact press on `BP_TacSuppPickup_SonicBlaster_C_2147099287` (runtime pickup, `UseName not sent (not a placed relayed class)`) |
| 259408308 | `card032: client EquipTacticalSupportAbility ... Config_TacSupp_SonicBlaster_C`, caller exe+0x51D2A9C |
| 259408308 | `card105: client blocked runtime pickup spawn of BP_TacSuppPickup_WhitePhosphorus_C at (-23787, -11588, 692)`: the game drops the old support as a pickup and the mod returns null |
| 259408308 | `CRASH 1/4: EXCEPTION_ACCESS_VIOLATION at exe+0x37E14E8, reading 0x5C`, rbx 0, rcx 0; stack exe+0x37E14E8 < exe+0x51D2E8D < exe+0x51D42C1 < exe+0x51D061D < exe+0x4A20AA1 < exe+0x4A1FE92 < ubwcoop+0xE0D13 |
| 259408312 | `CRASH ue: ReportCrash code 0xC0000005`, `Fatal error!` |

It is the only `blocked runtime pickup spawn` row of the log, and it is the first swap of one support for another.

## Cause (static)

`c105_client_blocks` (`mod/src/ubw/vehicles.cpp:1315`) refuses a client's spawn of a runtime pickup and the spawn
detour returns null. The tactical support pickup's swap code (caller exe+0x51D2E8D, in the function that called
`EquipTacticalSupportAbility` at exe+0x51D2A9C) uses the spawned old-support pickup with no null check and reads
0x5C of null. This is the fault class of card 132's `SpawnWeaponPickup` crash (hb39, NukeLauncher drop), which was
fixed for that one caller by refusing before the spawn. Any other game caller that spawns a relayed class on the
client and uses the result is still open.

## Step 1 (static, junior, no run)

1. Disassemble exe+0x51D2E00 to exe+0x51D2EA0 and exe+0x37E14E8: name the function, what it does with the spawned
   pickup (which fields it writes), and whether the host's side of the same swap spawns and relays the dropped
   pickup (the Use is not sent: "not a placed relayed class", so say how the host learns of the swap at all, and
   whether the host's copy of the picked-up SonicBlaster pickup is consumed).
2. List every native and Blueprint path that spawns an `AADPickupBase` class or another card 105 relayed class
   and uses the return value: weapon drop (card 132, done), tactical support swap (this), grenade and ammo drops,
   armour and health drops from crates and bodies, resupply rack, strike actors (card 104), mech pod (card 075).
   Mark each: null-safe or not.

Write the answers into this card before coding.

## Fix

A client's game code never receives null from a spawn the mod refuses. One rule in the spawn detour, not one
guard per caller: the spawn runs, and the client's own copy is made inert in the same call (hidden, no collision,
no tick, pickup disabled, not counted by digest or journal) and destroyed at the end of the frame, after the
caller has finished with it. The host's copy arrives by the WorldOp 'P' row as today. The card 132 pre-spawn
refusal for `SpawnWeaponPickup` moves to the same rule if step 1 shows it equivalent; otherwise it stays.

The swap itself is synced: the host runs the client's support swap on the server copy (Use relayed for runtime
tactical support pickups), drops the old support as its own pickup, relays it, and removes the picked-up one for
both.

- Rows: `card306: client t=<ms> inert own spawn of <class> at (...) for caller exe+0x<rva>, destroyed at frame
  end`; `card306: host t=<ms> support swap for <player>: <old> dropped as <pickup guid>, <new> equipped`; per-map
  tally `card306: inert spawns <n>, null returned to game callers <n>` (target 0 for the second).

## Sibling sweep (same class: the mod returns null or skips a call whose result the game uses)

| Item | Check |
|---|---|
| Every spawner of step 1 item 2 | no null to a game caller on the client |
| `c75_blocks_spawn` (mech pod) and `c104` strike spawns | same rule |
| Other detours that return null or 0 in place of an object (card 173 `SpawnSound2D` null, card 254 skipped `Pickup_Implementation`, card 100 and 258 skipped objective calls) | each caller checked for use of the result |
| Swap of every tactical support for every other, as client and as host | no crash; old one lies on the ground for both; counts match |
| Weapon swap with full inventory, grenade type swap | same |
| Client crash handling | the existing `CRASH` rows stay; a crash in a mod-refused path names the refusing card |

## Predictions (one run, after the playtest)

- Client holds White Phosphorus and picks up a Sonic Blaster pickup, then swaps back: no `CRASH` row, two
  `card306` inert-spawn rows, two host swap rows, the dropped pickup visible to both and usable by the host;
  tally `null returned to game callers 0`.
- Host does the same: client sees the dropped pickups.
- Digest round after: 0 mismatches.

## Step 1 answers (junior, static plus host log `client/UBWCoop-menu-20261113-182510.log`, 2026-11-13)

1. **Caller.** exe+0x51D2E8D is in `ASTTacticalSupportPickup::PickupTacticalSupport(ASTPlayerCharacter*)` (0x051D2970), called
   from `Pickup_Implementation` (0x051D4220) after `EquipTacticalSupportAbility` (exe+0x51D2A9C). It builds spawn params with
   `bDeferConstruction` (`or byte [rbp+0x42], 4` at exe+0x51D2E00), calls `UWorld::SpawnActor(UClass*, FTransform*, params)`
   (exe+0x51D2E28, return exe+0x51D2E2D), casts the result to the pickup class (null on failure, `mov rbx, r15`), then calls
   `AActor::FinishSpawning` (0x037E14C0, exe+0x51D2E88) on rbx with no null check. FinishSpawning+0x28 reads 0x5C of the
   null actor: the crash. The game itself only writes the transform; the caller needs a real deferred actor.
   **Host side.** The host learns of a client swap from the relayed Use (card 054): for a runtime pickup the client sends no
   UseName and the host takes its copy pawn's own trace target (host log 84926183: `copy target
   BP_TacSuppPickup_WhitePhosphorus_C_2147140423 ... no client name`, then host `EquipTacticalSupportAbility ...
   WhitePhosphorus ... caller exe+0x51D2A9C`, `relayed Use #28`). So the host runs the same PickupTacticalSupport on its
   copy, its old-support drop is a host runtime pickup relayed by card 107, and its Pickup_Implementation consumes the
   host's pickup ('D' to the client). At the crash (host t 85539072) the host log has no Use, UseName or card050 row: the
   client died in the frame of its press, before its Use frame left. The SonicBlaster pickup stayed on the host (card104
   settle row 85546468).
2. **Spawners that use the result** (callers of the core `UWorld::SpawnActor` 0x03DCA380 in game code, plus the paths
   named in the card):

| Path | Spawn route | Uses result | Null-safe |
|---|---|---|---|
| Tactical support swap (`PickupTacticalSupport` exe+0x51D2E28) | native core spawn, deferred | FinishSpawning, no check | **no** (this crash); fixed by the inert rule |
| Weapon drop (`USTWeaponUtilityLibrary::SpawnWeaponPickup` exe+0x52515BB) | native core spawn | writes +0x4D0 | no; card 132 refuses before the spawn (`detour_c132_spawn_weapon_pickup`), callers take null from SpawnWeaponPickup; stays |
| Dead or downed pawn weapon drop (vector `UWorld::SpawnActor` 0x03DCB440, core call return exe+0x3DCB50E) | native | engine wrapper only; no ST/AD game function calls the wrapper directly | inert rule covers it |
| Grenade, ammo, armour, health drops from crates and bodies; resupply rack | Blueprint `BeginDeferredActorSpawnFromClass` node (thunk exe+0x3C78583) | node result, `FinishSpawningActor` and later nodes test None | yes (Blueprint keeps null) |
| Strike actors `BP_TacSupp_*` (card 104) | Blueprint node | same | yes |
| Mech pod (card 075, `c75_blocks_spawn`) | Blueprint node | same | yes |
| Spawners, spawn points, objective markers, damage zones, GameMode, save Load (`PerformSpawnAtPoint`, `CreateSpawnPoint`, `CreateASTLevelObjectiveMarker`, `TrySpawnDamageZone`, `USTSaveComponent::Load`) | native core spawn | yes | not refused classes (no card 105 relayed class), never null from the mod |

## Fix (coded, uncompiled at writing)

- `detour_c75_world_spawn` (`mod/src/ubw/vehicles.cpp`): a card 105/107 client-blocked spawn from a native caller runs
  (`c306_inert_spawn`): hidden (`SetActorHiddenInGame` original), collision off (`SetActorEnableCollision` original), tick
  off (vtable +0x4A8); no card 098 note, no relay, not in the copy table, so no digest or journal entry. A Blueprint
  thunk caller still gets null. `c306_step` (first in `ubw_post_tick`, after the engine Tick) destroys each with the
  original `UWorld::DestroyActor`. Pickup is not disabled by call (DisablePickup before FinishSpawning would touch
  components that do not exist yet); collision off keeps the interaction trace and overlaps off it.
- Rows: `card306: client t=<ms> inert own spawn of <class> at (...) for caller exe+0x<rva>, destroyed at frame end`;
  `WARN card306: ... failed: null to the game caller`; tally at LoadMap enter `card306: inert spawns <n>, null returned
  to game callers <n>` (native callers only).
- Swap relay: the client's Use on a relayed runtime copy now sends UseName `#<host id> x y z`
  (`c50_send_use_name`, `c104_copy_host_id`); the host resolves it with `c104_relayed_by_id` in `c50_pick_target`, so it
  runs the swap on the client's target, not its copy pawn's trace. Miss: `WARN card306: host ... not a live relayed
  actor; the copy's own target ... is used`.
- Host row: `card306: host t=<ms> support swap for <player>: <old> dropped as <host id>, <new> equipped` (spawn at
  exe+0x51D2E2D, equip recorded in `detour_equip_tac_supp`).

Disassembly re-checked before the build (2026-11-13): exe+0x51D2E28 `call UWorld::SpawnActor`, exe+0x51D2E30 `test
rax,rax` / `je` to `mov rbx,r15` (null kept), exe+0x51D2E85 `mov rcx,rbx`, exe+0x51D2E88 `call 0x1437E14C0`
(`AActor::FinishSpawning`); FinishSpawning +0x28 (exe+0x37E14E8) `movzx eax, byte [rcx+0x5C]` is the faulting read
(rcx 0). The caller needs a real deferred actor, so the inert-spawn rule fits. Known limit: FinishSpawning runs the
construction script and BeginPlay on the inert actor; the actor-level hidden and collision flags hold through it,
and the actor is destroyed at the end of the frame.

## Build b7 change (senior review of b6)

- The new `c306_make_inert(actor)` (vehicles.cpp) hides the actor, turns its collision off and disables its tick.
  `c306_inert_spawn` calls it on the spawn result.
- `c306_step` calls it again on each queued actor just before `g_orig_c98_destroy`. The caller's `FinishSpawning`
  runs after the spawn returns, and its BeginPlay can turn tick and collision back on.
- Comment fixed: `c306_step` is called from `ubw_post_tick` (`hooks_ubw.cpp`, second step after `c140_pace_step`
  and `c138_step`). That is after `UEngine::Tick` has returned in the frame of the spawn, so it is after the caller's
  FinishSpawning and BeginPlay.
- Remaining gap: between BeginPlay and `c306_step`, for the rest of that engine Tick, the hidden actor can have
  collision or tick on again. Closing it would need a FinishSpawning hook; it is not coded.

## Closed

Closed 2026-11-17 on b180 `4c41c9c8455f774f` (fix from b7, proof owed until now). c423b180a/b/c on FED_C2M1: the
client holds a bombing run and takes white phosphorus over it (the playtest crash case). Client `card306: client
inert own spawn of BP_TacSuppPickup_BombingRun_C ... for caller exe+0x51D2E2D, destroyed at frame end`; host `card306:
host support swap for BP_PlayerCharacterMI_C: BP_TacSuppPickup_BombingRun_C dropped as 244901,
Config_TacSupp_WhitePhosphorus_C equipped`; client tally `inert spawns 1, null returned to game callers 0`; 0 WARN
card306, 0 EXCEPTION in three runs. Client shot c306-after-swap: the dropped bombing run pickup with its prompt, HUD
slot White Phosphorus. The host's view of the drop is by the log row only.
Playtest watch: an EXCEPTION at exe+0x37E14E8 or a `WARN card306` row reopens.
