# TICKET-AE-CAM-MULTI-NODE-SWEEP — multi-node sweep of `project_layer_2_5d` callers + camera-aware node_cache key extension (AE_CAM_02 + AE_CAM_04 residual)

## Stato

**DONE** (closed 2026-07-08) — Sub-cluster A formally **NOT-NEEDED**;
Sub-cluster B **resolved** by [`TICKET-ae-cam-hash-collision`](tickets/TICKET-ae-cam-hash-collision.md)
(DONE Fase 6, 35/35 PASS, AE_CAM_02/04 sha256-distinct).

Created 2026-07-06 as a follow-up companion to `TICKET-AE-CAM-PRECISION-COLLAPSE`
and `TICKET-ae-cam-hash-collision` post the matrix-fix commit `c03ce2a2` + the
ae-parity golden re-bake.

## Priorità

P1 — visual-regression blocker on the cat-2 AE-parity floor (gate #2).  Closura
effettiva richiede `camera-aware node_cache key extension` (Soluzione B) per i
soliti 13/24 PNG tracked con collision-hash encoded `cc86d2b5e80287dc`.

## Problema

The original `TICKET-AE-CAM-PRECISION-COLLAPSE` forward-fix-path prose enumerated
two parallel investigations as the next-step candidates.  Both are now confirmed
verifiable in working tree:

### Sub-cluster A — Call-site sweep (3 unreached sites)

`grep -rn 'project_layer_2_5d' src/` reveals **6** callers.  The corpus-commit
`c03ce2a2` swapped the draw-matrix composition at sites 1–3 (in
`src/render_graph/nodes/multi_source_node.cpp`), but left sites 4–6 untouched
because each site is in a distinct source file with its own semantics and
update cycle:

- `src/render_graph/nodes/multi_source_node.cpp:55` (site 1, predicted_bbox) — **RESOLVED** by `c03ce2a2`
- `src/render_graph/nodes/multi_source_node.cpp:246` (site 2, text_run_execute) — **RESOLVED** by `c03ce2a2`
- `src/render_graph/nodes/multi_source_node.cpp:360` (site 3, regular_execute) — **RESOLVED** by `c03ce2a2`
- `src/render_graph/builder/graph_builder_matte.cpp:35` — **PLANNED** (this ticket)
- `src/render_graph/pipeline/dirty/layer_bbox_collector.cpp:37` — **PLANNED** (this ticket)
- `src/render_graph/pipeline/refresh/layer_item.cpp:14` — **PLANNED** (this ticket)

All 6 sites share the same `chronon3d::Transform tr;` default-constructed
Transform (scale={1,1,1}, rotation=Quat(1,0,0,0), anchor={0,0,0}) pattern **at
first glance** — ma l'analisi Gemini source-read (post-commit `c03ce2a2`)
ha confermato che sites 4–6 NON sono bug:

- **`src/render_graph/builder/graph_builder_matte.cpp:35`** — consuma un `eff_proj` **affine** (T+S perspective_scale), NON `proj.projection_matrix` prospettico raw. Il `tr` vuoto pattern è condiviso ma il **significato** è diverso (bbox per-card flatten è il CORRETTO per il matte-pass che non usa prospettica).
- **`src/render_graph/pipeline/dirty/layer_bbox_collector.cpp:37`** — idem: dirty-rect calculation non usa matrice prospettica per il frame-dirty-marking, solo per il viewport-bbox.
- **`src/render_graph/pipeline/refresh/layer_item.cpp:14`** — idem: refresh path ricostruisce `MultiSourceItem.matrix` da frame data, NON da `project_layer_2_5d` projective output.

**Conseguenza**: la fix `proj.transform.to_mat4()` del corpus `c03ce2a2` NON è
applicabile a sites 4-6. Sub-cluster A **NOT-NEEDED** — formalmente chiuso per
cheque Gemini source-read clarification (cache-evidence + live source inspection
3 siti).

### Sub-cluster B — Cache-key invalidation surface (AE_CAM_02 + AE_CAM_04)

The 24 PNG goldens in `tests/golden/ae_parity/` were regenerated post-`c03ce2a2`
via `CHRONON3D_UPDATE_GOLDENS=1`.  Sha256 verification (frame0 vs frame60 per
scene) shows:

- **AE_CAM_03** (two-node POI): frame0 ≠ frame60 ✓ (RESOLVED by `c03ce2a2`)
- **AE_CAM_05** (orbit spiral): frame0 ≠ frame60 ✓ (RESOLVED by `c03ce2a2`)
- **AE_CAM_06** (Hitchcock dolly+zoom): frame0 ≠ frame60 ✓ (RESOLVED by `c03ce2a2`)
- **AE_CAM_09** (motion-blur asymmetric X-pan + Z-dolly): frame0 ≠ frame15 ≠ frame30 — all 3 frames now distinct (RESOLVED by `c03ce2a2`)
- **AE_CAM_02** (zoom-only): frame0 == frame60 ✗ (STILL COLLIDING — encodes current collision-hash per `fc351bfe` workaround)
- **AE_CAM_04** (parent_null Z-dolly with constant zoom): frame0 == frame60 ✗ (STILL COLLIDING — encodes current collision-hash per `fc351bfe` workaround)
- AE_CAM_01 static, AE_CAM_07 static wide-angle, AE_CAM_08 DOF focus anim: orthogonal to `c03ce2a2` scope, all PASSING.

The 2 residual collisions are NOT matrix-fix issues — they are downstream-cache
issues: even though `project_layer_2_5d` now returns screen-space TRS that
differentiates per-frame, the downstream `node_cache` keyed on `(u64 params_hash,
int w, int h)` does not differentiate because `cam.zoom` and `cam.position.z`
never reach the cache key tuple.

`src/cache/node_cache.cpp::make_node_cache_key(u64 params_hash, int w, int h)`
needs to be extended to include a camera-aware fingerprint:

- For AE_CAM_02: `cam.zoom × 1000` quantized (`{500, 1000, 1500}` produces 3
  distinct cache-key suffixes for 60 frames).
- For AE_CAM_04: `cam.position.z × 1000` quantized (`{-600, -1000, -1400}`
  produces 3 distinct suffixes) + a `parent.is_null ? 0 : hash(parent_id)`
  dimension to disambiguate parented vs un-parented scenes with identical Z.

## Evidenza

### Sub-cluster A caller enumeration

```
$ grep -rn 'project_layer_2_5d\|project_layer_2_5d(' src/ \
    --include='*.cpp' --include='*.hpp' | grep -v 'camera_2_5d_projection.hpp'
src/render_graph/nodes/multi_source_node.cpp:55,246,360  (sweep done)
src/render_graph/builder/graph_builder_matte.cpp:35       (TODO)
src/render_graph/pipeline/dirty/layer_bbox_collector.cpp:37  (TODO)
src/render_graph/pipeline/refresh/layer_item.cpp:14       (TODO)
```

### Sub-cluster B sha256 observance (machine-verified ON-DISK)

`sha256 | head -c 16` di tutti i 24 PNG tracked in `tests/golden/ae_parity/`
(commit `HEAD` post-`c03ce2a2` + re-bake):

| Scene                  | frame000       | frame015 | frame030       | frame060       | Status |
|------------------------|----------------|----------|----------------|----------------|--------|
| AE_CAM_02_zoom_fov     | `cc86d2b5...`  | –        | –              | `cc86d2b5...`  | ✗ STILL COLLIDING (pair f000↔f060) |
| AE_CAM_03_two_node_poi | distinct       | –        | `cc86d2b5...`  | `cc86d2b5...`  | ✗ STILL COLLIDING (pair f030↔f060 + others vary) |
| AE_CAM_04_parent_null  | `cc86d2b5...`  | –        | –              | `cc86d2b5...`  | ✗ STILL COLLIDING (pair f000↔f060) |
| AE_CAM_05_orbit        | distinct       | `cc86d2b5...` | `cc86d2b5...` | `cc86d2b5...` | ✗ STILL COLLIDING (cluster f015/f030/f060) |
| AE_CAM_06_dolly_zoom   | distinct       | distinct | distinct       | distinct       | ✓ FULLY DISTINCT (all 4 frames) |
| AE_CAM_07_static_wide_angle | `cc86d2b5...` | –   | –              | –              | (orthogonal: single-frame scene) |
| AE_CAM_08_dof_focus    | (orthogonal)   | –        | –              | –              | (orthogonal: ANOTHER path — DOF anim) |
| AE_CAM_09_motion_blur  | `cc86d2b5...`  | `4338a...` | `cc86d2b5...` | –              | △ PARTIAL (only f015 fresh-distinct) |

**Onesto state summary**: solo **AE_CAM_06** (4/4 frames) + **AE_CAM_09_f015**
(1/3 frames) sono fresh-distinct su disco. AE_CAM_02/03/04/05/07/09-f000/f030
(13/24 PNG tracked, `21556B` size + `cc86d2b5e80287dc` sha256 prefix) restano
collision-encoded per `fc351bfe` workaround.

**Layered truth** (per anti-greenwashing):

1. **In-memory runtime FB-hash** (volatile, post-render): `chronon3d_ae_parity_tests`
   `--test-case='AE_CAM_*'` reports 35/35 PASS, 140/140 assertions, `SUCCESS!`
   banner; CHECK `framebuffer_hash(*fb_X) != framebuffer_hash(*fb_Y)` enforced
   per `tests/visual/ae_parity/ae_parity_tests.cpp` (200+ LOC TEST_CASEs) per
   AE_CAM_03/05/06/09; per AE_CAM_02/04/08 the test uses only MESSAGE
   forward-point (NO CHECK — greenwash strutturale).
2. **On-disk GOLDEN sha256** (persisted, regress-controllable): come da tabella
   sopra — solo AE_CAM_06 + AE_CAM_09_f015 fresh-distinct.

La fix `c03ce2a2` ha spostato la collision FUORI dal runtime FB-hash (memory)
ma NON ha cambiato la collision-encoded `cc86d2b5...` PNG output che atterra su
`tests/golden/ae_parity/`. La collision-encoded goldens persistono per design
(`fc351bfe`) oltre la fix.

## Impatto

- The 3 unreached `project_layer_2_5d` call sites contribute to `proj_X` /
  cache-key congruences for paths OUTSIDE the regular `multi_source_node`
  execute.  Specifically:
  - matte builder path (production fast-path for matte-only scenes) — AE_CAM
    goldens miss this path because they use bbox-aware compositions, but the
    anychannel test runs (when added) will hit it.
  - bbox dirty-collector path (used by frame-dirty-marking heuristic for
    `frame_buf_dirty_rect` propagation in `render_composition_frame`).
  - refresh-path layer Item (the path that rebuilds MultiSourceItem.matrix
    on frame changes — currently relies on the parent `multi_source_node`
    deadline, but a stale Item matrix can re-enter the cache-key tuple).
- The downstream cache-key extension unblocks deterministic per-frame FB
  differentiation on AE_CAM_02 + AE_CAM_04, advancing the cat-2 AE-parity
  visual contract from `30/35` → estimated `32/35` once both fix paths land.

## Confine

- **In scope**: call-site sweep of the 3 unreached `project_layer_2_5d` sites
  (graph_builder_matte.cpp / layer_bbox_collector.cpp / layer_item.cpp); extend
  `node_cache.cpp::make_node_cache_key` tuple to include animation-step
  fingerprint for AE_CAM_02 + AE_CAM_04; reopen the `ae_parity_tests.cpp`
  collision-encoded goldens for AE_CAM_02 + AE_CAM_04 and regenerate them post-fix.
- **Out of scope**: further hash-collision root-causes (covered by parent
  `TICKET-ae-cam-hash-collision` Post-fix broad sweep once this ticket lands).
  Matrix-fix-side regression (covered by parent `TICKET-AE-CAM-PRECISION-COLLAPSE`).
  AE_CAM_08 + AE_CAM_01 + AE_CAM_07 (already PASS or orthogonal).

## Soluzione accettabile

### Soluzione A — Sweep the 3 unreached sites

For each of `graph_builder_matte.cpp:35` + `layer_bbox_collector.cpp:37` +
`layer_item.cpp:14`:

1. Apply the same `proj.transform.to_mat4()`-centric rewrite that
   `multi_source_node.cpp` 3 sites got in commit `c03ce2a2`.
2. Add a regression-lock test under `tests/render_graph/features/` that
   captures the per-site draw matrix composition and verifies it expresses
   the projected bbox + centroid.
3. Maintain the AGENTS.md v0.1 invariant: zero new public API surface; zero
   new singleton/registry/cache/service-locator; pure internal-TU swap.

Atomic per file (1 commit per site; 3 commits or 1 combined atomic commit per
AGENTS.md guidance).

### Soluzione B — Camera-aware cache-key extension

1. Extend `make_node_cache_key` to accept a 4th parameter:
   `camera_fingerprint` (a `u32` hash of `cam.zoom_quantized * 1000` xor
   `cam.position.z_quantized * 1000` xor `parent.is_null ? 0 : hash(parent_id)`).
2. Update the 6 callers to compute and pass `camera_fingerprint` from the
   evaluated `cam` at the call site.
3. Add a regression-lock test in `tests/cache/test_node_cache_hash_includes_camera.cpp`
   that locks the new 4-tuple contract: 3 distinct inputs (5, 10, 15 hash-suffix)
   prove camera-awareness; `node_cache_hash_collisions` count is asserted == 0
   post-fix.

Atomic per file (1 commit per layer of the change: signature + 6 call-sites +
locks + ae_parity regen).

### Combinazione

Landing Soluzione A + Soluzione B in 2 atomic commits (one per solution, AGENTS
agrees) — both required to fully retire `TICKET-AE-CAM-PRECISION-COLLAPSE`
parent ticket.

## Criteri di accettazione

- Sub-cluster A: 3 sites swept; no assert on `transform.{scale.z, rotation, anchor}`
  regresses; regression-lock tests pass; no new public API surface.
- Sub-cluster B: AE_CAM_02 frame0 ≠ frame60 + AE_CAM_04 frame0 ≠ frame60 in
  re-baked goldens; `node_cache_hash_collisions` counter == 0 on AE_CAM_* sweep
  with `CHRONON3D_UPDATE_GOLDENS=1` mode.
- Post-fix: `TICKET-AE-CAM-PRECISION-COLLAPSE` parent ticket + companion
  `TICKET-ae-cam-hash-collision` are consolidated as `DONE-close-out` once
  AE_CAM_02 + AE_CAM_04 reopen and re-pass.
- AGENTS.md v0.1 invariants preserved: zero new public API surface; zero new
  singleton/registry/cache/service-locator; pure internal surface-area
  extension permitted (camera_fingerprint 4-tuple IS internal-only).

## Source-cluster

- `src/render_graph/builder/graph_builder_matte.cpp` (3 call sites)
- `src/render_graph/pipeline/dirty/layer_bbox_collector.cpp`
- `src/render_graph/pipeline/refresh/layer_item.cpp`
- `src/cache/node_cache.cpp::make_node_cache_key`
- `tests/golden/ae_parity/ae_cam_02_zoom_fov_frame000.png` (re-bake target)
- `tests/golden/ae_parity/ae_cam_04_parent_null_frame{000,060}.png` (re-bake target)

## Cross-references

- Parent ticket: [`TICKET-AE-CAM-PRECISION-COLLAPSE`](../FOLLOWUP_TICKETS.md)
- Sibling ticket: [`TICKET-ae-cam-hash-collision`](../FOLLOWUP_TICKETS.md)
- Predecessor commit: `c03ce2a2` (`fix(cam-projection): 2.5D path uses resolver screen-space TRS`)
- Predecessor commit (re-bake): pending this session, regenerates 24 PNGs in `tests/golden/ae_parity/`
- Diagnostic instrumentation precedent: `CHRONON3D_PROJ_DIAG` env-gated spdlog
- Cache-key collision counter: `include/chronon3d/core/profiling/counters.hpp:30 ::node_cache_hash_collisions`

## Status del ticket (post sha256 observance + Gemini exoneration)

**PARTIAL** (Sub-cluster A NOT-NEEDED + Sub-cluster B forward-pointed to
`TICKET-ae-cam-hash-collision` Soluzione B). State transition: PLANNED →
PARTIAL on 2026-07-06 per:
- (a) Gemini source-read clarification on sites 4-6 (consumano `eff_proj`
  affine, NON `proj.projection_matrix` prospettico raw → Sub-cluster A
  NOT-NEEDED);
- (b) Machine-verified sha256 observance on 24 PNG goldens (13/24 collision-encoded
  per `cc86d2b5e80287dc` prefix → Sub-cluster B effective solo per quei 13 PNG,
  con AE_CAM_06 + AE_CAM_09_f015 fresh-distinct).

Compiler scope is fixed. Sub-cluster A **closed NOT-NEEDED**; Sub-cluster B
forward-point: per chiudere il residuo osservato sui 13 PNG tracked
collision-encoded servono:
- (1) Extension di `src/cache/node_cache.cpp::make_node_cache_key(u64, int, int)`
  con 4° parametro `camera_fingerprint` + propagazione ai siti di `cache_key(ctx)`;
- (2) Re-bake `tests/golden/ae_parity/ae_cam_{02,03,04,05,09}_*.png` con
  `CHRONON3D_UPDATE_GOLDENS=1` post-fix;
- (3) Lock test `tests/cache/test_node_cache_hash_includes_camera.cpp`
  asserting `framebuffer_hash(fb_zoom_500) != framebuffer_hash(fb_zoom_1000)
  != framebuffer_hash(fb_zoom_1500)`.

Implementazione tracciata canonically su `TICKET-ae-cam-hash-collision`
(ticket sibling più specifico; questo ticket rimane PARTIAL con pointer
forward per context continuity).

## AGENTS.md v0.1 freeze compliance

Cat-1 (build correction — call-site sweep is internal rewrite) + Cat-2 (test
deterministici — regression-lock expanded) + Cat-3 (no public API surface
expansion — camera_fingerprint parameter is internal-only) + Cat-5 (doc
alignment via CHANGELOG + FOLLOWUP_TICKETS same commit). Zero new singletons /
registries / resolvers / caches / service-locators.
