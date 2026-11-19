# Card 155: Playtest logs answer the questions the 2026-11-08 playtest left open

Status: CLOSED, outcome in docs/CARDLOG.md. Was: queued 2026-11-08 (user, after the playtest: "we will need more logs also for play tests"). Budget: three
runs. First in the post-playtest queue: cards 141, 143, 156, 157, 158, 160, 161 and 162 read these lines. Closed partial 2026-11-08 after c155d; open proofs carried into card 141 (senior).

Read: docs/playtest-2026-11-08.md (findings 2, 4, 9, 12, 13, 15, 16, 19, 23, 24 and "Host log"), docs/STATE.md
"Card scope and investigation rules".

## Why

The playtest logs pinned most bugs, but several needed data no log had:

- Armor (finding 19): the damage-handler hooks (UADDamageHandler_ApplyDamageToArmour, ApplyDamageToHealth) are in
  the host's "hooks: skipped" list, so no hit on a player pawn is logged.
- Hit registration (findings 15, 16): no round-trip time, and no way to match a client shot to the host's shot
  except by hand, by time and origin, across two clocks.
- Input and cursor (findings 23, 24): RemoveMappingContext, widget removal and the setter that turns
  bShowMouseCursor back on are not logged.
- Fire mode, ADS, grenades, doors, audio (findings 16, 9, 13, 4, 2): the weapon switch, the ADS camera, the throw
  aim, door state and where the other player's sounds play are not logged.
- The clock offset between the host and client logs had to be worked out by hand from a DigestQuery pair.

## Do

Every line is rate limited (per class and per second, with a suppressed count), and a 1-hour play session must stay
under 30 MB per log. The lines are on in the play build; they exist for the playtest logs.

1. Clocks: at join and every 60 s, both sides log the host and client clocks from one Ping/Pong pair: "clock host t
   ... client t ... offset ... rtt ... ms". Every 60 s also rtt min, avg and max.
2. Player-pawn damage (host): one line per hit on any player pawn (the host's own and every server copy): instigator,
   damage type, damage, Health and Armour before and after, and whether the armor or the health handler ran. Player
   pawns only: find why these hooks are skipped, and keep NPC hits off the logging path.
3. Shots: the client gives each own launch a shot sequence number and sends it with its fire, or logs it with time,
   origin and aim. The host logs each server-copy launch with the same number when it has it, else with time and
   origin. Add the script `tools/match_shots.py HOST_LOG CLIENT_LOG`: it uses the clock lines, pairs the shots, and
   prints matched shots, dropped shots (with the client's weapon, ADS and reload state at the time) and pellet-count
   mismatches.
4. Input (client): AddMappingContext and RemoveMappingContext with caller; SetInputMode; every change of
   bShowMouseCursor with its caller (find the setter, finding 23); AddToViewport and RemoveFromParent for the death
   screens, the pause menu and the downed HUD.
5. Weapon and abilities (both sides): the client's weapon-switch and fire-mode input (IA_SwitchWeapon_*); the server
   copy's current weapon and fire ability class after each change; the ADS press, the ability that ran and the camera
   FOV 0.5 s after; the grenade throw: the client's aim at the press, and the server's throw aim and velocity.
6. Doors (both sides): every placed door's open or close, with the cause (host row or local sim) and position; on the
   client, when its pawn is blocked by a door it shows open.
7. Remote-player audio (the client, and the host for the client's pawn): for sounds started on the other player's
   pawn or weapon: sound name, location, 2D or 3D, attenuation set, distance to the listener. At most one line per
   sound per 2 s.
8. A summary every 300 s and at session end, both sides: own and copy shots, hits, downs, deaths, loads, joins,
   rejoins, digest mismatches, and the lines suppressed by the rate limits.

## Done when

- One scripted pair run (FAST=1) that fires rifle and underslung, aims, throws a grenade, takes hits, goes down and
  is revived, and passes a door: every line above shows on the right side, and `tools/match_shots.py` prints its
  table for the run.
- The run's log sizes and the projected size for 1 hour.
- The usual Report items (WARN diff, run length, rc, ERROR/FATAL, CRASH, dumps, DLL hash). It ends with "Report
  complete."

## Trace (static, before code)

Budget first. Play-build logs from the 2026-11-08 playtest: host `client/UBWCoop-menu-20261108-190720.log` 21.2 MB in
3172 s = 24.0 MB/h; client `UBWCoop-menu-20261108-191343.log` 12.0 MB in 2783 s = 15.5 MB/h (verified, stat + first
and last t). The host is already near the 30 MB/h limit. Its largest classes (verified, awk by card): card132 8.3 MB,
of which the per-launch "launch #" lines are 21371 lines / 6.3 MB (most are AI rounds); card130 "fx grenade explosion"
watch lines 28312 / 5.7 MB; card074 2.5 MB. So the new lines need room: the per-launch card132 lines are replaced by
the item 3 shot lines (player shooters only, one line per burst; AI launches only counted), and the card130 fx watch
lines go through the new rate limiter. Target: host under 20 MB/h, client under 20 MB/h, with all of card 155 on.

Common part: a new `ubw/playlog.cpp` owns the rate limiter (per class: N lines per second, the rest counted as
suppressed, a "suppressed" total per class in the summary), the once-per-key-per-2 s table (audio, server-copy
activations), the session counters for item 8 and the 300 s summary. Fixed tables only, peaks logged.

1. Clocks. Carrier: new text op `ChanOp::Clock`, both directions (verified: `ChanOp` enum in ubw/chan_ops.h, text
   ops up to 1400 chars). Host, at the first Pong of a PC (chan_ops.cpp c26_host_pong, verified) and every 60 s from
   c26_ping_step (fire.cpp, verified, 5 s step on both sides): sends "Q n host_t0". Client replies "A n host_t0
   client_t". Host: rtt = now - t0, offset = client_t - (t0 + rtt / 2); logs "clock host t ... client t ... offset
   ... rtt ... ms" and sends "R n host_t client_t offset rtt" so the client logs the same line. rtt min/avg/max: fed
   from c26_pong's rtt (fire.cpp, verified), logged and reset every 60 s on both sides. Verdict: buildable; the
   offset sign matches the playtest's hand-worked "host t = client t - 249903283" (offset = +249903283).
2. Host player-pawn damage. Why skipped (verified): the hooks UADDamageHandler_Process, ApplyDamageToArmour
   (0x04A3B110) and ApplyDamageToHealth (0x04A3B190) sit in the hook table with gate `&g_gate_c40` (hooks_ubw.cpp
   1883-1889), and `g_gate_c40 = g_c40_trace` (flags.cpp:711, UBWCOOP_C40_DAMAGE_TRACE, default off). So they are
   card 040's gated diagnostic, and that trace only covers non-trooper enemies on the host (detour_c40_process,
   `c37_enemy(target)`). Change: the three hooks get no gate. In Process, target = c40_target(params); one class test
   (`is_a(target, g_player_character_class())`, else the mech class) picks player pawns; NPC hits return after that
   test with nothing logged. The player-hit record holds params, Health/Armour before (health set +0x30, armour
   +0x50, verified hooks_internal.h:1307 and finding 19), and two flags set by the Armour and Health detours when
   `params` matches. After Process returns: one line with instigator and causer (c40_process_begin, verified),
   damage type (spec Def at spec+0x0, UE FGameplayEffectSpec layout, open: confirm the name in the run), damage
   (+0x3C), Health/Armour before and after, armour ran 0/1, health ran 0/1. Client side: nothing (the server does
   the damage).
3. Shots. Log only, no wire change. Entry: c132_proj_launch (proj_c132.cpp, verified; called from detour_c74_spawn,
   grenades.cpp:816). Launches with the same shooter in the same game tick are one burst; it is closed at the next
   launch of another tick or at the per-tick step. Client: own launches only (`copying` false, shooter the own pawn
   or its weapon): "shot client seq N t ... weapon W init D pellets K ads A reload R at (x,y,z) P Y". Host:
   server-copy launches (shooter's pawn is a remote PC's): "shot host copy seq N t ... same fields"; the host's own
   shots too (side "own"). ADS state: open (the ADS tag/ability state, read from the pawn's tag container or the
   ADS ability's ActiveCount, chosen in the run); reload: an active reload ability on the weapon ASC, same check.
   `tools/match_shots.py HOST_LOG CLIENT_LOG`: reads the clock lines (piecewise offset), maps each client shot to
   host time, pairs it with the nearest host copy shot within -400..+1500 ms (the finding 16 window) and 300 uu,
   prints matched, dropped (with weapon, ADS, reload) and pellet mismatches. The old per-launch card132 lines stop
   (AI launches are counted only).
4. Client input. AddMappingContext (detour at hooks_ubw.cpp:1521, verified) and SetInputMode (1515 and the exec
   UIOnlyEx 1729, verified): one rate-limited line each with caller. RemoveMappingContext: only a resolved pointer
   today (hooks_ubw.cpp:2153, verified), gets a pure-logging hook
   `?RemoveMappingContext@IEnhancedInputSubsystemInterface@@UEAAXPEBVUInputMappingContext@@AEBUFModifyContextOptions@@@Z`.
   bShowMouseCursor: hook `?SetShowMouseCursor@APlayerController@@QEAAX_N@Z` (caller), plus a per-tick read of the
   bit (reflected bool, kBoolFieldMask) that logs any change the setter did not make (a Blueprint property write);
   the line names that case "direct write, last ProcessEvent function F" (open: whether that function name is
   enough to find the setter, finding 23). Widgets: hooks `?AddToViewport@UUserWidget@@QEAAXH@Z` (every call, a rare
   event) and `?RemoveFromParent@UWidget@@UEAAXXZ` (logged only for a widget in the table of ones added to the
   viewport, 32 rows), so the death screens, pause menu and downed HUD show by class with no class list.
5. Weapons and abilities. Client input: the existing FEnhancedInputActionEventBinding Execute detours
   (input_diag.cpp:711-724, gate g_gate_diag, verified) get no gate; a line for actions named IA_SwitchWeapon_*,
   IA_ADS and the grenade action, once per press (an Execute after 250 ms with none). Server copy weapon: card 057's
   EquipWeapon / CycleEquip detours (weapons.cpp, verified, capped 160) add one line after each change: current
   weapon (g_get_equipped_weapon) and its fire ability class. ADS: the IA_ADS line, then at +500 ms the camera FOV
   from PlayerCameraManager CameraCachePrivate POV (reflected offset, as remote_body.cpp:953; FOV at POV+0x30, open:
   confirm 0x30) and which ability ran (the activation lines below). Activations: InternalTryActivateAbility on a
   player pawn or weapon ASC, both sides, one line per ability class per 2 s (Fire stays one line per 2 s). Grenade:
   client, at the local activation of a class with "Grenade" in its name: control rotation (kControllerControlRotation
   0x320, verified) and camera location. Host: detour_c74_spawned (grenades.cpp:832, verified) for a grenade:
   location, the transform's P/Y and the projectile velocity (c74_velocity, verified).
6. Doors. Host: c95_host_call already logs open/close with name (verified, cap kC95Lines 200 per map). Client: the
   relay line "DoorOp ... applied" is the host-row cause (verified, c95_receive); detour_c95_open/close calls outside
   c95_receive are the local-sim cause, logged with position (c95_note, verified). Both go through the rate limiter
   instead of the per-map cap. Blocked: hook
   `?HandleImpact@UCharacterMovementComponent@@MEAAXAEBUFHitResult@@MAEBU?$TVector@N@Math@UE@@@Z` on the client's own
   CMC; when the hit actor is an ASTDoor (g_c95_door_class) with DoorState 1 (open): "blocked by door shown open".
   Open: the FHitResult actor field offset (HitObjectHandle) is read in the run against a known door.
7. Remote-player audio. Hooks `?PlayInternal@UAudioComponent@@...` and
   `?PlaySoundAtLocation@UGameplayStatics@@...` (symbols verified in the PDB). Filter: the component owner (or the
   OwningActor arg) is the other player's pawn, or a weapon whose Owner is that pawn; one class test, else nothing.
   Line: sound name, location, 2D/3D (bAllowSpatialization and attenuation), attenuation set, distance to the
   listener (own camera location). Once per sound per 2 s (keyed table).
8. Summary every 300 s and at session end (the DLL detach path / map-end), both sides: own and copy shots (item 3),
   hits (item 2 host, card 132 hit rows client), downs and deaths (card 043 counters), loads (card 137), joins (first
   Pong), rejoins (card 045), digest mismatches (g_c132x.mismatches summed per round), suppressed lines per class.

## Predictions (run 1, scripted pair, FAST=1)

- Both logs: a "clock" line within 2 s of the first Pong and every 60 s; host and client lines carry the same n,
  host t and client t; rtt under 50 ms on loopback; offset equals the two logs' t difference within rtt.
- Host: one "player hit" line per hit on the client's copy and on the host pawn, armour ran 1 while Armour > 0 and
  health ran 1 after it; no line for any NPC hit.
- Client rifle bursts: K 1, host copy shot within 200 ms; underslung bursts: client K 5-9, host K 1 (finding 16 not
  fixed by this card): match_shots.py prints them as pellet mismatches.
- Client: AddMappingContext, SetInputMode, cursor and AddToViewport lines at the pause menu and the downed screen,
  RemoveFromParent when each closes.
- Client IA_SwitchWeapon_Underslung and IA_ADS lines; host weapon line after the client's slot switch (rifle slot
  and back); no host weapon change for the underslung (finding 16).
- Client grenade aim line, host throw line with velocity; aim differs by the finding 13 amount, not checked here.
- Door: host row line and client "applied" line for the door the walk passes.
- Remote audio lines on the client for the host's rifle; on the host for the client copy's rifle.
- Two summaries in a run over 300 s, plus the session-end one; log growth under 20 MB/h projected.

## Report (junior, 2026-11-08)

Runs: c155a-c (earlier, see the transcript notes below), c155d (run 4, the last granted). DLL of c155d:
`6efda018934ab934`. DLL with the two fixes below, built but not run: `40e5082ba8d738ce`.

### c155d (run 4)

- 126 s, ended by the scenario: drive rc 1, FAIL at line 28 (the client's weapon wait). Host rc 0, client rc 0.
  FATAL/ERROR 0 both. No CRASH, no new dumps. Host WARN 2 (baseline: DisableThreadLibraryCalls, GOD off). Client
  WARN 13: the same 2 plus 11 card132 pickup mismatches (BP_AmmoPickup_MoritaSmall_C, BP_WeaponPickup_MoritaMk1_C).
  Those predate this card and belong to card 145's sweep.
- Cause of the FAIL (scenario timing, not the mod): the Morita rack re-enables its pickup about 10 s after a take
  (host take at 264522280, client collision 0 -> 1 at 264532274). In c155c the client walked 28 s and arrived after the
  re-enable. In c155d it was already beside the rack after taking the frag, and pressed E at 264524813, while the
  pickup was disabled. The scenario now waits for the client's `card095 ... InteractionCollider ... 0 -> 1` line before
  the client's E.
- Shown in c155d: the clock and rtt lines on both sides (clock 1: offset 32 ms, rtt 84 ms; clock 2 at 60 s), the
  cursor line on both sides, widget add and remove on the client, weapon slot lines on both sides, ability slot lines
  on the host, and the map-end summary with the new "input actions N tabled, M untabled executes" field.
- Not shown, because the run stopped first: the host hit line with Armour and Health, down and revive, ADS, grenade
  aim, remote audio, and the 300 s summary.

### The press and ADS lines: a second cause, found statically after c155d

- The host and the client both pressed E and Two in c155d (card043 logs "own interact press (Interact)"), but no
  `press` line came out. The action table was not the cause.
- `UEnhancedPlayerInput::ConvertInternalTriggerEvent` maps StartedAndTriggered to `ETriggerEvent::Triggered`. A
  one-frame tap (None to Triggered) therefore reaches the bindings with the instance's TriggerEvent (+0x12) equal to
  Triggered (1), never Started (2). `pl_execute` logged only Started, so it missed every tap and the ADS rising edge.
- Fix (`playlog.cpp`, `pl_execute`): a press is Started, or Triggered more than 100 ms after that action's last
  execute (`PlAction::last_ms`). A held action executes every frame, and several bindings of one action execute in the
  same frame, so neither repeats the line. ADS uses the same edge for 1, and Completed or Canceled for 0. `pl_ads_set`
  already drops repeats. Cost: one `GetTickCount64` per execute of a tabled non-quiet action. Quiet actions still
  return after the pointer compare.
- Limits: a second tap within 100 ms is not logged. An action bound only to Completed is never logged as a press.

### Host hit line (finding 19 data)

- `c40_player_hit` now prints `Armour %.1f -> %.1f, Health %.1f -> %.1f`. Armour is read through reflection of
  `UADAttributeSetHealth::Armour` on the set (`kC40ParamsSet`), resolved once, plus `kAttrDataCurrent`. There is one
  line on resolve (`card155: Armour on <class>: at +0x..`, WARN if absent). This is unrun, because c155d had no hit.

### Log size (c155c, play config: no UBWCOOP_C136_LOG)

- Whole file: host 677,199 B / 4391 lines, client 615,625 B / 4119 lines, about 197 s.
- In session: host 341 KB in 165 s = 7.42 MB/h, client 298 KB in 167 s = 6.42 MB/h. The whole files project to 12.4
  and 11.3 MB/h, because the startup lines are counted against a short run. Both are under the 20 MB/h target and the
  30 MB/h limit.
- The playlog lines themselves: host 9791 B, client 5388 B. The top producers are card132, card098, netbudget and
  card049.
- c155d ran with `UBWCOOP_C136_LOG=1` (the per-row fx lines, a test gate), so its size (host 1,113,951 B) is not a
  play-build figure.

### No-match cost of each hot hook

- `HandleImpact` (own CMC): 1 pointer compare against own_cmc.
- `PlayInternal` / `PlaySoundAtLocation`: 2 reads and at most 8 pointer compares (`pl_remote`, kC17Slots = 4, owner
  and owner's Owner).
- EI Execute (3 detours): 1 read and a scan of the action table (at most 48 entries, about 25 used). An untabled UI
  action reads its name again at each execute (event rate, not per frame).
- Shot, ability, widget, IMC and cursor hooks run at event rate. The cursor and clock lines are polled.

### Open after run 4 (no run 5 on this card)

- Unproven: the host hit line with Armour and Health, the down and revive lines with the cursor and IMC lines around
  them, the press and ADS lines (fix built, unrun), the grenade aim lines on both sides, the 300 s summary, and
  `tools/match_shots.py` on a client burst.
- Client audio for the host's rifle: unproven. The host never fired in c155d, so there are no MS_WPN rows to check.
  The card 136 "155 follow-up" condition (no host row names MS_WPN) was not tested, so no section is written there yet.
- The fixed scenario (`tools/scenarios/c155-playlog.txt`) is ready to run with DLL `40e5082ba8d738ce`. The senior
  decides whether these proofs ride in card 141's run, which needs the same client bursts and `match_shots.py`, or
  are carded.

Report complete.
