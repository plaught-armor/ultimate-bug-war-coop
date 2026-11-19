# 231 — Corpse settles 100 to 150 uu apart on FED_C3M1 (digest Pose:1)

Soak runs c127-c3m1 (guid 0x4e4, body 15, 105 uu, client log line 55786) and c127-c3m1-r2 (guid 0x4a0, body 6,
150 uu, 2 of 22 bodies over 100 uu, client line 56629): one `BP_AI_WarriorBug_C` corpse from the turret stage (bugs
placed at z 1500) stays off on the client in every later digest round, "simulating 0". A player would see one dead
bug's limb 1 to 1.5 m away from where the host has it.

Static first: why the final pose row (card 137 freeze, card 152 stream) does not bring the settled bodies together
for a corpse that fell from height; compare with a corpse of the same run that did converge.

Done when: the cause is named from the log and source, and a fix or a reasoned "leave" is in this card.

## Step 1 answer (senior, logs only, 2026-11-13): not reproduced on later builds, reasoned leave

- Both failing corpses have the same row shape in the c127 logs: `pose freeze check guid ... snap 2: ... 0.0 uu
  ...; simulating 1; held 0`, then no later snap. The last row the client got left its copy simulating; it then
  settled by its own physics and one body ended 105 or 150 uu off. Corpses that converged in the same runs end
  on a `simulating 0; held 1` row (18 and 12 of them).
- Later builds changed that final-row path (card 152 held final, card 186). On them the class is gone:
  `logs/c229-c3m1-client.log` 17 corpses, 0 over 100 uu; `logs/c242-c3m1-client.log` 33 rounds, 22 corpses, 0
  over 100 uu, 239 held finals, 0 frozen rows simulating.
- No code. Reopen only on a `round N pose guid ... is N uu from the host's ... simulating 0` row from a current
  build; the first thing to read then is that guid's last `pose freeze check` row.

2026-11-16 senior: the pose freeze row returned on b121 (c418b121a, guids 0xdc and 0xe6, 296 and 345 uu). Cause and fix are in card 424 (held final with gravity on); this card closes with 424.
