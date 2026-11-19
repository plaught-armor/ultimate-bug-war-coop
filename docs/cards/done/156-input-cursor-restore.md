# Card 156: Every return to Up restores the client's input and cursor

Status: CLOSED, outcome in docs/CARDLOG.md. Was: queued 2026-11-08, from docs/playtest-2026-11-08.md findings 23 and 24. Budget: three runs. Must land before the next play build.

Read: docs/playtest-2026-11-08.md findings 23, 24 and the "Host log" finding 24 entry, card 043 (down states), card 044 (death screen),
card 048 (respawn in place), card 155 (input lines).

## Why

After downs and deaths the client's mouse showed and its input was dead:

- The Out -> Up revive ran no respawn path: no SetInputMode, no cursor reset.
- After the one GameOnly reset (t=259994442), bShowMouseCursor was 1 again within 5 s with no logged setter.
- 4 WB_PlayerDeathScreen_C were built and none was logged as removed.
- IMC_DummyUI_KBM (priority 2, 30 mappings, caller exe+0x4467B35) was added twice with no remove, above the
  gameplay context.

## Do

1. Static first: the setter that re-shows the cursor (with card 155 line 4), who adds IMC_DummyUI_KBM at
   exe+0x4467B35 and what normally removes it, and whether the death screen removes itself on the host only.
2. One owner for the client's "back to Up" transition (Downed -> Up, Out -> Up, respawn, save load): game-only input
   mode, cursor hidden, viewport captured, every death screen removed, IMC_DummyUI_KBM removed. Each transition
   kind goes through it.
3. Sweep the other UI-only contexts and modal widgets the death and down flow adds.

## Done when

- One run with each transition kind on the client: at +5 s after each, bShowMouseCursor 0, capture 1, no death screen
  in the viewport, no IMC_DummyUI_KBM, and a scripted move and fire that works.
- The usual Report items (WARN diff, run length, rc, ERROR/FATAL, CRASH, dumps, DLL hash). It ends with "Report
  complete."

## Trace (static, before the run)

- **What re-shows the cursor.** The death UI goes up through the HUD's own BP function "Set Input Mode UI" on
  BP_HUD_Manager_C (the pc's MyHUD at pc+0x358, an ASTHUDGameplay subclass); card 018's log shows it as the UIOnly
  caller at death and game over. It sets UIOnly, shows the cursor and sets ASTHUDGameplay's UIMode byte (HUD+0x3C8;
  IsUIMode 0x5209620, SetUIMode 0x521CA00). The HUD's OnMouseChange / OnInputDeviceChange handler writes
  bShowMouseCursor from UIMode (CallFunc_IsUIMode in its name map). Our old reset called only the engine
  SetInputMode GameOnly and the cursor setter, so UIMode stayed 1 and the next mouse event put the cursor back. That is
  the playtest's "bShowMouseCursor 1 again within 5 s with no logged setter": the BP writes the property directly.
  The inverse is the same HUD's "Set Input Mode Game".
- **IMC_DummyUI_KBM.** Added at exe+0x4467B35 with the death screen (CompleteDeath), and also in the front end before
  joining (c143-r1 client log line 745). Mapping contexts live in the LocalPlayer's Enhanced Input subsystem, which
  survives a map load. In the 17 restore checks logged so far it was already gone at a respawn in place, so the death
  screen's own teardown removes it there; a map load tears the screen down without that path, so the context can
  leak into the next map.
- **Death screens.** card 045 tracks each client WB_PlayerDeathScreen_C (weak refs); c45_remove_screens removes the live
  ones. It ran only at a respawn in place.
- **Transitions that reach the client now.** Downed -> Up and Out -> Up (card 043 Down frames, c43_receive_down);
  respawn in place (c45_respawn_check, now rare: card 143 made every in-place checkpoint load a client rejoin); and a
  client map load (game over -> checkpoint, RESTART MISSION, any rejoin), first seen at AcknowledgePossession with
  g_game_only_pending.

## Design

One owner, `c156_client_up(pc, kind, restore, now)` in death.cpp:

- restore: remove the tracked death screens (c45_remove_screens, which also forces GameOnly); if the HUD's UIMode is 1,
  call the HUD's own "Set Input Mode Game" through ProcessEvent; cursor off and IMC_DummyUI_KBM removed
  (c48_client_cursor). One line "card156: client ... back to Up (kind): restore ..." with UIMode before and after.
- Every call arms a check 5 s later (`c156_client_check`, run from c45_respawn_check on the client each tick):
  bShowMouseCursor, viewport capture, UIMode, IMC_DummyUI_KBM and live death screens. It logs WARN unless they are
  0, 1, not 1, not mapped and 0.
- Call sites: c43_receive_down on an own-pawn change to Up from a down state (restore only when death screens are
  tracked, so a revive never closes an open pause menu); c45_respawn_check at a respawn in place (replaces the bare
  c45_remove_screens; the c48_respawn cursor call moved into the owner); detour_acknowledge_possession on a client map
  load, right after client_force_game_only.

DLL bcb32042f01a6f51 (also carries the card 143 follow-up label fix: kC40ParamsHealth 0x40, kC40ParamsArmour 0x44).

## Predictions (run c156-r1, tools/scenarios/c156-up.txt)

- P1. Downed -> Up: "back to Up (revive from Downed): check only"; +5 s line clean (0, 1, UIMode 0, not mapped, 0).
- P2. Out -> Up: the same with "revive from Out".
- P3. Game over: the client logs CompleteDeath, AddMappingContext IMC_DummyUI_KBM and the death screen; the HUD's "Set
  Input Mode UI" path runs (UIOnly, cursor 1).
- P4. After the checkpoint load and rejoin: "back to Up (map load): restore", the card 048 line "IMC_DummyUI_KBM was
  mapped, removed" (the subsystem outlived the map), a fresh HUD with UIMode 0 (so "Set Input Mode Game not called");
  +5 s line clean.
- P5. A move (goto) and a fire (host "shot s seq") work after each transition.
- P6. Label fix: every bug hit line's "(armour A, health H)" matches its own "Armour x -> y" drop (A = x - y).
- The first map load at session start also logs a clean "back to Up (map load)" pair.
- Not exercised: respawn in place (no route reaches it on the client since card 143) and RESTART MISSION (the same
  map-load owner as P4).

## r1 result and r2 changes (DLL bcb32042f01a6f51)

c156-r1 proved the label fix (P6: "damage 25.00 (armour 25.00, health 0.00), Armour 100.0 -> 75.0"), the map load restore
at start, Downed -> Up (check only, +5 s clean, fire works) and the game over path (respawn in place: "2 death screens
tracked, HUD UIMode 1 -> 0, Set Input Mode Game called, bShowMouseCursor 0"; then the map load restore, +5 s clean).
P4 was wrong: IMC_DummyUI_KBM was already removed by the respawn-in-place restore before the map load check.

Three things failed:

1. **Host crash after the rejoin** (EXCEPTION_ACCESS_VIOLATION in FName::GetDisplayNameEntry, reading 0x3630000007D).
   The stack named, from the mod's .pdata bounds and string references, detour_c74_spawn -> c132_proj_launch ->
   class_name(shooter). The shooter came from the FireProjectile task ring (proj_c132.cpp, C132ProjTask): a raw shooter
   pointer per task address, searched oldest slot first. After the checkpoint load a new task took a freed task's
   address, the older ring entry matched first, and its shooter was the pawn freed with the old world. Fix: the ring
   keeps the shooter as a weak handle (C31Weak, c31_weak_get gives null for a gone object) and the lookup takes the
   newest entry for the address. Sibling check: c76_mark_task keeps raw task/vehicle/pilot pointers, but those are
   consumed by the same task's spawn in the same activation and cleared there, so no stale read across a load.
2. **Out step**: the client was revived from Downed 5 s after the down with no E pressed. The host log names card 130:
   "AI pick-up: BP_AI_Trooper_SquadLeader_C goes to downed player BP_PlayerCharacterMI_C, 327 uu", then "revived from
   Downed after a 3078 ms hold". Card 130 sends a trooper within 4000 uu, so Out is not reachable near troopers. The
   step is dropped; Out -> Up calls the same c156_client_up as Downed -> Up.
3. **Scenario**: the host death screen wait has no host line to match (card 044 logs the client's screen only), so it is
   dropped. The post-rejoin goto went from the player start to a point about 8 km away; r2 warps the client back to the
   bug field first.

Seen, not in this card's scope: "WARN card043: host real death: 5 hits, Health 1.0" at the game over (the host's own pawn
is not killed by the real-death hits). It is in older runs too (c112c, c117a, c130r, c76d).

### r2 predictions

- R1: no host crash; the client's post-rejoin shot gives the host "playlog: host t=... shot s seq=" and the run ends with
  rc 0 on both sides and no new crash dump.
- R2: the r1 passes repeat: map load restore at start and after the rejoin with clean +5 s checks, Downed -> Up check
  only and clean, the respawn-in-place restore with HUD UIMode 1 -> 0.
- R3: the client's goto after the rejoin reaches 12900 14100.

## Report

Run c156-r2, DLL c93d7ed014529045: 118 s, host rc 0, client rc 0, drive rc 0 ("scenario passed"), no new crash dump.
Two runs of the three-run budget were used.

- R1 held: after the rejoin the client's shot reached the host ("playlog: host t=269815724 shot s seq=0
  w=BP_Weapon_MoritaMk1_C"), going through the launch line that crashed r1, with no crash. The fix is the weak-handle
  shooter ring in proj_c132.cpp (see "r1 result and r2 changes").
- R2 held:
  - Map load at the start: restore, then the +5 s check was clean (bShowMouseCursor 0, viewport capture 1, HUD UIMode 0,
    IMC_DummyUI_KBM not mapped, death screens live 0).
  - Downed -> Up: check only, +5 s clean.
  - Game over -> checkpoint: "respawn in place: restore; 2 death screens tracked, HUD UIMode 1 -> 0, Set Input Mode Game
    called, bShowMouseCursor 0". Then map load: restore, +5 s clean.
- R3 held: the warp back to the bug field (within 26 uu), then the goto to 12900 14100 ended done.
- P6 (the card 155 label fix) was proven in r1: "damage 25.00 (armour 25.00, health 0.00), Armour 100.0 -> 75.0".
- WARN diff:
  - Host 3, all known: attach, GOD off, and one "kill actor" try with no bug left.
  - Client 24, none new to this card:
    - card132 ammo-pickup mismatches (card 145).
    - 3 corpse arrival fallbacks (card 146).
    - client decal spawn null after the rejoin (also in r1).
- Not proven by a run: Out -> Up. Card 130's AI pick-up revives a downed client within about 5 s, before the bleedout, so
  Out was not reached. It uses the same c156_client_up call as Downed -> Up.
- Respawn in place has no standalone +5 s check: the map load that follows it re-arms the check, so its proof is the
  map load's clean +5 s check.
- Seen, outside this card: "WARN card043: host real death: 5 hits, Health 1.0" at the game over in r1 (also in c112c,
  c117a, c130r, c76d).

Report complete.
