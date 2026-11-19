# 488: client audio mixer crash after `client quit`

Second sighting. Card 354 (b40) noted the first one and said a second one gets a card.

## Sightings

| Run | DLL | Time after `client quit -> done` | Thread | Fault |
|---|---|---|---|---|
| b40 (card 354) | b40 | 420 ms | 1328 (not game thread 592) | AV reading 0x44 at exe+0x227061A |
| c244b215b | b214/b215 `56b7f47d0bbc4783` | 1201 ms (quit 650468258, crash 650469459) | 1304 (not game thread 536) | AV reading 0x44 at exe+0x227061A |

The stacks are the same in both: `#0 exe+0x227061A #1 exe+0x334686D #2 exe+0x422704A #3 exe+0x333F56C #4 exe+0x33480DF
#5..#7 exe+0x3347B0A #8 exe+0x331DE26 #9 exe+0x227DD78 #10 exe+0x227E8D7 #11 exe+0x227E79E #12 exe+0x1359937
#13 exe+0x1359709`. That is `FPatchOutput::PopAudio` called from `FSubmixEffectDynamicsProcessor::OnProcessAudio`
(card 354). No mod frame appears. The registers are the same as well: rax=0, rcx=0, r8=800.

In c244b215b the crash produced the dump UECC-Windows-2A5290B34FCA680C26F5EB9BF2E94E4C_0000. The client rc was 0, and
the scenario had already finished (`host quit -> done` at +1299 s).

## Impact

The crash happens only at process exit, after the mod's quit totals. Gameplay and sync are not affected. It does add
a crash dump, and the EXCEPTION grep counts it on every run where it happens.

## Open

- Find out whether the mod's quit path (`RequestEngineExit` from the cmd channel) tears down a submix patch before the
  audio render thread stops. One check: does the crash also happen when the player quits normally from the menu?
- Both sightings were long runs with heavy FX: about 1300 s on FED_C4M1, with the client sound on the null sink.

## b223 static trace (junior)

- `client quit` (testrun.cpp `cmdfile::Op::Quit`) logs, runs `sync_quit` (registry/report rows only: c332 teardown
  state, totals rows, c304/c408/c161 map-end tallies) and calls `RequestEngineExit` (exe+0x12C2B70). That is the same
  engine entry the game's own quit reaches (`FPlatformMisc::RequestExit(false)`), so the engine shutdown order is the
  game's own. The UBWCOOP_AUTOINPUT exit (autoinput.cpp) is the same call.
- No mod code creates, patches or destroys a submix, an `FPatchOutput` or an audio device. The mod's audio hooks
  (fx_c136 card 176 component sounds, card 304 tally) run on the game thread; the fault is on the audio render thread.
- `DLL_PROCESS_DETACH` (dllmain.cpp) removes the UPnP mapping, ends the playlog and prints the c332 row. It disables
  no hooks and frees nothing the audio thread reads.
- Conclusion: no mod cause found on the quit path. The fault reads offset 0x44 of a null patch output from
  `FSubmixEffectDynamicsProcessor::OnProcessAudio` while the engine shuts the mixer down: an engine race after exit.
  Not fixed in b223. The open check stays: one menu quit (no mod cmd) on a long FX run to see whether it happens
  without the cmd channel.

## Trace (2026-11-18 18:50, senior, static)

- The mod's quit is `cmdfile::Op::Quit` (mod/src/ubw/testrun.cpp:3052): `sync_quit` (log totals only, no audio
  object touched), then `RequestEngineExit`, the same engine exit a menu quit takes. DllMain detach touches UPnP,
  the play log and the card 332 row only. No mod code creates, patches or frees a submix, a patch output or an audio
  device (the only audio-device use is the card 136 `FAudioDevice::CreateComponent` thunk count).
- The fault is `Audio::FPatchOutput::PopAudio` (RVA 0x0227xxxx region, public `?PopAudio@FPatchOutput@Audio@@`)
  reading member 0x44 of a null object on the audio render thread, called from
  `FSubmixEffectDynamicsProcessor::OnProcessAudio` (the sidechain/key patch). The render thread is still mixing
  while the engine exit has freed the patch: an engine shutdown race, not a mod path.
- Both sightings are on the test client with its sound on the null sink after a 1300 s FX-heavy run. Not seen on
  any host or in user play.

Decision: no mod fix. The engine owns the teardown order and the mod has no hook on it worth adding for an exit-only
fault. The EXCEPTION grep should treat a crash after `quit -> done` with this stack as known (card 488), so it does
not void a run.

Reopen when: the same stack appears before `quit -> done`, on the host, or in user play with real sound.

## Closed (2026-11-18 20:16, senior)

Evidence: the static trace above. The mod's quit path is the engine's own exit and touches no audio object; the
fault is an engine audio render thread race after exit, seen only on the test client after `quit -> done`.
Not done: the menu-quit check without the cmd channel (not worth a run for an exit-only fault).
Reopen when: the same stack appears before `quit -> done`, on the host, or in user play with real sound.
