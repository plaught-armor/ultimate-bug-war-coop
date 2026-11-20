# Card 514: stay stands in the presence volume that sets a step's script flag

## Finding (senior kis.py trace, 2026-11-19, after c504k2)

FED_C4M1 Stadium_SO2 has no timer and no encounter gate. Ubergraph 8598: `if not (inStadium) goto 8891`; 8891 is
`CallFunctionWithDelay(10.0, 'Add Stadium_SO4')`, a 10 s retry. inStadium is set true at 5911 by
BP_PlayerPresence_C_5's enter event (bound event 10, offset 5729) and false at 6119 by its leave event (5937). So
Stadium_SO4 is added, and Stadium_SO2 completed (8740), on the first 10 s tick with a player inside C_5. In c504k2
the 95 s of retries (8231025 to 8326126) were players outside C_5. Lesson 18.

## Do

1. Per map (C4M1 first, then C5M2, C2M1 and the other soak maps), list with `tools/kismet/kis.py` every step whose
   Add or Complete waits on a script flag that a presence volume's enter/leave event sets. Signature: an
   `if not (<flag>) goto N` before the Add/Complete and a `CallFunctionWithDelay` retry at N; trace each flag's
   writes back to a `BndEvt__..._BP_PlayerPresence_C_<n>_...` entry. Table: map, step, flag, volume, set/clear offsets.
2. The routine's stay stands in that volume (not only at the step's marker actor) for steps in the table.
3. The encounter-progress stay (card 513 item 5) stays as it is.

## Done when

- Table written here for C4M1 and C5M2, each row with offsets.
- A run shows Stadium_SO2 closing on the first 10 s retry after both players stand in C_5.

## Rule (senior, 2026-11-19)

For a presence step, and for a flag step whose volume is known, the go and the stay stand at the volume's box
centre with both players inside before the wait. The go already does this for the step's own presence (the presence
nearest its marker, "stand inside 1" on both rows). Two gaps broke it in c513a; both are fixed in b287 (below).

## FED_C4M1 table (build/kis/c4m1.txt, ExecuteUbergraph offsets)

| Step (Add or Complete) | Gate | Volume that sets it | Set / clear | Retry or other path | Volume disabled |
|---|---|---|---|---|---|
| Stadium_SO2 complete (8740) and Add Stadium_SO4 (8612) | `if not (inStadium)` 8598 | C_5 (-3250, -21800, -1200): ev10 enter 5729, ev11 leave 5937 | 5911 true, 6119 false | Two ways into 'Add Stadium_SO4': (1) `CallFunctionWithDelay(10.0)` 8891 retry; (2) Fireworks event 20412 to 20473 `goto 20342`, `CallFunctionWithDelay(5.0)` (after the Half Time Show branch at 20079 and goto 15489; Fireworks is delayed 18 s) | C_5 at 9467 (after Stadium_Marker completes at 9251 and tankerCount reaches 2) |
| Add Stadium_SO3 | `inStadium` 14248 | C_5 | as above | 10 s retry at 14345 | none |
| Metro_SO1 complete (13377) and Add Metro_SO2 | `metroReady` (set at 13050 by the Metro_SO1 Add) | C_17 (-7500, -93720, 1080): ev29 14243 to 13740 | 13050 | none | C_17 at 13414, after the step |
| Metro_SO2 Add (13199), Metro_SO2 complete (14153) | `eastboundReady` (set at 13250 by the Metro_SO2 Add) | C_13 (-12680, -95700, 700): ev25 14104 | 13250 | C_13 triggers EM_3 (13964); the EM_3 encounter event (14119) adds Metro_SO3 by CallFunctionWithDelay, then completes Metro_SO2. Second complete path at 16672 (from 23863) | C_13 at 14000 (on entry). C_15 at 14070 (path 14034, from 17943) |
| Metro_SO3 complete, Add Metro_SO4 | encounter: EM_4 (14191) | none (encounter end) | none | none | none |
| Metro_SO4 Add (16445): horde start | `hordeReady` (set at 16496) and `atMetro` | C_191 (-7548, -93940, 620): ev11 16624 sets atMetro (and runs Trigger horde if hordeReady); ev20 16660 clears it | atMetro: 16624 true, 16660 false | Trigger horde (24075) goes to 16517 | C_191 at 16517 (then EM_11 and HordeZone KillEmAll). Metro_SO4 completes at 16321 (Add Metro_SO5 path) |
| Level end (imp847 via GameInstance, 13797) | no gate | C_70 (-7489, -97839, 830): ev7 16245 | none | none | none (needs 2 of 2 players inside, card 227) |

Other `imp858(false)` disables: 1299 C_8, 1332 C_257, 3810 C_4, 8857 C_31 (after the Stadium_SO4 add path), 9384
C_31, 12336 C_9.

## c513a findings

1. **Stadium_SO2 (flag step).** Try 1 stood in C_5 (both players entered at 9980248, so inStadium was set). Nothing
   reaches the 8598 gate until the Fireworks path (5 s delay) or a 10 s retry, so 10 s found no Complete row. Tries 2
   and 3 then moved to the next presences, and the leave event cleared inStadium. The 23 'Add Stadium_SO4' retries
   (10056669 to 10271886) all ran with the players outside C_5, in try 3's 255 s stay.
2. **Metro_SO2 (presence step, C_70 miss).** Try 1 stood in C_13 (the nearest presence to the marker at (-15200,
   -97860, 1240)), and its entry disabled C_13 (14000). Try 2 (`c284_nth_presence` nth 1) ranked live presences only.
   With C_13 now off, rank 0 was C_70 (7722 uu) and was skipped, so try 2 stood at C_191 (8620 uu) and try 3 at
   (-2500, -95400, 940). d2's first go (go count 3, nth 0) stood in C_70 and the level ended (10437725).
   This was a stand-choice bug, not a walk fault: every go row shows "inside 1".

## Fix (b287; b288 dwmapi-b288-cc587891d7bf7846 adds the review logs)

1. `c284_nth_presence` ranks the skipped presences over all of them, live or not; only the final pick must be live.
   A presence that an earlier try used up no longer shifts the next try past the live one after it.
2. `objective go <who> home` (cmdfile `kGoHome`) stands at the step's own stand (nth 0) without counting a go.
   `routine_presence` in drive.py uses it before the last try's long stay. So the encounter stay waits in the step's
   own volume (C_5 for Stadium_SO2), not at the last try's next presence.
3. b288 (senior review): a rank above 0 logs `c284: nth presence N: skipped <names>, picked <name>`, so a wrong rank
   shows as a row, not as a VOID. A home go logs `objective go: home stand at <presence> (<from>)`, so the row names
   the volume the stay waits in.

## Predictions for the next C4M1 run

- Metro_SO2: try 1 in C_13 (EM_3 path), and try 2 logs `marker step stands at presence try 2 of 3:
  BP_PlayerPresence_C at (-7489, -97839, 830)`. The C_70 2 of 2 close and the level end come in d1, with no d2.
- Stadium_SO2: tries 1 to 3 as before, then a `go all home` row stands both players at C_5 (stand inside 1). Both
  card245 C_5 begin overlap rows follow. The first 'Add Stadium_SO4' after that completes Stadium_SO2 (8740) within
  10 s of the gate first being reached. There are no retries with the players outside C_5.

## FED_C5M2 table (build/kis/c5m2.txt, ExecuteUbergraph offsets)

Imports: imp1064(false/true) presence disable/enable, imp1063 SetObjectiveVisibility, imp1060 CompleteSub, imp1057
Add, imp1059 Complete (parent).

| Step | Gate | Volume that sets it | Set / clear | Other path | Volume disabled |
|---|---|---|---|---|---|
| Outpost horde ('Outpost Bug Waves', event 3285): EM_1FactionSetWaves_C_5, HordeZone_C_1 KillEmAll, then Bombing Run 5 s, Airstrike 12 s, EndWhiskeyHordeZone 13 s, CompleteWhiskey 14 s | `atWhiskey` (pop_flow_if_not at 3285) | C_402: ev71 enter 32390, ev72 leave 32573 | atWhiskey 32390 true, 32573 false | `hordeReady` (set at 2488 on the Outpost path, Outpost_SO2 completes at 2670) and C_402 entry (32401) run `CallFunctionWithDelay(2.0, 'Outpost Bug Waves')` at 20298; the Outpost path also calls it at 2631 (2 s) | C_402 at 3368 (inside the horde start) |
| Research: show Research_SO2/SO4, Toggle c4 Flares, `CallFunctionWithDelay(3.0, 'Drop satchels')` (15403) | `researchReady` (15367); `bugnestReady` (15330) gates showing Research_SO3 | C_26: ev48 16601 to 16383 (hide markers), 16487 goto 15273 | researchReady and archersReady set at 2779/2790 on the Research_Marker2 Add (2728, ActorReference_C_1); researchReady cleared at 15392; bugnestReady set at 16960 after the Research_SO3 counter (16936) | C_22, C_25, C_27 events only toggle marker visibility (imp1063) | C_26 at 18531 |
| Archer spawn (BP_AISpawner_C_12, 23472) | `archersReady` (28529) | C_617: ev37 28529 | set at 2790 | none | C_617 at 23522 |
| Finale: Nuke Town, Add Finale_SO7 (30571), caveInReady | none on entry | C_408: ev67 30571 | caveInReady set at 30739 | 'Cave in' event 31328 is gated by caveInReady; the cave-in itself is a 30 s delay at 32214 (card 493) | none |

Order rule for the go: the volume entry must come after the flag is set. A stand already inside the volume when the
flag is set does not count; the go lifts out and re-enters (kC284LeaveUu).

Other presence entries (enter event @ offset): C_11 ev32 @13752, C_17 ev10 @8100, C_18 ev16 @4407, C_21 ev12 @8152,
C_22 ev3 @16373 / ev47 @16378, C_23 ev22 @8710 (Finale_SO0 completes there), C_25 ev50 @16611 / ev51 @16616, C_27
ev1 @16363 / ev2 @16368, C_2 ev15 @13527, C_3 ev30 @13528, C_403 ev60 @29405, C_404 ev63 @30070, C_405 ev62 @29696,
C_406 ev65 @30192, C_407 ev64 @30121, C_409 ev66 @30376, C_410 ev24 @32320, C_412 ev70 @32325, C_5 ev31 @13640,
C_62 ev73 @35214, C_6 ev11 @3057, C_8 ev42 @20246.

Other `imp1064(false)` disables: 301 C_8, 3870 C_27, 5298 C_25, 10183 C_22, 20246 C_8.
