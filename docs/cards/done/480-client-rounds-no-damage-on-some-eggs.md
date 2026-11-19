# Card 480: Client rounds on some Sewers eggs apply no damage

Opened 2026-11-17 17:35 from run `c245b194a` (b194 `a119f90574be9a2d`, C4M2 Sewers). Not a crash and not a play blocker yet, because the host can still shoot the eggs. It is carded, not chased.

## Seen
- The client fired one VeteranRifle round per press at eggs. A killed egg logs Health 200.0 -> -50.0 from one round.
- 22 eggs had an ok look and a fired round on the host's exact target point, but no Health row: no damage.
- BP_DestructibleEggs_GlowingEmissive_Base_C: 0 of 7 killed. The other egg classes were mixed.
- Not known: whether a host round on the same egg does damage. If it does, this is a co-op bug in the client's hit claim path (card 281 made rounds) for these egg classes. If it does not, the class takes other damage only, and this is a test fact, not a bug.

## Do
1. One discriminator: host and client each fire at the same GlowingEmissive egg, from one stand with line of sight. Log the hit claim, the made round and the damage row on the host.
2. If only the client fails: trace the client's hit claim for a destructible egg (target match, component, damage type) and fix it at the cause, for the whole destructible class.

## Done when
Client and host rounds each kill a GlowingEmissive egg in one run, or the trace shows the class is immune to rifle damage on both sides.

## b195 prediction (written before run c245b194b)
Static and log reading of c245b194a points to no bug:
- `c98_host_kill` (mod/src/ubw/world_c98.cpp) logs a "Health X -> Y: kill relayed" row only on the lethal change (old > 0, new <= 0). A round that does not kill leaves no row. "No Health row" therefore means "not killed", not "no damage".
- The client round at BP_DestructibleEggs_GlowingEmissive_Base_C_0 in b194: the host server copy logs `MS_WPN_BulletImpact_Flesh` at the client's hit point (1316, -1226, 913) and `card157: OnDamageCaused ... (server copy) damage 250.00` 8 ms after the round. The round hit and dealt 250.
- The parent class BP_DestructibleEggs_Base_C logged "Health 500.0 -> -500.0". GlowingEmissive_Base is very likely a 500-Health child, so one 250 round never kills it. The scenario fired one round per egg, so 0 of 7 died.

Discriminator in c245b194b, on BP_DestructibleEggs_GlowingEmissive_Base_C_0: the client fires one round, then the host fires one round at the same egg.
- Predicted: after the client round, an OnDamageCaused 250 row and no kill row. After the host round: `card098: host ... BP_DestructibleEggs_GlowingEmissive_Base_C_0 Health 250.0 -> 0.0: kill relayed`. The 250 start proves the client round counted.
- No kill row after the host round (500 -> 250 is not lethal and prints nothing) means the client round did no damage, and the prediction is wrong.

## b195 result (run c245b194b, b194 `a119f90574be9a2d`)
- Client round on BP_DestructibleEggs_GlowingEmissive_Base_C_0: client `HitClaim shot 1 miss ... at (1263.4, -1141.1, 858.1)`; host server copy fires the round, `MS_WPN_BulletImpact_Flesh` at that point, `OnDamageCaused ... (server copy) damage 250.00`. No kill row (not lethal).
- Host round (MoritaMk1) on the same egg: `card098: host ... BP_DestructibleEggs_GlowingEmissive_Base_C_0 Health 10.0 -> -30.0: kill relayed`, `OnDamageCaused ... (host own) damage 40.00`. The client copy logged the K row and OnDeath 6 ms later.
- The other GlowingEmissive_Base eggs, killed by code in the same run, read `Health 500.0 -> 0.0`. Max Health is 500.
- Verdict: the client round did damage (500 down to 10 before the host round). One VeteranRifle round never kills a 500-Health egg, and b194 fired one round per egg, so all 7 Base eggs survived. This is a test fact, not a co-op bug. The prediction was right on the claim but wrong on the number: the egg sat at 10 Health, not 250. The client round took 490 off, against 250 reported caused; the egg's own damage modifier is not traced.
- Not covered: the other 15 no-kill eggs of b194 were other classes. Their kill rows also print only on the lethal round, so the same reading (not killed, not undamaged) applies unless a 2-round test shows otherwise.

## Closed (2026-11-17 18:04)

Not a bug; a test fact. The b194 run fired one round per egg, and BP_DestructibleEggs_GlowingEmissive_Base_C has 500 Health (code kills log `500.0 -> 0.0`), so one VeteranRifle round can never kill it. "No Health row" meant "not killed": c98_host_kill logs only the lethal change. c245b194b discriminator on `_C_0`: the client's round showed one server-copy OnDamageCaused of 250.00 at host 597907793. card181 for that press: "client rounds 1, copy rounds 1, refused 1, late-fired 1": the copy fired, and the hit claim was refused, so the damage was not applied twice. The host's MoritaMk1 round then killed the egg (`Health 10.0 -> -30.0: kill relayed`), and the client copy logged its K row 6 ms later.

Not done: 250 caused against about 490 lost is not explained. One damage event was logged, and earlier damage to that egg in the run is not logged, so this is not evidence of a double apply.

Reopen when: one client round logs two damage events, or a refused claim is applied as well, on any target.
