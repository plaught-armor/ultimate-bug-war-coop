# Card 072: Keep the server copy's ammo equal to the client's

Status: sent 2026-11-05 03:44 to junior ultimate-bug-war-coop-bb, after card 071 was accepted.

Read `docs/STATE.md` ("Ammo is not synced") and the card 069 and 071 Reports. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

Card 071 relays a pickup's gain from the client's server copy to the client. The copy never fires or reloads, so after two Morita pickups it sits at its max (240). From then on the host's `AddAmmunitions` adds 0 to the copy and nothing is relayed, even when the client is nearly empty. The pickup's own acceptance check is probably made against the copy too, so a client may also be unable to take a pickup at all. Second gap: `ResetInventory` gives every pawn on the host `AmmunitionType.Laser` 300, but the client's own pawn gets nothing (card 069), so the client's Laser reserve starts at 0.

## Design

The client owns its counts. The copy mirrors them.

1. **Seed, host to client, once per spawn.** When card 050 first sends `Weapons` for a new copy pawn (its `force` reasons `new copy pawn` / `LoadAbilitySystemComponent`), also send the copy's whole `AmmoMap` as absolute counts on a new `ChanOp::AmmoSet` frame (`"<tag> <count> ..."`). The client applies each with `SetAmmoCount` on its own inventory. This gives the client its Laser 300. Log one line on each side.
2. **Mirror, client to host.** On the client, check its own inventory's `AmmoMap` (stride 0x14, keyed by `AmmunitionType.*`, as card 069 found) at most 4 times a second. When any count differs from what was last sent, send the whole map as `"<tag> <count> ..."` to the host through the client-to-host text path that card 050's `UseName` uses. On the host, set each count with `SetAmmoCount` on the sender's own pawn copy (the pawn of the PC the frame came from, never another pawn). Log only changes, not every check.
3. **Order.** The client's count is the truth. If a pickup credits the copy between two mirror frames, the next mirror frame can lower the copy again, but the relayed gain has already reached the client, and the client's next frame carries it. So the two sides agree after one round trip. Do not add sequence numbers unless a run shows a count that stays wrong.
4. Card 071's relay stays as it is: it hooks only `AddAmmunitions`, so the host's `SetAmmoCount` for a mirror does not trigger a relay. Check this in the log.
5. Checks: friends-only trust (see memory and STATE). Check the frame shape and that the tag name resolves with the find flag. Clamp each count to 0 or above and to `GetMaxAmmo`. That is all.

This runs in every co-op session, not behind a flag.

## Do

1. Implement the above.
2. Proof with a new scenario: the client walks first, takes both Morita small pickups, fires two full magazines and reloads, then takes a third pickup (place it or route to one; the c69 route has two near the start, find a third on the assault course with `look`/`goto`).
   - The host's copy count follows the client's within 1 s (log both).
   - The third pickup credits the copy, and the client's count rises by its amount. Show it in the shots before and after.
   - The client's Laser count reads 300 after the seed (1 s line from `UBWCOOP_C69_LOG=1`).
3. Rerun c71b (host first) and the solo scenario. Nothing changes for the host's own pawn.

## Done when

- All three runs pass, with the log lines and the shots in the Report.
- `logsum` gives ERROR/FATAL 0, both games end with rc 0, and there is no new dump.
- fnlen reports no function over 60 lines.

## Report

All three runs passed with deployed DLL 113cbf0428ade09f. In each run both games ended with rc 0 (the solo client has no process), logsum gave ERROR/FATAL 0, and there was no new crash dump. fnlen reports hooks.cpp at 1296 functions, 0 over 60 lines.

### What changed (mod/src/hooks.cpp)

- Two new ChanOp text frames, `AmmoSet` (host to client) and `AmmoMirror` (client to host over the UseName path). Both carry "<tag> <count> ..." pairs, at most 8.
- Seed: `c72_seed` runs from `c50_host_poll` when card 050 sees a new copy pawn (or its ASC). It sends the copy's AmmoMap as absolute counts. An empty map returns false and the next poll retries. The client applies it with `c72_receive_set`.
- Mirror: `c72_client_step` runs from `c50_step` at most every 250 ms (`kC72MirrorMs`). It sends the client's own AmmoMap only when its text differs from the last one sent. The host applies the frame in `c72_mirror_apply`, on the sender's own server copy only (the PC's pawn, through `c50_copy`).
- Checks only: the frame shape (sscanf_s pairs; a bad pair is a WARN), the tag resolving with the find flag (`c71_find_tag`, extracted from card 071; an unknown name is a WARN), and each count clamped to 0 or above and to GetMaxAmmo on the host. A map with holes is a one-time WARN and is not sent.
- Logs: one line per side for the seed. The mirror logs only changes ("tag before to after (asked, max, ret)"); a frame that changes nothing logs nothing on the host.
- Runs in every co-op session, with no flag. There are no sequence numbers.

### Two findings that shaped the design

1. **The server copy runs its own reload.** When the client's relayed fire empties a magazine, the copy reloads by itself and takes another magazine off its own count. So one mirror is not enough: the copy drifts one magazine below the client after each reload. The host therefore keeps the last mirror text for each copy (`C50Copy::ammo`) and re-applies it on each card 050 poll (250 ms). It logs a re-apply only when it changes something (`AmmoMirror re-applied`).
2. **A stale re-apply took back a relayed pickup.** In run c72b, the third pickup credited the copy from 120 to 240. The next poll re-applied the stored mirror (120) before the client's new frame arrived, and the client's mirror set the copy back to 240 130 ms later. Fix: when card 071 sends a relayed credit for a server copy, it clears that copy's stored mirror text. Nothing is re-applied until the client's next AmmoMirror, which already carries the gain. Run c72c shows no dip.

Other notes:

- `SetAmmoCount` returns the delta, not the new count (ret -60 for 240 to 180).
- On the client, the game's ResetInventory runs on the host's pawn proxy (which holds Laser 300). The client's own pawn gets nothing until the seed. That is why the Laser count read 0 before this card.
- The host's SetAmmoCount does not trigger the card 071 relay. Card 071 hooks AddAmmunitions only. In c72c the host log has exactly 3 card071 lines, the three pickups, and none at any mirror or re-apply time.
- Grenades are out of scope and untouched.

### Run c72c: client walks first (tools/scenarios/c72-mirror.txt)

The client takes the MoritaMk1, both MoritaSmall pickups, fires two full magazines with a reload after each, then takes a MoritaLarge. The host stays idle.

Seed, and the client's Laser count reading 300 (UBWCOOP_C69_LOG=1):

```
host   t=21341712 AmmoSet to BP_PlayerController_C (copy BP_PlayerCharacterMI_C): sent: AmmunitionType.Laser 300
client t=21341721 AmmoSet "AmmunitionType.Laser 300" on BP_PlayerCharacterMI_C: AmmunitionType.Laser 0 to 300 (asked 300, max 300, ret 300)
client t=21341772 card069: client 1s pawn ... weapon BP_Weapon_Unarmed_C ..., AmmoMap 1 [ AmmunitionType.Laser=300/300/300 ]
client t=21341856 AmmoMirror sent: AmmunitionType.Laser 300
```

Two pickups (card 071 relay, unchanged), then the first mirror:

```
host   t=21395050 AddAmmunitions on server copy ... AmmunitionType.Morita asked 120, 0 to 120 (max 240); Ammo sent
client t=21395058 Ammo AmmunitionType.Morita +120: 0 to 120
host   t=21395134 AddAmmunitions on server copy ... 120 to 240 (max 240) AT MAX; Ammo sent
client t=21395141 Ammo AmmunitionType.Morita +120: 120 to 240
client t=21395161 AmmoMirror sent: AmmunitionType.Laser 300 AmmunitionType.Morita 240   (no host change: copy already 240)
```

First and second reloads. The host copy follows the client within 17 ms and 8 ms; the copy's own reload is answered by the re-apply within 0.7 s:

```
client t=21406796 AmmoMirror sent: ... AmmunitionType.Morita 180
host   t=21406813 AmmoMirror from BP_PlayerController_C (copy ...): AmmunitionType.Morita 240 to 180 (asked 180, max 240, ret -60)
host   t=21407507 AmmoMirror re-applied ...: AmmunitionType.Morita 120 to 180 (asked 180, max 240, ret 60)
client t=21418139 AmmoMirror sent: ... AmmunitionType.Morita 120
host   t=21418147 AmmoMirror from ...: AmmunitionType.Morita 180 to 120 (asked 120, max 240, ret -60)
host   t=21418825 AmmoMirror re-applied ...: AmmunitionType.Morita 60 to 120 (asked 120, max 240, ret 60)
```

Third pickup (MoritaLarge, asked 240) credits the copy and the client's count rises. With the fix, no re-apply follows:

```
host   t=21422534 AddAmmunitions on server copy ... AmmunitionType.Morita asked 240, 120 to 240 (max 240) AT MAX; Ammo sent: AmmunitionType.Morita 120
client t=21422538 Ammo AmmunitionType.Morita +120: 120 to 240, AddAmmo returned 120
client t=21422675 AmmoMirror sent: AmmunitionType.Laser 300 AmmunitionType.Morita 240   (no host change: copy already 240)
```

Shots (prefix/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/Screenshots/Windows/):

- `client-c72c-before-third.png`: HUD Morita 60 / 120.
- `client-c72c-after-third.png`: HUD Morita 60 / 240.
- `client-c72c-equipped.png`: the weapon equipped.

Run c72b (before the clear-on-credit fix) passed the same way, apart from the dip described in finding 2.

### Run c72d: c71b rerun, host first (tools/scenarios/c71-host-first.txt)

Passed. The host's own pawn behaves as in card 071: three AddAmmunitions lines, all "own pawn, not sent" (0 to 120, 120 to 240, 224 to 240). The seed and the client's one Laser 300 mirror ran. No AmmoMirror change was applied on the host, and none touched the host's own pawn.

### Run c72f: solo (tools/scenarios/c71-solo.txt, SOLO=1)

Passed. Two AddAmmunitions lines, both "own pawn, not sent". There are no card072 lines, because there is no client. The single WARN is the standard solo "autodropship: 1 PlayerController(s); unpause action in 3000 ms", the same as in run c71solo. (Run c72e was my own mistake: I left UBWCOOP_AUTODROPSHIP out of HOST_ENV, the host never left the dropship, and the scenario timed out on line 2. It is not a mod failure.)

## Senior review (2026-11-05 04:02): accepted

- Checked c72c, c72d and c72f myself. All three drives end with `scenario passed`, logsum gives ERROR/FATAL 0 on both sides, and the only WARN is the known solo `autodropship` line. No new dump: the newest crash folder is still the 02:46 one from card 068.
- The deployed DLL 113cbf0428ade09f (03:46:22) predates c72c (03:46:28). fnlen: 1296 functions, 0 over 60 lines.
- The c72c host log has exactly 3 card071 lines, one per pickup, and none at mirror or re-apply times. The seed and mirror lines are on both sides, and the client's 1 s line reads Laser 300/300/300.
- Shots checked: `client-c72c-before-third` reads 60/120 and `client-c72c-after-third` reads 60/240.
- Both design changes are accepted: the host re-applies the stored mirror every 250 ms because the copy runs its own reload, and a card 071 credit clears the stored mirror so a stale re-apply cannot take back a relayed pickup.
- Follow-up: grenade pickups (`ASTItemPickup::Pickup_Implementation` gives the copy `GA_FragGrenadeThrowingAbility`) are still not relayed. Later card.
