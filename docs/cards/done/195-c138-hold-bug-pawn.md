# Card 195: The client's BeginPlay hold releases on a bug-side map

Status: queued 2026-11-12, from c177-r2. Budget: three runs. Trace first; the senior reviews the trace before code.

## Why

On a bug-side map the local pawn is `BP_PlayerCharacterBug_C`. `c138_local_mi` (`mod/src/ubw/begin_c138.cpp:39`)
returns a pawn only when it is a `BP_PlayerCharacterMI`, so the GameState BeginPlay hold never releases. c177-r2
client log: "WARN card138: client hold still on after 10012 / 20021 / 30025 ms ... pawn (BP_PlayerCharacterBug_C)",
then "hold dropped at LoadMap after 30555 ms". While held the client answers no Ping (card 120 gate), so it gets no
join replay and no live relay: bug-side co-op does not work. This is a new WARN class and blocks the play build.

## Do

1. Trace, no run. Name the native parent class of `BP_PlayerCharacterBug_C` and the common base it shares with
   `BP_PlayerCharacterMI` (PDB, asset). State what the hold waits for (card 138, `docs/cards/done/`) and whether the
   same condition holds on the bug pawn (possessed, begun play). Mark each line verified or open.
2. Sweep: one table row per `is_a(..., g_player_mi_class())` site in `mod/src/ubw/` (ammo, attributes, death,
   playlog, restore_c143, fx_c136, jump_c94, begin_c138). Verdict per row: gates session state (hold, Pong, ready,
   restore: must accept the bug pawn) or gates an MI-only feature (stays; listed for card 196). Fix only the first
   kind here.
3. Fix at the owner: the hold's pawn test accepts every possessed player character class, by the common base, not
   by a second name check.
4. One run: the bug-side part of `tools/scenarios/c177-r2.txt` (travel to the bug map with the client in).

## Done when

- The run shows the hold released on the bug map on the client, a Pong after it, the join replay lines, and no
  "card138: client hold still on" WARN; the MI maps in the same run still release as before. WARN classes per side
  equal the baseline. The Report ends with "Report complete."

## Trace (step 1, no run)

Sources: `tools/symbols.txt`, `mod/src/offsets.generated.h`, `mod/src/ubw/begin_c138.cpp`, `chan_ops.cpp`,
`input_diag.cpp`, card 030, card 138 (`docs/cards/done/`), the c177-r2 logs (`logs/c177-r2-client.log`,
`logs/c177-r2-host.log`). The bug map in c177-r2 is BUG_C1M1 (host inventory: `BUG_C1M1_C`, `BP_GameMode_GameplayBug_C`).

1. **Native parent of `BP_PlayerCharacterBug_C` is `ASTPlayerCharacterBug`; the common base with
   `BP_PlayerCharacterMI_C` is `ASTPlayerCharacter`.** Verified by PDB: card 030 found the `ASTPlayerCharacterBug`
   and `ASTPlayerCharacterMI` (0x051DEE70) overrides of `ASTPlayerCharacter::StartDeath` (0x051DE410), each calling the
   base first. Verified at run time that the bug pawn is an `ASTPlayerCharacter`: three sites gated by
   `is_a(pawn, g_player_character_class())` fired on it in c177-r2. `input_diag.cpp:85` set the PC's player character
   ("client-input: SetPawn(pc=00000000925BA300, pawn=000000007E711780 BP_PlayerCharacterBug_C): ... player character
   0000000000000000 -> 000000007E711780", client log line 7083); `pawn_collision.cpp:159` ran `fix_own_pawn`
   ("pawn-collision: client own pawn 000000007E711780 BP_PlayerCharacterBug_C (new pawn)", line 7146);
   `player_marker.cpp:71` placed the slot marker ("card110: client slot 1 shown over BP_PlayerCharacterBug_C", line 7255).
   The blueprint's direct parent being `ASTPlayerCharacterBug` and not some other subclass is open (no asset read);
   it does not change the fix, which needs only the `ASTPlayerCharacter` base.
2. **The base class is already resolved.** Verified: `ASTPlayerCharacter_StaticClass`
   (`?Z_Construct_UClass_ASTPlayerCharacter_NoRegister@@YAPEAVUClass@@XZ`, `tools/symbols.txt:112`, RVA 0x050847D0 in
   `offsets.generated.h:103`) resolves into `g_player_character_class` (`hooks_ubw.cpp:2061`, declared
   `input_diag.cpp:27`), next to `g_player_mi_class` (`hooks_ubw.cpp:2119`). At least twenty sites already use it
   (`claim_c157`, `sprint`, `audit_npc`, `save_c121`, `remote_body`, `player_marker`, `pawn_collision`, ...).
   `is_a` is `FObjectPtr::IsA` (`ue/reflect.cpp:169`), which walks the super chain, so the base test accepts both
   pawn classes.
3. **What the hold waits for.** Verified (card 138, `begin_c138.cpp:39-153`): on a client, when
   `AGameStateBase::OnRep_ReplicatedHasBegunPlay` (RVA 0x03C624D0) runs with bReplicatedHasBegunPlay set, the dispatch
   of level BeginPlay is held until `GetPlayerController(world, 0)->Pawn` is a possessed player pawn. The reason
   (card 138): level blueprints read the local player character in BeginPlay (`BP_PickupTracker`:
   `GetWeaponInventory(GetSTPlayerCharacter())`, crash at exe+0x51EB9BA on null). `GetSTPlayerCharacter` reads the
   PC's player character slot, which `input_diag.cpp:85` fills for any `ASTPlayerCharacter`. The test written in
   `c138_local_mi` (`begin_c138.cpp:43`) is `is_a(pawn, g_player_mi_class())`, stricter than the reason: card 138
   chose MI because every client log it read had BP_PlayerCharacterMI_C as the first pawn.
4. **The condition holds on the bug pawn; only the class test fails.** Verified, c177-r2 client: OnRep_Pawn with
   Pawn 000000007E711780 BP_PlayerCharacterBug_C at 104161149 (line 7084), the player character slot set the same ms
   (line 7083), then the hold start at 104161150 naming that pawn (line 7124), then AcknowledgePossession and
   ClientRestart at 104161154 (lines 7138, 7145), `client wait state pawn=client-own` passed, and the hold-still-on
   WARNs at 10012, 20021 and 30025 ms all name the same pawn (lines 7366, 7386, 7404). With a base-class test the
   OnRep at line 7124 would have found the pawn and logged "no hold", the same as both MI maps did in this run
   ("card138: client t=104112108 no hold: ... (BP_PlayerCharacterMI_C)", line 1828; t=104149583, line 5195).
5. **What the hold blocked.** Verified: `c120_client_ready` (`chan_ops.cpp:83`) returns false while
   `c138_hold_ping()` holds, before the card 120 minimap check, so no Pong of the map went out and the host ran no
   join replay on BUG_C1M1. On the MI map the host's first-Pong replays are the card049 journal, card095 door,
   card098 world journal, card137 ext join, card132 pose, card177 sprite, card132 decal and card159 dropship lines
   (host log at 104151365-104151368). The minimap origin was already the bug pawn ("card058: client minimap
   000000007D997900 OriginActor 0000000000000000 -> 000000007E711780 (BP_PlayerCharacterBug_C)", line 7082), so the
   card 120 gate opens at the first check after the release.
6. **The BeginPlay readers on the bug map.** Verified for `BP_PickupTracker`: the host's map-load inventory for
   BUG_C1M1 has no `BP_PickupTracker_C` (it is on FED_C2M1, host line 2227). Open: whether any other bug-map level
   actor reads an MI-only field in BeginPlay. The host ran BeginPlay with its own bug pawn and survived 30 s plus
   (card 192 run), and the release keeps the host's order (pawn first, then BeginPlay), so the risk is low.

Verdict: `BP_PlayerCharacterBug_C` is an `ASTPlayerCharacter`, possessed and in the PC's player character slot before
the GameState's OnRep; the hold never released only because `c138_local_mi` tests `ASTPlayerCharacterMI`. Testing
`g_player_character_class` (already resolved) releases it on the bug pawn and changes nothing for the MI pawn.

## Sweep (step 2): every `is_a(..., g_player_mi_class())` site in `mod/src/ubw/`

58 sites. Session state: 6. MI-only feature (card 196): 52. A session-state row needs only a class swap when the
pawn is not read at an `ASTPlayerCharacterMI` offset after the test; the "fix" column says which.

| Site | Function | What the test gates | Verdict | Fix |
|---|---|---|---|---|
| begin_c138.cpp:43 | c138_local_mi | card 138 BeginPlay hold release, the card 120 Pong gate, the BP_PickupTracker note | session state (must accept the bug pawn) | class swap (this card) |
| restore_c143.cpp:77 | c143_send_reload | host Reload before a checkpoint-restore rejoin; `c143_snapshot` then reads `kMiWeaponInventory`, `kMiAmmoInventory` | session state (must accept the bug pawn) | split: health and armour on the base, weapons and ammo MI only |
| restore_c143.cpp:178 | c143_after_load_asc | downed-Health revive after a load (card 043 hold) | session state (must accept the bug pawn) | class swap (reads Health only) |
| restore_c143.cpp:202 | c143_host_assign | marks a rejoined copy for the held Restore | session state (must accept the bug pawn) | class swap, with :77 |
| restore_c143.cpp:221 | c143_host_step | the held Restore's "ready" (applies weapons, ammo) | session state (must accept the bug pawn) | split, with :77 |
| playlog.cpp:229 | pl_pawns | `g_pl.mi[s]`: play log side label (`pl_side`: host on a bug map logs "menu", c177-r2 host log), own CMC row; `pl_weapons` (:253) reads `kMiWeaponInventory` through the same flag | session state (must accept the bug pawn) | split: a player flag on the base for the label and CMC, the MI flag kept for `pl_weapons` |
| ammo.cpp:205 | detour_c71_add_ammunitions | ammo pickup relay (MI ammo inventory) | MI-only feature (card 196) | |
| ammo.cpp:253 | c71_receive | MI ammo inventory | MI-only feature (card 196) | |
| ammo.cpp:369 | c72_take_set | MI ammo inventory | MI-only feature (card 196) | |
| ammo.cpp:415 | c69_step | MI ammo step | MI-only feature (card 196) | |
| attributes.cpp:37 | player_asc | card 017 player ASC filter | MI-only feature (card 196) | |
| attributes.cpp:157 | card017_step | card 017 attribute step | MI-only feature (card 196) | |
| attributes.cpp:193 | detour_init_features | InitFeatures log cap | MI-only feature (card 196) | |
| attributes.cpp:654 | log_c20_weapon | weapon log label | MI-only feature (card 196) | |
| attributes.cpp:915 | detour_fire_projectile | shooter of a soldier projectile | MI-only feature (card 196) | |
| attributes.cpp:1163 | c76_mark_task | grenade task owner (mech, turret, soldier) | MI-only feature (card 196) | |
| chan_ops.cpp:24 | c26_host_fire | Fire relay rider | MI-only feature (card 196) | |
| chan_ops.cpp:28 | c26_host_fire | soldier rounds on release (card 175) | MI-only feature (card 196) | |
| chan_ops.cpp:35 | c26_host_use | Use relay (card 043 revive) | MI-only feature (card 196) | |
| death.cpp:1449 | c44_client_death | MI vs base StartDeath trampoline (the bug pawn takes the base one) | MI-only feature (card 196) | |
| death.cpp:1494 | c44_host_fire | downed soldier fire | MI-only feature (card 196) | |
| death.cpp:3014 | c72_client_step | MI ammo inventory | MI-only feature (card 196) | |
| enemies.cpp:902 | c40_process_begin | shot label "relayed" | MI-only feature (card 196) | |
| fire.cpp:112 | c26_own_fire_pawn | soldier weapon ASC owner | MI-only feature (card 196) | |
| fx_c136.cpp:334 | c136_log_event | first-person FX log | MI-only feature (card 196) | |
| fx_c136_net.cpp:181 | fp_view | first-person FX view | MI-only feature (card 196) | |
| grenades.cpp:648 | c76_relay | grenade relay | MI-only feature (card 196) | |
| hooks_ubw.cpp:812 | c29_server_copy | vehicle server copy | MI-only feature (card 196) | |
| hooks_ubw.cpp:1049 | c43_enter_down | weapon ASC cancel at a down | MI-only feature (card 196) | |
| hooks_ubw.cpp:1188 | c48_inventory_text | MI weapon inventory text | MI-only feature (card 196) | |
| hooks_ubw.cpp:1451 | c72_mirror_apply | ammo mirror | MI-only feature (card 196) | |
| input_diag.cpp:1169 | c160_dump_tag_map | tag map dump | MI-only feature (card 196) | |
| input_diag.cpp:1534 | c21_log_tag_pressed | tag press log | MI-only feature (card 196) | |
| input_diag.cpp:1571 | c29_note_activation | activation owner | MI-only feature (card 196) | |
| input_diag.cpp:1572 | c29_note_activation | activation log | MI-only feature (card 196) | |
| jump_c94.cpp:74 | c94_jump_call | jump log | MI-only feature (card 196) | |
| jump_c94.cpp:283 | detour_c101_lerp_tick | jump lerp | MI-only feature (card 196) | |
| jump_c94.cpp:307 | detour_c101_lerp_end | jump lerp | MI-only feature (card 196) | |
| remote_body.cpp:651 | c192_mi_pawn | card 192 MI inventory guard | MI-only feature (card 196) | |
| sprint.cpp:915 | c22_sprint_dump | sprint log | MI-only feature (card 196) | |
| testrun.cpp:1687 | c63_pawn_text | test pawn text (MI weapon inventory) | MI-only feature (card 196) | |
| turret_sync.cpp:429 | c125_client_arms | turret rider arms | MI-only feature (card 196) | |
| turret_sync.cpp:437 | c125_client_arms | turret arms | MI-only feature (card 196) | |
| vehicles.cpp:296 | c29_scan | vehicle scan pawn class | MI-only feature (card 196) | |
| vehicles.cpp:579 | c29_drive | vehicle drive | MI-only feature (card 196) | |
| vehicles.cpp:1691 | c75_sender_copy | vehicle sender copy | MI-only feature (card 196) | |
| weapons.cpp:318 | c21_grant_step | weapon grant | MI-only feature (card 196) | |
| weapons.cpp:321 | c21_grant_step | weapon grant | MI-only feature (card 196) | |
| weapons.cpp:324 | c21_grant_step | weapon grant | MI-only feature (card 196) | |
| weapons.cpp:531 | c21_parity_step | weapon parity | MI-only feature (card 196) | |
| weapons.cpp:663 | c50_host_poll | MI weapon inventory | MI-only feature (card 196) | |
| weapons.cpp:865 | c50_client_step | MI weapon inventory | MI-only feature (card 196) | |
| weapons.cpp:888 | c50_after_assign | starting inventory (calls `c143_host_assign` at :885, before this test) | MI-only feature (card 196) | |
| weapons.cpp:1138 | c57_receive_equip | weapon equip | MI-only feature (card 196) | |
| weapons.cpp:1294 | c141_receive_switch | weapon switch | MI-only feature (card 196) | |
| weapons.cpp:1325 | c141_step | equipped weapon | MI-only feature (card 196) | |
| weapons.cpp:1334 | c141_pawn_of | weapon avatar to pawn | MI-only feature (card 196) | |
| weapons.cpp:1599 | c50_test_client | test interactor | MI-only feature (card 196) | |

## Fix (step 3, proposed, not written)

In `begin_c138.cpp`, `c138_local_mi` becomes `c138_local_pawn`: the test at :43 becomes
`is_a(pawn, g_player_character_class())`, the `ASTPlayerCharacter` base both pawn classes share. Its three callers
(the detour :66, `c138_step` :101, `c138_note_begin_play` :146) and the comments at :10, :38 and :87 say "player
pawn" instead of "MI pawn". The card 154 lines at :57 and :60 stay: the exception keeps the hangar out whatever its pawn class is. No new log line, no new WARN class. The card 154 menu-map exception stays. Only this file
changes; the other five session-state rows are in "Problems for the senior" below, since each needs a split and not a
swap.

## Predictions, run c195-r1 (the bug-side part of c177-r2)

1. Client, BUG_C1M1: "card138: client t=N no hold: GameState G begun play with pawn P (BP_PlayerCharacterBug_C) on
   PC C". If the OnRep comes before OnRep_Pawn instead: "card138: client t=N hold start ...", then "card138: client
   t=N pawn possessed: P (BP_PlayerCharacterBug_C) ..." and "card138: client t=N release after M ms: BeginPlay
   dispatch ran", M under 1000.
2. Client: "card120: client t=N minimap origin P (BP_PlayerCharacterBug_C) set: Pong to the host from now on", P the
   same pawn.
3. Host, after that Pong: the first-Pong replay lines for BP_PlayerController_C (card049 journal, card095 door,
   card098 world journal, card137 ext join, card177 sprite replay, card132 decal), with the counts the log gives.
4. Client: no "WARN card138: client hold still on" line, no "WARN card138: client hold dropped at LoadMap".
5. FED_C2M1 in the same run: "card138: client t=N no hold: ... (BP_PlayerCharacterMI_C)" and the BP_PickupTracker
   BeginPlay line naming the MI pawn, as in c177-r2 lines 1828-1829.
6. Both games alive 30 s after LoadMap returned on BUG_C1M1, both pawns possessed (the card 192 checks); WARN classes
   per side equal the c177-r2 baseline minus the card138 class.

## Problems for the senior

- **Five more session-state rows need the bug pawn, and none is a plain swap.** `restore_c143.cpp:77/221` feed
  `c143_snapshot`, which reads `kMiWeaponInventory` and `kMiAmmoInventory` unguarded (`restore_c143.cpp:58-62`); a
  checkpoint restore on a bug map today drops the Reload with "WARN card143: ... no player pawn", so the rejoined
  client loses its Health and armour. `playlog.cpp:229` labels the host "menu" on BUG_C1M1 (c177-r2 host log: 8
  "playlog: menu" lines after the travel) and skips the own CMC row; its flag also guards `pl_weapons`, which reads
  `kMiWeaponInventory`. A swap there would read MI offsets on a bug pawn, the card 192 crash class. Proposed: one
  card for the three splits (player flag on the base, MI inventory reads kept MI only). This conflicts with this
  card's "fix only the first kind here"; your call whether they come in here or in their own card.
- `death.cpp:1449`: a client bug-pawn death calls the base `StartDeath` trampoline, so the `ASTPlayerCharacterBug`
  override (card 030: it cancels the equipped weapon's ASC abilities) does not run on the client's real death. It is
  listed for card 196.
- Open: the direct native parent of `BP_PlayerCharacterBug_C` (asset not read). Only the `ASTPlayerCharacter` base is
  needed, and it is verified at run time.

## Build for c195-r1

`mod/src/ubw/begin_c138.cpp`: `c138_local_mi` renamed `c138_local_pawn` (three callers). The test is
`is_a(pawn, g_player_character_class())`. When `g_player_character_class` is null (the resolver did not find
`ASTPlayerCharacter_StaticClass`), the possessed pawn itself is returned, so the hold still releases at possession
instead of never. This is the claim_c157.cpp:211 null check, with "release" as the fallback in place of "no match".
The comments at the file header, :39 and the `c138_step` comment now say player pawn. The card 154 menu-map
exception is unchanged. No new log line. Build clean, DLL 4a67eed77aa38506.

Scenario `tools/scenarios/c195-r1.txt`: FED_C2M1 (an MI map), then `host travel next` to BUG_C1M1. It runs with the
c177-r2 host env (`HOST_EXTRA_ENV="UBWCOOP_AUTODROPSHIP=1 UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C2M1?listen
UBWCOOP_GOD=1"`), which the travel to the bug map needs.

## Run c195-r1 results

Exit 0. Drive 25 of 25 ok, "scenario passed", ended by the scenario after 70 s. No crash dumps. Dead time:
"dead time: sleeps 30.0 s, timed-out try waits 0.0 s in 0 (none)".

1. Pass. "card138: client t=104933810 no hold: GameState 0000000089EDAD20 begun play with pawn 000000004682F460
   (BP_PlayerCharacterBug_C) on PC 00000000AEF10BE0" (client log line 3032).
2. Pass. "card120: client t=104934594 minimap origin 000000004682F460 (BP_PlayerCharacterBug_C) set: Pong to the host
   from now on (held 788 ms)" (line 3103). It is the same pawn.
3. Pass. The host replays to BP_PlayerController_C at t=104934619 (host lines 4128-4163):
   - card049 journal 0 of 0 frames;
   - card098 world journal 1 of 1;
   - card137 ext join 71 of 71 live rows;
   - card132 pose 0 rows;
   - card177 sprite 0 rows sent of 0;
   - card132 decal 1 frame sent;
   - card159 dropship snapshot 3 of 3;
   - card136 fx join replay with 6 asset rows and 7 name rows.

   card095 logged no line on either map in this run. It logs only when its journal is not empty
   (`door_c95.cpp:378`).
4. Pass. There is no "WARN card138" line in the client log.
5. Pass. On FED_C2M1: "card138: client t=104924296 no hold: GameState 000000007BACC6A0 begun play with pawn
   00000000726FF1D0 (BP_PlayerCharacterMI_C) on PC 0000000071D4D180" (line 1828). Then "card138: client
   t=104924301 BeginPlay of BP_PickupTracker_C: local pawn 00000000726FF1D0 (BP_PlayerCharacterMI_C)" (line 1829).
6. Pawn checks pass, WARN check fails:
   - Pawn checks: "state pawn=host-own" and "state pawn=client-own" passed after the 30 s hold on BUG_C1M1, both games
     ran to quit, and there were no dumps.
   - WARN classes, host: only `WARN attach`, the same as the c177-r2 baseline.
   - WARN classes, client: `WARN attach` (baseline); the card138 class is gone (4 lines in c177-r2). Two classes are
     new, both on BUG_C1M1, from code that now runs because BeginPlay and the Pong are no longer held:
     - `WARN card073` (3 lines, t=104934546): "GA_CharacterJump_C: no SourceItem property, SourceItem not set". The
       same line comes for `BP_BugForm_VampirismAbility_Warrior_C` and `GA_BugTaunt_C`.
     - `WARN card132` (2 lines, t=104960656): "mismatch STLevelObjective key=88364238 presence host 1 client 0; no row
       sent", then "client round 1: actors 1122 host, 1121 client, mismatches 1 (classes STLevelObjective:1)".

The goal is met: the hold releases on the bug pawn, and the Pong and the join replays follow it. The two new
WARN classes are outside the goal; they are under Problems below. No further run was made.

## Problems for the senior (c195-r1)

- **card132 mismatch on BUG_C1M1:** an `STLevelObjective` (key 88364238) exists on the host and not on the client,
  in digest round 1, 26 s after the release. This breaks the 0-mismatch gate on the bug map. It is a world-sync gap
  of the card 132 class: either an objective the bug map's level script spawns on the host only, or one spawned in
  a BeginPlay that the c177-r2 hold used to block. Not traced.
- **card073 on the bug pawn's abilities:** the card 073 grant binding (`grenades.cpp:135`) looks for a `SourceItem`
  property, which `GA_CharacterJump_C`, `BP_BugForm_VampirismAbility_Warrior_C` and `GA_BugTaunt_C` do not have. The
  grants still bound ("bound to replicated spec h179/h180/h181"). It is an MI-weapon assumption, the card 196 class.

## Senior review (2026-11-12)

Accepted on c195-r1 (DLL `4a67eed77aa38506`, run 1 of 3). The diff against the pre-195 snapshot is
`begin_c138.cpp` and the new scenario only; the senior re-read the client and host logs: no hold on the bug pawn,
Pong 788 ms later, host join replays sent, no card138 WARN, MI map unchanged, host WARN classes equal the baseline.
Prediction 6's two new client WARN classes come from code the old hold blocked and are outside this goal: the
`STLevelObjective` presence mismatch is card 199, the card 073 "no SourceItem property" lines are in card 196. The
five other session-state sweep rows are card 198.
