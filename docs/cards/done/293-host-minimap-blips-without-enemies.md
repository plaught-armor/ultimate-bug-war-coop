# 293 Host minimap shows enemy blips where no enemy is

Playtest 2026-11-13 (user plays as client; the host player reports): "host is seeing enemies blipped that don't
exist". Cause not settled: the client log cannot show the host's minimap. Step 1 settles it from the host log and
the disassembly; no code before that.

## What the game does (static, from the exe)

- `USTMinimapComponent::TickComponent` 0x0521E460 scans (`MinimapScanFrequency`, `MinimapCollider`,
  `MinimapObjectTypesToShow`) for actors that implement `ISTMinimapMarkerInterface` and asks each
  `ShouldShowOnMinimap`. The mod does not touch this path (its only minimap code is the origin fix, card 058/120,
  and the card 068 log).
- `ASTNonPlayableCharacterBase::ShouldShowOnMinimap_Implementation` 0x051A6890 (enemies and AI troopers; the
  interface sits at actor +0x7A0) returns true when both hold:
  1. the actor's vfunc +0x9E0 returns false (the "is dead" test used by `UpdateColliders`, card 039);
  2. the object pointer at interface +0x98 (actor +0x838) is set and not flagged unreachable (flags bit 30).
- It does not test hidden, collision, mesh visibility or whether the actor is inside the world.

So a blip without an enemy is an NPC actor on the host that still exists, answers "not dead" to vfunc +0x9E0, and
cannot be seen.

## Step 1 (static plus host log, junior, no run)

1. Name vfunc +0x9E0 on `ASTNonPlayableCharacterBase` and the member at actor +0x838; state what each reads
   (health component `IsDead`, a state enum, a cached flag).
2. List every mod path on the host that makes an NPC invisible, moves it, or changes its death state without the
   game's own death call, and say for each whether vfunc +0x9E0 turns true afterwards. Known candidates:
   - the client kill relay (`HitClaim`, card 157; 1275 claims in this map's client log): the host applies the
     kill; check that it goes through the path that sets the state +0x9E0 reads, for every damage kind (bullet,
     grenade, melee, overkill and gib);
   - gib and overkill (card 039, card 148): body burst and mesh hidden, actor kept;
   - corpse removal and the dead set (card 140), pooled gore;
   - mod spawns on the host (card 169) and anything the mod hides or holds for a joining client (card 137, 138,
     195).
3. Game-side candidates the mod may expose (the host plays alone in single player, so these would be host-only
   game bugs made visible by a second player's position): bugs spawned for the client's area that never leave
   their spawn hole (underground), bugs that fell below the map.
4. After the playtest, from the host log: at the times the host player names, list the host's live NPC guids
   with no pose or sprite row sent for 10 s, and NPCs whose last location is below the ground height.

Write the answers into this card. If step 1 names the path, code the fix below; if not, code only the diagnostic.

## Diagnostic (code now, both sides, default on in the test build)

Once per second on each machine: walk the local player's `USTMinimapComponent` `ActiveMarkers` (+0xF8, count
+0x100, 0x38 bytes each). For each marker actor that is an NPC log one row only when it is suspect: hidden, mesh
not visible, no collision, health at or below 0, in the mod's dead set, or more than 300 uu below the nearest
ground hit.

- Row: `card293: <role> t=<ms> minimap marker <class> guid <g> at (<x>, <y>, <z>): suspect <reasons>; vfunc 9E0
  <0|1>, health <h>, hidden <0|1>, mesh visible <0|1>, last mod op <name>`.
- Map total: `card293: <role> minimap markers <n> seen, <k> suspect`.

## Fix (after step 1)

The host's NPC state must agree with what both players see: an NPC that is gone for the players is dead for the
game. The mod path found in step 1 ends the NPC through the game's own death or destroy call, so the minimap, the
AI, objective kill counts and the save all see it. No filter on the minimap as the fix.

## Sibling sweep (same class: an actor the mod removes from view but the game still counts)

| Item | Check |
|---|---|
| Client minimap | same suspect rows on the client; a client copy of a host-dead NPC still marked |
| AI troopers (`BP_AI_Trooper_*`) | same interface, same test |
| Objective kill counters and "kill all" objectives | a counted enemy that no player can see or hit |
| Enemy spawner alive counts and wave ends | spawner waits on an NPC that is invisible |
| AI targeting | troopers or turrets firing at a spot with no enemy |
| Card 132 digest | add live NPC count and the suspect count to the digest so a mismatch shows in every run |

## Predictions (one run, after the playtest, batched)

- C2M1 with 30 bug kills, half by the client, some by grenade and overkill: host and client `card293` totals show
  0 suspect markers; host screenshot of the minimap after the fight shows no blip without a body in view.
- Digest round after: 0 mismatches, live NPC counts equal.

## Step 1 answers (junior, static plus host log `client/UBWCoop-menu-20261113-182510.log`, no run)

1. vfunc +0x9E0 on `ASTNonPlayableCharacterBase` is `ASTCharacter::IsDead` 0x050F0D40: it reads the character's
   health component (+0x688) and returns its IsDead byte +0x148 (the byte `c25_dead` reads). It is a cached flag the
   health component sets on its own death path, not a health compare. The member at actor +0x838 (interface +0x98)
   has no symbol or property name; it is a raw object pointer the function only tests for null and the
   unreachable flag. The minimap marker struct `FSTMinimapMarkerData` is 0x38 bytes: MinimapLocation +0x0,
   MinimapSource (`AActor*`) +0x20, MarkerWidget +0x28 (offsets from the struct's property params).
2. Mod paths on the host that hide, move or end an NPC without the game's death call:
   - Client kill relay (card 157): the host log shows every applied HitClaim going through the host's own damage
     (`Health a -> b (n applied)`), so the health component's own death path sets IsDead; vfunc +0x9E0 turns true.
     Refused claims on a dead NPC (16 rows) apply nothing. Grenade and melee kills are host-native (the host's
     copy of the round or swing deals the damage), same path.
   - Gib and overkill (card 039, 148, 130): run after the game's death event; IsDead is already set, so the marker
     is dropped by `ShouldShowOnMinimap`.
   - Corpse removal and the dead set (card 140), pooled gore: act on dead actors only.
   - Card 169 mod spawns, card 137/138/195 join holds: card 137 holds corpses (dead); card 138 holds the join
     dispatch, not NPCs. No mod path on the host hides a live NPC.
   No host mod path found that leaves a live NPC invisible, so no fix is coded; the diagnostic below settles it.
3. Game-side candidates stay open: bugs spawned for the client's area that stay in their spawn hole or fall below
   the map are live, not dead, and the minimap test does not check visibility or ground. The diagnostic's
   below-ground reason (first visibility hit more than 300 uu above the NPC, tracing down from 2000 uu above)
   names them.
4. Host log: the host player named no times, and the host log has no per-NPC pose or sprite send rows with guids,
   so "live guid with no row for 10 s" cannot be listed from it. No NPC location rows below ground exist in it
   either. Item 4 is answered by the diagnostic rows in the next run.

## Diagnostic (coded, uncompiled)

`audit_npc.cpp` `c293_step` (from `ubw_post_tick`, 1 Hz, both sides) and `c293_map_end`. Reasons: hidden, mesh-off
(`ACharacter::Mesh` +0x328 bVisible), no-collision, health<=0, below-ground. A marker is logged again only when its
reason set changes (64-NPC table, 300 rows per map). Not coded: "in the mod's dead set" (no query exists; the dead
state is the health component's IsDead, printed as `vfunc 9E0`) and "last mod op" (the mod keeps no per-NPC
operation record; adding one is a separate change). Map total: `card293: <role> minimap markers <n> seen, <k>
suspect` (per-second samples).

## Senior verdict 2026-11-13

Static work is exhausted: no host mod path leaves a live NPC invisible, and the playtest logs have no row per
blip. The cause is not settled, so no fix is coded. The diagnostic (`c293_step`, `c293_map_end`) goes into the
next build as a diagnostic only; the fix is written from its rows (reason per phantom blip: hidden, mesh-off,
no-collision, health<=0, below-ground).

## Build 7

The diagnostic (`c293_step` from `ubw_post_tick`, `c293_map_end`) is in build 7 (diff `b7-J-293.diff`). Compile fix:
the 300 uu threshold constant is renamed `kC293BelowUu` (it clashed with the `kC293Below` reason bit).

## Senior trace 2026-11-14 (user answer: the host saw the blips; place unknown)

Static, `USTMinimapComponent::TickComponent` 0x0521E460 read in full:

- Every tick, last to first, each `ActiveMarkers` entry is dropped (removed delegate +0xC8, then array remove)
  when its source is null or unreachable, when `ShouldShowOnMinimap` (wrapper 0x0507D400) answers false, or when
  it is out of range (distance over `+0xDC`) and its always-show byte (+0x30) is 0. So a dead NPC leaves one tick
  after `IsDead` turns true. The tick does nothing while the origin actor (+0x108) is unset or `+0xD8` is 0.
- New markers do not come from a collider overlap. Every `MinimapScanFrequency` (+0xE0, countdown +0x118) the
  component walks a registry (0x0523DF50 on the owner's world object, list getter 0x051FC5D0: an array of weak
  actor pointers) and adds each valid actor not yet listed that is in range or always-show (0x0507D390).
  `ShouldShowOnMinimap` is not asked at add time, only on the next tick's drop pass.

What the existing rows show (C2M1 runs b7b-2, b8, b10, host log 20261113-211046):

| Rows | Reading |
|---|---|
| Host, `BP_AI_WarriorBug_C` `health<=0`, `vfunc 9E0 1` (guids 368, 458, 468, 388, one guid 0) | a dead bug in the list at the 1 Hz sample. One tick of lag by the code above, or longer: the row is written once per reason set, so its dwell is unknown |
| Client, `BP_AI_WarriorBug_C` guid 428 and 640: `health<=0`, `vfunc 9E0 0` | the client copy has no health left and `IsDead` 0: nothing drops its marker. A blip without a live enemy, on the client |
| Host, `BP_AI_WarriorBug_C` guid 0, health 184, `below-ground` at (216636, 20918, 9577) | a live bug with no mod guid: the client has no copy. Not a host phantom, a sync gap |
| Both, `BP_AI_Trooper_*` `below-ground` | false positive under a roof (known) |

No row explains a lasting blip on the host. The cause is still open; the next step is a diagnostic that measures
dwell, then the fix follows its rows.

### Step 2 (diagnostic, rides the next build; no fix code)

1. Per suspect marker keep first-seen and last-seen times and the sample count. When it leaves the list, or at
   map end: `card293: <role> marker <class> guid <g> left after <ms> ms, <n> samples, reasons <r>, IsDead <0|1>,
   health <h>, actor <alive|destroyed>`. Sample every tick while any suspect marker is listed, 1 Hz otherwise.
2. New reason `re-added`: a marker whose actor has `IsDead` 1 and that was dropped and listed again (the scan adds
   without asking `ShouldShowOnMinimap`; a dead bug still in the registry is re-added every scan and dropped the
   tick after, which would blink a blip at the scan rate). Count re-adds per actor in the row above.
3. New reason `no-guid`: a live NPC marker with mod guid 0 (host only).
4. Map total gains `longest dwell <ms>, re-added <n>, no-guid <n>`.

Predictions: host dead-bug markers leave within 2 samples (one tick of lag) or show `re-added` counts over 1 (the
blink is the phantom). Client guid rows like 428 / 640 dwell until the copy is destroyed: then the client fix is
to set the health component's `IsDead` on the copy when the host's death arrives (class: every client NPC copy's
death state, cards 291 and 157 own the death relay).

### Open from this trace

- The guid 0 live warrior bug on the host (265155395): which spawn path leaves an NPC without a guid. Own card
  once step 2's `no-guid` count shows it is not a spawn-frame race.

## Closed (2026-11-17 12:55): step 2 rows read, no mod fault found

Step 2 diagnostic rows over six runs (c423b178a to c423b182a on FED_C2M1, c419b185a):

- Dead markers on the host leave within one sample (b180a: 6 rows `IsDead 1 ... left after 0 ms, 1 samples`), as the
  tick trace predicted. `re-added` totals are 0 on the host in b180a to b185a (one in b179a), so no blink.
- `no-guid` rows are live NPCs that have no NetGUID yet (`c41_guid` reads the engine NetGUID): not yet replicated,
  0.3 to 11 s. The host sees those bugs, so they are not blips without enemies.
- The only host blips with no enemy in view are `below-ground` rows on live bugs: b182a guids 3530 to 3570,
  health 400, mesh visible, more than 300 uu under the ground for 3.1 to 4.6 s before they come up. The client logs
  the same guids as suspects over the same seconds (4.7 s). That is the game's own marker on bugs that spawn
  underground, and it is the same on both screens.
- Client rows `health<=0 ... IsDead 0` leave within 20 ms of the hit row (guid 3550: hit 575782677, left 575782697).

So the blips the host saw have one traced source, burrowed live bugs, and the mod neither adds nor keeps a marker.
The diagnostic stays in the build.

Reopens on: a playtest host row `card293: host marker ... IsDead 1` with a dwell over 1000 ms that is not guid 0, a
map total with `re-added` above 3, or blips on one screen only.
