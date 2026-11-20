# Card 545: performance mod diag, callers of the costliest Kismet queries

Family: card 527 (performance mod). Diag only; it changes no result.

## Why

Card 543 step 1 (c263L362 host, symbolized callers):
- UKismetSystemLibrary::CapsuleTraceSingleForObjects is the largest host query, at 696.4 ms per 30 s window.
- SphereOverlapComponents costs 140.2 ms per window, at 1507 us mean per call.

Both come from Blueprint graphs, so the native return address names only the Kismet thunk. Before any lever (rate
limit, distance cull, cheaper shape), the calling Blueprint function and class must be known.

## What (b360, d33663931543bda8)

- Detours on the two exec thunks (`execCapsuleTraceSingleForObjects`, `execSphereOverlapComponents`), with the
  signature `void(UObject*, FFrame&, void*)`.
- Each game-thread call is keyed by (entry, FFrame::Node = the calling UFunction, class of FFrame::Object), and
  records calls, total time and max time. Calls on other threads are only counted.
- Every 30 s (the same window as card 534) it logs:
  - `card545: <side> t=... bp query total <entry>: ...`
  - the top 16 rows: `card545: <side> t=... bp query top N: <entry> from <function> on <class>: ...`
- Rows hold weak references, so an unloaded Blueprint prints `(function unloaded)` instead of a stale name.
- Gate: UBWCOOP_C132_FX=1 (diag runs). Source: `mod/src/ubw/query_bp_c545.cpp`.

## Predictions (the b360 run)

- The host CapsuleTraceSingleForObjects total is within 30% of L362's 696 ms per window at a similar bug load.
- The top 3 rows hold at least 70% of that total, which names a short list of Blueprint functions.
- No `WARN card545` rows, mismatches 0, CRASH 0.

## Next

Read each top function's graph: what it traces for, how often it fires, and whether the result is used every call.
Then card a lever per caller (rate limit, distance cull or skip when unchanged), ranked by GameThread ms.

## Result c263L366 (b360): the premise was wrong

The exec thunk hooks installed (`hooks: UKismetSystemLibrary_execCapsuleTraceSingleForObjects hooked`), but no
`card545` row was logged and no call came on another thread, while the same windows logged about 16.5k
SweepSingleByObjectType calls. The callers are native. A static scan of the exe for direct calls to the native
functions finds:

- CapsuleTraceSingleForObjects (RVA 0x3d22bf0): USTGroundedMovementControl::Tick_DirectMovement; seven sites in
  USTFlyingMovementControl (ConvertLocationToFlyingPoint, PerformSceneryCapsuleTrace, STMoveToRelativeHeight,
  TickFlyingMovementNavmeshPath, three in TryConvertNavmeshPathToFlyingPath);
  UADShapeUtilityLibrary::execGenericShapeTraceSingleForObjects; the Kismet exec thunk.
- SphereOverlapComponents (RVA 0x3d40560): SphereOverlapActors, ASTGorePartBase::OnPostLoad,
  UADShapeUtilityLibrary::execGenericShapeOverlapComponents, the Kismet exec thunk.

## What (b361, 566c061299776a2d)

- The detours move to the two native functions, with their exact signatures (FVector and FLinearColor by pointer,
  floats in their own slots). Rows are keyed by (entry, return address, world context class).
- The report names the caller as `exe+0x<rva>`, to symbolize offline with `logs/pdb_publics.txt.funcs.pkl`:
  `card545: <side> t=... kismet query top N: <entry> from exe+0x... on <class>: ...`.

## Predictions (the b361 run)

- The host CapsuleTraceSingleForObjects total is within 30% of L362's 696 ms per window at a similar bug load.
- The top row is Tick_DirectMovement (return address just after RVA 0x51ae70e), with at least 60% of that total.
- No `WARN card545` rows, mismatches 0, CRASH 0.

## Static trace: USTGroundedMovementControl::Tick_DirectMovement (RVA 0x51ae1b0)

What the per-tick capsule trace is for (disassembly, host AI movement):
- The NPC walks straight to a target point (component +0xf0). Within the acceptance radius (+0xd8) it stops and fires
  the move-completed delegate. Otherwise:
- It runs one capsule trace forward from the pawn location. The trace uses the pawn capsule's radius and half height
  times scale (the scaled sizes at +0x540/+0x544 of the capsule), and a lookahead distance read from the NPC data at
  +0x274. The object types are WorldDynamic, WorldStatic and channel 18, built into a fresh TArray every tick. The
  trace ignores the pawn and is not complex.
- On a blocking hit whose normal slope is over the NPC data threshold (+0x278), the control marks itself blocked
  (+0xe2) and skips the move. The blocked time is added at +0xe8; past +0x280 the move fails.
- Otherwise a LineTraceSingleForObjects probes the ground ahead (WorldStatic and WorldDynamic), and then
  AddMovementInput (pawn vfunc +0x600) moves the pawn.

So the trace result drives host movement decisions. A perf mod lever must keep the result exact: no rate limit and
no skipping. Exact options, to rank once b361 measures them in the same run:
- The Kismet wrapper overhead. It is card545's CapsuleTraceSingleForObjects total minus card534's
  SweepSingleByObjectType row from exe+0x3d22dcf, which is the query itself. If the overhead is large (query params
  with a stat FName, TArray copies, the debug draw check), call the same world sweep directly from a detour.
- The query itself (about 36 us per call in L366). This is Chaos narrow phase against WorldStatic and WorldDynamic.
  Card 544 padding is the broad-phase side of it.

## Result c263L367 (b361)

Window t=89241348 (host):
- CapsuleTraceSingleForObjects: 531.9 ms (1.77% of GT). 99.2% comes from exe+0x51ae713 (Tick_DirectMovement) on
  World, 14359 calls at 36.7 us mean. Predictions 1 and 2 hit. No WARN card545.
- SphereOverlapComponents: 129.7 ms. The top caller is exe+0x3d4040b (SphereOverlapActors+0xab) on
  BP_HUD_Manager_MI_C, 29 calls for 106.5 ms. That is 3.67 ms per call, a hitch of about 4 ms each time. Next is
  STSquadLeaderComponent, 49 calls for 18.8 ms.

Next levers, ranked by GT ms:
- Tick_DirectMovement capsule (532 ms per window). Only exact levers apply, per the static trace above. First,
  measure the wrapper overhead as card545 total minus card534's SweepSingleByObjectType row from exe+0x3d22dcf, in
  the same window.
- The BP_HUD_Manager_MI_C SphereOverlapActors (106 ms, 4 ms hitches). Find what the HUD manager overlaps for, and
  whether the query can be narrowed (radius, object types, class filter) with the same result.

Wrapper overhead, measured (L367 host, window t=89241348): card545 CapsuleTraceSingleForObjects is 14569 calls for
531.9 ms. card534 SweepSingleByObjectType from exe+0x3d22dcf is the same 14569 calls for 527.9 ms. The Kismet wrapper
costs 4.0 ms per window (0.3 us per call), so calling the sweep directly is not worth a card. The Tick_DirectMovement
cost is the Chaos sweep itself, at 36.2 us mean and 1.7 ms max. The next exact lever is on the query side, for
example the 1.7 ms max sweeps (complex geometry, or long lookahead into dense static mesh).
