# Card 444: Client AmmoMirror sends the whole ammo map as text 4 times a second while firing

Status: queued 2026-11-16 15:52 (senior, efficiency audit, card 329 class; sibling of card 441). After the playtest.

## Numbers (run c423b141a, client `netbudget` rows)
AmmoMirror is the client's largest upload op: 5818 B of the run's listed client sends (Round 1518, Ping 912,
Pong 912, HitClaim 564, Fire 398). Peak 5 s window 2588 B = 24 sends of about 108 B (518 B/s) during one Morita
magazine. Each send repeats every pair (`AmmunitionType.Morita 240 AmmunitionType.UnderslungShotgun 8
AmmunitionType.Nuke 0 @AmmunitionType.Morita 0 @AmmunitionType.UnderslungShotgun 8`) though only the magazine pair
of the fired weapon changed.

## Cause
`c72_client_step` (`mod/src/ubw/death.cpp`) builds the full text with `c72_map_text` + `c274_magazine_text` and
sends it through `chan_text_frame` whenever it differs from the last one sent, at most every `kC72MirrorMs` (250 ms).

## Do
Binary frame: one entry per pair = tag hash (u32, the same hash both sides compute from the tag text) + magazine
flag + count (u16). Send only the pairs that changed since the last acknowledged send, and the full set on a new
pawn, after a reload and every few seconds (a lost delta must not leave the copy wrong). The host applies entries
through the existing `c72_mirror_apply` / `c274_magazine_apply` paths; an unknown hash is a WARN and skipped.

## Done when
Client `netbudget` AmmoMirror under 150 B in the peak 5 s window of a full Morita magazine; host magazine and
reserve counts on the copy equal the client's at each reload (existing card 274 rows); digest 0 mismatches.


## Senior trace and patch (2026-11-16 22:43, static, not built)

What the frames are for, read from the code: the host re-applies the kept reserve map on each card 050 poll
(`C50Copy::ammo`), and `c274_magazine_apply` only raises the copy's magazine, on the frame only. So during a
magazine the reserve map is the same in every frame, and the falling magazine pair does one job: it lifts a copy
that fired more rounds than the client (the copy would run dry early and reload on its own, and client hits then
find no host round, card 276). That correction does not need 4 Hz.

Smaller change than the binary delta in "Do", same ownership (`c72_client_step`, `c72_receive_mirror`):

- Client: when the reserve map text equals the last full frame's and the magazine did not rise, the frame is the
  magazine pairs only (`@AmmunitionType.Morita 23`), at most once a second (`kC72MagazineMs`). A map change or a
  reload sends the full text at once, as before.
- Host: a frame that starts with `@` goes to `c274_magazine_apply` only; the kept map text is not replaced.

Patch: scratchpad `c444_patch.py` (`--check` passes on the b169 tree; 4 edits in `hooks_internal.h`, `death.cpp`,
`ammo.cpp`). The binary form stays undone: after this the op is about 5 short frames in 5 s.

Attacked by reading: map text shorter or longer than the last one (both read as changed); no magazine pair at all
(full frame path); a magazine-only frame held back by the 1 s limit (sent on a later 250 ms step, the last count
arrives within 1 s of the last shot); a relayed pickup (map changes, full frame, kept text replaced as before); a
frame the host gets for a pawn with no copy weapon (`c274_magazine_apply` returns, no WARN).

Done-when, restated for this form: client `netbudget` AmmoMirror at or under 500 B in the peak 5 s window of a full
Morita magazine (was 2588 B); client `card072 ... AmmoMirror sent: @...` rows at most 1 per second; a full row at
each reload with the host `card274: host copy magazine` row after it; 0 host `found no round` rows; digest 0
mismatches.

## Closed

Closed 2026-11-17 (senior), build b173 `dedb14d000d6ec36`: proven in b170, no regression through b173 gate c448 (0 mismatches).
