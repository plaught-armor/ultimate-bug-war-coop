# Card 336: objective go places a pawn in a BP_FallZones_C and the game moves it away

**Status:** fixed in the lane 2 build after `67b9beb3f4376177` (this card's build), awaiting the b11-soak-c2m1 run.
Found in r6 (`logs/c333-soak-c2m1-*.log`, `.drive`): after the C2M1 checkpoint, `objective go` cmds 144, 162 and 167
failed the settle with a 3830 uu drift of the client.

**Cause:** C2M1 places 30 `BP_FallZones_C` actors (not replicated, host-side overlap). A pawn whose capsule overlaps
one takes 20 damage and the game moves it to a fixed point, (227524, -17297, 6512) on LOS_O03. Host rows at each
warp ms: cmd 144 at 320469034/035 (and the host's own pawn at 320469029), cmd 162 at 320585279, cmd 167 at 320608463,
each `playlog: hit slot 1 server-copy: damage 20.00 ... instigator BP_FallZones_C`; the client then reads (227524,
-17297, 6512). This is not a lost server correction: before the checkpoint, go placements drifted 0 to 15 uu (cmds 23,
45, 51, 57, 85, 91), and the client's own pawn follows host warps (c75_warp at 320485756).
Cmds 162/167 lifted the client out of the step volume first (kC284LeaveUu, 100000 uu straight up); cmd 144 had no
lift, so the overlap comes from either the lift point or the landing slot (kC284RingUu 90 from the stand).
**Cmd 172** (moved 70 uu at 193136, -38262) had no fall-zone hit: a plain settle drift, under the old cap of 50 only
by margin; not this card.
**Host at cmd 144, explained (b16 static trace):** ASTFallZonesBase::OnBeginOverlap (0x145161b40) arms one timer
handle per zone (zone+0x3a8) with RepositionPlayer bound to the overlapping pawn. The host entered at 029 and armed it;
the client copy entered the same zone at 034 and re-armed the same handle, so only the client was moved. Gameplay
co-op bug candidate: two players entering one zone within its rate (zone+0x358) means the first is never repositioned.
**No falling gate:** the overlap gates are save-load idle, zone+0x310 enabled, IsA ASTPlayerCharacter, and
Player.IsPlayer (else InstaKillTarget). There is no movement-mode check. b16 row `fall zone hit slot 1: mode 1, z velocity 0`
shows a walking pawn was hit, so a floor-exact landing does not help; the zone skip stays.

**Fix (objective_c284.cpp):** `c284_in_fall_zone` walks placed level actors (kMaxLevels x kMaxLevelActors, as
c64_find_actor) for `BP_FallZones_C` and tests the pawn capsule against the zone's shape (c126_shape_find,
c126_capsule_inside).
- `c284_slot` skips a ring slot inside a zone (`card336: host slot N ... skipped: inside <zone>`).
- `c284_lift` tries up 100000, up (2 x largest extent + 500), then +x, -x, +y, -y by the same hop; the first point
  outside the step shape and every zone wins (row names the try). None: WARN, no lift.
- A stand landing inside a zone fails the command at once (`<who>: landing inside <zone>`), no settle wait.
- Risk: c126_shape_find takes the root or first shape component; a zone with several boxes is tested on one.

**Predictions (b11-soak-c2m1):** 0 `instigator BP_FallZones_C` hit rows at a go warp ms; 0 `moved N uu after the
warp` with N over 50 after the checkpoint; any skip shows as a `card336:` row naming the zone.

## Fall-zone landing check refused valid landings (c335-range-load on b21), fixed in b22

`c284_in_fall_zone` tested every `BP_FallZones_C` and ignored the game's own gate: `OnBeginOverlap` (0x05161B40)
returns when the zone's byte at +0x310 is 0 (`cmp BYTE PTR [rsi+0x310], 0` at 0x145161e9e). FED_C1M1 zone 3 is
disabled and its box (center (17303, 22446, 204), extent (8972, 2227, 2547), yaw 90) covers x 15076..19530,
y 13474..31418, z -2343..2751, which holds the Assault_SO2 ring slot 90 (17340, 28568, 1373) and the host stand
(17312, 19616, 1208). b22 (`0c40725031a4aede`) skips zones with +0x310 0 and prints one row per zone per map
(FED_C1M1: 4 zones, 2 enabled; FED_C2M1: 30, all enabled) and a row per refusal.

## Closed (2026-11-17 11:11)

The fix of b22 (zones with +0x310 0 are skipped, one row per zone per map and one per refusal) has run in every scenario since; objective-driven steps arrive (card 284 runs). Reopen when an `objective go` step logs an arrival and the pawn is then moved by a fall zone. Evidence base: the 202 host and client logs of builds b150 to b180 written since 2026-11-16.
