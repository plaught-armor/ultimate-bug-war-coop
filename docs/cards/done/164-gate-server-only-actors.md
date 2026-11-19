# Card 164: The digest gate leaves server-only actors out

Status: queued 2026-11-08, from docs/playtest-2026-11-08.md findings 3 and 21. Budget: one run.

Read: card 132 (digest), docs/playtest-2026-11-08.md findings 3, 21.

## Why

Every round reported the 5 RecastNavMesh actors (presence host 1 client 0), and BP_AISpawnPoint_C _14, _15 and
_179-_182 were 100-550 uu apart. Navmesh is server-only. Spawn points may be moved by the host's spawn director.

## Do

1. Exclude RecastNavMesh classes from the gate.
2. Static: check whether anything the client draws or plays uses BP_AISpawnPoint_C locations. If nothing, exclude
   them too and say why; else send their location.

## Done when

- A run: the gate reports neither class, and a count line names what was excluded.
- The usual Report items (WARN diff, run length, rc, ERROR/FATAL, CRASH, dumps, DLL hash). It ends with "Report
  complete."

## From card 145 r3 (senior, 2026-11-08)

Client digest round 2 in c145-r3 (logs/c145-r3-client.log, t=273794212) had 4 location mismatches of about (250, 200)
uu: STAISpawner_5, STSpawnPoint_7 and _8, and BP_Dropship_LandingBlocker_C_0, all attached to the departing
AUTODROPSHIP dropship. Rounds 3 and 4 were clean. Likely the gate samples a moving actor at different times on each
side. Added scope: for actors attached to a moving parent, compare the relative transform to the parent (or allow a
velocity times sample-skew tolerance), and say which. Card 159 owns the attach itself at join.

## Senior trace and scope (2026-11-11)
All in `mod/src/ubw/digest_c132.cpp`; both sides build their digest with the same code, so a rule in the build
applies to host and client alike.
- **Where an entry is made (verified):** `c132_add_actor` (:433). It names the actor (`c132_actor_name`), indexes
  the class (`c132_name_index`), reads the world location of the anchor (`actor_location(anchor)`), and stores it in
  steps (`c132_step`). `c132_compare_pair` (:1031) compares class, state bits, ext and location against
  `kC132LocTolerance`; a host entry with no client entry is a "presence" mismatch (`c132_host_only_mismatch`).
- **Where exclusion exists today (verified):** `c132_excluded` (:974) only excludes a key with a ledger row newer
  than the last one sent. There is no class exclusion.
- **Server-only classes:** add one fixed table of excluded class names checked in `c132_add_actor` before the
  entry is added, with a per-map count logged once in the digest summary line ("left out N actors of M server-only
  classes"). RecastNavMesh goes in. BP_AISpawnPoint_C goes in only if the trace shows the client reads nothing from
  it (open: name the client-side reader or state there is none; card 144 owns the client spawner state, so do not
  send anything from here).
- **Actors attached to a moving parent:** `c140_attach_parent(root)` (:401) already finds the attach parent. When
  it is not null, store the root's RelativeLocation (`read_vec3(root, 0x140)`, card 034 reflected) in `e.loc`
  instead of the world location, on both sides. The class and state compare stay as they are. Open: whether an
  actor attached on the host is always attached on the client at digest time; if not, the entry must carry an
  "attached" bit so a one-sided attach reads as a mismatch of that bit, not a location jump.
- **Predictions (one run, FAST=0 so the dropship ride is in):** no mismatch line of class RecastNavMesh or
  BP_AISpawnPoint_C; no "location" mismatch for an actor the card 140 attach line names; the summary line gives
  the left-out count; mismatch total in the run is not higher than the last accepted run for other classes;
  baseline WARN only.
- **Files:** `digest_c132.cpp`, one scenario, the card. Not `hooks_internal.h` unless the entry layout changes.

## Evidence from c152-r1 (senior, 2026-11-11)

Client round 2 (t=94310094, `logs/c152-r1-client.log`): 12 mismatches in three groups that share one offset each
(about (250, -450, -150), (50, -500, -100) and (50, 150, 0) uu): BP_AISpawner_C_0/21/22 (WorldOp rows seq 192, 196,
197 applied 57 s earlier), BP_Dropship_LandingBlocker_C_0/3/4, BP_AISpawnPoint_C_2/49/57/58/59 and NiagaraActor_14,
the last three classes with "no row sent". One shared offset per group means each group rides one moving parent
(a dropship landing set). The trace states the parent of each group and whether the attached-actor rule above
(RelativeLocation for attached actors) covers all four classes, or whether the spawner's WorldOp row needs a later
end-state row once the parent stops.

## Evidence from c186-r1 (2026-11-11)
- Client round 2: Pose mismatch on guid 0xa8 (frozen row, 269 uu, 5 of 22 bodies). The host sent "pose unfreeze
  guid 0xa8 snap 3" 333 ms after the round: the host corpse moved before its unfreeze snap went out. A row whose
  host bodies moved since its last snap is left out of the round (the host self-check already reads that).
- The same round's 6 actor mismatches near dropships in flight are card 190's, and may be this card's moving
  parents.

## Added 2026-11-12 (from the card 199 trace)

- The digest's "Q" name query searches level actors only, so a mismatch on an objective key (FNV-1a of
  "O:<name>", c195-r1 key 88364238 = Awaken_O1) prints no name. Make the query name objective keys too.

## Trace (junior, 2026-11-12)

Step 1 only: no code, build or run. Each line is marked verified (V) or open (O). "dc" is
`mod/src/ubw/digest_c132.cpp`.

### a. Mismatch groups: attach parent, motion, rule

| Run, round | Group (mismatched entries) | Parent, host and client | Parent moving? | Rule | Status |
|---|---|---|---|---|---|
| c201-r2 r2 (client t=110170675, 9) | BP_AISpawner_C_21/_22, BP_Dropship_LandingBlocker_C_3/_4, NiagaraActor_14 | SceneRoot of BP_ProceduralDropshipTroops_C on both sides; relative (0,0,800) spawner, (0,0,939) and (0,0,6) blockers, (406,3,759) Niagara; equal on both sides | Yes: host spawner_21 went (209745,54623,15552) to (218731,40639,9246) between rounds; parent apart by about (89,-330,-80) and (91,-1026,-91) | 2 | V |
| c201-r2 r2 | BP_AISpawnPoint_C_49/57/58/59 | RootComponent of BP_AISpawner_C (chain: spawn point, spawner, dropship); relative (-50,-250,-697) and (-50,250,-697), equal on both sides | Yes (through the spawner) | 2, or 1 if BP_AISpawnPoint_C is excluded | V |
| c201-r2 r3 (client t=110201285, 4) | BP_AISpawner_C_20, BP_Dropship_LandingBlocker_C_2 (relative 0,0,0), BP_AISpawnPoint_C_47/48 | Dropship SceneRoot; host dropship (230231,38410,9093), client (230140,38480,11525) | Yes: z apart 2432, still descending. The _21/_22 groups had landed and were equal | 2 | V |
| c152-r1 r2 (client t=94310094, 12) | BP_AISpawner_C_0/21/22, BP_Dropship_LandingBlocker_C_0/3/4, NiagaraActor_14, BP_AISpawnPoint_C_2/49/57/58/59 | Same structure (dropship SceneRoot, spawner RootComponent); every relative equal on both sides | Yes (the _17/_20 groups were within tolerance) | 2 | V |
| c179-r2 r1 (client t=111769472) | STAISpawner_5, STSpawnPoint_7/8 | STAISpawner_5 on dropship SceneRoot, relative (0,0,800); STSpawnPoint_7/8 on STAISpawner RootComponent, relative (-50,-/+250,-697); from the c173-c175 card140 lines (round 1 is the baseline, so c179-r2 has none) | Yes (the round-1 group of the flying dropship) | 2 | V (parent), O (c179-r2 relatives not logged) |
| Real mismatch rule 2 must keep (c143-r3, host 268020419) | STAISpawner_5 | Host relative (-30279,-33340,224), client (0,0,800) | n/a | Still reported: the relatives differ | V |

The dropship's own position difference is a replicated actor's position (card 190), not the digest's.

**BP_AISpawnPoint_C reader on the client (V): none.** A spawn point's location is read by the spawn path:
`PerformSpawn` (0x51671F0) and `QueueSpawnRequest` (0x516A850), per card 144 item 3. Both are gated on the client
at function entry (`guards.cpp:312` `detour_perform_spawn`, `:327` `detour_queue_spawn`, `is_net_client`). No mod
file reads a spawn point (`grep -n "SpawnPoint" mod/src/ubw/*.cpp`: no code hit). Card 144 owns the spawn point's
enable and collision state, and that is construction only. So BP_AISpawnPoint_C may go in the table.
`STSpawnPoint` is the same native class (`ASTSpawnPoint`) with the same gated reader. It is not in the table unless
you add it.

### b. Is an actor attached on the host always attached on the client at digest time?

- V (logs): every host card140 attach line in c201-r2 and c152-r1 has a client line with the same parent class and
  the same relative location. In c143-r3 (checkpoint restart), the client got the attach again (267990443).
- O (static): the spawner's attach does not go through `AActor::AttachToComponent`, because the c98 detour
  (`world_c98.cpp:193`) logs no spawner attach in c201-r2. Only player and weapon attaches appear there. It is the
  level-saved or component attach that the game runs on each side.
- No "attached" bit is needed. A one-sided attach still shows as a location mismatch, because one side stores the
  relative location (hundreds of uu) and the other the world location (about 200000 uu). It is not hidden, only
  named "location".
- If you want the bit anyway: it goes in the C132Entry pad byte after `ext_row` (dc:131, no size change) and in bit
  11 of the 3-hex cls field of the P entry (dc:638 writes `%03x`; dc:1414 parses 3 digits; cls < kC132DigestClasses
  = 1024 uses 10 bits). The frame length stays kC132EntryChars = 31. It is mixed into `c132_entry_hash` (dc:570).
  `hooks_internal.h` is untouched either way, because C132Entry lives in dc.

### c. BP_AISpawner_C with an earlier WorldOp row

V: the moving parent, not a stale row. The spawner's WorldOp rows are X (ext) rows with no location:
- c201-r2 client "recv 423 ... WorldOp 1cce2a94 X 50331649" (and seq 422 and 424 the same);
- c179-r2 "recv 61 ... WorldOp 108da64c X 50331649";
- ASTSpawnerBase "enabled" is +0x3FC (SetSpawnerEnabled 0x01975B10).

So no end-state row and no new card. Rule 2 covers it.

### d. Rule 3 (a pose row whose host bodies moved since its last snap)

- V: the host self-check is `c140_pose_host_check` (`pose_c132.cpp:2697`), called at the round's E frame
  (dc:717). It walks the sent rows and counts those whose bodies moved over kPoseTolerance (100 uu) since the last
  sent snap. It keeps no list, and nothing reaches the client.
- V: **rule 3 would not remove the c186-r1 mismatch.**
  - The host self-check for that round reported 0 moved: c186-r1-host.log:7836, "card140: host round 2 pose
    self-check: 15 sent corpses checked, 0 moved over 100 uu since the send" (t=98762253).
  - The host has no event on guid 0xa8 between its freeze snap 2 (98730823) and unfreeze snap 3 (98764361).
  - The client compared at 98764028, before snap 3 existed (client.log:8954, 269 uu, simulating 0).
  - On the client, the copy re-arrived (98758448: "ran show 1 ... IsDead 0 health 400.0", then "corpse arrival ...
    held") and was posed to snap 2 at 98759567 ("208 uu from the client's before, 0 after").
  - So the copy drifted 269 uu on the client in the 4.5 s after a 0-uu apply, with simulation off, while the host
    corpse sat still.
  - That is a real client-side divergence, not a host send gap. Leaving such rows out would hide it.
- Proposal: drop rule 3 from this card. The guid 0xa8 drift (re-arrived corpse drifts after its pose apply,
  simulating 0) is a new card. Rule 3 also needs `pose_c132.cpp`, which is outside this card's files.

### e. Rule 4 (Q names objective keys)

- V: the client sends "Q<round> <key>..." (`c132_send_query`, dc:1194).
- V: the host's `c132_host_query` (dc:1650-1702) walks level actors only (`kWorldLevels`, `kLevelActors`,
  `c132_actor_name`, `c132_fnv`). An objective key is never found.
- V: the objective key is FNV-1a of `"O:%s"` of `fname_text(o + kObjectiveId)` (`c132_add_objectives`, dc:463-490,
  through `g_c49_get_subsystem`, `c49_objectives`, `sparse_allocated`).
- O (run): no route in hand gives a host-only objective key. Card 199 fixed the c195-r1 one. The run cannot show
  rule 4, and it is proven by reading only.

### Rule 1 evidence

- V: the RecastNavMesh presence mismatches are play-build only.
  - Class names from the playtest (logs/playtest-menu-20261108-191343.log, FED_C1M1, round 15 on): RecastNavMesh,
    RecastNavMesh_Small, _Medium, _Large, _Gigantic, 330 lines, "presence host 1 client 0; no row sent".
  - Test runs turn client nav on (`testrun.cpp`, card 064: "card064: ... bAllowClientSideNavigation 0 -> 1" on both
    sides in c201-r2), so the last 12 test runs show 0 such lines.
  - The run can show the left-out count, but not the play-build mismatch going away.
- V: the runtime-spawned RecastNavMesh ("card098: host spawn RecastNavMesh ... placed 0") is not digested
  (`c132_actor_name` dc:329: runtime, not a child actor, not card 104).

### Proposal (after approval; dc and one scenario only)

| Function | Change | Lines |
|---|---|---|
| `c132_server_only(const char* cls)` (new) | Bounded loop over `kC132ServerOnly[]`; strcmp; true on a hit | about 10 |
| `c132_entry_loc(const void* anchor, int16_t out[3])` (new) | Root = `read_ptr(anchor, kActorRootComponent)`. If `c140_attach_parent(root)` is not null, step `read_vec3(root, 0x140)`; otherwise step `actor_location(anchor)`. Returns attached | about 15 |
| `c132_add_actor` (dc:433) | Class name first. If server-only: `++d.server_only`, return. Location from `c132_entry_loc`. `c140_attach_note` keeps the world-stepped location so its lines still log a parent that moves | stays under 60 |
| `C132Digest` (dc:149) | Add `uint32_t server_only` and reset it with `n` | 1 |
| `c132_query_objectives(world, round, keys, n)` (new) | The `c132_add_objectives` walk, "O:%s", FNV, compare with keys. Logs "card132: host round %u host-only key %08x is objective %s (class %s, state %02x)". Returns found. Called from `c132_host_query` before its total line | about 30 |
| Client round line (dc:1222) and host "sent" line (dc:708) | Append "; left out %u actors of %d server-only classes" | text only |

- Excluded-class table: RecastNavMesh, RecastNavMesh_Small, RecastNavMesh_Medium, RecastNavMesh_Large,
  RecastNavMesh_Gigantic, BP_AISpawnPoint_C (6; STSpawnPoint only if you add it).
- The entry layout, the wire and `hooks_internal.h` are unchanged. There is no heap. Every loop is bounded by the
  table size, the level and actor caps, the query keys, or the objective count. There are no new log lines per actor;
  the objective line is capped by `kC132QueryKeys`.
- Route: c201-r2 (FED_C2M1, `UBWCOOP_AUTODROPSHIP=1`). FAST=0 is not needed. c201-r2 ran FAST=1 and still had the
  flying troop dropship groups in rounds 2 and 3 (FAST skips only the FED_C1M1 player dropship ride).
- Scenario `tools/scenarios/c164-r1.txt`: the c201-r2 start, warps and Part 1, then
  `client wait log "card132: client round 3: actors " timeout=120`, shots, quit. No nuke part: it adds nothing here.

### Run c164-r1 predictions

1. Client rounds 1-3: `card132: client round N: actors X host, X client, mismatches 0 (classes -)`, with no WARN
   prefix (c201-r2 had 9 in round 2 and 4 in round 3). If the dropships land before round 2, there are still 0
   mismatches, but the rule is unproven, so that is reported as such.
2. Each round, host and client "left out N actors of 6 server-only classes", with the same N on both sides and
   N > 0. Actor counts are lower than c201-r2's by N (30299 in round 1 minus N on the host).
3. "card140: host/client attached ... parent SceneRoot of BP_ProceduralDropshipTroops_C" lines still appear for the
   spawners, blockers and Niagara of a moving dropship, with equal relatives on both sides, and there is no
   location mismatch for those keys.
4. No `WARN card132` line. Client WARN is the baseline: "attach: DisableThreadLibraryCalls failed" plus any
   card152 bone check lines (2 in c201-r2). Host WARN is 1 (the attach line).
5. Rule 4: no Q objective line is expected, because there is no host-only objective key on this route.

### Commands behind the verified lines

- `grep -n "card140: .* attached" logs/c201-r2-{host,client}.log`, and the same for c152-r1, c143-r3 and
  c173-c175: parents and relatives.
- `grep "card132: client round [0-9]*: actors" logs/c201-r2-client.log` and `grep -c WARN logs/c201-r2-*.log`.
- `grep -n "WorldOp 1cce2a94\|WorldOp 108da64c" logs/c201-r2-client.log logs/c179-r2-client.log`.
- `sed -n 2697,2731p mod/src/ubw/pose_c132.cpp`, `grep -n "0xa8" logs/c186-r1-{host,client}.log` and
  `grep -n "pose self-check" logs/c186-r1-host.log`.
- `sed -n 1650,1702p` and `sed -n 463,490p` of dc, and `sed -n 308,330p mod/src/ubw/guards.cpp`.
- `grep -h "presence host" logs/*.log | sed -E 's/.*mismatch ([A-Za-z_0-9]+) .*/\1/' | sort | uniq -c` and
  `grep -h card064 logs/c201-r2-*.log`.

### Scope questions

1. Rule 3: drop it here and open a new card for the guid 0xa8 client drift? (It is a pose_c132.cpp matter.)
2. Add STSpawnPoint to the table?

Trace complete; waiting for approval.

## Run c164-r1 (junior, 2026-11-12)

Command: `SCENARIO=tools/scenarios/c164-r1.txt DEPLOY=1 FAST=1 HOST_EXTRA_ENV="UBWCOOP_AUTODROPSHIP=1
UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C2M1?listen UBWCOOP_GOD=1 UBWCOOP_GRANT_WEAPON=NukeLauncher" tools/run_pair.sh c164-r1 900`,
DLL 5d2d0e9e6dc7d3b1. Ended by the scenario after 115 s; host rc 0, client rc 0, drive rc 0, FATAL/ERROR 0, no new
crash dumps.

Lines (client, then host):

```
card132: client round 1: actors 29967 host, 29965 client, mismatches 0 (classes -); slots differing 2 ...
card132: client round 2: actors 29970 host, 29970 client, mismatches 0 (classes -); slots differing 0 ...
card132: client round 3: actors 29974 host, 29974 client, mismatches 0 (classes -); slots differing 0 ...
card164: client round N left out 332 actors of 7 server-only classes: RecastNavMesh:1 RecastNavMesh_Small:1
  RecastNavMesh_Medium:1 RecastNavMesh_Large:1 RecastNavMesh_Gigantic:1 BP_AISpawnPoint_C:263 STSpawnPoint:64   (N = 1, 2, 3)
card164: host round N left out 332 actors of 7 server-only classes: (the same nine counts, N = 1, 2, 3)
```

- Predictions 1, 2, 4 hold. WARN: host 1 (attach), client 2 (attach, one card152 bone check); c201-r2 client had 18
  (1 attach, 15 card132, 2 card152). No WARN card132, no "(one side attached?)" line.
- Prediction 3 fails: `grep -c 'card140: .* attached '` is 0 on both sides (c201-r2: 22 per side). Rule 2 was not
  exercised; the 0 mismatches in rounds 2 and 3 are unproven for riders, as prediction 1 said to report.
- Prediction 5 (rule 4): not run. The host-only query found only LevelSequenceActor_1 (as in c201-r2), no objective.
- RecastNavMesh rows read 1 each (the card064 client navigation switch does not remove the actors).

### Cause of the missing attach lines (from the logs)

The riders in c201-r2 rounds 2 and 3 are BP_AISpawner_C_21/_22, BP_Dropship_LandingBlocker_C_3/_4, NiagaraActor_14
(and the BP_AISpawnPoint_C children) on two BP_ProceduralDropshipTroops_C troop dropships; round 3 adds
BP_AISpawner_C_20 and blocker _2. The troop flight is a fixed timer from the player dropship landing, the same in both
runs (host clock, ms):

| event | c201-r2 | c164-r1 | after landing |
|---|---|---|---|
| player dropship DescendToLandCompleted / SkipFlight | 110115348 | 112502024 | 0 |
| `dropship BP_ProceduralDropshipTroops_C_20 state 2: sent to 1 of 1 PCs` | 110152865 | 112539570 | +37.5 s |
| `... _21 state 2` | 110159106 | 112545800 | +43.8 s |
| flight start: card095 SetCollisionEnabled on blockers _3/_4 at the landing spot, then at the flight start point 30 ms later | 110168359 | 112555041 | +53.0 s |
| riders back at the landing spot (card095 host collision 0 -> 2 on blocker _4) | | 112563551 | about +61.5 s |
| digest round 1 | 110141196 | 112523248 | +25.8 s / +21.2 s |
| digest round 2 | 110169011 | 112553261 | +53.7 s / +51.2 s |
| digest round 3 | 110199022 | 112583251 | +83.7 s / +81.2 s |

The riders sit at the landing spot until +53.0 s, are teleported to the flight start, and fly back to the same
landing spot by about +61.5 s. In c201-r2 round 2 snapshotted 0.65 s into the flight (riders at (209745, 54623,
15690), away from the round 1 spot), and round 3 saw them back at the landing spot, so both rounds logged a change. In
c164-r1 round 2 snapshotted 1.8 s before the flight started and round 3 20 s after it ended, so every rider had the
same stepped world location in all three rounds and c140_attach_note logged nothing. The digest timer runs every 30 s
from its own start, not from the landing; its phase against the landing differed by 4.6 s between the runs.

Not the code change: c140_attach_note gets `world_loc`, the same stepped world location `e.loc` held before card 164,
and the server-only skip returns before the note only for the seven table classes. The c201-r2 rider lines of
BP_AISpawner_C, BP_Dropship_LandingBlocker_C and NiagaraActor would still reach it; only the BP_AISpawnPoint_C ones
are gone by design.

### Run c164-r2 proposal (run 2 of 3)

- Code: one count on the existing card164 line, each side, each round: the attached entries seen and those whose
  stepped world location changed since the last round (c140_attach_note returns whether it saw a change; two
  `uint32_t` in C132Digest, reset in c132_build). The line becomes `card164: <side> round N left out ... ; attached A,
  moved since the last round M`. One line per round, so capped by the rounds.
- Scenario: the c164-r1 route, then a digest round forced during the flight with the `objectives` command (it calls
  c132_digest_now, a "command" round):
  1. `host mark`, `host wait log "dropship BP_ProceduralDropshipTroops_C_21 state 2: sent" since-mark timeout=120`.
  2. `host mark`, `host wait log "card095: host t=\d+ SetCollisionEnabled on BP_Dropship_LandingBlocker_C_[34] "
     since-mark timeout=30` (the flight start; no blocker _3/_4 line falls between the _21 state 2 line and it).
  3. `client mark`, `host objectives`.
  4. `host wait log "card164: host round \d+ left out .*moved since the last round [1-9]" since-mark timeout=20` and
     `client wait log "card132: client round \d+: actors " since-mark timeout=30`, then the client card164 line with
     `moved since the last round [1-9]`.
  5. Shots, quit.
- Predictions: the command round has M > 0 on both sides and `mismatches 0 (classes -)` on the client; card140
  attached lines name the riders with equal relatives on both sides; no "(one side attached?)" line.
- Risk: c132_host_start refuses the command round with "WARN card132: host digest round N (timer) still sending" if
  a timer round is still being sent at that moment (a round takes about 1.2 s of the 30 s, so about 4 %). Then the run
  is lost, and the WARN line says so. The timer itself waits for Idle and does not warn.
- The `objectives` command also runs c98_dump, c132_inventory, c132_ledger_report, c104_dump and c66_dump; their WARN
  lines, if any, are listed against c201-r2 in the report.

Nit done: the c132_host_query header comment is back above c132_host_query.

### Run c164-r2 predictions (junior, 2026-11-12)

Approved by the senior as proposed. Code: `c140_attach_note` returns kC140NotAttached, kC140Attached or kC140Moved;
`c132_add_actor` counts `d.attached` and `d.attached_moved` (reset in `c132_build`); the card164 line ends with
`; attached A, moved since the last round M`. Scenario `tools/scenarios/c164-r2.txt` (regexes checked with shlex and
re.compile, the same calls `drive.py` makes).

1. The command round (host `card132: host t=... digest round N (command) sent`) has M > 0 on both sides, and the client's round line
   reads `mismatches 0 (classes -)`.
2. card140 attached lines appear on both sides for the riders (spawners _21/_22, blockers _3/_4, NiagaraActor_14),
   with equal relatives on host and client; no "(one side attached?)" line.
3. The flying troop dropship's own entry: there is none. BP_ProceduralDropshipTroops_C replicates (card098 "placed 1
   replicates 1", tally "replicated=246"; card 159), so the digest leaves it out on both sides, and no log of any run
   has a BP_ProceduralDropshipTroops_C digest entry, mismatch or host-only key. If one appears, it is card 190's
   class, not this card's; rule 2 is still proven if no rider key has a location mismatch while M > 0. For scale: the
   tolerance is kC132LocTolerance = 2 steps of kC132LocStep 50 uu (a mismatch above 100 uu); the host flight goes from
   (222771, 62118, 15131) at 112555071 to (224387, 43889, 8812) at about 112563551 in c164-r1, about 19400 uu in 8.5 s,
   about 2300 uu/s. A world-location compare during the flight would differ by speed times the host-to-client snapshot
   gap (c201-r2 riders: 300 to 1050 uu, so 0.15 to 0.45 s), well over the 100 uu tolerance.
4. WARN: host 1 (attach) plus whatever the `objectives` command adds; client the c164-r1 baseline (attach, card152 bone
   checks). No WARN card132 unless the command round is refused ("still sending"), which names a lost run: then stop.

## Run c164-r2 (junior, 2026-11-12)

DLL c96de7c5851d32d0. `SCENARIO=tools/scenarios/c164-r2.txt DEPLOY=1 FAST=1 tools/run_pair.sh c164-r2 900`: ended by
the scenario after 87 s; host rc 0, client rc 0, FATAL/ERROR 0, WARN host 1, client 3; no new crash dumps; drive rc 0.

Timing: `_21` state 2 sent at host 113056418; timer round 2 at 113064522; the flight starts (card095
SetCollisionEnabled on blockers _3/_4) at 113065682; the command round 3 is sent at 113066798, about 1.1 s into the
flight. It was not refused: no "still sending" WARN.

Command round lines:
- host: `card132: host t=113066798 digest round 3 (command) sent: 29972 entries (0 dropped), 116 classes, 7 levels, 10 frames, 10 sends to 1 clients; 5 child actors; dropped 0 classes, 0 levels, 0 unnamed`
- host: `card164: host round 3 left out 332 actors of 7 server-only classes: RecastNavMesh:1 RecastNavMesh_Small:1 RecastNavMesh_Medium:1 RecastNavMesh_Large:1 RecastNavMesh_Gigantic:1 BP_AISpawnPoint_C:263 STSpawnPoint:64; attached 249, moved since the last round 5`
- client: `card132: client t=113065866 digest round 3 begins: 3 7514 74 7 142b command`
- client: `card132: client round 3: actors 29972 host, 29972 client, mismatches 0 (classes -); slots differing 0 ...`
- client: `card164: client round 3 left out 332 actors of 7 server-only classes: (same list); attached 249, moved since the last round 5`

Rounds 1 and 2 (timer) read `attached 249, moved since the last round 0` on both sides, with mismatches 0 (round 1:
29967 host, 29965 client; round 2: 29972/29972). The only other client "mismatches" text is the playlog summaries
(mismatches 0).

card140 attached lines: 5 per side, 0 "(one side attached?)". Two pairs:
- host `card140: host attached BP_AISpawner_C_21 at (208798, 58559, 16497): parent SceneRoot of BP_ProceduralDropshipTroops_C (BP_ProceduralDropshipTroops_C) at (208798, 58559, 15697), relative (0, 0, 800)`
  client `card140: client attached BP_AISpawner_C_21 at (208834, 58421, 16463): parent ... at (208834, 58421, 15663), relative (0, 0, 800)`
- host `card140: host attached NiagaraActor_14 at (222881, 60914, 15806): parent ... at (222842, 61318, 15047), relative (406, 3, 759)`
  client `card140: client attached NiagaraActor_14 at (222892, 60783, 15794): parent ... at (222853, 61188, 15035), relative (406, 3, 759)`
The other three are equal too: blocker _3 (0, 0, 939), blocker _4 (0, 0, 6), spawner _22 (0, 0, 800). The world
gap at the snapshot is about 140 uu for _21 and 130 uu for _22, over the 100 uu tolerance, so a world-location
compare would have mismatched; the relative compare gives 0.

Predictions: 1 holds (M = 5 on both sides, 0 mismatches). 2 holds. 3 holds: no BP_ProceduralDropshipTroops_C digest
entry, mismatch or host-only key. 4 holds: host 1 WARN (attach: DisableThreadLibraryCalls), the `objectives` command
added none; client 3 (attach, 2 card152 bone checks on guid 0xf8 sample 14). Against c201-r2: host 1 vs 1; client 3
vs 18 (c201-r2 had 15 WARN card132, none here). No WARN card132 on either side.

## Report (junior, 2026-11-12)

Files changed: `mod/src/ubw/digest_c132.cpp`, `tools/scenarios/c164-r1.txt`, `tools/scenarios/c164-r2.txt`, this card.

- Rule 1 (gate server-only classes): each round leaves out 332 actors of 7 server-only classes on both sides with the
  same counts (RecastNavMesh rows 1 each, BP_AISpawnPoint_C 263, STSpawnPoint 64); every round 0 mismatches.
- Rule 2 (attached actors compared by relative): c164-r1 had no round during the troop flight (rounds at +21.2,
  +51.2, +81.2 s against the flight at +53.0 to +61.5 s). c164-r2 forced a command round 1.1 s into the flight: 5
  riders moved, relatives equal on both sides, 0 mismatches, where world locations differed by 130-140 uu.
- Rule 4 was not run.
- The flying dropship itself has no digest entry (it replicates); no card 190 entry appeared.
- DLL hashes: c164-r1 5d2d0e9e6dc7d3b1, c164-r2 c96de7c5851d32d0.
- Runs used: 2 of 3.

Report complete.

## Senior review (2026-11-12)

Accepted on c164-r2, DLL `c96de7c5851d32d0`. Checked myself: the diff is `digest_c132.cpp` only (plus two scenarios),
no function over 60 lines; rounds 1 to 3 read 0 mismatches on the client; both sides leave out the same 332 actors
(5 nav meshes, 263 BP_AISpawnPoint_C, 64 STSpawnPoint); the command round 3 fell 1.1 s into the troop flight with 5
attached entries moved on both sides, world locations 130 to 140 uu apart and equal relatives; WARN is the baseline
(host 1; client the attach line and 2 card 191 bone checks). c164-r1 proved nothing for rule 2 (the 30 s timer missed
the 8.5 s flight); the "moved since the last round" count now makes that visible.

Not proven: rule 4 (objective names on a host-only key) and the "(one side attached?)" hint, read only; the play
build's RecastNavMesh mismatch (test runs enable client navigation, so both sides hold the nav meshes here). Rule 3
left this card: card 203.
