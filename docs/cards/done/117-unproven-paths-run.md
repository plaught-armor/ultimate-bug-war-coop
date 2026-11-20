# Card 117: One run for the unproven paths of cards 112, 113 and 114

Status: written 2026-11-06 (senior); sent and accepted 2026-11-06.

Read:

- card 112 "Not proven" (rescale of held waves at a join or leave);
- card 113 "Not run" (grace-end drop after 45 s);
- card 114 Report (the ammo and healing getters were never called).

Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`. DLL base: `17a60609748a4770` (card 114; cards 115 and 116 built no play change).

## Why

Three code paths shipped without a run. One combined run proves all three, so the user's playtest can focus on
feel.

## Do

One FED_C2M1 run, FAST=1, sped up with warps. Test-only runner commands are fine. In order:

1. **Card 114, ammo and healing.** In the user file, set `AmmoReceivedMultiplier` to 3 and
   `HealingPeriodMultiplier` to 0.25 for Medium. The client picks up ammo (warp to a crate or a drop), and the
   host's health regenerates after a hit. The log shows each getter called on the side that reads it, with the
   value applied. If a getter is still never called, find what the game uses instead and name it.
2. **Card 112, rescale.** The host starts the first encounter with held waves (card 112: Medium waves set up next to
   a Large or ExtraLarge wave). The client drops (`host kick client` or a client quit) while those waves are held,
   then rejoins. The log shows `card112: rescale` down at the drop and up at the rejoin, inside the 45 s grace or
   past it, as card 113 decides.
3. **Card 113, grace end.** The client stays out for more than 45 s after a drop. The log shows `left or not back
   after the grace` and the shrink.

Restore the user file to the defaults after the run.

rc 0, ERROR/FATAL 0, no new dump. Fix only what the run shows broken, and say so.

Stop when that run shows all three, or names the one that cannot be reached; the user will playtest.

## Done when

The Report has the lines for each of the three paths, any fix, the run length, rc, ERROR/FATAL, dumps and the DLL
hash. Ends with "Report complete."

## Report

Base: the 17a60609748a4770 source. `attack_cap.cpp` is out (grep for it and its hooks is empty). Two runs: c117a
found two problems, which are fixed below; c117b is the proof run. Scenario: `tools/scenarios/c117-proof.txt`
(FED_C2M1, FAST=1, Medium AmmoReceivedMultiplier 3 and HealingPeriodMultiplier 0.25 in the user ini).

### 1. Card 114, ammo and healing

**Ammo.** The game reads AmmoReceivedMultiplier through the getter, natively from
`USTWeaponUtilityLibrary::AddAmmunitions` (call at exe+0x522A074, after `GetCurrentDifficultySettings`, and only
when the receiver's ASC has a native gameplay tag). Pickups are host-authoritative, so the read is on the host. The
client fired 10 s, and the host then warped it onto the placed `BP_AmmoPickup_MoritaLarge_C`:

    host   75776059 card114: host or solo reads AmmoReceivedMultiplier = 3.000 (STDifficulty)
    host   75776059 card071: host t=75776059 AddAmmunitions on server copy BP_PlayerCharacterMI_C (BP_PlayerController_C), returned 60: AmmunitionType.Morita asked 240, 180 to 240 (max 240) AT MAX; Ammo sent: AmmunitionType.Morita 60
    client 75776080 card071: client t=75776080 Ammo AmmunitionType.Morita +60: 180 to 240, AddAmmo returned 60

The pickup asked 240 (its base amount x3), and the inventory cap held it to 60. In c117a the client fired only
2.5 s, so its reserve was still full and the pickup did not fire (no AddAmmunitions at all).

**Healing.** `GetHealingPeriodMultiplier` is never called. Its only caller is the exec thunk, and no Blueprint
references it. The game reads the value inline instead, from USTDifficulty +0x44, in
`USTSelfHealAbility::OnGiveAbility` (RVA 0x05130500, the read at exe+0x51306C5). There the regen period is set as
diff+0x44 x ability+0x3BC and stored into the GE spec at +0x54. New log-only hook:
`USTSelfHealAbility_OnGiveAbility` → `detour_c114_heal_give` (difficulty.cpp). It logs the multiplier it reads and
the period that results, 2 lines per map:

    host   75750472 card114: host or solo self heal given (GA_SelfHealPassive_C): HealingPeriodMultiplier 0.250 (inline read, STDifficulty) x base period 0.100 = regen period 0.025
    client 75758936 card114: client self heal given (GA_SelfHealPassive_C): HealingPeriodMultiplier 0.250 (inline read, STDifficulty) x base period 0.100 = regen period 0.025

The line appears again on each side after the rejoin. The ability is given on both sides, and both read 0.25. Limit:
the host was in the bug group, but no log traces player health, so the run does not show the host taking a hit and
regenerating. The card114 damage lines cover NPC receivers only. That part is for the user to watch in a play.

### 2. Card 112, rescale of held waves at a drop and a rejoin

The host warped into the first WarriorBug group, which set up 2 held waves. The client disconnected about 54 s into
the host world, and card 045 reconnected it 7967 ms later:

    host   75804498 card113: session players 2 -> 1 (present 1, left or not back after the grace)
    host   75804498 card112: rescale STEncounterWaveBase (data Default__BP_PWD_Warriors_Medium_C): budget 26 -> 16 (base 16, 2 -> 1 players), spawn list 5 entries cost 25 -> 3 entries cost 15   (x2)
    host   75804498 card112: 2 waves rescaled for 1 players, 2 not started in the table
    host   75813027 card113: session players 1 -> 2 (present 2, join)
    host   75813027 card112: rescale STEncounterWaveBase (data Default__BP_PWD_Warriors_Medium_C): budget 16 -> 26 (base 16, 1 -> 2 players), spawn list 3 entries cost 15 -> 5 entries cost 25   (x2)
    host   75813027 card112: 2 waves rescaled for 2 players, 2 not started in the table

In c117a the disconnect came 35 s into the host world, inside the 45 s grace, so the count held and nothing was
rescaled. The scenario now waits past the grace.

### 3. Card 113, grace end

The client quit (`RequestEngineExit`) 7 s after the rejoin, with the host world well past 45 s old. The shrink came
at once:

    host   75820276 card113: session players 2 -> 1 (present 1, left or not back after the grace)
    host   75820276 card112: rescale ... budget 26 -> 16 (2 -> 1 players)   (x2), 2 waves rescaled for 1 players, 2 not started in the table

The host ran another 75 s alone, and the count stayed at 1. Note: in c117a the client crashed instead of quitting
(below), and the host saw that drop only after about 60 s, at the net timeout.

### Fix: client crash after the mid-map rejoin (c117a)

About 1 s after the c117a client reconnected, it crashed: EXCEPTION_ACCESS_VIOLATION reading 0x8F8 at
`USkeletalMeshComponent::GetAnimInstance`+0, called from `UADSkeletalAnimatorComponent::StartAnimationSequence`+0x56.
The call came under a received RPC (ReceivedRPC → ProcessEvent → StartAnimationSequenceCallback), on an actor that
had just spawned. The function checks its animation data (+0xF0) but calls GetAnimInstance on its mesh (+0xF8)
with no null check, and the mesh was still null. The dump is
`prefix/.../Saved/Crashes/UECC-Windows-4954A6594905B7FBF825638CFF794984_0000/UEMinidump.dmp`.

The fix is a guard in the card 052 style. New symbol `UADSkeletalAnimatorComponent_StartAnimationSequence`
(0x04A24840) → `detour_skel_anim_start` (guards.cpp). It skips the call and logs
`guard: <side> UADSkeletalAnimatorComponent::StartAnimationSequence(mesh=null) skipped` when +0xF8 is null.
c117b did not crash, but the guard line did not fire either: the race did not recur. So the guard is in place, but
this run does not prove it.

### Run

- c117b: host log 75719531 → 75897567 ms, 178 s; rc 0.
- ERROR/FATAL: 0 host, 0 client. New dumps: 0.
- c117a: rc 0, ERROR/FATAL 0, 1 client dump (the crash above).
- DLL: 4ff26b46c39033ac (sha256 prefix; xxh64 cbc8712a866a9c56), copied to `build/dwmapi-c117-4ff26b46c39033ac.dll`; deployed in the game dir.
- UBWCoop-Difficulty.ini is restored to its pre-run copy, the defaults (Medium Ammo 1, Healing 1).

Report complete.
