# Card 183 — Playlog pawn pointers can go stale mid-map

## Goal
No playlog path dereferences a player pawn, controller or weapon that was destroyed since the last `pl_pawns`
refresh (every `kPlPawnMs`, 500 ms). Same crash class as card 182, inside a map instead of across LoadMap.

## Known
- `g_pl.pawn[]`, `g_pl.pc[]`, `g_pl.weapon[]`, `g_pl.remote[].pawn`, `g_pl.own_cmc` are raw pointers (playlog.cpp).
  Card 182 clears them at LoadMap enter. Between refreshes a pawn destroyed by death, respawn, a client leaving, or
  a mech enter/exit leaves the pointer stale for up to 500 ms.
- Readers that dereference: `pl_remote_source` (:622, from the card 162 sound detours, which fire at any time),
  `pl_listener_uu`, `pl_sound_row`, the weapon and inventory reads in `pl_pawns`' followers. STATE.md rule: a UObject
  pointer kept across frames is an FWeakObjectPtr (`C31Weak`), and validity is never checked by reading the object.

## Trace first (into the card, before code)
1. Which events destroy a player pawn or its weapon inside a map on host and on client (death, downed/revive,
   respawn, logout, mech), with the function and whether the destroy is immediate or deferred to end of frame.
2. Every g_pl pointer reader: dereference or compare-only. One table.
3. Fix shape: store `C31Weak` for the dereferenced fields and resolve at use, or drop the entry from the destroy
   hook the mod already has. Pick the one with fewer moving parts and say why.

## Predictions
One run on a route that destroys a client pawn mid-map while sounds play (name the route in the card; reuse an
existing scenario with a death or logout step). No CRASH line; the card 162 sound lines before and after the destroy
still name remote sources; baseline WARN only; no dead time.

## Files
`mod/src/ubw/playlog.cpp`, `mod/src/ubw/hooks_internal.h`, one scenario, the card.

## Junior trace

### 1. Mid-map destroy events

A destroy goes through `UWorld::DestroyActor` (hooked by card 098, `detour_c98_destroy`, world_c98.cpp:130). The
call is immediate: it marks the actor garbage and unregisters its components. The memory is freed at the next
garbage collection, which can run on any frame. So a raw pointer is safe to read until the next GC and is a crash
after it. `FWeakObjectPtr::Get()` (the `c31_weak_get` path, symbols.txt:68) returns null from the destroy on,
because it refuses a garbage object (UE5 source), without reading the object.

| Event | Side | Function | What goes | g_pl fields left stale |
|---|---|---|---|---|
| Client leaves (disconnect, quit, drop) | host | `AGameModeBase::Logout` (hook `detour_logout`, fire.cpp:281), then the PC's destroy, which destroys its pawn | the remote PC and its server copy pawn, with that pawn's weapons | `pc[s]`, `pawn[s]`, `weapon[s]` until the next `pl_pawns` (up to 500 ms); `remote[i].pawn` until it ages out (up to `kPlRemoteMs`, 3 s) |
| RESTART CHECKPOINT (LoadLastSave) | both | the save system respawns every player in place, no LoadMap (`c45_respawn_check`, death.cpp:2711, logs "respawn in place (new pawn ...)") | every player pawn and its weapons, replaced by new ones | all `pawn[]`, `weapon[]`; host `remote[]`; client `remote[]` (the host's pawn as card 028 drew it) |
| Mech destroyed while piloted | both | the mech's own destruction (card 079, `c79_mech_death`, death.cpp:598) | the possessed mech pawn | `pawn[s]` when the slot's pawn was the mech |
| Weapon swap or drop | both | the inventory unequips or drops the weapon; a dropped or replaced weapon actor can be destroyed | the old weapon | `weapon[s]` (read by name in the next weapon line and by `pl_shot`) |
| Downed and revive (card 043) | both | state on the same pawn | nothing | none |
| Mech enter and exit | both | possession change; the character pawn is kept | nothing | none (the next refresh picks the new pawn) |
| Map change | both | LoadMap | everything | none: card 182 clears at LoadMap enter |

So the card cannot close with no change. The client-leave path destroys a pawn mid-map on every rejoin, and its
`remote[]` entry stays for up to 3 s, while the card 162 sound detours call `pl_remote_source` on every sound.

### 2. g_pl pointer readers (playlog.cpp, before the fix)

| Reader | Line | Field | Use |
|---|---|---|---|
| `pl_slot` | 165 | `pawn[]` | compare only |
| `pl_remote` | 175 | `remote[].pawn` | compare only |
| `pl_remote_add` | 184 | `remote[].pawn` | compare and assign |
| `pl_pawns` aging | 221 | `remote[].pawn` | null test only |
| `pl_pawns` own CMC | 224 | `pawn[0]` | assigned this tick from the PC, so live; dereferenced (`pl_offset`, `read_ptr`) |
| `pl_fov` | 230 | `pc[0]` | dereference: `read_ptr(pc, kPcCameraManager)` |
| `pl_weapons` | 237 | `pawn[s]`, `weapon[s]` | dereference: `read_ptr(pawn, kMiWeaponInventory)`, `sprint_side(pawn)`, `object_name(was)` |
| `pl_cursor_poll` | 254 | `pc[0]` | dereference: `cursor_bit(pc)` |
| `pl_clock_step` | 274 | `pc[s]` | dereference: `c26_is_remote_pc`, `object_index`, `chan_send_client` |
| `pl_shot` | 533 | `pawn[s]`, `weapon[s]` | compare `pawn[s]`; dereference `object_name(weapon[s])` |
| `pl_activation` | 557, 560 | `pawn[s]`, `pc[0]` | dereference: `sprint_side(pawn)`, `read_vec3(pc)`, `read_ptr(pc)` |
| `pl_sound` | 607 | `pawn[0]` | dereference: `actor_location` |
| `pl_remote_source` | 632, 640 | `remote[].pawn`, `pawn[0]` | dereference: `actor_location` |
| `pl_listener_uu` | 649 | `pawn[0]` | dereference: `actor_location` |
| `pl_sound_row` | 667, 669 | `pawn[0]`, `remote[].pawn` | dereference: `actor_location` |
| `detour_pl_impact` | 791 | `own_cmc` | compare only (the hooked CMC is live) |
| `detour_pl_impact` | 805 | `pawn[0]` | dereference: `actor_location` |
| `pl_map_end` | 736 | all | clear only |

`own_cmc` is never read through. The pawn compares are made against live actors. A stale raw pointer can still equal
a new actor that GC put at the same address, which would name the wrong sound remote (card 162), but it cannot crash.

### 3. Fix shape

`pc[]`, `pawn[]`, `weapon[]` and `remote[].pawn` become `C31Weak`, set where they are set now and resolved at use
through `pl_pc(s)`, `pl_pawn(s)` and `c31_weak_get`. The compares resolve too, so each field has one copy and the
address-reuse case goes away as well. `own_cmc` stays raw, because it is compare-only. A remote entry whose pawn is
gone resolves null, so `pl_remote_add` reuses its slot and the readers skip it at once.

The other shape, dropping the entry from `detour_c98_destroy`, needs a change in world_c98.cpp, which is outside this
card's files. It also misses any destroy that does not go through `UWorld::DestroyActor`. The weak pointer covers every
path and needs no hook, so it has fewer moving parts.

Live pawns: the weak pointer resolves to the same object, so every line reads as before.

### Route

tools/scenarios/c56-rejoin.txt (FED_C1M1). The client disconnects mid-map and card 045 reconnects it. On the host,
Logout destroys the server copy and its weapons, and the rejoin spawns a new one.

- Before the destroy: the client takes BP_WeaponPickup_MoritaMk1_C_1 with E (relayed). The first server copy's weapon
  swap plays MS_FLY_Weapon_Drop_Generic with no attenuation, which gives a card 162 line from BP_Weapon.
- After the destroy: the rejoined client fires; the host logs the new server copy's sound (card 162 or playlog audio
  line, from BP_).
- FED_C1M1 gives no card 162 host line at join by itself (c183-r1), so the pickup is needed.
- The scenario's card 056 PickupGone waits were stale: that log path is gone from the mod, and card 098/145 world E
  rows replaced it (c183-r2). The waits now match those rows. All sleeps are removed.

## Report

### Files changed

- mod/src/ubw/playlog.cpp:
  - g_pl.pc, g_pl.pawn, g_pl.weapon and PlRemote::pawn are now C31Weak. They are resolved at use with c31_weak_get
    through two helpers, pl_pc(s) and pl_pawn(s).
  - Compares are resolved too, so a freed address that gets reused cannot match.
  - own_cmc stays raw, because it is compare-only (detour_pl_impact).
  - In pl_remote_add, an entry whose pawn resolves null counts as a free slot.
  - pl_weapons reads the inventory only when the pawn resolves non-null.
- tools/scenarios/c56-rejoin.txt:
  - Added: the card 183 block (the client's Morita_C_1 pickup and its card 162 wait, host marks, the Logout wait, and a
    client shot after the rejoin with its sound-line wait).
  - Every sleep is removed.
  - The stale card 056 PickupGone waits now match card 098/145 world E rows and the card 098 world journal replay line.
- docs/cards/183-playlog-pawn-pointers-mid-map.md: the trace (sections 1 to 3 and Route) and this report.
- hooks_internal.h: not changed.

### Summary

The playlog pointers to the player's controller, pawn and weapon were raw. A pawn or weapon destroyed mid-map, by client
Logout, RESTART CHECKPOINT, mech death or a weapon swap or drop, left them dangling until map end.

They are now weak pointers. A destroyed object resolves to null without being read, through FWeakObjectPtr::Get on the
index and serial. No heap is used. No check reads the object. Every function is 60 lines or fewer.

For a live pawn, the weak pointer resolves to the same address, so the card 160/162/176 decisions are unchanged. The card
162 line in r3 reads the same as the one in c182-r1.

### Verification

Build tail:
- "[1/2] Building CXX object CMakeFiles/ubwcoop.dir/src/ubw/playlog.cpp.obj"
- "[2/2] Linking CXX shared library dwmapi.dll"
- No warnings.
- DLL 1c8125f82b56dde3 ("deployed: 1c8125f82b56dde3", "lane 1: dll 1c8125f82b56dde3, build 1c8125f82b56dde3").

Runs:
- c183-r1 failed at scenario line 13: a card162 host wait at join timed out after 60 s. FED_C1M1 plays no
  unattenuated remote sound at join. Fix: the client's Morita_C_1 pickup.
- c183-r2 failed at line 18: the card056 PickupGone wait timed out after 15 s. The card 056 log path no longer exists
  in mod/src. The frag pickup itself worked: "card098: host t=91593120 world E BP_GrenadeAmmoPickup_Frag_C_0 ... sent to
  1 of 1 clients". Fix: the waits now match the card 098/145 rows. This was a gap in my audit of the scenario.
- c183-r3 passed: "run c183-r3: ended by scenario after 106 s; host rc 0, client rc 0".

Predictions:
- No CRASH line: CRASH 0 on both sides, "new crash dumps: none", FATAL/ERROR 0.
- Card 162 before the destroy names a remote source (the first server copy's weapons):
  - "card162: host t=91844499 remote sound MS_FLY_Weapon_Drop_Generic from BP_Weapon_MoritaMk1_C at (13124, 13347,
    280), -1 uu from the remote pawn: no attenuation, plays with ATT_FLY_Trooper_Footsteps"
  - The same line from BP_Weapon_Unarmed_C.
  - These match c182-r1's form: "card162: host t=90789121 remote sound MS_FLY_Weapon_Drop_Generic from
    BP_Weapon_Unarmed_C ... -1 uu from the remote pawn: no attenuation, plays with ATT_FLY_Trooper_Footsteps".
- The destroy: "card140: host Logout: pace queue of PC 0000000051668020 dropped with 0 frames". Then "card045: client
  t=91850287 reconnected (1): try 1, 5603 ms after the drop", and "card098: host t=91852569 world journal replay to
  BP_PlayerController_C (first Pong of the map): ...".
- After the destroy, the new server copy is the remote source: "playlog: host t=91852842 audio MS_WPN_AI_Morita_Mk1
  from BP_PlayerCharacterMI_C at (-560, -1715, 103) 14972 uu, 3D, attenuation ATT_WPN_MoritaMK1, volume 1.00".
  - This is the pl_sound remote-owned line, resolved through the new weak pawn.
  - No card 162 line follows the destroy. That is because the rejoined copy's shot carries its own attenuation, and
    card 162 prints only for sounds with none (the same as c162-r3's fire-sound finding). So "card 162 lines after the
    destroy" holds only through the playlog remote line. No unattenuated remote sound played after the rejoin.
- Drive: "scenario c56-rejoin.txt: drive rc 0".
- No dead time: "dead time: sleeps 0.0 s, timed-out try waits 0.0 s in 0 (none)".
- WARN:
  - Host 1, "WARN attach: DisableThreadLibraryCalls failed, err=126". This is baseline (c182-r1).
  - Client 5: the same attach line, plus 4 new card132 lines after the rejoin:
    - "WARN card132: mismatch BP_KillableBuilding_Base_TacSuppRangeTarget_C BP_KillableBuilding_Base_TacSuppRangeTarget_C_0
      location host (12650,14500,0) client (12650,14500,-250); last row seq 102 (WorldOp) sent ..."
    - The same for _C_1 and _C_3.
    - "WARN card132: client round 2: actors 1562 host, 1562 client, mismatches 3 (classes
      BP_KillableBuilding_Base_TacSuppRangeTarget_C:3) ...".
  - No earlier log has this class. The last accepted run of this scenario, card056x, had WARN 0 on an older DLL and
    no Morita_C_1 pickup.
  - The host moved the range targets by timeline, "card098: host t=91844281 world L
    BP_KillableBuilding_Base_TacSuppRangeTarget_C_0|Timeline 12665 14500 -482 1: sent to 1 of 1 clients", 439 ms
    before the Logout. That is the Morita_C_1 objective (TacSupport_SO2). The rejoined client's state for those
    targets differs.
  - This is not playlog: playlog moves no actor. It is a rejoin replay gap for timeline rows (card 098/140 class) and
    is out of scope.

"hooks: skipped" (host, the same on the client): "hooks: skipped: UWorld_Listen, AController_SetIgnoreMoveInput,
UCommonUIActionRouterBase_ApplyUIInputConfig, ASTPlayerController_HandleInputLook, ..." (7 lines; this is the baseline
skip list, unchanged by this card).

### Out of scope

- The rejoin mismatch for TacSuppRangeTarget (above): its timeline position is not restored for a client that joins
  after the timeline ran.
- Card 184 (the c104 reset order): not started.

Report complete.

## Senior review (2026-11-11)
Accepted, DLL 1c8125f82b56dde3, three runs (c183-r3 passed; r1 and r2 failed on scenario waits, fixed). Checked: diff
against the pre-183 snapshot (playlog.cpp only: `g_pl.pc`, `pawn`, `weapon`, `PlRemote::pawn` are `C31Weak`, every
reader resolves through `pl_pc`, `pl_pawn` or `c31_weak_get` and tests null before reading; `c31_weak_set` takes
null); rebuild "no work to do"; build and deployed hash equal; no function over 60 lines in playlog.cpp; 0 CRASH both
sides; host WARN baseline only; "scenario passed", dead time 0.0 s. Partial: no card162 line after the destroy (the
rejoined shot has its own attenuation). New WARN class on the client (card132 location mismatch of
TacSuppRangeTarget after rejoin) is not from this change; it is card 185.
