# Card 541: performance mod, component churn (sounds, effects, brushes): research, no lever now

Family: card 527 (performance mod). Status: researched and closed; it reopens on the trigger below.

## Question

Card 537's class rows (c263L359, b353, host, about 230 s) count 156448 owned-component set changes (an add or a
remove on an actor):
- AudioComponent 26833;
- BrushComponent 23303;
- NiagaraComponent 14098;
- StaticMeshComponent 4974;
- SceneComponent 2850;
- 16337 more past the 64-row class table.

That is about 680 changes per second. Sounds and effects get a fresh component per play, which is destroyed when it
finishes. Does that churn cost enough to fix?

## Findings (static and the L359 host profile)

- The spawn paths take a pool argument. UNiagaraFunctionLibrary::SpawnSystemAtLocation / SpawnSystemAttached, the
  game's USTParticleEffectManager::STSpawnSystemAtLocation and USTCommonUtilityLibrary::STSpawnNiagraSystemAtLocation
  all take `ENCPoolMethod`. Sounds come through UGameplayStatics::SpawnSoundAtLocation / SpawnSoundAttached and
  USTCommonUtilityLibrary::SpawnSoundAttachedToComponentWithAIAwareness. Each returns a UAudioComponent, so it
  allocates one. The PlaySound* paths do not.
- None of the creation or destruction path shows up in c263L359-host.perf.frame.txt, on any thread. That covers
  the SpawnSystem* and SpawnSound* entries, RegisterComponentWithWorld, ExecuteRegisterEvents, CreateRenderState,
  StaticConstructObject, BeginDestroy and FinishDestroy. The listing floor is a few samples (about 0.04% of a
  thread).
- What does show is ticking existing effects, not creating them:
  - GameThread: UNiagaraComponent 0.5% (PostSystemTick_GameThread 0.3%) and FNiagaraSystemInstance 1.0%;
  - BG: SendRenderDynamicData_Concurrent 0.1%.
  - Pooling would not change that.
- The churn's cost to our own FindComponentByClass cache (card 536) is also small: 156448 changes against
  105343846 hits, at a 99.69% hit rate.

## Decision

No pooling lever now. The churn is real, but it costs under the profile floor, so forcing `ENCPoolMethod::AutoRelease`
or reusing audio components would trade a near-zero gain for risk:
- Pooled components keep state.
- Game code that holds the returned component after it completes would act on a recycled one.
- Card 130's effect relay tracks those components by pointer.

The larger effect cost is the per-frame Niagara tick (about 1.5% of the GameThread). That is a separate question:
significance and culling settings, not churn.

## Reopen trigger

Any run whose GameThread or BG profile lists the spawn, register or destroy functions above at 0.5% or more. Then:
1. Card a diag of add/remove by (component class, owner class), which also names the BrushComponent owners. Brush
   churn is unexpected, since volumes and triggers are not per-event objects, and it may be level streaming
   driven by the scenario's warps.
2. Card the pool lever only on spawn sites whose return value is not kept.
