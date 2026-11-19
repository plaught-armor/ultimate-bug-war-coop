# 248: Host's own satchel hold refused when the client's copy stands at the site (play blocker)

Found by card 240 run 5 (logs/c240-c1m1-r5-host.log). The host holds E on the CTF satchel site; the hold's
completion reaches `detour_c133_completed` (mod/src/ubw/hold.cpp) with an interactor whose owner is the
client's server copy, the detour refuses it, and the charge is never placed. CaptureTheFlag_SO4, SO0, O1,
Asteroid_O1 and the mission end are lost. Two players standing together at a satchel site is normal play, and
every satchel site of the campaign goes through this detour.

Lines:
- 161383313 `card020: host host-own UADInteractorComponent::Interact(owner BP_PlayerCharacterMI_C): target SatchelSiteCAC_BP_SatchelPlacementSite_Flag_C_CAT`
- 161384556 `WARN card133: host t=161384556 hold completed on SatchelSiteCAC_..._CAT (satchel placement site) for server copy BP_PlayerCharacterMI_C: not run`
  (1243 ms after the host's press; the client pressed nothing, its log has no interact or hold line)
- 161386521 host release, MS_WPN_SatchelCharge_Cancel.
- Host at (-8357, 20900), client copy at (-8370, 20891): 16 uu apart.
- Real play 2026-11-07 (before card 133): one hold, placed.

Goal (one): a hold the host player runs completes on the host whoever stands near; a hold a client runs still
completes exactly once (card 133's rule stays).

Step 1, static, no run, appended here: who calls OnTimedInteractionCompleted (exe+0x4A20020) and where its
interactor argument comes from (the pressing interactor, the site's stored interactor, an overlap or focus
query). Explain from the code why the copy's interactor arrives for the host's press. State the predicted fix
and what distinguishes "the host's own hold, wrong interactor" from "a hold started on the copy" (for example
which interactor component holds the active timed interaction, or the host's own interactor's hold state).
If static reading cannot decide it, add one capped diagnostic line in the detour (return address, both
interactors' hold targets and timers) and say so; that costs one run.

Step 2: the fix in hold.cpp (and only where step 1 shows the cause lives). No widening: other WARNs stay.

Step 3: build, then one run of the card 240 scenario unchanged (client stays at the site: it is the repro):
`systemd-run --user --scope -q -p MemoryMax=12G cmake --build build -j4`, then the card 240 command with tag
`c248-c1m1`. Budget 2 runs (3 if step 1 needed the diagnostic).

Files allowed: mod/src/ubw/hold.cpp, hooks_internal.h if a declaration is needed, this card. The scenario
only if the fix changes a log line it waits on.

Done when: host hold places the charge with the client's copy at the site (CTF_SO4 Complete row and client
replay, the building dies on both sides once), 0 card133 refusals of that kind, the run goes on to
CaptureTheFlag_O1, Asteroid_O1 and FED_C2M1 on both sides (this also closes card 240: report N of M), 0 crash,
digest 0 mismatches. State what proves the client-run hold path is unchanged (review, or an existing scenario
block that runs a client hold; name it, do not add a run for it without asking).

## Step 1 (static, 2026-11-12)

From the exe disassembly (RVAs):
- `UADInteractorComponent::Interact` 0x4A1FE70 calls `TryInteract` 0x4A20A40 on its focus target (+0x100). For a timed
  target, TryInteract calls `OnTimedInteractionStarted` (interface slot 0x80), which sets the interface's started flag.
- The hold state lives on the interactable interface, not on the interactor: +0x9 started, +0xa completed, +0xc
  progress (`ResetInteractor` 0x4A201B0 clears exactly these). The interactor keeps no "who started" or "holding" field.
  +0xfc is set in the constructor only (config).
- `UADInteractorComponent::TickComponent` 0x4A20230, every interactor, every frame:
  1. Traces and stores the focus at +0x100.
  2. If focus->GetShouldTick (slot 0x58, 0x4A1FE00: timed && +0x9 && !+0xa), and the owner is within the range at
     +0xf8, it calls focus->TickInteractable(dt, this) (slot 0x50, 0x4A206D0) with ITSELF. The interactor does not need
     to be pressing.
- `TickInteractable` adds dt to the shared +0xc. At the interaction time it sets +0xa, then calls
  `OnTimedInteractionCompleted` (slot 0x88, 0x4A20020) with the interactor that ticked it.
- So after the host's press starts the hold, the client's server copy (its TickComponent runs on the host; it traced the
  same site, 16 uu away) also ticks the shared progress. Progress runs at 2x: 1243 ms is half the 2.5 s real-play hold.
  The completion carries the copy's interactor whenever the copy crosses the threshold first.
- `detour_c133_completed` refused it. +0xa stays set, so GetShouldTick is false: no retry. Release
  (`OnTimedInteractionReleased` 0x4A200B0) skips the interrupt when +0xa is set. The charge is never placed.
- Real play (2026-11-07, before card 133) ran the original with whichever interactor came first.

Distinguishing the cases on the host:
- Only the host's own Interact can start a hold on a relay-class target. Card 054 does not start one for a timed class;
  `c133_host_done` calls the original directly, not through TickInteractable or this detour.
- So a completion for a server copy while the host's own interactor's focus (+0x100) is the same interface is the host's
  own hold: run the original once with the host's own interactor.
- Any other server-copy completion keeps the WARN refusal.
- Exactly once: +0xa is set before Completed and the original clears +0x9, so neither interactor ticks again.

Not changed: the 2x progress with two players at a site is base-game shared progress.

## Step 2 (static checks and revised fix, 2026-11-12)

The senior rejected the completion-swap from step 1: double-speed progress is not base-game behaviour; if the host
looks away, the copy completes and the site dies; and on the client the mirror case sends no HoldDone. The revised
fix filters the tick, not the completion.

Checks:
1. 0x4A206D0 is the implementation every relay class uses. A byte scan of .rdata finds 12 IADInteractableInterface
   vtables: the interface itself, ItemPickup, LimitedInteractionActor, MountableTurret, PlayerMechBase,
   PlayerTransportDropship, SatchelPlacementSite, SecretPickup, Switch, TacticalSupportPickup, WeaponPickup and
   ZiplinePoint. All 12 hold 0x4A206D0 at +0x50 and 0x4A20020 at +0x88. The PDB has a single TickInteractable and a
   single OnTimedInteraction* body, and neither is a UFUNCTION, so no Blueprint overrides them.
2. Does the host's proxy pawn tick and trace on the client? This cannot be settled statically. TickComponent
   (0x4A20230) and TraceForInteractable (0x4A20780) have no role or local-control gate; they need only the scene
   component that Initialise (0x4A1FE50) sets. In r5 the server copy, which is not locally controlled, traced and
   ticked on the host, so Initialise is not gated on local control there. Fault (c) is therefore likely. The client
   branch of the filter covers it either way: if the proxy does tick, the branch is the fix; if it does not, the
   branch is a guard.
3. Does Released 0x4A200B0 still reset progress on early release? Yes. ReleaseInteraction (0x4A200F0) calls
   Released, which calls Interrupted (0x4A20070) when +0xa is clear. Interrupted then jumps to ResetInteractor
   (0x4A201B0), which clears +0x9, +0xa and +0xc. None of that path goes through TickInteractable, so the filter does
   not change early-release reset.
4. Timed classes outside the relay table: MountableTurret, PlayerMechBase and PlayerTransportDropship.
   GetIsTimedInteraction (0x4A1FE30) is `GetInteractionTime() > 0`, so whether each is timed depends on its Blueprint
   data, not on the class. They keep the base behaviour.

Residual: a foreign interactor that holds focus beyond +0xf8 (10000 uu) still calls Interrupted through the
out-of-range path in TickComponent. This is rare and is not filtered.

Fix: a detour on TickInteractable (slot 0x50, RVA 0x4A206D0) in hold.cpp. For a target with a card 054 relay row:
- On the host, an interactor whose owner is kSideServerCopy returns 0.0f without calling the original.
- On a client, an interactor whose owner is not kSideClientOwn returns 0.0f without calling the original.
- Every other call goes to the original.

Logging: one card248 line per target per map (at most 16 targets), plus a "foreign ticks dropped" count in the
card133 map totals. The card 133 refusal stays as the backstop.

On the host, a client-run hold now gets no progress from the server copy. It completes only through HoldDone (card
133), which is the existing rule.

## Run 1 (c248-c1m1, DLL 83cf0b9cdcfe0cb0)

The scenario is the unchanged c240-c1m1.txt (FAST=1).
- CTF_SO4 on the host: press at 163191301 (card043 own interact press), host sent CompleteSubObjective(CaptureTheFlag_O1,
  CaptureTheFlag_SO4) at 163193822 (2521 ms after the press; real play took 2505 ms). The client replayed it at
  163193835. CTF_SO0 and CaptureTheFlag_O1 completed in the same tick. AddObjective(Asteroid_O1) was sent and
  replayed (163200327 / 163200333).
- One card248 line on the host, at 163191303 (server copy tick on the satchel site dropped), 2 ms after the press.
  There were no card133 lines on either side, so 0 refusals. The map never ended, so the card133 totals line (which
  carries the dropped-tick counter) was not printed.
- Objectives: 26 of 27. That is 25 by Complete row on both sides (host 30 rows, client 30 replays) plus
  ShootingRange_SO3 at count 10/10. Asteroid_O1 was added, but travel did not happen.
- Travel: WB_TrooperLevelEnd opened at 163216089, 16 s after the add (real play: 153936049, also 16 s). It then
  waits for its Next button (BndEvt__TrooperLevelEnd_WB_Button_Next); in real play the user pressed it about 9 s later
  and the Browse followed. The scenario has no press, so lines 346-348 timed out. This is a gap in the c240 scenario,
  not part of card 248.
- 0 crashes, digest 38 rounds with 0 mismatches. The only FAIL other than travel is line 145 (flare, card 249).
- New WARN classes against r5: `WARN cmd: goto actor BP_PlayerPresence_C_N -> failed (no path)` on both sides, for
  _11. r5 never reached that line.
