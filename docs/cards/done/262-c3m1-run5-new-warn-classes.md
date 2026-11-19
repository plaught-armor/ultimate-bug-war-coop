# 262 New WARN classes from the FED_C3M1 soak

Found by card 242 run 5 (`logs/c242-c3m1-*`), none in run 4. None stopped play. Step 1 (static, no run):
for each class state the path, whether a player sees a difference, and fix or close with evidence.

| Side | Row | Count |
|---|---|---|
| host | `card132: OnHit of ADProjectile, whose GrenadeExplode already went` | 9 |
| host | `card130: host fx watch full (64)` | 1 |
| client | card145/card104 `no copy of host 245419` (`BP_GrenadeAmmoPickup_Incendiary`) | 8 |
| client | `card132: client decal pool at its cap 500` | 1 |
| client | card140 pose freeze check (179.5 uu one tick later) | 1 |
| host | `card049: host sent AddSubObjective(Satcom_O1, Satcom_SO2)` row missing although frame 68505 went out and the client replayed it (`c49_emit`, `mod/src/ubw/objectives.cpp`); log gap only | 1 of 28 Adds |

Table sizes (fx watch 64, decal pool 500) follow the STATE rule on table peaks: raise from the measured
peak, not by guess. After the playtest.

Missing `host sent Add` row, settled statically (junior, 2026-11-13): not a send fault. `c49_emit` logs the sent row
only when `c49_kind_line(kind)` holds; Complete/CompleteAny/CompleteSub/Increment always log, every other kind takes
`c49_line()` = `note_event_cap(g_c49m.lines, kC49MirrorLines = 240, ...)` (objectives.cpp:193, 214-222). The host hit
that cap at t=203538438 (`diag: card049 mirror ... further events not logged`, host log line 236211, the Satcom
SetObjectiveVisibility burst), so every later Add/Visibility sent row is suppressed (after it: 1 sent row, a
CompleteSubObjective); `c49_send` returned 1 (frame 68505 out, client replayed it). A line budget, no data loss.

## Step 1 answers for the five open rows (code plus run 6 logs, 2026-11-13)

The run 5 logs are gone (run 6 wrote the same file names), so each row is judged by its code path and by
whether run 6 (`logs/c242-c3m1-*`, same route, 04:58) repeats it.

| Row | Path | Player-visible | Run 6 | Verdict |
|---|---|---|---|---|
| `OnHit of ADProjectile, whose GrenadeExplode already went` | `grenades.cpp:1544`: a second hit of a projectile whose explode frame was sent; WARN only when the repeat captured an effect (card 188) | Only if the repeat spawns an effect | 4 or more rows, all `effects system no sound no zone no`, so notes, not WARN | Close: no effect lost |
| `card130: host fx watch full (64)` | `fx_watch.cpp:71`: the diagnostic watch table, not a sync path | No | Peak 39 of 64, no full row | Close; table size under the STATE rule on peaks |
| `no copy of host 245419` (incendiary grenade pickup) | card 104/145 client lookup | A pickup the host sees and the client does not | 0 rows | Not reproduced; reopen with logs if it returns |
| `client decal pool at its cap 500` | `decal_c132.cpp:1091`: pool full of host copies, the mod grows the cap by one | No: the growth is the fix | 1 row (500 to 501) | Close; lower the row from WARN to a note when the cap is the game's own and growth worked (batch with the next decal change) |
| card140 pose freeze check 179.5 uu | `pose_c132.cpp:1229` | A corpse that jumps one tick after the freeze | 239 freezes, worst 0.0 uu, 0 over 2 uu | Not reproduced; reopen with logs if it returns |

Run 6 WARN classes with their owners: `BP_SplineRiderMonorail_C` location (card 261), `card125: client turret
row ... no turret matches` (card 271 class), `card133: wire name ... does not fit` (card 230).

2026-11-16 senior: the pose freeze row returned on b121 (c418b121a, guids 0xdc and 0xe6, 296 and 345 uu). Cause and fix are in card 424 (held final with gravity on); this card closes with 424.

## Closed 2026-11-16 (senior)

Every row has its verdict above and an owner: the OnHit row is card 193 (closed), the pose freeze row is card 424
(accepted on b131), the decal cap row is known noise (STATE), `fx watch full` and `no copy of host` did not return
(0 rows in `c423b145a` and the c333 C2M1 soak). Nothing left to do under this card.
