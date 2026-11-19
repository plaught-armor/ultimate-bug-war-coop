# 317 A thrown flare's client copy flies its own way until the host's flare rests

Playtest 2026-11-13 (user: "after they are deployed their model is supposed to roll but it lags") and b17 run 3
(host log `UBWCoop-host-20261114-035445.log`, client log `UBWCoop-client-20261114-035447.log`), digest round 7:
`WARN card132: mismatch BP_SupportFlare_Tutorial_C #123416 location host (12450,12850,450) client (13800,13100,150)`.

Senior trace 2026-11-14, logs and code. What the client shows is settled; why the host sends nothing in flight is
supported, and step 1 confirms it with one row.

## What happens (the client threw; the host's server copy spawned the flare)

| t (ms) | Host flare | Client copy |
|---|---|---|
| 289083401 / 408 | spawn at (12542, 13122, 288), rows `world P` and `world M -1.71,100.18,-0.00,0` (yaw 100) | spawned at the same place, `rest applied, moved 0 uu, root collision 0, simulating 0` |
| +20 / +24 | (12521, 13240, 284): flies along yaw 100 | (12659, 13122, 287): flies along +x |
| 289084975 / 982 | (12451, 12199, 121) | (13769, 13122, 141) |
| 289084991 | digest heal row (card 256) | `was 1609 uu from the host's rest`, set there |
| 289085808 on | moving samples (card 287) | glides, 10 uu steps |
| 289086801 | first own rest row | |

For 1.6 s the client sees the flare fly 1200 uu in a direction the host's never took, then jump 1609 uu. Without a
digest round in that window the jump waits for the first rest row, 3.4 s after the throw.

## Cause

1. The copy launches itself. It is spawned by the `P` row with no rotation, its own BeginPlay or movement component
   starts it along +x (y stays 13122), and the `M` row's rotation in the same millisecond comes too late to turn it.
2. The host sends no row while the flare flies. `c104_settle_one` (`world_c98.cpp:1817`) samples a moving body only
   when its root simulates (`c104_sample_moving`, 100 ms, 10 uu). A root that does not simulate gets a row only after
   standing still 30 ticks. No client `card287 ... first moving sample` row exists before 289085808, so in flight the
   flare's root did not simulate (a movement component flies it; physics takes over on the ground). Supported, not
   read from the Blueprint.

## Step 1 (diagnostic, one row per relayed class, both sides)

At a relayed actor's spawn and again at its first rest: `card317: <side> <class> root <component class> simulating
<0|1>, movement component <class|none> velocity (x, y, z), actor yaw <deg>`. Prediction: host flare at spawn
`simulating 0`, a projectile movement component with a velocity along yaw 100; client copy the same component with a
velocity along yaw 0. If the host flare simulates at spawn, cause 2 is wrong and the card stops for a new trace of
`c104_settle_one`'s `rested` path.

## Fix (after step 1)

1. Host: a relayed actor whose root does not simulate and that moves (not still, more than `kC104SampleUu` from its
   last row) sends moving samples at the same rate as a simulating one. One rule for every relayed class.
2. Client: a copy is moved by the host's rows only. Its own launch is stopped at spawn (movement component
   deactivated, velocity zero; a simulating root is already put to sleep), and the `P` row's copy is spawned with the
   host's rotation, not turned afterwards.
3. Tally per map, client: `card317: client copies <n>, self-moved before the first host row <n>, largest jump on a
   host row <uu>` (targets 0 and under 100 uu).

## Sibling sweep (report before coding)

Every relayed class (card 104 list) with a movement component or a launch in its construction or BeginPlay: thrown
flares (all `BP_SupportFlare*`), resupply and drop pods, shell casings, dropped weapons, satchels, anything the
grenade path (`proj_c132.cpp`) does not carry. For each: what moves it on the host, what moves the copy, which rows
cover the flight. Also card 287's glide on the slow roll: the client row repeats `1 samples over 570 ms ... first
moving sample, 10 uu` every 0.7 s; report whether a 10 uu, 100 ms sample floor makes a roll at that speed look
stepped, and what floor would not.

## Predictions (one run, C1M1 TacSupport flare throw by the client, then by the host)

- Zero `WARN card132: mismatch BP_SupportFlare` rows.
- Client: the first `card287 ... first moving sample` row for the flare within 200 ms of its spawn row; no
  `was <d> uu from the host's rest` row over 100 uu for it.
- Client tally `self-moved before the first host row 0`.

## Done when

The predictions hold in one run, the sibling list is in the report, and the user sees a thrown flare fly and roll
the same on both screens in the next playtest.

## Closed

Closed 2026-11-17 (senior): card 470 fix in b173; b40b173a: thrown flare 0 hops after the first sample, largest step 82.3 uu, 0 mismatch rows. The 'user sees' check is replaced by the measured rows.
