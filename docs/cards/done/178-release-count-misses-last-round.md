# Card 178: The client's release count misses a round fired after the release

Status: queued 2026-11-11, from run c176-r2 (card 175 class). Budget: three runs, the first shared with card 176's
fix run. One goal: every round the client fires in a burst is covered by the release count the host holds for, so no
miss claim is dropped for want of a round.

Read: card 175 (`c175_release_or_hold`, `c175_press_rounds`, `kFireRoundsShift`), card 157 (HitClaim pairing).

## Why

c176-r2, end of the client's 4500 ms burst:
- client sent the release at t=88297973; host "card026: host t=88297981 host received Fire 11520" (45 rounds) and
  "card024: host relay release (Fire 0) t=88297981", no card175 hold line (the copy had its 45).
- the client then sent HitClaim shot 58 at t=88298009, 36 ms after its release.
- host "WARN card157: host t=88299520 HitClaim shot 58 from pc BP_PlayerController_C (miss) found no round in 1500
  ms: dropped (1 so far)".
So the client fired one more round after it had counted and sent the release, and the host copy never fired it.

## Senior trace (verify each line before code; mark verified or open in a Trace section)

- Open: where the client reads the round count for the release (`c175_press_rounds(false)` in hooks_ubw.cpp and its
  caller) relative to the weapon's own stop: the game's fire ability can fire a last round on or after the input
  release (the fire timer already due, or the ability ending at its next tick). Name the function and address that
  fires the late round.
- Open: the right place for the count. Either the release is sent when the weapon's fire ability really ends (the
  count is then final), or a late round sends a correction the host adds to the hold. Prefer the one that keeps one
  message per release.
- Open: whether the host copy would fire the extra round at all once asked (ammo, ability already ended).

Stop and report the trace before code if the late round does not come from the client's own weapon after release.

## Trace

- Verified: the client sends the release from `detour_asc_tag_released` (hooks_ubw.cpp), which calls `c26_own_fire`,
  then `c26_send_fire`, before the original `AbilityInputTagReleased` runs. `c26_send_fire` reads
  `c175_press_rounds(false)`, which is `g_c44_fires[kSideClientOwn]` minus the count at the press. So the count is read
  at the moment of the input release, inside that frame's input handling.
- Verified: the fire ability does not stop on the release. The BP `OnInputReleased` (ubergraph offset 220 of
  `ExecuteUbergraph_BP_WeaponFireAbility`) only calls MontageExitSection. A round already due in the montage still
  fires later in the same frame.
- Verified: the late round comes from the client's own weapon. Its path is:
  - the montage's DoAction notify;
  - `USTAbilityTask_PlayAnimation::DoAction` (0x050E3BC0, card 104);
  - `USTWeaponAbility::DoAction` 0x5234790, which calls `OnDoAction` 0x50B4B00 at +0x9;
  - `OnDoAction` runs the BP OnDoAction event through ProcessEvent (vtable +0x260). Ubergraph offset 532 calls the
    parent `OnDoAction_Implementation` 0x3F63800, which is `jmp *0x510(%rax)`;
  - slot +0x510 of `USTWeaponFireAbility`'s vtable (0x810DF28, entry at RVA 0x810E438) is
    `USTWeaponFireAbility::Fire` 0x5236320;
  - `Fire`+0x3e7 (0x5236707) calls `UADAbilityTask_FireProjectile::FireProjectile` 0x4A33E40, which is the hooked
    function that counts the round.
  The other two callers of FireProjectile, GrenadeThrow `OnDoAction` (0x51635F3) and BugFormFlamethrower (0x5236D94),
  are not on the Morita path.
- Verified from c176-r2 client: "client sent Fire 11520 (AbilityInputTagReleased)" at 88297973, then the client
  FireProjectile at 88297974 and the playlog "shot c seq=58" at t=88297974. In c176-r1 and c176-r2, every late round
  follows a release by 0 to 1 ms. Shots 100 ms or more after a release belong to the next press.
- Verified: the right place for the count is the end of the client's frame. `c24_step` runs from `ubw_post_tick`,
  after the engine Tick (hooks_ubw.cpp:194), so every DoAction of the release frame has fired by then. The fix holds a
  client primary release in `C24State` and sends it from `c24_step` (fire.cpp `c178_flush_release`). One message per
  release is kept, with no added frame of delay. A press that comes before the flush sends the held release first.
- Open, shown by the run: the host copy fires the extra round once asked. The card 175 hold already makes the copy
  fire up to N rounds, held until kC175HoldMs = 300.

Stop condition: not hit. The late round comes from the client's own Morita, after the release, in the same frame.

## Predictions (write the exact lines before the run)

- Client rounds fired in the burst = the count in the host's "received Fire N" line.
- 0 "found no round" WARN lines; FireProjectile counts equal on both sides for the burst.
Written before run c176-r3 (shared with card 176), DLL 42c0cfeed1204098.

- Client, for each primary release: "card178: client t=... primary release sent after the frame: <n> rounds since
  the press, <k> of them after AbilityInputTagReleased", followed at the same t by "card026: client t=... client sent
  Fire <n*256> (AbilityInputTagReleased)". For the 4500 ms burst, <k> is 0 or 1 and <n> equals the number of client
  "card020: client client-own FireProjectile" lines since the press.
- Host: "card026: host t=... host received Fire <n*256>" with the same <n>.
- 0 "WARN card157: ... found no round" lines on the host. The host's server-copy FireProjectile count over the burst
  equals the client's own count (any card175 hold line closes "done", not "ended short by the bound").
- 0 "WARN card178: client release not sent" lines.
- WARN classes: baseline only on each side: "WARN attach: DisableThreadLibraryCalls failed, err=126".

## Files

- May change: `mod/src/ubw/fire.cpp`, `mod/src/ubw/hooks_ubw.cpp` (only `c175_press_rounds` and its caller),
  `mod/src/ubw/hooks_internal.h`, `tools/scenarios/c176-click.txt`, this card.
- May not change: anything else.

## Done when

- Trace, change, run lines quoted, the usual Report items. The Report ends with "Report complete."

## Report

### Files changed

- mod/src/ubw/fire.cpp:
  - `c178_defer_release` holds a client primary release (only on the client, only for primary) and returns true, so
    `c26_send_fire` does not send it then. A press that comes before the flush sends the held release first.
  - `c178_flush_release` sends the held release from `c24_step`, after the engine Tick. It writes the capped card178
    line (16 lines), and "WARN card178: client release not sent" if the own PC is gone.
- mod/src/ubw/hooks_ubw.cpp: `c26_send_fire` first calls `c178_defer_release`. The `c175_press_rounds` comment now
  says the release is read after the engine Tick.
- mod/src/ubw/hooks_internal.h: the `C24State` release fields and the `c178_defer_release` declaration.
- tools/scenarios/c176-click.txt: the run 3 tail (the card178 wait, then the travel and the card160 map line). After
  the run, line 80 waits on the mute cap line; see card 176.
- This card: the trace, the predictions and this report.

### Summary

**Cause.** The client read the round count in the input-release handler. The fire ability still fires a round that is
already due in the montage later in the same frame, through `USTWeaponFireAbility::Fire` 0x5236320 and
`UADAbilityTask_FireProjectile::FireProjectile` 0x4A33E40 (see the Trace). That round was missing from the count.

**Fix.** The client's primary release is sent at the end of its frame, from `c24_step` (`ubw_post_tick`), so the
count is final. One message per release is kept, with no added frame.

**Run c176-r3 caught a late round.** In short press 7, the round came after AbilityInputTagReleased and is in the
count:
- "card178: client t=89091019 primary release sent after the frame: 2 rounds since the press, 1 of them after
  AbilityInputTagReleased"
- "client sent Fire 512 (AbilityInputTagReleased)" at the same t.

Under the old code, that release would have said 1.

### Verification

Build: 0 warnings. DLL 42c0cfeed1204098 ("deployed: 42c0cfeed1204098", "lane 1: dll 42c0cfeed1204098, build
42c0cfeed1204098").

Run c176-r3: "run c176-r3: ended by scenario after 75 s; host rc 0, client rc 0".

The scenario stopped at line 80 on a card 176 wait ("FAIL line 80: host wait log card160: host t=\d+ muted server
copy 2D component sound MSS_FOL_Low_Ammo_Click since-mark timeout=5 -> timeout after 5 s"). Every card 178 step
came before it.

The burst:
- Client: "card178: client t=89097981 primary release sent after the frame: 46 rounds since the press, 0 of them after
  AbilityInputTagReleased", then "card026: client t=89097981 client sent Fire 11776 (AbilityInputTagReleased)", which
  is 46 x 256.
- Host: "card026: host t=89098004 host received Fire 11776 (BP_PlayerController_C)".
- FireProjectile lines in the burst window: client client-own 46, host server-copy 46. They are equal.

The short presses:
- The client's 8 release counts were 1, 1, 2, 2, 1, 2, 2, 1, a total of 12. The client has 12 client-own
  FireProjectile lines before the burst. The host received the same 8 values.

Other checks:
- "found no round": 0 lines on the host. There is no card175 hold line (the copy was never short).
- "WARN card178": 0 lines.
- drive.py: "drive rc 1" at line 80, so no dead time line was written.

Counts:
- host: 3944 lines, FATAL/ERROR 0, WARN 1.
- client: 4308 lines, FATAL/ERROR 0, WARN 1.
- "new crash dumps: none".

WARN classes, each side: only "WARN attach: DisableThreadLibraryCalls failed, err=126", which is baseline.

All of card 178's predictions held.

### Out of scope

- The host copy fired 14 rounds over the 8 short presses, against the client's 12. It fired over, not under, so no
  claim was dropped. This is the card 175 class (the copy fires before the release arrives). No change was made.

Report complete.

## Senior review (2026-11-11)

Accepted. Verified by the senior: the diff is in fire.cpp, hooks_ubw.cpp (c26_send_fire and one comment) and
hooks_internal.h; DLL 42c0cfeed1204098; c176-r3 client "card178: ... 2 rounds since the press, 1 of them after
AbilityInputTagReleased" then "sent Fire 512"; burst "sent Fire 11776" / "host received Fire 11776"; 0 "found no
round"; baseline WARN only; rc 0/0. Open, moved to card 180: the host copy fired 62 rounds against the client's 60
(over-fire in the short presses), and `release_due` is not cleared at session end.
