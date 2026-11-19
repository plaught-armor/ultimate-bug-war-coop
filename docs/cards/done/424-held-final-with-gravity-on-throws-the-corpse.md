# 424: a held final with gravity on throws the corpse

## Finding
`WARN card140: client pose freeze check guid 0xdc snap 2: worst body error 0.0 uu after the apply, 295.9 after the
freeze call` (also guid 0xe6, 344.9 uu) in `logs/c418b121a-client.log`. The two corpses are the run's two Pose digest
mismatches (7 and 12 of 15 bodies over 100 uu).

Cause: `pose_try_apply` (`mod/src/ubw/pose_c132.cpp`) holds a final for one tick. A streamed copy already has gravity
off. A copy with no stream (a corpse that came back into relevancy: new copy, row no longer `grav_off`) simulates its
held tick with gravity on (`card152: client stream final ... held one tick (gravity on)`).

Last 40 client logs: held tick with gravity on, 3 bad of 10; gravity off, 0 bad of 44. Bad logs: c400b89a,
c408b113a, c418b121a.

## Fix
In `pose_try_apply`, before `pose_apply_bodies`: `if (hold && !r.grav_off) { pose_grav_off(r, mesh); }`.
`pose_freeze_step` turns gravity back on. Patch: scratchpad `c424_c279_patch.py`.

## Proof
Chain run: every `held one tick` row reads `(gravity off)`; 0 `WARN card140: client pose freeze check`; 0 Pose digest
mismatches. If a WARN remains with gravity off, the cause is elsewhere (awake bodies pushed out of the floor) and the
method changes.

## 2026-11-16: residual on reconnect corpses, repose patch (senior)

Not proven on b128/b129. C2M1 reconnect: every copy whose freeze waited a tick for bone 0 (`card191: ... freeze
waited 1 tick(s)`) ended off the host: c423b128a 5 of 7 waited copies (294.7-674.7 uu), c423b129a 9 of 9
(302.5-1061.6 uu). All rows read `(gravity off)`, `0.0 uu after the apply`, the error already there `after the
freeze call`, so the bodies move during the waited tick while simulating, not at the freeze. The 9 guids of b129 are
exactly the 9 `Pose` mismatches of digest round 9 (0xca8, 0xd18, 0xba6, 0xc9e, 0xa08, 0xd22, 0xc68, 0xbcc, 0xb9c);
none of them has a card 269 rest play row, so that digest WARN is this card, not card 269. No waited row exists
outside the reconnect in either run (first arrivals freeze after one tick with 0.0 uu).

What moves them is not logged (the apply ends in zero velocity and sleep; gravity is off). Fix, cause-independent:
`pose_freeze_step` also checks the bodies against the host's row before the freeze; when they are over
`kPoseFreezeTolerance` it poses them again (`pose_apply_bodies`) and waits one more tick, inside the existing
`kC191FreezeWaits` bound. Patch: scratchpad `c424_repose_patch.py` (pose_c132.cpp only).

Prediction (C2M1 with the reconnect): `card424: client guid ... held tick moved its bodies ... posed again` rows for
the waited copies, each with `freeze wait 1` (one repose is enough); 0 `WARN card140: client pose freeze check`;
digest round after the reconnect 0 `Pose` mismatches. If a copy reposes on every wait up to 10 and still WARNs, a
per-tick force moves it (corpse-on-corpse contact is the first suspect) and the method changes to holding the copy
without a simulating tick.
