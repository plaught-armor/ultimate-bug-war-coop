## Architecture (planned)
Injected proxy DLL + MinHook, functions resolved by RVA from the PDB. Host = listen server, authoritative; bug AI host-only. Use UE native replication: force bReplicates, hook GetLifetimeReplicatedProps for game classes, custom RPC path for events. Patch single-player assumptions (GetPlayerCharacter(0) etc.).

## Milestones
Design for the mod channel, the lobby overlay and downed/revive: `docs/design/coop-channel-lobby-downed.md` (senior draft, 2026-11-03).
1. Two instances in same map, see each other move. Done.
2. Shooting/damage/health synced. ← current (cards 022, 023)
3. Bug spawns + AI host-driven, replicated.
3a. Co-op death rule (user decision 2026-11-03): downed and revive. A downed player can be revived by a teammate within a timer; when every player is down, restart from the last checkpoint. The game is single-player and has no downed, revive, respawn or spectator classes, so this is new mod code driven by the host. A downed player can still shoot, reload and aim (card 109, user decision 2026-11-06); only sprint (`InputTag.Walk`) and interactions (card 081) are refused while Downed. Player death flow (static, 2026-11-03):
   - **Two ways in.** `ASTPlayerCharacter::HandleDeathEvent` (0x051CA3B0) is the health path. `ASTPlayerCharacter::Tick` (0x051E06F0) is the other: when `USTCommonUtilityLibrary::IsBelowKillThreshold` is true, it calls `StartDeath` (vtable +0xA40, for MI `ASTPlayerCharacterMI::StartDeath` 0x051DEE70) directly, with no health change.
   - **`StartDeath` (base 0x051DE410).** It calls `APawn::DisableInput` and turns off autosave (`SetAutoSaveEnabled`, `CancelAsyncSaveProcess`). It picks a random death animation from `USTCharacterData`, blocks new animations, moves and shrinks the capsule, and runs `CancelAllAbilities`. Last, it sets `TimeUntilDeathCompletion` (+0x9DC) to 3.0. The MI override then reads the equipped weapon's ASC.
   - **The countdown.** `Tick` counts +0x9DC down by DeltaTime. When it reaches 0, it calls `CompleteDeath` (vtable +0xA48, 0x051C27A0).
   - **`CompleteDeath`.** It only zeroes +0x9DC and broadcasts `OnDeath` (+0x898, a blueprint multicast). The game-over or checkpoint reload is therefore blueprint logic bound to `OnDeath`.
   - **`DeadPlayerPawn` (+0x8A8).** A class property that is probably the spectator or corpse pawn. Unverified.
   - **Downed hook point.** On the host, detour `HandleDeathEvent` and the kill-floor `StartDeath` call for a co-op player. While a teammate is still standing, enter a mod-owned downed state instead of `StartDeath`. Only let `StartDeath` and the `OnDeath` broadcast run once every player is down, so the game's own blueprint does the checkpoint reload. That broadcast must run on the host; whether the client then reloads through level travel is a milestone 4 question. Not yet runtime-checked: whether `OnDeath`'s blueprint handler reloads directly or shows a game-over UI first.
4. Objectives, pickups, checkpoints, level travel.
5. Lobby/join UI, 4 players, difficulty scaling. User decision 2026-11-03: both an in-game Host / Join-by-IP overlay with a ready-up wait (first, and pulled forward once milestone 2 is stable, so tests stop depending on env vars) and a LAN browser later. The binary has the engine's `OnlineSubsystemNull` (LAN sessions), `OnlineSubsystemSteam` (unusable: GOG build), `GameSession` and the blueprint `Create`/`FindSessions` callback proxies, but no game lobby UI. Overlay groundwork (static, 2026-11-03):
   - **UMG is in the binary.** Exported: `UUserWidget::AddToViewport` (0x0360E990), `UPanelWidget::AddChild` (0x03596690), `UEditableTextBox::SetText` (0x035E3300), `UWidgetBlueprintLibrary::SetInputMode_UIOnlyEx` (0x03645EA0), `APlayerController::ConsoleCommand` (0x0403E380), and the `UCanvasPanel` and `UTextBlock` class constructors. `UButton`, `UVerticalBox` and `UEditableTextBox` have no `NoRegister` export, but their classes exist at runtime, so find them by name.
   - **Plan.** Build the overlay in native code with no new assets:
     - construct a plain `UUserWidget` with its own `UWidgetTree`, a canvas root, an IP text box and status text, then call `AddToViewport`;
     - the mod owns no UObject that could take a dynamic delegate, so poll instead of binding `UButton::OnClicked`: keys plus the text box's `Text` property, each tick;
     - Host is `Listen` (already hooked), and Join is the existing `SetClientTravel` path with the typed address;
     - ready-up needs a mod channel, so it waits until a replicated mod state exists.
   - `UUserWidget` is abstract (static, 2026-11-03: `ClassParams` ClassFlags 0x00A810A1 has `CLASS_Abstract`), so a bare instance cannot be created. Use a concrete game widget class and replace its tree (card 031). Unverified: whether that renders.
