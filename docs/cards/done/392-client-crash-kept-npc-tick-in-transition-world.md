# Card 392: client crash, kept NPC ticks in the seamless transition world

Status: accepted. Fix in b80 `2e0c99e96d390d63` (lane 2). Run `c392b80`: 10 `card392: client` lines (6, 10 and 20 kept NPCs stilled on the three travels out of a combat map, 0 on the others), 0 FATAL/EXCEPTION, no crash dump, scenario passed. The crash was 1 in about 9 travels before, so one clean run does not prove it gone; the stilled counts prove the guard runs on the actors that crashed.

## Evidence
Run `c391b78`, 98 s: client `EXCEPTION_ACCESS_VIOLATION` reading 0x0 at exe+0x510B401, game thread, 7 ms after
`card346: client ... seamless map change done: world Untitled` (travel to the hangar after C4M1). Stack: exe+0x510B401 <
0x51AB094 (inside `ASTNonPlayableCharacterBase::Tick`, exe+0x51AAC68). Seen in 1 of about 9 such travels.

## Cause (static)
The engine keeps every replicated non-owned actor on the client through seamless travel, so the bugs live on in the
transition world `Untitled` and tick there until the host's channel closes arrive. Function exe+0x510ADB0 (called from
the NPC tick with `npc+0x8D8`) resolves the weak pointer at `X+0x520` through `FWeakObjectPtr::Get` and calls
`vfn[0x5D8]` on the result with no null check. The travel destroyed the target, so the call reads address 0.

## Fix
`c392_still_kept_npcs` (`mod/src/ubw/hooks_ubw.cpp`), called from `c346_seamless_return`: on the client, when the new
world is `Untitled`, `SetActorTickEnabled(false)` (vfn 0x4A8) on every actor of the NPC class. The host closes their
channels within the travel, so nothing visible changes.

## Predictions (run c392b80)
- Client log `card392: client t=... seamless travel: N kept NPCs in the transition world, tick off` at each travel
  through `Untitled`.
- No crash at exe+0x510B401; bugs on the next map move and attack as before.
