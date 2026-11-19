# 491 Client replay scores cascade sub-objective completes the host does not score

Opened 2026-11-18 10:59 by the senior, from the junior's card 323 report on c245b221a and c245b222a.

## Finding
At BrainBug_SO2 the client replays CompleteSubObjective for SO2_R2, SO2 and SO2_L2 (a cascade) and gains 25000, while the host gains 10000. The client shows +15000 for about 167 ms, then the card 323 settle sets it down by 15000. Seen at 659765354 (c245b222a) and 659384711 (c245b221a): same spot, same amount.

## Fix
Find which of the three replayed completes the host does not score (the host likely scores only the one its trigger completed, and the cascade completes the others without a score). Make the client replay score exactly what the host scored, either by not scoring the cascaded completes on the client or by scoring them on the host if the game does so in single player.

## Done when
c245 BrainBug_SO2: the client's own gain equals the host's, no set-down row, Score mismatch 0.

Reopen when: n/a (open).

## Static trace (senior, 2026-11-18 12:29)
- The ObjectiveComplete score comes from `USTLevelObjectiveSubsystem::ObjectiveComplete` (0x051613C0; the AddFedScore caller exe+0x516149D is +0xDD in it). It calls AddFedScore(FedScoreSystem.Reward.ObjectiveComplete) only when the objective's vtable +0x2B0 (IsSubobjective) is true and +0x6A (`kObjectiveInList`) is set, on the first local player's FedScore subsystem.
- `AddFedScore(tag)` (0x051BCBB0) calls the private `AddFedScore(tag, text, 0)` (0x051BC960): FetchRewardData by tag, times `USTDifficulty::GetFedscoreMultiplier`, rounded.
- c245b223a: both sides broadcast the same 5 subsystem completes (SO2_L1, R1, R2, L2, SO2). The order differs. On the host, L2's completion completes SO2 (caller 0x05150CF3, depth 1). On the client, the replayed CompleteSubObjective(O1, SO2) arrives first, and SO2's own CompleteObjective completes L2 as a child (caller 0x05150CBC). Host +10000, client +25000.
- So the client scores 15000 more on the same set of broadcasts. Either InList (+0x6A) on the markers differs at that moment (the digest compares it only in periodic rounds), or the reward differs per call. Next diagnostic: in `detour_c97_subsystem_complete`, log the FedScore before and after the original call, together with the objective name, IsSubobjective and +0x6A, on both sides. That names the completes the client scores and the host does not.

## Cause found in c245b225a (senior, 2026-11-18 13:09)

The b225 row now reads real scores, and the bug is still there: at BrainBug_SO2 the host's row is `score 331500 -> 341500`
(+10000), while the client's is `331500 -> 356500` (+25000). The client then logs `set-down held 80 ms` at 666783510.

Cause: the host sends the completes in the wrong order. On the host, the native complete of BrainBug_SO2_R2 (depth 0)
completes the parent BrainBug_SO2 inside itself (count 4/4, depth 1, caller exe+0x5150CF3). The card049
CompleteSubObjective hook sends on return, so the nested parent row goes out first. Journal 120 is (BrainBug_O1,
BrainBug_SO2) and journal 121 is (BrainBug_SO2, BrainBug_SO2_R2). The client replays the parent complete while SO2_R2 is
still open. The engine then cascades: it completes SO2_R2 inside the SO2 complete (depth 1, caller exe+0x5150CBC) and
scores it there. When row 121 arrives, R2 is already complete.

Fix (host, card049 send path): while a CompleteSubObjective runs nested inside another (depth > 0), queue its row and
send it right after the outer row, so children always reach the client before their parent. The fix is in the send
order, so the client replay stays unchanged. Proof: c245 BrainBug_SO2 host and client card491 rows show the same gain,
0 card323 set-down rows at that time, and journal order child before parent.

## Real cause in b228 data (senior, 2026-11-18 13:47)
The b226 send order fix holds, but the client still adds one extra award inside its ObjectiveComplete(BrainBug_SO2): `AddFedScore(tag) FedScoreSystem.Reward.ResupplyUpgraded`, caller exe+0x505D615, Blueprint BP_PlayerCharacterMI_C, +15000. The host logs 2 per run (both pawns at Sewers, 669562952); the client logs 2 at 669562977 plus 1 at 669575829. The pawn BP holds ResupplyLevel, FEDScoreThresholdsArray_{Easy,Normal,Hard,Extreme}, SetDifficultyResupplyThresholds and GetCurrentDifficultyLevel. Hypothesis: one client pawn copy runs with a different threshold array or ResupplyLevel, so it pays an upgrade the host does not.
Next: a diagnostic row per pawn on each OnFedScoreGained on both sides (own or proxy, ResupplyLevel, threshold array in use, GetCurrentDifficultyLevel), then a class fix that syncs the difficulty or resupply state (option a), not a client-side drop of the award.

## c245b229a pawn rows (senior, 2026-11-18 14:33)
The 230000 ResupplyUpgraded pair runs on both sides at the same time (host 671669813, client 671669832; own and proxy pawn each), so the client running the pawn's award is normal. The extra one: at BrainBug_SO2 (host 671686229, client 671686255) both sides add ObjectiveComplete +10000 to 344500, and only the client's own pawn then pays ResupplyUpgraded +15000 (client 334500 -> 359500). Card 323's settle sets the client back to the host's 344500 178 ms later (OnFedScoreGained 0). ResupplyLevel reads 1 on both sides, and difficulty 1 on both. Same total, same level, same difficulty, different outcome: the gate reads something else, either the threshold elements (they print `<ArrayProperty>` today) or a per-pawn accumulator fed by OnFedScoreGained amounts, which the client gets more of (relayed broadcasts plus its own adds, never reduced by a set-down).
Next diag: the threshold elements, every numeric property on BP_PlayerCharacterMI_C whose name holds Score, Supply or Threshold, and ResupplyLevel before and after each award. Visible effect today: a 178 ms client score blip, plus whatever the upgrade shows (toast or sound, MS_UI_FED_FedScore_Supply_Upgrade_*), to be checked.

## Closed (2026-11-19, senior, b262 `e138228037a8dede`, c245b262a)
Design: the host owns each pawn's ResupplyLevel and pays the ResupplyUpgraded award, then sends a "U <guid> <level>" row. The client refuses the FedScoreGained ProcessEvent on BP_PlayerCharacterMI_C, so its Blueprint never steps the tier, plays the sound or pays. The U apply sets the level, pays the award through the original, and for the local pawn calls Upgrade() on the support prompt widgets and plays MS_UI_FED_FedScore_Supply_Upgrade_0<level>. The one-shot g_c491_inner_text lets AddFedScore(tag)'s own inner AddFedScoreText pass, while a Blueprint award inside the broadcast still goes through the detours (the b261 depth gate missed it).
Evidence (checked in the logs): host "ResupplyUpgraded #" rows 3 (guids 0x6, 0x14, 0xD0C); client "own step run (sound played" 2, one per own-pawn U row; "not by a U row" 0; card323 "raised at once" 0; card132 mismatches 0 in all 18 rounds; FATAL/EXC 0 on both sides; levels equal after rejoin.
Not done: own-step sounds go through g_orig_c99_sound_2d, so only the U row's "sound played" proves them. One award per pawn per threshold is the game's own behavior with two pawns (seen in b228 on the host too). The 1 -> 0 proxy level from b260 did not recur, and its cause is unknown.
Reopen when: a client score raised at once, a client Supply_Upgrade sound without a U row, or a ResupplyLevel change not by a U row.
