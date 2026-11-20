# Card 071: Give the client the ammo its pickups credit on the host

Status: sent 2026-11-05 03:27 to junior ultimate-bug-war-coop-bb, after card 069 was accepted. Sent ahead of card 070: a client with no reserve runs dry in the first fight.

Read `docs/STATE.md` ("Ammo is not synced") and the card 069 Report and Senior review. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

Card 069 found that ammo pickups run only on the host. When the client's soldier walks over one, the host's `USTWeaponUtilityLibrary::AddAmmunitions` 0x05229F50 credits the client's server copy (`SetAmmoCount`, caller exe+0x522A10F). `USTAmmunitionInventory` does not replicate, so the client's own pawn keeps an empty `AmmoMap`, a reserve of 0, and a reload that does nothing (`client-c69a-after-reload`: 44/0).

## Design

Relay the credit, not the full ammo state.

- **Host:** detour `AddAmmunitions(AActor*, const TArray<FGameplayTag>&, const TArray<int32>&)`. After the original, if the actor is a remote player's server copy (resolve its controller the way card 050 picks the PC for `ChanOp::Weapons`), send that client one text frame on a new `ChanOp::Ammo`. The frame holds, for each tag, the ammunition type's name and the amount the host actually added: the count after the original minus the count before, read with `GetAmmoCount` 0x052393F0. An amount of 0 (the inventory was full) is not sent. For the host's own pawn, do nothing.
- **Client:** on `ChanOp::Ammo`, find its own pawn's `USTAmmunitionInventory` (pawn+0xA48), turn each name into an `FName` with the find flag (no new names), and call `USTAmmunitionInventory::AddAmmo(tag, amount)` 0x05229E20. Log one line per frame: tag, amount, the count before and after, and the `AddAmmo` return value. A name that does not resolve, or a missing inventory, is a WARN that names it.
- Known limit, to log and report rather than fix: the server copy never fires, so its count only rises. After a few pickups it reaches `GetMaxAmmo`, the host adds 0, and the client gets nothing even though its own reserve is low. Log the copy's count and max with each credit. If the proof run reaches the max, say so in the Report. A follow-up can then mirror the client's count to the copy, or send the requested amount from the pickup's array instead of the delta.
- Use `AddAmmo` rather than `SetAmmoCount`: the client's own fire and reload change its counts, and the host's copy never sees that, so the host's absolute count is wrong for the client. Only the amount added is true on both sides.
- Grenades: card 069's shots show the client with x0 grenades where the host has x1. Log whether the grenade pickups (`BP_GrenadeAmmoPickup_Frag`, `BP_GrenadeAmmoPickup_Incendiary`) go through `AddAmmunitions` too. If they do, the relay covers them with no extra code. If they do not, name the function they use in the Report and leave them for a follow-up.
- This runs in every co-op session, not behind a log flag. The card 069 log (`UBWCOOP_C69_LOG=1`) stays for the proof.

## Do

1. Implement the above.
2. Prove it with `tools/scenarios/c69-reload.txt`, client walking first (the c69b order), with `UBWCOOP_C69_LOG=1`:
   - The host log shows the credit on the server copy and the `Ammo` frame sent. The client log shows `AddAmmo` with the same tag and amount, and its 1 s line shows a Morita count above 0.
   - The client's `after-reload` shot shows a full magazine and the reserve reduced by the rounds reloaded (for example 60/224).
3. Run the c69a order (host walks first) too: the client gets no frame, and the host's own reserve is unchanged from card 069 (60/224 after its reload).

## Done when

- Both runs pass, with the log lines and the shots in the Report.
- `logsum` gives ERROR/FATAL 0, both games end with rc 0, and there is no new dump.
- A solo host (no client) logs no `Ammo` frame and plays as before.
- fnlen reports no function over 60 lines.

## Report

**Outcome: fixed.** The ammo a pickup credits to a client's server copy now reaches that client. When the client walks over the ammo first, its after-reload shot reads 60/224, where card 069 showed 44/0.

### What changed

All of the code is in `mod/src/hooks.cpp`, in the card 071 block after `c69_install`. The relay is installed in every co-op session through kHooks; it is not behind a flag.

**Host side:**
- `detour_c71_add_ammunitions` hooks `USTWeaponUtilityLibrary::AddAmmunitions` 0x05229F50. Only player pawns (`ASTPlayerCharacterMI`) on the host are measured.
- Before and after the original call, it reads `GetAmmoCount(tag, false)` for each tag, up to 8 tags per call.
- When the pawn is a server copy, it sends that copy's controller one `ChanOp::Ammo` text frame through `chan_send_client`. The frame is `"<ammunition tag> <gain> ..."` and lists only tags with a gain above 0.
- Every call logs a `card071: host` line, whether it is a server copy or the host's own pawn. The line gives each tag's requested amount, the count before and after, and the copy's `GetMaxAmmo`, marked `AT MAX` when the count has reached it.

**Client side:**
- `c71_receive` parses up to 8 `<tag> <amount>` pairs from the frame.
- It resolves each tag with `FName(…, FNAME_Find)`, so no new names are created.
- It calls `USTAmmunitionInventory::AddAmmo` on its own pawn's inventory (pawn+0xA48) and logs the count before and after and AddAmmo's return value.
- A name that does not resolve, a missing inventory, a bad amount or a bad frame each logs a WARN.

**Supporting changes:**
- `ChanOp::Ammo` is a new text opcode. The host's ServerChangeName dispatch ignores it.
- `GetAmmoCount`, `GetMaxAmmo` and `AddAmmo` now resolve in the always-on call table instead of inside `c69_install`. When `UBWCOOP_C69_LOG=1` is set, the client's AddAmmo call also goes through the card 069 log detour.
- `tools/symbols.txt` has the AddAmmunitions entry, and `offsets.generated.h` was regenerated (RVA 0x05229F50).

### Runs

In every run both games ended with rc 0, logsum gave ERROR/FATAL 0, and there was no new crash dump. All runs used deployed DLL 19eba0eb0baff79d. fnlen reports hooks.cpp at 1287 functions, 0 over 60 lines.

| Run | Scenario | Order |
|---|---|---|
| c71a | `tools/scenarios/c69-reload.txt` | client walks first, `UBWCOOP_C69_LOG=1` |
| c71b | `tools/scenarios/c71-host-first.txt` (new) | host walks first, `UBWCOOP_C69_LOG=1` |
| c71solo | `tools/scenarios/c71-solo.txt` (new) | `SOLO=1`, host alone |

**c71a (client first):**

| Host | Client |
|---|---|
| `AddAmmunitions on server copy BP_PlayerCharacterMI_C (BP_PlayerController_C), returned 120: AmmunitionType.Morita asked 120, 0 to 120 (max 240); Ammo sent: AmmunitionType.Morita 120` | `Ammo AmmunitionType.Morita +120: 0 to 120, AddAmmo returned 120` |
| `... asked 120, 120 to 240 (max 240) AT MAX; Ammo sent: AmmunitionType.Morita 120` | `Ammo AmmunitionType.Morita +120: 120 to 240, AddAmmo returned 120` |
| | reload: SetAmmoCount 180, then 224 (`RequestNewAmmunitionFromInventory`, the same callers as the host's reload in card 069) |
| | 1 s line: `equipped type count 224, AmmoMap 1 [ AmmunitionType.Morita=224/224/240 ]` |

- Client shots: `after-fire` 44/240, `after-reload` 60/224.
- Host shot: `after-reload` 44/0. The copy took the pickups, the same as card 069 c69b.

**c71b (host first):**
- The host's own pawn took both pickups (0 to 120 to 240), and a third after its reload (224 to 240, returned 16). Each logged `own pawn, not sent`.
- The client got no frame: 0 `card071` lines, AmmoMap 0.
- Host shot `after-reload` 60/224, unchanged from card 069. Client shot 44/0, as expected in this order.

**c71solo:**
- Two `AddAmmunitions on pawn ... own pawn, not sent` lines and 0 `Ammo sent`.
- Host shot `after-reload` 60/224.
- The one WARN is the existing `autodropship: 1 PlayerController(s)` line, which every solo run logs.

The `(no PC)` label on own-pawn lines only means the controller is read for server copies alone; it is not a fault.

### Known limit: the copy reaches its max

This happened in the proof run. After the second pickup the copy logged `120 to 240 (max 240) AT MAX`.

The copy never fires or reloads, so it stays at 240. From then on, the copy gains 0 from every later Morita pickup, so the host sends nothing, even though the client's own reserve is 224 or lower. A client that keeps fighting will stop getting pickup ammo after its first 240 rounds.

Two possible follow-ups:
- Mirror the client's count to the copy (the client sends its count; the host sets the copy).
- Send the pickup's requested amount, capped by the client's own max on the client, instead of the copy's gain.

### Grenades

Grenade pickups do not go through AddAmmunitions. `BP_GrenadeAmmoPickup_Frag_C` and `BP_GrenadeAmmoPickup_Incendiary_C` run `ASTItemPickup::Pickup_Implementation` (exe+0x51D4139, called from `AADPickupBase::Pickup_Implementation` exe+0x4C61EE3).

On the host, that path calls `UADGASBasicsUtilityLibrary::AddAbilityToActor` (exe+0x4A0EFD5), which gives the server copy `GA_FragGrenadeThrowingAbility` (card017 GiveAbility, handle 151). No AddAmmo or SetAmmoCount runs for grenades. The relay does not cover them; they are left for a follow-up.

## Senior review (2026-11-05 03:40): accepted

- Checked c71a, c71b and c71solo myself. All three drives end with `scenario passed`, and logsum gives ERROR/FATAL 0 on both sides. The only WARN is the known solo `autodropship` line. No new dump: the newest crash folder is still the 02:46 one from card 068.
- All three runs started after the DLL deploy at 03:23:26 (hash 19eba0eb0baff79d). fnlen: 1287 functions, 0 over 60 lines.
- c71a host and client lines match pair for pair: `Ammo sent: AmmunitionType.Morita 120` twice on the host, and `Ammo AmmunitionType.Morita +120: 0 to 120` then `120 to 240` on the client. The `client-c71a-after-reload` shot reads 60/224.
- Code read (`mod/src/hooks.cpp`, card 071 block):
  - The host measures only player pawns and passes everything else straight through.
  - The client resolves tags with the find flag, and the `%64s` bound matches `kChanNameMax` 64.
  - The host dispatch ignores `ChanOp::Ammo` from a client.
- Follow-ups:
  - The copy saturates at its max, so a client stops getting pickup ammo after 240 rounds. This is card 072, next.
  - Grenade pickups go through `ASTItemPickup::Pickup_Implementation` and give the server copy `GA_FragGrenadeThrowingAbility`, not ammo. They are a later card.
