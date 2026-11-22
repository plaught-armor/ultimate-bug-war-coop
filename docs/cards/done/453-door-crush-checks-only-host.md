# Card 453: door crush checks only the host player

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: open, low priority, after playtest. Found by static trace 2026-11-16 (SYNC_AUDIT row `ASTDoor::MoveDoorToNewPostion`).

## Fault

`ASTDoor::HandleCharactersInMovePath` (VA 0x1450ec950) has two parts.

1. A `SweepMultiByChannel` over the door mesh bounds. Every `ASTPlayerCharacter` and `ASTCharacter` it hits is handled (`IsActorCrushable`, block flag in r12b), so a client pawn in the path blocks the door the same as the host. This part is correct for co-op.
2. The crush step (0x1450ed056 to 0x1450ed247) takes `GetPlayerCharacter(world, 0)` only and tests `IsOverlappingActor` against the door's crush volume (door+0x360). When the host overlaps: a door with the damage flag (door+0x355) applies `USTDoorDamageData` damage once per overlap (latch door+0x4E0), otherwise a crushable host gets `InstaKillTarget`. NPCs in the volume are killed by the loop after it (`GetOverlappingActors`, 0x1450ed284).

A client pawn in the crush volume takes no door damage and is never killed. The host and bugs are.

## Fix (not written)

Host hook on `HandleCharactersInMovePath`: after the game's call, for each remote pawn that overlaps door+0x360, apply the same result the host would get (door damage through `USTCombatUtilityLibrary::ApplyDamageAtLocation` with the door's damage data, or `InstaKillTarget`), with a per-door, per-player latch. Do not call the game function a second time with a swapped player: it repeats the sweep and the NPC kills and clears the host's latch at 0x1450ed240.

Open before coding: which doors on the Trooper maps have the crush volume and which have the damage flag (read door+0x355, +0x357, +0x360 on each `ASTDoor` at map load, one diagnostic row per door class).

## Proof

Host row per crush on a client pawn, client health drop or death in the client log, screenshot on the client.

## Closed 2026-11-16 20:29: no ASTDoor on any Trooper map

- b163 scan row (`card453: host ... door scan: 0 class(es) ... (no ASTDoor)`) on FED_C1M1, FED_C2M1 and FED_C4M1.
- Every `card321: host map ...` row in `logs/*-host.log` reads `doors 0, doors with crush 0` (card 321 counts
  `ASTDoor`): FED_C1M1 204 rows, C2M1 102, C3M1 72, C3M2 63, C4M1 64, C4M2 50, C5M1 53, C5M2 47.
- The Trooper maps' doors are `BP_Door_Base` children (`BP_Door_Sliding_Large_C`), which derive from
  `BP_KillableBuilding_Base`, not `ASTDoor` (card 161).

The `ASTDoor` crush step never runs on these maps, so there is nothing to fix for the Trooper playthrough. Reopen
with the bug campaign if a `card321` row there shows `doors with crush` above 0. The b163 scan stays as a one-row
diagnostic. Not traced: whether the `BP_Door_Base` Blueprint has a crush of its own.
