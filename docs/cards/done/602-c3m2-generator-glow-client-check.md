# Card 602: C3M2 generator glow on the client (level script material parameters)

## Finding

The card 301 FED_C3M2 full pass (2026-11-22) found that the level script changes material parameters, and no relay in mod/src covers them.

- `GeneratorsOnCheck` (@26829-27788) makes 12 `SetScalarParameterValueOnMaterials` calls, on StaticMeshActor_12/16/8/9/2:
  - "Emissive Strength" is set to 5.0 or 0.0;
  - "TimeSpeedMultiply" is set to 0.25 or 0.0.
- ReceiveBeginPlay runs it on both machines (@7248, through @7208), so both start dark.
- The press of Switch C_2 runs `Plant_Generator_Reset` @14743, which sets GeneratorsOn, calls GeneratorsOnCheck and completes PowerPlant_SO2 @14848. The press arrives through the STSwitch OnPressSuccessful bind @24456-24607.

Open question: when the host runs that press, does the client's switch copy broadcast OnPressSuccessful? If it does not, the client's generators stay dark while the host's glow.

Grep found no `SetScalarParameterValueOnMaterials` handling in mod/src. No other FED level script calls it: the extracts in build/kis count 0 calls each for C1M1, C2M1, C3M1, C4M1, C4M2, C5M1 and C5M2.

## Check (static first, then one runtime check in a C3M2 run)

1. Trace the ASTSwitch press path on a client copy. Does `OnPressSuccessful` broadcast from replicated state, from a card 098 row, or only from a local press?
2. If it is local only, the fix is a class fix. When the host fires a switch's OnPressSuccessful, the client's copy of that switch broadcasts it too. That covers every script bound to it, not just this one material.
3. Runtime: in a C3M2 run with C_52 and Switch C_2, the client log shows the client's GeneratorsOnCheck path running. The proof is a card 099 or a script-call row, plus one client screenshot of StaticMeshActor_12 lit.

## Static result (2026-11-22): a gap, fixed by class in b456

- **Cause.** The host's own press ran `ASTSwitch::OnInteraction` on the host only. Card 054 relays client to host and runs a client's press on both machines, but nothing ran the host's press on the client's copy. The client's switch copy never broadcast `OnPressSuccessful`, so every graph bound to it stayed undone on the client: the C3M2 generator glow (Switch C_2 @24456 -> Plant_Generator_Reset -> GeneratorsOnCheck), and the switch copy's own state and prompt. Doors, objectives and sounds reached the client through their own relays, so only the unrelayed effects showed the gap. No earlier C3M2 log has a host press of Switch C_2 (c263L441 only adds PowerPlant_SO2), so there is no runtime row yet.
- **Gate, from the disassembly of exe+0x50F78A0.** `this` is the interface at actor +0x2C8, and the interactor is never read. The function returns without a broadcast when the reactivation timer handle (interface +0xA0) is set. It also returns when the lock byte (interface +0x40) is set and the component at interface +0x10 has byte +0xF0 set; only a denied sound plays then. Otherwise it broadcasts at actor +0x2E8, then starts the reactivation timer or runs the one-shot path.
- **Fix (class, every ASTSwitch).**
  - `c492_switch_press` is the host's one switch-press point (instant `c102_interact`, the host's own hold completion, and a client's HoldDone). It now returns `c602_fires(target)`, the gate above read before the original runs.
  - After the original, `c602_send_press` sends `ChanOp::SwitchPress` (text, the placed name) to every ready client except the presser's own. The presser's client already ran its local press.
  - The client's `c602_press_receive` finds the placed switch and checks its own copy's gate. It runs `ASTSwitch::OnInteraction` (new symbol `ASTSwitch_OnInteraction`) on the copy's interface with a null interactor. A copy whose gate refuses is a WARN and is not forced.
  - Four-part op rule: enum `SwitchPress`, kChanArgs text, the kC132Ops event row, and the chan_ops.cpp handler.
- **Rows.** Host: `card602: host t=... switch press <name> by <pawn> sent to N of M other clients`. Client: `card602: client t=... switch press <name> from the host: own copy's OnInteraction ran (OnPressSuccessful)`.

## b456 run predictions (C3M2)

1. The host presses Switch C_2 (after C_52 enables it). The host logs `card602: host ... switch press BP_Switch_C_2 ... sent to 1 of 1 other clients`. The client logs the `own copy's OnInteraction ran` row for BP_Switch_C_2 within 100 ms.
2. The client shows no `WARN card602` rows, and both logs keep 0 mismatches.
3. A client screenshot at StaticMeshActor_12 after the press shows the generator lit, and it matches the host shot.
4. A client press of a switch, if the run has one, gives a host `sent to 0 of 0 other clients` row (the presser is excluded), and no client card602 row.

## b456 results (c602b456b and c602b456c, FED_C3M2, 2026-11-22)

1. **Met, twice.** c602b456b: host `card602: host t=210599728 switch press BP_Switch_C_2@d7e2bc7e by
   BP_PlayerCharacterMI_C sent to 1 of 1 other clients`, client `... from the host: own copy's OnInteraction ran
   (OnPressSuccessful)` 46 ms later. A second press at 210614742 was relayed too, after the reactivation timer.
   c602b456c: host t=211168413, client t=211168447 (34 ms). PowerPlant_SO2 completed on the host at the same ms.
2. **Met.** 0 `WARN card602` rows in both runs; 0 mismatches on both sides of both runs.
3. **Not proven: the shots missed the generator.** `StaticMeshActor_12` is not a unique name in FED_C3M2: the host's
   look resolved it to a `BP_KillableCrate_05_C` (`aim: hit StaticMeshActor_12 (BP_KillableCrate_05_C) guid 0x0 at
   -67917 -12582 276`), in another sublevel than the level script's generator. The client shot shows a wall with the
   Power Plant marker 449.95 m away. Shots: prefix2/.../Saved/Screenshots/Windows/{client,host}-c602b456c-c602-*.png.
4. Not exercised (no client press in either run).

Open item: the generator shot needs a target that resolves to the level script's StaticMeshActor_12. The drive
commands take no level qualifier, so the next C3M2 run warps both players with `warp actor|client point <x> <y> <z>`
to a point read from the level script's actor (a `where` row for the actor in the FED_C3M2_Scripting level), then
looks at that point. The relay itself is proven by the rows above; only the visual check remains.

## Closed (senior, 2026-11-22)

The relay is proven twice (c602b456b, 46 ms; c602b456c, 34 ms), with 0 WARN and 0 mismatches. The glow is downstream of the same chain on the client copy. A receiver-side shot of the generator is still owed, and it rides the next C3M2 run (a point warp to the level script actor, lesson 93). That is a STATE proof line, not an open card.
