# Card 565: NPC troopers sometimes play the wrong animation

User report (2026-11-21): "sometimes the troopers have the wrong animations also"; asked which, the user answered
both the other player's body (card 564) and the game's NPC troopers. No symptom detail yet (which animation, which
side).

## Next step

Static first: how an NPC trooper's sprite sequence is chosen (USTSpriteVariantAnimatorComponent on
`BP_AI_Trooper_Morita_C` and other IsNPC2D troopers) and which inputs exist on the client copy (no AI controller
there: attack, reload and move states the animator reads may never arrive). Compare with card 041 (client bug attack
animations) and card 177 (sprite NPC deaths). Then one run: host and client side by side in view of the Morita squad
in a fight; a row per sequence change on each machine for the same trooper (object, sequence name, t), diffed.

## Prediction

The client's sequence rows differ from the host's for the same trooper in at least one state the client cannot see
(candidates: Attack01 while the host's copy fires, Reload, or Walk while the copy stands).

## Senior static notes (2026-11-21)

- NPC sprite sequences all go through `ASTNonPlayableCharacterBase::TryPlayAnimationSequence` (card 041). The host
  relays only `kC41AttackSeq` (`enemies.cpp` ~3886: Attack, Attack01, Attack02, Flinch, Taunt, Spawn, OverKill,
  GrenadeThrow, Reload) as ChanOp::Anim. It sends only on a forced call or a name change, the same restart rule the
  host's own player uses.
- The client plays a relayed sequence with empty callbacks: no AnimationEnded, so nothing on the client returns the
  copy to its locomotion sequence when the relayed one ends. The client's own Idle / Walk / Run choice is whatever the
  copy's own code picks; unknown when it runs.
- Play log (client, b366): troopers (Morita, Medic, SquadLeader) get Attack01, Flinch, Taunt rows; Attack01 re-sent
  every 60 to 120 ms per trooper during fire (forced plays on the host too).
- Leads: (a) a trooper sequence missing from the relay list (any name the host plays that the client never does);
  (b) the client copy stuck on a relayed sequence's last frame after it ends; (c) the client's own locomotion pick
  disagreeing with the host's (Walk while the host copy stands).

## Trace run

C1M1 on b371, `HOST_EXTRA_ENV` and `CLIENT_EXTRA_ENV` add `UBWCOOP_C41_ANIM_TRACE=1`. Both players stay near the
Morita squad through one fight (about 60 s). Diff: per trooper class, the sequence names each side plays
(card041 trace rows, `c41_dump` table at map end). Senior digs the logs.

### Run c551b376a predictions (junior, 2026-11-21, b376 `eb49926909111446` = b375 + card 568, moved from b372, scenario `tools/scenarios/c373-c1m1.txt`)

Both players about 600-700 uu from a Morita trooper; four warrior bugs spawned at the trooper start the fight; 60 s
with shots every 10 s on both sides. Prediction as above: for the same trooper the client's card041 sequence rows
differ from the host's in at least one state (Attack01 kept after the host's copy went back to locomotion, or a
missing Reload). The senior diffs the logs.

### Diff of run c551b376a (senior, 2026-11-21)

- The relay is lossless. The host sent 302 Anim rows, and the client received and played all 302, with 0 unmatched.
  The per-guid counts match exactly (Attack01, GrenadeThrow, Flinch, bug Attack). Lead (a), lost relay rows, is
  disproven.
- The host played 541 attack-type sequences. The other 239 went to NPCs with no client channel (card434: 192
  skipped), which is expected.
- Leads (b) and (c) are undecided. On the host, the card041 trace line cap (`kC41TraceLines`, 300) was used up at
  t=144736, before the fight at t=144817. The Morita trooper stood on a tower at z 1101, so the look shots show the
  sky.

Next run: reset the trace line cap at the scenario's mark, so the fight window logs in full. Log the locomotion
sequence rows per guid in that window. Use a ground-level trooper. Prediction: in the same window, the client keeps
an attack or flinch sequence after the host has gone back to locomotion. Otherwise the cause is lead (c), and the
client's AnimBP state is the next probe.

## Cause (junior static, 2026-11-21)

Lead (b). The client plays a relayed sequence with empty callbacks, so nothing returns the copy to locomotion when it
ends. On the host the attack is left through the NPC's FSM listener on `AnimationEnded` (exe+0x517CC10, which only
broadcasts the NPC+0x960 delegate). The host's Walk / Run (force 0) come from the state-enter function exe+0x51A0A00,
once per state entry, and Idle (force 1) from `TryPlayIdleAnimation` exe+0x51B1260, Blueprint driven. A client copy
that entered Walk before a relayed Attack01 never gets Walk again: the state does not re-enter.

## Fix (b409 `aee0d8aa0dee808f`, `enemies.cpp` card 565 block)

- A relayed play on an IsNPC2D copy binds a real end callback (`AnimationEnded` on the copy) and keeps one entry per
  NPC: the resume name (the copy's current sequence before the first relayed play; Idle when the current is empty or
  itself a relay name; an existing entry is kept, a relay name is never stored as the resume name).
- On the end (not inside a relayed play): the entry is cleared; Dying or later skips (`skipped dead`), a current
  sequence other than the relayed one skips (`skipped moved`), otherwise the resume name is played forced.
- The native `AnimationEnded` broadcast runs only when the NPC+0x960 delegate has 0 listeners on the copy; with any
  listener it is skipped (counted both ways), so no client Blueprint state changes from the broadcast.
- The copy's own play clears its entry; a destroyed NPC's entry fails the weak check and is freed as skipped dead.
- Rows: `card565: client ... relayed X ended (state N, current Y): resumed|skipped dead|skipped moved <resume>` (12
  lines) and the totals row at map end and quit.

### Run c565b409a predictions (junior, 2026-11-21, scenario `tools/scenarios/c565-c1m1.txt`)

Both players about 600-700 uu from a ground-level `BP_AI_Trooper_SquadLeader_C`; four warrior bugs at the trooper;
60 s of fight.

- Client card565 totals: bound about equal to the client's relayed trooper plays (the card041 play rows on IsNPC2D
  troopers), unbound only for 3D bugs' plays outside the entry rule (0 trooper plays unbound).
- resumed > 0, and each resume row comes after its relayed sequence's end, within one sequence length of the play.
- skipped dead > 0 when troopers die in the fight (expected 0 to a few; the SquadLeader squad is god-free).
- native AnimationEnded called vs skipped: both recorded; prediction called = ends, skipped 0 (no 0x960 listener on
  client copies, b376 data).
- card132 mismatches 0; no WARN class new against c551b384a.
- Screenshots: the client's troopers walk or idle between shots, not frozen on an Attack01 or Flinch last frame.

### Run c565b409a result (junior, 2026-11-21, b409 `aee0d8aa0dee808f`)

- Client card565 quit totals: relayed plays bound 344, unbound 21 (all `BP_AI_WarriorBug_C`, 3D, outside the entry
  rule; 0 trooper plays unbound), entries made on a relay sequence 0; ends 181 (inside a relayed play 0), resumed 179,
  cleared by own play 32, skipped dead 0, skipped moved 2, pending 0; native AnimationEnded called 181, skipped 0.
  Every end row reports `end delegate 0 listeners` (case: no client listener on 0x960, native always called).
- Rows: each resume follows its Attack01 end on the same guid (troopers 0xca, 0x124, 0x12e, 0x178); resume names Idle,
  and Run for 0xca once (its locomotion before the attack).
- card132: 6 rounds, 0 mismatches. WARN classes against c551b384a: new only `WARN cmd` x2 per side (the pickup
  gotos stopped 157 uu short of an off-navmesh goal; both takes still happened) and host `WARN card553` x1 (the known
  armed-taker row, also in c569b39x runs). No card041 / card565 WARN.
- Screenshots `c565-*-client` / `-host` (prefix2 Screenshots): the SquadLeader stands in the idle pose between shots
  on both screens, not on an attack frame.

Predictions met. Waiting on the senior's review; then the user's play check of the trooper animations.

## Closed (senior, 2026-11-22)

The fix is proven by logs and runs, and the proof is in the sections above. Under the no-playtest-parking rule, a proven fix does not stay open waiting for a play check. It is on the playtest checklist in STATE.md. If the user sees it again in play, reopen this card with that report.
