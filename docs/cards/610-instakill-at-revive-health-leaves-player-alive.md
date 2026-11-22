# 610: InstaKillTarget at revive Health can leave a player alive
Status 2026-11-22: open, parked until after the user's playtest.

Open 2026-11-22 (junior, found by card 607 run c607b466a step 3). Parked until after the user's playtest.

## Seen

`logs/c607b466a-host.log` 7891-7902, client copy just revived to 30: `InstaKillTarget ... health 30.0 dead 0`, damage
30.00, then `health 0.0 dead 0`. No death event, bark or Downed row. The host's revive press found nobody downed. The
client's regen began 5 s later and the player went on normally. Seen once. The same call from Health 49 downed the
copy later in the run (30761-30774).

## What is known

- `dead 0` before the hit rules out a death flag left over from the earlier down.
- Disassembly: RefreshState (0x4A3E480) sets the IsDead byte (+0x148) to current Health <= 0, and the fatal event is
  sent only when that byte turns on (AttributeSetPostEffectApplied 0x4A3B230). If RefreshState ran for this hit, as
  that path does, Health was above zero and printed as 0.0 by `%.1f`. The raw value was not logged.
- InstaKillTarget (0x5241580) deals current Health plus current armour as self damage.

## Not known

- The losing step. Hypothesis only: float rounding of the revive Health (0.3f x MaxHealth).
- Whether b465 does it. It was not run. By code reading card 607 changes nothing on this path.
- Whether the real callers on players (out of bounds, hazard crush; cards 360, 361) can leave a player alive this way.

Next: trace the damage sum and the Health write statically, then log the raw Health bits on one kill from revive
Health. Done when such a kill downs or kills the player, or the real callers are shown unable to hit it.
