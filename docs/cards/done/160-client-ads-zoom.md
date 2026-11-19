# Card 160: ADS zooms the client's camera

Status: CLOSED, outcome in docs/CARDLOG.md. Was: queued 2026-11-08, from docs/playtest-2026-11-08.md finding 9. Budget: three runs.

Read: docs/playtest-2026-11-08.md finding 9, card 021 (ability input tags), card 155 (ADS line).

## Why

The client's gun moves on ADS but the camera does not zoom. The client's ADS press found no ability bound
("InputTag.ADS ... handles -1", tag map Num 8). The pose replicates from the server; the zoom part runs only locally.

## Do

Find where the zoom is applied (camera FOV, camera mode, ability timeline) and make it run on the owning client.
Sweep the other InputTag entries missing from the client's tag map.

## Done when

- A run: the client's camera FOV drops to the ADS value on press and comes back on release, and the client's tag map
  holds every tag the host's own pawn has.
- The usual Report items (WARN diff, run length, rc, ERROR/FATAL, CRASH, dumps, DLL hash). It ends with "Report
  complete."

## From card 141 (senior, 2026-11-08)

Card 155's press and ADS log lines never fired for pawn actions: only `press IA_Pause` logged. The junior's
hypothesis (static): the pawn's actions are bound with FInputActionInstance-signature handlers, and the hooks cover
only the value, void and dynamic Execute variants. This card's ADS trace finds that binding anyway: hook it, and the
press and ADS lines are part of this card's proof.

## Trace (junior, 2026-11-09)

- The zoom is a GameplayEffect: the weapon's IronSights ability applies its FOVModifier effect to the pawn ASC's
  CameraFieldOfView attribute, and the camera reads that attribute. The attribute replicates from the server. The
  client's own IronSights activation cannot apply it (no prediction key the server accepts), so the zoom has to come
  from the host's server copy, the same way card 024 relays primary fire.
- Fix 1, the relay: the client's InputTag.ADS press and release on its own weapon ASC go to the host as a new fire
  kind, kFireAds (fire.cpp, chan_ops.cpp; soldier weapons only, not mechs). The host presses and releases
  InputTag.ADS on the server copy's weapon. Its IronSights applies FOVModifier there and the attribute replicates.
- Fix 2, the leak gates (full world sync, the other way): IronSights on the server copy also runs three local
  effects that would show on the host's own screen. These are a 2D sound (PlaySound2D and the ADAudioManager 2D
  path), a material parameter collection write (the scope FOV scalar) and a scope widget created with no owning
  player, which would land in the host's viewport. player_c99.cpp skips each of them on the host when the context or
  owner is a server copy, logs "card160: host ... skipped server copy ...", and gives per-map totals.
- Card 141's hypothesis is disproved statically. The exe has only three Enhanced Input binding Execute
  instantiations (value 0x51C57B0, void 0x51C5830, dynamic 0x446D9A0; read from the vtables), and all three are
  hooked. There is no FInputActionInstance-signature one. c158-r3 still showed 0 tabled and 0 untabled executes on
  the host for pawn presses, cause unknown. Runtime diag: playlog now logs every tabled action name, plus a count of
  executes that have no action.
- Tag map: the first press on each ASC (up to 8 a session) dumps the ASC's input tag map with each tag's handles
  ("card160: <side> input tag map of ..."). This compares the client's map with the host's own pawn's.
- The ADS line now comes from the relay edge (c26_own_fire, kFireAds): "playlog: client t=... ads 1 fov X", and the
  same line again at +500 ms. It does not depend on the Enhanced Input path.
- Known edge: the server copy's toggle-ADS setting reads the host's game settings.

Built: DLL 42b93b6221fa7e9c.

## Predictions, run 1 (combined with card 169: tools/scenarios/c160-c169.txt)

1. Client RightMouseButton held 1.5 s with the Morita: client "playlog: client t=... ads 1 fov F0". Host "card024:
   host relay press (ADS 1)". The client's "+500 ms" line shows fov below F0. On release: host "release (ADS 0)",
   and the client's "ads 0 ... (+500 ms)" line shows fov back at F0.
2. The host logs at least one "card160: host ... skipped server copy" line in the ADS window (the 2D sound or the
   MPC write), and no scope widget in the host's viewport. The map totals line comes at quit if the map changes;
   otherwise the per-line logs are the proof.
3. The tag map dumps: the client's weapon ASC map holds InputTag.ADS with at least one handle. The host's own weapon
   ASC map (the host's own ADS press, the control) holds the same tag set.
4. playlog lists the tabled actions on each side. If the host still logs 0 executes for pawn presses, the name list
   shows which actions are tabled, and that is the next lead (not this card's fix).
5. No new WARN classes beyond the card160 lines; rc 0/0; no crash.

## Run 1: c160-r1 (DLL 42b93b6221fa7e9c)

Ended by scenario after 126 s. Host rc 0, client rc 0. FATAL/ERROR 0 on both sides, no new crash dumps. Drive rc 1:
it stopped at line 53, a card 169 grenade wait that was a scenario fault (see card 169). Every card 160 step passed.

- Zoom: client "ads 1 fov 90.0" on press, then "ads 1 fov 61.0 (+500 ms)". On release "ads 0 fov 61.0", then "ads 0
  fov 90.0 (+500 ms)". Host "card024: host relay press (ADS 1)" at t=291115341 and "release (ADS 0)" at t=291116845.
- Gates: the host skipped the server copy's MPC_PlayerCharacterFOV write on the press and on the release (context
  BP_Weapon_IronSightsAbility_MoritaMk1_C). It also skipped two server copy WeaponSwap_MS 2D sounds on the client's
  weapon swap. No widget line: the Morita's IronSights made no scope widget.
- Tag maps: the client's weapon ASC (Morita) has Fire, Reload and InputTag.ADS:1. Pawn ASC: host Num 9, client Num
  8. The one tag the client lacks is InputTag.Skill.Ult, which is the grenade throw (G). The host's dump came after
  its grenade pickup bound that tag. The client's dump came before its own pickup, which bound it at t=291110613
  (card017 "Num 8 -> 9"). It is timing, not a gap: with the same items the maps match.
- Card 141's press lines: pawn actions are tabled on both sides in this run (host IA_Move, IA_Look, IA_UseItem;
  client IA_Move, IA_Look, IA_Interact, IA_SwitchWeapon_Slot2, IA_ADS, IA_Jump, IA_MoveUp). c158-r3's "0 tabled"
  did not repeat.
- WARN diff: host 3 (attach and config, as before, plus the card169 incendiary control line). Client 6 (attach,
  config, and the known card 132 STAISpawner/STSpawnPoint rows).

Card 160 is done.

## Review (senior, 2026-11-09)

Accepted on c160-r1: the client's ADS press reaches the server copy's weapon, the FOV effect replicates back (90 to
61 and back), and the server copy's local effects are gated. One gap is not an edge: the server copy's toggle-ADS
setting reads the host's settings. A host on hold and a client on toggle (or the reverse) gives the client the
wrong ADS behavior. Fix, batched into card 162's build: the client's own toggle-ADS setting goes with the relay (or
the client sends the ADS state, not the press), and the server copy uses it. Proof: one run step with the two sides
on different settings.

## Toggle ADS fix (junior, 2026-11-09)

Static trace: BP_Weapon_IronSightsAbility picks hold or toggle on the machine that runs it. It calls
GetSTGameUserSettings, then IsToggleADSKBMEnabled or IsToggleADSControllerEnabled, chosen by
UADInputDetectionSubsystem::CurrentInputGamepad. All three are native exec thunks (exe+0x50C7FA0, +0x50C7F70,
+0x4C5F0F0). On the host, the server copy's ability reads the host's settings and input device.

Fix (player_c99.cpp, card 160 section): the client puts its three values on every ADS edge. They go in fire arg bits
8 to 11 (kFireAdsToggleKbm, kFireAdsToggleCtrl, kFireAdsGamepad, kFireAdsKnown); the kind is masked to 7 bits on both
ends. The host keeps the bits per PlayerController (c160_host_ads_bits, before the relay). The three thunk detours
run the original. Then, when the Blueprint caller (Stack.Object) belongs to a server copy, they write that client's
value instead. A server copy read with no client value yet keeps the host's own value and logs a WARN. The rows reset
on each map. Test switch UBWCOOP_C160_TOGGLE_ADS=1 (client): SetToggleADSKBM(true) is an in-memory field write;
nothing is saved.

### Predictions, card 162 run 1 (tools/scenarios/c162-run1.txt; client UBWCOOP_C160_TOGGLE_ADS=1, host on hold)

1. First 700 ms tap. Client: "card160: client test: SetToggleADSKBM(true)" and "ADS edge carries toggle KBM 1, toggle
   controller 0, gamepad 0". Host: "<PC> ADS settings: known 1, toggle KBM 1, ...", then "server copy ... reads
   IsToggleADSKBMEnabled: the client's 1 (host's own 0)".
2. The zoom stays after the release: "ads 1 fov 61.0 (+500 ms)", then "ads 0 fov 61.0 (+500 ms)". Before the fix,
   the release returned it to 90.0.
3. Second tap: "ads 1 fov 61.0" on the press. The ability ends, so the +500 ms lines read fov 90.0.
4. No "WARN card160" line for these reads.
