# 347 The host throws the client's grenade along a stale view

## Logged (run 20, `logs/c335-range-{host,client}.log`, b29)

Client look -19.4 at t=331435352, -5.4 at t=331435651, G at 331436251, throw at 331437350. The host's server copy
threw at aim P -19.43 Y -42.32: velocity (2148,-1956,-748) against the client's own (2218,-2020,-20). The grenade
landed 290 uu short, SO3 stayed 10/10. The same step on b28 threw at -5.50 on the host.

## Cause (static, lane 1 exe)

`OnDoAction_Implementation` aims with the PlayerController's `GetPlayerViewPoint`. On the host that is the camera
cache of the client's copy, written only by `ServerUpdateCamera_Implementation` (RVA 0x0405DCB0; needs camera manager
flag 0x100 at +0x27A8, then sets the cache POV from the RPC's position and packed pitch and yaw).

The client sends that RPC from `APlayerCameraManager::UpdateCamera` (RVA 0x04065FB0), only when:

- flag 0x400 at +0x27A8 is set and the net mode is 3, and
- the camera moved more than the position limit since the last frame, or the time since the last send (+0x27C4) is
  over the game network manager's delta time, and then
- the packed pitch and yaw differ from the last frame's (0x144066229), or the position differs, or the time since
  the last send is over `ServerUpdateCameraTimeout` (+0x27C8, compared at 0x14406625B).

The comparison is against the previous frame, not against what the host last received. When the send on the last
frame of a turn is lost (the RPC is unreliable in the engine source as I remember it; not read in this exe) or held
back by the delta time, nothing resends the still view until the timeout. In run 20 the throw came 1699 ms after the
last look change, before a resend. ControlRotation is not involved: it reaches the host with ServerMove.

Class: every server copy ability that aims with the view point, not only grenades.

## Fix (written, uncompiled; senior)

`c347_client_step` (`mod/src/ubw/grenade_c158.cpp`, called from `c50_step` on the client, `death.cpp`): once per
camera manager, lowers +0x27C8 to 0.2 s (`kC347TimeoutS`) when the game's value is higher. A still view is then
resent 5 times a second, so the host's view is at most 0.2 s old plus one lost send. Row:
`card347: client camera manager <class> resend limit <was> s -> 0.200 s`. Reset per map in `c158_reset_map`.

Cost: 5 `ServerUpdateCamera` calls per second while the client's view does not change; none added while it turns.
Bytes per call to be read from the run (card 329 counters).

## Not settled

The client's own grenade flies its own path (it was 728 uu/s apart in Z here only because of the stale pitch). After
this fix the two launch lines should agree; if the paths still differ visibly, that is a new card.

## Prediction (`c335-range-load`, the grenade step of run 20)

- Client: one `card347` row per map with the game's old value (expected 2.000).
- Host `card158: host ... server copy throw` aim P within 0.5 of the client's last look pitch before the throw
  (-5.4), for every throw in the run.
- SO3 counts the kill the stale throw missed.
- 0 new WARN kinds.

## Result (b33 `100476d0085b4f43`, `c335-range-load` rerun, 2026-11-14): accepted

- Client `card347 ... resend limit 2.000 s -> 0.200 s` x4: two camera managers per FED_C1M1 load, about 0.45 s
  apart (335196755/335197227, 335420080/335420510). The prediction said one per map; the row is per camera manager.
- Host `card158 ... server copy throw ... aim P -5.32 Y -42.42`; client last look pitch -5.3. SO3 10/10.
- The first run of the scenario failed at line 271 (TacSupport_SO6: client `card021 ... handle 102: failed, spec
  present`, host no card105 activation); the rerun passed. Not this card; see STATE.
