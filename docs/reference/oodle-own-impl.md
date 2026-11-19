# Own Oodle decoder: notes (not started)

The user raised writing our own decoder (2026-11-03). The current route works, uses the game's own exact decoder, and costs only wine startup per batch. So an own implementation is only worth it if wine becomes a problem (CI, no wine, a large batch).

- The game's Oodle codec per block is the method byte; check which codecs actually appear (Kraken, Mermaid, Selkie, Leviathan) before scoping work.
- The open-source reverse-engineered decoder `ooz` (powzix) covers Kraken, Mermaid, Selkie, Leviathan and LZNA. A port or a binding is the shortest route; check its licence before vendoring it.
- Validation: decode every block both ways (the game exe through `oodec.exe`, and the new decoder) and compare the bytes.

Current route: `docs/reference/iostore-extract.md` (`tools/iostore/oodec.exe` calls the game's own `OodleLZ_Decompress` under wine).
