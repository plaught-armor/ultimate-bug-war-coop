# 472 — Kept NPC ticks again in the destination world after a seamless travel (client crash)

Found 2026-11-17 in `c423b173a` (junior): client crash 3 ms after `card346: client ... seamless map change done:
world 3D_Frontend`. EXCEPTION_ACCESS_VIOLATION reading 0x0 at exe+0x510B401, stack 0x51AB094
(`ASTNonPlayableCharacterBase::Tick`) < 0x51AA6D2 < 0x51AA5F6 < 0x37F619A. Dump
`UECC-Windows-1EADAE1E43747CE00513199165AF4B20_0000`. Same null weak target as the card 392 crash (c391b78), but in
the destination world. Rate: 1 crash in 166 client travels with kept NPCs logged since b80. Not a b173 regression.

Evidence: `card392: client ... 45 kept NPCs in the transition world, tick off` in Untitled, then 12 rows
`card132: client BP_AI_WarriorBug_C local switch to Falling held` in 3D_Frontend: the kept bugs' movement runs again.

Cause (junior, static, UE source, not yet checked in the exe): the kept NPCs are carried from Untitled into the
destination world, where `AActor::RegisterActorTickFunctions(true)` re-registers the tick with
`bStartWithTickEnabled || IsTickFunctionEnabled()`, undoing the card 392 tick-off.

Fix (client only, placed NPCs untouched): remember the stilled NPCs' object indices at the card 392 still (fixed
table, WARN when full, cleared at map end); in the card 346 arrival handler resolve each, check live and NPC, still
again; row `card392: client ... N kept NPCs stilled again in <world>`. If the first destination tick can run before
the handler, also clear `bStartWithTickEnabled` in Untitled (offset from the exe).

Done when: a c423 end-of-map travel shows the new row, 0 `local switch to Falling held` rows after arrival, no
crash; gate c448 passes. A second crash at exe+0x510B401 after the fix reopens it.

## Closed

Closed 2026-11-17 (senior), build b174 `4efbc7838d907625`. `c392_still_kept_npcs` (hooks_ubw.cpp:434) remembers the
stilled NPCs as weak refs (256, WARN when full) and stills the live ones again at the next arrival, which runs in
`UEngine::TickWorldTravel` before the destination world's first tick. c423b174a: `seamless arrival: 15 kept NPCs
stilled again in 3D_Frontend (47 remembered, 32 gone)`, 0 `local switch to Falling held` rows after the arrival
(b173a: 12), no crash; gate c448b174a 0 mismatches. One run against a 1 in 166 rate: the proof is the missing
post-arrival rows. A second crash at exe+0x510B401 reopens it.
