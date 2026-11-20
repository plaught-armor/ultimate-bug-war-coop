# 292 Satchel placement site stays on a dead flak bug (client)

Playtest 2026-11-13 18:53 (user plays as client, FED_C2M1): "satchel bug dead but i can still see the place to
place the satchel". Screenshot `/home/king/Pictures/Screenshots/Screenshot_20261113_185339.png`: dead flak bug, the
red placement box still drawn on it, prompt "INSUFFICIENT SATCHEL CHARGES".

## Trace (client log `UBWCoop-menu-20261113-184603.log`)

| Line | Client t (ms) | Row |
|---|---|---|
| 21924-21926 | 256303813 | `card137: client change BP_SatchelBuilding_Flakbug_C ... component collision 1 -> 0`, `0 -> 1`, `1 -> 0` by host row |
| 21927 | 256303814 | `card098: client ... world S BP_SatchelBuilding_Flakbug_C_0 230950 -10592 7908 0 ...: applied ReceiveOnSatchelExploded` |
| | | no `card163: client ... damage kill applied` row follows |
| 23805 | 256412441 | `HitClaim shot 370 miss BP_SatchelBuilding_Flakbug_C at (230877.1, -10816.1, 8121.3)`: the user at the dead bug, 109 s later |

## Cause (static)

`c98_client_satchel` (`world_c98.cpp:1828`) runs `SatchelPlacementSiteInstance->NeutralizeSite` only for a row
with value `kC163DamageKill` (2). This row has value 0: the host's `ReceiveOnSatchelExploded(false)` came by a
path that is not the building's own damage event. On the host that path's caller also removes the site; the client
runs only `ReceiveOnSatchelExploded(false)` (Become Destroyed(false)), so the building dies and its placement site
stays visible and usable.

## Step 1 (static, junior, no run)

Name every host caller of `ReceiveOnSatchelExploded` with `false` and what each does to
`SatchelPlacementSiteInstance` (neutralize, destroy, hide, disable interaction). The caller address is in the host
log row `card098: host ... ReceiveOnSatchelExploded(0) on BP_SatchelBuilding_Flakbug_C_0 ..., caller exe+0x...`
(host t about 82434564; host log after the playtest). From the code alone: the `BP_SatchelBuilding_Flakbug` and
`BP_SatchelBuilding_Base` graphs.

## Fix

A dead satchel building has no live placement site on any machine, whatever killed it. On every applied `S` row
(values 0, 1 and 2) the client ends with the site in the host's state: neutralized, not visible, no interaction
prompt. Do it by the site's own function (`NeutralizeSite` or the one step 1 names), not by hiding components.
The journal replay for a late joiner does the same.

- Row: `card292: client t=<ms> <building> site <name>: <function> run; visible <0|1>, interaction <0|1>` on every
  `S` row; `WARN card292` when the site is still visible or interactive after it.

## Sibling sweep (same class: an actor owned by a destroyed building or bug that outlives it on the client)

| Item | Check |
|---|---|
| `BP_SatchelBuilding_Plasmabug_C` | same row path, site state after each kill path |
| `BP_SatchelBuilding_Spawner_Base_C` (bug holes) | same |
| `BP_SatchelBuilding_CTF_C` and `BP_SatchelPlacementSite_Flag_C` | the earlier `card132` mismatch `SatchelSiteCAC collision host 1 client 0` (C1M1 rounds 26, 27): same class or not |
| Satchel placed on a building that dies by another cause | the placed charge and its timer on the client |
| Objective markers and HUD icons on a dead satchel target | removed with the host's |
| Digest (card 132) | add the site's visible and interaction state to the compared slots, so this class shows as a mismatch |

## Predictions (one run, after the playtest, batched)

- Host kills a C2M1 flak bug by each path step 1 lists: client `card292` row with `visible 0, interaction 0` for
  each; receiver-side screenshot of the dead bug shows no placement box and no satchel prompt.
- Digest round after: 0 mismatches, with the site slots compared.

## Step 1 answers (static, junior, 2026-11-13)

- No Blueprint graph (BP_SatchelBuilding_Base, _Flakbug, _Plasmabug, _Spawner_Base) calls `ReceiveOnSatchelExploded`.
  Its only caller is native `ASTSatchelBuildingBase::OnSatchelExploded` 0x051650A0, a UFunction bound to the site's
  `ASTSatchelPlacementSite::SatchelExplodedEvent`. The damage kill (value 2) is the Blueprint's own path (card 163).
- The event is broadcast by `ASTSatchelPlacementSite::Explode(bool premature)` 0x051567E0. Its callers:
  `TriggerSatchel` 0x05174EC0 (the charge timer: premature false, row value 0) and `PerformPrematureExplosion`
  0x05166F90 (premature true, value 1).
- What Explode does to the site before the broadcast: `SetVisibility(site+0x468 mesh, false, propagate)`,
  `SetIntensity(site+0x480 light, site+0x48C)`, `byte site+0x50E = 1`, clears the timer handle at site+0x500, then
  ApplyDamageAtLocation. That is the same state change as `NeutralizeSite` 0x05161300 (mesh hidden, light dimmed,
  +0x50E = 1, timer cleared). `ResetSatchelSite_Implementation` 0x0516C480 is the inverse (+0x50E = 0, +0x50C = 0).
- So on the host the site turns itself off inside Explode, in native code the client never runs; the client gets
  only `ReceiveOnSatchelExploded`. A value 0 row (this playtest) leaves the site live on the client.
- `CanInteract` 0x051503D0 tests bytes +0x10C/+0x10D/+0x10E of its `this`; whether that object is the site and how it
  reaches +0x50E is not confirmed. The row's `interaction` reads +0x50E == 0 (the byte both Explode and NeutralizeSite
  set); open point below.

## As coded (uncompiled, 2026-11-13; diff `S/c292.diff`)

- `world_c98.cpp` `c98_client_satchel`: `c163_client_site` (NeutralizeSite by ProcessEvent) now runs for every applied
  'S' row, values 0, 1 and 2, before `ReceiveOnSatchelExploded`. The journal replay goes through the same function.
- New `c292_site_line`: `card292: client t=<ms> <building> site <name>: <what ran>; visible <0|1>, interaction <0|1>`
  on every applied row; `WARN card292` prefix when visible or interaction is 1. Visible reads bVisible (+0x1A0 bit 5)
  of the site mesh +0x468; interaction is +0x50E == 0.
- Digest: `kC132Ext` row 9 `ASTSatchelPlacementSite`, field `neutralized` (+0x50E); `kC132ExtClasses` 9 -> 10;
  StaticClass registered in `hooks_ubw.cpp`. A site left live on the client shows as `neutralized` host 1 client 0.

## Sibling sweep answers

| Item | Answer |
|---|---|
| `BP_SatchelBuilding_Plasmabug_C` | Same base class, same site, same Explode path and 'S' row: covered by the fix. |
| `BP_SatchelBuilding_Spawner_Base_C` (bug holes) | Same: covered. |
| `BP_SatchelBuilding_CTF_C` / `BP_SatchelPlacementSite_Flag_C` | The C1M1 `SatchelSiteCAC collision host 1 client 0` is the child actor's collision, not the site state: a different field. The new digest row now also compares its `neutralized` byte. Same class only if that row shows a mismatch. |
| Satchel placed on a building that dies by another cause | The host's charge timer lives in the site (+0x500). A building killed by damage runs NeutralizeSite (clears the timer); the client mirrors it now on every row. A placed charge actor itself is not covered statically; open. |
| Objective markers and HUD icons | Driven by the building's Become Destroyed / Killed by Damage (Blueprint), which the client already runs; not changed. |
| Digest (card 132) | Added (above). The mesh visibility is not a separate slot: both setters write it together with +0x50E. |

## Open points

- `interaction` is read from +0x50E, not from `CanInteract`; confirm in the run that the prompt is gone with +0x50E = 1.
- Digest: the site is a template child actor (`_CAT_`); confirm the ext row is filled for child-actor keys.

## Closed (2026-11-17 11:43)

b182 `d3a6c76b270110c3`, run c423b182a: client row `card292: client t=575768478 BP_SatchelBuilding_Flakbug_C site ...: site NeutralizeSite run; visible 0, interaction 0`; client shot `client-c423b182a-c292-dead-flak-client.png` (senior viewed): the dead flak bug with no placement box and no satchel prompt, objective struck out. 203 such rows in the logs since 2026-11-16, all `visible 0, interaction 0`, 0 `WARN card292`, digest 0 mismatches. Reopen on a `WARN card292` row or a site slot digest mismatch.
