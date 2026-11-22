# 507: A client's own level script opens a door the host has not opened yet

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Opened 2026-11-19 by the senior, from the junior's c502c1b280a finding (b280 c014bee01ec0e705, FED_C1M1, three games), checked in door_c161.cpp and door_c95.cpp.

## Finding

- At 752930966 the client pressed its own CTF laser pickup. Its FED_C1M1_Scripting_C ran the pickup chain locally.
  - card463 skipped only the weapon edits.
  - The door open in the same chain ran: card095 SetCollisionEnabled PawnBuffer at 752930967, then `card161 ... BP_Door_Sliding_Large_C_1 DoorState 0 -> 2` at 752930970.
- The host's card 495 gate opened the door 2.4 s later, at 752933362, when 3 of 3 had the laser. Meanwhile the client's door stood open with its collision off, while the client still held MoritaMk1. The host's door was shut.
- Knock-on: the client's montage settled 2 -> 1 at 752935501, 1.9 s before the host's row 1. The 500 ms card270 hold ran out, and the client logged `WARN card161 ... client-only DoorState 2 -> 1, set back to the host's 2`.
- Cause: `c161_poll` (door_c161.cpp:286) sets a client change back only when `d.host != kC161NoHost`. `c161_add` sets `d.host = kC161NoHost` (line 160), and the host sends only changes. So a door the host has not touched has no host state on the client, and every local change to it passes as legitimate.
  - Affected: every tracked BP_Door_Base door on every map, until its first host row, on every client, including after a rejoin (C_1 was tracked again at 752882046).
- `c161_write` (line 221) writes PawnBuffer back only when `d.host_buffer >= 0`. A fix that sets only `d.host` would put DoorState back but leave the client's collision open.
- Late-join edge: if a DoorOp row arrives for a door that is not tracked yet, `c161_receive` returns false and `c95_receive` drops the row with `WARN card095 ... no placed ASTDoor has that name, skipped`. That WARN appears 0 times in the last 60 client logs. With the baseline in place, though, a lost row would set an open door back to closed, so the fix covers it.

## Do

1. `c161_add`, client: set the baseline to the map's own state, `d.host = d.state` and `d.host_buffer = c161_buffer(d)`. The host's untouched door is in that same state. Host rows keep overriding it through `c161_receive`.
2. `c161_receive`: when `c161_by_name` misses, run `c161_scan` on the world (c95_receive has the pc: `g_actor_get_world(pc)`), then look the name up once more before returning false. A replay row that comes before the 5 s scan then lands on the door instead of being dropped.
3. Log one line per set-back made against a baseline (no host row yet), giving the door, the states and "baseline". The run can then tell this path from the existing one.

## Done when

On the next bug-batch run that goes through FED_C1M1's laser door with CLIENT2=1:
- The client's own laser pickup logs a baseline set-back for BP_Door_Sliding_Large_C_1 (0 -> 2, set back to 0, PawnBuffer back to 3).
- The client's DoorState goes 0 -> 2 within 100 ms of the host's 3 of 3 row and not before.
- The aim screenshot after the client's pickup and before the host's 3 of 3 shows the client's door closed.
- 0 `WARN card161 client-only` lines from a montage settling early.
- 0 `WARN card095 ... no placed ASTDoor` lines.
- Class check: no client `card161 ... DoorState` change on any door in that run comes before its host row, unless a baseline set-back follows it.

## b281 result and the second fix (senior, 2026-11-19)

c502c1b281a (b281 5152ba17f51b94d0): the baseline set-back works for DoorState and PawnBuffer (753942390, 3 ms after the client's pickup), the DoorOp lands 18 ms after the host's 3 of 3, and there are 0 WARN card161 and 0 WARN card095. The visual still fails: the client's own "Open Door" montage keeps playing (OnNotifyBegin at 753945069, 1 s before the host's open), and client-c502c1b281a-c507-client-door-shut.png shows the panels apart.

Cause: the level script calls Open Door through the BP VM (EX_FinalFunction / EX_VirtualFunction → UObject::CallFunction → UFunction::Invoke), not through ProcessEvent, so `c161_pe` never sees it. Both paths meet in `UObject::ProcessInternal` (RVA 0x0154F980), the Func of every script UFunction. It checks the call space and, for a local call, calls `ProcessLocalScriptFunction(UObject* Context, FFrame& Stack, void* Result)` (RVA 0x0154FA80, call site 0x14154F9EC; the new frame's Node is at +0x10, `kFrameNode`). At that point the caller has already read the parameters into the new frame, so skipping the body leaves the caller's bytecode position correct.

Decision: option (c), the root-cause fix. The montage-stop and close-call options treat the symptom and leave a one-frame open on every map.

Do:
1. Add `?ProcessLocalScriptFunction@@YAXPEAVUObject@@AEAUFFrame@@QEAX@Z` to tools/symbols.txt, regenerate offsets.generated.h, and detour it.
2. In the detour, on the client only, when `g_c161.calling` is false, Stack.Node is one of a tracked door's `fn[]` and Context is that door: skip the original and count a refusal. Compare Stack.Node first (a few cached UFunction pointers, built at scan time) before walking the door list; the function runs for every BP function call, so the miss path must be the client check plus that pointer compare.
3. Log one capped line per (door, function) the first time a BP VM call is refused: `card507: client t=<ms> door <name> BP VM <fn>: refused`.
4. Keep the b281 baseline set-back as the backstop.

Done when, on the next C1M1 bug-batch run with CLIENT2=1:
- The client logs `card507 ... BP_Door_Sliding_Large_C_1 BP VM Open Door: refused` at its own laser pickup.
- No client OnNotifyBegin or DoorState change on that door before the host's 3 of 3 row; the client's DoorState goes 0 -> 2 within 100 ms after it.
- The aim screenshot after the client's pickup and before 3 of 3 shows the panels shut.
- 0 baseline set-backs on that door (the refusal comes first).
- The detour's cost is in the log: refusals and total calls per minute.

## b282 review (senior, 2026-11-19)

b282 1765d1a2dd105abb, door_c161.cpp:154 (c507_vm_add), :628 (c507_vm_refuse), :681 (detour). What I tried to break, and found no defect in:
- the host path (g_c507_vm false, one bool load);
- the map change reset (c161_map_end and c161_arm_map clear the set and the flag);
- the mod's own calls (calling=true passes);
- a stale actor pointer (refusal also needs a function match);
- PE double counting.

New behaviour to watch: the door's own internal calls to its refused functions (the close delay after an open, ExecuteUbergraph calling Close Door locally) are now refused on the client too, where before only the ProcessEvent ones were. That is by design, because the host's DoorOp row closes the door through c161_call. The readout must still show that the client's C_1 follows the host's close rows, with no door stuck open and no new WARN card161 or card270 lines. It must also quote the per-minute `script function calls` count, as the gate's cost.

## Closed 2026-11-19

Proved in c502c1b282a (b282 1765d1a2dd105abb, FED_C1M1, CLIENT2=1), and the b282 c448 gate passed:
- The client refused its own level script's call: `card507: client t=758613706 door BP_Door_Sliding_Large_C_1 BP VM Open Door: refused`. Client2 refused the same call at 758616485.
- Host 3 of 3 at 758616498. The client's C_1 went 0->2 at 758616521 (23 ms after), client2's at 758616536 (38 ms after). No client change or OnNotifyBegin on C_1 came before the host's row.
- 0 baseline set-backs. 0 WARN card161, card270 or card095.
- Both clients followed every host close row on C_0 within 14-31 ms.
- client-c502c1b282a-c507-client-door-shut.png shows the panels shut.
- Gate cost: about 21-26k script function calls per second on the client. The miss path is one bool, one counter and up to 6 pointer compares.
