# 295 Client movement is jittery after a revive

Playtest 2026-11-13 (user plays as client): "also experiencing jittery movement after i get revived".

## Trace (client log `UBWCoop-menu-20261113-184603.log`, FED_C2M1)

| Client t (ms) | Row |
|---|---|
| 256612751, 256627502 | `own pawn Downed -> Up`; `own view up: ... walk speed 875` |
| 256885638, 256924146 | `own pawn Downed -> Up`; `own view up: ... walk speed 1524` |
| 256887179 | `card028: ... key Run -> Walk (health 50.0, speed 389 ...)` |
| 256924830 | `card028: ... key Walk -> Attack01 (health 25.0, speed 700 ...)` |

Every `own view downed` row reads `walk speed 0`. The client's log has no row for a server correction after any
revive (`rot[...]` rows: `ClientAdjustPosition 0` in all 1260), so the log does not yet show the jitter itself.

## Cause (static; candidate, to confirm in step 1)

`death.cpp`: the downed state is made by writing the movement component's `MaxWalkSpeed` to 0, and the revive by
writing a saved value back. Each machine saves its own value at its own down edge:

- client: `c43_own_view` (line 1352) saves `c43_walk(pawn)` into `g_c43.view_walk` and restores it on Up;
- host: `c43_revive` (line 718) restores `e.saved_walk` on its copy of the pawn.

The value is whatever the game had written at that instant (875 and 1524 here; 389, 700 and 1050 in the card 028
rows are the game's own speeds by state). The two machines can save different values, and the saved value can
belong to a state the pawn is no longer in (sprint, aim, slow). After the revive the client's prediction and the
host's simulation then run at different top speeds until the game next rewrites the speed, and the host's
corrections pull the client back each update: jitter.

## Step 1 (static plus host log, junior, no run)

1. Name the game function that writes `MaxWalkSpeed` (sprint, aim, damage slow, weapon weight) and when it runs.
   Does it run on a revive without the mod's write.
2. From the host log after the playtest: the host's `saved_walk` for the client pawn at host t=82743402,
   82758266, 83016402 and 83054910 (client t minus 173869236) against the client's 875 and 1524.
3. Why the client's `ClientAdjustPosition` counter reads 0 all map: the startup row lists
   `UCharacterMovementComponent_PerformMovement` among skipped hooks; say whether the correction hook is
   installed in the play build.
4. Other mod writes on the own pawn at a revive: camera offset (60 uu, `g_add_local_offset`), capsule, collision,
   the StandUp window (`kC43StandUpMs`); say for each whether host and client apply the same thing at the same
   time.

Write the answers into this card before coding.

## Fix

- The saved raw speed goes. After a revive both machines get the pawn's speed from the game's own function
  (step 1 item 1), so the value fits the pawn's current state and is the same on both.
- The downed stop is one mechanism on both machines, driven by the host's Down row, not two independent saves.
  If the game has its own movement lock (movement mode, the game's stun or root state), use it instead of the
  `MaxWalkSpeed` write.
- Rows: `card295: <role> t=<ms> revive <pawn>: walk speed <v> from <function>, movement mode <m>`,
  `WARN card295: client speed <c> differs from the host row <h>` (the host sends its value in the Up row),
  `card295: client corrections in the 10 s after the revive: <n>, worst <uu>`.

## Sibling sweep (same class: a movement value each machine saves and restores on its own)

| Item | Check |
|---|---|
| Out (bleedout ended) then revive, 6 s hold | same restore path |
| Respawn in place (`c43_respawn`, card 130 item 10, line 737) | `saved_walk` kept or replaced |
| Host's own revive by the client | host's own pawn speed and the client's copy |
| Bug form switch (card 208), warp rows (card 209) | speed and movement mode after the switch |
| Grenade knockback, melee stagger, tanker fire slow | game writes; confirm both machines run them |
| Card 132 digest | add each player pawn's `MaxWalkSpeed` and movement mode so a mismatch shows in every run |

## Predictions (one run, after the playtest, batched)

- Client downed while sprinting, revived by the host, then walks 10 s: host and client `card295` revive rows show
  the same speed; zero `WARN card295`; corrections in the 10 s after the revive at most the count in a 10 s walk
  before the down.
- Digest round after: 0 mismatches, player speeds equal.

## Step 1 answers (static plus host log `client/UBWCoop-menu-20261113-182510.log`)

1. `USTAttributeSet_Movement::PostAttributeChange` (0x051D7BB0) is the game's writer: when the WalkSpeed attribute
   changes it writes the new value to the owning character's CMC `MaxWalkSpeed`. It runs whenever a gameplay effect
   on WalkSpeed is applied or removed (GE_Sprint through `USTSprintAbility::HandleSprintChange` 0x051272C0, and any
   other WalkSpeed effect). A revive changes no attribute, so it does not run at a revive; the mod's write is the
   only one. Aim, damage slow and weapon weight effects were not traced one by one; all go through the same
   attribute.
2. Host `saved MaxWalkSpeed` for the client's copy at the four downs before the revives: 875 at 82738669,
   82751007, 83010247 and 83050307 (revives at 82743504, 82758257, 83016394, 83054929). The client restored 875,
   875, 1524 and 1524. The last two differ: the client was sprinting at its down edge (1524 is the sprint speed),
   while the host's CancelAllAbilities had ended the sprint on its copy. Cause confirmed for those two revives.
3. The `ClientAdjustPosition_Implementation` hook is gated by `g_gate_diag` in the hook table (`hooks_ubw.cpp`), so
   it is not installed in the play build and the counter reads 0. `PerformMovement` is gated the same way.
4. - Camera offset (60 uu, `g_add_local_offset`): each machine moves only its own pawn's camera; a local view, not a
     simulated value. No mismatch possible.
   - Capsule and collision: no c43 write at a revive (no capsule or collision call in the down/up code).
   - StandUp window (`kC43StandUpMs`): both machines drop an Up entry after the same time from their own Up edge;
     the client's edge is one frame trip later. It keeps the entry only; no movement value.
   - MaxWalkSpeed: the only movement value each machine saves and restores on its own (this card).

## Coded (uncompiled)

- `death.cpp`:
  - `c295_restore_walk(pawn, saved, why)`: MaxWalkSpeed from the WalkSpeed attribute's CurrentValue (movement set at
    `kWalkSpeedOffset`, +0xC); the saved value only when the attribute is missing or 0 (card 019's zero set). Row
    `card295: <role> t=... <revive|restart> <pawn>: walk speed <v> from <the WalkSpeed attribute|the saved value>
    (saved <s>, attribute <a>), movement mode <m>`. Used by host `c43_revive`, host `c43_stand_all` (respawn in
    place sibling) and the client's `c43_own_view` up.
  - `c43_broadcast`: an Up frame's v3 (bleedout seconds, always 0 at Up) carries the host's walk speed
    (`c295_wire_walk`). `c43_receive_down` reads it for the own pawn and keeps seconds 0.
  - `c295_client_up`: `WARN card295: client speed <c> differs from the host row <h>`; opens a 10 s window.
  - `c295_note_adjust` (from `detour_client_adjust_position`, `input_diag.cpp`) counts corrections and the worst
    distance; `c295_step` (from `c45_respawn_check`) logs `card295: client corrections in the 10 s after the
    revive: <n>, worst <uu> uu`.
- `hooks_ubw.cpp`: the ClientAdjustPosition hook is no longer gated by `g_gate_diag` (one call per server
  correction; its log lines are capped by `note_event`).

Not coded: the downed stop stays the MaxWalkSpeed 0 write on each machine (the client's own view and the host's
copy); a game movement lock (movement mode or a root/stun tag) was not found in this pass. The digest row for
player MaxWalkSpeed and movement mode (sibling sweep) is not added; card 132's digest layout is shared and wants
its own change. Bug form switch and warp rows are not touched.

## Build b7 change (senior review of b6)

- `c295_restore_walk(pawn, saved, host, why)` takes its value in this order:
  1. `host`: the client's own revive passes `c295_host_value()`, the v3 of the host's Up frame.
  2. The WalkSpeed attribute.
  3. `saved`.
- Only a positive value is written. With no positive value, MaxWalkSpeed is not touched. The row then says
  `from no value` and the function returns -1. This replaces the `e.saved_walk >= 0.0f` guard that
  `c43_stand_all` lost.
- The host callers (`c43_revive`, `c43_stand_all`) pass host 0.
- The row now also prints `host <h>`.
- `c295_client_up(set, now, pawn)` compares the client's WalkSpeed attribute with the host's value and logs
  `WARN card295: client WalkSpeed attribute <a> differs from the host row <h> (speed set <s>)`. This is a
  diagnostic only: the host's value is the one set.

## User answer 2026-11-14

The jitter was on the revived (downed) trooper itself, the user's own pawn, which fits the cause above. Judged by the
user in the next playtest on a build with the b7 change.

## Closed

Closed 2026-11-17 (senior): card 469 proof on b173 (c448b173a): client walk of 3 s after the revive route, 0 position corrections. The feel check is replaced by that measured row.
