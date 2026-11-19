# 422 The frame prefix costs 6 units on every channel frame

Found 2026-11-16 by the efficiency audit (static, `mod/src/ue/chan.h`), numbers from run c421b119a (b119
`6a3adf36b52871d8`).

## Evidence

- Every channel frame starts with `kChanPrefix` = `"\x1Fubw1:"`, 6 units. A blob frame (Fx, every packed text op)
  is serialized as UCS-2, so the prefix is 12 bytes; a narrow frame pays 6.
- The prefix only tells a mod frame from a game string in `ClientMessage` (host to client) and `ServerChangeName`
  (client to host): `chan_prefixed` (`chan.cpp`). No game string starts with the control char 0x1F, so that char
  alone does the job. `ubw1` is not a version check: nothing compares it apart from the prefix test, and host and
  client must run the same DLL for every other reason.
- Size of the cost: the playtest log packed 122553 text frames (card 329) into blob frames, 5 units each to save =
  1.2 MB of the 11.35 MB packed, before Fx frames. In c421b119a the Fx channel alone sent about 1025 frames
  (168750 B): 10 kB of it is the 5 units.

## Fix

`kChanPrefix` becomes `L"\x1F"`, `kChanPrefixLen` 1. The three writers copy the constant (`chan_blob_frame`, the
text frame writer, `objectives.cpp`); `g_chan_wire`'s initializer and its `static_assert` in `chan.cpp` change with
it; the header comment names the new prefix. No handler changes: every offset derives from `kChanOpAt`.

The patch is written and held in the senior's scratchpad (`c422_patch.py`) until the junior's 408/409/418 build
batch has reported, so the wire change does not land inside that batch unannounced.

## Predictions (one chain run, `c399-fast-chain.txt`)

1. Client netbudget rows read `Ping 19` and `Pong 19` where b119 reads `Ping 24, Pong 24` (one narrow frame, 5
   bytes less).
2. Host Fx bytes per Fx frame (`chan bytes by op` Fx total over the `rows sent in N frames` total) fall by 10.
3. `card132` digest: 0 mismatches; `bad frames 0` in every `card150` pack row on both sides; no new WARN class.
4. A frame of the old format is not parsed: not testable in a pair run (both sides share the DLL); by reading,
   `chan_prefixed` passes it and the op lookup reads `u` as the opcode, which has no handler on either table.

Prediction 4 names the one behaviour change: an old-build peer's frames reach the op lookup instead of being
ignored. Old and new builds never share a session (the play rule is one DLL for both players).


## Closed 2026-11-16 22:41

In the source since b120 (`mod/src/ue/chan.h:57`, one unit, `static_assert` in `chan.cpp:184`). Prediction 1 holds
in every run since, last `c439b169a`: client netbudget `Ping 19, Pong 19` both ways (was 24). Prediction 3 holds:
`bad frames 0` in all 9 `card150` rows of that run, session summary `mismatches 0`, no new WARN class. Prediction 2
was not read as its own number; the same constant feeds all three writers, so the 5 units cannot stay on the Fx
frames while they left the Ping frame.
