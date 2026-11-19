# Card 080: Split S3: reflection to `ue/reflect.*`

Status: sent 2026-11-05 14:07 to junior ultimate-bug-war-coop-bb, after card 079 was accepted. Drafted 2026-11-05 (senior). This is S3 in `docs/split-map.md` ("Move order").

Read `docs/split-map.md` ("Proposed files" row `ue/reflect.cpp`, every "Section map" row that cuts names to `ue/reflect.cpp`, "Engine typedefs and constants left in a block", "Senior correction", "Renames", "CMake"), the card 077 and 078 Reports and Senior reviews, and `mod/src/ue/core.h`. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

Every later move card (actor, input, gas, widget, net, chan) reads objects through FName, `StaticFindObject`, `ProcessEvent` and the property offsets. Reflection is the next leaf after log and config, so it moves next.

## Scope

Move every name that the section map cuts to `ue/reflect.cpp`, and nothing else. That covers:

- the `fstring-reading` block (108-135);
- `ObjectName`, `fname_text`, `object_name`, `class_name`, `read_ptr`, `is_a`, `read_i32`, `read_f32`, `read_u64`, `array_num`, `object_index`;
- the `UObject`, `UStruct`, `FField`, `FProperty`, `TArray` and `TMap` offsets the rows list (`kObjectClass`, `kObjectName`, `kObjectOuter`, `kObjectIndex`, `kNameChars`, `kStructSuper`, `kStructChildProperties`, `kField*`, `kProperty*`, `kArray*`, `kMap*`, `kBool*`, `kClassDefaultObject`, `kScriptDelegate*`, `kFrameObject`, `kVtProcessEvent`, `kFNameAdd`);
- `SparseView`, `sparse_view`, `sparse_allocated`;
- the engine slot pointers the rows list, with their typedefs: `g_name_entry`, `g_fname_ctor`, `g_static_find_object`, `g_find_first_object`, `g_static_load_object`, `g_find_function`, `g_find_function_checked`, `g_object_ptr_is_a`, `g_uclass_class`, `g_weak_get`, `g_weak_assign`, `g_memory_malloc`, `g_memory_free`, and `g_memory_realloc` if it sits with them;
- the card 027 property walker;
- the three renames from "Renames": `c24_fname` to `fname_cached`; the `c27_*` walker names to `prop_*`, `PropKind`, `PropKindRow`, `kPropKinds`, `kPropMaxFields` and `kPropMaxSupers`; `c64_find_class` to `find_class`.

Use the map's rows as the list. If a listed name is not in hooks.cpp where the row says, or is used in a way the map does not show, say so in the Report.

## Rules

1. **Files and namespace.** Create `ue/reflect.h` and `ue/reflect.cpp` in namespace `ue::reflect`, and add `#include "ue/reflect.h"` to `ue/core.h`. `hooks.cpp` gets one `using namespace ue::reflect;` next to the card 077 using-declarations, so its call sites do not change. Only the renamed names change at their call sites. No `using namespace` goes in a header.
2. **A typedef goes with its pointer.** The map puts some typedefs (`FindFirstObjectFn`, `StaticLoadObjectFn`, `StaticFindObjectFn`, `WeakGetFn`, `WeakAssignFn`, `MemoryMallocFn`, `MemoryFreeFn`, `ProcessEventFn`, `FNameCtorFn`, `GetNameEntryFn`) in `ue/actor.cpp`, but their pointers in `ue/reflect.cpp`. That is a map error. Each typedef goes in `ue/reflect.h` with its pointer.
3. **The pointers stay resolved by today's call table.** `kCalls` stays in `hooks.cpp` until S5. It takes the address of each moved pointer through `reflect.h`, which declares the pointers `extern`. `reflect.cpp` defines each one once, as `nullptr`. Do not change the order of `kCalls` or `kHooks`.
4. **Check every moved offset (the card 061 correction).** Each offset you move must be an engine type's offset (`UObject`, `UStruct`, `UClass`, `FField`, `FProperty`, `TArray`, `TMap`, `FScriptDelegate`, `FFrame`, `FName`). An offset read only on a game class (`AD*`, `ST*`, BP classes) stays in `hooks.cpp`. List every offset you keep back, and why, in the Report.
5. **Log text.**
   - A moved function that logs keeps its exact text.
   - If that text names a card (for example a `card027:` prefix in the walker), keep it in this card and list the line in the Report. A later card passes the prefix in from the `ubw` caller.
   - Do not change any `wait log` pattern in `tools/scenarios`.
6. **No behaviour change.** Only the renames above. Do not reorder the hooks, and do not merge or split any function, except where a move needs it to stay under 60 lines.

## Proof

- **Build:** `systemd-run --user --scope -q -p MemoryMax=12G ninja -C build -j4`, with no warnings.
- **fnlen:** run it on `hooks.cpp` and on `ue/reflect.cpp`. No function may be over 60 lines.
- **Routes:**
  - the base routes `host-morita`, `client-zipline`, `c47-gates` and `c75-solo` (`DEPLOY=1` on the first run);
  - plus `c66-connected` and `c56-solo` (objective and pickup names resolve through FName).
- **Log check:** run `c75-solo` once on the card 078 DLL before you change anything. Then diff its host log against the new DLL's `c75-solo` host log, with the card 077 normaliser and sorted unique line texts. Any difference must have a stated reason.
- **Walker check:** with `UBWCOOP_DUMP_SPRITE_ANIMS=1` on `host-morita`, the card 027 dump files are byte-identical before and after. Run it before and after, and diff the `card027-*.txt` files next to the exe. Delete the dump files after the check, because they sit in the game directory.
- `logsum` gives ERROR/FATAL 0, rc 0, and there is no new dump.

## Done when

- `ue/reflect.h` and `ue/reflect.cpp` exist, and `ue/core.h` includes `reflect.h`.
- `grep -n 'c24_fname\|c27_\|C27\|c64_find_class' mod/src` finds nothing in `ue/` or in `hooks.cpp`.
- `grep -n 'c[0-9][0-9]' mod/src/ue/reflect.*` finds only the log texts listed in the Report.
- Every route passes, the log check and the walker check are clean, and the `hooks.cpp` line counts before and after are in the Report.

## Report

### What changed

- New `mod/src/ue/reflect.h` (161 lines) and `mod/src/ue/reflect.cpp` (237 lines), namespace `ue::reflect`. The header declares the 14 engine pointers `extern`; `reflect.cpp` defines each once as `nullptr`. `kCalls` and `kHooks` are untouched, and take the pointers' addresses through the header.
- `ue/core.h` includes `ue/reflect.h`. No file includes `ue/core.h` yet, so `hooks.cpp` also includes `ue/reflect.h` directly, next to its `ue/log.h` include (the same pattern card 077 used for `ue/log.h`).
- `hooks.cpp` has `using namespace ue::reflect;` directly after `using ue::log::note_skip;`. There is no `using namespace` in any header.
- `mod/CMakeLists.txt`: `src/ue/reflect.cpp` added to the `ubwcoop` sources after `src/ue/log.cpp`.
- Renames: `c24_fname` to `fname_cached` (23 sites), `c64_find_class` to `find_class` (6), and the walker names `c27_kind`, `c27_kind_is`, `c27_is_leaf`, `c27_find_prop`, `c27_bool_ok`, `c27_name`, `c27_object_field`, `C27Kind`, `C27KindRow`, `kC27Kinds`, `kC27MaxFields`, `kC27MaxSupers` to `prop_kind`, `prop_kind_is`, `prop_is_leaf`, `prop_find_prop`, `prop_bool_ok`, `prop_name`, `prop_object_field`, `PropKind`, `PropKindRow`, `kPropKinds`, `kPropMaxFields`, `kPropMaxSupers`. The rename is mechanical (`c27_` to `prop_`), so the map's `prop_*` gives `prop_find_prop`.
- `PropKindRow` and `kPropKinds` sit in an anonymous namespace in `reflect.cpp`, because only `prop_kind` reads them.
- No function was merged, split or changed. Comments moved with their code.

**`hooks.cpp` line count: 28336 before, 28044 after** (294 lines removed, plus the include and the using-directive).

### Moved, by kind

- **Functions:** `log_fstring`, `read_ptr`, `read_i32`, `read_f32`, `read_u64`, `array_num`, `sparse_view`, `sparse_allocated`, `fname_text`, `object_name`, `class_name`, `fname_cached`, `object_index`, `is_a`, `find_class`, and the seven `prop_*` walker functions.
- **Types:** `FStringRaw`, `SparseView`, `ObjectName`, `PropKind`, `PropKindRow`.
- **Pointers, each with its typedef (rule 2):**

  | Pointer | Typedef |
  |---|---|
  | `g_uclass_class` | `StaticClassFn` |
  | `g_object_ptr_is_a` | `ObjectPtrIsAFn` |
  | `g_weak_get` | `WeakGetFn` |
  | `g_weak_assign` | `WeakAssignFn` |
  | `g_name_entry` | `GetNameEntryFn` |
  | `g_fname_ctor` | `FNameCtorFn` |
  | `g_find_function_checked`, `g_find_function` | `FindFunctionFn` |
  | `g_static_find_object` | `StaticFindObjectFn` |
  | `g_find_first_object` | `FindFirstObjectFn` |
  | `g_static_load_object` | `StaticLoadObjectFn` |
  | `g_memory_malloc` | `MemoryMallocFn` |
  | `g_memory_realloc` | `MemoryReallocFn` (it sits with `g_memory_malloc`) |
  | `g_memory_free` | `MemoryFreeFn` |

- **Typedefs with no moved pointer:** `ProcessEventFn` and `ExecThunkFn`. Both were defined twice in `hooks.cpp` (`ProcessEventFn` at old lines 6538 and 13704, `ExecThunkFn` at 1850 and 4505), and each is now defined once in `reflect.h`.
- **Constants:** `kMaxLoggedFStringNum`, `kNameChars`, `kPropMaxSupers`, `kPropMaxFields`.
- **Offsets, every one an engine type's (rule 4):**

  | Engine type | Offsets |
  |---|---|
  | `UObjectBase` | `kObjectIndex`, `kObjectClass`, `kObjectName`, `kObjectOuter` |
  | `UObject` vtable | `kVtProcessEvent` |
  | `UStruct` | `kStructSuper`, `kStructChildProperties` |
  | `UClass` | `kClassDefaultObject` |
  | `FField` | `kFieldClassPrivate`, `kFieldNext`, `kFieldName` |
  | `FProperty` | `kPropertyElementSize`, `kPropertyFlags`, `kPropertyOffset` |
  | `FStructProperty` | `kStructPropStruct` |
  | `FArrayProperty` | `kArrayPropInner` |
  | `FMapProperty` | `kMapPropValue`, `kMapValueOffset`, `kMapSparseSize` |
  | `FBoolProperty` | `kBoolFieldSize`, `kBoolByteOffset`, `kBoolFieldMask` |
  | `TArray` | `kArrayData`, `kArrayNum`, `kArrayMax` |
  | `FScriptDelegate` | `kScriptDelegateBytes`, `kScriptDelegateName` |
  | `FFrame` | `kFrameObject` |
  | `EFindName` | `kFNameAdd` |

  No offset that the rows list was held back as a game-class offset.

### Kept in `hooks.cpp`

- **Engine offsets that no section-map row cuts to `ue/reflect.cpp`.** They stay for a later card: `kFieldOwner`, `kPropertyArrayDim`, `kStructPropertiesSize`, `kStructMinAlignment`, `kMapPropKey`, `kMapHashNext`, `kMapHashIndex`, `kMapSize`, `kMapNum`, `kMapNumFree`, the `kSet*` and `kSparse*` offsets, `kObjectPropClass`, `kPropertyRepNotify`, `kStructChildren`, `kFramePrevious` and `kFNameFind`. All of them are engine offsets, so S3 could take them if you want.
- **`read_vec3`.** It needs `Vec3`, which moves to `ue/actor` in S4.
- **`object_name_into` (card 074).** It is not in the map rows. It now reads `g_name_entry` and `kObjectName` through the using-directive.
- **The rest of the card 027 code.** The c027 map row sends it to `ubw/audit_npc.cpp`, and only the walker comes here. That code is the `c27_*`, `C27*` and `kC27*` names in the next section.

### Card conflict: the first done-when grep

`grep -n 'c24_fname\|c27_\|C27\|c64_find_class' mod/src` finds nothing in `ue/`. In `hooks.cpp` it finds:

- no `c24_fname` and no `c64_find_class`;
- 210 lines with `c27_`, `C27` or `kC27`, all of them the card 027 dump code. Examples: `C27Walk`, `C27Out`, `C27Sparse`, `C27Npc`, `C27State`, `c27_write`, `c27_dump`, `c27_owned`, `c27_struct_ok`, `c27_map_ok`, `c27_set_ok`, `c27_sparse`, `c27_object_prop_ok`, `kC27MaxElements`, `kC27MoritaClass`.

The map (c027 row, and the `ubw/audit_npc.cpp` row: "c025, c027 (the walker itself goes to `ue/reflect.cpp`)") sends these names to `ubw`. The card's scope says "and nothing else", so they stay. The grep can only come back empty in `hooks.cpp` once the `ubw/audit_npc.cpp` card is done.

### Log text

- `grep -n 'c[0-9][0-9]' mod/src/ue/reflect.*` finds nothing.
- None of the moved functions logs a card name. The two that log keep their exact text:
  - `log_fstring`: `%s: <null FString*>`, `%s: <unreadable FString data=%p num=%d>`, `%s: "%.*ls"`;
  - `sparse_view`: `ERROR client-input: implausible sparse array at %p (num=%d bits=%d cap=%d)`.
- All `card027:` lines stay in the `ubw` dump code in `hooks.cpp`.
- No `wait log` pattern was changed.

### Proof

- **Build:** `systemd-run --user --scope -q -p MemoryMax=12G ninja -C build -j4` built with no warnings or errors (/W4 /WX). The DLL is `1c6c01fe4b2c30db`.
- **fnlen:** `hooks.cpp` has 1432 functions, 0 over 60 lines. `ue/reflect.cpp` has 22 functions, 0 over 60 lines.
- **Name collisions:** none of the moved names is still declared in `hooks.cpp`, in its anonymous namespace or in `ubw::hooks`. A declaration there would silently hide the `ue::reflect` one.

| Run | DLL | Route | Result |
|---|---|---|---|
| c80a | `e8c631ba` (card 079) | `c75-solo`, SOLO, baseline | 76 s, rc 0, ERROR/FATAL 0, WARN 0, no dump |
| c80b | `e8c631ba` | `host-morita`, `DUMP_SPRITE_ANIMS=1`, walker baseline | rc 0/0, ERROR/FATAL 0, WARN 0/0, no dump. Dumps saved as `c27before` |
| c80c | `1c6c01fe` | `host-morita`, `DEPLOY=1` | 103 s, rc 0/0, ERROR/FATAL 0, WARN 0/0, no dump |
| c80d | `1c6c01fe` | `client-zipline` | 128 s, rc 0/0, ERROR/FATAL 0, WARN 0/0, no dump |
| c80e | `1c6c01fe` | `c47-gates` (`C43_TEST=1` on both sides) | 181 s, rc 0/0, ERROR/FATAL 0, no dump. Client WARN 1 is the existing `card044: client no CompleteDeath 8000 ms after StartDeath` on the card 043 test path; it is also in c77e and c78d |
| c80f | `1c6c01fe` | `c75-solo`, SOLO | 76 s, rc 0, ERROR/FATAL 0, WARN 0, no dump |
| c80g | `1c6c01fe` | `c66-connected` (`C66_CAP=64`) | 185 s, rc 0/0, ERROR/FATAL 0, WARN 0/0, no dump |
| c80h | `1c6c01fe` | `c56-solo`, SOLO | 213 s, rc 0, ERROR/FATAL 0, no dump. WARN 1 is the standard solo `autodropship: 1 PlayerController(s); unpause action in 3000 ms` |
| c80i | `1c6c01fe` | `host-morita`, `DUMP_SPRITE_ANIMS=1` | 105 s, rc 0/0, ERROR/FATAL 0, WARN 0/0, no dump. Dumps differ from `c27before` (see the walker check) |
| c80j | `e8c631ba` | `host-morita`, `DUMP_SPRITE_ANIMS=1`, noise check on the old DLL | 101 s, rc 0/0, ERROR/FATAL 0, WARN 0/0, no dump |
| c80k | `1c6c01fe` | `host-morita`, `DUMP_SPRITE_ANIMS=1`, second run on the new DLL | 102 s, rc 0/0, ERROR/FATAL 0, WARN 0/0, no dump |

The new DLL is the one deployed now.

**Log check: c80f against c80a**, card 077 normaliser, digits replaced by `N`, `sort -u`. That gives 741 unique lines against 737. Every difference has a stated reason:

- **Shot names:** `c80a-*` against `c80f-*`, from the run tag.
- **Which bug died:** c80a had a JumperBug's `HandleDeathEvent` and `OverkillDamageEffectReceived`, a Gib signal, a gore part, and the `further events not logged` diag line. c80f had a WarriorBug's `HandleDeathEvent`. This is combat variance.
- **`UnequipCurrentWeapon(null...)` `callers` chains.**
  - c80a: `0x0 < 0x52556A5 < 0x50DBE0A < 0x50F6B09`.
  - c80f: `0x52556A5 < 0x50DBE0A < 0x50F6B09 < 0x50E69BB`, the same chain with no leading `0x0` frame.
  - `caller_chain` skips a fixed number of frames, so one extra frame inside the mod (which `exe_rva` prints as `0x0`) shifts the whole chain. The card 079 build had that extra frame. The c80f chains are exactly the c77 and c78 runs' chains (3 lines each).
  - This is a code-generation difference in our own stack, not a behaviour change.
- No card log line or engine log line changed text.

**Walker check.**

- The c80i dumps are not byte-identical to `c27before`. The sizes match: 129561 B for Controller, 136341 B for Female.
- The only difference is the spelling of one FName: `Animations[StandUp]` in `c27before` against `Animations[Standup]` in c80i. That is 28 lines in Controller and 70 in Female.
- With that one spelling replaced, the files are byte-identical.
- FNames compare without case, and the name table keeps the spelling it saw first. So the displayed case depends on which asset registered the name first in that run.
- To confirm this is noise, I ran the dump twice more:
  - c80j, on the card 079 DLL `e8c631ba`, gave `Standup`. Its files are byte-identical to c80i (sha `52a934b7` / `b0127f64`, which are also the older `c27old` hashes).
  - c80k, on the new DLL `1c6c01fe`, gave `StandUp`. Its files are byte-identical to `c27before` (sha `12bb9dae` / `01c0cca5`).
- Each DLL produced each spelling, so the walker output is unchanged by the move.
- All four dump sets are saved in the scratchpad: `c27before`, `c80after`, `c80jdump`, `c80kdump`.
- The `card027-*.txt` files were deleted from the game directory by exact name after each run. None is left.

### Done when

- [x] `ue/reflect.h` and `ue/reflect.cpp` exist, and `ue/core.h` includes `ue/reflect.h`.
- [ ] The first grep is empty in `ue/` and has no `c24_fname` or `c64_find_class` in `hooks.cpp`. It still finds the 210 card 027 dump-code lines in `hooks.cpp`; see "Card conflict" above.
- [x] `grep 'c[0-9][0-9]' ue/reflect.*` finds nothing, so there are no card log texts to list.
- [x] Every route passed. The log check differences are explained. The walker check is byte-identical up to an FName spelling that changes from run to run on both DLLs.
- [x] `hooks.cpp` line counts: 28336 before, 28044 after.

Report complete.

## Senior review (2026-11-05 14:40): accepted

- **Runs.** I checked these myself:
  - c80a to c80k all end with `scenario passed`, and logsum gives ERROR/FATAL 0 on every log.
  - The newest dump is still the c79a one (13:37:32).
  - The deployed DLL matches `build/dwmapi.dll` (1c6c01fe4b2c30db).
  - No `card027-*.txt` is left next to the exe.
- **Code.**
  - `ue/reflect.h` is 161 lines and `ue/reflect.cpp` is 237. `core.h` includes `reflect.h`, and CMake lists `src/ue/reflect.cpp`.
  - `grep 'c[0-9][0-9]' ue/reflect.*` is empty, and there are no `c24_fname` or `c64_find_class` sites left.
  - No header has a `using namespace`.
  - fnlen gives `hooks.cpp` 1432 functions and `ue/reflect.cpp` 22, with 0 over 60 in both.
- **Grep conflict accepted. The card was wrong.** The 210 `c27_` / `C27` lines are the card 027 dump code, which the map sends to `ubw/audit_npc.cpp`. That grep comes back empty when that card lands.
- **Log check accepted.** The `callers` chain shift is a frame-count artifact of the card 079 build. The c80f chains match c77 and c78.
- **Walker check accepted.** The `StandUp` / `Standup` spelling depends on which asset registers the FName first in a run. Each DLL produced both spellings (c80j and c80k), so the move changed nothing.
- **Engine offsets left in `hooks.cpp`** (`kFieldOwner`, `kMap*`, `kSet*`, `kSparse*`, `kFramePrevious`, `kFNameFind` and the others in the Report): they stay for now. The card that moves their users takes them. If the user is only reflection, they go to `ue/reflect.h` then.
- **Minor.** `prop_find_prop` is a mechanical rename. It can become `find_prop` when the card 027 dump code moves.
