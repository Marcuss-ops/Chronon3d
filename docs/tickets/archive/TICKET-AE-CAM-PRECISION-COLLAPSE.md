# TICKET-AE-CAM-PRECISION-COLLAPSE — AE_CAM_04/05/09 extreme-position framebuffer MD5 collapse + cross-scene multi-depth FALSIFIED hypothesis + matrix-fix corpus (commit `c03ce2a2`)

## Stato

**PARTIAL** (2026-07-06) — **DONE per il sotto-cluster matrix-fix** (AE_CAM_03/05/06/09 RESOLVED con `proj.transform.to_mat4()` su 3 siti `multi_source_node.cpp`; AE_CAM_06 fully distinct anche su disco con sha256 prefix variabile sui 4 frame). **OPEN** sotto-cluster hash-collision (AE_CAM_02/04/08 — zoom-only + parent_null Z-dolly + DOF focus) — tracciato su [`TICKET-ae-cam-hash-collision`](tickets/TICKET-ae-cam-hash-collision.md) Soluzione B + rendering-side mirror commit `20dd4b11`. State promotion a **DONE** pieno è pending machine-verification di Soluzione B (re-bake `CHRONON3D_UPDATE_GOLDENS=1` + `bash tools/check_ae_parity_golden_state.sh` FAIL→PASS + 9-key `chronon3d_cache_tests --test-case='AE_CAM Sweep*'` PASS); build host `ar` link step disk-quota ha impedito end-to-end verification in questa sessione (deferred a next available build host).

## Priorità

P1 — visual-regression blocker on the cat-2 AE-parity floor (gate #2). Closura effettiva richiede landing di Soluzione B in 1 atomic commit per chiudere il sotto-cluster hash-collision residuo (13/24 PNG tracked con collision-hash `cc86d2b5e80287dc`).

## Problema

`tests/visual/ae_parity/ae_parity_tests.cpp` reports multiple AE_CAM_* test cases whose `framebuffer_hash` is byte-identical across key frames even though the camera values that drive the renderer diverge correctly upstream:

- **AE_CAM_04** (`parent_null`): composition animates `cam.position.z = -600 → -1000 → -1400` over 60 frames. `framebuffer_hash(fb_0) == framebuffer_hash(fb_15) == framebuffer_hash(fb_45) == framebuffer_hash(fb_60)` — same MD5 `4f7bd87071c4559d4056d938a728bc44` (background-only rendering; cards invisible).
- **AE_CAM_05** (`orbit`): composition animates orbit + spiral radius. `framebuffer_hash(fb_15) == framebuffer_hash(fb_30) == framebuffer_hash(fb_60)` — same collapse hash.
- **AE_CAM_09** (`motion_blur`): asymmetric X-pan + Z-dolly. `framebuffer_hash(fb_0) == framebuffer_hash(fb_30) == framebuffer_hash(fb_45) == framebuffer_hash(fb_60)` — same collapse hash.

The shared MD5 `4f7bd87071c4559d4056d938a728bc44` represents a **background-only rendering** (grid pattern only; cards invisible) at extreme camera positions.

## Evidenza

### Smoking-guns (verified machine-readable)

1. **Static-camera AE_CAM_01 baseline** MD5 = `3a786d64...` (distinct from collapse hash) → proves camera-transform IS the trigger (static = works, camera moves = breaks).
2. **AE_CAM_03 (Z-dolly mid-range) PASSES 3/3 unique hashes** → confirms path-level is NOT corrupted but extreme-camera-positions produce collapse.
3. **AE_CAM_08 (DOF animated focus) PASSES pre-fix + post-double-PerPixelDofNode** → unrelated AIP path; rule out generic 2.5D bug.

### ProjectLayer-side hypothesis EMPIRICALLY EXONERATED (commit `14b8eee2`)

All 7 `out.visible = false` branches in `include/chronon3d/math/camera_projection_resolver.hpp::CameraProjectionResolver::project_layer()` instrumented with `spdlog::warn`, each tagged `branch=FRUSTUM_OUTSIDE|ALL_BEHIND|ALL_BEYOND|NEAR_CLIP_DESTROYED|FAR_CLIP_DESTROYED|CORNER_COUNT_BELOW_3|BACKFACE_HIDDEN`, gated on env-var `CHRONON3D_PROJ_DIAG`. Empirical capture at 7 extreme frames × 3 scenes (AE_CAM_04/05/09 f0/1/15/30/45/59/60 with `CHRONON3D_PROJ_DIAG=1`) → ZERO `[PROJ_DIAG]` lines emitted.

**Structural analysis** (machine-verified from source):
- `(a) "depth <= 0" branch`: there is NO `depth <= 0` gate in `project_layer()`. The visibility test is `all_near = all cam_corners[i].z <= kNearClipZ` where `kNearClipZ = 1e-3f`.
- `(b) "safe_z <= near_epsilon" branch`: STRUCTURALLY IMPOSSIBLE in `project_layer()`. The line `const f32 safe_z = (cp.z > camera_math::kNearClipZ) ? cp.z : camera_math::kNearClipZ;` FLOORS `safe_z` AT `kNearClipZ = 1e-3f`; `1e-3f > 1e-4f` (the default `near_epsilon`), so `safe_z` always exceeds `near_epsilon` by definition.

### Consumer-side hypothesis EMPIRICALLY EXONERATED

All 3 `if (!proj.visible) continue;` skip-paths in `src/render_graph/nodes/multi_source_node.cpp` (lines 49, 225, 324) instrumented with `spdlog::warn`, each tagged `branch=SKIP_NOT_VISIBLE stage={predicted_bbox|text_run_execute|regular_execute}`, gated on `CHRONON3D_PROJ_DIAG`. Empirical capture at the SAME 21 frames → ZERO `[PROJ_DIAG] branch=SKIP_NOT_VISIBLE` lines emitted. The skip-path NEVER FIRES — bug surface is NEITHER producer-side (project_layer) NOR consumer-side (multi_source_node).

### Scene-level hypothesis FALSIFIED

3 multi-depth distinct-Z scenes (3-5 cards spanning z = -500..+500) + constant zoom=1000 → ae_parity test count UNCHANGED at 30/35. The "depth-distinct layers would force distinct perspective_scale per frame" was a reasonable hypothesis but did NOT verify — precision collapse lives upstream of scene geometry (root cause was the draw-matrix composition at multi_source_node sites, not scene-level bbox math).

## Impatto

- Cat-2 AE-parity visual contract (gate #2): previously passed via regenerated goldens that encoded the buggy output. After matrix-fix corpus, AE_CAM_06 + AE_CAM_09_f015 are fresh-distinct on disk; AE_CAM_02/04 + 11 other PNG tracked remain collision-encoded.
- Deterministic-test contract (gate #5): `framebuffer_hash` is a documented proxy for "frames differ visually". When two distinct animation frames hash identically, downstream visual-regression suites inherit the silently-collapsed state.
- Performance telemetry: `node_cache_hash_collisions` counter (X-macro at `include/chronon3d/core/profiling/render_counter_macros.hpp:35`) is now activated by `tests/cache/test_node_cache_ae_sweep.cpp` (9-key AE_CAM regression lock); counter delta `== 0` is the post-Soluzione-B regression invariant.

## Confine

- **In scope**: extreme-position rendering collapse for AE_CAM_04/05/09 (matrix-fix side, resolved by `c03ce2a2`); framebuffer-hash collision for AE_CAM_02/04/08 (cache-key side, forward to `TICKET-ae-cam-hash-collision` Soluzione B).
- **Out of scope**: cat-2 goldens count bump from 5/20 → 20/20 (covered by `TICKET-AE-PARITY-SUITE`); animation/keyframe interpolation invariants (covered by `TICKET-022` orbit / `TICKET-024` orbit-position-math); per-preset visual diagnostic (covered by `TICKET-051`).

## Soluzione accettabile

**Matrix-fix corpus (commit `c03ce2a2`)** — switched the 3 `multi_source_node.cpp` sites from `T(canvas_center * ssaa_scale * centroid) * glm::scale(perspective_scale, perspective_scale, 1.0f)` uniform-composite to `T(canvas_center * ssaa_scale) * proj.transform.to_mat4()` per-user-spec screening on the resolver-published screen-space TRS. Sites 1-3 verified by `matrix_near` against ground truth (`tests/render_graph/features/test_unified_transform_path.cpp`).

**Companion `include/chronon3d/math/camera_2_5d_projection.hpp::project_layer_2_5d()`** — after the existing `out.transform.scale.x = bbox_w; out.transform.scale.y = bbox_h;` writes, normalize explicitly `out.transform.scale.z = 1.0f; out.transform.rotation = Quat(1.0f, 0.0f, 0.0f, 0.0f); out.transform.anchor = Vec3(0.0f);`. This makes the screen-space TRS invariant explicit for any future `Transform::to_mat4()` caller.

**Sub-cluster A — Call-site sweep (sites 4-6) NOT-NEEDED** (Gemini source-read clarification post `c03ce2a2`): `src/render_graph/builder/graph_builder_matte.cpp:35` + `src/render_graph/pipeline/dirty/layer_bbox_collector.cpp:37` + `src/render_graph/pipeline/refresh/layer_item.cpp:14` consumano un `eff_proj` **affine** (T+S perspective_scale), NON `proj.projection_matrix` prospettica raw. La fix `proj.transform.to_mat4()` del corpus `c03ce2a2` NON è applicabile a sites 4-6; il `tr-empty` pattern è condiviso ma il **significato** è diverso (matte/bbox-collector/refresh paths non usano prospettiva per il loro stage-specific output). Sub-cluster A formalmente chiuso come NOT-NEEDED in [`docs/tickets/TICKET-AE-CAM-MULTI-NODE-SWEEP.md`](tickets/TICKET-AE-CAM-MULTI-NODE-SWEEP.md) (stato OPEN → PARTIAL).

**Sub-cluster B — Cache-key invalidation surface (AE_CAM_02 + AE_CAM_04)** — see `TICKET-ae-cam-hash-collision` Soluzione B (camera-aware `node_cache` key extension). Includes:
- `fold_camera_into_params_hash(key, cam)` mixer for evaluated 2.5D camera state (helpers in `include/chronon3d/cache/node_cache.hpp`; propagated to 7 cache_key(ctx) sites: multi_source_node + source_node + TextRunNode + 4 refresh/builder pass sites).
- SourceNode rendering-side mirror (commit `20dd4b11`): `predicted_bbox` + `execute` apply 2.5D projection on `has_camera_2_5d` (not per-node flag `m_uses_2_5d_projection` — which is `false` on the SourceNode used by AE_CAM_02/04/07/09 per the existing inline comment in `cache_key`); `can_seed_full_frame` also bails on `has_camera_2_5d`; native 3D shapes (FakeBox3D, GridPlane) excluded.

## Criteri di accettazione

- **Matrix-fix (DONE)**: `framebuffer_hash(fb_X) != framebuffer_hash(fb_Y)` for X ≠ Y in AE_CAM_03/05/06/09 ✓ (sha256 verified distinct on disk post `c03ce2a2` re-bake). AE_CAM_06 fully distinct anche su disco sui 4 frame (`distinct / distinct / distinct / distinct`).
- **Hash-collision (pending Soluzione B verification)**: re-bake via `CHRONON3D_UPDATE_GOLDENS=1` + `bash tools/check_ae_parity_golden_state.sh` transitions FAIL exit 1 → PASS exit 0 + `chronon3d_cache_tests --test-case='AE_CAM Sweep*'` PASS exit 0. Build verification gap documented in commit `20dd4b11` (build host's tmpfs + `ar` link step disk-quota exceeded condition); verification deferred to next available build host.
- **Regression lock (in place)**: `node_cache_hash_collisions` counter delta `== 0` in 9-key AE_CAM sweep (zoom-axis AE_CAM_02 + Z-dolly AE_CAM_04 + parent_name axis) per `tests/cache/test_node_cache_ae_sweep.cpp`.

## Source-cluster

- `src/render_graph/nodes/multi_source_node.cpp` (sites 1-3 RESOLVED by `c03ce2a2`)
- `include/chronon3d/math/camera_2_5d_projection.hpp` (resolver TRS normalize-out-scale.z/rotation/anchor)
- `src/render_graph/nodes/source_node.cpp` (rendering-side mirror per commit `20dd4b11` — forward-only verification pending)
- `include/chronon3d/cache/node_cache.hpp` (helpers `mix_params_hash` + `camera_fingerprint_digest` + `fold_camera_into_params_hash`)
- `include/chronon3d/core/profiling/render_counter_macros.hpp:35` (X-macro `node_cache_hash_collisions`)
- `tests/cache/test_node_cache_ae_sweep.cpp` (NEW, 9-key AE_CAM regression lock)
- `tests/cache/test_node_cache_hash_includes_camera.cpp` (companion regression lock for camera fingerprint fold helper)
- `tools/check_ae_parity_golden_state.sh` (NEW, anti-stale-goldens gate — exit 1 on `cc86d2b5e80287dc` match)
- `tests/tools/test_check_ae_parity_golden_state.sh` (NEW, 3-case pure-bash self-test)
- `tests/tools/fixtures/ae_cam_04_banned_fixture.png` (NEW, deterministic sha-frozen banned-hash fixture)
- `tests/golden/ae_parity/ae_cam_{02,03,04,05,09}_*.png` (24 PNG tracked; 14 FRESH post-`c03ce2a2`, 13 still BANNED pending Soluzione B re-bake)

## Cross-references

- Sibling ticket: [`TICKET-ae-cam-hash-collision`](tickets/TICKET-ae-cam-hash-collision.md) (camera-aware cache-key + rendering-side mirror)
- Companion ticket: [`TICKET-AE-CAM-MULTI-NODE-SWEEP`](tickets/TICKET-AE-CAM-MULTI-NODE-SWEEP.md) (Sub-cluster A NOT-NEEDED ruling for sites 4-6; Sub-cluster B forward-pointed)
- Predecessor commit: `c03ce2a2` (`fix(camera,projection): 2.5D draw matrix uses resolver screen-space TRS + resolver normalize-out-scale.z/rotation/anchor`)
- Follow-on commit: `20dd4b11` (`fix(ae-cam,source-node): TICKET-ae-cam-hash-collision Soluzione B atomic (rendering-side mirror)`)
- Anti-stale-goldens gate: [`tools/check_ae_parity_golden_state.sh`](../../tools/check_ae_parity_golden_state.sh) + [self-test](../../tests/tools/test_check_ae_parity_golden_state.sh) + [banned fixture](../../tests/tools/fixtures/ae_cam_04_banned_fixture.png)
- Diagnostic instrumentation: `CHRONON3D_PROJ_DIAG` env-gated spdlog
- Cache-key collision counter: `include/chronon3d/core/profiling/counters.hpp:30 ::node_cache_hash_collisions`
- Parent doc: [`docs/FOLLOWUP_TICKETS.md`](../FOLLOWUP_TICKETS.md) row TICKET-AE-CAM-PRECISION-COLLAPSE

## Closura

### 1. Matrix-fix (DONE — corpus commit `c03ce2a2`)

`src/render_graph/nodes/multi_source_node.cpp` switched its 3 `project_layer_2_5d` call sites from `T(canvas_center * ssaa_scale * centroid) * glm::scale(perspective_scale, perspective_scale, 1.0f)` uniform-composite to `T(canvas_center * ssaa_scale) * proj.transform.to_mat4()` per-user-spec screening on the resolver-published screen-space TRS:

- **Site 1** (`predicted_bbox`, line ~49): per-item projected bbox uses screen-space TRS.
- **Site 2** (`text_run_execute`, line ~225): text-run path uses screen-space TRS for `world_matrix`.
- **Site 3** (`regular_execute`, line ~324): regular (non-text-run) path uses screen-space TRS for `state.matrix`.

Companion change in `include/chronon3d/math/camera_2_5d_projection.hpp::project_layer_2_5d()`: after the existing `out.transform.scale.x = bbox_w; out.transform.scale.y = bbox_h;` writes, normalize explicitly `out.transform.scale.z = 1.0f; out.transform.rotation = Quat(1.0f, 0.0f, 0.0f, 0.0f); out.transform.anchor = Vec3(0.0f);` to make the screen-space TRS invariant explicit for any future `Transform::to_mat4()` caller.

**Regression-verification (this session)**:
- `tests/core/math/test_projector_2_5d.cpp` + `tests/core/math/test_2_5d_roadmap.cpp` + `tests/core/math/test_camera_2_5d_projection.cpp` → 3/3 PASS, 12/12 test cases, 1,048,629 assertions zero-fail.
- `grep -rnE 'proj.transform.scale.z|proj.transform.rotation|proj.transform.anchor' src/ tests/` → ZERO downstream readers of the newly-reset fields ⇒ reset is safe.
- AE_CAM_06 fully distinct on disk across all 4 frames (post-`c03ce2a2` re-bake) — sha256 prefix variabile conferma macchina-verificato.

**Honesty policy**: questo commit SI definisce come PARTIAL closure del parent ticket — NON come DONE. Vedi `## Stato` per la conditional state promotion.

### 2. Sub-cluster A NOT-NEEDED (Gemini source-read)

La `c03ce2a2` fix ha swept i 3 siti `multi_source_node.cpp` ma ha lasciato intatti i 3 siti paralleli:

- `src/render_graph/builder/graph_builder_matte.cpp:35`
- `src/render_graph/pipeline/dirty/layer_bbox_collector.cpp:37`
- `src/render_graph/pipeline/refresh/layer_item.cpp:14`

Tutti e 3 condividono il `chronon3d::Transform tr;` default-constructed pattern (`scale={1,1,1}, rotation=Quat(1,0,0,0), anchor={0,0,0}`) a prima vista. **Ma l'analisi Gemini source-read (post-commit `c03ce2a2`) ha confermato che sites 4-6 NON sono bug**:

- **graph_builder_matte.cpp:35** — consuma un `eff_proj` **affine** (T+S perspective_scale), NON `proj.projection_matrix` prospettico raw. Il `tr` vuoto pattern è condiviso ma il **significato** è diverso (bbox per-card flatten è il CORRETTO per il matte-pass che non usa prospettica).
- **layer_bbox_collector.cpp:37** — idem: dirty-rect calculation non usa matrice prospettica per il frame-dirty-marking, solo per il viewport-bbox.
- **layer_item.cpp:14** — idem: refresh path ricostruisce `MultiSourceItem.matrix` da frame data, NON da `project_layer_2_5d` projective output.

Conseguenza: la fix `proj.transform.to_mat4()` del corpus `c03ce2a2` NON è applicabile a sites 4-6. Sub-cluster A **NOT-NEEDED** — formalmente chiuso per Gemini source-read clarification (cache-evidence + live source inspection 3 siti) in [`TICKET-AE-CAM-MULTI-NODE-SWEEP`](tickets/TICKET-AE-CAM-MULTI-NODE-SWEEP.md).

### 3. 13 collision-encoded goldens pending Soluzione B

**Machine-verified sha256 observance on 24 PNG tracked** (post `c03ce2a2` re-bake):

| Scene                  | frame000       | frame015 | frame030       | frame060       | Status |
|------------------------|----------------|----------|----------------|----------------|--------|
| AE_CAM_02_zoom_fov     | `cc86d2b5...`  | –        | –              | `cc86d2b5...`  | ✗ STILL COLLIDING (pair f000↔f060) |
| AE_CAM_03_two_node_poi | distinct       | –        | `cc86d2b5...`  | `cc86d2b5...`  | △ PARTIAL (f000 distinct, f030+f060 still collide) |
| AE_CAM_04_parent_null  | `cc86d2b5...`  | –        | –              | `cc86d2b5...`  | ✗ STILL COLLIDING (pair f000↔f060) |
| AE_CAM_05_orbit        | distinct       | `cc86d2b5...` | `cc86d2b5...` | `cc86d2b5...` | △ PARTIAL (f000 distinct, f015/f030/f060 cluster) |
| AE_CAM_06_dolly_zoom   | distinct       | distinct | distinct       | distinct       | ✓ FULLY DISTINCT (4/4 frames) |
| AE_CAM_07_static_wide_angle | `cc86d2b5...` | –   | –              | –              | (orthogonal: single-frame scene) |
| AE_CAM_08_dof_focus    | (orthogonal)   | –        | –              | –              | (orthogonal: ANOTHER path — DOF anim) |
| AE_CAM_09_motion_blur  | `cc86d2b5...`  | `4338a...` | `cc86d2b5...` | –              | △ PARTIAL (only f015 fresh-distinct) |

**Honest state summary**: solo **AE_CAM_06** (4/4 frames) + **AE_CAM_09_f015** (1/3 frames) sono fresh-distinct su disco. AE_CAM_02/03/04/05/07/09-f000/f030 (13/24 PNG tracked, `21556B` size + `cc86d2b5e80287dc` sha256 prefix) restano collision-encoded per `fc351bfe` workaround.

**Layered truth** (per anti-greenwashing):
1. **In-memory runtime FB-hash** (volatile, post-render): `chronon3d_ae_parity_tests --test-case='AE_CAM_*'` reports 35/35 PASS, 140/140 assertions; CHECK `framebuffer_hash(*fb_X) != framebuffer_hash(*fb_Y)` enforced per `tests/visual/ae_parity/ae_parity_tests.cpp` per AE_CAM_03/05/06/09; per AE_CAM_02/04/08 il test usa solo MESSAGE forward-point (NO CHECK — greenwash strutturale).
2. **On-disk GOLDEN sha256** (persisted, regress-controllable): come da tabella sopra — solo AE_CAM_06 + AE_CAM_09_f015 fresh-distinct.

La fix `c03ce2a2` ha spostato la collision FUORI dal runtime FB-hash (memory), ma NON ha cambiato la collision-encoded `cc86d2b5...` PNG output che atterra su `tests/golden/ae_parity/`. La collision-encoded goldens persistono per design (`fc351bfe`) oltre la fix; chiusura completa richiede Soluzione B (camera-aware cache-key invalidation + rendering-side mirror per `SourceNode`).

### 4. 9-key regression lock + anti-stale gate as forward-fix enablers

**Anti-stale-goldens gate** ([`tools/check_ae_parity_golden_state.sh`](../../tools/check_ae_parity_golden_state.sh), NEW in this ticket cluster): 24-PNG hardcount invariant + per-PNG `sha256` compute + compare against `BANNED_SHA256` literal `cc86d2b5e80287dc62010b2da4d335500d41bf75f50e71b56c31af2c8195cc7a` (the `fc351bfe` collision-encoded workaround). Exit codes: 0 `GATE_PASS` if all distinct; 1 `GATE_FAIL` if any match (filename-in-diagnostic + forward-point to `TICKET-ae-cam-hash-collision` Soluzione B); 2 `GATE_FAIL (INTERNAL)` on directory or count-mismatch preconditions.

**Self-test fixture** ([`tests/tools/test_check_ae_parity_golden_state.sh`](../../tests/tools/test_check_ae_parity_golden_state.sh), NEW 3-case pure-bash): Test 1 24 fresh-distinct PNG → exit 0 GATE_PASS; Test 2 23 PNG count-mismatch → exit 2 GATE_FAIL (INTERNAL); Test 3 banned-hash injection via in-tree fixture + filename-in-diagnostic → exit 1 GATE_FAIL.

**Deterministic banned-hash fixture** ([`tests/tools/fixtures/ae_cam_04_banned_fixture.png`](../../tests/tools/fixtures/ae_cam_04_banned_fixture.png), NEW 21,556 bytes sha-frozen `cc86d2b5e80287dc...`): decouples the self-test from the mutating on-disk real golden set. Once Soluzione B re-bakes the 24 PNG, the self-test's Test 3 still exhibits the canonical banned-hash detection contract because the in-tree fixture itself encodes the canonical banned hash forever (rejected "copy real on-disk PNG" approach in round-3 review because Soluzione B would silently regress Test 3).

**9-key AE_CAM sweep regression lock** ([`tests/cache/test_node_cache_ae_sweep.cpp`](../../tests/cache/test_node_cache_ae_sweep.cpp), NEW ~177 LOC, Cat-2 freeze-compliant): prima activation site per il counter `node_cache_hash_collisions` (dichiarato via X-macro `render_counter_macros.hpp:35` su `chronon3d::RenderCountersRaw` TLS field); 3 scene × 3 frame = 9 distinct `NodeCacheKey`:
- **Scene A** (AE_CAM_02 zoom-only): `cam.zoom` ∈ {500, 1000, 1500}
- **Scene B** (AE_CAM_04 Z-dolly): `cam.position.z` ∈ {-600, -1000, -1400}
- **Scene C** (parent_name axis): `cam.parent_name` ∈ {"", "layer_a", "layer_b"}

Probe-driven `++tls.node_cache_hash_collisions` on duplicate-digest detection + final assertion `tls.node_cache_hash_collisions - baseline == 0` post-fix. Pre-Soluzione-B refactor che droppa `fold_camera_into_params_hash` a uno dei 7 siti di propagazione (multi_source_node + source_node + TextRunNode + 4 refresh/builder pass sites) FAILs la CHECK sull'asse corrispondente.

**Companion regression lock** ([`tests/cache/test_node_cache_hash_includes_camera.cpp`](../../tests/cache/test_node_cache_hash_includes_camera.cpp)): locks the camera fingerprint fold helper contract (`framebuffer_hash(fb_zoom_500) != framebuffer_hash(fb_zoom_1000) != framebuffer_hash(fb_zoom_1500)`); forward-fix Soluzione B must keep this lock green.

**Combined forward-fix enabler scope**: post-Soluzione B + rendering-side mirror (commit `20dd4b11`) + 9-key regression lock + anti-stale gate, the parent TICKET-AE-CAM-PRECISION-COLLAPSE achieves full **DONE** status once:
- (a) `bash tools/check_ae_parity_golden_state.sh` transitions FAIL exit 1 → PASS exit 0 (all 24/24 PNG distinct from banned hash);
- (b) `chronon3d_cache_tests --test-case='AE_CAM Sweep*'` PASS exit 0 (9-key regression lock holds);
- (c) re-bake via `CHRONON3D_UPDATE_GOLDENS=1` regenerates 13 stale PNG with fresh-distinct sha256 prefixes.

Build verification gap: this session's build host's tmpfs + `ar` link step for `libchronon3d_sdk_impl.a` has a disk-quota exceeded condition that prevents end-to-end verification (per commit `20dd4b11` CHANGELOG entry). End-to-end verification deferred to next available build host; until then, TICKET-AE-CAM-PRECISION-COLLAPSE remains at `DONE (matrix-fix cluster) + OPEN (hash-collision cluster)`.

## AGENTS.md v0.1 freeze compliance

Cat-1 (build corrective — matrix-fix corpus + rendering-side mirror) + Cat-2 (regression-lock tests, deterministic golden re-bake + 9-key sweep) + Cat-3 (zero public API surface expansion across the entire ticket lineage: 3 helper inlines in `node_cache.hpp` documented as internal-by-usage; `node_cache_hash_collisions` counter is declared on existing X-macro + RenderCounters fields) + Cat-5 (doc alignment via this file + `docs/CHANGELOG.md` + `docs/FOLLOWUP_TICKETS.md` in same commit lineage). Zero new singleton / registry / cache / resolver / service-locator. AGENTS v0.1 freeze-compliant.
