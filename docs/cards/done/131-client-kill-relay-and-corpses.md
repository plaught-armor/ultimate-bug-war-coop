# Card 131: Client kill relay does nothing; client bug corpses stay standing

Status: accepted 2026-11-07 (step 2 in run c133x8, step 1 in run c131k2), play DLL `4cb036fa33c090bc`. Step 1 (the `'K'` kill) and the nest screenshot sent back: no `'K'` row reached the client in c133x8. Step 1 reworked: runs c131k1, c131k2; Report below.

Read: card 130 item 12 Report (`world_c98.cpp` row kind `'K'`, `c98_client_kill`), card 038 Report (client death
edge, `TryPlayDeathAnimation`, the card038e screenshots), card 039 (client capsule set to NoCollision). Memory rules
playtest-card-rules and card-run-discipline apply.

## Why

The user's playtest (play DLL `0a98be3c89324a0d`, FED_C2M1, a remote friend as client) reports two things:

1. "bug nests are still not destroyed for client" (on the host they are).
2. "all dead bugs are still standing for client".

## Verified (senior, from existing logs)

- **Nests and every other `'K'` row: the client kill is a no-op.** Every client `'K'` line in c130d4 and c130d5 reads
  `applied InstaKillTarget, IsDead now 0` (for example `c130d5-client.log`, `BP_KillableCrate_03_C_3`, 164266901).
  `IsDead now` is read straight after the call, and it never becomes 1. The host side works: the user's host log has
  224 `kill relayed` lines, including `BP_DestructibleSpawner_Bugnest_HumanGameplay_C_11 Health 1999.0 -> 0.0`, and
  the world journal replay reached the friend (`15 of 15`).
- `InstaKillTarget` (exe+0x5241580) sums the health component's health and shield (and armor when the bool is set),
  then calls exe+0x5233F60 with that amount. That is the gameplay damage path. On a machine without authority, it
  most likely applies nothing. Exactly where the authority gate sits is open: find it.
- **Client bug corpses:** every client death edge since card 038 logs `TryPlayDeathAnimation; anim state 0xffffffff
  -> 0xffffffff`. The WarriorBug has no BugAnimPhys component, so `TryPlayDeathAnimation` is the only call made.
  `TryPlayDeathAnimation` (exe+0x51B11E0) builds a gameplay tag and calls exe+0x51B0F30 (bool true), which most
  likely plays a tagged montage. The last client-side visual proof that the corpse collapses is card038e
  (`ScreenShot00401`, +8 s, collapsed). Cards 039 and later added the client capsule set to NoCollision,
  EnableCorpseBoxCollider and the Gore OnDeath burst, and no run since has a client-side screenshot of a corpse.
  The cause is not known.

## Do

1. **Kill relay (static first).**
   - Find the authority gate on the client's `InstaKillTarget` path: exe+0x5233F60 and what it calls.
   - Then pick the smallest client-side call that runs the copy's own death, meaning the health component's OnDeath
     and the Blueprint's mesh swap, FX, spawner off and minimap icon. Candidates:
     - set the Health attribute base to 0 locally (`SetNumericAttributeBase`, the route card 130 item 14 found for
       revives), if the health component's value-changed handler then broadcasts death without authority;
     - call the health component's death handler or delegate broadcast directly.
   - State which one you chose and why, with the function addresses.
   - No game logic runs on the client: no score, no objective completion, no spawner state that the host owns. The
     objective state already comes from the host.
   - Keep one mechanism for the whole `'K'` class (nests, crates, and every placed killable actor).
   - `IsDead now` must read 1 after the call; log it as now.
2. **Client bug corpses (static first).** Diagnose why the client's WarriorBug stays standing.
   - Check what exe+0x51B0F30 needs to play the death montage (the anim instance, a montage lookup by tag, a role
     or significance check).
   - Check whether card 039's client capsule NoCollision, or `EnableCorpseBoxCollider`, cancels or hides the fall.
     For example, the movement component may snap the pawn, or a later replicated state may restart the locomotion
     montage.
   - Look in the client logs for any state written after the edge that would undo the montage.
   - Mark each finding as verified or open.
3. **One combined run, after the senior lifts the hold.**
   - FAST=1. Use a map with a Bugnest (FED_C2M1, LOS_O02_SO05a or SO05b) and natural WarriorBugs.
   - The host kills one Bugnest and at least three WarriorBugs within the client's view (warp the client close).
   - Client screenshots, at the edge and +5 s:
     - the nest destroyed (the same look as the host's);
     - each bug collapsed on the ground.
   - Take the host's view at the same times.

## Done when

- The Report has the gate addresses and the chosen client call.
- Client `'K'` lines read `IsDead now 1`, and the client screenshots show the nest destroyed and the bugs collapsed.
  Receiving-side visual proof is required; log lines alone do not pass.
- The Report also has the WARN diff against c130d5, the table peaks, the run length, rc, ERROR/FATAL, CRASH lines,
  dumps and the DLL hash (`sha256sum | cut -c1-16`), with the play DLL saved as `build/dwmapi-play-<hash>.dll`.
- It ends with "Report complete."

## Report (step 1 rework, runs c131k1 and c131k2)

### Static: the authority gate (step 1)

- Client chain: `InstaKillTarget` exe+0x5241580 -> `DealDamage` exe+0x5233F60 -> `ApplyDamage` exe+0x522B460 /
  `ApplyDamageToActor` exe+0x522DAE0 (`MakeDamageEffectSpec` exe+0x4A3D310, then the ASC virtuals at vtable +0x6B8 /
  +0x6D8) -> `UAbilitySystemComponent::ApplyGameplayEffectSpecToSelf` exe+0x494D1E0.
- The gate: exe+0x494D259, `call *0x690(vtable)` = `IsOwnerActorAuthoritative`. On the client it returns false, and
  with no valid local prediction key the function returns an invalid handle (-1) without applying the effect. So the
  damage is a no-op on the client and `IsDead` stays 0, which is the playtest's "IsDead now 0" (24 nests).
- Chosen client call: `UAbilitySystemComponent::SetNumericAttributeBase` exe+0x4972E40, Health base set to 0.
  - It has no role check. It fires `HealthAttributeChanged` exe+0x4A3CD20, which calls `RefreshState` exe+0x4A3E480.
  - `RefreshState` sets `IsDead` (+0x148, Health <= 0) and +0x149, and adds the Dead tag through
    `SetGameplayTagCount` exe+0x4A18A40. Neither function checks the role.
  - Then the mod broadcasts the health component's `OnDeath` (+0xF8) through
    `FHealthCompDamageEventDelegate_Broadcast` exe+0x4A3B490, with an `FADDamageEvent` (0x1E0 bytes; Receiver +0x10,
    ReceiverHealthComponent +0x18). This is the same broadcast the host's `FatalDamageEffectReceived` exe+0x4A3B8F0
    makes, so the Blueprint's death handling (mesh swap, FX, spawner off, minimap icon) runs on the client copy.
  - Why this call: it is the same route card 130 item 14 uses for revives, it runs the copy's own death without the
    host-only damage effect, and no game logic runs (no score, objective or spawner state). Getter `IsDead` exe+0x37E7590
    reads +0x148.
  - One mechanism for the whole `'K'` class: `c98_client_kill` (world_c98.cpp) for every placed killable actor.

### Runs

- Both runs: FED_C2M1, FAST=1, GOD=1, scenario `tools/scenarios/c131-kill.txt`, DLL `4cb036fa33c090bc`.
- New test support: `look actor <name> [dz] bounds` faces the actor's `AActor::GetActorBounds` centre
  (exe+0x37E33F0), not the origin.
- Nest: `BP_DestructibleSpawner_Bugnest_HumanGameplay_C_11`. Its class is the damage-killed nest from the playtest
  log. c133x8 used `_7`, which is a `BP_SatchelBuilding_Plasmabug_C` and took the `'S'` path.
- Host kill: `InstaKillTarget` through the gameplay damage path, not a satchel.
- Setup: client warped 1500 uu from the nest, host 1500 uu behind the client (out of the line), both facing the
  bounds centre.
- c131k1 passed the kill parts. It failed at its line 47 (wrong `count=2` on the host replay wait; the first join
  has an empty world journal, so no replay line). c131k2 is the full pass with that fixed.

### c131k2 log lines

- Host: `card098: host t=170551852 world K BP_DestructibleSpawner_Bugnest_HumanGameplay_C_11 203245 17223 6909 0:
  sent to 1 of 1 clients`.
- Client, 11 ms later: `card098: client t=170551863 world K BP_DestructibleSpawner_Bugnest_HumanGameplay_C_11 ...
  Health base 0 and OnDeath broadcast, IsDead now 1; client FedScore 20000`.
- Crates, host sent and client applied `IsDead now 1`:
  - `BP_KillableCrate_01_C_25`
  - `_05_C_30`
  - `_06_C_25`
  - `_04_C_26`, killed on the host by the others' blast; it was relayed the same way.
- Late join (card 045 drop and reconnect after the kills, a fresh map load):
  - Host: `world journal replay to BP_PlayerController_C (first Pong of the map): 22 ...`.
  - Client: `t=170578119 world K BP_DestructibleSpawner_Bugnest_HumanGameplay_C_11 ... IsDead now 1`, and the four
    crates `IsDead now 1`.
- Client log has 0 `IsDead now 0` lines in c131k1 and c131k2.

### Screenshots (logs/card131-shots/, looked at)

- `client-c131k2-c131k-nest-before.png`: the nest intact in frame, the host pawn not in the line.
- `client-c131k2-c131k-nest-edge.png`: the nest rubble with red smoke, and "+20,000 Bug Nest Killed".
- `client-c131k2-c131k-nest-5s.png`: the nest rubble with red smoke.
- `host-c131k2-c131k-nest-*.png`: the same times from the host's view.
- `client-c131k2-c131k-nest-rejoin.png`: after the rejoin and replay, the nest shows as rubble with red smoke.
- `client-c131k2-c131k-crates-before.png` / `-5s.png`: the grey killable crate left of the lidded box is gone at +5 s.
  The lidded box is not one of the killed crates.
- The c131k1 set is in the same folder, and matches.

### Run summary (c131k2)

- 83 s, ended by the scenario, drive rc 0, host rc 0, client rc 0.
- FATAL/ERROR: host 0, client 0. WARN: host 0, client 0. CRASH lines: none. New crash dumps: none.
- WARN diff against c130d5:
  - c130d5 had host `WARN cmd: N objective complete TacSupport_SON -> failed` and `WARN config:
    UBWCOOP_SKIP_FLIGHT=N needs UBWCOOP_AUTODROPSHIP=N`, and client none.
  - Neither appears here (different route and flags).
  - No new WARN.
- Table peaks: host `card130 AI pick-up peak 7 of 16`, client `card038 dead-set peak 3 of 256`.
- DLL `4cb036fa33c090bc` (`sha256sum | cut -c1-16`).
  - The play DLL is saved read-only as `build/dwmapi-play-4cb036fa33c090bc.dll`, with cards 131 and 133.
  - God mode is off by default in it: `UBWCOOP_GOD` is read from the environment, and only `run_pair.sh` sets it.

Report complete.
