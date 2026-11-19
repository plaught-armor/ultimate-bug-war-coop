# 409 A pooled world Niagara component is sent as a bare Activate, without its spawn row or parameters

Found reading card 325 target misses (run c325b101a, b101 `7a7505c44a494c79`).

## Evidence

- Host event log: `NiagaraActivate <asset> ... attach NiagaraComponent/None owner STWorldSettings ... key guid`
  with no ` of <id>` and no spawn row for the asset in the run: `NS_FX_FlakbugProjectile_AllInOne` 43 rows (bursts,
  about every 200 ms), `NS_FX_Frontend_PlasmaSpawnSmoke` 10, `NS_FX_PlasmaBug_MuzzleFlash` 2.
- Client: every row `spawned at world (component miss)`; no `client fx own` row for these assets, so the row is
  the only delivery. It is counted as a target miss on each one.
- The component is made by a path the spawn hooks do not see (owner = world settings is what a pooled at-location
  spawn gives). No Niagara user parameter is relayed, so a system that takes its direction or target from a
  parameter set after the spawn draws with defaults on the client.

## Step 1, diagnostic (next lane 2 build)

On the host, for a `NiagaraActivate` whose component is not in the live set and whose owner is the world settings:
log once per asset the 4 return addresses above the detour and the component's user parameter count. That names the
spawn path and says whether parameters matter.

## Step 2 (after the rows are read)

Hook the named spawn path as a spawn point (so the row is a spawn with its transform, not a miss), and relay the
user parameters only if step 1 shows any are set. The client row then reads `played`.

## Done when

0 target misses with owner class `STWorldSettings` in a fast chain run, and the flak round effect matches on a
receiver-side screenshot.

## Step 1 result (b102, run `c410b102a`, 2026-11-16)

The `card409: host pooled Activate` line (`fx_c136.cpp`, `c409_activate_line`) logged 16 rows. Two caller shapes:

- `0x48408DB < 0x0 < 0x51DAFA3 < ...` (13 rows, many assets: MoritaHit, BloodHit, DropPod, WalkGround): the Activate
  that runs inside the spawn call itself, before the spawn detour has the component in the live set. These already
  have a spawn row. The diagnostic must skip them (a "spawn in progress" depth counter) before step 2.
- `0x4849521 < 0x15576B4 < 0x154FAE8 < 0x1545188 < 0x1557ADB` (3 rows, `NS_FX_Frontend_PlasmaSpawnSmoke`): the
  script VM calling `Activate` on a component whose outer is `STWorldSettings`. This is the pooled path: a blueprint
  activates a component it took from the pool, no spawn call. Name the blueprint function next (object and function
  at `0x1545188`'s frame, or the ProcessEvent in progress).

The 16 row cap filled before any Flakbug AllInOne activation, and assets reload per map so one asset repeats. Also:
run `c325b101a` showed no `SetVariable` call for the flak or plasma assets.

## Step 1 final result 2026-11-16 (b113 rows, bytecode, disassembly): not a pool, an inlined script thunk

The b113 rows name the script callers: `NS_FX_FlakbugProjectile_AllInOne` in
`BP_SatchelBuilding_Flakbug_C.OnNotifyBegin_60505446...` (the firing montage notify, which runs `Fire Projectile`),
`NS_FX_Frontend_PlasmaSpawnSmoke` in `BP_Frontend_PlasmaProjectileSpawner_C.ReceiveBeginPlay`.

- `Fire Projectile` (ubergraph offset 8898) calls `NiagaraFunctionLibrary:SpawnSystemAtLocation(self, system, muzzle
  or socket location, rotation with a random -2..2 degree pitch and yaw, scale 1, bAutoDestroy true, bAutoActivate
  true, PoolingMethod None, bPreCullCheck true)`. No pool, and no `SetVariable` after it: no parameter to relay.
- The script thunk `execSpawnSystemAtLocation` is at `exe+0x4849050` (found through the native registration table:
  name string pointer followed by the function pointer). It reads 9 parameters and has the spawn body inlined: it
  never calls `UNiagaraFunctionLibrary::SpawnSystemAtLocation` (`exe+0x4840630`, the detoured entry), and its
  `call [rax+0x2b8]` at `exe+0x484951B` (return address `0x4849521`, the first caller in every row) is the
  component's virtual `Activate`. So every Blueprint call of this node passes the spawn capture point unseen; only
  the Activate detour sees it. A component made without an owner has the world settings as its outer, which is
  what looked like a pool.
- Sibling thunks checked the same way: `execSpawnSystemAttached` (`0x4849650`) calls `0x4840B10`, the two
  `WithParams` thunks call `0x4840900` and `0x4840EC0`, `execSpawnSoundAttached` calls `0x3C70060`,
  `STCommonUtilityLibrary` sound thunks call `0x3C62EB0` and `0x3C70060`, the ST decal thunks call `0x5138C40` and
  `0x5138D60`, `execSTSpawnSystemAtLocation` calls `0x51DAF10`: all captured. Inlined and so not captured from
  Blueprint: `UGameplayStatics` `execPlaySoundAtLocation` (`0x3C83870`), `execSpawnSoundAtLocation` (`0x3C8A340`),
  `execSpawnEmitterAtLocation` (`0x3C88AC0`), `execSpawnEmitterAttached` (`0x3C88E60`). Those are card 418.
- Blueprint users, import sweep of 1679 Blueprint packages (`BP_`, `GA_`, `GC_`, `GE_`, `ABP_`, `AN_`, `ANS_`,
  `BT*_`, `WBP_`, `W_`, `B_`; first 48 kB of each, 1 unreadable): `BP_SatchelBuilding_Flakbug`,
  `BP_SatchelBuilding_Plasmabug`, `BP_MissleSilo`, `BP_Frontend_PlasmaProjectileSpawner`,
  `BP_KillableBuilding_CommandPost` (2 calls), `BP_KillableBuilding_ElectricPylon`, `BP_AIGrenade_Frag_VivaGranata`,
  `BP_ResupplyRack_Level1`. All pass PoolingMethod None except the resupply rack, which passes AutoRelease (1): that
  one does take a pooled component, so the same component object can come back for a later spawn.

## Step 2 design

One detour on `exe+0x4849050` (`void exec(UObject* context, FFrame* stack, void* result)`), both sides. It must
always call the original: the thunk reads its parameters from the script frame, so skipping it corrupts the frame.

- Host: the detour raises a per-thread "script at-location spawn" mark around the original and notes the frame's
  object (`FFrame::Object`, the Blueprint instance) as the owner context. In the Activate capture, an outermost
  Activate of a component that is not in the live set while the mark is up becomes a `SpawnSystemAtLocation` event
  instead of a `NiagaraActivate` event: asset from the component, world location, rotation and scale read from the
  component (the inlined body sets them before it activates), owner from the noted object, key chosen as for any
  other spawn. The component goes into the live set with that event id and the seed line is logged, as after a
  detoured spawn. The card 409 diagnostic line is removed.
  A component that is already in the live set (a pooled one, the resupply rack) is treated the same while the
  mark is up: a new spawn event that replaces its live entry, never a restart ` of <old id>`.
- Client: the same capture point makes the own-or-host decision as point `SpawnSystemAtLocation`. When the effect
  is the host's to send, the Activate is skipped as today and, after the original returns, the component in
  `result` is destroyed (it is registered with auto destroy and never activated, so it would otherwise stay until
  the map ends). The host's row then plays through the normal `SpawnSystemAtLocation` path.
- A thunk call that returns a component with no Activate seen (bAutoActivate false) is counted and logged once per
  asset as a WARN; no guess is sent.

Predictions (fast chain run, flak and plasma buildings firing):

1. Host: one `SpawnSystemAtLocation ... NS_FX_FlakbugProjectile_AllInOne` event per flak round, owner the building,
   and 0 `NiagaraActivate` events whose owner is `STWorldSettings` without ` of <id>`.
2. Client: 0 `card325 ... target miss ... owner class STWorldSettings`; the rows read `played`; no own row for
   the asset and no second copy.
3. Client: live Niagara component count does not grow with flak rounds (the skipped components are destroyed).
4. Bytes: asset 75 was 28 rows, 406 B per map (14.5 B per row) as a bare Activate; the spawn row carries a
   transform, so expect about 35 to 45 B per row. Report the new figure.
5. Receiver-side screenshot of a flak round in flight matches the host's direction.

## Step 2 result (b116 `eaab0a957f07fec3`, run `c409b116a`, 2026-11-16)

Fast chain FED_C3M1 to FED_C5M2: both rc 0, FATAL/ERROR 0, every mismatch count 0, WARN diff against `c408b115a` has
no new kind (only per-instance counts of card132, card140 and card159 rows).

- `NS_FX_Frontend_PlasmaSpawnSmoke`: card325 target misses 20 to 0. Per frontend visit the client logs
  `card409: client map asset (map end) NS_FX_Frontend_PlasmaSpawnSmoke: 2 converted, 2 host-sent discarded, 0 with no
  Activate`. Prediction 2 holds for this asset.
- 0 host `NiagaraActivate` events with owner `STWorldSettings` (prediction 1, second half). `NS_FX_PlasmaBug_MuzzleFlash`
  2 converted on both sides.
- `NS_FX_FlakbugProjectile_AllInOne`: host 15 of 60 converted, client 12 of 60, 0 discarded. The host logs `card408:
  host fx local SpawnSystemAtLocation NS_FX_FlakbugProjectile_AllInOne by ProcessEvent`: card 408 keeps the flak
  rounds local on each side (both sides simulate the building), so no event is sent. This replaces prediction 1's
  "one event per flak round"; the round matches because the building's aim matches (card 408), not by a row.
- `NS_FX_TankerFire`: 0 rows in the run (no tanker fired). Not proven; it was 8 misses in `c325b101a`.
- `NS_FX_AcidSpill`: still 4 target misses on FED_C4M1, a different class. The event is a `NiagaraActivate` of the
  component `Niagara` owned by `BP_HazardousZone_Acid_C`, key world; the zone actor exists on the host only (the
  client has only the class default), so the client row is `spawned at world (component miss)`. The effect is
  delivered, but it is counted as a miss and may not follow the zone's movement (`TickMoveToNewPostion`). Card
  candidate: host-only hazard zone components.
- Quirk: the client's first card409 line reads `host/standalone (map arm)` (printed before the connect sets the side).
- Plasma screenshots `host-c409b116a-c408-c5m1-plasma.png` and `client-...`: both show `Plasmabug_C_2` with its plasma
  effect in frame.

Open for done-when: TankerFire and the flak receiver-side screenshot (the flak path is now card 408's).
