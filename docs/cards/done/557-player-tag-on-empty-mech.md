# Card 557: "Player" name tag stays on an empty mech

Playtest 2026-11-20 on b366 (`e62e76f63a05d88f`), FED_C2M1. Client screenshot `Screenshot_20261120_192313.png`
(about 101563716 ms in `UBWCoop-menu-20261120-184517.log`): two "Player" tags, one on the host's trooper and one on
an empty M7 Razorback mech (`BP_BaseMech_C`). Only one remote player exists.

## Log

- 101545581: `card416: client ring slot 2 on BP_BaseMech_C ... blip slot 2 added (... pawn BP_BaseMech_C)`: the host
  drove the mech, and card 416 tagged it as the remote player's pawn.
- 101574651: `card416: client ring slot 2 on BP_PlayerCharacterMI_C ... widget WB_Map_Marker_C tinted late`: the host
  left the mech and the trooper got the slot. No card416 row removes the tag, ring or blip from the mech.

## Cause (static, to confirm)

Card 416 adds a tag, ring and blip to each remote player pawn, but does not remove them when the pawn stops being a
player's pawn (unpossess: leaving a mech or vehicle). It removes them only when the actor ends.

## Fix direction

Key the tag to the player (PlayerState), not the pawn: on every pawn change of a remote player, move the tag, ring and
blip to the new pawn and take them off the old one. By class: every vehicle or mech a player can enter and leave.

## Prediction

Host enters a mech and leaves it: one card416 move row each way, the empty mech has no tag, ring or blip, and the
client sees one "Player" tag.

## Host log (`ultimate-bug-war-coop-public/logs/UBWCoop-menu-20261120-184651.log`, host t = client t + 241024698)

- 342598853 (client 101574155): the client entered a mech, and the host put the client's ring and blip on
  `BP_BaseMech_C`.
- 342642166 (client 101617468): the client left the mech, and the host added a new ring and blip on the client's
  trooper (`markers 10 -> 11`).
- Neither log has a card416 remove, move or clear row.

The bug is on both sides: the host's view of the client's empty mech keeps its tag too. The cause above is
confirmed, and the fix applies to both sides.

## b369 (`4d7adc330945a079`, build-b5 copy kept), not run yet

Fix: `c43_client_other_pawns` (`hooks_ubw.cpp`) now skips pawns with no PlayerState (`c557_ps_offset`), so
an empty mech gets no player tag.

### Run c559b371a predictions (junior, 2026-11-21)
Scenario `tools/scenarios/c371-c2m1.txt` (FED_C2M1, b371 `baeebee09d3e7f59`).
Mech source: the host takes a MechSupport pickup from the client's resupply drop (if the drop holds one) and throws
the mech drop. Void if no mech appears (`card075: host t=... mechs 0 -> 1` missing).
- Host enters and leaves the mech: one card416 move row on the client each way (trooper to mech, mech to trooper).
- Shot `c371-557-empty-mech-client`: the empty mech has no "Player" tag, ring or blip. One "Player" tag shows, on
  the host's trooper.

### Run c558b376a predictions (junior, 2026-11-21, b376 `eb49926909111446` = b375 + card 568)
Run 2 (c559b371b) got no mech: the C2M1 resupply drop held no MechSupport pickup. This run uses FED_C4M1, whose pod
mechs stand from the map start (`tools/scenarios/c375-c4m1-mech.txt`).
- After the host leaves mech A: shot `557-empty-mech-client` shows no "Player" tag, ring or blip on the mech. The
  host's trooper has the only tag.
- After the client leaves: shot `557-empty-mech-host` shows no tag on the mech. `557-empty-mech-client2` shows the
  same from the client's side.
- No card416 ring or blip row names `BP_BaseMech_C` on either side after its pilot leaves.

## Run c558b377a predictions (b377 bc12138adfaeb5ba, 2026-11-21)

In c558b376a the 557 shots pointed at the pod and at a trooper, so they decided nothing. The rerun looks at mech A on the
ground after the pilot leaves. Prediction: `557-empty-mech-client`, `557-empty-mech-host` and `557-empty-mech-client2`
show the empty mech with no "Player" tag, no ring and no map blip. Neither log has a `card416: ... ring slot N on
BP_BaseMech_C` row after the matching MechLeave row.

## Closed (junior, 2026-11-21): proven in c558b379a

- Client: the ring and the blip move from the mech to the host's trooper at 146116017, right after the host's
  LeaveMech at 146115852. Host: its ring moves to the client's trooper at 146170632, after the client's leave.
- Shots `557-empty-mech-client`, `557-empty-mech-host` and `557-empty-mech-client2` show the empty mech with no
  "Player" tag. No card416 ring or blip row names `BP_BaseMech_C` after the matching MechLeave row.
