# Card 054: The client's interactions happen on the host

Status: sent 2026-11-04 (senior, revised: audit replaced by Addendum 3, key and table decided).

Read `docs/STATE.md` ("Client interactions are client-only"), the card 043 Report (`ChanOp::Use`), the card 050 Report and card 050's own Addendum 4, and this card's Addendums 3 and 4 (the symbols and test driver are ready there). Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

`UADInteractorComponent::Interact` runs on the client for the client's own pawn. Interactable actors are not replicated, so a client interaction changes the client's world only. The host never sees the switch pressed, the satchel carried, or the support pickup taken, so the level script on the host does not advance. Card 050 relays the three pickup classes. This card relays the rest, and decides per class whether the client's local interaction should also run.

## Static facts (senior, 2026-11-04)

- `Interact` 0x04A1FE70, `ReleaseInteraction` 0x04A200F0, `TickComponent` 0x04A20230, `TraceForInteractable` 0x04A20780. The interactable is cached at component +0x100; `Interact` checks its `CanInteract` (vtable +0x28) and broadcasts the delegate at +0xC8.
- `OnInteraction` natives: `ASTSwitch` 0x050F78A0, `ASTSatchelPlacementSite` 0x05163E00, `ASTZiplinePoint` 0x05163F90, `ASTItemPickup` 0x051D03F0, `ASTTacticalSupportPickup` 0x051D05A0, `ASTSecretPickup` 0x051D0590, `ASTMountableTurret` 0x051D0410, `ASTPlayerTransportDropship` 0x051D0470, `ASTPlayerMechBase` 0x050F7840, `ASTLimitedInteractionActor` 0x050F7430.
- Placed in FED_C1M1 (from `FED_C1M1_Scripting.umap`): `BP_PanelSwitch_WithoutScreen` (its `SwitchPressedDelegate` is bound in the level script), `BP_SatchelItemPickup_CTF` and `BP_SatchelPlacementSite_Flag` (the capture-the-flag objective, `CaptureTheFlag_O1`, SO0 to SO4), `BP_TacSuppPickup_BombingRun_Tutorial`, `BP_ZiplinePoint`, `BP_AmmoCrate`, `BP_ArmourCrate`, `BP_SecretPickup`, and `BP_PlayerTransportDropship_VerySlow` (`PlayerSkippedFlight`).

## Do

Senior revision (2026-11-04): the old audit step and Addendum 2's log-only "Do first" are replaced by Addendum 3. The relay key and the class table are decided below; go straight to the code.

1. **Key: the server copy's own trace.** Card 050's relay #1 proved it: the copy's +0x100 was already the pickup when the client pressed Use (card 050 Report, step 3a). Keep `c50_pick_target`'s no-name branch as the main path for every class. Keep `UseName` as an override when the client does send one; do not try to fix the client's own null target in this card. On each relayed press, log one line: the copy's target and class, and the client's +0x100 and +0x108 names if the client sends them (no extra frame for this; skip it if they are not sent).
2. **Relay table, keyed by interactable class.** Extend card 050's relay class list (`kC50RelayClasses` and the `cls[]` arrays at hooks.cpp ~20224 and ~20271) into a table with one row per class: relay (yes or no) and the log label.
   - Relay: `ASTSwitch`, `ASTSatchelPlacementSite`, `ASTItemPickup` (covers the satchel pickup), `ASTTacticalSupportPickup`, `ASTSecretPickup`, ammo and armour crates, and the existing card 050 weapon pickup.
   - Zipline: relay, with fix (a) from Addendum 1: the relayed `OnInteraction` runs `ToggleZiplineUse` on the server copy with the same arguments, so the copy rides the line the client predicts. Keep the client's local run. Use fix (b) only if (a) still shows corrections, and say so.
   - Mech, turret, dropship: no relay; log the press only.
   - Report the final table.
3. **Double-run check.** The client's own target is null today (Addendum 3), so its local `Interact` does nothing and only the host's run counts. In the run, check from the shots whether the client still sees the result (switch state, door, satchel carried, objective) through cards 049 and 050. List each one that the client does not see; do not fix those here, they go to a follow-up card.
4. **Runs.** `card054x`: two players, FED_C1M1, driven by `UBWCOOP_C50_TEST=054` (Addendum 4). The client presses the panel switch, rides a zipline if one is reachable, and carries the satchel to its site, while the host stands elsewhere. Take shots of both screens at each step. The host's objectives advance. `card054s`: solo; no relays run. Run the first `card054x` with `STAGGER=5 tools/run_pair.sh ...` (new 2026-11-04, saves about 20 s per run; see the header of `run_pair.sh`). If the client connects, keep it for every later run and note it in the Report; if not, drop it and say so.

## Done when

- The client can complete the FED_C1M1 interaction objectives (switch, capture the flag, tactical support) with the host advancing.
- rc 0 on all runs, no new dump, the build is clean with `/W4 /WX`, and every function is 60 lines or fewer.

## Addendum 1 (senior static, 2026-11-04)

- **Switch latches; it does not toggle.** `ASTSwitch::OnInteraction` 0x050F78A0 (called with the interface pointer; the actor base is `this - 0x2C8`) returns early unless the switch is enabled (byte at interface +0x40) and a second check passes. Then it plays a sound, broadcasts the multicast delegate at actor +0x2E8 (`SwitchPressedDelegate`, the one the level script binds), sets the prompt, calls `BP_OnStateUpdated`, and calls `StartReactivationTimer` 0x05105540 and `UpdateState` 0x0510D030. A second press is ignored until the reactivation timer fires. So running the switch on both machines broadcasts once per machine, and each machine's level script sees one press. That is the intended result for step 3: both doors open, each by its own script. Step 3 then only has to show that the host's relayed press does not fire a second time on the client (through card 049 objectives or anything else).
- **Zipline is local movement, so expect corrections on the host.** `ASTZiplinePoint::OnInteraction` 0x05163F90 takes the pawn from interactor +0xA0, checks that it is an `ASTPlayerCharacterMI`, calls `AActor::SetActorLocation`, then `ASTPlayerCharacterMI::ToggleZiplineUse(FVector, float)` 0x051E85A0. That moves the capsule with `UKismetSystemLibrary::MoveComponentTo` (a latent action, local only), adds a gameplay tag, makes two vtable calls (+0x638 and +0x960; probably the movement mode), and fires `OnZiplineInteraction` 0x05085990. None of it is an RPC. On the client's server copy, the host's `CharacterMovementComponent` does not take the zipline mode, so it will likely correct the client back (rubber banding) during the ride. In step 1, ride the zipline as the client and log the client pawn's position on both machines once a second. If corrections show, prefer the smaller fix:
  (a) relay the zipline too, and call `ToggleZiplineUse` with the same arguments on the server copy. Keep the local run.
  (b) Or, while the client is on the zipline (`IsOnZipline` 0x051CCEE0 on the server copy is false, so key it on the relay), set the server copy CMC's `bIgnoreClientMovementErrorChecksAndCorrection` and clear it at the end.
  Report which one you used, with the position logs.
- **Pickups take the interactor's pawn.** `ASTItemPickup::OnInteraction` 0x051D03F0 is a jump through vtable +0x758 on the actor (`this - 0x490`). `ASTSecretPickup` 0x051D0590 jumps straight to `AADPickupBase::Pickup` 0x04C61940. `ASTTacticalSupportPickup` 0x051D05A0 reads the pawn from interactor +0xA0, checks it is an `ASTPlayerCharacter`, stores it in a weak pointer on the pickup, then calls `AADPickupBase::Pickup`. `Pickup` calls a Blueprint event (`FindFunctionChecked`), so the grant itself is Blueprint. So on the host, call `OnInteraction` with the **client's server-copy pawn's** interactor, never the host pawn's, or the host gets the support call-in. Log the pawn named in the weak pointer after the relay.
- **AI mech call-ins are placed in FED_C1M1.** The level script references four placed `BP_AIMechCallIn_C` actors (`_C_1` to `_C_3` in its ubergraph, plus the class). The card 029 row says no mechs are placed in FED_C1M1, which holds for player mechs and pads but not for these. Each machine runs its own level script, so the client may call in its own AI mechs next to the host's. In step 1's run, log the count and names of mech pawns on both machines once the call-in fires, and say whether the client has duplicates. Do not fix them in this card; report, and the senior cards it.
- **The level script acts on player 0 of each machine.** Its calls include `GetSTPlayerCharacter`, `GiveWeaponByClass`, `AddWeaponByTag`, `RemoveItemByDefinition` and `K2_SetActorLocation`. On the host they reach the host's pawn only, never the client's server copy. Card 050 already relies on the client's own script for its gives, so this is a note for step 3, not new work.

## Addendum 2 (senior static, 2026-11-04): why the client's interactor target is null (its "Do first" is superseded by Addendum 3)

Card 050 found that the client's own interactor target (+0x100) stays null at 130 and 164 uu while the client aims at a weapon case. This card's relay needs a client-side target, or must work without one, so settle this first.

**What the disassembly rules out**
- `UADInteractorComponent::TickComponent` 0x04A20230 calls `TraceForInteractable` 0x04A20780 on every tick, with no role or net-mode check. It writes the result to +0x100, with the interactable's begin and end calls through the vtable at +0x68 and +0x30.
- `TraceForInteractable` returns null in only three cases:
  - the scene component at +0x108 is null or pending kill;
  - the line trace misses;
  - the hit actor does not have `IADInteractableInterface`.
- The trace is a single line trace (0x143D36820) with `this` as the world context:
  - from the +0x108 component's world location (component +0x200 and +0x210);
  - along its forward vector (0x143A90020);
  - out to the range at +0xF8;
  - on the trace channel byte at +0xFD;
  - ignoring the actor at +0xA0.
- So on the client, either the +0x108 component points somewhere other than the camera direction, or the line hits something else first, or the pickup has no blocking collision on that channel.

**Runtime evidence**
- The client log has no `SetActorEnableCollision` or `DisablePickup` call for `BP_WeaponPickup_LaserMorita_C_0`.
- The client shot 01279 shows the case open and empty *before* the relay ran.
- On the client, placed pickups log roles local 0, remote 3, so `HasAuthority()` is false there. A Blueprint path in the pickup that is gated on authority (Switch Has Authority, in BeginPlay or the construction script) may leave it unarmed: no weapon mesh, and no interaction collision.
- `AADPickupBase::BeginPlay` 0x04C60540 sets +0x400 from +0x365 and an actor flag bit (+0x58 & 2). It then sets up the +0x2F0 component through the vtable (+0x4E0, +0x768, +0x5E0, +0x6D8, +0x8F0). No role check was seen in its first 0x130 bytes.

**Do first (log only, one run)**
1. On the client, every 500 ms while within 300 uu of a placed pickup, log:
   - the interactor's +0x108 (name, world location and forward vector) and its +0xF8, +0xFD and +0xA0;
   - the pickup's +0x400 and the collision enabled state and channel-+0xFD response of +0x2F0;
   - `CanInteract`, called through `ASTWeaponPickup::CanInteract` 0x051C19C0 in log only, with a scratch `FADInteractionFailed`.
2. Do the same once on the host's server copy, standing at the same case, for comparison.
3. Take a host shot of the same case, to tell whether it looks empty there too, or whether the empty case is just how the asset looks.
4. Report which of the three null causes it is. Only then choose:
   - fix the client target, and send `UseName` as card 050 built it;
   - or drop the name path and use the host-side trace from the server copy, with the client's camera rotation sent along with the Use press.

## Addendum 3 (senior static, 2026-11-04): the pickup side has no role gate; key on the host

- `ASTWeaponPickup::BeginPlay` 0x051BF690 calls its parent (0x051BD870), then 0x04C61740, which returns `!(actor +0x58 & 2)`, that is, not `bNetStartup`. Only for dropped (non-startup) pickups does it apply the launch impulse (+0x508 velocity times +0x4F8, through the +0x2F0 mesh's vtable +0x9B8 and +0x6F0). Then it copies the ammo fields +0x4E0 and +0x4EC into +0x4F0 and +0x4F4 when +0x4F0 is -1. No role or net-mode check.
- `AADPickupBase::BeginPlay` 0x04C60540: when +0x365 is 0 and the actor is net-startup (placed), it sets +0x400 to 0 and skips the physics setup. Otherwise it sets +0x400 to 1 and turns on physics on the +0x2F0 mesh. No role check.
- `bNetStartup` is set on placed actors on both machines by the level's network init; the client's placed actors then swap roles (local 0, remote 3), which is what card 050 logged. Neither BeginPlay reads the role, so the native pickup setup is the same on both machines.
- So the client's null target is on the interactor side (the +0x108 component, or the line hitting the client's own geometry first), or in a Blueprint graph. It does not block this card: the server copy's trace already finds the target (card 050 relay #1), so the host-side key is used, and the client-side cause stays open in STATE as a known issue.

## Addendum 4 (senior, 2026-11-04): symbols and the test driver, ready to use

Symbols to add to `tools/symbols.txt` (RVAs checked with `pdb_rva.py lookup` against this build). `ASTTacticalSupportPickup`, `ASTItemPickup` and `ASTWeaponPickup` are already there.

```
ASTSwitch_StaticClass ?StaticClass@ASTSwitch@@SAPEAVUClass@@XZ
ASTZiplinePoint_StaticClass ?StaticClass@ASTZiplinePoint@@SAPEAVUClass@@XZ
ASTSatchelPlacementSite_StaticClass ?StaticClass@ASTSatchelPlacementSite@@SAPEAVUClass@@XZ
ASTSecretPickup_StaticClass ?StaticClass@ASTSecretPickup@@SAPEAVUClass@@XZ
ASTLimitedInteractionActor_StaticClass ?StaticClass@ASTLimitedInteractionActor@@SAPEAVUClass@@XZ
```

RVAs: `ASTSwitch` 0x050A5570, `ASTZiplinePoint` 0x050BB630, `ASTSatchelPlacementSite` 0x0508C890, `ASTSecretPickup` 0x05095DA0, `ASTLimitedInteractionActor` 0x05073C40. These also extend the relay class table (`kC50RelayClasses`, hooks.cpp ~20541, `cls[]` ~21056).

Test driver for `card054x`: do not write a new one. The card 050 driver (`UBWCOOP_C50_TEST`, hooks.cpp ~21276) already does what this run needs: the host moves the client's server copy next to the nearest placed target (a client-side teleport is corrected back), and the client aims at it, presses Use through the interact detour, and releases. Generalize it:
- `c50_test_pick` (~21304) takes the class from a small table of steps instead of `g_c50_weapon_pickup_class()`. For this card, the steps are, in order: `ASTSwitch` (the `BP_PanelSwitch_WithoutScreen` in FED_C1M1), `ASTItemPickup` filtered by the class name `BP_SatchelItemPickup_CTF`, `ASTSatchelPlacementSite` (`BP_SatchelPlacementSite_Flag`), then `ASTZiplinePoint`.
- Run one step through the existing stages (Wait, Look, Aim, Pressed, Released, Done), then go to the next one. Take the shot at each step's Released plus `kC50TestShotMs`, as card 050 does.
- Select it with a new value, `UBWCOOP_C50_TEST=054`, so the card 050 run keeps working unchanged.
- A target that is disabled (for example, the switch before its objective is active) is logged with its enabled byte (interface +0x40 for `ASTSwitch`, Addendum 1) and skipped after `kC50TestGiveUpMs`. Do not script the tutorial up to it in this card.

## Report (2026-11-04)

Build clean (`/W4 /WX`); every new or changed function is 60 lines or fewer (longest: `c50_test_client` 36, `c50_relay_use` 32). Runs: `card054x` rc 0/0, `card054s` rc 0, no new crash dumps. `STAGGER=5` connected on the first try and was kept for both runs.

### Code
- `tools/symbols.txt` and `offsets.generated.h` have the five StaticClass symbols from Addendum 4. The regenerated RVAs match the ones in the addendum.
- **Relay table** `kC54Relays` (hooks.cpp, next to the card 050 class globals) replaces `kC50RelayClasses` and both `cls[]` arrays. `c54_relay_row` returns the first row that matches, so subclasses come before their parents. `c50_send_use_name`, `c50_find_placed` and `c50_relay_use` all use it.
- **Relay key:** the server copy's own trace stays the main path. `UseName` overrides it when the client sends one (unchanged from card 050). Each relayed press logs one `card054: host ... relayed Use #n` line. It gives the target, its class and its table row, plus the switch's enabled byte (interface +0x40) before and after the Interact call.
- **Zipline:** this is fix (a). The relayed `Interact` runs `ASTZiplinePoint::OnInteraction` on the server copy, and that calls `ToggleZiplineUse` with the copy's own arguments. The client keeps its local run. `c54_track` logs the pawn's position once a second for 12 s on both machines.
- **Test driver:** `UBWCOOP_C50_TEST=054` (`config::read_c50_test` now returns 0, 1 or 54) walks four steps from `kC54Steps`:
  - the panel switch;
  - `BP_SatchelItemPickup_CTF`;
  - `BP_SatchelPlacementSite_Flag`;
  - the zipline point.

  Each step is filtered by class and class-name prefix and runs the card 050 stages. The host moves the copy for each step, ends the step 8 s after its relayed Use (or when the client gives up), and takes a host shot 3.5 s after the relay. With `=1`, card 050 runs its single weapon-pickup step as before.

### Final relay table
| Row (first match wins) | Relay | Label |
|---|---|---|
| ASTWeaponPickup | yes | weapon pickup |
| ASTTacticalSupportPickup | yes | tactical support pickup |
| ASTSecretPickup | yes | secret pickup |
| ASTItemPickup | yes | item pickup (satchel, crates) |
| ASTSwitch | yes | switch |
| ASTSatchelPlacementSite | yes | satchel placement site |
| ASTZiplinePoint | yes (fix a) | zipline point |
| ASTLimitedInteractionActor | yes | limited interaction actor |
| anything else (mech, turret, dropship) | no | `card054: host relay ... declined ... (log only)` |

There are no StaticClass symbols for the mech, turret or dropship, so "no relay" is the fall-through for every class not in the table. Watch item: if `ASTMountableTurret` or the mech derives from `ASTLimitedInteractionActor`, that row would relay it. Its class is in the relay log line.

### card054x results (logs/card054x-{host,client}.log)
| Step | Client's own target | Host copy's target | Relayed | Effect |
|---|---|---|---|---|
| 0 panel switch | null | null | no (the copy's target is null; the client sent no name) | Enabled byte 0 on both machines: the switch is not active yet. Skipped as Addendum 4 says. |
| 1 satchel pickup | null | null | no | Neither machine's trace finds it. Same cause as step 0 most likely: the pickup is not live until the capture-the-flag objective starts. |
| 2 satchel site | **SatchelSiteCAC_…_CAT_0** | none | **yes**, #1, by the client's `UseName` (mismatch path) | Nothing changes: no satchel is carried (step 1). The client shows "RETURN WITH THE FLAG." before and after. |
| 3 zipline | **BP_ZiplinePoint_C_1** | BP_ZiplinePoint_C_1 (**match**) | **yes**, #2 | Neither machine rides. Both log (17420, 19661, 1208) for 12 s. |

- **The client's own target is not always null.** Addendum 3 assumed it was, but the client's trace found both the satchel site and the zipline. So for those two classes the client's local `Interact` also runs. The card020 line shows the client's own Interact on the site. This is the double run from step 3. For the switch it is intended (Addendum 1: each script sees one press). For the satchel site, the satchel pickup and the tactical support pickup it means both machines run `OnInteraction`; it was not observable here, because nothing was active yet. Card 050's null target (the weapon case) is unchanged.
- **Zipline: no corrections to report.** Both the client's local run and the host's relayed run did nothing: the position is constant on both machines. Most likely `CanInteract` is false this early in the tutorial, or `_C_1` is the far end of its line. Fix (b) was not needed or tried.
- **Not proven:** that the client completes the switch, capture-the-flag and tactical support objectives with the host advancing. From mission start, the driver cannot reach those objectives without scripting the tutorial, which Addendum 4 rules out. The relay path itself is proven on two classes: the copy-trace match on the zipline, and the `UseName` fallback on the satchel site.
- **What the client sees (step 3):** nothing changed on either machine, so there was no host-only result to look for. No duplicate fire through card 049 was seen: the client's objective list matches the host's in the shots.
- **AI mech call-ins (Addendum 1):** they did not fire in this run (the tutorial was not advanced), so there is no count to report.
- **Tactical support weak-pointer log (Addendum 1):** not added. The tactical support pickup is not a driver step, and its weak-pointer offset was not read.

### card054s (solo)
Host only, with `UBWCOOP_C50_TEST=054`: rc 0, no WARN, no `card054` or relay lines. The test does nothing without a client.

### Screenshots (logs/card054-shots/)
- `x_host_brief0.png`, `x_host_brief1.png`: the host past the brief, in game.
- `x_client_switch_near.png`, `x_client_switch_after.png`: step 0.
- `x_client_satchel_near.png`, `x_client_satchel_after.png`: step 1.
- `x_client_site_near.png`, `x_client_site_after.png`, `x_host_site_after.png`: step 2. The client shows "RETURN WITH THE FLAG."; the host's view is its own pawn's.
- `x_client_zip_near.png`, `x_client_zip_after.png`, `x_host_zip_after.png`: step 3. No ride.
- `x_unknown_01481.png`, `x_unknown_01485.png`: game shots from the same run that do not match a card054 log line (probably earlier queued card 043 shots).
- `s_host_ingame.png`: the card054s solo host, in game.

### Proposed follow-up
Proving the objective path needs one of two things:
- a run that starts from a checkpoint at the capture-the-flag and tactical support objectives;
- or the LAN playtest, where the client does them and notes whether the host's objectives advance.

PLAYTEST.md is updated to say this.

## Senior review (2026-11-04): accepted for the playtest

I checked these myself:
- **Runs.** card054x ended by its done line after 163 s, with host rc 0, client rc 0, ERROR/FATAL 0 on both sides and no new dump. Its two host WARNs are the test's own "no relayed Use came" for the panel switch and the satchel pickup. card054s (solo) ended by its cap after 110 s with rc 0 and no relay lines.
- **Function length.** fnlen gives 1152 functions, 0 over 60 lines.
- **Relay table.** `kC54Relays` lists subclasses before their parents, and every other class is declined and logged. Relayed Use #1 (the satchel site, through the UseName fallback) and Use #2 (the zipline, matched by the host copy's trace) both ran Interact on the server copy.

What is not proven, and why I accept it anyway:
- **Effect on the game.** No interactable did anything on either machine: the switch was disabled, no satchel was carried, and the zipline did not ride. These are the tutorial's gates, not relay faults. Reaching an active switch means playing the level forward, which a scripted run before 8 pm cannot do cheaply. The playtest is the test, and `docs/PLAYTEST.md` asks for it, with the host doing the interaction as the fallback.
- **Double run.** The client's own Interact also runs when its target is not null (the satchel site and the zipline). If both a client zipline ride and a server-copy ride start, the two movements can fight, which would look like rubber-banding. Playtest watch item; if it shows, the follow-up is to skip the client's local Interact for the relayed rows.
