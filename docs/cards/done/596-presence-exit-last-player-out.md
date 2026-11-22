# 596: A presence's exit fires when one player leaves while the other is still inside (card 301, C3M1 check)

Source: card 301 "C3M1 open checks" (junior trace 2026-11-21).

## Finding

`ASTPlayerPresence::OnEndOverlap` (0x050F6200) calls OnTriggerExited (0x050FA1A0), which broadcasts the exit for
the leaving actor with no test for another player still overlapping. Scripts that keep an area flag from enter and
exit get the last edge:

- FED_C3M1 presence C_19 sets and clears `inMonorailArea`. With both players inside, one stepping out sets it
  false. `Final Wave` (@11001) and the 2nd wave (@11269) then retry every 15/20 s and never start while the other
  player holds the area. The finale stalls.
- FED_C4M1 Stadium C_5 (`inStadium`) has the same shape.
- The area-flag presences of the other six maps are not swept yet.

## Fix

One class rule in `detour_c227_exited` (objectives.cpp:4339), host side. For a presence that card 227 does not gate,
the exit runs its original only when the leaving pawn is the last living player overlapping it
(`g_c227_is_overlapping` over the other player pawns). Presences that fire their exit once (+0x309) follow the same
rule. A held exit logs one row. When the other player later leaves, the exit runs with that pawn.

Sweep first (static, kis dumps): list every presence on the eight FED maps whose OnTriggerExited binding writes
state, and confirm that none of them needs the per-pawn exit. Put the list in this card.

## Proof (written before the run)

- C3M1: both players in C_19, the host steps out. A host row "card596: host t=<ms> exit held on <presence>: <pawn>
  left, 1 player inside" appears. `inMonorailArea` stays true, and Final Wave starts on its timer.
- When the client steps out too, one exit row appears and the original runs.
- No new WARN. 0 card132 mismatches.

## Static sweep (kis dumps, all eight FED maps)

Presences with an enter and an exit binding, by what the exit writes. Offsets are kis bytecode offsets in
build/kis/<map>.txt.

State flags or encounters written by the exit (the per-pawn exit is wrong for all of them):
- FED_C3M1 C_19: exit @11751 sets `inMonorailArea` false and restores the Beach/Monorail/Resort markers. Read at
  @11001 (Final Wave) and @11269 (2nd wave).
- FED_C4M1 C_191: exit @16660 sets `atMetro` false. Read at @16507, which gates the C_191 disable, the EM_C_11
  trigger and HordeZone_C_1 KillEmAll.
- FED_C5M2 C_402: exit @32573 sets `atWhiskey` false. Read at @3285, which gates the EM_C_5 trigger, HordeZone_C_1
  KillEmAll and the C_402 disable.
- FED_C4M1 C_5: exit @5937 restores the Stadium/Park/Bridge markers and sets `inStadium` false (@6119), then
  runs Stadium Music Eval. Read at @8598, @14248 and @18815.
- FED_C3M2 C_16: exit @17976 hides the PowerPlant markers, shows the area markers and runs ResumeEncounter on
  EM_C_31/C_34 (the enter @17869 stopped them). One player out resumes the encounters with the other still inside.
- FED_C1M1 C_1: exit @6034 sets SetTacSupportChargeable(false), turns off Handle Tactical Support 1..7 and shows the
  area markers.
- FED_C3M1 C_9, C_17, C_64: exit sets BeachBugs{1,2,3}FXOff false and turns the Niagara FX back on.

Marker visibility only (the exit restores the area markers; per-pawn exit shows them to a player still inside):
- FED_C2M1 C_0, C_3, C_4. FED_C3M1 C_11, C_12, C_14, C_15. FED_C3M2 C_15, C_51. FED_C4M1 C_4, C_9.
  FED_C4M2 C_20, C_21, C_22 (Plaza_Marker). FED_C5M1 C_12, C_14, C_15. FED_C5M2 C_22, C_25, C_26, C_27.
- Each marker-only exit block was read to its end (gotos followed); the only other ops are `pop_flow_if_not` on
  a ready flag before one more marker call.

Checks only, no state written: FED_C1M1 C_24 (ShootingRange_O1 / AmTraitor checks), FED_C1M1 C_3 (CaptureTheFlag_O1
checks).

Result: no presence needs the per-pawn exit. Single-binding presences were not classified; the rule is generic and
covers them.

## Code (b445, dwmapi-b445-ad417436f2416129)

`mod/src/ubw/objectives.cpp`, card 596 block before `detour_c227_exited`, and one call in that detour after the
card 227 gate: `if (g < 0 && c596_hold_exit(presence, actor)) return;`.
- Host only, and only for an actor that is a player body (`c321_player_body`).
- The other roster pawns (and their card 300 pilot bodies) that are not the actor, not downed, and overlap the
  presence count as inside. If any is inside, the exit is held: the original (exit broadcast, LTOnExit reactions,
  the +0x309 once-exit clear) does not run, and a row is logged. A small table (16 presences, weak refs) counts
  held exits.
- When no other living player is inside, the original runs with that actor; if exits were held before, one row
  says so.
- A player who dies inside gives its own EndOverlap exit when the pawn goes, which then runs as the last one out.
- Gated presences (card 227) are unchanged: before they fire the exit is held as before; after they fire every exit
  passes through. That is outside this card.

## Prediction (written before the run, adds to the proof above)

- Held row text: "card596: host t=<ms> exit held on <presence>: <pawn> left, 1 player inside (once-exit N,
  enabled N)". The release row: "card596: host t=<ms> exit on <presence> by <pawn> runs: last living player out
  after 1 held exit(s), ...".
- A single player crossing any presence alone logs no card596 row (no held exits, nothing to report).
- The client runs no card596 code (host only).

## Run c595b447a (b447, scenario tools/scenarios/c595-c3m1-b447.txt), prediction

C3M1 BP_PlayerPresence_C_19 (ungated, enabled from the start). Both players inside; the host warps out first: one
row "card596: host t=<ms> exit held on BP_PlayerPresence_C_19: <host pawn> left, 1 player inside (...)". The client
warps out: "card596: host t=<ms> exit on BP_PlayerPresence_C_19 by <client pawn> runs: last living player out after 1
held exit(s) ...". inMonorailArea and the Final Wave read are not reached in this run (no Monorail stage), so that
part stays unproven.

## Closed (senior, 2026-11-21)

The hold and the last-out run are proven twice (c595b447c and c595b447d, held then runs on BP_PlayerPresence_C_19).
inMonorailArea and Final Wave follow from the held exit and were not driven, because there was no Monorail stage.
They ride the playtest. The fix ships in b448.
