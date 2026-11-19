# 475 The client's FedScore is lower than the host's after a rejoin

Found 2026-11-17 09:08 in run c423b177a (FED_C2M1, b177 e7197c28f8ab7bb8), junior report; both HUDs show it in the shots
`host-c423b177a-c300-closed.png` (160,000) and `client-c423b177a-c300-closed.png` (140,000).

## Trace (logs/c423b177a-client.log)

The client's score as printed by its `card098 ... client FedScore N` rows: 0, 10000, 40000, 70000, 80000, 120000,
140000, 160000 before the card 440 disconnect (equal to the host's 160000), then 0 (5 rows) and 140000 (295 rows)
after the rejoin. The live `world K` row of nest `_C_5` at t=566390655 ran "Health base 0 and OnDeath broadcast"
(score 140000 -> 160000); its journal replay at t=566406481 ran "Destroyed set and SaveFullyLoaded run (no OnDeath)".

## Cause (static)

The mod has no score sync. `USTFedScoreSubsystem` is a per-machine world subsystem; each side adds to its own total
when its own copy of an event runs (objective completes, OnDeath of a building, NPC kills). The sides agree only
while every score event runs on both. A rejoin starts the client at 0; the journal replay re-runs the objective
completes (140000 here) but replays kills without OnDeath, so every kill and building score before the rejoin is
lost. The same holds for a first join into a running map, and for any event that scores on one side only.

## Fix (class: the host's total is the truth)

- Host: send its `GetFedScore` total to a client at the end of that client's journal replay, and again whenever
  the host's total changed and the client's last acknowledged total would differ (one small row; piggyback on the
  card 327 player row or the digest is fine).
- Client: set its total to the host's. `USTFedScoreSubsystem::AddFedScore(FGameplayTag, FText, int)` (exe+0x51BC960)
  adds at `[this+0x44]` (exe+0x51BCA8D: `add [rsi+0x44], r15d`) and then broadcasts the delegate at `this+0x30`
  (exe+0x51BCAD5..0x51BCAD9). The setter writes the field and runs the same broadcast so the HUD redraws, without a
  reward popup. Junior confirms the field with `GetFedScore` (exe+0x175E880) and the broadcast arguments before
  coding.
- Digest: a `Score:` entry (host total against client total), so drift shows as a mismatch.
- Rows: `card475: host t=.. score N sent to slot S (reason)`, `card475: client t=.. score A -> N from the host`.

Proof: c423 rejoin block: client `card475 ... 140000 -> 160000`, both HUD shots show the same number, digest 0.
Sibling sweep: the mission-end score list (`WB_TrooperEndScoreList`) reads the same subsystem; check its rows too.

## Senior static trace of `AddFedScore(tag, text, amount)` (exe+0x51BC960), 2026-11-17

- exe+0x51BCA6B..0x51BCA8A: the delta `r15d` is the amount times a float factor (the FedscoreMultiplier path), rounded.
- exe+0x51BCA8D: `add [rsi+0x44], r15d`: the total (int32 at subsystem +0x44).
- exe+0x51BCAD5..0x51BCAD9: `lea rcx,[rsi+0x30]; call 0x14505D1A0` with `edx = delta`, `r8 = FText copy`: broadcast
  of the multicast delegate at +0x30 (the HUD popup and counter). Skipped when the world check before it fails.
- exe+0x51BCAF6..0x51BCB24: appends `{FGameplayTag tag (8 bytes), int32 delta}` (12-byte element) to the TArray at
  +0x50 (Num at +0x58, Max at +0x5C, grow by exe+0x11653C0). This is the per-reward breakdown; the mission-end score
  list most likely reads it, so a setter that writes only +0x44 leaves the end list short of the HUD total.

## Design (senior): replay the host's score events, not the total

1. Host: in `detour_c99_score_text` (player_c99.cpp:780) read the total before and after the original; when it
   changed, journal a score row `{tag name, delta}` per event (bounded table, per map; coalesce equal tags by summing
   deltas and a count so the table stays small). Nothing is sent live: live events already run on both sides.
2. Join / rejoin replay: after the objective replay, the host sends its score rows and its total. The client, for
   each tag, applies `host delta sum - client delta sum of that tag` when positive: field +0x44 and one array element,
   no popup; one broadcast at the end only if the HUD counter does not redraw without it (junior checks with a shot;
   an empty-text popup is not acceptable).
3. Digest: `Score:` entry with the total, so live drift is a mismatch with a number.
4. Rows: `card475: host t=.. score rows R, total N sent to slot S`, `card475: client t=.. score A -> N (R rows
   applied, T tags)`, `WARN card475: client t=.. total N differs from the host's M after the replay`.

Open before coding (junior, static): confirm the array element layout with `Load` (exe+0x51CDB10) and the end-list
reader; confirm whether the client's own objective replay adds score before or after the host's rows arrive (the
per-tag difference rule is order-safe either way).

## Confirmed statically (senior, 2026-11-17)

- `GetFedScore` (exe+0x175E880) is `mov eax,[rcx+0x44]; ret`: the total is the int32 at subsystem +0x44.
- The TArray at +0x50 is the `FedScoreHistory` (`FSTFedScoreHistoryEntry` = tag + amount, also a field of
  `FSTSaveGamePlayerData`); `USTFedScoreUtilityLibrary::GetFedscoreHistory` reads it, so the mission-end list is built
  from it. A setter must add history elements, not only the total.
- The delegate at +0x30 has a gameplay listener: `USTTacticalSupportComponent::HandleFedScoreUpdate(FedScoreAmount)`.
  Tactical support availability follows the score, so the client must run the broadcast after the replay (one
  broadcast per applied row with the row's delta; an empty FText is acceptable only if the shot shows no popup, else
  call `HandleFedScoreUpdate` on the local pawn's component and redraw the counter directly).
- `exe+0x42A6DC0` is the world-validity check before the broadcast (returns the owner when it is a live world).

## Closed

Closed 2026-11-17 on b179 `e1cde94de299e31b` (`score_c475.cpp`). Built from the game's own history array instead of
a detour journal: the host sends per-tag sums ("R" frames) and the total ("E") on ChanOp::Score as the last join
replay; the client adds the positive per-tag differences (total, history element, OnFedScoreGained to every binding
except the `WB_Fed_Score_Log_C` popup). Digest has a `Score` entry. Real `Load` is exe+0x51CDAF0;
`GetFedscoreHistory` exe+0x523B290 feeds `WB_TrooperEndScoreList`.
Runs c423b179a and c423b179b: host `score rows 2, total 160000 sent to slot 1`, client `score 140000 -> 160000 (1
rows applied ...)`, 0 `WARN card475`, digest mismatches 0 in every round, HUD 160,000 on both sides 10 s after the
rejoin, no "+N" popup at 3 s. Gate c448b179a passes.
Playtest watch: a `WARN card475` row or a `Score` digest mismatch reopens.
