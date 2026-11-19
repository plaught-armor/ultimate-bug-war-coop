# Card 094: Client jump never activates on the host's copy (no vault)

Status: accepted 2026-11-05 (senior). Bind fix reviewed: (tag, handle) dedup, 64-entry cap checked before write. Mantle motion divergence moves to card 101.

Read:

- `docs/playtest-2026-11-05.md`, item 1 and the "Host vs client log comparison" section;
- card 024 (client fire relay: the `InternalServerTryActivateAbility` detour and its `card024: host server-copy` lines);
- card 021 (`client request handle N: activated/failed`).

Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

In the 2026-11-05 playtest the client could not vault over a wall: no animation, it just bumped off. The logs show the client's jump is never confirmed by the host:

- Client log (`client/playtest-menu-20261105-200846.log`): the client's own `GA_HumanJump_C` activates (`card024: client local activation handle 133 = 1`), and its `rot[]` rows show MovementMode 3 with Vz +763.
- Host log (`logs/playtest-menu-20261105-200840.log`): all 133 client jump requests reach the server copy as `InternalServerTryActivateAbility handle ... GA_HumanJump_C: ActiveCount 0, InputPressed 1`. Not one reaches `ActiveCount 1`. Sprint through the same path does (`GA_Sprint_C: ActiveCount 1`).

The mantle/vault is assumed to start from the jump (assets `AM_Arms_Mantle`, `MantleCurve`); confirm that in step 1.

## Do

1. **Cause.** Find why the server copy's `GA_HumanJump_C` does not stay active. Check, on the host for the server copy, in this order:
   - whether `CanActivateAbility` refuses it, and which check (tags, cooldown, cost, `ACharacter::CanJump`, movement mode);
   - whether it activates and ends in the same frame (log `EndAbility`/`CancelAbility` for the jump spec on the server copy);
   - whether the server copy's `ACharacter_Jump` / `ASTPlayerCharacterMI_Jump` runs (both are in the hooks skipped list today);
   - how the game starts a mantle (ability, movement component, or jump branch), and whether that runs on the client, the server copy, or both.
   Log only what is missing, once per jump press.
2. **Fix at the cause** so the server copy jumps and mantles when the client does, and the client is not corrected back. Do not turn off server corrections.
3. **Proof**, one combined scenario run (FAST=1), both sides logging:
   - the client jumps twice on flat ground: the host log shows the server copy's jump active (or the server copy leaving the ground), and no correction over 2 uu at landing;
   - the client vaults one wall or ledge it could not cross before: the client ends on the far side on both screens.
   rc 0, ERROR/FATAL 0, no new dump.

## Done when

- The Report names the cause with the logged values, the fix, and the proof run.
- The Report ends with "Report complete."

## Report

### Cause

InputTag.Jump drives two abilities. The host binds it on the client's server copy to handle 31 (GA_HumanJump) and
handle 32 (GA_Mantling_Search). The client's card 018 bind replay (`bind_entry` in attributes.cpp) skipped any tag
it had already bound, so it bound Jump to handle 31 only. Logged before the fix: client tag map 8 tags, and
"AbilityInputTagPressed InputTag.Jump ... this tag's handles 1". The client's jump never started the mantle search,
so it never reached GA_Mantling_Move (handle 45) and could not vault.

The server copy's jump itself was always fine. ActiveCount 0 right after InternalTryActivateAbility(31) is normal:
EndAbility runs in the same frame, the same as for the host's own jump.

### Fix

attributes.cpp `bind_entry` now dedups each bind by its (tag, spec handle) pair, not by its tag alone. C18State
gains `bound_handles[kC18MaxBoundTags]`, paired with `bound_tags`. Server corrections are untouched.

jump_c94.cpp holds log-only diagnostics, installed only with UBWCOOP_C94_LOG=1: ability activate and end, commit,
SearchForMantleLocation, mantle gameplay events, ClientEndAbility, and monitored tag changes.

DLL: build/dwmapi.dll sha256 718ad12a93ca2896...

### Proof

Runs used FAST=1, both sides logging with UBWCOOP_C94_LOG=1. Scenarios: c94-proof.txt (proof1), c94-scout2.txt
(scout7) and c94-proof2.txt (proof3). Each run: rc 0, ERROR/FATAL 0, no new crash dump.

- **Binds.** The client now binds Jump to 31 and 32: "bound 9 tags", "this tag's handles 2".
- **Mantle on the server copy.** The client activates handle 32. The host logs "server copy
  InternalTryActivateAbility(handle 32) for a client request = 1, ability GA_Mantling_Search_C". When the search
  finds a ledge, both sides activate GA_Mantling_Move (handle 45) at the same position, for example
  (-774.8, 20968.6, 824.0) in proof3. It ends after about 108 ms on both sides.
- **Flat jumps.** proof3 at (-559.5, -1714.5, 103.5): two jumps, and the server copy jumps both times at the same
  spot. Landing corrections are at most 1.8 uu in proof1, scout7 and proof3, under the 2 uu limit. The proof3 walk
  had 2.9 and 2.1 uu corrections at y 6494 and 6643, which are not jump landings.
- **Vault.** In scout7 the client mantled over the ledge at (-766.7, 20968.6, 817.4) on the third try. It then went
  on along the course on both sides, with server copy and client positions matching at each jump, out to
  y 26950. It also mantled at y 22928, 22998 and 26943. Proof3 did not cross that ledge: four tries, all four
  mantles activated on both sides, and the client ended each try back at y 20968.6.

### Open: mantle movement diverges

The server copy runs GA_Mantling_Move, but its pawn does not follow the client's mantle path. The client is then
corrected mid-mantle.

proof3, in-air mantle at (-796.4, 20856.9, 853.2), host corrections in order:

| Server copy | Client | Error |
|---|---|---|
| (-796.4, 20858.9, 847.1) | (-796.4, 20856.9, 853.1) | 6.4 uu |
| (-796.5, 20866.8, 821.5) | (-796.5, 20862.7, 841.1) | 20.0 uu |
| (-796.5, 20870.3, 817.2) | (-796.5, 20863.9, 841.3) | 24.9 uu |
| (-796.6, 20874.3, 817.2) | (-796.8, 20908.9, 877.9) | 69.9 uu |
| (-796.6, 20876.3, 817.2) | (-796.9, 20914.4, 879.8) | 73.2 uu |
| (-796.6, 20877.9, 817.2) | (-797.0, 20934.0, 888.9) | 91.0 uu |
| (-796.6, 20880.2, 817.2) | (-797.4, 20982.6, 915.1) | 141.7 uu |
| (-796.6, 20880.9, 817.2) | (-797.4, 20992.9, 921.5) | 153.1 uu |

The server copy's Z stays at 817.2, so it is not rising, and it creeps only about 20 uu forward. The client rises
to z 921 and moves 136 uu forward. The client's next jump was at y 20562.8, so it was corrected back off the
mantle.

Scout7 showed the same pattern on the first try at y 20968.6: server Y stayed at 20968.6 while the client reached
20988.9, with corrections of up to 38.7 uu.

So the mantle still violates "the client is not corrected back" whenever the server copy's mantle motion lags. The
cause is not found yet. The next step is to find how GA_Mantling_Move moves the pawn (USTMantleAnimation /
FSTMantleAnimationFrame: root motion, timeline or SetActorLocation) and why that motion does not run, or runs short,
on the server copy.

Testing stopped there at the user's request. They will playtest the bind fix themselves.

Report complete.
