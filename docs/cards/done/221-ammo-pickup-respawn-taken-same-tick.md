# Card 221: A pickup that respawns and is taken in the same tick stays visible on the client

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: queued 2026-11-12, from c171-r1 (new WARN class). Static first. Budget after that: three runs.

## Seen

c171-r1 host, `BP_AmmoPickup_MoritaSmall_C_0`: at 131024211 two card098 world E rows in the same ms, "... 1" then
"... 0", and one card095 collision row "PlayerCollider 1 -> 0". Client digest round 8 at 131039666: "pickup host 0
client 1" and "mesh visible host 0 client 1". Round 9 is clean only because the host's next respawn (131054223)
set 1 again. For about 30 s the client showed a pickup the host did not have.

## Do (static, each line marked verified or open)

1. What the client does with two E rows for one actor in one packet or tick: applied in order, merged, or the
   second dropped as a duplicate. Read the card 145 E row send and apply path.
2. Whether the client's own pickup code (respawn timer, overlap with the host's copy pawn) sets 1 after the
   host's 0.
3. Class rule: the fix covers every row class that can change twice in a tick (last value wins, in send order),
   not this pickup class.

## Done when

- Cause named from the code and the c171-r1 client log; one run where a player stands on a respawning pickup
  through two respawns: 0 pickup mismatches in every digest round. Ends with "Report complete."

## Static answers (c171-r1 logs plus source, senior, 2026-11-13)

1. Verified: both E rows were applied in order (client 131024218 `... 1: applied`, 131024219 `... 0: applied`).
   No merge, no drop.
2. Verified: the client's own respawn set it back. 131024221, 2 ms after the host's 0: `card137: client change
   ... component collision 0 -> 1 by local sim caller exe+0x4C618A1` (inside `AADPickupBase::OnUpdatePickupStatus`
   0x4C61870). The client's pickup runs its own respawn timer, armed when the earlier E 0 row disabled it. On
   the host the respawn was taken at once by the pawn standing on it; on the client the take is blocked (card
   254, `SFX_Pickup_Negative` at 131024221), so the pickup stays enabled and visible until the host's next row.
   The comment at `weapons.cpp:1160` already names this client respawn. Without a standing player the client's
   timer and the host's row give the same state, which hid the fault.
3. Class rule: on the client the host's E rows are the only source of `bEnabled` for a relayed pickup. The
   order of rows in one tick is not the fault.

## Fix spec (build 4)

- `world_c98.cpp` `c145_client_enable`: set a file-scope flag around the `g_c145_set_enabled` call (host row
  being applied).
- `weapons.cpp` `detour_c145_update_status`, client only, before the original: when the flag is clear, the
  pickup is in the scope of `c50_client_pickup_blocked` (placed, or a card 104 host copy) and `bEnabled` reads 1,
  write the byte +0x2A8 back to 0 and log `card221: client pickup <name> local enable set back (only the host
  enables; N so far)`. The original then runs with 0 and hides it. Pickups outside that scope are untouched.
- Do not touch the host path or the nested-take second run.
- Attack before coding: a local enable at BeginPlay or after a save load must not be set back before the
  host's snapshot row (`c145_load_actor`, `c145_send_copy_enable(live, true)`); if the client's own BeginPlay
  enable comes first, exempt calls before the first host row for that pickup and report it.
- Predicted rows (rides in the next run with a respawning ammo pickup, no extra run): one `card221` row per
  client-side respawn, each followed by the host's `world E ... 1` applied; 0 `pickup host 0 client 1` digest rows.

## Closed (2026-11-17 11:11)

The predicted rows are there: 7 `card221: client pickup ... local enable set back` rows in 7 logs and 0 `pickup host 0 client 1` digest rows. Reopen on a `pickup host 0 client 1` digest row. Evidence base: the 202 host and client logs of builds b150 to b180 written since 2026-11-16.
