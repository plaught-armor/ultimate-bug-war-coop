# Card 106: One combined, fast proof run of the unproven fixes before the playtest

Status: written 2026-11-06 (senior); sent and accepted 2026-11-06 (rows that failed go to card 107).

Read:

- the "Not shown" lines of the card 101, 103, 104 and 105 Reports;
- `tools/scenarios/c101-measure.txt`, `c103-proof.txt`, `c105-proof.txt` (routes to reuse);
- `tools/drive.py` (`wait log`, `wait state`, `warp`, `goto`, `look`, `key`).

Repo root: `/path/to/ultimate-bug-war-coop`. Play DLL: `533e5cd2cd04a851` (card 103). No code change
unless a check fails (then stop, report, and the senior cards the fix).

## Why

Four fixes are built but no run has shown them working. The user wants them proven in one run, sped up, not one
run per fix (rule: combine proofs into one scenario; each step must prove something new).

## Do

1. **Write one scenario, `tools/scenarios/c106-combined.txt`**, FAST=1, that does in order:
   1. **Mantle (card 101):** the client mantles three times, using the final DLL. Do not walk or hop the route: c101g
      reached the ledge by luck after many W+Space hops, and c101j drifted and fell. Use the one known-good spot from
      c101g instead: the client stood on the ground (mode 1) at **(-503.1, 21795.9, 142.9)**, facing +Y, pressed
      Space, and GA_Mantling_Move lerped it to (-503.1, 21848.0, 200.7) (the ledge top).
      - Add a point form to the warp command: `warp client point X Y Z` puts the server copy exactly at that location
        (no `kC75WarpAbove` lift, so it does not fall), the way `look` already takes a point. Small, test-only.
      - Each try: `host warp client point -503.1 21795.9 143`, wait for the client's pawn to arrive (log, about 0.5 s),
        `client look 90 0`, `client key W 400` with `client key SpaceBar 100`, then `wait log` on "mantle window
        closes". Then warp back to the spot for the next try.
      - If the client is corrected off the spot after the warp, log where it lands and stop the mantle part; do not
        hunt for another ledge.
   2. **Client flare and pod pickup (cards 104, 105):** the c105-proof flare route (objective completes, warp onto the
      tutorial pickup, E, 4, fire). Then warp the client's server copy next to one of the host's relayed pod pickups
      (by host actor name, farther than 300 uu from every placed pickup so E cannot hit the wrong one), `look`, E.
   3. **Host throw (card 104):** the host needs charge. If no route gives it, add the smallest test command that sets
      the host pawn's TacticalSupportComponent charge for slot 0 (through `UpdateAbilityCharge`, test-only, behind
      the runner's command channel). Host presses 4 and fires.
   4. **Fight with drops (cards 103, 105):** the c103-proof fight. Kill several bugs so any drop shows. Let the client
      go downed once and note whether bugs keep hitting it.
2. **Speed:** replace fixed `sleep`s with `wait log` on the line that marks the step done (for example the card104
   'P' row, the card105 'D' row, "card101 ... mantle window closes", the downed line), with a timeout. Keep fixed
   sleeps only where no log line exists, and keep them short. Target: the whole run under 6 minutes after load.
3. **Run it**: parts 1-3 in one FED_C1M1 run; part 4 in a second short FED_C2M1 run (`UBWCOOP_TRAVEL` as c103p; C1M1
   has no bugs). Decided 2026-11-06: two runs, no in-run travel command. Both sides muted, windowed, MemoryMax scope as usual.

## Done when

The Report has one row per check: mantle (3 tries, max correction during each, end gap), client pod pickup (host
shows it taken, client gets the item), host throw (strike on both), drops (classes seen, reach the client or none
dropped), downed (bugs hit a downed player: yes/no). Plus run length, rc, ERROR/FATAL, dumps. A failed row names
where it stopped; no fix in this card. Ends with "Report complete."

## Report

Play DLL 533e5cd2cd04a851 plus two test commands = **1623a6ff252f3e69** (copy `build/dwmapi-c106-1623a6ff252f3e69.dll`).

Test-only code (runner command channel only):

- `warp actor|client point <x> <y> <z>`: exact target, no lift (`cmdfile.cpp` parse_warp, `testrun.cpp` c75_warp);
  `kMaxTokens` 5 -> 6.
- `charge <slot> <value> <ready>`: own pawn's TacticalSupportComponent slot via `UpdateAbilityCharge` and the ready
  setter (`anim_c104.cpp` c106_set_charge, Op::Charge in `testrun.cpp`, "charge" in `tools/drive.py`). c106c host log:
  `charge slot 0 to 20000 ready 1 ...: 12500 0 ... -> 20000 1 ...`.

Scenarios: `tools/scenarios/c106-combined.txt` (C1M1: range route, flare, pod pickup, host throw, mantles) and
`tools/scenarios/c106-fight.txt` (C2M1 via `UBWCOOP_TRAVEL`). In the C1M1 run the mantles come last, after the throw,
so the flare/pod/throw state builds up in one pass.

| Check | Run | Result |
|---|---|---|
| Client flare (104/105) | c106a, c106c | **Pass.** Host `flare BP_SupportFlare_C ActivateFlare` (+58 s); the pod's MoritaMk1 copy arrives on the client 1 s later (host id 124220 in c106a). |
| Client pod pickup (105) | c106a, c106c | **Not proven, stopped at the warp.** `host warp client BP_WeaponPickup_MoritaMk1_C 80` moved the server copy (c106c: (12760, 13416) -> (12533, 13486)), but the client's own pawn stayed about 370-450 uu away (its look ran from (12929, 13484) after 3 s in c106c, from (12863, 13455) after 1 s in c106a). The server copy went back to the client's spot (12927, 13484 at the next warp). So `look actor` took the placed MoritaMk1 at (13239, 13381), and E found `interactor target null`. No DisablePickup, no 'D' for the pod gun, no item. c106a ended on that wait (line 45). c106c made the step non-blocking so the later rows still ran. The `warp client <actor>` calls earlier in the route (range pickups) did move the client. |
| Host throw (104) | c106c | **Pass.** After `charge 0 20000 1`, Four + fire: host 2nd `ActivateFlare` (+68.6 s); client `copy of host 124330: ...BP_TacSupp_Pod_Resupply_C at (13429, 18826, ...)`. Host 'D' for the landed pod at (13429, 18841) arrives at the client (`our copy destroyed`). Strike seen on both. |
| Mantle x3 (101) | c106c | **Fail on try 1, stopped there.** `warp client point -503.1 21795.9 143` landed exactly: host server copy at (-503.1, 21795.9, 143.0), client's own `CheckJumpInput` at the same point, so no correction. `look 90 0`, W 400 (no movement: the jump ran at the warp point), SpaceBar: GA_HumanJump and GA_Mantling_Search ran and ended on both sides, but no mantle started. No "mantle window closes" within 8 s (line 65). Max correction and end gap: none to measure. Per card, no other ledge tried. |
| Drops (103/105) | c106b | **No bug drops.** Three `kill actor BP_AI_WarriorBug_C` kills spawned no pickup. One drop class seen later: host `spawn BP_WeaponPickup_MoritaMk1_C at (224358, 40874, 8556)` at t=43859587 (during the client's bleedout, near the fight), spawned from caller exe+0x3DCB50E. That is not the Blueprint spawn thunk the card 105 relay keys on, so no 'P' row was sent (0 `card098: host t=` world lines in c106b) and the client got no copy. |
| Downed (043/103) | c106b | **Yes, bugs hit the downed player.** Client `own pawn Up -> Downed, 30 s bleedout` (+70.5 s). Host: `took 25.0 damage while Downed: bleedout -2.5 s` at 26.1, 22.3, 18.4 s left. |

| Run | Map | Length | Games rc | Drive rc | ERROR/FATAL | Dumps |
|---|---|---|---|---|---|---|
| c106a | FED_C1M1 | 99 s | 0 / 0 | 1 (line 45, pod pickup wait) | 0 / 0 | none |
| c106c | FED_C1M1 | 110 s | 0 / 0 | 1 (line 65, mantle try 1) | 0 / 0 | none |
| c106b | FED_C2M1 | 83 s | 0 / 0 | 0 | 0 / 0 | none |

Open, for the next card: (1) why `warp client <actor>` at the pod does not move the client's own pawn while the
range warps and the point warp do; (2) the c101g spot does not mantle from a standing start (c101g reached it
walking); (3) the drop MoritaMk1 from exe+0x3DCB50E is outside the card 105 relay.

Report complete.
