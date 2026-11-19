# 344 No client confirm in 60 s: the host returns to the main menu

User decision, 2026-11-14: "Send host back to main menu if that happens." It replaces the card 320 timeout, where
the host started the mission alone.

## Cause (static)

`c320_step` (`mod/src/ubw/intro_c135.cpp`) released the title-screen hold `kC135WaitMs` (60000 ms) after the
host's continue when a waited-for client still owed its IntroDone. The release unpaused the world, so the host
played on while the client was still loading or stuck.

## Fix (b29)

- `c344_no_confirm`: at the bound the host calls `USTGameInstance::ReturnToMainMenu` (0x050FFD10, virtual, one
  argument: the game instance; new symbol in `tools/symbols.txt`). That is the pause menu QUIT path: its checkpoint
  save past its own guards, then `OpenLevel` to `3D_Frontend` with `FromLevelQuit`.
- The hold stays on (world paused) until the travel ends the map; a late confirm no longer releases it
  (`c320_try_release` checks `to_menu`).
- The client follows by the existing card 290 row `quit` and the card 154 listen, so both players end in the
  host's frontend and can start the mission again.
- The card 052 panel shows "The other player did not finish loading: mission not started" on that menu
  (`C52Status::NoConfirm`, cleared on the next non-menu world).
- No game instance or no resolved function: WARN row and the old release, so the host never hangs in the hold.
- Test lever, client: `UBWCOOP_C344_NOCONFIRM=1` withholds IntroDone.

Not changed: a waited-for client that logs out still releases the hold and the host plays on ("disconnect").

## Rows

- `WARN card344: host t=<ms> <n> client(s) did not confirm within 60000 ms of the host's continue: host returns to
  the main menu (ReturnToMainMenu)`
- `card344: client t=<ms> IntroDone withheld (UBWCOOP_C344_NOCONFIRM=1, <path>)`

## Prediction (one run, `tools/scenarios/c344-no-confirm.txt`, client with `UBWCOOP_C344_NOCONFIRM=1`, hold on FED_C2M1 after the travel)

- Client: one `card344 ... IntroDone withheld` row.
- Host: `card320 ... hold waits for client slot 1 (join), clients 1`, then the `WARN card344` row 60000 to 60100 ms after its `confirm from host` row,
  no `hold off ... (timeout)` row, then a Browse row with `FromLevelQuit 1`, `hold off ... (map end)`, and
  `card052 ... world ... 3D_Frontend ... panel on, status 6`.
- Client: loads `3D_Frontend` from the host after the host's travel (card 045/154 rows), no failure dialog row.
- Between the WARN row and the map end the host world stays paused: no dropship or objective row on the host.
- A run without the client's withheld row proves nothing.
