# Card 130: Playtest 2026-11-07: settings lag with a client in, client missing TacSupport popups and training prompts, client sounds on the host

Status: partly accepted 2026-11-07, play DLL `0a98be3c89324a0d` (cinematic skip, items 8, 10, 15, 16, 17). Open: items 1-7, 9, 11, 13, 14; item 12 reopened in card 131 (the client kill logs `IsDead now 0`); the 5/9 relay, fix L.

**The user is playing on this machine.** While any game you did not start is running (check `pgrep -fa
Win64-Shipping`; the user's game uses port 47000 and log tag `menu`), do static work only: no game runs, and do not
copy anything over `game/StarshipTroopers_UBW/Binaries/Win64/dwmapi.dll`.

Read: card 096 Report (pause skipped while a client is connected), card 098 Report (world sync, one mechanism per class
of state), card 107 (runtime spawns shown on the client), card 049 and 070 (objective relay). Memory rules apply:
card-run-discipline, targeted-tests, full-world-sync, no-workarounds.

Log: `logs/playtest-host-20261107-1416-live.log`. This is a copy of the user's live host log, taken while the game was
still running. The friend's client ran on another machine, so there is no client log; ask me if you need one.

## What the user saw (host side, 2026-11-07 about 14:00-14:15)

1. **Settings menu is slow to change while a client is connected.** With a client in, card 096 skips the pause:
   `SetGamePaused(1) skipped: the world keeps running, 1 client(s) connected` at 153384722. The menu then runs on top of
   a live world. "Slow to change" means the menu reacts slowly to input: tabs, option changes and navigation.
   - Added by the user, 2026-11-07 evening: opening the settings menu is also slow, about 1 to 3 s from the key press to
     the menu showing. The user does not know whether it happens solo too.
   - So the measurement covers opening as well as changing, in three cases: solo, with a client in, and the same
     build with the mod's hooks idle (or the game without the mod, if the driver can open the menu without it). The
     log has to show the frame gap or the per-call cost that makes up the 1 to 3 s, and name the call. Candidates
     include the `UpdateBindings` guard (caller exe+0x51C6EA2), `GetAndApplyGameUserSettings`, display mode
     enumeration under Proton, and our ProcessEvent-level hooks.
2. **Tactical Support training course (FED_C1M1): the client cannot see the arachnid popup targets.** The host shows
   them. The step is `TacSupport_SO3`, and its marked actor is `BP_EncounterManager_C_1` (card 070 line at 153381276).
   It was shown at 153501776 and completed on the host at 153566002 (card 097, caller exe+0x5233805). The course's
   placed range targets are `BP_KillableBuilding_Base_TacSuppRangeTarget_C` (replicates 0, card 098 collision lines at
   153378645).
3. **Client sounds play on the host.** The user hears sounds from the friend's game on the host. The user confirmed
   this is a bug: sounds that belong only to the client's own game come out on the host, not world sounds the
   friend's actions make in the host's world. Find which sounds (cue or component names, from a sound hook) the host
   plays because of the client, and through which path: a multicast or client RPC, a relay of ours (card 074 copies,
   card 024 fire), or the remote pawn's components playing 2D or UI sounds. Keep world sounds at the friend's position
   (shots, footsteps). Drop sounds that belong only to the client's player: UI, its own voice lines, music, 2D.
4. **Training prompts are missing on the client.** The FED_C1M1 tutorial prompts (the on-screen training hints) show
   on the host but not on the client. Find what shows a prompt (the scripting actor, a trigger overlap, an objective
   callout) and why the client's copy does not run it. The client shows each prompt the host shows, for the steps
   both players do. Prompts that belong to the player who triggered them stay with that player. Say which applies and
   why. This is the same class as the popups (script-driven, host-only), so check whether one fix covers both.

5. **Tutorial airstrike explosions are missing on the client; only the sound plays.** This is on the TacSupport
   course. The classes are `BP_TacSupp_BombingRunProjectile_Tutorial_C` and `BP_TacSupp_Bomber_Tutorial_C`. The host
   relays them as card 098 'P' rows: `world P` lines from 153620599, destroys from 153620531, and the bomber destroy
   at 153629682. Card 119 made the normal airstrike bombs show on the client, so the tutorial classes are probably a
   sibling that card 119's path does not cover: a class match, or an explosion effect spawned on impact that is not
   relayed. Find out which, and cover the class at the card 119 layer, not with a tutorial-only relay. In the run, the
   client log shows each bomb and each explosion effect the host spawns.

6. **CaptureTheFlag course door looks shut on the client, but the client can walk through it.** The host opened
   the door, and the client's collision follows (card 095), but its mesh or animation does not. Mirror the door's
   visible state (the timeline, its relative location or rotation, or its mesh visibility) through the card 098
   mechanism for that class. Sweep every door and gate class on FED_C1M1 for the same split between collision and
   visuals. The objective lines are CaptureTheFlag_SO1 done at about 153762691, and SO2 done and SO3 added at 153833659.
7. **Every NPC on the client runs in place.** The NPC troopers play their run animation but do not move on the
   client, or they move only on the host. Check whether their positions replicate (movement component replication and
   the net update rate), whether the client's copy plays the host's velocity-driven animation with no location
   updates, or whether a mod path stops the client's copy from moving (card 034 world z, the card 019 input mirror,
   the card 103 or 108 AI changes). This touches every NPC, so it takes priority: do it first in this card. In the
   run, the client log shows one NPC's location changing over 5 s, matching the host's within one update.

8. **On the CTF course the client cannot take a laser rifle.** The host spawns `BP_WeaponPickup_LaserMoritaRed_C`
   runtime pickups (card 107 lines from 153918397), and `BP_WeaponPickup_LaserMorita_C` is destroyed at 153935191.
   The client's pickup gets no weapon. Card 105/107 pickups work for the client, so find where this one breaks: the
   client's copy is a card 107 visual with no interaction, the pickup's give-weapon path does not take the client's
   pawn (card 102 acting-player scope, card 021 GiveWeaponByClass), or the CTF script gates it to player 0. Sweep
   every weapon pickup class on FED_C1M1 (`BP_WeaponPickup_*`, including TacSupport_SO2's
   `BP_WeaponPickup_MoritaMk1_C_1`). In the run, the client takes a laser rifle and the host log shows the weapon given
   to the client's pawn.

9. **The client does not see the host's grenade explosions.** Card 074 sends the client a GrenadeSpawn copy of the
   host's grenade. Check whether the explosion effect (the impact Niagara or particle system, decal or camera shake)
   follows the copy. It may not if the copy is destroyed or detonated without its explode path, or if the host's
   explosion effect is spawned on its own and not relayed. Same class as item 5: one fix should cover all
   explosion effects (grenades, tutorial airstrike and normal airstrike, card 119). In the run, the host throws a
   grenade and the client log shows its explosion effect at the host's spot.
   Update from the user: the client does not see its own grenade explosions either. So the client sees no grenade
   explosion visuals at all, whoever threw the grenade. Look first at the client's explosion effect spawn (the Niagara
   system on the client, the card 074 GrenadeExplode apply, a client-side grenade suppression from cards 020 and 104),
   not at the host relay. In the run, the client throws a grenade too, and its log shows that explosion effect.

10. **A client that was downed cannot move properly after a checkpoint restart.** The client is downed (card 109),
    the host dies, and the host restarts from the death screen: card 044 lines at 154208438 and 154229594, card 049
    checkpoint snapshot at 154230592. After the restart the client cannot move properly. The cause is probably downed
    state that the restart does not clear on one side or both: the down tags, the movement mode, MaxWalkSpeed, the
    card 109 crawl, or the card 019 input mirror. The host respawns its own pawn, but the client's pawn or server copy
    keeps the downed values. Find what the restart resets for the host's pawn and make sure it does the same for the
    client's. In the run, the client is downed, the host restarts the checkpoint, and the client then walks a goto
    with its speed and movement mode logged equal on both sides.

11. **No prompt to revive a downed player.** Card 043 made revive work by holding interact near the downed player:
    `ChanOp::Use` from the client, and the host's own interact. But no on-screen prompt tells the other player that
    they can revive. The player has to know to hold interact. Show the game's own interact prompt (the widget and
    text the game uses for interactables, for example the one card 123's turret mount shows) to a standing player
    within revive range of a downed one, on both the host and the client, with a hold-progress indicator if the
    game's prompt has one. Use the game's prompt path, not a new widget, unless step 1 shows there is none; card 110's
    "DOWN <n>s" label stays. In the run, the log shows the prompt shown and hidden on each side as the standing player
    enters and leaves range, and a revive held through it.

12. **Bug holes die on the host but stay alive on the client.** This is on the second map (LOS objectives). The bug
    holes there are `BP_DestructibleSpawner_Bugnest_HumanGameplay_C` (objectives LOS_O02_SO05a `_7` and SO05b `_0`,
    card 070 lines at 154410337). Card 098 relays only the satchel-building death
    (`ASTSatchelBuildingBase::ReceiveOnSatchelExploded`). The host log has no card 098 death line for any Bugnest
    spawner, so this class is not covered. Find how a Bugnest spawner dies on the host (damage to 0 health, a destroyed
    event, a destructible break, the spawner stopping), then relay that death to the client through card 098's
    WorldOp journal, keyed by name and location as for satchel buildings, so late joiners get it too. Sweep every
    destructible or spawner class with an objective in the shipped maps: what kills each one, and whether card 098
    covers it. In the run, the host kills one Bugnest spawner, and the client log shows that same spawner's death
    and its spawning stopping.

13. **The host sees no checkpoint or objective markers (FED_C2M1).** The host player reports no objective or
    checkpoint markers on screen. The script does set them: card 049 lines at 154590710 show
    `SetObjectiveVisibility(CrashedDropship_Marker | Plasma_Marker | FlakBug_Marker) bools 01` and
    `LOS_O03_SO01/SO02 bools 11`, ctx FED_C2M1_Scripting_C. Find out why the host's own HUD does not draw them. Check
    whether a mod hook changes the host's markers: card 049, 070, 097 or 100 objective hooks, card 110's overlay on
    the same z, or card 099 "the player" remapping that points the marker HUD at the client's copy. Then check whether
    it began after a checkpoint restart (item 10) or a map change with a client in. Compare a solo host on the same map
    (markers shown?) with a co-op host. In the run, the host's log shows the active objective's marker actor visible
    and on screen with a client connected.

14. **Every AI trooper picks up downed characters; medics heal.** User rule: everyone can pick up a downed
    character, AI troopers included, and medics only add the heal. In the game today only medic troopers revive.
    Card 043 builds the player downed state in mod code (a mod hold timer, the pawn is not an interactable), so no
    AI targets a downed player. Find the medic's revive path: the behavior tree or task, the target query, the revive
    helper, and which part is the pick-up and which is the heal. Then make the pick-up available to every host-owned
    AI trooper, with downed NPC troopers and card 043 Downed or Out players (host or client) as targets. A pick-up of a
    player completes through the card 043 revive (same restore path, logged). Medics keep their heal on top. The host
    owns the AI, so the client sees the result through card 043's broadcast and the NPC sync. In the run, a non-medic
    AI trooper picks up a downed client and a downed host. The host log shows the trooper class, the target and the
    card 043 revive line.

15. **Client log findings (friend's log, `logs/playtest-client-20261107-143531.log`, FED_C1M1 only).** The client's
    clock runs on its own base (71653530 at attach); the log ends at the detach at 73223228. LOS (FED_C2M1) is not in
    it.
    - **Item 9, grenades.** All 14 client explosions log the system `-> spawned` and the sound `-> played` at the
      host's location: 0.2 to 25 uu off, once 991 uu (GrenadeExplode 5 at 72525519). The friend heard them and saw
      nothing. So the spawn call returns a component that never draws. Check the spawn parameters (scale, the
      zero fields `0.0 0.0 0.0 0.0 0.00` in the line, auto-activate, the pooling method), whether the system needs
      user parameters the game's own explosion sets, and whether the component is destroyed or culled on its first
      tick. Two explosions logged `system - -> none, sound - -> none` (72285203, 72962579): an ADProjectile destroy
      with no effect named.
    - **Dead set full (new).** `WARN card038: client dead set full (128) -- further deaths get no visual` at 72655669
      and 73005246. After 128 deaths in a map, the client shows no more NPC deaths: those bugs and troopers stay up
      on the client while dead on the host. That breaks full world sync and may be part of item 7 (NPCs running in
      place) and item 12. Size it so it cannot fill in a map, or free entries when the actor is destroyed. Sweep the
      other fixed-size client sets for the same limit, and log their peaks.
    - **Client follow gave up (new).** `WARN card045: client follow gave up after 4 tries, 63079 ms after the local map
      loaded` at 73160140. Tries 3 and 4 Browse returned 2 (Pending) and never connected; the client then went to the
      frontend with option "closed". The friend did not follow the host on that travel, so it had to rejoin by hand.
      Find why the pending connection never completed (the host's side at the same time: a map load still in
      progress, the listen URL, the card 113 grace), and make the follow wait for the host's listen world.
    - **Gore without data.** 83 `WARN card039: client gore component has no gore data -- no Niagara`. Say which
      classes; if the host shows gore for them, the client must too.
    - **Grenade items.** 4 `WARN card073: ... GrenadeItem ... no item inventory on our pawn` (Frag and Incendiary).
      Say what was lost (the client's grenade count?) and fix it.

16. **One log per launch.** The log is named `UBWCoop-<tag>.log` (`mod/src/ue/config.h`, `mod/src/ue/log.cpp`), so
    each launch overwrites the last. The friend's LOS log is lost that way. Add the launch time to the name
    (`UBWCoop-<tag>-YYYYMMDD-HHMMSS.log`, local time). Check every reader of the old name: tools/scenarios, the drive
    script, make-package, docs/PLAYTEST.md. Either update them or have them pick the newest file. Do not delete old logs.
    The per-launch name must still fit `kMaxNameLen`.

17. **Boot waits 14 s for GOG Galaxy sign-in (senior, static, 2026-11-07).** The game's
    `Engine/Binaries/ThirdParty/Steamworks/Steamv157/Win64/steam_api64.dll` is the "GOG Galaxy Steam Wrapper"
    (version info). `SteamAPI_Init` starts Galaxy and waits for sign-in. No Galaxy client runs in the prefix, so it
    waits out `auth_timeout`. That option is read from `./GalaxyConfig.json`
    (`Binaries/Win64/GalaxyConfig.json`, which has only `client_id` and `client_code`). Its default, 0xf, is set at
    steam_api64.dll 0x1800a5ab6, just before the `"auth_timeout"` key lookup at 0x1800a5aeb.
    - A sampled boot (senior, single instance, menu) showed 4.8 s to 18.8 s at 0% CPU, 0 disk and 29 threads. The
      host and client logs agree: `movieskip: no movie player yet` at t+1.0 s, then the player appears at t+18 s.
    - Verified: the default and the idle window. Open: the unit (seconds fits the 14 s) and what 0 means (it could
      mean no wait, or no limit). Read the code that uses the parsed value before choosing 0 or 1.
    - User decision: fix it mod-side, with no game file edit, so the friend gets the fix in the package.
    - The wrapper imports `CreateFileW` from KERNEL32 itself. Hook that import in steam_api64.dll only, when the
      module loads (after attach; it loads during PreInit at about t+4 s). An open of a path ending in
      `GalaxyConfig.json` gets a mod-written copy: the real file's JSON with `auth_timeout` added when it is absent.
      Two instances share one prefix, so write the copy without the card 114 sharing-violation failure. Same content
      from both is fine. Log the redirect, once.
    - Proof is in the soak run, with no extra run. On both sides, the `movieskip: player ... flags` line comes at
      about t+5 s instead of t+18 s. Nothing that used the Galaxy sign-in changes: the card 024 player name lines and
      the session join are the same as before. Report the old and new boot times.

## Do

1. **Measure (static first, then one run).**
   - Settings lag: what the menu waits on while the world runs. Candidates: the menu's widget animations or input on
     game time, card hooks that do per-frame work while the menu is up, or frame time itself. Get the host frame time
     with and without the menu open, and with and without a client. Name the cause with a logged number.
   - Popups: what the "arachnid popups" are. They could be actors the encounter manager spawns, an animation or
     timeline on placed targets, or a visibility or collision toggle. Find out what the host does to show one, and why
     the client's copy does not follow: not spawned, not replicated, or a relay that skips the class.
2. **Fix, at the layer that owns it.**
   - Settings: the menu responds at normal speed with a client in, and the world keeps running (card 096 stays).
   - Popups: the client sees the same popups at the same time as the host, through the existing mechanism for that
     class of state (card 098 or card 107). No per-actor relay. Sweep the other FED_C1M1 training courses (shooting
     range, grenade range) for the same class.
   - Prompts: the same mechanism as the popups if step 1 shows they are the same class. Otherwise use the layer that
     owns them.
   - Sounds: the host stops playing the client-only sounds at the layer that plays them. World sounds stay.
3. **Proof, one run, FED_C1M1, FAST=1.** Use the card 124 driver.
   - Both players at the TacSupport course: the client log shows each popup the host shows, at the same spot, and
     its hide or death, and each training prompt the host shows.
   - The client fires and moves near the host: the host's sound log shows the client's world sounds at the client's
     position, and none of the client-only sounds from item 3.
   - The host opens the settings menu with the client in. Log the menu input response or frame time, before and after
     the fix.
   - rc 0, ERROR/FATAL 0, no CRASH line, no new dump.

## Done when

The Report has the cause of each item with its log line, the fix and where it sits, the run lines, the run length,
rc, ERROR/FATAL, CRASH lines, dumps and the DLL hash (`sha256sum | cut -c1-16`), with the play DLL saved as
`build/dwmapi-play-<hash>.dll`. Ends with "Report complete."

## Report

### Static findings (2026-11-07, the user's game running: no runs, no deploy)

All lines are from `logs/playtest-host-20261107-1416-live.log` (host only; there is no client log). Items are in the
card's priority order.

**7. NPCs run in place: cause not found statically; needs the run (or the friend's client log).**
- The troopers replicate: `SetActorEnableCollision(... BP_AI_Trooper_Morita_C, LocalRole 3 RemoteRole 1, 1)` and the
  card 098 tally `BP_AI_Trooper_NonCombat_C spawn n=1 placed=0 replicated=1`. So the client's copies are simulated
  proxies, and the engine's replicated movement should move them. No mod relay owns their position.
- Mod paths checked and cleared:
  - The card 128 `OnSignificanceChanged` detours (`door_c95.cpp:508`) only count depth and call the original, with
    the right `void(this)` signature.
  - The card 034 refit touches only the c28 remote player renderers (`remote_body.cpp`), not NPCs.
  - Card 103/108 change only target selection on the host.
- Left to measure, in the order I would check them:
  - Whether the host's own significance update throttles the troopers that are far from the host's view. The client
    stands near them, so the host would send few movement updates. This is the host side of card 128's "view state,
    not world state".
  - The trooper's replicate-movement bit and NetUpdateFrequency on the host (`enemies.cpp:35` already reads the rep
    flags byte; the same dump applies).
  - On the client, location and velocity per second for one trooper.
- The run needs a client log line every second for the nearest trooper: location, velocity, mesh relative location,
  and the host's location for the same actor at the same tick.

**8. The client cannot take a laser rifle: cause found.** It is the card 104 relay table, not the pickup's give path.
- FED_C1M1 spawns 81 runtime pickups (`card107: ... runtime pickup` lines), mostly the CTF laser troopers' drops,
  which lie on the ground until taken.
- `kC104Copies = 64` (`world_c98.cpp:148`) bounds both the host's relayed list and the client's copies. Once 64 relayed
  actors are alive, every new one fails:
  `WARN card104: host t=153898943 BP_WeaponPickup_LaserMoritaRed_C (...) not relayed: no class path or table full`
  (7 such lines from 153898943 on). The class path is valid: the same class went out as `world P` rows earlier.
- So the later laser rifles exist only on the host. The client has nothing to take.
- The world relay line cap hides the count: 61 `world P` and 39 `world D` lines add up to exactly
  `kC98WorldLines = 100`, so the relay logged nothing more. The journal (`kC98WorldRows = 128`) can fill the same way
  for a late joiner.
- Fix:
  - Size the tables for the map's worst case: 81 pickups plus 18 bombs, 5 danger zones and the flares and pods seen
    here. Use 256 for `kC104Copies` and 256 for `kC98WorldRows`.
  - Log the peak alive count at map change.
  - Make the not-relayed WARN say which limit was hit; it currently says "no class path or table full" for both.
- Sweep: the weapon pickup classes seen on FED_C1M1 are Autocannon, the base class, EPulse44/88, FlameThrower,
  HighPower, LaserMorita, LaserMoritaRed, MoritaCarbine, MoritaMk1, MoritaMk1Scoped, MoritaMk2, PumpShotgun, Railgun
  and VeteranRifle. Only LaserMorita, LaserMoritaRed and MoritaMk1 spawn at runtime; the rest are placed (card 050).
  The run must still show a relayed laser rifle being taken: the host gives the weapon to the client's pawn.

**2 and 6. TacSupport popups and the CTF door: the same class of state, which nothing relays.**
- Popups: when TacSupport_SO3 shows, the host plays a timeline on each placed target:
  `card098: host timeline BP_KillableBuilding_Base_TacSuppRangeTarget_C ... at (12065, 15300, -532) placed 1
  replicates 0 detail Timeline caller exe+0x1424549` (153501776). Card 098 logs timeline starts (`world_c98.cpp:124`)
  but has no WorldOp kind for them. The client's placed copies never play, so the targets stay down.
- Sweep, same class: `TargetRangeTarget_C` at (6010, 14850) and (4580, 14850) (`MovementTimeline`),
  `GrenadeRangeTarget_C` at (5341, 24015) (`MovementTimeline`), and the bomber's `Move Along Spline Timeline` (item 5).
- Door: `BP_Door_Sliding_Large_C_0` at (-3674, 9849, 298) is placed with replicates 0. The host's
  `PawnBuffer 3 -> 0` at 153699649 is relayed (card 095), so the client can walk through. The door is not an ASTDoor:
  no OpenDoor, CloseDoor or SetDoorActive line in the whole log, so the card 095 door command never fires for it. No
  timeline line either: its slide is not a `UTimelineComponent::Play`. The visual mover needs one hooked run, or a
  look at the Blueprint's other latent movers (MoveComponentTo, SetRelativeLocation from a Tick).
- Fix design for the popups: a card 098 timeline kind, 'L'. The host sends the placed name, location, timeline
  component name and the call (Play or PlayFromStart). It is journalled like 'S' and replayed on the first Pong. The
  client finds the placed actor (`c50_find_placed`), finds the component by name, and calls the same function. Only
  placed actors with replicates 0; replicated ones (the dropship FadeInTimeline) are left alone. The door goes onto
  the same kind if the run shows a timeline, or onto a sibling transform kind if not.

**5. Tutorial airstrike: the bombs are relayed but show no explosion on the client.**
- The host relays the bombs (18 `world P` rows) and their destroys, for example
  `card098: host destroy BP_TacSupp_BombingRunProjectile_Tutorial_C ... at (13601, 22905, 38) ... caller exe+0x37DC6BD`
  and `world D ... 130389` at 153620531. The class path fits, which card 119 fixed.
- Two gaps:
  1. The client copy is spawned with zero rotation (`c104_client_spawn` passes `rot{}`) and none of the bomber's
     velocity. It falls slower than the host's bomb, and the host's 'D' arrives while it is still in the air. Card
     104's destroy then removes it, so its own impact, and the explosion with it, never runs.
  2. The bomber copy never starts its `Move Along Spline Timeline` (item 2's missing timeline kind), so it stays where
     it spawned.
- Fix, at the card 104 layer for every relayed projectile:
  - The 'D' row carries the host's last location.
  - On the client, if the copy is still live, it moves to that point before it is destroyed, so its Blueprint's
    hit or explode path runs where the host's did.
  - Alternatively, the host relays the explosion it spawns there. The run's client log will show which one the
    Blueprint needs.
- The bomber is covered by the timeline kind.

**4. Training prompts.** `WB_TutorialPrompt_C` is the widget that card 052 and card 110 already build as our own
overlay (`card110: host overlay WB_TutorialPrompt_C built for pc ...`, 153378633). The game's own prompt calls are not
in this log. The class has to be found in the run with a widget-add hook before I can say whether it is the timeline
class or an objective callout (card 049).

**3. Client sounds on the host.** This log has no sound data: `UBWCOOP_C99_LOG` was off. The card 099 hooks are in
place (`player_c99.cpp:212-250`).
- Candidate paths, from the card 099 remap lines:
  - `GA_HumanTaunt_C` on the client's server copy (153810794): its voice line.
  - `BP_WeaponReloadAbility_MoritaMK1_C` (153651796).
  - `WB_Fed_Score_Log_C` inside the client's interaction and damage scopes (153471183, 153640562): score UI sounds on
    the host's HUD.
- Fix design: in the card 099 2D sound detours (`PlaySound2D`, `SpawnSound2D`, `ADAudioManager::PlaySound2D`), the
  host skips a 2D sound whose context or owner belongs to a server copy (`c99_copy_of`), or that runs inside a card 102
  acting scope for a client. These hooks become always-on gates; they are no longer log-only. Located and attached
  sounds stay as they are.
- The run turns on `UBWCOOP_C99_LOG=1` to get the cue names first.

**1. Settings lag.**
- The menu opened at 153384721 (`IMC_DummyUI_KBM`), and the pause was skipped at 153384722.
- Host frames during the menu period are about 8 ms apart (card019 input-mirror lines), so frame time alone does not
  explain the lag.
- One logged lead: `guard: UpdateBindings(LocalPlayer=null, settings=...) skipped, caller exe+0x51C6EA2`, twice, at
  153386743. A settings-side call our guard drops.
- To measure in the run: menu input-to-widget time with and without the client, and the per-frame cost of our hooks
  while the menu is up.

**9. No grenade explosion visuals on the client: cause not found statically; needs the run.**
- The host's grenade explosion goes through `USTParticleEffectManager::STSpawnSystemAtLocation` (RVA 0x51DAF10). Its
  disassembly calls `UNiagaraFunctionLibrary::SpawnSystemAtLocation` (0x4840630) with the same arguments, stores the
  component in the manager's array and binds OnSystemFinished. It sets no user parameters. So the client's own call
  in `c74_play_fx` (`grenades.cpp`) is the same spawn as the game's.
- Older client logs (c82h, c88g, c98m3) show `c74_play_fx` "spawned" lines, and the card 074 Report saw the
  explosions on 2026-11-05.
- No client-side Niagara suppression exists in the card 020, 099, 102, 104 or 128 code.
- If `c74_receive_explode` finds no copy of the throw, it logs `WARN ... no copy of that throw in flight` and returns
  before `c74_play_fx`. The friend's client log is the only place that line could be.
- The run logs, on the client: the receive line, the copy lookup, and the component's state right after the spawn and
  about 0.5 s later (active, visible, culled). It also checks whether the copy's `LifeSpanExpired`, which runs after the
  FX, removes it. Gore Niagara on the same client tells a general Niagara loss apart from a grenade-only one.
- Item 5's bombs have no explosion relay at all: card 098 sends only 'P' and 'D' for them. They get their own fix
  (see item 5).

**10. A downed client cannot move after RESTART CHECKPOINT: cause found; fix built.**
- Sequence in the host log:
  - The client's server copy goes Downed at 154202978 (`saved MaxWalkSpeed 700`).
  - It goes Out, and the host goes Dead, at 154205443.
  - The death screen opens at 154208438 and again at 154229594.
  - The respawn in place runs at 154232426: `card045: host ... respawn in place ... per-map reset`, `reset 043
    (entries 2, holds 0, all down 1 ...)`.
- The per-map reset `c43_arm_map` (`hooks_ubw.cpp:1080`) drops every card 043 entry and sends nothing to the client.
- On the client, the entries are keyed by side (`kC43ClientSelf`), not by pawn. A stale own entry in a down state
  therefore holds whatever pawn the client now owns. The new pawn is then forced to walk speed 0 every tick
  (`c43_client_step`), and `InputTag.Walk` is refused on it (`c43_refuse_tag`). The server copy, which is new, walks
  at 700. Prediction and correction then fight, so the client can't move properly.
- The client clears its own entry only if its own card 045 check fires: a new own pawn while one of `all_down`,
  `death_done` or the card 044 timers is set. The host never told the client to stand up, so the client depends on
  that check alone.
- Fix (`death.cpp` `c43_stand_all`, called from `c45_respawn_check` before `c43_arm_map`): before the host's reset, it
  sets every down entry to Up and sends it with one Down frame per PC. If the restart kept the pawn, it also restores
  that pawn's saved MaxWalkSpeed. The client's existing Up path then clears its entry and its walk-0 hold.
- `c43_own_view` now remembers which pawn's camera it lowered (`view_pawn`). An Up that lands on a new pawn no longer
  raises that pawn's camera, which was never lowered.
- Sweep: card 043 is the only `MaxWalkSpeed` writer in the mod (the other `kCmcMaxWalkSpeed` uses are reads). Card 109
  only refuses `InputTag.Walk` while the pawn is down, through the same entries. Card 019 writes no walk speed.
- The run: the client is downed, the host dies, then RESTART CHECKPOINT. Both sides log walk speed, movement mode and
  the trooper's velocity after a goto.

**11. No revive prompt: cause found; fix designed, not built.**
- The game's interact prompt is `WB_Pickup_Prompt` (Gameplay_HUD). Its name map binds `OnCurrentInteractbleChanged`
  and `OnHoveredInteractableChange` on the interactor component. It calls `GetInteractionPrompt` and
  `GetInteractionIcon` on the interactable that `UADInteractorComponent::TraceForInteractable` finds.
- A player pawn is not an interactable. The classes with an `IADInteractableInterface` vtable are
  ASTLimitedInteractionActor, ASTMountableTurret, ASTPlayerMechBase, ASTPlayerTransportDropship, ASTSwitch and
  ASTZiplinePoint. So the trace never finds a downed player, and the prompt never shows. The card 043 revive is the
  mod's own Use edge (`c43_use_edge`), which is why the hold works with no prompt.
- Fix design: a local, unreplicated revive interactable for each downed player, on both sides. It is an
  ASTLimitedInteractionActor (or the lightest interactable class whose prompt text can be set with
  `SetInteractionPrompt`), attached to the downed pawn, with a trace-visible collision box of radius
  `kC43ReviveRange`. Its prompt is set to the game's revive text.
  - The card 081 trace detour returns it for the Up player in range, so the game's own widget shows the prompt.
  - Its `TryInteract` is a no-op; the hold stays on `c43_use_edge`.
  - It is spawned on Downed and destroyed on Up or Dead.
- Still to find statically: the prompt property offset in the chosen class, and the delegate signature for the run's
  log line.

**12. Bug holes die on the host but stay alive on the client: cause found; fix built.**
- The nests are placed, unreplicated actors with a health component. Their death (mesh swap, FX, spawner off,
  minimap icon) is Blueprint logic on the health component's OnDeath. The client's copy takes none of the host's
  damage, so nothing ever kills it.
- Fix (`world_c98.cpp`, the card 098 world journal, one mechanism for the class): a new row kind `'K'`. Host
  `c98_host_kill`, from `detour_health_changed` (`death.cpp`), journals one row when a placed (`c50_placed`),
  unreplicated, listen-server actor's Health crosses to 0. Late joiners get every dead one from the journal replay.
  Client `c98_client_kill` finds the same-named placed copy nearest the row's location, and kills it through
  `InstaKillTarget` so its own Blueprint OnDeath runs. A copy that is already dead (`UADHealthComponent::IsDead`) is
  left alone. A copy with no health component is a WARN and counted as missed.
- Class sweep: every placed killable actor goes through this one row. Satchel targets keep their `'S'` row, and the
  dead check covers both rows reaching one actor. Turrets keep `'T'` (card 125).

**13. No host objective markers (FED_C2M1): fixed statically, run proof pending.**
- Cause (static): `ASTLevelObjectiveMarker::CreateASTLevelObjectiveMarker` spawns `BP_ObjectiveMarkerActor_C`
  deferred (`UWorld::SpawnActor`, then `AActor::FinishSpawning`, then BeginPlay). When a client copy's overlap
  completes the objective step, that BeginPlay runs inside the card 102 acting scope. Its `GetPlayerController(0)` is
  then remapped to the client's PC, so the marker widget is built for the client and the host never sees it.
- Fix: `detour_c99_begin_play` (`player_c99.cpp`) hooks `AActor::DispatchBeginPlay` (exe+0x37DD4D0). Only while
  `g_c102.acting` is set, it suspends the acting scope around the original call and restores it after. Actors
  spawned for a copy (grenades, projectiles) still map through `c99_copy_of` on the Instigator/Owner chain, so they
  are unaffected. The first 16 suspensions are logged: `card130: host BeginPlay of <class> runs outside the acting
  scope of <copy> (entry N, M so far)`.
- Sibling sweep: every BeginPlay under the acting scope goes through this one hook, so any other actor class that a
  client-triggered step spawns gets the same treatment. The card 099 caller rows are now keyed by (rva, entry,
  class), so each class shows as its own row.

**14. Every AI trooper picks up downed players: built, run proof pending.**
- Static findings: the medic heal is `USTAttackActionHeal::PerformHeal` (exe+0x5165C40). It computes the amount
  (HealAmount, or a percent of MaxHealth when HealingType is 1) and calls `UADHealthUtilityLibrary::GiveHealth`. The
  game has no separate pick-up action. An NPC revive is Health going from 0 to above 0 through
  `SetNumericAttributeBase`, after which `HandleHealthValueChangedEvent` (exe+0x5193030) calls `BP_OnRevived`.
  `IsOverkilledComplete` (exe+0x51985A0) is `state >= 5` at +0xAE8.
- Design (`death.cpp`, `c130_pickup_step`, host only, from `c43_host_step`): any living trooper picks up. Every
  500 ms, each downed player (card 043 down entry) and each dead trooper that is not overkilled gets the nearest free
  living trooper within 4000 uu. The trooper is sent with `ASTNonPlayableCharacterBase::STMoveToActor`
  (exe+0x51A46E0), reissued every 1 s while it is farther than `kC43ReviveRange`. Once it has stayed near for the card
  043 hold time, a player goes through `c43_revive` (the same path as a player revive), and a trooper's Health is set
  to `kC43ReviveFraction` of its max. A medic's own heal still runs on top of this through the game's code.
- Table: 16 picks, with the peak logged and a WARN when the table is full. A pick is dropped (with its reason) when the
  target or the trooper is gone, or after 30 s. A refused move is a WARN once. The map totals line counts done, gave
  up, lost and refused moves.
- Open risks: the AI state machine may override the move order, and a client may not see an NPC's revive (check the
  card 038 "alive again" line).

**15. Client log findings (`logs/playtest-client-20261107-143531.log`).**
- **(a) Grenade explosion: spawned and the sound plays, yet nothing shows.** The cause is not found statically.
  `USTParticleEffectManager::STSpawnSystemAtLocation` (exe+0x51DAF10) calls
  `UNiagaraFunctionLibrary::SpawnSystemAtLocation` (exe+0x4840630) with the same arguments. It then only adds the
  component to its array and binds OnSystemFinished, so the client's replay is equivalent apart from the arguments.
  Instrumentation built (`fx_watch.cpp`, new): every effect the mod spawns on the receiving side is checked at
  +100 ms and +1000 ms. Each check logs whether the component is still the same object, plus IsComplete, the
  requested execution state, bIsActive, bVisible, bHiddenInGame, its location and its distance from the own pawn. A
  check that is not shown is a WARN. The host's own explosion is checked the same way, and the host also logs its
  spawn arguments (`card130: host grenade N effect spawn: rotation, scale, auto destroy, auto activate, pool,
  pre-cull`) against the client's (rotation 0, scale 1, 1 1 0 1). The table has 64 rows and logs its peak.
- **(b) The client's dead set was full at 128: fixed.** `kC38Max = kC37Max`. `c38_prune` (`enemies.cpp`) drops each
  scan's gone and revived entries. Only the card 037 scan adds entries, so the set holds at most one scan's corpses.
  It was 128 per map, and every death after the 128th lost its visual.
  Sibling sweep: the card 022/028/033 per-pawn slot tables were reset only at a map load. RESTART CHECKPOINT replaces
  every pawn with no load, so after a few restarts they filled. `pawn_slots_respawn` (`sprint.cpp`, called from
  `death.cpp` at the respawn in place) drops them and logs `card130: <side> respawn in place: pawn slots dropped`.
- **(c) The FED_C2M1 follow gave up after 4 tries: fixed.** The cause was that card 045 re-Browsed every 15 s while
  the try was Pending. Try 2 closed try 1's pending connection (reason 14) because the FED_C2M1 load and handshake
  take longer than 15 s. Now (`restart.cpp`) a Pending try waits for its LoadMap or a failure. The failure hooks reach
  `c45_pending_failed` from `c52_failure` (`lobby_ui.cpp`) and retry after `kC45RetryMs`. A 90 s backstop
  (`kC45PendingMs`) logs a WARN and retries. The follow cap is checked only between tries.
- **(d) WARN classes.**
  - `card039 gore component has no gore data` (83 lines): this is expected, so it is no longer a WARN. All 83 are
    `BP_AI_Trooper_Laser_Blue_C` (32) and `BP_AI_Trooper_Laser_Red_C` (51). Troopers carry a gore component with no
    data asset, and the game's own `USTGoreComponent::OnDeath` returns at once when `+0xD0` is null (exe+0x519C677).
    The first one per map is logged as a plain line with its class, and the map totals line counts the rest
    (`no gore data N`).
  - `card073 no item inventory on our pawn` (4 lines): fixed. These were GrenadeItem frames in the snapshot replay
    right after a respawn in place, before the new pawn had its item inventory, so the count was dropped. That broke
    the full-sync rule. Now `c73_defer` (`grenades.cpp`) keeps the latest wanted count per definition (8 rows, peak
    logged). `c73_pending_step`, from the client tick, applies it once the pawn has an inventory and logs `(deferred
    N ms)`. After `kC73WaitMs` (10 s) the count is dropped with a WARN. A newer direct frame supersedes a deferred
    row, and a map change drops the table, logged.

**16. One log per launch: built.** `log::open_next_to_exe` (`mod/src/ue/log.cpp`) names the file
`UBWCoop-<tag>-YYYYMMDD-HHMMSS.log` (local launch time; `UBWCoop-YYYYMMDD-HHMMSS.log` untagged). It builds the name
from kernel32 only, because it runs under the loader lock, and the name fits `kMaxNameLen` 64. The `.cmd`/`.state`
files keep their names. Every reader of the old name has been updated, and old logs are not deleted:
- `tools/drive.py` (`role_log`, the newest match of an exact 8-and-6-digit glob);
- `tools/run_pair.sh` (`mod_log`);
- `tools/playtest.sh` (copies the log stamped after its launch marker to `logs/playtest-<tag>-<stamp>.log`, or warns);
- `docs/PLAYTEST.md`;
- `docs/STATE.md`;
- `mod/src/ue/config.h`.

### Build (no deploy; the user's game is running)

- `ninja -C build` is clean in a MemoryMax=12G scope. It includes item 8 (tables 256, peak log, limit-specific WARN)
  and item 10.
- Rebuilt with items 12, 15 and 16 (2026-11-07): `[41/41] Linking CXX shared library dwmapi.dll`, no warnings.
  Nothing is deployed and no run is done yet.
