# Vendored upstream components (StargateTC source)

This file tracks third-party code compiled into StargateTC game libraries. Per-component
detail lives in the subtree; binary redistribution notices ship under `release/notices/`.

| Component | Location | Upstream detail |
|-----------|----------|-----------------|
| jk_botti | `bots/jk_botti/` | `bots/jk_botti/UPSTREAM.md` |
| jk_botti zlib | `bots/jk_botti/zlib/` | Jean-loup Gailly & Mark Adler; see `release/notices/zlib.txt` |
| Metamod (via jk_botti) | headers/shims in `bots/jk_botti/metamod/` | Will Day; see `release/notices/metamod.txt` |

## Delta — native jk_botti (PR #144 / ROW-284)

- **Integration:** jk_botti is linked into `hl.so` / `hl.dll` (not loaded as a Metamod plugin).
- **Pinned upstream:** `b8d46629d330928b36f3a523d5f1c1f21b83c628` (Bots-United/jk_botti).
- **SGTC hooks:** `dlls/sgtc_bots.cpp` — lifecycle, cvars, gate/teleport nav links.
- **Release notices added:** `release/notices/jk_botti.txt`, `zlib.txt`, `metamod.txt` (copied with tagged payloads).
