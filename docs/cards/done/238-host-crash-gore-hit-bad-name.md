# 238 — Host crashes in the gore hit hook on a damage event with no hit result (PLAY BLOCKER)

Soak c234-c5m2 (FED_C5M2, DLL `20cf859dd191a8fe`), 110 s in: `CRASH 1/4: EXCEPTION_ACCESS_VIOLATION at
ubwcoop+0xEADC3`, reading 0x150D0000. `fname_text` (`mod/src/ue/reflect.cpp:117`) copied a name entry that a
garbage FName index pointed at. Caller: `hit_queue` (`mod/src/ubw/hit_c132.cpp:162`),
`hit_name_token(event + kEventHit + kHitBoneName)`, from `detour_c132_gore_damage` (no env gate: the play build
has it). The fx log just before shows an AI trooper's projectile (`BP_AI_Trooper_Morita_C`) hitting a WarriorBug.
A player would see: the host's game dies when an allied AI trooper's shot hits a bug; the session ends for both.
Any map with AI troopers.

Cause to confirm statically (no run): `hit_queue` reads a hit result at `event + kEventHit` from every event. Find
in the game's gore damage function (the hooked one, PDB) how the game itself tells an event that carries a hit
result from one that does not (damage event type id, a flag, or a pointer), and what the event is for an AI
projectile.

Fix, both layers:
1. `hit_queue` reads the hit result only from events that carry one, by the game's own discriminator. Events
   without one queue no row (counted, one capped line naming the event type).
2. `fname_text` refuses an FName whose entry is not inside the name pool (block index under the pool's current
   block count, offset inside the block); it returns "?" and counts it. Every caller gets the guard. If the pool
   bounds cannot be read from the PDB facts already in the tree, say so and stop at layer 1 plus a report.

Sibling sweep (report, fix what shares the defect): every other hook that reads an FName or a pointer out of a
caller-supplied event or parameter struct whose type can vary (`hit_wound_value`, `kEventBone`, `kHitComponent`
via `g_weak_get`, card 157 claims, card 166 markers).

Proof: c234-c5m2 runs past the AI trooper fight to the end, 0 CRASH lines, the new counter line shows the
skipped events, host hit rows for player hits still sent.

## Result (junior, 2026-11-12, DLL d561aa4a8628ec3f)

Static finding: FADDamageEvent always holds an inline FHitResult; there is no discriminator to test, so the bad
read is the FName itself. Layer 1 became "count the bad name, send the row with None"; layer 2 is a pool guard on
every FName read that dereferences the entry.

- Layer 2, `mod/src/ue/reflect.cpp:138` `pool_resolve` reads the FNamePool and ready flag from GetDisplayNameEntry
  (exe RVA 0x1334190: `80 3d` ready flag at fn+6, `48 8d 15` pool at fn+0x11); on a byte mismatch it logs
  `WARN card238: ... FName pool guard off` and falls back to the unguarded read. `:158` `pool_entry_checked`
  refuses an index whose block is past CurrentBlock (+0x8) or 8192, a null block, or an entry (header plus
  length) past the cursor (+0xC) of the current block or past 0x20000 of an older one. `:192`
  `fname_entry_checked` counts refusals; `fname_text` returns "?" on a refusal. Declared at `reflect.h:127`.
  Pool offsets +0x8/+0xC/+0x10 and the block size are assumed from the UE5 layout, not read from the binary.
- Layer 1, `mod/src/ubw/hit_c132.cpp:133` `hit_name_token` returns false and "None" on a refusal; `:150`
  `hit_note_bad_name` logs `WARN card238: host hit ... FName not in the name pool, sent as None` (cap 10 per map,
  `:75`); the row is still queued (`:184`). The map totals line adds `%u bad bone names (card 238)`.
  The order asked for the instigator and causer classes in that line; dropped, their offsets are not known.

Sweep (same defect: raw g_name_entry and then the entry read):

| Site | Finding | Change |
|---|---|---|
| `difficulty.cpp:200` c114_fname | raw entry read | fname_entry_checked |
| `grenades.cpp:1685` fname_into | raw entry read | fname_entry_checked |
| `hooks_ubw.cpp:1290` c50_name | raw entry read | fname_entry_checked |
| `marker_c166.cpp:95` name_token | gets "?" now | "?" counts as None |
| hit_wound_value | kMapScan 64 within the 128 inline bits, game-built map | none |
| kHitComponent via g_weak_get | engine weak resolve checks index and serial | none |
| card 157 detour_c157_damage_caused | raw qwords and a float, no names | none |
| card 166 read_tags/read_values | through fname_text, now guarded | none |

Run c238-c5m2 (tools/scenarios/c234-c5m2.txt, FED_C5M2): scenario passed, 359 of 359 lines (c234-c5m2 was killed by
the host crash, dump UECC-...889B_0000). Host and client 0 CRASH, 0 FATAL/ERROR, no crash dumps. Both sides:
`card238: FName pool at 00006FFFFC95B680, ready flag at 00006FFFFC95B419`. No bad name met in this run (0 card238
WARN lines; client totals `... 522 received, 508 applied, ... 0 bad bone names`). 40 `card132: host hit` rows on the
host. Digest rounds 1-8, 0 mismatches. Host frame windows 127 and 489 at map load, then 532-585.
