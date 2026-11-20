# Card 019: Client sprint raises speed

Read `docs/STATE.md` first: the "Mod code rules" section, the "Runtime facts" line about `tools/run_pair.sh`, and rows 016 to 019. Then read card 018's report and its senior review. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`. Run logs: `logs/card018{a,b}-{host,client}.log`. Symbols: `tools/symbols.txt`.

## Why

Since card 018, client look and jump work. Sprint is the last basic on-foot input that does not.

- In card018b, Shift+W (`InputTag.Walk`, spec handle 30) reaches the server. The host logs `InternalTryActivateAbility(handle 30) = 1, ability GA_Sprint_C` once per server frame (8 to 17 ms apart, 12 lines before the log cap).
- MaxWalkSpeed stays 700.0 on the client and on the server copy of the client pawn.
- An ability that activates every frame is probably ending or being cancelled every frame. Card 016 found that `HandleInputTag_Pressed` fires every frame while a key is held, so the client may send a new activation each frame.
- `USTSprintAbility::HandleSprintChange` (0x051272C0) applies a set-by-caller gameplay effect. It does not write MaxWalkSpeed itself. Nobody has yet traced what turns that effect into a movement speed.
- No baseline exists for the host's own pawn. We do not know what a working sprint looks like in these logs.

## Do

1. **Host baseline.** Make the host's own pawn hold Shift+W on foot for about 3 s in an unattended run. Reuse the autoinput script on the host, or add a host-side variant of it; say which. Log once per row:
   - MaxWalkSpeed;
   - speed;
   - the GA_Sprint_C activations;
   - the ends and cancels.

   This gives the reference behaviour.
2. **Sprint lifecycle on the server.** Hook and log the following, rate-limited, for the client pawn's ASC and the host pawn's ASC:
   - activate, end and cancel for GA_Sprint_C (with `bWasCancelled`, and the caller of the end);
   - `HandleSprintChange` (its arguments);
   - the gameplay effect it applies: class, set-by-caller tag and magnitude, the attribute it modifies, and the active-effect handle.

   State why the ability re-activates every frame: does it end at once, or does it stay active and get re-activated?
3. **Speed path.** Find what turns the sprint effect or attribute into a higher speed. Candidates:
   - an attribute-change delegate on the character;
   - a CMC `GetMaxSpeed` override that reads the attribute;
   - a Blueprint event.

   Name the function and show it running in the host baseline.
4. **Fix the client.** Make the smallest change that makes the client match the host baseline. Use the game's own path. Do not write MaxWalkSpeed directly unless the report shows the game does the same. If the fix is in the input path (for example, pressed sent every frame instead of once), fix it at the narrowest layer and check that jump still works.
5. **Other bound tags (one press each, log only).** On the client, press Crouch and HumanMelee once each. Report whether the server activates the matching spec (handles 43 and 42 in card018b). Do not fix anything here; report only.

## Done when

- The host baseline rows show what a working sprint looks like, or show that host sprint does not raise speed either. That is a valid result; report it.
- The client's Shift+W rows show speed above 700 on the client and on the server copy with no large `ClientAdjustPosition` corrections. Or sprint is reported open with the lifecycle and speed path named and the blocker proven from the logs.
- The cause of the per-frame re-activation is stated from the logs.
- The crouch and melee results are reported.
- Clean build (`/W4 /WX`), 84 exports, KERNEL32-only imports. No new crash dumps in the runs. Look and jump still work in the final run.

## Runs

- Build: `python3 -I tools/gen_proxy_exports.py && python3 -I tools/pdb_rva.py gen && cmake --build build --clean-first`.
- Run: `DEPLOY=1 HOST_EXTRA_ENV=UBWCOOP_AUTODROPSHIP=1 CLIENT_EXTRA_ENV="UBWCOOP_AUTOINPUT=1 UBWCOOP_AUTOINPUT_DELAY_MS=40000" DONE_PATTERN='autoinput: script done' tools/run_pair.sh <tag> 240`. Never pass `-nosound`; it crashes the host. Audio goes to a null sink by default.

## Risks

- Sprint may be ended deliberately by a stamina or ground check. If so, the attribute values behind that check may be non-replicated on the client, as the camera set was. Audit them the same way (with the `CPF_Net` flag) before you change anything.
- Hooks taking UE by-value structs (`FGameplayEffectSpec`, `FGameplayTag`, `TSubclassOf`) receive a hidden pointer. Check each new detour signature against the disassembly; card017a crashed on exactly this.
- A host-side autoinput must not run on the client pawn, and it must not change the client run's timing. Gate it by its own env var.

## Report

Runs: `card019b` (Blueprint trace), `card019c` (first input mirror, after ServerMove), `card019d` (mirror in TickComponent), `card019e` (plus the client movement re-init), `card019f` (final: plus the crouch/melee try-activate log). The final run used the card's run line with `UBWCOOP_HOST_SPRINT=1 UBWCOOP_AUTOINPUT_DELAY_MS=1500` added to the host env. It ended on the done pattern after 136 s. Host and client rc 0, FATAL/ERROR 0, WARN 0, no new crash dumps.

### 1. Host baseline

- New env var `UBWCOOP_HOST_SPRINT=1` (`config.h`). It reuses the autoinput engine with a separate host script: W for 3 s, then LeftShift + W for 3 s, starting `UBWCOOP_AUTOINPUT_DELAY_MS` after the host's landed character is possessed. It runs only on the host's own pawn. The client script and its timing are unchanged.
- Result (card019e and card019f, identical): GA_Sprint (handle 1) ends once, then activates and stays active. `HandleSprintChange(1.100)` applies the effect: handle -1 -> 2, MaxWalkSpeed 700 -> 770. WalkSpeed then climbs through `PostAttributeChange` to a peak of 962.1 and drops back to 700 on release. So host sprint does raise speed. The host pawn's measured velocity stayed low during its sprint (about (0, 202)); why was not traced (possibly blocked after landing), so MaxWalkSpeed is the reference here, not velocity.
- The first activation ends at once because Shift lands one frame before W: `IsMovingForward` sees a zero last input vector (see 2). The second press sustains.

### 2. Lifecycle and the per-frame re-activation cause

Hooks on GA_Sprint `ActivateAbility`/`EndAbility`/`CancelAbility`, `USTSprintAbility::HandleSprintChange` (the GE class, set-by-caller tags and magnitudes, active-effect handle) and a Blueprint `UFunction` invoke trace of GA_Sprint, per side (host-own, server-copy, client-own), each capped.

**Cause.** GA_Sprint_C's Blueprint `IsMovingForward` calls `PawnMovementComponent.GetLastInputVector`, which returns `APawn::LastControlInputVector` (+0x308). When it is zero, the Blueprint calls `RemoveActiveGameplayEffectBySourceEffect` and `K2_EndAbility`. On the server copy of a remote pawn nothing calls `AddMovementInput`: client moves arrive as `ServerMove` acceleration only. So `LastControlInputVector` was always zero there. The server ended the ability straight after activating it; `ClientEndAbility` ended the client's predicted instance; the client still held Shift, so its input path sent a new activation the next frame. The ability ends at once each time; it is not left active and re-activated. Shown in card019b: the server-copy trace has `GetLastInputVector` return all zero, followed by `K2_EndAbility`.

### 3. Speed path

`GE_Sprint` (set-by-caller magnitude from `HandleSprintChange`) modifies the `WalkSpeed` attribute of `USTAttributeSet_Movement`. `USTAttributeSet_Movement::PostAttributeChange` (0x051D7BB0), called from `SetNumericValueChecked` (caller exe+0x49E0DB1), writes `UCharacterMovementComponent::MaxWalkSpeed`. It runs in the host baseline (36 lines, WalkSpeed 700 -> 770 -> 962.1 -> 700) and on the server copy (700 -> 1050 -> 1270.9, then back to 700 on release).

### 4. Fixes (two, both through the game's own path)

1. **Server: client move input mirror.** A pre-hook on `UCharacterMovementComponent::TickComponent` (0x03A59FC0), host only. For a pawn with LocalRole 3 / RemoteRole 2 (the server copy of a client pawn) and a non-zero CMC `Acceleration` (0x328, the last `ServerMove` acceleration), it calls the game's `APawn::Internal_AddMovementInput(Acceleration / MaxAcceleration, false)` (0x03F67150; the `FVector` by-value argument is a pointer to the caller's copy, checked in the disassembly). `bForce=false` keeps the game's `IsMoveInputIgnored` check. The CMC's own `ConsumeInputVector` at the top of TickComponent then moves it into `LastControlInputVector`, so the Blueprint sees forward input. It does not move the remote pawn: the server's CMC simulates remote pawns from `ServerMove` only. The first try (card019c) added the input after `ServerMove_PerformMovement`. Ticks without a ServerMove then had a zero vector, and moves summed up to 2.0, so the server still ended sprint now and then. Ticking from the persisted acceleration fixed both.
2. **Client: movement attribute re-init.** `USTAttributeSet_Movement` has no `CPF_Net` properties (all flags `...0015`, `CPF_Net 0`), so the client's replicated set object is all zero (WalkSpeed base 0). When the server's sprint GE replicated, the client aggregator recomputed WalkSpeed 0 and `PostAttributeChange` set the client's MaxWalkSpeed to 0 (card019d: MaxWalkSpeed 0.0 during and after sprint). The fix is card 018's camera fix applied to this set: at the same once-per-ASC point after `OnRep_SpawnedAttributes`, it re-runs `AddAttributeSetFromData` (0x04A0F060) with the pawn definition's `PlayerMovementData_C`. The log shows `WalkSpeed base 0.000 current 0.000 -> base 700.000 current 700.000`, on the same set object. There is no direct MaxWalkSpeed write.

The client's sprint GE handle stays -1 on the client: `HandleSprintChange` runs outside the prediction window there, so the effect is not predicted. Client speed comes from the server's replicated GE on top of the re-inited base.

**Result (card019f, as in card019e).**

- Client rows: rot[21] MaxWalkSpeed 770 (speed 95.8, accelerating); rot[22] MaxWalkSpeed 1251.1, speed 1251.1; rot[23] MaxWalkSpeed 1240.8 (W released just before the sample); rot[24] back to 700.
- Server copy: `HandleSprintChange(1.500)` effect handle -1 -> 3, MaxWalkSpeed 700 -> 1050, peak 1270.9; released by `ClientEndAbility`.
- The client's GA_Sprint shows the one early End (Shift before W, as on the host), then one Activate that stays active. The held key's later `InternalTryActivateAbility(handle 30)` calls return 0 because the ability is already active. There is no re-activation.
- `ClientAdjustPosition`: 5 in the whole run, distances 0.1, 1.8, 0.7, 14.8, 11.3. The two largest are at the sprint start and release (rot[22] 2, rot[23] 1); there are none mid-sprint.

### 5. Crouch and HumanMelee (log only)

The client presses LeftControl (`IA_Crouch`, `InputTag.Crouch`, handle 43) and V (`IA_HumanMelee`, handle 42) once each. A new `card019:` try-activate log has its own cap, because the card 016 jump cap is spent by then. Host log, card019f:

- `InternalTryActivateAbility(handle 43) for a client request = 1, ability GA_Slide_C`. The Crouch tag is bound to the slide ability. It activates on the server.
- `InternalTryActivateAbility(handle 42) for a client request = 1, ability GA_HumanMelee_C`. It activates on the server.

Not checked: their effects (montage, slide movement, hit).

### Other observations (not fixed)

- `HandleSprintChange` uses two multipliers: 1.1 (host-own; 700 -> 770) and 1.5 (server copy; 700 -> 1050). In card019f the server copy switched 1.5 -> 1.1 -> 1.5 -> 1.1 during one sprint. What picks the multiplier was not traced.
- The client's held-Shift retries (`= 0`, already active) spent the client's try-activate log cap during the sprint, so the client side of the crouch/melee presses is not logged. The host side is.
- Cosmetic note from the senior review (pre-session client lines labelled "host" by `side()`) is not fixed; `side()` was not changed in this card.

### Regression checks (card019f)

- Look: ControlRotation yaw changes across rot[3]-rot[12] and pitch to 270.1 at rot[13].
- Jump: rot[19] MovementMode 3, Velocity z 614.6; `GA_HumanJump_C` (handle 31) activates on the client and on the server.
- Build: clean with `/W4 /WX`; DEPLOY confirmed 84 exports and KERNEL32-only imports.

### Code

- `mod/src/hooks.cpp`: the card 019 lifecycle/GE/Blueprint/attribute logs; `detour_cmc_tick` + `mirror_client_input`; `card019_movement_fix` (called next to the card 018 camera fix); the card 019 try-activate log in `detour_internal_try_activate`.
- `mod/src/autoinput.cpp`, `autoinput.h`, `config.h`: `UBWCOOP_HOST_SPRINT` host script; client script steps for LeftControl and V after the sprint step.
- `tools/symbols.txt`: `APawn_Internal_AddMovementInput`, `UCharacterMovementComponent_TickComponent`, `USTAttributeSet_Movement_StaticClass`, `UFunction_Invoke`.

## Senior review (2026-11-03)

**Accepted. Client sprint works.**

Checked against the logs and the code:

- card019f: FATAL/ERROR/WARN 0 on both sides. The newest crash dump (`UECC-Windows-5DFCEA9D...`) is from 2026-11-02, so it is older than this card.
- Client re-init line: `movement set 0000000042EB5500 -> 0000000042EB5500, WalkSpeed base 0.000 current 0.000 -> base 700.000 current 700.000`. It reuses the same set object.
- Client rows reach MaxWalkSpeed 1274.3 under Shift+W. The host logs `InternalTryActivateAbility(handle 30)` twice, instead of once per frame in card018b.
- Five real `ClientAdjustPosition` events in the whole run. The other grep hits are per-row counters ("ClientAdjustPosition 0").
- Input mirror (`mod/src/hooks.cpp`, `mirror_client_input` and `detour_cmc_tick`):
  - Attacked: AI pawns (RemoteRole 1, skipped), the local host pawn (RemoteRole 0, skipped), zero acceleration (skipped, matches the client), and an unresolved call (`resolve_all` fails before anything is patched).
  - The `FVector` argument is passed by pointer, as MSVC x64 requires.
  - It runs once per tick, before `TickComponent` consumes it, so it does not move the pawn.
  - Side effect to watch: the mirror also feeds input to a client-possessed mech or other pawn with a CMC and RemoteRole 2. That is probably what those pawns expect, but nobody has tested it.

**Open, moved to card 020:**

- Sprint speed differs between players. The host's own pawn peaks at about 962 (multiplier 1.1). The client peaks at about 1270, and the server copy flips between 1.1 and 1.5. In this run's host log, `HandleSprintChange` shows 51 lines with value 0.000 and one with 1.100. The host HUD shows "UNARMED" while the client's shows MORITA MK1 (a known issue). If the multiplier depends on the equipped weapon, both issues have one cause; that is unproven.
- Crouch (GA_Slide_C, handle 43) and HumanMelee (handle 42) activate on the server. The client side of those presses is unlogged.
