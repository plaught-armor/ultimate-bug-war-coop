# 490: Infinite stamina setting

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: Opened 2026-11-18 08:44. User request: if stamina does nothing but sprint, add a setting that disables stamina or makes it infinite.

## What stamina does (static, from the cooked packages, 2026-11-18)
- Stamina is a Blueprint float on BP_PlayerCharacterMI (Stamina, Edit Stamina, Regen Stamina, StaminaRegenTimer, StaminaCostCurve). It is not a native GAS attribute: no native attribute set has a stamina field.
- Spenders call "Edit Stamina": GA_Sprint (cost from SprintStaminaCost_Curve), GA_HumanJump, GA_CharacterJump, GA_Slide and GA_Mantling_Move.
- The only gate is the Abilities.NoStamina tag. It appears in GA_Sprint, GE_Sprint and BP_PlayerCharacterMI only (USTSprintAbility::OutOfStaminaTag is the one native stamina property). Jump, slide and mantle spend stamina but are not blocked when it is empty.
- GA_Sprint also reads SprintStaminaSpeed_Curve, so sprint speed may scale with current stamina.
- Readers with no gameplay effect: WB_StaminaBar, WB_Health_Armour (LowStamina/NoStamina glow), WB_Vignette_Stamina, the Stamina_Empty and breathing barks.
- Limit: this is from name maps only; zen packages hide calls with no return value. Confirm at run time before building.

## Plan
1. Run-time check: log the Stamina variable and the NoStamina tag while sprinting, jumping and sliding to confirm the list above.
2. User decision (2026-11-18): the host sets it for both players; the host sends the value to the client at join and on change. Option in the mod settings (ubwcoop ini plus the in-game options if a slot exists): when on, hold Stamina at max on each local player pawn and never add Abilities.NoStamina. Host and client each apply it to their own pawn; the sprint speed must match on the other screen (card 019/022 parity).
3. Proof: sprint 60 s with no stop, full speed from the speed curve, jumps and slides do not drain the bar, the client's view of the host's speed is unchanged.

Reopen when: n/a (open).

## Step 1 run-time check (c244b218a, 2026-11-18 09:16)
- Sprint drains about 1.2 per 100 ms at full and about 0.6 near 30. Regen after a short delay is fast (30 to 98 in about 1 s).
- Sprint speed scales with Stamina: MaxWalkSpeed 1312 at 100, 1208 at 30 (walk 700).
- A jump costs a flat 5.
- Abilities.NoStamina was not reached (minimum 30.2). A true slide was not exercised.
- The host's server copy of the client pawn drains with the same values, so the logic runs on the server too.
Verdict: no gameplay effect beyond sprint speed, the sprint gate and the jump cost. Plan stands, with one change: the host holds Stamina at max on every player pawn it has authority over, and the client holds it on its own pawn.

## Closed (2026-11-18 09:58)
Built in b219 (3a050e4f1afc39b7): host Stamina hold on every player pawn it has authority over, client hold on its own pawn, lobby [I] option, `host stamina on|off`, ChanOp::Stamina in the join replay. Default off. c448b219a off path: "host stamina off (ini), unchanged" with 3 join replays applied on the client. c244b219a on path for about 66 s: Stamina 100.0 and NoStamina 0 on host pc 0, host pc 1 (role 3 copy) and client pc 0 (role 2); MaxWalkSpeed 1312 on all three; jumps left Stamina at 100. Off again: client drain about 1.2 per 100 ms, same as b218.
Not done: the "no movement corrections" item. The run had 16-55 corrections per second for about 12 s, but the server copy of the client was pinned 70 uu from the host pawn on a shared goto path (pawn contact), and the burst ended when the host moved off. Slides were not exercised.
Reopen when: corrections above 4 per second for 3 seconds in a row, or any correction above 20 uu, while the hold is on with the two players on separate paths, or Stamina below 100 on any player pawn with the hold on.

## Separate-route check c244b220a (2026-11-18 10:59)
The hold kept Stamina 100.0 on all player controllers, with MaxWalkSpeed 700/1050/1312. In the first 28 s (row cap) there were 47 srv[] corrections, 1.7 per second on average, |e| at most 3.4 uu; b219 with shared paths had 16 to 55 per second. Three single seconds were above 4 (7, 17, 5), which met the old reopen condition literally. The senior kept the card closed, because the errors are under 4 uu and not sustained, and replaced the condition with the one above. Both host gotos ended "stuck on the leg past the navmesh path's end".
