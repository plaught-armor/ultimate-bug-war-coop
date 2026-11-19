# Card 213: Card 028's trooper renderer sits on remote bug pawns

Status: queued, batched into card 210's build (run c210-r2). Found in c210-r1 (DLL a6a918954db931f2).

Known: card 028 adds its renderer (the FemaleMoritaTrooper sprite) to remote bug pawns on both sides (card 210
trace). Since card 210 runs the vanilla form switch on every copy, the pawn has no form for about 200 ms per
switch and the client logs a new WARN class, 8 lines in c210-r1:
`WARN card034: client refit of BP_PlayerCharacterBug_C skipped: offsets ok, half-height 0.0, FitMeshToCollider
resolved` (`remote_body.cpp:384`).

Do: trace statically what the card 028 renderer does on a bug pawn (visible or hidden, what it replaces; a bug
pawn has its own third-person mesh, SKM_AssassinBug). When the trace shows the renderer is never wanted on a bug
pawn, bug pawns (`kBugFormsInventory`) are left out of `c28_visit`, with one line per pawn left out (capped).
When it is wanted, say why and stop; the senior decides.

Done when: no card 028 renderer on a bug pawn on either side (log line, and the shots show no trooper sprite on
a bug), zero `WARN card034` in a run with form switches, human pawns unchanged.

## Trace (junior, 2026-11-12)

- What the renderer is for: card 028 gives a remote MI pawn a body. An MI player pawn has first-person arms only
  (card 022 audit), so the remote player gets the FemaleMoritaTrooper billboard sprite. `c28_visit` attaches it
  (`c28_try_attach` -> `c28_attach`: a `USTSpriteRenderComponent` and a `STSpriteVariantAnimatorComponent` copied
  from a placed Morita NPC), then drives its key (`c28_drive`) and refits it to the capsule (`c34_track_half_height`,
  card 034).
- On a bug pawn: nothing hides it. `c33_hide` only logs on a bug pawn (card 196, no visibility write), and the
  renderer is added with the NPC's visible settings. So it draws a trooper sprite inside the bug's own
  third-person mesh (SKM_AssassinBug). The bug pawn needs no stand-in body: the r1 shots show its own mesh on the
  remote side. During a vanilla form switch the capsule reads half-height 0, so the card 034 refit warns (the 8
  client WARNs in c210-r1).
- Other uses of the renderer on player pawns: `c34_log_z_line` and the card 092 view check (`g_c92`, only with
  `UBWCOOP_C92_LOG`) read it and skip a pawn without one. `c43_sprite_key` only feeds `c28_pick`.
- One use of `c28_visit`'s result that a bug pawn still needs: true calls `pl_remote_seen`, which on the client is
  the only feed of playlog's remote-player list (`pl_remote`, used to name a remote player's sounds). So a bug
  pawn must still return true.
- Result: the renderer is never wanted on a bug pawn.

## Code (junior, DLL 04264aa86c6b11e0)

`remote_body.cpp`, `c28_visit`: after the mech test, a pawn whose class has `kBugFormsInventory`
(`c196_inventory_at`) gets `c213_note_bug` (one `card213: <side> no card 028 renderer on bug pawn <name> (index N,
renderer present 0|1): it shows its own mesh` line per pawn, a table of 8 pawns per run, logged as
`card213 bug pawn` at the cap), then `c33_hide` (the card 196 log only) and returns true. No renderer is attached,
driven or refitted on it. MI pawns take the unchanged path.

## Senior review (2026-11-12): accepted

Run c210-r2: one `card213` line per side (`renderer present 0`), zero `WARN card034` over four form switches.
MI path unchanged in the diff; not rerun on a human map here (card 127 covers it).
