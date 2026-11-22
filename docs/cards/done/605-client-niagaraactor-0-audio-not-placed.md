# Card 605: a host component-play row for NiagaraActor_0 finds no placed copy on the client

Found in c604b460a (b460, C4M1). At t=216405585 the host sent `P NiagaraActor_0 Audio 100 0.0` in a burst with
NiagaraActor_17 to _29. The client matched _17 to _29 to their placed actors. For _0 it logged
`WARN card604: client host row "P NiagaraActor_0 Audio 100 0.0": no placed NiagaraActor_0 with component Audio here
(fields 4)`, and the quit totals show `1 host rows missed here`. The client therefore does not play that sound.

The host row passed the b460 flag test (RF_WasLoaded or RF_DefaultSubObject), so on the host NiagaraActor_0 is a
loaded actor. Questions to answer statically before any run:
- Which level holds the host's NiagaraActor_0 (outer chain), and is that level loaded on the client at that time?
  The client reloaded the map after the death-screen load at t=216121664; check whether a streamed sublevel did not
  come back.
- Or does the client hold the same actor under another name, for example an actor renamed at load?

Done when a C4M1 run shows no `no placed NiagaraActor_` row and the client plays that component.

## b461 (dwmapi 559ed50803f35572): name probe on the miss

A client miss now logs `name probe:` from `c50_name_probe` (mod/src/ubw/interact.cpp): the first same-named actor in
any level without the placed filter, as "<level> flags 0x..", or "none in N levels".

Predictions (c604b461a, C4M1):
1. Any `no placed NiagaraActor_` row carries a probe. "none in N levels" means the client copy is gone (destroyed when
   its system finished, or never loaded): close as a host-only effect lifetime, with the cause. A level plus flags
   without RF_WasLoaded means the copy is not the placed one: fix the lookup class.
2. If no miss row shows (the death-screen reload is not in this run), the card stays open with that note.

Result (c604b461a, C4M1): 13 miss rows at client t=218255240, all from one host tick (t=218255230, 14 rows "P
NiagaraActor_<n> Audio 100 0.0" for _0 and _17 to _26 and more). Every row says `name probe: owner found`: the probe
runs only when the owner lookup fails, so the client found a placed actor of that name, and `c95_component(owner,
"Audio")` returned null on it.

Cause (static): the host sends a row only for a placed, unreplicated owner whose component loaded with the level
(`c604_placed_owner`, `c604_level_component`), so the host actor has a level component named Audio. NiagaraActor_<n>
names are per level, not per world, and `c50_find_placed_any` returns the first placed name match in any level. The
client took a same-named NiagaraActor in another level, one with no Audio component. The row carries no level, so the
client cannot pick the right copy.

Fix (next batch, not b461): use the card 426 level key. The host sends the owner as `<name>@<level hash>`
(`c50_level_key`, 9 more chars, still under kChanNameMax 64), and the client resolves it with the level-aware
match (`c50_key_split` plus the `by_level` test in interact.cpp) for any class. A plain name keeps the old lookup.
Prediction for that run: 0 `no placed NiagaraActor_` rows on C4M1, and a component play row on the client for those 14.

## b462 (dwmapi b01ad3ed4e0b535b)

In source: `c604_names` sends the owner as `c50_level_key`, and `c50_find_placed_any` (and the `c50_name_probe`
diagnostic) accept a level key, matching only the copy in that level. A plain name keeps the old lookup (dropship.cpp).

Prediction for c606b462a and the c448b462a gate, written before the run: 0 `WARN card604: client ... no placed
NiagaraActor_` rows, and every card 604 row's owner is a `name@<8 hex>` key.

### b462 result

c606b462b (FED_C4M2): 0 `no placed NiagaraActor_` rows on the client. The card 604 HordeAudio play row carried the
key `BP_HordeZone_C_1@7eb20b13` and was found on the client. The c448b462a gate result follows.
c448b462a gate (FED_C1M1) passed: drive rc 0, host rc 0, client rc 0, 0 mismatches, 0 `no placed NiagaraActor_` rows.
The keyed card 604 door rows (`BP_Door_Sliding_Large_C_0@1cb15b55`) applied on the client.

Accepted by the senior 2026-11-22; b462 deployed to lane 1 (game/), hashes verified.
