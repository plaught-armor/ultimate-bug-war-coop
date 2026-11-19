# Card 029: Audit weapons, mechs and turrets for the remote sprite body

Status: sent 2026-11-03.

Read `docs/STATE.md` first: the "Mod code rules" section, the "Runtime facts" line about `tools/run_pair.sh`, and rows 027 and 028. Then read `docs/design/coop-channel-lobby-downed.md` section 4, and card 027's report and senior review. Repo root: `/path/to/ultimate-bug-war-coop`. This card only logs; it changes no behaviour.

## Why

Card 028 draws a remote player as a trooper sprite. To pick the right sprite and animation, the mod must know the player's equipped weapon, what weapon actions each side can see, and when a player is in a mech or on a turret. Design section 4 has the static facts and the proposed mapping; this card checks them at run time.

## Do

1. **Sprite sets.** Reuse card 027's dump switch. Find every loaded or loadable `BP_CharacterAnimations_*` asset (list in design section 4). For each, write one summary line per key to `logs/card029-sets.txt`: key, frame count, total seconds, `Materials.Num` on frame 0, and the first material name. Report which sets have their own materials and which reuse another trooper's.
2. **Equipped weapon.** Find the field that holds the player's current weapon (start from the inventory or `ASTPlayerCharacter` properties; card 024 already hooks weapon changes). On both sides, for both pawns, log every change with the weapon class name. Switch through every weapon `UBWCOOP_GRANT_WEAPON` can give.
3. **Weapon actions.** On both sides, log each activation of fire, reload, iron sights, pump, spool up and down, and underslung grenade, for both pawns. Use the existing `AbilityInputTagPressed` logger where it already covers them. Report which actions the other machine can see without the mod channel.
4. **Mechs.** Find a mech in C1M1 or the first mission that has one; report the map. Hook `ASTPlayerMechBase::EnterMech` and `LeaveMech` (log only) and log on both sides: who enters, `ControllingPlayerCharacter`, whether the pilot pawn is hidden or attached, whether the mech actor replicates (it exists on the client, and its position on the client follows the host), and whether a client can enter one.
5. **Turrets.** Same for `ASTMountableTurret::MountTurret` and `LeaveTurret`: who mounts, `ControllingCharacter`, `IsBeingUsed`, whether the turret's rotation follows on the other machine, and whether a client can mount one.

## Done when

- `logs/card029-sets.txt` exists and step 1's question is answered.
- Steps 2 to 5 are answered with log lines from both sides, or the step says why it could not be tested (for example, no mech on the reachable maps).
- All hooks are log-only and behind a test-only switch that is off by default. Clean build (`/W4 /WX`, `--clean-first`), 84 exports, KERNEL32-only imports. No new crash dumps. Every new function is at most 60 lines.

## Runs

Pair runs as in card 024, with `UBWCOOP_GRANT_WEAPON` on both sides. Single-player runs are fine for step 1. Never pass `-nosound`.

## Report

Done 2026-11-03. Every addition is behind `UBWCOOP_AUDIT_029=1`, which is off by default. The vehicle and activation detours pass their arguments through unchanged. The audit also includes a test driver that grants and equips weapons, presses weapon input tags, and mounts and leaves a turret. The driver runs only under the switch. The final build was clean (`/W4 /WX`, `--clean-first`), with 84 exports and KERNEL32 as the only import. No function is longer than 60 lines. The final pair run, `card029-pair3`, produced no new crash dumps (16 before, 16 after). Earlier runs produced four dumps, all caused by the driver and all fixed; see "Crashes" below.

Runs (logs in `logs/`):
- `card029-solo1` (C1M1, sets dump).
- `card029-survey*` (vehicle survey of the FED maps).
- `card029-solo3-C4M1`.
- `card029-pair1` to `card029-pair3`: C4M1, `UBWCOOP_GRANT_WEAPON=MoritaMk1` on both sides, with `UBWCOOP_TRAVEL=/Game/Maps/FED/FED_C4M1?listen` and `UBWCOOP_AUTODROPSHIP=1` on the host.

C4M1 is used because it starts on foot, while C2M1, C3M1, C3M2, C5M1 and C5M2 start in a dropship.

### 1. Sprite sets

`logs/card029-sets.txt` has 227 lines: one line per key, giving frames, seconds, `mats0` and the first material.
- Every set has its own `MI_<Set>_*` materials, except one: all 21 keys of **ControllerTrooper** use `MI_MoritaTrooper_*`, so it reuses MoritaTrooper's materials.
- Shared materials: in most sets the `OverKill` key uses the shared `MI_FX_Overkill_01`, with `mats0=1`. ControllerTrooper's `Standup` key also has `mats0=1`.
- FlamerFieldScientist has three keys whose first material is null: `Attack01Start`, `Attack01End` and `Corpse`.
- These sets load on demand: MoritaTrooper, MK2Trooper, Shotgun_Trooper, Epulse88Trooper, FlamerFieldScientist, RadioTrooper, TurretTrooper and 2DTargetWarriorBug. The others were already loaded in C1M1.

### 2. Equipped weapon

The equipped weapon is read from the pawn's weapon inventory (`GetEquippedWeapon`) every 500 ms, for every player pawn on each side. The host cycles all 17 `kC29Rows` on its own pawn and on the server copy.

Grant results:
- `ok 1`: MoritaMk1, MoritaMK1Scoped, MoritaMK2, MoritaCarbine, EPulse44, Epulse88, NukeLauncher, Flamethrower, AutoCannon and Railgun.
- `GiveWeaponByClass ok 0`: Shotgun, VeteranRifle, Unarmed, HighPower, LaserMorita and LaserMoritaRed. One exception: LaserMoritaRed was granted to the server copy in pair2.

Host (pair3) logs every change on both pawns:
```
card029: host equipped weapon on BP_PlayerCharacterMI_C (server-copy): BP_Weapon_MoritaMk1Scoped_C (...), weapons 2
card029: host equipped weapon on BP_PlayerCharacterMI_C (server-copy): BP_Weapon_EPulse88_C (...), weapons 3
card029: host equipped weapon on BP_PlayerCharacterMI_C (server-copy): BP_Weapon_Railgun_C (...), weapons 3
```

The client never saw any of these. Its own pawn kept `BP_Weapon_MoritaMk1_C, weapons 1`, which was its own card 021 grant, for the whole run. The host's pawn always read `null, weapons 0` on the client:
```
card029: client equipped weapon on BP_PlayerCharacterMI_C (client-own): BP_Weapon_MoritaMk1_C (...), weapons 1
card029: client equipped weapon on BP_PlayerCharacterMI_C (other): null (class null), weapons 0
```

**Conclusion:** neither weapon grants nor the equipped weapon replicate, in either direction. The remote sprite needs the weapon sent over the mod channel.

### 3. Weapon actions

Activations are logged from `InternalTryActivateAbility`, per pawn side. The input tags of an equipped weapon's ASC are `InputTag.Fire`, `Reload` and `ADS`. Underslung is an ability granted with MoritaMk1, not a tag.

What each action did on the pressing side (pair3, host's own pawn):
- **Fire:** activated for every weapon tried, e.g. `BP_WeaponFireAbility_NukeLauncher_C = 1`.
- **Iron sights (ADS):** activated for every weapon tried, e.g. `BP_Weapon_IronSightsAbility_Epulse88_C = 1`.
- **Spool:** `BP_Weapon_SpoolDownAbility_EPulse88_C` and `SpoolDownAbility_NukeLauncher_C`. SpoolUp for the same two weapons was seen in `solo3-C4M1`.
- **Underslung:** `BP_Weapon_UnderslungAbility_MoritaMk1_C = 1` activates when MoritaMk1 is equipped, on the host's own pawn and the server copy alike.
- **Weapon switch:** `GA_WeaponSwitchAbility_C`.
- **Reload:** pressing `InputTag.Reload` activated nothing (`activation null = 0`), with full magazines.
- **Pump:** untested, because no shotgun could be granted.

The client's own pawn logged `BP_WeaponFireAbility_MoritaMk1_C = 1` and `BP_Weapon_IronSightsAbility_MoritaMk1_C = 1`.

**What the other machine sees without the mod channel: nothing.**
- The client's Fire and ADS (488549941) produced no server-copy activation on the host.
- The host's actions produced no activation for the "other" pawn on the client.

This matches card 024, which needed the relay for fire.

### 4. Mechs

Not testable. The vehicle scan found 0 `ASTPlayerMechBase` actors at load on all of C1M1, C2M1, C3M1, C3M2, C4M1, C4M2, C5M1 and C5M2. The `EnterMech` and `LeaveMech` hooks are installed, but they never fired. For the record, the mech's `OnInteraction` adjusts its interface pointer by 0xC08.

### 5. Turrets

`BP_MountableTurret_C` counts per map:

| Map | Turrets |
|---|---|
| C3M1 | 9 |
| C3M2 | 5 |
| C4M1 | 11 |
| C4M2 | 3 |
| C5M1 | 5 |
| C5M2 | 7 |

On the host every turret is roles local 3, remote 1, with `bReplicates 1`. On the client it is local 1, remote 3, so the actor exists there as a replicated proxy. `ControllingPlayerCharacter` does not exist on the turret. `ControllingCharacter` is a `WeakObjectProperty`.

Host mount and leave (pair3):
```
host MountTurret after: ... pawn BP_PlayerCharacterMI_C (host-own), controller BP_PlayerCharacterMI_C, being used 1, pawn bHidden 0, pawn attached to BP_MountableTurret_C
host changed turret ... gun yaw -4.8, ... controller BP_PlayerCharacterMI_C (host-own), being used 1
host LeaveTurret after: ... controller null, being used 0, pawn bHidden 0, pawn attached to null
```
The pilot is attached to the turret but not hidden. Mounting sets the gun yaw to -4.8.

The client sees none of this: the client logged no `changed turret` line between 488567691 and 488576030, while it was on foot and polling every 500 ms. So `IsBeingUsed`, `ControllingCharacter` and the gun yaw do not follow on the other machine.

The driver could not turn the gun. `HandleInputLookHorizontal` returns immediately while input-receiver byte +0xF9 is 0, and it was 0 throughout. Rotation-follow was therefore not observed directly. Since no mount state crosses at all, I expect it does not follow either.

**Can a client mount: not through game code.**
- `UADInteractorComponent` and `ASTMountableTurret` have no Server RPCs.
- Calling `OnInteraction` locally on the client attaches the client's pawn and sets `being used 1`, but on the client only:
  ```
  client MountTurret after: ... pawn attached to BP_MountableTurret_C ...
  ```
- The host logged nothing after 488640863.
- Calling `LeaveTurret` on the client crashes (see below), so the driver now leaves the client mounted.

Also: `OnInteraction` has no range check. The mounts above happened from 4666 and 4729 units away.

### Crashes during the card (all caused by the driver, all fixed)

1. `B8C7065C…` (C4M1) and `F57EAB69…` (C4M2): access violation in `c27_find_prop`.
   - Cause: `OnInteraction` was called with the actor pointer, but MSVC interface overrides expect the interface subobject (turret +0x740, from `lea rdi,[rcx-0x740]`).
   - Fix: pass `actor + 0x740`. The mech uses +0xC08.
2. `C2515B94…` (pair1, client): `LowLevelFatalError GameplayAbilityTargetTypes.cpp:240` when firing a Flamethrower that the client had granted only to itself.
   - Fix: the client driver no longer grants anything. The host equips each row on the server copy.
3. `3402DC26…` (pair2, client): null+0x338 read in `AController::Possess`, called from `ASTMountableTurret::DetachCharacterFromTurret+0x1E5` and `LeaveTurret`.
   - Cause: a local mount on the client never possesses, so leaving dereferences a null controller.
   - Fix: the client driver skips `LeaveTurret`.

### For card 028

- The remote sprite must take its weapon class, its weapon actions and its turret state from the mod channel. None of them replicate natively.
- A pilot on a turret is attached and visible, not hidden.
- Mechs cannot be checked: none are placed on any reachable map.

## Senior review (2026-11-03)

Accepted. Checked against the logs, the build and the binary:

- Build: `build/dwmapi.dll` (10:43) has 84 exports and imports only KERNEL32.dll. The switch `UBWCOOP_AUDIT_029` is in `config.cpp`. `card029-pair3` ran 302 s with 0 FATAL/ERROR lines on both sides (the only matches are the `ServerCheckClientError` hook names) and no new crash dumps; the crash directory still holds 16.
- Sprite sets: `logs/card029-sets.txt` has 17 sets. The only keys with fewer than 8 directions are OverKill, Standup on the male sets, TauntResponse, Medic `Heal`, M7Razorback `Corpse`, and every key of `2DTargetWarriorBug`. FemaleMoritaTrooper has 8 directions on every key except OverKill, so card 028's choice holds.
- Weapons: on the client, the host's pawn reads `null (class null), weapons 0` for the whole run, while the host logs each server-copy change. Neither grants nor the equipped weapon replicate.
- Turrets: the client found the host's turret at (23700, -91500, 455) at 488539658 and logged its first `changed turret` line only at 488641339, after its own local mount. The host's mount (488567847) and leave (488575996) never reached it. No `Server*` functions exist on `ASTMountableTurret`, `UADInteractorComponent` or `ASTPlayerMechBase`.
- Static check of `MountTurret` (0x051CE770): it requires the character's controller (+0x2D8) to be an `APlayerController`, sets `bBeingUsed` (+0x848), writes the weak `ControllingCharacter` (+0x830, matching the reflected offset 0x830), calls `AttachCharacterToTurret`, then `AController::Possess(turret)`. A host-side mount therefore makes the player controller possess the turret, so code that assumes the controller's pawn is the trooper must handle a turret pawn. On a client the possess path does not run, which explains crash 3.

Corrections and gaps:

- "Mechs are untestable" is too strong. No mech is placed in a level, but a player mech is delivered by the MechSupport tactical support (`BP_TacSuppPickup_MechSupport`, `Config_TacSupp_MechSupport`, `GA_TacSupp_MechSupport`, `BP_TacSupp_Pod_MechDrop`). A later card can grant it with `USTTacticalSupportComponent::EquipTacticalSupportAbility` (0x050E69E0) and call it in with `TryThrowFlare` (0x0510A5B0), or spawn `BP_BaseMech` and enter through `OnInteraction` at `mech + 0xC08`. See design doc section 4.
- Still unobserved: pump (no shotgun grant), reload with a partial magazine, and turret gun yaw following the pilot's aim. None of these block card 028; the weapon and turret state will come over the mod channel anyway.
- `OnInteraction` has no range check (mounts from 4666 and 4729 units). That matters only for the mod's own relay: the host must check range itself before mounting for a client.
