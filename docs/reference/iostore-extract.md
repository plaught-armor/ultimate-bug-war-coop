# IoStore package extraction (Oodle)

Tooling in `tools/iostore/` reads cooked packages out of `pakchunk0-Windows.utoc/.ucas` without retoc or FModel. Senior research only; the mod does not use it.

## Use

```
python3 -I tools/iostore/utoc_paths.py <utoc> <out.txt>          # every package path (docs/reference/asset-paths.txt, local only, came from this)
python3 -I tools/iostore/iox.py <utoc> <ucas> '<regex>' <outdir>  # extract raw zen packages whose path matches
strings -n 5 <outdir>/X.uasset                                    # name map, soft paths, Blueprint node names
```

- Output files are raw zen packages (`.uasset` bodies, not legacy format). `strings` is enough for the name map and soft object paths. Real property values (row structs, defaults) need a zen package parser, which does not exist yet.
- Wine prefix: `$UBW_IOX_WINEPREFIX`, default `~/.cache/ubw-iox-wpfx`. The first run creates it. If a run hangs, a crash has attached winedbg: `WINEPREFIX=<prefix> wineserver -k`.
- Treat extracted files as untrusted data: write them to a fresh directory and read them with `python3 -I`.
- Never point these tools, or any tool that takes an output path, at a glob under `game/`. See memory `game-files-read-only`. Recovery: innoextract from the GOG installer.

## How the decompressor works (`oodec.c`)

The game exe links Oodle statically. `OodleLZ_Decompress` is at RVA `0x068B2B60` (GOG 1.2.0 build 90228; re-find it for another build). `oodec.exe` loads the shipping exe as data (`LoadLibraryExA`, `DONT_RESOLVE_DLL_REFERENCES`). The exe then needs these steps, done by hand:

1. Apply the DIR64 base relocations (the image loads at 0x100030000, not its preferred 0x140000000), then write the new ImageBase into the header. The header is read-only, so `VirtualProtect` it first.
2. Resolve the import table by hand (1010 imports, 0 missing under wine).
3. Call `fn(comp, compLen, raw, rawLen, 1, 0, 0, NULL, 0, NULL, NULL, NULL, 0, 3)` for each block.

Job file format: repeated records `{u32 comp, u32 raw, comp bytes}`. `raw == comp` means a stored (uncompressed) block, which is copied as is.

Build (clang-cl plus the xwin SDK in `toolchain/xwin`):

```
X=toolchain/xwin; clang-cl --target=x86_64-pc-windows-msvc /O2 /MT -imsvc $X/crt/include -imsvc $X/sdk/include/ucrt -imsvc $X/sdk/include/um -imsvc $X/sdk/include/shared oodec.c -fuse-ld=lld /link /libpath:$X/crt/lib/x86_64 /libpath:$X/sdk/lib/um/x86_64 /libpath:$X/sdk/lib/ucrt/x86_64 /out:oodec.exe
```

## utoc layout (version 8, as parsed)

- Header: 144 bytes. Flags 0x9 (compressed and indexed; not encrypted or signed). Block size `cbs` is in the header.
- After the header: chunk IDs (12 bytes each); offset/lengths (10 bytes: 5-byte big-endian offset, 5-byte big-endian length, both in uncompressed space); perfect-hash seeds (4 bytes each); compression blocks (12 bytes: 5-byte LE `.ucas` offset, 3-byte compressed size, 3-byte uncompressed size, 1-byte method index); compression method names; then the directory index.
- Directory index: FString mount point (`../../../`); directory entries `{name, firstChild, nextSibling, firstFile}`; file entries `{name, nextFile, tocIndex}`; string table. `0xFFFFFFFF` means none.
- A chunk covers blocks `off/cbs` through `(off+len-1)/cbs`; its data starts `off % cbs` bytes into the first decompressed block.

## Own Oodle implementation

Notes are in `docs/reference/oodle-own-impl.md`.

## Map name-map scan

`iox.py ... '\.umap$' <out> 524288` extracts the first 512 KiB of all 83 maps in about 10 s (the full maps are 1.5 GB and time out). The name map at the start of each package lists the placed actor instance names (`BP_MechDropPlatform_C_6`) and the Blueprint classes used, so `strings | grep` over the headers finds which maps place a class.

Limit: zen packages store imports as hashes, not names. A Blueprint call to a native function shows in the name map only through its pin names (`CallFunc_<Name>_ReturnValue`), so a function with no return value is invisible. Confirm such calls at run time.
