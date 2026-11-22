# Card 560: tactical nuke (guided missile) leaves the client stuck in a third-person view

Playtest 2026-11-20 on b366 (`e62e76f63a05d88f`), resort map. User report (client): "when i dropped tactical nuke i
am now stuck in some weird over the shoulder third person". Screenshot `Screenshot_20261120_193630.png`: a pixel
sprite trooper body fills the view in front of the camera.

## Client log (`UBWCoop-menu-20261120-184517.log`)

| Client t (ms) | Row |
|---|---|
| 102317541 | flare throw (tac support call); 102318114 card174 "TryThrowFlare not run here (the host's server copy throws)" |
| 102321704-102321780 | SetPawn null, then SetPawn / OnRep_Pawn / AcknowledgePossession / ClientRestart of `BP_GuidedMissile_C`; minimap origin moves to the missile |
| 102321761 (line 75275) | `card028: client attached to BP_PlayerCharacterMI_C (index 88560, role 1): renderer STSpriteRenderComponent ...`: the client's OWN trooper got a remote sprite body |
| 102327993 | minimap origin back on `BP_PlayerCharacterMI_C` |
| 102328035 | `pawn-collision: client own pawn ... BP_PlayerCharacterMI_C (new pawn) ... last hook SetPawn` |

No later row removes the card028 sprite body from the trooper, and no row restores the first-person view.

## Cause (static, `remote_body.cpp:689` `c28_remote`)

While the client possesses the missile, its own trooper has no controller and replicates as a simulated proxy
(role 1). `c28_remote` then treats it as remote unless it is attached to a pawn the local player drives (the card 125
turret case). The missile is not an attach parent of the trooper, so the trooper counts as remote and gets a sprite
body. On repossession nothing takes the body off: card 028 removes a body only when the actor ends. The camera sits
behind the sprite, so the view looks like a stuck over-the-shoulder camera.

## Fix direction

1. Decide "remote" by owner, not by current control: a pawn whose PlayerState (or last possessor) is the local
   player is never remote, whatever its role or controller at the moment. This replaces the card 125 attach-parent
   special case with the class rule.
2. When a pawn with a card028 body becomes the local player's pawn again (possess or OnRep_Pawn), remove the body and
   restore the pawn's own mesh visibility. Logged once per removal.
3. Sibling sweep, both sides: every temporary possession that leaves the player's trooper unpossessed (guided
   missile, mech, turret, vehicle). Check also the host side: the host's own trooper while the host flies a missile.

## Prediction

Client calls a tactical nuke, flies the missile, returns: no card028 attach row for its own trooper, first-person view
back on return. Host copy of the client's trooper still gets its body as before.

## User follow-up

The user left the session and rejoined to get out of the view. The rejoin's new pawn has no card028 body. A rejoin
is not a fix.

## Host log (`ultimate-bug-war-coop-public/logs/UBWCoop-menu-20261120-184651.log`, host t = client t + 241024698)

- 343346373: launch sound of `BP_GuidedMissile_C` skipped on the server copy (card160); 343346376 `card416: host ring
  slot 1 hidden`: the host hides the client's ring while it flies, correct.
- From 343346376: `WARN card490: host pawn class BP_GuidedMissile_C: Stamina at -1` every frame of the flight (card
  562).
- No host row touches the client's trooper body: the bug is client side only, as the cause above says.

## b369 (`4d7adc330945a079`, build-b5 copy kept), not run yet

Fix: `c28_step` records the local player's on-foot pawn (`g_c28.own_trooper`), and `c28_remote` never treats it
as remote, even after unpossess clears its PlayerState. `c560_drop_body` removes a sprite body that was already built
on it and unhides its FirstPersonMesh and weapon. Proof row: `card560: ... removed`.

### Run c560b371b predictions (c560b371a crashed at 60 s, host still in the dropship; same predictions) (junior, 2026-11-21, b371 `baeebee09d3e7f59`, scenario `tools/scenarios/c372-c3m1.txt`)

The client takes a guided missile pickup by its own Use press, throws it with Five (tac slot 1), flies the missile
and returns.
- Client: `card303 possession SetPawn pawn BP_GuidedMissile_C`, then `SetPawn pawn BP_PlayerCharacterMI_C`.
- Client: no `card028: client attached to BP_PlayerCharacterMI_C` row for its own trooper during or after the
  flight. A `card560: ... removed` row only appears if a body was built anyway; either way, after the return no body
  stays on the own trooper.
- Shot `c372-560-back-client`: first person view (arms and weapon), no sprite trooper in front of the camera.
- Host: shot `c372-560-back-host` shows the client's trooper with its body as before.

## Result c560b371b (senior, 2026-11-21, b371 `baeebee09d3e7f59`): passed

- Flight: client SetPawn `BP_GuidedMissile_C` at 142728560. The missile is destroyed at 142734995. The client gets
  AcknowledgePossession and ClientRestart of its own trooper `0000000053DBF1D0` at 142735195-142735196, and
  `pawn-collision: client own pawn ... BP_PlayerCharacterMI_C` at 142735402.
- No card028 attach row names the client's own trooper during or after the flight. The three card028 rows
  (142724511, 142821799, 142890568, indices 229980, 225231, 224821) are role 1 (simulated proxy) troopers, while the
  client's own trooper is role 2. No body was built, so no `card560: ... removed` row, as predicted.
- Shot `client-c560b371b-c372-560-back-client.png`: first person, Morita Mk1 held, HUD 58 / 240, no sprite trooper
  in front of the camera.
- Scenario step 42 failed for a harness reason, not a game one. `card303 possession` rows log a trooper only while
  a mount press is open (`c303_possession`, `stall_c303.cpp`), so the return to the trooper never logs there. Line 42
  of `tools/scenarios/c372-c3m1.txt` now waits for `possess: ClientRestart(...) Pawn ... BP_PlayerCharacterMI_C`,
  which matched the return in this log.

Closed. The fix has been in every build since b369.
