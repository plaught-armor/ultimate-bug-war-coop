# 310 Level-script per-player writes reach every player (audit class F)

From card 301 (objective co-op audit), class F. Senior trace 2026-11-14, static plus the b11 soak and the playtest
logs. Cause settled; code may start after the b12 batch.

## Cause

Level scripts write per-player state on `GetSTPlayerCharacter(self)` (imp577), which is player 0. On the host that
is the host's pawn, or inside a card 102 scope (a server copy's overlap, interaction or damage) the acting copy. So
each write lands on one pawn: the host's when a timer, delegate or objective event triggers it, the acting client's
copy when that client's overlap triggers it. The other players never get it.

None of the state replicates. b11 soak run 1, host and client rows: `ADFactionComponent`, `STSquadLeaderComponent`
and `STTacticalSupportComponent` each `bReplicates 0`, CPF_Net only on `ActorComponent.bReplicates` and
`bIsActive`. Loose gameplay tags and inventories are per machine (`engine-facts.md`).

## What already works (do not rebuild)

- Weapons, host pawn to copies: card 267 (`weapons.cpp`, `c267_frame_kind`, `c267_mirror_add/give/drop`) mirrors a
  level-script `AddWeapon`, `AddWeaponByTag`, `GiveWeaponByClass` and drop on the host's pawn to every copy; card
  050 sends each copy's list to its client. Playtest C1M1 host log: 11 `card267: host level script ... mirrored to
  copy` rows, covering `RemovePlayersWeapons`, `GiveMorita`, `GiveLaser` and `GiveUnarmedWeapon`.
- Tactical support pickups: per player, both sides (`EquipTacticalSupportAbility` caller exe+0x51D2A9C on the
  client and on its copy). Slot use: card 100; playtest charges agree on both sides (17000, 10250, 20000, 10000).
- Grenade removal on a copy: card 073.

## Gaps (FED_C1M1 sites; the same calls recur on the other maps)

| Write | Site | How it is made | Who reads it |
|---|---|---|---|
| Weapons when player 0 is a copy | `RemovePlayersWeapons`, `GiveMorita`, `GiveLaser`, `GiveUnarmedWeapon`, `EnableLeftWeapons` reached from a client's overlap | The card 267 calls; `c267_frame_kind` mirrors only from the host's pawn (`weapons.cpp:86`, `:55`) | The host's own inventory, and the other copies |
| Loose tags `PlayerAbilities.TutorialBlock`, `TutorialBlockTaunt`, CTF flag carry | @25052 add, @25395 remove, @22700-23223 | `AddLooseGameplayTags` (imp650), `RemoveLooseGameplayTags` (imp651) | The owning machine: abilities activate locally, so the client needs the tag on its own pawn; the host needs it on the copy |
| Tactical support default class | @25461 | A property write: `TacticalSupportComponent.DefaultTacticalSupportClass = ...`, no call | The re-equip after a throw (caller exe+0x50E3A66) on the owning machine and on the copy |
| Tactical support chargeable | @26754, from `SetTacSupportChargeable(bool)` | imp609 on the component | The copy (card 100 runs the slot there) and the client's HUD charge |
| Faction | @354 `Faction.BlueTeam`, @11867 `Faction.EnemyNumberOne`, @22204, @24551 | imp565 on `FactionComponent` | Host AI targeting, so host pawns only |
| Squad leader | @770 on, @17136 off | imp607 on `SquadLeaderComponent` | Host AI; the client's squad list is card 121 |
| Item removal | @25112, @25212 `RemoveItemByDefinition(..., 15, true)` | imp586 on `ItemInventory` | Each machine's inventory; card 073 already relays a copy's removal |

Out of scope here: teleport @24304 and whipping @23548 / @28404 (placement, class D and card 300 rule), class G
(position tests on player 0), class B (delegate binds on player 0).

## Fix: one manager, the card 267 rule in both directions

A level-script per-player write is applied to every player pawn on the host, whoever triggered it, and sent to
each owning client where that client reads the value.

1. Generalise `c267_frame_kind`: a call counts when the frame's object is the host's level script and the target
   is any player pawn, host-own or a copy. The mirror target list is every other player pawn. A re-entry guard
   keeps a mirrored call from mirroring again.
2. Add the call natives to the same table: `AddLooseGameplayTags`, `RemoveLooseGameplayTags`, the faction setter
   (imp565), the squad leader setter (imp607), the chargeable setter (imp609), `RemoveItemByDefinition` (imp586).
   Resolve the three unnamed imports with the hash method in `objective-coop-audit.md` (imp572 note). One table
   row per native: thunk, argument copy, "client reads it" flag.
3. `DefaultTacticalSupportClass` has no call. Host, 1 Hz in the existing player step: compare each player pawn's
   value to the last synced one; a changed pawn is the source, the others get the value.
4. Client relay for the rows flagged "client reads it" (tags, default class, chargeable, item removal): one chan
   op carrying write kind and payload (tag names as text, class path, bool, item path and count), applied on the
   client's own pawn by the same native. Journal the rows per map so a client that joins or restarts later gets
   the current state (the card 267 replay list is the model, `weapons.cpp:171`).
5. Faction and squad leader: host pawns only, no client row.

Not a per-objective patch: no script offsets in code. The table names natives, not maps.

## Sibling sweep

- Every FED map script: grep the listings for imp577 followed by a write that is not one of the natives above;
  each new native is one more table row. List them in the report before coding.
- Checkpoint load (caller exe+0x50F83A1, flag 1): the save holds the host's tactical support only. Playtest host
  log: the host's component is restored `Resupply` then `BombingRun`, the copy `Resupply` only, and the client never
  runs the load. A client that held a picked-up ability loses it on every host checkpoint load. Same owner as
  card 278 (weapons after a load): keep each copy's equipped classes across the load and re-equip, then relay.

## Step (a) result: imports and sibling sweep (junior 2026-11-14, senior checked)

Imports (CityHash64 of the lower-case UTF-16 path, top 2 bits cleared): imp565
`ADFactionComponent:SetFactionTag`, imp607 `STSquadLeaderComponent:SetEnabled`, imp609
`STTacticalSupportComponent:SetCanTacticalSupportBeCharged`, imp586 `STItemInventory:RemoveItemByDefinition`,
imp650 / imp651 `AbilitySystemBlueprintLibrary:AddLooseGameplayTags` / `RemoveLooseGameplayTags`, imp577
`STCommonUtilityLibrary:GetSTPlayerCharacter`.

Sweep over all 8 FED listings. The junior's script read only lines that name the imp577 return value; the senior
added the cast variable, name-called methods and the other player getters (`GetSTPlayerController`,
`GameplayStatics:GetPlayerCharacter` / `GetPlayerController`). C3M1, C3M2, C4M1: no player 0 site.

- One new table row: FED_C2M1@18547 `STWeaponUtilityLibrary:AddAmmunition(player 0, AmmunitionType.Nuke, 2)`.
  Before coding it, trace which machine reads that ammunition count (card 050 / 267 may already carry it).
- Table rows confirmed: `SetFactionTag` C1M1@11867, @22280, @24572 (@354 loops `GetAllActorsOfClass`, not player
  0); `SetEnabled` @770, @17136; `RemoveItemByDefinition` @25112, @25212; tag add @23223, @25052, remove @23079,
  @25395; `DefaultTacticalSupportClass` @25461 (the only property write on player 0 in any map); chargeable @26754
  through the getter at @26673.
- Cast variable (`AsBP_Player_Character_MI`): only the card 267 weapon calls (`AddWeaponByTag`, `GiveWeaponByClass`,
  `GetWeaponBySlot` reads). Fix item 1 covers them.
- Not this card: C1M1@23583 `CharacterMovement.Deactivate()` (2 sites) and @23663
  `GetSTPlayerController.EnableInput` (whipping, card 300 rule); C2M1@18204 `OnWantsToBoardMech` bind (class B);
  C4M2@20573 `IsOverlappingActor(GetPlayerCharacter(0))` (class G); C4M2@32831 `SetControlRotation`, @32943
  `SetActorLocation`, C1M1@24425 (class D); C5M2 `ApplyDamageAtLocation` x4 and @28906 (host damage, player 0 as
  instigator only).

## Rows (each write, both sides)

- Host: `card310: host level script <native>(<args>) on <pawn> (<side>) applied to <n> other player pawn(s),
  client row <sent | not needed>`.
- Client: `card310: client <native>(<args>) applied on the own pawn: <result>`.
- Map end, both sides: `card310: map totals: writes <n>, mirrored <n>, client rows sent <n> applied <n>`; WARN when
  sent and applied differ.

## Predictions for the proof run (C1M1, client does every step, host stands off)

- A client-triggered `RemovePlayersWeapons` / `GiveMorita` shows the host row with the copy as source and the
  host's pawn as a mirror target; host and client weapon lists agree after it (card 050 `matches the host`).
- `TutorialBlock` add and remove appear as one host row and one client row each.
- After `ChangeTacSupportToTutFlavour` every player pawn's default class is the tutorial class; the re-equip after
  a throw shows that class on the client and on its copy, not `Config_TacSupp_Resupply_C`.
- Sent equals applied in the map totals; zero WARN.

## Done when

The rows above appear in one C1M1 run with the client acting, the sibling list is in the report, and the checkpoint
load keeps a client's picked-up tactical support.

## b14 drive-log audit (junior 2026-11-14)

Source: `logs/b14-soak-c1m1.drive`, every FAIL / VOID / skip line (196 lines matched; 112 distinct steps once the
repeated per-target triples are folded). Line numbers are b14 scenario lines. Causes are read from the b14 host and
client logs. No step is put down to "environmental".

| b14 line(s) | Failing step | Cause from the logs | Covered by |
|---|---|---|---|
| 114 | `host objective now`: "no current step" | `c126_active_parent` did not see TacSupport_O1 as active, so autoplay found no open step | b15 autoplay active-parent change (accepted in the b15 review) |
| 115, 123, 132, 137, 145 | `objective expect TacSupport_SO2/SO3/SO41/SO5/SO6`: VOID "not under the active parent" | same active-parent cause | b15 active-parent change |
| 125 | `try host objective go all`: TRY-FAIL "no current step" | same active-parent cause (a try line, the run did not fail on it) | b15 active-parent change |
| 167 | `objective expect ShootingRange_SO1`: VOID "complete" | SO1 completes when it is added (the second Morita pickup), so the expect saw it already complete | b15: the expect is replaced by the card049 `CompleteSubObjective(ShootingRange_O1, ShootingRange_SO1)` wait |
| 180 | `wait log AddCountSubObjective(ShootingRange_O1, ShootingRange_SO3)`: timeout | the level script sends it at map start, before the step's wait, so no new row comes | b15: the wait is dropped |
| 186-233 (each range target: look, key, IncrementObjectiveCount wait, card065 Activate wait) | client looks VOID "aim hit BP_PlayerCharacterMI_C_2147479339"; keys SKIPPED; IncrementObjectiveCount count 1..9/10 and card065 Activate(1) for C_11, C_3, C_4, C_10, C_5, C_6, C_9, GrenadeRangeTarget_C_1 time out | the host pawn stood in the client's line of fire (`go all` put both players at the range), so no shot hit a target; card065 Activate(1) rows exist only for TargetRangeTarger_C_1, C_2 and GrenadeRangeTarget_C_1 (the grenade target was activated, its throw was skipped), and the host log has no native IncrementObjectiveCount call (only the two hook-install rows and the card049 UFunction params row) | b15: `objective go client` only, the host stays back |
| 234-237 | SO3 wait (count 0/10), client IncrementObjectiveCount replay, `CompleteObjective(ShootingRange_O1)` host and client waits | downstream of 186-233: SO3 never counted, the host log has 0 `CompleteObjective(ShootingRange_O1)` rows | b15 range fix above |
| 241 | card161 door `BP_Door_Sliding_Large_C_0 DoorState 2 -> 1`: timeout | the door opens on range completion: in c240-c1m1-r3 the host log has `sent CompleteObjective(ShootingRange_O1)` at 158987042 ms and the DoorState 2 -> 1 row at 158991029 ms; b14 has no completion, so no door row | b15 range fix |
| 245-248 | client/host `wait goto` timeouts to presence C_7, card227 presence fired, `CompleteSubObjective(ShootingRange_O1, ShootingRange_SO0)` | the path runs through door C_0, still closed (241) | b15 range fix |
| 250, 257, 258, 261, 263, 270, 271, 278 | `objective expect/wait CaptureTheFlag_SO1..SO4`: "not in the map", current ShootingRange_SO3 | CaptureTheFlag_O1 is added only after ShootingRange_O1 completes; the run was still on ShootingRange_SO3 | b15 range fix |
| 252-256 | goto `BP_WeaponPickup_LaserMorita_C_0` timeout; look VOID "aim hit BP_Door_Sliding_Large_C_0"; key E skipped | the pickup is behind the closed door C_0 | b15 range fix |
| 262 | client card161 `DoorOp state 3 BP_Door_Sliding_Large_C_1`: timeout | that door is driven by the CTF objective, never started | b15 range fix |
| 265-269, 273-277 | gotos to `BP_SatchelItemPickup_CTF_C_0` and `BP_SatchelPlacementSite_Flag_C` time out; looks VOID "aim hit Actor_1"; keys skipped | same: the CTF area is behind closed doors and the objective is not in the map | b15 range fix |
| 279-281 | `CompleteSubObjective(CaptureTheFlag_O1, CaptureTheFlag_SO0)`, `CompleteObjective(CaptureTheFlag_O1)` host and client | downstream: CTF never added; the host log has 0 `CompleteObjective(CaptureTheFlag_O1)` rows | b15 range fix |
| 338, 339 | client/host `wait goto` to the Asteroid presence: "no path" | the route is closed by the CTF doors (CTF not complete) | b15: warp fallback by actor (`host warp actor\|client BP_PlayerPresence_C_11 60`), gated on no `CompleteObjective(CaptureTheFlag_O1)` row |
| 340-343 | `AddObjective(Asteroid_O1)` host and client; card018 `WB_TrooperLevelEnd` UI-only on both sides | downstream: the Asteroid presence was never reached | b15 warp fallback |
| 348-360 | `unless ... host ui click` level-end buttons and the 3D_Frontend world wait | no level-end widget (342-343), so no buttons to click and no frontend travel | b15 warp fallback (chain resumes when Asteroid completes) |
| 361-365 | card308 journal peak, card098 and card132 map totals, card304 audio rows, client card045 reconnect, all since-mark | these rows are written at map end; the map never ended | b15 warp fallback |
| 378, 379 | card045 `Browse URL ... FED_C2M1`; `wait state map=FED_C2M1`: VOID, last state FED_C1M1 | downstream of no level end | b15 warp fallback |

Lines 188-233 key skips, 325, 349-357 odd and 367-377 are `ok` try lines or skips caused by the look gates above;
none failed the run on its own. Open (no cause found): none.

## b15 proof-run scenario audit (junior 2026-11-14)

Two scenario faults, not mod faults:

| Line | Step | Cause | Fix (b16) |
|---|---|---|---|
| 100 | `host objective expect Assault_SO3`: VOID "complete; doable: Assault_SO4" | the level script completes SO3 by itself 1.1 s after SO2 (host t=283542542 SO2, 283543642 SO3; b14 1.4 s) with no player step, so the expect races it | wait on `card049: host sent CompleteSubObjective(Assault_O1, Assault_SO3)`; the SO3 clear and gos are gone |
| 141 | `client look actor {g1} 0`: "could not convert string to float: 'BP_EncounterManager_C_1'" | drive.py formatted every bare `{gN}` as a float | drive.py: a bare `{gN}` whose group is not a number gives the raw text; `{gN+k}` still needs a number. 418 bare uses in 34 scenarios, all numeric before, same output |

Self-completing sub-objectives (b14 and b15 host card049 rows) and how the scenario meets each:

| Sub-objective | Completes | Scenario step |
|---|---|---|
| Assault_SO1 | same ms as Assault_SO2 is added | wait log on the add row, then `objective wait` |
| Assault_SO3 | 1.1-1.4 s after Assault_SO2 | b16 wait log row (above) |
| TacSupport_SO4 | same ms as TacSupport_SO3 (b14 279153307) | `objective wait` only, no expect |
| TacSupport_SO7, SO0 | same ms (b14 279196755) | `objective wait` / wait log only |
| ShootingRange_SO1 | same ms as it is added (b14 279205887) | wait log on the complete row (b15) |

The other expects (Assault_SO0/SO2/SO4, TacSupport_SO2/SO3/SO41/SO5/SO6, ShootingRange_SO2/SO3) complete only on a
player step. The CaptureTheFlag expects have no run evidence yet (b14: not in the map; b15 stopped at the crash).

## Closed (2026-11-17 16:37)

Proven in c448b192a (b192 `ebf720195a29235d`), both logs read by the senior:
- Host-own write mirrored to the copy and sent: `SetCanTacticalSupportBeCharged(true) on BP_PlayerCharacterMI_C (host-own) applied to 1 other player pawn(s), client row sent`, client `C 1 applied on the own pawn`.
- Client-triggered write (copy as source) mirrored to the host pawn: `... (server-copy) applied to 1 other player pawn(s), client row sent` at 593584254 and 593657821, applied on the client 4 ms and 19 ms later.
- Join replay: `join rows to BP_PlayerController_C at its first Pong (card 139): ... chargeable 1, default class Config_TacSupp_Resupply_C; 2 client row(s) sent, 0 not sent`, and the client applied C and D rows. A row sent before the first Pong is deferred and sent at it.
- Map totals: client received 6 applied 6, then 7 applied 7; 0 card310 WARN on either side. No crash in the run.

Not done: this run did not exercise the loose tag rows (TutorialBlock add and remove), a changed default class (ChangeTacSupportToTutFlavour), item removal, or a level-script weapon call with a copy as player 0. They go through the same manager and table as the proven chargeable and default class rows.

Reopen when: any `card310` map total shows sent and applied differing, a `WARN card310` row appears, or a tag, default class, item removal or weapon write is seen on one player and not the other (in a run or the playtest).
