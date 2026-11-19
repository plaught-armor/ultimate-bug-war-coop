# Card 026: Mod channel with binary frames, both directions

Status: done 2026-11-03.

Read `docs/STATE.md` first: the "Mod code rules" section, the "Runtime facts" line about `tools/run_pair.sh`, and row 024. Then read `docs/design/coop-channel-lobby-downed.md` section 1 (rewritten 2026-11-03: friends-only trust, binary frames, send on change only) card 024's senior review, and the senior reviews of cards 028 and 033 (sprite body, hidden first-person meshes). Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

The lobby, ready-up and downed state all need a channel in both directions. Card 024 built the client-to-host direction with text verbs, a 250 ms heartbeat, a 750 ms guard and capped reject logging. The user decided (2026-11-03) that the co-op is friends only and that payloads must be small and fast. This card turns the channel into the format the design doc specifies and adds the host-to-client direction.

## Do

1. **Frame format.** A frame is the `FString` `\x1F` `ubw1:`, then one opcode `wchar_t`, then that opcode's fixed number of argument `wchar_t`s, then the terminator. Define the opcodes in one `enum class` and one table of argument counts. The receiver checks `Num` against the table, compares the prefix with one `memcmp`, and dispatches with a `switch`. Frames that are malformed or unknown are dropped without logging.
2. **Client to host.** Move card 024's fire relay to the opcode `Fire` with one argument (0 or 1).
   - Remove the 250 ms heartbeat and the 750 ms guard. RPCs are reliable and ordered.
   - Keep the release on pawn destroy and weapon change. Also release on `AGameModeBase::Logout` (0x03C60E60) for that PC's pawn.
   - Remove the reject logging.
   - Keep one capped log line per relay press and release for proof.
3. **Host to client.** Send through the target PC's `ClientMessage` (exported stub 0x0403B500, with the type `FName` set to None and the lifetime 0). On the client, detour `ClientMessage_Implementation` (0x0403B5F0): swallow frames that have the prefix, and pass everything else to the original.
   - No C++ class overrides `ClientMessage_Implementation` (PDB publics: only `APlayerController`), so the detour sees every call.
   - Send only to remote PCs. On a listen server the host's own PC is local, and `ClientMessage` on it would run the implementation on the host.
4. **Host fire to client (first real use).** The client cannot see the host fire (card 028 review, follow-up 2). On each press and release of the host's own fire, send `Fire` (0 or 1) to every remote PC. On the client, set the card 028 sprite of its copy of the host pawn to Attack01 while the value is 1. That pawn has no weapon actor on the client (card 033), so this drives the sprite only; there is no weapon to press. Log one capped line per edge on each side.
5. **Smoke test.** The client sends `Ping n` (one argument, a counter) every 5 s. The host answers `Pong n` to that PC only. Both sides log the round trip in ms.
6. **Prove it.** Show with log lines:
   - every ping gets its pong, and report the round-trip times;
   - fire relay parity matches card 024c (relayed shot count against the client's local count over two 2 s bursts);
   - host fire reaches the client: each host press logs a client receive, and one `UBWCOOP_C28_SHOTS=1` shot on the client shows the host sprite in Attack01;
   - no frame reaches the HUD (a screenshot during a ping, taken with the game's `shot` command as in card 025) or `ChangeName` (player names unchanged).

## Done when

- The proofs in step 6 hold.
- The fire relay sends exactly 2 frames per burst: a press and a release, with no heartbeat. The same holds host to client.
- Clean build (`/W4 /WX`, `--clean-first`), 84 exports, KERNEL32-only imports. No new crash dumps. Every new function is at most 60 lines.

## Runs

Pair run with the card 033 env (card 028 env, `UBWCOOP_GRANT_WEAPON=MoritaMk1` and `UBWCOOP_C28_SHOTS=1` on both sides). The host fire for step 4 comes from the `UBWCOOP_HOST_SPRINT=1` parity script (`mod/src/autoinput.cpp`: `fire-ground` at 16000 ms and `fire-up` at 18500 ms, 2 s each), which the card 028 env already sets. No new driver is needed. Never pass `-nosound`.

## Report

**Result: done.** Every step 6 proof holds in `card026c`, with one note: the relay count is 41 against the client's 42 local shots (see Parity). Build: clean `/W4 /WX --clean-first`, 0 warnings, 84 exports, KERNEL32-only imports. A line-count check of `hooks.cpp` finds no function over 60 lines. No new crash dumps in any run.

### What changed (`mod/src/hooks.cpp`, `tools/symbols.txt`)

- **Frame layer.**
  - `enum class ChanOp : wchar_t { None, Fire, Ping, Pong, Count }`, with argument counts in `kChanArgs[] = {-1, 1, 1, 1}` (a static_assert ties the table to the enum).
  - `chan_frame` writes the opcode and argument slots into a static, prefix-prefilled buffer `g_chan_wire`.
  - `chan_prefixed` is the single memcmp. `chan_parse` checks the opcode range, `Num == prefix + 1 + nargs + 1`, the terminator, and that no argument is 0.
  - Any prefixed frame is swallowed. A malformed one is dropped without logging.
- **Senders.**
  - `chan_to_host(pc, op, arg)` uses FindFunctionChecked("ServerChangeName") plus ProcessEvent, as in card 024.
  - `chan_to_client(pc, op, arg)` calls the `APlayerController::ClientMessage` stub (0x0403B500) with type 0 (NAME_None) and lifetime 0. It sends only when `sprint_side(pc) == kSideServerCopy`, which means a remote PC. The host's own PC is never sent to.
- **Receivers.**
  - `detour_server_change_name` handles Fire, Ping and Pong on the host. Fire targets only the receiving PC's own server-copy MI pawn, as before.
  - The new `detour_client_message` (hook on `ClientMessage_Implementation` 0x0403B5F0) handles them on the client. Each side ignores frames that arrive on the wrong side.
  - Unprefixed strings go to the originals.
- **Fire edges, both sides.** `c26_own_fire` replaces `c24_client_tag` and is called from the AbilityInputTagPressed/Released detours. It filters to InputTag.Fire on the own pawn's weapon ASC: `kSideClientOwn` on the client, `kSideHostOwn` on the host. It sends one Fire 1 per press edge and one Fire 0 per release edge. The client sends to the host; the host sends to every remote PC.
  - `c26_own_check` (per tick) sends Fire 0 if the held own pawn is gone or replaced.
- **Removed:**
  - the 250 ms heartbeat, the 750 ms guard and the client 200 ms gap release;
  - the reject logging and the text verbs (`c24_body_is`, `c24_receive`, `c24_is_channel`);
  - `C24Send` and `C24Relay.last_beat_ms`.
- **Kept:** the host relay release on pawn gone or weapon change (`c24_host_guard`), the relay press checks, and one capped relay line per press and release.
- **Added:** release on `AGameModeBase::Logout` (0x03C60E60, `detour_logout`) for the leaving controller's pawn, if it holds a relay.
- **Card 028 sprite.** `c26_firing(pawn)` drives Attack01:
  - host: a held relay on that server copy (unchanged behaviour);
  - client: the host's last Fire frame, applied to the `kSideOther` pawn.
  - The card 028 key-change log field `relay` is now `firing`.
- **Ping/pong.** Every 5 s the client pings the host and the host pings every remote PC. The receiver answers Pong n to the sender only. Each side logs `ping n sent` and `pong n ... rtt X ms` (capped at 80 lines).
- **HUD proof.** With `UBWCOOP_C28_SHOTS=1`, the client requests a `shot` while handling host ping 3.

### Deviations

- **Argument encoding is value + 1, and opcodes start at 1.** A payload wchar_t of 0 would cut the string: a loaded FString is trimmed at its first null, and FString serialization's pure-ANSI check stops there. Values are kept below 0x7FFF, out of the surrogate range. The ping counter wraps there.
- **Pings run both ways.** The card asks for client pings with host pongs. I added host pings with client pongs, so the host-to-client direction also gets its own RTT proof. Cost: one frame each way every 5 s.
- **Client env adds `UBWCOOP_AUTOINPUT_DELAY_MS=40000`, and the run uses `DONE_PATTERN='autoinput: RequestEngineExit'`.**
  - Run `card026a` (default done pattern `rot[19]`) ended at 87 s, before the host landed.
  - Run `card026b` (240 s cap) had the client's script call RequestEngineExit at 494222089. The host's script only started at 494219787 (it lands about 30 s after the client), so its fire bursts never reached a client.
  - The 40 s delay puts both scripts' bursts in the same window. Both earlier runs were clean (rc 0/0, no FATAL/ERROR/WARN, no crash dumps), and their client-to-host and ping results match `card026c`.

### Host fire driver

`UBWCOOP_HOST_SPRINT=1`, the card 020 parity script in `mod/src/autoinput.cpp`. It holds LeftMouseButton for 2 s at 16000 ms and again at 18500 ms, starting 5000 ms after the host's landed pawn is possessed. No new driver.

### Run `card026c`

- **Command:** `DONE_PATTERN='autoinput: RequestEngineExit' HOST_EXTRA_ENV="UBWCOOP_AUTODROPSHIP=1 UBWCOOP_HOST_SPRINT=1 UBWCOOP_C28_SHOTS=1 UBWCOOP_GRANT_WEAPON=MoritaMk1" CLIENT_EXTRA_ENV="UBWCOOP_AUTOINPUT=1 UBWCOOP_AUTOINPUT_DELAY_MS=40000 UBWCOOP_C28_SHOTS=1 UBWCOOP_GRANT_WEAPON=MoritaMk1" tools/run_pair.sh card026c 260`. The dll was deployed in `card026a` (DEPLOY=1, deploy hash 4614fd38930fd537) and not rebuilt after.
- **Result:** ended by done after 134 s, rc 0/0. Host 2713 lines, client 2469 lines, FATAL/ERROR 0, WARN 0, map loaded 2/2. The client detach is its own scripted exit. No new crash dumps.
- **Logs:** `logs/card026c-{host,client}.log`.

**Ping/pong.** Every ping got its pong.

| Side | Pings | Pongs | RTT min / avg / max |
|---|---|---|---|
| Client | 15 | 15 | 13 / 17.9 / 23 ms |
| Host | 14 | 14 | 13 / 17.1 / 23 ms |

Runs a and b also show every ping answered, at 13 to 34 ms.

**Client-to-host fire: 2 frames per burst.** Each frame on one side has exactly one line on the other.

| Burst | Client sent Fire 1 | Host received / relay press | Client sent Fire 0 | Host received / relay release |
|---|---|---|---|---|
| 1 | 494538821 | 494538835 | 494540820 | 494540827 |
| 2 | 494541320 | 494541327 | 494543320 | 494543327 |

**Parity with card 024c.**
- Shots in each burst window:

  | Burst | Server-copy FireProjectile (host) | Client local FireProjectile |
  |---|---|---|
  | 1 | 20 | 21 |
  | 2 | 21 | 21 |

  Total: 41 against 42 (card 024c: 21/21).
- The missing shot is timing, not a lost frame:
  - The press reached the host 14 ms after the client's press, and the release only 7 ms after. That cut the host's hold to 1992 ms.
  - The client's 21st shot fired on its release frame (494540820). The host's next shot was due about 494540836, after the release at 494540827.
  - The heartbeat-era relay had the same exposure.
- Origins follow the server copy, as in card 024c.

**Host-to-client fire: 2 frames per burst.** The host sends to the one remote PC only.

| Burst | Host sent Fire 1 | Client received | Host sent Fire 0 | Client received |
|---|---|---|---|---|
| 1 | 494534656 | 494534666 | 494536661 | 494536665 |
| 2 | 494537161 | 494537169 | 494539162 | 494539171 |

- **Sprite.** On the client, the host's sprite key went Idle -> Attack01 at 494534669 and 494537254, and back to Idle between bursts.
- **Shots.** The `UBWCOOP_C28_SHOTS` shots of the host in Attack01 are client SHOT 4 (`logs/card026c-client-4-Attack01-front.png`, distance 828) and SHOT 7 (`logs/card026c-client-7-Attack01-front.png`, distance 437, muzzle flash visible).
- **Host's view.** Of the relayed client: `logs/card026c-host-4-Attack01-front.png`.
- **Mapping.** Screenshots were matched to log lines by file mtime. Both instances write to one `Saved/Screenshots/Windows` folder. Client SHOT 5 and SHOT 6 fired in the same millisecond and produced one file (`ScreenShot00132`), so one of them is lost. That is a card 028 tooling quirk.

**No frame reaches the HUD or ChangeName.**
- **HUD.** `logs/card026c-client-hud-ping3.png` was taken while the client handled host ping 3 (494496325). It shows no message text.
- **Names.** Player names are unchanged all run: host pc 0 `'257'`, host pc 1 `'258'`, client pc 0 `'258'`. The first line on each side is the menu default `'Player'`, as in card 024c.

### Not exercised

- **Logout release.** The client exits with no relay held, so `detour_logout` found nothing to release. It is hooked (`AGameModeBase_Logout hooked`) and returned cleanly. A held-fire disconnect was not tested.
- **The own-pawn-changed release** (`c26_own_check`) did not fire.

### Limitations

- **Three or more players.** The client applies the host's Fire to every `kSideOther` MI pawn. With 3+ players it cannot tell the host's pawn from other remote clients' pawns, which are also simulated proxies on it. That needs a pawn id in the frame, or host-to-client forwarding of each player's fire.
- **Only one host Fire bit is kept.** Fire state from other clients is not forwarded to clients.

### Observed, not in scope (card 028 placement)

The user watched the run and reports that both remote sprites are sunk to about the knees: the client's sprite on the host and the host's sprite on the client. `logs/card026c-host-4-Attack01-front.png` shows it clearly. My first read of `card026c-client-7-Attack01-front.png` was that its feet were on the ground; the user corrected that.

Because both sides are affected, the cause is probably the card 028 sprite's vertical anchor, not anything specific to server copies. It may be anchored at the capsule centre or the mesh origin with too large a downward offset, rather than at the capsule bottom (location z minus the capsule half-height). Not investigated here; recommend a small card 028 follow-up card on sprite z placement.

## Senior review (2026-11-03)

Accepted.

Verified:
- Run: `card026c` ended by done after 134 s, rc 0/0, FATAL/ERROR 0, WARN 0, no new crash dumps (`logs/card026c-run.txt`). The deployed `dwmapi.dll` hash is 4614fd38930fd537, matching the report.
- Ping/pong: the client sent 15 pings and the host 14; every one has its pong, with RTT 13 to 23 ms on both sides.
- Client to host: there are exactly two "client sent Fire" lines per burst, and each has one "host received" line plus one relay press or release, 7 to 14 ms later.
- Parity: in the burst windows the host logs 20 and 21 server-copy FireProjectile lines against the client's 21 and 21 local ones. Four host-own shots overlap window 1 and are excluded. The missing shot is the timing edge the report explains.
- Host to client: two "host sent Fire" lines per burst, each with one "client received" line 4 to 10 ms later. The client's sprite of the host goes Idle to Attack01 and back for both bursts.
- Screens: client SHOT 7 shows the host sprite firing, with a muzzle flash. The ping-3 HUD shot has no message text. Host SHOT 4 shows the relayed client firing.
- Code: `chan_parse` rejects opcodes out of range, a wrong `Num`, a missing terminator and zero arguments. `detour_client_message` sends unprefixed strings to the original and swallows prefixed ones. `detour_logout` releases before calling the original. No function in `mod/src/*.cpp` is over 60 lines.

Accepted deviations: value + 1 argument encoding, symmetric pings, and the client's 40 s autoinput delay with the `RequestEngineExit` done pattern.

Follow-ups (not blocking):
- Logout release and own-pawn-changed release are untested. Exercise them when a disconnect card exists.
- Three or more players need a pawn id in host-to-client frames. Note this in the lobby design before a third player is tried.
- Sprite sinks to the knees on both sides (`card026c-host-4-Attack01-front.png`). This is a card 028 z-anchor follow-up, probably anchored at the capsule centre instead of its bottom.
