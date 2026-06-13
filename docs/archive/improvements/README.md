# Chronon3d — Suggested Improvements (split)

This directory contains the split version of the monolithic
`docs/SUGGESTED_IMPROVEMENTS.txt` (originally 1849 lines, now broken
into 5 navigable files).

## Index

| # | File | Content | Lines |
|---|---|---|---|
| 0 | [`00_INDEX.md`](00_INDEX.md) | Header, current state, target, table of contents | 52 |
| 1 | [`01_TIER1_GAME_CHANGERS.md`](01_TIER1_GAME_CHANGERS.md) | Items 1-3 — tile rendering, frame graph compiler, tile surface cache (10-100× impact) | 452 |
| 2 | [`02_TIER2_QUICK_WINS.md`](02_TIER2_QUICK_WINS.md) | Items 4-19 — blur/SIMD/SPSC/sharding/prefetch/affinity (2-10× impact) | 965 |
| 3 | [`03_TIER3_LONG_TERM_VISION.md`](03_TIER3_LONG_TERM_VISION.md) | Items 20-29 — GPU backend, ECS, RDG, HarfBuzz, MSDF, CI multi-OS | 303 |
| 4 | [`04_PRIORITY_AND_PRINCIPLES.md`](04_PRIORITY_AND_PRINCIPLES.md) | Recommended execution order + 7 architectural principles | 77 |

## Reading order

- **If you have 5 minutes:** read `00_INDEX.md` then `04_PRIORITY_AND_PRINCIPLES.md`.
- **If you have 30 minutes:** add `01_TIER1_GAME_CHANGERS.md`.
- **If you have 1 hour:** read everything in order.

## Why split?

The monolithic file made it hard to:
- Reference a specific item from a commit message or PR description
- Diff changes between reviews (one section vs whole file)
- Print or navigate on mobile / IDE markdown preview
- Distribute the architectural principles independently

The split preserves the original 29-item structure verbatim, only the
container changes.

## See also

- `../IMPROVEMENTS.md` — resolved improvements (status, files, dates)
- `../MIGLIORAMENTI.md` — Italian-language changelog
- `../ORIENTATION.md` — Italian-language onboarding guide
