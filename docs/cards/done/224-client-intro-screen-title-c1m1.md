# Card 224: Client press-to-start screen on FED_C1M1: title, prompt and Enter in the play path

Status: queued 2026-11-12, remainder of card 135. Static trace first; one run after the senior accepts it.

## Seen

- c135-r1b (FED_C1M1, test skip): the client's screen reads "Level Name" with the default image
  (T_Loading_Screen_Klendathu); the host's reads "BASIC TRAINING" with the camp image.
- c135-r2 (FED_C2M1): client title and image equal the host's; the client's IDs were 1/0 before the set.
- c135-r3 (FED_C1M1, play path): client IDs 0/0 before and after the set, equal to the host's; the client's screen
  was never shot (the brief video covered it, then the scenario stopped). So the chapter and level IDs are not the
  cause on FED_C1M1.

## Trace (each line verified or open)

1. What else `WB_Loading_Screen_Final_C` Apply Data and `USTMetaGame::GetUIDataForLevel` read besides the two IDs
   (a meta game object, a data table, a save slot), and which of them the client lacks on FED_C1M1 but has on
   FED_C2M1. Where the client's 1/0 on FED_C2M1 came from.
2. Whether the r1b shot was taken before Apply Data ran (the "screen prompt ready" line of r3 came 1.9 s after the
   pause; r1b shot time against its pause).
3. Order on the client in the play path: screen, brief video (WB_Cutscenes_C), prompt. Same as the host's order or
   not; what the client sees when the video ends.

## Run (one, play path, FED_C1M1)

Waits on `card067: dump \d+ in \S+ \(WB_Stack_C RequestGameUnpause\)`, never on a `pause:` line (capped at 16 per
map, `dropship.cpp:795`). The prompt wait needs WB_Cutscenes_C gone, not only the prompt-ready line. Shots on both
sides at the prompt; client Enter by `winkey`; client back in game.

## Done when

- The client's screen on FED_C1M1 shows the host's title and image, the client leaves it with Enter, digest 0
  mismatches, no new WARN class.

## Step 1 answers (static plus the c135 logs, senior, 2026-11-13)

- Item 1, settled. The screen's Construct (ubergraph 521 to 843) reads only `GetSTMetaGame`, the game instance's
  `GetChapterID` and `GetLevelID`, and `USTMetaGame::GetUIDataForLevel(chapter, level)` 0x515D290; a null result
  leaves the designer defaults ("Level Name", Klendathu image).
  - `GetUIDataForLevel` reads `USTMetaGame::GetMetadata` 0x515A6E0 (the class default object of the
    `USTMetaGameDataBase` subclass at meta game +0x30; the same on both sides) and bounds-checks the chapter
    against the array at +0x60 (count +0x68, element 0xF8) and the level against the chapter's array at +0xE8
    (count +0xF0, element 0x128). No save slot, no data table, no soft reference to load.
  - The IDs live at game instance +0x410 and +0x414. Their only native writer besides the two setters is
    `USTGameInstance::ValidateLevelChapterID` 0x510D660 (derives both from the level name), and its only caller is
    `ASTGameplayGameMode::StartPlay` 0x513A150. A client has no game mode, so nothing validates its IDs;
    `InvalidateLevelChapterID` 0x50F01A0 writes -1 to both.
  - The c135-r1b DLL did not set the IDs: its `IntroStart` and `GameStarted` rows carry no chapter or level
    (`c135-r1b-client.log`, t=135362913 and 135362919), while r2 and r3 carry `chapter 1->1 level 0->0` and
    `chapter 0->0 level 0->0`. So r1b's "Level Name" is the unset-ID case that card 135's later `c135_set_ids`
    (`intro_c135.cpp:292`) fixed. With the IDs equal to the host's (r3), the lookup has no other input that can
    differ.
  - The client's 1/0 before the set on FED_C2M1 (and 0/0 in r3): not decided statically; the likely source is a
    `StartPlay` of the client's own earlier standalone map load. It does not matter for the title, since the set
    overwrites both before `GameStarted`.
- Item 2, settled: not a timing fault. r1b: Construct at 135362920, shot `client-screen` at 135368887 (6 s later).
- Item 3 stays for the run (order of screen, brief video and prompt on the client in the play path).

Prediction for the run (current DLL, FED_C1M1, play path): the client row `GameStarted ... chapter 0->0 level
0->0` without WARN, and the client's shot at the prompt shows "BASIC TRAINING" with the camp image, equal to the
host's. If the shot still shows "Level Name" with that row present, the trace is wrong: next step is a log of the
`GetUIDataForLevel` return value on the client, no fix by guess.
No code is specced for the title. Code may come from item 3 only.

## Build 4 run c224-c1m1-b4d (FED_C1M1, DLL `5e2ce00acad8efa3`, 639 s, scenario passed, 0 ERROR, no VOID), senior verdict 2026-11-13

- Held in the log: client `GameStarted ... chapter -1->0 level -1->0` at 241003249 and `chapter 0->0 level 0->0`
  at 241469760 (after the rejoin), no WARN.
- Screen not proven, and this harness cannot prove it: the host continued alone (`IntroStart sent to 0 of 0
  client(s)`), the client joined after and got the join replay, so no press-to-start screen was ever up on the
  client. The wait for `screen prompt ready` timed out (240 s of dead time) and both `c224-*-prompt` shots show
  the camp in game. Item 3 (order with a client connected at the host's screen) goes to the playtest check list.
- Scenario: drop the 240 s prompt wait from `c224-c1m1-b4.txt` before any reuse.

## Closed (2026-11-17 11:47)

Closed under the backlog rule (2026-11-17). Items 1 and 2 are settled by the trace (the title and image come from the meta game's level data at Construct; not a timing fault; no code specced). Item 3 (order of screen, brief video and prompt with a client connected at the host's press-to-start screen) cannot be reached by the harness and is on the playtest check list in STATE. Reopen if the playtest shows the client's screen with the wrong title, or the prompt out of order.
