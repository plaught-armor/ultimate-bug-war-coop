# Card 053: The client's objective list matches the host's after a checkpoint

Status: sent 2026-11-04 (senior, revised: audit step removed, widget path from Addendum 2, FName hash and TMap layout in Addendum 3). Not a playtest blocker.

Read `docs/STATE.md` and the card 049 Report and Senior review. Repo root: `/path/to/ultimate-bug-war-coop`.

## Why

After RESTART CHECKPOINT, card 049's snapshot keeps every client objective. Two visible differences follow:
- The client keeps objectives that the host's restore dropped. In FED_C1M1 this is `Traitor_O1` ("TRAITOR! You are a traitor to the Federation and will be executed!"), added by the level script at a player death.
- The host's restore rebuilds its list in a different order (TacSupport, ShootingRange, Assault). The client keeps the load order.

## Static facts (senior, 2026-11-04)

- There is no remove or clear native for objectives. The PDB publics for `USTLevelObjectiveSubsystem` and `USTObjectiveUtilityLibrary` hold only Add, Complete, Callout, Increment, SetVisibility, the getters, `AddObjectiveObject` 0x0514AF00 and `SetObjectiveMapPostSave` 0x0516E010.
- `SetObjectiveMapPostSave(TMap<FName, USTLevelObjective*>)` is what the host's checkpoint restore calls. It replaces the subsystem's map at +0x60 (stride 0x18, card 049). Its body walks the old map, uses `FWeakObjectPtr::Get` twice, and rebuilds a `TSet`. The host's list widget rebuilds after it, which is where the host's new order comes from.

- Senior disassembly of `SetObjectiveMapPostSave` (2026-11-04), whole body:
  1. For every objective in the old map (+0x60), it removes the subsystem's binding from that objective's multicast delegate at objective +0x38 (array of 16-byte entries, count at +0x40).
  2. It assigns the argument to +0x60 with the TMap copy-assign (0x01C511F0).
  3. For every objective in the new map, it binds `USTLevelObjectiveSubsystem::ObjectiveComplete` to the objective's delegate at +0x38 (an `AddDynamic`: `FindFunction` on the name, then `CompactInvocationList` and an add).
  4. It frees the argument map: `FMemory::Free` on its hash pointer at +0x40 (and zeroes +0x48), then the `TSparseArray` destructor on its element array. The map is passed by value, and in the MSVC x64 ABI the callee destroys it.
  It calls nothing on the widget side and broadcasts nothing. So the host's new order comes from the level reload rebuilding the list widget, not from this call. On the client, step 3 (a widget rebuild) will be needed.
- Consequences for step 2: pass a pointer to a temporary TMap whose element array and hash were allocated with `FMemory` (the engine allocator). Do not free or destroy it after the call; the callee already did. Re-binding objectives the client keeps is safe, because step 1 of the body unbinds them first. `Traitor_O1` is unbound and dropped from the map. Its object is left for the GC.

## Do

The senior static work (Static facts, Addendum 1, Addendum 2) replaces the old audit step. Go straight to the code.

1. **Client map rebuild.** At SnapshotEnd (card 049), if the client's objective set or order differs from the host's snapshot:
   - Build a `TMap<FName, USTLevelObjective*>` in the host's order from the client's existing objective objects. The snapshot names them all; skip and log any the client lacks. Objects the host lacks are left out, so `Traitor_O1` goes.
   - Allocate its element array and hash with the engine allocator (`FMemory::Malloc`), and use the engine's own FName hashing (the same `GetTypeHash(FName)` the game's TMap code uses; find one inlined call in 0x01C511F0 if needed).
   - Call the client's own `SetObjectiveMapPostSave` 0x0516E010 with a pointer to it. Do not free or destroy it afterwards (Static facts: the callee does).
   - If building the TMap natively turns out unsafe, stop and report what you found.
2. **Client widget rebuild.** On the client's `WB_Objective_List_C` (find it with `c23_collect` or the HUD), through `ProcessEvent`:
   - Call `Clear` (no parameters).
   - Then call `UpdateObjectiveListItems` (no parameters). Addendum 2 says why this is expected to re-add from the subsystem map.
   - Dump the widget's `ObjectivesVerticleBox` children in order before and after.
   - If the order still differs, fall back to Addendum 2's second option and report which one worked.
3. **Runs.**
   - `card053x`: two players, FED_C1M1. Make the first player death happen (card 044 route) so `Traitor_O1` is added, then RESTART CHECKPOINT. Take shots of both panels before and after; the item dumps must be identical and in the same order.
   - `card053s`: solo; the snapshot code does not run.

## Done when

- After RESTART CHECKPOINT the client's panel has the same objectives in the same order as the host's, with no `Traitor_O1` on the client.
- rc 0 on all runs, no new dump, the build is clean with `/W4 /WX`, and every function is 60 lines or fewer.

## Addendum 1 (senior static, 2026-11-04): the list widget keeps its own items

Name strings in `WB_Objective_List.uasset` (from the senior's UI extract) show that the widget does not read the subsystem's map when it draws. It keeps its own state:
- A map `ObjectiveListItems` (objective to item widget) and a `VerticalBox` named `ObjectivesVerticleBox` (the game's spelling).
- The events `OnLevelObjectiveAdded`, `OnLevelObjectiveComplete` and `OnLevelObjectiveCalledOut`, which are driven by gameplay events (`WaitGameplayEventToActor`). Also `UpdateObjectiveListItems`, `SetWholeObjectiveListVisibility`, `SubObjective_Added`, `SubObjective_Complete`, `ShowLast` and `Clear`.
- No native objective-added delegate exists on `USTLevelObjectiveSubsystem`.

So the host's new order most likely comes from the HUD being rebuilt on the level reload: each objective is added again through `OnLevelObjectiveAdded`, in restore order. Calling `SetObjectiveMapPostSave` on the client will not change its panel by itself.

For step 3, the expected client fix is:
1. Call the widget's `Clear` event if it empties the box and the map. Otherwise call `ClearChildren` on `ObjectivesVerticleBox` and empty `ObjectiveListItems`.
2. Call `OnLevelObjectiveAdded` for each objective, in the host's order, through `ProcessEvent` with the objective as the parameter.

Addendum 2 corrects this: the event takes a gameplay-event payload, not the objective.

## Addendum 2 (senior static, 2026-11-04): what the widget reads

More name strings in `WB_Objective_List.uasset`:
- `OnLevelObjectiveAdded_Event` takes `Payload` (a `FGameplayEventData` from `WaitGameplayEventToActor`), not the objective. So calling `OnLevelObjectiveAdded` directly needs a built payload. That is the fallback only.
- The widget also holds `LevelObjectivesMap`, `NonAddedObjectives`, `FirstInit` and an `OnInitialized` override, and calls `GetWorldSubsystem`. So on creation it reads the subsystem's objective map and adds every item from it, in map order. That is where the host's new order comes from: the reload creates a new widget, which reads the map that `SetObjectiveMapPostSave` set.
- On the client, the widget outlives the restore. After step 1 replaces the map, `Clear` empties the box and `ObjectiveListItems`, and `UpdateObjectiveListItems` is the likely re-read of `LevelObjectivesMap`.
- Fallback, in order: (a) call `OnInitialized` with `FirstInit` set back to true; (b) build the `FGameplayEventData` payload (which field carries the objective is not known statically; log one host payload first) and call `OnLevelObjectiveAdded` per objective in the host's order.

## Addendum 3 (senior static, 2026-11-04): FName hash and the TMap layout, verified

This answers step 1's "find the engine's FName hashing". Read from `DoesObjectiveExistForID` 0x05153100, which does an inlined `Find` on the subsystem map at +0x60.

**Hash.** `hash = GetTypeHash(ComparisonIndex) + Number`, where `ComparisonIndex` is the FName's low dword (+0) and `Number` the high dword (+4). `GetTypeHash(FNameEntryId)` is 0x01338A90, five instructions. Port it; do not call it:
```cpp
uint32_t fname_hash(uint64_t name) {
    const uint32_t id = static_cast<uint32_t>(name), number = static_cast<uint32_t>(name >> 32);
    const uint32_t offset = id & 0xFFFFu, block = id >> 16;
    return (block << 19) + block + (offset << 16) + offset + (offset >> 4) + number;
}
```
Bucket = `hash & (HashSize - 1)`.

**`TMap<FName, USTLevelObjective*>` layout, 0x50 bytes** (offsets relative to the map; the subsystem's map starts at subsystem +0x60):

| Off | Field | Value to build |
|---|---|---|
| +0x00 | element data pointer (`FMemory::Malloc`) | n elements of 0x18 |
| +0x08 | ArrayNum (int32) | n |
| +0x0C | ArrayMax (int32) | n |
| +0x10 | allocation-flag bits, inline (4 dwords) | low n bits set (n ≤ 128) |
| +0x20 | flag bits, heap pointer | nullptr |
| +0x28 | NumBits | n |
| +0x2C | MaxBits | 128 |
| +0x30 | FirstFreeIndex | -1 |
| +0x34 | NumFreeIndices | 0 |
| +0x38 | hash, inline (one int32) | used only when HashSize is 1 |
| +0x40 | hash, heap pointer (`FMemory::Malloc`) | HashSize int32 buckets, or nullptr when HashSize is 1 |
| +0x48 | HashSize (int32, power of two) | see below |

Empty test in the game: `ArrayNum == NumFreeIndices`. Lookup picks the heap hash pointer, or +0x38 when it is null.

**Element, 0x18 bytes:** +0x00 key (FName, 8 bytes), +0x08 value (`USTLevelObjective*`), +0x10 `HashNextId` (int32, -1 ends the chain), +0x14 `HashIndex` (int32, the bucket).

**Build, in the host's order, i = 0..n-1:**
1. Set HashSize to UE's default: 1 if n < 4, else the next power of two ≥ n/2 + 8. For FED_C1M1's few objectives this is 1 or 16. Fill every bucket with -1.
2. For each element, compute `b = fname_hash(key) & (HashSize-1)`. Set `HashIndex = b`, `HashNextId = bucket[b]`, then `bucket[b] = i`.
3. Allocate with `FMemory_Malloc` (0x011FD810, already in `offsets.generated.h`), alignment 8 for the elements and 4 for the hash. Free nothing afterwards; `SetObjectiveMapPostSave` frees +0x40 and the element array itself (Static facts).
4. Self-check before the call: look up every key through your built bucket and chain, and log a mismatch then abort the rebuild. After the call, `DoesObjectiveExistForID` on each host name must return true. That uses the game's own `Find`, so it proves the hash is right.

Cap n at 128 (the inline flag bits). Log and skip the rebuild above that.

## Report

**Result: done.** After RESTART CHECKPOINT the client's panel shows the host's three objectives in the host's order (TACTICAL SUPPORT TRAINING, SHOOTING RANGE, ASSAULT COURSE) and no "TRAITOR!" entry. The widget fix is neither `Clear` nor `UpdateObjectiveListItems`; see step 2.

### Code (`mod/src/hooks.cpp`, block "Card 053")

**Trigger.** At SnapshotEnd, `c49_replay_snapshot_end` calls `c53_same_order`, which compares the client's map ids in slot order with `g_c49m.snapshot_ids`.
- If they match, it logs "no rebuild" and stops.
- Otherwise it arms `c53_due_ms`: immediately in play, or 6 s later under `UBWCOOP_C49_TEST` so the "before" shot is taken first.
- `c53_step`, which runs from `c49_step`, then runs `c53_rebuild`.

**Step 1: map.**
- `c53_collect` takes the snapshot ids that have a client object, in the host's order.
- `c53_build` and `c53_fill` build the 0x50 `TMap` per Addendum 3, with `FMemory::Malloc` (alignment 8 for the elements, 4 for the heap hash).
- `c53_check` looks up every key through the buckets and chains first. On a mismatch it frees the map and aborts.
- The map is then handed to the original `SetObjectiveMapPostSave` and not freed. Afterwards `DoesObjectiveExistForID` is checked for each id, and the order is checked again.

**Step 2: widget.**
- The card's `Clear` does not exist as a function: `FindFunction(WB_Objective_List, Clear)` returned null in card053x.
- The cause was found in card053x: the client's HUD is re-created on the checkpoint reload, so the panel is a new object (`64E70FD0`, not `62216E50`). It was built from the client's map before the snapshot arrived (old order, plus `Traitor_O1`).
- Fallback (a) alone (`FirstInit` = 1, then `OnInitialized`) re-reads the rebuilt map but appends: the box went from 4 slots to 7, and `ObjectiveListItems` stayed at 15.
- The working sequence (`c53_widget`):
  1. `ProcessEvent(ObjectivesVerticleBox, ClearChildren)`, the `UPanelWidget` function.
  2. `c53_empty_items` returns the panel's `ObjectiveListItems` TMap (+0x370, the same 0x50 layout) to the default-constructed state. It frees the element array and the heap hash, then zeroes the struct. It refuses if the allocation bits are on the heap (more than 128).
  3. Fallback (a).
- The items are dumped before and after, and `c53_items_match` is logged.
- Fallback (b), the `OnLevelObjectiveAdded` payload, was not needed.

### Runs

| Run | What | rc | FATAL/ERROR, WARN | Dumps |
|---|---|---|---|---|
| card053x | 2 players, `TacSupport_SO0` death adds `Traitor_O1`, RESTART CHECKPOINT; first widget attempt (`Clear` + `UpdateObjectiveListItems`, then fallback (a)) | host 0, client 0 | 0; client WARN 1 (`Clear` not found) | none |
| card053x2 | same, with `ClearChildren` + emptied item map + fallback (a) | host 0, client 0 | 0, 0 | none |
| card053s | solo, card 045 RESTART MISSION | host 0 | 0; WARN 2, the existing autodropship lines | none |

That is three runs against the two asked for. card053x showed the widget path did not work, and card053x2 is the fix.

**card053x2 client log** (`logs/card053x2-client.log`):
- `SetObjectiveMapPostSave with 14 objectives in the host's order (was 15), hash size 16`
- `client map after: 14 objectives, DoesObjectiveExistForID true for 14 of 14, order matches`
- Items before (match 0): Assault_O1, ShootingRange_O1, TacSupport_01, Traitor_O1.
- `ClearChildren` was called. `ObjectiveListItems emptied (15 entries)`. `OnInitialized` was called.
- Items after (match 1): TacSupport_01, ShootingRange_O1, Assault_O1.
- Panel watch: `list items 14, box slots 3`. This equals the host's panel after the checkpoint (`list items 14, box slots 3`, host log), and the 1.5 s re-dump is unchanged.
- The host does not dump its items; the shots below show the same three headings in the same order.

**card053s:** no `card053` line at all, and no snapshot or SnapshotEnd. The solo path does not run the code.

### Shots (`logs/card053-shots/`)

- `card053x2-client-before.png`: the client after the checkpoint, before the rebuild. It shows ASSAULT COURSE, SHOOTING RANGE, TACTICAL SUPPORT TRAINING, TRAITOR!
- `card053x2-host-after-checkpoint.png`: the host shows TACTICAL SUPPORT TRAINING, SHOOTING RANGE, ASSAULT COURSE.
- `card053x2-client-after.png`: the client after the rebuild, with the same three headings in the host's order and no TRAITOR!
- `card053x2-host-at-client-after.png`: the host at the same moment, unchanged.

### Checks

- `/W4 /WX` build is clean.
- Card 053 function lengths, all 60 lines or fewer: `c53_fname_hash` 7, `c53_hash_size` 10, `c53_same_order` 18, `c53_collect` 15, `c53_fill` 26, `c53_build` 18, `c53_check` 20, `c53_free` 7, `c53_items_match` 21, `c53_call` 18, `c53_reinit` 10, `c53_empty_items` 17, `c53_widget` 19, `c53_rebuild` 32, `c53_step` 6.
- No new line in the block is over 120 characters.

### Risks

- `OnInitialized` runs a second time on the client's panel. If it binds the gameplay-event waits again, a later objective event could be handled twice on the client, which would add a duplicate item.
  - card053x2 had no objective events after the rebuild, so this is not tested.
  - Watch item for the playtest: after a checkpoint, complete an objective, and check that the client's list does not show it twice.
- The client's panel was rebuilt within the same tick; no flicker was visible in the shots.

## Senior review (2026-11-04): not accepted yet, one run

I checked these myself:
- **Runs.** card053x, card053x2 and card053s all ended with rc 0 and no new dump. card053x2 had no WARN on either side, and its client log shows 14 objectives in the host's order (was 15), hash size 16, `DoesObjectiveExistForID` true for 14 of 14, and the panel at match 1 after the rebuild.
- **Function length.** fnlen gives 1168 functions, 0 over 60 lines.
- **`c53_empty_items`.** The memset of the panel's map is safe. With `NumFreeIndices` 0 the zeroed `FirstFreeIndex` is never read, and `MaxBits` 0 makes the first add grow the inline bits. The game's own adds through `OnInitialized` then refilled it (14 entries).

**Why not accepted: the open risk is worse than the bug this card fixes.** The old fault was one extra entry and a different order after a checkpoint. If `OnInitialized` binds its `WaitGameplayEventToActor` waits a second time, every later objective event on the client is handled twice: duplicate items, or a completion that toggles twice. That would show on every objective after every checkpoint, during the playtest.

**Run card053x3** (STAGGER=5): card053x2's route, then after the rebuild let one objective event reach the client.
- An objective added, completed or called out all count, whichever the test driver reaches cheapest. The tactical support case completes `TacSupport_01` on the host, and card 049 then completes it on the client.
- Dump the client's box slots and `ObjectiveListItems` count before and after that event. The event must change exactly one item, and no item may appear twice.
- If it shows a duplicate, do not try to unbind the waits. Instead, in `c53_widget`, keep the map rebuild and drop the panel rebuild: skip the ClearChildren, the empty and `OnInitialized`. Log it as the safe fallback, and the panel's order stays wrong until a later card.

## Report: card053x3 (2026-11-04)

**Result: pass. The post-rebuild completion changed exactly one item on the client, nothing appeared twice, and the client's panel matched the host's line for line. The full rebuild (ClearChildren, the map empty and `OnInitialized`) stays; the safe fallback was not needed.**

Test driver (`mod/src/hooks.cpp`, test runs only):
- Host: `c53_test_step`. The checkpoint snapshot in phase 2 arms `g_c53_event_ms` (snapshot + `kC53EventMs`, 10 s; past the client's rebuild at snapshot end + 6 s). When it comes due, the host calls `detour_c49_complete` on the test objective's root (`TacSupport_01` for `TacSupport_SO0`), once, so card 049 mirrors it to the client.
- Client: `c53_dump` logs `ObjectiveListItems`, the box slot count, the objective count and the box items. It runs right before the first non-snapshot frame is replayed after the rebuild, and again 1.5 s later (from `c53_step`).

Run: `card053x3` (STAGGER=5, the card053x2 route), ended by done after 241 s. Host rc 0, client rc 0; 0 FATAL/ERROR, 0 WARN on both sides; no crash dumps.

Timeline (client log):

| Tick | Event |
|---|---|
| 586187019 | rebuild: 14 objectives in the host's order; ClearChildren, `ObjectiveListItems` emptied (15 entries), `OnInitialized`; match 1 |
| 586190516 | before the event: ObjectiveListItems 14, box slots 3, objectives 14 |
| 586190518 | replayed `CompleteObjective(TacSupport_01)`: objectives 14 -> 14 |
| 586192017 | 1.5 s after: ObjectiveListItems 14, box slots 3, objectives 14 |
| 586201426 | card045: client test done |

The before and after item dumps differ in one line only: `item 0 TacSupport_01: vis 0/1.00` became `vis 1/1.00` (the root item is collapsed). The counts did not move, so no second item widget was added. The client's after dump is identical to the host's own dump 1.5 s after the same completion (host log 586192118), item for item.

Shots (`logs/card053-shots/`):
- `card053x3-client-before-rebuild.png`: ASSAULT COURSE, SHOOTING RANGE, TACTICAL SUPPORT TRAINING, TRAITOR! (the stale list, as in card053x2).
- `card053x3-client-after-rebuild-and-event.png`, `card053x3-client-after-event.png`: SHOOTING RANGE, ASSAULT COURSE.
- `card053x3-host-after-event.png`: SHOOTING RANGE, ASSAULT COURSE, plus the "Tactical Support Training" completion banner.

The client shot 5 s after the event shows no completion banner; the host shot at 3 s does. The banner may have already faded by 5 s, so this run does not show whether the client plays it. It is a separate question from the double-bind, which this run answers.

Limit: one completion of one root. An objective *added* after the rebuild, or a call-out, was not driven.

## Senior review 2 (2026-11-04): accepted

I checked these myself:
- **Run.** card053x3 ended by its done line after 241 s, with rc 0 on both sides, WARN/ERROR 0 and no new dump.
- **Dumps.** The client's dumps read 14 list items, 3 box slots and 14 objectives both before the replayed `CompleteObjective(TacSupport_01)` and 1.5 s after it, and only item 0 changed. A double-bound wait would have added a second item or toggled the completion back. Neither happened, and the client matches the host's dump item for item.
- **Test gating.** The completion driver is armed only under `g_c49_test` (hooks.cpp ~19519), and `c53_dump` runs only after a rebuild. A playtest launch runs neither.
- **Function length.** fnlen gives 1170 functions, 0 over 60 lines.

**Untested:** an objective added after the checkpoint, and a call-out. Both go through the same rebound panel, so the risk is the same as the completion, which passed. The `docs/PLAYTEST.md` watch item covers them.
