# Card 438: attached FX rows sent before the owner has a NetGUID

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: open. Design below; one static question first. Sibling of card 436 (Anim rows, fixed in b134).

## Finding (run c399b134a, b134, card 434 FX diagnostic rows)

99 host rows `card434: host FX <asset> (attached) owner has no NetGUID` in one chain run: 95 are
`NS_Compute_DamageAndRadius` (wound shading, card 329's heaviest FX asset: 757 rows, 19936 bytes) on WarriorBug,
ChariotBug, AphidBug and HopperBug, 4 are `MS_FLY_HopperBug_Flying_Flaps`. Every owner has RemoteRole 1: the effect is
spawned attached in the NPC's spawn frame, before the first replication gives the NPC a NetGUID, so `host_key` writes
guid 0 and the row goes out with the world key. The client draws it at the world point; it does not follow the NPC.
The journal keeps the same body, so a join replay repeats the error. Client C5M2 totals in the same run: 944 rows,
193 key misses, 230 target misses.

## Static question first (no run)

Does the client's own copy of the NPC spawn `NS_Compute_DamageAndRadius` in its BeginPlay or construction, and does the
client's local-spawn block (`c136_client_own`) drop it? Read the client `card136` rows for the asset on a bug spawn.
- The client spawns and keeps its own -> the host row is a duplicate: stop sending this asset for replicated NPC
  owners (bytes saved: up to 19936 per C2M1 pass) and no hold is needed for it.
- The client's own is blocked, or it never spawns -> wound shading is missing on the client for every bug that spawns
  while a client is connected; the hold below is the fix.

## Fix design (host hold, same shape as card 436)

In `c136_net_done`, for a spawn row with `p.attached`, key kind guid, `p.guid == 0` and a replicated owner: copy the
`Pending`, a weak ref to the owner and to `made`, and the seed into a held list (64 entries, 2000 ms); do not journal
or enqueue. Each `c136_step`, per held entry: owner guid now set -> write the guid, build the body, `journal_apply`,
`host_enqueue`; owner or component gone -> drop; 2000 ms passed or list full -> send with the world key as today.
A stop (Deactivate, DeactivateImmediate, destroy) for a held component drops the entry and sends no row.
Totals row at map end: `card438: host Fx held for a NetGUID N, sent by guid N (longest wait N ms), sent at the world
point after 2000 ms N, ended while held N, list full N`.

Order: other rows are not delayed (no head-of-line wait). The held effect starts up to about 320 ms late on the
client (card 436 measured waits of 47 to 318 ms), which is when its NPC copy appears.

## Done when (one chain run)

- 0 `card434: host FX ... (attached) owner has no NetGUID` rows for replicated owners (or the asset is no longer sent).
- `card438` totals: held = sent by guid + ended while held, `sent at the world point` 0, `list full` 0.
- Client C5M2 `misses: N key` drops by the held count against 193.
- Receiver-side screenshot: a WarriorBug that spawned in view, shot on the host, shows the wound on the client.

## 2026-11-16 13:35: static question answered (senior)

The client blocks its own: `card136: client fx block map totals: 1474 outermost calls blocked, 0 kept` and
`c136_client_own` keeps only the client's own pawn's effects, widget/2D ones, trails and card 408 attacks. So the
client's NPC copy does not spawn its own wound-shading system and the host's row lands at the world point: the second
branch holds and the host hold is the fix (it also covers the HopperBug flaps sound and any other attached effect
spawned in an NPC's first frame, by class). Senior writes the patch.

## 13:38: patch written

`<scratchpad>/c438_fx_hold_patch.py` (`--check` ok on the b134 tree; one file, `fx_c136_net.cpp`). Not built yet.

- `c438_want` at the card 434 diagnostic site in `c136_net_queue`: an attached spawn whose attach owner is the event
  owner, replicates, and has NetGUID 0 is marked for a hold; the card 434 FX row prints only for events not held.
- `c136_net_done` splits: `c438_op` (a stop of a held component ends the entry and sends nothing; a respawn on the same
  pooled component ends it; any other op naming it sends it first), `c438_hold` (64 entries; full = sent with the world
  key, counted), else `host_emit` (the old tail: journal update and the queue row, unchanged).
- `c438_step` in `c136_step` before `host_flush`: owner or component gone = ended; owner has a guid = `host_emit` with
  the guid key; 2000 ms = `host_emit` with the world key.
- `c438_report` in `c136_net_arm_map`: totals row, held rows dropped.

Review (attacked): filter pointers are dereferenced by `runtime_target` in `host_emit`, so a release needs the owner
weak live and the hold needs attach owner == event owner; name and asset row indices are per map and the hold is
cleared per map; a held row's id is older than rows sent after it (the client has no id ordering check); the stop of a
held row finds no journal row either way. Not covered: an attached spawn whose attach owner is not the event owner
(its card 434 row still prints, so the count shows).

## 2026-11-16 14:03: b135 run `c423b135b` (C2M1), prediction not met; step 2 patch written (senior)

Totals: `held for a NetGUID 494, sent by guid 99 (longest wait 1833 ms), sent at the world point after 2000 ms 132,
sent early for an op 6, ended while held 257, still held 0, list full 0`. Predicted `sent at the world point` 0: not
met, 132 owners had no NetGUID after 2000 ms (not relevant to the client: the card 434 rows show hidden NPCs with
NetDormancy 1). 257 of 494 ended while held, so most held effects are short. The hold itself works (99 by guid, list
never full). Remaining `card434: host FX` rows, 20: 11 `tag (at a point)` WarriorBug, 7 Mech autocannon fire sound
(attach owner is not the event owner), 2 WarriorBug pain bark.

Step 2, `<scratchpad>/c438_wound_patch.py` (`--check` ok, `fx_c136_net.cpp`): the wound shading system is local on
both sides. Junior finding: it is a GPU compute simulation with no renderer; its caller sets the hit positions on the
returned component (exe+0x4BBA724), which the row never carried, so the host row drew nothing on the client while the
client's own call was blocked. `wound_local` (asset name, index cached): host `c136_net_queue` does not send it
(count row `card438: host wound shading calls kept local, not sent (<when>): N`), `c136_client_own` keeps it (first 4
`card438: client t= kept its own wound shading call`).

Predictions for the b136 run: host wound count row N > 0; 0 FX rows for `NS_Compute_DamageAndRadius` in the client
log (was 5 target misses in c423b135b); `card329` Fx bytes for the asset 0 (was 19936 per C2M1 pass); `card438` held
total falls well below 494. Open after that run: which assets make up the rest of `sent at the world point`; if the
owner is still not replicated after 2000 ms the client has no actor to attach to, so the likely change is to drop a
short one-shot there and keep the journal row for a looping one (decide from the per-asset numbers, not before).
Receiver-side proof owed: screenshot of a wound on a bug on the client.

## 2026-11-16 16:28: b144 numbers (run `c423b144a`, C5M2)

Host totals: held 934, sent by guid 4 (longest wait 253 ms), sent at the world point after 2000 ms 3 (all
`NS_FX_Enemy_Aphid_Launch_Start1`), ended while held 927 with 0 of those owners within 15000 uu of a client pawn
(889 `NS_FX_Enemy_Morita_MuzzleFlash4`, 32 mech muzzle flashes, 6 saw sparks: one-shots on NPCs the client does not
have), list full 0. Wound shading kept local 683 calls, 0 sent. So no effect a client could see is lost or misplaced
by the hold; the 3 Aphid rows are the only world-point sends. The 20 remaining `card434 ... owner has no NetGUID`
rows are sound and tag rows (not Fx spawns) on owners outside the cull distance: card 434's subject, not this card.
Left before closing: the receiver-side wound screenshot.

## Closed (2026-11-17 13:09)

The host hold is in and measured (run `c423b144a`, C5M2): held 934, sent by guid 4 (longest wait 253 ms), sent at the
world point after 2000 ms 3, ended while held 927 with 0 of those owners within 15 000 uu of a client pawn, list
full 0. No effect a client could see is lost or misplaced by the hold.

Not done: the receiver-side wound screenshot. Wound shading is kept local (683 calls, 0 sent), so it is not a row
this card's hold touches.

Reopen when: the host's FX hold totals show `ended while held` with an owner within 15 000 uu of a client pawn above
0, or `list full` above 0, or a playtest shows an attached effect (muzzle flash, saw sparks, Aphid launch) on the
host's screen and not on the client's for a bug the client can see.
