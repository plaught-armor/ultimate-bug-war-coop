# Card 076: Mech weapons in co-op: the client's mech fire hits on the host, and both sides see it

Status: sent 2026-11-05 12:20 to junior ultimate-bug-war-coop-bb, after card 061 was accepted. Drafted 2026-11-05 (senior).

Read the card 075 Report ("Mech fire") and Senior review, the card 024 row in `docs/CARDLOG.md` and the card 024 code (the `InputTag.Fire` relay to the server copy), and the card 074 Report (grenade spawn relay: a pooled `ADProjectile` does not replicate). Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

Card 075 lets the client pilot a mech, but no run fired a mech weapon. What is known:

- The client adds `IMC_Mech_KBM`, which has `IA_MechPrimaryFire <- LeftMouseButton` and `IA_MechSecondaryAbility <- RightMouseButton`. It binds 4 mech ability tags from `AD_MechPlayerAbilityDefinition_C`.
- GAS activation reaches the host: c75b11 has `card024: host server-copy InternalServerTryActivateAbility handle 143 ability BP_BaseMech_CrushProps_C`.
- The client's mech ammo is `AmmunitionType.RazorBackAutocannon 999` (card 072 line on `BP_BaseMech_C`).

Not known: whether the mech's primary fire goes through card 024's `InputTag.Fire` relay or through a separate ability tag. Not known either: whether its projectile replicates. A pooled `ADProjectile` would not, which is the grenade case from card 074.

## Design

1. **Check first, log only** (behind `UBWCOOP_C76_LOG=1`):
   - The client presses `IA_MechPrimaryFire` in the mech. Log the ability tag and ability it activates on the client, and whether the host's mech then fires: `InternalServerTryActivateAbility`, the card 020 `FireProjectile` log (`kC20FireWhat`), or a hitscan trace, whichever the mech weapon uses.
   - Log whether the host's mech projectile or tracer shows on the client, for the client's own fire and for the host's mech fire.
   - Do the same for `IA_MechSecondaryAbility`.
2. **Damage on the host.** If the host's mech does not fire for the client's press, relay it. Reuse card 024's relay if the tag is a fire tag it can press; otherwise add the mech tag to the same path. No new `ChanOp` unless the card 024 path cannot carry it. The host stays the only side that deals damage.
3. **Visuals on the client.** If step 1 shows that the mech projectile does not reach the client, reuse card 074's spawn relay (`ChanOp::GrenadeSpawn` path generalised to the mech weapon's projectile class, cosmetic and with no damage). If the mech is hitscan with a tracer or impact FX, relay the FX the same way the card 074 explosion frame does. Do only what step 1 shows is missing.
4. This runs in every co-op session. Only the step 1 log is behind the flag.

## Do

1. Implement the above.
2. Proof run on FED_C4M1, based on `tools/scenarios/c75b-pods.txt`: the client enters a pod mech, fires primary at a bug for 3 s, fires secondary once, and leaves. The host does the same in its own mech. Take shots on both sides while each one fires.
   - The host log shows the bug's health dropping from the client's fire (card 025 NPC health line or the bug's death).
   - Each side's fire shows on both screens.
3. Run `c75-solo` (`SOLO=1`): no new frames, and the host's mech fires as before.

## Done when

- The shots show both mechs' fire on both screens.
- The client's mech fire damages or kills a bug on the host, and the log shows it.
- `logsum` gives ERROR/FATAL 0, both games end with rc 0, and there is no new dump.
- With `UBWCOOP_C76_LOG` unset, the step 1 log detours are not installed (card 047 gate).
- fnlen reports no function over 60 lines.

## Report

Done (junior, 2026-11-05). All proof on FED_C4M1. Scenarios: `tools/scenarios/c76-mech-fire.txt` (co-op, based on `c75b-pods.txt`) and `tools/scenarios/c76-solo.txt` (c75-solo with the walk replaced by the mech's fire). Final DLL hash 3150ef2db7259a92.

### Step 1 findings (c76a, log only)

- The mech's primary is `InputTag.Fire` on `BP_Weapon_MechRazorback_C`, so card 024's relay already carries it. But the mech's fire ability fires one round per press, while the soldier's fire ability loops on one press. A held LMB calls `AbilityInputTagPressed` every frame on the client, and the one relayed press gave the host one round.
- The secondary is `InputTag.MechSecondaryAbility`. Card 024 did not relay it.
- On a client pilot, RMB stayed `InputTag.ADS`. `ASTPlayerMechBase::AddMechInputContext` / `RemoveMechInputContext` run on the server, and for a client's mech the server has no local controller, so card 005's guard skips them. The client kept the soldier contexts on top.
- Rounds are pooled `ADProjectile`s (`FireProjectile`, the grenade case of card 074). The client predicts its own rounds. The host's own mech rounds never reached the client.

### What changed (mod/src/hooks.cpp)

- **A. Fire kinds (card 024 path, ~6580).** A Fire frame's argument is now `(kind << 1) | pressed`, so Fire 0 and Fire 1 keep their meaning. Kind 0 is `InputTag.Fire`, kind 1 is `InputTag.MechSecondaryAbility` (counted on a mech, on the client only). `C24Relay` carries the kind. No new `ChanOp`.
- **B. `c24_mech_repress` (~7133).** On the host, per tick, a held primary relay on a mech presses again, so a held LMB fires a stream as it does locally.
- **C. Mech round relay (`c76_mark_task` from `detour_fire_projectile`, `c76_relay` from card 074's always-on `c74_relay`, ~25745).** Every round the host fires from a mech goes as one `GrenadeSpawn` frame with throw number `kC76MechSeq` = 0 to every remote PC except the mech's pilot, whose client already shows its own predicted round. The copy is cosmetic: no explosion frame, no damage, and it keeps its own life span.
- **D. `c76_client_imc` (~7216).** The client does what those natives do for its own pilot: on entering a mech it adds every context of the mech's input config (mech+0xE60) at priority 3 with IgnoreAllPressedKeysUntilRelease, and on leaving it removes them. The contexts are kept (the mech may be gone at leave) and dropped on every map.
- **E. Class lookup on the client.** In `c74_receive`, the class lookup now uses `c50_resolve` (find, then `StaticLoadObject`). The mech round's init data class `BP_ProjectileInitData_Autocannon_C` is not loaded on the client yet, and c76b had 24 WARN "no such class null".
- **F. Step 1 log** (`UBWCOOP_C76_LOG=1`, ~27156): tag edges, ability activations, `FireProjectile` with a mech avatar, and near a mech the pool spawns, impact damage (mech instigators only, so ArcherBug damage no longer uses the cap as it did in c76b) and Niagara systems, plus 1 Hz NPC Health changes. The pool, damage and Niagara lines ride the card 074 log detours, so `c74_install` runs with this flag too (card 074 lines stay off without `UBWCOOP_C74_LOG`). The relay, copy and IMC lines print only with the flag, or as WARN when something fails.
- fnlen: 1442 functions, 0 over 60. Build clean.

### Runs

| Run | Kind | Result |
|---|---|---|
| c76a | co-op, log only | step 1 findings above |
| c76b | co-op | client copies failed (no such class null) -> E; damage cap used by ArcherBug -> mech filter |
| c76c | co-op, flag on | passed. Client fire 53012301-53016027: 28 mech-instigated hits on the host, warrior bug Health 40 -> -215, dead at 53013300. 24 client copies of host rounds "set to the host's". 0 downs |
| c76d, c76e | co-op | the watcher was warped 600 uu from the firing mech and got downed / out; host phase lost. Warp reverted |
| c76f | co-op | the client's mech was worn to Health 1/400 by bugs and card 043 downed it; host phase lost (0 copies). Random; see findings |
| c76g | co-op, flag on, final DLL | passed: rc 0/0, ERROR/FATAL 0, host WARN 0, client WARN 1 (pre-existing card 034 refit), no dump, 121 s, 0 downs. 24 mech hits on the host inside the client fire window 53854165-53857806, warrior bug 400 -> 230 -> -452, dead at 53855381, 571-682 uu from the mech. 8 relay lines (3 Fire press/release pairs + 1 mech secondary pair). Client IMC "0 removed, 2 added at priority 3" on enter, "2 removed, 0 added" on leave. 24 client copies of host rounds |
| c76h | co-op, flag unset, final DLL | passed: rc 0/0, ERROR/FATAL 0, host WARN 0, client WARN 1 (card 034), no dump, 122 s. 0 card076 lines on both sides, no "card 074 log detours installed" line, "card 047: 115 of 225 hooks not installed" |
| c76s1 | solo (`SOLO=1`), flag on | passed: rc 0, 0 ERROR/WARN, no dump; 23 rounds all "sent to 0 of 0" |
| c76s2 | solo, flag on, final DLL | passed: rc 0, 0 ERROR/WARN, no dump, 81 s; 24 rounds all "sent to 0 of 0", no GrenadeSpawn frame; warrior bug 400 -> 145 -> -280, dead |

During the client's fire the relay lines read "sent to 0 of 0 clients (pilot pc ... skipped)". That is correct: the pilot's client shows its own predicted rounds.

### Shots

- `client-c76g-c-fire1`, `client-c76h-c-fire1`: the client's own mech firing at the warrior bug (muzzle flash and impact).
- `client-c76c-h-fire1`, `h-fire2`: the client sees the host mech's rounds hit the bug.
- `host-c76g-h-fire1`: the host's mech killing bugs.
- `host-c76c-c-fire1`, `host-c76g-c-fire1`: the client's mech is far from the host's view, but the host HUD shows "+2,000 Kill: Warrior Bug" for the client's kill. `host-c76e-c-fire2` (supplementary, from the reverted warp) is a close view of the client mech firing, with a hit marker.
- Weak spots: in c76g/c76h the client watcher's h-fire shots are dark and blocky (its camera sits in low-LOD geometry), with only faint orange tracers. And because the watcher stays put, the host's c-fire shots show the client mech only from far away. The log (24 copies per run, 24-28 hits) is the stronger proof for "both screens".

### Findings for the senior (not fixed, out of scope)

1. **A mech can be "Downed".** Card 043 treats a mech pawn like a player: at Health 1/400 under bug attack it goes Downed with a bleedout, not the native mech destruction. Seen in c76d (host mech) and c76f (the client's mech server copy, slot 1).
2. **An Out pawn entered a mech.** In c76e the host pawn was already Out and still ran EnterMech.
3. **Shared kill score.** The client's kill shows on the host HUD (+2,000). Observation only.

## Senior review (2026-11-05 13:05): accepted

- **Final runs.** I checked c76g, c76h and c76s2 myself:
  - All three drives end with `scenario passed`.
  - ERROR/FATAL is 0 on every log. Host WARN is 0. The client WARN is 1 in c76g and c76h: the known card 034 refit line.
  - The newest crash dump is still the 11:31 one from c75a12, so these runs left no new dump.
  - The deployed DLL matches `build/dwmapi.dll` (3150ef2db7259a92). It was deployed at 12:47:43, before c76g started (12:47:49).
  - fnlen: 1442 functions, 0 over 60 lines.
- **Gate.** In c76h, with the flag off:
  - The host log has the line `UBWCOOP_C76_LOG unset -- card 076 mech fire log off` and 0 `card076` lines on both sides.
  - `install_card_logs` runs `c74_install` only when `g_c74_log` or `g_c76_log` is set (hooks.cpp:28035).
- **Relay.**
  - The Fire argument stays compatible: kind 0 keeps the old meaning of `Fire 0` and `Fire 1`.
  - On the host, `c26_host_receive` relays kind 1 only for a mech pawn, and any other kind is dropped, so a bad kind cannot index `own[]` on the host.
  - `c24_mech_repress` re-presses only kind 0 on a mech, from the existing `c24_host_guard` tick.
- **Shots.** Montage: `~/.cache/claude-tmp/claude-1000/-path-to-ultimate-bug-war-coop/c340cb3a-800c-4276-ae3c-b37fdc6f665d/scratchpad/c76.png`.
  - Clear: the client's own mech fire (client-c76g-c-fire1), the host mech killing bugs (host-c76g-h-fire1), and the host on foot seeing the client mech fire with a hit marker (host-c76e-c-fire2).
  - Weak: client-c76c-h-fire1 shows the host's rounds hitting only at range, in low-LOD geometry.
  - host-c76g-c-fire1 shows nothing useful.
  - "Both screens" is accepted on the log: 24 client copies of host rounds, and 24 mech-instigated hits on the host in the client's fire window.
- **Follow-ups**, queued as card 079:
  - Card 043 downs a mech pawn instead of letting the native mech destruction run (c76d, c76f).
  - An Out pawn can enter a mech (c76e).
- **No action:** the shared kill score on the host HUD.
