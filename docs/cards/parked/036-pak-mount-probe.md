# Card 036: Does the game mount an extra container?

Status: draft (senior, 2026-11-03). Send only after the user says go and the junior has been cleared.

Read `docs/STATE.md` first ("Mod code rules"), then `docs/reference/iostore-extract.md` and memory note `game-files-read-only`. Repo root: `/mnt/based_backup/Repos/ultimate-bug-war-coop`.

## Why

Custom sprite art (a real crouch, crouch-fire, a co-op HUD icon) needs new textures in the game. Building textures at run time means heap allocation in the mod, which the code rules forbid. If the game mounts an extra pak or IoStore container that we ship, new art can be cooked data instead. This card only answers whether a mount happens and which file wins; it ships no art.

## Hard rules for this card

- **Do not use retoc**, and do not download any other pak tool. Write what you need in `tools/iostore/`, in Python run with `-I`.
- **The game's files are read-only.** The only change allowed under `game/` is adding new files whose names start with `zz_ubwcoop` (or a new `~mods/` folder holding only such files). Never open an existing game file for writing, never pass a game file or a glob as an output argument (`xxd`, `cp`, `dd`, `>`), and delete the added files at the end. List the folder before and after, with sizes, in the report.
- If any game file changes by mistake, stop and report. Recovery is innoextract from the GOG installer (see the memory note).

## Static leads (senior)

- `game/StarshipTroopers_UBW/Content/Paks` holds `global.utoc/.ucas` and `pakchunk0-Windows.pak/.utoc/.ucas`. There are no `.sig` files. The utoc header flags are 0x9 (compressed, indexed; not encrypted, not signed).
- Natives (PDB publics): `FPakPlatformFile::Mount` 0x023B4310 and 0x023B50F0 (two overloads), `FPakPlatformFile::MountAllPakFiles` 0x023B5130 and 0x023B5180, `FPakPlatformFile::GetPakFolders` 0x023AAF70.
- The mod DLL may load after `MountAllPakFiles` has run, so a detour may see nothing. Check first, and say which method you used: a detour, a call to `GetPakFolders` to log the folders searched, or reading the platform file's mounted-pak list.
- In UE5, cooked assets load through IoStore (`.utoc/.ucas`). A `.pak` on its own carries only non-asset files. A container that overrides a package needs a `.pak` + `.utoc` + `.ucas` triple with the same base name. Higher pak order wins, and the `_P` suffix raises the order.
- `pakchunk0-Windows.pak` footer (read by the senior, read-only): magic 0x5A6F12E1 at 204 bytes from the end, version 11, index at offset 143183304 (89874 bytes), encryption key GUID all zero and the encrypted-index flag 0, compression method list `Oodle`. A version 11 pak with stored (uncompressed) entries and an empty compression list should be accepted.
- `tools/iostore/` has readers only (`utoc_paths.py`, `iox.py`). There is no writer yet.

## Do

1. **Find the search folders.** Log the folders the game scans for paks (`GetPakFolders`, or the strings near `MountAllPakFiles`) and whether `~mods` is one of them.
2. **Mount test with no assets.** Write a minimal legacy `.pak` (the version the existing `pakchunk0-Windows.pak` uses; read its footer) holding one small text file, for example `StarshipTroopers_UBW/zz_ubwcoop_probe.txt`. Name it `zz_ubwcoop_P.pak` and put it in the first search folder. Report whether the game mounts it (log line or mounted list) and whether the game still starts and reaches the main menu.
3. **Override test (only if step 2 mounts).** Write an uncompressed IoStore container (`zz_ubwcoop_P.utoc/.ucas` beside the `.pak`) carrying one existing package, extracted with `iox.py`, unchanged except one visible change. Pick something small and easy to see at the main menu, such as a UI texture, and say which. Report whether the change appears, with a screenshot. If writing the container takes more than a few hours, stop after step 2 and report what the writer still needs.
4. **Clean up.** Delete every added file and confirm the game starts as before.

## Answer, with log lines or screenshots

- Which folders are searched, and does a `zz_ubwcoop_P.pak` get mounted?
- Does an IoStore container we wrote override a game package?
- Is a pak signature or a container check enforced anywhere (log any refusal text)?

## Done when

- The questions above are answered, and the Paks folder listing after cleanup matches the listing before.
- Any mod code is behind `UBWCOOP_PAKPROBE=1`, off by default. Clean build (`/W4 /WX`, `--clean-first`), 84 exports, KERNEL32-only imports. No new crash dumps. Every new function is at most 60 lines.

## Risks

- A bad container can crash the game at start. Run single player, windowed, and remove the files right after each test.
- The override may need the package to match the cooked hash or the global shader and script-object data in `global.utoc`. If it refuses, report the message rather than patching around it.
