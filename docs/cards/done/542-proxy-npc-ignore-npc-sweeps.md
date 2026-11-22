# Card 542: client NPC proxy capsules stop sweeping against other NPCs

Family: co-op mod sync and performance. It changes only client proxy movement, never a host decision.

## Why

c263L361 (b355, card 539 blocker split, client, about 230 s):
- 325004 NPC proxy capsule sweeps; 51.1% were blocked.
- Of the blocked ones, 86.1% hit another NPC and 92.8% started penetrating.
- That means 47.4% of all proxy sweeps begin inside another bug.

c263L356 put ComponentSweepMulti from BP_AI_WarriorBug_C at 5.17% of the client GameThread (18.7 us mean). An
initially penetrating sweep is the costly case, because it computes the penetration depth.

The host already separates NPCs. A client proxy that a neighbour proxy blocks stops short of the host position:
- the block delays it, against full world sync and the 100 ms latency target;
- the next replicated update pulls it back, which shows as jitter in packed swarms.

## What

On the client only, NPC simulated proxies ignore other NPC capsules for movement sweeps:
- Proxy capsules keep blocking the world, so falls and floors stay the same.
- They keep blocking player pawns, which is only 0.6% of blocks anyway.
- Hits, overlaps for damage, and line traces are untouched: only the capsule's movement response to the NPC
  object channel changes.

The exact mechanism is chosen after the trace. Candidates:
1. Set the proxy capsule's response to the NPC capsule object channel to Ignore once, when the proxy spawns (after
   BeginPlay or at the first c539_move). This needs the capsule's FBodyInstance ObjectType and the response
   container offsets.
2. Filter in the sweep: add the other proxy to the move's ignore list. This is more expensive and more invasive.

## Trace before any build

- Find the bug capsule's collision object type: GetCollisionObjectType and the FBodyInstance offsets next to
  kBodyCollisionEnabled (actor.h, 0x38F).
- Find which object type other bugs' capsules use. Confirm that player pawns use a different one, so that player
  blocking stays.
- Confirm the blocking NPC component is the capsule, not the skeletal mesh.

## Predictions

- card539 "blocked by NPC" falls to near 0, and blocked falls from 51.1% to about 7%.
- ComponentSweepMulti by BP_AI_WarriorBug_C on the client falls by at least 40% in ms against L356.
- card132 client drift rows do not rise. Proxy position error against the host, where logged, falls.
- Gate: mismatches 0, CRASH 0, WARN diff.

## Trace result (junior, static, 2026-11-20)

- Collision offsets on the primitive:
  - ObjectType is at +0x38D (GetCollisionObjectType, exe 0x143A8F600); now `kBodyObjectType`.
  - The responses are at +0x3E0, 32 bytes, one per channel (GetCollisionResponseToChannel, exe 0x1440972F0); now
    `kBodyResponses`.
- Profiles (DefaultEngine.ini):
  - NPC_CapsuleCollider_IsNotDead has object type PhysicsBody (5), Blocks PhysicsBody and Blocks Player (15).
  - PlayerCollider has object type Player (15) and Blocks PhysicsBody.
  - The Dying profile is PhysicsBody; IsDead is Pawn_NoDamage (19).
  - Not yet proven from cooked data: that BP_AI_WarriorBug_C's capsule uses the IsNotDead profile. The b357 row
    shows it.
- UPrimitiveComponent::SetCollisionResponseToChannel (exe 0x1440A73F0) calls FBodyInstance::SetResponseToChannel.
  On a change, that updates the physics filter data and then runs the component's collision-settings-changed path.

## What (b357, a809a361cccca83c)

In `perf_move_c539.cpp`, on the card 461 MoveComponentImpl row, before a client NPC proxy capsule moves: if its
object type is PhysicsBody and its response to PhysicsBody is not Ignore, it calls the game's
SetCollisionResponseToChannel(PhysicsBody, Ignore).
- `UBWCOOP_C542_OFF=1` is the A/B switch.
- New row: `card542: client t=... proxy capsule PhysicsBody ignore on|off, applied N; blocker object types
  (channel:count) ...`, counting each blocked proxy hit by the blocking component's object type.

## Predictions (b357 run, client)

- applied is about the number of proxy bugs spawned, give or take re-applies after profile changes. It is not one
  per move: about 1000s per run, not 100000s.
- card539 "blocked by NPC" falls from 143033 to under 5% of that, and "blocked" falls from 51.1% to under 15%.
- Blocker object type 5 is near 0. What remains is world types (0 WorldStatic, 1 WorldDynamic) and a few of type 15
  (Player).
- Client ComponentSweepMulti from BP_AI_WarriorBug_C falls by at least 40% against L356 (1550.5 ms per run).
- card132 client drift stays at or below 16 rows. Mismatches 0, CRASH 0, WARN diff with no new kinds.
- If applied is 0: the capsule is not PhysicsBody. Read the blocker types and pick the channel from them.

## Result (c263L363, b357 a809a361cccca83c)

- applied 0. Blocker object types (channel:count): 0:10945 1:3360 2:116404 15:474. The NPC blockers are Pawn (2),
  not PhysicsBody: the Blueprint overrides the ini profile, so the b357 type check never matched.
- Gate passed (drift 16); b357 went to lane 1 as a no-op.

## b358 (2033b596958baa95)

- The lever keys on the proxy capsule's own object type, not a fixed channel. Own types WorldStatic, WorldDynamic and
  Player (15) are kept (counted as "kept own type"); any other own type sets its response to that same channel to
  Ignore. For bugs that is Pawn (2) to Ignore. Players (15) still block bugs; the world is untouched.
- Risk: other Pawn-type objects on the client (any non-player pawn) no longer block proxy bug capsules, and Pawn
  channel traces on the client miss them. Client-side only, and positions stay host-owned.
- Predictions (L364): applied mostly on channel 2, near the proxy count per spawn (re-applies after profile
  changes); card539 blocked by NPC falls from 116491 to under 10% of that; client ComponentSweepMulti by
  BP_AI_WarriorBug_C falls by at least 40% from 1388.8 ms; drift at most 16; mismatches 0, CRASH 0, no new WARN kinds.

## Result (c263L364, b358 2033b596958baa95): all four predictions hit, lane 1

- card542 applied 2:385, kept own type 0. No Pawn (2) blockers remain: 0:13337 1:2622 15:2291.
- card539: blocked 8.3% (L363 49.0%); blocked by NPC 283 (L363 116491, 0.24%).
- Client ComponentSweepMulti by BP_AI_WarriorBug_C: 26308 calls, 437.6 ms (1.46%), down from 1388.8 ms (68.5%).
- Gate: mismatches 0/0, CRASH 0/0, drift 16, no new WARN kinds. Bug-mesh floor drift rows 13 (L363 20).
- Player (15) blocks rose from 474 to 2291: proxies no longer shove each other, so more of them reach the player
  capsule. Watch at playtest whether bugs body-block players more on the client than on the host.
- Status: done; stays on by default (UBWCOOP_C542_OFF=1 for A/B).
