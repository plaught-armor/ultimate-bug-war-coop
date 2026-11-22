# Card 216: A player's Swoop effects do not all reach the other side
Status 2026-11-22: parked, out of scope (bug mode, user rule 2026-11-19). The status below is as first written.

Status: queued 2026-11-12, from run c212-r1 (DLL `6d7f5193580ad2cd`). Static first, then one run on BUG_C1M1
(scenario c212-r1 is the base).

## Why

c212-r1: the host's Swoop sound reached the client (`card062: client ... remote sound
MS_FLY_PlayerHopperBug_AirDash`). The client's Swoop gave no AirDash line on the host; the host logged only the
client pawn's movement sounds. Niagara and montage of a Swoop have no per-event line on either side, so their sync
is unknown. User rule: every visible and audible effect is the same for every player.

## Do

1. Static: where the AirDash sound, the Swoop Niagara and the Swoop montage start (ability graph, cue, anim
   notify), and which relay each passes on the host-own path and on the server-copy path. Name where the
   client-own Swoop's sound is lost (muted server copy, card 060; or never played on the copy).
2. Class check: the same path for every bug-form ability with a sound or effect (Jump, Glide, DamageVolume, Taunt,
   Melee, Vampirism, Switch).
3. Fix at the cause; a per-event receiver-side line for each effect kind.

## Predictions

Written by the junior after the trace, before the run.

## Done when

- Trace, fix, run lines for both directions. The usual Report items. It ends with "Report complete."

## Senior note (2026-11-13)

Bug-campaign card: it waits behind the Trooper playthrough (user rule 2026-11-12, Trooper first) and is not in
build 5. Step 1 stays with the junior as written. Start points for it, from the code as it stands:
the host-to-client sound path that worked is the `card062` remote sound relay; the server copy's sounds are
muted by card 060 and 2D ones skipped by card 160 (`card160: host ... skipped server copy 2D sound`), so the
first check is whether `MS_FLY_PlayerHopperBug_AirDash` plays on the client's own pawn through a path the
client-to-host cue relay (card 099) does not hook. Card 217 (Glide re-activates on the host copy, same run
family) shares the scenario; do both in one BUG_C1M1 run.
