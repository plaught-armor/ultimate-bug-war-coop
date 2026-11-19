# 322 Mission radio, subtitles, music and time dilation run per process

Sync audit second pass, 2026-11-14 (`docs/SYNC_AUDIT.md` L2). No mod code touches `USTConversationSubsystem`
(`StartConversation` 0x05104C90, `AddConversationToQueue`, `StartConversationFromList`), `USTMusicManager::
TriggerMusic` (0x05109670), `UADTimeDilationSubsystem::PlayTimeDilation` (0x05A1C670), the out-of-bounds countdown
(`USTOutOfBoundTriggerComponent`) or `ASTCompanion`. Their callers are Blueprint thunks, plus
`TickMusicModeManager` and `UADGameplayAbility_TimeDilation::TimeDilationStart`.

Not settled: which of these the client's own level script copy fires (class 5, card 280), so whether the client
hears each line once, twice, late or never.

## Step 1 (diagnostic, both sides, log only)

One row per call: `card322: <side> t=<ms> <conversation|music|dilation|out of bounds> <asset or trigger name>,
caller <object class>`. Map-end tally per side by name.

## Prediction

Host and client tallies differ on at least one conversation or music trigger on C1M1 when the players are apart at
the trigger. Equal tallies with start times within 500 ms on every name close the card with no fix.

## Fix (after step 1)

Host owns each: the host's start goes out as a row (name, stage, time), the client's own start for the same name
is cut, and a join replay sends the conversation in progress and the current music mode. Time dilation: the host's
world value is the only one; a client ability asks the host.

## Sibling sweep

Barks (`UADBarkSubsystem::RequestBark`, ranged from the host's controller) go with card 314's sound sweep.

## Step 1 result (run 3, b19, `logs/c338-c337-{host,client}.log`)

| Line | Host t | Client t |
|---|---|---|
| FED_C1M1_INTRO01 | 324687457 | none (client LoadMap returned at 324690833: not in the map yet) |
| FED_C1M1_LANDED01 | 324689102 | none (same) |
| FED_C1M1_TACSUPPORTINTRO01 | 324695560 | own start (run 2) |
| FED_C2M1_DROPSITELANDING01 | 324721607 | none (client in the map since 324711777) |
| FED_C2M1_INTROFLIGHT01 | 324722607 | none |
| FED_C2M1_FLAKBUGINTRO01 | 324723272 | 324723315 (own start) |
| FED_C2M1_PLASMABUGINTRO01 | 324729222 | 324729249 (own start) |

Every row is `StartConversationFromList` with a list of 2, caller the level script. No music, dilation or
out-of-bounds row in this run. The prediction held: the client misses lines its own script copy does not reach
(the dropship landing chain is host-only), so a fix is needed.

## Static trace (b19 exe)

- `StartConversationFromList` (0x05104E80): empty list returns. Otherwise it picks `rand() & 0x7FFF` scaled to the
  list length, clamped to `num - 1`, and calls the internal start 0x05104D40 `(self, &picked, a, -1, b >= 0 ? b :
  the data CDO's int at +0x38, c)`. It frees the list's data (the by-value TArray is the callee's). So host and
  client pick their own variant even when both start the same line.
- `StartConversation` (0x05104C90) is the same tail for one class: `(self, &cls, a, -1, b or the CDO default, c)`.
  Its second argument is a pointer to the `TSubclassOf` (by-value class type passed by address), not the class.
  The step 1 detours `detour_c322_start_conversation` and `detour_c322_add_conversation` name the pointer, not
  the class: read `*data_class`. No row hit them in step 1, so no logged name is wrong.

## Fix, part A: conversations (always on, both sides)

Host owns every conversation start. One new op `ChanOp::Radio`, text row `<kind> <class path> <a> <b> <c>`, sent
with `c95_send_one` to every remote PC (the card 260 `EndLevel` loop), ledger kind event.

- Host, `StartConversationFromList`: the host picks the index itself, writes the picked class to `data[0]`, sets
  the count to 1, then calls the original (a list of 1 always picks 0, and the original still frees the data).
  It sends kind `start` with the picked class.
- Host, `StartConversation`: sends kind `start`. `AddConversationToQueue`: sends kind `queue`.
- Client, own script call of any of the three: skipped. For the list call the original is not
  called, so the detour releases the list's data the way the original does (the call at exe+0x5104F9B to
  0x011F0340 on the data pointer when it is not null). Counted per name.
- Client, relay: class by full path with `g_static_find_object` (find only, no load; null is a WARN row), then
  the original `StartConversation(self, &cls, a, b, c)` or `AddConversationToQueue(self, &cls, a, b)`.
- Client `self`: the subsystem. Take it from `USubsystemBlueprintLibrary` through ProcessEvent on its default
  object, the card 337 pattern (`GetWorldSubsystem`, then `GetGameInstanceSubsystem`; the non-null one wins and
  one row per map names which). Not from a cached `self` of the client's own calls: on FED_C2M1 the first relay
  comes before any own call.
- The three native hooks and the three conversation exec thunks leave the `g_gate_c62_diag` gate. The music,
  dilation and out-of-bounds hooks stay diagnostic.

Not in part A, with reasons:
- Join replay of the line in progress: no seek entry is traced, and a replay from the start would run long after
  the host's. A joiner misses the line in progress and gets every later one. The card 320 hold has both players
  in the map before the host's continue, so only a mid-mission rejoin is affected.
- Music mode and time dilation: no row in run 3. Part B, after a run that shows them.

Presume-defect checks:
- A client-only line (started by something only the client's process sees) is lost. The skip row names its caller
  and the map-end tally counts skips by name; a skipped name with no relay of the same name on that map is a WARN
  at map end.
- A relay before the client's subsystem exists (during its load): WARN row, line dropped.
- The host's pick must stay inside the list: index `min(num - 1, pick)`; a null picked class sends nothing and
  calls the original unchanged.

Rows:
- `card322: host t=<ms> radio <start|queue> <class> (picked <i> of <n>) sent to <k>`
- `card322: client t=<ms> radio <start|queue> <class> started (relay)`
- `card322: client t=<ms> radio <class> own start skipped, caller <class>`

## Predictions (run with card 339, scenario `c338-c337-c1m1.txt`)

- FED_C2M1: the client logs `started (relay)` for DROPSITELANDING01, INTROFLIGHT01, FLAKBUGINTRO01 and
  PLASMABUGINTRO01, each once, each with the class name of the host's row, each within 500 ms of the host's t.
- The client logs `own start skipped` for FLAKBUGINTRO01 and PLASMABUGINTRO01, and none for the other two.
- FED_C1M1: INTRO01 and LANDED01 `sent to 0` (the harness client joins after them), TACSUPPORTINTRO01 relayed.
- 0 WARN card322 on either side.

## Part A result (run 4 of c338-c337, b21 `ba009c82b58a8d90`)

- Client `started (relay)` rows: TACSUPPORTINTRO01, DROPSITELANDING02, FLAKBUGINTRO02, INTROFLIGHT01,
  PLASMABUGINTRO01, each 8 to 47 ms after the host's row (junior's numbers). The host picks the variant, so 02
  variants appear where the prediction named 01.
- Client `own start skipped` rows: TACSUPPORTINTRO01, FLAKBUGINTRO01, PLASMABUGINTRO01. 0 WARN card322.
- The client subsystem came from `GetWorldSubsystem`. Part A accepted 2026-11-14.

Gap, fixed in b22: the client's list skip counts only `data[0]` (FLAKBUGINTRO01) while the host relayed another
variant (02), so the map-end check (a skipped name with no relay of that name) would WARN falsely. Fix: a list skip
counts a skip for every element of the list, and a relay of any element clears the whole list's group.

Part B (music mode, time dilation, out of bounds) stays open: no row in any run so far.

b22 result (`0c40725031a4aede`): list skips count the whole list. Three list skip rows (TACSUPPORTINTRO01,
FLAKBUGINTRO01, PLASMABUGINTRO01, each "(list of 2)"), 0 card322 map-end WARN on both sides.

## Part B, 2026-11-14, junior (batch b44): log check first

The four named logs (b42, b43, b43r2, c355-b40) have no music, dilation or out-of-bounds row: the hooks are in and
diag-gated, but each run's FED_C1M1 part was short (FAST) and none entered a volume.

Music, older logs (b37, b38, b38r2, b39, b40, all FED_C1M1): every run has `music trigger 1`, then `music trigger 2`
about 245 s later, on both sides, same type, host and client 14 to 41 ms apart. Caller `native exe+0x5235B13`, inside
`USTCommonUtilityLibrary::ExecuteMusicTrigger` (0x5235AA0), a static Blueprint library call (the Blueprint behind it
is not named: no exec hook on that thunk). b40's client logged one more type 2 after its checkpoint reload (the
detour drops a repeat of the last type, so it is the reload's own restart). By this card's own rule (equal tallies
within 500 ms) music needs no fix. Not settled: a trigger only one process reaches (an area the other player is not
in); none seen.

Time dilation: no FED source found. The only dilation asset is
`Blueprints/BugForms/HopperForm/BP_BugForm_DiveBombSlowDown_Hopper` (the Bug side's Hopper dive), outside the Trooper
playthrough. The producing event is a Hopper-form player's dive-bomb ability; not added to b44.

Out of bounds: FED_C2M1 has 4 placed `STOutOfBoundsVolume` actors (card 098 tally placed=4 rep=0), one per process.
The producing event is a pawn entering one; the pawn's `OutOfBoundsTrigger` (`STOutOfBoundTriggerComponent`) runs
the countdown. b44 warps each player into the nearest volume (`warp actor STOutOfBoundsVolume 0`), screenshots,
warps back.

Predictions (b44):
1. Host warp: host `card322: host ... out of bounds entered BP_PlayerCharacterMI_C`. Unknown: whether the client
   logs one for the host's pawn on its side (a proxy's trigger overlapping the client's own volume copy). A client
   row means both processes run the countdown for one pawn.
2. Client warp: client row for its own pawn; host row for the client's server copy if the copy's trigger overlaps
   too. Rows on both sides for one pawn = per-process countdown (fix by the part A pattern: host owns it, client's
   own start skipped, host row relayed). A row on one side only = that side alone runs it; the screenshot shows
   whether the other player sees the warning.
3. An `exited` row on the same side(s) after the warp back.

### Part B, run b44r2 (build d875ca838402bfc5): out of bounds result

- Host warp into the volume: `card322: host ... out of bounds entered BP_PlayerCharacterMI_C` at 345101683 and
  `card322: client ... entered BP_PlayerCharacterMI_C` at 345101690 (the client's copy of the host's pawn), exited
  345104006 / 345104021, both from the same native callers (exe+0x51CEDDF entered, exe+0x51D0385 exited). Both
  processes run the volume's overlap for the host's pawn: the out-of-bounds start runs per process, the case
  prediction 3 named. Screens: host `oob-host-in` has the red out-of-bounds tint; the client's view at that moment has
  no tint (the screen effect is local to the own pawn only).
- Client-local warp (`client warp actor STOutOfBoundsVolume 10 behind`): client entered 345107132 and exited 27 ms
  later, no host row; the client stayed at its start position (232629, 43682, 8720). The host's correction moved the
  client back at once, so the client case is unproven by that step.

Run 3 (last of the budget): the client block warps the client's server copy instead (`host warp client
STOutOfBoundsVolume 10 behind`, back with `host warp client point 232629 43682 8720`). Predictions:
1. A host `card322: host ... out of bounds entered` row for the client's server copy, then a client `entered` row for
   its own pawn within about 100 ms (the moved copy replicates).
2. The client screen `oob-client-in` has the red tint; the host view `oob-client-in-hview` has none.
3. Both `exited` rows after the point warp back. Rows on both sides = the same per-process countdown for the client's
   pawn as for the host's; then the fix is one class: the host owns every out-of-bounds start and end, a proxy's
   start is skipped, and the host relays start and end to the owning client (the part A pattern).

### Part B, run b44r3 result (build d875ca838402bfc5, drive rc 0, scenario passed, no crash; WARN host 5, client 2, all known classes)

1. Held: host `entered` for the client's server copy at 345344374 (`cmd: 12 warp: server copy ...`), client `entered`
   for its own pawn at 345344385 (11 ms later). The host-pawn case repeated run 2: host 345338925, client 345338934.
2. Held: client `oob-client-in` has the red tint, host view `oob-client-in-hview` has none. Run 2 showed the mirror
   case. The tint is drawn only by the owning player's process: correct in both runs.
3. Held: both `exited` rows for each pawn (host 345341247 / client 345341263; host 345346686 / client 345346731).

Conclusion: the out-of-bounds begin and end overlaps run on every process for every player pawn (the engine's
normal overlap on both copies). The one visible effect, the tint, is right on both sides. Not shown: what the
countdown's expiry does, because each stay lasted 2.3 s (the pawn fell from z 1100 to below -2700 in that time). A gap
exists only if the expiry kills or moves a pawn on a process that does not own it (the client killing its copy of the
host's pawn, or the host and the client both running the kill for the client's pawn). Next step, static: trace the
handler behind the entered caller exe+0x51CEDDF to its timer callback and name its authority check. If there is none,
apply the part A pattern (the host owns the expiry, a proxy's expiry is skipped). No fix was built in b44.

## Closed (2026-11-17 13:12)

The one open step of part B (name the authority check behind the out-of-bounds countdown's expiry) is answered,
static, from the exe:
- `USTOutOfBoundTriggerComponent::OnOutOfBoundsEntered` (exe+0x51D1070) sets two timers: the expiry, through the
  thunk exe+0x51BB3E0 to the component's virtual at exe+0x51CD6E0, and `UpdatePostProcessEffect` (exe+0x51EA3E0).
- The expiry virtual is three instructions: owner (`this+0xA0`), `dl = 1`, tail jump to
  `USTCombatUtilityLibrary::InstaKillTarget` (exe+0x5241580). There is no authority check on the way, so every
  process kills its own copy of the pawn at its own expiry.
- That is already closed by cards 360 and 361: the client never InstaKills a player-class actor
  (`insta_c361.cpp`; this caller is named there as KillOwningActor through `g_c361_oob_expiry`), the host's kill
  reaches the client by the Down frame, and card 362 WARNs when a skipped kill gets no Down frame in 3 s.

State of the other parts: conversations (part A) fixed and proven in b21; music triggers run on both sides 14 to
41 ms apart with equal tallies (no fix by this card's rule); time dilation has no Trooper-side source; the
out-of-bounds tint is drawn by the owning player's process only, right on both screens (runs b44r2, b44r3).

Not done: a music trigger only one process reaches (an area the other player is not in) was never seen and never
forced.

Reopen when: a `WARN card322` row outside the known noise (after a scenario rejoin; `client map end: radio ... own
start skipped`), a `WARN card362 ... had no Down frame` row, music heard by one player and not the other, or a
radio line or subtitle on one screen only.
