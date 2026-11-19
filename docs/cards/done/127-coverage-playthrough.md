# Card 127: Co-op playthrough on autoplay of the maps that cover the game

Status: queued, last. Gate for the next play build. Card 126 (autoplay) is parked: drive the soak with a scripted scenario of the c140q shape instead of autoplay.

Read:

- card 126 Report (autoplay, `wait autoplay`, the progress watchdog);
- card 123 Report, item 4 (the 13 campaign maps and their sub-levels, the sweep table);
- memory rules: test launches muted, windowed 640x360 side by side; builds and game pairs in the MemoryMax=12G scope.

Repo root: `/path/to/ultimate-bug-war-coop`. DLL base: card 126's.

## Why

User (2026-11-07): "We should test drive the entire game. Properly." Then: "maybe not every map. I think the training
covers a lot of scenarios. Just cover what makes sense." So the playthrough plays the maps that together reach every
feature class from the card 123 sweep, not all 13. Each map is played by both test players on autoplay, start to
mission end, with the travel to the next map, to find the co-op faults a player would hit before the friend does.

## Do

1. **Maps, chosen by coverage.** Default set (one run each, started with `UBWCOOP_TRAVEL`, ending once the
   mission-end travel has loaded the next map on both sides; cutscenes and dropship rides on):
   - FED_C1M1, the training: switches, the assault course lever, zipline, crates and pickups, tactical support
     (resupply, airstrike), checkpoint saves, the dropship ride and the first mission-end travel.
   - FED_C2M1: the first full WarriorBug fights, arena and horde zones, NPC troopers, satchels.
   - FED_C3M1: mounted turrets (cards 123 and 125) and its new destructibles and hazards.
   - The first map with mech drop platforms that FED_C1M1-C3M1 do not cover (FED_C4M1 per card 075), for mech play.
   - BUG_C1M1: the bug campaign has never run in co-op; its player pawn and abilities differ.
   Before the runs, check the card 123 sweep table: if a class there appears on none of these maps, add the first
   map that has it, and say why in the Report. If two of these maps add nothing over the others, drop one and say
   why.
2. **Each map, both players on autoplay.** No `objective complete`, no warp, no hand-placed goto. Per map, record:
   - completed or not; the time; each objective's time and who did it;
   - every `blocked` line and its cause;
   - desyncs between host and client: the `objectives` command on both sides at each objective change (lists must
     match), enemy counts near each player, doors and world state (card 098 lines), pickups;
   - deaths, downs and revives; anything stuck (player, enemy, objective);
   - ERROR/FATAL, WARN counts by source, CRASH lines and dumps on either side.
3. **On a fault.** Stop the campaign at that map. A crash, desync or a blocked objective that a human could do is a
   bug: find the cause, fix it with one mechanism for its class (user rules: no workarounds, check every other case,
   full world sync), then rerun that map until it passes, and go on. A block that is the autoplay's own limit (the
   human could do it) is fixed in the autoplay, not worked around in the scenario. Log each fault in the Report as it
   is found, with its fix, so the senior can review mid-card when asked.
4. **Report back after FED_C1M1** with its row, so the senior can review the autoplay on the training before the
   rest. Keep going after the report unless told to stop.

Every run: muted, windowed 640x360, in the MemoryMax=12G scope. Watch each run's liveness (log growing, progress
lines); a hung run is killed, logged as a fault and investigated, never waited on.

## Done when

The Report has the map set and why each map is in it, one row per map (completed, time, objectives, blocked lines, desyncs, downs, WARN by
source, ERROR/FATAL, CRASH, dumps, DLL), every fault with its cause, fix and the rerun that passed, and the final DLL
hash (`sha256sum | cut -c1-16`) with the play DLL saved as `build/dwmapi-play-<hash>.dll`. Ends with "Report
complete."

## Watch items added 2026-11-11 (from c186-r2)

- Host pose stream backlog peak (262 frames in c186-r2, 74 in r1) and `kPoseStreamPerStep` cap hits.
- Card 188 repeat-hit note path (never exercised in a card run).
- Laser trooper deaths (card 177: sprite rows keyed on IsNPC2D, never proven on the laser classes in a card run).
- Card 177: the revive path ("revived" above 0 in the sprite totals) and the client read-back 1000 ms after a
  rest-state arrival (0 of 4 ran in c177-r2).
- Card 192: the totals line's side label (fixed after the run build, not run).
- Card 199: FED_C4M1 Park objectives (12 adds inside LoadMap, lost before the fix) reach the client; play a bug map.
- Card 198: `card198: host ... Restore read-back` with a damaged bug pawn (a WARN there means the spawn path
  overwrites Health after the apply); MI restore path unchanged.
- Card 184: a `world P` row written inside LoadMap (BP_TacSupp_Pod_MechDrop_C) is in the join replay.
- Card 201: a `card201: client gib rest miss` line names the history of a piece resting over 2 uu from the host's
  point (c201-r1 part 245, 59.9 uu, no cause yet); `sent late` above 0 and mirror resets on a failed send are unrun.
- Card 179: client on the FED_C1M1 barbed wire: the Armour bar drops 2 per tick, then Health; same for the host.
- Card 164: on the play build (no client navigation) the digest shows no RecastNavMesh mismatch and the card164
  left-out line is equal on both sides; a host-only objective key is logged with its name.
- Card 185: after a rejoin while a timeline is playing or reversing (a door mid-move), the client line "applied timeline state" shows a position between 0 and the length with playing 1, and the next digest round has no location mismatch; any "host timeline gone: end by op" line is checked against the host's view.
- Card 142: after a rejoin, a decal row on an NPC guid that is held and then "applied late"; the round line's
  held sum (pending + applied late + dropped + released while held + failed) with failed 0.
- Cards 206, 204: after an in-place load, a "card206: host decal N world row from attach 0" line whose world pose lies on the ground at the kill site; client live = host live in the next round.
- Card 196: a bug-side client's Use or revive hold reaching the host ("card043" hold lines for a
  BP_PlayerCharacterBug_C server copy), on a bug map that has a downed teammate or an interactable.
- Card 208: host bug switch while airborne is refused by the game (EquipAbility blocks Falling); join-replay BugForm rows for a mid-map join with a non-zero form; rows for a third player (card 209).
- Cards 210, 213: look at the other player's bug in each form (Warrior, Hopper, Tanker) from the side, both ways; no trooper sprite on a bug; human pawns still have their sprite body. Card 211: host real death with a human causer and with none.
- Card 147: main-menu Continue (from-save start), then a client joins: no lasting `collision host 0 client 1` digest mismatch on corpse destructibles.
- Card 212: break a door (debris class `STGorePartNonSticky_StaticMesh`) near both players; no `asleep above the ground` WARN with bottom `0 0 0`. Card 214: Swoop sound and effects of the other player's Hopper, seen and heard from the other side.
- Card 171 item 13: host screenshot on FED_C2M1 showing objective markers on the host's HUD (needs card 222 if a menu step comes first).

## Senior direction (2026-11-12): soak now, before the rest of the queue (user decision)

The user moved the soak ahead of the queue: soak, fix only what blocks play, play build, the rest of the queue after
the user's playtest. This narrows the card for now:

- Maps of this pass: FED_C1M1, then FED_C2M1 (the two maps of the 2026-11-08 playtest), one run each, with the
  mission-end travel of FED_C1M1 into FED_C2M1 if the scenario reaches it. FED_C3M1, FED_C4M1 and BUG_C1M1 stay in
  the card for after the play build.
- Driver: the scripted scenario of the c140-soak1x shape at TimeDilation 1.0, test skip on, muted, windowed. Before
  the run, every wait of the scenario is matched against a real log line of a run on the current DLL family
  (c135, c222, c206, c201) or the format string in source; warps go through the host. A capped log line is never a
  wait target.
- "On a fault" (Do 3) changes for this pass: do not fix. Record each fault (log line, side, time, what a player
  would see) and go on with the map if the run can go on. The senior sorts faults into "blocks play" (fixed before
  the play build) and "queued".
- Read out per map: ERROR/FATAL/CRASH and dumps, WARN classes per side with counts, digest rounds and every
  mismatch by class, table peaks against their caps, `netbudget` peaks and pace backlog peak, objectives lists on
  both sides, the watch items below that the run reached (say which it did not reach).
- Budget: two runs per map. A run that dies on a scenario fault is reported before any rerun.

## Soak C1M1 (junior)

### Step A: scenario audit (no run)

Scenario `tools/scenarios/c127-c1m1.txt`: `tools/scenarios/c140-soak1x.txt` (TimeDilation 1.0, the c140q steps) with
`failsoft` at the top, so a missed step is recorded and the run goes on. Every wait was checked against the current
source for its format string and its cap. Line numbers below are c140-soak1x lines.

Replaced (capped or gone):

| c140 line | Old target | Why not | Now |
|---|---|---|---|
| 13, 15, 579, 584 | `dropship-watch: local pawn is transport dropship` / `PC pawn is now` (dropship.cpp:850, 843) | capped, kDropshipWatchLines 60 (dropship.cpp:24) | `wait state map=FED_C1M1 pawn=other` (and `map=FED_C2M1` after travel), then `pawn=host-own`. State line testrun.cpp:1850; kind "other" when the pawn is not a player character (testrun.cpp:1832); map is `object_name(world)`, the same name the card067 lines print |
| 38 | card049 `SetObjectiveVisibility(TacSupport_SO41) bools 11` (objectives.cpp:1172-1180) | capped, kC49AuditLines 240 (objectives.cpp:58) | `sleep 2` (c140q: 1.1 s after the press) |
| 40, 42 | card098 `host show BP_TacSuppPickup_BombingRun_Tutorial_C` (world_c98.cpp:95) | capped, kC98Lines 500, used up early (659 card098 lines before this point in c140q) | `sleep 2`, `try objective complete TacSupport_SO41`, `sleep 3` |
| 50 | card050 `DisablePickup on BP_TacSuppPickup_BombingRun_Tutorial_C` (hooks_ubw.cpp:1278) | capped, kC50Lines 200 | `sleep 1`; the pickup is proven by the next uncapped waits (the client's card104 copy of BP_SupportFlare_Tutorial_C and of the BombingRun projectile, world_c98.cpp:911) |
| 85 | card056 `PickupGone BP_GrenadeAmmoPickup_Frag_C` | gone from source; the client's pickup line now is card095 `collision ... applied`, capped (kC95Lines 200, door_c95.cpp:172) | no wait; the `try client goto` stays |

Kept (uncapped): state waits (testrun.cpp:1850), `client wait near` (state), card120 minimap origin (chan_ops.cpp:102),
card104 copy lines (world_c98.cpp:911), card074 GrenadeExplode (grenades.cpp:1386, always logs outside a round),
card125 host spawned (enemies.cpp:312, count=3 since-mark), card137 pose freeze (pose_c132.cpp:2535), card045
reconnected (restart.cpp:134). Warps: all client moves are `host warp client`. Sleeps are the c140 minimums.

Limits: the mission end is the driver's `host travel next`, not the game's own mission-end travel; objectives are
completed by command (c140 shape, as directed), so this is no autoplay coverage of the objectives.

Watch items the scenario can reach: host pose stream backlog peak and kPoseStreamPerStep cap hits (c186); card 201
gib rest miss / sent late / mirror resets (34 kill rounds); card 164 digest RecastNavMesh and left-out line (if the
run build has no client navigation); laser trooper deaths only if a laser trooper dies near the fight (not driven);
card 188 repeat-hit note (possible, not driven). Not reachable: card 179 barbed wire (no walk onto the wire), card 177
revive (no downs), card 192 label (needs a sprite totals line; read if printed), cards 199/196/208-214 (bug map),
198 (damaged bug pawn restore), 184/185/142 (rejoin), 206/204 (in-place load), 147 (Continue from save), 212 (door
break), 171 item 13 (FED_C2M1 run).

Expected wall time: c140q ran 400 s of scenario; with launch and deploy about 9 min. Liveness check at 14 min.
Timeout 1800 s.

Run: `systemd-run --user --scope -q -p MemoryMax=12G env SCENARIO=tools/scenarios/c127-c1m1.txt DEPLOY=1 FAST=1
HOST_EXTRA_ENV="UBWCOOP_AUTODROPSHIP=1 UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" CLIENT_EXTRA_ENV="UBWCOOP_C132_FX=1
UBWCOOP_C136_LOG=1" tools/run_pair.sh c127-c1m1 1800`

### Run c127-c1m1 (Step C) -- void: 8 fps

DLL 8edaffb0745a50fe. Logs: logs/c127-c1m1-{host,client}.log, logs/c127-c1m1.drive. Read-out script (scratchpad,
not in the repo): c127sum.py LOGDIR TAG BASE_TAG...

**Completed: yes, to the end of the scenario, but the mission end is the driver's.** 509 s of scenario. Host rc 0,
client rc 0, drive rc 2 (2 failed lines, below). The whole c140-soak1x path ran: drop, landing, resupply flare,
pickup rack, frag, 34 spawn/kill rounds, 4 objectives commands. The tail `host travel next` (490.6 s) loaded
FED_C2M1 on both sides; the client reconnected 10.1 s after the drop (`card045: client t=138060994 reconnected (1)`),
both pawns own on FED_C2M1, quit. The game's own mission end (objective chain to the extraction) is not driven.

**ERROR/FATAL/CRASH: 0.** No new crash dumps. The only matches are the card067 `CallFunc_Unhandled_ReturnValue`
property names (not errors).

**WARN classes.** Host 13 lines, client 89. "New" is against c135-r3, c201-r2 and c140q; c140q is an older build
without the card148/152/159 lines, so "new" there mostly means "logged by a newer card", not "new fault".

| Side | Count | Class | First | Note |
|---|---|---|---|---|
| host | 4 | card140 round pose self-check / body over 100 uu | host:119585 | corpse moved after a send, 2 rounds |
| host | 3 | card132 OnHit of ADProjectile after GrenadeExplode | host:67686 | known card132 note |
| host | 2 | card169 held visuals destroyed unrendered | host:4569 | flare and grenade held visuals |
| host | 1 | cmd charge failed (no TacticalSupportComponent slot) | host:4951 | fault 1 |
| host | 1 | card132 digest round 16 cut by the map change | host:143206 | expected at travel |
| host | 1 | attach DisableThreadLibraryCalls | host:3 | old |
| client | 24 | card148 gib host part asleep above the ground | client:13535 | |
| client | 20 | card140 hit WarriorBug host location over kHitDrift | client:6570 | 185 drifts, largest 1932 uu |
| client | 20 | card132 client rows not acked within 5000 ms | client:54219 | fault 3 |
| client | 19 | card152 no stream row, own ragdoll started | client:9309 | fault 4 |
| client | 1 | card159 dropship check 8004 uu off | client:2459 | at the join, tolerance 2643 |
| client | 1 | card159 dropship totals: join check 3 of 4 match | client:120616 | |
| client | 1 | card152 bone check | client:5632 | old |
| client | 1 | card152 pose stream map totals | client:120410 | totals line |
| client | 1 | card152 ragdoll wait table: 19 of 26 fallbacks | client:120412 | fault 4 |
| client | 1 | attach DisableThreadLibraryCalls | client:3 | old |

**Digest: 14 rounds, 0 mismatches, 0 mismatch lines on either side.** Actors 1494 to 1510 host = client every round.
Rounds 5, 6, 8, 10 had 1 differing slot each, all excluded as a ledger row after the snapshot (in flight, not a
difference). Round 16 cut by the travel. No RecastNavMesh mismatch; card164 left out 66 server-only actors every
round (RecastNavMesh x5, BP_AISpawnPoint_C 18, STSpawnPoint 43).

**Table peaks against caps** (none at the cap except the gib window, which is by design a byte window):
inventory classes 141 of 384 (map load C2M1), 122 of 384 on C1M1; card095 collision rows 169 of 256; card136 fx live
1132 of 4096; card130 fx watch 17 of 64; card130 AI pick-up 2 of 16; card104 relayed actors 16 of 256; card177 sprite
rows 7 of 256; card038 client dead-set 27 of 256; card073 grant waits 1 of 16; card152 ragdoll wait 5 of 32; card140
gib budget window 16382 of 16384 bytes (594 recorded, 594 sent, late 0, lost 0; peak waiting 35 of 1024, longest S
wait 7 ms); pose stream peak 5 samples in one step of the 24 cap, 0 cap hits.

**Netbudget and pace.** Reliable bunches window peak 51 of 511 (BP_PlayerController_C). Pace queue: fault 2. Pose
stream: 287 samples, 343 backlog skips, backlog peak 1516 frames.

**Objectives.** 4 `objectives` commands; every one: host and client both `ObjectiveListItems 14, box slots 3,
objectives 14` (host:103557/118801/138252/141311, client:87021/97147/110967/119090). Same on both sides.

**Watch items.** Pose stream backlog (c186): reached, see fault 2. kPoseStreamPerStep cap: 0 hits. card201: 0 gib
rest misses, gib pools 12 of 100 non-sticky mesh at once, `rest checks over 2 uu 0`. card164: reached, clean.
card188 repeat-hit: not logged. Laser trooper deaths: none. card177: 8 sprite deaths sent, 0 owed. The card137
freeze waits at scenario lines 269 and 329 timed out (15 s each): 81 `card137: host pose freeze` lines in the run, so
those two rounds produced fewer than 3 freezes inside 15 s; a `try`, not a fault by itself.

**Fault list (C1M1)**

1. host, logs/c127-c1m1-host.log near line 4884-4951, t=137702872-137708754 (about +125 s). The host warps to the
   tutorial bombing-run pickup (BP_TacSuppPickup_BombingRun_Tutorial_C_2) and presses E: `card020: host host-own
   UADInteractorComponent::Interact(owner BP_PlayerCharacterMI_C): target null (null)`; no PlayerCollider 1 -> 0, no
   DisablePickup. Then `WARN cmd: 21 charge 1 20000 1 -> failed (no TacticalSupportComponent slot ability or charge
   functions)` (host:4951). Key Five and LMB give no flare, so the client waits at scenario lines 79 and 80
   (BP_SupportFlare_Tutorial copy, bombing-run projectile copy) time out. Player sees: the tutorial airstrike pickup
   cannot be taken, no airstrike. In c140q the same press took it 100 ms after the look (c140q-host.log:4935). The
   pawn stood at z 347 against the pickup's 247, and the show-to-warp gap was 7.5 s (2.2 s in c140q, the sleeps that
   replaced its waits). Guess: scenario timing or the interact trace from 100 uu above (driver), not a game fault;
   not proven. Card: new (or c140 scenario).
2. host, logs/c127-c1m1-host.log:138836 (peak), from about +240 s (t=137791961) to the map end. Host pace queue grows
   to 1545 frames, 965082 bytes, longest wait 32438 ms. Every frame in the window held for bits (`holds: bits 89`),
   drain 29019 B/s against CurrentNetSpeed 250000 B/s; Fx 14191 B/s is the largest op. It peaks during the spawn
   rounds and never drains before the travel. c201-r2 peak 451 frames / 1552 ms, c140q 53 / 33 ms. Player sees: on the
   client, host events (FX, corpse poses, gibs, hits) arrive up to 32 s late in a long fight. Guess: card 140 pace
   (net_budget.cpp) or card 136 Fx volume. The worst fault of the run.
3. client, logs/c127-c1m1-client.log:54219 to 119753, 20 lines from +370 s. `WARN card132: client rows N-N not acked
   by the host within 5000 ms` (ledger_c132.cpp:406). Same window as fault 2, so likely the host's acks are behind the
   pace queue. Player sees: nothing direct; client-side state rows confirmed late. Guess: follows fault 2.
4. client, logs/c127-c1m1-client.log:9309 (first) and the totals at :120412. 19 of 26 held game ragdoll starts fell
   back to the client's own start after 2000 ms (`no stream row`). Player sees: on the client, about 3 in 4 corpses
   ragdoll on their own, not on the host's pose, so they land differently. Guess: card152, made worse by fault 2
   (stream rows late).
5. client, logs/c127-c1m1-client.log:6570 (first), totals at :120735. 185 hits drift over kHitDrift, largest 1932 uu;
   28 hits unmapped. Player sees: blood and hit marks on the client off the bug's body. Guess: card140 hit relay, also
   behind fault 2.
6. client, logs/c127-c1m1-client.log:2459, at the join. Dropship BP_ProceduralDropshipTroops_C_3 8004 uu off
   (tolerance 2643); totals: join check 3 of 4 match. Player sees: one dropship in the wrong place for a moment at the
   join. Guess: card159.
7. client, logs/c127-c1m1-client.log:13535 (first), 24 lines. card148 gib host parts asleep above the ground (one 40
   uu check, z down to -1822 at rest). Player sees: some gib chunks floating. Guess: card148.

## C1M1 run 1 faults (junior, static)

Common factor: frame time. Host frame median 135.6 ms over the whole run (85-224 ms; host:4902, 74 frames in 10 s,
43 over 50 ms), client 132.6 ms; c140n was 9.3 ms with the same flags. A concurrent heavy job from another session
(dbc-base on an ISO, started about 09:02 and a second one at 10:04, load average 7.1) was running during the run.

**Fault 1, bombing-run pickup gave no slot.** Cause: the E press came 206 ms (about 1.5 frames at 7 fps) after the
`look`, before the interactor's TickComponent trace had refreshed its focus target, so Interact ran with
"target null (null)" (logs/c127-c1m1-host.log:4912) and the pickup's DisablePickup never ran. The pickup collision was
on (host:4769), and no card081 or card130 line fired, so the mod's trace detour returned the game's own result. In
c140n the same warp and look were followed by E at 9 ms frames and the pickup went (c140n-host.log:4987). Verdict:
scenario fault, not a mod regression: drive lines 64-65 press E right after the look with no wait.
Blocks play: no. Fix: in the scenario, wait for the focus (or for "DisablePickup on
BP_TacSuppPickup_BombingRun_Tutorial_C", as c140n line 50 does) and press E again if it is missed; do not run with
other heavy jobs on the machine.

**Fault 2, 20 client rows not acked within 5000 ms.** The rows are the client's own ledger rows (client-to-host chan
frames: Fire, Ping, Pong and others; first logs/c127-c1m1-client.log:54219, "rows 134-134 ... (acked 133)"). The host
acks in its LedgerAck (ledger_c132.cpp:489), sent through chan_send_client (ue/chan.cpp:360), the one host send point,
so it waits in the same per-connection pace FIFO as every Fx and pose frame. That FIFO peaked at 1545 frames, 965082
bytes, longest wait 32438 ms (host:138836), so the acks reached the client late. Late only, nothing lost: the
"pending and no ack" WARN (ledger_c132.cpp:518) never fired and the acked count kept advancing. Blocks play: no by
itself; it is a symptom of the backlog. Fix: none in the ledger. The backlog belongs to card 140 (pace drain at low
frame rates); a re-run at normal frame rate should show 0, as c140n, c201-r2 and c206-r1 did.

**Fault 3, 19 ragdoll fallbacks.** Cause: backlog, skipped by rule. Card 152 skips sample 0 at the ragdoll start
(pose_c132.cpp:2474) and every stream step (pose_c132.cpp:788) while the pace queue holds more than
kPoseStreamBacklog = 8 frames (line 105), and the samples that were sent waited in the same FIFO. The first fallback
(client:9309, at 137796603 ms, 2175 ms) falls in the host window with queue peak 220 frames and longest wait 3911 ms
(host:13104); host totals 343 backlog skips, backlog peak 1516 (host:142209). Player sees on the client: those 19
corpses ragdoll on their own, so their falls and rest positions can differ from the host's until a later snap
(card 137 snaps are never held back) corrects them. Blocks play: no. Fix: the same as fault 2, the card 140 backlog;
no change in card 152.

### Run c127-c1m1b -- void: GPU (not charged to the map's budget)
Void as soak evidence: two other jobs held drm-engine-gfx at 36% and 56% with our games stopped (card1 gpu_busy_percent 96), so both games ran at about 6 fps; the 1-minute load check counts CPU only and missed it.
Pre-launch rule from now on: card1 gpu_busy_percent under 30 over three 1 s samples, else do not start.
The one recorded fault, scenario line 86 (client flare copy wait): the client logged the flare throw sound (client:3325, card 136 fx relay) but no BP_SupportFlare_Tutorial copy line.
Static trace: not capped (the client copy line, world_c98.cpp:911, is uncapped); the copy was never sent. The host relays the flare only after ActivateFlare (detour_c104_flare_activate, anim_c104.cpp:232), and ActivateFlare never ran: the flare spawned at (12516, 13425, 428) (host:4986), logged one collision 205 ms later at (12639, 14306, -374) (host:4990), already below the floor, and ExpireFlare destroyed it at z -14842 (host:5088-5089).
At about 6 fps the flare fell through the world before its landed query, so no world P row for it went out (compare c140n-host:5205-5215 and c140n-client:3105, the same throw at normal frame rate). Low frame rate physics, void with the run; no fix.

### Soak C2M1 scenario (junior): tools/scenarios/c127-c2m1.txt waits and their proof
- `host wait state map=FED_C2M1 pawn=host-own`: state fields (testrun.cpp:1850); passed at about 28-30 s in c201-r1 and c212-r1.
- `client wait state map=FED_C2M1 pawn=client-own`: state fields; c212-r1.drive.
- `client wait log "card120: client t=\d+ minimap origin \S+ \(BP_PlayerCharacterMI_C\) set"`: c212-r1-client ("card120: client t=126668385 minimap origin ... (BP_PlayerCharacterMI_C) set"), chan_ops.cpp:102, uncapped.
- `client wait near 229612 38956 9100 600`: passed at 31 s in c201-r1 after the same warps.
- `try host wait log "card125: host spawned" count=3 since-mark` (every round): enemies.cpp:312, uncapped; c201-r1 has 8.
- `try host wait log "card137: host pose freeze" count=3 since-mark` (every round): pose_c132.cpp:2535; c212-r1-host.
- `try host/client wait log "card049: ... CompleteSubObjective\(LOS_O01, LOS_O01_SO01\)"`: format objectives.cpp:359 and :1134 (uncapped); trigger seen only in hb50, hb54 and hb55 (older DLL), never in c201 or c212. UNPROVEN on the current family; a try.
- `client wait log "card074: client t=\d+ GrenadeExplode"`: grenades.cpp:1386; c127-c1m1b-client.
- `host wait log "card049: host sent CompleteSubObjective\(LOS_O04, LOS_O04_SO01\)"` and the client "replayed" line: hb55 cmd 9 (the same ms as the warp); format objectives.cpp:359 and :1134. Proven only on an older DLL.
- `client wait near 196820 3390 7433 600`: point from hb55; proven only on an older DLL.
- `host wait log "cmd: \d+ look: BP_PlayerCharacterMI_C at ..."` (look-relative rounds): testrun.cpp:748 format; spawns at the look point plus offsets, as c132-decal-proof.
- `host/client wait log "card049: ... CompleteSubObjective\(LOS_O03, LOS_O03_SO01\)"`: hb55 cmd 26 at the host's own warp to 228451 2890 9205; older DLL only.
- `client wait near 228451 2790 7905 600`: hb55 (c132-decal-proof.txt:115); older DLL only.
- `client wait near 158606 23488 10615 600` and `host wait log "card049: host sent CompleteSubObjective\(LOS_O02, LOS_O02_SO02\)"`, `(LOS_O02, LOS_O02_SO01)`, client "replayed ... LOS_O02_SO01": c117a cmd 4 (client copy warp by BP_AmmoPickup_MoritaSmall_C); older DLL only.
- Not included: LOS_O03_SO02 (three targets a, b, c) and later; no scenario ever reached it.

## Senior read-out (2026-11-12): FED_C1M1 and FED_C2M1 pass, play build made

Runs c127-c1m1 and c127-c1m1b are void (another job held the GPU at 93 %, 6 to 8 fps; card 226 holds what the low
frame rate showed). Valid runs, DLL `8edaffb0745a50fe`, about 58 fps on both sides:

| | c127-c1m1c (FED_C1M1, 563 s) | c127-c2m1 (FED_C2M1, 430 s) |
|---|---|---|
| Scenario | passed, 0 FAIL | passed, 0 FAIL |
| ERROR/FATAL/CRASH, first-chance AV | 0 | 0 |
| Digest | 21 rounds, 0 mismatches | 14 rounds, 0 mismatches |
| Client ragdoll wait | 12 held, 12 cleared, 0 fallbacks | 59 held, 59 cleared, 0 fallbacks |
| Pace queue | 0 frames | peak 451 frames, longest wait 1607 ms |
| Gib budget window | 7019 of 16384 bytes | not read |
| Objectives on the client | LOS_O01, LOS_O01_SO01 | 84 host objective rows sent, markers replayed |

Recorded, none blocks play: one frozen-final bone check 485.6 uu on C1M1 (card 191 class); gib parts asleep above
the ground on the client (4 and 12 lines, corpse class); C2M1 host `card157` "HitClaim ... found no round in
1500 ms: dropped" once and 8 "refused: NPC dead" lines; one client-only door state set back by card 161; the
C2M1 pace queue peak (card 226). Scenario dead time: every `try host wait log card137: host pose freeze count=3`
timed out (15 on C1M1, 8 on C2M1): the wait no longer matches a logged line and should be replaced before the next
soak. Not reached: LOS_O03_SO02 and later on C2M1, the game's own mission end on either map.

Open on this card after the playtest: FED_C3M1, FED_C4M1, BUG_C1M1.

## Soak C3M1 scenario (junior)

Scenario `tools/scenarios/c127-c3m1.txt` (written by a generator in the scratchpad, gen_c3m1.py; not in the repo).
It has the c127-c2m1 shape: failsoft, UBWCOOP_TRAVEL to FED_C3M1, god on, TimeDilation 1.0, and 22 fight rounds.
In each round three WarriorBugs are placed off the client copy's position (look-relative) or in front of the turrets.
Both players fire, and then the host kills whatever is left. A digest round ends each stage.

What earlier FED_C3M1 runs reached (c123*, c125*, c128*, c129*; all on DLLs older than the current family; the source
is c125zc unless noted):
- Travel and landing: AUTODROPSHIP; both pawns own at about +49 s (c129c.drive). The host lands at (-70860, 17097,
  413). The client copy lands at (-61946, -49703, 450).
- Mounted turrets: 9 BP_MountableTurret_C. The client mount on _8 (-69777, -45940) and the host mount on _4
  (-71175, -45940) passed in c125zc and c129c, with TurretMount and TurretLeave on the host.
- Destructibles: benches, bins, loungers, parasols, luggage, BP_WarriorBug_Corpses_Destructible, and 13
  BP_DestructibleSpawner_Bugnest_HumanGameplay_C (bug nests, for example at (-3775, 7060, 640) and
  (-18600, -22550, 400)).
- Hazards: no hazard class appears in any C3M1 log.
- Objectives:
  - Added: Beach_O1 (Beach_Marker1, Beach_Marker2, Beach_SO1 with a count of 3 over A, B and C, and Beach_SO2),
    Resort_O1 (Resort_Marker, Resort_SO1, and Resort_SO3 with a count of 3 over A, B and C), Monorail_O1
    (Monorail_Marker, Monorail_SO1).
  - Completed: none in any run. No objective trigger point is known.
  - Stage 2 therefore shoots the nearest bug nest, with the Beach_SO1 completion as a `try` (a prediction only).

Card137 wait (order point 2): the host's corpse-frozen line is `card137: host pose freeze guid 0x%llx snap %u sent at
the game's SetAllBodiesSimulatePhysics(0)` (pose_c132.cpp:2535). It is not capped.
Real line: logs/c227-c2m1-host.log, `card137: host pose freeze guid 0x186 snap 4 sent at the game's SetA...` (drive
line 67). The c2m1 pattern "host pose freeze" does match it. The line logs only when a corpse settles, which is usually
later than 15 s after the kill: 81 freeze lines against 2897 unfreeze lines in that run, and the waits at drive lines
37 and 52 timed out. No uncapped line marks a corpse frozen within the round. The wait is dropped, and the freeze
count is read out after the run instead.

Waits and their proof (format string, plus a real line on the current family where one exists):

| Scenario wait | Format / source | Real line |
|---|---|---|
| `host/client wait state map=FED_C3M1 pawn=host-own/client-own`, `pawn=other` (on a turret) | state line testrun.cpp:1834-1835; kind "other" when the pawn is not a player character | c127-c2m1 and c227-c2m1 drives (map=, own); c125zc (turret pawn) |
| `client wait log "card120: client t=\d+ minimap origin \S+ \(BP_PlayerCharacterMI_C\) set"` | chan_ops.cpp:102, uncapped | c127-c2m1, c212-r1 client |
| `client wait near x y z r` | drive state (drive.py:279) | c127-c2m1 drive |
| `client wait log "card074: client t=\d+ GrenadeExplode"` | grenades.cpp:514 (`card074: %s t=%llu %s #%u ...`), uncapped outside a round | c127-c1m1b client, c127-c2m1 |
| `host wait log "cmd: \d+ look: BP_PlayerCharacterMI_C at (x) (y) (z) from "` | testrun.cpp look line | c127-c2m1 drive (every look-relative round) |
| `try host wait log "card125: host spawned" count=3 since-mark` | enemies.cpp:312, uncapped | c127-c2m1, c201-r1 |
| `host wait log "cmd: \d+ warp: own pawn \S+ \(...\) -> \((x), (y), (z)\)"` (host, nest warp; client, turret `behind`) | testrun.cpp c75_warp, `cmd: %u warp: %s %s (...) -> (...)`, one per command | c125zc host `5 warp: own pawn ... -> (-71175.0, -45700.0, 1500.0)`; c125zc and c129c client `1 warp: own pawn ... 135 uu from BP_MountableTurret_C_8` |
| `try host wait log "card049: host sent CompleteSubObjective\(Beach_SO1, Beach_SO1_[ABC]\)"` and the client `replayed` line | objectives.cpp:359 and :1134, uncapped | format seen for C3M1 objectives (`host sent AddSubObjective(Beach_O1, Beach_Marker1)`, c125zc); the completion itself was never seen. UNPROVEN, a try |
| `host wait log "card123: host TurretMount from BP_PlayerController_C: turret BP_MountableTurret_C at \d+ units"` | vehicles.cpp:1871, uncapped | c125zc, c129c host |
| `host wait log "card123: host TurretLeave from BP_PlayerController_C: turret BP_MountableTurret_C"` | vehicles.cpp:1891, uncapped | c123t host |
| `host wait log "card132: host t=\d+ digest round \d+ \((command|timer)\) sent: "` | digest_c132.cpp:783, once per round. `objectives` starts a "command" round (testrun.cpp:1718); a round already in flight refuses it with a WARN (digest_c132.cpp:802), so a "timer" round also matches | c127-c2m1 host `digest round 14 (command) sent: 29970 entries` |
| `client wait log "card132: client round \d+: actors \d+ host, \d+ client, mismatches "` | digest_c132.cpp:1303, once per round | c127-c2m1 client `round 14: actors 29970 host, 29970 client, mismatches 0` |

No `sleep` stands in for a state-dependent step. The sleeps that remain are these:
- 1 s before the looks of each round, as in c2m1.
- 2 s for the turret bugs to land.
- 3 s for the client frag fuse.
- 1 s between the host's turret look and its E press. This is C1M1 fault 1: the interactor's focus refresh.

No wait targets a capped line. Every warp goes through the host; the turret `behind` warp runs on the client, as in
c125-proof.

Predictions:
- Both pawns own on FED_C3M1 by about +60 s.
- The client mounts _8 and the host mounts _4: TurretMount on the host, `pawn=other` on both sides; both leave.
- 4 command (or timer) digest rounds with 0 mismatches, plus timer rounds every 30 s.
- The nest stage: the Beach_SO1 line probably does not appear (a rifle may not destroy a nest), so it is recorded
  either way.

Expected wall time is about 8 min: boot and drop about 60 s, 22 rounds of about 6 s, the turret stage about 40 s, and
4 digests. The liveness check is at 12 min.

## Soak C3M1 (junior)

Two runs on DLL 2928654b3a96090d, both with the run_pair command in the scenario header and tags c127-c3m1 and
c127-c3m1-r2. Before each run: no game process up, GPU samples 5/0/0 and 21/0/0, and the DLL prefix checked.
Each run ended by the scenario after 184 s, with host rc 0, client rc 0, FATAL/ERROR 0 (run_pair count) and no crash
dumps. Every stage was reached in both runs: landing, frag, nest, both turret mounts and leaves, and the client-copy
landing. The drive rc was 2 in both runs, from scenario lines only.

Correction to the scenario section above: on this DLL, `host warp client <class> dist behind` runs on the host. The
host logs `cmd: 106 warp: server copy BP_PlayerCharacterMI_C (-69777.0, -45700.0, 1474.3) -> (-69776.8, -45850.0,
1497.6), 135 uu from BP_MountableTurret_C_8` (c127-c3m1-host:56981), and the client logs nothing. The scenario now
waits on that host line.

Drive FAIL and TRY-FAIL lines:
- Run 1: FAIL 233 (the client wait for the `behind` warp line; the line is on the host), then 234 and 235 (no
  groups). TRY-FAIL 154 and 155 (Beach_SO1 completion, as predicted).
- Run 2: FAIL 234, `client wait near {g} 60` -> `last 175 uu of (-69776.8, -45850.0, 1497.6)`. The pawn falls about
  130 uu to the floor after the warp. TRY-FAIL 154 and 155 as in run 1. The mount at line 239 still passed.
- After run 2 the radius in the scenario is 250, so that wait now passes the 175 uu seen. That fix is not run (the
  budget is used up).

ERROR/FATAL/CRASH: 0 on both sides in both runs. The read-out script matches card067 lines whose struct names hold
`CallFunc_Unhandled_ReturnValue`; those are function dumps, not errors.

WARN classes (counts are run 1 / run 2; NEW means not in logs/c227-c2m1-*.log):
- Host:
  - 44 / 44 NEW `WARN card133: wire name of BP_Bench_Metal_Destructible_01_GEN_VARIABLE_BP_Bench_Metal_Dest does not
    fit (70 > 64)` (host:2608): benches and bins at map load, from the fx placed-key path.
  - 35 / 39 `WARN card157: host t= HitClaim`, a known class.
  - 8 / 8 NEW `WARN card136: host fx name table full (1024)` (host:2370). The C3M1 map-arm totals read
    `1024 names (1646 table full)`. This was not seen on C1M1 or C2M1.
  - 1 / 0 NEW `WARN card132: host digest round 6 (timer) still sending: command round not started` (host:68249). The
    command digest met a timer round; the client round line still matched.
  - 1 / 1 `attach: DisableThreadLibraryCalls failed`.
- Client:
  - 16 / 15 `client bone check`, known.
  - 10 / 0 `client gib host`, known.
  - 9 / 6 NEW `WARN card132: client round N pose ...` (the Pose mismatch below).
  - 0 / 1 `client pose stream map totals`, known.
  - 1 / 1 attach.

Digest rounds: run 1 had 8, run 2 had 9. Actors are about 24871 on both sides.
- Mismatches:
  - Run 1, rounds 6 to 8: Pose:1 each.
  - Run 2, rounds 8 and 9: Pose:1 each.
  - All other rounds: 0.
- Both Pose mismatches are a BP_AI_WarriorBug_C corpse from the turret stage (the bugs are placed at z 1500 and fall
  about 1000 uu). The corpse stays off in every later round (`simulating 0`), so it settled apart and does not
  converge.
  - Run 1, guid 0x4e4: `body 15 at (-69506, -46289, 533) is 105 uu from the host's (-69599, -46272, 579); 1 of 22
    bodies over 100 uu` (c127-c3m1-client:55786).
  - Run 2, guid 0x4a0: `body 6 at (-71386, -45984, 489) is 150 uu from the host's (-71353, -46005, 634); 2 of 22
    bodies over 100 uu` (c127-c3m1-r2-client:56629).

Table peaks against caps (run 2; run 1 is the same or lower):
- card136 fx live 1975 of 4096
- card136 fx name table 1024 of 1024, FULL (1646 not stored)
- card140 gib window 9923 of 16384 bytes
- card132 classes 228 of 384
- card095 collision rows 132 of 256
- card130 fx watch 23 of 64
- card130 AI pick-up 1 of 16
- card038 dead-set 21 of 256
- card152 ragdoll wait 2 of 32
- card227 gated presences 1 of 32
- pace queue 311 frames
- reliable window 136 of 511

card227 lines: one presence gated on each side (`BP_PlayerPresence_C_2 gated (LTOnEnter LR_SpawnAI_C), table 1 of
32`). Held 0, fired after hold 0, map-end peak 1 of 32.

Card137 freeze lines: 32 in run 1, 31 in run 2.

Host frame windows: every 10 s window outside the map load and the client join is 527 frames or more. The windows
that include the load and the join are 96/119 and 477/485 frames. Neither run is void.

What a player would see:
- Pose: one dead warrior bug near the turrets has a limb about 1 to 1.5 m away from where the host has it. This is
  cosmetic and it stays that way.
- fx name table full: the C3M1 map arm stores only the first 1024 placed-effect names. The other 1646 placed rows
  carry no placed key, so the client cannot place them on their prop. Most rows logged around the overflow are
  `NiagaraActivate None` (no asset) LoopingVFX on luggage, parasols and benches, which show nothing anyway. A row with
  a real asset past the cap would be missing on the client. The run does not show which rows those are.
- card133 wire name: 44 destructible props (benches and bins named `..._GEN_VARIABLE_...`, 70 characters) have no
  wire name. Their placed effects, and the other users of `c95_wire_target` (the hit relay at attributes.cpp:1142,
  hold.cpp:168 and the decals at decal_c132.cpp:325), cannot key them. A client hit on such a bench may not reach the
  host. This was not exercised in the run.

Watch items the run did not reach:
- No objective completion: the Beach_SO1 try waits both timed out, and Resort_O1 and Monorail_O1 were not tried
  (no trigger point is known).
- The bug nest was not seen destroyed.
- No hazards (C3M1 has none).
- Not reached: mission end, player death and respawn (god on), and the dropship ride (FAST=1).

Candidate cards:
1. The C3M1 fx name table overflows (1024 cap, 1646 more).
2. The card133 64-character wire name limit for `_GEN_VARIABLE_` child-actor names.
3. A turret-stage ragdoll that settles 100 to 150 uu apart and never converges (Pose:1).
4. A C3M1 objective trigger route, so that the soak reaches a completion.

## Soak C4M1 scenario (junior)

Scenario `tools/scenarios/c127-c4m1.txt` (350 steps, written from a scratchpad generator; it reuses the c127-c3m1
round and digest blocks unchanged). Failsoft, UBWCOOP_TRAVEL to FED_C4M1, god on, TimeDilation 1.0. It loads through
the drive.py parser with no misplaced warp, and every log regex compiles.

What earlier FED_C4M1 runs reached (c75a*, c75b*, c76a-h, c79a-i, c89a-g; all on DLLs older than the current family):
- Travel and landing: `UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C4M1?listen` with AUTODROPSHIP. Both pawns land at about
  (20790, -94940, -865) (c76c-host.log:1321 server copy, :1324 own pawn).
- Mechs: two pod mechs, BP_BaseMech_C, appear 9 to 17 s after the load, each about 290 uu from a
  BP_MechDropPlatform_C: mech A at (3052, -61452, 1519) and mech B at (-28941, -83026, 129) (c89g-host.log:1009-1011,
  same in c79i and c76c). `host warp client BP_BaseMech_C 300` takes the client to mech A (c76c-host.log:1321).
- Mech entry: client entry with E passed in c76c (host :1350 `card075: host MechEnter from BP_PlayerController_C: mech
  BP_BaseMech_C at 1 units`). The host entered the same mech in c76c (:2329/:2333, `EnterMech after`, capped lines).
  Nobody entered mech B in any run.
- Mech fire: c76c fired primary 3 x 1 s and secondary once on both sides; the proof lines are card076 (capped at
  kC76MechLines and gated by UBWCOOP_C76_LOG).
- Downs and revive: the c79 runs used GOD=0 and the UBWCOOP_C79_DOWN test path (card079 lines, test env only). The
  current down path is `host down host|client` (card 109, testrun.cpp:932-946), which calls c43_enter_down directly;
  c210-r2 ran it with GOD=1 and downed both players (host :5020/:5047, client :4289). The revive steps are those of
  c142-r4 and c171-r1 (GOD=0 runs on C1M1).
- Park objectives (card 199): Park_O1, Park_Marker, Park_SO1 (3 BP_SatchelBuilding_Flakbug_C), Park_SO2 (5
  BP_DestructibleSpawner_Bugnest_HumanGameplay_C) are added before LoadMap returns (c89g-host.log:711-733), in the
  journal-reset gap, so the client lacks them. LZ_O1 is added after the load. No run completed or triggered any Park
  objective. c199-r1 is FED_C2M1 to BUG_C1M1, not this map.

Lines the old runs waited on that no longer exist or must not be waited on:
- `card075: client blocked spawn`: no longer in the source.
- `card075: %s t=%llu mechs %u -> %u` (death.cpp:3160): exists, capped at kC75CountLines=32. Not waited on; the mech
  stage comes minutes after the mechs appear.
- `card075: %s %s %s: mech ...` (EnterMech/MechEnter/LeaveMech before/after, vehicles.cpp:1432): exists, capped at
  kC75Lines=128. Not waited on.
- `possess: AcknowledgePossession` (input_diag.cpp:110): exists, capped, client only. Not waited on.
- card076 lines: exist, capped and gated by UBWCOOP_C76_LOG. Not waited on; the run does not set the flag.
- `card079: host t=%llu test down` (death.cpp:2604): exists, test env only. Not used.

Stages:
1. Landing point: warps, a host frag and a client frag, 6 fight rounds, digest.
2. Mechs: the client enters mech A, the host enters mech B, 3 mech rounds (2 WarriorBugs 1200 uu north of each mech,
   both pilots aim at the nearest and fire 3 s; secondary fire once after the first round), both leave, digest.
3. Downs at the landing point: `host down client`, the host revives (E held 6 s); `host down host`, the client
   revives. 2 fight rounds, digest.
4. The bug nest nearest the landing point: both shoot it 5 s, the Park_SO2 completion as a `try`. 4 fight rounds,
   digest.

Waits and their proof (the waits shared with c127-c3m1 keep the proof listed in "Soak C3M1 scenario (junior)": state
map/own, card120 minimap, wait near, card074 GrenadeExplode, look, card125 spawned, card132 digest host and client):

| Scenario wait | Format / source | Real line |
|---|---|---|
| `host wait log "cmd: \d+ warp: server copy ... \d+ uu from BP_BaseMech_C"` | testrun.cpp:822 (now `%.1f` coordinates and ` (yaw %.1f)`), one per command | c76c-host.log:1321 (old integer format; the regex takes either) |
| `host wait log "card075: host MechEnter from BP_PlayerController_C: mech BP_BaseMech_C at \d+ units"` | vehicles.cpp:1721, uncapped | c76c-host.log:1350 |
| `client wait log "cmd: \d+ warp: own pawn BP_BaseMech_C"` after `client warp actor point 3233 -61494 1769`, and `host wait log "cmd: \d+ warp: own pawn BP_BaseMech_C"` after `host warp actor point -28886 -83182 379` (in a mech) | testrun.cpp:822: the warp line names the pawn it moved, which is the mech while it is piloted | c127-c4m1-r2-client.log `cmd: 18 warp: own pawn BP_BaseMech_C (3207.9, -61487.0, 1521.1)`, c127-c4m1-r2-host.log `cmd: 66 warp: own pawn BP_BaseMech_C (-28844.4, -83183.8, 130.7)`. These replace the run 1 waits `wait state weapons=-`, which cannot see a pilot: BP_BaseMech_C is a player MI pawn, and in run 1 the state read `weapons=1` in the mech |
| `host wait log "cmd: \d+ warp: own pawn BP_PlayerCharacterMI_C ... -> \(-2\d{4}\.\d, -8\d{4}\.\d, ...\), \d+ uu from BP_BaseMech_C"` (host at mech B) | testrun.cpp:822 | format on the current family (c125zc, c229-c3m1); mech B as a warp target: SOURCE ONLY |
| `host wait log "card075: host MechLeave from BP_PlayerController_C: mech BP_BaseMech_C"` | vehicles.cpp:1742, uncapped | c76c-host.log:2236 |
| `host wait log "cmd: \d+ warp: own pawn BP_PlayerCharacterMI_C "` (the host's own leave: in a mech the warp names BP_BaseMech_C) | testrun.cpp:822 | c125zc, c229-c3m1 (own pawn MI) |
| `host wait log "cmd: \d+ down: slot \d+ "` | testrun.cpp:943 | c210-r2 host |
| `client wait log "card043: client t=\d+ own pawn Up -> Downed"` | death.cpp:1384 | c210-r2-client.log:4289 (GOD=1) |
| `client wait log "card156: client t=\d+ back to Up \(revive from Downed\):"` | death.cpp:2415; any reviver (host or an AI trooper) | c142-r4-client.log:4748 (GOD=0) |
| `host wait log "card043: host t=\d+ BP_PlayerCharacterMI_C \(host-own, slot 0\) -> Downed"` | hooks_ubw.cpp:1101 | c171-r1-host.log:5080 (GOD=0); c210-r2-host.log:5047 (GOD=1, bug pawn) |
| `client wait log "card043: client t=\d+ host pawn Up -> Downed"` | death.cpp:1384 | c171-r1 client (`host pawn Downed -> Up` at :4623 is the same format) |
| `host wait log "card043: host t=\d+ BP_PlayerCharacterMI_C \(host-own, slot 0\) -> Up "` | hooks_ubw.cpp:1101, reason `revived from %s after a %llu ms hold` (death.cpp:719) | c171-r1-host.log:5132 (GOD=0) |
| `client wait log "card043: client t=\d+ host pawn Downed -> Up"` | death.cpp:1384 | c171-r1-client.log:4623 |
| `try host wait log "card049: host sent CompleteSubObjective\(Park_SO2, Park_SO2_[A-E]\)"` and the client `replayed` line | objectives.cpp:359 and :1134, uncapped | format seen (`host sent CompleteSubObjective(Assault_O1, Assault_SO1)`); Park never completed. UNPROVEN, a try |

The revive on a GOD=1 run is source-only: god (death.cpp:393) blocks only Health decreases, and a revive sets Health
up, so it should pass; c210-r2 proves only the down under god. God stays on for every stage.

Sleeps: 1 s before each round's looks; 1 s between each mech look and its E press (C1M1 fault 1, the interactor's
focus refresh); 3 s for the client frag fuse. No sleep stands in for a state-dependent step. Every `warp client` goes
through the host; the client moves only its own pawn (`client warp actor point`).

Run:
```
systemd-run --user --scope -q -p MemoryMax=12G env SCENARIO=tools/scenarios/c127-c4m1.txt DEPLOY=1 FAST=1 \
  HOST_EXTRA_ENV="UBWCOOP_AUTODROPSHIP=1 UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C4M1?listen UBWCOOP_GOD=1 UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" \
  CLIENT_EXTRA_ENV="UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" tools/run_pair.sh c127-c4m1 1800
```
Expected wall time is about 8 min (boot and drop 60 s, 15 rounds, the mech and down stages about 90 s, 4 digests).
The liveness check is at 12 min.

What the scenario cannot reach:
- Park_O1 and Park_Marker: no trigger point is known.
- Park_SO1: the flak bug buildings need a satchel charge; no driver command places one.
- Park_SO2: only one nest is shot, and a rifle may not destroy it; the completion is a prediction.
- Mech fire on the receiver: no uncapped line proves a mech round reached the client; only the digest rounds and the
  shots show it.
- Mech B entry: never done in an earlier run.
- The client's Park objective list: the journal-reset gap of card 199 item 9 means the client list lacks Park
  objectives; the `client objectives` output records it, nothing fixes it here.

## Soak C4M1 (junior)

Two runs on play build DLL 20cf859dd191a8fe (no rebuild), `tools/scenarios/c127-c4m1.txt`, the command above.
Before each launch: no game process; GPU card1 samples 0/0/0.

Run 1, tag c127-c4m1 (200 s, drive rc 2): 2 FAILs, both scenario faults.
- Line 146 `client wait state weapons=- timeout=10` and line 155 `host wait state weapons=- timeout=10` failed with
  `last state weapons=1`. Cause: BP_BaseMech_C is a player MI pawn (log_player_pawn logs its PossessedBy, the
  `EnterMech after` line names the controller's pawn BP_BaseMech_C), so the state line of a pilot shows a weapon
  inventory like a pawn on foot. Both entries happened: host:34308 `MechEnter ... at 0 units` for the client, host:46450
  `EnterMech after` (roles 3/1) for the host.
- Fix for run 2, taken from the run 1 lines: each pilot warps its own pawn 100 uu over its mech (mech A at
  3233 -61494 1669 and mech B at -28886 -83182 279, the run 1 look lines) and waits for the warp line to name
  `own pawn BP_BaseMech_C`.

Run 2, tag c127-c4m1-r2 (184 s, drive rc 0, scenario passed): 0 FAILs. Both mech proofs passed (client `cmd: 18
warp: own pawn BP_BaseMech_C`, host `cmd: 66 warp: own pawn BP_BaseMech_C`), MechLeave passed, both downs and both
revives passed.

Both runs:
- TRY-FAIL: only the Park_SO2 completion pair (run 1 lines 310/311, run 2 lines 317/318). 5 s of rifle fire from
  both players does not destroy the nest.
- FATAL/ERROR: 0 on both sides. No crash dumps.
- Digest: 9 rounds per run, mismatches 0 in every round. Actors were 28499 to 28514, equal on both sides.
- Host frame windows: after the first window (map load: 96 and 86 frames), every 10 s window had 528 frames or more.
  Neither run is void.
- Table peaks against caps (run 1 / run 2):
  - fx live: 2524 / 2385 of 4096
  - gib window: 3314 / 9433 of 16384 packed bytes
  - collision rows: 176 of 256
  - inventory classes: 187 of 384
  - fx watch: 35 / 31 of 64
  - AI pick-up: 4 / 3 of 16
  - reliable bunches: 153 / 156 of 511
  - pace queue: 244 frames
  - client dead-set: 26 of 256
  - client gated presences: 3 of 32
- Client objective list: 14 entries on both sides at every `objectives` command. The client replayed Park_O1,
  Park_Marker, Park_SO1 and Park_SO2 (c127-c4m1-r2-client.log:1969-1976), so the card 199 item 9 gap did not show.

WARN classes new against c229-c3m1:
- Host `card132: host hit <NPC>: impact ... is N uu from the NPC`. Run 1: 2 BP_AI_ChariotBug_C at 2187 and 2785 uu,
  3 BP_AI_WarriorBug_C at 1447 to 6017 uu. Run 2: 1 BP_AI_ArcherBug_C at 1275 uu. Damage was 8 to 13. The host
  applied a hit to an NPC 13 to 60 m from the impact point. A player sees a bug take damage from a shot that landed
  far from it. This is host-side; it is not a sync fault.
- Host `card132: host t=H OnHit of ADProjectile, whose GrenadeExplode already went: no second frame`: 1 in run 1
  (ArcherBug), 2 in run 2 (BP_KillableProp_ExplosiveCar_C). It only records that a second explode was not sent; nothing
  is visible.
- Client `card125: client turret row BP_MountableTurret_C_<runtime id> at (x, y, z): no turret matches, skipped`, each
  with its `recv ... WorldOp ... result skipped` line: 4 in run 1, 2 in run 2. The rows are runtime turrets, at points
  where a BP_AI_TurretTrooper_C died (9425 -55430 266 in both runs; 21470 -95170 -874; 5995 -60245 278; 5525 -62965 278).
  The client has no placed copy for them, so that turret's world op is not applied on the client. That client may
  keep the turret in its old state.

Watch items (not faults):
- Abandoned mechs self-destruct about 60 s after their pilot leaves: in run 1 the eject and buildup sounds at
  host:106355 and :107829, two explosions, then `mechs 2 -> 1 -> 0` (host:108915, :111244). Run 2 is the same. The
  game logs a second `LeaveMech after` pair at the eject (host:106352, :107826). The client plays the same fx rows
  (client:73126-76993).
- The client's mech count drops `1 -> 0` when the client goes back to the landing point (relevance). The client never
  listed mech B, which is about 390 m (38700 uu) from mech A.

Not reached: Park_SO2 completion (the nest survives 5 s of fire), Park_O1, Park_Marker, Park_SO1, and mech fire on the
receiver (its lines are capped or gated).

Candidate cards:
1. Runtime turret rows: the card125 client match has no rule for turrets spawned at run time (TurretTrooper
   deploys). It needs a NetGUID or spawn-row match.
2. Host hit far from the NPC: find which weapon or projectile path sets the hit NPC 13 to 60 m from its impact
   (ChariotBug, WarriorBug, ArcherBug).
3. Park_SO2 reach: a nest kill needs more than 5 s of rifle fire. For a soak that completes it, use grenades or a
   longer burst.

## Senior read-out: FED_C3M1 and FED_C4M1 (2026-11-12)

Both soaks accepted; checked by the senior from the logs (drive passed, 0 failed lines, every digest round
0 mismatches, 0 errors on both sides).

- FED_C3M1 (c127-c3m1, c127-c3m1-r2 on `2928654b3a96090d`; c229-c3m1 on `20cf859dd191a8fe`): faults went to
  card 229 (fx name table cap, fixed and in the play build), 230 (wire name over 64), 231 (corpse settles
  apart), 232 (digest round overlap, test instrument only). Not reached: objective completions, mission end.
- FED_C4M1 (c127-c4m1, c127-c4m1-r2 on `20cf859dd191a8fe`): fault went to card 233 (turret rows for run-time
  turrets). "impact is N uu from the NPC" and "OnHit of ADProjectile, whose GrenadeExplode already went" are
  known classes (cards 165, 167). Not reached: Park_SO2 completion, Park_O1, Park_Marker, Park_SO1, mech fire
  lines on the receiver. Watch item: an empty mech destroys itself about 60 s after the pilot leaves, same on
  both sides.

No play blocker found on either map. Remaining map: BUG_C1M1.

## Soak BUG_C1M1 scenario (junior)

Scenario `tools/scenarios/c127-bugc1m1.txt` (325 steps after the run 1 fix, written from a scratchpad generator). Failsoft, god on,
TimeDilation 1.0. It loads through the drive.py parser with no misplaced warp. DLL `20cf859dd191a8fe`, no build.

What earlier BUG_C1M1 runs reached (c195-r1, c196-r1/r2, c199-r1, c208-r1..r3, c210-r1/r2, c212-r1; c196-r2,
c199-r1, c210-r2 and c212-r1 passed):
- Travel: start on FED_C2M1 (`UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C2M1?listen`, AUTODROPSHIP), `host ui continue`, then
  `host travel next` to BUG_C1M1 (host LoadMap, client reconnect and two LoadMaps).
- Pawn: BP_PlayerCharacterBug_C on both sides. Forms 0 BP_BugFormDefinition_Warrior_C, 1 Hopper, 2 Tanker; tags
  BugForms.Can.Hopper and BugForms.Warrior, so Tanker is not allowed (c212-r1-host.log:3794). Health 400.
- Abilities: Warrior melee BP_BugForm_MeleeAbility_Warrior_C (LMB; seen only as server-copy, c212-r1-host.log:4267;
  no run logged a host-own melee); GA_BugForm_SwitchAbility_C with Equip/Unequip of each form (Two to the Hopper, One
  back); Hopper Jump and Glide (Space), Swoop (LMB while gliding), DamageVolume; a grounded Hopper LMB is refused
  (Swoop needs airborne).
- Down and revive: `host down client` downs the bug client (c196-r2-client.log:3841); downing both runs the bug
  StartDeath and the death screen. No revive: card 196 records that a bug client's Use press is dropped
  (`chan_ops.cpp:36` passes Use to c43_receive_use only for an MI pawn), BUG_C1M1 has no Use target and no AI trooper.
- Enemies: the map places none (card098 tallies show only class defaults, `placed=0`); `host spawn bug` places
  BP_AI_WarriorBug_C (c212-r1, count 3).
- Objectives: the level script adds Awaken_O1 (c199-r1-host.log:3615, client replay c199-r1-client.log:3601). No run
  completed it or found a trigger.

Stages:
0. Travel from FED_C2M1, arrival digest.
1. Landing tunnel: the host walks 1200 uu past the client (c212-r1), 6 Warrior rounds (3 WarriorBugs spawned
   between the players, both press LMB 2.5 s, the host kills what is left). Round 1 carries the melee proofs. Digest.
2. Each bug ability once per side: client Two, Space (Jump, Glide), LMB 300 ms gliding (Swoop), One; then the host
   the same. The other side logs the moving pawn after the Swoop and takes a shot. Digest.
3. The host warps 1200 uu past the client again (the swoops moved both), 6 Warrior rounds, digest.
4. The client is downed (`host down client`); the run quits inside the 30 s bleedout, so neither the Out state nor
   the all-down death screen is reached. Objective lists on both sides, shots, quit.

Waits and their proof (state, look, card125, card132 digest: as in "Soak C3M1 scenario (junior)"):

| Scenario wait | Format / source | Real line |
|---|---|---|
| travel: `UEngine::LoadMap returned true`, `reconnected \(`, card138 `(BP_PlayerCharacterBug_C)`, card120 minimap, card098 `world journal replay to BP_PlayerController_C \(first Pong of the map\)` | engine; begin_c138.cpp:74, chan_ops.cpp:102, world_c98.cpp:1671 | c212-r1 passed these; c212-r1-host.log:2404 |
| `try client wait log "card049: client t=\d+ replayed AddObjective\(Awaken_O1"` | objectives.cpp | c199-r1-client.log:3601 (a try: the add may land in the journal-reset gap) |
| `card208: host|client bug forms of \S+ \(host-own|client-own\): tags ` | weapons.cpp:1307 | c212-r1-host.log:3794 |
| `card024: host relay press \(Fire 1\) t=\d+: pawn BP_PlayerCharacterBug_C, weapon ` | fire.cpp:199 | c212-r1-host.log:4268 |
| `playlog: host ... server-copy( \(client request\))?: BP_BugForm_MeleeAbility_Warrior_C`; host-own melee as a try | playlog.cpp:576, one per class per pawn per 2 s | c212-r1-host.log:4267; host-own: SOURCE ONLY, never logged |
| `card057: client t=\d+ switch to weapon \d+: Equip sent` | weapons.cpp:1161 | c212-r1-client.log:4326 |
| `card210: host t=\d+ Equip 1 from \S+: tag BugForms\.Can\.Hopper ...` and `Equip 0 ... tag - none needed` | weapons.cpp:1513 | c212-r1-host.log:5009; c210-r2-host.log:4928 |
| playlog server-copy GA_BugForm_SwitchAbility_C, BP_BugForm_EquipAbility_Hopper_C | playlog.cpp:576 | c212-r1-host.log:5008, :5019 |
| `card208: host t=\d+ form [01] of \S+ \((server-copy|host-own)\) equipped: BugForm sent to 1 client` | weapons.cpp:1336 | c212-r1-host.log:5028, :5277; c210-r2-host.log:3686 |
| `card208: client t=\d+ BugForm [01] for the host pawn \S+: EquipWeaponByClass\(\S+\), equipped ...` | weapons.cpp:1435 | c212-r1-client.log:4543; c210-r2-client.log:4245 |
| `card210: client form look of \S+ \((client-own|other)\): equipped [01], .*\(\+1 s,` | weapons.cpp:1547 | c212-r1-client.log:4388, :4594; c210-r2-client.log:3634 |
| playlog client-own / host-own BP_BugForm_GlideAbility_Hopper_C | playlog.cpp:576 | c212-r1-client.log:4440; c212-r1-host.log:5371 |
| `card024: client local activation handle \d+ = 1, ability BP_BugForm_SwoopAbility_Hopper_C`; host-own Swoop playlog | input_diag.cpp:1590; playlog.cpp:576 | c212-r1-client.log:4450; c212-r1-host.log:5387 |
| `cmd: \d+ warp: own pawn BP_PlayerCharacterBug_C` | testrun.cpp:822 | format proven in c127-c4m1-r2 (`own pawn BP_BaseMech_C`) |
| `card043: host t=\d+ BP_PlayerCharacterBug_C \(server-copy, slot \d+\) -> Downed ` | hooks_ubw.cpp:1101 | c196-r2-host.log:4612 |
| `card043: client t=\d+ own pawn Up -> Downed` | death.cpp:1384 | c196-r2-client.log:3841 |
| `host wait log "playlog: host ... host-own: BP_BugForm_DamageVolumeAbility_Hopper_C"` before the host's One (added after run 1) | playlog.cpp:576; the Swoop impact, +0.57 s after the Swoop line | c127-bugc1m1-host.log:39128, c212-r1-host.log:5472 |
| `when host last "card208: host t=\d+ form \d of \S+ \(host-own\) equipped: " "form 1 " host key One` (up to three re-presses, each after a 3 s try wait on the form 0 line) | drive.py run_when; weapons.cpp:1336 | c127-bugc1m1-host.log:37982 (form 1); run 1 showed One refused 1.03 s after the Swoop (host:39335, no form line after it) |

Sleeps: 1 s before each round's looks. No sleep stands in for a state-dependent step; no wait is on a capped line
(the playlog lines are once per class per pawn per 2 s, and each is waited on once per step).

Run:
```
systemd-run --user --scope -q -p MemoryMax=12G env SCENARIO=tools/scenarios/c127-bugc1m1.txt DEPLOY=1 FAST=1 \
  HOST_EXTRA_ENV="UBWCOOP_AUTODROPSHIP=1 UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C2M1?listen UBWCOOP_GOD=1 UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" \
  CLIENT_EXTRA_ENV="UBWCOOP_C132_FX=1 UBWCOOP_C136_LOG=1" tools/run_pair.sh c127-bugc1m1 1800
```
Expected wall time is about 6 min (boot 60 s, travel 60 s, 12 rounds, the ability stage about 60 s, 4 digests). The
liveness check is at 9 min.

What the scenario cannot reach:
- A revive on the bug pawn: the game's bug pawn has no revive path the mod relays (card 196).
- Awaken_O1 completion: no trigger is known.
- The Tanker form: the pawn lacks BugForms.Can.Tanker.
- Other bug enemies (Tanker, Chariot, Jumper): the map places none and `spawn bug` places only WarriorBugs.

## Soak BUG_C1M1 (junior)

DLL 20cf859dd191a8fe, no build. Two runs, the budget.

### Run 1 (`c127-bugc1m1`): host One refused, scenario fault

- Drive rc 2. The waits at lines 215, 216 and 217 failed. Every other wait passed, both melee proofs included.
- Logged cause: the host pressed One 1.03 s after its Swoop. The Swoop is at host:38800, the DamageVolume impact at host:39128, and One is `cmd 68` at host:39333.
- `ConstructWeaponSwitchRequest(0, ...) (host-own)` was logged at host:39335. Nothing followed it: no card208 form line and no card210 Equip.
- Card 210 says vanilla EquipAbility is blocked while the pawn is Falling or Gliding. The host then fought stage 3 as a Hopper.
- Fix in the scenario (gen_bug.py):
  - The host waits for its own DamageVolume ability line, which marks the Swoop impact.
  - It presses One.
  - While the host's last own card208 line still says form 1, it re-presses One after each 3 s try wait, at most three more times.
  - This fix produces 325 steps, and the scenario section above lists the two new wait rows.
- Read-out:
  - 0 ERROR.
  - Host WARN 3. One is DisableThreadLibraryCalls; two are new: `host hit BP_AI_WarriorBug_C`, impact 1061 and 1380 uu from the NPC, host:17110 and :17203.
  - Client WARN 6: DisableThreadLibraryCalls, card152 bone check frozen final, card148 gib asleep, card152 pose stream map totals, and one new, `client digest round 8 cut by the map change`.
  - 7 digest rounds, 1076 actors on both sides, 0 mismatches.

### Run 2 (`c127-bugc1m1-r2`): passed, two screenshot steps failed on the name

- `ended by scenario after 126 s; host rc 0, client rc 0`, no crash dumps. Drive rc 2.
- Drive span 124 s. Dead time: sleeps 12.0 s, timed-out try waits 3.1 s in 1 (line 220).
- Failed lines 183 and 184: `shot c127-bugc1m1-client-swoop` was skipped with `bad shot name (1-40 chars of [A-Za-z0-9_-])`.
  - run_pair prefixes the tag: `c127-bugc1m1-r2-c127-bugc1m1-client-swoop` is 41 chars, against 38 in run 1.
  - Test only. The other shots are shorter and were taken.
- Host One gate:
  - The first One, 0.46 s after the impact line, was refused again (`cmd 68` and the switch request at host:32550 and :32552, nothing after).
  - Line 220 timed out, and the `when` re-press at line 221 (3.6 s after the impact) gave `form 0 ... BugForm sent to 1 client`. The client BugForm 0 wait passed.
- Every proof wait passed:
  - travel to BUG_C1M1 and the bug forms on both sides;
  - round 1 melee: host relay press, server-copy Melee, and the host-own Melee try;
  - client Two/Glide/Swoop/One with server copies;
  - host Two/Glide/Swoop/One with the client copies;
  - warp in stage 3;
  - client down: host `card043 ... BP_PlayerCharacterBug_C (server-copy, slot 1) -> Downed` at host:48435, client `own view: Downed, 29 s left` at client:33343.
- ERROR: 0 on both sides. The ERROR filter lists only card067 widget function dumps, whose parameter names contain `Unhandled`.
- Host WARN, 7 lines:
  - 5 NEW `host hit BP_AI_WarriorBug_C`: impact 1351 to 2175 uu from the NPC, host:27461 to :28145, the last round before the stage 2 digest.
  - 1 DisableThreadLibraryCalls.
  - 1 NEW `shot ... bad shot name` (test only).
- Client WARN, 11 lines:
  - 6 card152 bone check: frozen final 276.9 uu from body 0 at client:8055 and 37.9 uu at :13149.
  - 1 DisableThreadLibraryCalls.
  - 1 card148 gib host part asleep above the ground (client:15321).
  - 1 NEW bad shot name (test only).
  - 1 card152 pose stream map totals.
  - 1 NEW `client digest round 7 cut by the map change` (the quit cut a timer round, test only).
- New against `c127-c4m1-r2` means a class that was not in that run. Only `host hit BP_AI_WarriorBug_C` is a game-side class, and it is the class of cards 165 and 167 ("impact is N uu from the NPC").
- Digest: rounds 1 to 6 received on the client, 1076 host, 1076 client, 0 mismatches each. Rounds 1, 3, 4 and 6 were command rounds and 2 and 5 timer rounds. Round 7 was cut by the quit.
- Host frame windows per 10 s: 102 285 454 (load and join), then 584 584 585 583 584 584 585.
- Peaks:
  - pace queue 437 frames (host:8631, at the join);
  - reliable window 49 of 511;
  - gib budget 1100 of 16384;
  - inventory classes 154 of 384;
  - fx live 116 of 4096;
  - fx watch 11 of 64;
  - AI pick-up 3 of 16;
  - collision rows 165 of 256 (the dropship landing map, before travel).
- Objectives: host and client each list 1 objective (`ObjectiveListItems -1, box slots -1, objectives 1`) at every `objectives` step, so they match. Nothing completed.

### What a player would see

- `host hit BP_AI_WarriorBug_C`, 1351 to 2175 uu: a host shot or melee lands on a WarriorBug whose body is 13 to 22 m from the impact point. The damage counts on a bug that is not where the hit struck. Class of cards 165 and 167, no new card.
- The host's One right after a Swoop is dropped (both runs). The player presses 1 as the Swoop lands, stays a Hopper, and has to press again. This matches card 210's vanilla rule (no EquipAbility while Falling or Gliding). It was not checked on a standalone game, so whether vanilla drops it too is open.
- card152 frozen final 276.9 uu: a client corpse froze with its pelvis bone off its body, so the corpse looks stretched or split. This is a known class (also in c127-c4m1-r2).
- card148 gib asleep above the ground: one gore piece on the client rests in the air. Class of card 212.
- The digest round cut at quit, and the bad shot name: test only, nothing to see.

### Not reached

- A revive on the bug pawn (card 196: the client's Use press is dropped, and the map has no Use target).
- The all-down death screen.
- Awaken_O1 completion (no trigger known).
- The Tanker form (no BugForms.Can.Tanker).
- Bug enemies other than WarriorBug (none placed, and `spawn bug` places only WarriorBugs).
- The swoop screenshots of run 2 (shot name). Run 1 has them.

### Candidate cards

None new. The hit distance is the class of cards 165 and 167. The One-after-Swoop drop is a note unless a standalone check shows vanilla takes the press. The scenario's shot name `c127-bugc1m1-client-swoop` should be shortened before any further run with a suffixed tag.

## Senior read-out: BUG_C1M1 and close (2026-11-12)

Accepted. c127-bugc1m1-r2 on `20cf859dd191a8fe`, checked from the logs: 0 errors on both sides, 6 digest rounds
with 0 mismatches, host frame windows 583 after the load, every state wait passed. The two failed lines (183, 184)
are screenshot names over 40 characters with the `-r2` tag; shot names shortened to `c127-bug-*` by the senior,
no rerun. No play blocker, no new card: the WarriorBug hit distance is the class of cards 165 and 167.

Notes kept here, not carded:
- Form key 1 pressed within about a second of a Swoop landing is dropped; the player presses again. Card 210
  records the game's own rule (no form change while Falling or Gliding). Card it only if standalone accepts
  the same press.
- Not reached on BUG_C1M1: revive on the bug pawn, the all-down death screen, Awaken_O1 completion, the Tanker
  form, enemies other than WarriorBug.

All five maps soaked (FED_C1M1 to FED_C4M1, BUG_C1M1). Card closed. Play build stays `20cf859dd191a8fe`.
