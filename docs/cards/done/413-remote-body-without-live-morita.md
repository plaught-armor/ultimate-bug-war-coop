# Card 413: remote player body without a Morita NPC in play

Status: accepted 2026-11-16. Class: full world sync (player body) and card 330. Build b107 `86e7f6eefa55c9b9`.

## Problem

`c28_try_attach` took the body template (sprite renderer, animator) from the first `BP_AI_Trooper_Morita_C` actor
in the world and retried each second until one existed. With no Morita in play the teammate had no visible body:
about 30 s at the start of FED_C2M1 on the host in playb72 (same 16 capped `Morita null` rows in playb70, b71,
b72b, b72c), and the whole map on FED_C3M1, C4M2, C5M1 and C5M2 on the client in the fast chain. Each failed retry
cost 1 to 2.5 ms, and it loaded the animation set synchronously before looking at the template (140.7 and 591.3 ms
`c28_step` calls on the client in the transition map, run `c411b105a`).

## Change (`mod/src/ubw/remote_body.cpp`)

- With no live Morita, the template is the class default object's two default subobjects, found by
  `c413_ref_of_class` (a class default object has no owned-component list). Probe run `c413b106a`
  (b106 `e5a9c77af196f7d0`): on every mission map, both sides, the default renderer has the live one's offset
  (0, 0, 14.3), scale (1.000, 2.286, 2.286) and `FowardFacingQuad` mesh; its 56 logged properties differ from a live
  one's only in three set at run time (`DisplayMaterial`, `DisplayMaterialInstanceDynamic`, `PlayerController`).
- A live Morita is still preferred (unchanged path).
- `c29_load_set` is asked only once a template exists.

## Proof (run `c413b107a`, fast chain, passed, 22 rows `mismatches 0` on both sides, no new WARN class)

- Attached on all six mission maps on both sides: host 3 from the class default, 3 from a live Morita; client 4
  and 2. No `Morita null` row. "3 settings copied" from the default, 4 from a live one (the fourth is the live
  `PlayerController` reference, which the new component sets itself).
- Receiver-side shots on FED_C3M1 (no Morita in play): `host-c413b107a-c413-c3m1-h-body.png` and
  `client-c413b107a-c413-c3m1-c-body.png` each show the teammate's sprite body under the name plate.
- Client `c28_step` in the transition map: 111.4 and 131.7 ms single calls (b106) down to 0.1 ms total. Mission
  maps: 5.3 to 7.8 ms total per map on the client, 8.3 to 12.7 ms on the host.
- Seen again, not from this card: `WARN card072: host: AmmoMirror from BP_PlayerController_C with no server copy
  inventory`, once, also in runs c401b93b, c401b93c, c404b95b, c413b106a. Not carded yet.
