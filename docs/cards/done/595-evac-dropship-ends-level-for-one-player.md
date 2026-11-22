# 595: One player's evac dropship entry ends the level for both (card 301 class E, transport)

Source: card 301 audit, docs/reference/objective-coop-audit.md (C3M2 row 90, C5M1 row 126, C5M2 rows 155-156,
C3M1 rows 185-186). C2M1 C_53 and C4M1 C_70 are presence ends and are already handled by card 288 and card 492.

## Finding

On four maps the evac is a transport dropship. The first player to enter or request entry starts the outro and the
level end, with no check that every living player is aboard. Card 399 carries riders only when PC 0's pawn (the
host's) is the dropship, so a client entering first leaves the host on the ground while the level ends.

| Map | Site | Level end |
|---|---|---|
| FED_C3M2 | Transport C_2 OnPlayerEntryRequested -> EDropship_Interacted @12862 | EndLevel after 21.5 s (@13680) |
| FED_C5M1 | Transport C_0 PlayerSkippedFlight @29092, FlightPath_End -> @26145 | EndLevel (@26091) |
| FED_C5M2 | Transport C_2 PlayerEntering @13949; PlayerSkippedFlight @28639 | `End The Level` after 27 s |
| FED_C3M1 | Transport C_1 PlayerEntering @18514; PlayerSkippedFlight @19002 | `End the level` after 13 s; skip at once (@12337) |

## Trace first (static, before any code)

1. Who calls PlayerEntering / OnPlayerEntryRequested on the host when the client presses Use on the transport
   (native ASTDropship path, the pawn argument), and what the native code does to that pawn (possess, attach).
2. Whether PlayerSkippedFlight can fire before every player is aboard, or only after entry (then gating entry
   covers the skip too).
3. Where card 399's boarding keys on PC 0, and whether it boards a host who is not the entering player.

## Fix direction (confirm with the trace)

One host-side gate, reusing the card 288 all-living-players test: the evac event runs its original only when
every living player pawn has entered (or requested entry to) that transport. Until then the entering player is
seated and a prompt row is logged. A downed or dead player does not block. No per-map patches: the gate keys on
the transport dropship class and the event names above.

## Proof (written before the run)

- Host row "card595: host t=<ms> evac <dropship> held: <n> of <m> aboard" when the client enters first.
- The release row when the second player enters; then the existing card260 EndLevel rows on both sides.
- No EndLevel row before the release row. 0 card132 mismatches.

## Trace results (static, 2026-11-21)

All addresses are RVAs (VA = 0x140000000 + RVA). Kismet listings: build/kis/BP_PlayerTransportDropship.txt,
build/kis/c5m1.txt, build/kis/c3m1.txt, build/kis/c3m2.txt (kis.py stops at op 0x11 after @26203 in the C3M2
ubergraph, so later C3M2 offsets are not listed).

The finding above is half wrong. A client entering first changes nothing on the host. The host entering first ends
the level and carries the client wherever the client stands.

### Step 1: the entry path

- `ASTPlayerTransportDropship::OnInteraction` (0x051D0470) takes the interface pointer (dropship+0x708) and ignores
  the interactor argument. It writes the word 0x100 at iface+0x40: dropship+0x748 (bIsInteractable, the byte
  `CanInteract` 0x0175E890 returns) goes to 0 and +0x749 (player inside) to 1. It broadcasts the delegate at
  dropship+0x728 with the dropship as its only parameter, then calls `EnterDropship` (0x051C56C0), which boards
  `GetPlayerController(world, 0)`, then attaches and runs a camera call.
- The delegate at +0x728 is OnPlayerEntryRequested (signature FPlayerEntering). The level scripts' "PlayerEntering"
  bound events and C3M2's `OnPlayerEntryRequested` binds are this one broadcast. It carries no pawn.
- `EnterDropship` has three direct callers: BeginPlay+0x50 (StartWithPlayer), ActorFullyLoaded+0x1f (save load) and
  OnInteraction+0x3a. No path boards anyone but PC 0.
- Host presses Use: OnInteraction runs on the host, PC 0 (the host) boards, and the level script starts the end
  (C3M1 @18514 -> @4448, EndLevel 13 s later). Nothing checks where the client is.
- Client presses Use: the client's own Interact runs locally (interact.cpp detour_interact -> c102_interact ->
  original). The client's OnInteraction clears the client's +0x748 (the prompt is gone on the client), sets +0x749
  and broadcasts on the client. EnterDropship is skipped on the proxy (input_diag.cpp detour_enter_dropship, card
  015). The client's EndLevel is skipped (card 290), and its SetObjectiveVisibility and callout calls are skipped
  (card 140). The ChanOp::Use reaches the host (attributes.cpp:1217), and the host relay declines it in
  death.cpp:3711-3716 because the transport is not in kC54Relays (hooks_internal.h:5150): "card054: host relay for
  %s declined: ... not in the relay table". c102_interact (player_c99.cpp:822-832) also leaves the dropship out of
  the acting scope. Result: nothing happens on the host, and the client has lost its prompt until the host acts.

### Step 2: PlayerSkippedFlight

- Broadcast in BP_PlayerTransportDropship's ubergraph @912, right after `SkipFlight(true)` @901, reached only from
  InpActEvt_IA_ExitDropship_0 (@722). That is an enhanced-input event on the dropship pawn, so it fires only for the
  possessing local controller. Only PC 0 possesses, inside EnterDropship (ReceivePossessed @1681 runs
  SetPlayerInDropship(true) and PlayerEnterDropship). The client never possesses (card 399 keeps its possession).
- So the skip comes only from host input, and only after the host has boarded. Gating the host's OnInteraction
  until every living player has asked to board covers the skip too: no possession, no skip event.
- The harness FAST SkipFlight call (dropship.cpp:1083 detour_skip_flight) is native and does not broadcast
  PlayerSkippedFlight.
- Start-of-level skip events are not level ends: C5M1 C_1 @28913 goes to 2190, C3M2 C_4 @25899 only prints
  'Skipped Intro Flight', C3M1 C_3 @20969 only ends the conversation.
- Evac skip events: C5M1 C_0 PlayerSkippedFlight @29092 -> 26091 (EndLevel path), with PlayerEntering @29082 ->
  19590 and EndLevelDelegate @29097; C3M2 C_2 PlayerSkippedFlight @25324 is EndLevel(); C3M1 C_1 @19002 -> @12337.

### Step 3: card 399 boarding

- `c399_host_ride` (dropship.cpp:1502) returns PC 0's pawn when it is a transport dropship with +0x749 set.
- `c399_step` (dropship.cpp:2148) runs on the host every 100 ms. For each roster slot 1..kC43Max it checks only that
  the pawn is a player character server copy, then seats it with `c399_board`. There is no distance check and no
  check that the player asked to board. The host's boarding therefore teleports every client into the seat from
  anywhere on the map, which is the ride mechanism and stays.
- Boarding never keys on a client. A client's entry never reaches the host, so it never boards the host.

### Sibling: C3M2 mid-level rides

Three C3M2 mid-level transport rides use the same OnPlayerEntryRequested entry, so the host alone decides them and
card 399 pulls the client in from wherever it is:

- Monorail C_3: bind @22802/@22825, Dropship_Monorail_Boarded @22867 -> 12215.
- Powerplant C_1: bind @26028/@26051 -> 12215.
- Silo C_0: bind @26092/@26115 -> 12234.

The C3M2 evac (C_2) path: EDropship_Interacted @12862, bound @667/@690, removed @12885 (op62), SetIsInteractable
@12738/@12926, StartAction @13001.

### Fix direction after the trace (no code yet)

1. Host: hold `ASTPlayerTransportDropship::OnInteraction` (0x051D0470) for a transport until every living player
   has asked to board it. The held press marks the host as aboard, logs the hold row, and leaves the original
   unrun (no broadcast, no EnterDropship, no possession, so no skip). Downed or dead players do not count.
2. Client Use on a transport: add a kC54Relays row whose host action only marks that client as aboard for the
   dropship, then runs the held original when the set is complete. The client keeps its own prompt clear as now.
3. The last request runs the original once on the host: PC 0 boards, the script runs, and card 399 seats the
   others. The same gate covers the evac on all four maps and the three C3M2 mid-level rides, because all of them
   go through this one OnInteraction.
4. Open for the fix card: when the host is the last to ask, the original runs from the host's own press; when a
   client is last, the host runs the original from the relay with the host's PC 0 boarding, which is what the
   script expects.

## Senior decision (2026-11-21)

The trace result is accepted, and the fix goes ahead as the junior proposed.
- Host: hold `ASTPlayerTransportDropship::OnInteraction` until every living player has asked to board. The last
  request runs the original once.
- Client: add a kC54Relays row for transports, so a client's Use marks that client as aboard on the host.
- Downed and dead players do not block the hold. Card 399 then carries them.
- The gate covers all four evacs and the three C3M2 mid-level rides.
- No new UI. The held state is shown by a host row and by the client's prompt, which stays cleared.
Today the client's Use clears its prompt and nothing happens, so that is a client bug as well.

## Code (b445, dwmapi-b445-ad417436f2416129)

- `tools/symbols.txt` and `mod/src/offsets.generated.h`: `ASTPlayerTransportDropship_OnInteraction` = 0x051D0470.
  Its `this` is the interaction interface at dropship+0x708; the interactor argument is not used by the original.
- `mod/src/ubw/hooks_ubw.cpp`: hook row for `detour_c595_interaction`.
- `mod/src/ubw/hooks_internal.h`: `C54Kind::Transport` and a kC54Relays row for `g_transport_dropship_class`, so a
  client's Use reaches the host's OnInteraction through c50_interact; declarations of the detour and `c595_step`.
- `mod/src/ubw/dropship.cpp` (card 595 block before `c399_step`): one held dropship (weak ref), a mask of roster
  slots that asked, and the hold start time. A press by a roster pawn sets its bit. When every slot with a living,
  not downed pawn has asked, the original runs once with the dropship's interface and the hold clears. Otherwise
  the press logs a held row and returns. `c595_step` (called from `c399_step` every 100 ms) releases the hold when
  the remaining players die or go down or leave, and drops it when the dropship is gone. A press by a pawn that is
  not in the roster logs a WARN and runs the original unheld.

## Prediction (written before the run)

- Host presses Use on the evac first: one row "card595: host t=<ms> evac <name> held: 1 of 2 aboard (slot 0 ...)".
  No card260 EndLevel and no dropship flight. The host's own prompt stays visible (+0x748 is not touched); the
  client still sees its prompt.
- Client presses Use: the relay row for the transport kind, then "card595: host t=<ms> evac <name> released: 2 of 2
  living players asked ... the last press; the original OnInteraction runs once". Then the original flow: card 399
  seats the client, card260 EndLevel follows on both.
- Reverse order (client first): the held row names slot 1; the host's press releases.
- If the client is downed when the host presses, the host's press releases at once (1 of 1 living).
- No new WARN. 0 card132 mismatches. No second EndLevel.

## Client fix (b446, dwmapi-b446-d72ef6455c7908bb; senior review of b445)

The b445 client path still ran the original OnInteraction on the client's own copy, which broadcast the local
PlayerEntering (flying music, the client's own outro chain) before the host released. Now `detour_c595_interaction`
on a client clears +0x748 (bIsInteractable) on its own copy so the prompt goes away, logs one row
"card595: client t=<ms> Use on <dropship> relayed, own original skipped", and returns. It does not set +0x749 and
does not broadcast. The press still reaches the host: the client's Use is sent at Interact entry
(`c43_interact_edge` in attributes.cpp: `c50_send_use_name`, then ChanOp::Use), before and apart from OnInteraction,
and the kC54Relays transport row carries it.

Replication check: bIsInteractable is not replicated. Its FBoolPropertyParams (.rdata VA 0x80adc60) has flags
0x20080001000000, with no CPF_Net (0x20) bit, so no host replication can write +0x748 on the client copy and re-show
the prompt during the hold. Only the client's own script (SetIsInteractable, @18480 on the landing delegate) can set
it again, and that runs once when the dropship lands, before any press.

## Test hook (b447, dwmapi-b447-627843c2d92d10ea)

`c237_levelcall` (testrun.cpp) now runs a level script function whose name starts with "InpActEvt_" and whose
parameters fit in 64 bytes with a zeroed block; other functions with parameters are still refused. The run uses
`host levelcall InpActEvt_O_K2Node_InputDebugKeyEvent_1`, the C3M1 debug key event at ubergraph @6344: it lands
BP_PlayerTransportDropship_C_1 (StartAction(0,0,0)) and adds Satcom_SO3. It skips the Satcom SO1/SO2 chain, so the
run proves the evac hold, not that chain.

## Prediction for b447 (written before run c595b447a, scenario tools/scenarios/c595-c3m1-b447.txt)

- Client presses Use first: client row "card595: client t=<ms> Use on BP_PlayerTransportDropship_C_1 relayed, own
  original skipped"; host row "card595: host t=<ms> evac BP_PlayerTransportDropship_C_1 held: 1 of 2 aboard (slot
  ...)". The client's prompt goes away.
- During the hold the client log has no MUS_FlyingSequence row, no StartAction row and no card260 own-script
  EndLevel row before the host's release row.
- Host presses Use: "card595: host t=<ms> evac ... released: 2 of 2 living players asked ...", then Satcom_SO3 and
  Satcom_O1 complete on both, and card260 EndLevel follows about 13 s later (host sent, client relay).
- No new WARN. 0 card132 mismatches. One EndLevel.

## User rule 2026-11-21: a downed player at the evac bleeds out in 10 s

User: "What about if a player gets downed when going to the drop ship? ... I think a downed player needs to die
after like 10s or something".

The b445 hold boards no one before the release, so a player who has pressed is still on foot and can revive. But
`c595_needed` leaves downed players out, so a player who goes down during a hold releases it at once (c595_step).
The dropship then leaves and card 399 carries the downed body. Change for b446:
- While a hold is active, a downed player blocks it. Their bleedout is capped at 10 s (kC595BleedS = 10.0f,
  applied once when the hold starts or when they go down during it).
- If a teammate revives them, they are Up again and must press Use themselves.
- If they bleed out to Out, they no longer block, and the dropship releases on the next c595_step. Card 399 carries
  what is left, and the level ends as usual.
- Rows: "card595: host t=<ms> evac <dropship>: <pawn> downed during the hold, bleedout capped to 10 s" and the
  release row naming "bled out".
- Prediction (run): the client is downed during a hold. The host revives it, so there is no release, and the client
  presses, which releases. Second case: the client is downed and left, so it goes Out in 10 s (+-1 s) and the next
  step releases.

## Run c595b447d (b447 627843c2d92d10ea): client first, then the host: proven

- Client E at 380 uu: "card595: client t=198718742 Use on BP_PlayerTransportDropship_C relayed, own original skipped".
- Host: "held: 1 of 2 aboard (slot 1 BP_PlayerCharacterMI_C asked; mask 0x2 of 0x3)" at 198718749 (7 ms later).
- Host E: "released: 2 of 2 living players asked (mask 0x3 of 0x3) after 3785 ms, the last press" at 198722534.
- Satcom_O1/SO3 complete on the host (198722546) and replayed on the client (198722591).
- card260 EndLevel: host 198735539 (sent to 1), client relay 198735561.
- In the hold the client log has no MUS_FlyingSequence, no StartAction and no own-script EndLevel row. Its only
  StartAction row is card449 "host took off, StartAction run on the landed copy" at 198722591, after the release.
- card132 mismatches 0 in every round. WARN rows are all known (card046 warp gap, card148 gibs, card486, card537).

## Code (b448 28e16459fd3c774b): user rule 2026-11-21

- `c595_needed` counts Up and Downed players (by `c43_life`); Out, Dead and a slot with no pawn do not block.
- `c595_scan_downed` runs at the hold's start (each press) and in every `c595_step` (100 ms). For a newly Downed
  slot it caps the card 043 entry's `secs_left` to `kC595BleedS` (10.0f) once and clears the slot's ask, then writes
  "card595: host t=<ms> evac <dropship>: <pawn> downed during the hold, bleedout capped to 10 s (<s> s left before,
  slot <n> ask cleared)". A revived player leaves the downed bits and must press Use again. A Downed player that goes
  Out or Dead leaves them too, and the step's release names "a downed player bled out". No new timer: card 043's own
  bleed tick drains the capped value and broadcasts the seconds.

## Prediction for b448 (runs c595b448a and c595b448b)

- Both runs: host presses first ("held: 1 of 2 aboard (slot 0"), then `host down client` gives the card043 Downed row
  and the card595 capped row within 2 s.
- Run a (revive): the host revives within the 10 s; no "released" row between the revive row and the client's relayed
  row. The client presses: "released: 2 of 2 living players asked (mask 0x3 of 0x3) ..., the last press", then
  Satcom_SO3 and card260 EndLevel on both.
- Run b (left): card043 "-> Out (bleedout over)" 10 s (+-1 s) after the capped row, then within one step
  "released: 1 of 1 living players asked (mask 0x1 of 0x1) ..., a downed player bled out", then card260 EndLevel on
  both (card 399 carries the Out client).

## Runs c595b448a and c595b448b (b448 28e16459fd3c774b): user rule proven

- Run a (revive): host held 1 of 2 at 198998613. The client went Downed at 198998812 (bleedout 30.0 s), and was
  capped at 198998902 ("29.9 s left before, slot 1 ask cleared"). The host revived it at 199003049, after a 3001 ms
  hold, about 5.9 s of bleedout left. No release row came until the client pressed: relayed at 199007137, then
  "released: 2 of 2 living players asked (mask 0x3 of 0x3) after 8535 ms, the last press" at 199007148. card260
  EndLevel came on the host at 199020169 and on the client at 199020190.
- Run b (left): host held 1 of 2 at 199196239. The client went Downed at 199196450 and was capped at 199196519.
  card043 "-> Out (bleedout over)" came at 199206535, 10.02 s after the cap. "released: 1 of 1 living players asked
  (mask 0x1 of 0x1) after 10363 ms, a downed player bled out" came at 199206602, on the next step. card399 boarded
  the Out client into C_1's seat at 199206702. card260 EndLevel came on the host at 199219621 and on the client at
  199219645.
- In both runs card132 mismatches were 0 and there were no WARN rows beyond the known ones (card046, card148,
  card486, card537). The cap lands on the first c595_step after Downed, 69-90 ms later.

## Closed (senior, 2026-11-21)

Proven in c595b447d (client first, then host), c595b448a (revive, then press) and c595b448b (left, Out in 10.02 s,
then the release). The senior checked the c595b448b host rows. card132 mismatches were 0 and there were no new
WARNs. The fix ships in b448.
