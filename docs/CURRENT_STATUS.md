# Chronon3D — Current Status

## GPU checkpoint — 2026-08-19/20

- GPU-native 1920x1080 960f watermark+subtitle: **4.11s** vs NVDEC→NVENC 2.53s (+62%), CPU 29%, `gpu_readback_bytes=0`, `fallback=0`, `effective_backend=vulkan`.
- Residuo: D2D Vulkan→CUDA→NVENC (`gpu_surface_copy_frames` + `encoder_staging_copy_bytes`), zero-copy NVDEC→NVENC ancora da chiudere (surface importabile).
- CLI/IPC: daemon caldo 10.09s vs CLI 13.17s, 100 job x150f stabili `1059s`, probe CUDA/Vulkan `CUDA_VULKAN_INTEROP_PASS` su RTX A4000, `hwmap=derive_device=vulkan` ancora FAIL.

> Ultima baseline certificata: `main@7eb5c2ba` 11/11 PASS (2026-07-06). HEAD `main@8aad8e00f` worktree sporco — developer gates 20/20 PASS, architecture 26/26, ma CTest fast 92 test mancano binari, native decoder cache FAIL, no baseline same-SHA. Dettaglio: `docs/baselines/main-7eb5c2ba-baseline.md`.

## Active Blockers

| ID | Area | Stato | Scheda |
|---|---|---|---|
| TICKET-125-TEST-AGGREGATOR | testing | OPEN | [TICKET-125](tickets/TICKET-125-test-aggregator.md) |
| TICKET-DEPRECATED-API-REMOVAL | API | OPEN | [DEPRECATED-API-REMOVAL](tickets/TICKET-DEPRECATED-API-REMOVAL.md) |
| TEST-FONT-ASSET-PATH / CERT-SEQUENCE-WBH | testing/cert | OPEN | [TEST-FONT-ASSET-PATH](tickets/TICKET-TEST-FONT-ASSET-PATH.md) |

Indice completo: [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md).

## Stato per area (sintesi)

| Area | Stato | Note |
|---|---|---|
| CLI V3 / RenderPlan | PASS | `PreparedRenderPlan → CompiledComposition` + `render --plan` canonico |
| Camera V1 | PARTIAL | 9/9 visual + 64/64 suite + 406/406 scene PASS; manca baseline same-SHA + sanitizer |
| Text Core V1 | PASS | FreeType/HarfBuzz/FriBidi + layout/cache/animator certificati |
| Text Production | PARTIAL | 20+8 preset, 192/192 subtitle PASS, manca corpus CapCut PNG |
| GPU Production V1 | PARTIAL | Vulkan native + CUDA interop probe PASS, D2D copy residua |
| Executor / Tile-prune | P2 OPEN | `tile_prune_skip_unification` tracked |
| SDK C++ / C ABI v2 | PASS (focused) | `install_consumer` 14/14 + `CHRONON_ERROR_ASSET_CHANGED` |
| Video pipeline | PASS | 13 codici errore, atomic output, 98 test |
| CI infrastructure | FAIL | gate WIRED, run recenti non verdi |
| Test coverage | PARTIAL | 39/39 golden + 1423/1423 core --no-skip PASS, manca globale |
| Packaging | PASS (locale) | `verify_packaging_linux.sh` 14/14 |
| Determinism | PASS | seriale/parallelo + cold/warm SHA identici |
| Next Steps | PLANNED | vedi [`docs/CHRONON_PLAN.md`](CHRONON_PLAN.md) |

## Gate Audit

- `main@7eb5c2ba` **11/11 PASS** certificata. `main@ef9c83f1` 14/14+1 BLOCKED. HEAD non certificato.
- Dettaglio storico in `docs/CHANGELOG.md` + `docs/CHANGELOG.archive.md`.

## Link canonici

- [`docs/ROADMAP.md`](ROADMAP.md) — milestone
- [`docs/RELEASE_GATE.md`](RELEASE_GATE.md) — requisiti release
- [`docs/FOLLOWUP_TICKETS.md`](FOLLOWUP_TICKETS.md) — blocker
- [`docs/CHRONON_PLAN.md`](CHRONON_PLAN.md) — next steps operativi
- [`docs/baselines/main-7eb5c2ba-baseline.md`](baselines/main-7eb5c2ba-baseline.md) — baseline verde

## Hygiene

`branch.main.rebase=true` + `tools/wrap_push.sh` + `tools/check_main_clean.sh` (GATE-MNT-01).
