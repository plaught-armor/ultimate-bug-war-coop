# Card 558: Razorback animations still do not play (reopens card 297's class)

Playtest 2026-11-20 on b366 (`e62e76f63a05d88f`), FED_C2M1. User report: "razorback animations still don't play".
Card 297 (done) fixed the client pilot's hidden cockpit arms: `c297_swap` (`attributes.cpp:1757`) shows the arms
with the exe's `SetMeshVisibility`, and its proof was the arm gun firing in c447b177c.

## Client log (`UBWCoop-menu-20261120-184517.log`)

- The client piloted mech 13516 from 101573910 to 101617114 (card303 MechEnter / MechLeave rows).
- 101573910 `card297 ... enter: FirstPersonMesh ... IsVisible 0 IsComponentTickEnabled 0, anim instance
  ABP_Player_RazorbackMech_Cockpit_C`. 101573917: the swap made the arms visible (0 -> 1). The row does not log the
  tick state after the swap.
- No `card297 ... shot` rows in that window, and the attack tag count is 0 at enter and at exit.
- Before that, from 101545581, the host drove a `BP_BaseMech_C` (card416 put the remote player's ring on it).

## Candidates (unproven)

1. The arms are shown but their component tick stays off (`IsComponentTickEnabled 0` at enter): the anim instance
   never updates, so walk bob, idle and saw poses stay frozen. Card 297's proof only covered firing.
2. The mech the host drives, seen by the client (third-person body and legs), has no animation: card 297's sibling
   rows "host pilots, seen by the client" were never proven.

## Question for the user

Which animations: the client's own cockpit arms while piloting, or the host's mech seen from outside, or both?

## Next step (after the playtest)

Static: what the game's enter does to the arms' tick (OnBoardingComplete next to the +0x19A mesh swap) and what
drives the third-person body. Then one run: client pilots, walks, fires and saws; host pilots in view of the client.
Rows: arms tick state and anim instance montage per second on the pilot, body animation state on the viewer.

## Prediction

The fix shows the arms tick on (0 -> 1) at the client's enter and a changing animation state each second while
walking, and the viewer's copy of a remote mech plays its walk.

## Host log (`ultimate-bug-war-coop-public/logs/UBWCoop-menu-20261120-184651.log`)

The host log has no card297 rows and no per-mech animation rows. It cannot say whether the host's copy of the
client's mech animates. The question for the user and the run above stay open.

## User answer (2026-11-20)

"the seen mech from outside": the bug is candidate 2. The client sees the host's mech (third-person body and legs)
with no animation. The client's own cockpit arms are not the report. The next run: the host pilots and walks in view
of the client, with a row each second for the body animation state of the client's copy of the mech.

## b369 (`4d7adc330945a079`, build-b5 copy kept), not run yet

No fix yet: the play logs cannot name the cause. The trace run in "Next step" above is still needed.

## Static trace and fix (senior 2026-11-21)

- `ASTPlayerMechBase::OnBoardingComplete` (+0x19A, +0x1AC) calls `ASTPlayerCharacter::SetMeshVisibility` on
  FirstPersonMesh (+0x848, true) and the second mesh (+0xC20, false), then `AttachControllingPlayerCharacterToMech`.
  It has no tick call.
- `SetMeshVisibility` (exe+0x51DBB20) runs two calls on the component: vtable +0x2C8, which is a `ret` stub on
  USkeletalMeshComponent, and `USceneComponent::SetVisibility(visible, true)`. It never enables the tick either.
- So candidate 1 holds: the client's arms keep `IsComponentTickEnabled 0` (every client card297 enter row in logs/),
  and the cockpit ABP never updates.
- Side finding: ASTPlayerMechBase calls the protected ASTPlayerCharacter member on itself, so a mech is an
  ASTPlayerCharacter. b374 `pawn_interactor` gates on that class.

Fix in b375 `7c9524e1d2eed647` (build-b5 + .map) = b374 + this. On enter, `c297_swap` calls
`UActorComponent::SetComponentTickEnabled(true)` (vtable +0x410, the same slot card 532 uses) when the arms' tick is
off. The card297 fix row now logs `arms tick <before> -> <after>`.

Predictions (client pilots on C2M1 or a map with a mech, b375):
- `card297: client ... enter (possessed): FirstPersonMesh visible 0 -> 1, ..., arms tick 0 -> 1`.
- Shots while walking, firing and sawing show cockpit arm poses that change between shots.
- Candidate 2 (the host's mech seen by the client) still needs its own shot.

### Run c558b376a predictions (junior, 2026-11-21, b376 `eb49926909111446` = b375 + card 568)
Scenario `tools/scenarios/c375-c4m1-mech.txt` (FED_C4M1: two pod mechs from the map start, no objective route).
- Client enter: `card297: client t=... mech ... enter (possessed): FirstPersonMesh visible 0 -> 1, ..., arms tick 0 -> 1`.
  `arms tick 1 -> 1` would mean the tick was already on, so the fix did not apply here and the cause is elsewhere.
- Shots `558-client-idle`, `558-client-walk1`, `558-client-walk2`, `558-client-fire1`/`fire2` (LMB) and
  `558-client-saw1`/`saw2` (RMB, `IA_MechSecondaryAbility`): the cockpit arm pose differs between idle, walk, fire
  and saw, and between the two shots of each pair.
- Candidate 2 (no fix yet): shots `558-host-mech-idle`, `558-host-mech-walk1` and `558-host-mech-walk2`, the
  client's view of the host's mech from 600 uu. Same leg pose in walk1 and walk2 = candidate 2 still open.

## Run c558b377a predictions (b377 bc12138adfaeb5ba, 2026-11-21)

Rerun of c558b376a with the warp regexes fixed and mech A walked off its pod first (lesson 45). c558b376a already
showed the fix row (`arms tick 1 -> 1`, arms visible in the cockpit shots). The open item is candidate 2: the
`558-host-mech-walk1` and `walk2` shots show mech A on the ground, in frame, with the leg pose different between the two.
If the legs stay in the same pose in both shots, candidate 2 is real.

## Candidate 2 result (junior, 2026-11-21): no animated third-person mech body exists

- c558b429a (b429 `a719680599a35ca0`): the first card558 row read the wrong components. ACharacter::Mesh (+0x328) is
  null on `BP_BaseMech_C`, and +0xC20 is a SpriteComponent that stays hidden (visible 0) on the client whether the mech
  is empty or piloted.
- c558b430a (b430 `961c941ae5a47531`): the row now lists the "Mech" component and every SkeletalMeshComponent. The
  client's rows for the host's piloted mech, during the walk (CMC velocity up to 1256 uu/s), show 31 components, and
  only 3 are listed:
  - `FirstPersonMesh` (SkeletalMeshComponent): visible 0, tick 0, anim `ABP_Player_RazorbackMech_Cockpit_C`.
  - `1PArmsSKM` (SkeletalMeshComponent): visible 0, tick 0, anim null.
  - `Mech` (StaticMeshComponent): visible 1.
- Cause: seen from outside, the mech is one static mesh. The only skeletal meshes are the cockpit's first-person
  parts. The game has no leg animation for a third-person mech, because single player never shows a piloted mech from
  outside. So this is not a sync gap. Sync already moves the mech (its velocity and position), and the client sees it
  slide with its legs still. Shot `client-c558b430a-558-host-mech-walk1` shows the host's mech standing upright with a
  "Player" tag.
- Options for the senior or the user: (a) accept it as a vanilla asset limit; (b) a base-game side mod that fakes a
  walk (a bob or tilt of the "Mech" component scaled by velocity). Real leg animation would need a skeletal mech asset,
  and the game ships none.

## Closed 2026-11-21 (user choice: accept as vanilla)

The game ships no third-person skeletal mesh for a piloted mech (`ACharacter::Mesh` null, only the static Mech
mesh), so a piloted mech seen from outside slides with still legs in single player too. The user accepted this as a
vanilla limit. No fake walk mod.
