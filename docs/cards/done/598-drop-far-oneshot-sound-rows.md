# Card 598: drop keyed one-shot sound rows for a PC past the net cull

## Finding

In run c582b449d, during a 60 s idle window after the join replay, the host sent 63 keyed sound E rows (1086 B) to a PC whose pawn was more than 15000 uu (kC518CullUu) from the sound. That is 3.6% of the fx blob. Most of these rows are wasted:

- A NetGUID owner past the cull has no actor on the client (card 530), so the client cannot attach the sound at all.
- User rule 2026-11-21: sounds that far away do not need to be audible. This holds even inside the 20000 uu attenuation radius.

## Fix

In `host_flush` (fx_c136_net.cpp, the per-PC row loop), skip a row for that PC when all of these hold:

- `meta.keyed_sound` is set;
- `meta.kind == FxKind::OneShot`;
- the PC's pawn is past kC518CullUu from `meta.at`.

Reuse the distance test in `c518_culled_sound`. Turn it into a predicate called before `frame_add_named`, and keep the counter so the skip is measured.

These rows are not skipped:

- Journal rows (`FxKind::Journal`, the looping or kept sounds). A player who walks in later must still hear a running loop, and the journal replay only covers joins.
- End and D rows.
- Rows with no position (`is_zero(m.at)`).
- Rows for a PC with no pawn.

## Trace first

Before you write the code, confirm these two points statically:

1. No D or End row refers back to a one-shot row id that the client must have seen. A dropped one-shot then leaves no dangling stop.
2. No one-shot keyed sound carries state the client needs later. For example, check that none of them is a music or stinger cue that card 302, 304 or 314 depends on.

Name any exceptions in this card.

## Proof

Run one C3M1 run, folded into the next build's run. Do not add a dedicated run.

- The card518 "(60 s)" rows show the skipped count. The sent bytes fall by about that amount compared with c582b449d.
- The skip itself causes 0 mismatches and no new WARN class.
- Both players, standing close together, still hear the same near sounds: card173 heard rows for owners within 15000 uu are unchanged.

## Static trace (2026-11-21, before the code)

1. No D or End row refers to a sound row. `is_stop` covers only NiagaraDeactivate and NiagaraDeactivateImmediate,
   so a sound never gets a D or End row. D rows name journal entries or Niagara spawn ids. On the client,
   `client_end_row` and a D row whose live copy is missing both return without effect, so even a stray stop would
   do nothing.
2. No skipped row carries state the client needs later. 2D sounds (music and stingers through PlaySound2D) have no
   point, so `is_zero(at)` keeps them on every path. The card 304 and card 314 rows are counters and attenuation
   choices. None of them depends on a far 3D one-shot reaching the client.

**Exception.** `is_spawn` covers Niagara only, so sounds are never journaled. Every keyed sound row is a OneShot,
loops included. A looping sound keyed by a placed actor name, skipped while the player is far, would never start
later, because nothing replays it when the player comes near. The skip therefore covers only NetGUID-keyed rows.
Their owners past the 15000 uu cull have no actor on the client (card 530), so the row is dropped on arrival
anyway, and skipping it loses nothing. Placed-keyed far rows are still sent. The first 16 per map are named by
`card598: host t=... far placed-keyed sound ... sent to ...` rows, so a later card can decide them by name.

## Code (b451, dwmapi c4b1edf9083c61a5)

`fx_c136_net.cpp`: `FxMeta.guid_keyed` (set in host_emit: keyed sound with a NetGUID owner). In host_flush,
`c518_past_cull` tests the pawn distance and `c598_skip_far` skips a OneShot with `guid_keyed`, counting rows and
bytes. The card518 totals row appends `; card598 NetGUID-keyed one-shots not sent N rows M B`. Its prefix is
unchanged, so existing scenario waits still match. Journal, End and D rows are never skipped.

## b451 run c582b451a: result (scenario passed, exit 0, 2026-11-21)

Host log `UBWCoop-host-20261121-233040.log`, client log `UBWCoop-client-20261121-233042.log` (lane 2 Win64).

- The card518 (60 s) rows show the skip. Cumulative sent past the cull: 44 rows 745 B, then 104 rows 1778 B, then
  164 rows 2846 B. NetGUID-keyed one-shots not sent: 14 rows 115 B, then 21 rows 169 B, then 23 rows 185 B. The skip
  is small in this run, about 6% of the far bytes. Most far rows are placed-keyed and are still sent.
- 16 card598 placed-keyed rows hit the per-map cap. All of the logged rows are
  MS_EMIT_Gate_Large_MilitaryBase_Malfunction at (-67850, -44500, 1100), repeating about once a second: a looping
  gate emitter. This is the bulk of the remaining far bytes. It is a candidate for a follow-up that sends a placed
  loop once and skips its repeats, not for this card.
- Mismatches: 0 (card132 rounds 1-6, playlog summaries). WARN: host 6, client 7, the same classes as b450 (attach,
  card046 warp, card152 bone check, card537 not-keyed, config). No new class.
- Near sounds still play on both sides: card173 rows at 277 uu (host) and 348 uu (client) from the listener.

Done for this card's scope.

## b452 follow-up: the far placed gate sound

**Test.** Run c582b451a host card455 rows show `attached sound MS_EMIT_Gate_Large_MilitaryBase_Malfunction on
BP_Door_Sliding_Gigantic_C ... caller exe+0x50FBA73`. The rows come at exact 1000 ms steps (202857029, 202858029,
202859029, ...), each a new attached sound spawned from the same caller, and the asset list has the Gate Malfunction
anim montages. So it is a one-shot re-fired once a second by the gate's malfunction loop, not one loop start. The
next re-fire covers a player who walks in.

**Code (b452, dwmapi 61270761ad74d597).** `fx_c136_net.cpp`: a 32-row table of (asset, placed key, last ms) pairs.
A placed-keyed OneShot sound whose pair was enqueued within 2500 ms before is marked `repeat`. `c598_skip_far` skips
a far row with `repeat` and counts it. The first row of a pair is always sent, so a real loop start (one row) is
never skipped. The table resets at each map arm. The card518 totals row now ends `..., placed re-fires not sent N
rows M B`.

**Prediction.** In run c599b452a the card518 60 s totals show placed re-fires not sent at about 1 row per second while
the gate plays (about 60 per totals row). The far placed-keyed rows sent drop by the same count. Near gate sounds
(card173) still play on the client.

## b452 run c599b452a: result (2026-11-21)

The card518 60 s rows show cumulative far rows still sent: 1 row 15 B at each of the three totals rows. NetGUID-keyed
one-shots not sent: 12 rows 96 B, then 15 rows 117 B, then 18 rows 138 B. Placed re-fires not sent: 44 rows 704 B,
then 104 rows 1664 B, then 164 rows 2624 B, which is 60 per minute as predicted. The far keyed bytes sent fell from
2846 B in b451 to 15 B. Mismatches: 0. Done.

## Closed (senior, 2026-11-21)

Proven in b452 (c599b452a), and c448b452a passed. Ships on lane 1 as b452 (dwmapi 61270761ad74d597).
