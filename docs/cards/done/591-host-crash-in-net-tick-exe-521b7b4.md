# 591: Host crash in the engine net tick at exe+0x521B7B4 (null read)

Found 2026-11-21 in run c572b426a (b426 `8bf1b2d3f80f12ae`, FED_C4M1 mech, host crashed at 126 s). The same crash
happened in c558b377a (b377 `bc12138adfaeb5ba`). The stack and registers are the same in both, so the cause is older
than b377 and has nothing to do with the b426 changes.

- `EXCEPTION_ACCESS_VIOLATION` reading 0x0 at exe+0x521B7B4, game thread. rax=rbx=rcx=0, r8=FFFFFF01.
- Stack: exe+0x521B7B4, 0x50E2AAA, 0x2329963, 0x42DB625, 0x3DF7C2A, 0x3C25E0C, then the co-op dll at
  `ue::net::detour_tick` +0x3B (b426 0x18495B, b377 0x17CAFB), which is the call through to the original net driver
  tick. Below it: exe+0x444B00A, 0x444F72C, 0x444F7AA, 0x445049D, 0x4457724 (the engine tick).
- Lines before the crash:
  - c572b426a: god-mode mech damage on the host's own BP_BaseMech_C, then OnDamageCaused.
  - c558b377a: a WarriorBug Gib sent to 1 PC.
- No crash dump was decoded yet: UECC-Windows-7E900256435FF101CF5DE88484626123_0000.

Next: trace exe+0x3C25E0C down to 0x521B7B4 statically (which net driver step, which object is null). Then decide
whether a co-op replication change feeds it, such as an actor destroyed while a channel still holds it.

## Static trace (senior, 2026-11-21)

- Frame names (nearest public): exe+0x3C25E0C `UGameEngine::Tick`, 0x3DF7C2A `UWorld::Tick`, 0x42DB625
  `FTimerManager::Tick`, 0x2329963 a timer delegate `Execute`, 0x50E2AAA `ASTPlayerMechBase::DeathComplete` +0x7A.
  So a timer ran the mech's DeathComplete. The net driver frame is only our detour around the engine tick.
- DeathComplete +0x64: `rcx = [mech+0xC20]`, `rdx = mech+0xC48`, `r8b = 1`, call 0x0521B790. That callee reads
  `[rcx]` at once (+0x24, the faulting `mov rax, [rcx]`), then calls component vfn 0x410
  (`SetComponentTickEnabled`, as in attributes.cpp). rcx=0, so `mech+0xC20` is null when DeathComplete runs.
  attributes.cpp names +0xC20 `kMechSecondMesh` (the mech body mesh, "Mech" in the card075 rows).
- Both crashes are the same sequence on the host's own mech: a second `LeaveMech` with the
  `MSS_MECH_FLY_Razorback_SelfDestruct_Eject` and `_Buildup` sounds, then the crash about 3.0 s later (c558b377a:
  145464269 to 145467288; c572b426a: 177528598 to 177531620). The first LeaveMech in each run was a normal exit
  (`Razorback_Exit` sound) and did not crash. At LeaveMech the card075 row still reads the body (`body Mech
  bHiddenInGame 0`), so +0xC20 turns null within those 3 s.
- The god-mode put-back row in c572b426a (Health -400 to 400 at 177531616) is not the cause, because c558b377a has
  no god row and crashes the same way.

## Next (junior, one diag run)

- Find what nulls `mech+0xC20` between the self-destruct eject and DeathComplete. Candidates: the component is
  destroyed (DestroyComponent, or a mod gib/death-FX path), or DeathComplete runs twice and the first run destroys it.
- Diag, host: hook `ASTPlayerMechBase::DeathComplete` (count calls per mech, log +0xC20 on entry). From the
  self-destruct LeaveMech, log +0xC20 every 100 ms until DeathComplete. Log every `DestroyComponent` whose owner is a
  mech, with the caller rva.
- Run: C4M1, enter the mech, self-destruct eject, wait 5 s. A second mech gives a repeat.
- Fix only after the row names the path. The guard on a null +0xC20 in DeathComplete is a workaround and is not the
  fix.

## Diag result (c591b427a, b427 `e84faf5a7b0234b0`)

- Cause: card 560's `c560_drop_body` (remote_body.cpp, called from c28_step at dwmapi+0x81E83) ran on the own pawn at
  each mech enter. While piloting, the own pawn is the mech, so it destroyed the mech's own native body sprite at
  +0xC20 (SpriteComponent, STSpriteRenderComponent). Row 5255 at 178323700: `DestroyComponent SpriteComponent ... on
  mech BP_BaseMech_C ...: IS its body +0xC20, caller dwmapi+0x81E83`, then the card560 "body SpriteComponent removed"
  row. Every earlier crash run has that card560 row too, and the client did the same to its own mech (c572b426b).
- After the normal leave (178328942, body still set), GC nulled +0xC20 40 ms later (178328982). The abandoned-mech
  eject came 60.0 s later (178388961). DeathComplete ran once (calls 1) with body 0 at 178391970 and crashed at
  exe+0x521B7B4. DeathComplete does not run twice, and no game path destroys the body.

## Fix (b428 `28982ce0c7a6b385`)

`c560_drop_body` returns at once for a mech, and destroys only the renderer that card 028 attached (the pawn's
C28Slot renderer index must match). Card 075 already says a mech keeps its own body and gets no card 028 body. No
null guard in DeathComplete.

Proof c591b428a: no card560 row and no "IS its body" row. Mech A and mech B each: eject 60.0 s after the leave, then
DeathComplete before and after with body set, calls 1. No crash, no dump, scenario passed, mismatches not measured
(no digest step).
