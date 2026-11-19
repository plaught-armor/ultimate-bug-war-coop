# 260 Client gets no mission-end screen on FED_C2M1

Found by card 241 run 10 (`logs/c241-c2m1-r10-*`). Visible sync gap, not a play blocker: the client travels
to FED_C3M1 with the host.

## Evidence

- Host: `EnterDropship` 194528441, `LOS_O05` complete 194528438, `WB_TrooperLevelEnd` at 194561958.
- Client: `LOS_O05` replayed 194528476, own pawn hidden at 194528476, then no `WB_TrooperLevelEnd`, no
  `SetInputMode UIOnly`, no `EndLevel` row until the travel (in FED_C3M1 at 194664486). For about 135 s the
  client player has a hidden pawn and no end screen.
- FED_C1M1 (`logs/c240-c1m1-r8-*`): the client shows `WB_TrooperLevelEnd` at 166937077, 12 ms after the
  host, from its own level script. So the C1M1 end runs on both sides and the C2M1 end (dropship boarding,
  `PlayerEntering`, `End Level` 32.5 s later) runs on the host only.
- The mod has no `EndLevel` relay.

## Step 1 (static, no run)

Trace `STGameInstance::EndLevel` callers per Trooper map (C1M1 to C5M2): which run on the client by
themselves and which hang on a host-only event. Find where the stats shown by `WB_TrooperLevelEnd` come
from on a client.

## Step 2 (fix by class)

One relay: when the host's `EndLevel` runs, every client runs its own `EndLevel` once (no double screen on
maps where the client's script already does it). The client's screen stays until the host's Next travels.

Proof: c241 scenario tail: `WB_TrooperLevelEnd` on both sides within 1 s; c240 tail: still exactly one
widget on the client. Files: decided in step 1. Budget: 1 run with both tails batched.

## Doubt, 2026-11-13

The client shots of 241 run 10 and 242 run 6 show the client still on the loading card ("PRESS ANY KEY TO
CONTINUE") at the run end: the scenarios dismissed it only on the host. The missing client level-end screen
and the client's dead E press may be that test fault. Re-check with the re-run of 241 before any work here.

## Step 1 answers (static plus existing logs, senior, 2026-11-13)

- `USTGameInstance::EndLevel` (0x50E40C0, 0xD0 bytes) has four callers: `execEndLevel` (Blueprint, 0x505D646),
  `ASTPlayerTransportDropship::EndCurrentLevel` (0x51C3D50, call at +0x53), `ASTHUDGameplay::EndStreamingPause`
  (+0x76) and the benchmark tool. It is the only caller of `ASTHUDGameplay::ShowLevelEndScreen` (0x506DE70), which
  shows `WB_TrooperLevelEnd` for the local player. Nothing in it is replicated.
- `EndCurrentLevel` is reached only from Blueprint (`execEndCurrentLevel` 0x508B200), after
  `ASTPlayerTransportDropship::OnInteraction` -> `EnterDropship` (0x51C56C0). The mod runs that interaction on the
  host only (`c102_interact` leaves the transport dropship to "the player"), and the client's own `EnterDropship`
  is skipped by the card 015 detour (`collision: client EnterDropship ... skipped`). So on every map that ends by
  boarding the dropship, the client has no caller of `EndLevel`.
- Per map, from the level script name maps: C1M1 and C3M2 call `EndLevel` from the level script (runs on both
  sides; c240 shows the client widget 12 ms after the host). C2M1 ends through `BP_PlayerTransportDropship_EndLevel`
  (host only). Correction from the bytecode: the C2M1 script also has its own `End Level` function (ubergraph 23073,
  `GetSTGameInstance(self).EndLevel()`), reached as well from the `PlayerSkippedFlight` event of
  `BP_PlayerTransportDropship_C_4` (23147); the relay below covers both callers. C4M2 has `EndLevelDelayed`. C3M1, C4M1, C5M1 and C5M2 have no end-level name in the script, so they
  end through an actor (dropship class) and are expected host only as well.
- The doubt is settled against a test fault: the client was in the map (pawn hidden at boarding, FX rows playing at
  194561974) and has no code path to the screen. The loading card is a separate scenario fault.
- Stats on the client screen: not traced. `EndLevel` reads them from the local game instance; the proof run shows
  what the client screen holds. A stats gap is a new card, not this one.

## Fix spec (step 2)

One class fix for every map, no per-map list.

- New symbol `USTGameInstance_EndLevel` 0x050E40C0 in `tools/symbols.txt` (regenerate `offsets.generated.h`), one
  hook-table row in `mod/src/ubw/hooks_ubw.cpp`.
- New `ChanOp::EndLevel`, 1 argument (unused, 0), in `mod/src/ubw/chan_ops.h` and `chan_ops.cpp`.
- Detour, in `mod/src/ubw/objectives.cpp` next to the objective relay:
  - Host: call the original, then send `ChanOp::EndLevel` to every client. Log `card260: host EndLevel, sent to N`.
  - Client: set a per-map flag `ended`, call the original. Log `card260: client EndLevel (own script)` or
    `(relay)`.
  - Client receive: if `ended` is clear, call `EndLevel` on the local game instance (`GetSTGameInstance`), else log
    `card260: client relay dropped, already ended`. Clear the flag on map load.
- Do not touch `EnterDropship`, `c102_interact` or the pause path.

Predicted rows: C2M1 tail: host `card260: host EndLevel, sent to 1`; client `card260: client EndLevel (relay)` and
`Widget Stack[0] = WB_TrooperLevelEnd_C` within 1 s of the host's. C1M1 tail: client `(own script)` then
`relay dropped, already ended`, exactly one widget. Budget: 1 run per tail, batched with other cards on those maps.

## Run B (c241-c2m1-b1, build 3), senior verdict 2026-11-13

C2M1 tail held: host `card260: host EndLevel, sent to 1` at 234140760; client `card260: client EndLevel (relay)`
at 234141677 and `WB_TrooperLevelEnd_C` opened in the same millisecond. C1M1 tail (`own script` then `relay
dropped`) still owed; it rides in the card 224 play-path run.

C1M1 tail held in build 4 run c224-c1m1-b4d (FED_C1M1, DLL `5e2ce00acad8efa3`, 639 s, scenario passed, 0 ERROR, no VOID): host `card260: host EndLevel, sent to 1` at 241523266; client `client EndLevel (own
script)` then `client relay dropped, already ended`; one `WB_TrooperLevelEnd_C` on the client stack. Shot `client
c240-client-end` viewed (in game before the end screen, tag and objective shown). Card closes.
