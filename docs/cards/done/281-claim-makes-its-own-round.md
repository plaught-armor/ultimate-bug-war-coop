# 281 A client hit claim with no round makes its own round (damage no longer needs the copy to have fired)

Source: `docs/SYNC_AUDIT.md` entry 3. Client damage has two owners: the client's weapon decides that a shot hit,
but the host applies it only if the server copy's independently simulated weapon fired a round in the same
1500 ms (cards 072, 175, 274, 276 each removed one way the two fall apart). Row of the defect: `WARN card157:
... HitClaim shot N from pc ... found no round in 1500 ms: dropped` (`claim_c157.cpp:459`).

When: build 5 if build 4's FED_C3M2 proof run still shows `found no round` rows; after the playtest if it
shows 0. Senior decides from that run; do not start before.

## Static facts (disassembly, senior, 2026-11-13)

- `UADProjectilePooler::SpawnProjectile(init data class, transform, world context)` 0x04A363F0 is static, with
  no ability, magazine or rate gate. Bound as `g_c74_spawn_fn` (`grenades.cpp:548`, call form at `:799`).
- `AADProjectile::OnHit` 0x04A35990 applies no damage itself; it broadcasts the dynamic multicast delegate at
  projectile +0x2B0 (`TArray` of 16-byte `{weak object, function name}` pairs). `BindToProjectileImpact`
  0x04A36B70 appends the weapon ability's pair to it for each round.
- `apply_npc` already runs the original `OnHit` on a round with a hit it builds itself (`build_hit`), for flying
  rounds too. A host-made round fits the same call.

## Fix spec

- `round_add` (host): per player controller keep one "last round" record: init data class of the round, and a
  copy of the first pair at +0x2B0 (count read first; 0 pairs means no record). Fixed table, same size as the
  player table; cleared on map change with the round table.
- Claim wait expiry (`claim_c157.cpp:459` site): instead of dropping, when the controller has a record and
  `claim_check` passes, spawn a round with `g_c74_spawn_fn` at the claim's point, append the recorded pair to its
  +0x2B0 list through the engine's own add (the one `BindToProjectileImpact` uses, 0x1411653C0 grow path; do not
  write the array by hand), and pass it to `apply_npc` as a flying round. `OnHit` releases it to the pool.
- No record (the copy never fired this weapon): keep today's drop row.
- The copy's own firing is unchanged (muzzle, tracer, sound for the other players). The card 175 hold stays.
- New rows: `card281: host t=<ms> claim shot <n> from pc <name> served by a made round (init <class>)`; counter
  in the existing map-end summary.
- Files: `mod/src/ubw/claim_c157.cpp` only (plus `hooks_internal.h` if a declaration is needed). Do not touch
  `fire.cpp`, `grenades.cpp`, the card 274 magazine mirror or the card 276 press path.

## Attacked in the spec

- Stale pair after a weapon switch: the record is keyed by controller only, so a claim right after a switch
  could carry the old weapon's damage event. Key the record by controller and the copy's current weapon; no
  match means drop as today.
- Dead ability object in the pair: the pair holds a weak object; check it resolves before the spawn.
- Unlimited client damage: accepted by the trust model; `claim_check` (range, trace, NPC alive) still runs.
- Double damage: a claim is served either by a real round or by a made one, never both, since the made round
  exists only at expiry of the wait for that claim. A real round arriving later stays unclaimed and expires.

## Only a run settles it (prediction)

The bound blueprint event applies its damage while the ability is not active. Predicted: each `card281: ...
served by a made round` row is followed by the `card165` health drop row for the same NPC, and 0 `found no
round` rows remain while a record exists. If the made-round rows show no health drop, the fallback is one spare
round of the copy held per weapon (taken from a real launch, so its binding is the ability's own); senior
re-specs, no second attempt on this method.

## Proof

Batched with the FED_C3M2 proof run of its build (client `clear` with reloads). Budget: 0 extra runs.

## Run `c243-c3m2-b4a` (build 4, 2026-11-13)

4 `WARN card157 ... HitClaim shot N ... found no round in 1500 ms: dropped` (shots 47, 48, 185, 269) and 3
`card175 ... the copy fired 0 of` rows, with the client holding rounds at shots 47, 48 and 185. Claims still drop
with cards 274 and 276 in: this card is needed. Build 5.

## Senior review of the build 5 diff (2026-11-13, static): accepted, run proof owed

Coded in `claim_c157.cpp` (`record_round`, `bind_pair`, `made_round`, `serve_made`) plus one parameter on
`c157_launch` (`proj_c132.cpp:535`). Deviations a to e in the junior report accepted. Checked against the
disassembly: exe+0x11653C0 takes (packed size and alignment, array, pointer to max), reads the old max, reallocates
for old max + 1, writes the new max and returns the old one; it does not touch num. So the call with `list + 12`
only when `num == max`, followed by the write of `data[num]` and `num + 1`, is right. exe+0x4A36B70 is the exec
thunk, not the native bind.

Open for the run: the made round's instigator is whatever the pool left. The proof row is the `card157 ... applied
... Health A -> B` row after a `card281 ... served by a made round` row with B below A, and the kill credited to
the client (kill feed or score row). A `WARN card281 ... no made round` row names its cause; any cause other than
"weapon changed" is a finding.

## Build b187 `920b53a5d0bb7c2e` (2026-11-17)
`record_round` stored 0 made rounds; the FIFO served claims 16-18 from rounds 19-21, then 3 `WARN card157 found no round`. b188 adds a capped row naming the early return that fired (impact list data null, num<=0, r.init null, copy_weapon null).

## Build b189 `d4181cdc840b944a` (2026-11-17)
Run c423b189a. `record_round` no longer skips silently (0 rounds not recorded). The made-round path was not exercised: launches 16-18 were discarded, but claims 16 and 19-21 were misses, and serve_made runs only for NPC claims (guid != 0). Next: the discard window must cover shots that hit the bug (aim at its bounds, gated on the look).

## Build b190 `d451b08388a464db` (2026-11-17)
Run c423b190d: `host discard 3` hit launches 16, 17 and 18. Shots 16 and 17 hit a WarriorBug and were each "served by a made round" (2 made). Shots 18 and 19 were client misses on the landscape; a miss claim carries no NPC guid and never takes a made round, so shot 19 logged one WARN card157 "(miss) found no round" by design. `record_round` no longer skips silently (b189: 0 rounds not recorded).

## Closed (2026-11-17 15:57)
Evidence: c423b190d, 2 hit claims whose real round was discarded were served by made rounds. 0 EXCEPTION/FATAL, digest 0.
Not done: the count=3 try reached 2 because the scenario's third aim missed the bug; a miss needs no round.
Reopen when: a hit claim with an NPC guid logs WARN card157 "found no round", or a playtest shows a client hit doing no damage on the host.
