# 498: FED_C4M1 stadium Gigantic C_1 closes while a player is outside

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: open (written 2026-11-18, senior, from c492b250a). Batch; no run of its own.

## Finding

Card 492 fix 6 (b249) holds the `Add Stadium_SO2` closes until every living player is on the inner side of each
door. In c492b250a, Medium C_1 and C_2 logged "close held: 1 of 2 players inside" and then "closing: all 2 players
inside after 5664 ms", but `BP_Door_Sliding_Gigantic_C_1` went DoorState 1 -> 3 at 691242865 and 3 -> 0 at
691246859 with the host outside the stadium at (-2600, -30320). Card 288 decides inside by the player's half-plane
along each door's local x from the trigger point (the C_5 presence centre); the host stood on Gigantic C_1's inner
half-plane while outside the stadium volume.

## Fix

For an objective trigger with a presence (card 492 fix 6 rows), the inside test is overlap of that presence
(`c227_count` on BP_PlayerPresence_C_5), not the per-door half-plane: a held door closes when every living player
overlaps the presence. Presence-triggered holds keep the half-plane test.

## Proof

The c492 stadium block: all three doors log "close held" while one player is outside, then "closing: all 2 players
inside" after the walk-in; host and client door states equal in the digest.

Reopen when: a player is shut out of the C4M1 stadium.

## Presence test failed (b251/b252, 2026-11-18 20:36, senior)

The presence overlap design was wrong: BP_PlayerPresence_C_5 is a box at (-3250, -21800, -1200) with extent
(13000, 12000, 5000), so it spans y -33800 to -9800 and reaches about 4000 uu past the doors (Medium doors at y
-29720 and -31072). A player outside the doors at y -30320 overlaps it (overlap list and shape test agree). In
c492b251a/b252a every trigger read 2 of 2 inside and all three doors closed unheld; the soak also stopped at
Stadium_SO2 (b250 went on to Metro). Not for lane 1.

Fix (replaces the one above): a group half-plane test. The doors that one objective trigger closes form one hold
group. Every door in the group stays held until every living player is on the inner side of every door in the
group, with the inner side taken from the trigger point (the C_5 centre, as in b249). Presence-fired holds keep
the per-door test. Drop the presence overlap count.
Proof: the stadium block shows all three doors "close held" while the host stands 600 behind Medium C_1, then
"closing: all 2 players inside" after the walk-in, and the soak goes on past Stadium_SO2 as in b250.

## Closed (2026-11-18 21:03, senior, b253 `168fd79b995e1d9c`, c492b253a)

Fix: group half-plane hold (door_c161.cpp: C288Trigger/C161Door group, c288_hold_group reopens and holds the other
doors of the group; per-pawn inside test over every door of the group). Presence overlap count removed.
Evidence (host log, read by the senior): at 695219154 Medium_C_1 and Medium_C_2 "close held: 1 of 2 players inside
(outside ... outer side of BP_Door_Sliding_Medium_C_1)", Gigantic_C_1 "close held: group 4 with Medium_C_1
(reopened)"; at 695224798 all three "closing: all 2 players inside after 5644 ms". 0 EXCEPTION/FATAL, 0 card132
mismatches; progression to Metro_SO1/SO2 as in b250.
Not done: c448 gate on b253 still owed before lane 1.
Reopen when: a player is shut out of the C4M1 stadium.
