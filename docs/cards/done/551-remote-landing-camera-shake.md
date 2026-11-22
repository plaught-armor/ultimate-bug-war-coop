# Card 551: a remote player's landing shakes the local camera

Playtest 2026-11-20 on b366 (`e62e76f63a05d88f`), training map. User report: "when either of us jump, we feel the
recoil of the landing of whomever jumped". Both sides feel it, so the host's copy of the client pawn and the
client's copy of the host pawn both reach the local camera.

## What is known

- No human landing shake asset exists (`docs/reference/asset-paths.txt` has only `Shake_Enemy_HeavyLanding`,
  `Shake_Mech_HeavyLanding`, `Shake_Mech_Landing`). The landing kick is likely a camera effect run from the player
  character's land handler, or one of these shakes started through a "Get Player" lookup that resolves to the
  local player (the card 099 class).
- The playtest client log (`UBWCoop-menu-20261120-184517.log`) has no `StartCameraShake` rows: the card 099 shake
  hook (`detour_c99_shake`, `hooks_ubw.cpp:3237`) is installed only under `UBWCOOP_C99_LOG`.
- STATE.md 2026-11-16 17:34 audit: world camera shakes (`PlayWorldCameraShake`) reach every local camera in range
  by design. A landing that goes through the world shake path would shake every player near the lander.

## Next step (after the playtest)

Static trace first: find what `ASTPlayerCharacter` (or the BP player character) runs on `Landed` and which camera
it reaches. Then one run with `UBWCOOP_C99_LOG=1`, the two players about 300 uu apart, each jumping once.

## Prediction

The run shows one camera effect row per landing on each side, with the lander being the remote pawn and the camera
being the local player's. The fix gates that effect to the lander's own locally controlled camera, by class (every
effect started from a remote pawn's land handler).

## Host log (`ultimate-bug-war-coop-public/logs/UBWCoop-menu-20261120-184651.log`)

No shake rows on either side: the card 099 hook is off in the play build, and the host log has a single
`ClientStartCameraShake` rpc call (340764858), not one per landing. The logs cannot name the path. The static trace
and the `UBWCOOP_C99_LOG=1` run above stay the next step.

## b369 (`4d7adc330945a079`, build-b5 copy kept), not run yet

No fix yet: the play logs cannot name the cause. The trace run in "Next step" above is still needed.

## Cause (senior static trace, 2026-11-21)

- No Blueprint landing handler: `BP_PlayerCharacterMI` (`build/kis/pcmi.txt`) has no Landed or shake call.
- Every direct caller of `USTCommonUtilityLibrary::StartCameraShake` (exe+0x5251D30, 14 sites) was listed. The library
  call shakes `UGameplayStatics::GetPlayerController(context, 0)` (this machine's own player) after only a screen
  shake settings check. The player path among the callers is `USTAnimNotify_ScreenShake::Notify_Implementation`
  (exe+0x50F6170). It passes the notify's Owner as the context and has no locally controlled check. So a screen shake
  notify in a remote player's animation (the landing) shakes the local camera, on both machines.
- Ruled out: `ASTPlayerCharacter::HandleDamageEvent` (fall damage) calls `PlayHurtCameraShake` only after a vtable
  +0x810 check (locally controlled), and `USTFallDamageAbility::OnLandedCallback` has no shake call.

## Fix (b370 `d5ac89d9a9fda0f7` = b369 + card 551, build-b5 copy + .map kept)

`detour_c551_shake_notify` (`player_c99.cpp`, ungated hook): a ScreenShake notify whose owner, or the owner's Owner
(for a weapon or other held actor), is a trooper or a mech runs only when that pawn's controller is this machine's own
player controller. NPC and world notifies are unchanged; each machine plays its own copy's shake. Proof row:
`card551: <side> ... skipped screen shake notify ... on <remote pawn>`, at least one per remote landing on each side,
and none for the local player's own pawn. Known gap: the local player's own pawn with no controller (unpossessed
inside a mech, dead) also skips its shake.

### Run c551b370a predictions (junior, 2026-11-21)
Scenario `tools/scenarios/c369-c1m1.txt` (pins `d5ac89d9a9fda0f7`): after `host clear 4000 kill` the host warps to
300 uu from the client's trooper, both look at each other (shots `c370-551-facing-host/client` must show the other
player), then the card 554 soak: 20 jumps each, about 1.3 s apart.
- Each side logs at least one `card551: <side> ... skipped screen shake notify ... on <remote trooper>` row after the
  jump mark (row cap 24 per run, so the count stops there).
- 0 card551 rows whose pawn is that side's own trooper (host: the host's pawn; client: the client's own pawn).
- Void if the facing shots show a wall or no player.

### Run c566b372a diag predictions (b372) (senior 2026-11-21, UBWCOOP_C99_LOG=1, scenario `tools/scenarios/c373-c1m1.txt`)

Changed per the user (2026-11-21): jumps alternate, host then client, 10 each, about 350 uu apart. Each jump waits
for its own landing row (card490 `MovementMode 3`, then `MovementMode 1`, since-mark), then a 1 s gap; both sides mark
before each jump, so each landing has its own window. Each landing of
the local player logs one card099 `CameraManager::StartCameraShake` row with one caller exe+0x address on that side.
A row on the watcher's side inside the other player's landing window is the bug (the copy's landing shakes the watcher's
camera). Jump times: the mark, key and card490 landing wait steps in the .drive log.

### b373 fix: landing impact cue (senior 2026-11-21)

User, after the b370 run: "It client jumps the bounce will happen on host also when they land". Run c551b370a logged 0
card551 notify rows, so the landing shake is not the anim notify. Static trace: `ASTPlayerCharacter::Landed` calls
`USTGASUtilityLibrary::ExecuteGameplayCueFromHit`, which runs `USTImpactGameplayCueNotify::ExecuteImpact(Target, params)`
on every machine that simulates the pawn. ExecuteImpact calls `PlayScreenShake` (library StartCameraShake on player 0, the
local camera), `PlayForceFeedback` and `ApplyScreenEffect`. On the host the client pawn's landing therefore shakes the
host camera.

Fix in b373 `8dd8388aa82bcd57` (build-b5 copy + .map, = b372 + this), `player_c99.cpp`, 4 ungated hooks:
`detour_c551_execute_impact` scopes the Target for the call; PlayScreenShake and PlayForceFeedback skip, and
ApplyScreenEffect skips, when that Target is a trooper or mech whose controller is not this machine's local player
controller. Impact sound and particles still play. Proof row: `card551: <side> t=... skipped impact cue <what> of <cue>
on <remote pawn> (...)`.

Predictions (run 4, C1M1, b373, alternating jumps, UBWCOOP_C99_LOG=1):
- Each client landing window: host logs `card551: host ... skipped impact cue screen shake ... on BP_PlayerCharacterMI_C...`
  and no card099 StartCameraShake row.
- Each host landing window: the same on the client side, naming the host's trooper.
- Each side's own landings: still one card099 StartCameraShake row (own shake kept), 0 card551 skip rows naming its own
  pawn.

## b378 `3ed1d2468aee7c98` (= b377 + this fix + card 566 material load, build-b5 copy kept, not run yet)

Run c551b376a: the b373 ExecuteImpact gate skipped nothing on a remote landing; `Shake_Generic_Fall_C` still shook the
local camera through card099 caller exe+0x5251E77, which is inside `USTCommonUtilityLibrary::StartCameraShake`
(exe+0x5251D30, signature `UCameraShakeBase* (UObject* WorldContext, TSubclassOf<UCameraShakeBase> (by reference),
float Scale)`). That function shakes `GetPlayerController(WorldContext, 0)`, so it always hits this machine's own
player. Its callers: the Blueprint thunk, the shake anim notify, the impact cue, AttackAction landed/AOE/charge/melee,
`ASTPlayerCharacter::PlayHurtCameraShake`, `USTWeaponFireAbility::ApplyRecoil` and
`USTCombatUtilityLibrary::PlayCameraShakeWithRadius`.

Fix: a detour on the function itself. An actor WorldContext whose pawn (itself, or its Owner within 2 hops) is a
trooper or mech not controlled by this machine's player returns null without shaking (the game's own no-PC return).
A non-actor context runs unchanged. Rows: `card551: <side> t=N skipped StartCameraShake <shake> for <ctx> (<class>),
caller exe+0x...` (shared cap 24), and every Fall or Land shake that runs: `card551: <side> t=N ran StartCameraShake
<shake> scale S for <ctx> (<class>, actor | not an actor), caller exe+0x...` (cap 24).

Predictions (alternate-jumps rule: host jumps and lands, then client, repeated):
- Host log: a `skipped StartCameraShake Shake_Generic_Fall_C for <client pawn>` row per client landing; a `ran` row
  per host landing with the host pawn as context.
- Client log: the mirror (skips for the host pawn, runs for its own pawn).
- No card099 Fall shake row for a remote landing on either side. If one still shows, the matching `ran` row names the
  context class and caller, which is the next gate.
- Own shakes unchanged: own landing, own hurt and own recoil still shake (shots or card099 rows).

## b380 `149a113fdff1893a` (= b379 + this fix, build-b5 copy kept, not run yet)

Run c551b378a (b378): both screens still shook. The `ran StartCameraShake` rows show the context is the World, not a
pawn: `USTImpactGameplayCueNotify::PlayScreenShake` (exe+0x5133E50) passes the notify's own `GetWorld()`, so the b378
actor gate never sees a player. The b373 ExecuteImpact gate also missed: on a landing, the `Target` argument of
`ExecuteImpact` (exe+0x511E7A0) is the hit surface, not the pawn.

Fix: the ExecuteImpact detour takes its subject from `Target`, else `FGameplayCueParameters::GetInstigator`
(exe+0x49FCB90), else `GetEffectCauser` (exe+0x49FCB00, new symbol). When the subject is a remote player, the existing
shake, feedback and screen detours skip as before. Row (cap 16): `card551: <side> t=N ExecuteImpact target T (class),
instigator I, effect causer C: subject S, remote, feedback skipped | own`.

Predictions (alternate jumps, UBWCOOP_C99_LOG=1):
- Each remote landing: an ExecuteImpact row naming the landing pawn as instigator or causer, `remote, feedback
  skipped`, then the `skipped impact cue screen shake` row, and no `ran StartCameraShake Shake_Generic_Fall_C` row.
- Each own landing: the row ends `own`, and the Fall shake runs.
- If the instigator and causer are both null or not the pawn, the row shows what they are, and that is the next gate.

### Run c551b380a result and b382 `6b9ecfddb41035c5` (= b381 + this, build-b5 copy kept)

The 16 ExecuteImpact rows were all used on warps before the jumps. In them, Target was always the surface and
instigator and causer were always the trooper, so the b380 subject works inside ExecuteImpact (host: 1 remote row,
followed by `skipped impact cue screen shake`). But the client logged `ran StartCameraShake Shake_Generic_Fall_C` from
PlayScreenShake (caller exe+0x5133F6A) at t=146563309 with no ExecuteImpact row around it. PlayScreenShake also runs
outside ExecuteImpact; it is Blueprint-callable (`execPlayScreenShake`).

b382: the shake and force-feedback detours, when no ExecuteImpact is running, take the subject from their own cue
parameters (instigator, then effect causer). The subject row now has a `where` field and logs `no player` subjects too.
The caps are raised to 64 (subject rows, skipped rows, ran rows).

Predictions (jumps alone, alternating): each remote landing gives a subject row (`ExecuteImpact` or `PlayScreenShake
outside ExecuteImpact`) ending `remote, feedback skipped`, plus a skipped row, and no `ran ... Shake_Generic_Fall_C`
within 100 ms. If a `no player` row comes before a remote-time Fall shake, the cue parameters carry no pawn, and that
is the next gate.

## Run c551b383a (b383 `24422c58baea3ff8`, jumps alone): host passes, client fails

- Host: every client landing logs `skipped impact cue screen shake` and `force feedback` of
  Default__GC_Impacts_Human_Landing_C. No Fall shake runs except at the host's own landings (11 ran rows).
- Client: each of the 10 host landings is a card 136 Fx cue replay (`recv ... Fx`, the same ms). Its ExecuteImpact row
  has target BP_LandingPlatform_01_MilitaryBase_C and `instigator null, effect causer null: subject null, no player`,
  and the Shake_Generic_Fall_C runs from PlayScreenShake (exe+0x5133F6A). The replay builds fresh cue parameters with
  only the location, normal and physical material, so the subject is lost on the wire.

## b384 `6bb0bf21e5a76ccf` (= b383 + relayed player cue mark, build-b5 copy kept)

- Host (fx_c136_net.cpp capture): a cue row whose instigator is a trooper or mech (itself or its Owner) sets
  kC136FlagPlayerSource. Cue rows did not use bit 0x80 before; on sound rows it keeps its card 304 meaning. make_filter
  never sends a client its own pawn's cues, so the marked cue is always a remote player's.
- Client (client_play_cue): while a marked row replays, `g_c551_relayed_player_cue` is over 0, and c551_skip_feedback
  skips the screen shake, force feedback and screen effect. It logs `skipped impact cue <what> of <notify>: relayed cue
  of a remote player`. The decal, particles and sound still play.

Predictions (same scenario c551-c1m1-jumps, alternate jumps):
- Client: each host landing logs the relayed-cue skip rows for Human_Landing, and no `ran StartCameraShake
  Shake_Generic_Fall_C` row within 50 ms of the host landing. Its own landings still log `ran` (10).
- Host: unchanged from c551b383a.
- The landing sound still plays on the client at each host landing (card173 row present).

## Run c551b384a: passes, card closed (senior, 2026-11-21)

- Client, the host's landings: 11 sets of "skipped impact cue screen shake + force feedback of
  GC_Impacts_Human_Landing_C: relayed cue of a remote player" (t=149764107 to 149855402). No StartCameraShake ran
  within 50 ms of any of them.
- Client, its own landings: 10 "ran" rows (1 Shake_Generic_Fall_C at t=149762978, then 9 Shake_Enemy_HeavyLanding_C,
  caller exe+0x5133F6A). card173 rows: 11.
- Host: unchanged. The host's own landings ran Shake_Generic_Fall_C; the client copy's landings were skipped
  (Human_Landing and Human_Landing_Heavy).
- WARN classes against c551b383a: the same set (card537 x2 on both sides, card137 x1 client landing blocker, an old
  row), plus 2 card159 dropship rows on the client at t=149746691 (ProceduralDropshipTroops 4723 uu off, tolerance
  560, speed -7413; PlayerTransportDropship_VerySlow 2542 uu off), both in flight. Not 551.
- New find, carded separately: the client's own landings run Shake_Enemy_HeavyLanding_C 9 of 10 times while the
  host's own landings on the same jumps run Shake_Generic_Fall_C.

Status: closed on b384 `6bb0bf21e5a76ccf`.
