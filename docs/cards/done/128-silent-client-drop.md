# Card 128: Host drops a lagging client silently, and the client never notices

Status: sent and accepted 2026-11-07.

Read:

- card 125 Report, the c125zb paragraph; logs `logs/c125zb-host.log`, `logs/c125zb-client.log`, `logs/c125zb-run.out`;
- card 026 Report (ping/pong), card 045 Report (client follows travel), card 049 (journal sync slot), card 113 Report
  (session players, the rejoin grace).

Repo root: `/path/to/ultimate-bug-war-coop`. DLL base: card 125's `69e1cb9079f52374`.

## Why

The card 125 Report calls the c125zb failure environmental. It is a bug a player will hit. A friend whose machine
or link stalls for a few seconds gets dropped, and neither side says why:

- RTT on the host's card 026 pongs was 33 ms, then 1186, 702 and 2195 ms (139802714 to 139813741) during the dropship
  flight on FED_C3M1. The load average was 12-14.
- At 139817088 the host destroyed the client's `BP_PlayerController_C`, pawn and PlayerState. The caller was
  exe+0x37DC6BD, and card 049 logged "left: journal sync slot freed". Card 113 dropped to 1 player at 139820371.
- Neither side logged `UEngine_HandleNetworkFailure` (hooked on both). No close reason was logged anywhere.
- The client kept running for another 50 s. It sent pings 7 to 17 with no pong after pong 6, and did nothing about
  it: no failure, no return to the menu, no rejoin. The player would be left in a dead world.

A stall of about 3 s should not cost a friend the session (memory: friends-only trust model). A real drop should
reach the client.

## Do

1. **Measure (static, then the run in step 3).**
   - What closed the connection: the function at exe+0x37DC6BD and its caller chain, and the close reason
     (connection timeout, a failed bunch, or a kick from game code). Log the reason at close with a hook on the close
     path.
   - The timeouts in force on the host and the client: `ConnectionTimeout`, `InitialConnectTimeout`, any timeout the
     game or our travel sets, and whether the connection was still in its initial phase at 43 s after the join.
   - Why the client did not see the close: whether the host sent a close bunch, and why the client's own timeout did
     not fire in 50 s.
2. **Fix, at the layer that owns it.**
   - A friend whose game stalls for up to about 10 s stays in the session. Set the timeouts where the engine reads
     them, not through a per-symptom relay.
   - When the host does drop a client, the client finds out within a few seconds. Use the engine's network failure
     path, or card 026 pongs missing for N s. The client then either rejoins through the existing card 113 grace or
     card 045 path, or returns to the main menu with the failure shown. Say which, and why.
   - Both sides log the drop with its reason.
3. **Proof, one run, targeted.** Reproduce the stall on purpose: `kill -STOP` the client's game process, wait, then
   `kill -CONT` it, during the dropship flight on FED_C3M1 (the c125zb point). Use two stalls in the same run:
   - 5 s: the client stays in, the host keeps its PC, and play goes on (the client moves after it resumes).
   - Longer than the new timeout: the host logs the drop with its reason, and the client detects it within the bound
     you chose and takes the path from step 2.
   - rc 0, ERROR/FATAL 0, no CRASH line, no new dump on either side.

Stop when the fix builds and that run shows it.

## Done when

The Report has the close path and reason, the old and new timeouts, the client detection mechanism and its bound,
the run lines for both stalls, the run length, rc, ERROR/FATAL, CRASH lines, dumps and the DLL hash
(`sha256sum | cut -c1-16`) with the play DLL saved as `build/dwmapi-play-<hash>.dll`. Ends with "Report complete."

## Report

DLL `2f443e99fa6ed52a` (`build/dwmapi-play-2f443e99fa6ed52a.dll`, packaged as `dist/UBW-Coop-20261107-1157.zip`),
built on `69e1cb9079f52374`. Proof run: c128n.

### Close path and reason

The close reason is logged by a hook on `UNetConnection::Close(FNetResult&&)`, which records the result value, its
LexToString name, the error context and the caller chain. Two separate paths were measured:

1. **Timeout (reason 4 ConnectionTimeout).** `UNetConnection::Tick` calls `UIpConnection::HandleConnectionTimeout`,
   which calls `Close`. `CleanUp` then destroys the PlayerController. exe+0x37DC6BD from c125zb is
   `AActor::Destroy+0x4d`, the PC destroy inside that teardown. With the old 60 s timeouts, this path alone cannot
   explain a drop after a 3 s stall.
2. **Reliable buffer overflow (reason 65 RPCReliableBufferOverflow). This is the c125zb drop.** The chain runs
   `UNetDriver::ProcessRemoteFunctionForChannelPrivate+0x2e0` < `InternalProcessRemoteFunction(Private)` <
   `ProcessRemoteFunction` < `AActor::CallRemoteFunction` < `ProcessEvent`. The overflowing RPC is `ClientMessage` on
   `BP_PlayerController_C`, which is the mod's reliable host-to-client chan transport.

   Per-opcode send counters at the close (c128l, c128m) showed what was flooding it: `25:1205` CollisionOp, against
   `2:4 3:4 11:1 15:1 26:9 28:1 29:2` for the other opcodes. Those CollisionOps come from the card 095 collision
   relay. Their caller is `ASTDamageableActorBase::OnSignificanceChanged` (exe+0x5165410), with
   `ASTCustomPickupBase::OnSignificanceChanged` as a second source. That is the significance LOD: each machine turns
   collision on and off by distance from its own viewpoints. The host's dropship camera sweeping the map produced
   about 40 changes a second.

   A stalled client acks nothing. Reliable bunches merge per net flush, so about one outstanding bunch was added per
   host frame. The 256-bunch limit was reached about 4 s into a stall, and the host closed with reason 65. The same
   flood also filled the 256-row card 095 journal ("journal full: not replayed"), so real collision state was being
   lost for late joiners. It also caused the `name not sendable` WARNs: 128 per run in c128m, 0 in c128n.

The client never saw the close because the close bunch was lost along with the reliable stream. With the old 60 s
timeout, the client's own timeout fires only after 60 s of silence after it runs again. Before the fix, the client
kept pinging into a dead connection.

### Timeouts

These are the driver fields that `UIpConnection::GetTimeoutValue` reads: +0xDC `InitialConnectTimeout` and +0xE0
`ConnectionTimeout`.

| | InitialConnectTimeout | ConnectionTimeout |
|---|---|---|
| Old, both sides | 60 s | 60 s |
| New, both sides | 60 s, kept so the join and its map load stay covered | **20 s** |

The mod writes 20 s into the driver field, so the engine applies it. A stall of up to 20 s keeps the friend. The
connection was past its initial phase at the c125zb point (state 3).

### Fix

- **Overflow source (`door_c95.cpp`, `hooks_ubw.cpp`).** The game's six `OnSignificanceChanged` overrides are hooked,
  one template detour each: ASTDamageableActorBase, ASTCustomPickupBase, ASTNonPlayableCharacterBase,
  ASTAttackSystemEnemyBase, ASTGorePartBase and ASTProceduralDropship. Each detour keeps a depth counter. While the
  counter is above zero, `c95_relay_collision` does not relay: these changes are per-machine view state, not world
  state. All other collision writes still relay as before (card 095, one mechanism).

  Two earlier gates missed. `ASTGameplayGameMode::UpdateSignificanceManager` (c128l) and `USignificanceManager::Update`
  (c128m) are not the callers of these callbacks. Both gates are removed.
- **Sibling, client-to-host (`turret_sync.cpp`, `chan_ops.cpp`).** TurretAim is the only continuous client-to-host
  reliable sender: every 50 ms while the aim moves. A stalled host would overflow on it after about 13 s. The client
  now records the time of each host Ping (`g_c128_host_ping_ms`; the host pings every second) and holds TurretAim
  while the last Ping is more than 2.5 s old. Aim is latest-value state, so the newest aim goes on the first tick
  after the host is back. Every other chan sender is edge-based: Fire on and off, Ping and Pong at 1 per second,
  Use and the other one-shot ops. This hold was built and deployed in c128n but not exercised by it; the run has no
  turret.
- **Client detection (`net_drop.cpp`, from earlier in the card).** In `GetTimeoutValue`, a connection that went
  longer than its own timeout between two ticks has already been dropped by the host, which uses the same 20 s. The
  detour then returns 0.001 s, so the engine's own Tick timeout path closes it at once and reports
  `HandleNetworkFailure`. If the host's close bunch does arrive, the engine closes on it directly; c128n took this
  path (reason 66 ControlChannelClose).

  **Bound:** the client's first tick after it runs again, measured at about 0.1 s. A client that keeps running while
  the host goes silent times out by the engine at 20 s.
- **Rejoin, not menu.** After `HandleNetworkFailure`, the client browses to the frontend, and the existing card 045
  path ("session dropped ... reconnect once it loads") rejoins the host. On the host, card 113 shows the friend back
  (`1 -> 2 join`), and the card 049, 098 and 095 join replays bring the world back in sync. A friends-only session
  should put the friend back in with no clicks. The failure is still logged on both sides and shown by card 052.
- **Logging on both sides.** Every close is logged with its reason. On an overflow, the overflowing RPC and the
  per-opcode chan send counts are logged too. The client logs the dropped detection ("did not tick for N ms").
- **Side fix (`dropship.cpp`).** The autodropship now finishes when the game unpauses the dropship world by itself
  before the first action: "world unpaused after 0 action(s)". This was the c128i and c128k scenario hang.

### Proof run c128n

FED_C3M1 during the dropship flight, `tools/scenarios/c128-stall.txt`. 121 s, host rc 0, client rc 0. FATAL/ERROR
0 on both sides. WARN 0 on both sides. No CRASH line. No new crash dumps. Scenario passed (drive rc 0).

**Stall 1, 5 s.** The client stays in and moves afterwards:

```
11:56:20 +  53.4s ok   line 11: client stall 5 -> stalled pid 2645317 for 5 s
11:56:21 +  54.4s ok   line 13: client wait state pawn=client-own timeout=10 -> state pawn=client-own
11:56:22 +  55.7s ok   line 14: client goto -61646 -49703 450 150 timeout=30 -> done
[ 145233386 ms] card026: host pong 2 from pc BP_PlayerController_C: rtt 3092 ms
[ 145235318 ms] card026: host pong 3 from pc BP_PlayerController_C: rtt 22 ms
```

There was no close and no card 113 change on the host, which kept the PC.

**Stall 2, 25 s.** This is longer than the 20 s timeout. The stall started at about 145238600:

```
[ 145258711 ms] card128: host t=145258711 connection 000000009F9C4060 closed: reason 4 ConnectionTimeout, then none, state 3, timeouts initial 60.0 connection 20.0 s
[ 145260285 ms] card113: session players 2 -> 1 (present 1, left or not back after the grace)
[ 145263716 ms] card128: client t=145263716 connection 0000000051380080 closed: reason 66 ControlChannelClose, then none, state 3, timeouts initial 60.0 connection 20.0 s
[ 145263716 ms] card052: t=145263716 network failure 3 (ConnectionLost) in session, 145263716 ms after the join arm
[ 145263723 ms] card045: client t=145263723 session dropped (Browse to local map "/Game/Maps/3D_Frontend/3D_Frontend"): reconnect to "127.0.0.1:7777" once it loads
[ 145264755 ms] card113: session ended (standalone world), session players 1 -> 0
[ 145272959 ms] card113: session players 1 -> 2 (present 2, join)     (host: friend back)
[ 145275345 ms] card026: host pong 8 from pc BP_PlayerController_C: rtt 16 ms
```

The host closed with reason 4 about 20.1 s into the stall; there was no reason 65 this time. The client detected
the drop on resume (about 0.1 s) and rejoined about 9 s later.

For comparison, the same scenario before the significance gate (c128l, c128m) failed. The host closed with reason
65 about 4 s into stall 2, with counts `25:1174` and `25:1205`.

### Left open

- The TurretAim hold is built in but not run-proven; it needs a host stall while the client is in a turret.
- The `significance changes kept local` total is logged only at the next map load, so c128n, which stays on one map,
  does not print it. The evidence that the gate works is that c128n has no reason 65 and 0 `name not sendable`
  WARNs.

Report complete.
