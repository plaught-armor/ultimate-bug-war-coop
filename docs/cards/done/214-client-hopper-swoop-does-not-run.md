# Card 214: The client's Hopper Swoop does not run

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-12, from run c210-r2 (DLL `04264aa86c6b11e0`). Budget: static first, then one run, batched
with card 212 (same map, BUG_C1M1).

Read: docs/cards/done/210-bug-copy-form-look-on-host.md (fix 2, the Swoop grant), `mod/src/ubw/grenades.cpp`
(`c73_point_item`, `c73_wait_slot`), the card 021 and card 024 local activation path.

## Why

c210-r2: the client, as a Hopper, pressed LMB (scenario line 80, InputTag.Fire). The grant was bound ("grant
BP_BugForm_SwoopAbility_Hopper_C bound to replicated spec h194"), but the ability failed locally 48 times:
"card021: client local activation handle 194: failed, spec present, ability BP_BugForm_SwoopAbility_Hopper_C" and
"card024: ... handle 194 = 0, ability null". The host logged no Swoop activation. The host's own Swoop was not
pressed in that run, so the host side is unknown.

Known: the Swoop ability is InstancedPerExecution, so the spec has no instance until it runs; card 024 reads an
instance ("ability null").

## Do

1. Static: which check fails in the local activation (CanActivateAbility: tags, cost, cooldown, movement mode; or
   the mod's own path needing an instance). Name the function and the failing condition. Check the same path for
   every other InstancedPerExecution ability a bug form grants (class fix, not Swoop only).
2. Fix at the cause. A press that the game itself refuses (for example Swoop only while airborne) is not a bug:
   then the scenario must meet the game's condition, and the 48 repeated fail lines get a cap.
3. One run: client Hopper Swoop under the game's own conditions, host Hopper Swoop, each seen from the other side.

## Predictions (write before the run)

To be filled by the junior after the trace: the activation line on the client, the server-copy line on the host,
the receiver-side line for the host's Swoop on the client, 0 "card021 ... failed" lines for the Swoop handle.

## Done when

- The trace, the fix, the run lines. The usual Report items. It ends with "Report complete."

## Junior trace (2026-11-12, static: mod source, cooked asset, c210-r2 logs)

**The game refuses the press. The mod's path does not fail.**

The path: `AbilityInputTagPressed(InputTag.Fire)` on the STBugForm ASC, then the game's TryActivateAbility, then
`detour_internal_try_activate` (`input_diag.cpp:1236`), then the original `InternalTryActivateAbility`, which
returns 0.

- Before the original call, the mod refuses only through `c65_refuse_client_try`, for event-data presses. No card065
  line exists for the Swoop.
- `c62_before_try` and `c104_policy_set` (server copy only) do not refuse.
- No code on this path needs an ability instance. "ability null" in card024 is `*out`, which the engine writes only
  on success. An InstancedPerExecution ability gets its instance from `CreateNewInstanceOfAbility` only after
  CanActivateAbility passes, so a refused press always shows null.
- The press reached the original with "spec present" because card 210 fix 2 bound the grant (h194).

**The failing check is CanActivateAbility, through the ability's tag requirements.** The cooked asset
`BP_BugForm_SwoopAbility_Hopper.uasset` (pakchunk0, extracted with `tools/iostore/iox.py`) names two gameplay tags,
`MovementMode.Native.Walking` and `MovementMode.Native.NavWalking`. It also names `BP_BugForm_SwoopCooldown_Hopper`
and `K2_CommitAbility`. The asset uses unversioned serialization, so which tag container holds the two tags cannot
be read from it.

The run timeline shows they block activation on the ground:

- All 48 failures (125608610 to 125609397 ms) come while the client is on the ground, one per frame while LMB is
  held 800 ms.
- Jump h192 and Glide h193 activate on the same ASC 140 ms later.
- No Fire press comes after the jump.

The same ASC activates Jump and Glide, so the locally-controlled and net-policy checks pass. A Swoop pressed while
airborne (Glide active, movement mode Falling/Flying) meets the game's condition. The c210-r2 scenario already noted
"A grounded Hopper's Fire is Swoop, blocked while Walking". Not a bug.

**Class check, every bug-form grant on the instance-less path.**

- Binding (card 073, `c73_point_item`) is keyed on the ability class having a SourceItem, not on the instancing
  policy. In c210-r2 all 8 bug-form grant classes the client got bound to their replicated specs: Jump, Glide,
  Swoop, DamageVolume and Taunt (Hopper), Vampirism (Warrior), GA_BugTaunt, GA_CharacterJump.
- The activation path has no instance dependency.

So no other InstancedPerExecution bug-form ability can fail in the mod's path. Other fail lines in c210-r2 are the
game's own conditions, and are listed here, not fixed:

- `DamageVolumeAbility_Hopper` h195 failed once at 125609620.
- `GlideAbility_Hopper` h193 failed once at 125609654, a re-press while Glide was active.
- `MeleeAbility_Warrior` h44 failed once.
- `GA_BugForm_SwitchAbility` h173 failed once.

## Plan (Phase B, after the senior's answer)

- Cap, in `mod/src/ubw/input_diag.cpp` `c24_log_local_activation` only: consecutive failures of the same handle log
  at the 1st, 10th and 100th, with the count (`card024: client local activation handle %d = 0 ... (fail %u in a
  row)`). A success or another handle resets the run. Successes log as now. The constant `kC24FailRunLogs` goes in
  `hooks_internal.h`.
- No fix to the activation path itself.
- Scenario (c212-r1), for each side as a Hopper: Space (Jump and Glide), wait for the Glide line, LMB 300 ms, wait
  for the Swoop line. One grounded LMB is kept on the client to show the cap.

Files: `mod/src/ubw/input_diag.cpp` (`c24_log_local_activation`), `mod/src/ubw/hooks_internal.h` (one constant).

### Predictions, run c212-r1

5. Client, airborne: `card024: client local activation handle \d+ = 1, ability BP_BugForm_SwoopAbility_Hopper_C,
   owner BP_PlayerCharacterBug_C`, `card021: client local activation handle \d+: activated, spec present, ability
   BP_BugForm_SwoopAbility_Hopper_C` and `playlog: client t=\d+ ability slot \d+ client-own:
   BP_BugForm_SwoopAbility_Hopper_C`.
6. Host, the client's Swoop seen there: `playlog: host t=\d+ ability slot \d+ server-copy \(client request\):
   BP_BugForm_SwoopAbility_Hopper_C` and `card021: host client request handle \d+: activated, spec present, ability
   BP_BugForm_SwoopAbility_Hopper_C`.
7. Host's own, airborne: `playlog: host t=\d+ ability slot \d+ host-own: BP_BugForm_SwoopAbility_Hopper_C`.
8. Client, the host's Swoop seen there (open, see question 1 in the report): the host pawn's position from two
   `cmd: \d+ look: BP_PlayerCharacterBug_C at` lines on the client, before and after the host's Swoop. The
   displacement is larger than the glide-only drift measured the same way.
9. The grounded client LMB: at most 3 `card024 ... handle <swoop> = 0` lines for the held press, the last with
   `(fail 10 in a row)`. Zero `card021 ... failed` lines for the Swoop handle while airborne.
10. Mirror of 8, the client's Swoop seen on the host: prediction 6's server-copy activation line, plus the client
    pawn's position from the host's `cmd: \d+ look: BP_PlayerCharacterBug_C at` lines: one before the jump, one
    while gliding (the glide-only drift), five after the client's Swoop line. The horizontal displacement after the
    Swoop is larger than the glide-only drift.

Unproven: the tag container that blocks the grounded Swoop. The Swoop asset is unversioned, so its blocking tags
(MovementMode.Native.Walking, MovementMode.Native.NavWalking) are read from the name table only; the evidence is the
timeline (the grounded press fails, the airborne press is not yet seen). The run settles it: prediction 5, "the
client's Swoop activates while gliding", is met or missed.

## Run c212-r1 (2026-11-12, DLL 6d7f5193580ad2cd, BUG_C1M1)

Host rc 0, client rc 0, scenario passed. Client Swoop handle 204.

5. Met: client `t=126718005` `card024: client local activation handle 204 = 1, ability
   BP_BugForm_SwoopAbility_Hopper_C, owner BP_PlayerCharacterBug_C`, the card021 "activated" line and `playlog:
   client t=126718005 ability slot 0 client-own: BP_BugForm_SwoopAbility_Hopper_C`, 309 ms after the Glide line.
   The tag claim is settled: the game refuses Swoop on the ground and runs it while gliding.
6. Met: host `t=126718012` server-copy (client request) Swoop playlog line and `card021: host client request handle
   204: activated`.
7. Met: `playlog: host t=126721450 ability slot 0 host-own: BP_BugForm_SwoopAbility_Hopper_C`.
8. Met: the client's look lines for the host pawn: -15481 -447 2794 (t=126720986, ground), -15521 -456 3526
   (t=126721295, gliding: 40 uu horizontal), then -16218 -651 4532 (t=126721809) to -17168 -828 4108 (t=126722228)
   after the Swoop: about 1700 uu horizontal in 0.9 s.
9. Met: the grounded press gave two lines, `handle 204 = 0 ... (fail 1 in a row)` and `(fail 10 in a row)`, and one
   card021 failed line at 126716566 (grounded). Zero card021 failed lines for 204 while airborne.
10. Met: the host's look lines for the client pawn: -16982 -377 2804 (t=126717536, ground), -17084 -377 3926
    (t=126717943, gliding: 102 uu horizontal), then -17979 -498 4722 (t=126718458) to -18433 -610 4296
    (t=126718865) after the Swoop: about 1350 uu horizontal in 0.9 s.

Effects (Answer 2; card136 logs map totals only, so the per-event lines are the card062/card073 sound lines):
host Swoop: client `card062: client t=126721483 remote sound MS_FLY_PlayerHopperBug_AirDash` (33 ms after it).
Client Swoop: no AirDash line on the host (the host logs only movement sounds of the client pawn in that window:
Heavy_Land_Dirt, Movement_Stone, and the muted server copy's Hopper_Flight). No Niagara or montage line on either
side in either window.

Report lines (not fixed here):
- The card024 fail cap holds per handle, but the airborne press alternates handles 203, 204 and 205, so each fail
  is "(fail 1 in a row)" (8 lines in 0.25 s).
- Glide re-activations (handle 203 = 1, about every 50 ms from t=126718811, the client airborne, no key held by the driver) fill the 120-line card024 cap at
  t=126725087.
- Failed card024 lines print `ability null` (the out ability is not set on a failure).

## Senior review (2026-11-12): accepted

Predictions 5 to 10 read from the c212-r1 logs by the senior. The game refuses a grounded Swoop; the mod's path is
sound. Follow-ups: card 216 (the client's Swoop sound, Niagara and montage on the host), card 217 (Glide
re-activation every 50 ms, and the fail-run cap reset by alternating handles).
