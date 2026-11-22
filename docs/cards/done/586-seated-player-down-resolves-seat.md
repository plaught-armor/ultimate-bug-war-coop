# Card 586: a seated player's down or all-down resolves the seat, not the trooper

Found in c582b412a (b412 `33bcaf31e0733831`, c372-c3m1): the host crashed at exe+0x3AC6124 (AV reading
0xFFFFFFFFFFFFFFFF). Stack: c43_own_view (the g_add_local_offset camera call) < c43_transition < c43_enter_down <
c109_down < c63_step < net detour_tick. The scenario's `host down host` ran while the host sat in the Door Gunner seat:
"cmd: 32 down: slot 0 BP_MountableTurret_Gunner_C". `c43_roster` takes each controller's pawn, so a seated player's
roster pawn is the seat. c43_own_view read kCharacterCamera on the seat and passed garbage to AddLocalOffset.

## Part (b), b413: crash guard (done)

`c43_is_trooper(pawn, where)` (hooks_ubw.cpp) returns false and logs one "WARN card043: <side> <where> on <pawn>
(class C), not a trooper: refused; callers ..." row when the pawn is not a `g_player_character_class`.
c43_enter_down (after the card 079 mech check) and c43_own_view return early on false. The death.cpp:1560 join path
already handles a refused pawn. c43_all_down -> c43_transition -> c43_own_view is covered by the same guard.

## Part (a): deferred, static trace only

Find where the seat or turret stores its gunner's trooper (the seat's exposed `Target`, card 561 notes, and the
turret classes), then resolve it at the down entry points only: c109_down, c43_all_down and the join-after-all-down
path. Not in c43_roster: a global remap changes location and vehicle sync for every roster caller and needs a caller
audit first. No run.

## Part (a) trace (junior, 2026-11-21, with card 587)

- The same helper as card 587 resolves the trooper: a seat's exposed `Target`, a missile's Instigator (card 587 "Rule
  found").
- A real gap beyond the cmd path: the HandleDeathEvent down gate (death.cpp ~915) uses `c43_slot(r, object_index(pawn))`
  on the dying trooper. While its PC holds a seat or missile, the trooper is not in the roster, slot < 0, the gate
  returns false, and the native death runs (no down state).
- Other entry points: death.cpp:936/943 (enter_down / all_down), 1560 (c374_late_copies, `r.p[s].pawn`), 1580
  (c43_host_step all_down with `r.p[0].pawn`), 3092 (card 079 test), objectives.cpp:2705/2830/3163, c109_down.
- Not clean: downing a trooper whose PC views a seat or missile needs behavior decisions for the senior. Does the
  down end the seat or missile (unpossess back to the trooper first, then down)? Which view does c43_own_view use?
  Does revive repossess the trooper? Plus the card 587 role fact (the unpossessed trooper reads host own).

## How the seat and the missile hand control back (junior static, 2026-11-21)

Listings: `build/kis561/BP_MountableTurret_Gunner.txt`, `build/kis_txt/BP_MountableTurret.uasset.txt`,
`build/kis_txt/BP_GuidedMissile.uasset.txt`, `build/kis_txt/GA_TacSupp_GuidedMissile.uasset.txt`.

### Seat (`BP_MountableTurret_Gunner_C`, parent `BP_MountableTurret_C`, native `ASTMountableTurret`)

- The end is native: `ASTMountableTurret::LeaveTurret()` (exe+0x051CD920, already in `tools/symbols.txt` and detoured in
  `vehicles.cpp` `detour_leave_turret`). It runs `DetachCharacterFromTurret`, which calls `AController::Possess` with the
  turret's controller on the mounted character (card 029 dump 3402DC26). That is the repossess of the trooper.
- The Blueprint reacts to it. `ReceiveUnpossessed` (ubergraph 263) calls the parent's `ReceiveUnpossessed` (hit box
  collision off, overheat audio stop), then `Fly Away` (FlightStage 2, MoveTime 0; the stage 2 tick flies off and ends
  in `K2_DestroyActor` at 3321). The `BP_LeaveTurret` event (4600) teleports `Target` (the caller's trooper) to the
  first free spot around `PlayerLoc` (offsets of 200, 500, 1000 uu; `K2_SetActorLocation(PlayerLoc)` if none fits).
- The orbit timeout (2724, `MoveTime >= OrbitTimeoutTime`) calls an unresolved `imp72(self, false)` and `Fly Away`. It
  is not a possess call (the PC possess hash 553ce29c9def639f does not appear in the seat). I could not resolve imp72
  without the import table.
- The call for 586 (a): on the host, `ASTMountableTurret::LeaveTurret(seat)` for the downed trooper's seat. The host owns
  the seat's controller, so the detour's host branch runs the game's own leave. A client seat's leave already goes to
  the host this way (card 123 `c123_client_leave`).

### Missile (`BP_GuidedMissile_C`, spawned by `GA_TacSupp_GuidedMissile`)

- The return is the Blueprint function `Return to Character` (ubergraph 3760). It removes the significance location,
  sets `Possessed = false`, calls `GetPlayerController(self, 0).Possess(PlayerChar)`, puts the camera pitch limits back
  (2451/2328: ViewPitchMin -90, ViewPitchMax 90), turns stencils off and destroys `MarkerActor`. It takes no
  parameters and does not detonate. The missile keeps flying, and `Explode` later skips the return because
  `Possessed` is false (3665).
- `Explode` (3665) runs `Return to Character` when `Possessed`, then the detonation (NukeLauncherRound cue, damage,
  `K2_DestroyActor` at 3650). It fires on a hit and on the 10 s `Explode` timer (909).
- `PlayerDamaged` (3959) is bound to `PlayerChar`'s `HealthComponent.OnDamaged` (4842 to 4894). It also ends in
  `Return to Character` (4204). So damage to the trooper may already hand control back natively. The entry labels in
  this listing are uncertain (the bind sits under the `ReceiveUnpossessed` entry), so a run should check it.
- The call for 586 (a): `Return to Character` on the missile, by ProcessEvent with no parameters. It needs one
  dependency. `GetPlayerController(self, 0)` here and in the ability (GA 777/803 possess, 397 to 609 `PlayerChar` /
  `Instigator` from `GetPlayerCharacter(self, 0)`) must resolve to the missile owner's PC on the host. The card 099
  context remap must cover the `BP_GuidedMissile_C` context, or the host's own PC would possess the client's trooper.
  This is not checked here.

### Proposed path for 586 (a)

On the host, before the down state (the senior's decision 1): if the trooper's PC holds a `BP_MountableTurret_Gunner_C`
whose `Target` is the trooper, call `LeaveTurret(seat)`. If it holds a `BP_GuidedMissile_C` whose `PlayerChar` (or
Instigator) is the trooper, call `Return to Character`. After either call, the trooper is possessed again, so the roster
slot, `sprint_side` and the down gate see it normally. That also removes the role misread below for the down path.

## sprint_side reads an unpossessed client trooper as host own (from card 587)

- Fact: `APlayerController::OnUnPossess` calls `SetAutonomousProxy(false)` on the old pawn. While a client PC holds a
  seat or missile, its trooper on the host reads RemoteRole simulated (kActorRemoteRole 0x60), and `sprint_side`
  returns `kSideHostOwn` for it.
- Affected gates: the c181 round credit gate, the card 160 / 314 side gates, and death.cpp `HandleDeathEvent`
  (`sprint_side(pawn) == kSideHostOwn`), which takes the c43_all_down "host death" branch for the client's trooper.
- Fix options: (1) the 586 (a) path above repossesses before any down logic, so the death path never sees the
  unpossessed state. (2) For the other gates, `sprint_side` resolves an unpossessed trooper through the seat or
  missile that holds it (the card 587 rule: seat `Target`, missile Instigator) and returns that PC's side. Option 2 is
  the class fix. Option 1 alone leaves the c181 and card 160 / 314 gates wrong while the player is seated.

## Build b417 `12b126b1c9b25066` (junior, 2026-11-21): the three approved parts

Code: `vehicles.cpp` card 586 block, `hooks_internal.h` (C586Holds), `sprint_side` (hooks_ubw.cpp), `c99_copy_pc`
(player_c99.cpp), `c24_step` (fire.cpp), `HandleDeathEvent` (death.cpp), `c43_enter_down` and `c43_map_end`
(hooks_ubw.cpp).

- Hold table (host): `c586_frame(world)` runs each frame from `c24_step` and lists, for each connected client's PC
  (`c26_is_remote_pc`), the trooper its pawn holds. A seat is a `g_turret_class` pawn whose `Target` is a player
  character. A missile is a pawn with the `Return to Character` function whose `PlayerChar` is a player character
  (the ability sets `PlayerChar` and `Instigator` from the same `GetPlayerCharacter`). The host's own PC is not
  listed: its trooper stays host own. One row per new hold: `card586: host t=... <trooper> held by seat|missile
  <holder> of <pc>: read as server copy` (40 lines). Cleared at map end.
- `sprint_side`: an Authority actor with a non-autonomous remote role is checked against the table only while the
  table count is non-zero; a listed trooper returns `kSideServerCopy`. With nobody seated the cost is one load and
  compare.
- `c99_copy_pc`: a pawn with a null Controller resolves to its holding PC from the table.
- Down ends the hold first, for any PC (the host's own too):
  - `HandleDeathEvent` (host, after the god and client gates, before the roster): `c586_end_hold_of(trooper)`
    finds the PC whose seat or missile holds the trooper and ends that hold.
  - `c43_enter_down` (every other down entry: card 046 kill floor and copy out, card 079, card 374, testrun `down`)
    gets the roster pawn; a seat or missile there ends its hold and the trooper is downed. The slot and PC in the
    caller's roster are unchanged.
  - Seat: `c24_release_pawn` then `detour_leave_turret` (host: the game's LeaveTurret), the card 123 TurretLeave path.
  - Missile: `Return to Character` by ProcessEvent only when `Possessed` is still 1; 0 logs `none (Possessed already
    clear)`, which means the missile's own PlayerDamaged handed control back.
  - Row: `card586: host t=... <why>: <trooper> down while in seat|missile <holder>: LeaveTurret | Return to Character
    | none (...); PC <pc> pawn now <pawn>`, WARN when the PC's pawn is not the trooper afterwards.

### Callers whose answer changes, with predictions (only while a client's player sits in a seat or flies a missile)

| Caller | Before | After (prediction) |
|---|---|---|
| c181 round credit gate (`round_c181.cpp` 394, 434) and `detour_weapon_fire` side (180) | the client's trooper reads host own: no c181 row, no gate | trooper reads server copy; it fires nothing while seated, so no new gate rows; card587 diag rows show `server copy` |
| card 099 / 160 walk (`c99_copy_walk` 162, 170) and the card 314 other-player audio gates through it | an effect whose context is the held trooper is treated as the host's own (plays as local, 2D on the host) | it resolves to the client's PC (via `c99_copy_pc` and the table): remapped like any server copy effect; card099 rows name the trooper with the client PC |
| death.cpp `HandleDeathEvent` god gate (896, `g_c132_god_host_only`) | the held client trooper reads host own, so host-only god kept it alive | it reads server copy; GOD host-only no longer covers it (correct: it is the client's player) |
| death.cpp `HandleDeathEvent` all-down branch (939) | held trooper not in the roster (slot < 0): native death ran; with a roster hit it took "all down: host death" | the hold ends first, the trooper is the slot's pawn again: Downed when a teammate is Up, Out otherwise; never "host death" for a client |
| `c43_transition` own view (hooks_ubw.cpp 1909), `c43_skip_start_death` copy branch | own view could run on the client's trooper | server copy: no host own view; StartDeath skipped as a copy |
| fx_c136 1353, weapons.cpp 139/151/328, attributes.cpp 944/1204, interact.cpp 89, objectives.cpp 3649, script_c310 557, spawn_c169 244, insta_c361 255 | held client trooper counted as host own | counted as server copy; none of these run on an unpossessed seated trooper in the c588-style runs (no row change predicted) |

Open item, not chased: imp72 on the missile's orbit timeout (above).

### Proof needed (scenario to be set by the senior)

A down while seated and a down while flying a missile, for the client, with GOD off for the client (GOD host only
is no longer enough after this card, see the god row above). Predictions: one `card586: ... LeaveTurret; PC ... pawn
now <trooper>` row for the seat down, then card043 `-> Downed` for the client trooper (slot 1), no WARN card586, no
`all down: host death`. For the missile: the run decides between `Return to Character` and `none (Possessed already
clear)`; either way the PC pawn is the trooper after the row.

## Build b418 `ebd111fc55392f06` (b417 + test command fix)

`host down client` (testrun `c109_down`) found no slot for a client in a seat or a missile: `c29_server_copy_role`
needs the PC's pawn to be the trooper, and then it is the seat or the missile. b418 adds a fallback: the slot whose PC
holds a trooper in `g_c586` and sent the role. The roster pawn there (the seat or the missile) goes to
`c43_enter_down`, whose `c586_end_hold` hands the player back first. The checks after the down read the trooper.
Test only; the game path is unchanged from b417.

Proof scenario `tools/scenarios/c586-c3m1.txt` (from c372-c3m1): a missile down, then a seat down, each followed by
a host revive, the pawn, view and walk checks. The mod has no apply-real-damage command, so both downs are testrun
downs: `HandleDeathEvent` -> `c586_end_hold_of` is not proved, and the missile row answers the PlayerDamaged question
for a testrun down only.

## Run c586b418a (run 1 of 3) and build b419 `d708f6fa42b54a48`

Missile down passed: the hold row, then `enter down: ... down while in missile BP_GuidedMissile_C: Return to
Character; PC ... pawn now BP_PlayerCharacterMI_C`, no WARN; revive after a 3006 ms hold, client pawn and walk checks
done. Seat down: the hold row and `... seat BP_MountableTurret_Gunner_C: LeaveTurret; ... pawn now
BP_PlayerCharacterMI_C`, no WARN, host Downed; then the client crashed (`EXCEPTION_ACCESS_VIOLATION` in `c588_row` <-
`c588_step` <- `ubw_post_tick`), so the seat revive was not proved. 0 `all down: host death`, 0 WARN card586.

Three fixes in b419:

- Crash: the seat was destroyed in the same net frame that set the client's pawn back to the trooper, before
  post-tick read the cached seat. `c588_forget` in `detour_c98_destroy` logs the seat's last window and clears it.
- WARN card209 (host, 2): `c43_enter_down` sent the first Down frame with the roster taken before `c586_end_hold`, so
  the slot's pawn was still the seat or missile, slot -1, frame dropped; the client went Downed about 1 s late on the
  bleedout resend. The transition now uses a copy of the roster with the trooper in that slot.
- WARN card502 (host, 2, test only): `c109_down` asked `c29_server_copy_role` first, which finds no trooper for a held
  client. It now tries the `g_c586` hold first.

## Run c586b419a (run 2 of 3): fixed

Accepted by the senior 2026-11-21; run 3 skipped. Both downs (missile and seat) end the hold and down the trooper
with no WARN; the client goes Downed 28 ms (missile) and 14 ms (seat) after the host, the revive arrives 34 and 33 ms
after the host's, and after each revive the client's pawn is its own trooper and it walks (goto done). The host stayed
alive and unseated: 0 `all down: host death`, 0 WARN card586/209/502, card132 0 mismatches in 3 rounds, no crash.
The seat down's `possess: ClientRestart` row was not logged (the possession diag cap was hit at its SetPawn); the
hand-back is shown by SetPawn moving input to the trooper, the card319 shot origin BP_PlayerCharacterMI_C at downed,
revived and walked, and `pawn=client-own`. b419 is on lane 1.

Open items (kept here, no new cards):

1. `HandleDeathEvent` -> `c586_end_hold_of` is not proved: the mod has no real-damage command, so both downs were
   testrun downs. The first real-damage down of a seated or missile-flying client in play shows it in the card586
   row (and answers the PlayerDamaged question for the missile).
2. `imp72(self, false)` on the missile's orbit timeout, unresolved (above).
