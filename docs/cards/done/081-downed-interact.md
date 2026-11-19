# Card 081: A Downed or Out player cannot interact

Status: sent 2026-11-05 14:41 to junior ultimate-bug-war-coop-bb, after card 080 was accepted. Drafted 2026-11-05 (senior). Use the card 080 names (`fname_cached`, `prop_*`, `find_class`).

Read the card 079 Report ("Design 4" and "Edge to know") and its Senior review, and the card 043 and card 050/054 code: `detour_interact` (~5596), `c43_interact_edge` (~16398), `c43_is_down` (~15927), `c43_refuse_tag`, `c50_relay_use` (~22163) and `c43_kill_host` (~16315). Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

Card 079 stopped a Downed or Out pawn from entering a mech, but only at `EnterMech`. Any other E interaction still runs for that pawn:

- **The host's own pawn.** `detour_interact` passes every press to the native `UADInteractorComponent::Interact`. In c79e the Out host's E reached Interact with target `BP_BaseMech_C`. A turret (`ASTMountableTurret::OnInteraction`), a pickup or a switch would run.
- **A client.** `c50_relay_use` relays a Downed client's Use to its server copy without checking `c43_is_down`. The client's own local Interact also runs natively.
- **The prompt.** The HUD still shows "E ENTER M7" to a Downed or Out player (shots `host-c79e-again` and `client-c79f-refused`).

There is also a test edge. `c43_kill_host` damages `r.p[0].pawn`. When the host pilots a mech, that pawn is the mech, so the card 043 test starts the mech's self-destruction instead of downing the host.

## Design

1. **One gate on each machine's own press.**
   - In `detour_interact`, after `c43_interact_edge(interactor, true)`, skip `g_orig_interact` when the owner pawn `c43_is_down`. Log one capped line per refused press.
   - `c43_interact_edge` must still run first. Its client branch sends `ChanOp::Use` to the host. A Downed press does nothing there, but do not change that path in this card.
   - `detour_release_interaction` keeps calling the native release. A release with no press before it is harmless; confirm that in the log.
   - On the client, `c43_is_down` reads the client's own view (`kC43ClientSelf`). Check that a Downed client's own pawn is found there. If it is not, say so in the Report and gate on what the client does know.
2. **The host relay.** At the top of `c50_relay_use`'s press branch, refuse the relay when the server copy `c43_is_down`. Log one line, like the revive hold skip. The release branch stays as it is.
3. **The prompt: find it first, fix it only if the fix is narrow.**
   - Find what draws the "E ENTER" / interact prompt: the interactor's focus target (`kInteractorTarget`), or a widget bound to it.
   - If there is a narrow way to hide it for a down pawn, do it. One example is to clear the focus target each tick while the pawn is down, provided the native code sets it again on its own when the pawn stands up.
   - If the only fix is a widget hook, or it touches the revive prompt, do not fix it. Report what you found.
4. **`c43_kill_host` and a piloted mech.** When `r.p[0].pawn` `is_a(g_mech_class())`, skip the hit and leave `kill_tries` unchanged. Log one capped line, and retry at the next gap. The test needs no further change.
5. Points 1 and 2 run in every co-op session, with no flag. The card 079 `EnterMech` refusal stays as a second line of defence.

## Do

1. Implement the above.
2. **Proof run 1:** `c79-down-host`.
   - The Out host's E is refused at `detour_interact`.
   - The card 079 `EnterMech refused` line no longer appears, because `EnterMech` is never reached.
   - The card 043 entry is kept.
3. **Proof run 2:** `c79-down-client`.
   - The Downed client's own Interact is refused on the client.
   - If any Use reaches the host relay, the host refuses it there.
   - The card 075 "its copy is downed" line may still appear: the MechEnter op is a separate path.
4. **Proof run 3, a pickup or switch:** a Downed player presses E at a card 054 relay target, a pickup or a switch, and nothing happens. Use a test driver like `UBWCOOP_C79_DOWN` if no route puts a Downed pawn next to one. The Report says which way was used.
5. **Regression:**
   - `c47-gates`: the card 043 revive still completes. Its done lines and shots are on both sides.
   - `c74-respawn`: the revive from Out.
   - `client-zipline`: the relay still works for a pawn that is Up.
   - `c75-solo`.

## Done when

- No native Interact and no relayed Use runs for a Downed or Out pawn in any proof run.
- The revive in c47-gates and c74-respawn and the zipline relay in client-zipline pass unchanged.
- The prompt is hidden for a down pawn, or the Report says what draws it and why it is left.
- `c43_kill_host` never hits a mech. Show the skip line, or explain why no route reaches it.
- `logsum` gives ERROR/FATAL 0, both games end with rc 0, and there is no new dump.
- fnlen reports no function over 60 lines.

## Report

### What changed

All of the changes are in `mod/src/hooks.cpp`, except the test driver in `mod/src/ubw/flags.cpp`/`flags.h` and the scenario files. Every gate runs in every co-op session, with no flag. The card 079 `EnterMech` refusal is unchanged and stays as the second line of defence.

- **Own-press gate.** `detour_interact` still calls `c43_interact_edge(interactor, true)` first, so the card 043 edge is kept. It then calls the new `c81_refuse_press(interactor, owner)`. When `c43_is_down(owner)` is true, that function logs `card081: <side> t=... Interact refused: <pawn> (<side>) is Downed|Out, target <name> (refusal #n)` (capped at 40 lines) and the detour returns without calling `g_orig_interact`. `detour_release_interaction` is unchanged.
  - A release that follows a refused press is harmless. In every proof run the native `own interact release (ReleaseInteraction)` line and the host's `Use 0 received` follow the refusal, and nothing else happens.
  - On the client, the own pawn is found through `kC43ClientSelf`. c81a and c81c log `client-own ... is Downed` on the client.
- **Host relay gate.** `c50_relay_use` gets a check on the press branch, after the `!pressed` return and before the revive-hold check. When the server copy `c43_is_down`, it logs `card081: host t=... relay for <pawn> refused: its copy is Downed|Out (refusal #n)` (capped) and returns. The release branch is unchanged.
- **"E ENTER" prompt: fixed (narrow fix).**
  - The prompt is driven natively. `UADInteractorComponent::TickComponent` (RVA 0x04A20230) calls `TraceForInteractable` (RVA 0x04A20780) at 0x144a20269. This is its only caller in .text.
  - When the result differs from `+0x100`, TickComponent unfocuses the old target (vtable +0x68), broadcasts `HoveredInteractableChangeDelegate` (+0xd8), stores the result at `+0x100` and sets HasTarget at `+0x110`. The HUD prompt follows that delegate. No mod code draws a prompt.
  - The new hook `detour_trace_interactable`, a hook-table row after ReleaseInteraction, returns null for the own pawn (host-own or client-own) while it is down. The game then clears the target and hides the prompt by its normal walk-away path. It logs one line per hidden/restored change (`own interact trace hidden|restored`).
  - The return value is an `IADInteractableInterface*`, not a UObject. The log resolves the name through the new `iface_object()`, which was split out of `interact_target()`.
  - Side effect: `+0x100` is null while down, so a refused press reports `target null`. Both lines of defence are still logged.
- **`c43_kill_host` mech skip.** When `host` `is_a(g_mech_class())`, it sets `kill_ms = now + kC43KillGapMs`, does not change `kill_tries`, logs `card081: host t=... real-death hit skipped: player 0 pilots <mech>; <n> hits so far, retry in <ms> ms (skip #n)` (capped), and returns.
  - The C43 test KillAll caller had to change for this. Its condition is now `(kill_tries == 0 && kill_ms == 0) || due`. Without that change, a skip at try 0 would retry on every tick instead of at the next gap.
- **Test driver.** `UBWCOOP_C79_DOWN=zipline` (mode 3) downs the client's copy at the nearest `BP_ZiplinePoint_C`, the same way `client` does at a mech. `c79_down_step` picks the class, and the new `nearest_of_class()` was split out of `c75_nearest_mech`, which now wraps it.
- **Scenarios.**
  - New: `tools/scenarios/c81-down-zipline.txt`.
  - `c79-down-host.txt` now waits for the host `Interact refused` line.
  - `c79-down-client.txt` now waits for the client `Interact refused` line, then the host `relay ... refused` line.
- fnlen: `hooks.cpp` 1437 functions, `flags.cpp` 41 functions, 0 over 60 lines.

### Why no route reaches the mech skip

The mech skip line is not shown, because no current route calls `c43_kill_host` while the host pilots a mech.

- The all-down path needs every player Down or Out. `c43_up_count` counts a mech pilot as Up, because a mech is never in the entry table. So the all-down path cannot fire while the host is in a mech.
- The only other caller is the C43 test KillAll stage, and that test first puts both pawns on foot with `c43_place`.

The skip is a guard for a future caller.

### Pickup/switch proof: which way was used

I used a zipline point, a card 054 relay target, with the new `UBWCOOP_C79_DOWN=zipline` driver on FED_C1M1. The host warps the client to `BP_ZiplinePoint_C_3`, the driver downs the client's copy 156 units from the point, and the client looks at the point and presses E.

### Runs

The first seven runs used DLL 96b9b35c06085fb2. A log-only fix followed: the trace line printed the interface pointer's name and showed garbage, `trace found ne??ByteProperty...`, in c81d. c81h repeats c47-gates on the fixed DLL 8b89f8abeb1b7004, which is `build/dwmapi.dll`.

All runs: ERROR/FATAL 0, no new dump, every game rc 0.

| Run | Scenario | Result |
|---|---|---|
| c81a | c81-down-zipline (proof 3) | Client `own interact trace hidden ... Downed`, then `Interact refused ... (client-own) is Downed (refusal #1)`. Host `relay for BP_PlayerCharacterMI_C refused: its copy is Downed (refusal #1)`. No card054 relayed Use, no zipline ride. The `downed` shot at the zipline point has no E prompt. |
| c81b | c79-down-host (proof 1) | Host Out. Two `Interact refused ... (host-own) is Out` lines. No `EnterMech refused` line, so EnterMech was never reached. The card 043 entry is kept. |
| c81c | c79-down-client (proof 2) | Client `Interact refused ... is Downed`. Host `relay ... refused: its copy is Downed`. The card 075 mech lines are present, as expected. No EnterMech. |
| c81d | c47-gates | Revive done, `shot revived` on both sides, all scenario lines ok. Showed the garbage trace-name log. |
| c81e | c74-respawn | `-> Up (revived from Out after a 6010 ms hold)`. Client `own pawn Out -> Up`, then trace restored. |
| c81f | client-zipline | `card054: host relayed Use #1 ... Interact on BP_ZiplinePoint_C` for an Up pawn; the scenario passed. run65 reports "ended by exited, drive rc none": both games quit before the drive's rc was recorded, but the drive log reads `scenario passed`. |
| c81g | c75-solo | Clean, no card081 lines. |
| c81h | c47-gates, fixed DLL | Same as c81d. Trace names now resolve, for example `trace restored: ... Up, trace found BP_PlayerTransportDropship_VerySlow_C`. |

The client WARN 1 in c81d and c81h matches the earlier c47-gates baseline (c80e).

Report complete.

## Senior review (2026-11-05 15:00): accepted

- **Runs.** I checked c81a-c81h myself:
  - Every drive ends with `scenario passed`. logsum gives ERROR/FATAL 0 for every log. The only WARN is the pre-existing client WARN 1 in c81d/c81h (c47-gates).
  - The newest crash dump is still the 13:37 c79a one.
  - The deployed DLL matches `build/dwmapi.dll` (8b89f8abeb1b7004). fnlen gives `hooks.cpp` 1437 functions, 0 over 60.
- **Proof.** c81a: the client logs `own interact trace hidden` and `Interact refused ... (client-own) is Downed`, and the host logs `relay ... refused: its copy is Downed`. Shot `client-c81a-downed.png` is at the zipline tower and has no E prompt. c81f has no card081 line, so an Up pawn is untouched.
- **Code.** `detour_trace_interactable` acts only on the own pawn and still calls the native trace; the server copy of a client is covered by the relay gate. One `trace_hidden` flag per machine is enough, because each machine has one own pawn.
- **The prompt fix is accepted.** It goes through the native walk-away path (`+0x100` cleared by TickComponent) rather than a widget hook.
- **The KillAll condition change is accepted.** Without it, a skip at try 0 would retry on every tick. The mech skip is a guard with no caller today, as the Report explains.
