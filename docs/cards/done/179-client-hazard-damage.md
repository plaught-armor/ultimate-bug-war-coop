# Card 179: The client takes the host's hazard damage and hears it (finding 1)

Status: queued 2026-11-11, split from card 144. Budget: three runs. One goal: a client pawn standing in a hazard zone
the host owns takes the host's damage and plays the hazard sound.

Read: card 144 (the client's own hazard logic; run 179 after it, since 144 decides who owns the zone's collision),
docs/STATE.md "Card scope and investigation rules".

## Why (playtest 2026-11-08)

- Jumping onto the barbed wire on the FED_C1M1 course gave the client no sound and no damage
  (BPHazardousZone_BarbedWire_01/02, ASTHazardousZoneBase DamageActor). At map load the client's own game turned a
  BPHazardousZone_C collision 3 -> 0 by local sim (caller exe+0x514542A, client t=258272260).
- Added scope: whether the host's zone damages the client's server copy (overlap or "the player" gate), where the
  hazard sound plays, and that the client hears and feels it. Proof: client pawn on the wire takes the host's damage
  and plays the hazard sound.

## Senior trace (verify each line before code; mark verified or open in a Trace section)

- Open: `ASTHazardousZoneBase` DamageActor on the host: the overlap that feeds it, and whether it accepts a server
  copy (a "the player" gate such as GetPlayerCharacter(0) or a class cast), with addresses and kis.py bytecode for
  BPHazardousZone_BarbedWire_C.
- Open: how the damage reaches the client's health (the copy's replicated ASC or a mod relay), and where the hazard
  sound is played (host 2D, 3D at the zone, or a cue); which existing manager carries it (card 136 effects, card 160
  server-copy effects).
- Open: every hazard zone class on every map from the asset list; the Report lists covered and not covered.

## Predictions (write the exact lines before the run)

- Host: DamageActor on the server copy, one line per tick while the client stands on the wire.
- Client: health drops by the host's amount; the hazard sound line on the client; a receiver-side screenshot or the
  damage vignette line.
- WARN classes: baseline only.

## Files

- Named by the trace; the senior confirms the list before code.

## Done when

- Trace, change, run lines quoted, the class list, the usual Report items. The Report ends with "Report complete."

## From card 144 (senior, 2026-11-12)

The collision 3 -> 0 at map load (caller exe+0x514542A) is the `ASTHazardousZoneBase` constructor writing
HazardEndPoint on an archetype, on host and client alike: it is not client-local logic and does not explain the
missing damage. The zone's collision is the same on both sides, so the trace starts at the host's overlap and
DamageActor path for the server copy.

## Trace (junior, 2026-11-12; static only, no build or run)

Tools: `dis.py EXE <rva> <len>` (llvm-objdump with PDB publics), `kis.py <uasset>`, `strings` on the extracted
uassets and umaps, grep on logs/. Exe RVAs below; absolute address = 0x140000000 + RVA.

### a. Host overlap and DamageActor: does it accept the client's server copy?

| Step | Address | Finding | Mark |
|---|---|---|---|
| BeginPlay binds the overlap | ASTHazardousZoneBase::BeginPlay 0x514DED0; FindFunction/AddUnique at 0x14514DFAA/0x14514DFBA and 0x14514E061/0x14514E071 | Binds OnBeginOverlap/OnEndOverlap on the component at this+0x318 (`<HazardousDetection>`). Then SetTickFunctionEnable(active +0x2F8 != 0 or move state +0x2F9 >= 2) at 0x14514E134..0x14514E15F. No role, authority or local-control test, so the zone runs on host and client alike. | verified |
| Active default | ctor 0x5144FB0, `movb $0x1,0x2f8(%rdi)` at 0x145145038 | bIsHazardActive defaults to 1. FED_C1M1's zone reads active on both sides: card 132 ext row `BPHazardousZone_C BPHazardousZone_C_2 at (5391, 28904, 1092) 0x01000001 -> host 0x01000001` (c140k-client.log). | verified |
| Overlap entry | OnBeginOverlap 0x51629A0 | Null test of OtherActor, IsA ASTCharacter (GetPrivateStaticClass 0x5039980 and class-chain compare), not-pending-kill (ObjectFlags bit 30), then Emplace(OtherActor) into ContainedActorMap at this+0x330. No GetPlayerCharacter, IsLocallyControlled, team or tag test. (The GetPlayerCharacter(0) compare at 0x145162A66 belongs to the next function, the ASTJumpPad overlap.) | verified |
| Tick | ASTHazardousZoneBase::Tick 0x51722D0 | `cmpb $0,0x2f8(%rbx)`, then ApplyDOT at 0x14517241D. No role test. | verified |
| DOT loop | ApplyDOT 0x514BBF0 | Walks ContainedActorMap (+0x330) with the per-entry timer at +8, valid-object test, DamageActor at 0x14514BD96. No player gate. | verified |
| Damage | DamageActor 0x51527F0 | Reads the zone data (this+0x2F0, IsA USTHazardousZoneData), the zone owner weak pointer (+0x4A0, falls back to the zone), and calls USTCombatUtilityLibrary::ApplyDamageToTarget 0x522EA40 with the data's FSTDamageTemplate (data+0x30) at 0x14515290A. No player gate. | verified |
| Damage apply | ApplyDamageToTarget 0x522EA40 | FSTDamageTemplate copy, GenerateHitResult, ApplyDamageToActor 0x522DAE0 / ApplyDamage 0x522B460. No PlaySound or cue call in the function. | verified |
| The test that drops the copy | none | No test on the path drops the server copy. The copy's capsule generates overlaps with the zone's HazardousDetection profile: hb68-host.log has `card137: host zone BP_HazardousZone_Acid_C DamageActor on BP_PlayerCharacterMI_C BP_PlayerCharacterMI_C (side 1, call 2): health 24.0 -> 23.0`. | verified for spawned zones |
| Static zone and moving copy | CMC ServerMove on the host (sprint.cpp:993 comment) | The copy moves through the engine's ServerMove, which sweeps and updates overlaps, so a placed zone should see the copy walk in. No log anywhere has a `host zone BPHazardousZone_C` line on either side (`grep -ho "host zone [A-Za-z_0-9]* DamageActor on ..." logs/*.log` lists Acid and IncendiaryGrenade only), so nobody, host or client, has been measured on the wire. | open (run part 2 and 3) |
| Wire damage amount | BPHazardousZoneData_RazorWire CDO | The data BP has unversioned properties; its tail holds 0x3D4CCCCD (0.05) and 0x3E800000 (0.25), field names not decoded. Whether the wire hurts anyone in the base game is not known statically. | open (run part 1, host baseline) |

The FED_C1M1 "barbed wire" is not BPHazardousZone_BarbedWire_01/02: FED_C1M1_Scripting.umap holds the base
`BPHazardousZone_C` with `BPHazardousZoneData_RazorWire`, one placed instance (`card098: client tally BPHazardousZone_C
collision n=1 placed=1`), at (5391, 28904, 1092) (`card098: host collision BPHazardousZone_C BPHazardousZone_C at (5391,
28904, 1092) placed 1`). kis.py: BPHazardousZone has only UserConstructionScript (scale, MoveEndPoint);
BarbedWire_01/02 have no bytecode (data-only children with mesh, SideCollision_01/02 and RazorWire data).

### b. Health, sound and hit feedback on the client

| Item | Finding | Carrier | Mark |
|---|---|---|---|
| Health | Damage to the copy reaches the client's health through card 021 (CPF_Net on the health attributes, client calls HealthAttributeChanged). hb68: host `card021: host server-copy Health 24.0 -> 23.0` (235701077), client `card132: client t=235701094 client pawn health BP_PlayerCharacterMI_C (client-own) 24.0 -> 23.0 ... [mod notify]` and `card021: client client-own Health 24.0 -> 23.0, HealthAttributeChanged called`. | card 021 health relay | verified (acid) |
| Hazard sound | No sound exists for the wire. Asset names: BPHazardousZone_BarbedWire_01 imports only meshes, BPHazardousZone and BPHazardousZoneData_RazorWire; BPHazardousZoneData_RazorWire and BPHazardousZoneData import nothing (no DamageType, no GameplayCue, no sound). Compare BPHazardousZoneData_ElectricFence: `DamageType.Electric`, `GameplayCue.Impacts.ElectricHazardZone`. DamageActor and ApplyDamageToTarget play no sound (call lists above). The acid zone on the copy fired no cue on the host in the 1 ms after its DamageActor (hb68-host.log lines 15509..15530). | none needed | verified |
| Hit feedback | What the player gets on damage is the pawn's own reaction to the health change. On the client that is HealthAttributeChanged, which card 021 already calls on the client-own pawn. | card 021 | verified that it is called; what it draws is open (screenshot in the run) |
| Cue-carrying zones | ElectricFence's `GameplayCue.Impacts.ElectricHazardZone` executes on the damaged pawn; on the copy that is the card 136 cue relay (`card136: client engine cue dropped GameplayCue.Impacts.NeedleLarge target BP_PlayerCharacterMI_C`, then played). | card 136 effects | not in this card's route |

No new manager: the health relay (card 021) and the cue relay (card 136) cover both carriers.

### c. Every hazard zone class on every map

From `strings` on the 83 extracted umaps (243dfbb6 scratchpad x_maps) and the BP list. "Covered" means the same
native path as above (no player gate, damage to the copy, card 021 health relay).

| Class | Maps (placed) | Data / extra | Covered |
|---|---|---|---|
| BPHazardousZone_C (base) | FED_C1M1 (RazorWire), FED_C3M2, FED_C4M1, FED_C5M2 (child actors) | RazorWire or level data; no sound | covered; FED_C1M1 measured by this run |
| BPHazardousZone_BarbedWire_01 / _02 | BUG_C1M1, BUG_C3M2, BUG_C4M2, BUG_C5M1, FED_C3M1, FED_C3M2, FED_C4M1, FED_C5M1 | RazorWire data, no bytecode | covered (same class path as the run) |
| BPHazardousZone_ElectricFence | BUG_C2M1, BUG_C5M1, FED_C3M1, FED_C5M2 | Electric cue; BP Toggle events (SetCollisionEnabled at kis 362/599/1157, ElectricAudio.Play at 321, SpawnSoundAttached at 477/1195) | damage covered; the cue rides card 136; the toggle sounds are card 136/144 scope, not this card |
| BPHazardousZone_Shunt | FED_C2M1, FED_C3M1, FED_C3M2, FED_C4M1, FED_C4M2, FED_C5M1, FED_C5M2 | not decompiled | covered for the DamageActor path; CrushActor 0x5152020 not traced (open) |
| BP_HazardousZone_AmbientFire | BUG_C4M2, FED_C3M1 | Fire data | covered |
| BP_HazardousZone_Flamethrower_TankerBug | FED_C5M2 (placed) and spawned | Flamethrower data | covered |
| Spawned only (TrySpawnDamageZone 0x5254E80): IncendiaryGrenade, Acid, Flamethrower_Fire/_NukeFire/_PlayerTanker/_AssassinBug/_FieldScientist, WhitePhosphorus, Insecticide | any map | | covered; Acid and IncendiaryGrenade measured on side 1 (logs) |
| BP_DamageVolume_C | FED_C3M1 and others | not an ASTHazardousZoneBase | not covered (other damage source, out of scope) |

### Proposal

No code before the run. The trace finds no gate that drops the copy and no sound to carry, so the open lines are
measurements the existing card 137 6c detour (`detour_c137_zone_damage`, change_c137.cpp:149, in kHooksUbw at
hooks_ubw.cpp:1708, all builds) and card 021 lines already log. Files: a new scenario `tools/scenarios/c179-r1.txt`
only (the senior to approve; I may not write under tools/ yet).

Run c179-r1: FED_C1M1, FAST=1, GOD=0 (run_pair.sh sets UBWCOOP_GOD=1 by default; god mode zeroes the drop: the IncendiaryGrenade rows read 100.0 -> 100.0).

```
# Run: SCENARIO=tools/scenarios/c179-r1.txt DEPLOY=1 FAST=1 GOD=0 tools/run_pair.sh c179-r1 600
failsoft
host wait state pawn=host-own timeout=240
client wait state pawn=client-own timeout=180
host ui continue
# Part 1: host baseline, the host's own pawn on the wire.
host mark
host warp actor BPHazardousZone_C 0
host wait log "card137: host zone BPHazardousZone_C DamageActor on BP_PlayerCharacterMI_C \S+ \(side 0, call \d+\): health (\S+) -> (\S+)" since-mark timeout=10
host shot host-on-wire
host warp actor BPHazardousZone_C 900
# Part 2: the server copy put on the wire (the client follows the copy).
host mark
client mark
host warp client BPHazardousZone_C 0
host wait log "card137: host zone BPHazardousZone_C DamageActor on BP_PlayerCharacterMI_C \S+ \(side 1, call \d+\): health (\S+) -> (\S+)" since-mark timeout=10
host wait log "card021: host server-copy Health \S+ -> \S+" since-mark timeout=10
client wait log "card021: client client-own Health \S+ -> \S+, HealthAttributeChanged called" since-mark timeout=10
client shot client-on-wire
host shot client-on-wire
# Part 3: the playtest move, a run-and-jump onto the wire from 400 uu.
host warp client BPHazardousZone_C 400
client wait near 5391 28904 1092 700 timeout=30
client look actor BPHazardousZone_C
host mark
client mark
client key W 1500 SpaceBar 300
host wait log "card137: host zone BPHazardousZone_C DamageActor on BP_PlayerCharacterMI_C \S+ \(side 1, call \d+\)" since-mark timeout=10
client wait log "card021: client client-own Health \S+ -> \S+, HealthAttributeChanged called" since-mark timeout=10
client shot client-jumped
host shot client-jumped
host quit
client quit
```

Predicted lines (X > Y on every damage line): see "Run c179-r1 predictions" below, which replace the first draft here
(that draft predicted `card132: client ... client pawn health` lines; those log only with UBWCOOP_GOD_HOST_ONLY=1,
death.cpp:440, so this run has none).

Outcomes and what each means:
- Parts 1, 2, 3 all match: the host path works; the playtest miss was the landing spot (above the HazardousDetection
  box, on SideCollision). The card closes with no code and the sound line "no sound in the asset".
- Part 1 has no line within 10 s: the wire does no damage in the base game (zero damage or detection out of reach);
  not a co-op bug, the card closes.
- Part 1 and 2 match, part 3 has none while the client stands on the wire: the copy's ServerMove path does not update
  overlaps; the next trace is the copy's movement (sprint.cpp ServerMove), before any code.
- Part 2 host line drops but no client card021 line: card 021 relay gap, fixed in the card 021 relay.

Out of scope, noted: the client's own zone also ticks and calls DamageActor locally (BeginPlay has no role test), but
the apply is a no-op on a net client (see "Client-local zone damage" below). ElectricFence toggle sounds, Shunt CrushActor, BP_DamageVolume_C.

### Commands behind the verified lines

- `python3 -I dis.py EXE 514DED0 ...`, `51629A0`, `51722D0`, `514BBF0`, `51527F0 1400`, `522EA40 1400`, `5144FB0 600`
  (scratchpad d-bp.s, d-onbegin.s, d-tick.s, d-dot.s, d-damage.s, d-adt.s, d-ctor.s).
- `kis.py` on BPHazardousZone, BPHazardousZone_BarbedWire_01/_02, BPHazardousZone_ElectricFence.
- `strings -n 4` on BPHazardousZone_BarbedWire_01.uasset, BPHazardousZoneData_RazorWire.uasset,
  BPHazardousZoneData_ElectricFence.uasset, BP_HazardousZoneData_Acid.uasset; `xxd` on the RazorWire data.
- `grep -l -a HazardousZone *.umap` and `strings | grep -oE 'BP_?HazardousZone[A-Za-z0-9_]*'` over x_maps.
- `grep -ho "host zone [A-Za-z_0-9]* DamageActor on ..." logs/*.log | sort | uniq -c`; hb68-host.log 15509..15530;
  hb68-client.log card021/card132 lines at 235701094; `grep -h "BPHazardousZone_C .* at (5391" logs/*.log`.

## Client-local zone damage (junior, 2026-11-12; static, before run c179-r1)

Senior's question: does ApplyDamageToTarget on a net client change the local pawn's Health? Answer: no, it is a
no-op for Health. The client's own zone copy ticks, overlaps the client pawn and runs ApplyDOT, DamageActor and
ApplyDamageToTarget like the host's, but the Health write is gated by the stock GAS authority test.

| Step | Address | Finding | Mark |
|---|---|---|---|
| Effect apply | ApplyDamageToActor 0x522DAE0 | MakeDamageEffectSpec (0x14522E135), three SetSetByCallerMagnitude (Flinch, ThreatParameter, WoundType), then `callq *0x6b8(%rax)` on the target's ASC at 0x14522E1B0 with a zeroed FPredictionKey (`xorps; movdqa %xmm0,0x170(%rbp)`, r9 = &key). No role, net mode or authority test in ApplyDamageToActor, ApplyDamage 0x522B460 or ApplyDamageToTarget (grep of the three dumps). | verified |
| Slot 0x6b8 | UADAbilitySystemComponent vftable (`??_7UADAbilitySystemComponent@@6BUObject@@@`, section 2 + 14405064) | slot 0x6b8 = `UAbilitySystemComponent::ApplyGameplayEffectSpecToSelf` 0x494D1E0 (not overridden); slot 0x6d8 = MakeEffectContext | verified |
| Authority gate | ApplyGameplayEffectSpecToSelf 0x494D1E0 | `callq *0x690(%rax)` at 0x14494D259 = `IsOwnerActorAuthoritative` 0x4964840; false and key.Current <= 0 (`testw %ax,%ax; jle`) goes to 0x14494D279: invalid handle, return. This is the engine's HasNetworkAuthorityToApplyGameplayEffect. On the client the pawn's ASC owner is not authoritative and the key is empty, so nothing applies. | verified |
| After the apply | ApplyDamageToActor 0x14522E1B6.. | Reads GetCurrentHealth + GetCurrentArmour again and tests Tag_Player_IsPlayer; reads only. | verified |

So the client cannot double-damage itself through the zone. Its Health moves only through replication, which card 021
logs and notifies.

### Run c179-r1 predictions

Logging facts that shape the counts:
- `detour_c137_zone_damage` (change_c137.cpp:149) logs only on the host (`!g_client_session`), sides 0 and 1, one shared
  cap of 40 lines (kZoneLines).
- `card021: <side> <pawn side> Health X -> Y` (weapons.cpp:403) is a per-tick poll of each pawn's Health, so it shows
  any change, replicated or local. Its cap is 12 lines per process, shared by every pawn side (kC21HealthLogs,
  weapons.cpp:364). The host spends lines on both pawns' first reads and on part 1, so the host's part 2 server-copy
  lines may hit the cap (`diag: card021 health ... further events not logged`). The scenario's host card021 wait is
  therefore a `try` step, and the one-for-one count uses the host's card137 side 1 lines (each logs the copy's Health
  before and after) against the client's card021 client-own lines.
- The log prefix `[ N ms]` timestamps every line, which gives the tick interval.

Predictions:
1. Client card137 lines: absent on the client, in every part (the detour does not log there, and the client-local
   apply changes nothing).
2. Host part 1: k0 >= 1 lines `card137: host zone BPHazardousZone_C DamageActor on BP_PlayerCharacterMI_C ... (side 0,
   call n): health X -> Y`, X > Y, a constant drop d0 per tick and a constant interval t0 between lines. The data's
   0x3D4CCCCD (0.05) and 0x3E800000 (0.25) suggest t0 = 250 ms; not decoded, so recorded, not predicted.
3. Host parts 2 and 3: side 1 lines with the same drop d1 = d0 and interval t1 = t0 (the copy takes what the host
   pawn takes).
4. Client parts 2 and 3: every client `card021: client client-own Health X -> Y, HealthAttributeChanged called` line
   (after the first read) matches one host side 1 card137 line by X and Y, in order; count equal, within the client's
   card021 cap. Replication may merge two ticks into one client change (X of the first, Y of the second) when two
   ticks land inside one net update; that is a merge, not an extra drop, and the sum of drops is equal. No client
   change appears that the host did not make (no client-only drop). Host card021 server-copy lines, where under the
   cap, equal the card137 side 1 lines one for one.
5. No hazard sound line on either side (no sound exists).
6. WARN classes: host the attach line only; client the c201-r2 FED-independent classes (`card132: client round`,
   `card132: mismatch` on this map's actors, `card152: client bone check` only if a gib happens) and no new class.

Double damage outcome: a client card021 drop with no matching host side 1 line is a defect of this card. I stop after
the run and propose the fix at the zone's damage on a net client; no code in this run.

## Run c179-r1 (junior, 2026-11-12; run 1 of 3, no code)

Command: `SCENARIO=tools/scenarios/c179-r1.txt DEPLOY=1 FAST=1 GOD=0 systemd-run --user --scope -p MemoryMax=12G ...
tools/run_pair.sh c179-r1 600`. DLL 853951c74ca46986 (deployed and built). Ended by the scenario after 109 s, host rc 0,
client rc 0, FATAL/ERROR 0 on both sides, no crash dumps. Drive rc 2: 6 failed lines (13, 14, 20, 21, 23, 34).

Scenario error (mine): `warp actor|client <name> 0` is rejected, "warp takes actor|client <name> [dist [behind]] (dist
10-5000)". The trace said dist 0 was allowed; it is not (minimum 10). So parts 1 and 2 never put a pawn on the wire,
and lines 14, 20, 21 and 23 failed after that. Part 3 ran.

Part 3 (the copy warped 400 uu short, then W + SpaceBar) measured the wire on the server copy:

| Host t (ms) | Host playlog, slot 1 server-copy | Host card137 side 1 | Client card170 client-own (t ms) |
|---|---|---|---|
| 111604263 | damage 2.00 (armour 2.00, health 0.00), Armour 100.0 -> 98.0 | call 1, health 100.0 -> 100.0 | 111604280 Armour 100.0 -> 98.0 |
| 111604520 | 2.00, Armour 98.0 -> 96.0 | call 2, 100.0 -> 100.0 | 111604535 98.0 -> 96.0 |
| 111604761 | 2.00, Armour 96.0 -> 94.0 | call 3 | 111604774 96.0 -> 94.0 |
| 111605018 | 2.00, Armour 94.0 -> 92.0 | call 4 | 111605030 94.0 -> 92.0 |
| 111605923 | 2.00, Armour 92.0 -> 90.0 | call 5 | 111605934 92.0 -> 90.0 |
| 111606162 | 2.00, Armour 90.0 -> 88.0 | call 6 | 111606173 90.0 -> 88.0 |
| 111607565 | 51.00, Armour 88.0 -> 37.0, instigator Landscape (not the wire) | none | 111607573 88.0 -> 37.0 |

- The wire hurts the copy: the copy's moves (the warp and the run) produce overlaps, so the ServerMove question is
  answered yes. The first hit came at the warp to 400 uu, before the mark, so the detection box reaches past 400 uu
  from the actor origin on that side.
- The damage is 2.0 per tick and lands on Armour first. Health stays 100.0, so every card021 Health wait in r1 waited
  on a value that never changes while Armour lasts. Interval: 257, 241, 257, 905, 239 ms. The 905 ms gap is the jump
  (the copy left the box and came back).
- One for one: 7 host Armour changes on slot 1, 7 client card170 Armour changes with the same X and Y in the same
  order, 11 to 17 ms after the host's. No client-only change. Client card137 lines: 0 (as predicted). Client Health
  lines after the first read: 0; host server-copy Health lines after the first read: 0.
- Client screenshot client-jumped: HUD armour 37, health 100, the host's values.
- Side 0 baseline: not measured (part 1's warp was rejected). The host-own playlog row 111574809 (51.00 armour,
  instigator Landscape) is the landing from the part 1 `warp actor ... 900`, not the wire.
- Double damage: none. The static answer holds in the run.
- So the playtest "no damage" report is most likely the Armour bar taking the hit while the Health bar stays still.
  Whether the playtester had armour left is not in this run.

WARN classes against c201-r2 (FED-independent part):
- Host: `attach: DisableThreadLibraryCalls` (baseline). New for this run, each with its cause:
  - `config: UBWCOOP_GOD not "1"`: GOD=0, as wanted.
  - `config: UBWCOOP_SKIP_FLIGHT=1 needs UBWCOOP_AUTODROPSHIP=1 -- off`: FAST=1 without UBWCOOP_AUTODROPSHIP=1, so the
    flight skip was off. Harmless here, and r2 keeps it the same.
  - `cmd: "warp ... BPHazardousZone_C 0" skipped`: the scenario error above (2 lines).
  - `card132: seq gap 37-337 from BP_PlayerController_C` and 8 `card132: recv ... Objective ... result no handler`, all
    at 111616634, the quit. The same classes appear in c157c-host.log and c157d-host.log; not from this card.
- Client: `attach` (baseline), `card132: client round`, `card132: mismatch` (STSpawnPoint, STAISpawner, this map's
  actors), and `config: UBWCOOP_GOD not "1"` (GOD=0). No new class from the zone.

### Run c179-r2 predictions (run 2 of 3, no code)

`tools/scenarios/c179-r2.txt` is c179-r1 with warp distance 10 in parts 1 and 2. The client waits are the card 170
Armour line, and the host part 2 wait is the playlog slot 1 row.
1. Part 1: host card137 side 0 lines at about 250 ms, each with a playlog slot 0 row `damage 2.00 (armour 2.00, health
   0.00)`, Armour down 2.0 per tick.
2. Part 2 and 3: side 1 rows with the same 2.00 and about 250 ms (d1 = d0, t1 = t0). Each one has a client card170
   line with the same X and Y. Count equal, no client-only change.
3. Client card137 lines: 0. Health unchanged on both sides while Armour lasts.
4. WARN classes: those of c179-r1 without the two `warp ... 0 skipped` lines.

## Run c179-r2 (junior, 2026-11-12; run 2 of 3, no code)

`run c179-r2: ended by scenario after 57 s; host rc 0, client rc 0`, drive rc 0. Host 3132 lines, client 3082 lines,
FATAL/ERROR 0 on both, new crash dumps none. Screenshots: host-on-wire, client-on-wire, client-jumped (both roles).

Side 0 (host-own on the wire, part 1): one tick at t=111768152, `damage 2.00` to Armour, Armour 100.0 -> 98.0. Only
one tick, because the scenario's next step warped the host off the wire at once, so no side 0 interval exists in this
run. The amount matches side 1 (d1 = d0 = 2.00).

Side 1 (server copy on the wire, parts 2 and 3), host playlog `hit slot 1 server-copy` rows against client card170
`client-own ... ArmourAttributeChanged called` lines:

| host t | host Armour | client t | client Armour |
|---|---|---|---|
| 111768458 | 100.0 -> 98.0 | 111768466 | 100.0 -> 98.0 |
| 111768738 | 98.0 -> 96.0 | 111768758 | 98.0 -> 96.0 |
| 111768953 | 96.0 -> 94.0 | 111768961 | 96.0 -> 94.0 |
| 111769192 | 94.0 -> 92.0 | 111769201 | 94.0 -> 92.0 |
| 111769448 | 92.0 -> 90.0 | 111769459 | 92.0 -> 90.0 |
| 111770163 | 90.0 -> 88.0 | 111770176 | 90.0 -> 88.0 |
| 111770401 | 88.0 -> 86.0 | 111770416 | 88.0 -> 86.0 |

- Each row is `damage 2.00` to Armour. Intervals 280, 215, 239, 256, 715 (the jump), 238 ms; r1 gave 257, 241, 257,
  905, 239. The tick is about 250 ms on side 1.
- One for one: 7 host changes, 7 client changes, same X and Y in the same order, 8 to 20 ms after the host. No
  client-only change.
- Client card137 lines: 0. Health unchanged on both sides (Armour absorbs every tick).
- The host-own `damage 51.00` row at 111769585, instigator Landscape, is the landing from the part 3 `warp ... 900`,
  not the wire.
- Prediction 1 was half right: amount 2.00 matched, but only one side 0 tick (the scenario left the wire too soon).
  Predictions 2 to 4 held.

WARN classes (host 12, client 6), the same set as c179-r1 without the two `warp ... 0 skipped` lines:
- Host: `attach: DisableThreadLibraryCalls` (c201-r2 baseline); `config: UBWCOOP_GOD not "1"` (GOD=0, wanted);
  `config: UBWCOOP_SKIP_FLIGHT=1 needs UBWCOOP_AUTODROPSHIP=1` (FAST=1 alone, as in r1); `card132: seq gap` 1 and
  `card132: recv ... Objective` 8 at the quit (same classes as r1, c157c and c157d; not from this card).
- Client: `attach` (baseline), `config: UBWCOOP_GOD not "1"`, `card132: client round` 1, `card132: mismatch`
  STSpawnPoint 2 and STAISpawner 1 (this map's actors, as in r1). No new class from the zone.

## Report (junior, 2026-11-12)

Files changed:
- `tools/scenarios/c179-r1.txt` (new): the approved route, FED_C1M1, FAST=1, GOD=0.
- `tools/scenarios/c179-r2.txt` (new): r1 with warp distance 10 and waits on the Armour lines.
- `docs/cards/179-client-hazard-damage.md`: the client-local static table, predictions and results for r1 and r2,
  this report.
- No `mod/` change. DLL 853951c74ca46986, deployed by run_pair. Runs used: 2 of 3.

Summary:
- No double damage. Statically, the client's own zone apply (ApplyDamageToActor 0x522DAE0, through vtable slot 0x6b8,
  ApplyGameplayEffectSpecToSelf 0x494D1E0) returns an invalid handle on a net client, because
  IsOwnerActorAuthoritative is false and the prediction key is zero. The runs agree: 0 client card137 lines and no
  client-only Armour change.
- The host's server copy takes the wire damage, and it reaches the client one for one through the card 170 Armour
  relay: r1 7 of 7, r2 7 of 7, with the same values, 8 to 20 ms behind the host.
- Side 1 amount and interval match side 0's: 2.00 per tick on both sides, about 250 ms per tick on side 1. Side 0's
  interval was not measured (one tick in r2).
- The wire damage lands on Armour first, and Health stays 100.0 while Armour lasts. The playtest report "no damage" is
  most likely the Armour bar dropping while the Health bar stays still. The client HUD shows the host's values
  (r1 client-jumped: armour 37, health 100).

Verification (counted lines):
- r2 host `playlog: host t=\d+ hit slot 1 server-copy: damage 2.00`: 7. Client `card170: client client-own ...
  Armour X -> Y, ArmourAttributeChanged called`: 7, X and Y equal row by row. Host slot 0 wire rows: 1.
- r1: 7 host slot 1 rows and 7 client card170 lines, equal.
- Client card137 zone lines: 0 in both runs. FATAL/ERROR 0, crash dumps none, run rc 0 in both runs.

Outcome: no code needed for card 179 as written. The senior decides whether to close it or to open a card for making
the Armour hit visible (a hit sound or flash on the client when Armour changes), if the playtest needs it.

Report complete.

## Senior review (2026-11-12)

Closed with no code on c179-r2 (DLL `853951c74ca46986`, no file under `mod/` changed). Checked by me in
logs/c179-r2-*.log: the host zone calls DamageActor on the host pawn (side 0, 1 call) and on the client's server copy
(side 1, 7 calls about 250 ms apart); each tick is 2.00 damage taken by Armour (playlog: armour 2.00, health 0.00);
the client logs 7 `card170: client client-own Armour` steps 100 -> 86 with ArmourAttributeChanged called, 8 to 20 ms
after the host; no client-side zone line. The base game has no sound on this wire. The playtest "no damage" is the
Armour bar dropping while Health holds. c179-r1 lost parts 1 and 2 to a rejected `warp ... 0` (dist is 10 to 5000).

Not proven: the side 0 tick interval (one host tick); Shunt CrushActor; ElectricFence toggle sounds.
