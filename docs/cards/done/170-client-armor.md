# Card 170: The client's armor drops when it is hit

Status: DONE (junior), awaiting senior review. Run c170-r1 passed, DLL 0ab2f45b9c855cc5.

Read: finding 19; docs/cards/done/157-client-hits-and-feedback.md lines 13-45 (cause b); docs/reference/engine-facts.md
(attribute sets replicate, attributes have no CPF_Net).

## Why
The client loses health but never armor. Card 157 found that on the host armor takes the damage first, and that the
armor value does not reach the client. No fix was built.

## Senior trace (2026-11-11)
- verified: the host applies damage to the server copy's Armour first (c141b: "Armour 100.0 -> 84.0, Health 100.0 ->
  100.0"; `detour_c40_armour`, mod/src/ubw/enemies.cpp:936).
- verified: Armour and MaxArmour are already marked CPF_Net with Health (card 021, `kC21NetAttributes`,
  mod/src/ubw/weapons.cpp:362; host log "card021: host CPF_Net set on UADAttributeSetHealth.Armour (offset 0x50").
  So the value is carried by the engine's replication of the health set, the same way Health is.
- verified: replication writes the attribute silently on the client (no RepNotify). For Health, card 021's
  `c21_watch_health` (weapons.cpp:383) sees the change each tick on client-own and calls the game's
  `UADHealthComponent::HealthAttributeChanged` so the state and HUD follow. Nothing does this for Armour.
- verified: the PDB has `?ArmourAttributeChanged@UADHealthComponent@@MEBAXAEBUFOnAttributeChangeData@@@Z` and
  `?MaxArmourAttributeChanged@...`, same signature as the Health handler.
- open: that the client's own set's Armour value really falls (read it with `c48_armour`, death.cpp:2060). If it does
  not, the value lands in the other health set (see `c21_spawned_health_set`) and the watch must read that one.
Verdict: the missing piece is the client-side notify, not a relay.

## Do
1. Add `UADHealthComponent_ArmourAttributeChanged` to tools/symbols.txt. Extend the card 021 watch so client-own
   Armour is watched like Health and a change calls ArmourAttributeChanged with old and new values. Log it capped:
   "card170: client client-own Armour A -> B, ArmourAttributeChanged called".
2. No new relay op, no new file. Fix the swapped breakdown labels in the enemies.cpp:936 hit line only if it is a
   one-line change; otherwise report it out of scope.
3. One run (the client stands in a bug's reach, GOD=0): prediction, for each host line
   "playlog: host ... hit slot N ...: ... Armour X -> Y" on the server copy, the client logs the card170 line with
   the same Y within one second, and a client screenshot shows the armor bar below full.

## Done when
- Client Armour equals the host's server-copy value after each hit (quoted line pairs), and the bar shows it.
- The usual Report items (WARN diff, run length, rc, ERROR/FATAL, CRASH, dumps, DLL hash). Ends "Report complete."

## Files
mod/src/ubw/weapons.cpp, mod/src/ubw/hooks_internal.h (declarations only), tools/symbols.txt, one new scenario
tools/scenarios/c170-armor.txt. Nothing else.

## Static check of the senior trace (junior, 2026-11-11)

- **g_health_attribute_changed** is resolved from the PDB in hooks_ubw.cpp's resolve table (line 2126), as a plain
  address. The Armour handler is resolved the same way, beside it.
- **The PDB name** is `?ArmourAttributeChanged@UADHealthComponent@@MEBAXAEBUFOnAttributeChangeData@@@Z` (const), at RVA
  0x04A3B200. The non-const spelling is not in the PDB.
- **What the handler does** (disassembly at 0x144A3B200): it reads only `[rdx+0x3C]` (Old) and `[rdx+0x38]` (New), and
  broadcasts the component's delegate at +0xC8 with (Old, New). The same 0x48 FOnAttributeChangeData the Health
  notify builds is enough.
- **The watch's per-side state:** `g_c21_health_seen[kSprintSides]` lives in attributes.cpp and is reset per map in
  sprint.cpp. Neither file is in this card's scope. The Armour watch therefore keys its last value to the pawn
  pointer: the first value of a new pawn is only recorded, so no reset is needed.
- **Where the watch is called:** c21_parity_step (weapons.cpp, run each tick from hooks_ubw.cpp:192), for each
  player-controller pawn. Client-own already adopts the replicated set there, before the watch.
- **The open line, closed from code:** `c48_armour` reads the set at the component's HealthAttributeSet (+0x130). On
  client-own, card 021's `c21_adopt_replicated_set` has repointed +0x130 to the replicated set, and Health is read
  from that same pointer. c165d shows Health following there: client "card021: client client-own Health 100.0 ->
  95.8" at 280166481, and the host hit at 280166470, "Health 100.0 -> 95.8". So Armour (offset 0x50 per card048, same
  set) lands in the set the watch reads.
- **Swapped breakdown labels (enemies.cpp:936):** already fixed. kC40ParamsHealth is 0x40 and kC40ParamsArmour is
  0x44, with a comment naming ApplyDamageToArmour and ApplyDamageToHealth. c165d reads "(armour 25.00, health 0.00),
  Armour 100.0 -> 75.0". No change made.
- **Card 046 `warp damage`** ignores armour (c48_deal_ignore_armour), so it cannot test this. The scenario uses a
  spawned WarriorBug (`host spawn bug`, card 125), as in c165.

## Predictions, c170-r1 (tools/scenarios/c170-armor.txt, DLL 0ab2f45b9c855cc5)

1. At start the client logs "card170: client client-own t=.. Armour -1.0 -> 100.0" (recorded only).
2. For each host "playlog: host t=T hit slot 1 server-copy: ... Armour X -> Y" line:
   - The client logs "card170: client client-own t=T' Armour X -> Y, ArmourAttributeChanged called", with the same Y
     and T' - T < 1000 ms.
   - Hits 1 and 2 are 25 armour each (100 -> 75 -> 50), as in c165d.
3. Screenshot client-c170-r1-c170-after-hit1 shows the armor bar below full (about 75 %).
4. Once armour is spent into health, the client's card021 Health line still follows the host's.
5. rc 0 and 0, ERROR/FATAL 0, CRASH 0. WARN classes are the baseline set, plus any card157 lines the bug's attacks
   cause.

## Report

### Files changed

- mod/src/ubw/weapons.cpp:
  - Adds g_armour_attribute_changed.
  - Adds c170_watch_armour. On the client's own pawn, it reads Armour from the adopted replicated set through
    c48_armour, the same set card 021 uses for Health. On a change, it calls
    UADHealthComponent::ArmourAttributeChanged with New and Old. Lines are capped at 12.
  - c21_parity_step's client-own branch calls the watch after c21_adopt_replicated_set.
- mod/src/ubw/hooks_internal.h: the extern for g_armour_attribute_changed (the HealthChangedFn shape).
- mod/src/ubw/hooks_ubw.cpp: one resolve-table row, next to the Health row.
- tools/symbols.txt: UADHealthComponent_ArmourAttributeChanged, with its const PDB name. offsets.generated.h was
  regenerated by `pdb_rva.py gen` (RVA 0x04A3B200).
- tools/scenarios/c170-armor.txt (new).
- docs/cards/170-client-armor.md: the static check, the predictions and this report.

### Summary

**Cause, confirmed.** Replication writes Armour into the client's set silently. No client-side
ArmourAttributeChanged runs, so the HUD's armour delegate (component +0xC8) never fires.

**The open trace line is closed from code.** The client's Armour lives in the set adopted by c21_adopt_replicated_set
(component +0x130), at offset 0x50. That is the same set and the same path as Health. The c165d log line also shows
the host's split already labelled correctly.

**Fix.** The client-own watch calls the game's own ArmourAttributeChanged on each replicated change, in the same way
card 021 does for Health. The watch is keyed per pawn, because the per-map reset of card 021's state lives in
sprint.cpp, which is out of scope.

**Not done:**
- The breakdown labels at enemies.cpp:936 were already fixed (kC40ParamsHealth 0x40, kC40ParamsArmour 0x44). No
  change was made.
- Scenario weakness: the Health try regex "Health 100\.0 -> \d" also matched hit 1's "Health 100.0 -> 100.0". The
  Health proof below uses hit 3's lines, read from the logs.

### Verification

Build: 0 warnings. DLL 0ab2f45b9c855cc5 ("deployed: 0ab2f45b9c855cc5", "lane 1: dll 0ab2f45b9c855cc5, build
0ab2f45b9c855cc5, port 7777").

Run c170-r1, GOD=0: "run c170-r1: ended by scenario after 66 s; host rc 0, client rc 0".

Start: "card170: client client-own t=83519201 Armour -1.0 -> 100.0".

Hits. All host lines are "hit slot 1 server-copy" with instigator BP_AI_WarriorBug_C.

| Hit | Host t | Host line | Client t | Client line | Delay |
|---|---|---|---|---|---|
| 1 | 83523023 | "(armour 25.00, health 0.00), Armour 100.0 -> 75.0, Health 100.0 -> 100.0" | 83523034 | "card170: client client-own t=83523034 Armour 100.0 -> 75.0, ArmourAttributeChanged called" | 11 ms |
| 2 | 83524404 | "(armour 25.00, health 0.00), Armour 75.0 -> 50.0, Health 100.0 -> 100.0" | 83524424 | "Armour 75.0 -> 50.0, ArmourAttributeChanged called" | 20 ms |
| 3 | 83525690 | "(armour 20.83, health 4.17), Armour 50.0 -> 29.2, Health 100.0 -> 95.8" | 83525707 | "Armour 50.0 -> 29.2, ArmourAttributeChanged called" | 17 ms |

**Health still follows.** "[83525707 ms] card021: client client-own Health 100.0 -> 95.8, HealthAttributeChanged
called", against the host's "Health 100.0 -> 95.8" at 83525690.

Screenshots:
- client-c170-r1-c170-after-hit1.png: the HUD armor readout is 75, with the bar about three quarters full. Health is
  100. Prediction 3 holds.
- client-c170-r1-c170-after-kill.png: armor 30 (the HUD rounds 29.2), with the bar about a third full. Health is 96
  (95.8), with the bar nearly full.

drive.py:
- "scenario c170-armor.txt: drive rc 0", "scenario passed".
- "dead time: sleeps 2.0 s, timed-out try waits 0.0 s in 0 (none)".

Counts:
- host: 3020 lines, FATAL/ERROR 0, WARN 2.
- client: 2806 lines, FATAL/ERROR 0, WARN 2.
- CRASH 0. "new crash dumps: none".

WARN classes, both sides:
- "WARN attach: DisableThreadLibraryCalls failed, err=126"
- "WARN config: UBWCOOP_GOD not "1" (status=1, err=0)"

This is the c162-r3 set exactly. c172-r1 differed only by GOD=1, and by its card169 and card157 lines from the flare
and the Morita shots. There is no new class, and no card157 line in this run.

### Out of scope

- Card 021's g_c21_health_seen per-map reset lives in sprint.cpp. If the watch should share it, the armour state
  (g_c170_pawn and g_c170_armour) could be reset there too. It is not needed now, because the watch is keyed per pawn.
- tools/scenarios/c170-armor.txt line 29: the Health try regex is too loose, since it matches "100.0 -> 100.0". It
  should be "Health 100\.0 -> (?!100\.0)" if the scenario is reused.

Report complete.

## Senior review (2026-11-11)
CLOSED, accepted. Diff matched the allowed files; rebuild gave DLL `0ab2f45b9c855cc5` (build and deployed). c170-r1: three host hit lines and the three client `card170:` lines agree (100 -> 75 -> 50 -> 29.2); the client screenshot after the kill shows armour 30, health 96. WARN classes unchanged apart from the GOD=0 config line. The scenario Health wait now excludes `100.0 -> 100.0`.
