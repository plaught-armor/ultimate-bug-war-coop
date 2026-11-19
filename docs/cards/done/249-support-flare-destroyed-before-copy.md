# 249: Tutorial support flare destroyed in its spawn second: client never sees it

Found by card 240 run 5. Host 161245637 spawns BP_SupportFlare_Tutorial_C; 161246664 LZQueryFinished and the
flare is destroyed at (12655, 14440, -53) in the same ms; no card104 copy was sent. In run 4 the flare came
to rest and was copied (client 159708319). Bomber, DangerZone and projectiles reached the client both times
and TacSupport SO6/SO7 completed on both sides. Not a play blocker: after the playtest.

Step 1, static: where card104 decides to send the flare copy (on rest?) and why a flare that is destroyed
before rest sends nothing; whether the host player sees a flare in that case at all (thrown, in flight). Fix
by class: every thrown marker of that family is visible on the client from the throw, not from rest.

## Step 1 answers (runs 4 and 5 of card 240, source, disassembly; senior, 2026-11-13)

- The only place the host relays a thrown flare is `detour_c104_flare_activate` (`anim_c104.cpp:229`), after
  `ActivateFlare`. Run 4: throw 159707231, `ActivateFlare` 159708278, 'P' row 159708302 at the landed place. For the
  1047 ms of flight the client has no flare at all, in every run. The host player sees it from the throw.
- Run 5: no `ActivateFlare`. The flare is destroyed inside `ASTSupportFlare::LZQueryFinished` 0x50F1DD0, 1 ms after
  `RunLZQuery`, at the same place as run 4's landing (12655, 14440, -53). The function head: when the query result
  is null or its status at +0x28 is not 1 (success) it calls the virtual at vtable +0x4A8 with argument 0 and
  broadcasts the delegate at +0x2D8, then goes on to `StartActivationTimer`. The destroy row sits between the
  `LZQueryFinished` row and the `StartActivationTimer(0.01)` row, so it comes from that branch. The timer then
  fires on a destroyed actor and `ActivateFlare` never runs: no 'P' row, no copy.
- The bomber is not tied to the landing: `BP_TacSupp_Bomber_Tutorial_C` spawns about 120 ms after the throw in both
  runs and gets its own 'P' and 'M' rows.
- Not settled by the logs: why the landing query failed in run 5 (same landing place as run 4). Decided by the
  diagnostic row below.

## Fix spec (by class, build 4; `anim_c104.cpp`, `hooks_ubw.cpp`, `tools/symbols.txt`, `world_c98.cpp`)

- Relay from the throw: detour `ASTSupportFlare::BeginPlay` 0x50DE120 (new symbol row). After the original, on the
  host, call `c104_relay_spawn(flare, actor_location(flare))` for a flare that is not level-placed (same placed
  test as the card 098 rows). Flight then goes through the existing 'M' sampling (`c104_sample_moving`), landing
  through the rest row, the early destroy through the existing 'D' row.
- `detour_c104_flare_activate` keeps its call but only for a flare that is not relayed yet
  (`c104_is_relayed`, `world_c98.cpp:642`); a level-placed flare keeps today's behaviour.
- Check before coding: `c104_relay_spawn` must not add the same actor twice; the client's copy must not throw or
  activate anything of its own that today's copy does not (it already runs its own Blueprint from the 'P' row).
- Diagnostic: the `LZQueryFinished` row gains the result status (int at result +0x28, or `null`) and the item count
  (int at result +0x8). No behaviour change.
- Do not touch: `detour_try_throw_flare`, card 174, the bomber path.
- Prediction (FED_C1M1 run, tutorial flare step): host 'P' row for `BP_SupportFlare_Tutorial_C` within 50 ms of
  its `card098: host spawn` row; client `copy of host <id>` within 100 ms of that; at least 2 'M' rows before
  `RunLZQuery`; when the flare is destroyed early, a 'D' row and the client's destroy row in the same second, and
  the `LZQueryFinished` row shows a status other than 1. Budget: 0 extra runs (batched with the next FED_C1M1 run).

## Build 4 run c224-c1m1-b4d (FED_C1M1, DLL `5e2ce00acad8efa3`, 639 s, scenario passed, 0 ERROR, no VOID), senior verdict 2026-11-13

Held for the normal path: host spawn of `BP_SupportFlare_Tutorial_C` at 241286082, client `copy of host 126984`
at 241286091 (9 ms), host destroy row `D 126984` sent 241296691, client `our copy destroyed` at 241296706. The
early-destroy case did not occur (`LZQueryFinished status 1`), so the status diagnostic is unexercised.

## Closed (2026-11-17 11:11)

The normal path held in c224-c1m1-b4d and the early-destroy case has not occurred since (`LZQueryFinished status 1` in every run). Reopen on a `LZQueryFinished` row with a status other than 1 together with a missing client copy. Evidence base: the 202 host and client logs of builds b150 to b180 written since 2026-11-16.
