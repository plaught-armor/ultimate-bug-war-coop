# Card 227: Presence triggers that start an event wait for every player

Status: queued 2026-11-12, from the user ("events like the CTF event in tutorial with the door need to wait for
multiple players to pass through instead of 1"). Static trace first (step 1 needs one logged run); no fix code until
the senior accepts the list from step 1. Budget after that: three runs.

## What the game does now (senior trace, static)

- Progress volumes are `ASTPlayerPresence` (`BP_PlayerPresence_C`). `OnBeginOverlap` leads to `OnTriggerEntered`,
  which fires the actor's `LTOnEnter` level trigger (`UADLevelTriggerComponent`); `OnEndOverlap` and
  `OnTriggerExited` fire `LTOnExit`. The trigger's reactions are the level script: `CompleteSubObjective`,
  `LR_Door_Close`, `LR_Door_Open`, `LR_SpawnAI`, `LR_ActivateGeneric`, `LR_Fallzone_Toggle`.
- The game is single-player: the first player pawn that overlaps fires the trigger. Nothing counts players.
- FED_C1M1 capture-the-flag: `CaptureTheFlag_SO0` is the presence `BP_PlayerPresence_C_7` (card 126 table). The
  first player through the door pair `BP_Door_Sliding_Large_C_0` / `_1` starts the event for both players.
- Card 065 found that `BP_PlayerPresence_C` OnEnter fires on each machine on its own; card 100 found the host's
  copy of the client pawn completes move-to objectives on the host. So today either player alone starts the event.
- `SetTriggerEventsEnabled` (0x05103780, flag at +0x30A) is the game's own switch for a presence; card 132 already
  carries that byte to the client.

## Rule

A presence whose enter trigger starts an event that shuts players out or starts a fight (door close, AI spawn,
encounter or match start) fires only when every connected, living player's pawn is inside the volume. A presence
that only completes a "go to" marker stays as it is (one player is enough).

## Do

1. One run on FED_C1M1 with `UBWCOOP_C65_LOG=1`, both players walked through every presence up to and into the
   capture-the-flag arena. From the log, list each `BP_PlayerPresence_C` with the reactions its `LTOnEnter` runs.
   Mark each "event" (waits) or "marker" (does not). Write the prediction for `_7` before the run. Sibling sweep in
   the same list: `ASTActivationVolume`, `ASTTeleporterVolumeBase`, `BP_TutorialPromptTrigger_C`, and
   `ASTEnemyPresence` (state for each: needs the rule or not, with the reason).
2. The rule must not need a per-map name list. Decide from the reaction classes on the trigger (read at
   `BeginPlay`, reaction array at component +0xC0, count +0xC8, card 065), or state why that cannot work.
3. Host only: a detour on `ASTPlayerPresence::OnTriggerEntered` holds the trigger for an "event" presence while any
   living player pawn is outside the volume, and lets it through on the overlap that completes the set. A player
   who leaves before the set is complete is no longer counted. A dead or disconnected player is never waited for.
   The client's own copy never fires an "event" presence; it takes the result from the host (objective rows, door
   rows, spawns already relayed).
4. While held, every player sees why: reuse the game's objective or prompt text path if one fits ("waiting for all
   players"); otherwise a capped log line only and a note here for a follow-up card.
5. No heap, no pointers kept across frames, table reset at `ubw_load_map_enter`, peak logged against its cap.

## Done when

- The step 1 list is in this card and accepted.
- One run, both logs: host player enters `BP_PlayerPresence_C_7` alone, the event does not start and the doors do
  not close; the client enters, the event starts once on both machines; digest 0 mismatches.
- Solo host (no client connected) plays as the unmodded game.

## Open for the user

- Stragglers are waited for, not teleported. If a pull-in is wanted after a time limit, that is a second card.

## Step 1 and 2 (junior)

### Log code (behind `UBWCOOP_C65_LOG=1`, log only)

- The card 065 trigger line (`c65: ... Reactions::Trigger ...`) now also prints `card227: owner name <full name>,
  reaction classes <num>[: <class>, ...]` (the first 6 classes). Its cap is now 240 per map (was 16, used up long
  before the capture-the-flag arena in c65a).
- `card227: <side> presence <name> at x y z BeginPlay: LTOnEnter reactions ...; script binds ...; LTOnExit reactions
  ...; script binds ...; once enter E exit X, triggers enabled T`: one line per `ASTPlayerPresence` at BeginPlay
  (shared counter with the trigger line). "script binds" is the level-script delegate at +0x2C8 / +0x2D8: the number
  of bindings and the first bound function's name. That delegate is broadcast before `LTOnEnter` fires, and most
  FED_C1M1 presences showed `reactions 0` in c65a, so the level script, not the reaction array, may do the work.
- `card227: <side> t=<ms> presence <name> entered|exited by <actor> (<class>) at x y z: begin|end, triggers enabled
  T`: around `OnTriggerEntered` / `OnTriggerExited` (its own counter, cap 240). Every line the level script writes
  for that presence (objectives, doors, spawns) logs between the begin and end lines.
- The three detour RVAs (BeginPlay 0x050DE0C0, folded with ASTAnimatedLight, ASTTeleporterVolumeBase and
  ASTJumpPadBase BeginPlay, so the dump checks `is_a ASTPlayerPresence`; OnTriggerEntered 0x050FA160;
  OnTriggerExited 0x050FA1A0) sit as local `offsets::Symbol`s in hooks_ubw.cpp, because tools/symbols.txt is
  outside this step's files.

### Prediction for `BP_PlayerPresence_C_7` (written before run c227a)

- Position (-4255, 9850, 340) (card 132 ext line), on the wall line between door `_0` (-3674, 9849) and door `_1`
  (-4630, 9855). Door `_0` opens (DoorState 0 -> 2 -> 1) when TacSupport completes and the script adds
  `CaptureTheFlag_O1` (c127-c1m1b host 5344, 5583).
- Dump: `LTOnEnter reactions 0` or a door reaction, `script binds` at least 1 (the level script's handler).
- Host walks in alone: between the host's `entered ... begin` and `end` lines, `CompleteSubObjective(CaptureTheFlag_O1,
  CaptureTheFlag_SO0)` (card049), then door `_0` closes (card161 `DoorState 1 -> 3 -> 0`) on the host and, by DoorOp,
  on the client, with the client still outside. Door `_1` is the arena side; it stays as it is or opens. The capture
  event (objective rows for the next CaptureTheFlag sub-objectives) starts on the host and the rows reach the client.
- The client's own copy of `_7` fires too when the host's pawn copy overlaps it on the client (card 065), so the
  client logs its own `entered` edge for the host pawn.
- The client then cannot walk in through the closed door `_0` (expected: no client `entered` edge from its own walk).

### Run c227a (void) and run c227b

c227a was void for a logged cause: `FAIL line 16: host wait log card227: host presence BP_PlayerPresence_C_7 at .*
BeginPlay: timeout=10`. The dump prints the side as `host/standalone` (BeginPlay runs before the side is set on
either machine), so the scenario regex never matched. Its frame windows were fine (542 to 585 per 10 s) and its
BeginPlay dumps are complete on both sides, so the table below comes from it. The scenario was fixed (`\S+` for the
side in the dump waits, `host/standalone` in the host edge waits) and rerun as c227b.

c227b (DLL 0a01700b14b9d6e5, FED_C1M1, FAST=1, `UBWCOOP_C65_LOG=1`, `UBWCOOP_C132_FX=1` on both sides): scenario
passed, exit 0. Host and client frame windows 583 to 586 frames per 10 s from 60 s on. Digest rounds 1 to 7: 0
mismatches. WARN: host 1 (attach), client 4 (attach, plus the 3 card161 lines below). No ERROR lines.

Host walks into `_7` alone (client pawn at (-3810, 9423), outside door `_0`):

- Host `card227: host/standalone t=142848844 presence BP_PlayerPresence_C_7 entered by BP_PlayerCharacterMI_C_2147480180
  (BP_PlayerCharacterMI_C) at -3921 10042 204: begin, triggers enabled 1`. Inside the begin/end window: `card111: wave
  budget STEncounterWaveBase (... BP_PWD_LaserTrooperBlue_C): base 12, difficulty 12, scaled 19 (2 players)`, `card112:
  activate STEncounterWaveBase: spawn list 19`, a `BP_AI_Trooper_Laser_Blue_C` spawn at (-9169, 17720, 105), the c65
  `Reactions::Trigger <LT_OnEnter>` line (reactions 1, `LR_Door_Toggle_C`), and `card095: SetCollisionEnabled on
  BP_Door_Sliding_Large_C_0 PawnBuffer 0 -> 3` sent to the client. Then `... : end, triggers enabled 0`.
- Door `_0` on the host: `card161: host t=142848847 door BP_Door_Sliding_Large_C_0 DoorState 1 -> 3` (closing), then
  `t=142852839 ... DoorState 3 -> 0` (closed, PawnBuffer 3). Earlier in the run it opened by the objective command:
  0 -> 2 at t=142767664, 2 -> 1 (PawnBuffer 0) at t=142771655.
- Door `_0` on the client: `card161: client t=142848862 ... DoorState 1 -> 3` and `DoorOp state 3 ... applied`, then
  `t=142852868 ... DoorState 3 -> 0` and `DoorOp state 0 ... applied`. The client's own copy of `_7` fired for the host
  pawn at the same moment (`card227: client t=142848844 presence BP_PlayerPresence_C_7 entered by
  BP_PlayerCharacterMI_C_2147479422 ... begin` / `end, triggers enabled 0`), and ran its own door toggle; card161 set
  those client-only changes back (`WARN card161: client t=142848847 door BP_Door_Sliding_Large_C_0 client-only
  DoorState 1 -> 3, set back to the host's 1`, and `t=142852855 ... 3 -> 0, set back to the host's 3`); the host's
  DoorOps then brought it to the same states.
- Door `_1` stayed DoorState 0 (closed) on both sides for the whole run.
- `CaptureTheFlag_SO0` was not completed by the entry: host `card066: host objective CaptureTheFlag_SO0: parent
  CaptureTheFlag_O1, complete 0` after the entry (the prediction expected a completion; the wave start is the event).
- So the event (wave of 19 and door `_0` closing) started with only the host inside and the client outside.

Client walks in after: its goto to (-3674, 10400) reached (-3747, 10373), then `goto actor BP_PlayerPresence_C_7`
reached (`dist 75`), client pawn at (-4314, 9875, 204), inside the volume. No `entered` edge on either machine for the
client pawn: `_7` has once-enter set and its triggers were already disabled (`triggers enabled 0`) by the host's entry.
The prediction that the client could not get in was wrong; the route it took past the closed door `_0` is not
established from the logs.

### Step 2: every presence on FED_C1M1

From the c227a BeginPlay dumps (identical on host and client). "Event" means entering starts gameplay (wave, door,
objective, activation); "marker" means it only drives an objective marker, prompt or nothing visible. Every bind is one
level-script handler `BndEvt__FED_C1M1_Scripting_<name>_K2Node...`. There are no `_14` or `_16`.

| Presence | Position | LTOnEnter reactions | Enter binds | Exit binds | Once enter/exit | Enabled | Kind |
|---|---|---|---|---|---|---|---|
| _0 | -584 13895 430 | 0 | 1 | 0 | 1/1 | 1 | script only, unknown |
| _1 | 11784 18691 64 (TacSupport_SO0) | 0 | 1 | 1 | 0/0 | 1 | marker (objective) |
| _2 | -528 15189 69 (Assault_SO0, SO1) | 0 | 1 | 0 | 1/1 | 1 | event by script (objective step) |
| _3 | -8588 13200 100 | 0 | 1 | 1 | 0/0 | 1 | script only, unknown |
| _4 | 5470 11502 69 | 0 | 1 | 0 | 1/1 | 1 | script only, unknown |
| _5 | 11997 12428 69 | 0 | 1 | 0 | 1/1 | 1 | script only, unknown |
| _6 | 17241 28454 1718 (Assault_SO2) | 0 | 1 | 0 | 0/0 | 0 | event by script (objective step) |
| _7 | -4255 9850 340 (CaptureTheFlag_SO0) | 1: LR_Door_Toggle_C | 1 | 0 | 1/1 | 1 | event (door + wave, seen in c227b) |
| _8 | -8360 20950 680 | 0 | 1 | 0 | 1/1 | 1 | script only, unknown |
| _9 | 17312 19694 1291 (Assault_SO3) | 0 | 1 | 0 | 0/0 | 0 | event by script (objective step) |
| _10 | 11864 12461 69 | 0 | 1 | 0 | 1/0 | 1 | script only, unknown |
| _11 | -8631 24011 520 | 0 | 0 | 0 | 1/0 | 1 | marker (nothing bound) |
| _12 | -306 24710 1192 | 0 | 1 | 0 | 1/0 | 1 | script only, unknown |
| _13 | 11858 12396 507 | 0 | 0 | 1 | 0/0 | 1 | exit only, unknown |
| _15 | 11896 20129 71 | 0 | 0 | 0 | 0/0 | 1 | marker (nothing bound) |
| _17 | 5311 13281 187 (ShootingRange_SO2) | 1: LR_ActivateGeneric_C | 1 | 0 | 1/0 | 0 | event (range lane) |
| _18 to _23 | x 5311, y 14811 / 16331 / 17871 / 19431 / 20981 / 22511, z 187 | 1: LR_ActivateGeneric_C | 0 | 0 | 1/0 | 0 | event (range lanes) |
| _24 | 5311 18971 476 (ShootingRange_SO0) | 0 | 1 | 1 | 0/0 | 1 | script only, unknown |

Sibling classes: `BP_TutorialPromptTrigger_C` `_0` to `_7`, `_9`, `_10` (no `_8`) is an `ASTPlayerPresence` subclass;
every one has LTOnEnter reactions 0 and binds `OnTriggerEnter_Event` / `OnTriggerExit_Event`: markers (prompts).
`ASTActivationVolume`, `ASTTeleporterVolumeBase` and `ASTEnemyPresence` were not dumped (open point).

Can the reaction-class rule separate events from markers without a name list? No, not alone. It catches `_7`
(`LR_Door_Toggle_C`) and the range lanes (`LR_ActivateGeneric_C`, which only say "activate something"), but the wave
that `_7` starts comes through the level-script handler, which the reaction array does not show, and twelve presences
have only a script handler, so their kind cannot be read from the reactions. A rule that needs no name list has to
look at effect: hold when the reactions include a door or spawn class, or when the host's first entry starts a wave
or closes a door inside the begin/end window (both are visible to the existing card111/card112/card161 hooks).

## Step 3 (junior)

### Rule (senior order)

A presence is gated when its LTOnEnter reaction array holds `LR_Door_Close_C`, `LR_Door_Open_C`,
`LR_Door_Toggle_C` or `LR_SpawnAI_C`. The array is read at BeginPlay and the presence is kept by its actor name in a
table that is reset at `ubw_load_map_enter`; the table's peak is logged against its cap. On FED_C1M1 only `_7` is
gated. For a gated presence, the OnTriggerEntered detour calls the original only when every living player pawn in the
world overlaps the presence; otherwise it returns and logs a capped line `card227: <side> presence <name> held: <n> of
<m> players inside`. The entry that completes the set calls the original with that entering actor. OnTriggerExited
of a gated presence that has not fired yet does not call the original; after it fires, both edges pass through.
The same code runs on the host and the client (each machine sees both pawns); there is no new network row. The gate
is always on. The edge and BeginPlay dump lines stay behind `UBWCOOP_C65_LOG=1`; the "held" and "fired after hold"
lines do not.

Recorded gap, not fixed: presences whose event comes only through an `LR_ActivateGeneric_C` reaction (the shooting
range lanes `_17` to `_23`) or only through the level-script delegate (twelve presences, step 2 table) are not gated.

### Trace (static, before code)

1. **What runs before the detour point.** `ASTPlayerPresence::OnBeginOverlap` (0x050F6880, args this,
   OverlappedComp, OtherActor, OtherComp) is the caller. It returns without doing anything when the enabled byte
   +0x30A is 0, when `(+0x5D & 0x18) != 0x10`, when the filter call `0x14134E620(this + 0x2A8, ...)` returns false,
   when `0x145209180(this)` returns true, when OtherComp is null or pending kill, or when OtherComp's `+0x97 & 0x20`
   is clear. Otherwise it calls vtable +0x758 (this, this, OtherActor), which is OnTriggerEntered. So OnBeginOverlap
   neither consumes once-enter nor disables triggers.
2. **What the original does.** `OnTriggerEntered` (0x050FA160) broadcasts the enter script delegate (+0x2C8,
   call 0x1437BE1E0), calls LTOnEnter's Trigger (+0x2E8, vtable +0x4E0), and then, when once-enter +0x308 is set,
   writes +0x30A = 0. `OnTriggerExited` (0x050FA1A0) is the same with +0x2D8, LTOnExit +0x2F0 and once-exit +0x309.
   Every effect (script event, reactions, disabling) is inside the original, so a held entry leaves the presence
   exactly as it was: still enabled, nothing triggered.
3. **No re-check on enable.** `SetTriggerEventsEnabled` (0x05103780) only writes the byte at +0x30A. It does not look
   at actors already inside.
4. **Second edge.** A pawn already inside gets no second entered edge when the other player arrives (begin overlap
   fires once per actor per stay). So the completing entry is the late player's own edge. On the host, the server
   copy of the client pawn raises that edge: c227b host log line 3427 shows the client pawn (`_2147477687`) entering
   `_3`. The client's own copy of the presence raises the edge for its own pawn too (c227b client log).

### Helpers and inside test

- Living players: `c43_roster(world)` (every PlayerController's pawn; on the client only its own) plus, on the
  client, `c43_client_host_pawn(world)`. A pawn counts when it is a `g_player_character_class()` and not
  `c43_is_down(pawn)` (Downed, Out or Dead on this machine's view). The world comes from `g_actor_get_world(presence)`.
- Inside test: `AActor::IsOverlappingActor(presence, pawn)` (0x037EA670), plus the entering actor itself. It is the
  game's own component overlap list, which the engine updates before it raises the begin-overlap event, and it covers
  a pawn that was already inside before the triggers were enabled. Edges kept per pawn would miss that pawn, and a
  distance would not match the volume's shape.

### Prediction for run c227c (written before the run)

Scenario `tools/scenarios/c227-presence.txt`, tag c227c. The client stays outside door `_0`; the host walks into `_7`.

1. Host: `card227: host/standalone presence BP_PlayerPresence_C_7 held: 1 of 2 players inside`. The client logs its
   own held line only if the host pawn's overlap reaches the client's copy (`card227: client presence
   BP_PlayerPresence_C_7 held: 1 of 2 players inside`, expected: the client sees the host pawn enter).
2. No `card111`/`card112` wave budget line on either machine after the held line; door `_0` stays DoorState 1 on
   both machines (the door list).
3. The client walks in. Host: the entered edge of `_7` by the client pawn, then `card227: host/standalone presence
   BP_PlayerPresence_C_7 fired after hold: 2 of 2 players inside`, the wave budget line once, door `_0` 1 -> 3 -> 0 on
   both machines with both pawns inside the arena.
4. Digest rounds: 0 mismatches. No new WARN classes against c227b.

### Code (DLL 2928654b3a96090d)

- `tools/symbols.txt`: `ASTPlayerPresence_BeginPlay`, `ASTPlayerPresence_OnTriggerEntered`,
  `ASTPlayerPresence_OnTriggerExited`, `AActor_IsOverlappingActor`; `mod/src/offsets.generated.h` regenerated by
  `python3 -I tools/pdb_rva.py gen` (four rows added, nothing else changed).
- `hooks_ubw.cpp`: the three presence detours moved from `c65_install` (log only) to the end of `kHooksUbw`; the
  local RVAs removed; `AActor_IsOverlappingActor` in the resolve table; `c227_map_end()` in `ubw_load_map_enter`.
- `objectives.cpp`: the gate table `g_c227` (32 entries, no heap, keyed by the presence's raw FName), the condition
  table `kC227GateClasses`, `c227_add_gated` (BeginPlay, logs `gated (LTOnEnter <class>), table <n> of 32`),
  `c227_count` (living players and how many the presence overlaps), `c227_release`, and the gated entered/exited
  detours. The "gated", "held" and "fired after hold" lines are always on (one counter, 64 per map); the edge lines
  and the BeginPlay dump are behind `UBWCOOP_C65_LOG=1`. `c227_map_end` logs `map end: gated presences peak <n> of
  32` and resets the table and the edge counter.

### Run c227c (results)

Scenario passed in 78 s, no failed or timed-out step, no try-waits. Frame windows 583 to 584 frames per 10 s.

- Held, host: `card227: host/standalone presence BP_PlayerPresence_C_7 held: 1 of 2 players inside (entered by
  BP_PlayerCharacterMI_C_2147480179, hold 1)` (host line 3680). Client: `card227: client presence
  BP_PlayerPresence_C_7 held: 1 of 2 players inside (entered by BP_PlayerCharacterMI_C_2147479423, hold 1)` (client
  line 3970): the client's own copy also saw the host pawn enter and held.
- While held (host lines 3680 to 4071, client 3970 to 4261): no `card111: wave budget` line and no DoorState change
  of door `_0` on either machine; door `_0` stayed DoorState 1 (last change 2 -> 1 at host 2759 / client 2733).
- Fired, host: `card227: host/standalone presence BP_PlayerPresence_C_7 fired after hold: 2 of 2 players inside,
  entered by BP_PlayerCharacterMI_C_2147477687 (1 held)` (host 4071), the entered edge of the client pawn (begin 4072,
  end 4088, triggers enabled 1 -> 0), `card111: wave budget STEncounterWaveBase (outer Package, data
  Default__BP_PWD_LaserTrooperBlue_C): base 12, difficulty 12, scaled 19 (2 players, ...)` once (4074). Client:
  `fired after hold: 2 of 2 players inside, entered by BP_PlayerCharacterMI_C_2147479709 (1 held)` (client 4261).
- Door `_0`: host 1 -> 3 (4089), 3 -> 0 (4153); client 1 -> 3 (4287), 3 -> 0 (4419).
- Digest: client rounds 1 and 2, mismatches 0.
- WARN classes against c227b: none new. Host: attach (1). Client: attach (1) and one `WARN card161: client ... door
  BP_Door_Sliding_Large_C_0 client-only DoorState 1 -> 3, set back to the host's 1` (c227b had three).

### Open points

1. The `_7` volume reaches south of door `_0`'s line: the client's entry edge was at (-3918, 9443), the door is at
   y 9849, and the client's goto ended at (-4267, 9781). "Every player inside the volume" can be true with a player
   still on the south side when the door closes. Whether that matters is the senior's call; the gate follows the
   game's own volume.
2. A player who is downed or dies while the others are inside gives no new edge, so a held presence fires only on the
   next entry by someone.
3. A player in a mech has a pawn that is not a player character on the host's roster, so they are not waited for. On
   the client, `c43_client_host_pawn` finds the host's player character, which may still exist while the host pilots;
   then the client's copy waits for it and the host's does not.
4. The client's own presence copy still runs its reactions locally when its set completes (the one client-only
   DoorState WARN above); the card 132 X row then disables the triggers byte from the host.
5. The `map end: gated presences peak` line logs only at the next map load; run c227c quit on FED_C1M1, so it did not
   appear.
6. The gap recorded above: `LR_ActivateGeneric_C` lanes and script-only presences are not gated.
7. The scenario's shot names come out doubled (`c227c-c227c-host-end`: the driver adds the tag as a prefix).

## Senior read-out (2026-11-12, accepted)

- Diff against the pre-card snapshot: only `objectives.cpp`, `hooks_ubw.cpp`, `hooks_internal.h`, `symbols.txt`,
  `offsets.generated.h`, the scenario and this card. Gate code read; rule as written above.
- Open point 1 is not a lock-out: door `_0` stands in the wall at x -3650 with its through-axis along x; the client
  fired the gate at x -3918, past the door plane, and the client's end screenshot shows both players in the room.
- Regression soak with DLL `2928654b3a96090d`: c227-c1m1 (502 s) and c227-c2m1 (524 s) passed; digest 19 and 17
  rounds, 0 mismatches; 0 ERROR lines; no WARN class that earlier runs do not have (one card 152 ragdoll fallback on
  C2M1, card 207's class); all timed-out try-waits are the dead `card137: host pose freeze count=3` waits.
  FED_C1M1 gates one presence (`_7`, peak 1 of 32); FED_C2M1 gates none.
- Play build `2928654b3a96090d`, `dist/UBW-Coop-20261112-1155.zip`.
- Open points 2, 3, 4 and 6 go to card 228. Point 5 (peak line only at the next map load) and 7 (doubled shot
  names) are accepted as they are.

