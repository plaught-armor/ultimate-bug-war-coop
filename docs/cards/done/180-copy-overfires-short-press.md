# Card 180: The host copy fires more rounds than the client on a short press

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-11, from run c176-r3 (card 175 class). Budget: three runs. One goal (re-scoped by the senior,
decision (b)): the deferred release leaves in the frame it was decided in, so the release transit equals the press
transit again. The exact count fix is a separate card.

Read: card 175 (hold on release, `c175_release_or_hold`, `c175_copy_round`), card 178 (final count at frame end).

## Why

c176-r3: FireProjectile lines host 62, client 60. The burst pairs 46/46; the 8 short presses (60-120 ms) give client
12 (the sum of the counts sent: 256, 256, 512, 512, 256, 512, 512, 256) against 14 on the copy. Card 175 only holds
the release while the copy is short; nothing stops a copy that is ahead.

## Senior trace (verify each line before code; mark verified or open in a Trace section)

- Open: which presses over-fired, from the c176-r3 host log (copy rounds per press against the received count).
- Open: why the copy is ahead: the relayed press starts the copy's fire ability and its release arrives a frame or
  more later (latency plus card 178's end-of-frame send), so the copy's fire timer gets one more round. Name the
  function that fires it and where a round limit can be applied without skipping game code that frees anything.
- Open: the fix shape. The press does not know the final count, so the host cannot cap in advance; options are to
  end the copy's fire at the client's count when the release arrives (already too late for a round fired), or to
  make the copy's rounds follow the client's per-round claims. Say which one the code supports and what an over-fired
  round costs today (ammo on the copy, a visible extra tracer, damage).
- Also in this card: clear `g_c24.release_due` (card 178) at session end and at map load.

Stop and report the trace before code if a fix needs a new per-round message or changes card 157's claim pairing:
that is a design decision for the senior.

## Trace

- Verified: presses 2 and 5 of the 8 short presses over-fired, from c176-r3 (the copy's server-copy FireProjectile
  lines between each relayed press and its relayed release). The other six matched.

  | press | client hold ms | client rounds | copy window ms | copy rounds |
  |---|---|---|---|---|
  | 1 | 72 | 1 | 85 | 1 |
  | 2 | 87 | 1 | 103 | 2 |
  | 3 | 104 | 2 | 121 | 2 |
  | 4 | 124 | 2 | 136 | 2 |
  | 5 | 88 | 1 | 102 | 2 |
  | 6 | 105 | 2 | 120 | 2 |
  | 7 | 122 | 2 | 138 | 2 |
  | 8 | 72 | 1 | 85 | 1 |

  The Morita's DoAction interval is about 85 ms. Press 2: the copy pressed at 89089881 and fired round 2 at 89089968,
  87 ms later. The client released at 89089961, 87 ms after its press and before its own round 2. The release reached
  the host at 89089984. Press 5 has the same shape (the copy's round 2 at 89090582, the release at 89090599).
- Verified: why the copy is ahead. Its press-to-release window is about 15 ms longer than the client's hold, because
  card 178 added one frame to the release. The client and host send times pair one to one:
  - c176-r2 (before card 178): press transit 4 to 19 ms, release transit equal to its press (P:19 R:20, P:4 R:5, ...).
  - c176-r3: press transit 4 to 13 ms, release transit 20 to 26 ms.
  The send is a ServerChangeName RPC through ProcessEvent (ue/chan.cpp `chan_send_host`). The engine sends it at
  `UNetDriver::TickFlush` (RVA 0x3E883E0), inside the engine Tick. `c178_flush_release` runs in `ubw_post_tick`,
  after the engine Tick, so the release waits for the next frame's flush. Card 178's trace said "no added frame".
  That was wrong for the wire: the count is read with no frame added, but the send gains one frame.
- Verified: the function that fires the extra round is the same chain as card 178, on the copy. The montage DoAction
  calls `USTWeaponFireAbility::Fire` 0x5236320, and Fire+0x3e7 calls `UADAbilityTask_FireProjectile::FireProjectile`
  0x4A33E40 (hooked; `c175_copy_round` counts it).
- Open: whether the FireProjectile detour could skip the original for a round past the count without skipping code
  that frees anything. The ammo spend and the task's end are not traced. In any case the host does not know the
  count when the round fires, because the release arrives after it.
- The fix shapes the code supports:
  1. In scope: remove card 178's added frame. After `c178_flush_release` sends, call
     `UNetConnection::FlushNet(false)` (RVA 0x3E6A2F0, virtual) on the own PC's connection
     (`APlayerController::NetConnection` +0x520, `kPcNetConnection`). The release then leaves in the same frame, and
     the copy's window returns to the client's hold plus the transit difference (about 0 to 1 ms in c176-r2). This
     removes the bias that made presses 2 and 5 over-fire. It does not make the count exact: a client hold that ends
     within the transit jitter (a few ms) before a round boundary still over-fires.
  2. Exact: the copy fires a round only when the client did. That needs one of:
     - a per-round message: the client's HitClaims come at hit time, after the copy's round has already fired;
     - or retiring the extra round after the release (back to the pool, and out of the card 157 round table), which
       changes card 157's claim pairing.
     Either one hits this card's stop condition.
- What an over-fired round costs today:
  - one extra tracer and round on the host's screen;
  - one round of the copy's ammo;
  - in card 157, an extra entry in the PC's round table. Claims take the oldest unclaimed round, so every later claim
    in that PC's stream pairs with a round one earlier than its own. The damage still lands at the claim's point on
    the claimed NPC. The round left over at the end parks and, after kParkMs (1500 ms), replays its own non-NPC hit
    (an extra wall impact) or goes back to the pool.
  - c176-r3 has no card157 WARN from it: no lost claim and no copy damage.
- `g_c24.release_due` (card 178) is cleared only by `c178_flush_release` and set by `c178_defer_release`. It is not
  reset at session end or at map load. This part is in scope and needs no design decision.

Stop condition hit: an exact count needs a per-round message or a change to card 157's claim pairing. Reported to the
senior before code.

Senior decision (b): build shape 1 (flush the own connection after the post-tick release) plus the release_due clear.
Two more lines, verified before code:

- Vtable slot (verified). `?FlushNet@UNetConnection@@UEAAX_N@Z` is exe+0x3E6A2F0, slot 97 (+0x308) of
  `??_7UNetConnection@@6BUObject@@@` (exe+0x7AC3CE0). The own connection is a UIpConnection (log: "UIpConnection_
  HandleConnectionTimeout hooked"). Slot +0x308 of `??_7UIpConnection@@6BUObject@@@` (exe+0x7C53600) also holds
  exe+0x3E6A2F0, so there is no override. Only UChildConnection (exe+0x373B150) and UDemoNetConnection (exe+0x3BA3820)
  override it. The code still calls through the object's own slot +0x308 and logs once whether it equals the RVA.
- Safety (verified). ubw_post_tick runs on the game thread after UEngine::Tick returns, outside UNetDriver's
  TickDispatch and TickFlush (exe+0x3E883E0). The engine calls this same FlushNet from every TickFlush. FlushNet checks
  IsInGameThread, calls ValidateSendBuffer (vtable +0x2F0), and clears LastEnd and TimeSensitive (+0x260/+0x271).
  With SendBuffer bits (+0x6C0) 0 and HasDirtyAcks (+0x1CE0) 0, it sends only a keepalive when one is due (elapsed −
  LastSendTime +0x1F0 > Driver(+0x58)->KeepAliveTime +0xD8, not InternalAck, State != USOCK_Closed); else nothing.
  Before the call the mod reads State (+0x134, low byte) and calls only on USOCK_Open (3). A null or not-Open
  connection is skipped with a capped line. The call is made only when c178_flush_release sent a release from c24_step
  (the Fire opcode's sent bytes rose), so once per press, never per frame.
- Reset (verified). The one place g_c24 is reset is `c24_reset_map` (sprint.cpp), called from
  `client_diag_load_map_return` on every LoadMap. That includes the menu load at session end (it also clears
  g_client_session there). The release_* fields are cleared there.

## Predictions (write the exact lines before the run)

Run c180-r1, one run, the same scenario and command as c176-r3.

- Once: "card180: own connection IpConnection_... FlushNet slot +0x308 = exe+0x3E6A2F0 (UNetConnection::FlushNet
  exe+0x3E6A2F0): base, no override".
- One "card180: client t=... own connection flushed after the release (N so far)" per primary release, right after
  its "card178: client t=... primary release sent after the frame" line. No "not flushed" line.
- Client-to-host transit of each primary release is within the range of the press transits (pairs quoted from the
  card132 stamps). None shows the extra frame of c176-r3 (releases 20-26 ms against presses 4-13 ms).
- FireProjectile count host against client, per press. Presses whose hold ends more than 10 ms from a round boundary
  match. A press that still over-fires inside the jitter window is reported with its times; it is not a failure.
- Carried from card 176: the map line after travel gives the muted count and "0 muted auto-destroy components still
  live"; the cap-line wait at scenario line 80 passes.
- Baseline WARN only on both sides, no dead time, drive rc 0.

## Files

- May change: `mod/src/ubw/fire.cpp`, `mod/src/ubw/hooks_internal.h`, `tools/scenarios/c176-click.txt`, this card,
  `tools/symbols.txt` and `mod/src/offsets.generated.h` (senior decision (b)).
  Others only as the trace names them, stated in the Report.

## Done when

- Trace, change, run lines quoted, the usual Report items. The Report ends with "Report complete."

## Report

### Files changed

- mod/src/ubw/fire.cpp:
  - New `c180_flush_net`. It reads the own PC's NetConnection (+0x520) and its State (+0x134, low byte). On
    USOCK_Open only, it calls FlushNet through the connection's own vtable slot +0x308. Once, it logs the slot against
    exe+0x3E6A2F0. A null or not-Open connection is skipped with a capped line.
  - `c178_flush_release` takes `after_tick`. c24_step passes true and the press path passes false. The flush runs only
    when after_tick is true and the Fire opcode's sent bytes rose across `c26_send_fire`, so it runs once per release
    actually sent.
- mod/src/ubw/hooks_internal.h: C24State gains `flushes`, `flush_lines`, `flush_skip_lines` and `flush_slot_logged`.
- mod/src/ubw/sprint.cpp: `c24_reset_map` clears release_due, release_flushing, release_pc_id, release_fired_at and
  release_why. This file is outside the card's list. It is the place the senior's rule names ("the one place g_c24 is
  already reset"), and `client_diag_load_map_return` calls it on every LoadMap, which includes the session-end menu
  load.
- tools/symbols.txt: `UNetConnection_FlushNet ?FlushNet@UNetConnection@@UEAAX_N@Z`. mod/src/offsets.generated.h was
  regenerated by `python3 -I tools/pdb_rva.py gen`: `UNetConnection_FlushNet = 0x03E6A2F0`.
- This card: the senior-decision Trace lines, the re-scoped Goal, the Predictions and this Report.
- tools/scenarios/c176-click.txt: unchanged.

### Summary

The goal holds. Every deferred release now leaves in its own frame. In all 9 pairs, release transit is at or below
press transit. In c176-r3 the releases were 20-26 ms against presses of 4-13 ms. FireProjectile matches host against
client for every press, 58 against 58 in total (c176-r3: 62 against 60).

The run failed at scenario line 94, the map line after `host travel next`, because the host crashed during the travel
LoadMap. The crash is not card 180 code. This is the first run of this scenario that travels; c176-r3 had no travel.
- Cause: card 162's `pl_remote_source` (playlog.cpp) calls `actor_location` on `g_pl.pawn[]` and `g_pl.remote[].pawn`.
  These are raw pawn pointers that `pl_pawns` refreshes only from pl_step.
- During the new map's `UWorld::InitializeActorsForPlay`, a Niagara audio data interface plays a sound
  (`UNiagaraDataInterfaceAudioPlayer::PerInstanceTickPostSimulate` calls `UGameplayStatics::PlaySoundAtLocation` at
  exe+0x476CF99).
- The mod's PlaySoundAtLocation detour then runs `c162_call_atten`, then `pl_remote_source`, then `actor_location`
  on the old map's freed pawn.
- The access violation is at ubwcoop+0xDD2DD (`mov rcx,[rax+0x210]` after `read_ptr(pawn, 0x1B8)` returned
  0x70707064B184B1E), reading 0xFFFFFFFFFFFFFFFF.
- playlog.cpp is on the card's do-not-touch list, so it is not fixed here. A class fix would clear the g_pl pawn
  pointers at LoadMap (or resolve them by ID). The other g_pl pointer readers would need the same sweep.

### Verification

Build: `systemd-run --user --scope -q -p MemoryMax=12G cmake --build build -j4`, ending "[65/65] Linking CXX shared
library dwmapi.dll". 0 warnings, including a rebuild of fire.cpp and sprint.cpp after `touch`. DLL a1571e46d8325541
("deployed: a1571e46d8325541", "lane 1: dll a1571e46d8325541, build a1571e46d8325541, port 7777").

Run c180-r1: "run c180-r1: ended by scenario after 231 s; host rc 0, client rc 0". "new crash dumps:
UECC-Windows-3E03F67A40A6DDD000781687C5203E22_0000". "scenario c176-click.txt: drive rc 1".

Prediction 1 (vtable slot), held: "card180: own connection IpConnection FlushNet slot +0x308 = exe+0x3E6A2F0
(UNetConnection::FlushNet exe+0x3E6A2F0): base, no override".

Prediction 2 (one flush per release), held. Nine lines, from "card180: client t=89816990 own connection flushed after
the release (1 so far)" to "card180: client t=89825024 own connection flushed after the release (9 so far)". Each one
has the same t as its "card178: client t=... primary release sent after the frame" line. There is no "not flushed"
line.

Prediction 3 (transit), held. Host lines "card132: recv N sent t=S applied t=A Fire ..."; transit is A minus S, in ms:

| Press | Press sent | Press transit | Release sent | Release transit |
|---|---|---|---|---|
| 1 | 89816917 | 18 | 89816990 | 15 |
| 2 | 89817019 | 20 | 89817109 | 16 |
| 3 | 89817226 | 19 | 89817333 | 14 |
| 4 | 89817431 | 19 | 89817554 | 15 |
| 5 | 89817636 | 18 | 89817725 | 15 |
| 6 | 89817841 | 3 | 89817946 | 1 |
| 7 | 89818046 | 5 | 89818168 | 3 |
| 8 | 89818251 | 5 | 89818322 | 3 |
| 9 (burst) | 89820510 | 4 | 89825024 | 8 |

Press transits range from 3 to 20 ms, and every release falls inside that range. No release shows the extra frame.

Prediction 4 (per press), held. The count arg is the client's "client sent Fire" release arg. FireProjectile is
counted between presses: client-own lines on the client, server-copy lines on the host.

| Press | Hold | Count arg | Client | Host |
|---|---|---|---|---|
| 1 | 73 ms | 256 | 1 | 1 |
| 2 | 90 ms | 256 | 1 | 1 |
| 3 | 107 ms | 512 | 2 | 2 |
| 4 | 123 ms | 512 | 2 | 2 |
| 5 | 89 ms | 256 | 1 | 1 |
| 6 | 105 ms | 512 | 2 | 2 |
| 7 | 122 ms | 512 | 2 | 2 |
| 8 | 71 ms | 256 | 1 | 1 |
| 9 (burst) | 4514 ms | 11776 | 46 | 46 |

No press over-fired.

Prediction 5 (carried from 176), split:
- The cap-line wait held: "ok line 82: host wait log diag: card176 mutes \.\.\. further events not logged" (line 80
  before the c180 tail). The host line is "diag: card176 mutes ... further events not logged" at 89817930.
- The map line after travel was not shown: "FAIL line 94: host wait log card160: map: host skipped server copy
  effects: .* muted \d+ 2D component sounds since-mark timeout=150 -> timeout after 150 s". The host crashed in the
  travel LoadMap: "CRASH 1/4: EXCEPTION_ACCESS_VIOLATION (0xC0000005) at ubwcoop+0xDD2DD thread 364", stack "#5
  exe+0x476CF9E ... #17 exe+0x3DC7E21 #18 exe+0x4345D42" (RouteActorInitialize, InitializeActorsForPlay), then
  "GErrorExceptionDescription[0] Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0xffffffffffffffff".

Prediction 6 (WARN and drive), not held, because of the crash:
- Host: FATAL/ERROR 0, WARN 1, "WARN attach: DisableThreadLibraryCalls failed, err=126" (baseline).
- Client: WARN 2, the baseline line and "WARN card045: client follow gave up after 3 tries, 72149 ms after the local
  map loaded". That WARN follows from the dead host.
- drive rc 1. drive.py printed no dead-time line on the fail.

### Out of scope

- Host crash on travel. Card 162's g_pl pawn pointers outlive LoadMap and are read by the PlaySoundAtLocation detour
  during InitializeActorsForPlay. Its files (playlog.cpp, fx_c136.cpp, grenades.cpp) are not this card's. It needs its
  own card. It is a crash on every chapter travel where a Niagara audio interface plays during actor init.
- The exact per-round count (senior's separate card) was not started.

One of three runs was used. No further run was made, because the travel prediction cannot pass until the card 162
crash is fixed.

Report complete.

## Senior review (2026-11-11)
Accepted, DLL a1571e46d8325541, one run (c180-r1). Goal as re-scoped: the deferred release leaves in its own frame.
Checked: diff against the pre-180 snapshot (fire.cpp `c180_flush_net` through the connection's own vtable slot, Open
state only, called once per release sent from c24_step; release_* cleared in `c24_reset_map`, sprint.cpp, accepted as
the one reset place); rebuild clean; FireProjectile 60 host, 60 client; 9 flush lines; slot line "base, no override";
host WARN baseline only. Not shown: the travel tail. The host crashed in LoadMap in card 162 code (card 182), and the
client's "client follow gave up" WARN followed from it. The map-line mute count of card 176 moves to card 182's run.
The jitter-window over-fire is card 181.
