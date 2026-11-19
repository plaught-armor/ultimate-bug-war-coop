# Card 182 — Host crash in travel: stale pawn pointers read during LoadMap

## Goal
No mod code reads a pointer into the old world between `UEngine::LoadMap` enter and the first tick of the new map.
The host must survive `host travel next` in `tools/scenarios/c176-click.txt`.

## Found (c180-r1, DLL a1571e46d8325541)
Host: "EXCEPTION_ACCESS_VIOLATION at ubwcoop+0xDD2DD", reading 0xFFFFFFFFFFFFFFFF, inside the travel LoadMap
(log line 4159; LoadMap enter at line 4011). Path: UWorld::InitializeActorsForPlay → RouteActorInitialize →
UNiagaraComponent::ActivateInternal → UNiagaraDataInterfaceAudioPlayer::PerInstanceTickPostSimulate →
PlaySoundAtLocation → the mod's detour → `c162_call_atten` → `pl_remote_source` (playlog.cpp:622) →
`actor_location` on `g_pl.pawn[0]` or `g_pl.remote[].pawn`.

## Senior trace (2026-11-11)
- **Verified:** `g_pl.pawn[]`, `g_pl.pc[]`, `g_pl.weapon[]`, `g_pl.remote[]`, `g_pl.own_cmc` are raw pointers,
  refreshed by `pl_pawns` every 500 ms from `pl_step`. They are cleared only in `pl_map` (playlog.cpp:732), which
  runs from `c98_arm_map` ← `c50_arm_map` (sprint.cpp:239), that is, after the new map is up. During LoadMap they
  still name the old world's freed pawns.
- **Verified:** `ubw_load_map_enter` (hooks_ubw.cpp:233) is the place that already drops tables naming old-world
  objects (`c157_map_end`, `c132_rounds_map_end`, `c75_map_end`).
- **Fix shape:** a `pl_map_end()` called from `ubw_load_map_enter` clears the g_pl pointer fields (the summary and
  counters stay in `pl_map`). The same rule in STATE.md applies: a pointer kept across frames is a weak pointer or
  its table is cleared at map load; "map load" must mean LoadMap enter, not the first tick after.
- **Class sweep (required, in the Report):** every detour that can fire inside LoadMap (the card 136/160/162/173/176
  sound, Niagara and widget play detours, spawn hooks) and every raw game pointer those paths read from a mod global.
  List each global, where it is cleared, and whether that is before LoadMap destroys the old world. Fix the ones in
  the files below; name the others for the senior. Start with the `c50_arm_map` family: any table it clears that a
  LoadMap-time detour reads has the same hole. `g_c176` uses weak entries; confirm.

## Junior trace (2026-11-11, before code)
- **Order, verified.** `ue::net`'s LoadMap detour calls `ubw_load_map_enter` (hooks_ubw.cpp:233), then the
  original LoadMap, then `ubw_load_map_return` → `client_diag_load_map_return` (sprint.cpp:203) → `c50_arm_map`
  (sprint.cpp:239) → `c98_arm_map` → `pl_map` (world_c98.cpp:1569). The new world's `InitializeActorsForPlay` runs
  inside the original LoadMap, so every table the `c50_arm_map` family clears is stale during that window. No tick
  runs inside LoadMap, so `pl_pawns` (pl_step, playlog.cpp:479) cannot refill `g_pl` before `pl_map`.
- **Crash path, verified from the c180-r1 call stack.** RouteActorInitialize → UNiagaraComponent::ActivateInternal →
  the audio data interface → `detour_c74_play_sound` (grenades.cpp) → `c162_call_atten` (fx_c136.cpp:927) →
  `c162_remote_atten` → `pl_remote_source` (playlog.cpp:622) → `actor_location(g_pl.remote[].pawn)` and
  `actor_location(g_pl.pawn[0])`. ubwcoop+0xDD2DD is inside `actor_location`'s read of the root component (+0x1B8).

### Sweep: LoadMap-time detours (sound, Niagara, widget, spawn) and the mod globals they read

One row per global. "Deref" means the path reads memory through the stored pointer; "compare" means it only tests
the stored value for equality.

| Global (file) | Read by (LoadMap-time path) | Use | Cleared | Before old world freed? |
|---|---|---|---|---|
| `g_pl.pawn[]` (playlog.cpp) | `pl_remote_source`, `pl_listener_uu` (c162/c173 via c74_play_sound, c74_spawn_sound, SpawnSoundAttached), `pl_sound` and `pl_sound_row` (c74 sound detours, `detour_pl_play_internal`) | **deref** (`actor_location`) | `pl_map`, from `c98_arm_map` after LoadMap | **No: the crash** |
| `g_pl.remote[].pawn` (playlog.cpp) | `pl_remote_source` | **deref** (`actor_location`) | `pl_map` | **No: the crash** |
| `g_pl.pc[]` (playlog.cpp) | `pl_shot` (`read_vec3`, camera manager), `pl_fov`; both from the fire and tick paths, not from the swept detours | deref, but not on a LoadMap-time path | `pl_map` | No (cleared with pawn in the fix) |
| `g_pl.weapon[]`, `g_pl.mi[]` (playlog.cpp) | `pl_weapons` (tick), `pl_shot`; not on a LoadMap-time path. `mi[]` is never cleared today | deref of `pawn[s]` gated by `mi[s]` | `pl_map` (weapon); `mi[]` only by `pl_pawns` | No (cleared with pawn in the fix) |
| `g_pl.own_cmc` (playlog.cpp) | `detour_pl_impact` | compare | `pl_map` | No, harmless; cleared in the fix |
| `g_pl.widget[]` (playlog.cpp) | `detour_pl_add_to_viewport`, `detour_pl_remove_from_parent` | compare | `pl_map` | No, harmless; left in `pl_map` so the old map's widget-remove lines during teardown still log |
| `g_client.pc`, `g_client.pawn` (fx_c136_net.cpp) | `c136_client_own` → `filter_hits` (c136_fx_enter, client) | compare | refreshed per call when null (fx_c136_net.cpp:1811) | n/a: compare only |
| `g_c176.ring[]` (player_c99.cpp) | `c176_mute_copy_2d` (`detour_pl_play_internal`) | weak (`c31_weak_set`) | `c99_arm_map` | n/a: weak, confirmed |
| `g_c162.fallback[].weak` (fx_c136.cpp) | `c162_fallback` | weak (`c31_weak_get`); the asset is game-instance registered | never (persistent asset) | n/a |
| `g_util_live.weak[]` (grenades.cpp) | `c132_util_track` (`detour_c132_util_niagara`) | weak | its own sweep | n/a |
| `g_c136.live.rows[]` (fx_c136.cpp) | `c136_spawned_id`, `c136_forget` (Niagara activate/deactivate) | compare, then `c136_alive` (weak) | `c136_arm_map` | n/a: weak |
| `g_c104_relayed`, `g_c104_pending` (world_c98.cpp) | `c104_relay_spawn` (`detour_c75_world_spawn`) | append and compare; live use goes through `weak[]` | `c98_arm_map` | n/a: weak (but see Out of scope) |
| `g_c160.widget_copy` (player_c99.cpp) | `detour_c160_widget_world` | `object_name` of a pointer set and restored inside the same `detour_c160_exec_create` call | scoped | n/a: call-scoped |
| `g_c102.acting` (player_c99.cpp) | `detour_c99_begin_play` | `object_name`, set inside an acting scope | scoped | n/a: call-scoped |
| `t_c136_notify` (fx_c136.cpp) | `c136_owner` → `c136_first_actor` | deref, saved and restored around the notify call | scoped | n/a: call-scoped |
| `g_c132_fx_proj` (proj_c132.cpp) | `c132_fx_probe` | compare | card 132 map end | n/a: compare only |
| `g_c74_fx_got.system` (grenades.cpp) | `detour_c74_st_niagara` and siblings | compare and set (an asset) | c74 capture end | n/a |
| `g_c76.mech_at[]` (fire.cpp) | `c76_fx` → `c76_mech_dist` | Vec3 values, no pointer | c76 map arm | n/a |
| `g_mk` (marker_c166.cpp) | `detour_c168_play_animation` | counters only | `c166_arm_map` | n/a |

Other paths checked and clean: `c99_copy_of`/`sprint_side` read only the argument actor; `c46_spawn_spot` takes
its target from `c43_roster(world)` of the new world; `c98_note` reads the spawned actor; `c136_net_queue` holds
weak references; `c98_fx_mute`, `c74_log_fx`, `c132_fx_audit_line`, `c136_log_event` read no stored game pointer.

**Result.** The only raw old-world pointers dereferenced on a LoadMap-time path are `g_pl.pawn[]` and
`g_pl.remote[].pawn`. Both are in the allowed file. No other file needs a fix.

**Fix.** `pl_map_end()` (playlog.cpp) clears `pawn`, `pc`, `mi`, `weapon`, `remote` and `own_cmc`, and sets
`pawns_ms = 0`; `ubw_load_map_enter` calls it. `pl_map` keeps the summary, the counters, `widget_n`, `reload_ms`
and the ads state. With the pointers null, `pl_remote_source` returns false and `pl_sound`/`pl_listener_uu` use
their null branches, so sounds during LoadMap play as if no player were present. `pl_pawns` refills on the first
tick of the new map, as it did before.

## Predictions (one run, c176-click.txt, same command as card 180)
- host and client reach the new map; no CRASH line on either side; drive rc 0; no dead time.
- carried from 176: the map line after travel "card160: map: host skipped server copy effects: ... muted N 2D
  component sounds (0 muted auto-destroy components still live)".
- carried from 180: FireProjectile host = client; one flush line per release.
- baseline WARN only; the client's "client follow gave up" WARN of c180-r1 is gone (it followed the host crash).

## Files
`mod/src/ubw/playlog.cpp`, `mod/src/ubw/hooks_ubw.cpp` (`ubw_load_map_enter` only), `mod/src/ubw/hooks_internal.h`,
the card. Another file only for a sweep finding of the same class, named in the Report with the reason.

## Report

### Files changed
- mod/src/ubw/playlog.cpp: new `pl_map_end()` clears `g_pl.pawn`, `pc`, `mi`, `weapon`, `remote`, `own_cmc` and
  `pawns_ms`. `pl_map` calls it in place of its own clears, and keeps the summary, the counters, `reload_ms`,
  `widget_n` and the ads state.
- mod/src/ubw/hooks_ubw.cpp: `ubw_load_map_enter` calls `pl_map_end()` after `c75_map_end()`.
- mod/src/ubw/hooks_internal.h: the `pl_map_end` declaration.
- docs/cards/182-stale-pawn-pointers-at-loadmap.md: the trace, the sweep table and this report.
- No file outside the list. The sweep found no other raw old-world pointer read on a LoadMap-time path.

### Summary
**Cause.** The c180-r1 crash came from `g_pl.pawn[]` and `g_pl.remote[].pawn`. They were cleared only in `pl_map`,
which runs after LoadMap returns, while a Niagara audio interface in the new world's actor init played a sound
through `detour_c74_play_sound` → `c162_call_atten` → `pl_remote_source` → `actor_location` on a freed pawn.

**Fix.** These pointers are now cleared at LoadMap enter, and `pl_map` still clears them as before. Now `mi[]`
is cleared too; before, nothing but `pl_pawns` reset it. Behaviour outside LoadMap is unchanged.

**Sweep.** See the table in the Junior trace. Only the two `g_pl` rows were raw old-world derefs. The rest are
weak, compare-only, call-scoped or value-only.

**Weak pointers for pl_pawns:** not needed for this crash class. With the LoadMap-enter clear, every `g_pl` pointer
is refreshed every kPlPawnMs from the live world and is null across the only window where the world is freed under
it. A weak pointer would only matter if a pawn could be destroyed mid-map and its address read within 500 ms
(a death and respawn). `pl_remote_add` ages entries, but `pawn[s]` is read through `actor_location` with no check
until the next refresh. That window is not proven in any run, and is not fixed here.

**Not done:**
- No proof that a sound played inside this travel's LoadMap. The host log between LoadMap enter (line 4019) and
  return (line 4698) has no card099, card162, card173 or playlog audio line. The run shows the travel survives. It
  does not show the crash path being exercised with null pointers, because the c180-r1 crash was timing-dependent.

### Verification
Build: `[64/64] Linking CXX shared library dwmapi.dll`, 0 warnings (rebuild of playlog.cpp and hooks_ubw.cpp grepped
for "warning": 0). DLL d84ab865f918c520 ("deployed: d84ab865f918c520", "lane 1: dll d84ab865f918c520, build
d84ab865f918c520, port 7777").

Run c182-r1: "run c182-r1: ended by scenario after 83 s; host rc 0, client rc 0".

Predictions:
- Both sides reach the new map:
  - Host: "UEngine::LoadMap enter (ctx=0000000003EB3580, pending=0000000000000000)" (line 4019), "UEngine::LoadMap
    returned true" (line 4698), "card052: t=90809390 world FED_C2M1 (package /Game/Maps/FED/FED_C2M1, net mode 2)".
  - Client: "card052: t=90813087 world FED_C2M1 (package /Game/Maps/FED/FED_C2M1, net mode 3)".
  - "new crash dumps: none". No CRASH line on either side.
  - drive: "line 93: host travel next -> done", "scenario c176-click.txt: drive rc 0", "dead time: sleeps 0.0 s,
    timed-out try waits 0.0 s in 0 (none)", "scenario passed". Held.
- Card 160 map line after travel (host line 4888): "card160: map: host skipped server copy effects: 2 2D sounds, 0
  material parameters, 0 widgets; muted 58 2D component sounds (0 muted auto-destroy components still live)". Held.
- FireProjectile: 58 "card020: host server-copy FireProjectile" against 58 "card020: client client-own
  FireProjectile". Releases: 9 "card024: host relay release" on the host, and 9 "card180: client ... own connection
  flushed after the release (N so far)" on the client. Held.
- WARN: host 1, client 1, both "WARN attach: DisableThreadLibraryCalls failed, err=126" (baseline). There is no
  "client follow gave up" line. Held.

"hooks: skipped" (7 lines per side, the first starting "hooks: skipped: UWorld_Listen,
AController_SetIgnoreMoveInput, UCommonUIActionRouterBase_ApplyUIInputConfig, ..."): identical to c180-r1 on both
sides (diff empty).

Counts: host 7180 lines, FATAL/ERROR 0, WARN 1; client 7216 lines, FATAL/ERROR 0, WARN 1.

### Out of scope
- `g_c104_relayed` and `g_c104_pending` are reset in `c98_arm_map`, after LoadMap. A relayed spawn that
  `detour_c75_world_spawn` records during the new world's init is wiped by that reset and never sent. This is not a
  stale-pointer read (the rows are weak). It is a possible lost relay, untested.
- Mid-map pawn death and respawn: `g_pl.pawn[s]` can name a destroyed pawn for up to kPlPawnMs (see above).

Report complete.

## Senior review (2026-11-11)
Accepted, DLL d84ab865f918c520, one run (c182-r1). Checked: diff against the pre-182 snapshot (`pl_map_end` in
playlog.cpp, one call in `ubw_load_map_enter`, declaration; nothing else); rebuild "no work to do"; build and deployed
hash equal; 0 CRASH lines both sides; FireProjectile 60/60; WARN baseline only both sides; drive "scenario passed",
dead time 0.0 s; host map line after travel "skipped server copy effects: 2 2D sounds ...; muted 58 2D component
sounds (0 muted auto-destroy components still live)", which closes the carried card 176 item. Not exercised: no sound
played inside this LoadMap, so the run shows survival, and the fix rests on the trace. Follow-ups: card 183 (the same
pointers stale mid-map), card 184 (card 104 tables reset after the new map's init).
