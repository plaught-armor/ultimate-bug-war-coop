# 457: host crash in the checkpoint save thumbnail (null viewport RHI)

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Opened 2026-11-16 19:25 by the senior from run `c423b159a` (C2M1, build `885a2976f01932ac`). One sample.

## Facts

- Host `CRASH 1/4: EXCEPTION_ACCESS_VIOLATION at exe+0x2E12530`, render thread (T 856), `reading 0x90`, r8 = 0.
  Stack: `FD3D12DynamicRHI::RHIGetViewportBackBuffer` (exe+0x2E12530) < exe+0x51F1C35 < render command task <
  `RenderingThreadMain`. The run ended 44 s in (25 ok steps).
- 16 ms before it: `card050: host USTSaveSystemLibrary::Save_CheckPoint(context FED_C2M1, flag 0), caller
  exe+0x51F2936` and `card121 ... save engine save runs`, the checkpoint zone at (231988, 42270, 9070), 9 s after the
  map load.
- Caller code (objdump, exe+0x51F1C04 to 0x51F1C2E): reads `GEngine (0x1493FF208) -> +0xC10 (GameViewport) -> +0xF8
  (Viewport) -> +0x90 (ViewportRHI)` and passes it to the RHI's `RHIGetViewportBackBuffer` with no null check. The
  viewport RHI was null at that frame, so the callee read null + 0x90.
- The same save caller (exe+0x51F2936) ran in `c423b146a` with no crash. This crash address is in no other log in
  `logs/`. The b159 diff (card 298 row order, card 435 callout skip) touches neither the save nor the render path.

## Reading

The game's own save thumbnail reads the back buffer on the render thread with no check for a missing viewport RHI.
The viewport RHI is null while the window's viewport is rebuilt (resize, move between outputs, minimise). In tests
the harness places the 640x360 windows side by side after launch, which can fall on the same frames. In play it
would need a checkpoint in the same frames as a window resize or alt-tab.

## Next step

1. Rerun `c423` on the same build once. A second crash at the same address means the build or the lane changed
   something: then stop and compare with b158 on the same scenario.
2. If it does not repeat: log-only until a second sample. Possible mod guard (not built): in the mod's
   `Save_CheckPoint` detour (card 050), skip or delay the save by one frame while `GEngine->GameViewport->Viewport`
   +0x90 is null, with a WARN row.

## Done when

Either a second sample names what nulls the viewport RHI and the guard is in with a run that shows its WARN row and
no crash, or 10 further whole-map runs pass with no crash at exe+0x2E12530 and the card closes as a one-off.

Harness check (senior): `tools/run_pair.sh` places windows only by launch arguments (`-WinX`, `-WinY`); it moves or resizes nothing after launch (xdotool only activates a window in MENU mode). So the harness is not the source of the viewport rebuild; what nulled the viewport RHI is not known from this sample.

## Senior note (2026-11-16 19:38)

Runs since the crash with no repeat on build `885a2976f01932ac`: `c423b159b` (13 checkpoint saves), `c447b159b`,
`c448b159a`: 3 of the 10 clean whole runs. Guard limit: the null read (`GEngine -> GameViewport -> Viewport ->
ViewportRHI`) happens on the render thread about 16 ms after the game-thread `Save_CheckPoint` call, so a check at
the entry of `detour_c50_save_checkpoint` (hooks_ubw.cpp:3102) only narrows the window; it cannot close it. A guard
is worth building only after a second sample shows the pointer is already null at the game-thread call (log it there
first: one row per save with the three pointers).


## Closed 2026-11-16 22:40

Closed as a one-off. Crash address `exe+0x2E12530` appears only in `logs/c423b159a-host.log`. 34 host logs on
builds b159b to b169 since (C1M1, C2M1, C3M1, C3M2, C4M1 scenarios, checkpoint saves included) hold no `EXCEPTION`
row and no row at that address. (`CRASH` text matches in the `c423` logs are the `FED_C2M1_CRASHEDSHIP...` radio
names, not crash rows.) No guard built: the null read is on the render thread, a game-thread check cannot close
the window. Reopen on a second sample; first step then is the one-row-per-save pointer log described above.
