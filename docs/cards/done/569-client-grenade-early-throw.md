# Card 569: client grenade sometimes does not appear at the client's release

User 2026-11-21: "sometimes the client grenade wouldn't spawn and that might be a host not syncing fast enough."

## Cause (static, not yet proven at runtime)

The host does not throw late. It throws early, on the wrong animation event.

- `USTAbilityTask_PlayAnimation::Activate` (exe+0x50D9580) starts the throw sequence on the avatar's animator, then
  binds the task's `DoAction` (exe+0x50E3BC0) to the animator's shared +0xC0 multicast and `AnimationEnded` to +0xD0.
- `UADAnimNotify_DoAction::Notify_Implementation` (exe+0x4A24210) finds the actor's animator component (vfunc 0x720,
  FindComponentByClass) and broadcasts that +0xC0 delegate. It does not say which sequence it came from, and the task's
  `DoAction` does not check its key: it is `rcx+0x80; jmp Broadcast`.
- So any DoAction notify from any sequence on the same animator fires every bound task. On the host, the server copy
  of the client trooper still plays Morita fire sequences that the client's fire RPCs started (the client fired until
  t=101037769). A fire sequence's DoAction can then release the grenade task 107 ms after the ability started.
- Evidence: host grenade copy 14 left with velocity (1481, 2400, -1023), a yaw of about 58 degrees. That matches the
  client's aim at the ability start (Y 53.97), not its aim at release (Y 82.11). The client got copy 14 and it
  exploded in the same place on both screens. But the client's own launch is released (card 286), and the host copy
  launched about 1 s before the client's release animation, from the start aim. So at the client's release the user
  saw no grenade leave.
- In single player, fire stops before a throw starts, so the shared delegate rarely matters. In co-op, the server
  copy's fire sequences arrive late, so they overlap the throw.

## b381 `d96452661a363ea9` (= b380 + card 569 diagnostic, build-b5 copy kept)

Hook `UADAnimNotifyWrapper::Notify(mesh, sequence)` (exe+0x4A241F0). It notes the inner notify (wrapper +0x38) and
the sequence while it runs. Each PlayAnimation `DoAction` on a player (host own, server copy, client own) logs
(cap 48):

`card569: <side> t=N <owner side> DoAction, task key K, actor A: notify N (class) in sequence S; M outside a notify`

Predictions (C1M1 range, the client fires the Morita for 1500 ms, then throws at once at the grenade target,
UBWCOOP_C74_LOG=1 for the release rows):
- Host log: the server copy's grenade task has a `DoAction` row whose sequence is a Morita fire sequence, not the throw
  sequence, about 100 ms after the throw's card104 start row. The host grenade spawn follows at once.
- Client log: the client's own throw `DoAction` names the throw sequence.
- If every grenade DoAction names the throw sequence, the cause is wrong. Then read the time from the start row to the
  DoAction row on each side.

## Fix design (after the b381 rows)

Gate in `detour_c104_do_action`: when a task's `DoAction` comes from a notify whose sequence is not the sequence that
task started, skip it. The task's own sequence is learned by the first start-then-notify pair on that animator: from
the card104 start detour (key) and the wrapper's sequence, keep a small key-to-sequence table per run, filled only
when one task is bound on that animator. The rows above show whether that pairing is clean before the gate is built.

## Run c569b390b (b390, junior 2026-11-21): no early throw seen, and the diagnostic rows were used up

The range reached SO3 9/10 and the nest Activate, and all 3 client throw rounds ran (Morita fire for 1500 ms, then G).

| Throw | Grenade | Client OnDoAction | Host OnDoAction | Host spawn |
|---|---|---|---|---|
| 1 | Incendiary | 153684194 | 153684184 | 153684188 |
| 2 | Frag | 153697844 | 153697833 | 153697838 |
| 3 | Frag | about 153711000 | 153710966 | 153710967 |

- From the G press to OnDoAction took about 600 ms (Incendiary) and about 1100 ms (Frag). The host's DoAction is 10 to 11 ms
  before the client's, and its spawn trails its own DoAction by 1 to 5 ms. The throw 1 explode copy is 0.3 uu from the
  host's. The prediction of a host DoAction about 100 ms after the start row did not reproduce in these 3 throws.
- The first G threw the Incendiary: G uses ability slot 0, and the client held 1 Incendiary and 1 Frag. Throws 2 and 3
  landed but the nest count stayed 9/10 (scenario side, with the junior).
- Diagnostic gap: all 48 `card569:` rows per side were Morita weapon tasks (fire, pump, swap; the task's actor is the
  weapon), before any throw. On the client, a weapon reads as Authority, so its rows said "host own".

## b391 `7b3f1572485d9f18` (= b390 + card 569 row filter, build-b5 copy kept)

`c569_note_source` logs only tasks whose actor is a player character (the grenade ability's avatar); weapon tasks are
skipped before the cap. Prediction for the next c569 run: every `card569:` row has a `BP_PlayerCharacter*` actor and
side text matching the pawn's side, and each throw has a row. A row whose sequence is a fire montage for a grenade
task proves the shared-delegate cause; throw-sequence rows on every throw over several fire-then-throw rounds weaken it.

## c569b392a (b392) and b393 `849fb12b3ffcbd19`

Client: one `client own DoAction` row per throw, 3 of 3. The rows read Grenade_Throw_Incendiary, Frag and Frag, each
with `notify ADAnimNotify_DoAction`, and the grenade spawned about 1 ms later. Host: 0 grenade rows. All 48 of 48 rows
were server copy weapon tasks (MoritaMk1_Fire, MoritaMK1_Underslung_*, notify null). On the host those tasks carry the
pawn as actor, so the b391 pawn filter keeps them.

b393 = b392 + a task key filter in `c569_note_source`: only keys that start with `Grenade` are logged, on both sides.
Prediction for the next run: every host and client throw gives one card569 row, and no row has a weapon key.

## c569b392b (b393)

4 of 4 throws paired: one host and one client row per throw, about 10 ms apart, every one from
`ADAnimNotify_DoAction` in a `Grenade_Throw_*` montage. No early throw in 7 throws over c569b390b to c569b392b. A repro
needs fire still held, or released less than about 100 ms before, when G goes down (the card 107 chord verb can
press both).

## b394 `1e26bc27dc1be5e2` (= b393 + gate + card 576, build-b5 copy kept)

Gate in `detour_c104_do_action` (`c569_skip`, anim_c104.cpp): a task whose key starts with `Grenade` runs DoAction only
inside an `ADAnimNotify` wrapper notify whose sequence name contains `Grenade`. Anything else is skipped with a row:

`card569: <side> t=N skip <key> DoAction (notify <n>, sequence <s>): not its grenade sequence; K skips`

`UBWCOOP_C569_OFF=1` turns the gate off.

Proof run: the overlap repro (fire held through the G press, chord verb), gate on, then the same with
`UBWCOOP_C569_OFF=1`. Predictions:
- Gate on: 0 early throws. Each throw spawns at its own montage notify. Every skip row (if any) names a fire montage or
  notify null. Normal throws give 0 skip rows.
- Gate off: the early throw reproduces, matching a host DoAction row outside a grenade notify. If it does not
  reproduce with the gate off, the shared-delegate cause is not proven and the gate stays only as a guard.

## Result: proven, senior 2026-11-21 (runs c569b395a gate on, c569b395d gate off, clean window)

- Gate on (a): 0 early throws in 3 chord rounds. Each host throw was the in-notify DoAction. The 16 host skips were
  all notify-null or sequence-null rows during the fire hold. Normal throws gave 0 skips.
- Gate off (d, `UBWCOOP_C569_OFF=1`): the host threw at the first outside-notify DoAction of the fire hold in all 3
  rounds (card074 server-copy, caller exe+0x1424549). It was 523, 1026 and 736 ms before the client's own DoAction
  notify, with 22 outside-notify rows at 150 ms spacing.
- So the shared-delegate cause is proven and the gate fixes it, 3 of 3. No new WARN types on either side. Done when
  the user's playtest confirms it; the card104 AnimationEnded try-waits timing out is the scenario's, not this card's.

## Closed (senior, 2026-11-22)

The fix is proven by logs and runs, and the proof is in the sections above. Under the no-playtest-parking rule, a proven fix does not stay open waiting for a play check. It is on the playtest checklist in STATE.md. If the user sees it again in play, reopen this card with that report.
