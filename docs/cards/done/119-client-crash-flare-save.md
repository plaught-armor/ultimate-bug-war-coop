# Card 119: Client crash at the checkpoint save after a support flare

Status: written 2026-11-06 (senior); sent and accepted 2026-11-06.

Read:

- card 104 Report (flare relay, client copies, the `ASTSupportFlare` detours in `hooks_ubw.cpp` 1695-1699);
- card 074 Report (asset path field, `grenades.cpp:800`);
- card 048 Report (checkpoint save and load on the client, `USTSaveComponent`).

Repo root: `/path/to/ultimate-bug-war-coop`. DLL base: `f0a52e0573cfefb1` (card 118).

## Why

Second playtest (2026-11-06, both players on FED_C1M1, rtt 55 ms): the host threw the tutorial support flare and
the client got a UE Fatal error about 5 s later. The card 118 trace caught it.

Logs: `logs/playtest-host-20261106-flare.log`, `logs/playtest-client-20261106-flare.log` (client lines 3200-3261).

Senior analysis (symbols from `build/publics.txt`):

```
CRASH 1/4: EXCEPTION_ACCESS_VIOLATION at exe+0x50FD2B4, reading 0x10, rax=0     [client 8039110]
#0 ASTSupportFlare::PreSaveGame+0x34
#1 ASTSupportFlare::execPreSaveGame
#2-#7 UFunction::Invoke / ProcessEvent / multicast delegate
#8 USTSaveComponent::PrepareForSave+0x7e
#9 ASTSaveSystemEngine::Save_ProcessBatch
#10 ASTSaveSystemEngine::Tick
```

- `PreSaveGame` (0x050FD280): if byte +0x53A is set it destroys the actor; otherwise it reads the pointer at +0x300
  and copies `[+0x300]+0x10` to +0x328 and `[+0x300]+0x28..+0x43` to +0x308..+0x320. +0x300 is null on the client.
- The actor is the client's card 104 copy of the host's `BP_SupportFlare_Tutorial_C` (spawned at 8035844, "copy of
  host 124665"). The host's own flare gets +0x300 at the throw; a relayed copy is spawned bare and never gets it.
- The save came about 5 s after the copy spawned, after the TacSupport objectives completed (SO5, SO6). The save
  system's PrepareForSave runs PreSaveGame on every actor with a save component, including relayed copies.

Second fault in the same run: the bombs never reach the client. The host logs `WARN card074: host asset
BP_TacSupp_BombingRunProjectile_Tutorial_C: no path that fits (package length 95)` and then 15 times `WARN card104:
... BombingRunProjectile_Tutorial_C (-) not relayed: no class path or table full` (host 89608186-89608794).

## Do

1. **Measure (static, then the run below).**
   - Which relayed client copies (cards 098, 104, 105, 107) have a `USTSaveComponent` and take part in the client's
     save. Name each class seen in the two logs and what its `PreSaveGame` or save callback reads.
   - Whether the client's save matters at all: what the client does with its own save data (card 048 load path).
2. **Fix, one mechanism for every relayed copy** (user rule: one mechanism per class of state, not per symptom).
   - Preferred: a relayed copy takes no part in the client's save. For example, skip `USTSaveComponent::PrepareForSave`
     for a component whose owner is a relayed copy, or mark the copy's save component not to save when the relay
     spawns it.
   - Only if that cannot work: a guard on `ASTSupportFlare::PreSaveGame` that returns when +0x300 is null, logged
     once, plus the same for any other class that the measurement shows reads uninitialized state.
3. **Bomb relay.** Make `BP_TacSupp_BombingRunProjectile_Tutorial_C` (package length 95) fit the card 074 path
   field, so the bombs relay to the client. Size the field for the longest relayed path seen, plus margin, and keep
   the full-table WARN.
4. **Proof, one run:** FED_C1M1, FAST=1, both players in. The host takes the tutorial flare pickup and throws it.
   The TacSupport objectives complete and a checkpoint save runs on the client after the flare copy exists.
   - The client survives the save; the log shows the copy skipped (or guarded).
   - The client log shows the bomb projectile copies, and the host has no "not relayed" WARN for them.
   - rc 0, ERROR/FATAL 0 and no CRASH line on either side, no new dump.

Stop when the fix builds and that run shows it; the user will playtest.

## Done when

The Report has the relayed classes with save components and what each reads, the fix and where it sits, the path
field size, the run lines (save on the client after the flare copy, bomb copies), the run length, rc, ERROR/FATAL,
CRASH lines, dumps and the DLL hash (sha256 prefix, `sha256sum | cut -c1-16`). Ends with "Report complete."

## Report

### Measurement: relayed copies in the client's save

`USTSaveComponent::PrepareForSave` (0x05212370) reads the owner at component+0xA0, broadcasts the pre-save delegate
and then calls the owner's `PreSaveGame`. The playtest crash was that call on a card 104 copy of
BP_SupportFlare_Tutorial_C (`ASTSupportFlare::PreSaveGame`, +0x300 null on the copy). The client never loads its
save (card 048), so a copy has nothing to contribute to it.

These relayed classes reached the client's save in the runs (one "card119 save skips" line per copy per save):

| Class | Runs |
|---|---|
| BP_SupportFlare_Tutorial_C (ASTSupportFlare, the playtest crash) | c119g |
| BP_SupportFlare_C (same native class, regular flare) | c119c, c119g |
| BP_TacSupp_Bomber_Tutorial_C | c119g |
| BP_TacSupp_BombingRunProjectile_Tutorial_C (3 alive at the save) | c119g |
| BP_TacSupp_DangerZone_C (2) | c119c |
| BP_TacSupp_Pod_Resupply_C | c119c |
| BP_ResupplyRack_Level0_C | c119g |
| BP_WeaponPickup_MoritaMk1_C | c119g |
| BP_HealthPickup_Medium_C | c119g |

Copies that are not alive at a save (ammo and grenade pickups taken or destroyed before it) do not appear. The
host's save is untouched: `g_c104_copies` is empty on the host (0 card119 lines in c119g-host.log).

### Fix

One mechanism, client only, in `mod/src/ubw/world_c98.cpp`: `detour_c119_prepare_save` hooks
`USTSaveComponent::PrepareForSave` (new row in `hooks_ubw.cpp`, symbol `USTSaveComponent_PrepareForSave` in
`tools/symbols.txt`). If the component's owner is a row in the card 104 copy table `g_c104_copies` (which holds every
copy from cards 104/105/107: flares, strikes, DangerZone, pods, racks, pickups), the whole PrepareForSave is
skipped and a capped line is logged (`kC119Lines = 40`, reset in `c98_arm_map`); otherwise the original runs.
The owner pointer is only compared against the table, never dereferenced. A copy that the game destroyed but whose row
is still waiting for 'D' can only match a new actor at the same address, which would then skip a save that the client
never loads anyway.

### Path field

`kAssetPathChars = 192` in `hooks_internal.h` (with the terminator). `C74Path`, `C98World::name`, the card 074
explode text buffer and the c98 world text buffers use it. The wire sscanf widths are `%191s`, and a static_assert
ties each one to the size. The longest relayed path is 138 chars
(`/Game/Blueprints/Combat/BombingRun/BombingRun_Tutorial/BP_TacSupp_BombingRunProjectile_Tutorial.BP_TacSupp_BombingRunProjectile_Tutorial_C`).

### Runs (DLL 4b162843fc154d5f, FED_C1M1, FAST=1, both in)

- c119a/b/d/e: route attempts. Commanded objective completes do not drive the level script, so the bombing pickup
  stayed hidden, and a throw with tac charge 0 did nothing. None crashed: rc 0/0, ERROR/FATAL 0, no dumps.
- c119c: the client threw the regular resupply flare, and its save skipped 4 copies, BP_SupportFlare_C among them.
  No crash, 107 s, rc 0/0, ERROR/FATAL 0, WARN 0, no dumps.
- c119f: the resupply was thrown at the range, but the host was never moved to the rack, so the pickup did not show.
  The drive failed at the show wait. 152 s, rc 0/0, ERROR/FATAL 0, no dumps.
- **c119g (proof)**: the host throws the resupply at the range. The pickup shows when the rack lands, the host takes
  it and throws the tutorial flare, and the airstrike runs.
  - Scenario passed (drive rc 0), 101 s, host rc 0, client rc 0, ERROR/FATAL 0/0.
  - WARN host 1: the scenario's `try objective complete TacSupport_SO41` fallback found SO41 already complete.
  - WARN client 1: card 114 defaults ini err 32, a sharing violation because both games share one Saved dir.
  - No CRASH lines, no new dumps.
  - Client lines:
    ```
    card104: client t=91019504 copy of host 125883: /Game/Blueprints/Combat/BP_SupportFlare_Tutorial.BP_SupportFlare_Tutorial_C at (11952, 16091, 26) -> BP_SupportFlare_Tutorial_C
    card104: client t=91028705 copy of host 126449: /Game/Blueprints/Combat/BombingRun/BombingRun_Tutorial/BP_TacSupp_BombingRunProjectile_Tutorial.BP_TacSupp_BombingRunProjectile_Tutorial_C ...
    card119: client t=91028740 save skips the copy of host 125883: BP_SupportFlare_Tutorial_C (BP_SupportFlare_Tutorial_C)
    card119: client t=91028740 save skips the copy of host 126449: BP_TacSupp_BombingRunProjectile_Tutorial_C (BP_TacSupp_BombingRunProjectile_Tutorial_C)
    ```
  - 39 bomb projectile copies arrived on the client. The host logged 0 "not relayed" or "no path that fits" lines.

Scenario: `tools/scenarios/c119-flare-save.txt`. The pickup is revealed when the script completes TacSupport_SO41,
and the script does that when the resupply rack lands at the range. A resupply that lands at the spawn (c119e) never
completes SO41.

Report complete.
