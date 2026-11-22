# 607: A host self-kill that only downs starts Traitor and blocks the FED_C1M1 tutorial
Status 2026-11-22: open, parked until after the user's playtest. The fix in b466 failed its proof at step 3 and is not in b465.

Open 2026-11-22 (senior, card 301). The user's verdict the same day: card this case only if it blocks the tutorial.
It does, by the static trace below. Traitor after a host kill of a friendly NPC is accepted and out of scope.

## Bug

When the host's lethal hit on itself is turned into Downed by card 043, the kill still reaches the level script.
`Traitor_O1` is added and a revive does not remove it. In solo the same hit is a death and a restart.

## Evidence

Trigger (old builds, not rerun on a current one):

- `logs/c89c-host.log`: 2745 host Downed (HandleDeathEvent skipped), 2747 AddObjective(Traitor_O1) 2 ms later,
  2842 revived.
- `logs/c70b-host.log`: 1232 Downed, 1235 AddObjective(Traitor_O1).
- Both hits are `c43_hit` with "causer null", which passes the target as its own causer. docs/PLAYTEST.md records the
  same message for a kill by one's own grenade; no log of a grenade case was checked.

Block (static, `build/kis/c1m1.txt`):

- `AmTraitor()` is `DoesObjectiveExist(Traitor_O1)`. kis.py misses this function's script start; it decodes from
  export offset +132 (88 bytes).
- The script never completes or removes Traitor_O1 and never clears `Traitor?`.
- ShootingRange completion (@4821, @4859, @5028) needs NOT AmTraitor. The only other caller is a debug key (@17222).
- Setup CTF (@4602) needs ShootingRange_O1 complete (@4100). EndLevel (@1227) is reached only from the CTF satchel
  or a debug key. A Traitor before the range is complete therefore cannot finish the level without a restart.
- 60 s later TraitorFallzone removes the weapons, teleports the host and starts `Whip`: 100 self damage, requeued
  every 1 s with no stop condition. This part is read from the script, not seen in a run.

## Work

1. Trace statically before code: the broadcast of `OnCharacterKilledNPC` (+0x660) for a player victim (AddObjective
   caller exe+0x5081B44), and what it can read to know that card 043 skipped the death.
2. Fix by class: a player death that card 043 skips is not announced as a kill to that delegate. A real death after
   all-down and an NPC kill stay vanilla. No objective removal after the fact.
3. Sibling check: the host downing the client (same delegate, victim also Faction.Human.Player), and every other
   listener of the delegate (marker_c166 hit marker included).

## Static trace 2026-11-22

- Order, read from the disassembly of `UADHealthComponent::FatalDamageEffectReceived` (exe+0x4A3B8F0):
  1. It broadcasts the victim's fatal delegate (component +0xF8). HandleDeathEvent runs from this (inference from the
     UFunction and the two logs' order), so card 043 has decided before step 2.
  2. It gets the event Instigator's (+0x00) damage-causer interface and calls slot +0x18, OnDeathCaused, with the same
     event. Receiver (+0x10) is the victim. This call has no check of what step 1 did.
  3. It sends a gameplay event to the object at component +0x128 (vcall +0x740). What listens was not traced.
- `ASTPlayerCharacter::OnDeathCaused` (exe+0x51D01F0) is a jump to `ASTCharacter::OnDeathCaused` (exe+0x50F7040),
  which broadcasts interface+0x8 (pawn +0x660) with a tag built from the victim's tags.
- C1M1 PublicEnemyTrigger @21906-22139 takes any tag under `Faction.Human` while Traitor_O1 does not exist. It has no
  alive check and receives no victim, so the script cannot know about the skip.
- Fix as built in b466 (the senior's change applied): `kC17Slots` weak rows keyed by victim index and serial.
  `c607_death_decided` runs after `c43_death` and before the original: a skip sets the victim's row, a false return
  clears each of its rows; map arm clears all. `detour_c166_death_caused` calls neither the original nor the relay when
  the event Receiver is the live object of a row. A bare index or pointer would not do: with no Instigator nothing
  follows step 1, and the index can be reused after the victim is freed. One row for all would not do either: a second
  down inside step 1's broadcast would replace the first victim before its step 2. The row does not read the Downed
  state: the decision is the key, so each skip branch is covered (Downed, hit while down, Out, card 132 god).
- Stays vanilla: an NPC victim matches no entry. A real death (all_down is set before both the host-death return and
  the timed kill) returns false and clears the entry.
- Siblings: host downs client, client downs host and client downs itself use the same two detours. The relay is
  inside the second, so a client gets no kill marker for a down. WB_Hit_Marker on the host loses the kill marker for a
  down and keeps the damage marker. In the 95 decoded scripts of build/kis only c1m1 binds the delegate; assets that
  are not decoded were not swept.
- Not covered: an NPC instigator (its OnDeathCaused is not the detoured function, and no listener on an NPC's
  delegate was found in that same sweep), and step 3.

## Done when

One combined run on C1M1 shows: host self-kill gives Downed and no AddObjective(Traitor_O1); revive; the range then
completes; a host kill of a friendly NPC still adds Traitor_O1.

## Senior verdict 2026-11-22: accepted with one change

Checked: `detour_player_death_event`, every branch of `c43_death`, `detour_c166_death_caused` and its relay,
the real-death hit, and the order in c89c-host.log (Downed at 2745, AddObjective 2 ms later). The disassembly was not re-read.

- **Change: key the entries by the victim and not by the player slot.** `kC17Slots` rows of index and serial. The god branch
  returns true before a roster or slot exists, and the branches that return false include `slot < 0`, no listen server
  and fewer than 2 players. A clear by slot can then miss and leave an entry that swallows a real death.
  Set reuses the victim's row, else a free or dead one. Clear scans every row for the victim on every false return,
  whatever the branch. A full table gives one WARN and suppresses nothing.
- A down showing no kill marker is accepted: it is not a kill. The damage marker stays.
- A capped log row on each suppressed call names instigator and victim.
- Out of scope as listed: NPC instigator, step 3 and assets not decoded. No card without a seen failure.

Build: one batch with the net_budget name copy (card 608). FName at insert, printed with fname_text, is fine.

Proof, one combined run with the prediction written first:

1. Host self-kill: Downed, one suppressed row, no AddObjective(Traitor_O1). Revive.
2. Host downs client: suppressed row, no Traitor, no kill marker relayed. Revive.
3. Client downs itself: suppressed row, no Marker 'K'. Revive.
4. The shooting range completes.
5. Host kill of a friendly NPC still adds Traitor_O1. Confirm from build/kis/c1m1.txt that PublicEnemyTrigger is still
   live after the range before scripting this order.
6. All down, host real death last: the call is not suppressed and the death and restart run as before.

## Prediction b466 (written before the run)

b466: dwmapi c43256c1745ffb29, ubwperf ccb1fa91ca26e74c. Scenario `tools/scenarios/c607-c1m1-proof.txt`, GOD=0.

Trigger still live after the range (static, c1m1.txt): bound once @29096-29140, no unbind or clear in the script, and
its handler @21906 gates only on the victim tag and on Traitor_O1 not existing.

1. Kill floor falls (self caused): host-own Downed, a card607 suppressed row right after it, no AddObjective(Traitor_O1),
   then Up.
2. Armour taken by code, then host rifle hits (instigator host-own): copy Downed, suppressed row, no Traitor, no
   `card166: ... OnDeathCaused on server copy` row, then Up.
3. InstaKillTarget on the copy: Downed, suppressed row, no `OnDeathCaused on server copy` row, then Up.
4. CompleteObjective(ShootingRange_O1), with no AddObjective(Traitor_O1) before it.
5. Host rifle kill of a friendly trooper: AddObjective(Traitor_O1) once, and no suppressed row for that victim.
6. Copy Downed by itself (suppressed), then `host down host`: gate off with all down 1, Hit_Kill marker, no suppressed
   row for that host death although step 1 left a row for that pawn. Death screen, LoadLastSave, both pawns back.

The run also has no crash, no dump and no `WARN card607` row. Card 608: the net budget class rows print names, none
garbled, through the restart's map change.

## Result b466, run c607b466a (2026-11-22): failed, not promoted

Ended by scenario at 511 s: VOID at line 237, failed lines 171, 172, 178, 179. No CRASH, new dump or `WARN card607` row.
Line numbers below are from `logs/c607b466a-host.log` unless named.

| Step | Result | Evidence |
|---|---|---|
| 1 host self kill | as predicted | Downed 5455, suppressed 5457, no Traitor, Up at 30 (5593) |
| 2 host downs client | as predicted | Downed 7574, suppressed 7576, no Traitor or Hit_Kill, Up at 30 (7749) |
| 3 client self kill after revive | **failed** | 7891-7902: Health 30.0 to 0.0, `dead 0` before and after, no death event or Downed row |
| 4 range completes | completed, but the run is VOID | line 237 failed first; CompleteObjective at t=239709852, before Traitor |
| 5 host kills friendly NPC | as predicted | rifle kill, one AddObjective(Traitor_O1) 30474, no suppressed row |
| 6 all down, host last | as predicted in the log | copy Downed 30772, suppressed 30774, host gate off, Hit_Kill 30812 and no suppressed row, death screens, reload, reconnect |

- Step 3 never reached this card's code. The finding and the limit of its diagnosis are card 610.
- Step 6's client self kill (from Health 49) did show suppression on the copy. The client's map totals read 6 damage
  and 0 kills relayed. It does not replace step 3, which starts from revive Health.
- Step 2's missing `K` row proves little: only a copy instigator is relayed. The missing host Hit_Kill is the evidence.
  No positive `K` relay occurred in this run.
- Line 237: the presence step waited for ShootingRange_SO0, which stayed open for three tries. SO0 completed in the
  same ms as ShootingRange_O1, at SO3 10/10 (29801). That points to the routine waiting on it too early. Not traced.
- Screenshots (14 proof, the s5 aim) were viewed. s3-host-view and s4-range-done show sky and prove nothing by eye.
  s6-respawn-client shows the loading screen, so the client's return is proved by log rows only. In s2-host-view the
  downed client has its DOWN label and looks upright 0.17 s after the down; a later pose was not shot.
- WARN rows: card502 (8) and card537 (7) match the counts of c604b461a. card494 (2, client) is absent there and
  occurs in older runs such as c397b407a.

Play build stays b465 (03df06297887feeb, a59345153c6e5358). No rerun or fix build, by the senior's order.
