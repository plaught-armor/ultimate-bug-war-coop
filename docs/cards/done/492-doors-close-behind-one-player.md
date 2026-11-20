# 492: Script steps close doors behind one player (card 301 class H)

Status: open (written 2026-11-18, senior). Batch into the next build after b232; no run of its own.

## Finding

The card 301 audit (docs/reference/objective-coop-audit.md, maps FED_C3M2, FED_C4M2, FED_C5M1, FED_C4M1, FED_C5M2) found ten level
script sites that run `Close Door` on doors behind the players with no all-players test. None of them is under the
card 227 gate (no `LR_Door_*` / `LR_SpawnAI_C` reaction) or in `kC300Closures`. The second player can be shut out.

| Map | Site | Trigger | Doors |
|---|---|---|---|
| FED_C3M2 | `BP_PlayerPresence_C_52` enter @24446 -> @15107 -> Setup Objective 3_02 @9225 | presence | Gigantic C_8, C_7, C_10 (@9296-9368) |
| FED_C4M2 | `BP_PlayerPresence_C_73` enter @10933 -> @9746 | presence | Medium C_1, C_2, C_3, C_4 |
| FED_C3M2 | `BP_Switch_C_3` SwitchPressedDelegate @5595 | switch | Gigantic C_2, Medium C_1, Medium C_2 (@6057-6129) |
| FED_C5M1 | `BP_Switch_C_5` SwitchPressedDelegate @29072 -> @28419 | switch | Gigantic C_17, C_15, C_16 |
| FED_C4M2 | Plaza_SO2b flow @4530 -> @5066 -> @5339 | encounter event: `BP_EM_1FactionSetWaves_C_39` EncounterDelegate (wave set cleared) | Gigantic C_2 |
| FED_C4M1 | `BP_PlayerPresence_C_17` enter @14243 -> @13740 (if metroReady) -> @13363 | presence | Large C_3, C_2, Medium C_3, C_4 (@13447-13555) |
| FED_C4M1 | `BP_PlayerPresence_C_255` enter @19570 | presence | Large C_0, then FallZones C_1/C_5 on |
| FED_C4M1 | objective event `Add Stadium_SO2` @6586 (Stadium_SO1 kill count done) | objective event | Medium C_2, C_1, Gigantic C_1 (@6976-7048) |
| FED_C4M1 | `BP_PlayerPresence_C_70` enter @16245 -> @13797 | presence | none: EndLevel at once (class E, same fix) |
| FED_C5M2 | `BP_PanelSwitch_WithoutScreen_C_1` SwitchPressedDelegate @14353 | switch | Large C_4, C_1, C_5, C_6, C_7, C_8 |

## Fix

1. Presence sites: two rows in `kC300Closures` (mod/src/ubw/objectives.cpp), one group per map:
   `{"FED_C3M2_Scripting", "BP_PlayerPresence_C_52"}`, `{"FED_C4M2_Scripting", "BP_PlayerPresence_C_73"}`.
   Check first that the level package name of each presence is the `_Scripting` package (the BndEvt names in the
   decompile say so).
2. Switch sites (traced 2026-11-18): reuse the card 288 hold, no new mechanism. All three switch chains run
   `Close Door` in the same Blueprint call as SwitchPressedDelegate (C3M2 Switch C_3 @5595 -> @6057-6129, no latent
   node before the closes; C5M1 Switch C_5 @29072 `goto 28419`; C5M2 PanelSwitch C_1 @14353). Card 288's
   `c288_hold` (mod/src/ubw/door_c161.cpp) already holds any open-half to Closing step that `c161_poll` (every
   frame) sees within `kC288TriggerMs` (250 ms) of `c288_presence_fired(pawn)`, with the inner side taken from that
   pawn's position along the door's local x. Today only presences call it, so a switch close is never held.
   Fix: on the host, call `c288_presence_fired(presser pawn)` just before a switch press runs its original:
   - instant press: `c102_interact` (mod/src/ubw/player_c99.cpp:453; both the host's own press from
     `detour_interact` and the relayed client press from `c50_interact` pass through it), before `g_orig_interact`;
   - timed press: `detour_c133_completed` (hold.cpp, the host's own completion, before `g_orig_c133_completed`) and
     `c133_host_done` (the client's HoldDone, before its original completion call).
   Switch test: the target's class has a `SwitchPressedDelegate` property (`prop_find_prop`), so `BP_Switch_C` and
   `BP_PanelSwitch_WithoutScreen_C` both count whatever their native parent. The pawn is the interactor's
   `kComponentOwner` (for a relayed press, the server copy, which is the client's pawn on the host). One row per
   press: "card492: host t=<ms> switch press <target> (<class>) by <pawn>: card 288 trigger set".
   Side semantics: the hold waits until every living player is on the presser's side of each closing door.
3. Presence sites and card 288 (traced 2026-11-18): every presence site's close is synchronous too (C3M2 @9225 ->
   @9296-9368, C4M2 @9746-9854, C4M1 @13740 -> @13363 -> @13447-13555, C4M1 @19570), and card 227 calls
   `c288_presence_fired` on each enter that runs its original, so card 288 already holds those doors. The 0 card288
   rows in c244b231a-b233a and c245b229a are expected: the soaks move both players together, and the hold logs only
   when a player is outside. The kC300Closures rows still matter for the non-door effects: C4M1 C_70 (EndLevel) and
   C_255 (fall zones on) and the objective step of C3M2 C_52; the door rows (C3M2 C_52, C4M2 C_73, C4M1 C_17) are
   kept as belt and braces, not removed.
4. FED_C4M2 Gigantic C_2 (traced): the close follows the `BP_EM_1FactionSetWaves_C_39` wave-clear event, not a
   player action. Same shape as the C4M1 stadium (fix 6): hold the close until every living player is inside the
   area; the test volume is not named yet.
5. FED_C4M1 (2026-11-18 14:49 addition): kC300Closures rows `{"FED_C4M1_Scripting", "BP_PlayerPresence_C_17"}`,
   `C_255` and `C_70`. **Group key first:** today every presence of one map forms one group, and a player overlapping
   any of them counts as past. With three separate sites on C4M1 a player standing in C_17 would release C_255 and
   the level end. Add a site column to the table (C2M1 C_78/79/81 stay one site) and test "past" per site.
6. FED_C4M1 Stadium (objective event, not a presence): hold the three closes until every living player is inside the
   stadium. Candidate test volume: `BP_PlayerPresence_C_5` (its handler @5937 shows the outside markers again and
   clears `inStadium` @6119). Fixes 4 and 6 share one mechanism: an objective-event close held for an area presence.

## Fixes 4 and 6 traced (2026-11-18 18:52, senior, decompile lst/FED_C4M2_Scripting.umap.txt and FED_C4M1)

- Fix 4 needs no code. The FED_C4M2 Gigantic C_2 close @5339 is reached only from `BP_Switch_C_0`
  SwitchPressedDelegate @26080 (`goto 5046`: Add Plaza_SO2b, CompleteSub Plaza_SO2a, close C_2, siege on, checkpoint
  C_9). The `BP_EM_1FactionSetWaves_C_39` wave event @4530 opens C_2 (@4958); it does not close it. The card 492 fix 2
  switch press (`c492_switch_press`, door_c161.cpp:556) already sets the card 288 trigger for this close.
- Fix 6 (FED_C4M1 stadium): `Add Stadium_SO2` is the OnCompleted callback of Stadium_SO1 (the 4-kill count,
  @6135). It runs inside the host's Complete/Increment call (card 497: OnCompleted runs inside the call): @6586 checks
  the id is Stadium_SO1, adds Stadium_SO2, turns FallZones C_6 and C_7 on (@6910, @6943), and closes Medium C_2, C_1
  and Gigantic C_1 (@6976-7048) in the same call. The stadium volume is `BP_PlayerPresence_C_5`: enter @5729 sets
  `inStadium` and hides the outside markers, and exit @5937 clears it.
  Code: one table row `{"FED_C4M1_Scripting", "Stadium_SO1", "BP_PlayerPresence_C_5"}`. On the host, in `c491_enter`
  (before the original) for any kind whose objective name (name[0], or name[1] for a sub kind) is in the table: take
  the presence by name, `c227_count(presence, nullptr)`, and when `first_inside` is set call
  `c288_presence_fired(first_inside)`. Card 288 then holds the three closes until every living player is on the
  inner side. The presence can come from the card 227 detour, which records its weak ref when it sees the presence,
  or from a level actor scan by name; the junior picks whichever exists. One row per trigger: "card492: host
  objective <id> trigger from <presence>: <inside> of <living> inside, card 288 trigger set". When no player is
  inside, the row logs that and sets no trigger.
  Not held: FallZones C_6/C_7 going on. When a player is outside, the fall zones arm while the doors are held open.
  Whether a fall zone can kill a player outside the stadium is not traced; card it only if a run shows a death.

## Proof (written before the run)

- Presence rows: in the next C3M2 / C4M2 soak, the card300 "waiting" row at C_52 / C_73 while one player is
  outside, then the doors close once both overlap; host and client door states equal in the digest.
- Switch rows: the card492 press row, then the card288 "close held" row while one player is on the far side, and
  the card288 "closing: all N players inside" row after. Scenario: one player presses, the other stands outside
  the doors for 5 s, then walks in.

## Proof status (2026-11-19, senior, from existing logs)

- Switch rows: proved (b245, BP_PanelSwitch_WithoutScreen_C_1; STATE 2026-11-18). Fix 6 (C4M1 stadium): proved in
  c492b250a. Fix 4: no code.
- FED_C4M2 presence C_73: proved in c245b257a. Host 729727550: "closure BP_PlayerPresence_C_73 ... players past 1 of
  2, waiting", then "closed: players past 2 of 2" and "row sent to 1 of 1 clients"; Medium C_1..C_4 go 2 -> 3 -> 0.
- FED_C3M2 presence C_52: open. No FED_C3M2 run since c236b170a, before the kC300Closures row
  (objectives.cpp:3458). The only proof left is one c243 (FED_C3M2) soak past Setup Objective 3_02: a card300
  C_52 "waiting" or "closed" row, and Gigantic C_8/C_7/C_10 states equal on host and client.

Reopen when: a playtest or soak shows a player shut out by any of these doors.

## Closed 2026-11-19 (senior, c492pb274b)

FED_C3M2 presence C_52 is proved. Host 748851727: "players past 1 of 2, waiting". Host 748857365: "closed: players past 2 of 2", then "row sent to 1 of 1 clients". The junior reports the Gigantic door states equal on both sides and mismatches 0.
