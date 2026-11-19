# 363: client decides corpse tickets on its own (InstaKill of dying NPCs)

## Trace (senior, static, 2026-11-14)

- exe+0x5192EFE is the return address in `ASTNonPlayableCharacterBase::HandleDying` (RVA 0x05192BE0) after its call
  to `TryTakeCorpseTicket` (RVA 0x051B16D0). That function ends in a tail jump to
  `USTCombatUtilityLibrary::InstaKillTarget(npc, true)` (exe+0x51B1733), so card 361's row names HandleDying's site.
- `TryTakeCorpseTicket`: `GetSTGameplayGameMode` (0x0523DF00), then `GetCorpseTicketManager` (0x05121880), then
  `USTNPCCorpseTicketManager::TakeTicket(npc)` (0x051E0370). If there is no game mode, no manager, or no ticket, it
  calls the virtual at vtable +0x9E8; if that returns false it InstaKills the NPC with overkill.
- A client has no game mode, so TakeTicket never runs there. Every client decision comes from the virtual alone, while
  the host decides with the ticket manager. Two writers of "does this corpse stay".
- The call runs inside the mod's card 131 HandleDying, so card 357's skip (which exempts mod calls) does not apply.
- Seen: b46 (BP_AI_WarriorBug_C) and b47r2 (BP_AI_Trooper_Morita_C). The card 361 row is logged once per caller per
  map, so the true count is unknown. b47r2 client: 8 TryTakeCorpseTicket rows, 0 StartOverkillState calls.

## To find out before any fix (static first)

1. What the virtual at +0x9E8 is and what it returns for a bug and for a 2D trooper.
2. What InstaKillTarget does to an NPC that is already dying, on the client: any state, lifetime or gore change, or
   nothing.
3. Whether the host's ticket-less InstaKill reaches the client by an existing signal (card 357 Gib signal).

## Fix shape if 2 shows an effect

One writer: the client does not run the InstaKill from this site; the host's outcome arrives by its signal. Count
rows on both sides (not once per map) so a run can compare: client skips equal host ticket-less kills.

## Answers (junior, static, 2026-11-14; no build, no run)

Sources: disassembly of the lane 2 exe (same image as play build 5's), PDB publics (`llvm-pdbutil dump --publics`),
and the client logs of b46, b47 and b47r2 (15 client TryTakeCorpseTicket calls in all).

1. **The +0x9E8 virtual is `ASTCharacter::IsOverkilled` (RVA 0x050F1380).** The AActor-primary vtables of
   ASTCharacter, ASTNonPlayableCharacterBase, ASTEnemyBase and ASTAttackSystemEnemyBase all hold it in that slot; no
   ST NPC class with a public vtable overrides it, and a Blueprint class (BP_AI_WarriorBug_C, BP_AI_Trooper_Morita_C)
   cannot override a native virtual. It reads the health component (+0x688) and returns its byte +0x149
   (`0x37E75A0`: `movzx eax, [rcx+0x149]`). The health-change handler writes the two bytes at 0x4A3E4AC..0x4A3E511:
   +0x148 Dead = Health <= 0, +0x149 Overkilled = Health <= -|OverkillThreshold| (inclusive, `comiss; setae`). For a
   normal death (bug or 2D trooper) it returns false: every client card039 edge before a ticket call says
   "overkilled 0". So with no game mode the client InstaKills every corpse that reaches HandleDying.
2. **On the client the InstaKill changes nothing.** `InstaKillTarget(npc, true)` (0x5241580) is
   `DealDamage(npc, npc, GetCurrentHealth + GetCurrentArmour + GetOverkillThreshold, true)` (0x5233F60). DealDamage
   builds a default FSTDamageTemplate, a hit result, and calls `ApplyDamageToActor` (0x522DAE0). That makes a damage
   spec (`MakeDamageEffectSpec`) and applies it through the ability system component's
   `ApplyGameplayEffectSpecToSelf` (0x494D1E0). That function first checks `IsOwnerActorAuthoritative` (vtable
   +0x690); not authoritative and no prediction key (the key is 0 from DealDamage) returns an invalid handle (the GAS
   `HasNetworkAuthorityToApplyGameplayEffect` gate). A client's NPC copy is not authoritative, so no attribute change,
   no Dead/Overkilled write, no OverkillDamageEffectReceived. The only other side effects in ApplyDamageToActor are a
   gameplay cue loop and ShuntActor, both driven by the template's cue tags and knockback; DealDamage's default
   template has neither. Logs agree: in 15 client calls no card037 Health row follows the InstaKill for that NPC,
   every later edge for it still says "overkilled 0", and the b47r2 map total has StartOverkillState 0.
   Correction to the trace above: card 357's skip does apply here. `g_c39.in_mod_call` is set only around the mod's
   own `OnDeath`/`OnOverkill` gore calls (enemies.cpp:3012, 3035), not around the card 131 HandleDying, so a client
   StartOverkillState from this path would be skipped as well.
3. **The host's ticket-less kill reaches the client by the existing Gib signal.** On the host the same DealDamage is
   authoritative: Health goes to <= -|OverkillThreshold| (armour absorbs its own share, so Health lands at
   -OverkillThreshold or below; the Overkilled compare is inclusive), the damage effect calls
   `UADHealthComponent::OverkillDamageEffectReceived`, whose mod detour sends the Gib frame (`c39_send_gib`,
   enemies.cpp:2139). The same path for an already dead corpse is in b47: host OverkillDamageEffectReceived on a
   WarriorBug at t=348293907 (dead 1, state 4), client Gib guid 0x242 at 348293935, then the client's own
   StartOverkillState skipped by card 357 at 348293959. Not seen live with the InstaKill as the cause: the host's
   card 361 detour logs nothing, and no logged run ran out of tickets.

## Outcome

No behaviour change. The site is named in card 361's caller table ("ASTNonPlayableCharacterBase::HandleDying via
TryTakeCorpseTicket", exe+0x5192EFE), and the card 038 comment in hooks_internal.h no longer calls TryTakeCorpseTicket
a no-op on a client. Both ride the next build; nothing was built for this card.
