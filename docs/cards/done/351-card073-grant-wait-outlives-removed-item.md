# 351 The card 073 grant wait warns when the granted item was removed in the same tick

Run `b37` client: `card073: client t=338569845 grant GA_IncendiaryGrenadeThrowingAbility_C waits for its replicated
spec`, then `WARN card073 ... no replicated spec after 10000 ms, input not bound` at 338579847. At
CaptureTheFlag_SO1 complete (338569830) the level script grants the ability (handle 237) and removes
`BP_GrenadeItem_Incendiary_C` from the client's server copy in the same tick, so no spec ever replicates.

Fix (not written): the grant wait (`death.cpp:3442`) ends without a WARN when its item is gone from the inventory;
one plain row. First confirm on the host log that the spec was removed, not lost. Prediction: 0 `WARN card073` in
the same scenario, the plain row once.

## Result (b38 `24c5e7454353752f`, run `b38r2`, 2026-11-14): accepted

Host (`UBWCOOP_C73_LOG=1`, t=339968925): the Frag removal row shows ActivatableAbilities 17, the Incendiary removal
row 16, so the spec was removed in that tick. Client: grant waits at 339968937, `its item left the inventory after
6 ms, wait ended` at 339968943. 0 `WARN card073` in `b38r2`. Run `b38` had 1 `WARN card073` from another path:
card 353.
