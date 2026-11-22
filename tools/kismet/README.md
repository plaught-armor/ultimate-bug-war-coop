# tools/kismet: Blueprint bytecode listings from the game's IoStore packages

Recovered on 2026-11-19 from the session transcripts that wrote them (2026-11-08, sessions c3fda9b0 and 62996b75).
They had lived only in session scratchpads (`kis/`, `kis2/`), which the prune timer deleted. Keep them here.

| File | What it does |
|---|---|
| `zen.py` | Reads a raw zen package (UE5 IoStore): names, imports (hashes), exports. `python3 -I zen.py X.uasset` lists exports. |
| `kis.py` | Kismet disassembler. `python3 -I kis.py X.umap [function ...]` prints every function's statements with bytecode offsets (`8598: if not (inStadium) goto 8891`). It uses the kis2 fixes: an FName in script is 12 bytes in memory. |
| `city.py` | CityHash64, for resolving import hashes to names (`resolved.txt`, optional, next to kis.py; without it imports print as `impN:<hash>`). |
| `bpreach.py` | Card 138: walks ReceiveBeginPlay's synchronous flow in a kis.py listing and prints the reads of the local player's pawn. |

## Getting a package

Extract with `tools/iostore/iox.py` into a gitignored dir (never the scratchpad, never the game folder):

    P=game/StarshipTroopers_UBW/Content/Paks
    python3 -I tools/iostore/iox.py $P/pakchunk0-Windows.utoc $P/pakchunk0-Windows.ucas 'FED_C4M1_Scripting\.umap$' build/kis
    python3 -I tools/kismet/kis.py build/kis/FED_C4M1_Scripting.umap > build/kis/c4m1.txt

Check: the C4M1 listing has `8740: imp854:...(self, name:Stadium_O1, name:Stadium_SO2)`, the offset card 244 cites.

## Reading a level script

- `=== BndEvt__..._BP_PlayerPresence_C_5_..._VolumeTriggerDelegate...` entries are bound events. Each jumps into the
  ubergraph at an offset (`ExecuteUbergraph_<map>(5729)`); read the ubergraph from that offset.
- Known imports: imp848/imp849 AddSubObjective (count variants), imp853 CompleteObjective, imp854 CompleteSubObjective,
  imp855 the counter step, imp856 SetObjectiveVisibility (card 244).
- `CallFunctionWithDelay(10.0, 'Add Stadium_SO4')` is a retry loop: the step waits on a script flag, not a timer.
