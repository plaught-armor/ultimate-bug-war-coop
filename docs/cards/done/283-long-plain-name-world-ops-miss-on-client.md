# 283 — World ops and collision rows for an actor whose own name is over 64 characters miss on the client

Found by build 4 run `c230-c3m1-b4e` (DLL `5e2ce00acad8efa3`, FED_C3M1), junior step 1 report 2026-11-13.

## Evidence (logs/c230-c3m1-b4e-{host,client}.log)

Actor `BP_Bench_Metal_Destructible_03_GEN_VARIABLE_BP_Bench_Metal_Destructible_01_C_CAT_53` (82 characters, class
`BP_Bench_Metal_Blue_Destructible_01_C`, at (-16036, -48490, 750), level FED_C3M1_Art). It is a child actor; its
own object name is the same text on both machines (the digest pairs it by that name).

- Host 242721535: `card098: host ... Health 25.0 -> 0.0: kill relayed`, and in the same ms
  `WARN card095: host collision write on ...: name not sendable`.
- Client 242721556: `WARN card098: client ... world K <name> -16036 -48490 750 0: no placed copy with a health
  component (no actor), skipped`; 242721560: `world H ... 1: no placed copy matches, skipped`.
- Digest: `dead host 1 client 0`, `hidden host 1 client 0`, `collision host 0 client 1` in 23 of 29 rounds, this
  one actor only.

A player sees: a bench the host destroyed stays whole, visible and solid for the client.

## Cause (static, senior, 2026-11-13)

The world-op sender has room for the name and sends it; the receiver's lookup cannot match it.

1. Send side is fine for world ops. `C98World::name` is `kAssetPathChars` (192, `world_c98.cpp:251`), and
   `c98_host_kill` (`world_c98.cpp:499`) and the other `c98_host_*` writers fill it with
   `c50_name(actor, row.name, sizeof(row.name))`. The 82-character name went out whole (client WARN prints it).
2. `c50_name` (`hooks_ubw.cpp:1325`) returns -1 when the name does not fit `cap - 1`.
3. Receive side: every world-op receiver resolves with `c95_find_nearest(world, r.name, r.at)`
   (`door_c95.cpp:683`). Its scan compares each placed actor through `char got[kChanNameMax + 1]`
   (`door_c95.cpp:700`); for an actor whose name is 65 characters or more `c50_name` fails, the actor is skipped,
   so the name never matches. The class fallback `c95_name_fallback` then finds no other same-class actor in
   range and returns null. Hence "no actor".
4. Collision rows fail one step earlier, on the host: `c95_host_collision` (`door_c95.cpp:850`) reads the owner
   name into `char name[kChanNameMax + 1]` and gives up with `name not sendable`. The row struct
   `C95Collision::name` (`door_c95.cpp:68`), the frame text `char text[3 * kChanNameMax]` (`:867`), the receive
   parse `%64s` (`:877`) and the load-time matcher `c95_match_rows` (`got[kChanNameMax + 1]`, `:764`) all hold 64.

Card 230 gave the combined "parent/component" text its own bound; it did not touch plain names over 64. This is
the same class on the plain-name path.

## Fix spec (one class fix, one file plus one constant)

Add in `hooks_internal.h` next to `kC95TargetMax`:
`inline constexpr int32_t kC95ActorNameMax = kAssetPathChars - 1;` (191; the longest plain actor name a world row
can carry). `kChanNameMax` stays 64 and named frames are not touched.

| Place (`door_c95.cpp`) | Now | Change |
|---|---|---|
| `:700` `got` in `c95_find_nearest` | `kChanNameMax + 1` | `kC95ActorNameMax + 1` |
| `:621` `got` in `c95_name_fallback` (log only) | `kChanNameMax + 1` | `kC95ActorNameMax + 1` |
| `:764` `got` in `c95_match_rows` | `kChanNameMax + 1` | `kC95ActorNameMax + 1` |
| `:68` `C95Collision::name` | `kChanNameMax + 1` | `kC95ActorNameMax + 1` |
| `:850` `name` in `c95_host_collision` | `kChanNameMax + 1` | `kC95ActorNameMax + 1` |
| `:867` frame `text` | `3 * kChanNameMax` | `kC95ActorNameMax + kChanNameMax + 64` |
| `:877` receive parse | `%64s` for the name | `%191s`, with a `static_assert(kC95ActorNameMax == 191)` beside it |

Before coding, check each of these and report anything that differs from the table:

- Every other reader or writer of `C95Collision::name` in `door_c95.cpp` (journal replay text buffers such as
  `:480` `char text[3 * kChanNameMax]`, `c95_collision_row`, `c95_collision_text`): widen any buffer that holds
  a whole row text to the same size as `:867`. The frame is a text frame (`kChanTextMax` 1400), so no frame limit
  is in the way.
- The collision journal's size in memory grows by 127 bytes per row; state the row count and the new total in
  the report (card 219 raises the cap to 2048: about 260 kB more, acceptable).
- `c95_child_actor` (`:733`) keeps `parent_name[kChanNameMax + 1]`: a parent over 64 is refused by
  `c95_wire_target` on the sending side already. Do not change it.
- Sibling sweep, report only (no change unless the same fault is shown): the other `name not sendable` sites
  `door_c95.cpp:291`, `:342` (door rows, `C95Row::name`), `door_c161.cpp:143`, `world_c98.cpp:956` (uses
  `row.name` of 192, fine), and the receivers `turret_sync.cpp:604`, `ext_c132.cpp:286`: for each, state the
  name buffer on the send side and whether a 65+ character door, turret or ext actor exists in any run log
  (`grep 'name not sendable' logs/*host.log | sort | uniq -c`).

Do not add a hash or a second naming form; the names are identical on both machines.

## Prediction (FED_C3M1 run, batched with the next build)

Destroy a pillar child bench on the host (the b4e scenario already does). Then:

- Host: `kill relayed` row for the 82-character name, and a `card095: host ... collision <name> ...: sent to 1 of
  1 clients` row instead of `name not sendable`.
- Client: no `WARN card098 ... no placed copy` for that name; the kill and hidden rows are counted as applied.
- Digest: 0 mismatches for that actor (dead 1/1, hidden 1/1, collision 0/0).
- Whole run: 0 `name not sendable` rows.
- Receiver-side shot of the bench place on the client after the kill: no bench.

## Run c283-b5 predictions

Scenario `tools/scenarios/c283-bench.txt` (FED_C3M1, build 5 faaa085d7714170b). Both players stand 400 and 500 uu
east of the bench at (-16036, -48490); the host fires an 800 ms burst at it.

- P1: host `card098: ... BP_Bench_Metal_Destructible_03_GEN_VARIABLE_BP_Bench_Metal_Destructible_01_C_CAT_53 Health X
  -> 0.0: kill relayed` within 10 s of the press.
- P2: host `card095: host ... collision <that name> ...: sent to 1 of 1 clients`. A miss here is reported, not voided
  (the scenario waits on it under `try`).
- P3: 0 `name not sendable` rows in the host log for the whole run.
- P4: client: no `WARN card098 ... no placed copy` for that name.
- P5: digest round after the kill: 0 mismatches on the client row.
- P6: client shot `c283-client-bench-after` shows no bench where `c283-client-bench-before` showed one.
- P7: WARN classes in both logs against the c230-c3m1-b4e baseline: no new class.

## Priority

Not a play blocker (cosmetic and a small solid leftover on one prop class). Build 5.

## Run b (`c283-b5`, DLL `faaa085d7714170b`): kill reaches the client; client screen proof owed

Senior check of the logs and shots. Host line 5801 `Health 35.0 -> -5.0: kill relayed` at t=247860430; client line 5543
`world K ... IsDead now 1` at t=247860449 (19 ms) and line 5546 `world H ... hidden now 1`. Collision row sent to 1 of
1 clients. `name not sendable` 0 on both sides, 0 client card098 WARN, client digest round 3: 24874 actors each side,
mismatches 0. Host shots viewed: `bench-aim` shows the middle bench, `bench-after` shows it gone.

Not shown: the client screen. All three client shots show the mission brief ("PRESS ANY KEY TO CONTINUE"); the scenario
never sent `client ui continue`. Scenario faults to fix (junior): add `client ui continue`; line 35 regex to
`-> -?[0-9.]+: kill relayed`; add `UBWCOOP_AUTODROPSHIP` or drop FAST on C3M1 (`WARN config`).

Status: fix accepted on the log proof. The client before/after shot rides the next C3M1 run; no dedicated rerun.
WARN diff against `c230-c3m1-b4e`: host `config` 1 (scenario), client `card152` 12 (ragdoll bone-check diagnostic, seen
in earlier runs); no new mod fault class.

## Closed (2026-11-17 08:50)

Owed client shots, b177, run c283b177a (FED_C3M1, `tools/scenarios/c283-bench-b177.txt`): host `kill relayed` for
the 82-character bench `..._CAT_53`, K, collision and H rows `sent to 1 of 1 clients`; client `world K ... IsDead now
1` 9 ms later and `world H ... hidden now 1`; 0 `name not sendable`; digest round 1 mismatches 0. Client shots
`c283-client-bench-before` (bench stands) and `c283-client-bench-after` (bench gone, debris on the ground; viewed by
junior and senior).
