# Card 033: Hide the remote pawn's first-person meshes

Status: done 2026-11-03.

Read `docs/STATE.md` first: the "Mod code rules" section, the "Runtime facts" line about `tools/run_pair.sh`, and row 028. Then read card 028's report and senior review. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

Card 028 draws a remote player as a sprite, but the remote pawn's own first-person parts still render on the viewing machine. `card028d-host-5-Walk-side.png` shows the client's MoritaMk1 floating above the sprite's head. `card028d-client-5-Idle-side.png` shows the host's grey first-person arms near head height. Both have to go before the sprite body reads as a player.

## Static leads (senior)

- The pawn's arms are `FirstPersonMesh` (`SkeletalMeshComponent`, `SKM_Arms`, anim `ABP_Arms_Skeleton_AnimBlueprint_C`) at pawn +0x848, attached to `Camera`. Card 022 logged it on both sides with `bVisible=true, bHiddenInGame=false, bOwnerNoSee=false, bOnlyOwnerSee=false`.
- The equipped weapon is an `AADWeapon` actor from `UADWeaponInventory::GetEquippedWeapon` (`?GetEquippedWeapon@UADWeaponInventory@@QEBAPEAVAADWeapon@@XZ`), which card 029 reads every 500 ms. Weapon actors do not replicate (card 029), so each machine owns its copy and hiding it is local.
- Candidates, all in the PDB publics (resolve them with `tools/pdb_rva.py`):
  - `?SetVisibility@USceneComponent@@AEAAX_NW4EVisibilityPropagation@1@@Z` and `?SetHiddenInGame@USceneComponent@@AEAAX_NW4EVisibilityPropagation@1@@Z`. These are the private workers behind the public inline wrappers. The second argument is `EVisibilityPropagation`; check its values in UE 5 source before use.
  - `?SetOwnerNoSee@UPrimitiveComponent@@QEAAX_N@Z`.
- The parts float only on the machine that does not own the pawn. On the owner, the same meshes are the player's own first-person view and must stay visible.

## Do

1. On the remote pawn only (the `c28_remote` test, after the sprite attach), hide `FirstPersonMesh` and its attached children, with propagation to children. If the weapon actor's root is not under `FirstPersonMesh`, hide the weapon actor too.
2. Keep it hidden. The game may show the weapon again on equip, swap or reload. Recheck in the existing `c28_visit` sample, re-hide when needed, and log every re-hide with the component or actor name and its class.
3. Log once per remote pawn: what was hidden (name, class, attach parent) and the state before and after (`bVisible`, `bHiddenInGame`).
4. Runs: the card 028 env (with `UBWCOOP_GRANT_WEAPON=MoritaMk1` on both sides and `UBWCOOP_C28_SHOTS=1`), plus one run that swaps weapons on the host (card 029 driver or grant rows) so the re-hide path runs.
5. Screens: the card 028 Idle/Walk side shots on both sides, with no floating arms or rifle. Then one shot of each player's **own** view, showing their own first-person arms and weapon still visible.

## Answer, with log lines

- Which components or actors had to be hidden, and with which call?
- Does anything show itself again (on equip, swap or reload), and does the re-hide catch it?
- Is the owner's own first-person view untouched?

## Done when

- No floating first-person parts in the side shots on either machine, and the own-view shots show normal first-person arms and weapon.
- Standalone untouched: the code runs only where `c28_visit` already runs.
- Clean build (`/W4 /WX`, `--clean-first`), 84 exports, KERNEL32-only imports. No new crash dumps. Every new function is at most 60 lines.

## Risks

- A wrong `EVisibilityPropagation` value may hide only the root. If so, walk `AttachChildren` and hide each one.
- Hiding the server copy's weapon on the host must not change gameplay (traces, fire). Weapon fire on the host's copy of the client comes from the card 024 relay. Confirm that Attack01 still triggers on the host after the hide.

## Report

Done. Neither machine shows the other player's first-person arms or rifle any more; only the card 028 sprite remains. The owner's own first-person view is unchanged. Each new weapon after a swap is hidden as soon as it is equipped.

### What was built (`mod/src/hooks.cpp`, `c33_*`; `tools/symbols.txt`)
- **Call.** `USceneComponent::SetHiddenInGame(bool, EVisibilityPropagation)` (private worker, 0x03AF2C50, added to `tools/symbols.txt` as `USceneComponent_SetHiddenInGame`), called with `true` and `Propagate` (2).
  - The UE 5 enum is `{NoPropagation, DirtyOnly, Propagate}`. The disassembly matches: `cmpb $0x2, %r8b; sete` takes the always-walk-children path, `bHiddenInGame` is bit 3 of +0x1A1, and the worker calls vtable +0x558 when the value changes.
- **What is hidden.** `c33_hide` runs from `c28_visit` after the sprite attach, so only on remote pawns and never in standalone. It hides:
  - `FirstPersonMesh` (pawn +0x848) with its children;
  - the root component of the equipped weapon (`UADWeaponInventory::GetEquippedWeapon` on the pawn's inventory at +0xA40). The log shows the weapon root is attached under `FirstPersonMesh`, so propagation already reaches it. It is checked on its own anyway, because a swap brings in a new actor.
- **Keeping it hidden.** Every c28 step (100 ms), each target's reflected `bHiddenInGame` is read and the call is repeated only when it is false. The slot remembers the object index last hidden in each role:
  - same component again: logged as "re-hid";
  - a different component: logged as "hid new" (a swap).
  - Logging is capped at 64 lines per map.
- **Logged per hide:** component name and class, owning actor, attach parent and class, and `bVisible`/`bHiddenInGame` before and after. `bVisible` stays 1; only `bHiddenInGame` changes 0→1.
- **No new env.** Always on in pair sessions, gated exactly like card 028. Per-map reset in `c28_arm_map`. Fixed-size slot table (`kC28MaxPawns`), no heap.

### Answers
- **Which components, which call.** On the host's copy of the client:
  - `FirstPersonMesh (SkeletalMeshComponent), attach parent Camera (CameraComponent)`;
  - the weapon root `Mesh (SkeletalMeshComponent), owner BP_Weapon_MoritaMk1_C, attach parent FirstPersonMesh`.

  On the client's copy of the host, only `FirstPersonMesh` is hidden: no weapon root line ever appears there, so that pawn has no equipped weapon actor (weapon actors do not replicate, card 029). The grey arms seen in card 028 were the whole problem on that side. All hides use `SetHiddenInGame(true, Propagate)`.
  - `card033-host.log`: "host hid FirstPersonMesh …", "host hid weapon root … owner BP_Weapon_MoritaMk1_C, attach parent FirstPersonMesh …".
  - `card033-client.log`: "client hid FirstPersonMesh …".
- **Does anything show itself again?** No component was shown again: there is no "re-hid" line in either run. Swaps create a new weapon actor, and each one was caught on the next step. In `card033b-host.log`, the card 029 cycle (`UBWCOOP_AUDIT_029=1` on the host, which swaps on the server copy too) produced "hid new weapon root" for MoritaMk1Scoped, MoritaMk2, MoritaCarbine, EPulse44, EPulse88, NukeLauncher, Flamethrower, AutoCannon and Railgun. Each appears 20 to 300 ms after the driver's "equipped now" line. Reload was not exercised: no reload driver exists, and a reload keeps the same actor, so it would show up as "re-hid" if it ever un-hid the mesh.
- **Owner's view untouched.** The code runs only on `c28_remote` pawns. Every screenshot shows the viewer's own first-person weapon or hand in the foreground:
  - MoritaMk1 in `card033-host-5-Walk-side.png` and `card033-client-4-Idle-side.png`;
  - the cycled AutoCannon in `card033b-host-3-Idle-side.png`;
  - the bare gloved hand after the cycle in `card033b-host-6-Walk-side.png`.
- **Gameplay on the host's copy.** Attack01 still triggers from the card 024 relay after the hide: in `card033-host.log`, "key Idle -> Attack01 (… relay 1 …)" at 492703453 and 492705886. `card033-host-7-Attack01-back.png` shows muzzle sparks and ejected cases.

### Runs (both rc 0/0, 0 FATAL/ERROR, no new crash dumps)
Env as card 028 (host `UBWCOOP_AUTODROPSHIP=1 UBWCOOP_HOST_SPRINT=1 UBWCOOP_C28_SHOTS=1 UBWCOOP_GRANT_WEAPON=MoritaMk1`; client `UBWCOOP_AUTOINPUT=1 UBWCOOP_C28_SHOTS=1 UBWCOOP_GRANT_WEAPON=MoritaMk1`). Deployed `dwmapi.dll` 47f1f99ebb61236b.

| Run | Extra | Result |
|---|---|---|
| `card033` | — | 185 s, 0 WARN. No floating parts in any shot. Side shots: `card033-host-3-Idle-side.png`, `card033-host-5-Walk-side.png`, `card033-client-4-Idle-side.png`. Fire: `card033-host-7-Attack01-back.png`. |
| `card033b` | host `UBWCOOP_AUDIT_029=1` (weapon cycle) | 186 s. One host WARN, "card021: host grant of MoritaMk1 stopped: grant cap reached", is the card 021 grant meeting the card 029 cycle, not new code. Side shots: `card033b-host-3-Idle-side.png`, `card033b-host-6-Walk-side.png`, `card033b-client-5-Idle-side.png`. All 9 swaps hidden. |

### Done-when check
- No floating first-person parts in the side shots on either machine. The own view shows normal first-person arms and weapon: yes (files above).
- Standalone untouched: `c33_hide` is called only from `c28_visit`, which returns before any work in standalone.
- `/W4 /WX` `--clean-first` build is clean; DEPLOY check (84 exports, KERNEL32-only) passed. No new crash dumps. The new functions (`c33_slot`, `c33_keep_hidden`, `c33_hide`) are each well under 60 lines, and the awk check finds nothing over 60 in `mod/src/*.cpp`.

## Senior review (2026-11-03)

Accepted.

Verified:
- Runs: `card033` 185 s and `card033b` 186 s, both rc 0/0, 0 FATAL/ERROR, no new crash dumps. The single `card033b` host WARN is the card 021 grant cap meeting the card 029 cycle, as reported.
- Logs: each client log has exactly one "client hid FirstPersonMesh" line (before `bHiddenInGame 0`, after 1). The host logs FirstPersonMesh plus the weapon root under it. `card033b-host.log` has 9 "hid new weapon root" lines and 0 "re-hid" lines. Attack01 from the card 024 relay still fires after the hide.
- Screens: `card033-host-5-Walk-side`, `card033-client-4-Idle-side`, `card033-host-7-Attack01-back` and `card033b-host-6-Walk-side` show no floating arms or rifle. The viewer's own first-person weapon or hand is visible in each.
- Build: deployed DLL 47f1f99ebb61236b, 84 exports, KERNEL32 only. No function is over 60 lines. The `SetHiddenInGame` symbol is in `tools/symbols.txt`.

Follow-ups (not blocking):
- Reload un-hide is untested. A reload keeps the same actor, so the "re-hid" path would catch it; watch for that line once a reload driver exists.
- The client's copy of the host has no equipped weapon actor. The host's weapon is therefore invisible on the client entirely, not only hidden. The sprite body carries the weapon look for now. A third-person weapon on the remote pawn needs its own card.
