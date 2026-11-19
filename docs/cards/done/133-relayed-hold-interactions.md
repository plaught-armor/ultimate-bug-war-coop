# Card 133: Relayed hold interactions are cancelled on the host (satchel on the flak bug never explodes)

Status: accepted 2026-11-07 (run c133x8, test DLL `d3998ec44c365276`). Shipped with card 131 in play DLL `4cb036fa33c090bc`.

Read: card 050 (`c50_relay_use`, `c50_pick_target`, the UseName fallback), card 054 (relay table `kC54Relays`),
card 098 (`'S'` row, `ReceiveOnSatchelExploded`), card 132 (this card's fix is one of its gap classes). Memory rules
full-world-sync, playtest-card-rules and card-run-discipline apply.

## Why

User playtest, play DLL `0a98be3c89324a0d`, FED_C2M1: "they attached the satchel to the flak bug, but it didn't
explode". The user's rule for this class: the client must do everything the host does, minus being the host.

## Verified (senior, static and from the user's host log)

- Host log `UBWCoop-menu-20261107-172301.log` lines 6941-6945: the client's Use on
  `SatchelSiteCAC_BP_SatchelPlacementSite_C_CAT_1034` reached the host. The copy's own target was `(none)`, so card 050
  set `+0x100` from the client's UseName, and card 054 ran relayed Use #20 `Interact`. The release (`Use 0`) came 2.9 s
  later. No `ReceiveOnSatchelExploded` line follows on the host, at any time.
- `UADInteractorComponent::Interact` (exe+0x4A1FE70) does not complete an interaction. It checks the target's
  `CanInteract` (vtable +0x28) and broadcasts the start delegate at `+0xC8`. The interaction completes later in
  `TickComponent` (exe+0x4A20230) once the hold time passes. `ASTSatchelPlacementSite::GetInteractionTime_Implementation`
  (exe+0x38D8C00) returns a stored float, so the site is a hold interaction (its runtime value is open; the client's
  2.9 s hold is consistent).
- `TickComponent` calls `TraceForInteractable` (exe+0x4A20780) every tick. When the result differs from `+0x100` and
  an interaction is running (`+0xFC`), it calls the old target's cancel (vtable +0x68), broadcasts, and writes the new
  result to `+0x100`. On the host copy the trace finds nothing (the same miss that sent card 050 to the UseName path),
  so the first tick cancels the relayed hold and clears the target. `ASTSatchelPlacementSite::OnInteraction`
  (exe+0x5163E00: removes the satchel item, shows the charge mesh, sets `+0x50C`, then `PlaceChargeInNetwork` or
  `TrySetTimerOn`) never runs on the host, so the building never explodes and no card 098 `'S'` row is sent.
- This hits every hold interaction relayed through the mismatch path, not only satchels. Instant interactions work
  because they finish inside `Interact`. In this log the mismatch path ran once (Use #20).

## Verified by the user

- On the client the flak bug blew up; on the host it never did. So the client's own local hold completed, its
  `OnInteraction` ran and its local `Explode` destroyed its copy of the building, while the host's relayed hold was
  cancelled. The worlds split: the client's flak bug is dead and the host's is alive.

## Open

- Whether the name `..._CAT_1034` (a child actor component name) names the same site on both machines. Log the host
  site's location and its owning building next to the client's.

## Do

1. **Send the client's finished interaction to the host (card 132 rule, client to host).**
   - The client's interactor completing a hold, meaning its target's `OnInteraction` running on the client's own pawn,
     is the event. Send it to the host with the target's name and location.
   - The host finds the same target (name plus location, step 2) and runs its `OnInteraction` with the server copy's
     interactor directly. It does not re-run the hold through the copy's trace and timer.
   - Use one mechanism for every hold interaction in `kC54Relays`. Instant interactions keep the card 054 press relay;
     state in the Report which path each class takes.
   - The host-side hold that card 054 starts today must not also complete. Either do not start it for hold
     classes, or cancel it, so `OnInteraction` runs once on the host.
   - Sanity checks only (friends-only trust model): the target exists, it is within 500 uu of the copy, and
     `CanInteract` passes on the host copy. Each refusal logs a WARN with the reason.
2. **Name match check.** When card 050 resolves a target by name, also compare the host target's location with the
   client's (send the client's target location in the UseName frame). Log the distance, and refuse with a WARN above
   500 uu instead of using a wrong site.
3. **Client parity (user rule).** The client must see what the host sees: the charge placed on the site, the timer,
   the explosion, and the flak bug destroyed. The host's `OnInteraction` and the `'S'` row are the source of truth.
   - The client's own local `OnInteraction` does run (verified by the user). The host decides: the client's local
     site must not explode its building on its own. The building on the client dies only from the host's `'S'` row,
     applied once (card 100 double-explode row). The charge mesh and timer may show locally.
   - If the client's local hold completes but no host `'S'` row arrives within the site's interaction time plus the
     timer plus 5 s, log `WARN card133: client site <name> completed, no host explode`.
   - If the site's own visual state (charge mesh shown, `+0x50C`) does not reach the client, carry it in the card 098
     journal as a placed-actor state row, so a late joiner sees it too.
4. **One run, after the hold is lifted.** FAST=1, FED_C2M1, the client carries a satchel to a flak bug site and holds
   Use. Warp as needed. Combine with the card 131 step 3 run if both are ready.

## Done when

- Host log: the relayed hold completes (`OnInteraction` on the site), then `ReceiveOnSatchelExploded` and the `'S'`
  row sent; the client log shows the row applied once.
- Client screenshots: the charge on the site, and the flak bug destroyed, matching the host's view at the same times.
- The open items above are marked verified or still open, with the evidence.
- The Report has the WARN diff against c130d5, the table peaks, the run length, rc, ERROR/FATAL, CRASH lines, dumps
  and the DLL hash (`sha256sum | cut -c1-16`), with the play DLL saved as `build/dwmapi-play-<hash>.dll`.
- It ends with "Report complete."
