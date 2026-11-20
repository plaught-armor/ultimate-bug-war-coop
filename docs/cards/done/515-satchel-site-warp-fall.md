# Card 515 (harness, waits): satchel plant presses E while the client is still falling

c514a (b288) Park_SO1_B VOID, 3 tries: `warp client BP_SatchelPlacementSite_C 200 front` lifts the pawn by
kC75WarpAbove (z -320 over a -531 floor); `satchel_plant` looks and presses E about 500 ms later, while the client is
in MovementMode 3 (card490 row 11285522, mode 1 only at 11286170). card456: "hold interrupted ... speed 801.8,
trace now null". Fix: in `satchel_plant`, mark the client before the site warp, then wait (best effort, 2 s) for
`card490: client t=\d+ pc 0 \S+ role 2: .*MovementMode 1,` since-mark before the look and the key.

Applied to tools/drive.py 2026-11-19 after c263L314 ended (`LANDED_RX`, `LANDED_WAIT_S` next to `HOLD_RX`; the
mark and wait in `satchel_plant`'s side loop). py_compile clean. Proof: the next C4M1 satchel step plants on the first
try with the walking row before the look.

## Patch (senior, 2026-11-19, staged: drive.py was in use by c263L314, apply after that run ends)

In `satchel_plant`, in the side loop:

```python
LANDED_RX = r"card490: client t=\d+ pc 0 \S+ role 2: .*MovementMode 1,"  # module level, next to HOLD_RX

        client.mark = len(log_lines(role_log(client)))  # before the site warp
        ok, detail = send(host, f"warp client BP_SatchelPlacementSite_C 200 {side}")
        ...
        # Card 515: the warp lifts the pawn by kC75WarpAbove; look and press E only once it walks again.
        landed, _ = wait_log(client, [LANDED_RX, "since-mark"], 2)
        if not landed:
            note(f"satchel plant: {side}: no walking row within 2 s of the warp, going on")
```

Limits: card 490 rows are logged on change and capped per map (`kC490Rows`); a warp that lands with no fall, or a
map past the cap, waits the full 2 s and goes on. The later `client.mark` before the key stays as it is.

## Closed 2026-11-20 (c244b322a, b322, C4M1)

All four satchel steps went through `routine_satchel` and `satchel_plant` and completed on try 1: Park_SO1_C,
Park_SO1_B (the step that VOIDed in c514a), Park_SO1_A and LZ_SO1. The drive log has 0 `no walking row` notes, so each
plant waited for the walking row before the look and the key.
