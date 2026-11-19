# 459 Client mech fuel shows the init value, not the host's

## Facts
- `STPlayerMechAttributeSet` has no replicated attributes (client `card020` row: CPF_Net 0 of N).
- The client re-init (card 298, b160) sets Fuel to the init value 100.
- Run `c447b160a`: host `card298 ... host enter` at the second mech reads Fuel 83.00/83.00; the client would show 100.

## Not known
- Whether the client's own prediction changes Fuel while it drives (fast move, jump jets), or the value stays at 100.
- Which HUD element reads it, and whether the host refuses a jet/fast move at 0 while the client HUD shows fuel.

## Next step
- Diagnostic first: client row of Fuel base/current once per second while piloting, next to the host's value for the
  same mech, in one c447 run that burns fuel (hold fast move 10 s).
- Then a host send: Fuel and MaximumFuel for each piloted mech, on change, quantised, on the existing attribute
  mirror path if one fits (check card 444 ammo mirror first).

## Done when
- Client and host Fuel rows agree within one send interval during a fuel burn and after a remount, in one run.

## Senior note (2026-11-16 19:55)
- Run `c447b161a` (b161): client Fuel after the second re-init 100, host 77.
- The mod has no fuel send today: the only Fuel reads are the card 298 rows (attributes.cpp:1487-1499). No ammo
  mirror code carries a `c444` tag either, so there is no ready path to reuse; the send is new work.
- Diagnostic rows ordered in b162 (item 4). Design the send after reading them.

## Diagnostic result and fix order (2026-11-16 20:05, senior)

Run `c447b162a` (b162): host `card459` rows drain 1.00 per second on both mechs; the client's rows stay `Fuel
100.00/100.00` for the whole mount. The client's own mech never gets the host's value.

Fix ordered for b163: the host's once-per-second step sends Fuel and MaximumFuel to the mech's own client when either
changed since the last send (reset on a new pawn or PC, so a mount and a rejoin send at once); the client writes them
into its own `STPlayerMechAttributeSet` through the attribute write path so the HUD delegate fires.

Prediction (`c447b163a`): client rows follow the host's pc 1 rows within 1.00 at every second of both mounts; the
second mount starts at the host's value, not 100.

## Closed 2026-11-16 22:30

run `c447b164a` (b164 `762d8c2f6aae855c`): receive applied 1, the client read-back equals the host value on both mounts; one tick of 12 to 17 ms before the first frame stays (not visible). Guard path unexercised, playtest watch.
