# 353 Client grenade count one low: the host's count frame and the client's own consume both apply

Run `b38` client: the host's GrenadeItem frame `Frag 1` arrived at 339452912 (`our count 2 to 1`), 5 ms before the
client's own launch (339452917). The local consume then took 1 to 0, the game removed the Frag item and equipped
the Incendiary (grant waits at 339453013). The host's copy still held Frag 1 and never equipped Incendiary: no spec,
`WARN card073` at 339463017, grenade input not bound until the next host frame. Proof of the one-low count: `Frag 0:
our count 0 to 0` at 339505586 where the host went 1 to 0. In `b38r2` the order was the usual one (local consume at
339916022, host frame `1 to 1` at 339916038).

Cause: two writers of the client's own count, the host's absolute frame and the local consume
(`RemoveItemByDefinition`, caller exe+0x5163343). The host is the authority.

Fix (not written; trace first): in a client session the local consume of a thrown grenade is not run, and the host's
frame is the only writer. Trace what else that caller does (equip next, HUD) and whether card 286 depends on the
local consume. Sibling sweep before coding: every own-inventory count the client lowers locally while the host also
sends it (tactical support charges, satchel, ammo mirror).

Prediction: at every throw the client's count changes once, by the host frame; 0 `our count N to N` rows with N
differing from the host's previous value by 2; 0 `WARN card073` at the grenade range in 2 runs.

## Result (b39 `f6cfe3ce7f7ad7a8`, run `b39`, scenario `b39-c353-c1m1`, 2026-11-14): accepted

- Fix (`mod/src/ubw/grenades.cpp`, `c353_skip_local` at the top of `detour_c73_remove`): in a client session a
  `RemoveItemByDefinition` on our own pawn's inventory is not run unless `c73_apply` (the host frame) makes it. The
  skipped call returns the item still held.
- Client log: 3 `card353` skip rows (Frag and Incendiary throw, caller exe+0x5163343; satchel, caller
  exe+0x5163EBB), each followed 16 to 32 ms later by the frame row `our count N to N-1, writer the host frame`. All 4
  removals that ran have caller exe+0x0 (`c73_apply`): 0 local-consume writes. 0 `WARN card073`, 0 new WARN kinds.
- The race order (frame before the local consume) did not recur in this run; the proof is the writer rows.
- Sibling sweep: tactical support charges have the host as only writer; ammo is client-written consumption plus
  host-sent gains, no pair of writers. Item adds: card 354.
- Senior review, call sites of `RemoveItemByDefinition` (RVA 0x0516B960) in the exe, 5 direct calls: the 2 traced
  ones; 0x507381B (the blueprint exec thunk, stores the result as the return value); 0x5150DE3 (discards the result,
  then returns the drop in the item's quantity at +0x38, so a skipped call reports 0 consumed); 0x51637D1 (discards
  the result). None loops on the result. The last 3 are not traced to a game path; senior edit after b39 (unbuilt,
  rides b40): a skip from any caller but the 2 traced ones logs as `WARN card353 ... caller not traced`.
- Left as it is: the client's HUD count trails its throw by the round trip (17 ms here).
