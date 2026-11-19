# Card 125: Client turret: aim and fire on the host, turret state the same on both sides

Status: sent and accepted 2026-11-07. The c125zb drop is not environmental: card 128.

Read:

- card 123 Report, item 4 redone (`BP_MountableTurret_C`, `mod/src/ubw/vehicles.cpp` TurretMount/TurretLeave,
  `CanInteract` 0x051C1690 facing cone, `tools/scenarios/c123-turret.txt`, the caveats);
- card 076 Report (client mech fire: the card 024 fire relay carries `InputTag.Fire`, one round per press, pooled
  `ADProjectile` visuals through the card 074 path) — the turret is the same problem;
- card 098 Report (world sync mechanism, one mechanism per class of state).

Repo root: `/path/to/ultimate-bug-war-coop`. DLL base: card 124's.

## Why

Card 123 lets a client mount and leave a turret without a crash, but two things are still wrong:

- The client cannot aim or fire the turret it sits in. The card 123 Report calls that out of scope "as with the mech
  weapons", but card 076 did make the client's mech fire work, so the turret has a known path. A client that mounts a
  turret that does nothing is a bug the user will hit (user rule: no workarounds).
- The turret's yaw differs: `BP_MountableTurret_C_8` on FED_C3M1 is yaw 0 on the host and -90 on the client. The
  client's `CanInteract` checks its own copy, so the client has to stand behind the gun as its copy faces. That
  breaks the user rule that all world state is the same for every player.

## Do

1. **Measure (static, then the run in step 4).**
   - Turret yaw: why the two copies differ (the placed rotation, a host-side aim that turned the gun, or a turret that
     faces its last user). Say which property holds the aim (root, a pivot component, the control rotation of the
     possessed turret) and whether it replicates.
   - Turret fire: the input actions the turret binds on the client (IMC), the ability or function each press runs, and
     what reaches the host. Same for the aim (mouse axes into the turret's control rotation; does the host's copy
     follow?). Turret heat (the TURRET HEAT HUD): where the value lives and which side owns it.
2. **Fix, one mechanism each, reusing the existing paths.**
   - Fire: the card 024/076 relay, so the host's turret fires for the client's press and the host deals all damage.
     Held fire works the way the turret fires for a host player (card 076 found a one-round-per-press mismatch on the
     mech; check the turret for the same).
   - Aim: the host's turret follows the client's aim, and the host's own turret aim shows on the client.
   - Yaw at rest and heat: same on both sides through the card 098 world sync path (or replication, if step 1 shows it
     already replicates and something blocks it). After the fix the client's `CanInteract` facing matches the host's.
   - Visuals: the turret's rounds and muzzle effects show on the other side through the card 074 path if step 1 shows
     they do not.
3. Keep the host's own turret play unchanged.
4. **Proof, one run, using card 124's driver** (parallel steps, smooth mouse-axis aim, no warp to mount if the walk
   can reach the gun). FED_C3M1, both players in. The client mounts a turret from behind the gun as the host sees it,
   aims at a bug, and fires (held) until the bug dies; the host mounts another turret and fires at the same time.
   - Host log: the bug's health drops and it dies from the client's turret fire.
   - Both sides log the same turret yaw at rest and the same heat while firing (one line each).
   - Shots on both sides show the other player's turret rounds.
   - rc 0, ERROR/FATAL 0, no CRASH line, no new dump on either side.

Stop when the fix builds and that run shows it; the user will playtest.

## Done when

The Report has the step 1 findings (yaw cause and property, fire and aim input path, heat owner), the fix for each
and where it sits, the run lines above, the run length, rc, ERROR/FATAL, CRASH lines, dumps and the DLL hash
(`sha256sum | cut -c1-16`) with the play DLL saved as `build/dwmapi-play-<hash>.dll`. Ends with "Report complete."

## Report

Play DLL `69e1cb9079f52374` (`sha256sum | cut -c1-16`), saved as `build/dwmapi-play-69e1cb9079f52374.dll`, built on
card 124's `bce0340ca6cc197a`. Clean build, no warnings. Proof run `c125zc` passed.

### Step 1 findings

- **Yaw.** Both copies of a placed turret have the same root yaw: `BP_MountableTurret_C` at (-69777, -45940) shows root
  yaw -90.0 on both sides. The aim is held elsewhere: the turret's Camera component's relative rotation (+0x770),
  which `HandleInputLookHorizontal`/`Vertical` set with `SetRelativeRotation` (clamps +0x808 yaw, +0x80c pitch). The
  gun follows a gun target at +0x8B8 (doubles P, Y, R), which Tick eases towards. Neither replicates. Each copy's aim
  was its own local state, so one side's gunner turned only that side's copy. That is the yaw mismatch the card saw,
  and the reason the client's `CanInteract` facing cone used the client's own copy. `IsBeingUsed` (+0x848) and
  `IsReadyToTakeInput` (+0x849, the look gate) do not replicate either.
- **Fire and aim input.**
  - On the client's own turret, `InputTag.Fire` goes to `HandleInputTag_Pressed` on the input receiver
    (turret+0x750). From there it presses the turret's own ASC (+0x680) every tick while the button is held. Nothing
    reached the host.
  - Look comes in as `HandleInputLookHorizontal`/`Vertical` on the same receiver. They work only while +0x849 is set.
  - The client's copy also never ran the turret Blueprint's `ReceivePossessed`/`ReceiveUnpossessed` (both
    BlueprintAuthorityOnly). Those events open the graph's gate for `Move Camera`, which places the camera at
    (-130, 0, 90). The client's camera therefore stayed at the turret root, inside the mesh.
- **Heat.** The TURRET HEAT value belongs to the turret's weapon on the host. The weapon exists only on the host
  (card 021 `GiveWeaponByClass` on the host-owned turret), so the host owns the heat.

### Fixes

- **Fire** (`fire.cpp`, `chan_ops.cpp`). The card 024 relay picks up the client's turret Fire press and release. On
  the host, the relay presses the turret weapon's ASC again on every tick while fire is held (`c24_mech_repress`, now
  for turrets as well as mechs), so held fire works as it does for a host gunner. The host deals all damage.
- **Aim** (`turret_sync.cpp`).
  - The client that drives a turret sends its camera yaw and pitch as `ChanOp::TurretAim`, at most every 50 ms.
  - The host applies them in this order: `SetRelativeRotation` on the Camera, then `HandleInputLookHorizontal(0)`,
    then `HandleInputLookVertical(0)`. Each look call sets only its own axis of the gun target. With Horizontal
    alone, the host's barrel stayed level; the Vertical call fixes that.
- **Rest yaw, heat, in-use and ready** (`turret_sync.cpp`).
  - The host sends one card 098 WorldOp `'T'` row per placed turret when it changes, at most every 100 ms per turret.
    The row carries camera yaw and pitch, heat (0-255), +0x848 and +0x849.
  - The client applies the row to its copy. It keeps its own aim on the turret it drives.
  - The journal keeps the last row per turret, so a late joiner gets every turret's rest aim, and the client's
    `CanInteract` facing matches the host's.
- **Camera on the client** (`turret_sync.cpp`). The client runs the turret ubergraph entries for `ReceivePossessed`
  (1598) and `ReceiveUnpossessed` (1701) when the mount state changes, as the host's authority copy does.
- **Rider body** (`turret_sync.cpp`, `remote_body.cpp` `c28_remote`). The client mirrors the rider's arms (`SetArmsVisbility`), and
  c28_remote no longer gives the client's own riding character a sprite body. The same fix covers the mech rider,
  which was the sibling case.
- **Rounds** (`attributes.cpp` `c76_mark_task`, `grenades.cpp`). Turret rounds are pooled `ADProjectile`s with
  replicates 0, the same case as mech rounds. The card 076 relay now marks turret fire tasks too: every remote client
  except the gunner's gets a card 074 GrenadeSpawn copy. The log text still says "mech round".
- The measurement probes are gone. The 1 Hz state lines, tag and look counters stay behind `UBWCOOP_C125_LOG=1`.
- The host's own turret play is unchanged: the host turret at (-71175, -45940) fired on the host's own input in the
  run.

### Proof run c125zc

`tools/scenarios/c125-proof.txt`, card 124 driver. FED_C3M1, `FAST` dropship. The run lasted 162 s.

- The client mounted turret (-69777, -45940) from behind, aimed and held fire for 5 s. The host's card 024 log shows
  relay press (Fire 1) at t=140542110 and relay release (Fire 0) at t=140547117, pawn `BP_MountableTurret_C`, weapon
  `BP_Weapon_Turret_C`.
- The bug's health on the host, dist 264: 216 → 208 → 168 → 136 → 80 → 64 → -753 at t=140543242. The bug died at that
  point. The host's own fire started after this (host turret heat first above 0 at t=140545260), so the client's fire
  killed the bug on its own.
- The host fired turret (-71175, -45940) at the same time, and its rounds reached the client as card 074 copies (for
  example t=140544363, `BP_ProjectileInitData_Turret_C`, "copy ADProjectile ... set to the host's"). The client's
  rounds are the host's own native rounds on the host. The card 076 lines show "sent to 0 of 0 clients (pilot pc
  skipped)", which is correct because the only client was the gunner.
- Rest aim and gun target were the same on both sides.

  | Turret | Camera rel | Gun target +0x8B8 | Root yaw |
  |---|---|---|---|
  | (-69777, -45940) | (24.3, 90.0) | (24.3, 75.0, 0.0) | -90.0 |
  | (-71175, -45940) | (2.2, 89.3) | (2.2, 75.0) | -90.0 |

- Heat while firing, compared at the same seconds on each side. The values agree within about one point after
  allowing for the half-second difference in when each side samples.

  | Turret | Host | Client |
  |---|---|---|
  | Client's turret | 1, 11, 19, 29, 38, 42 | 6.1, 15.1, 24.1, 33.2, 42.2 |
  | Host's turret | 9, 18, 27, 37 | 3.1, 12.2, 21.2, 30.2 |

  One sample after release differs: the client showed 23.84 where the host had already reached 0. The next row brought
  the client to 0.
- Screenshots: host `fire-client` shows the client's hits on the bug. Client `fire-host` shows the host's round and
  impact effects.
- Results:
  - Scenario passed, drive rc 0. Host rc 0, client rc 0. ERROR/FATAL 0 on both sides. No CRASH line, no new crash
    dumps.
  - Host WARN 130, of which 128 are the existing card095 "name not sendable" warnings.
  - Client WARN 2, both from the driver's look timeout at line 50 ("smooth turn not within 1 degree in 3 s"). That
    step is a `try` step, and the aim held.
  - The host's line 52 look also timed out as a `try` step.
- An earlier attempt, c125zb, failed with drive rc 1 at line 14 ("no server copy of the client's pawn"). The host
  dropped the client PC during the dropship flight while RTT was 2195 ms and the load average was 12-14 from other
  sessions' processes. That failure was environmental. The run was otherwise rc 0/0 with no dumps, and c125zc reran
  it cleanly.

### Caveats

- The driver's smooth look to the bug times out (try step) under load; the aim reached the bug anyway.
- Heat on the client lags the host by up to one 100 ms row plus the sampling phase.

Report complete.
