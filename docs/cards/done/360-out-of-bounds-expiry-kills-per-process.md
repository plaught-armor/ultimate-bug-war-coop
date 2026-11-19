# 360: Out-of-bounds countdown expiry kills per process, no authority check

From card 322 part B (run `b44r3`): `STOutOfBoundsVolume` overlaps run on both processes for both pawns, so each
process runs its own countdown for each pawn. Senior static trace, 2026-11-14 (exe read-only, image base 0x140000000):

- `USTOutOfBoundTriggerComponent::KillOwningActor` (RVA 0x051CD6E0) is three instructions: load the owner
  (`this+0xA0`), `dl = 1`, tail-jump to `USTCombatUtilityLibrary::InstaKillTarget(AActor*, bool)` (RVA 0x05241580).
- `InstaKillTarget` reads the target's ability system component (vtable +0x720), sums health, armour and (flag set)
  shield, and calls the damage apply at 0x145233F60. Neither function tests the net role or authority.
- Not traced: where `OnOutOfBoundsEntered` (RVA 0x051D1070) binds the timer to `KillOwningActor`, and the countdown
  length (read `GetTimeLeft`, RVA from `tools/pdb_rva.py lookup`, in a diagnostic row).

So at expiry the client kills pawns itself: its own pawn, and the host's proxy when the host is out of bounds, 7 to
15 ms from the host's own expiry (b44r2/b44r3 enter offsets). The host must be the only writer of a player death.

## Fix direction

Part A pattern of card 322: the host owns the expiry. In a client session `KillOwningActor` is not run (one plain
`card360` row per skip, with the pawn and its side); the host's kill of its own pawn or of the server copy reaches the
client through the existing player death sync. Confirm statically, before building, that both host kills take that
path (host own pawn death seen on the client's proxy; server copy death seen on the client's own pawn). Sibling
sweep: other callers of `InstaKillTarget` reachable on a client (list by the call-site scan used in card 353).

## Prediction (1 run, FED_C2M1, each player in turn held in a volume until expiry; countdown length from the row)

- Host pawn expiry: one host kill, one client `card360` skip for the proxy, host death shown on both screens (shots).
- Client pawn expiry: one host kill of the server copy, one client `card360` skip for its own pawn, the client dies
  once and respawns by the normal path; counts after the respawn follow cards 354/356.
- 0 new WARN kinds.

## Junior trace (2026-11-14, exe read-only)

Timer binding (step 1):

- `OnOutOfBoundsEntered` (0x1451D1070) gets `USTOutOfBoundsSettings` by the call at 0x14507EE80 (kept in rdi). The
  countdown is the float at settings+0x38 (config value, xmm6). It builds a `TBaseUObjectMethodDelegateInstance`
  whose method is the vcall thunk 0x1451BB3E0 (`mov rax,[rcx]; jmp [rax+0x4E0]`) and calls `SetTimer`
  (0x1442D0570) on handle this+0xC8, loop false. Vtable `??_7USTOutOfBoundTriggerComponent@@6BUObject@@@`
  (RVA 0x080A2B80) slot +0x4E0 = 0x1451CD6E0 = `KillOwningActor`; slots +0x4E8 and +0x4F0 are Entered and Exited.
  A second timer (handle this+0xD0) drives `UpdatePostProcessEffect` (0x1451EA3E0, the screen tint).
- No role or net mode test before `SetTimer`. The countdown length is logged at runtime (`card360: <side> t=..
  out of bounds countdown <s> s`, diag, after the original Entered, from `GetTimeLeft` RVA 0x051C9FE0).

Death path (step 2, static): `KillOwningActor` → `InstaKillTarget` → damage apply → Health 0 → `HandleDeathEvent`,
detoured (death.cpp:321) into `c43_death`. On the host, for the host's own pawn and for a server copy alike:
`c43_enter_down` (Downed while another player is Up, else all down / Out), then `c43_broadcast` sends the Down
frame (`card294: host t= Down <state> for pawn id N (slot S) sent now`). The client applies it in
`c43_receive_down` (`card043: client t= own pawn|other pawn guid .. -> <state>`), and card 044 runs its death when
all are down. Both host kills take this path. Gap confirmed: on the client `HandleDeathEvent` on its own pawn is
skipped only once `g_c43.client_seen` is set (a Down frame arrived before), and a proxy (`other`) death is never
skipped, so the client's own expiry kills locally.

## Fix (build b45, `build-b5/dwmapi-b45-1956b296fed986e9.dll`)

`radio_c322.cpp`: `detour_c360_kill_owning_actor`, always installed (no gate, hooks_ubw.cpp next to the card 322
out-of-bounds entries). In a client session the original is not run:
`card360: client t=<ms> out of bounds expiry on <pawn> (<side>): KillOwningActor not run; the host's kill comes by
the Down frame`. On the host the original runs after `card360: host t=<ms> out of bounds expiry on <pawn> (<side>):
KillOwningActor runs`. Rows capped 64 per map per side (reset at the card 322 map end). New symbols
`USTOutOfBoundTriggerComponent_KillOwningActor` and `_GetTimeLeft` in tools/symbols.txt.

## Sibling sweep: direct callers of `InstaKillTarget` (E8/E9 scan, 18 sites)

Authority hits = calls to `AActor::InternalGetNetMode`, `UWorld::InternalGetNetMode`, `APawn::IsLocallyControlled`
or reads of `0x168(` (Role) between the function start and the call site.

| Site | Caller | Authority hits | Player target on a client? | Mod coverage |
|---|---|---|---|---|
| exe+0x50421AB | `execInstaKillTarget` (Blueprint calls) | 0 | any BP caller | none |
| exe+0x50ED239, 0x50ED319, 0x50ED4FD | `ASTDoor::HandleCharactersInMovePath` | 0 | yes | card 321 host part (player0_c321.cpp) |
| exe+0x50F9531 | `ASTPlayerMechBase::OnSelfDestructSequenceComplete` | 0 | mech | card 079 |
| exe+0x511A623 | `ASTEncounterManagerBase::CleanUpEncounter` | 0 | NPCs | none |
| exe+0x515270A | `ASTHazardousZoneBase::CrushActor` | 0 | yes | card 321 table |
| exe+0x5161F78, 0x51626AA | `ASTFallZonesBase::OnBeginOverlap` | 0 | yes | card 338 skips other pawns' overlaps on the client; the own pawn's overlap runs locally |
| exe+0x5170C90 | `ASTTeleporterVolumeBase::StartPlayerTeleport` | 0 | yes | card 321 |
| exe+0x519CF09 | `USTGrenadeDamageVolumeComponent::OnGrenadeExploded` | 0 | NPCs | none |
| exe+0x51AAC63 | `ASTNonPlayableCharacterBase::Tick` | 0 | NPC self | none |
| exe+0x51B1733 (jmp) | `TryTakeCorpseTicket` | 0 | NPC corpse | cards 038/039 |
| exe+0x51CD6E9 (jmp) | `USTOutOfBoundTriggerComponent::KillOwningActor` | 0 | yes | this card |
| exe+0x51DD9BA | `ASTProceduralDropship::ShuntAndCrushBelow` | 0 | yes | none |
| exe+0x51DDAA7 | `ASTProceduralDropship::ShuntAndCrushBelow` | 1 | yes | none |
| exe+0x5209CB1 | `USTCheatManager::KillAllNPCs` | 0 | NPCs | cheat only |
| exe+0x5211D8E | `USTSaveSystemLibrary::PostSavedLevelLoadSetup` | 0 | load | card 143 |

Gap candidates (a player target, no authority test, reachable on a client): the fall zone own-pawn overlap, door,
hazard crush, dropship crush, teleporter, BP exec. Proposed next card (not built here): one class hook at
`InstaKillTarget` that, in a client session, does not run on a player-class target (one row with the caller), so the
host's death sync stays the only writer of a player death for every caller at once.

## Run b45 predictions (FED_C2M1, GOD=0, scenario `tools/scenarios/b45-c360-c2m1.txt`)

Each player warped to a point inside `STOutOfBoundsVolume_0` just above its floor ((211563, 87613, -2740); b44r3
landed there at z -2788 still overlapping), so the countdown, not the fall, ends it. Client first (host Up, so the
client goes Downed), then the host (all down).

1. Countdown row on both sides for each entry, same length on both (config value).
2. Client turn: one `card360: host .. expiry on <copy> (server-copy): KillOwningActor runs`, one `card294: host ..
   Down Downed`, one `card043: client .. own pawn .. Up -> Downed`, and one `card360: client .. (client-own):
   KillOwningActor not run`. 0 `WARN card043: client HandleDeathEvent on its own pawn` rows.
3. Host turn: one `card360: host .. (host-own): KillOwningActor runs`, one `card360: client .. (other): .. not run`,
   the client's `other pawn .. -> ` row from the Down frame, all down.
4. Shots on both screens at each death show the same state. 0 new WARN kinds.

## Run b45 (1 of 3) result

Void at the expiry wait. Countdown row held (prediction 1): `card360: host .. countdown 10.00 s on .. (server-copy)`
and `card360: client .. countdown 10.00 s on .. (client-own)`, 21 ms apart. The pawn then fell out of the volume's
bottom (exited 2.95 s in on both sides, no warp): there is no floor under (211563, 87613); b44r3's z -2788 was
mid-fall. Run 2 (`b45r2`): the same point, re-warped every 1 s for 11 s, so the pawn stays inside until the 10 s
expiry. Predictions 2 to 4 unchanged.

## Run b45r2 (2 of 3) result: all predictions held

Host log `UBWCoop-host-20261114-195135.log`, client `UBWCoop-client-20261114-195137.log`, drive rc 0, 0 FATAL/ERROR.

1. Countdown 10.00 s on both sides at every entry (host and client rows 8 to 28 ms apart).
2. Client turn: host `expiry on .. (server-copy): KillOwningActor runs` at 346345563, `card294: host .. Down Downed for
   pawn id 233993 (slot 1)` 1 ms later, client `own pawn guid 0x262 (host slot 1) Up -> Downed, 30 s bleedout` at
   346345569 (6 ms after the host kill), then client `expiry on .. (client-own): KillOwningActor not run` at 346345570.
   0 `WARN card043: client HandleDeathEvent on its own pawn` rows.
3. Host turn: host `expiry on .. (host-own): KillOwningActor runs` at 346360263 with `Down Dead (slot 0)` and
   `Down Out (slot 1)` (all down); client `expiry on .. (other): KillOwningActor not run` at 346360287; client
   `other pawn guid 0x2ce (host slot 0) Up -> Dead` and `own pawn .. Downed -> Out` at 346360370, then the card 044
   StartDeath and CompleteDeath on the client's own pawn (2995 ms) by the normal path.
4. Shots (prefix2 Screenshots, `*-b45r2-c360-*`): client turn, the client's screen shows Downed (red, health 1), the
   host's view is at the start area with the host Up; host turn, the death screen on both (host: Restart Checkpoint
   active; client: the same menu, dimmed). WARN kinds: known classes only (card 046 warp gap rows from the void hold,
   `UBWCOOP_GOD not "1"` from GOD=0, pose and hit checks, camera guard).

Side note, not this card: the Downed server copy, left in the void, entered the volume twice more (346351646,
346357812) while the host was being held; each countdown ended by an exit before expiry. The card 046 warp gap rows
(1130 and 2485 uu) are the downed copy falling below the host.
