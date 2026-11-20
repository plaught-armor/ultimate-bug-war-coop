# 506: A dead AA turret that comes back into net range is hidden for 9 s on the client, then dies again

Opened 2026-11-19 by the senior, from a static trace of c243b273a and c243b271c (FED_C3M2).

## Finding

- BP_AI_AATurret_C is killed on the host by AI Morita rounds. c243b271c shows the client reading Health -20.0, so it
  is a real death and not an unreplicated default. The host logs no pose row (no ragdoll) and no card269 rest row,
  and it logs no WARN card269 "not added" either.
- The client copy leaves relevancy (card434 reason 3) and comes back dead. `c38_death_edge` (corpse) then calls
  `c132_pose_arrival`, which hides the mesh and waits for a host pose or rest row. None comes, so after
  kArrivalWaitMs (about 9 s) `arrival_tick` shows the corpse and calls `c38_corpse_death_play`. The player sees no
  turret for 9 s, then watches its death play a second time. This is the hb70 "corpses die again" bug, for this
  class.
- It happened twice in the last 40 client logs, both AA turrets: guid 0xa1c in c243b273a and guid 0x728 in
  c243b271c.
- Likely cause (to confirm with one log row): `c269_state_host` runs only from ReceiveBaseStateChanged to Dead.
  The turret's death does not seem to reach base state Dead on the host: the client copy arrived with replicated
  base state 0, and the client set 3 itself through HandleDying. `rest_host_step` would also drop a row whose base
  state is not Dead.

## Do

1. Host: add a rest row for every 3D NPC that dies with no pose row, keyed on the death (`c25_dead` == 1 at the
   health edge or at the c269_death_note call), not only on base state Dead. Use the death note's name, or kRestBare
   when it has none.
2. `rest_host_step`: keep a row while the NPC is dead by health or base state. Drop it only when the NPC is revived
   (health above 0 and base state not Dead) or gone.
3. One host row per added row, with the class, the base state at the add, and the path (state, health), so the next
   run shows which path the turret takes.

## Done when

- On the next run that carries a bug batch and kills an AA turret: the host logs a card269 rest row for
  BP_AI_AATurret_C. A client arrival of that corpse logs "card269/card207 rest arrival" with no hold and no
  `WARN card137 ... not posed after`.
- Class check: the same run shows no `not posed after` WARN for any class.

## Closed 2026-11-19 (senior)

Proved on b280 c014bee01ec0e705, run c492pb280a (FED_C3M2 plant route, three games).
- Host: `cmd: 74 kill: InstaKillTarget(BP_AI_AATurret_C) guid 0xa54` at 752394710, then `card506: host rest row added by health edge ... base state 3, health 0.0` at 752399764. This confirms the cause: a turret death reaches base state 3, not Dead (4), so only the health-edge path catches it. The row came 5.1 s after the kill, which is inside the client's 9 s arrival wait.
- Both clients, on relevancy return: `card131 corpse first seen dead ... no death play`, then `card207: client rest arrival ... bare: shown as it is` after 7 ms (client) and 6 ms (client2). No hold and no replayed death.
- "not posed after": 0 on all three logs. EXCEPTION/FATAL: 0.
- Side notes, not bugs: `card486 ... ADSkeletalAnimatorComponent, swap back skipped` (1 per client) is a correct skip. The card269 rest log cap (kRestLines 60) was spent before the kill, so the host "rest row sent" line was suppressed; this is a logging matter only.
