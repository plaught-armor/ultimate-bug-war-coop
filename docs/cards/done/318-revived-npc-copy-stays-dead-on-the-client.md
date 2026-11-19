# 318 A revived NPC's client copy stays in the Dead state (CTF AIs frozen in place)

Playtest 2026-11-13, user answer 2026-11-14: on the client the CTF AIs were "frozen in place". Client logs
`logs/playtest-menu-20261113-183002.log` (C1M1 CTF) and `logs/playtest-menu-20261113-184603.log` (C2M1, C3M1); host log
`client/UBWCoop-menu-20261113-182510.log`.

Senior trace 2026-11-14, logs, code and exe. The state fault is settled. What the Blueprint shows in that state is
inferred from the user's report, and step 1 logs it.

## Cause

The client's revive (`c131_client_revive`, `enemies.cpp:1430`) sets the base state to Aware only when the copy is
still in Dying (3). The game moves a copy from Dying to Dead (4) by itself when the death flipbook ends, about 1 s
after the death, and a laser tag revive comes 5 s or more after it. So the copy is in Dead at the revive and is left
there.

| Log | Revives | `base state 3 -> 2` | `base state 4 -> 4 (not Dying: left as is)` |
|---|---|---|---|
| C1M1 CTF (`BP_AI_Trooper_Laser_Red_C`, `_Blue_C`) | 40 | 1 | 39 |
| C2M1, C3M1 (`BP_AI_Trooper_Morita_C` 143, `_Medic_C` 3, `_RadioTrooper_C` 4, `_SquadLeader_C` 1) | 151 | 1 | 150 |

The host's handler, `ASTNonPlayableCharacterBase::HandleHealthValueChangedEvent` (RVA 0x05193030), has no such
condition. On old health <= 0 and new health > 0 it does, in order:

1. vfunc +0x790, and when that returns false, vfunc +0x850.
2. Removes the Dying tag (the mod's `c131_remove_dying_tag` copies this).
3. Writes 1 to byte +0x17A of the component at NPC +0x690.
4. `SetCurrentBaseState(Aware)` (RVA 0x051A61F0), unconditional.
5. Calls RVA 0x051AA100 (acts on the object at NPC +0xA20) and RVA 0x051A9E30 (creates or restarts the object at
   NPC +0xA10 from the class at +0x9D0).
6. Broadcasts delegates, calls `BP_OnRevived` (RVA 0x050CD8E0), then vfunc +0xA78.

The client skips the game's handler on a revive (`card037: client skip ... revive branch, HealthAttributeChanged not
called`) and does only step 2 and the capsule collision. A revived copy keeps base state Dead, never gets
`BP_OnRevived`, and its sprite animator holds the last death frame. Later host animation rows for it are
`played visual only` on an NPC whose Blueprint thinks it is dead.

## Step 1 (diagnostic, rides the fix build)

One row per client revive, 2 s after it: `card318: client revived <class> guid <g>: base state <n>, sprite sequence
<name> frame <f> of <n>, moved <uu> since the revive, host moved <uu>` (the host distance from the copy's replicated
location history, or from a host row if the junior finds no cheaper source; say which). Tally at map end:
`card318: client revives <n>, left Dead <n>, still on a death sequence after 2 s <n>, not moved while the host
moved <n>`.

## Fix

1. `c131_client_revive` mirrors the host's handler for a copy in Dying or Dead: steps 3 to 6 above that are visual or
   local state (base state Aware, `BP_OnRevived`, the +0xA20 and +0xA10 calls), not only the tag and the capsule.
   Before coding, the junior names the component at +0x690 and its +0x17A byte, the objects at +0xA10 and +0xA20, and
   vfuncs +0x790, +0x850 and +0xA78, and reports which of them are host-only (AI controller, behaviour tree) and must
   not run on a copy. Nothing host-only is called on the client.
2. The `entry->dying` early return stays only if it cannot hide a revive: report how many revives returned there in
   the playtest logs (they log nothing today) and add a counter to the tally.
3. Card 177: a revive removes the copy's sprite row and death hold at once on the client, without waiting for the
   host's `R <guid>` row (the host sends it at its next pose step).
4. Card 038 dead set: the revived copy's entry is cleared so the next death edge runs in full (the logs show second
   deaths as `Health 15.0 -> -35.0` rows; report whether those ran the full death visual).

## Sibling sweep (report before coding)

- Every NPC class that can revive or be reused from a pool: the laser, Morita, Medic, RadioTrooper and SquadLeader
  troopers above, pooled bugs (card 131's first case), and any class whose health can rise from 0. For each: what the
  host does on the revive and what the copy gets.
- Every other place where the client reproduces part of a game handler it skips (cards 037, 038, 039, 131): list the
  handler's host steps beside the client's, as in the cause section, and name each missing step.
- 3D troopers: whether a skeletal copy left in Dead keeps a ragdoll or pose row after a revive (cards 132, 189).

## Scenario (coverage gap)

The b14 soak and the b18 C1M1 scenario never spawn a laser trooper (only class-default rows): the CTF route skips the
AI fight. The C1M1 scenario's CaptureTheFlag section must start the real match, wait on the `client spawn
BP_AI_Trooper_Laser_` rows, and stay until at least 5 client revives are logged, with a screenshot of the client view
of a revived trooper. No bug clearing in that step: the fight is the test.

## Predictions (one run, C1M1 CTF with the AI match)

- Client: zero `(not Dying: left as is)` revive rows; every `card131: client revive` row reads `-> 2`.
- Client tally `left Dead 0, still on a death sequence after 2 s 0, not moved while the host moved 0`.
- No `WARN card131`, `WARN card177` or `WARN card318` rows on either side.

## Done when

The predictions hold in one run, the sibling report is in, and the user sees the CTF AIs and the squad troopers get
up and move on the client in the next playtest.

## Batch 3 run 9 result (`6ce017264c0f3c81`, `logs/c310-proof-c1m1-*.log`), senior read 2026-11-14

- Prediction 1 holds: 77 `card131: client revive` rows, every one read `base state 4 -> 2` (or `3 -> 2`), `card318
  steps 0xf`, `sprite rows dropped 1`. No `(not Dying or Dead: left as is)` row.
- "Frozen in place" is fixed by the rows: the WARN rows show `moved N since the revive, host moved N` with equal
  numbers (907/907, 170/170).
- Prediction 2 fails on the read-back: 29 `WARN card318: client revived` rows. 13 of them read base state 3 or 4 with
  Death frame 1 to 9: the trooper died again inside the 2 s check window (laser tag), a false WARN of the check, not a
  fault. 16 read base state 2 with `sprite sequence Death frame 11 of 12`: the copy is Aware and moves, and its
  animator's current sequence is still the finished Death.
- Not determined: whether the 16 show a death frame on screen. No client anim row arrives for a revived copy until its
  next Attack01 (host Anim rows are Attack01, Spawn, Taunt only), and the host's animator state after a revive is not
  logged, so the read may be the same on the host (locomotion drawn by another path). No run 9 screenshot has a
  trooper in view.
- The about 30 client `Pose ... text R <guid> result skipped: no row` WARN rows are this card's item 3 working: the
  client drops the sprite row at the revive, the host's `R` comes later (0x584: revive 312597063, `R` sent
  312608336). Benign; the row should not be a WARN when the client dropped the row on a revive.

**Next (one scenario step, no code):** in the CTF section, after a `card131: client revive` row, the client looks at a
laser trooper and takes a screenshot; the junior views it. A walking or standing frame closes the card with the check
changed to read the state after 2 s only when the copy did not die again; a death frame on a moving trooper keeps the
card open with the host-side read-back added (host sprite sequence 2 s after its revive) before any fix.

## Batch 3 run 11 result (`6ce017264c0f3c81`), 2026-11-14

Client screenshot `client-c310-proof-c1m1-c318-client-revived.png`, taken after a `card131: client revive
BP_AI_Trooper_Laser_Red_C ... base state 4 -> 2` row (junior viewed it cropped and enlarged): the near trooper
(about 130 uu) stands upright in a carry pose, the far one is upright too. No death frame on screen, while the same
run logged 16 `WARN card318 ... sprite sequence Death frame` rows. So the animator's current-sequence field is not what
is drawn after a revive; the read-back check is wrong, the fix is right. Left to do after the playtest (small): the
2 s check drops the sprite-sequence condition (or reads the drawn flipbook) and skips copies that died again inside
the window. The user confirms in the playtest that the CTF AIs get up and move.

## Check change (b37 `f3d490b51e1e21f4`, run `b37`, 2026-11-14): accepted, card closed

`c318_step` warns on `dead || still` only; a copy that died again inside the 2 s window is counted, not reported; the
client's `R` row for a guid it dropped at a revive is Applied. Run: 0 `WARN card318` (16 before), 0 client `Pose ...
skipped: no row` WARN rows, 21 `card131: client revive` rows, 15 `card318: client revived` rows (6 died again). Shot
`client-b37-c318-client-revived.png` (junior viewed): revived trooper upright and running. The map-end totals did
not print (the run quits mid-map).
