# 276 Host copy loses a fire press relayed in the tick of the previous release; client hits dropped

Found in run A of card 243 (FED_C3M2, `logs/c243-c3m2-a1-{host,client}.log`, DLL `5e9069094b412c66`): 32 rows
`WARN card157 ... found no round in 1500 ms: dropped` with the card 274 fix in. Play-visible: a client's shots do
no damage for seconds. Card 274's relay and magazine rows work; this is the cause that is left.

## Mechanism (run A logs plus `fire.cpp`, senior, 2026-11-13)

- Claim lag (client `HitClaim shot N ... sent` to host `applied`) is 11 to 30 ms for shots 1 to 16 and 22 to 45,
  then 350 to 1100 ms from shot 46 to the end of the magazine.
- Shot 46 follows this host sequence: `relay press (Fire 1)` 230445217, copy `FireProjectile` 230445220,
  `relay release (Fire 0)` 230445234, `relay press (Fire 1)` 230445234 (same tick). The copy fired no round on the
  second press while the client fired 4 (`card175: release ... held: the copy fired 0 of the client's 4 rounds`,
  230445699). Same shape at 230396068/230396074 (0 of 5; shots 17 to 21 dropped).
- Prediction for the cause: the copy's fire ability from the first press is still active when the second press
  arrives, so the press starts nothing and the ability then ends on its own. The diagnostic below decides it.
- Each lost press leaves the copy behind by that press's rounds. Later claims wait for later rounds (lag), the
  copy's magazine empties after the client's, and its own reload (230451489) starts 1458 ms after the client's
  (230450031) and blocks fire while the client is firing again: 16 drops at 230454 to 230456 with the copy's
  magazine already raised to 60 (`card274 ... 0 to 60`, 230452497).
- `ended short by the bound` at +16 ms is the next press ending the hold (`c24_relay`), not the 300 ms bound. The
  log text is wrong for that case.
- The client's reload on an empty magazine has no key press, so card 274's key relay does not carry it: 6 client
  reload abilities, 4 relayed presses, 2 of those 4 were key presses on a full magazine with no reload.

## Fix spec (`mod/src/ubw/fire.cpp`, `hooks_internal.h` `C24Relay`; reload source in `playlog.cpp` call site only)

1. Re-press: `C24Relay` gains `press_ms` and `represses`. In `c24_host_guard`, a primary relay of a soldier weapon
   with `fired == 0`, not in a copy reload, and `now - press_ms >= 100` gets tag released and pressed again
   (`g_orig_asc_tag_released`, `g_orig_asc_tag_pressed`), at most 3 times per press. Log `card276: host re-pressed
   Fire on <pawn> (try N, M ms after the press, fire ability active A)`.
2. Diagnostic on every relayed primary press (capped 60 lines): `card276: host press with the copy's fire ability
   active A, D ms after the last release`.
3. Hold end text: `ended by the next press` when `c24_relay` ends it, `ended short by the bound` only from
   `c175_hold_due`.
4. Reload source: the client sends Fire kind 3 when its own `BP_WeaponReloadAbility*` activates (the activation
   detour that feeds `pl_activation`, own pawn, `ok` true), not on the reload key. Remove the key source.
5. Do not touch `claim_c157.cpp`, `kC175HoldMs`, or the card 274 magazine code.

## Predictions (next FED_C3M2 run, batched; budget 0 extra runs)

- Diagnostic rows with `active 1` are exactly the presses that fired 0 rounds before the fix; each gets one
  `re-pressed` row and the copy then fires (`the copy fired 0 of` rows only before a re-press).
- Claim lag stays under 100 ms through a whole magazine; 0 `found no round` rows while the client has rounds.
- Relayed reload presses equal the client's reload ability rows; the copy's reload ability starts within 100 ms
  of each.
- If `active` is 0 on the lost presses the cause is another gate (rate of fire or the Shunt ability at
  230445530): stop, report the rows, no further change.

## Run `c243-c3m2-b4a` (build 4, 2026-11-13): partial, no verdict

The run was void for objectives (card 243 note: no ammo), so most rows sit on an empty or near-empty weapon.
Junior counts: 60 host press rows (9 with the fire ability active, 51 not), 60 re-press rows (try 1: 33, try 2: 14,
try 3: 13; 106 to 927 ms after the press). The re-press is bounded at `kC276Represses` per relay
(`fire.cpp:284`), so the 50 re-presses near 244034940 are many presses, not one storm. Host `relay press
(reload)` 5 rows against 2 client `sent Fire N (reload ability)` rows: the prediction "relays equal the client's
reload sends" did not hold as logged.

For the next FED_C3M2 run (with ammo), report per press: was the copy's magazine above 0 at the press (card274
`host copy magazine` row before it), and did the re-press produce a round (`r.fired`). Count re-presses only for
presses with a non-empty magazine. For the reload rows, list each of the 5 host relays with the client row (or
none) within 200 ms before it; a host relay with no client send means a second sender, name it from the code.

## Run `c276b67` (b67 `cc1d4251720f389d`, FED_C1M1, 2026-11-15): pass for taps and a hold through a reload

Scenario `tools/scenarios/c276-tap-hold.txt` (20 taps of 80 ms, then one 9 s hold from a full magazine; Morita Mk1,
miss claims at the ground). Numbers from `logs/c276b67-{host,client}.log`:

- 22 relayed presses, 22 `host press` rows, all `fire ability active 0`. Tap presses arrived 100 to 300 ms apart;
  the closest followed the release before it by 17 ms (406931559 release, 406931576 press) and fired.
- Tap phase: every press end row has rounds 1, re-presses 0; the magazine at the press steps 60, 59, ... one per tap.
- Hold: `magazine at the press 60, re-presses 1, rounds 65, re-press produced a round`. The one re-press came 8538 ms
  after the press with `credit waiting 1` (after the copy's reload). Reload rows: client 2, host received 2.
- `tools/match_shots.py`: client shots 86, host copy shots 86, matched 86, dropped 0. The claim offset is constant
  (+106 to +108 ms from shot 2 to shot 81, +31 after the reload; clock offset, no growth through the magazine).
- 0 `found no round` rows, 0 FATAL/ERROR.

Not exercised: a press in the same host tick as the release before it with the copy's ability still active
(`active 1`, the run A shape). Earlier firing runs (b38r2, b39, b40) also show only `active 0` and 0 drops. The card
stays open as a playtest watch item: a client whose shots do no damage, or any `found no round` row in the playtest
host log, reopens it with those rows.

## Closed

Closed 2026-11-17 (senior): proof run above (86 of 86 shots matched, 0 found-no-round rows); b38r2, b39, b40 and every later firing run also 0 drops. The active-1 shape is a playtest-log watch in STATE: any found-no-round row reopens.
