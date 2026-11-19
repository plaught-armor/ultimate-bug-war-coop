# Card 198: Card 143 restore and the play log work on the bug-side player pawn

Status: queued 2026-11-12, split from card 195 (one goal per card). Budget: three runs. Static first.

## Why

The sweep in `docs/cards/done/195-c138-hold-bug-pawn.md` marks five session-state sites that still test
`g_player_mi_class`: `restore_c143.cpp` lines 77, 178, 202, 221 and `playlog.cpp` line 229. On a bug map the host
therefore sends no Reload ("WARN card143: ... no player pawn"), drops a client's Restore after the hold time, and
the play log reports no player pawn. Lines 77, 221 and the playlog row read `kMiWeaponInventory` /
`kMiAmmoInventory` behind the same test, so a plain class swap there is the card 192 crash class.

## Do

1. Trace what a checkpoint restore must carry for the bug pawn (Health only, or more: name the bug pawn's saved
   state) and what the play log needs.
2. Split each site: the pawn test by `g_player_character_class`, the MI inventory reads behind `c192_mi_pawn`.
3. One run: a checkpoint restart on a bug map with the client in.

## Done when

- The run shows the Reload sent and the Restore applied for the bug pawn on both sides, no card143 "no player pawn"
  WARN, and the MI maps unchanged. The Report ends with "Report complete."

## Trace (step 1, static and c195-r1 logs; no run)

Each line is verified (V) or open (O).

### What a checkpoint restore must carry for the bug pawn

1. V. Health. `c21_health` reads `kCharacterHealthComponent` through `c21_character_ptr`, which is on the
   ASTPlayerCharacter base, so it works on the bug pawn; death.cpp:2430 already reads it behind
   `g_player_character_class`. `c43_set_health` also goes through the same component.
2. O. Armour. `c48_armour` reads the "Armour"/"MaxArmour" offsets that `c30_set_offset` found in the health set
   class. It is not verified that the bug pawn's health set has an Armour attribute. If it does not, `c48_armour`
   returns -1 and the apply's `g_set_numeric_attribute_base` must be skipped (not a WARN).
3. V. The bug pawn has its own saved state beyond Health. `ASTPlayerCharacterBug::PreSaveGame` 0x051D8FE0 calls
   `ASTPlayerCharacter::PreSaveGame` 0x051D8E00, then, on two branches, `USTBugFormInventory::SaveInventory`
   0x0524C6C0 on the component at +0xA30 and `USTWeaponInventory::SaveInventory` 0x0524C8B0 on +0xA40. The
   reflected fields are `FormsInventory` and `FormDefinitions`. So a save row carries the bug's form inventory.
4. O. Whether a rejoined server copy gets its forms back without the Reload. The level script binds forms in
   `ReceiveBeginPlay` ("Setup Forms Binding", BUG_C1M1_Scripting, kis.py, card 199 trace), which suggests forms are
   set per map rather than from the pawn's save. Not verified. The first run must read FormsInventory on the
   restored copy and on the host's save-loaded pawn and compare them.
5. V. Weapons and ammo cannot be carried. `kMiWeaponInventory` / `kMiAmmoInventory` are ASTPlayerCharacterMI
   offsets; on the bug pawn they read other fields (the card 192 crash class).

Proposed carry for the bug pawn: the same text format with empty MI parts, "<health> <armour>;;". Forms are not in
the first fix. Item 4 decides whether they need a row (a later card, or an addendum here if the run shows a
difference).

### The card 143 hooks the bug pawn does not reach

6. V. `c143_after_load_asc` (site 178) never runs for the bug pawn, whatever its class test says. It is called from
   `detour_load_asc` (death.cpp:2147), which hooks `ASTPlayerCharacterMI::LoadAbilitySystemComponent` 0x051CDF30
   (hooks_ubw.cpp:1910). The bug pawn overrides it: `ASTPlayerCharacterBug::LoadAbilitySystemComponent` 0x051CDB60
   calls `ASTCharacter::LoadAbilitySystemComponent` 0x050F28E0 directly (call at 0x1451CDBE6) and never the MI
   one. `c50_after_load_asc` is fed by the same detour; it is MI-only, so it does not care.
7. V. `c143_host_assign` (site 202) is fed by `detour_assign_starting` (weapons.cpp:156 to `c50_after_assign`),
   which hooks the base `ASTPlayerCharacter::AssignStartingInventories` 0x051BD040 (hooks_ubw.cpp:1809).
8. O. Whether the bug pawn's spawn calls AssignStartingInventories at all. c195-r1 cannot show it: the host line
   needs `g_c143.armed`, and the client line (card050) needs the MI test. If it is never called, the apply still
   happens through the 5 s late path (`kC143ApplyLateMs`), with its WARN.

### What the play log needs

9. V. `g_pl.mi[s]` (playlog.cpp:229) drives three things:
   - `pl_side` (line 148): "host" only when slot 0 is MI. On the bug map the host logs as "menu"; for example c195-r1
     host line 4091 "playlog: menu t=104933795 widget add ...", after LoadMap returned at line 3503.
   - The CharacterMovement offset (line 239). The bug pawn is an ASTPlayerCharacter with a CharacterMovement, so it
     can take the lookup. The front-end pawn cannot ("its absent row would stick for the process").
   - `pl_weapons` (line 253) reads `kMiWeaponInventory`: MI only.
10. O. The front-end pawn's class, `BP_Pawn_3DFrontend_C` (c195-r1 host line 634). Its name suggests a plain APawn,
    not an ASTPlayerCharacter, which would keep it out of a character-class test. Not verified; the run's menu
    lines settle it (P5).

## Proposed split per site (step 2)

- restore_c143.cpp:77 (`c143_send_reload`): the pawn test becomes `g_player_character_class`. `c143_snapshot` keeps
  Health and Armour for any player pawn and reads the weapon and ammo parts only behind `c192_mi_pawn(pawn, "c143
  Reload inventory")`. Otherwise those parts are left empty, which gives "<h> <a>;;".
- restore_c143.cpp:178 (`c143_after_load_asc`): the test becomes `g_player_character_class`. The call must also
  reach the bug pawn (item 6). Either hook `ASTPlayerCharacterBug::LoadAbilitySystemComponent` 0x051CDB60 as a
  second entry to the same detour body, or move the detour to the shared `ASTCharacter::LoadAbilitySystemComponent`
  0x050F28E0 with the player-class test inside. Proposed: the bug entry. The ASTCharacter one also runs for every
  NPC load and changes card 048's count lines.
- restore_c143.cpp:202 (`c143_host_assign`): the test becomes `g_player_character_class`. There are no MI reads here.
- restore_c143.cpp:221 (`c143_host_step` ready test): the test becomes `g_player_character_class`. In `c143_apply`,
  the weapon and ammo reads (lines 122, 125) go behind `c192_mi_pawn(pawn, "c143 Restore inventory")`. For a non-MI
  pawn, "weapons not restored" is the expected result, not a WARN: the WARN condition becomes `(weapons_ok || !mi)
  && marked`. Armour is set only when `c48_armour` reads >= 0 (item 2).
- playlog.cpp:229: split the flag. `g_pl.player[s]` = `is_a(pawn, g_player_character_class())` drives `pl_side`
  and the CharacterMovement lookup. `g_pl.mi[s]` (the MI test as now) stays only for `pl_weapons`. pl_weapons runs
  per tick, so it keeps the plain flag rather than `c192_mi_pawn`'s per-call skip counting.

## Predictions (step 3, one run: a checkpoint restart on a bug map with the client in)

- P1. Host: "card143: host t=... Reload to <pc> (copy BP_PlayerCharacterBug_C...): sent: <h> <a>;;". No "WARN
  card143: ... no Reload ... no player pawn".
- P2. Host: one "card192: host skips c143 Reload inventory on BP_PlayerCharacterBug_C" line and no MI read on the
  bug pawn, with no crash in the 30 s after the restart.
- P3. Host: "card143: host t=... Restore applied to <pc> (copy BP_PlayerCharacterBug_C..., ...): Health a -> b
  (want h)", with b equal to h. Either "after its AssignStartingInventories" or "late, no Assign mark" (item 8
  decides which; the line is the evidence). No "Restore for controller N dropped" WARN.
- P4. If the save held a downed bug pawn: "card143: host ... BP_PlayerCharacterBug_C ... loaded at the downed
  Health": proof that the LoadAbilitySystemComponent bug entry fires. Otherwise one card048
  "LoadAbilitySystemComponent ... on BP_PlayerCharacterBug_C" line shows that the hook is reached.
- P5. Play log: on the bug map, host lines read "playlog: host ..." and client lines read "playlog: client ...". On
  the front end, both still read "menu".
- P6. The MI maps are unchanged: a FED-map restore in the same session (if the route has one) logs the same Reload
  text shape as c143-r3 ("<h> <a>;<weapons>;<ammo>").
- P7. FormsInventory read on the host's loaded pawn and on the restored copy: a logged comparison (item 4). A
  difference is a finding, not a failed prediction.

## Problems for the senior

- Item 6 is a hook gap and not part of this card's class swap: any card that keys on
  `ASTPlayerCharacterMI::LoadAbilitySystemComponent` (card 048's lines, card 050's `c50_after_load_asc`) is blind
  on the bug pawn. Only card 143's use is in this card's scope.
- The bug pawn's FormsInventory (item 3) is saved per pawn and is not relayed by any card I know of. If forms differ
  between the host's pawn and a client's server copy, it is a full-world-sync gap (bug form state, card 196 area).
- `pl_side` reports "menu" for the host for the whole bug map today, so any bug-map playlog-based check has been
  reading the wrong side label. It is fixed by the split above; noted so past bug-map logs are read with it in mind.

## Run c199-r1 predictions (junior, before the run)
Build e7d7860ed7f47b95. Sites 77, 202, 221 test `g_player_character_class()`; the MI reads in `c143_snapshot` and
`c143_apply` sit behind `c192_mi_pawn`; `ASTPlayerCharacterBug::LoadAbilitySystemComponent` (0x051CDB60) has its own
entry on the card 143 detour path; playlog keys the side and movement on the character class.
- C198-P1. Host: "card143: host t=N Reload to <pc> (copy BP_PlayerCharacterBug_C...): sent: <h>;;". Health only:
  Armour is left out because trace item 2 did not resolve where the bug pawn keeps it.
- C198-P2. Host: a "card192: host skips c143 Reload inventory on ... (BP_PlayerCharacterBug_C)" line; no crash.
- C198-P3. Client: "card143: client t=N Reload received"; then "Restore sent". Host: "card143: host t=N Restore applied
  to <pc> (copy BP_PlayerCharacterBug_C...): Health a -> b (want h)" with b equal to h, the Armour part reading "not
  carried" and the weapons part "not MI, none carried". No "Restore ... dropped" WARN.
- C198-P4. No "WARN card143: host ... no Reload to ...: no player pawn" line anywhere in the run.
- C198-P5. "card198: host LoadAbilitySystemComponent (bug entry) #1 on ... (BP_PlayerCharacterBug_C)" appears on the
  host at the checkpoint load (and on the travel to the bug map).
- C198-P6. Play log on the bug map: host lines read "playlog: host t=", client lines "playlog: client t=". No
  "playlog: menu" line while a bug pawn is possessed.

## Run c199-r1 result (card 198 part; senior review)
Build e7d7860ed7f47b95. The Reload, the Restore and the bug entry hook work. One defect:
`WARN card143: host t=106877748 Restore applied to BP_PlayerController_C (copy BP_PlayerCharacterBug_C, late, no
Assign mark, 5002 ms after it came): Health 400.0 -> 400.0 (want 400.0), Armour -1.0 -> -1.0 (want 0.0, not carried),
weapons not MI, none carried`.
Cause: only the MI pawn gets an AssignStartingInventories mark. `c143_host_step` therefore held the bug pawn's Restore
for the full `kC143ApplyLateMs` (5 s without the restored Health), and `c143_apply` flagged it as WARN.

## Fix for run c198-r2 (run 2 of 3)
- Static question: does anything on the bug pawn's spawn path overwrite Health after LoadAbilitySystemComponent?
  **Cannot tell statically.** The PDB lists these `ASTPlayerCharacterBug` overrides: BeginPlay, PossessedBy,
  ActorFullyLoaded, LoadAbilitySystemComponent, ResetInventory, OnMovementModeChanged, StartDeath and others. Any of
  the first three, or the ASTPlayerCharacter or GAS attribute setup they call, could set Health. That was not
  decompiled. So the host now logs a read-back of Health one second after every apply:
  "card198: host t=N Restore read-back 1000 ms after the apply: controller C pawn P Health h (applied a)". The line
  is a WARN when h differs from a.
- Changes in `restore_c143.cpp`:
  - `c143_host_step` waits for the Assign mark only on an MI pawn (`is_a(pawn, g_player_mi_class())`, not
    `c192_mi_pawn`, so the skip line stays capped). Any other class is applied on its first ready step.
  - In `c143_apply`, the WARN prefix covers only an MI pawn that is late or whose weapons failed. For a pawn that is
    not MI, the text reads "no Assign on this class" and carries no WARN.
  - New `c143_read_back` runs at the start of `c143_host_step`, using new `C143State` fields: check_pc_id (-1 means
    none), check_ms and check_want.
  - The MI path, the Reload text, the hook, playlog and cards 199 / 184 are unchanged. Function sizes: c143_apply
    41 lines, c143_host_step 24, c143_read_back 16.
- Limit: under UBWCOOP_GOD=1 the saved Health and a freshly spawned pawn's Health are both 400 (full). The read-back
  therefore cannot tell "nothing overwrote it" from "it was overwritten with the same value". A save taken with the
  pawn damaged would; that needs a scenario step outside this fix.
- Build 669cdec6305c5510.

## Run c198-r2 predictions (before the run)
- C198-R2-P1. Host WARN classes: the attach line only.
- C198-R2-P2. "card143: host t=N Restore applied to BP_PlayerController_C (copy BP_PlayerCharacterBug_C, no Assign
  on this class, M ms after it came): Health a -> 400.0 (want 400.0) ...", with no WARN prefix and M well under
  1000 ms after "Restore received".
- C198-R2-P3. "card198: host t=N Restore read-back 1000 ms after the apply: ... Health 400.0 (applied 400.0)",
  with no WARN.
- C198-R2-P4. Card 199 as in c199-r1: on the bug map, "journal replay ... 2 of 2 frames", and digest rounds 1 and 2
  with mismatches 0.

## Report (run c198-r2, build 669cdec6305c5510)
Files changed (card 198, both runs): mod/src/ubw/restore_c143.cpp, playlog.cpp, hooks_ubw.cpp (hook table entry),
hooks_internal.h, tools/symbols.txt, mod/src/offsets.generated.h (one symbol, from the generator). Run 2 changed only
restore_c143.cpp. The scenario was tools/scenarios/c199-r1.txt, unchanged, run with tag c198-r2.

Summary: the bug pawn's checkpoint Restore is applied on its first ready step, with no WARN. The Health read back one
second later still holds the applied value. Scenario passed in 97 s; dead time 2.0 s of sleep, 0 timed-out try waits.

Verification lines:
- C198-R2-P1 holds. Host WARN classes: `WARN attach: DisableThreadLibraryCalls failed, err=126` only. Client: attach 1
  and card073 9, the same as c199-r1.
- C198-R2-P2 holds. `card143: host t=107107399 Restore received from BP_PlayerController_C (7 chars): 400.0;;`, then
  `card143: host t=107107404 Restore applied to BP_PlayerController_C (copy BP_PlayerCharacterBug_C, no Assign on this
  class, 5 ms after it came): Health 400.0 -> 400.0 (want 400.0), Armour -1.0 -> -1.0 (want 0.0, not carried), weapons
  not MI, none carried`.
- C198-R2-P3 holds. `card198: host t=107108411 Restore read-back 1000 ms after the apply: controller 192723 pawn
  BP_PlayerCharacterBug_C Health 400.0 (applied 400.0)`. Under god mode this does not rule out an overwrite with the
  same value (see the Limit above).
- C198-R2-P4 holds. `card049: host t=107072104 journal replay to BP_PlayerController_C (first Pong of the map): 2 of 2
  frames`; `card132: client round 1: actors 1122 host, 1122 client, mismatches 0` and `round 2: ... mismatches 0`.
- From c199-r1, unchanged: `Reload to BP_PlayerController_C (copy BP_PlayerCharacterBug_C): sent: 400.0;;`; one card192
  skips line each for c143 Reload and Restore inventory; `card198: host LoadAbilitySystemComponent (bug entry) #1 on
  BP_PlayerCharacterBug_C (host-own)` and `#2 ... (server-copy)`, on the checkpoint load only. A fresh pawn on the
  travel loads no save, so the c199-r1 prediction "and on the travel" did not hold. No "no player pawn" WARN.
  Playlog on the bug map: host 12 "playlog: host" lines, client 15 "playlog: client" lines.

Problems:
- The "summary (map end)" playlog line reads "menu" on both sides, because it is written after the old pawn is gone.
  c195-r1 shows the same, so this predates card 198. It is not fixed (playlog was not in run 2's scope).
- Armour for the bug pawn is not carried (trace item 2 is still open). The FormsInventory relay and the card 048 /
  card 050 hooks on the bug pawn stay out of scope.
- The Health read-back under god mode is weak proof. A save taken with the pawn damaged would show an overwrite.

Report complete.

## Senior review (2026-11-12)

Accepted on c198-r2 (DLL `669cdec6305c5510`). c199-r1 showed the bug pawn's Restore waiting 5 s for an Assign mark
that never comes, with a new WARN; fixed in `c143_host_step` / `c143_apply`. Checked by me: only restore_c143.cpp
changed between the runs; host WARN is the attach line only; Restore applied 5 ms after it came, no WARN; read-back
400.0 after 1000 ms. Unproven: an overwrite of a damaged pawn's Health after the apply (GOD run, saved and spawned
Health equal; the read-back line WARNs if it happens, soak watch). Open, not in this card: bug-pawn Armour is not
carried (card 196); the playlog map-end summary reads "menu" after the pawn is gone (predates this card).
