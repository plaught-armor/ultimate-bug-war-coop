# 469 — Former playtest waits proven by measured scenario steps

User rule 2026-11-16: only hearing waits on the playtest (cards 321, 451, 455, 314). Feel, numbers, "user sees" and
watch items get measured proofs now. State after the senior's log pass (2026-11-16 late):

| Card | Item | State from logs | Still owed (scenario step) |
|---|---|---|---|
| 320 | both leave the title screen into the same start | proven: `c460b170a` host `hold on` → client `GameStarted ... world paused 1` → `hold off after 9917 ms (all confirmed)` | shot pair at the release only; then close |
| 454 | client hurt shake | `card454 ... PlayHurtCameraShake` rows in c448/c460 b164–b170 | one count: client `card099 ... StartCameraShake Shake_Generic_Damaged*` rows = `card454` rows, shake class by loss size same as the host's own hits of that size; then it stays closed |
| 295 | no jitter after a revive | c448 b162–b170: `walk speed 700 ... (host 700, saved 700, attribute 700)`, `corrections in the 10 s after the revive: 0` — but the client stands still in those 10 s | c448: `client key W 3000` right after the `back to Up` wait, inside the 10 s window; same row must stay `0, worst 0 uu` (or under the count of a 3 s walk before the down, logged by the same counter) |
| 298 | client looks down in the mech | limits equal host after re-init (`-89.90/89.90`, c447b164a) | c447: client seated, `client look point` at the ground 200 uu in front of the mech's feet; wait on the client `aim:` row with ControlRotation P under -60; host row for the client's pawn (RemoteViewPitch / BaseAim P) within 2 degrees; host does the same in its own mech as baseline; shot pair |
| 317 | flare the same on both screens | b40b168a: 0 mismatches, at rest 0.0 uu; **6 hops in flight: card 470** | card 470's run |
| 319 | bug blips on the client minimap | c423b170a 78 rows, c447b164a 11 rows: hostile gathered = hostile in range except single-sample lags; every `missed` class (5 Trooper, 1 Warrior, `capsule ... responses none`) is missed on the host too (3 Trooper rows, same capsule values): the game's own filter, not a client gap | C4M1 or C2M1: one client sample pair seated in the mech and one on a turret, each waiting on `hostile [1-9]` with gathered equal; shot of the client minimap with red blips |
| 226 step 2 | queue backlog numbers | none under load | one c448 pass with the client capped at 20 fps (`t.MaxFPS`), read the `netbudget`/queue rows; a backlog above 0 for over 1 s gets the two-lane queue, none closes step 2 |
| 274, 276 | watches | gate c448 covers both (0 `found no round`) | none |

Rules: one combined run per map where steps share a map (C1M1: 295, 454, 317/470, 320 shot, 226; C4M1: 298, 319).
Each step waits on its log row. Shots viewed by the junior. Budget 3 runs.

## Scenario steps written by the senior (2026-11-17)

Card 298 look-down, `tools/scenarios/c447-c4m1.txt`, after the line `client shot c298-mech-after-dash` (client
seated in the mech, host on foot). The `c20[...]` aim row is on-foot only (attributes.cpp:910), so the measured
value is the smooth look itself: it turns by real mouse axis events and ends with `smooth turn done` only when the
view rotation is within 1 degree of the target (testrun.cpp:842); a pitch clamp shows as
`WARN cmd: N look: yaw ... off, pitch X off at the timeout` with X = the missing degrees.

```
# Card 298 (a): the client looks down in the mech by mouse input. Negative pitch is down (c106-combined line 51).
client mark
client look 0 -70 smooth
client wait log "cmd: \d+ look: smooth turn done" since-mark timeout=6
client shot c298-mech-look-down
host shot c298-mech-look-down
client mark
client look 0 60 smooth
client wait log "cmd: \d+ look: smooth turn done" since-mark timeout=6
client shot c298-mech-look-up
client look 0 0 smooth
```

Pass: both `smooth turn done` rows, 0 `WARN cmd: ... look: ... off at the timeout` after the mark, the client shot
shows the ground under the mech, the host shot shows the client's mech torso or guns pitched down. The same three
look lines with `host` in the host's own mech block (after its `key W 3000`) give the baseline. If the `smooth` form
is refused while seated, use the plain `client look 0 -70` and read the next `card298` row.

Card 295 walk after revive, `tools/scenarios/c448-c1m1.txt`, after the client's
`card156 ... back to Up (revive from Downed)` wait and before the `+5 s` wait: add `client key W 3000 timeout=10`.
Pass: the existing `corrections in the 10 s after the revive: 0` row, 0 `WARN card295`, walk speed row 700.

## Results (senior, 2026-11-17)

- 295: proven, c448b173a/b (walk inside the 10 s, 0 corrections, 0 WARN card295).
- 454: proven, c448b173b (2 `card454` rows, 2 `Shake_Generic_Damaged*` rows, same ms).
- 317: proven by card 470, b40b173a.
- 298: proven, c447b173a (6 `smooth turn done` rows, look-up and look-down shots on both sides).
- 319: proven, c447b173a (client in mech: hostile 8 gathered 8; on turret: hostile 9 gathered 9; minimap shot).
- Still owed: 320 release shot pair, 226 pass with the client capped at 20 fps.
- Side finding: c447b173a has 4 `WARN card140: client hit ... host location ... from the copy's ReplicatedMovement`
  rows (WarriorBug, C4M1); card 140 stays open for them.

- 320: proven, c469b174c (FED_C2M1 shot pairs inside the hold and after release). Card closed.
- 226 step 2: proven, c469b174a at 20 fps (no backlog in play). Card closed.
- The four `WARN card140` rows are card 474 (held frames read as fresh). All rows of this card are done; it closes
  with card 474's build.

## Closed

Closed 2026-11-17: every former playtest wait except hearing (cards 321, 451, 455, 314) has a measured proof
above; the one bug the proofs found (stale hit rows after a client hitch) was fixed and closed as card 474 on
b175 `63689adfcb1ec80e`.
