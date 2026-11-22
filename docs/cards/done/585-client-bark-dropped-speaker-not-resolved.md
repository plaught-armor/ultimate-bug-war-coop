# Card 585: client bark row dropped when the speaker GUID does not resolve

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: open (found 2026-11-21 in c582b411a, b411 `0b694ba883d43fae`).

## Finding

Host `logs/c582b411a-host.log:5703` shows `bark started Default__BP_BarkType_sawbugtanker_C line 5 speaker
BP_AI_M7Mech_C guid 0x236 sent to 1 of 1`. The client dropped it at `logs/c582b411a-client.log:5839` with
`WARN card451: ... bark row dropped (parsed 1, class 1, speaker 0, subsystem 0)`. The class resolved, but
`c41_object_from_guid(pc, 0x236)` found no actor. Most likely the mech was not net-relevant to the client then, or
its channel was not open yet. The bark line is lost on the client, which is an all-effects-sent gap.

## To do

1. Trace statically before any run. Determine whether the host sends bark rows for a speaker whose actor channel to
   that client is closed (card 434 already logs relevance per FX owner). If it does, decide between two options:
   send the row only for relevant speakers (the client cannot hear a speaker it cannot see anyway), or carry the
   speaker location so the client plays the bark at that point.
2. Count the dropped rows per map in the card 451 totals so the size of the gap is known. One drop in a 171 s run
   is the only data point so far.

## Trace and fix (senior, 2026-11-21, rides the next build)

- The host sent every bark row to every ponged client through `c95_send_all`, with no relevance check. A client with
  no actor channel for the speaker cannot resolve the guid, so the row is dropped.
- Card 451 shows the bark is range-checked and its sound is attenuated by distance. A speaker outside a client's net
  relevancy is therefore outside its hearing too, so not sending is the correct behavior, not a lost effect.
- Fix (class): `c95_send_all` takes an optional `relevant` actor. It skips a PC whose connection has no channel for
  that actor (`c434_has_channel`) and counts it in `C95Fanout::culled`. `c451_host_send` passes the speaker. The host
  bark row prints `culled <n>`, and the relay tally prints `culled <n>`. Other actor-keyed rows can opt in later.
- The dropped count per map was already in the card 451 relay tally (`dropped`), so to-do item 2 needs no code.
- Prediction for the next run: zero `WARN card451: ... bark row dropped` rows with speaker 0. If the client drops a row
  anyway, its speaker channel closed between send and arrival; log both t values. Host `culled` greater than 0 only
  when players are far apart.
- Syntax checks for bark_c451.cpp and door_c95.cpp returned rc 0.

## Closed 2026-11-21 (run c587b421a, b421)

Host bark rows print "culled 0" 13 times, and the client logs 0 "WARN card451: ... bark row dropped" rows. Clean.
