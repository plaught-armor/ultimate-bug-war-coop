
## Host log: the teleports are card 389 falling warps (2026-11-20, cause found)

Host log `ultimate-bug-war-coop-public/logs/UBWCoop-menu-20261120-184651.log` (host t = client t + 241024698). Eight
`card389 ... Falling for ~6020 ms at z <z>: kill floor warp beside BP_PlayerCharacterMI_C`, each followed by `card046
... kill floor warp N: BP_PlayerCharacterMI_C (host-own) ... to BP_PlayerCharacterMI_C (slot 1) ... moved 1`: the
host's own pawn was moved beside the client.

| Host t (ms) | Map | Host z | Client z |
|---|---|---|---|
| 340652988 | FED_C1M1 (training) | 549 | 103 |
| 340845652 | FED_C1M1 | 431 | 157 |
| 340883787 | FED_C1M1 | 265 | 103 |
| 341044320 | FED_C1M1 | 312 | 97 |
| 341721074 | FED_C2M1 (client t 100696376, "keeps teleporting to me") | 10250 | 8997 |
| 341996140 | FED_C2M1 | 10028 | 10263 |
| 342040393 | FED_C2M1 | 8749 | 8751 |
| 342895383 | FED_C2M1 | 9378 | 9890 |

None of these is a real fall: the host stands at normal height (z within a few hundred uu of the client's, or above
ground on C2M1), far above KillZ -22000. In the 6.5 s before each warp the host jumps again and again
(`GA_HumanJump_C` / `HumanMovementJump_MS` every 1-3 s, e.g. 340647296, 340648443, 340649553, 340650713, 340651946,
340652566 before the first warp).

### Cause (`c389_step`, `mod/src/ubw/objectives.cpp:2870`)

`c389_step` samples the movement mode once a second (`kC389StepMs` 1000) and warps a pawn that read Falling at six
samples in a row (`kC389FallMs` 6000). A player who keeps jumping is in the air at most sample times and lands between
samples, so the run never resets: repeated jumps read as a 6 s fall and the player is warped beside the other player.
That is "host keeps randomly teleporting" (training course) and "host keeps teleporting to me". The client is exposed
the same way (card 389 runs on the host for every slot); the client simply did not jump as much.

### Fix

Card 389 warps only a real fall: the pawn is Falling at every sample AND its z has dropped a large distance since the
fall began (the card 389 case fell from -437 to -163219; a jump changes z by a few hundred uu). Concretely: record the
z at the first Falling sample, and warp only when the pawn has been Falling for `kC389FallMs` and is at least a
fixed drop (for example 5000 uu) below that start z. Any non-Falling sample still resets the run. The c389b72 C5M1
case still warps; jumping never does.

### Prediction

Host and client jump continuously for 30 s on FED_C1M1 and FED_C2M1: no card389 row. The c389 C5M1 scenario still
logs `card389 ... kill floor warp beside`.

The relevancy lead above (copies re-created after a card434 channel close) is a separate visual pop-in; the host log's
eight warps account for the reported teleports.

## b369 (`4d7adc330945a079`, build-b5 copy kept), not run yet

Fix: the card 389 kill floor warp no longer runs on the drop gate kC389DropUu (`objectives.cpp`), which caused the
eight host warps. Proof: the jump soak above shows no card389 row, and the C5M1 scenario still warps at the real kill
floor.

### Run c551b370a predictions (was c552b369a, now on b370) (junior, 2026-11-21)
Same run (C1M1 soak). A jump block at map start: host and client each press SpaceBar 20 times about 1.3 s apart.
0 `card389` warp rows and 0 `card046 ... kill floor warp` rows in the host log for the whole run.

## Closed 2026-11-21 (junior, from c551b370a)

c551b370a (b370, C1M1 jump soak) host: 0 `card389` warp rows. The single `card046 ... kill floor warp 1` row
(141195963) is the card 399 dropship release ("warp beside the host" row in the same ms): c46_warp prints "kill floor
warp" for every caller. No real kill floor warp. The relevancy pop-in part stays in card 554 (host pawn teleports on
client).
