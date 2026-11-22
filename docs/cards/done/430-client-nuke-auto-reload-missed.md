# Card 430: client nuke launcher misses its auto reload after a shot

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: open, diagnostic in b128 (`c430_reload_try`, attributes.cpp). Found 2026-11-16 in run `c423b127a`
(b127 `74c1fdca882992af`, scenario `tools/scenarios/c423-gates-c2m1.txt`); it voided the run from line 190.

## What happened (tick ms, client log unless noted)

- Shot 1 at nest `_C_0`: SpoolUp 489960086, Fire 489962097, SpoolDown 489963096, WindDown AnimationEnded 489964097.
  AmmoMirror after the shot: `Nuke 1 @Nuke 0` (1 in reserve, magazine empty).
- Host server copy reloaded on its own at 489964107 (`card021: host AbilityInputTagPressed InputTag.Reload`).
- The client's `BP_WeaponReloadAbility_NukeLauncher_C` (handle 157) did not activate: no `card276` row until
  489975831. In run `c423b126a` the same point gave: client WindDown end 489372042, host reload 489372056, client
  reload 489372061.
- Ammo pickup 489969249 (`Nuke 1 to 2`): still no reload.
- Second press sent 489971819. The client started nothing for 4012 ms. The host copy (magazine 1 after its own
  reload) ran SpoolUp 489971823, Fire 489973807 with no FireProjectile row, SpoolDown 489974824.
- Client reload activated 489975831 (7 ms after the host copy's SpoolDown end), ended 489977747; windup 489977763;
  the scenario's 6500 ms hold released 489978329: `WARN card296 ... fire input released inside the windup`.
  No second shot, `LOS_O02_SO05` stayed 1 of 2, every later objective step cascaded.

## What is not known

- Whether the client tried the reload at its WindDown end and failed, or never tried. The `card021: client local
  activation` rows were capped (11 rows) before that point.
- Why the client's reload followed the host copy's SpoolDown end in both late cases (489372061 and 489975831). One
  reading: state replicated from the host copy (an owned tag of its spool or fire ability) blocks the client's reload
  until the copy's sequence ends, and the client's one try at its own WindDown end lost a 10 ms race in b127. Not
  proven. With real latency the host copy trails the client by more, so this could be the common case in a playtest.

## Next

1. b128 diagnostic: `card430: client t=... reload try handle N <class> = 0|1, ActiveCount, weapon ASC owned tags,
   pawn ASC owned tags` on every change. Read the rows from the client's WindDown end onward:
   - a `= 0` row right after WindDown end: the tags on it name the block; trace which side adds that tag.
   - no row at all until the late one: the client never tried; find the Blueprint trigger of the auto reload.
2. Scenario (same step): before each second nuke press, wait on the client's
   `AnimationEnded, key NukeLauncher_Reload, actor BP_Weapon_NukeLauncher_C` log row since the mark.
3. Player-facing check: after a client nuke shot with reserve held, the launcher must be loaded within 3 s of the
   WindDown end with no key press. Done when 5 of 5 shots in one run show a `card276 ... reload press and release
   sent` row within 100 ms of the client's WindDown end.

## 2026-11-16 12:42: b129 rows (`c423b129a`), cause and fix (senior)

Not game behaviour: a mod-visible client bug. The client's own reload try at the WindDown end returns 0 while the
pawn ASC still owns `WeaponSystem.Blocked` and `WeaponSystem.EquippedWeapon.SpoolDown`; the host's server copy
reloads on its own 10 ms after its WindDown end. Rows:

- Shot 1: `card430: client t=492359867 reload try handle 157 BP_WeaponReloadAbility_NukeLauncher_C = 0, ActiveCount
  0`, pawn tags include Fire, Recoiling, Blocked, SpoolDown. No further try for 27.7 s, until the scripted R at
  492387584 (= 1, with Fire and Recoiling still owned, so those two do not block the reload).
- Shot 2: failed try at 492395584 (Recoiling, Blocked, SpoolDown), 3 repeats, then the game's own try goes through
  at 492396601 (+1017 ms).
- The "+1" pickup added nothing (reserve already 2, "Nuke Rounds full"): the earlier pickup reading is void.

Reading: the block tags outlive the client's local ability end (they are held by the replicated host copy), so the
single try at the WindDown end fails on any real latency; whether the game tries again is not dependable (shot 1
never did). Not known: what drives the game's second try on shot 2, and why the Fire tag stays owned 27 s
(`WARN card296: client spool-up ... ended without a fire activation`, own item).

Fix (patch `c430_reload_retry_patch.py`, for b131): a failed client try of the own weapon's reload ability with
`c352_blocked(pawn, kFireReload) > 0`, no active copy and not a server request arms a retry
(`c430_note`, attributes.cpp). `c430_retry_step` (client branch of `c24_step`) calls `TryActivateAbility` again
once the Blocked count is 0: at most 5 tries, 100 ms apart, within 3000 ms; a success of any try (the game's or
the mod's) ends it; a weapon change ends it with a row. The success runs through the same detours, so
`c276_reload_activation` sends the reload press and release to the host as for any client reload. A try that failed
with no Blocked tag (full magazine, no reserve) is not retried.

Prediction (FED_C2M1 `c423` scenario, R press kept as a fallback so the run is not voided): after each nuke shot a
row `card430: client t=... reload retry 1 (handle H) = 1, N ms after the try that failed under WeaponSystem.Blocked`
with N under 1500, followed by `card276 ... reload press and release sent`; 5 of 5 shots reload with no R press
used (the scenario's R press finds the reload already running); 0 `WARN card430`. If retry rows print `= 0` five
times with Blocked at 0, a second tag gates the reload and the row's tag list names it (SpoolDown first suspect).


## 2026-11-16 16:28: diagnostic rows read (b144, run `c423b144a`, C5M2)

3 of 3 client nuke shots: the reload activated 1, 0 and 18 ms after the client's WindDown end. The third shows the
block: at the WindDown end the try returned 0 with pawn tag `WeaponSystem.Blocked` owned (the row repeated 59
times, so the game retries every tick), and it returned 1 at +18 ms, the first tick without that tag. So the client
does try, the game keeps trying, and `WeaponSystem.Blocked` on the pawn ASC is what holds it. The b127 miss is then
that tag staying on for 11.7 s; the open question is narrowed to which side keeps `WeaponSystem.Blocked` on the
client pawn after the client's own SpoolDown (candidate: the tag replicated from the host copy's spool sequence,
which card 437's press carry now keeps in step). Not reproduced since b128; done-when stays 5 of 5 in one run, next
read on any run with 5 client nuke shots. If a miss shows again, the `= 0` rows name the tags at that moment.

## Closed 2026-11-16 20:00 (senior)

Measured over every `c423` client log from b131 (the reload retry fix) to b159: 40 client nuke shots in 18 runs.
For each `AnimationEnded, key NukeLauncher_WindDown` row, the time to the next
`card430: client ... reload try ... NukeLauncher_C = 1` row: 0 to 119 ms, 0 misses, 0 `WARN card430`. Last
miss: `c423b130a` (30016 ms, before the fix). The done-when asked for 5 of 5 in one run; no scenario fires 5,
and 40 of 40 across runs is the stronger proof, so the card closes on that. Reopen on any play-log
`WARN card430` or a reload try that stays `= 0` past 1500 ms.
