# Card 380: solo load of a co-op save leaves the client's saved pawn in the world

Status: accepted 2026-11-15 (run `c379b67`, b67 `cc1d4251720f389d`).

## Defect

A save taken with two players holds two player pawn rows. Each row stores the index of the pawn's controller in the
world's player controller list (`SaveOwnerVariables_Pawn`, exe+0x52170B0; trace in card 375). On load,
`LoadOwnerVariables_Pawn` (exe+0x520B820) possesses the pawn with the PC at that index. With fewer players than at
the save there is no PC at the index: the pawn stays, loaded and armed, with no controller.

Evidence, run `c379b65` (b65 `3676f5d1b88c01de`): save with host at (6554, 11459, 97) and client at
(6254, 11459, 97); client quits; host `travel checkpoint`. The client's saved pawn (save id `83804E24-...`) spawns at
(6254, 11459, 97) with no `PossessedBy` row, gets `ActorFullyLoaded` and `LoadAbilitySystemComponent`
(weapons 0 -> 1, MoritaMk1), and is never destroyed (0 `card098 ... destroy BP_PlayerCharacterMI_C` rows).

## Fix (`mod/src/ubw/death.cpp`, `detour_load_owner_pawn`)

Host only. Before the original runs, `c380_saved_possessed` peeks the row's first int ("held by a controller at save
time", read at exe+0x520B861 through the archive fast-path buffer at archive+0x8). After the original, a player
character whose row said possessed and whose Controller (pawn+0x2D8) is null is destroyed through
`detour_c169_destroy` (K2_DestroyActor path, so card 169 bookkeeping runs), with a `card380:` row. Rows saved
unpossessed (player in a mech or the dropship) are left alone. The engine's own branch in the same function destroys
the pawn too (savecomp+0x160 zero), so later load steps already tolerate it.

Not hit: hangar load with a client (card 375; the client's row is possessed by the old client PC, Controller non-null)
and co-op in-place loads (PC present at the index).

## Predictions for `c379b66`

1. One `card380: host ... destroyed` row after the host's `travel checkpoint`, for the pawn at (6254, 11459, 97).
2. No `ActorFullyLoaded`-time crash; 0 FATAL/ERROR; no new WARN class against `c379b65`.
3. Shot from about (6254, 11159) looking at (6254, 11459, 97): no trooper there.
4. Sibling `c377` (hangar load with client): 0 `card380` rows, result as `c377b65`.

## Run c379b66 (b66 `4d6b30d4225de8b1`): fix did not fire

The checkpoint load's archive has no fast-path buffer (the engine reads the int through the virtual Serialize), so
the peek returned "not readable": 1 `WARN card380 ... left in the world`, the orphan stayed (shot `h-orphan`). No
crash, 0 FATAL/ERROR. Fix 2 (b67): when the fast buffer has no room the peek calls the archive's own Tell (+0x28),
Serialize (+0x180), then Seek (+0x1B0) back; slots read from the FMemoryReader vtables in the exe. Predictions for
`c379b67` are the same four, plus 0 `WARN card380` rows.

## Run c379b67 (b67 `cc1d4251720f389d`): pass

1 `card380: ... destroyed` row for the pawn with save id `8FB85743` at (6254, 11459, 97); 0 `WARN card380`; no row
of any kind for that pawn afterwards (b66: ActorFullyLoaded and LoadAbilitySystemComponent, weapons 1). Host's own
pawn possessed by the same PC as before the load, stand 0 uu at (6554, 11459, 97). 0 FATAL/ERROR, WARN set as
`c379b65`, 0 `card382` rows. Shot `h-orphan`: nothing at the look point (an AI squad trooper stands about 28 degrees
right, respawned by the load). Junior's review change: a null fast-path pointer also takes the slow peek.
Prediction 4 (`c377` on b67) is run separately.
