# Card 406: client Fx receive spikes are the game's own play cost (diagnostic kept)

Status: accepted 2026-11-16 (diagnostic, no fix). Class: card 330 (per-frame cost).

## Question

After card 405 the client's `Fx` receive still showed one frame of 10 to 80 ms per 30 s window. Written prediction:
the first use of an asset on a map loads it on the game thread (`c74_find_asset`, `StaticLoadObject`).

## Result

- Run `c406b97a` (`5ec0105b75e22ed4`, asset resolve timed): no resolve took 2 ms. Prediction refuted.
- Run `c406b98a` (`da9d5d719936c8e2`, each row's play call timed, `card406: client fx slow row` at 3 ms or more):
  16 rows in 194 s. Eight are `NiagaraActivate` of `NS_FX_WA_BugsSpline_JustWalk` on placed `NiagaraActor`s in the
  join replay (3.4 to 24.1 ms each, 63 ms inside 100 ms: the 73 ms frame of that window). Seven are the first
  `ExecuteGameplayCue_NonReplicated` of a cue (4.5 to 9.1 ms), one a `SpawnSoundAttached` (4.5 ms).

All of it is time inside the game's own play functions, which the host pays for the same effects. The mod's part
of an Fx row is under the 3 ms line.

## Kept

The `card406` line stays (16 lines per chain run): a playtest hitch from an effect shows with its asset and time.

## Left open (not carded)

The join replay plays all its rows in the frame they arrive. A per-frame time budget on the client would spread the
63 ms over a few frames; it is at a map load, so low value.
