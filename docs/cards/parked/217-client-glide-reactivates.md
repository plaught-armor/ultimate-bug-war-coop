# Card 217: The client's Glide re-activates every 50 ms with no key held
Status 2026-11-22: parked, out of scope (bug mode, user rule 2026-11-19). The status below is as first written.

Status: queued 2026-11-12, from run c212-r1 (DLL `6d7f5193580ad2cd`). Static first; one run only if a fix follows.

## Why

c212-r1, client log from 126718811: `card024: client local activation handle 203 = 1` (Glide) about every 50 ms
while airborne, the driver holding no key. The lines filled the card024 cap (120 of 120 at 126725087). While
airborne the presses alternate handles 203, 204 and 205, so the card 214 fail-run pair resets on every fail (8
"(fail 1 in a row)" lines in 0.25 s).

## Do

1. Static: what re-activates Glide (the game's own airborne loop, a stuck pressed state in the mod's input path,
   or the mod's relay echoing the server copy's activation back). Check whether the host sees the same rate for
   its own Glide, and whether each client activation sends a row to the host.
2. If the game does it: cap the success lines of a repeating handle the same way as fails, and hold the run count
   per handle in a small fixed table (no heap). If the mod does it: fix at the cause.

## Done when

- The cause named with file and line, the fix or the cap, and the rows per second sent for a held Glide. The usual
  Report items. It ends with "Report complete."

## Step 1 evidence (senior, logs of c212-r1, 2026-11-13)

- Client: 101 `local activation handle 203 = 1` rows; the first two are 16 ms apart (126717696, 126717712), so
  the rate is one activation per client frame at the start, not 50 ms.
- Host: each one arrives as a client request and activates the server copy (`card105: host server-copy
  activation handle 203 ... path client request`, 126717706 and 126717722; 276 Glide lines in the host log). So
  every re-activation sends a request to the host: the cost is real traffic, not only log lines.
- Host row `card024: host server-copy InternalServerTryActivateAbility handle 203 ... ActiveCount 0,
  InputPressed 1`: the spec's input reads pressed, and the ability is not active when the next request comes.
  So Glide ends at once and is started again while its input is held.
- The log line is written at `input_diag.cpp:1279` (inside the client's `InternalTryActivateAbility` detour);
  the mod does not start the activation there.

Most likely cause (to confirm, not yet verified): the scenario's jump press. The driver's `key` verb calls
`APlayerController::InputKey` (card 222); a press with no matching release leaves the jump input pressed, and
the Hopper's Glide is bound to "jump held while airborne". Then this is the game's own loop under a held key
and the card's title ("no key held") is wrong.

Junior checks, static, in this order:
1. `tools/scenarios/` file of c212-r1: the key lines before 126717696 (which key, press or hold, release line).
2. `logs/c212-r1.drive`: the release time of that key against the last `handle 203 = 1` row.
3. If a held key explains it: does a human hold give the same rate? The host's own Glide in the same run (host
   log, `ability slot` rows for the host pawn) answers it. Same rate on the host's own pawn: game behaviour.
4. Fix, game behaviour case: no gameplay change. (a) `c24_log_local_activation`: cap repeated success lines
   per handle (a fixed table of 8 handles with a run count; log at runs 1, 10, 100, as `kC24FailRunLogs` does
   for fails) so one handle cannot spend `kC24LocalLines`; (b) the fail-run pair of card 214 keys its count per
   handle in the same table. (c) State the requests per second sent to the host for a held Glide; if above 30,
   propose (do not code) a client-side skip of a repeat request while the host has not answered the last one.
5. Fix, mod case (key was released and activations go on): find who sets the spec's `InputPressed`
   (`input_diag.cpp` and `fire.cpp` writers of the spec input flag) and fix there.

Priority: bug campaign only (Hopper form); after the Trooper playtest.

## b192 note (2026-11-17 16:37)

Log code in b192 (`input_diag.cpp`, fixed table `C24Run g_c24_runs[8]`, rows at runs 1, 10 and 100 per handle). From c212-r1: client SpaceBar held 100 ms, yet client Glide handle 203 logged 96 rows over 7.3 s that kept going after the release (per second 2, 16, 19, 20, 7, 5, 20, 7; gaps of 1.1 s and 1.4 s). The host server copy logged 110 card105 rows on handle 203 over 8.1 s (about 13.6/s). Lead: the server spec's InputPressed stays 1 after the release (step 5 mod case), not yet checked. Caveat: success rows now appear only at 1/10/100, so since-mark waits in c127/c212 must wait on those rows. Fix those scenarios when the bug campaign resumes (Trooper first).
