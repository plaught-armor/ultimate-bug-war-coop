# 451: a bark is ranged from the host's view only

Status: in b151 `7d8087dbc3cee8ab`; hook runs (run c448b151b, one `played 1` row). Client hearing and subtitles are playtest checks.

## Fault (static trace)

`UADBarkSubsystem::RequestBark` (exe+0x4C5BF00) reads the event data class default object (class +0x110), takes the
view point of `UWorld::GetFirstPlayerController` (vcall +0x708) and refuses the bark when the speaker's root location
is farther than the range (default object +0x34, float) from it (exe+0x4C5C0D9). Both `ASTNonPlayableCharacterBase::PlayBark`
and `PlayBarkOnNPCNearThePlayer` end there. NPC logic runs on the host only, so a client next to an NPC that is out of
the host's bark range hears no bark. The sound itself already reaches the client: `UADBarkSubsystem::PlaySound`
(exe+0x4C5B880) calls `UGameplayStatics::SpawnSoundAttached`, a card 136 capture point.

## Fix

Host, always on: hook `RequestBark`. When a remote player's pawn is within the range of the speaker, the range field is
set to 1e18 for the one call and restored after it. The host's own copy of the sound is attenuated by distance as any
far sound.

## Proves it done

- Host row `card451: host bark <class> from <npc>: a client is within its range <r>, host range check passed over;
  played 1` in a run where the client stands at NPCs and the host is far (the c448 C1M1 route with the host held at
  the start works). At most 8 rows per process.
- No row while both players stand together is not a failure: the row prints only when the range was widened, which
  also happens when both are in range. Count of rows over a normal run gives the call rate (efficiency check: the
  hook adds one loop over 4 slots per bark request).
- 0 EXCEPTION, digest 0 mismatches.

## Risk, open

- `FBarkDelegate::Broadcast` runs on the host for the widened bark; if a subtitle hangs on it the host sees a subtitle
  for a far bark. Check at playtest. The client gets no bark delegate at all (subtitles for barks, if any, are a
  separate gap: not traced).
- A widened bark takes the speaker's active bark slot and cooldown on the host as any bark does.

## Bark subtitles on the client (traced 2026-11-16, not fixed)

`UADBarkSubsystem::PlaySound` (VA 0x144c5b880) does three things: `SpawnSoundAttached`, `FBarkDelegate::Broadcast` with the `FADActiveBark`, and an entry in the subsystem's active bark array, which `GetBarkSubtitleForActor` / `GetAllBarkSubtitles` read. Card 136 relays only the sound, so the client has no active bark and no bark subtitle. Fix shape if the playtest shows the host gets bark subtitles and the client does not: host hook on `PlaySound` sends speaker, event data class and voice type; the client calls its own `PlaySound`, and card 136 leaves that sound out. Playtest check, with subtitles on: does the host see bark subtitles at all, and does the client.

Run c448b151a (dll 7d8087dbc3cee8ab): 6 `card451` host rows for `BP_BarkType_Idle_C` in 4 ms, all `played 0` (another gate, chance or cooldown, refused them). The hook runs; a `played 1` row is still owed.

## Bark subtitles are shown, so the client gap is visible (senior, 2026-11-18 12:35, static)

`WB_Secondary_Subtitles` (Gameplay_HUD, read from the iostore with `tools/iostore/iox.py`) gets the bark subsystem with
GetWorldSubsystem and binds its `OnBarkStarted` / `OnBarkFinished` delegates (custom events `StartBark` / `End Bark`,
param `ActiveBarkData`). It looks the voice up in the `BarkVoiceToSecondarySubtitle` map (the GRUNT01 to GRUNT11 voices),
gets the line's subtitle and the speaker's name (cast to `STNonPlayableCharacterBase`, `STLocalisedNameInterface`), and adds a
line item. So the host shows trooper bark subtitles. On the client, `PlaySound` (0x04C5B880), which broadcasts
`FBarkDelegate` with the `FADActiveBark`, never runs, because NPC logic is host-only and card 136 relays only the sound. So the
client shows none. This is a visible gap under the "all effects sent" rule, not a playtest check.

Path on the host: `RequestBark` (0x04C5BF00) checks the gates, `ProcessNextBarkIndex` picks the line, an active bark
entry is stored (speaker as a weak pointer), and the sound loads async. The load callback calls `PlaySound(event data, sound)`.
PlaySound runs `SpawnSoundAttached` and `FBarkDelegate::Broadcast(active bark)` (exe+0x4C5BB51). It has no speaker argument:
the speaker comes from the active bark entry.

Fix shape: a host detour on `PlaySound` sends one row (speaker NetGUID, event data class, voice class, line index; read
the `FADActiveBark` layout at the Broadcast call first). The client builds the same `FADActiveBark` for its copy of the
speaker and broadcasts its own subsystem's `OnBarkStarted`, then `OnBarkFinished` when the relayed sound ends (or after
the line's duration). The client calls no sound of its own, since card 136 already plays it. Proof: a client shot with a secondary
subtitle line while a trooper barks near it, and one client row per relayed bark.

## FADActiveBark fields (senior, 2026-11-18 12:38)

FADActiveBark is a reflected USTRUCT (TCppStructOps at exe+0x1162D20), so the fix needs no static offsets: read and
write its fields by name through the reflection helpers. WB_Secondary_Subtitles reads these members of ActiveBarkData:
Speaker (the actor, cast to STNon_Playable_Character_Base and STLocalised_Name_Interface for the name), Bark (event
data), BarkType, BarkVoice (class, the key into BarkVoiceToSecondarySubtitle), LineChoice (index) and VoiceLines. The
widget erases a line by matching the same struct in OnBarkFinished (BarkToErase). The host row therefore sends speaker
NetGUID, Bark object path index, BarkVoice class, BarkType and LineChoice. The client fills an FADActiveBark with those
fields and broadcasts OnBarkStarted, then OnBarkFinished with the same struct when the relayed sound (card 136) ends.

## Closed (2026-11-18 14:33)
Evidence: b231 `ba9d7b915f00ef61`, bark_c451.cpp. c448b231a: host `card451: host t=672397758 bark started Default__BP_BarkType_Idle_C line 4 speaker BP_AI_Trooper_NonCombat_C guid 0x1A6 sent to 1 of 1`, the client relay row 20 ms later, the finish relayed too; tally relayed started 1, finished 1, dropped 0, own broadcasts 0. The client shot client-c448b231a-c451-bark-subtitle.png shows "Non-Combat Trooper: I'm doing my part.".
Not done: the finish match is proven on one bark only (one bark on the C1M1 route). The c448 step order fix (the 451 step after the card 449 block) is unrun.
Reopen when: a client subtitle line stays after its bark ends, a `WARN card451` row appears, or the client tally shows dropped above 0 or own broadcasts above 0.
