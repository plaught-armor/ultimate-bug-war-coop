# Card 166: The shooting client gets its hit marker for a claimed hit

Status: CLOSED, outcome in docs/CARDLOG.md. Was: queued 2026-11-09, from card 165 run 2 (c165d). Budget: three runs. Gameplay effect (all effects sent).

Read: card 157 item 3, card 165 "Run 2 result", mod/src/ubw/claim_c157.cpp.

## Why

In c165d two client claims applied once each on the host (Health 376 -> 336 -> 296, matched on the client) and
the client's hit-react ran. The hit marker did not: ASTPlayerCharacter_OnDamageCaused fires only on the host's
server copy ("OnDamageCaused on None (other) damage 40.00") and logged 0 calls on the client. The marker event is
not relayed to the shooter.

## Do

1. Static first: what OnDamageCaused drives on the shooter's side (widget, sound, its arguments), and whether the
   game normally reaches the owning client through a client RPC that the mod's netcode drops.
2. Relay it: when the host applies a claim (or the server copy's OnDamageCaused fires for a client's pawn), the
   client runs its own pawn's OnDamageCaused with the same damage, causer and target, through the game's own path.
3. Check the sibling events of the same class (kill confirm, headshot, any other shooter-side damage feedback) in
   the same pass.

## Done when

- A run: a client claimed hit on a bug shows the marker on the client (log line of the client's OnDamageCaused with
  the host's damage), once per hit, and a kill shows its kill feedback if the game has one.
- The usual Report items (WARN diff, run length, rc, ERROR/FATAL, CRASH, dumps, DLL hash). It ends with "Report
  complete."

## Static trace (junior)

- WB_Hit_Marker (/Game/Blueprints/UI_Aloft/Gameplay_HUD/WB_Hit_Marker) binds two delegates of
  GetSTPlayerCharacter: ASTCharacter.OnCharacterCausedDamage (+0x670, param FADDamageEvent DamageData) and
  ASTCharacter.OnCharacterKilledNPC (+0x660, CharacterInstigator, EnemyKilledTag; event "Kill Caused"). It reads
  DamageTypes (DamageType.NoHitmarker, DamageType.LimitedHitmarker), ReceiverTags (Actor.Hitmarker.NoMarker,
  Actor.Hitmarker.NoDeath), FinalDamage, ValueMap DamageModifier.CustomModifierAmount (weakpoint / strongpoint /
  reduced), an object compare with the player character, and USTGameUserSettings IsHitIndicatorEnabled. Animations
  Hit_Regular, Hit_Weakpoint, Hit_StrongPoint, Hit_NoDamage, Hit_Kill; sounds MS_GORE_2D_*_Hitmarker.
- The broadcasters: ASTCharacter::OnDamageCaused (exe 0x1450F6FC0) is `add rcx, 0x18; jmp
  FCharacterDamageCausedDelegate_DelegateWrapper`, and ASTCharacter::OnDeathCaused (0x1450F7040) builds the kill tag
  from the event's ReceiverTags and calls FCharacterKilledNPCDelegate_DelegateWrapper (call at 0x1450F70F9). Both
  are IADDamageCauserInterface methods: `this` is the subobject at character +0x658. These are the only callers of
  the two wrappers outside the BP thunks.
- ASTPlayerCharacter::OnDamageCaused (0x1451CF450) calls the base, then (FinalDamage > 0 and a valid Receiver)
  updates the receiver's ability system tag counts (FGameplayTagCountContainer::UpdateTagMap_Internal) and checks
  ASTEnemyBase / UADFactionComponent::IsEnemyFaction. That is host game state.
- The receiver's damage code calls the event Instigator's OnDamageCaused / OnDeathCaused. Damage runs only on the
  host, so for a client's claimed hit both run on the host's server copy: c165d's two "OnDamageCaused on None
  (other)" lines are the two applied claims (shot 10 and 11, the same tick each). "None (other)" is because the
  card 157 diag names the +0x658 subobject, not the pawn. The game has no client RPC for either: nothing reaches
  the client, so the marker never plays there. It is single-player authority code, not a dropped RPC.

## Fix (built in DLL a4b6ea6a5a7d1e8e, batched with card 161's class fix)

- mod/src/ubw/marker_c166.cpp, new ChanOp::Marker (host to client, text, no world state).
- Host: detour_c157_damage_caused (after the original) and a new ASTPlayerCharacter::OnDeathCaused detour. When the
  pawn (self - 0x658) is a server copy, one Marker frame goes to its PC: "D|K <NPC guid> <original> <final>
  <health> <armour> <counts>", the value map entries by tag name, the DamageTypes and ReceiverTags names. Line:
  "card166: host t=... OnDamageCaused|OnDeathCaused on server copy ... to <pc> (sent): <text>".
- Client: the frame rebuilds the event (Instigator and Causer the own pawn, Receiver the NPC copy by guid and its
  health component, the tag containers and value map from the names) and calls the base ASTCharacter
  OnDamageCaused / OnDeathCaused on the own pawn, which only broadcast the delegate. The ASTPlayerCharacter
  versions are not called on the client (they write host game state). Line: "card166: client t=... OnDamageCaused|
  OnDeathCaused on own pawn ... damage <final>".
- Sibling events: headshot / weakpoint is the same event (ValueMap CustomModifierAmount, sent); the kill is
  OnDeathCaused (relayed). No other shooter-side damage feedback binds these delegates.

## Predictions for the run (c166a, with the card 161 door proof)

1. Every host "card157: host ... HitClaim shot N ... applied to BP_AI_WarriorBug_C" has one host "card166: host ...
   OnDamageCaused on server copy ... (sent)" line in the same ms, and one client "card166: client ...
   OnDamageCaused on own pawn ... damage 40.00" line (the host's final damage), so the counts match.
2. The kill: one host "OnDeathCaused on server copy" line on the claim that took the bug's Health to 0, and one
   client "OnDeathCaused on own pawn" line.
3. A client screenshot taken right after a burst shows the hit marker; the client's map totals show 0 bad, 0 NPC
   not mapped.
4. The client's card157 "OnDamageCaused on" diag stays at 0 lines (the client calls the base version).
5. Card 161: with the client standing in the door (-3674, 9849), the host's close gives the client's 3 -> 0 within
   about 50 ms of the host's; no "client-only DoorState" line in the whole run; map totals 0 set back.

Risks: IsHitIndicatorEnabled off in the client's settings would hide the marker with the log lines present; the
kill tag the base OnDeathCaused builds from the rebuilt ReceiverTags may differ from the host's (logged by name).

## c166b result and c166c changes (junior)

- c166b: host 24 OnDamageCaused + 1 OnDeathCaused "(sent)", client 25 received, 25 applied (24 damage, 1 kills),
  0 without own pawn, 0 NPC not mapped, 0 bad. The client's card157 "OnDamageCaused on" count is 0.
- Visual check failed: client-c166b-c166-client-marker-1.png was taken 34 ms after an applied pre-death event
  (283500736 vs the shot at 283500770, no Actor.Hitmarker.NoMarker tag) and shows the crosshair only. Shots 2-4
  come after the kill (NoMarker tag on the events), so no marker is expected there.
- Host crash at the travel: AV in object_name from net_budget_arm_map. g_net.rel_map_class held a UClass pointer
  that the travel's LoadMap freed. Fix: net_budget keeps the class name (ObjectName) at note time.
- c166c diag (DLL dcdd3143fd991394): on the first 2 applies the client logs the own pawn's +0x670 and +0x660
  delegate invocation lists (bound object and function). The scenario adds a host reference burst and shot
  (host-c166c-c166-host-marker).

Predictions for c166c:
1. Travel with no crash; the host prints "card166: host marker relay map totals" with sent equal to the client's
   received, 0 not sent.
2. The binding lines tell the cause. If +0x670 shows 0 bound, or a WB_Hit_Marker bound to another pawn, the
   widget was bound before this pawn existed, and the fix is a rebind. If WB_Hit_Marker is bound to this pawn, the
   cause is past the broadcast (the widget's own checks: IsHitIndicatorEnabled, the player character compare).
3. The host shot shows the host's own marker; if it doesn't show one either, a still shot at this size cannot catch
   the marker and the check must use a different method.

## Review after c166a-c166c (senior, 2026-11-09)

Closed partial: three runs used. The relay works (23 of 23 applied, WB_Hit_Marker bound to the own pawn on both
delegates), but no marker shows in the client stills. Two runs looked for it with stills; the method changes.
Remainder in card 168.
