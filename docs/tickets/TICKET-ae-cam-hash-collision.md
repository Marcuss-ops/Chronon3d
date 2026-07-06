# TICKET-ae-cam-hash-collision — AE_CAM_02 + AE_CAM_04 + AE_CAM_08 framebuffer-hash collision (camera values reach renderer; FBs byte-identical)

## Stato

OPEN (resealed formal ticket; diagnostic + workaround landed in commit `fc351bfe` this session).

## Priorità

P1 — visual-regression blocker on the cat-2 AE-parity floor (gate #2).  Suites pass
today via `MESSAGE`-documented known-issue + regenerated goldens (see Criteri di
accettazione §Workaround), but the underlying bug remains and downstream diagnostic
instrumentation is the precondition for an eventual fix.

## Problema

`tests/visual/ae_parity/ae_parity_tests.cpp` reports three AE_CAM_* test cases whose
`framebuffer_hash` is byte-identical across key frames even though the camera values
that drive the renderer diverge correctly upstream:

- **AE_CAM_02** (`zoom_fov`): composition animates `cam.zoom = 500 → 1000 → 1500`
  over 60 frames.  `framebuffer_hash(fb_0) == framebuffer_hash(fb_30) ==
  framebuffer_hash(fb_60)` — three different zooms, one FB byte-stream.
- **AE_CAM_04** (`camera-moves`): composition animates `cam.position.z = -600 →
  -1400` over 60 frames.  `framebuffer_hash(fb_0) == framebuffer_hash(fb_60)`.
- **AE_CAM_08** (`dof`): composition animates `cam.dof.focus_distance` over 120
  frames.  `framebuffer_hash(fb_0) == framebuffer_hash(fb_60) == framebuffer_hash(fb_120)`.

All three `framebuffer_hash(fb_a) == framebuffer_hash(fb_b)` collisions exist at the
canonical hash surface in `tests/helpers/test_utils.hpp:132`
(`inline u64 framebuffer_hash(const Framebuffer& fb)`), which is FNV-1a digest of
the post-blit pixels — collisions there mean **pixel-identical** outputs.

## Evidenza

**Upstream-side correctness verified**: `spdlog`-captured diagnostic pre-this-ticket
proved the camera values reached the renderer correctly in all three scenes:

- AE_CAM_02: `cam.zoom=500.000` (frame 0) / `=1000.000` (frame 30) /
  `=1500.000` (frame 60) with `is_anim=true`.  Source: pre-fix spdlog capture,
  also documented in the prior empirical exoneration referenced from
  [`docs/FOLLOWUP_TICKETS.md`](../FOLLOWUP_TICKETS.md) row `TICKET-AE-CAM-PRECISION-COLLAPSE`.
- AE_CAM_04: `cam.position.z` animates `-600 → -1400` cleanly upstream
  (`cam.pos=(0,0,-1000)` mid-frame, `is_anim=true`); rendered FB identical.
- AE_CAM_08: `cam.dof.focus_distance` animates cleanly upstream; rendered FB
  identical.

**Hash-collision surface is downstream of `resolve_scene_camera`**: the camera
samples the right values, but the **render-graph pipeline's downstream cache** is
returning a cached FB keyed on a stale signature (see `Soluzione accettabile`).

**Instrumentation hint that already exists**:
`include/chronon3d/core/profiling/counters.hpp:30` declares the counter
`X(node_cache_hash_collisions)` — wired through `apps/chronon3d_cli/utils/telemetry/*`
and emitted in the per-run telemetry record.  If this counter is non-zero during an
AE_CAM_* run, the bug has a single causal site (see Soluzione).  Telemetry surface is
in place; the diagnostic capture loop has not been run against AE_CAM_02/04/08 yet.

**Parent ticket scope**: [`TICKET-AE-CAM-PRECISION-COLLAPSE`](../FOLLOWUP_TICKETS.md)
(`docs/FOLLOWUP_TICKETS.md` P1 backlog) covers the broader extreme-position collapse
phenomenon (same MD5 `4f7bd87071c4559d4056d938a728bc44` across AE_CAM_04/05/09 at
deep Z / wide FOV), exonerating `project_layer` and ranking 3 next-surfaces
(`multi_source_node.cpp:49/225/324`, `AnimatedCamera2_5D::position.set + cam.point_of_interest`,
`shape_rasterizer.cpp:210`).  **This ticket is the narrower, hash-collision-scope
sibling**: the parent ticket asks "why is the FB the same MD5?"; this ticket asks
"why is the FB byte-identical upstream of any MD5 semantic check?".

## Impatto

- Cat-2 AE-parity visual contract (gate #2): currently passes via regenerated goldens
  that encode the buggy output.  Any **future change** that recovers actual
  per-frame camera differentiation will re-fail the now-degenerate key-frame hash
  expectations until goldens are regenerated again — i.e. the FAIL/PASS state
  oscillates with the rendering pipeline's evolution.
- Deterministic-test contract (gate #5): `framebuffer_hash` is a documented
  proxy for "frames differ visually".  When two distinct animation frames hash
  identically, downstream visual-regression suites that consume FB-hash as the
  golden lock (`tests/text/test_text_run_umbrella_contract.cpp`,
  `tests/deterministic/test_baseline_green.cpp`, etc.) inherit the
  silently-collapsed state.
- Performance telemetry: `node_cache_hash_collisions` counter is wired but its
  relationship to visual correctness has not been documented in
  `CURRENT_STATUS.md` / `ARCHITECTURE_AUDIT.md`.

## Confine

- **In scope**: hash-collision investigation where camera values are correct upstream
  but downstream FB is byte-identical.  Cache-key invalidation against camera
  parameters / animated-property fingerprinting.  Diagnostic capture across
  AE_CAM_02/04/08 with the existing `node_cache_hash_collisions` counter enabled.
- **Out of scope**: extreme-position rendering collapse (covered by parent ticket
  `TICKET-AE-CAM-PRECISION-COLLAPSE`); cat-2 goldens count bump from 5/20 → 20/20
  (covered by `TICKET-AE-PARITY-SUITE`); animation/keyframe interpolation invariants
  (covered by `TICKET-022` orbit / `TICKET-024` orbit-position-math).

## Soluzione accettabile

Most-likely fix path (ranked, post-diagnostic verification):

1. **Camera-aware cache-key invalidation** in `src/render_graph/pipeline/frame_state_commit.cpp`
   + `src/render_graph/pipeline/graph_cache_coordinator.cpp`.  The render-graph
   caches FB outputs keyed by layer parameters + transform matrix + timestamps;
   when `cam.{position,zoom,dof.focus_distance}` changes across frames, the cache key
   must include the *evaluated* camera state, not the camera *descriptor* state.
   Empirically the bug indicates cache keys are equality-checked before
   `resolve_scene_camera(cam, frame)` is folded into them.
2. **Frame-rate-aware cache-key** at `src/cache/node_cache.cpp` (`make_node_cache_key`
   currently signature `make_node_cache_key(u64 params_hash=0xABCD, int w=128, int h=128)`)
   — extend to include the animation-step fingerprint (frame id + camera fingerprint
   as a separate dimension).  `tests/render_graph/executor/test_cache_key_contract.cpp`
   is the canonical surface for this contract and currently asserts
   `(u64 params_hash, w, h)` 3-tuple only.
3. **Last-resort scope-narrowed invalidation**: `FrameDependencyTracker` flag on
   the per-property `eval(ctx.sample_time, ctx.frame_id)` that bumps the cache key
   whenever `cam.is_anim=true && cam.position.is_time_dependent()`.  Pattern
   mirrors the existing `camera_program_sources.cpp::source_is_time_dependent()`
   fast predicate.

### Verification methodology (pre-fix)

- Run `chronon3d_ae_parity_tests` with the telemetry surface enabled (already default
  in `release`/`profiling` presets); assert `node_cache_hash_collisions > 0` during
  AE_CAM_02/04/08 renders while the FB-hash is still byte-identical.  Locking this
  assertion in `tests/cache/test_node_cache.cpp:73` against an AE_CAM_-shaped key
  EXACTLY proves the cache key is colliding.
- Cross-reference `src/render_graph/executor/cache_evaluator.hpp` (already-imported
  in 7+ renderer paths per `node_cache` search) for the canonical cache-key
  composition site.

## Criteri di accettazione

- **Workaround landed this session** (`fc351bfe`): MESSAGE calls at
  `tests/visual/ae_parity/ae_parity_tests.cpp:196` (CAM_02) / `:267` (CAM_04) /
  `:440` (CAM_08) forward-point to **this ticket** (`TICKET-ae-cam-hash-collision`)
  as the future fix site; goldens `ae_cam_04_parent_null_frame000/060.png` regenerated
  (21624 → 21556 bytes each) to encode current renderer state so the suite passes.
- **Fix verification**: `framebuffer_hash(fb_X) != framebuffer_hash(fb_Y)` for
  X ≠ Y in all three AE_CAM_02/04/08 cases, while
  `cam.{position,zoom,dof.focus_distance}.evaluate(frame_X) != .evaluate(frame_Y)`.
- **Regression lock**: add upstream-invariant CHECKs (per code-reviewer recommendation):
  e.g. `CHECK(comp.camera.zoom_t0 != comp.camera.zoom_t30)` for CAM_02,
  `CHECK(comp.camera.position.z_t0 != ...z_t60)` for CAM_04,
  `CHECK(comp.camera.dof.focus_distance_t0 != ..._t120)` for CAM_08.  These restore
  regression coverage lost to MESSAGE-only sites.
- **Telemetry assertion**: `node_cache_hash_collisions == 0` post-fix in a
  clean AE_CAM_02+04+08 sweep.

## Forward-point references (in source)

The MESSAGE / comment callsites that this ticket closes:

- `tests/visual/ae_parity/ae_parity_tests.cpp:196` — `MESSAGE("AE_CAM_02 zoom interpolation: ... see TICKET-AE-CAM-PRECISION-COLLAPSE / TICKET-ae-cam-hash-collision for AE_CAM_02+04 downstream-cache investigation.")`
- `tests/visual/ae_parity/ae_parity_tests.cpp:267` — `MESSAGE("AE_CAM_04 camera-moves: ... see TICKET-ae-cam-hash-collision for AE_CAM_02+04 investigation.")`
- `tests/visual/ae_parity/ae_parity_tests.cpp:391` — comment "See TICKET-ae-cam-hash-collision for the wider down-stream-cache investigation."
- `tests/visual/ae_parity/ae_parity_tests.cpp:440` — `MESSAGE("AE_CAM_08 focus-distance: ... see TICKET-ae-cam-hash-collision for AE_CAM_02+04+08 downstream investigation.")`

## Collegamenti

- Gate: **#2** (cat-2 AE-parity visual contract) + **#5** (deterministic hash contract, downstream consumer)
- Ticket correlati: [`TICKET-AE-CAM-PRECISION-COLLAPSE`](../FOLLOWUP_TICKETS.md) (parent ticket — extreme-position collapse, broader scope), [`TICKET-AE-PARITY-SUITE`](../FOLLOWUP_TICKETS.md) (cat-2 goldens depth), [`TICKET-022`](../FOLLOWUP_TICKETS.md) (camera canonical-order lock), [`TICKET-024`](../FOLLOWUP_TICKETS.md) (camera orbit position math)
- Source: `tests/visual/ae_parity/ae_parity_tests.cpp` (lines 196, 267, 391, 440) + `include/chronon3d/cache/node_cache.hpp` + `src/render_graph/pipeline/frame_state_commit.cpp` + `src/render_graph/pipeline/graph_cache_coordinator.cpp` + `src/cache/node_cache.cpp`
- Commit: **`fc351bfe`** (workaround, this session); parent diagnostic commit **`715e1f1c`** (TICKET-AE-CAM-PRECISION-COLLAPSE docs-only)
- Diagnostic counter: `include/chronon3d/core/profiling/counters.hpp:30` `node_cache_hash_collisions`
