# Card 337: the client sees the new map for 2 to 4 s before the title screen on a host travel

**Status:** accepted 2026-11-14 (fixed in lane 2, b19 `40e935ba1137d921`, three proof runs of `c338-c337`; see CARDLOG). Was: open, traced only; not in the card 336 build. Reported by the user in the 2026-11-14 lane 2 runs.

**Numbers (r6, `logs/c333-soak-c2m1-client.log`):** C2M1: client LoadMap returned at 319904770, the title screen
(WB_Loading_Screen_Final_C via the card 135 GameStarted call) was built at 319909057, about 4.3 s of rendered map.
C1M1: about 2.3 s. The host shows no gap: its screen comes from GameMode BeginPlay in the same load.

**Cause:** the client's screen waits for the IntroStart op plus its own HUD being a live BP_HUD_Manager with a
possessed pawn (intro_c135.cpp header). Nothing covers the view between LoadMap return and that call.

**Fix direction:** at LoadMap entry of a host travel the client raises a cover (the game's loading screen, or a plain
full-screen widget) and drops it when the card 135 screen is on its stack, or on a bounded timeout with a WARN.
Not a short trace: the game's own loading screen widget is built by the HUD that does not exist yet at LoadMap
entry, so the cover needs its own widget class and owner; trace that before coding.

**Proof:** client screenshot rows at LoadMap return +500 ms and +2 s show the cover, not the map; the `screen
confirmed` row still follows.

## Senior trace 2026-11-14 (static): cover by world rendering off, no widget

User rule behind this card: "the client should be watching the same cutscene as the host".

A cover widget is not needed. The engine already has the switch: `UGameplayStatics::SetEnableWorldRendering(const
UObject* WorldContextObject, bool bEnable)` sets `UGameViewportClient::bDisableWorldRendering`; the viewport then
draws no world, only UI (black behind the widgets). The game ships its PDB next to the exe, so the function resolves
through `tools/symbols.txt` like every other native.

**Design (client only, play build and tests alike):**
1. Client LoadMap detour, on return from a host travel (the place that logs the client's LoadMap return; the card 098
   map arm in world_c98.cpp runs there): call `SetEnableWorldRendering(world, false)`; row
   `card337: client t=<ms> world rendering off at LoadMap return on <map>`.
2. Switch it back on in one place: the card 135 tick, on the first frame the client's own WB_Loading_Screen_Final_C
   is element 0 of its stack (the same test as `screen_seen`); row `card337: client t=<ms> world rendering on after
   <ms> ms (title screen up)`.
3. Every path that ends the wait without a screen switches it on too: `c320_client_done` with a failure reason
   (GameStarted not called, failed, timed out), a refused IntroStart, disconnect, and the next LoadMap entry. One
   helper `c337_uncover(why)`; WARN when the reason is not "title screen up". Bound: 15 s after LoadMap return, WARN.
4. Maps with no title screen (front end, a join into a running map): no cover. Arm only when the travel is a host
   travel to a map on which the host sends IntroStart; else the client would sit on black until the bound.

**Presume-defect checks before the run:**
- The engine's own transition may set the flag: read `bDisableWorldRendering` at LoadMap return before the write and
  log it, so a pre-set value is seen and not overwritten blindly.
- World rendering off must not stall the load: the client's `screen prompt ready` row needs LoadingFinished 1 and
  LandscapeLoadingFinished 1. Prediction: both still arrive, prompt ready no later than b15's (+901 ms after the
  screen's pause on C2M1).
- The flag lives on the viewport client, which survives map changes: a missed uncover leaves the next map black.
  Hence step 3's next-LoadMap-entry reset.

**Predictions for the proof run (one run, b11-soak-c2m1):** two `world rendering off` rows (C1M1, C2M1), each
followed by `world rendering on ... (title screen up)`; 0 WARN card337; client shots at LoadMap return +500 ms and
+2 s show black or the title screen, never the map; `screen confirmed` rows unchanged; hold off still "all confirmed".

**Call path check (2026-11-14):** the name `SetEnableWorldRendering` is in the shipping exe (reflected UFunction);
a grep of the PDB for `?SetEnableWorldRendering@UGameplayStatics` found nothing, so do not plan on a native symbol.
Call it through ProcessEvent on the UGameplayStatics class default object (parms: WorldContextObject pointer, bool),
the way card 135 calls GameStarted; `GetEnableWorldRendering` gives the read for the pre-set check.
