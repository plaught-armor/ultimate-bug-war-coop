# 357: Client runs the game's StartOverkillState when a corpse is overkilled

Found in run `b42` (b42 `8965a327d495d442`, FED_C2M1), client log lines 8799-8818. First run in which host grenades hit
bug corpses in a client session.

## What the log shows

- Host grenade explosion at 342844323 near two dead warrior bugs (guid 0x32c, 0x336).
- Host sends `Gib` for both (342844691, 342844693). Client handles them at 342844707/708: `card039: client Gib signal
  ... dead 1 overkilled 0 state 4: Gore OnOverkill; death edge already ran`. This is the intended client path.
- 342844720: the replicated Health of the corpses drops past the threshold (`-80.0 -> -10080.0`, `-16.0 -> -4471.0`,
  `OverkillThreshold 2500.0`), `HealthAttributeChanged called`.
- 342844725/726: `WARN card038: client game-logic StartOverkillState` and `ReleaseCorpseTicket`, 2 calls each, outside
  the mod's HandleDying.

So the client runs the overkill twice: once from the host's Gib signal, once from its own health notify. Card 039's
rule is "No StartOverkillState or HandleDying on the client". The client death edge covers overkill at death; overkill
of an existing corpse (health crossing the threshold after death) was not covered. `StartOverkillState` on the client
also reads GameMode (null on a client) and the spawn manager at GameMode+0x388.

## Fix direction (junior traces first)

1. Trace, static: what `StartOverkillState` (0x051A8A20) does after the GameMode read, and whether it spawns gore
   (`SpawnGoreResponse`) a second time on the client. State from the b42 rows whether a second gore spawn happened
   (rows after line 8818).
2. In a client session the host's Gib signal is the only overkill writer: `detour_start_overkill` does not run the
   original for a call that is not the mod's own (`g_c39.in_mod_call`), and logs one plain `card357` row per skip.
   Same for `ReleaseCorpseTicket` if the trace shows it is only reached from there.
3. Sibling sweep: the other card 038 game-logic entries on the client reached from `HealthAttributeChanged` after
   death (`OverkillDamageEffectReceived`, `TryPlayOverkillAnimation`), and corpse actors' `OnOverkilled`.

## Prediction (1 run: host grenade into 2 or more fresh corpses near the client, FED_C2M1)

- Each overkilled corpse: one host `Gib`, one client `Gib signal ... Gore OnOverkill`, one `card357` skip row, gore
  seen in a client shot.
- 0 `WARN card038` in the client log; client `card038` summary counts `StartOverkillState 0, ReleaseCorpseTicket 0`.

## Trace result (junior, static on the exe plus the b42 rows, before b43)

`StartOverkillState` 0x1451A8A20 has no GameMode read of its own. In order:

1. State byte `+0xAE8` != 5: set it to 5, call vtable `+0xB30`, vtable `+0xA38` and `ReceiveBaseStateChanged`
   0x1450CDB40.
2. Bark cancel (0x1437E78F0, 0x145178C00, 0x144C5B1D0).
3. `+0xA28` and `+0xA29` both set: `ReleaseCorpseTicket` 0x1451A38B0.
4. Gore component `+0xA90`, `GetGoreLocation` 0x1450CD9F0; with valid gore data (`+0xD0`) it calls `SpawnGoreResponse`
   0x14524F880 with the overkill response (data `+0x230`). The only condition is valid data. The GameMode gates are
   inside `SpawnGoreResponse`: the Niagara burst (0x14524FC85) and the chunks (0x14524FE3A). Sound, decals and screen
   effects are not gated.
5. Vtable `+0xB38` with "%s has been Overkill" (debug print), then the `+0xA20` component.
6. State machine `+0xA30` not active: vtable `+0x400` with 0.1 (`SetLifeSpan(0.1)`), `+0x690+0x176` = 0.

So on a client the native call spawns the overkill gore a second time (sound, decals and screen effects; no burst or
chunks), releases a corpse ticket and puts a 0.1 s lifespan on a replicated corpse.

The b42 rows cannot show the second spawn directly: the c39 trace rows are off and the card 132 gore trace hit its cap
of 100 earlier. The client destroys of 0x32c and 0x336 (342844823/824) come 98 ms after the client's
`StartOverkillState` (725), which matches the 0.1 s lifespan, but the host destroyed both at 342844805, so a replicated
destroy about 18 ms later fits too.

Xrefs (rel32 calls and pointer scan):

- `ReleaseCorpseTicket`: one caller, 0x1451A8ACD inside `StartOverkillState`. The `StartOverkillState` skip covers it;
  `detour_release_corpse` is left as it is (diag log only).
- `StartOverkillState`: no direct calls, 3 vtables (0x148013448, 0x14804BBE8, 0x1481323F8). The client path is the
  mod's own `c37_notify` calling `HealthAttributeChanged` (enemies.cpp), which reaches it once Health crosses
  -OverkillThreshold.

Sibling sweep:

- `OverkillDamageEffectReceived` 0x144A3D850: only through a pointer at 0x147DE6048 (a GE callback); b42 client count 0.
  Left alone.
- `TryPlayOverkillAnimation` 0x1451B12E0: only from an exec thunk at 0x1450D0D25 (Blueprint), animation only; its hook
  is ENEMYAUDIT gated and logs only. Left alone.
- `OnOverkilled`: no mod hook; reached from the overkill state the skip no longer enters on the client.

## Fix (b43)

`c357_skip_overkill` in enemies.cpp: in a client session `detour_start_overkill` returns before the original unless
`g_c39.in_mod_call` is set, before the card 038 count, with one `card357: client StartOverkillState skipped` row per
skip (capped at 64). The hook in hooks_ubw.cpp is no longer diag gated, so the skip is in the play build.

Prediction for the b43 run: one `card357` skip row per corpse whose replicated Health crosses the threshold; 0 `WARN
card038`; summary `StartOverkillState 0, ReleaseCorpseTicket 0`; the client destroy of an overkilled corpse comes from
the host's replicated destroy (about 15-20 ms after the host row), not 100 ms after the client notify.
