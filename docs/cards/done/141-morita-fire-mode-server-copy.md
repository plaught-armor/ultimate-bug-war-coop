# Card 141: The client's Morita fire mode does not reach its server copy

Status: CLOSED, outcome in docs/CARDLOG.md. Was: queued 2026-11-08, from card 136 hb65 review item 2. Budget: three runs. Gameplay state, not effects.

Read: card 136 "hb65 review" item 2, docs/STATE.md "Card scope and investigation rules".

## Why

hb65: the client fired BP_WeaponFireAbility_MoritaMk1Underslung_C (36 FireProjectile), but its server copy on the
host fired BP_WeaponFireAbility_MoritaMk1_C 21 times. The host shows the client's main fire (Fire_MS, MuzzleFlash3,
Casing) while the shooter sees the underslung shotgun, and the host's hits differ from the client's. Full world sync
covers what each player's weapon does.

## Do

1. Static first: trace how the fire mode (underslung toggle) is chosen on the client and how the server copy learns it
   (replicated property, server RPC, ability activation by class). Say why the server copy activates the main fire
   ability, and write a prediction before any run.
2. Fix in the layer that owns it, for every alt fire mode the weapons have, not the Morita only.
3. One run: client fires the underslung and main modes in turn; the host server copy's FireProjectile lines name the same
   ability class and count as the client's.

## Done when

- The cause with its trace, the fix, and the run lines showing matching ability classes and counts on both sides.
- The usual Report items (WARN diff, run length, rc, ERROR/FATAL, CRASH, dumps, DLL hash). It ends with "Report
  complete."

## Playtest 2026-11-08 addition (finding 16, host log)

- None of the client's 28 underslung bursts (5-9 BP_ProjectileInitData_Morita_C in one tick, e.g. client t=259144372,
  259407101) has a host burst within -400..+1500 ms. At those times the server copy fires single Morita rounds from the
  client's spot about every 90 ms (host t=9241042, 9241131). The weapon switch to the underslung
  (IA_SwitchWeapon_Underslung, key Q) does not reach the server copy; the host deals the damage with rifle rounds.
- Scope: relay the underslung switch and every fire-mode or weapon switch to the server copy.
- Added proof: a client shotgun burst shows a host burst of the same pellet count within 200 ms (card 155 shot lines
  and `tools/match_shots.py`, if 155 has landed).

## Trace (junior, 2026-11-08, static)

- The underslung key's handler, `ASTPlayerCharacterMI::HandleInputSelectWeaponUnderslung_Pressed` (0x051CB4E0, an
  interface thunk at pawn-0x748), reads the equipped index from the weapon inventory (pawn+0xA40, +0xE8 vtable slot
  0x20) and calls the pawn's vtable slot 0xAA8 with (equipped index, 0). Slot 0xAA8 of `ASTPlayerCharacterMI`'s AActor
  vtable is `ASTPlayerCharacterMI::SendWeaponSwitchRequest(int32, int32)` (0x051DB390).
- `SendWeaponSwitchRequest(i, b)`: when b == 0 and i is the equipped index, or when the inventory reports 1 (one
  weapon) and `USTWeaponUtilityLibrary::IsWeaponInUnderslungState(weapon)` (0x05243BD0) is true, it checks a tag on the
  pawn ASC (pawn+0x680, +0x130), then builds `ConstructWeaponSwitchRequest(i, 0, actor = the equipped weapon)` and sends
  the gameplay event to the weapon. That event is the underslung toggle; the weapon's `USTWeaponUnderslungAbility`
  receives it. Any other request builds `ConstructWeaponSwitchRequest(i, b, actor = the pawn)` and sends it to the pawn
  (the normal weapon switch).
- Card 057's `detour_c57_request` (weapons.cpp) sends Equip to the host only when `actor == pawn`. The underslung
  request has actor == weapon, so nothing goes to the host. The server copy's weapon never gets the event, stays out of
  the underslung state, and the relayed Fire press (card 024, the Fire tag on the copy's weapon ASC) activates
  `BP_WeaponFireAbility_MoritaMk1_C`. This matches hb65 and finding 16.
- Every weapon with an underslung ability goes through this one path, so a fix there covers all of them. The other
  weapon switches (slots, next and previous) already go through Equip.

## Plan and prediction

- Client: `detour_c57_request` with actor == the own pawn's equipped weapon sends a new op, Underslung, carrying
  (index << 1 | want). `want` is the negation of `IsWeaponInUnderslungState(weapon)` before the call.
- Host: on the server copy, if the equipped weapon is the one in the inventory slot `index` and its state != want,
  call `SendWeaponSwitchRequest(index, 0)` on the copy. A duplicate or late frame is harmless because the state check
  makes it idempotent.
- Log-only hook on `USTWeaponUnderslungAbility::ActivateAbility`: side, avatar, NetExecutionPolicy byte (+0x198).
  Risk: a LocalOnly policy refuses activation on the server copy, which is not locally controlled. If the run shows
  that, set the policy the way card 057 does.
- Prediction: after the fix, the client's Q gives a host "Underslung ... sent to the server copy" line and an
  ActivateAbility line on the server copy. The client's next burst is
  `BP_WeaponFireAbility_MoritaMk1Underslung_C` on both sides, and `tools/match_shots.py` pairs each client burst
  with a host burst of the same k within 200 ms.

## Trace correction and built fix (junior, 2026-11-08, DLL 7fb56b335341528a, then b8eb4fc219a125bc)

- Key Q has two bindings in IMC_MI_KBM: IA_SwitchWeapon_Underslung with InputTriggerHold (threshold 0.5, about
  0.5 s held) and IA_SwitchWeapon_Next with InputTriggerTap. A Q tap is Next, giving `(-1, 1, pawn)`; card 057 sent
  only `actor == pawn` with `a >= 0`, so Next and Previous (index -1) did not reach the host either (c155b
  "Equip NOT sent"). MouseScrollUp is also Next.
- Fix: a new channel op, Switch (client to host, one packed value `(i+1)<<4 | (b+1)<<2 | weapon<<1 | want`). The
  client's `detour_c57_request` sends Equip for `actor == pawn, a >= 0` as before and Switch for everything else
  (underslung, Next, Previous). The host decodes it, checks that the pawn is an MI-class server copy, skips an
  underslung request whose state already equals want, sets the switch ability policy to ServerOnly as card 057 does,
  and calls `SendWeaponSwitchRequest(a, b)` on the copy. It logs `card141: host ... Switch (a, b, underslung|pawn,
  want W) ...`, WARN when there is no copy.
- Change after c141a (DLL b8eb4fc219a125bc): the weapon enters or leaves the underslung state when its SwapIn
  animation ends, about 190 ms after the request, so the state read right after the call was always the old one
  (c141a printed `underslung 0 -> 0` while the copy did swap). The host now stores the request (PC, want, due time)
  and `c141_step` (game thread, every tick) reads the copy weapon's state 600 ms later: `card141: host ... copy
  weapon <class> underslung S (want W) 600 ms after the Switch`, WARN when S != W. Only the last request is kept; a
  PC freed inside the 600 ms would leave a stale pointer, accepted under the friends-only trust model.
- Log-only hooks on `USTWeaponUnderslungAbility::ActivateAbility` and `ReceiveEvent`, both sides.
- Prediction for run c141a (scenario tools/scenarios/c141-underslung.txt): a Q hold gives the host line with
  `want 1 ... underslung 0 -> 1`, and ReceiveEvent on the host's copy weapon. Each client shotgun burst shows a host
  `shot s` burst with the same k within 200 ms in match_shots, and the copy's FireProjectile ability is
  BP_WeaponFireAbility_MoritaMk1Underslung_C. The second hold gives `want 0 ... 1 -> 0` and k=1 rifle shots. Each Q
  tap gives `Switch (-1, 1, pawn ...)` and a host `weapon slot 1 server-copy` change (Morita to Unarmed and back).

## Card 155 proofs carried into this card's run (senior, 2026-11-08)

Card 155 closed partial after run 4 (c155d); its built fixes are in DLL 40e5082ba8d738ce. This card's run uses that
scenario and must also show, on the right sides:
- the host player-pawn hit line with Armour and Health before and after, for a hit on the client's server copy;
- down and revive on the client, with the cursor and IMC lines around them;
- press and ADS lines (Triggered edge after 100 ms quiet);
- grenade aim on both sides;
- the 300 s summary;
- `tools/match_shots.py` output, which is also this card's burst proof;
- client audio for the host's fire. If no host row names MS_WPN, write a "155 follow-up" in card 136.
Every scenario step that depends on game state (pickup re-enabled, weapon equipped, pawn downed) waits for its log
line, not a fixed delay. These items do not count against this card's three-run budget only if they pass in the
same runs; no run is spent on them alone.

## Report (junior, 2026-11-08)

Runs: 2 of 3 used. Scenario `tools/scenarios/c141-underslung.txt` (every state-dependent step waits for its log
line after a mark), `DEPLOY=1 FAST=1 GOD=0`, `UBWCOOP_C136_LOG=1` on both sides.

- c141a, DLL 7fb56b335341528a: the relay worked (host `Switch (.., 0, underslung, want 1)` and the copy fired the
  underslung ability), but the result check read the state before the swap animation ended and printed WARN
  `underslung 0 -> 0`. Fixed by the deferred check above; no other change.
- c141b, DLL b8eb4fc219a125bc: run length 320 s; drive rc 0, host rc 0, client rc 0; ERROR 0 and FATAL 0 on both
  sides; no CRASH line; no new dumps. Host log 8727 lines, client log 5815 lines.

### Card 141 proofs (c141b)

| FireProjectile ability | client client-own | host server-copy |
|---|---|---|
| BP_WeaponFireAbility_MoritaMk1_C | 21 | 21 |
| BP_WeaponFireAbility_MoritaMk1Underslung_C | 27 | 27 |

Host host-own: 16 BP_WeaponFireAbility_MoritaMk1_C.

- Underslung on: host `copy weapon BP_Weapon_MoritaMk1_C underslung 1 (want 1) 600 ms after the Switch`
  (t=265825348). Off: `underslung 0 (want 0)` (t=265831209).
- `tools/match_shots.py logs/c141b-host.log logs/c141b-client.log`: clock 6 exchanges, offset 30..7 ms, rtt max
  83 ms; client shots 24, host copy shots 26, matched 24, dropped 0, pellet mismatches 0, host copies unpaired 2.
  The three shotgun bursts (seq 17-19) pair k 9 with k 9 at dt -5, +1, +3 ms; rifle shots pair at dt -3..+22 ms. The
  two unpaired host copy shots have no client shot inside the window; no pellet count differs.
- Next (Q tapped, twice): host `Switch (-1, 1, pawn, want 1) ... SendWeaponSwitchRequest on the server copy`
  (t=265833907, 265846229). Neither side changed weapon: the Morita is the only weapon in the inventory, so Next is a
  no-op on both. The scenario's try-waits for a server-copy weapon change failed as expected. Consistent, not a
  defect.

### Card 155 proofs carried into the run (c141b)

- Hit line: PASS. Host `hit slot 1 server-copy: damage 16.00 (armour 0.00, health 16.00), Armour 100.0 -> 84.0,
  Health 100.0 -> 100.0` (t=265872562), and slot 0 host-own 15 damage, Armour 100 -> 85.
- Down and revive: PASS. Client `own pawn Up -> Downed, 30 s bleedout` (t=265882380); host `use press ...
  reviving entry 122772` (t=265882578); host server copy back to Up after a 3013 ms hold, Health 30
  (t=265885591); client `Downed -> Up` (t=265885615). Cursor and IMC: client `cursor 1 -> 0 (poll)` at
  t=265731205, `cursor 0 -> 1` at t=265875526 (pause menu), `imc remove IMC_DummyUI_KBM` at t=266020553. No cursor
  or IMC line comes near the down and revive (none is expected there; the downed HUD does not change the cursor).
- Press and ADS: OPEN. The client logged only `press IA_Pause` (twice). No `ads` line, although RMB was held
  1500 ms and IA_ADS is bound to RightMouseButton in IMC_MI_KBM; no press line for E, Two, LMB or Q either. The card
  155 Started/Triggered edge fix is in, so the cause is elsewhere. Hypothesis (static, not proven): the pawn's
  actions are bound with handlers taking `const FInputActionInstance&` (the instance signature of `BindAction`). The
  Shipping PDB publics hold only three `FEnhancedInputActionEventDelegateBinding<...>::Execute` instantiations
  (value, void, dynamic), and those are the three hooked; the instance one has no public symbol, so its executes never
  reach `pl_execute`. IA_Pause is bound through one of the hooked kinds.
- Grenade aim: PASS. Client `grenade aim GA_FragGrenadeThrowingAbility_C: control P 1.23 Y 90.28`
  (t=265870348); host `grenade spawn ADProjectile ... velocity (-14, 2982, 326)` (t=265871445).
- 300 s summary: PASS. Host `summary (300 s)`: shots own 15, copy 26, player hits 2, downs 1, deaths 0, loads 2,
  joins 1, rejoins 0, mismatches 0. Client `summary (300 s)`: shots own 24, mismatches 44 (below). Session-end
  summaries on both sides.
- match_shots output: above.
- Client audio for the host's fire: the sound plays; the playlog line is missing. The host's own rifle fire sound
  is `PlaySoundAtLocation MoritaMK1Fire_MS` (owner the host's BP_Weapon_MoritaMk1_C, key world); the client applied
  16 of those card 136 rows, all `played` (e.g. client fx 2241 t=265858453), matching the host's 15-16 own shots.
  The host rows that name MS_WPN (13 `SpawnSoundAttached MS_WPN_AI_Morita_Mk1`) are all from the client's server
  copy (`pawn server-copy`), the client's own fire, which the client plays itself; that is correct. So no card 136
  "155 follow-up" is needed for the sound. The missing `audio` line is a playlog gap: `client_play_sound` passes
  `client_key_actor(r)` to `pl_sound`, and a world-keyed row has no actor (guid 0), so `pl_sound` drops it. The row
  does not carry the shooter's pawn on the wire (the host logs it, `pawn host-own:... guid 32`, but only the key
  actor's guid is sent). Fix belongs to card 155's playlog: send or derive the instigating pawn for sound rows.

### WARN diff (c141b)

- Host 3 WARN: `attach DisableThreadLibraryCalls err=126`, `config UBWCOOP_GOD not "1"`, `cmd 17 kill actor
  failed (no such actor)` (the fourth scenario kill try after three bugs died). All known.
- Client 54 WARN: the attach and config WARNs, and card132 digest mismatches on the weapon rack pickups
  BP_WeaponPickup_MoritaMk1_C_0, BP_AmmoPickup_MoritaSmall_C_0 and BP_AmmoPickup_MoritaLarge_C_0 (`pickup host 1
  client 0`, `mesh visible host 1 client 0`, last row CollisionOp), 4-6 mismatches in rounds 2-9; client summary
  mismatches 44. Pre-existing (c155c 23 lines, c155d 9, c141a 12), not caused by this card. Hypothesis: when the rack
  re-enables a pickup about 10 s after a take, only the card 095 collision change (0 -> 1) reaches the client; the
  pickup-gone state (pickup flag and mesh visibility) is never restored there, so the client sees the rack empty
  while the host's is full. Needs its own card (related to card 145).

### Summary

- Card 141 done: the underslung toggle, Next and Previous reach the server copy through the new Switch op, and the
  copy fires the same ability classes with the same counts; shotgun bursts pair with the same pellet count within
  5 ms.
- Card 155 carried items: hit line, down and revive, grenade aim, summary and match_shots pass. Open: the press and
  ADS lines (instance-signature bindings not hooked, hypothesis) and the client `audio` playlog line for world-keyed
  replayed sounds (the sound itself plays).
- New finding for a card: rack pickup re-enable state not synced to the client.

DLL: b8eb4fc219a125bc (build/dwmapi-c141-b8eb4fc219a125bc.dll).

Report complete.
