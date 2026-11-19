# Card 139: Every host-to-client send waits for the client's first Pong

Status: accepted 2026-11-08 (hb61, test DLL 94e38ac3dc93b321).

Read: card 138 Report (hb60), card 120 (the Pong gate), docs/STATE.md "Card scope and investigation rules".

## Verified (junior, hb60)

- Five host-to-client sends have no ponged check: Anim (enemies.cpp:3187), Gib (enemies.cpp:3932), Fire
  (hooks_ubw.cpp:509), Down (death.cpp:573), Warp (objectives.cpp:1700).
- hb60: "card132: recv 1 sent t=216670063 ... Anim" applied on the client 2 ms before its rejoin dispatch. It was
  harmless that time ("no NPC matched not played"). During a card 138 hold, the same rows can reach actors that have not
  run BeginPlay yet.

## Do

1. Static: list every host-to-client send site (grep the channel send calls), and mark which pass the ponged check.
2. Class fix: put the ponged check in the one send path all of them use, not five copies. A client that has not ponged
   gets nothing live; what it needs comes from the journal replay after its Pong. Say for each of the five whether its
   state is journalled, or whether losing the pre-Pong rows is fine because it is a one-shot.
3. Prediction and run: the client logs no live row before its first Pong on join and on rejoin. Batch with the next run.

## Done when

- The send list with its gate status, the choke point used, and the run lines showing no live row before the Pong.
- The usual Report items (WARN diff, run length, rc, ERROR/FATAL, CRASH, dumps, DLL hash). It ends with "Report complete."

## Step 1: every host-to-client send site (junior, static)

All of them end in `ue::chan::chan_send_client` (chan.cpp), the one host send point: `chan_to_client` and
`chan_to_client_args` wrap it, and the text-frame senders call it directly. Gate status before this card:

| Site | Op | ponged check before |
|---|---|---|
| chan_ops.cpp:35 c26_host_ping | Pong (reply to the client's Ping) | no (must not have one) |
| fire.cpp:228 c26_ping_step, enemies.cpp:4343 c42_ping_clients | Ping | no (must not: the Pong answers it) |
| ledger_c132.cpp:472 | LedgerAck | no (ledger bookkeeping, no world state) |
| digest_c132.cpp:611 | Digest | yes |
| door_c95.cpp:159 c95_send_all | DoorOp, CollisionOp | yes |
| grenades.cpp:600 c74_send_all | GrenadeSpawn, GrenadeExplode | yes |
| weapons.cpp:592 c50 poll (Weapons, Equip, AmmoSet seed, TacCharge c104) | several | yes |
| weapons.cpp:1428 | PickupGone | yes |
| world_c98, pose/decal/dropship c132 | WorldOp, Pose, Decal, Dropship | yes (to every ponged PC) |
| objectives.cpp:303 c49_send | Objective, ObjectiveActors | no: sent live to every remote PC |
| objectives.cpp:411 c49_first_pong, difficulty.cpp c114_send | journal replay, Difficulty | ran before `ponged` was set |
| objectives.cpp:2171 c65_send_activate | Activate | no |
| ammo.cpp:190 c71_after_credit | Ammo | no |
| grenades.cpp:234 c73_relay | GrenadeItem | no |
| enemies.cpp:3187 c41_send_attack | Anim | no |
| enemies.cpp:3932 c42_send_gib | Gib | no |
| hooks_ubw.cpp:509 c26_send_fire | Fire | no |
| death.cpp:573 c43_broadcast | Down | no |
| objectives.cpp:1700 c46_warp | Warp | no |

The grep found more ungated sites than the five from hb60: Objective, Activate, Ammo and GrenadeItem too.

## Step 2: the gate (code)

- `ue::chan::ChanOps::client_hold` (chan.h): an optional host callback, `const char* (pc, op)`. `chan_send_client`
  asks it for every frame after the remote-PC check. A held frame is not sent; the ledger gets it as unsent with the
  callback's text.
- `c139_client_hold` (chan_ops.cpp): Ping, Pong and LedgerAck always go. Every other op goes only when the PC's
  C50Copy has `ponged` set, else "not ponged yet (card 139)". The copies are emptied at map load
  (sprint.cpp) and at logout (fire.cpp), so the gate is per map and per PC.
- Order at the first Pong (c26_host_pong): `c50_first_pong` (sets `ponged`, split out of the old `c50_pong`), then
  `c49_first_pong` (journal, c114), then `c50_join_replay` (c56, c95, c98, c132 pose, decal and dropship, c96).
  Before this card, c49 ran before `ponged` was set; with the gate in place it would have been held. The replay
  order is otherwise unchanged.

What a pre-Pong client loses, for each of the five sends from hb60:

- Anim (c41): one attack-sequence play. One-shot, not journalled; fine to lose.
- Gib (c42): one gib burst. One-shot; a gibbed corpse's end state comes from replication and the c132 pose replay.
- Fire (c26): a host fire edge (on or off), not journalled. The next edge corrects it, so at most one burst of the
  host's fire is shown wrong.
- Down (c43): not journalled, but c43_broadcast sends it again each time the whole-second bleedout count changes, so
  a downed state reaches the client within 1 s of its Pong.
- Warp (c46): the kill-floor warp. The host moves the server copy itself and queues a follow-up check; the frame only
  tells the client. Not journalled. A client that has not ponged has a pawn the host is still placing.

Also newly held: Objective (c49 live; the journal replay at the Pong carries it), Activate (c65), Ammo (c71) and
GrenadeItem (c73). These are not checked one by one here.

DLL 94e38ac3dc93b321. The run is batched into card 132's hb61; prediction 5 there is this card's.

## Step 3: run (batched into card 132's hb61)

Prediction: on join and on rejoin the client logs no live row before its first Pong; held frames appear only before
the host's first Pong from that client.

Result, holds. Client `card132: recv` rows before the "card120: ... minimap origin" line: join 1 a Pong only; the
rejoin a Ping and a Pong. First other row both times is Difficulty, sent from the first Pong: "recv 4 ...
applied t=217160217 Difficulty" (origin t=217160146) and "recv 5 ... applied t=217235715 Difficulty" (origin
t=217235695). Host held lines: 2, "card132: sent 0 t=217160087 Anim ... not sent: not ponged yet (card 139)" and the
same at t=217160108, both before "card049: host t=217160171 journal replay ... (first Pong of the map)". The
journal, card095, card098 and card050 Weapons replays went out at the rejoin's first Pong (t=217235713 to 217235727),
and the client applied them.

Report items: hb61, 192 s, rc 0/0, 0 ERROR/FATAL, 0 CRASH lines, no dumps, DLL 94e38ac3dc93b321. WARN diff in card
132 (no card139 WARN kind).

Report complete.
