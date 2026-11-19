# 418 Blueprint `SpawnSoundAtLocation` passes the sound capture point unseen (inlined script thunk)

Found 2026-11-16 tracing card 409 (same class of gap: a script thunk with the native body inlined).

## Evidence (static)

- `UGameplayStatics` `execSpawnSoundAtLocation` is at `exe+0x3C8A340` (native registration table). It never calls
  `UGameplayStatics::SpawnSoundAtLocation` (`exe+0x3C6FDD0`, the detoured entry): the body is inlined (it calls the
  audio component creation and the component's virtual play itself). A Blueprint call makes no capture event on the
  host and gets no own-or-host decision on the client.
- Same shape, no Blueprint user found, so nothing to do: `execPlaySoundAtLocation` (`0x3C83870`),
  `execSpawnEmitterAtLocation` (`0x3C88AC0`), `execSpawnEmitterAttached` (`0x3C88E60`). `UGameplayStatics` decals
  (`0x3C87550`, `0x3C87810`) are no capture point and have no Blueprint user either. `PlaySound2D` and `SpawnSound2D`
  are 2D and not sent by design.
- Blueprint users (import sweep of 1679 Blueprint packages, see card 409): `BP_SatchelPlacementSite` (the planted
  satchel's beep, ubergraph offset 81, on the 0.5 s `Pulse Light` timer), `BP_WhitePhosphorus_Projectile` (offset
  1518, before `K2_DestroyActor`), `BP_WhitePhosphorusCenter_Projectile` (offset 43), `BP_RandomEmitterVolume` (two
  random ambient one-shots at a random point of the volume).
- Today each side hears only what its own copy of the Blueprint runs. The ambient volume is random per side by
  design of the asset. The satchel beep and the white phosphorus burst are the ones that can be missing or doubled.

## Step 1, diagnostic and fix in one (next lane 2 build with other cards)

Detour `exe+0x3C8A340` (`void exec(UObject* context, FFrame* stack, void* result)`), both sides; always call the
original (the thunk reads its parameters from the script frame).

- Host: after the original, when `result` holds an audio component, queue a `SpawnSoundAtLocation` event from the
  component (sound asset, world location and rotation, volume and pitch multipliers, owner = the frame's object),
  through the same queue the detoured entry uses. Skip `BP_RandomEmitterVolume` (per side by design; name it in the
  own-table so the client keeps its own).
- Client: the usual own-or-host decision for point `SpawnSoundAtLocation`; when the effect is the host's to send,
  stop and destroy the component in `result` right after the original (same frame, nothing audible), and let the
  host's row play.
- Log one line per asset per map on each side with the count, so the first run says which of the four fire where.

Predictions:

1. Host plants a satchel: host log has `SpawnSoundAtLocation` events for the beep at 2 per second while armed; the
   client's playlog has one `audio row` per event at the site and no own copy.
2. Client plants a satchel: same rows (the host's copy of the site runs the timer), no doubled beep on the client.
3. `BP_RandomEmitterVolume`: 0 events sent; client count of own ambient one-shots unchanged from before.
4. Bytes: about 35 B per row (the `MS_ATK_FlakBug_Fire` sound row measured 490 B for 14 rows); the beep is 2 rows
   per second per armed satchel, so at most about 70 B/s for the few seconds a satchel is armed.

## Done when

Predictions 1 to 3 hold in a run that plants one satchel from each side, with both logs.

## Result of step 1 (junior, 2026-11-16, runs c418b116b and c418b116c on b116)

- c418b116b voided on a scenario fault (a `Resort_SO2` completion wait; that objective completes only after the
  plants). Fixed in the scenario; c418b116c passed with rc 0/0, 0 FATAL or ERROR, 0 digest mismatches.
- Predictions 1 and 2 refuted, and the premise was wrong: the planted satchel's beep is not a
  `SpawnSoundAtLocation` call on a timer. It is the site's `TriggeredSFX` audio component, activated by the
  `SatchelTimerStarted` event that `ASTSatchelPlacementSite::StartTimer` (`exe+0x5171A70`) fires. Only the copy that
  ran StartTimer beeps: on a host plant the client's copy of the site never started its fuse and stayed silent.
- Prediction 3 holds: `BP_RandomEmitterVolume` sent 0 events (host 94 own ambient one-shots, 6 assets, 12 volumes;
  client 97, 8, 8, each side its own by design).

## Fix (b120 `d74ad9c849fde45c`, carried into b121 `821e36ce8e035086`)

- Host: `ASTSatchelPlacementSite_StartTimer` is detoured (`hold.cpp` `detour_c133_start`). After the original, the
  host sends `HoldDone` "name x y z" to every client and logs `card418: host ... fuse ... started: arm sent to N of M
  clients`.
- Client: the `HoldDone` handler is `c133_client_arm`. It finds the placed site by name and runs the original
  StartTimer on its own copy unless the fuse already started (site byte `0x50D`), so the client's copy beeps on its
  own fuse. It logs `card418: client ... arm of <site> from the host: StartTimer run, armed 1` or `armed already (own
  hold)`.
- The card's planned `execSpawnSoundAtLocation` detour is not built. The white phosphorus Blueprint users
  (`BP_WhitePhosphorus_Projectile`, `BP_WhitePhosphorusCenter_Projectile`) are not traced and stay open for the senior.

## Result (run `c418b121a`, b121 `821e36ce8e035086`, FED_C3M1, 2026-11-16)

rc 0/0, 87 s, 0 FATAL/ERROR, no crash dumps.

- Prediction 1 holds. Host plant: at t=486585514 the host logs `fuse 3.0 s started: arm sent to 1 of 1 clients`; 10 ms
  later the client logs `arm of SatchelSiteCAC_... from the host: StartTimer run, armed 1, fuse 3.0 s`.
- Prediction 2 holds. Client plant (t=486593294): the host sent the arm, the client logged `armed already (own hold)`.
  No double arm.
- Prediction 3 holds: no `WARN card418`.
- Card 408 F4 screenshots are valid on both sides (destroyed building with destroy effects).
- Not this card: the client logged digest rounds 1 and 2 with 2 mismatches, class `Pose` (corpse pose).
