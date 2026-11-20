# Card 020: The host gets everything it needs for the client's pawn (server-side parity audit)

Read `docs/STATE.md` first: the "Mod code rules" section, the "Runtime facts" line about `tools/run_pair.sh`, and rows 016 to 021. Then read card 019's report and its senior review. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`. Run logs: `logs/card019f-{host,client}.log`. Symbols: `tools/symbols.txt`.

## Why

The game is single-player. Its gameplay code runs on the host (the server) and reads state that, for a local player, is filled in locally: input vectors, camera, attributes, held-input flags. For the server copy of the client's pawn, nothing local fills that state in. Cards 017 to 019 found three cases one at a time:

- the input-tag map (card 018);
- the camera and movement attribute sets, which have no `CPF_Net` (cards 018 and 019; client side);
- `GetLastInputVector`, which is zero on the server copy, so the server ended GA_Sprint every frame (card 019).

Finding these one symptom at a time is slow. This card checks all of it in one pass: for each piece of state the host's gameplay code reads, does the server copy of the client pawn hold the same kind of value as the host's own pawn doing the same action? Every mismatch is a bug waiting for a feature to hit it (firing, aiming, interacting, melee, vehicles).

## Do

1. **Same script on both pawns.** Run the same input script on the host's own pawn (card 019's `UBWCOOP_HOST_SPRINT` host script, extended) and on the client, so the rows can be compared action by action. Actions:
   - idle;
   - walk forward;
   - look 30 px right and 30 px up;
   - jump;
   - sprint;
   - crouch;
   - fire for 2 s (LeftMouseButton);
   - aim down sights (RightMouseButton) if bound;
   - interact key once;
   - melee.
2. **Snapshot per action, on the host**, for both the host's own pawn and the server copy of the client pawn, in the same row format so they diff line by line:
   - **Input:** `GetLastInputVector`, `ControlInputVector`, `Acceleration`.
   - **Aim:** the controller's `ControlRotation`, `GetBaseAimRotation` and `RemoteViewPitch`. Also the world location and rotation of `CameraComponent` (+0x818), and `GetActorEyesViewPoint`. Weapon traces and interaction traces usually start from one of these. Name which one the game's fire and interact code uses (disassembly or a hook).
   - **Attributes:** every attribute in every set on the pawn's ASC, base and current.
   - **GAS:** `ActivatableAbilities` and, per spec, `InputPressed` and `ActiveCount`. Also the owned gameplay tags, and the active gameplay effects (class and stack count).
   - **Weapon and inventory:** the equipped weapon actor or class, its ammo, and the item and weapon inventory counts.
   - **Interaction:** what `InteractorComponent` reports as the current target.
3. **Outcomes, on the host:** for fire, whether a shot was simulated on the server (trace, projectile or damage event; hook the game's damage-apply path or the weapon fire function) and from what origin and direction. For interact, whether the server ran it.
4. **Report a table**, one row per piece of state, with columns: host-own, server copy of the client, match yes/no, and what reads it. For every mismatch, name the cause in one line.
5. **Fix only the cheap, safe mismatches** where the fix follows a pattern already in place (the card 019 input mirror, the card 018 and 019 attribute re-inits). List every other mismatch as a proposed card, with the evidence. Do not start the body-visibility work; that is card 021.

## Done when

- The table covers every item in step 2 for every action in step 1. Items not reachable in the run (for example, no enemy to shoot) are listed with the reason.
- Fire: it is shown whether the client's shot exists on the server, and from which origin.
- Every mismatch is either fixed (with before and after rows) or listed as a proposed card.
- Movement, look, jump and sprint still work on the client in the final run.
- Clean build (`/W4 /WX`), 84 exports, KERNEL32-only imports. No new crash dumps.

## Runs

- Build: `python3 -I tools/gen_proxy_exports.py && python3 -I tools/pdb_rva.py gen && cmake --build build --clean-first`.
- Run: `DEPLOY=1 HOST_EXTRA_ENV="UBWCOOP_AUTODROPSHIP=1 UBWCOOP_HOST_SPRINT=1" CLIENT_EXTRA_ENV="UBWCOOP_AUTOINPUT=1 UBWCOOP_AUTOINPUT_DELAY_MS=40000" DONE_PATTERN='autoinput: script done' tools/run_pair.sh <tag> 240`. Change the host env var name if you generalise the host script; say so in the report. Never pass `-nosound`; it crashes the host. Audio goes to a null sink by default.

## Risks

- The two pawns run the script at different times, so compare like-for-like actions, not like timestamps. Put the action name in each row.
- The snapshot reads many fields every row. Bound every walk (attributes, specs, tags, effects) with a fixed cap, and log the cap when it is hit.
- Firing on the host may hit the client pawn or alter the mission. Fire at the ground or the sky if no enemy is in view, and say which.
- Hooks taking UE by-value structs receive a hidden pointer. Check each new detour signature against the disassembly.

## Report

Final run: `card020c` (logs `logs/card020c-{host,client}.log`), ended by `autoinput: script done` after 146 s, host rc 0, client rc 0, FATAL/ERROR 0, WARN 0 on both, no new crash dumps. Build clean with `/W4 /WX`; `DEPLOY=1` passed the 84-export / KERNEL32-only check (dll hash 669ec175bd514411).

### Script and env

- `UBWCOOP_HOST_SPRINT` was **extended, not renamed**: it now arms the shared card 020 parity script on the host (after the landed character is possessed). The client still uses `UBWCOOP_AUTOINPUT=1`. Both run the same `kActions` list: idle, walk, look (30 px right, 30 px up), jump, sprint, crouch, fire 2 s (LeftMouseButton), ads (RightMouseButton, bound to IA_ADS), interact (E, bound to IA_Interact), melee (V, bound to IA_HumanMelee). 20 InputKey calls on each side, 2 returned false on each side (which keys was not logged).
- Run command used: card's command with `UBWCOOP_AUTOINPUT_DELAY_MS=1500` on the host and `62000` on the client (so the two scripts do not overlap and the host rows of the server copy are not mixed with the host's own actions).
- Rows: `c20[k] <who> act=<action> ...`, one row every 500 ms (cap 200 rows, 300 GAS lines, 40 fire lines, 12 interact lines per map; every cap logs when hit, none was hit). `who` is `host-own` and `server-copy` on the host, `client-own` on the client. The host does not know the client's action, so server-copy rows carry `act=client`; they are joined to the client's `autoinput: action` timeline by host log time (both instances share the machine clock). Join scripts are not committed; the join is a bisect on the client's action timestamps.

### Table (card020c, compared action by action)

| State | host-own | server copy of client | match | reader |
|---|---|---|---|---|
| `GetLastInputVector` walk / sprint | (0.97, 0.26, 0) / (0.94, 0.33, 0) | (0, 1, 0) / (-0.07, 1, 0) | yes (unit vector along each pawn's facing; card 019 mirror) | GA_Sprint_C end check, CMC |
| `ControlInputVector` (+0x2F0) | 0 every row | 0 every row | yes (consumed by the CMC before the row) | CMC |
| `Acceleration` walk / sprint | magnitude 4096 | (0, 4096, 0) / (-300, 4085, 0) | yes | CMC |
| `ControlRotation` look | P 4.20 Y +4.20 | P 4.20 Y +4.20 | yes | `GetBaseAimRotation`, camera |
| `GetBaseAimRotation` | P 4.20 | P 4.20 (P 3.8-4.0 in sprint/jump, smoothing) | yes | weapon aim (static RE, see below) |
| `RemoteViewPitch` (+0x2BA) | 4.20 | 4.20 | yes | `GetBaseAimRotation` on non-locally-controlled pawns |
| `GetActorEyesViewPoint` | P 4.20 | P 4.20 | yes | AI, perception |
| `CameraComponent` (+0x818) world pitch | 4.20 | **0.00** in every row | **no** | interactor trace source (`InteractorComponent` +0x108 = Camera) |
| Camera z during crouch | 178 -> 102 | 184.5 -> 144.5 | yes (both lower) | |
| Attributes (5 sets, 52 attributes) | init values | same as host-own, all 52 | yes | GAS, CMC, weapon |
| Attributes on the **client's own pawn** (before this card) | | Health/MaxHealth/Armour/MaxArmour 0 (100), DamageTakenMultiplier 0 (1), Accuracy 0 (50), DynamicZeroCloseTargetDistanceThreshold 0 (100), DynamicZeroObstacleTraceRadius 1 (4), RecoilMultiplier 0 (1), RecoilReturnDelay 0.1 (0.5) | **no, fixed** | client HUD, client-predicted weapon code |
| Client own `CapsuleCollision_Radius` / `HalfHeight` | 40 / 95 | client 0 / 0 (server copy 40 / 95) | **no** | `USTAttributeSet_Movement::PostAttributeChange` (sets the capsule) |
| Specs (`ActivatableAbilities`) | 16, same classes and order | 16, same classes and order | yes | |
| `InputPressed` while held: sprint, crouch, melee, jump | 1 | 1 | yes | ability input tasks |
| `InputPressed` after release | 0 | **stays 1** (GA_Sprint, GA_HumanJump, GA_HumanMelee, GA_Slide) | **no** | `WaitInputRelease`, `InputReleased` |
| `ActiveCount` per action | sprint 1->0, slide 1, melee 1->0 | sprint 1->0, slide 1, melee 1->0, jump 0 | yes | |
| Spec flags byte (+0x29) high nibble | 0xF0, 0xD0, 0x90 on some specs | 0x00 / 0xA0 | n/a (bits 4-7 are padding after the 4 bitfields; only bit 0 `InputPressed` is meaningful) | |
| Owned tags at rest | 6: Faction.Human.Player, Player.IsPlayer, HealthAndDamageSystem.FriendlyFire.Enabled, **PlayerAbilities.TutorialBlockTaunt**, MovementMode.Native.Walking, IsPossessed | 5: same without TutorialBlockTaunt | **no** | GA_HumanTaunt_C activation block |
| Tags per action | +Sprinting, +Crouch, +InProgress.Melee +WeaponSystem.Blocked | the same; jump also +Falling +InAir -Walking and back | yes | |
| Active effects | GE_HealthRegeneration_C; +GE_Sprint_C (sprint), +GE_Crouched_C (crouch) | the same | yes | |
| Sprint WalkSpeed / MaxWalkSpeed | 700 -> 1050 -> about 1312, back to 700 | 700 -> 1050 -> 1302.5, back to 700; the client's own pawn has exactly the same series | yes | `PostAttributeChange` -> MaxWalkSpeed |
| Weapon inventory (+0xA40) | STWeaponInventory, BP_Weapon_Unarmed_C, ammo 0, weapons 1 | the same | yes | |
| Weapon inventory on the **client's own pawn** | | equipped null, ammo -1, weapons 0 | **no** | client HUD, client fire prediction |
| Item inventory (+0x860) equipped item | null | null | yes | |
| Interactor target (+0x100) / has-target (+0x110) | null / 0 | null / 0 | yes (nothing to interact with in view) | |
| Fire outcome | no `FireProjectile`, no `ApplyDynamicZeroThresholdAdjustment` | none on the server for the client | yes (no weapon on either) | |
| Interact outcome | `UADInteractorComponent::Interact` ran on the host for host-own, target null | ran on the client only, for client-own; never on the server for the client pawn | **no** (unverified: no target, so no server RPC is expected either way) | |

Mismatch causes, one line each:

- **Camera pitch on the server copy:** the camera component's rotation follows the control rotation only in the local camera update; the server never runs it for a remote pawn, so the component stays at pitch 0. Anything tracing from the camera on the server (the interactor) aims level.
- **Client attributes 0 (Health, WeaponOwner sets):** neither `ADAttributeSetHealth` nor `STRangedWeaponOwnerAttributeSetBase` has a `CPF_Net` attribute (checked at runtime, the re-init only runs on a set with 0 `CPF_Net` attributes), so the replicated subobject keeps 0. Same cause as cards 018 and 019.
- **Client capsule attributes 0:** the movement init data does not set them. On the host they are set later (`PostAttributeChange(CapsuleCollision_Radius) 0 -> 40`, caller exe+0x49E0DB1, an aggregator update), most likely from a server-only effect or from the capsule component. The client's own capsule component keeps its own size, so nothing visible breaks yet.
- **`InputPressed` stuck at 1 on the server copy:** the client only sends `ServerSetInputReleased` for a spec it holds active, and each of these abilities had already ended, or never activated locally, at release. The server clears nothing else.
- **TutorialBlockTaunt missing on the server copy:** the tutorial or mission flow grants it to player 0 only. Effect: the client can taunt where the host cannot.
- **Client weapon inventory empty:** the unarmed weapon actor that the server copy holds does not reach the client's inventory (the inventory array, or the weapon actor, does not replicate to the owner). In the AUTODROPSHIP flow neither player gets a real loadout.

### Unreachable fields

- **Fire origin and direction:** both players are unarmed (BP_Weapon_Unarmed_C on the server, nothing on the client), so LeftMouseButton fires nothing. Neither side calls `FireProjectile` or the zero-threshold adjust, so there is no shot on the server, no origin, and "ground or sky" does not apply. Both pawns were looking 4.2 degrees up, so a shot would have gone at the sky. Static RE: the target point comes from `GetTargetLocation_Implementation`, which deprojects through PlayerController 0. Not confirmed at runtime; that needs a weapon.
- **ADS:** RightMouseButton is bound to IA_ADS, but no spec or tag changed on either pawn while unarmed.
- **Server-side interact:** nothing is in view, so the target is null on both sides and the client never sends a server interact. Whether the server would run it for the client is untested.
- **Host melee action label:** the host's GA_HumanMelee rows show `act=done`. The ability activates on the row after the short V tap, which comes after the script's last action ended. Like for like with the client's melee rows anyway.

### Fix applied (cheap, card 018/019 pattern)

`card020_set_fix` in `mod/src/hooks.cpp` runs at the card 018/019 once-per-ASC point (`card018_camera_fix`, own pawn, LocalRole 2). For `ADAttributeSetHealth` and `STRangedWeaponOwnerAttributeSetBase` it re-runs the game's `AddAttributeSetFromData` with the pawn definition's init data. It only does this when the set has 0 `CPF_Net` attributes and the property walk did not hit its cap; otherwise it logs the skip. Before and after (client log):

```
card020: client ADAttributeSetHealth re-init AddAttributeSetFromData(AttributeData_PlayerHealth_C) ... first attribute base 0.000 current 0.000 -> base 100.000 current 100.000
card020: client STRangedWeaponOwnerAttributeSetBase re-init AddAttributeSetFromData(BP_Player_WeaponOwnerAttributeSetInitData_C) ... first attribute base 0.000 current 0.000 -> base 50.000 current 50.000
```

After the fix, the client's own 52 attributes match the server copy except for the two capsule attributes (card020b to card020c diff).

### Client move, look, jump, sprint (final run)

- Server copy walk: y -1543 -> 417.
- Look: yaw 90 -> 94.2, pitch 4.2.
- Jump: camera z 184 -> 377 -> 184, with Falling / InAir tags.
- Sprint: MaxWalkSpeed up to 1302.5 on both the server copy and the client's own pawn, the same series; moved 686 -> 4240.
- Crouch: GA_Slide_C and GE_Crouched_C.
- Melee: GA_HumanMelee_C active, then ended.

### Runs

- `card020a`: host crash `UECC-Windows-FB579B6E4DBA88E7FD63DBBE77E1E354_0000`, `EXCEPTION_ACCESS_VIOLATION` in `FName::GetDisplayNameEntry`, while walking the server copy's owned tags. This was my bug. I read the owned tags at ASC+0xEF0, but `GetOwnedGameplayTags` (0x04960E00) takes `this` = the IGameplayTagAssetInterface subobject at ASC+0x130. Checked against `HasMatchingGameplayTag` (interface+0xE40 count map) and `GetGameplayTagCount` (ASC+0xF70). The tag container is at ASC+0x1020. Fixed, and the walk now also refuses `Num < 0 || Num > Max`.
- `card020b`: clean, used for the first table; found the client attribute zeros.
- `card020c`: clean, final, includes the attribute fix.
- Existing guard line, not new: on the host, `guard: UPlayerInput::IsPressed(this=null) from ASTPlayerController::Tick: returned false`.
- On the client, the host's pawn (simulated proxy) has its movement set at 0 too (`client other ... WalkSpeed 0 -> 0, MaxWalkSpeed 0`). That is card 021's territory (remote bodies, sprint parity), not touched.
- Sprint multiplier: in this run host-own peaked at about 1312 and the client at 1302.5. The 1.1 vs 1.5 gap from card 019 did not show, but it is not re-checked here; it is card 021's.

### Proposed cards

1. **Loadout in the AUTODROPSHIP flow.** Both players are unarmed, and the client's own weapon inventory is empty (weapons 0, equipped null) while the server copy holds BP_Weapon_Unarmed_C. Give both a real weapon, then redo the fire half of this audit: shot on the server, origin, and whether `GetTargetLocation_Implementation` deprojects through PlayerController 0, which would aim the client's shot from the host's camera.
2. **Server copy camera component rotation.** On the server the remote pawn's `CameraComponent` stays at pitch 0. Drive it from the control rotation (or RemoteViewPitch) before the interactor and any camera-based trace runs.
3. **`InputPressed` stuck on the server copy.** It stays 1 after release for abilities that ended before release. Check against `WaitInputRelease` users (sprint hold, ADS) once weapons exist, and clear it on release if needed.
4. **Server-side interact.** Repeat the interact test with an interactable in view (needs card 2 first, because the trace source is the camera).
5. **Low priority:**
   - the client's own capsule attributes are 0 (server 40 / 95);
   - the server copy lacks `PlayerAbilities.TutorialBlockTaunt`;
   - the client's copy of the host pawn has its movement set at 0 (fold into card 021).

## Senior review (2026-11-03)

**Accepted.**

Checked against the logs:

- card020c: FATAL/ERROR/WARN 0 on both sides.
- The only new crash dump, `UECC-Windows-FB579B6E...`, is from 01:22. That is card020a (its host log ends at 01:23:18), and the report owns it as a mod bug, now fixed. card020c ran after it (01:34) and is clean.
- All three client re-init lines are present, each reusing the same set object: movement (WalkSpeed 0 -> 700), `ADAttributeSetHealth` (0 -> 100), and `STRangedWeaponOwnerAttributeSetBase` (0 -> 50).
- Sprint reaches 1291 to 1312 on both sides.

Points the next cards must carry:

1. **Client health will not follow server damage.** `ADAttributeSetHealth` has no `CPF_Net` attribute. The re-init gives the client correct starting values, but nothing in GAS sends the server's later Health changes to the client. Unless the game sends health another way (a replicated property, or a client RPC behind `OnHealthValuesUpdated`), the client's health HUD and any client-side death logic stay at 100. Test this the first time the client takes damage.
2. **`GetTargetLocation_Implementation` deprojects through PlayerController 0.** On the server, PlayerController 0 is the host's. If that path runs on the server for the client's weapon, the client's shots aim from the host's camera. That is the first thing to check once weapons exist; it may be the biggest gap in this audit.
3. The client's copy of the host pawn has its movement set at 0 (MaxWalkSpeed 0). This is a simulated proxy, so the effect is likely limited to animation or smoothing; it is folded into the body card.
4. Proposed cards 1 to 4 are accepted in that order: weapons first, because firing, aiming and interaction all wait on it.
