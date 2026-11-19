# 473 — Rested flare keeps re-arming as moving on the client

Found 2026-11-17 in `b40b173a` (junior). After `BP_SupportFlare_Tutorial_C` host 125663 came to rest, the client
logged for about 7 s, every 600 to 700 ms: `first moving sample, ~10 uu from the copy`, then `1 samples over ~603
ms, largest step 1.7 uu, at rest 0.0 uu`. Flare 121993 shows the same once. Nothing visible moves.

Efficiency item (card 329 class): the host sends a moving sample for a body that is at rest. Trace first, static:
which host test marks the flare moving (velocity threshold, a physics wake, or the rest row's place differing from
the sampled place by ~10 uu), and why the client copy is 10 uu off after a rest row that read 0.0 uu.

Done when: a rested flare gets no further sample rows on either side for 10 s, or the traced reason shows the 10 uu
is the game's own motion.

## Closed

Closed 2026-11-17 (senior), traced static, no code change. `c104_settle_one` (world_c98.cpp) sends a moving sample
only when the host actor's location is over `kC104SampleUu` (10 uu) from its last row and it moved that tick, and a
rest row after it stands still again. So each client pair of rows is a real host move of about 10 uu every 600 ms:
the game's own motion of the burning tutorial flare, until its 'D' row at 560497872. The client follows it (rest
0.0 uu). Cost about 3 rows a second for 7 s per tutorial flare; nothing to save.
