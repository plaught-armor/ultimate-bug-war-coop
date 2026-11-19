# Card 174: The client sees the support flare in its own hand during the throw

Status: queued 2026-11-11, from card 172 (run c172-r1) and card 169's open prediction. Budget: static first, one run.

Read: docs/cards/done/172-client-flare-press.md "Report"; docs/cards/done/169-ability-spawn-visuals.md;
tools/scenarios/c172-flare.txt (reuse it).

## Why
The client's flare press runs on the host's server copy (the target data cannot be net-serialized, card 100). The
flare throw ability and its BP_FlareVisuals_C therefore exist only on the host: c172-r1 host "card169: held
BP_FlareVisuals_C of the server copy destroyed during first person montage AM_FlareThrow at 1.747 s", no client
"card169: client ... spawned" line, and client-c172-r1-c172-flare-held.png shows the Morita and no flare. The host
also logs a WARN for that visual ("rendered in the last 0.2 s 0"), a new WARN class in this run shape.

## Senior trace (2026-11-11)
- Verified (mod/src/ubw/vehicles.cpp `detour_c100_tac_input`): on the client the original
  HandleTacticalSupportInput never runs for its own pawn, so the client's GA_UseTacticalSupportAbility never
  activates; only the slot number goes to the host. Reason in the comment there: the client's activation sends
  STGameplayAbilityTargetData_TacticalSupportInput, which has no NetSerialize (LowLevelFatalError,
  GameplayAbilityTargetTypes.cpp:240).
- Verified (mod/src/ubw/spawn_c169.cpp `detour_c169_begin_spawning`): the held visual is spawned on the client only
  for a client-own ability that runs locally. The grenade and knife abilities run locally and on the host; the flare
  runs on the host only, so the detour never sees it. The throw montage on the first-person arms is missing for the
  same reason (client-c172-r1-c172-flare-held.png shows the Morita held).
- Verified (c172-r1 host log): the host WARN is for the server copy's visual on the copy's FirstPersonMesh, which
  the host never renders. It is expected for a server copy.
- Open: the exact call that sends the target data on the client (candidate
  UAbilitySystemComponent::CallServerSetReplicatedTargetData), and what else the local ability does on the client
  (TryThrowFlare from OnDoAction, charge spend, the weapon re-equip at the end).

## Do
1. Static: close the two open lines above with addresses and kis.py lines, in this card, before code.
2. Preferred shape (same as the grenade): the client's own ability runs locally for its cosmetic side (montage,
   BP_FlareVisuals_C through the card 169 detour), the target-data send is dropped on the client for this struct
   only, and the client's local TryThrowFlare does not spawn a flare (the host's server copy still owns the flare,
   the charge and the strike through the TacSupport op). If the trace shows the local ability cannot run without
   the send, stop and report instead of building a relay.
3. The host WARN for a server copy's first-person visual becomes a plain capped line; it stays a WARN for the
   host's own pawn and for the client's own.

## Done when
- c172-flare.txt: the client logs the held flare spawned and rendered, and the held screenshot shows it. The host
  WARN is gone or explained and reclassified.
- The usual Report items. Ends "Report complete."

## Files
mod/src/ubw/spawn_c169.cpp, mod/src/ubw/vehicles.cpp (`detour_c100_tac_input`), hooks_internal.h, hooks_ubw.cpp and
tools/symbols.txt if a hook is needed,
tools/scenarios/c172-flare.txt.

## Trace (junior, 2026-11-11)
Stop condition hit: dropping the send needs a detour that skips an original which frees something. No code written.

**The call that sends the target data.** It is not CallServerSetReplicatedTargetData. The client's predicted
activation sends the event data, with the target data inside it, as an argument of the activation RPC:
- UAbilitySystemComponent::InternalTryActivateAbility (exe+0x4963860), client predicting path: FScopedPredictionWindow,
  then SetPredicting, then at exe+0x4963d05 `??0FGameplayEventData@@QEAA@AEBU0@@Z` copy-constructs the trigger event
  data into a by-value temporary at -0x28(%rbp).
- At exe+0x4963d2e it calls ServerTryActivateAbilityWithEventData (exe+0x4972180) with that temporary. Without event
  data the same path calls CallServerTryActivateAbility (exe+0x4951030, at exe+0x4963d54) instead.
- Later, at exe+0x4963e8f, it calls CallActivateAbility: the local activation runs after the send.
- The RPC stub ServerTryActivateAbilityWithEventData (exe+0x4972180): FGameplayEventData operator= into the parms
  (exe+0x497216c), FindFunctionChecked, then ProcessEvent/CallRemoteFunction through `*0x260(%r9)`. The net
  serializer of that call reaches STGameplayAbilityTargetData_TacticalSupportInput, which has no NetSerialize.
- The stub then destroys its by-value argument: `??1FGameplayEventData@@QEAA@XZ` (exe+0x49261a0) is called at
  exe+0x49722e0, exe+0x497232c and exe+0x4972349. Under MSVC x64 the callee owns a by-value argument and destroys it.
- So a detour that skips the original for this struct leaks the copy: its TargetData handle (an array of
  TSharedPtr<FGameplayAbilityTargetData>, which holds the TacticalSupportInput) and its tag containers.

**Everything else the local GA_UseTacticalSupportAbility does on the client, if it ran.**
- USTUseTacticalSupportAbility::StartAbility (exe+0x5139840): GetOwningActorFromActorInfo, then
  USTAbilityTask_PlayAnimation::PlayAnimation (exe+0x50fafb0) and ReadyForActivation (the throw montage), then two
  dynamic delegate binds (`__Internal_AddDynamic` for DoAction and AnimationEnded), then OnThrowStarted
  (exe+0x50b4b90), the Blueprint event.
- The Blueprint (kis.py on GA_UseTacticalSupportAbility) is visual only:
  - OnThrowStarted: cast the owner to BP_Player_Character_MI, then BeginSpawningActor of BP_FlareVisuals_C (the class
    literal), on Success attach Flare Visuals to FirstPersonMesh socket hand_r, then AddActorLocalTransform(90, 0, 0).
    This is the spawn the card 169 detour already handles for a client-own ability.
  - OnDoActionTriggered and K2_OnEndAbility: destroy Flare Visuals.
- USTUseTacticalSupportAbility::OnDoAction (exe+0x512fde0): OnDoActionTriggered (exe+0x50b4b60, the Blueprint
  destroy), then a weak-pointer Get of the TacticalSupportComponent, then **TryThrowFlare (exe+0x510a5b0) at
  exe+0x512fe17**. A local run on the client calls TryThrowFlare, which spawns the flare and spends the charge. It
  would need a new hook to block it for the client's own pawn (not in tools/symbols.txt today).
- Not traced: the weapon re-equip at the ability's end, and what a predicted activation that the server never hears
  of leaves behind (prediction key never acked, no ServerEndAbility match). Both need tracing before a build.

**Options for the senior (none built).**
1. Detour ServerTryActivateAbilityWithEventData: for this struct on the client's own pawn, skip the original and call
   `??1FGameplayEventData@@QEAA@XZ` (exe+0x49261a0) on the argument ourselves. This replaces the original's free with
   the same destructor. It needs one new symbol and one hook row, plus a TryThrowFlare hook (a second new symbol) to
   block the local throw.
2. Clear the TargetData in the copy and call the original. Rejected: the host would then activate the ability on the
   server copy from that RPC, a second activation next to the TacSupport op's HandleTacticalSupportInput.
3. Keep the ability on the host only and show the visual on the client by a cosmetic spawn of BP_FlareVisuals_C on
   the client's FirstPersonMesh hand_r, timed from the press to the host's throw. This is a new effect path, which the
   card's rules do not allow without the senior's say.

## Trace, closed before coding (junior, 2026-11-11; senior chose option 1)
- How the local ability ends without a server confirm. USTUseTacticalSupportAbility's ActivateAbility is
  UADGameplayAbility::ActivateAbility (exe+0x4a0edb0): Super, IsLoading, then StartAbility (vtable 0x4a0) and a
  Blueprint event. It calls no CommitAbility and applies no cost effect. StartAbility plays the montage
  (PlayAnimation task), binds its events and calls OnThrowStarted, which spawns the Flare Visuals through the card 169
  path. OnAnimationEnded (vtable 0x4c0, exe+0x512e9d0) calls EndAbility(bReplicate 1, bCancelled 0), the base
  UGameplayAbility::EndAbility (exe+0x492b360). The Blueprint's K2_OnEndAbility destroys Flare Visuals. So the local
  ability ends by itself when its montage ends; nothing waits for the server.
- The prediction key. With the send skipped, the key is neither confirmed nor rejected; it carries no predicted
  state (no effect, no attribute change, no tag from a cost). The local EndAbility's ServerEndAbility reaches the
  host's RemoteEndOrCancelAbility (exe+0x496fda0), which matches an instance by prediction key Current (+0xd8) and
  bIsServerInitiated (+0xdc). The server copy's instance has key 0 (it runs from TacSupport, server initiated), the
  client's key is non-zero, so the match fails and nothing is ended on the host.
- The weapon re-equip at the end. There is none to undo: the throw is montage-only. The ability never unequips the
  weapon (no switch request, no RemoveWeapon call in StartAbility, OnDoAction or EndAbility). The first person weapon
  stays equipped through the throw.
- The charge. OnDoAction (vtable 0x4b8, exe+0x512fde0) calls OnDoActionTriggered and then TryThrowFlare
  (exe+0x512fe17). TryThrowFlare is the only spend and the only flare spawn on the path; it is blocked on the client
  for the client's own pawn. No inventory or objective call is on the path.
- Stop condition: not met. The local ability ends by itself, and the local run changes no inventory, charge or
  objective state.
- Skipped original: ServerTryActivateAbilityWithEventData (exe+0x4972180). Its arguments: rcx the ASC, edx the
  handle, r8b InputPressed, r9 a pointer to the FPredictionKey copy (16 bytes, read with movups, no destructor), and
  at stack +0x28 a pointer to the caller's FGameplayEventData temporary, which the callee owns (x64 ABI). The stub
  destroys its parms locals and then calls ~FGameplayEventData (exe+0x49261a0) on that argument at exe+0x49722e0. The
  other two destroy sites (exe+0x497232c, exe+0x4972349) are its unwind thunks. So the skip calls the destructor once
  on the event data and owns nothing else.

## Predictions (run c175-r1, scenario tools/scenarios/c175-flare-fire.txt)
1. Client: "card100: client t=… own HandleTacticalSupportInput(slot 0), local charge 20000: run here for the local
   throw …; sent to the host's server copy (1 sent)".
2. Client: "card174: client t=… own BP_PlayerCharacterMI_C…: ServerTryActivateAbilityWithEventData(handle …) not
   sent …" once, and no LowLevelFatalError.
3. Client: "card169: client t=… …: spawned BP_FlareVisuals_C…", then its destroy line without WARN ("rendered in the
   last 0.2 s 1").
4. Client: "card174: client t=… own …: TryThrowFlare not run here …" once, and no client "card104: client … flare
   BP_SupportFlare_C" of its own (the only BP_SupportFlare_C line is the "copy of host" one).
5. Screenshot client-c175-r1-c175-flare-held.png shows the flare in the first person hand.
6. Host: one "server copy DoAction, key Flare_Throw", one "flare BP_SupportFlare_C ActivateFlare" and TacCharge
   "20000 1" then "0 0" once.
7. Host: "card169: host t=… held BP_FlareVisuals_C (BP_FlareVisuals_C) destroyed …" without the WARN prefix.

## Report

### Files changed

- mod/src/ubw/spawn_c169.cpp:
  - New: detour_c174_server_try, c174_tac_spec and c174_block_throw, with capped card174 lines (20).
  - The c169 destroy line no longer uses the WARN prefix for a server copy's held visual.
- mod/src/ubw/hooks_internal.h: the card 174 typedefs, externs and declarations.
- mod/src/ubw/vehicles.cpp:
  - detour_c100_tac_input now runs the original on the client's own pawn and still sends TacSupport.
  - detour_try_throw_flare returns early when c174_block_throw is true.
- mod/src/ubw/hooks_ubw.cpp: hook rows for UAbilitySystemComponent_ServerTryActivateAbilityWithEventData (exe+0x4972180), and call rows for FGameplayEventData_Destructor (exe+0x49261A0) and USTUseTacticalSupportAbility_StaticClass (exe+0x50B1D50).
- tools/symbols.txt: the same three rows. mod/src/offsets.generated.h was regenerated.
- tools/scenarios/c175-flare-fire.txt (new, shared with card 175).

### What was done

Option 1, as traced above:
- On the client, HandleTacticalSupportInput runs the local predicted GAS ability, so the client spawns its own BP_FlareVisuals_C and plays the throw montage.
- The ability's ServerTryActivateAbilityWithEventData is not sent. Its target data cannot be net-serialized, and the host's server copy already runs the ability from TacSupport. The by-value FGameplayEventData is destroyed once, and FPredictionKey has no destructor.
- The client's own TryThrowFlare is blocked, so the only BP_SupportFlare_C is the host's thrown one, which reaches the client as a card 104 copy.

### Predictions (run c175-r2; c175-r1 showed the same lines for this card)

1. Holds. Client: "card100: client t=86824222 own HandleTacticalSupportInput(slot 0), local charge 20000: run here for the local throw (no server send, no local flare); sent to the host's server copy (1 sent)". Host: "card100: host t=86824239 TacSupport slot 0 from BP_PlayerController_C: server copy BP_PlayerCharacterMI_C, ...; HandleTacticalSupportInput run, charge 20000 -> 20000".
2. Holds. Client: "card174: client t=86824222 own BP_PlayerCharacterMI_C: ServerTryActivateAbilityWithEventData(handle 130) not sent (the host's server copy runs it from TacSupport); local prediction runs (1 skipped)".
3. Holds. Client: "card169: client t=86824222 GA_UseTacticalSupportAbility_C: spawned BP_FlareVisuals_C (BP_FlareVisuals_C) for its own ability", then "card169: client t=86825097 held BP_FlareVisuals_C (BP_FlareVisuals_C) destroyed after 875 ms: rendered in the last 0.2 s 1, 40 cm from the camera, attached to FirstPersonMesh socket hand_r, root visible 1" with no WARN.
4. Holds. Client: "card174: client t=86825097 own BP_PlayerCharacterMI_C: TryThrowFlare not run here (the host's server copy throws) (1 blocked)". The client's only BP_SupportFlare_C is the copy: "card104: client t=86826740 copy of host 124170: /Game/Blueprints/Combat/BP_SupportFlare.BP_SupportFlare_C at (11784, 16025, -9) -> BP_SupportFlare_C".
5. Holds. client-c175-r2-c175-flare-held.png shows the red flare in the first-person right hand, with the HUD charge at 20,000.
6. Holds.
   - Host: one "server copy DoAction, key Flare_Throw ... DoAction count 1" at 86825097.
   - One "card098: host spawn BP_SupportFlare_C".
   - One "card104: host t=86826726 flare BP_SupportFlare_C ActivateFlare".
   - TacCharge "20000 1 -1 0 -1 0 -1 0" at 86823455, then "0 0 -1 0 -1 0 -1 0" at 86826796.
   - In c175-r1 the host logged DoAction count 2 (86470568 and 86470723) and still had one spawn and one ActivateFlare.
7. Holds. Host: "card169: host t=86825097 held BP_FlareVisuals_C (BP_FlareVisuals_C) destroyed after 858 ms: rendered in the last 0.2 s 0, ..." without the WARN prefix.

### Skipped originals

- ServerTryActivateAbilityWithEventData, on the client's own pawn and only for a USTUseTacticalSupportAbility spec.
  - The host's server copy runs the same ability from the TacSupport frame.
  - The one by-value arg with a destructor (FGameplayEventData) is destroyed once by the detour.
  - The 16-byte FPredictionKey has no destructor.
  - Nothing leaks.
- TryThrowFlare, on the client's own pawn only. The host throws, and the flare reaches the client as the card 104 copy. No state is held by the skipped call.

### Build and runs (shared by cards 174 and 175)

- Build 1: 0 warnings, DLL 7c138d9577687993. Run c175-r1: "run c175-r1: ended by scenario after 75 s; host rc 0, client rc 0".
- Build 2 (the card 175 wire fix below): 0 warnings, DLL e6c02da7a4d52da1 ("deployed: e6c02da7a4d52da1", "lane 1: dll e6c02da7a4d52da1, build e6c02da7a4d52da1, port 7777").
- Run c175-r2: "run c175-r2: ended by scenario after 79 s; host rc 0, client rc 0".
  - host: 3876 lines, FATAL/ERROR 0, WARN 1. client: 4228 lines, FATAL/ERROR 0, WARN 1. "new crash dumps: none".
  - "scenario passed". "dead time: sleeps 2.0 s, timed-out try waits 5.1 s in 1 (line 53 5 s)". Line 53 is the carried card 173 click wait (see card 175).
- The second run was allowed by a cause quoted from the r1 log: the client logged "client sent Fire 188416" and the host logged "host received Fire 24581" for the same frame (card 175).
- WARN classes, compared with c173-r1: both runs have only "WARN attach: DisableThreadLibraryCalls failed, err=126" on each side, which is the c173-r1 baseline. There is no new class. The c172 host WARN "card169: host ... held BP_FlareVisuals_C ... destroyed" is gone.
- Screenshots: prefix/drive_c/users/steamuser/AppData/Local/StarshipTroopers_UBW/Saved/Screenshots/Windows/client-c175-r2-c175-flare-held.png, client-c175-r2-c175-after-strike.png and client-c175-r2-c175-after-burst.png (the r1 set is beside them).

### Out of scope

- c175-r1 host: two DoAction Flare_Throw for one throw (count 2, 155 ms apart), with one flare spawned. c175-r2 had one. This is not investigated.

Report complete.

## Senior review (2026-11-11)
Accepted. Diff against the pre-174 snapshot touches only the allowed files. Rebuild clean; run c175-r2 ended by scenario,
rc 0/0, WARN classes on both sides are the baseline only. The client log has the skip line, the own-ability spawn, the
rendered destroy line and the blocked TryThrowFlare; `client-c175-r2-c175-flare-held.png` shows the flare in the right
hand. The skipped send destroys its FGameplayEventData once. A stale comment in hooks_ubw.cpp ("bits 12-19") was corrected to "bits 8-14" at review; the rebuilt DLL is `876c59da61692a92` (comment-only change, not re-run).
Not shown: a second throw by the same client (the local predicted ability is never confirmed by the server); carried to
card 176's run. The c175-r1 double `DoAction Flare_Throw` (one flare made) did not repeat in r2; watch in the soak.
