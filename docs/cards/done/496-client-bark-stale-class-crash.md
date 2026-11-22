# 496: Client crash in UADBarkSubsystem: stale bark class key

Status: finished (in done/; outcome at the end of the card or in CARDLOG). Status as first written: open (written 2026-11-18 17:43, senior trace). This is a crash, so it is fixed at once: batch it into b243, or into b244
right after b243.

## Finding (c247b242b, b242 `2d310c1ec05b87fe`, client crash at 856 s)

- The client hit EXCEPTION_ACCESS_VIOLATION at exe+0x4C59B53, reading 0xFFFFFFFFFFFFFFFF. That address is
  TSet::FindIndexByHash+0xb3, called from UADBarkSubsystem::ProcessNextBarkIndex+0xa0 (0x04C5BC20), from RequestBark,
  from PlayBark on BP_AI_Trooper_Morita_C (OnAttackTargetChanged), from SelectPrimaryTarget on the client.
- ProcessNextBarkIndex (disassembly) hashes `event->ClassPrivate` (rdx+0x10; the key is the bark event class) and
  looks it up in the map at subsystem+0x70, which has 0x20-byte elements.
- FindIndexByHash runs IsChildOf on the stored key (rdi=0x906F9C40) and reads its +0x30 (the struct base chain).
  That read holds garbage (0xBFEC273FA58F148D), so the stored key is a freed class.
- **The class unloaded on the client:**
  - At 683090663 the client relayed BP_BarkType_sawbughopper_C.
  - At 683764795 the row `WARN card451: client ... bark row dropped (parsed 1, class 0, ...)` shows that find_class
    no longer found BP_BarkType_sawbughopper_C. It was gone.
  - The crash came 73 s later.
  - The map is not a UPROPERTY (the class has only CurrentlyTestedBark and TestingBarkType as properties), so GC
    neither keeps its keys alive nor clears them.
  - On the host, the server-only AI (controllers, BT tasks) holds the bark types. On the client nothing does, so a
    class can unload once its last referrer goes. When the class is loaded again, the map keeps the old key.

## Fix

Keep each bark event class alive once it enters the bark map, on both sides.

- In `detour_c451_request_bark` (enemies.cpp; installed on both sides, a pass-through on the client), before the
  original runs, root `*bark` (the event class) the first time it is seen.
- Rooting sets the RootSet internal flag on its FUObjectItem (reflect.cpp object_item, flags at +0x8). Confirm the
  bit from UObjectBase::AddToRoot in this build before using it, and write the flag atomically.
- Rooted classes go in a bounded table of 128. A full table logs a WARN and does not root more.
- One row per new class: `card496: <side> t=<ms> rooted bark class <name> (n of 128)`.
- Barks are fixed assets (a few dozen types), so the memory cost is trivial.

## Proof (written before the run)

- In the next long C5M2 or C4M1 soak, the client's card496 rows grow to a stable count and the client shows 0 bark
  EXCEPTION.
- The card451 `class 0` drop rows for a class seen earlier on the client go to 0.

Not proven by one run: the crash is rare (one in about 856 s of trooper combat). The proof is the class-gone row going
to 0, not the absence of a crash.

Reopen when: any client or host crash under ProcessNextBarkIndex or FindIndexByHash, or a card451 `class 0` row for a
class seen earlier in the same map.

## Build (b244/b245, 2026-11-18 18:05)

- RootSet is bit 0x40000000, from UObjectBase::AddObject 0x1569C40 (`btsl $0x1e` at 0x1569C76). It is set with an
  atomic fetch_or on the flags at +0x8, and skipped when the item is Unreachable or Garbage.
- c448b245d: card496 rows host 1 and client 1, and card451 `class 0` rows 0 on both sides.
- The proof still waits on a long c247 run.

## c247b245a result and the real cause (2026-11-18 18:26, senior)

- FAIL: client crash at 586 s, `0xC0000005 at exe+0x4C59B53` (FindIndexByHash), the same site.
- The client rooted `BP_BarkType_sawbughopper_C` twice, at 685714345 (5 of 128) and at 685916886 (10 of 128). There
  was no map change between the two rows. The table dedupes by pointer, so the first rooted class was freed and a new
  one loaded. The bit did not keep it.
- **Cause:** this build is UE 5.5 or later. `FUObjectArray::AllocateUObjectIndex` takes an `FRemoteObjectId`, and
  `FUObjectItem::SetRootFlags` (RVA 0x0149F8E0) exists. SetRootFlags takes the object array lock (call at 0x149F8F8).
  When the item has none of the flags 0x6E500000 and GC is not running, it adds the item to the root list at
  0x148FAB6A0. It then sets the flag with a cmpxchg loop at 0x149FB40. GC marks roots from that list, so a flag set
  with our bare fetch_or is not a root, and the class is still collected.
- **Fix:** `object_add_root` (mod/src/ue/reflect.cpp:289) calls the engine's `FUObjectItem::SetRootFlags(item,
  0x40000000)` and drops the fetch_or. Add the RVA to offsets. Keep the Unreachable/Garbage skip. It runs on the game
  thread, outside GC. The b244/b245 builds set the bit by hand, which SetRootFlags reads as "already rooted", but no
  item in a new process carries it, so no clear step is needed. `object_add_root` has no other caller.
- Diag: on each rooted row, log the item's flags word (+0x8) after the call, so the RootSet bit shows.
- Proof: in the next long c247, each bark class has exactly one card496 rooted row (no second row for the same name),
  and there are 0 card451 `class 0` rows. The crash rate is too low for one run to prove its absence.
- Not chosen: refusing NPC RequestBark on the client. The bark speakers are trooper NPCs (`BP_AI_Trooper_Morita_C`),
  and card 136 relays no human voice sound (c247b245a: 0 non-bug voice assets in 38042 card136 rows). Refusing would
  drop the trooper voices on the client.

## Closed (2026-11-18 18:46)

Accepted on b246 (object_add_root calls FUObjectItem::SetRootFlags, which adds the item to the GC root list; the bare
RootSet bit of b244/b245 did not). Run c247b246a: 953 s, rc 0, 0 FATAL/ERROR/EXCEPTION on both sides. card496 rooted
rows: host 13, client 9, no name rooted twice, item flags 0x40004000 / 0x40008000. 0 card451 `class 0` rows, 0
not-rooted WARN.

Not done: one run cannot prove the crash is gone (it was rare before the fix).

Reopen when: any crash under UADBarkSubsystem::ProcessNextBarkIndex or FindIndexByHash, or a card496 rooted row
repeats a name.
