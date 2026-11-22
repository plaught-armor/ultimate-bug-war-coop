# 493: FED_C5M2 cave-in blast follows player 0 only (card 301 class G)

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: open (written 2026-11-18 14:50, senior). Traced static 2026-11-18; batch, no run of its own.

## Finding

FED_C5M2_Scripting event `Cave in` @31328 (armed by `BP_PlayerPresence_C_408` @30571, `caveInReady`; re-armed every
30 s by `CallFunctionWithDelay(30.0, 'Cave in')` @32214) reads `GetSTPlayerCharacter(self)` location @31338, spawns a
Niagara burst there and applies a 20 damage, 1500 uu radius, self-damaging blast at the same point. On the host
player 0 is the host pawn, so the hazard tracks the host only: the client takes damage only within 1500 uu of the
host, and the host never sees a burst at the client. What the client's own copy of the level script does (its
burst at its own pawn, damage dropped or applied) is not known.

## Trace (2026-11-18, senior, static)

1. The client runs it. `BP_PlayerPresence_C_408` is not under the card 227 gate, so `c227_run_edge`
   (objectives.cpp) runs the original edge on both sides. The client's own level script then runs the whole @30571
   chain (Nuke Town, Add Finale_SO7, the sand-fall Niagara, `caveInReady = true`) and its own `Cave in` loop every
   30 s, with its own `GetSTPlayerCharacter` (the client pawn). Its Niagara burst at its own pawn is local and is
   the client's visual (card 132 item 4a: the client spawns its own STSpawnNiagraSystemAtLocation). Its
   `ApplyDamageAtLocation` does nothing: damage runs only on the host (marker_c166.cpp header). The host's loop
   damages within 1500 uu of the host pawn only, and its burst at the host is not seen on the client.
2. Net effect: the client player takes cave-in damage only when standing within 1500 uu of the host. The client
   sees bursts at itself on its own 30 s clock, which is out of phase with any host damage. Not a crash and not a
   play blocker: batch it, do not chase it.

## Fix (after the trace)

- Host: each time the level script's `Cave in` runs (exec thunk on the level script frame, the card 310 pattern),
  run its damage part once more for every other living player inside a card 102 acting scope for that player's
  server copy, so that `GetSTPlayerCharacter` returns the copy. The `CallFunctionWithDelay(30.0, 'Cave in')` at the
  end re-arms the same named timer, so the reruns must not add timers: rerun only the event body, guarded so a
  rerun never reruns again (the card 310 "kind cleared" rule).
- Client: keep its own `Cave in` for the burst (the visual stays at its own pawn). The client's 30 s clock drifts
  from the host's; accept that, or a later card can drive the client's burst from the host's rerun. The visual is
  not the damage, so this is not a full world sync gap as long as the damage is right.
- Simpler candidate, to check first: the event is one Blueprint function, `Cave in` (ExecuteUbergraph @31328). If
  card 102 already has a "per-player rerun of a level script event" helper (card 310 / 267 `c267_other_players`),
  reuse it, not a new mechanism.

## Proof (written before the run)

In a C5M2 soak past Finale_SO7, with the players more than 3000 uu apart: one blast row per player per 30 s on the
host, the client's health drops by a blast at its own position, the burst is seen on both sides.

Reopen when: a cave-in blast misses a player in the tunnel, or a player sees no burst.

## c493b250a result and first caller (2026-11-18, senior)

The tunnel run timed out on the 2 rerun rows: the host's C_408 enter ran its original (CompleteSub Finale_SO6, Add
Finale_SO7) and no `Cave in` CallFunctionWithDelay ran in about 120 s. Static: in the FED_C5M2_Scripting decompile,
@30739 only sets `caveInReady = true`; @30709 schedules `Sand fall` (a Niagara burst, @31212-31322, no further
call). The only call to `Cave in` in the package is its own re-arm @32214, and no `goto`/push targets @31328. So
nothing in the level script starts the first `Cave in`; the first call, if any, comes from outside the package
(a level sequence event track or another actor calling it by name). Not traceable from the Scripting decompile.

Next: in one C5M2 finale run, log every `CallFunctionWithDelay` and every `ProcessEvent` of a function named
`Cave in` on the level script (both sides) from the C_408 enter to the level end. No row at all means the event is
dead in the shipped game: close this card as "no first caller", reopen if a row ever appears. Also fix the
scenario's client warp: `warp client BP_PlayerPresence_C_408 4000` landed at 2238 uu.

## Closed (2026-11-18 20:36, senior, c493b252a)

Evidence: the probe logged every CallFunctionWithDelay and every ProcessEvent of `Cave in` on both sides from the
C_408 enter (host 693019457) to the run end (127 s). Host: 'StartIntroConversation' at map start and 'Sand fall' at
the C_408 enter; client: 'Sand fall' only. No 'Cave in' on either side. Matches the static read: nothing in the
shipped game starts the first `Cave in`, so the player-0 blast never runs.
Not done: no per-player rerun built (nothing to rerun).
Reopen when: any run logs a `Cave in` call.
