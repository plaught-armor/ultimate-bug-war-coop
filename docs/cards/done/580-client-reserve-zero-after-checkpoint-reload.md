# Card 580: the client's reserve ammo reads 0 for a few seconds after a checkpoint reload

Found in c397b400a (b400, C1M1 checkpoint restart, junior 2026-11-21). In the client shot right after the reload,
the HUD shows 3 / 0 and the selector shows 3 / 0. The 579 shot about 5 s later shows 3 / 60 on both. The magazine
(card 570) is right. Only the reserve is wrong for a moment.

## Trace (static first)

- Which value the HUD's reserve reads (inventory ammo stack for `AmmunitionType.Morita` on the own pawn), and what
  writes it on the client after the restart: the client's own checkpoint load, the card 072 AmmoMirror (the client
  sends `AmmunitionType.Laser 300 @Morita 3 @UnderslungShotgun 0` at 160204036 and no Morita reserve), or a later
  host row.
- Whether the single-player reload shows the reserve at once. If it does, the client's reserve has to be set in
  the same step as the card 570 magazine, from the host's saved count.

## Prediction

Client shots 1 s and 3 s after the reload show the reserve the host holds for slot 1 (60 here). No 0 frame.

## Budget

Trace, then one fix build and one run (it can share the next C1M1 restart run).

## Trace result (senior, 2026-11-21, c397b400a logs)

- Host 160204015: the card 050 poll seeds `AmmoSet ... Laser 300` for the new copy. In the same tick, the card 143
  Restore then sets `AmmunitionType.Morita 0 to 60` on the copy and marks `seed` for the next poll.
- Host 160204324: the next poll sends `Laser 300 Morita 60`. The client applies `Morita 0 to 60` at 160204330.
- The client shot at 160204206 falls in that 309 ms gap, which is over the 100 ms latency target.

## Fix (source, for b402)

`c143_apply_inventory` (`restore_c143.cpp`) sends the AmmoSet at once through `c72_seed` after the restored ammo is
applied. A failed send leaves `seed` set, so the poll retries.

Prediction: the host log has a full `AmmoSet ... Morita 60` row in the same ms as the card143 Restore row. The
client applies it within one frame plus transit (under 100 ms of the restart done row), and a client shot 1 s
after the reload shows 3 / 60.

## Result c397b402a (b402 `5eba36ba27bfa080`, junior 2026-11-21): passed, closed

- Host: card143 "Restore applied" and card072 "AmmoSet ... sent: AmmunitionType.Laser 300 AmmunitionType.Morita 60"
  are both at 161072897, the same ms as predicted.
- Client: the earlier "Laser 300" AmmoSet (recv 186) applied at 161072920. The Restore AmmoSet (recv 191) applied
  Morita 0 to 60 at 161073351. That is 454 ms after the send, but it was not a late send: the client frame from 161072932
  to 161073344 was one 412 ms stall in c135_step / cine_skip_step (the checkpoint cinematic skip, WB_Cutscenes_C,
  "+412 ms after the pause request"). The AmmoSet applied 7 ms after that frame ended.
- Player view: the loading screen WB_Loading_Screen_Final_C closed at 161073360, 9 ms after the apply, so the HUD
  never showed a 0 reserve. The shots 580-reserve-1s, 580-reserve-3s and 570-mag-after-client all show 3 / 60.
- Under the latency target (the 100 ms user rule), the delay a player sees is 0: the counts land before the screen opens. The
  412 ms frame is the game's own skip path, and it is the same when a player presses skip.
