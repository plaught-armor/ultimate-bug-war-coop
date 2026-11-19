# 270 Client door montage completes before the host's state row and is set back for one frame

Found by card 243 run 9 (`logs/c243-c3m2-r9-*`, FED_C3M2, DLL 75d9c5ce204aa049). One line in the run.
Not a play blocker on its own: it can make the door flick back to open for about 40 ms.

## Evidence

Door `BP_Door_Sliding_Gigantic_Vertical_C_0` at (-60069, -3614, 1275):

| side | t | row |
|---|---|---|
| host | 222798779 | `DoorState 0 -> 2, PawnBuffer 3`, sent |
| client | 222798818 | applied state 2 |
| client | 222800299 | montage `OnCompleted_37142D5D44E3E0AACA07ACAF41BD161B` ran |
| client | 222800311 | `WARN card161: client t=222800311 door BP_Door_Sliding_Gigantic_Vertical_C_0 client-only DoorState 2 -> 1, set back to the host's 2 (PawnBuffer 0, host 3)` |
| host | 222800314 | `DoorState 2 -> 1, PawnBuffer 0`, sent |
| client | 222800351 | the host's 1 arrived and applied |

The client's own montage completion ran 3 ms before the host's, and 40 ms before the host's row arrived.
The card 161 set-back treats it as a client-only change.

## Step 1 (static, no run)

1. In the card 161 door code, list the client transitions that the door's own montage drives (here 2 -> 1
   at `OnCompleted`) and the host state they always lead to.
2. Check whether the set-back also writes `PawnBuffer`, and whether the host value it writes (3 here) is
   stale at that moment.

## Fix outline (class, all doors)

A client transition that matches the host's next expected state (montage-driven, for example 2 -> 1) is
held for one round-trip time instead of set back. If the host's row confirms it, nothing is written. If no
row comes within the hold, it is set back as now. `PawnBuffer` stays host-owned in every case.

## Step 1 answers and fix as written (2026-11-13)

- Cause: on the client, the door montage settles DoorState one step ahead of the host's row (2 to 1, 3 to 0).
  `c161_poll` set it back as client-only. The set-back also re-wrote a stale `PawnBuffer` (host 3 while the host was
  about to send 0).
- Fix (`door_c161.cpp`, `c270_hold`):
  - A montage-settled step is held for up to 500 ms for the host's row, and not set back.
  - `PawnBuffer` is not written during the hold.
  - `c161_receive` logs whether the host confirmed the step or differed. When it differed, the host's state is
    written.
  - Every other client-only change is set back as before.

## Proof

No dedicated run. The next soak that opens a sliding door counts `client-only DoorState` WARNs. The target
is 0 for montage-driven transitions, with the set-back still logged for real client-only changes.

## Senior code review (2026-11-13)

Attacked: hold expiry with no host row (falls to `c161_own`, set back as before), host row that differs during
the hold (host's state written, door's own Open/Close called when the open half changes), repeated polls during
the hold (`d.state` not advanced, `PawnBuffer` not written), a non-montage client change (hold cleared, set
back). No defect found. Run proof still owed: a `card270: ... confirmed` row and 0 `client-only DoorState`
WARNs for montage steps in a FED_C3M2 run.

## Run C (c240-c1m1-c1), senior note 2026-11-13

2 `WARN card161: ... client-only DoorState 1 -> 3, set back to the host's 1 (PawnBuffer 3, host 0)`, doors `_0`
(234507095) and `_1` (234531900). Not a montage step: the client's own presence chain opened the door 4 ms after
the host's presence fired (234507091), before the host's door row arrived. The set-back then closes a door the
host is about to open. Fix for build 4 (`door_c161.cpp`): the card 270 hold also covers a client-only step from
closed to opening; if the host's row confirms inside 500 ms nothing is written, else set back as today.
Predicted rows in the next FED_C1M1 run: `card270: ... confirmed` for doors `_0` and `_1`, 0 `client-only
DoorState 1 -> 3` WARNs.

## Montage hold proven; the remaining WARN is the door's own close delay (senior, 2026-11-16)

- Montage hold: proven in `b40b166a` and `b40b168a` (client): `card270: ... montage settled DoorState 2 -> 1 ...
  held 500 ms` at 527233516, then `held DoorState 1, host row 1: confirmed` at 527233545.
- Both runs still have one `WARN card161: client ... door BP_Door_Sliding_Large_C_1 client-only DoorState 1 -> 3,
  set back to the host's 1 (PawnBuffer 3, host 0)` at 527234337. Trace (`b40b168a`):
  - host: `DoorState 2 -> 1` at 527233513, `DoorState 1 -> 3` at 527234313 (800 ms later, no presence or other PE
    row before it), sent.
  - client: own `1 -> 3` at 527234337 (825 ms after its own settle at 527233512), set back; the host's row
    applied at 527234345 with `OnInterrupted ... (mod call)`.
  - So it is not a presence step (card 288 removed those on the client; 1 `card288: client` row in the run, none
    near this time). It is the door's own close delay, which runs on both sides. The client's fired 8 ms before
    the host's row arrived.
- What the player sees today: the door starts to close, snaps open for one frame, and restarts its close montage.
  With real latency (50 to 100 ms) the snap lasts that long.
- Fix: scratchpad `c270b_patch.py` (`c270_hold` also holds `host 1, client 3` for 500 ms; row text `own close
  delay started`). With no host row in 500 ms the step is set back as before (the host held its close, card 288).
- Predictions for the next b40 C1M1 run: one `card270: ... own close delay started DoorState 1 -> 3 ... held 500
  ms` row followed by `held DoorState 3, host row 3: confirmed`; 0 `WARN card161 ... client-only`; no
  `OnInterrupted ... (mod call)` row at that step; digest 0 mismatches.
- "Run C" closed-to-opening case: no longer occurs (card 288 stripped the client's presence door reactions; 0
  such WARNs in b40b166a, b40b168a, c448b169b, c236b169a, c460b169a, c423b163a).

## Closed

Closed 2026-11-17 (senior), build b173 `dedb14d000d6ec36`: b40b172a and b40b173a: 0 `client-only DoorState` rows, door close confirmed.
