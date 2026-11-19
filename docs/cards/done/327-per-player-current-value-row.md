# 327 Per-player current-value row: join replay and digest read the same row

User decision 2026-11-14: before the playtest. Source: card 323's table (`c323-opcodes.md`, junior): the player
state ops are sent live only, and the card 132 digest reads no player state at all, so a wrong player state is never
replayed and never detected. Batch 2, with card 326 (this is the registry's "player" mechanism).

## What is wrong today

| Player value | Host source | Sent | Join replay | Digest | Status |
|---|---|---|---|---|---|
| Life state (Up, Downed, Out, Dead) and bleedout seconds | `g_c43.e[]` (`C43Entry`) | `c43_broadcast` on a transition and on each whole bleedout second (`death.cpp:698`, `c43_bleed`) | none | none | settled: a joiner gets a Downed player within 1 s (the seconds repeat), never an Out or Dead one (no repeat) |
| Mech seat | `g_c75_pilots[]` (`vehicles.cpp`) | MechEnter / MechLeave requests, result by possession | none | none | open: what a joiner sees for a mech piloted before the join (step 1) |
| Turret seat | card 125 turret rows (`turret_sync.cpp`) | TurretMount / TurretLeave requests | none | none | open, as the mech (step 1) |
| Weapons list, ammunition, tactical charge, grenade count | `C50Copy` (`sent`, `ammo`, `charge`), card 073 | `c50_host_poll` on a change | yes (poll forced at first Pong) | none | settled: replayed, never checked |

## Design

1. Host: one row per roster slot (`C43Roster`), rebuilt on the `c50_host_step` cadence from the sources above:
   life state, seat kind (none, mech, turret), seat actor key (the wire name the digest already uses for that
   actor), and a 16-bit hash of the weapons, ammunition and charge text last sent to that slot.
2. Join replay (the registry's `join_replay` for "player"): for every other player, the joiner gets that player's
   life state through the existing `Down` frame and receiver (`c43_receive_down`), and the seat through whatever
   step 1 shows the joiner is missing. No new receiver where an existing op's receiver applies the value.
3. Digest: two entries per slot in the global level, built on both sides: `P:<slot>` (state = life state, ext =
   seat kind and the seat key's low bits) and `PW:<slot>` (ext = the 16-bit inventory hash; compared only for the
   slot the client owns, the client reads its own inventory). The client builds them from its own tables (its
   `g_c43` entries, the card 075 client mech identity, the card 125 client turret) so a mismatch means the client
   shows something else than the host holds.
4. Transitions: the `Down` op gets the ledger key `P:<slot>` (`ledger_c132.cpp` key table), so the digest's existing
   "last row after S is left out" rule covers a change in flight. No second mechanism.
5. Rows: `card327: host t=<ms> player row slot <n>: <life> <secs> s, seat <none|mech|turret> <key>, inventory
   <hash4>` on a change (cap 64 per map); the digest's own mismatch rows name `P:` and `PW:` keys as any other.

## Step 1 (junior, static, report before coding)

- Mech and turret: for a seat taken before a client's first Pong, what that client has today (the pilot pawn hidden
  or not, the mech's or turret's possessed look, card 075's client identity, the card 125 client rows), each line
  with `file:line`, marked verified or open. This decides item 2's seat part.
- The client-side source for each digest field in item 3, with `file:line`.
- Whether `PW:` can also cover the other player's held weapon as the client shows it; if not, say what would.

## Predictions (batch 2 run)

- Late join while the host is Out (downed, bleedout over): the joiner's log has `card043 ... Down Out` for the
  host's pawn within 1 s of its first Pong; today it has none.
- Digest rounds: `P:` and `PW:` entries present on both sides (2 per slot), 0 mismatches at rest; one deliberate
  check: a round taken while a player is Downed shows equal `P:` entries.
- After the checkpoint reload: `P:` entries Up for both, 0 mismatches.

## Done when

The predictions hold, the card 323 table's player rows read "replayed, in digest", and card 326's join replay row
lists "player".

## Step 1 result (junior, 2026-11-14, static)

Paths are under `mod/src/ubw/`.

### What a client has for a seat taken before its first Pong

With two players, the only seat that can be taken before a client's first Pong is the host's own. After a checkpoint Reload, every pawn respawns on foot.

**Turret: covered, verified statically.**
- The host polls every turret each tick (`c125_host_rows` `turret_sync.cpp:349`).
- The packed value (`c125_pack` `:300`) holds aim, heat, in use (bit 30) and ready (bit 31).
- It is journalled last-wins as a c98w turret row (`c98_host_turret` `world_c98.cpp:513`), so `c98_replay` sends it at the join.
- The client applies it in `c125_client_turret` `turret_sync.cpp:602`. That sets in use and ready, and runs `c125_client_possess_graph` on a change (`:614-618`), which sets the hit box collision, the audio and the camera gate. It also applies the aim when the turret is not the client's own.
- Open: the gunner pawn's own position and attachment come from the game's replication of that pawn. No mod row reads them.

**Mech: open.**
- On the host, MechEnter runs the game's `OnInteraction` (`vehicles.cpp:1895`), and the game possesses the mech. The mech is a replicated pawn: the client sees its own mech as an autonomous proxy (`vehicles.cpp:1554`).
- So a client gets the mech actor and its movement through replication. Whether the pilot soldier is hidden or attached on the client depends on properties the game sets on the server; that is not verified.
- `g_c75_client` (`vehicles.cpp:1563`) holds only the client's own seat, and it is empty for a seat it did not take, which is correct.
- `g_c75_pilots` exists on the host only.
- What would settle it: one client row at its first Pong, for each mech, logging the PlayerState or pilot it shows and the soldier's hidden and attached state.

### Client-side source of each digest field

| Field | Client source |
|---|---|
| `P:` life | `g_c43.e[]` (`death.cpp:524`), written by `c43_receive_down` `death.cpp:1524-1559`. The entry's slot comes from the wire (`args.v[4]`, `:1541`). An absent entry means Up: an entry is Up only inside the stand-up window (`hooks_internal.h:2943`). |
| `P:` bleedout seconds | `C43Entry.secs_left` from the same receiver. Leave it out of the digest: it changes every second. |
| `P:` seat, mech | No per-slot client table for another player. Own seat only: `g_c75_client`. A client source needs a live read of the mech's PlayerState (offset to find). |
| `P:` seat, turret | Per turret, not per slot: `turret[kC125InUse]`, set at `turret_sync.cpp:614`. The row has no free bit for a slot (`c125_pack` uses all 32 bits). Proposal: the digest entry for a seat is keyed by the seat actor (`T:<turret key>` in use), which both sides already hold, not by the player slot. |
| `PW:` weapons | `c50_text(own inventory)` `hooks_ubw.cpp:1439`. This is the same function the host runs on the copy in `c50_host_poll` (`weapons.cpp:1050`). |
| `PW:` ammo | Do not hash `C50Copy.ammo`: it is the client's own AmmoMirror text, so it would compare the client with itself. Use `c72_map_text` (`ammo.cpp:277`) on the copy's inventory on the host and on the own inventory on the client. |
| `PW:` charge | On the host, the text from `c104_charge_text` (`anim_c104.cpp:370`), the same text `c104_charge_poll` (`:385`) sends. On the client, the same function on its own pawn's component, filled by TacCharge. |

### Can `PW:` cover the other player's held weapon?

No. `PW:` is the client's own inventory compared against its copy. The held weapon the client shows for the other player is that pawn's equipped weapon on the client.
- What would cover it: a third entry, `PH:<slot>`, holding the equipped weapon's class hash through `c141_equipped` (`weapons.cpp:2298`). The host reads it on its pawn or the copy; the client reads it on the proxy pawn.
- Open: whether the client's proxy pawn has a weapon inventory with an equipped weapon. If it does not, the held weapon the client draws comes from a mesh or attachment instead, and that is what `PH:` would have to read.
- Needs one client row per other player's pawn: inventory present, equipped weapon class.

## PW WARN in run 4 of c338-c337 (b21): cause and fix

`WARN card327: client t=325799851 digest round 2 PW differs for two rounds: host 8f9b, client 9ed1`. The host row
(b21 prints its texts) and the client row hold the same weapons, ammo values and charge. Only the ammo text order
differs: host Morita, UnderslungShotgun, Laser; client Laser 300, Morita 240, UnderslungShotgun 8. `c72_map_text`
(`ammo.cpp:277`) writes the pairs in TMap order, and the map order is per process.

Fix (in `c327_inventory_hash`, `weapons.cpp`, rides b22): each `"<tag> <count>"` pair is hashed alone and the pair
hashes are summed, and the sum goes into the hash in place of the text. `c72_map_text` is unchanged (death.cpp and
restore_c143.cpp use its text).

Prediction: 0 `PW differs for two rounds` WARN in the b22 run of `c338-c337-c1m1.txt`.

Result (b22 `0c40725031a4aede`, run of `c338-c337`): 0 `PW differs for two rounds` WARN on both sides. Limit: a
text cut at `kChanTextMax` can drop a different pair on each side when the orders differ (very long ammo maps only).

## Closed (2026-11-17 13:10)

The per-player row is built and in the digest since b22; the order fault (`PW differs for two rounds`, TMap order per
process) was fixed in b22 and the WARN count is 0 on both sides in `c423b182a` and gate `c448b186a`. The remaining
Done-when lines are table wording in cards 323 and 326, which stay open and own that text.

Known limit, kept: a text cut at `kChanTextMax` can drop a different pair on each side when the orders differ
(very long ammo maps only).

Reopen when: any `WARN card327` row appears in a run or a playtest log.
