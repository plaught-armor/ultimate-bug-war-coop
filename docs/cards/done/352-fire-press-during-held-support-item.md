# 352 A fire press during a held support item reaches the weapon relay

b18 run 3 (`logs/b18r3-c310-client.log`): LMB about 200 ms into the client's flare hold sent `Fire 1` at 295260933
and `Fire 0` at 295261036 with 0 rounds; the host relayed the press to the server copy's Morita (card028 Idle to
Attack01 at 295261035). The host's own pawn does not fire during a hold.

Step 1 (static): find what the game's own fire ability checks during a held support item (tag or montage block), and
make the card 024 relay refuse a press under the same condition. Prediction: with LMB 200 ms into the hold, 0 client
`Fire 1` rows and 0 host card028 Attack01 rows inside the hold; the throw is unchanged. A client shot at that moment
also settles card 324's last open path.

## Result (b38 `24c5e7454353752f`, runs `b38`, `b38r2`, 2026-11-14): accepted

- Static: the fire, reload and underslung abilities do not start under the tag `WeaponSystem.Blocked`; the flare
  ability, `GA_GrenadeThrowAbility` (frag, incendiary, satchel), melee and weapon switch put it on the pawn. ADS names
  no such tag and stays relayed. One rule covers every held item.
- Fix: `c26_own_fire` (`hooks_ubw.cpp`) does not send a client press while `c352_blocked` (`fire.cpp`) reads the tag
  on the own pawn or weapon; a press still held when the tag goes is sent then (the pressed call repeats per frame).
- Run: LMB 200 ms after Four refused (`b38` client line 5409, t=339318995); 0 client `sent Fire 1` and 0 host card028
  Idle to Attack01 rows inside the hold; throw unchanged (destroy at 884 ms), SO4 completed. Shot
  `client-b38-c352-hold-lmb.png` (junior viewed): flare in hand. This was card 324's last open path.
- Not proven by a run: the "Blocked gone while held" send (0 rows).
- Senior change after the run, not built yet: the host backstop in `c24_relay` logs only and no longer refuses. The
  copy's tag can trail the client's by the round trip, and a refused press would skip the card 276 re-press.
