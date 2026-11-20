# Card 045: Co-op restart after game over

Status: done 2026-11-04.

Read `docs/STATE.md` first ("Mod code rules", "Runtime facts", known issue "Co-op restart after game over"), then card 002 (auto-travel, `SetClientTravel`), card 043 (downed state) and card 044 (death screen, Report and Senior review). Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

After all down, the host's RESTART MISSON calls `UEngine::Browse` with a plain map URL and no `listen` option. The host reloads the mission standalone (net mode 0). Its net driver closes, and the client Browses to the title screen 68 ms later (card 044, `logs/card044x3-*.log` around 558110977). The client's own death-screen buttons act only on the client, so a restart there would desync. Co-op needs a restart that keeps the session and brings the client back into the new mission.

## Do

1. **Log the restart URLs first.**
   - In the existing `detour_browse` (hooks.cpp ~375), log the URL's map, host, port and every option. `url_copy` is an `FURL` passed by value. Read its layout from the UE5 source: `Protocol`, `Host`, `Port`, `Valid`, `Map`, `RedirectURL`, `Op` (`TArray<FString>`), `Portal`. Confirm the offsets against a known URL, e.g. the card 002 host travel `/Game/Maps/FED/FED_C1M1?listen`.
   - Log the host's URL for RESTART MISSON and for RESTART CHECKPOINT (LoadLastSave), driven by the card 044 `ProcessEvent` test path. LOAD GAME and EXIT are out of scope.
2. **Host keeps listening.**
   - Condition: the host is `NM_ListenServer` with 2 or more PlayerControllers (or had them at all down), and `Browse` gets a local map URL (empty `Host`) with no `listen` option.
   - Make the load keep listening. Preferred: add `listen` to the URL's `Op` array in place, before calling the original. Allocate the new `FString` with the engine's allocator (the same way the mod already builds FStrings), never with the CRT.
   - If editing `Op` in place is not safe, the fallback is `SetClientTravel(world, "<map>?listen<other options>", TRAVEL_Absolute)` instead of the original call. Report which one you used and why.
   - Never change a URL in single player or on the client.
3. **Client follows.**
   - On the client, remember the address it last connected to: the `SetClientTravel` address from `UBWCOOP_TRAVEL`, or the lobby's typed address.
   - When the client's session drops and it Browses to the title (a local map while `g_client_session` was set), let that load finish. Then reconnect with `SetClientTravel(world, <address>, TRAVEL_Absolute)` from the game thread (the card 002 auto-travel tick path), every 3 s, for up to 60 s, until `Browse` returns Pending and `LoadMap` succeeds.
   - Log each try. Stop the retries if the user starts something else (any other `Browse`).
4. **Client death-screen buttons.**
   - On the client, when the death screen is built (card 044's hook), disable RestartMission, LoadLastSave and LoadGame with `UWidget::SetIsEnabled(false)`. Keep Exit, so the player can still leave.
   - If `SetIsEnabled` is not in the publics, write the `bIsEnabled` bit, with the offset read from the reflection data, not guessed. Log what was disabled.
5. **Mod state across the reload.**
   - On both machines, clear the card 043 entries and holds, `client_seen`, the all-down flag and the card 044 one-shot flags when a new map loads. Log the reset.
   - After the restart, run one more down and revive: a hit to the copy, then the host's interact hold. This proves the state is fresh.
6. **Run and report.**
   - Two-player run `card045x`: all down, the client gets the death screen, the client's buttons are disabled, the host test presses RESTART MISSON, the host reloads with net mode 2, the client reconnects into the mission, then one down and revive.
   - Solo run `card045s`: RESTART MISSON must reload standalone as before (net mode 0, URL unchanged).
   - Use `shot showui` on the client's disabled buttons, and after reconnect on both machines. Read every shot before describing it.

## Done when

- The restart URLs (MISSION and CHECKPOINT) are logged with their options.
- After the host's restart, the host's net mode is 2. The client's `Browse` returns Pending, then `LoadMap` returns true into the same map, and both pawns are on foot.
- After the restart, one down and revive works (Down and Use counts, Health 30).
- On the client, the three buttons are disabled and Exit is enabled (log and shot).
- Single player: the restart URL is unchanged and the reload is standalone.
- No crash or new dump. rc 0 on both runs. The build is clean with `/W4 /WX`, and every function is 60 lines or fewer.

## Risks

- `Browse` may be called with the URL by value, and the original may copy `Op` again. If editing the copy does not reach `LoadMap` (net mode stays 0), use the step 2 fallback.
- RESTART CHECKPOINT may load save data, not just the map. If its URL carries a save or checkpoint option, keep it as is and only add `listen`. Report whether the client's mission state then matches the host's.
- The client may reach the title before the host is listening again. The retries cover this; do not shorten the 60 s cap.
- The host's death screen draws over a stale level-intro card (card 044, shots 00865/00867). It is not in scope, but note whether it shows up again after the restart.

## Senior addendum (2026-11-04): symbols for steps 2 and 4

From the publics (RVA, base 0x140000000). Add any you use to `tools/symbols.txt`:
- `FMemory::Malloc(size_t, uint32 align)` 0x011FD810 and `FMemory::Realloc(void*, size_t, uint32)` 0x011FFFF0. The mod already resolves `FMemory_Free`. Use these to grow `FURL::Op` (TArray: data, Num, Max) and to allocate the new `FString`'s buffer (`L"listen"`, Num 7 including the NUL).
- `FURL::FURL(FURL* Base, const TCHAR* TextURL, ETravelType)` 0x042E9400 parses a URL string, and `FURL::~FURL` 0x0373EC40. Use them to build a known FURL (e.g. `L"/Game/Maps/FED/FED_C1M1?listen"`) and read the field offsets from it at runtime, instead of trusting the source layout.
- `UWidget::SetIsEnabled(bool)` 0x0362DFC0 is a virtual (`UEAAX_N`). Calling the RVA directly is fine. Step 4's fallback (writing `bIsEnabled`) is not needed.

## Senior addendum 2 (2026-11-04): FURL layout, read from the copy constructor

`FURL::FURL(const FURL&)` 0x03BFE240 copies, in order:
- `Protocol` FString at +0x00;
- `Host` FString at +0x10;
- `Port` int32 at +0x20 and `Valid` int32 at +0x24;
- `Map` FString at +0x28;
- `RedirectURL` FString at +0x38;
- `Op` `TArray<FString>` at +0x48 (data +0x48, Num +0x50, Max +0x54);
- `Portal` FString at +0x58.

The size is 0x68. This matches the UE5 source. Still confirm it once at runtime with the parse constructor (addendum 1) before writing `Op`. When you grow `Op` past `Max`, realloc the data with `FMemory::Realloc` and set the new `Max`. Each element is a 0x10-byte FString (data, Num, Max).

## Senior addendum 3 (2026-11-04): a simpler place to add `listen`

The game's level loads go through `USTGameInstance::OpenLevel(FString)` 0x050FA520. It updates the meta game (level timer, weapons found), clears secrets and music triggers, then calls `UGameplayStatics::OpenLevel(WorldContext, FName LevelName, bool bAbsolute = true, FString Options)` 0x03C62660 at 0x1450FA7B2. That one builds `LevelName?Options` and hands it to `SetClientTravel`, so `Browse` runs on the next tick.

The death screen's blueprint calls `USTSaveSystemLibrary::LoadLatestSave` 0x0520B350 and `IsLatestSaveValid` 0x05208F30 (from its name table). So RESTART CHECKPOINT is a save load, and that load most likely also ends in `OpenLevel`.

Option for step 2: detour `UGameplayStatics::OpenLevel` instead of editing `FURL::Op`. On the co-op host, append `?listen` (or `listen` when Options is empty) to the `Options` FString before calling the original. The FString is passed by value (a pointer to a temporary in `r9`). Build a new FString with `FMemory::Malloc` and pass that, and free it after the call. This avoids TArray surgery.

Keep the `Browse` URL log from step 1 either way, so the final URL is proven to carry `listen`. Use whichever path you have working. If the save load does not go through `OpenLevel`, the `Browse` path is the fallback.

Note to addendum 3 (senior, later the same day): `LoadLatestSave` calls `GetLatestSave` 0x052012D0, then `USTSaveSystemLibrary::Load` 0x0520A580. `Load` reads `UGameplayStatics::GetCurrentLevelName` and `USTMetaGame::GetLevelIDsFromLevelName`, and it calls `ASTGameplayGameMode::LoadSettingsFromSave` 0x0512C8E0 and `USTMusicManager::LoadSettingsFromSave` directly. So a checkpoint load in the same level may restore in place, with no travel and no `Browse`. If step 1 logs no `Browse` for RESTART CHECKPOINT, say so. Then the session is kept, but the client's world is not restored. Report it as found; do not fix it in this card.

## Report

**Method.** The host hooks `UEngine::Browse` and edits the by-value `FURL` copy in place. When the host is `NM_ListenServer` with 2 or more PlayerControllers, a local URL and no `listen` option, it appends `L"listen"` to `Op`, using `FMemory::Malloc`/`Realloc`. The step 2 fallback was not needed. At startup the mod builds a known FURL (`"/Game/Maps/FED/FED_C1M1?listen"`) through the `FURL` ctor/dtor and verifies the field offsets at runtime: Map +0x28, Op +0x48, size 0x68. This matches senior addendum 2 (copy ctor 0x03BFE240). Senior addendum 3's `UGameplayStatics::OpenLevel` Options path was not used, because the Browse path already works. The client hooks the drop to the local map, then calls `SetClientTravel("127.0.0.1:7777", TRAVEL_Absolute)` every 3 s with the 60 s cap kept. The client's `WB_PlayerDeathScreen` disables RestartMission, LoadLastSave and LoadGame through `UWidget::SetIsEnabled` (0x0362DFC0) and keeps Exit. Per-map state for cards 043 and 044 is reset on every new map on both machines.

**RESTART CHECKPOINT does not travel.** `LoadLastSave` is an in-world reload: there is no `Browse` and no `LoadMap`. `GA_ResetStatusCheckpointLoad` is granted, `LoadInventory` runs, and both players get new pawns in the same world. So it has no URL and no save option to carry, and the session never drops. This caused two problems, both now fixed:
- Per-map state was never reset. `c45_respawn_check` (called from `c43_step`) now detects a pawn change while card 043/044 state is stale. It logs "respawn in place (new pawn ..., no map load): per-map reset" and runs the same resets as a map load.
- On the client, both the game-over and the checkpoint load construct `WB_PlayerDeathScreen`, and nothing removed them. The screens stacked and stayed on screen in gameplay. The client now tracks up to 4 death screens per map as weak pointers. On respawn in place it removes them all and sets input to GameOnly ("2 of 2 death screens removed").

**Results.** Pair run `card045x` (run 4; logs in `logs/card045x-run4-{host,client}.log`, shots in `logs/card045-shots/`). Runs 1–3 were the iterations that found the checkpoint issues above.

| Check | Result |
|---|---|
| RESTART MISSON URL | Host Browse map `/Game/Maps/FED/FED_C1M1`, 0 options, port 7777. Then "listen added to the Browse URL (in place, engine allocator)", and the URL as passed on has 1 option, `listen`. |
| RESTART CHECKPOINT URL | None: no Browse (in-world reload, see above). |
| Host net mode after restart | 2, players 2 ("test phase 1: on foot, net mode 2, players 2"). |
| Client follow | Drop to `3D_Frontend` (option `closed`). Local map loaded 830 ms after the drop. Try 1 at +3007 ms. `Browse` returned 2 (Pending), then `LoadMap` true into FED_C1M1: "reconnected (1): try 1, 6394 ms after the drop". |
| Down/revive after restart | Server copy Downed, then Out, then "Up (revived from Out after a 6000 ms hold): Health 30.0 of 100.0". Run 2 counts: Down sent 36 (host) and received 32 (client). This run uses the bleedout path, so Use sent is 0. |
| Client buttons | Log on every death screen: "3 buttons disabled (RestartMission, LoadLastSave, LoadGame), Exit bIsEnabled 1". Shot `run2-client-death-screen.png`: RESTART CHECKPOINT, RESTART MISSON and LOAD GAME are greyed and EXIT is bright. |
| After the checkpoint | Host shot `run4-host-after-checkpoint.png`: camp gameplay, objectives panel filled, dialogue. Client shot `run4-client-after-checkpoint.png`: gameplay, HUD, Morita MK1, no death screen. |
| Exit | Host rc 0, client rc 0, FATAL/ERROR 0, WARN 0, no new crash dumps. The runner ended by done after 243 s. |

Solo run `card045s`: RESTART MISSON gave "Browse URL unchanged: net mode 2, players 1, local map 1, listen option 0" and a standalone reload ("test phase 1: on foot, net mode 0, players 1"). Before the restart the solo host was net mode 2, because the test launch URL carries `listen`. The restart drops to standalone, as in vanilla. Host rc 0, FATAL/ERROR 0, no new dumps. The 2 WARN lines are the existing autodropship "1 PlayerController(s)" notices. Shot `solo-host-after-restart.png` shows the BASIC TRAINING level-intro card ("PRESS ANY KEY TO CONTINUE").

Build: `/W4 /WX` is clean, and no function is over 60 lines.

**Answers to the card's questions.**
- CHECKPOINT save option: none. There is no URL at all, because the checkpoint reloads in place.
- Client mission state vs host: probably a mismatch. After the reconnect and after the checkpoint, the client's OBJECTIVES panel is empty (shots `run2-client-after-reconnect.png`, `run4-client-after-checkpoint.png`). The host shows TACTICAL SUPPORT TRAINING, SHOOTING RANGE and ASSAULT COURSE. Objective state does not seem to replicate to the client.
- Stale level intro: yes, it shows again. On the host after RESTART MISSON, the intro FMV replays. The host's next game-over menu draws over the still-playing FMV (shots `run3-host-after-restart-mission-fmv.png`, `run4-host-game-over-after-restart.png`, `run4-host-game-over-later.png`). In solo, the BASIC TRAINING intro card shows after the restart. The client after its reconnect shows no FMV.

**Open items.**
- The client's objectives panel is empty after a reconnect or checkpoint (above).
- The intro FMV replays on the host after RESTART MISSON, and the game-over menu draws over it.
- After the checkpoint, the mouse cursor is still drawn on the client (top-left in the shot). The host's checkpoint `SetInputMode(GameOnly)` also logs `bShowMouseCursor 1`. Whether `IMC_DummyUI_KBM` stays mapped on the client after the screens are removed was not checked.
- Cosmetic: before the session and after the drop, the client log labels its URL lines "host", because `side()` is false while not connected.

## Senior review (2026-11-04)

Accepted. Verified from `logs/card045x-run4-{host,client}.log`, `logs/card045x.runner.txt` (rc 0 on both, FATAL/ERROR 0, WARN 0, no dumps), `logs/card045s-host.log`, `logs/card045s.runner.txt`, and every shot in `logs/card045-shots/`.

What holds:
- The FURL layout is verified at runtime on both machines.
- RESTART MISSON on the co-op host: the URL arrived with 0 options, `listen` was added in place, and the host reloaded with net mode 2 and 2 players (host log 996-999, 1484).
- The client dropped at 561050697, tried once at +3007 ms (Pending), and `LoadMap` returned true 6394 ms after the drop (client log 962-1118).
- Down and revive after the restart work: Downed, then Out, then Up after the 6000 ms hold with Health 30 (host log 1556-1737).
- The client's three buttons are disabled on every death screen. `run2-client-death-screen.png` shows them greyed, with EXIT bright.
- RESTART CHECKPOINT reloads in place with no `Browse`, as the static note predicted. The respawn-in-place reset and the removal of the client's death screens are good additions.
- Solo: the URL is unchanged and the reload is at net mode 0. `solo-host-after-restart.png` shows the BASIC TRAINING intro card.

Findings (open, not blocking):
1. **The checkpoint re-runs `CompleteDeath`.** The host's LoadLastSave click runs `CompleteDeath` on the host's own pawn 7 ms later and builds a second host death screen (host 1968-1974). The client's own pawn gets a second `CompleteDeath` 712 ms later (10599 ms after its StartDeath, client 1711) and a second screen. That second screen is why the client stacked them. The host's screen goes away on its own (`run4-host-after-checkpoint.png` shows no menu). The caller is unknown; log the return address when checkpoints are next touched.
2. **Checkpoint state is not synced, beyond the objectives.** The server copy's inventory is loaded from the save (`BP_WeaponDefinition_Unarmed_C`, host 2127-2128), and the host HUD reads UNARMED. The client's own HUD still shows the Morita MK1 (`run4-client-after-checkpoint.png`). Together with the empty client objectives, the client keeps its pre-checkpoint local state.
3. The client mouse cursor stays drawn after the checkpoint (confirmed in the shot, top left).
4. The shot name `run4-host-game-over-after-restart.png` is wrong: it is an intro FMV frame ("and do your duty all from the comfort of your home!"), not a game-over screen. Name shots for what they show.
5. The host's `CompleteDeath` line prints "-1 ms after the client's StartDeath", a metric that has no meaning on the host. The client "host" label is noted in the Report.
