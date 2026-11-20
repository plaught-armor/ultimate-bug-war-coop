# Card 040: Where do the ×1.5 and ×0.1 hits come from?

Status: sent 2026-11-03.

Read `docs/STATE.md` first ("Mod code rules", "Runtime facts"), then card 037's report (step 5, "Damage per shot") and its senior review. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

Card 037 logged every hit on a bug as `ADGEDamage` with magnitude 40. Health then fell by 40 on most hits, but some relayed client hits gave −60 (×1.5) and some −4 (×0.1). The 4 host hits in that sample were all −40, too few to tell whether this is normal hit-location damage or something our relay does wrong (a wrong hit component or bone, wrong source tags, or a missing attribute on the client's pawn). This card finds the stage that scales the damage and says whether host and relayed shots go through it the same way. It is a measurement card; change nothing unless the answer is a relay bug, and then ask before fixing.

## Static leads (senior, PDB publics and disassembly)

The damage pipeline is `UADDamageHandler::ProcessDamage` 0x04A3DD30 (public) → `Process` 0x04A3DBD0 → stages over one `FADDamageProcessParams`. The working damage value is the float at **params+0x3C** (every stage below reads or writes it).

- `UADDamageHandler::ModifyDamage` 0x04A3D560 (base):
  - reads the damage, multiplies it by two ASC attribute values through `UADAbilitySystemComponent::GetGameplayAttributeValueOrDefault` 0x04A10410 (default from a constant at 0x14704EB4C; probably a source damage-dealt multiplier and a target damage-taken multiplier, so name them from the attribute);
  - then, unless `IsIgnoreDamageModification` 0x04A3D250, calls `UADDamageModifierSubsystem::GetModifiedDamageByTags(damage, source tags, target tags)` 0x04A3C700. That walks the active preset (`GetActiveDamageModifierPreset` 0x011BA690; the difficulty supplies it through `USTDifficulty::GetDamageModifiers` 0x02D09A50) with `GetModifiedDamage` 0x04A3C130 per modifier set.
- `USTAIDamageHandler::ModifyDamage` 0x0512DB90 (the override enemies most likely use) calls the base first, then walks more aggregated tags. Check whether it also triggers hit volumes.
- **Hit volumes, the main suspect:**
  - `USTHitVolumeComponentBase::TryTriggerVolume` 0x051B1740, `ShouldTrigger` 0x051A69E0;
  - `USTHitVolumeDamageModComponent::ShouldTrigger` 0x051A69F0 and `TriggerVolume` 0x051AEDB0. `TriggerVolume` calls `GetModifiedDamage` with the component's own `FADTagDamageModifierSet` at **component+0x578** and writes the result back to params+0x3C. A second set sits at +0x5D8.
  - `USTHitVolumeGrenadeHoleComponent::TriggerVolume` 0x051AEE80.
  - A weak-spot volume (×1.5) and an armoured volume (×0.1) would explain both numbers.
- Other stages, to log for completeness: `PreProcess` (`USTDamageHandler` 0x051347E0), `PreApplyDamage` (`USTAIDamageHandler` 0x05134510), `SplitDamage` 0x04A3E850, `ApplyDamageToArmour` 0x04A3B110, `ApplyDamageToHealth` 0x04A3B190. `IsIgnoreArmour` 0x04A3D1F0 and `IsIgnoreHealth` 0x04A3D2B0.
- Card 037's `AttributeSetPostEffectApplied` hook (0x04A3B230) is the end point; keep it for the final Health delta.

## Do

1. **Trace the stages on the host**, log only. For each hit on a non-trooper enemy, log one block: the damage at params+0x3C on entry to and exit from `ModifyDamage` (base and AI), each `TriggerVolume` (with the volume component's name and class), `SplitDamage`, `ApplyDamageToArmour` and `ApplyDamageToHealth`, plus the final Health delta. Log the two ASC attribute values the base multiplies by, and the source and target tag containers' tag names (cap the count).
   - Say which pointer in the params is the hit result or the hit component, if you find it (the hit volume must get it from somewhere).
2. **Tag each hit's origin.** Host-fired or client-relayed (card 037's relay path already knows). For relayed hits, also log the hit component and bone our relay passes in, next to what the host's own trace would give for the same shot if that is cheap to get. Otherwise log only the relayed one.
3. **Shots.** One run with both players firing at the same bug type for at least 20 hits each: body shots, then aimed at the head or weak spot, then at legs or armour if the bug has it. The host must get at least 10 hits of its own (card 037 had only 4).
4. **Answer** with a table: origin, volume hit (or none), the multiplier at each stage, final delta. Then say whether host and relayed hits on the same spot give the same multipliers.
5. **If relayed hits differ from host hits on the same spot**, do not fix. Report which input differs (component, bone, tags, attribute) and stop; the fix is the next card.

## Done when

- At least 10 host hits and 10 relayed hits traced end to end, with the ×1.5 and ×0.1 cases each pinned to one stage and one input.
- All new code is log-only and behind a flag (`UBWCOOP_C40_DAMAGE_TRACE=1`). The default behaviour is unchanged.
- Clean build (`/W4 /WX`, `--clean-first`), 84 exports, KERNEL32-only imports. No new crash dumps. Every new function is at most 60 lines. Write `card040x-run.txt` with the rc.

## Risks

- `FADDamageProcessParams` layout beyond +0x10 (the tag aggregators, reached through `[params+0x10]`) and +0x3C (the damage) is not known. Read offsets from the disassembly of the stage you hook, and show the lines you used.
- Hooking `Process` or `ModifyDamage` runs on every damage event (players too). Filter early by the target's class, and cap the lines.
- If no ×1.5 or ×0.1 shows up in 40 hits, report the hit spots you tried and stop. Do not fake the case.

## Senior addendum (2026-11-03): how a hit volume is chosen

Read from the disassembly after the card was sent. Use these as starting offsets, and confirm them in your own log.

- `USTAIDamageHandler::ModifyDamage` 0x0512DB90 calls the base first. Then:
  - If the source tags contain one particular tag (the pointer at 0x14943AAD8), it sets the damage at params+0x3C to 0 and returns (0x0512DCC7).
  - Otherwise it reads `[params+0x10]` → `[+0]` → a shared pointer at +0x280/+0x288 (probably the `FGameplayEffectContext`) and calls its vtable+0x70, which is probably `GetHitResult`. The result is a `FHitResult*`.
  - It skips all volumes unless the hit's byte +0xAD bit 0 is set (`bBlockingHit`).
  - It gets the hit actor from `FActorInstanceHandle` at hit+0xB8 (`FetchActor` 0x03C10B90) and walks that actor's `USTHitVolumeComponentBase` components.
- For each volume, it calls `TryTriggerVolume` 0x051B1740 only if `IsWithinVolume` (vtable+0xA60, 0x05199D80) is true for **both hit+0x70 (TraceStart) and hit+0x28 (ImpactPoint)**. `IsWithinVolume` is `UKismetMathLibrary::IsPointInBoxWithTransform` with the component transform at +0x1E0 and the extent at +0x540 times the scale at +0x220.
- `TryTriggerVolume` calls `ShouldTrigger` (vtable+0xA68), then `TriggerVolume` (+0xA70), then `StartCooldown` 0x051A8090 (+0xA78) with the float at +0x560.
  - Base `ShouldTrigger` passes only when +0x568 is null, which is probably the cooldown timer handle.
  - The damage-mod `ShouldTrigger` also needs the modifier set at +0x578 to change the damage. So a volume on cooldown does not scale the damage.

What this means for the trace:

- The volume test uses the hit result inside the effect context, not a bone name. For every hit, log hit+0x28 and hit+0x70 (doubles, 3 each), the distance between them, `bBlockingHit`, the hit actor, and each volume's IsWithinVolume result for both points and its cooldown state.
- For relayed hits, compare TraceStart with the host's own shots. A projectile or sweep hit has its TraceStart close to the impact. A relayed hit whose TraceStart is the client muzzle, zero, or some other far point fails the box test and gets ×1. One with a near start passes and gets the volume's multiplier.
- The cooldown (+0x560) can make the same spot give ×1.5 on one hit and ×1 on the next. Log it before blaming the relay.
- Relayed shots are not built hit results. The relay only sends fire on and off (`ChanOp::Fire`, hooks.cpp around line 6482), and the host runs the client pawn's own weapon, aimed with the client's view (card 024, `c24_view_point`). So relayed hits come from the host's projectile simulation, just like host shots. The expected outcome is stock volume behaviour, with the difference coming from where each player aimed or from the cooldown. Prove it or disprove it with the trace.

## Report (2026-11-03, junior)

**Answer: both multipliers come from `USTAIDamageHandler::ModifyDamage` → `USTHitVolumeComponentBase::TryTriggerVolume` on the WarriorBug's `STHitVolumeDamageModComponent` volumes. Host-fired and client-relayed hits behave the same: same stage, same inputs, same result. Nothing differs, so there is no relay fix to make for this.**

| Multiplier | Volume (input) | Stage that changes damage | Host hits | Relayed hits |
|---|---|---|---|---|
| ×1.5 | `NerveStem` (ImpactPoint and TraceStart both inside, ready) | AI ModifyDamage 40 → 60 (TryTriggerVolume NerveStem 40 → 60) | 6 (040d 4, 040e 2) | 6 (040d 4, 040e 2) |
| ×0.1 | `Leg_Front_L0` (host), `Leg_Back_R0` (relayed); both points inside, ready | AI ModifyDamage 40 → 4 (TryTriggerVolume Leg_* 40 → 4) | 2 (040e hits 68, 69) | 1 (040e hit 21) |
| ×1.0 | none (all five volumes report IsWithinVolume 0 for both points) | no stage changes it: base 40 → 40, AI 40 → 40, SplitDamage 40 → 40, ApplyDamageToHealth 40 → 40 | 51 | 41 |

Every row's other stages are unchanged: base `ModifyDamage` 40 → 40 (attributes DamageTakenMultiplier = 1, DamageTakenModifier = 0, no tag modifier), `SplitDamage` and `ApplyDamageToHealth` pass the volume's output through, and armour is 0. The Health delta equals the ApplyDamageToHealth damage on every shot.

Totals over card040d + card040e: **59 host and 48 relayed projectile hits traced end to end** (040d host 18 / relayed 20, 040e host 41 / relayed 28).

What the trace shows about the inputs:
- **Hit result.** It comes from the effect context (spec+0x280, vtbl+0x70 GetHitResult). The volume test uses `FHitResult` ImpactPoint (+0x28) and TraceStart (+0x70). On every projectile hit, host or relayed, TraceStart is 1.0 cm from ImpactPoint, bBlockingHit is 1, and the hit actor is the bug. The component is `CharacterMesh0` (SkeletalMeshComponentBudgeted) and BoneName is `None` for both origins. Relayed shots are simulated by the host from the client pawn's weapon, as the addendum expected, so their hit results are the same kind as host shots.
- **Volume choice is purely positional.** Every hit with both points inside a volume triggered it. There was no inside-but-on-cooldown case in 107 shots: cooldown was 0.00–0.01 s and the timer was 0 (ready) every time. So the cooldown is not what decides between ×1.5 and ×1 in these runs.
- **The difference players see comes from where the shot lands.** Hits on the body/thorax get ×1.0, hits in the NerveStem box get ×1.5, and hits in a leg box get ×0.1. card040d aimed only at the actor location (each burst ended before the 1000 ms aim step), which is why it had no ×0.1. card040e stepped the aim through lifts {0, +80, −70, +140, −120} cm every 150 ms, and that reached the legs.

Hits that are not shots, logged for completeness (not part of the counts):
- `npc-other` 25 → 0 at base ModifyDamage (040e hits 51, 62, 63): bug-on-bug friendly damage, zeroed by the tag modifier (GetModifiedDamageByTags 25 → 0) before the volumes run.
- `host-other` 800 → 800 and 800 → 4000 (040e hits 73, 75, 76): instigator and causer are the host player pawn, not a projectile; distance 52–224 cm. These hit bugs that were already dead or nearly dead (Health 92, −20). Probably a melee or corpse-overkill effect. Not traced further.
- `npc-other` 1792 (hit 74): bug-instigated, no hit result. Not traced further.

Run notes:
- The client fires through the `AbilityInputTagPressed/Released` detours (`c40_client_step`), so card 026 relays the press like a mouse press. Its face aim is `c23_view_step` with the same lift steps.
- The host fires with `g_orig_asc_tag_pressed` (`c40_host_fire`) after card 037's once-per-map burst.
- On the first bug the client kills it before the host's press delay, so the host's shots come from the later bugs.
- The host died late in card040e (bug attacks, Health 75 → 0) after the data was in.

Checks:
- Clean build (`--clean-first`, /W4 /WX) rc 0, 84 exports, KERNEL32.dll the only import. New functions are all ≤ 60 lines.
- All new code runs only when `UBWCOOP_C40_DAMAGE_TRACE=1`. card 023's kill step, scan rate and client face aim change only under that flag.
- Crash dumps: 17 before, 17 after (none new).
- `logs/card040d-run.txt` and `logs/card040e-run.txt` have **no rc**: I stopped both runs by hand after "host stops spawning", because no more data could arrive. Each file says so.

Logs and tables:
- `logs/card040d-host.log`, `logs/card040d-client.log`, `logs/card040d-table.txt`
- `logs/card040e-host.log`, `logs/card040e-client.log`, `logs/card040e-table.txt`

New issue from the user, not looked at: **the arachnid attack animations do not show on the client** (seen while watching these runs).

## Senior review (2026-11-03)

Accepted. The ×1.5 and ×0.1 hits are stock WarriorBug hit-volume behaviour (`NerveStem` ×1.5, `Leg_*` ×0.1), chosen by where the projectile lands. Host and relayed hits go through the same stage with the same kind of hit result, so the relay needs no fix.

What I checked:
- The hit-volume lines in `card040d-host.log` and `card040e-host.log` match the report's table: 040d has 8 NerveStem 40 → 60 lines and no leg lines; 040e has 4 NerveStem and 3 Leg 40 → 4 lines.
- The detours: `detour_c40_process` traces only when `g_c40_trace` is set, on the host, on an enemy target, under the hit cap. The other detours pass straight through unless their params are the traced hit's (`g_c40.params`), or, for the attribute detour, while the base stage runs. With the flag off, the game behaves exactly as before.
- No new function is over 60 lines. Crash dumps stayed at 17.

Open points, none blocking:
- The 11 card 040 hooks install even when the flag is off; only their bodies check it. That is harmless, but `GetGameplayAttributeValueOrDefault` and `IsWithinVolume` are hot paths. When the trace is no longer needed, install these hooks only under the flag, or remove them.
- `run.txt` has no rc for either run. Both runs were stopped by hand, and the files say so. Accepted for a log-only card.
- `host-other` 800 → 4000 (×5) on bugs that were already dying is not explained. It is probably a player melee or overkill effect landing in NerveStem or another volume. Not a co-op issue unless the client's version of it behaves differently.
- New issue reported by the user: arachnid attack animations do not play on the client.
