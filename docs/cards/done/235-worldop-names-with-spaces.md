# 235 — WorldOp rows whose name holds a space are refused by the client

Soak c234-c3m2 and c234-c3m2-r2 (FED_C3M2, DLL `20cf859dd191a8fe`): `WARN card098: client: bad WorldOp frame`,
6 per join. The host rows are `L MissleSilo_C_<n>|Fire Bomb <x> <y> <z> 4` (timeline name "Fire Bomb"). The sender
(`world_c98.cpp:324`) writes `"%c %s %.0f %.0f %.0f %u"`; the parser (`c98_receive_world`,
`world_c98.cpp:1558`) reads the name with `%191s`, which stops at the space, so the row fails and nothing is
applied. A player would see: on the client the silo "Fire Bomb" timelines do not follow the host (join replay
and, by the same parser, the live row at the silo launch).

Fix at the parser, one place: take the kind from the first character, then split the last four fields
(x, y, z, value) off the end of the text; the name is everything between. No wire change, names with spaces need
no escape. Keep the bad-frame WARN, and add the refused text to it (capped) so the next class names itself.
Sweep: every other row parser in `mod/src` that reads a game name with `%s` (list them in the report; fix the
ones that share the defect, same method).

Proof: one run on `tools/scenarios/c234-c3m2.txt`: 0 `bad WorldOp frame`, the six `L MissleSilo_C_*|Fire Bomb`
rows applied on the client (ledger line). Regression rides in the next soak.

Senior sweep 2026-11-12 (`sscanf_s` in `mod/src`): the same WorldOp text is parsed a second time by the ledger
(`ledger_c132.cpp:207`, `%c %159s`, and `:234`), which must take the same split. Parsers that read a component
or timeline name, which the game may name with a space: `door_c95.cpp:818` (trailing `%64s`),
`objectives.cpp:2590` (owner and component), `decal_c132.cpp:1020`. Parsers that read only actor object names or
asset paths (no spaces by engine rule) stay as they are: `hold.cpp:224`, `interact.cpp:103`, `door_c95.cpp:427`,
`dropship.cpp:519`, `grenades.cpp`, `gib_c137.cpp:790`, `hit_c132.cpp:294`. The junior checks the three
component parsers against their senders and fixes only those whose sender can write a space.

## Result (junior, 2026-11-12, DLL d561aa4a8628ec3f)

- `mod/src/ubw/world_c98.cpp:1552` `c98_world_parse`: the kind is the first char; a bounded backward scan finds the
  fourth space from the end, so the name between keeps its spaces; then `" %lf %lf %lf %u%n"` must end the text.
  Used at `:1581`; the WARN at `:1611` now quotes the refused text.
- `mod/src/ubw/ledger_c132.cpp:205` `c132l_world_split`, the same split, used at `:227`.
- Component parsers against their senders:
  - `door_c95.cpp:819`: sender c95_collision_text writes `object_name(component)` last, which can hold a space;
    now `%64[^\n]`.
  - `objectives.cpp:2585` `c65_activate_parse`: sender c65_send_activate writes prop_name of the component FName,
    which can hold a space; owner `%64s`, comp up to the last space, reset `%d`. WARN quotes the text.
  - `decal_c132.cpp:1020`: no change; its sender (`:325`) never sends a comp with a space
    (`strchr(t.comp, ' ') == nullptr`).
- Open: `ledger_c132.cpp:254` c132l_named Collision still reads the comp with `%63s`, so a spaced name is truncated
  in the ledger field (the key is unaffected). Not in this card's order.

Run c235-c3m2 (tools/scenarios/c234-c3m2.txt, FED_C3M2): 0 `bad WorldOp frame` on both sides (c234-c3m2: 6). The six
rows applied on the client, e.g.
`card098: client t=155700918 world L MissleSilo_C_0|Fire Bomb -59220 9422 1152 20 (BP_MissleSilo_C): applied
timeline state: position 0.00 of 10.00, playing 0, reversing 0`, MissleSilo_C_0 to _5. Digest rounds 1-10,
0 mismatches. 0 CRASH, 0 FATAL/ERROR. Failed lines 142, 146, 147, 182 are the turret block; c234-c3m2 failed 146, 147
and 182 the same way; 142 (client 1610 uu from the turret warp point) is new in this run.
