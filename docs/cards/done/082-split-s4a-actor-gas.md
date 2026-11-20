# Card 082: Split S4a: actor and GAS to `ue/actor.*` and `ue/gas.*`

Status: sent 2026-11-05 15:01 to junior ultimate-bug-war-coop-bb, after card 081 was accepted. Drafted 2026-11-05 (senior).

This is the first half of S4 in `docs/split-map.md` ("Move order"). S4 lists four files. This card moves `ue/actor` and `ue/gas`; card 083 moves `ue/input` and `ue/widget`.

Read `docs/split-map.md` ("Proposed files" rows `ue/actor.cpp` and `ue/gas.cpp`, every "Section map" row that cuts names to them, "Engine typedefs and constants left in a block", "Renames"), the card 080 Report and Senior review, and `mod/src/ue/reflect.h`. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

Every gameplay card reads actors, components, the world's levels and the ability system component. These are the next leaves after reflection. Input, widget, net and chan all call into them.

## Map corrections (senior)

The section map's `ue/actor.cpp` cuts are a dumping ground for typedefs. Apply these rules instead of the rows where the two disagree:

1. **A typedef goes with its pointer, and only when its pointer moves** (the card 080 rule).
   - A typedef whose pointer goes to `ue/input`, `ue/widget` or `ue/net` stays in `hooks.cpp` until that card. Examples: `GetLocalPlayerFn`, `FKeyDestructorFn`, `AddMovementInputFn`, `ConsoleCommandFn`, `RequestScreenshotFn`, `GetInterfaceAddressFn`, `WorldGetFn`, `ActorNetModeFn`, `GetPlayerControllerFn`, and the card 031 widget and FText typedefs.
   - A typedef that two files share goes in the file of its first moved pointer. For example, `GetSubsystemFn` goes to `ue/actor.h` with `g_get_data_table_subsystem`, and `ue/input` uses it from there.
2. **Already moved.** `kMaxLoggedSkips` and `kMaxLoggedEvents` moved in S1. `ObjectPtrIsAFn`, `WeakGetFn`, `WeakAssignFn`, `GetNameEntryFn`, `FNameCtorFn`, `FindFirstObjectFn`, `StaticLoadObjectFn`, `StaticFindObjectFn`, `MemoryMallocFn` and `MemoryFreeFn` moved in S3. The rows that still name them are stale.
3. **Wrong file in the map:**
   - `kActorInfoAvatar` is an `FGameplayAbilityActorInfo` offset. It goes to `ue/gas`.
   - `kFunctionParmsSize` is a `UFunction` offset. It goes to `ue/reflect.h`.
   - `caller_chain` and its `kMaxStackFrames` / `kStackFrames` walk the stack and call `exe_rva`. They stay in `hooks.cpp` until S5 (`ue/hooktable`).
4. **`read_vec3`**, which card 080 kept back, goes to `ue/actor` with `Vec3`.

## Scope

**`ue/actor.{h,cpp}`**, namespace `ue::actor`:

- The pointers, each with its typedef:
  - `g_actor_get_world`, `g_get_attach_parent_actor` and `g_world_is_paused` (with `ConstBoolFn`), `g_set_actor_location`, `g_set_hidden_in_game`, `g_life_span`;
  - `g_add_component_by_class`, `g_finish_add_component` and the `FindComponentFn` pointer;
  - `g_query_params_ctor` and `g_line_trace_by_profile` (with `ViewPoint`);
  - `g_spawn_params_ctor`, `g_world_spawn_actor`, `g_spawn_ai_from_class` and `g_spawn_niagara`;
  - `g_get_data_table_subsystem` (with `GetSubsystemFn`).
- The offsets: `kActorOwner`, `kActorRootComponent`, `kActorCollisionFlags`, `kActorEnableCollisionBit` and `kActorFindComponentByClass`; `kPawnController` and `kControllerPawn`; `kPcCursorFlags`; `kComponentOwner`, `kComponentRotation` and `kComponentTranslation`; `kCharacterMovement`, the `kCmc*` offsets and the `kMove*` offsets; `kTransformTranslation`; `kHitFlags` and `kHitLocation` (`FHitResult`, read after the line trace; the map cuts `kHitFlags` to `ue/input`, which is wrong); `kWorldLevels` and `kLevelActors`; `kPauseRequesters`; `kTeleportPhysics`; `kRowMapElement`; the `kSpawn*` offsets.
- `Vec3`, `read_vec3`, `kRadToDeg`, `quat_pitch_yaw`, `component_pitch_yaw` and `actor_location`.
- The renames from "Renames": `c25_dist` becomes `vec_dist`, `kC25MaxLevels` / `kC25MaxActors` become `kMaxLevels` / `kMaxLevelActors`, and `c28_find_component` becomes `find_component`.

**`ue/gas.{h,cpp}`**, namespace `ue::gas`:

- `g_get_asc` (with `GetAscFn`), `g_find_spec_from_class` (with `FindSpecFromClassFn`), and `g_set_numeric_attribute_base` (with `SetNumericAttributeBaseFn` and `GameplayAttributeRaw`).
- The offsets: `kAscAbilityNum`, `kAscSpawnedAttributes`, `kAscSpawnedAttributesNum`, `kAscSpecs`, `kSpecAbility`, `kSpecHandle`, `kSpecBytes`, `kSpecActiveCount`, `kAttrDataBase`, `kAttrDataCurrent`, `kCpfNet`, `kAbilityActorInfo`, `kActorInfoAvatar`, `kAttributeChangeNew` and `kAttributeChangeOld`.
- `avatar_of`.

If a listed name is not where the row says, or a moved function calls a name that stays in `hooks.cpp`, say so in the Report and leave that name where it is.

## Rules

1. **Includes and namespaces.**
   - `ue/core.h` includes both new headers.
   - `hooks.cpp` gets one `using namespace` line each for `ue::actor` and `ue::gas`, placed next to the `ue::reflect` one.
   - No `using namespace` goes in a header. `ue/actor.h` and `ue/gas.h` may include `ue/reflect.h`.
2. **Calls.** The pointers stay resolved by today's `kCalls`, through `extern` declarations (the card 080 pattern). Do not reorder `kCalls` or `kHooks`.
3. **Check every offset (the card 061 correction).**
   - Each moved offset must belong to an engine type: `AActor`, `APawn`, `AController`, `APlayerController`, `ACharacter`, `USceneComponent`, `FHitResult`, `UCharacterMovementComponent`, `UWorld`, `ULevel`, `FTransform`, `FActorSpawnParameters`, `UDataTable`, `UAbilitySystemComponent`, `FGameplayAbilitySpec`, `FGameplayAttributeData`, `FGameplayAbilityActorInfo` or `FOnAttributeChangeData`.
   - An offset on an `AD*`, `ST*` or BP class stays. For example, `kCharacterInteractor` is on `ASTPlayerCharacter` and stays. List every offset you keep back in the Report.
4. **Log text.** A moved function keeps its exact text. If that text names a card, the function stays in `hooks.cpp`; list it in the Report.
5. **No behaviour change.** The only changes are the three renames. Do not merge or split a function unless a move needs it to stay under 60 lines.

## Proof

- **Build:** `systemd-run --user --scope -q -p MemoryMax=12G ninja -C build -j4`, with no warnings.
- **fnlen:** run it on `hooks.cpp`, `ue/actor.cpp` and `ue/gas.cpp`. No function may be over 60 lines.
- **Routes:**
  - the base routes `host-morita`, `client-zipline`, `c47-gates` and `c75-solo` (`DEPLOY=1` on the first run);
  - plus `c65-range` and `c74-grenades` (component lookup, spawns, attributes).
- **Log check:** run `c75-solo` and `c74-grenades` on the card 081 DLL before you change anything. Then diff each against the new DLL's run of the same route, with the card 077 normaliser and sorted unique line texts. State a reason for each difference.
- `logsum` gives ERROR/FATAL 0, rc 0, and there is no new dump.

## Done when

- `ue/actor.*` and `ue/gas.*` exist, and `ue/core.h` includes both headers.
- `grep -n 'c25_dist\|kC25MaxLevels\|kC25MaxActors\|c28_find_component' mod/src` finds nothing.
- `grep -n 'c[0-9][0-9]\|card' mod/src/ue/actor.* mod/src/ue/gas.*` finds nothing.
- Every route passes, the log check is explained, and the Report gives the `hooks.cpp` line counts before and after.
- The Report ends with "Report complete."

## Report

### What moved

New files `mod/src/ue/actor.{h,cpp}` (namespace `ue::actor`) and `mod/src/ue/gas.{h,cpp}` (namespace `ue::gas`) follow the card 080 pattern. Each header has the typedefs, `extern` pointer declarations, `inline constexpr` offsets and helper declarations. Each .cpp defines the pointers as `nullptr` and the helpers. `ue/core.h` includes both headers, and `mod/CMakeLists.txt` lists both .cpp files after `ue/reflect.cpp`. `hooks.cpp` includes `ue/actor.h` and `ue/gas.h` next to `ue/reflect.h`, and has `using namespace ue::actor;` and `using namespace ue::gas;` right after `using namespace ue::reflect;`. No header has a `using namespace`. `kCalls` and `kHooks` are unchanged: they still name `&g_x`, which now resolves through the `extern`s.

- **`ue/actor`:**
  - **Pointers:** `g_actor_get_world`, `g_get_attach_parent_actor`, `g_world_is_paused`, `g_set_actor_location`, `g_set_hidden_in_game`, `g_life_span`, `g_add_component_by_class`, `g_finish_add_component`, `g_query_params_ctor`, `g_line_trace_by_profile`, `g_spawn_params_ctor`, `g_world_spawn_actor`, `g_spawn_ai_from_class`, `g_spawn_niagara`, `g_get_data_table_subsystem`.
  - **Typedefs:** one per pointer, plus `FindComponentFn`.
  - **Structs:** `Vec3`, `ViewPoint`.
  - **Offsets:** every offset in the card's list, plus `kTeleportPhysics`, `kRowMapElement`, the four `kSpawn*` FActorSpawnParameters names, and `kMaxLevels` / `kMaxLevelActors`.
  - **Helpers:** `kRadToDeg`, `read_vec3`, `vec_dist`, `quat_pitch_yaw`, `component_pitch_yaw`, `actor_location`, `find_component`.
- **`ue/gas`:**
  - `g_get_asc`, `g_find_spec_from_class`, `g_set_numeric_attribute_base` and their typedefs.
  - `GameplayAttributeRaw` with its static_assert. It uses `ue::reflect::FStringRaw`.
  - The 15 listed offsets.
  - `avatar_of`.
- **`ue/reflect.h`:** `kFunctionParmsSize` (map correction 3).

The moves were done by a script, `c82split.py` in the session scratchpad. It matched each listed name's definition (one line; a typedef up to its `;`; a function or struct up to its closing brace) and wrote out the removed lines, and that output was checked by hand. Every listed name was found, and none was found twice except `GetSubsystemFn`.

### Decisions and deviations

1. **`c25_dist` becomes `vec_dist`: a merge the rename forced.**
   - `hooks.cpp` already had a global `vec_dist(const Vec3&, const Vec3&)` (old line 1665, next to `vec_len`). It does the same sum of squares as `c25_dist`, in the same order, through `dx`/`dy`/`dz` locals.
   - Renaming `c25_dist` alongside it would have defined two functions with that name, so one `vec_dist` now lives in `ue::actor` and `c25_dist` is deleted. Every caller of either now calls `ue::actor::vec_dist`.
   - `vec_len` is not on the card, so it stays in `hooks.cpp`.
2. **`GetSubsystemFn`:**
   - Both aliases are gone from `hooks.cpp`: the `local_player` one at old line 581 and the `engine` one at old line 5814.
   - The single alias is now in `ue/actor.h`, with its parameter named `outer` and a comment that the owner is a local player or the engine. The type is unchanged.
   - `g_get_enhanced_input` stays in `hooks.cpp` and gets the alias through the `using namespace`.
3. **`ConstBoolFn` moved with `g_world_is_paused`.** `g_actor_tick_enabled` (`AActor::IsActorTickEnabled`) uses the same alias and stays in `hooks.cpp`; it reads the alias through the `using namespace`.
4. **Forward declaration:** `struct Vec3;` (old line 288, used by the `c76_fx` prototype) is removed. Left in place, it would have declared a second `ubw::hooks::Vec3` and made the name ambiguous; `Vec3` now comes from the include.
5. **Comments:**
   - `kCharacterMovement`'s comment began "Card 011: was 0x328"; it now reads "Was read as 0x328", and the rest is unchanged.
   - The section header `// Card 011, CMC fields (see the card 011 report for the listings):` is deleted from `hooks.cpp`. Every line under it moved, so it would have headed nothing.
   - The "Card 024 step 3" section comment above `ViewPoint` stays in `hooks.cpp`, because it describes the code that follows it.
   - New one-line comments are added for:
     - `kActorOwner`, from the block comment next to it ("AActor::Owner +0x158 (AActor::GetNetConnection)");
     - `kAttrData*`, `kCpfNet`, `kAttributeChange*` and `kAbilityActorInfo`, from the existing block comments in `hooks.cpp`;
     - `kActorInfoAvatar`, from the "UGameplayAbility::CurrentActorInfo +0x328; FGameplayAbilityActorInfo::AvatarActor (weak) +0x10" note.
6. **Offsets on engine types outside the card's type list.** These were moved because the card names them:
   - `kAbilityActorInfo`: `UGameplayAbility::CurrentActorInfo`, an engine GAS class;
   - `kCpfNet`: the `CPF_Net` bit of `FProperty::PropertyFlags`;
   - `kMoveUpdatedComponent` and `kMoveVelocity`: `UMovementComponent`, the engine base class of UCharacterMovementComponent.
7. **No moved function writes a log line**, so none stayed back under the log-text rule.

### Kept back in `hooks.cpp`

- **`kPauseRequesters` (named in the card's scope):** it is not on an engine type. Its comment places it on USTGameInstance's pause subobject (the game instance is GI+0x428 from it), so it stays next to `kPauseRequestersHard`.
- **Not named by the card, so not moved. These are candidates for the senior to decide on:**
  - `kHitBlocking` and `kHitStartPenetrating` (bits of `kHitFlags`);
  - `kHitSize`, `kHitTime`, `kHitItem` and `kHitMyItem` (FHitResult);
  - `kSpecFlags` (FGameplayAbilitySpec; its comment says "inferred");
  - `kMoveWalking` and `kMoveFlying`: these are EMovementMode values, not offsets, so "the `kMove*` offsets" was not read as covering them;
  - `kVtSetMovementMode`;
  - `kBodyCollisionEnabled`.
- **Stay by rule:**
  - `kSpawnResult*` (FSTSpawnResult);
  - `kActorRemoteRole` and `kActorLocalRole` (net);
  - `kControllerIgnoreMove` and `kControllerIgnoreLook` (input);
  - `kPc*` names other than `kPcCursorFlags`;
  - the typedefs from map correction 1.

### Proof

- **Build:** `systemd-run --user --scope -q -p MemoryMax=12G ninja -C build -j4` is clean, with no warnings. The DLL is e15b3c5e2320ab69.
- **fnlen at a 60-line limit:** `hooks.cpp` has 1429 functions with 0 over 60 (1437 before; the 8 moved or merged are `read_vec3`, `vec_dist`, `c25_dist`, `quat_pitch_yaw`, `component_pitch_yaw`, `actor_location`, `c28_find_component` and `avatar_of`). `ue/actor.cpp` has 6 functions, 0 over 60. `ue/gas.cpp` has 1 function, 0 over 60.
- **`hooks.cpp` line count:** 28135 before, 27979 after.
- **Greps:** `grep -rn 'c25_dist\|kC25MaxLevels\|kC25MaxActors\|c28_find_component' mod/src` finds nothing. `grep -n 'c[0-9][0-9]\|card' mod/src/ue/actor.* mod/src/ue/gas.*` also finds nothing, and the case-insensitive grep for "card" finds nothing too.
- **Routes:** all run windowed with audio to the null sink. All end with rc 0, ERROR/FATAL 0, no new crash dumps, and drive rc 0.

| Run | Route | Time | Host lines / WARN | Client lines / WARN |
|---|---|---|---|---|
| c82c | host-morita (DEPLOY=1) | 103 s | 1380 / 0 | 1177 / 0 |
| c82d | client-zipline | 122 s | 1771 / 0 | 1721 / 0 |
| c82e | c47-gates | 192 s | 2513 / 0 | 2562 / 1 (same as c80e) |
| c82f | c75-solo | 78 s | 1225 / 0 | solo |
| c82g | c65-range | 181 s | 2127 / 0 | 2263 / 0 |
| c82h | c74-grenades | 183 s | 1849 / 0 | 2056 / 0 |

### Log check

Baselines were run on the 081 DLL (8b89f8abeb1b7004) before any change: c82a was c75-solo (81 s, host 1225 lines) and c82b was c74-grenades (178 s, host 1835 lines, client 2058 lines). Both passed with ERROR/FATAL 0 and no dumps. Each log was put through `c77norm.sh` and `sort -u`, then compared with its new-DLL run of the same route. Every differing line text falls into one of the classes below.

- **c75-solo, host (c82a vs c82f): 21 lines only in the old run, 19 only in the new.**
  - `card038` HandleDeathEvent `(call N)`: the order in which bugs and troopers die differs between runs.
  - `card039` gib position and cell, `card042` gore part rolls, `card075` mech and pad distances and piloted-mech Z, and the `look:` distance (373 vs 372 uu): positions and random rolls.
  - `cmd: ... shot c82a-*` vs `c82f-*`: the run tag in the screenshot name.
  - `movieskip ... seen for 6 polls` vs `5 polls`: timing.
- **c74-grenades, host (c82b vs c82h): 48 lines only in the old run, 69 only in the new.**
  - `card028` animation key changes: the `speed` values (the sprint/walk speed at each change), and how many changes there were.
  - `goto:` distances; both runs reach the end, and each drive log has one "scenario passed".
  - `card026` ping/pong 27: the run lasted long enough for one more ping.
  - `card022` MulticastInlineDelegate heap bytes: allocator addresses.
  - `card022` Morita `VariantID` and `ForcedSpecificVariantIndex` 1 vs 0: the game picks the trooper variant per run, and earlier logs show both values.
  - `dropship-watch` lines: positions, and from t=14s the raw `PrimaryActorTick+8` bytes read `00 00 9E 08 BA 27 AA 3B` instead of `00 00 9E 04 00 00 00 00`. Those bytes are the dropship's own tick-function memory, read through an offset that did not move. The same `9E 08` pattern appears in earlier DLLs' host logs (c63a, c63d, c63e, c64a–e, c64l, c64n), so it is game state that depends on timing, not a change made by this card.
- **c74-grenades, client:** after the same noise is filtered out, only `rot[N]` per-row counters and the `pawn-collision` tick number are left; both are timing.

No line text appears or disappears for any other reason.

Report complete.

## Senior review (2026-11-05 15:24): accepted

- **Runs.** I checked c82a-c82h myself:
  - Every drive ends with `scenario passed`. logsum gives ERROR/FATAL 0 for every log. The only WARN is the pre-existing c47-gates client WARN 1 (c82e).
  - The newest crash dump is still the 13:37 c79a one.
  - The deployed DLL matches `build/dwmapi.dll` (e15b3c5e2320ab69).
- **Code.**
  - fnlen gives `hooks.cpp` 1429/0, `ue/actor.cpp` 6/0 and `ue/gas.cpp` 1/0. `hooks.cpp` is 27979 lines.
  - Both done-when greps are empty. The case-insensitive card grep is empty too. The only `kC25Max` left is `kC25MaxNpcs`, which is card code and correct.
  - `ue/core.h` includes both headers, and CMake lists both files.
- **Deviations accepted:**
  - the `vec_dist` merge: the bodies are the same;
  - the single `GetSubsystemFn`;
  - `ConstBoolFn` used from `ue::actor`;
  - the removed `struct Vec3;` forward declaration;
  - the comment rewording.
- **`kPauseRequesters` kept back: correct.** It is on the USTGameInstance pause subobject. My card was wrong to list it.
- **The log check is explained.** Every difference is a position, a random roll, a timing or a run tag. The `dropship-watch` tick bytes are a known state that depends on timing, and the c63/c64 logs show it too.
- **The candidates the Report listed go to card 083**, under "From card 082":
  - `kHitBlocking`, `kHitStartPenetrating`, `kHitSize`, `kHitTime`, `kHitItem`, `kHitMyItem`, `kMoveWalking`, `kMoveFlying`, `kVtSetMovementMode` and `kBodyCollisionEnabled` go to `ue/actor`;
  - `kSpecFlags` goes to `ue/gas`.
