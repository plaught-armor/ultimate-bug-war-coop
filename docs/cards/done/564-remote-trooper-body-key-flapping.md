# Card 564: Remote player trooper body flips animations every 100 ms

User report (2026-11-21): "sometimes the troopers have the wrong animations also" — both the other player's trooper
body and NPC troopers. This card is the remote player body (card 028). NPC troopers are card 565.

## Evidence (play logs of 2026-11-20, b366)

- Host log (`ultimate-bug-war-coop-public/logs/UBWCoop-menu-20261120-184651.log`): 575 `Walk -> Run` and 575
  `Run -> Walk` card028 rows, often 100 ms apart (one step), speeds 650 to 815 while the client walked (walk 700).
  `kC28RunSpeed` 750 sits inside the walk's sample noise: the host speed is the location change over one 100 ms step.
- Client log (`game/StarshipTroopers_UBW/Binaries/Win64/UBWCoop-menu-20261120-184517.log`): many keys held under
  150 ms (52 Walk, 44 Attack01, 25 Attack01 -> Idle). Every change restarts the sequence at frame 0, so the body
  stutters between poses.

## Fix (b371, `baeebee09d3e7f59`, build-b5 copy kept)

`remote_body.cpp` `c28_pick` / `c28_drive`:
- Run reads a smoothed speed (`kC564SpeedBlend` 0.5 per 100 ms step), with a band: enter above 750, leave below 720.
- A started Idle, Walk, Run, Flinch or Attack01 holds at least `kC564HoldMs` (300 ms) before another of those replaces
  it. Death, Corpse and Standup never wait.
- The card028 key row now logs `avg` (the smoothed speed).

## Prediction

C1M1 run with the client walking (no sprint) 10 s in view of the host, then sprinting 5 s: the host log shows at most
2 key changes for the walk (Idle -> Walk, Walk -> Idle) and no key held under 300 ms except into Death01; one
Walk -> Run and one Run -> Walk for the sprint. Same on the client for the host's walk.

### Run c551b376a predictions (junior, 2026-11-21, b376 `eb49926909111446` = b375 + card 568, moved from b372, scenario `tools/scenarios/c373-c1m1.txt`)

Host watches the client from about 400 uu. Client walks (W, no sprint) 10 s, then sprints (LeftShift + W) 5 s.
Host card028 rows for the client's body: walk, at most 2 key changes; sprint, one Walk -> Run and one Run -> Walk;
no key held under 300 ms (except into Death01); rows log "avg".

## Walk proof c584b432a (b432 `06c5597f6e2354bb`, FED_C1M1 FAST, 2026-11-21)

- The client held S 5 s in the host's view. Host card028 rows for the client body after the host mark (host log line
  5726): `Idle -> Walk` at t=180975727 (speed 239), `Walk -> Idle` at t=180976728 (speed 0). The client backed into
  the dropship after 1 s (host shot host-c584b432a-c564-after-sprint-host.png), so the walk ended there. One change in,
  one out, Walk held 1001 ms: no flapping.
- The sprint step was malformed (`key W 2000 LeftShift 2000`, chord at_ms must be below hold_ms) and did not run.
  The sprint rides c590b432a (`key W 2000 LeftShift 50`, open ground, bugs cleared).

## Sprint check from c594b436a/b/c (b436, 2026-11-21): no flapping, sustained sprint not yet shown

- No card028 key for the client body was held under 300 ms in any of the three runs, and there was no Walk/Run flapping.
- Each sprint was cut short, so "one Walk -> Run, one Run -> Walk" over a real sprint is not proven yet. The step
  sprints straight at the host from 600 uu, so the client reaches the host body in under 1 s. Run a: `Walk -> Run`
  (avg 846) at host t=185168839, then `Run -> Idle` 301 ms later (host log line 19087). Run c: the client's GA_Sprint
  activated at 185788068 (MaxWalkSpeed 1050) and ended by itself at 185788168 (EndAbility, not cancelled), so the
  host saw only Idle -> Walk -> Idle.
- Next: a sprint across the host's view on open ground (3 s, sideways at about 1200 uu), carried in the b437c C3M1 run.

## Sustained sprint: c593b437ca (b437c, FED_C3M1, 2026-11-21)

Logs: `logs/c593b437ca-host.log` (sprint window from line 6113), shot `c564-sprint-host`.

- The client walked 3000 uu on the navmesh holding LeftShift (`client goto ... sprint`) across the host's view.
- Host card028 rows for the client body: Idle -> Walk at 187569054, Walk -> Run at 187569705 (speed 1315, avg 966),
  Run -> Walk at 187571667 (arrival slowdown), Walk -> Idle at 187571997. Run was held 1962 ms, with one change in
  and one change out. No key was held under 300 ms. The key does not flap.
- Side finding: for the first 300 ms the host's server copy of GA_Sprint activates and ends every frame (20 pairs,
  187569016 to 187569333). The first activation reads `GetLastInputVector` as zero, so `IsMovingForward` is false
  and the ability ends itself, and the held input activates it again on the next frame. Only one pair set
  MaxWalkSpeed (1312). The body key was not affected. This is vanilla sprint logic meeting a held key before
  movement input arrives, not a sync gap.

## Closed (senior 2026-11-21)

Passed in c593b437ca: one Walk -> Run -> Walk change pair, Run held 1962 ms, no key held under 300 ms. The GA_Sprint
activate/end pairs on the server copy in the first 300 ms do not reach the body key; they are noted here, not carded.
