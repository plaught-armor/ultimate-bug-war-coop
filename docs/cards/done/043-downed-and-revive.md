# Card 043: Downed and revive (first pass)

Status: sent 2026-11-04.

Read `docs/STATE.md` first ("Mod code rules", "Runtime facts", milestone 3a "Player death flow"), then `docs/design/coop-channel-lobby-downed.md` section 3 (the design, decided by the user), card 030 (player death audit), card 028 (remote sprite body) and card 024 (fire relay). Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

When the client's server copy dies, only the host knows; the client plays on (card 030). The user chose downed and revive as the co-op death rule. A player at 0 health goes down. A teammate can revive them within a bleedout timer. Only when every player is down does the game's own death run, and its blueprint shows the game-over menu. This card builds the core of that on the host, with the minimum visuals on both sides. The kill floor (`Tick` calling `StartDeath` directly) is left for a later card.

## Rules (from the design; constants at the top of the code)

- States per player: `Up`, `Downed(t_left)`, `Out`. Bleedout is 30 s. The revive hold is 3 s from `Downed` and 6 s from `Out`.
- Everything is host-authoritative. Clients only display what the host sends.
- Gate: the intercept runs only on the host (`NM_ListenServer`) with more than one player connected. Single player stays vanilla.

## Do

1. **Enter Downed.** Detour `ASTPlayerCharacter::HandleDeathEvent` 0x051CA3B0 on the host, for the host's own pawn and for server copies. If another player is `Up`:
   - skip the original;
   - set Health to 1 with `SetNumericAttributeBase` (the card 025 revive write), so health replication shows it on the client;
   - enter `Downed` and send a new `ChanOp::Down` (args: player slot, state, seconds left) to every client.
   If no other player is `Up`, run the original (see step 5).
   Check first, and say in the report: is `HandleDeathEvent` called once per 0 crossing (card 030 says edge-triggered), and does anything else act on Health reaching 0 before it?
2. **While Downed** (host):
   - Clamp Health at 1 for downed pawns: damage must not reach 0 again. Damage to a downed player instead takes seconds off the bleedout (start with 1 s per 10 damage, as a constant).
   - Refuse `AbilityInputTagPressed` for fire and sprint on that pawn's ASCs, and ignore relayed `ChanOp::Fire` from a downed client.
   - Set the pawn's MaxWalkSpeed to 0 (or the nearest working equivalent), and restore it on revive.
   - At 0 seconds, go to `Out` and broadcast it.
3. **Revive.** An `Up` teammate within 200 uu holds interact for the revive time.
   - The host's own interact: find out whether `InputTag.Use` reaches `AbilityInputTagPressed` on the host-own ASC (card 030 found no ability handle). If it does not, find the native the interact key reaches, for example `UADInteractorComponent`, and say which one you used.
   - The client's interact is local only (card 030). Relay its press and release edges on a new `ChanOp::Use`, the same way card 024 relays fire.
   - On completion: Health becomes 30% of MaxHealth, the state goes back to `Up`, and `ChanOp::Down` is broadcast.
4. **Visuals, minimum.**
   - Own view of a downed player (each machine for its own pawn, from `ChanOp::Down` on the client and from the state directly on the host): lower the camera (for example an offset on the camera component), and block local fire on the client so it does not show its own muzzle flashes. Show the seconds left in the log (no UI yet).
   - Remote view: the card 028 sprite plays `Death` and then holds the last `Corpse` frame while downed or out, then plays `StandUp` on revive (card 027: male Morita has front-only Standup frames; use the FemaleMoritaTrooper set as card 028 does). Override `c28_drive` while a pawn is downed.
5. **All down.** When no player is `Up`, the host runs the real death on its own pawn only (`StartDeath`, the game's 3 s countdown, `CompleteDeath`, `OnDeath`), and the game-over menu shows on the host. A server copy must never run the real `StartDeath`, because it turns off the host's autosave. Treat a client that would die then as `Out`. Report what the client shows when the host's game-over menu appears; the reload itself is milestone 4.
6. **Test hooks.** A test-only env var (for example `UBWCOOP_C43_TEST=1`) scripts the run:
   - kill the client's server copy through the real damage pipeline (card 030's kill switch);
   - have the host pawn walk to it and hold interact until the revive completes;
   - then kill the host pawn, and let the client revive it with autoinput;
   - then kill both, to show the all-down path.
   Take screenshots of: the downed player's own view; the remote sprite downed (`Corpse` hold) and standing up; the host's game-over menu.
7. **Run** a 2-player run with the test script. Report the state transitions on each side with timestamps, the Health values, the bleedout ticks, the revive hold times, and the counts of `ChanOp::Down` and `ChanOp::Use` sent and received.

## Done when

- Both players can go down and be revived by the other. Bleedout leads to `Out`, and `Out` is still revivable. All down leads to the game-over menu on the host.
- Health never reaches 0 for a player while a teammate is `Up`. The real `StartDeath` never runs on a server copy, and host autosave stays on through a client downed-and-revive cycle (log `IsAutoSaveEnabled`).
- Single player is unchanged: one run with no client, where the host dies as before.
- Clean build (`/W4 /WX`, `--clean-first`), 84 exports, KERNEL32-only imports. No new crash dumps. Every new function is at most 60 lines. Write `card043x-run.txt` with the rc.

## Risks

- **Abilities that are still active.** Skipping `HandleDeathEvent` also skips `StartDeath`'s `CancelAllAbilities`. A downed player may keep an active fire or sprint ability. Cancel those on enter (find the ASC call `StartDeath` uses), but leave the rest of `StartDeath` out.
- **Health replication timing.** The client may briefly see Health 0 before the host's 1 arrives. Make sure the client's own death path (card 021 notify, card 037 adopt) does not start a death from that edge on the client's own pawn.
- **Revive clears state.** The game's own revive helper for NPCs does not apply to players. Restore everything the downed state changed (speed, input block, camera), and log each restore.
- If interact on the host cannot be seen in a native in reasonable time, use a mod key (for example F7) for this card and say so. The final binding can wait for a later card.

## Senior addendum (2026-11-04): where the interact press lands

From the PDB publics. Not yet runtime-checked.

- `UADInteractorComponent::Interact()` 0x04A1FE70 and `UADInteractorComponent::ReleaseInteraction()` 0x04A200F0 are UFUNCTIONs (`execInteract` 0x0401D510, `execReleaseInteraction` 0x04A20B90). So the interact press probably reaches them from a blueprint input event on the local machine, on press and on release.
- For step 3, detour both on each machine:
  - on the host, for the host-own pawn's component, drive the revive hold directly;
  - on the client, for its own pawn's component, send `ChanOp::Use` press and release.
  Find the owning pawn through the component's owner. Confirm with one log line per edge that pressing interact reaches them with no interactable in view. `Interact` traces for a target (`TraceForInteractable` 0x04A20780) and may return early, so detour the entry, not a later step.
- Other related functions: `TickComponent` 0x04A20230 (the timed-interaction progress), `IADInteractableInterface::GetIsTimedInteraction` 0x04A1FE30, `OnTimedInteractionStarted` 0x04A200C0, `OnTimedInteractionCompleted` 0x04A20020 and `OnTimedInteractionInterrupted` 0x04A20070. Making the downed pawn a real interactable would need an `IADInteractableInterface` object; the mod has none, so keep the revive hold timer in mod code.

## Senior addendum 2 (2026-11-04): the ability cancel that StartDeath does (Risks, first bullet)

From the disassembly:

- Base `StartDeath` 0x051DE410 sets +0x9DC to 3.0 and then calls `UAbilitySystemComponent::CancelAllAbilities(pawn+0x680, nullptr)` 0x04951680. The pawn's ASC is at +0x680.
- `ASTPlayerCharacterMI::StartDeath` 0x051DEE70 calls the base. Then it takes the equipped weapon (`UADWeaponInventory::GetEquippedWeapon` 0x04A2BD10 on the inventory at pawn+0xA40). If that is non-null and valid, it calls `CancelAllAbilities(nullptr)` on the weapon's ASC through a folded getter (0x03650320).
- So on Enter Downed, call `CancelAllAbilities(nullptr)` on both ASCs, the pawn's at +0x680 and the equipped weapon's, and run nothing else from `StartDeath`. The weapon ASC is the one card 024's fire relay presses `InputTag.Fire` on, so reuse that lookup.

## Report

Done (first pass). All code is in `mod/src/hooks.cpp` in the card 043 block (`c43_*`, before `struct HookSpec`), plus `config::read_c43_test` (`UBWCOOP_C43_TEST`). Two-player run `card043x`: rc 0 (`logs/card043x-run.txt`), ended by `card043: host test done` after 167 s. Host and client both have 0 FATAL/ERROR and 0 WARN lines. No new crash dumps. Solo run `card043s`: rc 0 (`logs/card043s-run.txt`), ended by done after 232 s, 0 FATAL/ERROR lines, and 1 WARN (the expected autodropship single-PC line).

### What was built

- **Gate.** The mod acts only on the host (`NM_ListenServer`, 2) with 2 or more PlayerControllers, and not after all down. Otherwise `HandleDeathEvent` runs vanilla and the host logs `gate off for <pawn> (net mode, players, all down): vanilla death` once.
- **HandleDeathEvent detour (host).**
  - It applies when another player is Up.
  - It skips the original, sets Health to 1 (`SetNumericAttributeBase` on the health component's ASC), and cancels abilities: `CancelAllAbilities(nullptr)` on the pawn ASC (+0x680) and on the equipped weapon's ASC (card 024's lookup).
  - It releases a held fire relay, saves MaxWalkSpeed, sets it to 0, and goes Downed with 30 s of bleedout.
  - It sends `ChanOp::Down` (slot | self flag, state, seconds) to every remote PC.
  - With nobody else Up: a server copy goes Out (still revivable from 6 s); the host's own pawn runs the vanilla death.
- **Edge-triggered.** `HandleDeathEvent` is edge-triggered and runs on the authority only. The health component calls it once when Health crosses to 0 or below, not on every hit at 0. Before it, in the same Health change, these act on Health 0:
  - `HealthAttributeChanged`;
  - the blueprint-bound health event;
  - `HandleDamageEvent` (camera shake and force feedback);
  - `RefreshState`, which sets `IsDead` (+0x148).
  The mod's health hook brackets the change (`c43_health_pre` and `c43_health_post`). On a pawn already Downed or Out, it puts Health back to 1 after the original, so a second edge never comes. On the first edge the detour itself sets Health to 1.
- **While Downed (host).**
  - Health is clamped at 1. Damage drains the bleedout at 1 s per 10 damage (`kC43DamagePerS`).
  - `AbilityInputTagPressed` refuses `InputTag.Fire` and `InputTag.Walk` (sprint, card 016) on a downed pawn's ASCs.
  - `ChanOp::Fire` 1 relayed from a downed client is ignored; Fire 0 still releases.
  - MaxWalkSpeed is 0 and is restored on revive. At 0 s of bleedout the state is Out and is broadcast.
  - Bleedout seconds are broadcast each time the whole second changes.
- **Revive.**
  - `UADInteractorComponent::Interact` and `ReleaseInteraction` are detoured at entry on both machines; each edge is logged once.
  - Host-own pawn: the edge drives the hold directly. Client own pawn: the edge goes as `ChanOp::Use` 1/0.
  - An Up player within 200 uu of a Downed or Out player starts a hold: 3 s from Downed, 6 s from Out. Release, moving out of range or the reviver going down ends it.
  - On completion: Health = 30% of MaxHealth, state Up, walk speed restored, broadcast.
- **Views.**
  - Own view, both machines: the camera component is moved -60 uu (`AddLocalOffset`), restored on Up. Own fire is released and walk is held at 0.
  - The client logs the seconds left (capped).
  - Remote c28 sprite: the key is `Death01`, then `Corpse01` after 1.1 s while down, then `Standup` for 1.2 s after a revive.
- **All down.**
  - Only the host's own pawn runs the real death. The tick deals MaxHealth×10 damage up to 5 times, 1 s apart, until Health is at or below 0.
  - Server copies go Out. `StartDeath` and `StartDeath` (MI) on a server copy are skipped while the gate holds. On the client, its own pawn's `StartDeath` is skipped once a Down frame has arrived.
- **Logout** removes the leaver's entry and holds.

### Two-player run `card043x` (host times, ms of `GetTickCount64`)

| Time | Event |
|---|---|
| 554737694 | Both pawns on foot. Test hits on the server copy start 20 s later; the first does no damage (self-hit window). |
| 554759216 | Copy: Health 100 → 96 → 80, then the 400 hit. **Up → Downed** (HandleDeathEvent skipped). Health 1/100, bleedout 30 s, autosave 1, Down sent 1. CancelAllAbilities on the pawn ASC and the weapon ASC (`BP_Weapon_Unarmed_C`). Saved MaxWalkSpeed 700. |
| 554759232 | Client: own pawn Up → Downed (Down received 1). Camera -60 uu, walk 0. |
| 554762231 | Drain test: a 50 hit lands as 5.0 damage (Health 1 → clamped 1). Bleedout 27.0 → 26.5. |
| 554760220–554787722 | Bleedout ticks 29 s … 1 s, one Down frame per whole second. |
| 554788716 | Copy **Downed → Out** (bleedout 0, Down sent 31). Client gets it at 554788729 (received 31). |
| 554788721 | Host interact press (`Interact`, 120 uu). The hold needs 6000 ms (from Out). |
| 554794724 | Copy **Out → Up** after a 6002 ms hold. Health 30/100, Down sent 32. Client 554794735: Out → Up, camera +60, walk 700. |
| 554794738 | Host release (`ReleaseInteraction`). |
| 554800733 | Host own pawn: 100 → 96, then the 400 hit. **Up → Downed**. Health 1/100, bleedout 30, autosave 1, Down sent 33. Camera -60, walk 0. Both ASCs cancelled. |
| 554802761 | Client interact press (`Interact`). Host receives Use 1 at 554802770 (Use received 1). Hold on the host pawn: Downed, 120 uu, needs 3000 ms. |
| 554805779 | Host **Downed → Up** after a 3008 ms hold. Health 30/100, Down sent 39. Camera +60, walk 700. |
| 554805811 | Client release (`ReleaseInteraction`). Host receives Use 0 at 554805816 (received 2). |
| 554807691 | Host Downed again (Health 30 → 14 → 1). Five 16-damage hits drain 1.6 s each: 27.9 → 20.0 s left. |
| 554810788 | Copy hit while the host is Downed: nobody else Up, so the copy goes **Up → Out** (Health 1, HandleDeathEvent skipped, Down sent 47). |
| 554810799 | **All down.** Host pawn → Dead, copy → Out (Down sent 48, 49). The gate is now off: `gate off … all down 1: vanilla death`. |
| 554810853 | Real-death hit 1 on the host pawn: Health 1 → -1999, autosave 0 (the vanilla host death turns it off). At 554811806: `real death: 1 hits, Health -2015`. |
| 554813812 | Host: `AddMappingContext IMC_DummyUI_KBM` (the game-over menu's input context), 3 s after the death. |

- **Health.** Neither machine logs Health at 0 or below for any player pawn before all down:
  - card021 and card037 lines only show 100/96/80/30/14 → 1 and 1 → 30;
  - the first value at or below 0 is the host's real death after all down.
- **Server copy.** No `StartDeath` ran on a server copy. No `WARN card043 … StartDeath … skipped` line appears either, so the skipped `HandleDeathEvent` never reached it.
- **Autosave.** `IsAutoSaveEnabled` is logged on every transition and is 1 on all of them, through the client's Downed → Out → Up cycle and the host's two downs. It turns 0 only with the host's real death at game over.
- **Totals:**
  - host: Down sent 49, received 0; Use sent 0, received 2; 9 transitions;
  - client: Down received 49; Use sent 2, received 0.
- **Not exercised at runtime:**
  - the refused fire/sprint tag path (0 `refused` lines: the test pressed no fire while down);
  - a downed client's relayed Fire 1.
  Both are code paths only.

### Client at game over

- The client gets the host pawn Downed → Dead (Down received 48) and its own pawn Out (47).
- It keeps the lowered camera and walk 0, with the host's sprite on `Corpse01`.
- It gets no game-over menu and no UI input context: its PC stays `Playing`, and it sits in the world with its pawn Out until the host leaves the menu.
- The host's real death replicates to the client as Health 1 → -1999 → -2015 (card037 notify). The client's own pawn stays at Health 1.

### Screenshots (`logs/card043-shots/`, 640x360)

| File | What it shows |
|---|---|
| `host-remote-Death-00816` | Host view of the copy's sprite 0.3 s after Downed. The first `Death01` frame still looks like the standing pose. |
| `host-remote-Corpse-00818` | The `Corpse01` key is set (log), but the sprite is not in frame. The camera cuts at the sprite's knees (see 00816), so a lying corpse is below the frame. |
| `host-remote-StandUp-00819` | The `Standup` sprite is visible. |
| `client-own-downed-00817`, `host-own-downed-00820` | Own downed views with the camera lowered. |
| `client-remote-Corpse-00821`, `client-remote-StandUp-00822` | The host's pawn is not in the client's view direction; the keys are confirmed in the client log (`Death01` → `Corpse01` → `Standup`). |
| `host-gameover-00823` / `-later-00825`, `client-at-gameover-00824` / `-later-00826` | The world only. The UE `shot` command does not capture UI, so the menu is not in the image. Its presence on the host is shown by `IMC_DummyUI_KBM`. |

### Solo run `card043s`

Command: `SOLO=1 DEPLOY=1 HOST_EXTRA_ENV="UBWCOOP_AUTODROPSHIP=1 UBWCOOP_C43_TEST=1" DONE_PATTERN='card043: host test done' tools/run_pair.sh card043s 300`.

The test driver gained a solo branch, `c43_test_solo`. With one PlayerController and no copy, it deals the same real-death hit to the host's pawn 30 s after it is on foot.

- The host travels with `?listen`, so the net mode is 2, but there is 1 player.
- 555265844: `gate off for BP_PlayerCharacterMI_C (net mode 2, players 1, all down 0): vanilla death`. `HandleDeathEvent` runs the original.
- Health 100 → -800. Autosave goes 0, as in the vanilla death.
- `IMC_DummyUI_KBM` is added 3 s later (555268847), the same game-over timing as the two-player host.
- There are no Down or Use frames, no transitions and no entries.
- Shots: `solo-death-00827` and `solo-death-later-00828`.

### Build

- `cmake --build build --clean-first` with `/W4 /WX`: 0 warnings.
- `run_pair.sh` deploy check: 84 exports, KERNEL32-only imports.
- Every `c43_*` function is 60 lines or fewer (checked by script).
- New symbols in `tools/symbols.txt`: `CancelAllAbilities`, `IsAutoSaveEnabled`, `AddLocalOffset`, `ReleaseInteraction`.

### Open points

- The game-over menu can only be seen on screen, or in a shot taken with UI (`HighResShot` with `showui`, or a UI-capturing screenshot). Not done here.
- After all down, the client gets no menu and stays in the world. A later card should send it to its own game-over UI or follow the host's travel.
- The c28 `Corpse01` sprite should be checked with a camera aimed down at the body.

## Senior review (2026-11-04)

Accepted as the first pass of downed and revive.

Checked myself:
- `logs/card043x-run.txt` and `logs/card043s-run.txt` are both rc=0.
- `card043x-host.log` and `card043x-client.log` have 0 FATAL/ERROR and 0 WARN lines. `card043s-host.log` has 1 WARN, the expected autodropship line. No new `.dmp` files.
- Transitions: the 9 host transitions match the report's table. The client got Down 49 of 49 and sent Use 2.
- Client health: `client-own Health` goes 80 → 1 → 30 → 1 and never reaches 0 or below.
- `IsAutoSaveEnabled` is 1 on every transition line.
- The function-size script finds no function over 60 lines in `mod/src/*.cpp`.

Corrections to the report:
- `host-remote-StandUp-00819.png` shows no sprite, only the rail and the ground. The report says "The `Standup` sprite is visible"; it is not. The `Standup` key is shown by the log only.
- `host-remote-Death-00816.png` shows the copy standing with its gun up, 0.3 s after Downed. So nothing on screen yet shows that the remote downed sprite looks downed. This needs a shot with the camera aimed at the body (it is already an open point).

Open (carried to STATE):
- The client gets no game-over UI after all down. It stays in the world with its pawn Out.
- The remote downed sprite (`Death01`/`Corpse01`/`Standup`) is not yet confirmed visually.
- Not run: the refused Fire/Walk tags on a downed pawn, and relayed Fire 1 from a downed client.
- The game-over menu has not been captured with UI.
