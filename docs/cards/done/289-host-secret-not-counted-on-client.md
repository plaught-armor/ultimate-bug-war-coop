# 289 A secret the host picks up is not counted for the client

Playtest 2026-11-13 18:30 (user plays as client): "secrets picked up don't count if host picks them up, for
client". User rule (full world sync): a secret found by any player counts for every player.

## Trace (live client log `UBWCoop-menu-20261113-183002.log`)

| Line | Client t (ms) | Row |
|---|---|---|
| 7487-7488 | 255344749 | `card137: client change BP_SecretPickup_C ... at (-11011, 28954, 160) component collision 1 -> 0 by host row` |
| 7489 | 255344749 | `card145: client t=255344749 world E BP_SecretPickup_C_0 -11011 28954 160 0 ...: applied` |
| next 3 | 255344749 | `WorldOp f66a767d E 0`, `CollisionOp ... PlayerCollider 0`, `CollisionOp ... InteractionCollider 0`, all `applied` |

The host took `BP_SecretPickup_C_0`. The client received only the enabled row (card 145) and two collider rows. No
row on the client credits the secret.

## Cause (static)

- `ASTSecretPickup::OnInteraction` 0x51D0590 calls `AADPickupBase::Pickup` 0x4C61940; the take ends in the base
  `Pickup_Implementation` 0x4C61DC0 (card 254, `docs/SYNC_AUDIT.md` pickup entry).
- The secret credit (the found-secrets count shown in the mission tally) is the subclass's own work before the base
  call. It runs on the machine that runs the take. Since card 254 the host is the only machine that runs a take, so
  the credit is written only into the host's game.
- The mod sends the pickup's visible state (`E` row, colliders, card 136 FX) and nothing for the credit.

## Step 1 (static, junior, no run)

From the disassembly of the `ASTSecretPickup` take (vtable 0x758 override and the `BP_SecretPickup_C` graph if the
credit is in blueprint): name the function that records the secret, its object (game state, player state, save or
meta-progress object) and its arguments (secret id or the actor). Record where the HUD and the end-of-mission
tally read the count. Write the answer into this card before coding.

## Fix

- Host: when a secret is credited (the function found in step 1), send one row to every client naming the secret by
  its placed-actor key, the same key the `E` row uses. Journal it, so a client that joins or reloads later gets
  it.
- Client: on that row, call the same credit function on its own game for that secret, once per key. Do not run the
  pickup (`Pickup_Implementation` stays skipped by card 254): no second FX, no `OnPickup` chain.
- A secret the client takes already goes to the host by the interact relay; the host credits it and the same row
  comes back. The client must not credit its own take a second time.
- Rows: `card289: host t=<ms> secret <key> credited, sent to <n> clients`,
  `card289: client t=<ms> secret <key> credited from the host row (count <before> -> <after>)`,
  `card289: client t=<ms> secret <key> already credited` for a repeat.

## Sibling sweep (same class: a per-player credit the host's take writes only on the host)

Report each as synced, not synced, or not applicable, with the function that proves it:

| Credit | Check |
|---|---|
| Kill count and score in the mission tally | the client's `FedScore` row already follows the host; confirm kills and accuracy |
| Key pickups (`ASTKeyPickup`) | does the client hold the key the host took |
| Tactical support pickups | charge count on the client after a host take |
| Health, armour and ammo pickups | per player by design; list only |
| End-of-mission statistics screen | every counter it shows, source object, whether the client's copy is written |
| Saved progress (unlocks, secrets found per map) | whether the client's save gets the host's credits at mission end |

Any counter found not synced gets the same host row and client credit, in this card if it shares the function,
else a new card.

## Predictions (one run, after the playtest, batched with 284-288)

- Host takes a C1M1 secret: host `card289` credited row; client `card289` credited row within 200 ms with
  `count 0 -> 1`; the client's HUD or tally shows the same count as the host.
- Client takes a second secret: host row, client row `count 1 -> 2`, no `already credited` row for a first take.
- Digest round after both: 0 mismatches.

## Step 1 answers (static, 2026-11-13)

- The credit is native, in `ASTSecretPickup::Pickup_Implementation` (RVA 0x051D41B0, the vtable override of the base
  0x4C61DC0). It runs before its tail `jmp` to the base, so card 254's skip of the base on the client does not
  cover it: a client's own take credits locally today.
- The credit, in order (disassembly 0x1451D41B0-0x1451D421B):
  1. `USTSecretsUtilityLibrary::IsSecretLocked(GetWorld(), &SecretTag)` (0x052437D0). `SecretTag` is the
     `FGameplayTag` at actor +0x4B8. If the secret is locked, `UnlockSecret(GetWorld(), &SecretTag)` (0x05255B50)
     runs. It writes `FSTCampaignSlot::UnlockSecret`, which is saved campaign progress.
  2. `USTCommonUtilityLibrary::GetSTMetaGame(this)` (0x0523E090). If the meta game is non-null and ObjectFlags
     bit 30 is clear, `USTMetaGame::SecretFound(meta)` (0x0516CA50) runs. It broadcasts `OnNewSecretDiscovered`
     (+0x100) and increments `SecretsFoundCount` (+0x48, int32).
- The object is `USTMetaGame`, one per machine, reached through the world context. The host's take writes only the
  host's meta game and campaign slot.
- `SecretsFoundCount` is Edit|BlueprintVisible and not replicated. The HUD and the end tally read it through
  blueprint. `SetSecretsFound` is called only from `USTGameInstance::OpenLevel` (per-level reset) and
  `USTSaveSystemLibrary::Load`. `GetNumLevelSecretsUnlocked` and `GetTotalLevelSecrets` read the campaign slot.
- The credit can be called on the client without the pickup: the same three calls on the client's own copy of the
  secret, with no base call (no FX, no `OnPickup`).
- Open point: `execSecretFound` exists, so a blueprint that calls `SecretFound` directly is not covered. The
  `BP_SecretPickup_C` graph has not been dumped.

## Sibling sweep answers (static)

| Credit | Answer | Proof |
|---|---|---|
| Kill count and score | Not proven | `USTMetaGame::EnemyKilledByGameplayTag` is called only from host death logic (`ASTEnemyBase::StartDying`, `ASTNonPlayableCharacterBase::StartDying`, `ClearNPC`). `SetEnemiesKilledByGameplayTag` is called only from save `Load`. The card038 counters read 0 on both sides in every log (206 host lines, all 0), so they are no evidence. Needs one run reading `EnemiesKilledByGameplayTag` on both sides. New card if the client stays at 0. |
| Key pickups (`ASTKeyPickup`) | Not applicable natively | `ASTKeyPickup::Pickup_Implementation` calls only `GetOverlappingCharacter` and the base. The key state is the blueprint `OnPickup` chain: host only (card 254) plus the objective rows. |
| Tactical support pickups | Per player | `PickupTacticalSupport(ASTPlayerCharacter*)` calls `USTTacticalSupportComponent::EquipTacticalSupportAbility` on the taker's own component. |
| Weapons found | Not synced (tally candidate) | `USTMetaGame::WeaponFound` (WeaponsFoundCount) is called from `USTWeaponUtilityLibrary::AddWeapon`, for the taker only. A new card if the tally should count the other player's finds. |
| Health, armour and ammo | Per player by design | none |
| End-of-mission statistics | Secrets: fixed by this card. Kills: see row 1. Weapons: see the weapons row. | `USTMetaGame` counters, per machine |
| Saved progress (secrets) | Synced by this card | `UnlockSecret` runs in the client credit when the secret is locked in the client's campaign slot |

## As coded (uncompiled, 2026-11-13)

- `tools/symbols.txt`, `mod/src/offsets.generated.h`: `ASTSecretPickup_Pickup_Implementation`,
  `USTMetaGame_SecretFound`, `USTCommonUtilityLibrary_GetSTMetaGame`, `USTSecretsUtilityLibrary_IsSecretLocked`,
  `USTSecretsUtilityLibrary_UnlockSecret`.
- `hooks_ubw.cpp`: hook on `ASTSecretPickup_Pickup_Implementation` (`detour_c289_secret_pickup`); four resolve rows.
- `world_c98.cpp`:
  - New world kind `'G'` (`kC98KindSecret`).
  - `c98_host_world` returns its `C95Fanout`.
  - Detour behaviour:
    - Host (listen server, placed secret): reads the count, runs the original, reads the count again. If the count
      went up, it sends a journalled `'G'` row with value = host count. If not, it logs a WARN and sends no row.
    - Client (placed secret): logs `own take ... credit skipped` and calls the base at its game address, so card
      254 still decides the base.
    - Anything else: runs the original.
  - `c289_client_secret`: finds the copy by `c95_find_nearest` and `is_a` secret class, then dedupes by object
    index in a per-map set (64 entries, reset in `c98_world_map_end`). The credit itself is `IsSecretLocked`,
    then `UnlockSecret`, then `GetSTMetaGame`, the garbage check, then `SecretFound`. It logs the rows the card
    names and reports through the card 132 ledger.
- Open points:
  - A client save `Load` mid-map resets `SecretsFoundCount` while the dedupe set stays. A journal replay after that
    would not re-credit.
  - A blueprint that calls `SecretFound` directly is not covered.
  - The hooked prologue holds a rel32 `call` at +0x0D, inside the first 14 bytes. The hook library must relocate
    it, as for other hooks on functions that start with a call.

## Closed 2026-11-16 20:31 (run `c236b163a`, b163 `67d43ad88c956cf3`, FED_C3M2 with the ride)

Run passed (65 ok, 0 FAIL, EXCEPTION 0/0, 0 mismatches). Host `521386329 card289: host ... secret BP_SecretPickup_C_2
credited, sent to 1 clients (count 0 -> 1)`; client `521386341 card289: client ... secret BP_SecretPickup_C_2 credited
from the host row (count 0 -> 1); unlocked in the campaign slot`.
