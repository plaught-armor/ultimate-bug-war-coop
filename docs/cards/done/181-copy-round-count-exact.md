# Card 181 — The host copy fires exactly the client's rounds

## Goal
For every primary press, the host copy of a client's weapon fires the same number of rounds as the client did.
Card 180 removed the one-frame bias on the release; what is left is a hold that ends within the transit jitter (a
few ms) before a round boundary, where the copy's montage DoAction fires one round more before the release arrives.

## Known (card 180 trace, c176-r3)
- The extra round is fired by the copy's montage DoAction → `USTWeaponFireAbility::Fire` 0x5236320 →
  FireProjectile 0x4A33E40 (Fire+0x3e7). The release carries the client's count (Fire arg bits 8-14), but it
  arrives after the round has fired, so the host cannot cap it there.
- Cost of one over-fire: one extra tracer and round on the host's screen, one round of the copy's ammo, and in
  card 157 every later claim of that PC pairs with a round one earlier; the leftover round parks 1500 ms and then
  replays its own non-NPC hit or returns to the pool.

## Design question (senior decides from the trace, before code)
Two shapes; the trace must cost both:
1. **Per-round message.** The client sends one small op per FireProjectile and the copy fires a round only when
   one is waiting. Trace: how the copy's DoAction is gated without skipping what the original frees, what happens
   to the montage and ammo when a round is refused, and the bytes per second at the fastest weapon.
2. **Retire the extra round.** At the release, when the copy's count is above the client's, the newest round goes
   back to the pool and out of the card 157 table before any claim pairs with it, and the ammo is given back.
   Trace: whether the round can already have hit something in that window, and the card 157 pairing change.

## Predictions
Written after the design is chosen. At minimum: FireProjectile host = client for every press of a run with at least
20 short presses with holds near a round boundary; no card157 WARN; baseline WARN only.

## Files
`mod/src/ubw/fire.cpp`, `mod/src/ubw/hooks_internal.h`, one scenario, the card. Card 157 files only if shape 2 is
chosen.

## Senior design note (2026-11-12, design review)

The server-copy cards (104, 141, 174 to 180, 208, 210) all replay the client's input on the host copy and then
correct the timing. Where the client has already decided a result, send the result, not the input. For this card
that is shape 1 (per-round message): the trace costs both shapes as written, but shape 2 needs a reason why a
correction after the fact is better than the copy firing only the rounds the client reports.

## Step 1 trace (senior, static, 2026-11-13) — shape 1 chosen, spec below

Disassembly of `USTWeaponFireAbility::Fire` (exe+0x5236320, PDB public `?Fire@USTWeaponFireAbility@@MEAAXXZ`,
`void Fire()` with `this` = the ability instance, vtable slot +0x510). Order of work inside it:

| Offset | What |
|---|---|
| +0x2c to +0x98 | owning actor as `ASTWeapon`, `GetEquippedAmmunitionType`, `GetAmmunitionTypeByTag`; exit when no ammunition type |
| +0x101 | `CommitAbility` (vtable +0x358) with the current handle, actor info and activation info; **exit when it returns false**. The cost (the round of ammo) and the cooldown are spent here |
| +0x19f to +0x1d9 | owner interface `GetMuzzleTransform`, `ApplyDynamicZeroThresholdAdjustment`, `UADTransformGeneratorBase::GenerateTransforms`: one transform per projectile |
| +0x2a7 to +0x4f9 | loop over the transforms (stride 0x60): `FireProjectile` (+0x3e7), bind the task's delegate, `ReadyForActivation` |
| +0x5a7 | `PlaySoundAtLocationFNameWithAIAwareness` (the fire sound with AI hearing) |
| +0x5af to +0x66c | weapon ASC tag count update, three virtual calls (+0x960, +0x528, +0x530), `PostFire` (0x50B4C10), vtable +0x538 |

Findings:

1. Everything a round costs or shows (ammo, cooldown, projectiles, sound, PostFire) is inside `Fire`, after its
   entry. A detour that returns before calling the original for one DoAction spends nothing and creates no task,
   so there is nothing to free. This answers card 180's open point; gating `FireProjectile` instead would be
   wrong (ammo already spent at +0x101, and one Fire can make several projectiles).
2. One `Fire` can call `FireProjectile` more than once (one per generated transform: pellet weapons). The round
   unit for this card is therefore one `Fire` call, not one `FireProjectile`. The card 175 counters
   (`C24Relay::fired`, `wanted`, `g_c44_fires`) count `FireProjectile` on both sides and stay as they are.
3. `Fire` is not hooked today. Its callers on the Morita path are listed in card 178 (montage DoAction notify,
   `USTWeaponAbility::DoAction` 0x5234790, BP OnDoAction, vtable +0x510).
4. A refused DoAction leaves the copy's montage running; the next DoAction notify comes one fire interval later
   (Morita about 85 ms, card 180).
5. Wire cost: one channel frame per client round. At the Morita's 85 ms that is about 12 frames per second while
   the trigger is held, the same frame size as a Fire edge. Acceptable under the friends-only netcode model.

### Spec

New hook on `USTWeaponFireAbility::Fire` (add the decorated name above to `tools/symbols.txt`, regenerate
offsets), both sides. The shooter is found as in `detour_fire_projectile` (`attributes.cpp:909`): the ability's
avatar weapon, then `weapon_pawn`, then `sprint_side`.

- **Client, own pawn (`kSideClientOwn`).** Read `g_c44_fires[kSideClientOwn]` before and after the original. When
  it rose (CommitAbility passed and at least one projectile left), send one new channel op `ChanOp::Round` to the
  host with argument 0, from inside the detour. It is sent inside the engine Tick, so it leaves with that frame's
  `TickFlush`; no extra flush. The round of the release frame (card 178) is sent the same way and so arrives
  before the deferred release.
- **Host, server copy (`kSideServerCopy`).** Per pawn (a small table keyed by pawn InternalIndex, size
  `kC17Slots`): `credits` (rounds reported by the client and not yet fired), `owed` (the ability instance of the
  last refused DoAction, or null) and counters for the log.
  - Fire detour: `credits > 0`: take one, call the original. `credits == 0`: do not call the original, set
    `owed = this`, count a refusal.
  - `ChanOp::Round` received for that client's pawn: when `owed` is set and that ability is still active (its
    spec's ActiveCount is over 0; `c276_spec_active` in `fire.cpp:238` reads it), clear `owed` and call the
    original `Fire(owed)` at once; otherwise `++credits`. So the copy fires each round either at its own DoAction
    (the report came first) or at the report (the DoAction came first), never without a report.
  - The table row is cleared (credits 0, owed null) on a primary press, when the relay is dropped (`c24_release`,
    `fire.cpp:218`) and when the pawn goes. A credit still unused then is logged once per press:
    `card181: host t=... %s press ended: client rounds %u, copy rounds %u, refused %u, late-fired %u, unused %u`.
- **Every other side** (host-own, client-side copies, AI weapons, mechs, turrets): call the original untouched.
  Check before coding which classes reach this `Fire` on a server copy besides the soldier weapons (mech primary
  uses kind `kFirePrimary` too, card 076): if a mech or mounted turret weapon of a client reaches it, the client
  side of the hook must report those rounds as well, or the gate must skip them. State which in the report.
- **Card 175 hold stays.** It covers the other direction (the copy behind at the release): with a release in
  hold, a credit still fires at the copy's next DoAction. Do not change `kC175HoldMs`.
- Guards: the op is ignored with one capped WARN when its sender has no pawn row; `credits` is capped at
  `kFireRoundsMax` (a count above it is a fault, logged). No allocation, no new thread.

Pre-code checks (report anything that differs):

1. `detour_fire_projectile` on the client runs for the own weapon inside the client's own `Fire` (it must, for
   the before and after count to work); confirm from one existing client log that card020 client-own
   FireProjectile rows exist for a Morita press.
2. The next free `ChanOp` value and that an op with argument 0 is legal (`mod/src/ue/chan.h`).
3. Whether calling the original `Fire(owed)` outside the montage notify is safe: it reads only the ability's own
   current handle, actor info and activation info (see table), so it needs the ability instance alive and
   active. If the instance can be destroyed between the refusal and the report (instanced per execution), hold
   the weapon ASC and spec handle instead and resolve the instance at the report; if neither is sound, drop the
   late-fire branch (credits only) and say so.

### Predictions (one run, batched with the next build; FED_C1M1 FAST, the card 180 scenario shape)

At least 20 short primary presses with holds placed near a round boundary (80 to 95 ms and 165 to 180 ms), plus
two long bursts of 30 rounds or more and one pellet weapon press if the loadout has one.

- Every press: host `card181` line shows client rounds = copy rounds, unused 0. Before the fix the same scenario
  shows the copy one over on some presses (card 180, presses 2 and 5).
- FireProjectile totals: `g_c44_fires` server-copy on the host equals client-own on the client at run end.
- Refusals over 0 and late-fired over 0 somewhere in the run (proves both branches ran); if either is 0, say so,
  do not rerun for it.
- The copy's ammo equals the client's own ammo after each burst (card 069 1 s line).
- No `card157` WARN, no new WARN class against the baseline, digest 0 mismatches.
- Receiver-side: one host shot during a long burst showing the copy's tracers (senior views it).

Budget: 3 runs. After the playtest; not a play blocker (one extra tracer and round on the host's screen).

## Senior review of the build 5 diff (2026-11-13, static): accepted, run proof owed

Code matches the spec (`mod/src/ubw/round_c181.cpp`, hook row `hooks_ubw.cpp:1942`, clears at `fire.cpp:243`,
`:423`, `:567`, receiver `chan_ops.cpp:217`). Compile hash with card 153 fixes: `faaa085d7714170b`.

Attacked, no defect found: null shooter and null controller pawn (`sprint_side` and `ability_shooter` take null);
row table full (WARN, call not gated) and idle-row reuse; `owed` held as a weak ref and cleared before the late
call, so a Fire that re-enters the detour cannot fire it twice; late Fire only for an instanced-per-actor ability
whose own spec (exact class match, not the Underslung) is active; credits capped at `kFireRoundsMax`; host-own,
client-side copies, AI, mechs and turrets pass through untouched on both sides.

Decisions: mechs and mounted turrets stay ungated (their relay re-presses every tick, one round per press). The
`kC132Ops` ledger row for `Round` is accepted.

What the run must settle (read from the press-end rows, do not rerun to chase them):

- Fast taps: a press arriving while the previous release is still in the card 175 hold clears a credit of the
  previous press (`fire.cpp:423`). It shows as `unused` over 0 on a `next press` row with copy rounds one under
  client rounds. If seen, the fix is to carry the unused credit over a `next press` clear, not to lengthen the
  hold.
- A credit spent on a copy Fire whose CommitAbility fails (ammo differs) shows as copy rounds equal but
  FireProjectile totals unequal.
- Late-fired 0 with refused over 0 means the fire ability is not instanced per actor; then the credit path
  alone carries the card and the late branch is removed.

## Run c181-b5 predictions (junior, 2026-11-13, before launch)

Build 5 (`faaa085d7714170b`), `tools/scenarios/c181-rounds.txt` (card 180 shape: c176-click throw spot, FED_C1M1
FAST, GOD=1): 24 short client presses (80 to 95 ms and 165 to 180 ms), two bursts of 3000 ms (about 35 rounds) after
`ammo fill` and a reload, one last 85 ms press. No pellet weapon in the default loadout, so no pellet press.

- P1. Every host `card181: host t=... press ended (...)` row: client rounds = copy rounds, unused 0, no "a refused
  round still owed". At least 25 rows (24 short presses and 2 bursts; a press with no round prints none).
- P2. The two burst rows: client rounds and copy rounds 30 or more each, equal.
- P3. Over the run, refused over 0 and late-fired over 0 on some row. Either at 0 is reported as is, no rerun.
  Late-fired 0 with refused over 0 means the ability is not instanced per actor (senior review).
- P4. Fast taps: any `next press` row with unused over 0 and copy rounds one under client rounds is the carry-over
  case of the senior review; reported, not fixed.
- P5. FireProjectile totals: host `g_c44_fires` server-copy count equals the client's client-own count at run end
  (card175/card044 count rows on each side).
- P6. Copy ammo equals client ammo after each burst (card 069 1 s rows).
- P7. No `WARN card181`, no `card157` WARN, no WARN class new against c180-r1; digest 0 mismatches.
- P8. Shots: `c181-burst1-host`, `c181-burst2-host` show the copy's tracers.

## Run a (`c181-b5r3`, DLL `faaa085d7714170b`): short presses exact, bursts fail on a card 276 fault

Result, from `logs/c181-b5r3-host.log`: 24 short presses and the final press end with client rounds = copy rounds and
unused 0. Both 3000 ms holds fail: `client rounds 8, copy rounds 1, refused 1, late-fired 1, unused 7` (host line
3875) and `7, 1, 1, 1, 6`. 13 `WARN card157 ... found no round in 1500 ms: dropped` follow from the missing copy rounds.
Digest 0 mismatches, 0 card181 WARN.

Cause (senior trace, host lines 3790 to 3876). The scenario reloads right before each hold, so the press at
t=247617774 lands in the copy's reload and starts nothing. `c276_repress` (`fire.cpp:308`) waits for the reload to end,
then:

1. Its time gate is `now - r.press_ms < kC276RepressMs * (r.represses + 1)`, measured from the press. After 2188 ms
   under a reload all three tries pass at once: tries 1, 2, 3 at 2188, 2204, 2221 ms, one per tick.
2. Try 1 (fire ability active 0) starts the copy's fire ability. Its first Fire is refused by card 181 (no Round yet),
   so `r.fired` stays 0 and tries 2 and 3 run with the ability active 1. Each does tag released + tag pressed on the
   active ability. The release ends the automatic fire loop; the press starts nothing on an active ability.
3. The first Round fires the owed round late (t=247620007). The ability then ends. No further copy Fire call, so the
   next 7 Rounds stay credits.

Before card 181 the copy fired at once after try 1, `r.fired` went non-zero and tries 2 and 3 never ran; the fault in
the gate was there but hidden.

Fix (class: the re-press, all soldier weapons):

- `C24Relay` gets `repress_ms` (0 = none yet). The gate is measured from the later of the press and the last try or
  skipped try: after a reload ends, one try, then `kC276RepressMs` before the next.
- No release + press while the copy's fire ability is active (`c276_spec_active(asc, "FireAbility") > 0`): return
  without counting a try. An ability left active by the press before ends by itself; the try then runs on the next
  tick that sees it inactive. An active ability with a refused Fire is waiting for its Round and must not be released.
- The reload skip also sets `repress_ms`, so the first try comes `kC276RepressMs` after the reload ends, not the tick
  it ends. (Try 1 at the same tick as the reload end worked in this run; keep it if the delay costs a round. Junior
  decides from the first run and records which.)

Scenario corrections for the rerun: one hold that starts under a reload (this case, predicted now: client rounds =
copy rounds within the card 175 bound, re-presses 1) and one hold with no reload before it, magazine full, 30+ rounds;
tracer shot during the hold, not after the release; card069 ammo rows and the `g_c44` total row turned on.

Predictions for the rerun: every press end row `client rounds == copy rounds`, `unused 0`; no `re-pressed Fire` row
with `fire ability active 1`; 0 `WARN card157 ... found no round`; 0 card181 WARN; digest 0 mismatches.

Re-press fix reviewed in source (`fire.cpp:312`), accepted, rerun owed. Attacked: reload skip only sampled every
`kC276RepressMs` (first try 0 to `kC276RepressMs` after the reload end, bounded); stale active ability from the press
before (try waits, runs on the first inactive tick; host line 3250 shows that ability ends within 104 ms); refused
Fire pending (no release now; `r.fired` goes non-zero at the late fire). The re-press row's `fire ability active` now
always prints 0.

## Gap: a held weapon that fires one round per activation (senior, 2026-11-14, b19 run 2)

- Log (`logs/c310-proof-c1m1-host.log`, underslung shotgun, 1500 ms hold, twice): `card181: host t=304317711 ...
  press ended (release (card 175 hold)): client rounds 3, copy rounds 1, refused 0, late-fired 0, unused 2`;
  `card175: ... the copy fired 9 of the client's 27 rounds`; `card276: ... re-presses 0, rounds 9, no re-press`;
  then 31 `WARN card157 ... HitClaim ... (miss) found no round in 1500 ms: dropped`. Playtest host log: 288 of 396
  rounds over 40 `card175` rows, 40 dropped claims, all `(miss)`.
- Cause (settled, static): the client's ability ends after each round and its held input starts it again. The
  relay presses the copy once. `c181_receive_round` (`round_c181.cpp:174`) turns rounds 2 and 3 into credits, but
  a credit is only spent by the copy's own `Fire`, and nothing starts the copy's ability again: `c276_repress`
  (`fire.cpp:312`) stops at `r.fired != 0`.
- Effect: the host and its screen miss those rounds (tracer, sound, impact), and a hit claim for one of them has
  no round to pair with. Damage loss is possible; the logs so far only show dropped misses.
- Fix: `c276_repress` also runs when the pawn's card 181 row has a credit waiting (a round the client reported and
  the copy has not fired), with the same idle, reload and spool gates and the same `kC276RepressMs` spacing. That
  path is bounded by the credits, not by `kC276Represses`. The `r.fired == 0` path stays as it is. The re-press row
  says which path (`credit waiting <n>`).
- Sibling sweep (report before the run): every soldier fire ability by how it repeats on a hold (montage loop, or
  ends and restarts per round), from the asset list; mech and turret copies already re-press every tick.
- Proof (rides the batch 3 run, same range step): both holds end with `client rounds 3, copy rounds 3, ... unused
  0`, `card175` reads `27 of the client's 27`, 0 `WARN card157 ... found no round`.

## Closed (2026-11-17 13:03): round counts exact over 25 runs

Tally of every host `card181: ... press ended` row in the runs of builds b162 to b182 (25 host logs: `c423`,
`c439`, `c236`, `c315` series): 138 press ends, all `client rounds == copy rounds` with `unused 0`; 0 `found no
round` rows in the same logs. The credit re-press of the one-round-per-activation gap is in (`fire.cpp:338`) and
ran (`credit waiting` rows in `c423b172a`).

Reopen (playtest-log check, shared with cards 274 and 276): a press end row with unequal rounds or `unused` above
0, or any `found no round` row.
