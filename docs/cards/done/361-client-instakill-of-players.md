# 361: Client runs InstaKillTarget on player pawns (fall zone, door, hazard crush, dropship crush, teleporter, BP exec)

From card 360's sibling sweep (table of the 18 direct `USTCombatUtilityLibrary::InstaKillTarget` call sites in card
360). Five native callers and the Blueprint exec can kill a player pawn in a client session with no authority test.
Rule: the host is the only writer of a player death; the client learns it from the Down frame (card 043/044 path).

## Fix (one manager for the class)

One always-installed detour on `InstaKillTarget` (RVA 0x05241580):

- Client session, target is a player-class pawn (own pawn or a proxy), call not made by the mod: the original is not
  run; one plain `card361` row per skip with the pawn, its side and the caller (`exe+0x...`, named from the card 360
  table when known, "caller not traced" with WARN otherwise).
- Client session, any other target: the original runs; one log-only row per distinct caller (capped), so client-side
  kills of non-player actors are measured before anything is decided about them.
- Host: unchanged, no row.
- Card 360's `KillOwningActor` detour becomes log-only (its expiry rows stay); the skip lives in this one place. The
  mod's own calls (`g_insta_kill` from the test commands and card 023/025) go to the trampoline and are not skipped.

## To state before the run

For each of the five native callers: does the same overlap or event also run on the host for that pawn (server copy
or host-own), so that the host's kill exists to replace the skipped one? A caller whose event runs only on the
client would leave the player alive; name it and stop before building if any is found.

## Prediction (1 run, FED_C2M1 or the map where a fall zone is nearest to a spawn; fall zone is the cheapest caller)

- Client pawn enters a fall zone: host kill of the server copy, client `card361` skip naming the fall-zone caller,
  client downed or dead once through the Down frame, shot.
- Host pawn the same: client `card361` skip for the proxy, death on both screens.
- 0 `WARN card361` (no untraced caller), 0 own-pawn HandleDeathEvent WARN, 0 new WARN kinds.
- The other four callers are covered by the same hook; proven by call-site table, not by run.

## Pre-build check (junior, 2026-11-14, exe disassembly): does the host run each caller for the same pawn?

Stopped before building, per the order: one caller runs only on the client for the client's own trooper.

| Caller (InstaKillTarget call) | What picks the target | Host kills the client's trooper too? |
|---|---|---|
| `ASTFallZonesBase::OnBeginOverlap` (0x5161F78, 0x51626AA) | the overlapping actor | Yes. Per-actor overlap; the host's overlap fires for the server copy (card 338 lets the host's run and skips the client's for other pawns). |
| `ASTDoor::HandleCharactersInMovePath` (0x50ED239/319/4FD) | part 1 sweep hits, part 2 `GetPlayerCharacter(0)` | Yes. Card 321's `detour_c321_door_path` runs part 2 on the host in the copy's scope when a copy is in the crush volume. (No doors on FED_C1M1 or FED_C2M1.) |
| `ASTProceduralDropship::ShuntAndCrushBelow` (0x51DD9BA, 0x51DDAA7), from `TickDescent` only | `GetOverlappingActors` filtered to `ASTNonPlayableCharacterBase`, both loops | Not a player caller: both kills come from the NPC-filtered lists. Per actor, runs wherever the descent runs. |
| `ASTTeleporterVolumeBase::StartPlayerTeleport` (0x5170C90), from `OnBeginOverlap` (exe+0x5162F1A) and `ActorFullyLoaded` (exe+0x514AA5F) | the teleported character; kill of what stands at the destination | Yes for the overlap path (per actor, the host's overlap fires for the copy). The `ActorFullyLoaded` path is a save load on each side. No teleporters on FED_C1M1 or FED_C2M1. |
| `ASTHazardousZoneBase::CrushActor` (0x515270A), from `TickMoveToNewPostion` only (exe+0x5174382, exe+0x5174406) | first call: `GetPlayerCharacter(0)` + `IsOverlappingActor(zone+0x320, player 0)`; second call: overlapping `ASTNonPlayableCharacterBase` | **No.** On the host, player 0 is the host's trooper, so a moving hazard never crushes a client's server copy. Only the client's own process crushes its own trooper (its player 0), since card 132 replays the hazard's move on the client. Card 321's fix names hazards under "is any player in the way", but only the door part is built (`player0_c321.cpp`). FED_C2M1 has 32 hazard zones and FED_C1M1 has 1. |

With card 361's skip as specified, a client trooper caught by a moving hazard would never die: the client skips the
crush and the host never makes one. The fix needs the host half first: on the host, after `TickMoveToNewPostion`,
crush each server copy that overlaps zone+0x320 (the card 321 rule, the same shape as the door), so the host's kill
exists to replace the skipped one. Waiting for the senior's call before building.

## Senior call (2026-11-14): build the host hazard half and the client skip together in b46

### TickMoveToNewPostion branch conditions before the first CrushActor (exe disassembly, 0x05173AA0)

- No early return: the branches at exe+0x5173C3E and exe+0x5173C89 rejoin at exe+0x517422D, so every call reaches
  the gate.
- Gate: `cmp byte [zone+0x315], 0` at exe+0x517430A (`bCrushCharactersInPath`); zero jumps to exe+0x517442E, past
  both crush paths.
- Path 1: `GetWorld(zone)`, `GetPlayerCharacter(world, 0)`, `IsOverlappingActor(zone+0x320, p0)` at exe+0x517434A,
  then `CrushActor(zone, p0, &dir)` at exe+0x5174382. `dir` is the normalized negation of the HazardousZone
  component's translation minus the new location. It lives only in xmm8/6/7 and at the caller's rsp+0x30/0x38/0x40,
  so it cannot be read after the original returns.
- Path 2: `GetOverlappingActors` at exe+0x51743C5 with the NPC class filter, then `CrushActor` at exe+0x5174406.
- `CrushActor` (0x05152020) has no repeat guard: capsule, `SweepSingleByObjectType`, then InstaKill at exe+0x515270A.

### Hook choice: the acting-player scope around the whole tick, not a call-site wrapper

The mod has no call-site patch infrastructure. The senior's alternative was to wrap the `GetOverlappingActors` call
at exe+0x51743C5 and append copies, or to call CrushActor with the direction from rsp+0x30. Both need a new
mid-function patch kind.

The smaller option reuses card 321's door pattern. On the host, when a server copy overlaps zone+0x320 and the gate
byte is set, the original runs once inside `c102_enter(copy, kC102Overlap, zone)`. Card 099's remap then answers
`GetPlayerCharacter(world, 0)` with the copy, so the game's own gate, overlap test, direction and CrushActor call run
for it. Nothing is approximated, and the move itself runs once per tick as before.

- Repeat guard: CrushActor has none, so a (zone, copy) pair crushed by the host is recorded. The copy is not offered
  to that zone again until it leaves the zone's component or the roster.
- Host and copy both in the zone: ticks alternate between the copy's scope and the plain original (the host as
  player 0). The host's own crush is at most one tick later than today.
- Kill detection: the InstaKill detour sees the scoped copy as target, with return address exe+0x515270F.

Rows:
- `card361: host t=<ms> hazard <zone> crushed <pawn> (server-copy)`: one row per host crush of a copy.
- `card361: <side> t=<ms> hazard <zone> (<class>) moves, crush <0|1>`: once per zone per map, the first move tick.

This completes card 321's "any player in the way" hazard half (doors: player0_c321.cpp; hazards:
insta_c361.cpp). It rides card 361 and has no card number of its own.

The client's own-trooper crush in the card 132 replay goes through the same InstaKill detour and is skipped. Its row
names `ASTHazardousZoneBase::CrushActor`, and the death arrives by the Down frame.

The host's own pawn in a hazard follows the unchanged player 0 path. On the client the replay crushes only the
client's own player 0, so the host's proxy gets no crush and no skip row.

### No hazard step is drivable: no FED hazard crushes

- The `ASTHazardousZoneBase` constructor writes 0 to +0x315 (`mov %r15b,0x315(%rdi)` at exe+0x514503F, with r15d
  zeroed at exe+0x5144FD6).
- None of the 8385 extracted .uasset/.umap files (all 83 maps, every BPHazardousZone child) has
  `bCrushCharactersInPath` in its name table. A serialized override or a Blueprint write would put that name there.
- `BPHazardousZone` sets the move properties (MoveEndPoint etc.) but not crush. `BPHazardousZone_Shunt` (32 on
  FED_C2M1, inside BP_HordeZone) overrides neither.

So path 1 never fires in the shipped game, and the host half is a latent guard. The b46 `moves, crush` rows are the
runtime check: any moving zone prints crush 0.

### Correction: the fall zone gives no card361 row for troopers

Card 338: `ASTFallZonesBase::OnBeginOverlap` InstaKills only when the ASC lacks `Player.IsPlayer`, and troopers are
repositioned. The prediction above ("client card361 skip naming the fall-zone caller") was wrong for players.

### Predictions, b46 (3f7559c146eb5a81, tools/scenarios/b46-c361-c2m1.txt, GOD=0)

1. FED_C2M1 fall zone (card 338 point, LOS_O03_SO01), both pawns: host `fall zone hit slot 0` and `slot 1`, two `card338 ... repositioned to` rows, 0
   `card361` rows on either side, both alive, shots.
2. FED_C2M1 out-of-bounds expiry, client turn:
   - host `card360: host ... (server-copy): KillOwningActor runs` and a Down row;
   - client `card360: client ... (client-own): KillOwningActor runs`, then `card361: client ... InstaKill of <pawn>
     (client-own) by KillOwningActor (out of bounds expiry): skipped`;
   - the client downed once by the Down frame, shot.
3. Host turn:
   - host `card360 ... (host-own): KillOwningActor runs`;
   - client `card360: client ... (other) ... runs` and `card361 ... (other) by KillOwningActor (out of bounds
     expiry): skipped`;
   - death on both screens, shots.
4. Whole run:
   - 0 `WARN card361`, 0 `card361: host ... crushed`;
   - every `card361 ... moves` row reads `crush 0` (or no rows: no zone moves on this path);
   - non-player `card361 ... runs` rows only from NPC callers;
   - 0 new WARN kinds against b45.

## b46 results (dll 3f7559c146eb5a81, 2026-11-14)

- Run 1 void, junior error: the fall zone block warped to a FED_C2M1 point while on FED_C1M1; both pawns fell to the kill floor and the drive stopped at line 23. Log `logs/b46r1.drive`. Hooks installed on both sides, FATAL 0, crash 0.
- Run 2 (`logs/b46.drive`, `logs/b46-{host,client}.log`): scenario passed, drive rc 0, FATAL/ERROR 0, crash dumps none.
  - Prediction 1 (fall zone, FED_C2M1): host `fall zone hit slot 0` and `slot 1`, two card338 repositioned rows, 0 card361 rows. Held. Shots `host-b46-c361-fall-host`, `client-b46-c361-fall-client`: both standing on the rock after the reposition.
  - Prediction 2 (client out of bounds expiry): host `card360: host t=347558709 out of bounds expiry on BP_PlayerCharacterMI_C (server-copy): KillOwningActor runs`, then `card294: host ... Down Downed for pawn id 233993 (slot 1)`; client `card361: client t=347558712 InstaKill of BP_PlayerCharacterMI_C (client-own) by KillOwningActor (out of bounds expiry): skipped; the host's kill comes by the Down frame`. Held. Shot `client-b46-c361-client-expiry`: client downed (red screen, health 0).
  - Prediction 3 (host expiry): host `card360: ... (host-own): KillOwningActor runs`, `Down Dead ... (slot 0)`; client `card361: client t=347573327 InstaKill of BP_PlayerCharacterMI_C (other) by KillOwningActor (out of bounds expiry): skipped`. Held. Shots `host-b46-c361-host-expiry`, `client-b46-c361-host-expiry-cview`: both on the mission-fail menu.
  - Prediction 4: 0 `WARN card361`, 0 host crushed rows, 0 `moves, crush` rows (no hazard zone moved on the maps visited; FED_C2M1 has 32 hazard zones, none moving in this path). One non-player row: `card361: client InstaKill of BP_AI_WarriorBug_C by exe+0x5192EFE: runs` (NPC caller not in the table, original ran as intended).
  - WARN diff against b45r2: no new class; the changed rows are ragdoll/gib families (card148/152/191) from the extra bug kills in the fall zone block.
- Hazard half of card 321 completed as a latent guard: no shipped asset sets `bCrushCharactersInPath`, so no hazard step was drivable.

## Senior review (2026-11-14): accepted

Checked: game2 dll hash 3f7559c146eb5a81, lane 1 still d20f47aa1b0383a5; read `insta_c361.cpp` whole; log rows
in `logs/b46-{host,client}.log`; shot `client-b46-c361-client-expiry` (red screen, health 0).

- Client expiry: host KillOwningActor at 347558709, host Down Downed (slot 1) at 347558710, client skip at 347558712.
- Host expiry: host KillOwningActor at 347573291, Down Dead (slot 0) and Down Out (slot 1) at 347573291, client skip
  of the proxy at 347573327.
- 0 `WARN card361`; WARN totals host 8 (b45r2: 12), client 13 (b45r2: 13).
- My own trace of the branch agrees with the junior's: gate byte zone+0x315, path 2 filter is
  ASTNonPlayableCharacterBase. The scope hook (player 0 swapped to the copy for one whole tick, alternating with the
  host when both overlap) is accepted in place of the call-site wrapper I suggested; it handles one copy per tick,
  which is enough for two players.

Not proven by run: the host crush row and the hazard caller skip (no shipped asset sets bCrushCharactersInPath, so
the host half is a latent guard), and every skip caller other than the out-of-bounds expiry.

Open risk, carded as 362: a client skip relies on the host killing the same pawn. For the callers in the pre-build
table that is traced; for `execInstaKillTarget (blueprint)` and any untraced caller it is not, and a miss leaves the
client's trooper alive with no row saying so.
