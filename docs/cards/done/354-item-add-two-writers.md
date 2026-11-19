# 354 The client's own item adds have two writers (mirror of card 353)

Found by the card 353 sibling sweep (junior, b39). Not seen in a log yet.

## Suspected cause (to be traced, not run)

The host's GrenadeItem frame is absolute. The client's own `AddInventoryItem_Internal` (pickups) also adds locally.
A frame that lands before the local add leaves the client 1 over until the next host frame for that definition.
The initial loadout is the open point: it comes from local adds before any frame arrives.

## Step 1 (static)

List the callers of the add path on the client's own inventory with `UBWCOOP_C73_LOG=1` rows from `b39` and the exe:
which are pickups or script grants the server copy also runs and sends, which is the loadout. State for the loadout
whether the host sends its counts in the join replay.

## Fix direction

Same rule as card 353: the host frame is the only writer. Callers the server copy also runs are not run locally in a
client session; a caller the host does not send keeps running and is named in the card. Untraced callers log WARN.

## Prediction (1 run, the b39 scenario plus one pickup of a grenade or satchel by the client)

Each pickup changes the client's count once, by the host frame; 0 local add writes for the skipped callers; the
loadout counts equal the host copy's after the join replay; 0 new WARN kinds, 0 `WARN card353`.

## Trace (junior, static plus `b39` rows, 2026-11-14): a race is possible

- `AddItemByDefinition` (0x514AE40) calls `AddInventoryItem_Internal` (0x514AAD0) at return address 0x514AE7B; all
  4 client add rows in `b39` have that caller, so the fix detours `AddItemByDefinition` to see the real one.
- Callers of `AddItemByDefinition`: 0x51D3FA4 `ASTItemPickup::Pickup_Implementation` +0xb4 and 0x51DA8EE
  `ASTPlayerCharacterMI::ResetInventory` +0x4be (the loadout, on spawn and respawn). Both ignore the result; the
  server copy runs both and sends the count (`b39`: local add, host copy add 6 to 12 ms later, frame "N to N").
  0x5072667 `execAddItemByDefinition` (Blueprint grant): whether the host copy runs it is unknown.
- Other callers of the internal add: 0x514AEE8 `AddItemByTag` (Blueprint only), 0x5160CBA `LoadInventory` (save).
- There is no join replay of item counts: the host sends on each add or remove of the server copy; a frame before
  the client's inventory exists is deferred (`c73_defer`). `ResetInventory` removes nothing, so a frame that lands
  first (respawn) plus the local add leaves the count 1 over.
- Card 353's other callers named: 0x5150DE3 `USTInventoryItemGameplayAbility::ConsumeItem` +0xb3 (Blueprint
  `execConsumeItem`), 0x51637D1 the base `OnDoAction_Implementation` +0x91; 0 rows from either in `b39`.

## Fix (b40, junior)

`c354_skip_local` in a detour on `AddItemByDefinition`: client session, own inventory, not the host write: the
Pickup and ResetInventory callers are not run (row `card354 ... local add not run`); any other caller runs and logs
`WARN card354 ... caller not traced`. `c73_apply`'s raise sets the host-write flag and calls the trampoline. The
internal add from `AddItemByTag` or `LoadInventory` runs and logs WARN. The run's pickup is the range resupply.

## Result (b40 `81b66827ad3499bb`, run `b40`, 2026-11-14): pickup half accepted, loadout half not proven

- 4 pickups (Frag, Incendiary x2, satchel), all caller exe+0x51D3FA4: one `card354 ... local add not run` row each,
  the host copy's add 4 to 15 ms later, one count change by the frame 16 to 26 ms after the skip. Grants bound
  (Frag h153, Incendiary h184). 0 `WARN card353`, 0 `WARN card354`.
- Loadout (`ResetInventory`, caller 0x51DA8EE): 0 rows. The FED_C1M1 loadout adds no grenade item, and the run's
  respawn came after the mission's end. The skip for that caller is in the build and has not run.
- Seen after the reload that followed the mission's end, not from this card (no card354 row after 341836062): host
  WARN card130 x1, client WARN card132 x2 (satchel building dead 0/1), WARN card177 x18, WARN card327 (the host's
  server copy holds LaserMorita, both own pawns Unarmed). Noted in STATE, not carded: a reload after mission end.
- Client crash 420 ms after `client quit`: access violation reading 0x44 in the audio mixer thread
  (`FPatchOutput::PopAudio` from `FSubmixEffectDynamicsProcessor::OnProcessAudio`), no mod frame. First sight; a card
  on a second one.

## Loadout run (b41 `9b2775ac3b120578`, run `b41`, FED_C2M1): the skip has nothing to skip there

The client's own `ResetInventory` takes the Unarmed branch on FED_C2M1 and calls no `AddItemByDefinition`: 0 card354
rows from caller 0x51DA8EE. The respawn step did not run (the host was still in the dropship flight: `FAST=1` does
not skip it on the c346 path without `UBWCOOP_AUTODROPSHIP=1`). Finding: card 356. The respawn half is proven in
card 356's run.
