# Card 288: an entry door closes only when every player is inside

Playtest 2026-11-13 (user is the client), user rule: "the CTF door shouldn't close at all until both the client(s)
and host are in the door".

## Trace (static, live client log `UBWCoop-menu-20261113-183002.log`, FED_C1M1)

Doors: `BP_Door_Sliding_Large_C_0` at (-3674, 9849, 298), the entry to the capture-the-flag yard;
`BP_Door_Sliding_Large_C_1` at (-4630, 9855, 285), the far door. Doors are host owned (card 161): the client
refuses its own door triggers and takes the host's `DoorOp`.

| line | client t | row |
|---|---|---|
| 9448 | 255625458 | client's own pawn at (-3793, 9746, 204): past door 0 |
| 9460-9463 | 255625647 | client's own trigger chain starts door 0 closing (`presence started DoorState 1 -> 3`), with the spawner calls of the same trigger (`PerformSpawn skipped on client`) |
| 9465 | 255626149 | `WARN card161: client-only DoorState 1 -> 3, set back to the host's 1`: the host did not close |
| 9568-9576 | 255652147-161 | client chain starts the close again; host row 3 arrives 14 ms later: `confirmed` |
| 9640 | 255656153 | door 0 closed (host row 0) |
| 9596, 9603 | 255654258-274 | `CaptureTheFlag_SO1` complete, door 1 opening |

So the host closes door 0 from a trigger that the client's server copy did not fire at 255625647 and that fired
26.5 s later, most likely when the host's own pawn entered. The close takes no account of where the other players
are: with the host first through, the door shuts with the client outside. The host log (copied to
`logs/playtest-*` when the playtest ends) names the host-side caller; read it before coding.

## Step 1 (static, no run; answers go in this card)

1. From the host log and the level data, name the chain that closes door 0: which trigger actor, which overlap
   filter (player 0 only, any player pawn, any pawn), which function reaches `Close Door`, and what else the same
   trigger starts (the spawner at `0000000050983740`, objective steps).
2. Say why the client's server copy did not fire it at 255625647 host-side (host t = client t - 173869236).
3. List every door, blocker or blocking volume in the campaign maps that a player trigger shuts behind the player
   (table: map, actor, trigger, filter). Card 161 tracked doors and card 095 collision ops are the starting lists.

## Fix (host; class fix for every row of the step 1 table)

1. A close (or blocker enable) that a player trigger asks for is held while any living player pawn (the host's and
   every client's server copy) is still on the outer side of the door or inside its `PawnBuffer` box. The inner
   side is the side of the pawn that fired the trigger. The door closes on the first tick that every living player
   is on the inner side and clear of the box. No timeout: it stays open until then.
2. What the same trigger starts besides the door (spawns, objective steps) runs when the trigger fires, as now,
   unless step 1 shows that leaves a player fighting through an open door into a wave; then say so in the card.
3. A dead or downed player does not hold the door; a player who leaves the session stops holding it.
4. NPC proximity opens and closes (`NPCDetectionCollider`) are unchanged.
5. Rows: `card288: host t=<ms> door <name> close held: <n> of <m> players inside (outside: <who>)` once per hold,
   and `card288: host t=<ms> door <name> closing: all <m> players inside after <ms> ms`.

## Predictions (one run, C1M1 capture-the-flag entry)

- Host enters alone: one `close held: 1 of 2` row, host `DoorState` stays 1, no `DoorOp` 3.
- Client enters: `closing: all 2 players inside`, `DoorOp` 3 then 0 on the client, both pawns inside.
- Client enters first, host second: same two rows with the roles swapped.

## Step 1 answers (junior, static; the host log is not available yet)

1. Chain (client side and level data): `BP_PlayerPresence_C_7` (`ASTPlayerPresence`, `LTOnEnter` holds
   `LR_Door_Toggle_C`; card 227 gates it, one gated presence on FED_C1M1) fires `OnTriggerEntered`; the reaction
   reaches door `_0`'s "Close Door" / "Toggle Door" **inside the Blueprint VM, not through ProcessEvent**: card 161
   logs the first ProcessEvent of each (door, function) pair, and the client log has no "Close Door" or "Toggle Door"
   PE line for door `_0` in that map before or at the DoorState 1 -> 3 at 255625647 (only `Recheck Blockers:
   refused` at 255625698, then the overlap events). A refused PE call would also have left DoorState at 1. So a
   hold in `c161_pe` would never see the close; the hold is on the DoorState change instead (As coded).
   Filter: card 227's gate counts every living player-character pawn overlapping the volume (any player, not player
   0 only). The same tick runs the spawner `0000000050983740` (`PerformSpawn` / 8 `QueueSpawnRequest`, skipped on
   the client): the presence's `LR_SpawnAI` side or a script bind. Waits for the host log: the host-side
   caller, and whether the spawner and objective steps hang off the same presence.
2. Why the host did not fire at 255625647: from code, the host's card 227 gate held the entry (the host's own pawn
   was not in the volume), while the client's own presence copy fired on the client (card 228 item 3: both
   gates see both pawns, but the client's view of the host pawn can differ). The host log's `card227: host ...
   held: 1 of 2` and `fired after hold` rows near host t 81756411 and 81782911 confirm or refute this. Waits for
   the host log.
   Note from card 161 run c: door `_0` itself never blocks a pawn in any DoorState (Blocker1, Blocker2 and the mesh
   stay CollisionEnabled 3, only PawnBuffer toggles); a player shut outside can walk through it. The user's rule
   is about the door closing at all, so the fix stands; it is not a lock-out fix.
3. Doors, blockers and blocking volumes that a player trigger shuts behind the player:

| map | actor | trigger | filter |
|---|---|---|---|
| FED_C1M1 | `BP_Door_Sliding_Large_C_0` (-3674, 9849, 298) | `BP_PlayerPresence_C_7`, `LR_Door_Toggle_C` | any living player pawn (card 227 gate: all of them) |
| FED_C1M1 | `BP_Door_Sliding_Large_C_1` (-4630, 9855, 285) | opened by `CaptureTheFlag_SO1` completion; no player-trigger close seen | none |
| FED_C1M1 | `BlockingVolume_1` (-3635, 9855, 225) | no op in the playtest log (card 095: CollisionEnabled 0 on FAST) | none |
| FED_C1M1 | `BlockingVolume_13` (8080, 3720, 135) | collision 1 -> 0 at map start (opens; never shuts) | none |
| FED_C2M1 | `BP_PlayerPresence_C_2` | `LR_SpawnAI_C`, no door reaction | not a door |
| FED_C3M1, C4M1, C5M2 | unknown | no card 227 build run yet; the soak's `card227: ... gated` rows and card 161 `tracked` rows settle it | |

FED_C3M2, FED_C4M2 and FED_C5M1 log no gated presence (card 228). Card 161 tracked doors in this playtest:
only the two FED_C1M1 doors. The As coded hold covers every tracked `BP_Door_Base` door on every map (class fix);
`ASTDoor` doors (card 095's OpenDoor/CloseDoor path) and blocking volumes are not covered: none is seen shutting
behind a player.

## As coded (junior, uncompiled)

Diff: scratchpad `c288.diff` against `snap288/` (door_c161.cpp, objectives.cpp, hooks_internal.h).

- `c288_presence_fired(pawn)` (host): card 227's `detour_c227_entered` and `c227_recheck` call it just before the
  original `OnTriggerEntered`; it stamps the time and the triggering player pawn's location. Every presence
  counts, gated or not; a pawn that is not a player character is ignored.
- `c161_poll` (host): a DoorState step from the open half (1, 2) to the closed half (3, 0), not from the mod's own
  call, within `kC288TriggerMs` (250 ms) of that stamp is a player-trigger close (`c288_hold`). The inner side is
  the sign, along the door root's local x (ComponentToWorld), of the stamped pawn location. A living player is
  inside when on that side and its capsule does not overlap the PawnBuffer box (BoxExtent times scale, card 126's
  overlap test). All inside, or none counted: the close runs as now. Otherwise "Open Door" runs at once (mod call),
  the Closing step is not sent, and the rows `close held: <n> of <m> players inside (outside: <pawn> (outer side |
  PawnBuffer), ...)` plus one `inner side +|- of local x` diagnostic line are logged once per hold.
- `c288_step` (host, from `c161_step`): every 250 ms a held door is counted again; when all are inside it calls
  "Close Door", logs `closing: all <m> players inside after <ms> ms` and consumes the stamp so this close is not
  held again. No timeout.
- A held door that something else closes outside the trigger window (an NPC, the level) ends its hold with a
  `hold ended` line; NPC opens and closes are otherwise unchanged. Dead or downed pawns and players gone from the
  roster are not counted.

Open, for the run or the senior:

- Assumption: the door's through-axis is its root's local x (door `_0`: the wall runs along y and the arena is at
  x < -3650). The diagnostic line prints the trigger pawn's distance along it; a wrong axis shows as the trigger
  pawn itself counted on the outer side.
- The host's close sets PawnBuffer to 3 before the reopen; card 095 sends that CollisionOp, so the client sees
  PawnBuffer 3 then 0 for one tick. Harmless (PawnBuffer does not block pawns), but it is a row pair per hold.
- Client side is not changed: the client's own presence copy still starts the close locally (card 270 holds that
  `presence started` step 500 ms, then sets it back as client-only). With the host holding, the client will show
  up to 500 ms of the closing montage start before the set back. That is card 228 item 3 (the presence's door
  reaction host-only); not coded here.
- Whether "Open Door" from Closing reverses the montage cleanly on BP_Door_Sliding_Base is not known statically.

Prediction changes: none to the card's three; add `card288: host door BP_Door_Sliding_Large_C_0 inner side - of
local x` (arena at negative x if the root's yaw is 0).

## Host log check (junior, 2026-11-13, `client/UBWCoop-menu-20261113-182510.log`)

Item 2 confirmed: host 81756436 `card227: host/standalone presence BP_PlayerPresence_C_7 held: 1 of 2 players inside
(entered by BP_PlayerCharacterMI_C_2147477502, hold 1)`, then 81782895 `fired after hold: 2 of 2 players inside`. The
host's gate held the entry; the client's own presence copy fired the close on the client. The client's DoorState is
set back to the host's by card 161, so the host-side hold of the As coded section governs both machines.

## Build b7 change (senior review of b6)

### (a) Why the client's presence fired with no card 227 gate row (client 255625647, host 81756411)

- The host pawn was at (-5889, 26983) and the client pawn at (-3793, 9746), about 17,000 uu apart. That is past the
  net cull distance, so the client had destroyed its copy of the host pawn. The copy was made again at 255631932;
  that is PostInitializeComponents #9, which logged only the "further events not logged" line because the cap is
  `kPawnWatchLogs` = 8 per map (the eighth copy was at 255536823).
- On the client, `c227_count` counts the own roster pawn plus `c43_client_other_pawns` (side kSideOther). With no
  host copy it counted living 1, inside 1.
- `c227_release` then returned true with holds 0. That path logs nothing, so the original `OnTriggerEntered` ran
  with no card 227 row: LR_Door_Toggle_C plus the guarded spawn requests at 255625647. The client's DoorState
  stepped 1 to 3 at 255625649.
- The presence was not fired earlier on the client: the round 19 digest showed 0 mismatches. The host held 1 of 2
  at 81756436 and fired 2 of 2 at 81782895.

### (b) Fix as coded (objectives.cpp, door_c161.cpp)

- The client's presence copy no longer runs door reactions. In `c227_run_edge`, a gated presence on the client
  (`g >= 0`) goes through `c288_client_edge`. That function copies the component's reaction TArray (data +0xC0,
  num +0xC8, on the UADLevelTriggerComponent at LTOnEnter +0x2E8 / LTOnExit +0x2F0) without LR_Door_Close_C,
  LR_Door_Open_C and LR_Door_Toggle_C. It points the array at the copy, calls the original edge (so the script
  broadcast and the non-door reactions still run), then restores the pointer and the count. The door is host
  owned, and the host's DoorOp is the only source of the client's door state.
- The row is `card288: client t=... presence <p> entered|exited by <pawn>: <n> door reactions left to the host's
  DoorOp, <m> run`, capped at 32 per map. A component with more than `kC227ReactionsRead` reactions logs a WARN
  and keeps only the ones that fit.
- The same path covers `detour_c227_entered`, `detour_c227_exited` and `c227_recheck`.
- `c227_count` on the client counts the host as living and outside when it sees no other pawn
  (`g_client_session && n == 0`), because then the host's copy is out of net relevancy. The client gate then holds
  instead of passing silently.
- `c270_hold` no longer holds a `presence started` step (host 1, client 3). With the client's door reactions
  removed, a started close on the client is no longer expected. If one appears, it goes to `c161_own` at once and
  is set back in the same tick with the client-only WARN. Only `montage settled` steps keep the 500 ms hold.
- The host hold (As coded section) is unchanged.

### Chain delay longer than kC288TriggerMs (250 ms)

On C1M1 the time from presence to DoorState was 2 ms. BP_PlayerPresence_C_7 (LR_Door_Toggle_C) is the only
door-gated presence in all logs so far. The LR reaction's delay field has not been read. If a later map's door
reaction has a delay over 250 ms, the host's `c288_hold` misses the close (it is keyed on time). The fix for that
case is to key on the reference: the presence's door reaction names the door actor, so match the stepping door
against it instead of the time window. Not coded, because no map has needed it so far.

### Gap

Doors that the level script drives (not a presence reaction) are not covered by `c288_client_edge`. The mod does
not relay conversations, which is why the client's original edge still runs for the script broadcast.

### (c) Prediction (replaces the client part)

The client enters C_7 alone while the host is elsewhere. On the client:

- DoorState stays 1.
- There are zero `client-only DoorState` WARN rows.
- There is no `card270 ... presence started` row.
- There is one `card288: client ... presence BP_PlayerPresence_C_7 entered by ...: 1 door reactions left to the
  host's DoorOp` row.

The host keeps its `held: 1 of 2` row.

## Closed (2026-11-17 13:16)
- Evidence: run `c448b186a` on b186 `6bdc1fb92f415071` matched prediction (c) of this card (client alone into
  `BP_PlayerPresence_C_7`); EXCEPTION 0 on both logs, digest 0 mismatches (junior's b186 report).
- Not done: no proof on maps other than the one the run drove.
- Reopen when: a playtest shows an entry door opening or a gate firing with one player still outside, or a
  `card227 ... held` row followed by the gate not firing with every player inside.
