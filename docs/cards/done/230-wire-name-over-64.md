# 230 — Child-actor wire names longer than 64 characters

Found by soak run c127-c3m1 (DLL `2928654b3a96090d`): 44 lines `WARN card133: wire name of
BP_Bench_Metal_Destructible_0N_GEN_VARIABLE_... does not fit (70 > 64)` (32 at 70, 12 at 68) on FED_C3M1.

Cause: `c95_wire_target` (`mod/src/ubw/door_c95.cpp:593`) builds "parent/component" into a `kChanNameMax` (64)
buffer and returns 0 when it is longer. Callers then drop the target: placed effect keys (`fx_c136_net.cpp:580`),
the client hit relay (`attributes.cpp:1142`), hold (`hold.cpp:168`), decals (`decal_c132.cpp:325`). A player would
see: a client's hit on such a bench or bin may not reach the host; its effects and decals are not keyed.

`kChanNameMax` has 81 uses in 19 files, so do not raise it. Trace first: the buffers of the four callers and of the
resolver (`c95_find_nearest` and its parser), then give the combined "parent/component" text its own, longer bound.

Done when: one run on FED_C3M1 shows no `does not fit` line and a client hit on `BP_Bench_Metal_Destructible_01`
reaches the host.

## Step 1 answers (static, senior, 2026-11-13)

- The 12 failing names are child actors of benches and bins: parent name plus `/` plus the child actor component
  name comes to 68 or 70 chars. Each part alone fits 64.
- Only the combined text is too short. `c95_child_actor` (`door_c95.cpp:665`) already accepts a parent of up to 64
  and `c95_component` a component of up to 255.
- Wire paths that carry a `c95_wire_target` text are all text frames (`kChanTextMax` 1400), never named frames, so
  no frame limit is in the way. The collision relay (`door_c95.cpp:783`) and the `world_c98.cpp` and
  `ext_c132.cpp` receivers name the owner or use wider buffers; they are not touched.
- A hash form for long names was weighed and dropped: the mod has no component enumeration helper, so it would
  add more code than the bound.

## Fix spec (step 2)

One constant, `kC95TargetMax = 2 * kChanNameMax + 1` (129), in `hooks_internal.h` next to the `c95_wire_target`
declaration. Every place that holds or parses a wire target uses it; `kChanNameMax` itself stays 64.

| Place | Now | Change |
|---|---|---|
| `door_c95.cpp:600` `full` | 65 | `kC95TargetMax + 1`; fail only when a part is over 64 |
| `hold.cpp:263`, `:268` | `name[65]`, `%64s` | `kC95TargetMax + 1`, `%129s` |
| `hooks_internal.h:4065` `use_name`, `interact.cpp:103` | 65, `%64s` | same |
| `decal_c132.cpp:618`, `:621` | `name[65]`, `%64s` | same |
| `decal_c132.cpp:117`, `:1019`, `:1020` | `where[64 + 48]`, `%111[^;]` | `kC95TargetMax + 48`, `%176[^;]`; check the `T` row still fits its frame |
| `fx_c136_net.cpp:75` `kKeyChars` | 64 + 80 | `kC95TargetMax + 80`; check the receiver's `placed` key buffer and its parser widths |

The format widths are written by hand next to each buffer: add one `static_assert` per site tying the width to
the buffer size. Do not touch `c95_find_nearest`, `c95_child_actor` or any named-frame code.

Prediction (FED_C3M1 run, batched with 261): 0 `does not fit` rows; a client hit on
`BP_Bench_Metal_Destructible_01` logs `card133: child actor <parent>/<component> -> parent ..., child ...` on the
host with no `WARN`, followed by the host's damage row for that bench.

## Run D (c230-c3m1-d2, build 3), senior verdict 2026-11-13

- Held: 0 `does not fit` rows on both sides (c242 had 44). All FED_C3M1 objectives completed (Beach, Monorail,
  Resort, Satcom), travel to FED_C3M2 done; digest mismatches in 5 rounds, class Pose only.
- Not proven: the client hit on a long-name child. The scenario aimed at the standalone
  `BP_Bench_Metal_Destructible_01_C_0` (aim trace hit `StaticMeshActor_474`); the long names are children of
  `BP_MonorailStationPillar_C_N`. Host `card133: child actor` rows: 0.
- Next: no run of its own. The build 4 proof run on FED_C3M1 aims at the `BP_Bench_Metal_Destructible_01` child
  of `BP_MonorailStationPillar_C_1`; the prediction above stands. Gib rest WARN rows belong to card 273.

## Shots of run c230-c3m1-d2 viewed by senior (2026-11-13)

- `host ... c242-bpsc-0-stand`: console centred, INTERACT prompt shown. Aim good.
- `client ... c230-bench-aim`: a blue shutter wall fills the view, no bench. Confirms the wrong bench aim point
  (replaced by the pillar child aim for the build 4 run).
- `host ... c242-satcom-so2-check`: sky and tent roofs in view while a warrior bug hits the host. The `-check` shots
  do not look at the objective target and the bugs there were not cleared first. Scenario fix for the build 4
  FED_C3M1 run: each `-check` shot looks at its target with the crosshair trace row, after a `clear` (scenario rules
  in `docs/STATE.md`, cards 264 and 265).

## Correction to the prediction (senior, static, 2026-11-13)

The "client hit logs `card133: child actor` on the host" prediction is wrong. `attributes.cpp:1142` is the client
Use-name relay (`c50_send_use_name`), gated by `c54_relay_row`; a bench is not a relay class, so a client weapon
hit on a bench never sends a wire target. The wire target is used host to client (placed effect keys, decals,
hold). Proof wanted instead: a client `card133: child actor <parent>/<component>` resolve row for a target over
64 characters, no WARN, 0 `does not fit`. Verdict of the build 4 FED_C3M1 run follows below.

## Build 4 run c230-c3m1-b4e (DLL `5e2ce00acad8efa3`), senior verdict 2026-11-13: held, closed

- Client: 6 `card133: child actor` resolve rows, 5 of them for a target over 64 characters (70:
  `BP_MonorailStation_FlowerpotWall_01_C_0/BP_Bench_Metal_Destructible_03` four times, `..._05` once), each
  resolved to parent, component and child (for example at 242705171). 0 `WARN card133`, 0 `does not fit` on
  both sides.
- The run found a neighbouring fault on a path this card did not touch: a plain actor name over 64 characters
  (82) is sent by world ops but never matched on the client. That is card 283.
