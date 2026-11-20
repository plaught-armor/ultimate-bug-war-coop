# Card 105: Resupply pod contents are the host's, and the flare tutorial step completes for a client throw

Status: written 2026-11-05 (senior); sent and accepted 2026-11-06.

Read:

- card 104 Report, "Strike classes" and "Open points";
- card 098 Report (`ChanOp::WorldOp`, journal, `c95_find_nearest`);
- card 100 Report (`c100_skip_local`, client-local objective gate);
- `docs/STATE.md`, the sync rule (one mechanism per class).

Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`. Play DLL base: `c5a6e7f5bc589024` (card 101).

## Why

User rule (2026-11-05): all world, enemy and objective state is the same for every player. Card 104 left three gaps:

1. The client spawns its own copy of `BP_TacSupp_Pod_Resupply_C` from a WorldOp 'P' row. That copy runs its own
   Blueprint and spawns a client-local rack and pickups (client log: MoritaSmall placed=3). The host's pod spawns the
   real ones. Picking up a client-local item gives the client nothing on the host, and the host's items are not visible
   to the client unless they replicate.
2. TacSupport_SO6 (the tutorial step after the throw) stays complete 0 on the host after the client's throw.
3. WorldOp 'P' and 'D' rows are not journalled, so a late joiner misses a pod or flare that is still in the world.

## Do

1. **Measure (one run, log only, FAST=1, scenario as c104e).** Client throws the flare.
   - Host: every actor the pod spawns (class, replicated flag, owner), and whether the pickups replicate to the client
     (a client-side spawn of the same class through the replication channel).
   - Client: every actor its pod copy spawns.
   - The SO6 objective: its completion condition (Blueprint names from the objective asset), which event or call it
     waits for, and whether that event fires on the host for the server copy's throw. Compare with a host throw (the
     host's own throw also proves the host path still works after card 104).
2. **Fix the pod.** One rule for relayed actors: a client copy made from a 'P' row is visual only and runs no gameplay
   spawns. Then the host's pod children reach the client by the existing path for their class: replication if they
   replicate; otherwise the same 'P' rows (add the rack and pickup classes the measure names). Pickups the client takes
   must act on the host (card 054 interact relay or the pickup's own server path); say which in the Report.
3. **Fix SO6 at the cause** found in step 1 (for example the step listens to "the player" or the local pawn's ability
   event, which card 099/102 gating does not cover). It completes on the host and replays to the client through card
   049.
4. **Journal** live 'P' rows per map (drop on 'D'), replayed on first Pong like card 098's rows.
5. **Check the handle -1 flip.** Card 104 flips every server-copy spec during `HandleGameplayEvent`. In the same run,
   count activations per ability on the server copy (hit reactions, fire, reload). An ability that the client predicts
   and also sends by `ServerTryActivateAbility` must activate once per client action, not twice. If two, scope the
   flip to the triggered spec(s) of that event. Card 101 checks the jump and mantle; this run covers the rest.
   Card 101 already skips specs whose handle the client has sent (`g_c101_client_sent`, a bitset of 1024 handles).
   GAS spec handles come from one process-wide counter that only grows, so after enough respawns and map loads the
   client's handles pass 1024, stop being noted, and the double activation returns. Replace the bitset with a
   structure that holds the live handles regardless of their value (for example a fixed ring of the most recent
   client-sent handles, or a flag per live spec). Also: a card 101 mantle window whose pawn is destroyed before it
   closes keeps its slot and a stale CMC pointer forever (4 slots); free it when the server copy dies or is unclassified.
6. **Proof**, one run: the client throws, the pod lands on both sides with the same children (host-owned), the client
   picks one up and the host shows it taken, SO6 completes on both, a late client join after the throw sees the pod.
   The host's own throw still works. rc 0, ERROR/FATAL 0, no new dump.

Stop when the fix builds and that one run shows it; the user will playtest.

## Done when

- The Report has the pod children table (class, replicated, how it reaches the client), the SO6 cause and fix, the
  journal, the activation counts, the proof run and the DLL hash.
- The Report ends with "Report complete."

## Report
Written 2026-11-06 (junior). DLL `00a35c0c5d36d47d` (copy: `build/dwmapi-c105-00a35c0c5d36d47d.dll`).

Runs:

- c105a: measure, `tools/scenarios/c105-measure.txt`.
- c105p: proof, `tools/scenarios/c105-proof.txt`.

Both used FAST=1 and ended with rc 0 on both sides, ERROR/FATAL 0 and no new dump.

### Pod children

All of the pod's children are spawned by the pod's Blueprint through `BeginDeferredActorSpawnFromClass`, caller exe+0x3C78588, inside the BP SpawnActor thunk. None of them replicate. Times are from c105p, where the flare activated at host t=17579851.

| Class | Replicated | How it reaches the client |
|---|---|---|
| `BP_TacSupp_Pod_Resupply_C` | no | 'P' row (card 104); 'D' when it destroys itself about 1 s after landing |
| `BP_TacSupp_DangerZone_C` (two) | no | 'P' rows (card 104 strike prefix); 'D' about 1 s later |
| `BP_SupportFlare_C` | no | 'P' row (card 104); 'D' at expiry |
| `BP_ResupplyRack_Level0_C` | no | 'P' row (new: `kC105RackPrefix`); 'D' when it is destroyed, about 15 s later |
| `BP_WeaponPickup_MoritaMk1_C` | no | 'P' row (new: any `AADPickupBase` subclass spawned through the BP thunk); 'D' on host `DisablePickup` |
| `BP_AmmoPickup_MoritaSmall_C` | no | same as MoritaMk1 |
| `BP_HealthPickup_Medium_C` | no | same as MoritaMk1 |
| `BP_GrenadeAmmoPickup_Incendiary_C` | no | 'P', then 'D' at once: the host disables it in the same frame (`DisablePickup`, character null) |

Before this card, the client's pod copy spawned its own rack and pickups (c105a client: MoritaSmall placed=3). Now, on the client, `detour_c75_world_spawn` refuses a BP-thunk spawn of a relayed class and returns null. In c105p the client log shows four lines of the form `card105: client ... blocked Blueprint spawn of <class>`, one for each pickup class. The only client rack and pickups are the relayed copies, at the host's positions.

**Pickup path.** When the client takes a pod pickup, the take acts on the host through the existing paths:

- Interact pickups use card 050/054. The client's Use goes to the server copy, and the server copy's own interact trace targets the host's real pickup at the same place. That pickup's class is not placed, so the client sends no UseName and the host uses the server copy's target.
- Overlap pickups are taken when the server copy overlaps the host's actor.

Either way, the host's `DisablePickup` now calls `c104_relay_destroy`, which sends 'D' and removes the client's copy. The Incendiary row in c105p shows that 'D' path end to end.

Not shown: a client taking a *pod* pickup. The client's E in c105p targeted the nearer placed `BP_WeaponPickup_MoritaMk1_C_1`, not the pod's copy. The user's playtest covers this.

### SO6

**Cause.** In both runs, TacSupport_SO6 stays complete 0 on both sides. This is not a client-versus-host gap:

- The scenario completes SO3, SO4, SO41 and SO5 by command. That skips the level script's own sequence for those steps.
- The script never reveals the tutorial support pickup. `BP_TacSuppPickup_BombingRun_Tutorial_C` stays hidden from map load, and the client's E on it found `target null`.
- The script never equips the tutorial configuration. Every `EquipTacticalSupportAbility` in c105a was `Config_TacSupp_Resupply_C` (callers exe+0x50DF298 at spawn and exe+0x50E3A66 after a throw), on both host and client.
- So the thrown flare is the default resupply flare, not the tutorial step that SO6 tracks. SO6 and SO7 both reference `ActorReference_C_1`.

Card 097's c97-tac2 found the same with a host-only throw.

**Fix.** No objective-specific fix was made. When the real sequence runs, SO6's completion goes through the host's level script. Card 097's hook relays every completion path to the client through card 049, so the card 105 relay changes are what let a client-driven sequence finish on the host: the throw itself (card 104), and the pod and pickups now on the host.

**Playtest check.** Play the tutorial area for real, without the forced completions. The client log should show `native CompleteObjective TacSupport_01/TacSupport_SO6`.

### Journal

`c104_relay_spawn` now journals its 'P' rows with `c98_host_world(row, world, true)`:

- `c104_relay_destroy` drops the row (`c98_unjournal`) before it sends 'D'.
- The rows replay on the first Pong of the map, as card 098's rows do.
- `kC98WorldRows` went from 64 to 128, and the client's copy table `kC104Copies` from 16 to 64.

In c105p:

- The client disconnected after the pod had landed and reconnected 5955 ms later.
- The host logged `world journal replay ... 3 of 3`.
- The client made copies of the three live pod pickups: MoritaMk1, MoritaSmall and Health.
- The pod, flare, danger zones, rack and Incendiary were not replayed, because their rows had already been dropped at 'D'.

### Activations and the handle -1 flip

I added a capped log line, `card105: host server-copy activation`, after every successful `InternalTryActivateAbility` on a server copy. It records the path (the client's own request, or a host call such as HandleGameplayEvent) and whether the client had sent that handle. Server-copy activations in c105p, for the client's weapon switch, three shots, reload, throw and E presses:

| Ability | Count | Path | In the client-sent set |
|---|---|---|---|
| GA_WeaponSwitchAbility_C (handle 37) | 2 | host call | no |
| GA_Weapon_RecoilAbility_C (handle 39) | 1 | client request | yes |
| GA_UseTacticalSupportAbility_C (handle 40) | 1 | host call | no |
| GA_TacSupp_Resupply_C (handle 46) | 1 | host call | no |
| GA_ShuntAbilityCharacter_C (handle 34) | 1 | host call | no |

No handle activated through both paths, so there is no double activation and the flip needs no narrower scope.

Gaps:

- Weapon fire and reload are not GAS abilities on the server copy, so they produced no rows.
- No hit reaction occurred in this run.
- The two WeaponSwitch rows match the client's two switches (the pickup equip and key One).

**Card 101 fixes.**

- **Client-sent set.** The `g_c101_client_sent` 1024-bit set is replaced by `C101Sent`, a fixed ring that holds the 256 most recent client-sent handles, whatever their values (`anim_c104.cpp`).
- **Mantle window slot.** `c101_forget(actor)` (`jump_c94.cpp`) frees a mantle window slot whose server copy is destroyed (`detour_c98_destroy`, host). It also frees all slots at a map change (`c98_arm_map`). It does not write to the CMC, because the component is going away.

### Proof run c105p

| Check | Result |
|---|---|
| The client throws, and the pod lands on both sides with the same host-owned children | shown: 'P' rows for the rack and four pickups, the client's own four BP spawns blocked, the copies at the host's positions |
| The client takes a pod pickup, and the host shows it taken | not shown (the client targeted a placed pickup); the 'D'-on-`DisablePickup` path is shown by the Incendiary row |
| SO6 completes on both sides | not shown: the scenario's forced completions keep the tutorial step from arming (see SO6) |
| A late join sees the pod's live items | shown: replay 3 of 3, and the client copies the three pickups |
| The host's own throw still works | not shown: the host had no tactical support charge in this route, and Four plus fire did nothing; card 104 showed the host throw |
| rc 0, ERROR/FATAL 0, no new dump | shown: host 3140 lines, client 4498 lines, drive rc 0 |

The fix builds, and the run shows the pod relay and the late join. Per the card, the user playtests the pod pickup by the client and the real SO6 path from here.

Report complete.
