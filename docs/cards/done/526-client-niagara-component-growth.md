# 526: Client NiagaraComponent count grows through a fight (host flat)

Opened 2026-11-19 by senior from the c263L card 252 census (4-player load, 150 bugs, FED_C3M2, b309). Real bug
candidate: a client that keeps more live FX components than the host pays for them every frame, and the gap grows
with play time.

## Evidence (census rows, 30 s apart)

| t (s from first row) | host NiagaraComponent | client NiagaraComponent |
|---|---|---|
| 0 | 1783 | 1177 |
| 30 | 1914 | 2056 |
| 60 | 2031 | 4288 |
| 90 | 2067 | 5756 |
| 120 | 2397 | 6693 |
| 150 | 2371 | 7118 |
| 180 | 2355 | 7639 |

The host levels off near 2400. The client grows by about 1000 per 30 s of fighting and has not levelled off at the
end. The other counted classes match on both sides (AudioComponent 1210 against 1225, DecalComponent 1103 on both).
GPU busy peaked at 57 percent (`logs/c263L.gpu`), so the frames are bound by the game thread, which these
components tick on.

## Step 1 (diagnostic, next lane 2 build)

The census does not say whose components they are. Behind the same `UBWCOOP_C132_FX` gate as the census, once per
census window, group the live NiagaraComponents on each side:

- by Asset (the UNiagaraSystem name), top 10 by count;
- by outer or owner class;
- by state: active, inactive but not destroyed, and pooled (in the world's FX pool).

One row per side: `card526: <side> niagara by asset: <name> <n>, ...; by owner: ...; active A, inactive I, pooled P`.

Deciding rows:

- The top client assets that the host lacks, or holds far fewer of, are the leak.
- If they are the mod's FX replays (card 136, `fx_c136.cpp`, which detours SpawnSystemAtLocation and
  SpawnSystemAttached), the cause is in the replay. Check its bAutoDestroy and pool method arguments, and how an
  attached replay ends when its parent dies or is hidden.
- If they are the game's own client-side FX on bug copies (for example a ragdoll or corpse whose effect never ends
  because the client copy skips the path that ends it), the cause is in that copy path.

## Done when

The client NiagaraComponent count tracks the host's within 20 percent over a whole C3M2 load run, with digest 0
mismatches.

## Step 1 in source (senior, 2026-11-19, not yet in a build)

- `frame_c330.cpp`: `c526_niagara_row`, called right after each `card252` census row (same `UBWCOOP_C132_FX` gate).
  It reads the live NiagaraComponents (`objects_of_class`, up to 16384) and groups them by Asset (0x520), top 10,
  and by owner class (OwnerPrivate 0xA0), top 6, with up to 64 distinct keys each (more are counted as "other
  keys"). It also counts active and inactive components (UActorComponent byte 0x96 bit 4, from `execIsActive`
  0x03A60C40) and each pool method (0x650: none, auto, manual, manual on complete, free in pool).
- Row: `card526: <side> niagara T (N read): by asset ...; by owner class ...; active A, inactive I; pool method none
  ..., free in pool F, unknown U; scan S ms`.
- `kNiagaraPoolingMethod` moved from `fx_c136.cpp` into `hooks_internal.h` next to `kNiagaraAsset`, so there is
  one definition. Both objects compile clean, with 0 warnings. Backups: `build/frame_c330.cpp.pre526`,
  `build/fx_c136.cpp.pre526`, `build/hooks_internal.h.pre526`.
- Predictions (next lane 2 run with the FX flag): the client's total grows as in c263L. One to three assets, or
  one owner class, hold most of the growth. The growth sits in inactive or free-in-pool components, not active
  ones (the GPU is not loaded).

## Step 1 run (c263L312, b312 8ac5b700c29e6f94, FED_C3M2, junior run, senior dig 2026-11-19)

- Client 6807 NiagaraComponents, 6481 inactive, 6557 pool method none. 4704 of them are
  `NS_FX_Enemy_Morita_MuzzleFlash4`. Host 2351 in all, that asset not in its top 10. Predictions hold: one asset,
  inactive, no pool.
- Both sides spawn it through the game's SpawnSystemAttached / SpawnSystemAttachedScale (owner
  BP_AI_Trooper_Morita_C; client 5547 spawns, host 2188).
- The owner-class table overflowed (other keys 6205), so that column told nothing.

## Cause (static, `proj_c132.cpp`)

`C136_PASS_INACTIVE` (and the SpawnSystemAttachedWithParams detour): on the client a host-sent attached spawn runs
the original with bAutoActivate cleared, because the callers use the result without a null check (hb63). The
component is real, never activated, and bAutoDestroy only fires when a system finishes, so it lives until its
parent goes. Moritas live the whole map and fire constantly, so every muzzle flash adds one. This is the attached
twin of what card 409 fixed on the thunk path with `c409_discard`.

## Fix in source (senior, 2026-11-19, not yet in a build)

- `proj_c132.cpp`: `c526_quiet`, after each blocked attached spawn on the client. A table of 64 keys (parent
  component, socket FName, asset) holds a weak ref to the key's last quiet component. A new quiet component for the
  same key discards the older one through `c136_discard_component` (new external wrapper of `c409_discard` in
  `fx_c136.cpp`: back to its pool when pooled, else DestroyComponent). A full table takes over slots round robin;
  that slot's old component is left to its parent, as before. The caller still gets its non-null component.
- Row at each map load (`c132_proj_map`): `card526: client previous map quiet attached spawns: M made, D older of
  the same key discarded, R table slots taken over (table of 64)`.
- Both objects compile clean, 0 warnings. Backups: `build/proj_c132.cpp.pre526` (before this fix). The step 1
  backup `build/fx_c136.cpp.pre526` was overwritten by this fix's backup, so it now holds the after-step-1 file;
  step 1's only change there was the `kNiagaraPoolingMethod` move.
- Risk: a caller that keeps the older quiet component in a variable now holds a destroyed component. It was never
  active and the host plays the effect, so nothing visible changes; a later Activate of it is blocked as point 6.
- Predictions (next lane 2 C3M2 run with the FX flag): client `NS_FX_Enemy_Morita_MuzzleFlash4` under 50 at each
  census; client total within 20 percent of the host's; discarded about equal to made minus the key count; 0 new
  WARN or ERROR lines; digest 0 mismatches.

## Fix run (c263L313, b313 99a5d59922248f6b, FED_C3M2, senior dig 2026-11-19): done

- Client NiagaraComponents per census: 1179, 1513, 1694, 1728, 2075, 2026, 2079. Host: 1782, 1892, 1990, 2072, 2362,
  2439, 2393. The client stays below the host (c263L312: 6807 against 2351). `NS_FX_Enemy_Morita_MuzzleFlash4` peaked
  at 40 on the client (c263L312: 4704).
- Map row: 15228 made, 7583 older of the same key discarded, 0 table slots taken over. The other 7645 found no older
  component, because it was already gone (its parent or the Morita went). So "discarded about equal to made minus
  keys" fails as written, and it does not matter: the count is flat.
- Digest 11 rounds, 0 mismatches. Client WARN 82 (c263L312: 34): 62 are card 137 arrival rows after the map change
  (card 528, not this fix); the other 20 match c263L312's other 19.
- Done-when met: the client count tracks the host's (it stays lower) over the whole load run, digest 0.
