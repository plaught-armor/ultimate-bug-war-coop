# 470 — Thrown flare hops on the client while it flies

Found 2026-11-16 by the senior from `logs/b40b168a-client.log` while turning card 317's "user sees" item into a
measured proof. Real mod bug, visible on the receiver.

## What the log shows

- `card287: client ... copy BP_SupportFlare_C host 122633: 28 samples over 6320 ms, largest step 22.7 uu, at rest
  0.0 uu from the host's (glided, 6 teleports)`; host 118268: 1 teleport; `BP_SupportFlare_Tutorial_C` 125738: 1.
- Pickups in the same logs (Morita, health, ammo, grenade): 0 teleports each.
- `largest step` does not count a set-at-once, so the row reads smooth while the copy hopped 6 times.

## Cause (static, `world_c98.cpp` `c287_take`, line 1943)

`too_far = dist(copy now, new sample) > kC287TeleportUu (300)`. The host sends a moving sample every 100 ms
(`kC104SampleMs`). A flare in flight covers more than 300 uu per sample (over 3000 uu/s), and the copy is one sample
behind by design (it blends towards the last sample), so every sample of the fast part of the flight is "too far" and
is set at once: the flare is drawn at 10 positions a second instead of gliding.

## Fix (`c470_patch.py`, senior scratchpad, `--check` ok: 2 edits)

`lag` = copy to new sample, `hop` = last sample to new sample. Set at once only when
`lag > 300 && (lag > 2 * hop || hop > kC470HopMaxUu (1500))`: more than two samples behind, or a real host teleport.
A copy with no blend entry yet has no last sample, so its first sample keeps the plain 300 uu limit.

Attacked: first sample (hop 0, as before); rest row far from the copy with a small hop (still set at once); real warp
of 5000 uu on a live entry (hop over 1500, set at once); repeat sample at one place with a far copy (hop 0, set at
once); blend time stays bounded by `kC287MaxMs`.

## Done when

One run, the card 317 flare steps (C1M1 TacSupport flare thrown by the client, then by the host):

- every `card287 ... BP_SupportFlare` rest row reads `0 teleports` or 1 (the first sample's `set at once`), and
  `largest step` under 150 uu (3000 uu/s at 60 fps is 50 uu a frame; a hop was 300+);
- 0 `WARN card132: mismatch BP_SupportFlare`, 0 `WARN card287`, at rest within 100 uu of the host's;
- receiver-side shot pair in flight (host and client 300 ms after the throw key) and at rest, viewed by the junior;
- pickups keep 0 teleports.

Also owed, same run: change the rest row to print the teleports apart from the first sample's, so the number reads
without subtraction (`N hops after the first sample`).

## Closed

Closed 2026-11-17 (senior), build b173 `dedb14d000d6ec36`: b40b173a: flare rest rows 0 hops after the first sample, largest step 82.3 uu, pickups glided, shot pair viewed; leftover re-arm rows are card 473.
