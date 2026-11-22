# Card 552: no bullet holes on shot training course targets

Playtest 2026-11-20 on b366 (`e62e76f63a05d88f`), training map shooting range. User report with screenshot: "don't
see holes in dead training course targets". The screenshot shows the green trooper targets standing with no hole
decals.

## Likely cause (static, not yet proven)

`decal_c132.cpp` syncs only gore decals (material name holds "Gore"). Every other decal, bullet holes included,
"stays local on each side". The host's shots on the client are replayed as effects (card 136 effect rows) but the
client does not trace them, so it never spawns the hole decal for a host hit. If the client shot the targets itself,
the cause is different: the client's own hit decal on a target the host owns (or an attach target that does not
resolve on the client).

Open question for the user: who shot those targets, the host, the client, or both?

## Next step (after the playtest)

Trace what spawns a hole on a range target in single player (SpawnDecalAttached caller, the target component), then
extend the card 132 decal journal from gore to every decal a player's hit spawns, by class. Sibling sweep: every
other non-gore hit decal (walls, ground, props) the host spawns.

## Prediction

After the fix, a client card132 decal row exists for each host hit on a target, and the client sees the same holes
as the host.

## Host log (`ultimate-bug-war-coop-public/logs/UBWCoop-menu-20261120-184651.log`, host t = client t + 241024698)

- The host shot every training target (`shot h` rows, host FireProjectile, card098 kills relayed). This answers the
  open question: the host.
- The host spawned `MI_FX_BulletGround_Decal` on the target's Box component, non-null (3 sampled trace rows).
- The client log has 0 SpawnDecal rows on `TargetRangeTarget`.

Cause confirmed: non-gore decals are not synced, and the client does not trace the host's shots. The fix direction
above stands.

## b369 (`4d7adc330945a079`, build-b5 copy kept), not run yet

Fix: the host's hole decals are drawn inside its impact events (the projectile's cue, caller exe+0x524F1F7), so
only the outer event was sent. The client's replay of a cue has the location, normal and physical material but no hit
component, so it drew no hole. `c136_net_nested_decal` (`fx_c136_net.cpp`) now sends every non-gore decal drawn inside
a queued event as its own DecalAt/DecalAttached row, with the outer event's per-PC filter, so a client never gets a
second copy of its own shot's hole. On the client, `c136_fx_enter` blocks any decal drawn while a host row plays
(`inner_decal`). The class covers every non-gore decal drawn inside an effect event: holes on targets, walls, ground,
props and NPC sprite colliders. Proof row: `card552: host ... sent as its own row`, and client decal rows on
`TargetRangeTarget`.

### Run c551b370a predictions (was c552b369a, now on b370) (junior, 2026-11-21)
Scenario `tools/scenarios/c369-c1m1.txt` (the C1M1 soak for b369, no death round, plus a host block before the
ShootingRange_SO3 recipe: the host warps to (5446, 13305) beside the client's firing line and fires at target C_1).
- Host: at least one `card552: host t=... decal MI_FX_BulletGround_Decal on BP_KillableBuilding_Base_TargetRangeTarget_C
  inside event N sent as its own row` in the host fire window.
- Client: a `card132: client gore trace SpawnDecalAttached N: ... of BP_KillableBuilding_Base_TargetRangeTarget_C` row
  after the host fire and before the client's own first range shot; shot `c369-host-range-t1` shows the target.
- The client's own range shots draw no second hole: no card552 host row for an event the client fired (per-PC filter).

## Closed 2026-11-21: passes in c551b380a

Since b372 the card 132 relay carries every host decal, which replaced this card's nested path. In c551b380a the host
fired at the range targets (TargetRangeTarger); its `MI_FX_BulletGround_Decal` decals were relayed and the client
spawned them (client log: 135 spawned, 0 bad rows, 0 round mismatches). Shots
`host-c551b380a-c373-566-both-holes-host.png` and `client-c551b380a-c373-566-both-holes-client.png` show the holes
on the target wall on both screens. Logs `logs/c551b380a-host.log`, `logs/c551b380a-client.log`.
