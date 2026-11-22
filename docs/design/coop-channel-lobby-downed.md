> Historical design draft of 2026-11-03. The channel, lobby and downed state are built. The code and `docs/guide/` are authoritative.

# Design: mod channel, lobby overlay, downed and revive

Status: senior draft, 2026-11-03. This is static design; nothing here is runtime-checked unless a line says so. Each section ends with the first card it implies. User decisions behind it (2026-11-03): the lobby is an in-game Host / Join-by-IP overlay with ready-up first and a LAN browser later; the death rule is downed and revive, with a checkpoint restart when every player is down.

## 1. Mod channel (needed by both features)

The mod owns no replicated UObject and cannot add UFUNCTIONs, so it rides two existing engine RPCs that carry a free-form `FString`. Both are reliable and ordered.

| Direction | RPC | Send | Receive (hook) |
|---|---|---|---|
| client to server | `APlayerController::ServerChangeName(FString)` | client local PC: `FindFunctionChecked("ServerChangeName")` then `ProcessEvent` (vtable +0x260), the same pattern the generated `ClientMessage` stub uses at 0x0403B70D. The `ServerChangeName` stub is not exported. | `ServerChangeName_Implementation` 0x0405C9B0 on the host |
| server to one client | `APlayerController::ClientMessage(FString, FName, float)` 0x0403B500 (exported stub) | host, on the target client's PC | `ClientMessage_Implementation` 0x0403B5F0 on the client |

Rules:

- **Framing.** Every mod message starts with the prefix `\x1Fubw1:`. The receive hooks handle prefixed strings and swallow them, so they never reach `AGameModeBase::ChangeName` or the HUD. Any other string passes through to the original.
- **Binary payload (from the channel smoke test on).** After the prefix, the payload is fixed binary, not text: one opcode `wchar_t` (an enum), then fixed-width argument `wchar_t`s, so each opcode has an exact `Num`. The host checks `Num` for the opcode, compares the prefix with one `memcmp`, and dispatches on a `switch`; nothing is parsed. The client keeps a prefilled static buffer and writes only the opcode and argument slots. Card 024's text verbs (`fire 0`, `fire 1`) move to this format with that card. Per-frame code never builds or compares strings; it compares cached `FName` values as integers.
- **Trust model: friends only (user, 2026-11-03).** Peers are trusted, and the priority is fast, small payloads. Keep only the checks that stop a crash or an out-of-bounds read: the exact `Num` for the opcode, a known opcode, and argument ranges used as indices. Unknown or malformed frames are dropped silently, with no reject logging and no anti-cheat. `ServerChangeName_Validate` (0x0405CA00) accepts any string longer than one character.
- **Traffic.** Send on change only, never per frame or on a timer. RPCs are reliable and ordered, so edges are not lost and need no heartbeat. Disconnects and pawn destruction (`Logout`, pawn gone) clear any held state on the host.
- **Ownership.** The sender is the PC the RPC arrived on, so payloads carry no sender id. This saves bytes and keeps routing simple; it is not a security measure.
- **State, not events.** The host resends the full lobby or downed state to every client on every change and when a client joins (`AGameModeBase::PostLogin` 0x03C63FB0). A late joiner then needs no history.
- Messages (opcode, then arguments; each argument is one `wchar_t`, 16 bits): client to host `FIRE on`, `READY on`; host to client `LOBBY count, then a ready bitmask`, `DOWN player_slot, state, whole seconds left`.

First card: a channel smoke test. The client sends `ping N` every 5 s, and the host answers `pong N` to that PC only. Log both sides and prove nothing reaches the HUD or the player name.

## 2. Lobby overlay (milestone 5, first part)

Flow:

1. **Main menu.** The overlay is shown and toggled with F8. It has a status line, an IP text box and two key hints: H to host and J to join.
2. **Host (H).** The mod arms a listen flag. When the host starts a mission from the game's own menu, the mod appends `?listen` to the map URL in the existing `UEngine::Browse` hook. Today the host listens through `UBWCOOP_TRAVEL`, and that stays as the test path.
3. **Join (J).** The mod reads the text box, validates it as an IPv4 address with an optional `:port`, and calls the existing `SetClientTravel` path with it.
4. **Ready-up.** It runs on the mission map, under the pause the game already holds there (`WB_Stack_C`, card 015). The host does not release the pause until every connected client has sent `ready 1` and the host has pressed ready. The release is `RequestGameUnpause`, the same as `UBWCOOP_AUTODROPSHIP`. The overlay lists the players and their ready flags from the host's `lobby` message.
5. **Disconnect before ready.** `AGameModeBase::Logout` (0x03C60E60) drops the player from the list, and the check is re-run.

Building the widget is native UMG with no assets (see STATE milestone 5, "Overlay groundwork"). Input is polled because there is no delegate target. While the overlay has focus, it uses `SetInputMode_UIOnlyEx`. On close, restore the game's own input mode and do not force GameOnly: card 018 found that the game sets input modes itself.

Open:

- **Rendering.** A bare `UUserWidget` cannot be created: the class is abstract (static, 2026-11-03). Use a concrete game widget class with a replaced tree (card 031). Fallback: keep that class's tree and change only its texts.
- **Clean start.** Does the client need the mission's chapter and level state, or does travel set it? Card 021 found that the client's GI chapter is -1. That breaks the loadout, and possibly more.

First card: render-only overlay test. Show a canvas with one `UTextBlock` and one `UEditableTextBox` at the main menu, log the text box contents when Enter is pressed, and take screenshots. It needs no network.

## 3. Downed and revive (milestone 3a)

Static facts are in STATE milestone 3a ("Player death flow"). Assets for the downed visual (pak index names, 2026-11-03):

- **No player downed state or animation exists.** The player has only three first-person arm death animations (`A_FPS_Arms_Death_01` to `03`, plus `GrenadeDeath`), played by `StartDeath`.
- **NPC troopers have a full death and revive cycle.** It is 8-direction sprite frames, one material instance per frame and direction (`MI_<Type>Trooper_<State>_<F|FL|L|BL|B|BR|R|FR>_<n>`), played by `USTSpriteAnimatorComponent` and `USTSpriteVariantAnimatorComponent` (native, with little reflected data). They are not skeletal animations, and NPC troopers are billboard sprites. States include `Death`, `Corpse`, `StandUp`, `Flinch`, `Idle`, `Walk` and `Run`. The medic adds `Heal`, `ABL_MedicTrooper_Revive` and `BP_MedicTrooper_RessurrectAttackData`. This is the NPC revive path that `HandleHealthValueChangedEvent` serves (STATE, enemy known issue). `MoritaTrooper` and `FemaleMoritaTrooper` have the richest sets.
- **NPC death and revive are health-driven (static, 2026-11-03; runtime kill proven in card 025).** `USTCombatUtilityLibrary::InstaKillTarget` (0x05241580) is the game's own kill helper, also used by fall zones, crushers and the cheat manager's `KillAllNPCs`. An NPC killed this way keeps its actor in the world for at least 60 s (card 025). `ASTNonPlayableCharacterBase::HandleHealthValueChangedEvent` (0x05193030) revives on old health <= 0 and new health > 0: it removes `Tag_Corpse`, restarts the core state machine and calls `BP_OnRevived`. Corpse lifetime is rationed by `TryTakeCorpseTicket`, which can call `InstaKillTarget`.
- **Downed visual plan.**
  - Others' view: play `Death`, hold `Corpse` while downed, and play `StandUp` on revive.
  - Own view: lower and tilt the camera, and hide or lower the arms. There is no first-person crawl, so a downed player cannot move, only look.
  - Whether the player's third-person body can be shown as an NPC trooper sprite (a sprite animator with the `MoritaTrooper` frame set) depends on card 022's audit of `Mesh` (+0x328). If that body is a skeletal mesh with no such animations, fall back to a static pose or ragdoll (`AN_TempRagdoll` exists).

Everything below is host-authoritative. Clients only display state from `down` messages.

States per player: `Up`, `Downed(t_left)`, `Out`. Bleedout is 30 s and the revive hold is 3 s; both are tunable constants.

- **Gate.** The intercept runs only when the net mode is `NM_ListenServer` and more than one player is connected. Single player stays vanilla.
- **Enter Downed.** Detour `ASTPlayerCharacter::HandleDeathEvent` (0x051CA3B0) on the host. If at least one other player is `Up`, skip the original and enter `Downed`:
  - Set Health to 1 through the same attribute write the card 021 test hit used, so the client's health sync shows it.
  - Cap the pawn's speed. Block the sprint input by refusing `AbilityInputTagPressed` (`InputTag.Walk`) for that pawn's ASCs on the host and on the client. Card 109 (user decision 2026-11-06): a downed player can still shoot, reload and aim, so Fire is not refused and a downed client's relayed Fire is passed on. Entering Downed still releases a held trigger and cancels the active shot once; the player presses again to shoot.
  - Broadcast `down`.
- **Damage while Downed.** Health must not reach 0 again, so clamp Health at 1 for Downed pawns on the host. Damage spent on a downed player drains bleedout instead: it subtracts seconds, at a rate to be tuned.
- **Revive.** An `Up` teammate within 200 units holds the interact input tag for the revive hold. The host sees that tag for both its own pawn and server copies (the card 021 `AbilityInputTagPressed` logger shows server copies' tags arrive). On completion, set Health to 30% of MaxHealth, return to `Up` and broadcast.
- **Bleedout reaches 0.** The player becomes `Out`. `Out` players stay revivable with a 6 s hold, so a full team wipe is the only fail state, as the user specified.
- **All down.** When no player is `Up`, the host runs the real death on its own pawn (`StartDeath`, then the game's 3 s countdown, `CompleteDeath` and `OnDeath`), so the game's blueprint performs the checkpoint reload. Whether clients follow the reload is a milestone 4 question; until then the test is that the host reloads and the client reconnects.
- **Kill floor.** `ASTPlayerCharacter::Tick` calls `StartDeath` directly below the kill threshold. For co-op players with an `Up` teammate, teleport to that teammate and apply 25% MaxHealth damage instead. The detour sits on the `StartDeath` vtable slot +0xA40, gated on the health still being above 0.

Static leads from 2026-11-03 are in card 030's "Static leads" section:
- `StartDeath` has no authority check, and it switches off autosave on the game instance.
- `RestartCurrentLevel` and the `DeadPlayerPawn` spawn are blueprint-only.

The downed detour already skips `StartDeath`. But the kill-floor detour falls through to the real `StartDeath` when no teammate is `Up`. If it does that for a client's server copy, the host's autosave is switched off. In that case the detour should treat the client as `Out` instead, and leave the real death to the host pawn's all-down path.

Open, for a log-only audit card first:

- Is `HandleDeathEvent` edge-triggered (called once, at the 0 crossing) or called on every damage while at 0?
- Does any blueprint act on Health reaching 0 before `HandleDeathEvent`, for example HUD death screens or `OnHealthValuesUpdated`?
- Which input tag is interact, and does the server copy receive it (card 020 listed interaction under parity)? Static lead (2026-11-03): probably `InputTag.Use`; `UADInteractorComponent` has no Server RPC, so the server copy likely never interacts and revive needs a channel opcode.
- What does the `OnDeath` blueprint do: reload directly, or a game-over screen first? Static answer (2026-11-03, card 030 leads): a game-over menu (`WB_PlayerDeathScreen`: Restart Mission, Load Last Save, Load Game, Exit), pushed by the local PC's `BP_HUD_Manager`. In co-op, only the host may act on it; a client's copy must never reach `OnDeath`, or the client can load a level on its own.

First card: a death audit. Use a test-only env var to kill the server copy with damage through the real pipeline (not a direct write). Log the `HandleDeathEvent` call count, the state of +0x9DC, whether `CompleteDeath` and `OnDeath` fire, and what the host and client screens show. Also check the four open questions above.

## Card order (proposal)

1. 024: fire relay (sent).
2. 022: remote body.
3. 023: enemy audit.
4. Channel smoke test, which unblocks both features.
5. Overlay render test.
6. Death audit.

Steps 4 to 6 are static-independent and could run in any order once milestone 2 is stable.

## 4. Remote player representation: weapons, mechs, turrets (static, 2026-11-03)

The player has no third-person body (user, 2026-11-03), so each machine draws a remote player as an NPC trooper sprite (card 028). The sprite has to show what the player holds and whether they are in a mech or on a turret. Facts below are from the pak index and PDB symbols; none are runtime-checked yet.

**Player weapons** (`BP_Weapon_*`): MoritaMk1, MoritaMk1Scoped, MoritaMk2, MoritaCarbine, VeteranRifle, HighPowerAutomaticRifle, LaserMorita (and LaserMoritaRed), PumpShotgun, Flamethrower, EPulse44, EPulse88, EPulsePistols, Railgun, RocketLauncher, NukeLauncher, AutoCannon, Unarmed; also MechRazorback and Turret. Weapon-specific actions are separate abilities: iron sights (most weapons), pump (shotgun), spool up and spool down (EPulse88, NukeLauncher), and the underslung grenade (MoritaMk1).

**NPC sprite sets** (`BP_CharacterAnimations_*`): MoritaTrooper, FemaleMoritaTrooper, ControllerTrooper, MK2Trooper, Shotgun_Trooper, Epulse88Trooper, LaserTrooperBlue, LaserTrooperRed, FlamerFieldScientist, MedicTrooper, RadioTrooper, SquadLeader, NonCombatTrooper(Female), TurretTrooper, M7Razorback. Each set has its own materials in the pak (`MI_<Set>_<State>_...`; for example `MI_Epulse88Trooper`, `MI_LaserTrooperBlue`, `MI_MK2Trooper`, `MI_FlamerFieldScientist`, `MI_SquadLeader` and `MI_M7Razorback` all have idle, walk, run or attack frames). The audit must dump each set's keys and direction counts.

**Proposed weapon mapping.** Swap the remote sprite's `AnimationData` when the equipped weapon changes, and fall back to the Morita set:

| Weapon | Sprite set |
|---|---|
| MoritaMk1, Mk1Scoped, Carbine, VeteranRifle, HighPowerAutomaticRifle | FemaleMoritaTrooper (8 directions) |
| MoritaMk2 | MK2Trooper |
| PumpShotgun | Shotgun_Trooper |
| Flamethrower | FlamerFieldScientist |
| EPulse44, EPulse88, EPulsePistols | Epulse88Trooper |
| LaserMorita / LaserMoritaRed | LaserTrooperBlue / LaserTrooperRed |
| Railgun, RocketLauncher, NukeLauncher, AutoCannon, Unarmed | FemaleMoritaTrooper until a better match is found |

Weapon actions map to keys inside the chosen set: fire to `Attack01`, reload to `Reload`, underslung or hand grenade to `GrenadeThrow`. Iron sights, pump and spool have no sprite state; they show as `Attack01` or `Idle`. The audit lists each set's keys, so the mapping can use any extra keys a set has.

**Mechs (`ASTPlayerMechBase`, `BP_BaseMech`).** The mech is its own actor with its own `SpriteRenderComponent`, `DirectionalMaterials` and `BP_MechRazorback_AnimationData`, so it already has a world body. Entering runs `EnterMech`, `TryEmbarkingAnimationSequence`, `OnEmbarkingComplete`, `AttachControllingPlayerCharacterToMech` and `AddMechInputContext`. Leaving runs `RequestLeaveMech`, `LeaveMech` and `HandleInputExitVehicle_Pressed`. `ControllingPlayerCharacter` names the pilot. Plan: while a remote player pilots a mech, hide their trooper sprite and let the mech's own sprite show. Open: is the mech actor replicated, does the client see it move and fire, and can a client enter one at all? Mechs are not placed in the levels: card 029's survey of C2M1 through C5M2 found turrets but no mech. A player mech arrives through the MechSupport tactical support (`BP_TacSuppPickup_MechSupport`, `Config_TacSupp_MechSupport`, `GA_TacSupp_MechSupport`, `BP_TacSupp_Pod_MechDrop`). A test can grant it with `USTTacticalSupportComponent::EquipTacticalSupportAbility(TSubclassOf<USTTacticalSupportAbilityConfig>, bool)` (0x050E69E0) and call it in with `TryThrowFlare(int slot)` (0x0510A5B0), or spawn `BP_BaseMech` directly and enter it through `OnInteraction` (0x050F7840; `EnterMech` 0x050E65F0, `LeaveMech` 0x050F2200).

**Mounted turrets (`ASTMountableTurret`, `BP_MountableTurret`, `BP_MountableTurret_Gunner`).** These are mesh actors (`StaticBase`, `GunHolderMesh`) with `MountTurret`, `AttachCharacterToTurret`, `LeaveTurret`, `DetachCharacterFromTurret`, `ControllingCharacter` and `IsBeingUsed`. Plan: while mounted, show the remote trooper with the `TurretTrooper` set (an NPC gunner sprite exists), or the `Idle` key behind the gun, and rotate the turret mesh from the gunner's aim. Open: does the turret's rotation replicate, and can a client mount one?

First card: a log-only audit (card 029). Dump every `BP_CharacterAnimations_*` set's keys and direction counts, log the equipped-weapon field and each weapon ability's activation on both sides, and log mech and turret enter and leave events on both sides, with whether the vehicle actor and its rotation replicate.
