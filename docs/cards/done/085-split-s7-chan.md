# Card 085: Split S7: the chan transport to `ue/chan.*`, the op set to `ubw/chan_ops.h`, dispatch by table

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: sent 2026-11-05 16:25 (senior). Part of the 19:00 batch (see card 084): lighter proof, no baseline runs.

Read the card 084 Review, the S7 row of `docs/split-map.md`, and `hooks.cpp` from the chan header comment (~6200, "Wire string") to `detour_logout` (~6830). Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

The chan is the mod's own wire: a prefixed FString over `ServerChangeName` (client to host) and `ClientMessage` (host to client). The frame format, the send paths and the two receiving detours are generic. Only the op set and what each op does are UBW. Today both receivers are if-chains with "everything else" else branches, so adding an op means editing four places and getting the exclusion lists right. A table indexed by op removes that.

## Scope

### 1. `ue/chan.{h,cpp}`, namespace `ue::chan`

- **Types and constants:**
  - `WireString`, `ChanArgs`, `kChanPrefix`, `kChanPrefixLen`, `kChanOpAt`, `kChanMaxArgs`, `kChanMaxFrame`, `kChanArgMax`, `kChanTextMax`, `kChanTextFrame`, `kChanArgsNamed` and `kChanArgsText`.
  - Rename `kChanObjectiveHead` to `kChanNamedHead`, and `kChanObjectiveFrame` to `kChanNamedFrame`. They describe the named-frame format, not the objective op. `kChanNameMax` keeps its name.
  - `kChanMaxChars` (the card 024 PlayerState buffer) stays in `hooks.cpp`.
- **Functions:**
  - `chan_frame`, `chan_parse`, `chan_prefixed`, `chan_name_chars`, `chan_text_chars`, `chan_text` and `chan_text_frame`;
  - `chan_send_host`, `chan_send_client`, `chan_to_host`, `chan_to_client` and `chan_to_client_args`;
  - `g_chan_wire`.
- **Op type.** In `ue/`, an op is a raw `wchar_t`, and 0 means none. `ue/` must not see `ChanOp`.
- **Arg counts.** `chan_frame` and `chan_parse` read the arg-count table through the registered ops (see below), not through `kChanArgs` directly.
- **The `ServerChangeName` FName cache.** It moves out of `C24State::change_name_fname` and becomes a file-local static in `ue/chan.cpp`. Remove the field from `C24State`.
- **`sprint_side` in `chan_send_client`.** Card 084 keeps `sprint_side` in `hooks.cpp`. Pass the "remote PC" test in through the registered ops as `bool (*is_remote_pc)(const void* pc)`, and add it to `ChanOps`. Do not copy `sprint_side`.
- **The two detours.** `detour_server_change_name` and `detour_client_message` move here, with `g_orig_server_change_name`, `g_orig_client_message` and `g_client_message`. They stay `extern`, resolved by the unchanged `kHooks`/`kCalls` rows. `detour_logout` stays in `hooks.cpp`.
- **Registration:**

  ```cpp
  using ChanHandler = void (*)(void* pc, const WireString* s, const ChanArgs& args);
  struct ChanOps {
      std::span<const int32_t> arg_counts;  // by op; -1 = not a valid op
      std::span<const ChanHandler> host;    // by op; run by ServerChangeName when *client_session is false
      std::span<const ChanHandler> client;  // by op; run by ClientMessage when *client_session is true
      const bool* client_session;
  };
  void set_ops(const ChanOps& ops);
  ```

  `install()` calls `set_ops` before any hook is enabled, next to `set_engine_steps`.
- **The detours keep today's flow:**
  1. If the string is not prefixed, call the orig and return.
  2. Otherwise parse it.
  3. If the op is not 0, is below the table size, the side matches, and the handler is not null, call the handler.
  4. Any prefixed frame is swallowed, as today.
- **The header comment:**
  - keeps the transport text (frame layout, value + 1, the surrogate range, the wire string ownership note with `0x0403B5DF`);
  - drops the per-op card paragraphs, which go to `ubw/chan_ops.h`;
  - says "named frame" and "text frame" without card numbers.

### 2. `ubw/chan_ops.h`, namespace `ubw::chan`

- **Contents:**
  - `enum class ChanOp : wchar_t`, with values unchanged and in the same order;
  - `kChanArgs`, with its `static_assert`;
  - the per-op card paragraphs from today's header comment, word for word.
- **Typed wrappers.** One-line inline wrappers take `ChanOp`, so the call sites in `hooks.cpp` keep their spelling: `chan_to_host(pc, ChanOp::X, arg)`, `chan_to_client`, `chan_to_client_args`, `chan_frame`, `chan_text_frame` and `chan_parse` (which returns `ChanOp`). No call site outside the chan code changes.
- **Header only this card.** The handlers and the two tables use `hooks.cpp` internals (`g_c24`, `c44_host_fire`, `sprint_side` and others), so they stay in `hooks.cpp` until the F card that moves the card 024/026 code. Update the S7 row of `docs/split-map.md` to say so.

### 3. The tables, in `hooks.cpp`

- **Build them by index.** `constexpr std::array<ue::chan::ChanHandler, size_t(ChanOp::Count)>` comes from a `consteval` builder that assigns `t[size_t(ChanOp::X)] = handler`. No position depends on the order of the initializers.
- **Host table, the exact current behaviour:**
  - `UseName`, `AmmoMirror`, `MechEnter` and `MechLeave` go to their current receivers;
  - `Fire`, `Use`, `Equip`, `Ping` and `Pong` go to the bodies from `c26_host_receive`;
  - every other op is null. Today `Gib`, `Anim`, `Down`, `Warp`, `Objective` and `ObjectiveActors` reach `c26_host_receive`, which does nothing with them.
- **Client table:**
  - `Anim` through `GrenadeExplode`, as in today's chain;
  - `Fire`, `Ping` and `Pong` go to the bodies from `c26_client_receive`;
  - `UseName`, `AmmoMirror`, `MechEnter`, `MechLeave`, `Use` and `Equip` are null.
- **Splitting the two receivers.** `c26_host_receive` and `c26_client_receive` become one handler per op. Each keeps its body unchanged, and each takes `args.v[0]` where it took `arg`. Host `Pong` still runs `c26_pong`, then `c49_first_pong`, then `c50_pong`, in that order.
- **Adapters.** Receivers whose signature does not match `ChanHandler` get one-line adapters next to the table, for example `c57_receive_equip(pc, args.v[0])` and `c70_receive(s)`. Do not change the receivers themselves.

## No behaviour change

- Every log line keeps its text.
- No op is renumbered, and the wire format is byte-identical.
- Every op reaches the same receiver on the same side as today, and no op that is ignored today gets a handler.
- Put the before/after op-to-receiver mapping, for both sides, as a table in the Report.

## Proof (lighter)

- **Build:** `systemd-run --user --scope -q -p MemoryMax=12G ninja -C build -j4`, with no warnings.
- **fnlen:** no function over 60 lines in `hooks.cpp` or `ue/chan.cpp`.
- **Routes:**
  - the base routes `host-morita` (`DEPLOY=1`), `client-zipline`, `c47-gates` and `c75-solo`;
  - the relay routes `c57` (Equip, client to host), `c72-mirror` (AmmoMirror text, client to host), `c74-grenades` (GrenadeSpawn/Explode, host to client) and `c75b-pods` (MechEnter/Leave).
- **Log check:** for `c74-grenades` and `c72-mirror`, compare the sorted unique `card0(24|26|50|72|74):` line texts, with numbers replaced by `N`, against the newest earlier log of the same route. Show the command and any differences, with a reason for each.
- `logsum` gives ERROR/FATAL 0, rc 0, and there is no new crash dump (the newest is still c79a's, 13:37).

## Done when

- `ue/chan.*` and `ubw/chan_ops.h` exist, and `grep -n 'c[0-9][0-9]\|card' mod/src/ue/chan.*` is empty.
- `grep -n 'op == ChanOp::' mod/src/hooks.cpp` is empty.
- `c26_host_receive` and `c26_client_receive` are gone.
- All eight routes pass, and the split-map S7 row is updated.
- The Report gives the `hooks.cpp` line count before and after, and the mapping table. It ends with "Report complete."

## Report

### What moved

- **`mod/src/ue/chan.h` / `ue/chan.cpp` (new, 111 + 214 lines, namespace `ue::chan`):**
  - `WireString`, `ChanArgs` and the constants `kChanPrefix`, `kChanPrefixLen`, `kChanOpAt`, `kChanMaxArgs`, `kChanMaxFrame`, `kChanArgMax`, `kChanTextMax`, `kChanTextFrame`, `kChanArgsNamed`, `kChanArgsText`, `kChanNameMax`, plus the renamed `kChanNamedHead` / `kChanNamedFrame`.
  - The functions `chan_frame`, `chan_parse`, `chan_prefixed`, `chan_name_chars`, `chan_text_chars`, `chan_text`, `chan_text_frame`, `chan_send_host`, `chan_send_client`, `chan_to_host`, `chan_to_client` and `chan_to_client_args`, and the buffer `g_chan_wire`. In `ue/`, an op is a raw `wchar_t`, and 0 means none.
  - `detour_server_change_name`, `detour_client_message`, `g_orig_server_change_name`, `g_orig_client_message` and `g_client_message`. These are extern, and the unchanged kHooks rows (27197-27198 before) and kCalls row (27439 before) resolve them through `using namespace ue::chan`.
  - The ServerChangeName FName cache is now a file-local `g_change_name_fname`, and `C24State::change_name_fname` is gone. The comment of `C24State`'s next field, `fire_tag`, takes over the "FName values (not game pointers); 0 = not built yet" note.
  - `ChanOps` / `set_ops` are as specified, plus `is_remote_pc` and `send_skipped` (see deviation 1).
  - The two detours keep today's flow:
    - an unprefixed string goes to the orig;
    - a prefixed one is parsed and dispatched when the op is not 0, is below the table size, the side matches (`!*client_session` on the host, `*client_session` on the client) and the handler is not null;
    - every prefixed frame is swallowed.
  - The header comment keeps the transport text: the frame layout, value + 1, the surrogate range, and the wire-string ownership note with `0x0403B5DF`. It describes "named frame" and "text frame" and carries no card numbers.
- **`mod/src/ubw/chan_ops.h` (new, header only, namespace `ubw::chan`):**
  - `enum class ChanOp : wchar_t`, with its values unchanged;
  - `kChanArgs` with its static_assert;
  - the card 049 / 050 / 057 paragraphs, word for word;
  - the one-line typed wrappers `chan_to_host`, `chan_to_client`, `chan_to_client_args`, `chan_frame`, `chan_text_frame` and `chan_parse` (which returns `ChanOp`).
- **`hooks.cpp`:**
  - It includes both headers and adds `using namespace ue::chan; using namespace ubw::chan;` next to the other `ue::` using-directives.
  - `kChanMaxChars` stays, and the 19097 / 19187-19201 / 19963 uses now say `kChanNamed*`.
  - `c26_host_receive` and `c26_client_receive` are split into `c26_host_fire`, `c26_host_use`, `c26_host_ping`, `c26_host_pong`, `c26_client_fire`, `c26_client_ping` and `c26_client_pong`. Each body is unchanged, with `args.v[0]` where it used `arg`. Host Pong still runs `c26_pong`, then `c49_first_pong`, then `c50_pong`.
  - Four one-line template adapters (`chan_pc_s`, `chan_s`, `chan_pc_args`, `chan_pc_arg0`) fit the receivers to `ChanHandler`. No receiver changed.
  - Two `consteval` builders assign by index into `std::array<ChanHandler, kChanOpCount>`, giving `kChanHost` and `kChanClient`.
  - `constexpr ChanOps kChanOps{kChanArgs, kChanHost, kChanClient, &g_client_session, c26_is_remote_pc, c26_send_skipped}`.
  - `install()` calls `set_ops(kChanOps);` right after `set_engine_steps(kEngineSteps);`, before any hook is enabled.
- **Build files:** `CMakeLists.txt` adds `src/ue/chan.cpp` after `src/ue/net.cpp`, and `ue/core.h` includes `ue/chan.h`.
- **`docs/split-map.md` S7 row:** now says `ue/chan.cpp` is the transport and `ubw/chan_ops.h` is header only (enum, `kChanArgs`, wrappers). The handlers and the two tables stay in `hooks.cpp` until the F card that moves the card 024/026 code.
- **`hooks.cpp` line count:** 27689 before, 27490 after.

### Op to receiver mapping

"Before" is the if-chain in the detour plus the `c26_*_receive` branch. "After" is the table entry. A dash means the frame is swallowed with no call.

| Op | Host before | Host after | Client before | Client after |
|---|---|---|---|---|
| 1 Fire | c26_host_receive Fire branch | c26_host_fire (same body) | c26_client_receive Fire branch | c26_client_fire (same body) |
| 2 Ping | c26_host_receive: chan_to_client Pong | c26_host_ping | c26_client_receive: chan_to_host Pong, c42_ping, C28 shot | c26_client_ping |
| 3 Pong | c26_host_receive: c26_pong, c49_first_pong, c50_pong | c26_host_pong (same order) | c26_client_receive: c26_pong | c26_client_pong |
| 4 Gib | c26_host_receive (no branch) | - | c42_receive_gib(pc,args) | chan_pc_args<c42_receive_gib> |
| 5 Anim | c26_host_receive (no branch) | - | c41_receive_anim(pc,args) | chan_pc_args<c41_receive_anim> |
| 6 Down | c26_host_receive (no branch) | - | c43_receive_down(pc,args) | chan_pc_args<c43_receive_down> |
| 7 Use | c26_host_receive Use branch: c43_receive_use | c26_host_use (same body) | c26_client_receive (no branch) | - |
| 8 Warp | c26_host_receive (no branch) | - | c46_receive_warp(pc,args) | chan_pc_args<c46_receive_warp> |
| 9 Objective | c26_host_receive (no branch) | - | c49_receive(pc,s,args) | c49_receive (exact signature) |
| 10 UseName | c50_receive_use_name(pc,s) | chan_pc_s<c50_receive_use_name> | excluded | - |
| 11 Weapons | excluded | - | c50_receive(pc,s) | chan_pc_s<c50_receive> |
| 12 Equip | c26_host_receive: c57_receive_equip(pc,arg) | chan_pc_arg0<c57_receive_equip> | c26_client_receive (no branch) | - |
| 13 Activate | excluded | - | c65_receive_activate(pc,s) | chan_pc_s<c65_receive_activate> |
| 14 Ammo | excluded | - | c71_receive(pc,s) | chan_pc_s<c71_receive> |
| 15 AmmoSet | excluded | - | c72_receive_set(pc,s) | chan_pc_s<c72_receive_set> |
| 16 AmmoMirror | c72_receive_mirror(pc,s) | chan_pc_s<c72_receive_mirror> | excluded | - |
| 17 ObjectiveActors | c26_host_receive (no branch) | - | c70_receive(s) | chan_s<c70_receive> |
| 18 GrenadeItem | excluded | - | c73_receive(pc,s) | chan_pc_s<c73_receive> |
| 19 PickupGone | excluded | - | c56_receive(pc,s) | chan_pc_s<c56_receive> |
| 20 GrenadeSpawn | excluded | - | c74_receive(pc,s) | chan_pc_s<c74_receive> |
| 21 GrenadeExplode | excluded | - | c74_receive_explode(s) | chan_s<c74_receive_explode> |
| 22 MechEnter | c75_receive_enter(pc,s) | chan_pc_s<c75_receive_enter> | excluded | - |
| 23 MechLeave | c75_receive_leave(pc,s) | chan_pc_s<c75_receive_leave> | excluded | - |
| 0 None, >= 24 | swallowed | swallowed (op 0 / out of table) | swallowed | swallowed |

Every op reaches the same receiver on the same side as before. No op that was ignored before gets a handler.

### Deviations

1. **`ChanOps::send_skipped`.** `chan_send_host` logged `"card026: WARN client send of op %u skipped: ..."`. That text would break the `card` grep on `ue/chan.*`, and every log line must keep its text. So `ChanOps` gained `void (*send_skipped)(unsigned op, const void* pc, const void* function, int32_t num)`. `hooks.cpp` implements it as `c26_send_skipped`, which logs the same format string with the same arguments. `chan_send_host` calls it in place of the old `log::line`.
2. **`chan_parse` lookup.** `ubw::chan::chan_parse` has the same parameters as `ue::chan::chan_parse`, so an unqualified call from `hooks.cpp` would be ambiguous: ADL on `WireString` brings in the `ue::chan` one. There are no callers outside the detours, which moved, so nothing is ambiguous today. The `chan_ops.h` comment says to qualify it as `ubw::chan::chan_parse`. The other wrappers take `ChanOp`, which does not convert implicitly, so their overloads stay unambiguous.
3. **`kChanArgsNamed` / `kChanArgsText` in both namespaces.** `ubw::chan` aliases the `ue::chan` values, because `kChanArgs` needs them. Unqualified lookup in `hooks.cpp` finds the `ubw::chan` aliases first. The values are identical.
4. **`chan_ops.h` note.** It adds one line saying that `kChanObjectiveHead`, named in the verbatim card 049 paragraph, is now `ue::chan::kChanNamedHead`.

### Proof

- **Build:** `systemd-run --user --scope -q -p MemoryMax=12G ninja -C build -j4` gives rc 0 with no warnings (/W4 /WX). The DLL is d8e435342dcd9230.
- **fnlen:** `hooks.cpp` has 1409 functions and none over 60; `ue/chan.cpp` has 18 and none over 60.
- **Done-greps:**
  - `grep -n 'c[0-9][0-9]\|card' mod/src/ue/chan.*` is empty;
  - `grep -n 'op == ChanOp::' mod/src/hooks.cpp` is empty;
  - `c26_host_receive|c26_client_receive` gets 0 hits.

### Routes

DLL d8e435342dcd9230 for all eight runs, on two lanes in parallel (lane 1: game/prefix, port 7777; lane 2: game2/prefix2, port 7778). Every run gives rc 0, has FATAL/ERROR 0 on both sides, and ends with "new crash dumps: none".

| Tag | Route | Lane | Time | Host WARN | Client WARN |
|---|---|---|---|---|---|
| c85a | host-morita DEPLOY=1 | 1 | 131 s | 0 | 0 |
| c85b | client-zipline | 1 | 130 s | 0 | 0 |
| c85c | c47-gates | 1 | 201 s | 0 | 1: card044 "client no CompleteDeath 8009 ms after StartDeath", the same as in c84e |
| c85d | c75-solo (SOLO=1) | 1 | 92 s | 0 | (solo, no client) |
| c85e | c57 DEPLOY=1 | 2 | 132 s | 1: card039 "host probe table full (32)", the same as in c67j | 0 |
| c85f | c72-mirror | 2 | 141 s | 0 | 0 |
| c85g | c74-grenades | 2 | 184 s | 0 | 0 |
| c85h | c75b-pods | 2 | 109 s | 0 | 1: card034 "client refit of BP_PlayerCharacterMI_C skipped: offsets ok, half-height 0.0, ...", also seen in c76d, c76g and c76h |

None of the three WARNs comes from the chan code.

### Log check

`c85diff.sh OLD NEW` works on each side separately:
- it greps the `card0(24|26|50|72|74):.*` texts;
- it keeps the card tag;
- after the tag, it maps `0x...` values, hex runs of 8 or more digits and signed numbers to N;
- it runs `sort -u`, then `diff`.

- **c74-grenades:** `c85diff.sh c82h c85g`. c82h is the newest earlier c74-grenades run. Host and client are both identical.
- **c72-mirror:** `c85diff.sh c72c c85f`. c72c is the newest earlier c72-mirror run, from 03:48 today. Three differences, all from later cards and none from 085:
  1. **Host lacks `card050: host ASTWeaponPickup::PickupWeapon on BP_WeaponPickup_MoritaMkN_C...`, and both sides lack `card050: ... USTSaveSystemLibrary::Save_CheckPoint(...)`.**
     - Both hooks have been gated on `g_gate_diag` since card 047, which came after c72c.
     - The pickup still happens: c85f host logs `GiveWeaponByClass(BP_WeaponDefinition_MoritaMk1_C ... server-copy)`.
  2. **The client card072 AmmoSet line gains `(held N ms)`.**
     - Card 073 changed that format after c72c.
     - The `(held N ms)` format is also in the hooks.cpp backups from c79 to c84.

### Crash dumps

The newest directory under `Saved/Crashes` in both prefix/ and prefix2/ is still `UECC-Windows-1320E1484F0D6BC6BA16FD9789FD4E6B_0000`, from 13:37:32. That is c79a's dump; no run made a new one.

Report complete.

## Senior review

Senior, 2026-11-05 16:42. **Accepted.**

- Checked: the eight drive logs end with `scenario passed`, and logsum gives ERROR/FATAL 0 for each. `grep 'op == ChanOp::\|c26_host_receive\|c26_client_receive' hooks.cpp` and the `ue/chan.*` card grep are both empty. `detour_server_change_name` keeps the flow: an unprefixed string goes to the orig, a prefixed one is parsed, dispatched by table and swallowed. Host `Pong` runs `c26_pong`, `c49_first_pong` and `c50_pong` in that order.
- The c72-mirror differences are explained by later cards (the card 047 diag gate and the card 073 `(held N ms)` text). They are not chan changes.
- Deviations 1-4 are accepted. `send_skipped` in `ChanOps` keeps the card 026 WARN text in `ubw`, which is right.
- Running routes on both lanes cut the proof time to about 10 minutes.
