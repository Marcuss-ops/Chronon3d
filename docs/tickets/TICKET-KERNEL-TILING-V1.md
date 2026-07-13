# TICKET-KERNEL-TILING-V1 — Kernel-level parallelism via tiling (`for_each_tile`)

## Stato: OPEN-APERTURA (2026-07-13, FASE 4.3 scaffold commit)

## Problema

I 100+ `tbb::parallel_for(tbb::blocked_range<...>, grain)` direct call
sites disseminati in `src/backends/software/{kernels,utils/effects}/`
operano tipicamente su **range di righe** (`blocked_range<int32>(0,
height, grain)`) — ma per i kernel pesanti (blur, glow, resample +
compositing) il pattern row-major non sfrutta il tile-cache L2 del CPU
moderno (tipicamente 256-512 KB / core). La penalità è misurabile su
scene 4K: B03 CinematicGlow1080p + B05 Blur4K mostrano speedup
< 1.7× a 2 thread e < 3.0× a 4 thread invece del potenziale ~2×/4×.

F4.3 (user-spec verbatim) introduce un'API unificata:

```cpp
scheduler.for_each_tile(dirty_bbox, TileSize{128, 64}, [&](Tile t){
    blur_kernel(t, src, dst);
});
```

con routing automatico:
- `tile_count() == 0` → no-op (B00 EmptyFrame, dirty-rect vuoto).
- `tile_count() == 1` → serial fast-path (single tile, no TBB overhead).
- `tile_count() >= 2` → `tbb::parallel_for` via `parallel_for_tracked`
  (mantiene `tbb_active_workers_peak` telemetry invariata).

L'invariante §honesty-critical è: la **unione** di tutti i tile
ricevuti dal body `body(Tile)` deve coprire il `dirty_bbox`
**esattamente** (no overlap, no gap, edge tiles possibly smaller at
right/bottom).

## Soluzione (scaffolding commit + forward-points)

### 6-file change-set

**NEW** `include/chronon3d/core/scheduler/tile_size.hpp` (~80 LoC):
- `struct TileSize { int width; int height; }` — user-spec verbatim
  brace-init shape.
- `struct Tile { int x, y, w, h; std::size_t index; }` — per-tile
  payload (cheap to copy via pass-by-value).
- `constexpr std::size_t compute_tile_count(BBox, TileSize)` — pure
  math, `ceil(bbox.w / ts.width) × ceil(bbox.h / ts.height)`.
- `constexpr Tile compute_tile(BBox, TileSize, std::size_t i)` —
  row-major iteration `i = ty * nx + tx`.

**NEW** `include/chronon3d/core/scheduler/for_each_tile.hpp` (~70 LoC):
- `template <typename Fn> inline void for_each_tile(BBox, TileSize, Fn)`
  nel namespace `chronon3d::scheduler` (free function).
- Routing automatico: empty → no-op; tile_count == 1 → serial;
  tile_count >= 2 → `parallel_for_tracked`.

**EDIT** `include/chronon3d/core/scheduler/execution_scheduler.hpp`
(+~20 LoC):
- Aggiunge **`template <typename Fn> void for_each_tile(BBox, TileSize,
  Fn)`** come metodo delegator che wraps `m_arena.execute(...)` (per
  Sequential-mode isolation dell'audit §9.10) e poi chiama il free
  function `chronon3d::scheduler::for_each_tile`.
- `#include <chronon3d/core/scheduler/for_each_tile.hpp>` aggiunto al
  fondo del header.

**NEW** `tests/kernel/test_kernel_tiling.cpp` (~200 LoC, 9 TEST_CASEs):
- Tile count per 3 scenari (cleanly-aligned / unequal-split / empty).
- Tile coverage **EXACTNESS** invariant (union-of-tiles == bbox).
- Edge tiles correctly smaller quando bbox dimension non multiplo.
- Single-tile bbox (serial fast-path condition).
- Bbox exactly tile-aligned (no edge tiles).
- Non-zero-origin bbox (absolute coordinates).
- Bitmap-traced union covers bbox area exactly (pixel-level).

**NEW** `docs/tickets/TICKET-KERNEL-TILING-V1.md` (this file).

**EDIT** `docs/CHANGELOG.md` — F4.3 entry prepended at TOP per Cat-5
newer-at-top + Cita-Only ticket-link.

### AGENTS.md compliance

- **Cat-3 minimal-surface**: 2 NEW ABI structs (Tile, TileSize) + 1
  NEW template free function + 1 NEW delegating method. NO new
  singleton/registry/resolver/cache. The free function uses
  `parallel_for_tracked` (existing wrapper) — no new TBB machinery.
- **No ADR required**: per AGENTS.md "no nuovi singleton/registry/
  resolver/cache senza ADR" rule, the `for_each_tile` API does NOT
  introduce a new singleton. The free function uses the canonical
  `parallel_for_tracked` (already ADR-anchored via parallel-precedent
  audit §9.10 Pattern). The ExecutionScheduler delegator extends an
  existing class (ABI-additive only).
- **Cat-5 2-doc same-commit**: this file + `docs/CHANGELOG.md`
  EDIT co-updated (per AGENTS.md ticket-home rule + §Disciplina di
  aggiornamento dei canonici).
- **Subject envelope**: `feat(scheduler): for_each_tile kernel-level
  tiling (KERNEL-TILING-V1)` = 64 chars ≤ 72 ✓.

### §honesty-limitation disclosure (B03/B05 speedup gates)

User spec: "Misura speedup su B03 CinematicGlow1080p + B05 Blur4K:
gate iniziale 1→2 ≥1.7x, 1→4 ≥3.0x, 1→8 ≥5.5x".

Per `TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV` precedent, macchina-
verifica B03/B05 è DEFERRED-WBH (this VPS lacks vcpkg glm/magic_enum
+ tmpfs + rot-cascade; `chronon3d_cli` not linkable). Forward-point
(a) below is the canonical implementation chore; this commit delivers
the SCAFFOLD ONLY.

## Criteri di accettazione

- [ ] 6-file SCAFFOLD landed on main (commit subject envelope ≤ 72).
- [ ] All 9 TEST_CASEs in `tests/kernel/test_kernel_tiling.cpp` pass
  on working build host (VPS-macchina-verifiable: doctest + tile
  math runs without `chronon3d_cli` binary).
- [ ] Tile coverage EXACTNESS invariant enforced by
  `tests/kernel/test_kernel_tiling.cpp::union-of-tiles == bbox
  exactly`.
- [ ] No new symbols beyond `TileSize`, `Tile`, free function, and
  method (Cat-3 minimal-surface audit).
- [ ] No `static inline` misuse (AGENTS.md `static\s+inline\s+\w+`
  audit) — `for_each_tile` is a free `inline` template.
- [ ] No `-march=native` (per ADR-025 prohibition, anchored in
  ADR-025).
- [ ] 11/11 baseline suite verde on working build host (range:
  currently 12-16/11).
- [ ] B03/B05 speedup gates (1→2 ≥1.7x, 1→4 ≥3.0x, 1→8 ≥5.5x) per
  forward-point (a) below.

## Out of scope (forward-pointed)

- **Wiring dei kernel esistenti** (blur_kernel / glow_kernel /
  resample_kernel) al nuovo `for_each_tile` API: deferred a forward-
  point (a).
- **B03/B05 macchina-verifica speedup gates**: deferred a forward-
  point (b).
- **Per-tile deterministic-hash tracing** per copertura diagnostica
  (parallel precedent: TileTelemetryRecord esistente in
  `include/chronon3d/runtime/telemetry/render_telemetry_record.hpp`):
  forward-point (c).

## Forward-points

(a) **`TICKET-KERNEL-TILING-IMPLEMENTATION`** — migrazione dei 3
kernel pesanti (blur + glow + resample) al `for_each_tile` API:

  - `src/backends/software/utils/effects/effect_blur.cpp`: `tbb::parallel_for`
    su righe → `for_each_tile(bbox, {128, 64}, [&](Tile){ ... })`.
  - `src/backends/software/utils/effects/glow_pipeline.cpp`: idem.
  - `src/backends/software/utils/blend2d_bridge_transforms.cpp` +
    `transforms_fb.cpp` + `core.cpp`: resample path.

(b) **`TICKET-KERNEL-TILING-MACHINE-VERIFY`** — full B03 + B05
macchina-verifica end-to-end su working build host:

  - `bash tools/measure_cpu_budget.sh` extended per loggare 1/2/4/8 thread
    speedup per `BenchB03_CinematicGlow1080p` + `BenchB05_Blur4K`.
  - Assert gate (1→2 ≥1.7x, 1→4 ≥3.0x, 1→8 ≥5.5x) emitted via
    `[INFO] check_tile_tiling_speedup:` line pattern (per AGENTS.md
    INFO-level diagnostic style).
  - macchina-verifica PARTIAL on this VPS per VPS env-block.

(c) **`TICKET-KERNEL-TILING-DETERMINISTIC-HASH`** — per-tile SHA-256
hashing integrato con `parallel_for_tracked` telemetry: every tile
dispatch records `{tile_index, tile_x, tile_y, tile_hash}` to
`~/chronon3d/telemetry/bench_tiling_${ts}.jsonl` for cross-ISA
parity debugging.

(d) **`TICKET-KERNEL-TILING-CACHE-COHERENCE`** — ADR per la
tile-cache-key (l'invariante "tile hash matches tile coordinates + tile
size") + integration con `NodeCacheKey.tile_x/tile_y/tile_size`
esistenti nel cache layer.

(e) **`TICKET-KERNEL-TILING-DETERMINISM-MATRIX`** — estendere
`tests/determinism/test_tile_determinism.cpp` per coprire
`compute_tile + for_each_tile`-based dispatch con gli stessi scenari
deterministi (TBB chunk boundaries, Sequential mode).

(f) **`TICKET-KERNEL-TILING-3DOC-CAT5-ALIGN`** — Cat-5 3-doc chaser
after `(a)` landed (CHANGELOG + FOLLOWUP + CURRENT_STATUS update
post-macchina-verifica).

## Cross-link

- F4.2 (TICKET-CPU-BUDGET-UNIFIED-V1) — `CpuBudget::render_threads`
  controls the TBB pool that `for_each_tile` parallelizes across.
- F5.1 (TICKET-SIMD-REGISTRY-V1 `98245ab4`) — `PixelKernelSet::Blur/Glow/
  Resample` fn-pointers are the per-tile work; `for_each_tile` is the
  outer dispatcher.
- Existing `include/chronon3d/parallel_for_tracked.hpp`: canonical TBB
  wrapper (used by `for_each_tile` for telemetry).
- Existing `include/chronon3d/core/tile_grid.hpp`: legacy grid-tiling
  types (TileRect, TileCoord, TileGrid); orthogonal to `Tile/TileSize`
  (kernel-dispatch surface only).
- V3_BLUEPRINT.md `TileScheduler (P9) | Planned` — orthogonal future
  scope; `for_each_tile` is the pattern-level kernel dispatcher
  (P-tier pre-cursor).

## Caller-discipline contract

Per AGENTS.md "NON imporre scaling uniformemente: scene seriali
(B00 EmptyFrame) devono rimanere seriali": il caller decide se
invocare `for_each_tile` (per kernel pesanti: blur, glow, resample)
oppure loop seriale (per scene seriali: B00 EmptyFrame, dirty-rect
vuoto). Il scheduler API NON auto-decide serial-vs-parallel in base al
contenuto della scena — la scelta è esplicita al call-site. Forward-
point (a) documenterà la caller-discipline matrix per i 100+ existing
kernel sites.

## Status

Scaffold 2026-07-13 (post-F4.3 chore commit on `main`).
macchina-verifica PARTIAL-WBH per VPS env-block.
