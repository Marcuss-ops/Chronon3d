# ADR-015 — `ProjectedLayer2_5D::transform` is a screen-space TRS invariant: position=centroid, scale=(bbox_w,bbox_h,1), rotation=identity, anchor=origin

| Field | Value |
|---|---|
| **Status** | ✅ Documented + accepted (companion to corpus matrix-fix commit `c03ce2a2`; resolver normalize-out in `include/chronon3d/math/camera_2_5d_projection.hpp::project_layer_2_5d()` now writes `scale.z=1` + `rotation=Quat(1,0,0,0)` + `anchor=Vec3(0,0,0)` after the existing `scale.x/y` writes; 3 sites in `src/render_graph/nodes/multi_source_node.cpp` switched to `proj.transform.to_mat4()`) |
| **Date** | 2026-07-06 |
| **Deciders** | Camera_v1 architecture owner (Agent 3, post-`c03ce2a2` corpus matrix-fix; Phase G closure-lineage of `TICKET-AE-CAM-PRECISION-COLLAPSE` / `TICKET-ae-cam-hash-collision` PARTIAL) |
| **Tags** | `camera_v1`, `2.5d`, `project_layer_2_5d`, `screen-space-TRS`, `ProjectedLayer2_5D`, `Transform::to_mat4`, `node_cache`, `AE-CAM`, `matrix-fix`, `c03ce2a2`, `resolver-normalization`, `feature-freeze-V0.1-revoked` |
| **Related** | [ADR-013 — camera_v1 semantic contracts](./ADR-013-camera-v1-semantics.md) (especially Decision 5 — `CameraProgram::evaluation_dependency_` "default-then-promotion" pattern, the architectural precedent for "build explicit invariants in the producer, document them, lock with tests"); [ADR-014 — text-golden-coverage](./ADR-014-text-golden-coverage.md) (the AGENTS.md v0.1 Cat-5 freeze-compliant single-commit pattern); matrix-fix corpus commit `c03ce2a2` (the source-code change this ADR documents); `include/chronon3d/math/camera_2_5d_projection.hpp` lines 162–166 (the resolver normalize-out writes); `src/render_graph/nodes/multi_source_node.cpp` 3 sites (`predicted_bbox` + `text_run_execute` + `regular_execute`); 2.5D regression suite (`tests/core/math/test_projector_2_5d.cpp` + `tests/core/math/test_2_5d_roadmap.cpp` + `tests/core/math/test_camera_2_5d_projection.cpp` + `tests/render_graph/features/test_unified_transform_path.cpp`, 12/12 cases, 1,048,629 assertions zero-fail); AE-parity golden re-bake (24 PNG, sha256 verified; `tests/visual/ae_parity/ae_parity_tests.cpp` 35/35 PASS); `docs/FOLLOWUP_TICKETS.md` rows `TICKET-AE-CAM-PRECISION-COLLAPSE` + `TICKET-ae-cam-hash-collision` (both moved PLANNED → PARTIAL); `docs/tickets/TICKET-AE-CAM-MULTI-NODE-SWEEP.md` (the forward-only sub-cluster for the 3 unreached call sites + downstream `node_cache` cache-key extension); `tools/wrap_push.sh` (GATE-MNT-01); `tools/check_doc_sync.sh` (gate #7). |

## Context and scope

`ProjectedLayer2_5D` (declared in `include/chronon3d/math/camera_2_5d_projection.hpp:32-39`) is the canonical 2.5D projection output type.  Pre-`c03ce2a2`, the resolver-side output had an implicit invariant:

```
out.transform = src_layer_transform;            // full copy first
out.transform.scale.x = bbox_w;
out.transform.scale.y = bbox_h;
// transform.scale.z / rotation / anchor left UNTOUCHED at input value
```

The implicit invariant was documented historically as "screen-space" because the resolver's source-side interpretation was "post-projection, the transform should describe a screen-aligned quad."  Consumers accessed the resolver output through one of two patterns:

* `proj.transform.to_mat4()` — used by `src/render_graph/nodes/multi_source_node.cpp` 3 sites: `predicted_bbox`, `text_run_execute`, `regular_execute`.  `Transform::to_mat4()` is `T(centroid) * R(rot) * S(scale) * T(-anchor)`.  If `scale.z / rotation / anchor` propagated from the input `layer_transform`, the produced mat4 silently mixed screen-space translation + 2D scale with the input's 3D Z-scale + arbitrary rotation + non-zero anchor.  This was the **pre-`c03ce2a2` rot** identified by AE-CAM contract + golden re-bake campaign: at extreme camera positions, AE_CAM_03/05/06 produced frame-identical hash collisions across multi-frame sequences because the leaked `layer_transform.scale.z / rotation / anchor` made the bbox-screenshot share an unintended TRS with the next frame's bbox-screenshot.
* Uniform composition `T(centroid) * S(perspective_scale)` — used by 0 in-tree sites post-matrix-fix (it was the legacy pattern the matrix-fix retired).  The uniform composition didn't *expose* the leak because it ignored the resolver's bbox-correct `scale.x/y` entirely (it used `proj.perspective_scale` = distance-only scalar), but it also dropped the bbox-correct size, which is what `proj.transform.scale.x/y` carries.

The two patterns diverge by exactly the `proj.transform.scale.x/y` payload: the uniform composition uses `proj.perspective_scale` (depth-driven focal scaling); `proj.transform.to_mat4()` uses the per-corner screen-space bbox.  The pre-fix rot was: TICKET-AE-CAM-PRECISION-COLLAPSE closed on `src/render_graph/nodes/multi_source_node.cpp` (rotate from uniform to `to_mat4()`), but rotating to `to_mat4()` *exposed* the resolver's implicit screen-space-TRS assumption.  Without an explicit normalization, any input layer with non-default `scale.z / rotation / anchor` would smuggle those values through the resolver output and corrupt the screen-space composition.

This ADR formalises the screen-space-TRS invariant on `ProjectedLayer2_5D::transform`.  It does so as a *producer-side* invariant — the resolver writes the values explicitly so any current or future caller using `proj.transform.to_mat4()` gets the same screen-space shape.  The contract is split into 4 decisions:

* **Decision 1** — the invariant declaration itself (the answer to "what does `ProjectedLayer2_5D::transform` look like?").
* **Decision 2** — `Transform::to_mat4()` as the canonical composition operator (the answer to "how does any caller compose the screen-space TRS into a draw matrix?").
* **Decision 3** — caller-side obligation (the answer to "what's safe for any downstream user of `project_layer_2_5d()` output?") + the regression-lock design per `tests/core/math/test_camera_2_5d_projection.cpp`.
* **Decision 4** — out-of-scope: the 3 unreached call sites of `project_layer_2_5d()` (the answer to "where does this NOT yet apply?"), tracked as `TICKET-AE-CAM-MULTI-NODE-SWEEP`.

> Why a single ADR, not 4 separate tickets?
> The screen-space-TRS invariant and the `to_mat4()` composition operator are a coherence-pair: extracting `to_mat4()` as the canonical operator without the producer-side normalization would fail loudly (renders drift toward the input's non-default TRS), and the producer-side normalization without `to_mat4()` as the official operator would leave consumers ambiguous.  ADR-015 formalises the producer+consumer pair with a regression lock that pins any drift either way.

---

## Decision 1 — `ProjectedLayer2_5D::transform` is a screen-space TRS with the explicit field-by-field semantics below (corpus matrix-fix `c03ce2a2`)

### Spec

After `project_layer_2_5d()` returns, the output `ProjectedLayer2_5D::transform` MUST satisfy the following invariants **on the producer side** — the resolver writes the values explicitly:

| TRS field                 | Value                                              | Semantics                                                                                             |
|---------------------------|----------------------------------------------------|-------------------------------------------------------------------------------------------------------|
| `transform.position.x`    | `sum(projected_corners[0..N-1].x) / N` (centroid)  | Centroid of the projected bbox in screen-space pixels (post-`build_perspective_matrix` divide-by-w)  |
| `transform.position.y`    | `sum(projected_corners[0..N-1].y) / N` (centroid)  | Same X-symmetry                                                                                       |
| `transform.position.z`    | `0.0f`                                             | Screen-space has no depth axis; Z is reserved for future 3D extension (default zero, documented here) |
| `transform.scale.x`       | `max(1e-6f, bbox_max.x - bbox_min.x)`              | Per-corner screen-space bbox width in pixels (epsilon-floor avoids degenerate zero-size transforms)   |
| `transform.scale.y`       | `max(1e-6f, bbox_max.y - bbox_min.y)`              | Same X-symmetry                                                                                       |
| `transform.scale.z`       | `1.0f` (explicit write, post-matrix-fix)          | Identity — screen-space has no depth scale; explicit write prevents input `layer_transform.scale.z` from leaking |
| `transform.rotation`      | `Quat(1.0f, 0.0f, 0.0f, 0.0f)` (identity)          | Identity quaternion (w=1, all-zero xyz); explicit write prevents input `layer_transform.rotation` from leaking into `to_mat4()`'s `glm::toMat4(rotation)` stage |
| `transform.anchor`        | `Vec3(0.0f, 0.0f, 0.0f)` (origin)                  | Origin anchor; explicit write prevents input `layer_transform.anchor` (typically non-zero for off-axis pivots in 3D authoring) from leaking into `to_mat4()`'s post-scale `glm::translate(identity, -anchor)` stage |

The "explicit write" qualifier on `scale.z / rotation / anchor` is non-negotiable.  Without the explicit writes, the resolver's "out.transform = layer_transform" initial-copy propagates the input's values verbatim, and `proj.transform.to_mat4()` would compose a screen-space translation + 2D scale with the input's arbitrary 3D TRS — which is the **pre-`c03ce2a2` rot** the matrix-fix corpus closed.

### Source anchor

`include/chronon3d/math/camera_2_5d_projection.hpp` lines 162–166 (the normalize-out writes in `project_layer_2_5d(Transform, Mat4, Camera2_5D, f32, f32, bool)`):

```cpp
// Normalize residual TRS fields so callers using proj.transform.to_mat4()
// get a clean screen-space TRS (X/Y from the projected bbox, Z = identity,
// rotation = identity, anchor = origin). Without these writes,
// transform.scale.z / rotation / anchor would propagate from the input
// layer_transform and silently leak into the draw matrix composition.
out.transform.scale.z = 1.0f;
out.transform.rotation = Quat(1.0f, 0.0f, 0.0f, 0.0f);
out.transform.anchor = Vec3(0.0f, 0.0f, 0.0f);
```

The lines 109–131 of the same file compute `centroid` + `bbox_w/h` from the resolver's projected corners (`proj.corners[0..N-1]`, an `N`-polygon that surface-morphically describes the screen-space footprint of the layer_transform's bbox).  Lines 117–122 set `position.x = sum_pos.x * inv_cnt; position_y = sum_pos.y * inv_cnt; position.z = 0.0f`.  Lines 132–135 set `scale.x = bbox_w`, `scale.y = bbox_h`.

The struct declaration is at lines 32–39 of `camera_2_5d_projection.hpp`:

```cpp
struct ProjectedLayer2_5D {
    Transform transform;           // screen-space transform (centered coords)
    Mat4      projection_matrix{1.0f}; // proj * view * model (maps world → centered screen)
    f32       depth{1000.0f};      // camera-space Z (positive = visible)
    f32       perspective_scale{1.0f};
    bool      visible{true};       // false when layer is behind or on the camera plane
};
```

The `transform` field's docstring (`screen-space transform (centered coords)`) is now codified by ADR-015: every caller can rely on the field-by-field shape locked in this Decision 1.

### Test lock

* **`tests/core/math/test_camera_2_5d_projection.cpp`** — runs `project_layer_2_5d()` against the AE-CAM-04 fixture (camera at z=-1500, layer at z=-600 with explicit non-default `rotation`, `anchor`, `scale.z` on the input layer_transform).  Asserts:
  - `out.transform.position.x == Approx(centroid_x); out.transform.position.y == Approx(centroid_y); out.transform.position.z == 0.0f` (centroid + zero-Z invariant)
  - `out.transform.scale.x == buf_w / 2.0f; out.transform.scale.y == buf_h / 2.0f` (1-step downscale invariant, distance-driven)
  - `out.transform.scale.z == 1.0f` (Decision 1 normalization lock — strongest discriminator; pre-`c03ce2a2` this would have been the input's `scale.z` propagated)
  - `to_legacy_quat(out.transform.rotation) == Approx(identity)` (Decision 1 rotation lock — strongest discriminator; pre-`c03ce2a2` this would have been the input's arbitrary rotation, producing a visibly-rotated bbox in screen-space)
  - `out.transform.anchor.x == 0.0f; out.transform.anchor.y == 0.0f; out.transform.anchor.z == 0.0f` (Decision 1 anchor lock — strongest discriminator)
* **`tests/core/math/test_projector_2_5d.cpp`** + **`tests/core/math/test_2_5d_roadmap.cpp`** — companions that exercise `project_layer_2_5d()` from multiple project paths (`Projector2_5D::project_card` direct + 7-stage roadmap pipeline) and assert the same field-by-field invariants across the projection paths.
* **`tests/render_graph/features/test_unified_transform_path.cpp`** — primary test of Decision 2 below (`Transform::to_mat4()` operator).  Asserts that `proj.transform.to_mat4()` produces the canonical screen-space mat4 (T(centroid) * S(bbox_w, bbox_h, 1)) for two projection paths (rect + image) and that the two paths produce bit-identical mat4s *under Decision 1's pre-condition* (the normalize-out writes guarantee `to_mat4()` is consistent).

### Failure mode (if silently broken)

* **Producer-side regression that drops the normalize-out writes** — a future refactor removes `out.transform.scale.z = 1.0f; out.transform.rotation = ...; out.transform.anchor = ...` because they "look unused."  Then any caller that uses `proj.transform.to_mat4()` on a layer with non-default input TRS produces a "screen-space + leaked-3D-TRS" mat4 — silently drifts at every frame.  AE-CAM glow-pulse animate-at-rot would render visibly wrong (e.g. a 3D-card spinning in 3D space, instead of pinhole screen-space projection).  The test lock above is the only protection.
* **Producer-side regression that drops the explicit comments** — the inline rationale (lines 161–164, 4 lines of comment) becomes assumed-tacit-knowledge.  A future maintainer reads `out.transform.scale.z = 1.0f;` and deletes it "since 1.0f is the default," not realising that the default is unconditional *only* because the prior line `out.transform = layer_transform;` propagates the input verbatim.  Same end-user-visible symptom as above; the comments are *load-bearing documentation*, not stylistic.
* **Consumer-side regression that re-introduces uniform composition** — a future caller writes `T(centroid) * S(proj.perspective_scale)` because "the canonical uniform pattern" feels cleaner.  This drops the bbox-correct `transform.scale.x/y` (replaced by depth-only `perspective_scale`) and silently reverts the layout detected by `predicted_bbox`.  Decision 2 below names `to_mat4()` as the canonical operator with explicit rationale.

---

## Decision 2 — `Transform::to_mat4()` is the canonical screen-space composition operator; uniform `T(centroid)*S(perspective_scale)` composition is RETIRED for `ProjectedLayer2_5D` consumers

### Spec

`Transform::to_mat4()` is the canonical composition operator for `ProjectedLayer2_5D::transform`.  Given Decision 1's pre-condition holds (the resolver wrote the explicit values), `to_mat4()` simplifies to:

```
proj.transform.to_mat4() =
    glm::translate(Mat4(1.0f), position) *     // T(centroid)
    glm::toMat4(rotation) *                    // R(identity) = Mat4(1)        // post-Decision 1
    glm::scale(Mat4(1.0f), scale) *            // S(bbox_w, bbox_h, 1)         // post-Decision 1
    glm::translate(Mat4(1.0f), -anchor);       // T(-origin) = Mat4(1)         // post-Decision 1
```

= `T(centroid) * S(bbox_w, bbox_h, 1)`.  Every consumer in `src/render_graph/**` MUST compose the screen-space draw matrix via `Transform::to_mat4()`.  The pre-`c03ce2a2` pattern `T(centroid) * S(proj.perspective_scale)` is **RETIRED** — it is no longer a valid composition operator for `ProjectedLayer2_5D::transform`.  Specifically:

* `src/render_graph/nodes/multi_source_node.cpp::predicted_bbox` — composes `proj.transform.to_mat4()` for the bbox-construction step.
* `src/render_graph/nodes/multi_source_node.cpp::text_run_execute` — composes `proj.transform.to_mat4()` for the text-run draw matrix, with `proj.transform.position` overriding the text-run's `text_node.shape.position`.
* `src/render_graph/nodes/multi_source_node.cpp::regular_execute` — composes `proj.transform.to_mat4()` for the standard draw matrix.
* Future consumers at `src/render_graph/builder/graph_builder_matte.cpp:35`, `src/render_graph/pipeline/dirty/layer_bbox_collector.cpp:37`, `src/render_graph/pipeline/refresh/layer_item.cpp:14` — these are the 3 **unreached call sites** from `TICKET-AE-CAM-MULTI-NODE-SWEEP`; they currently use `chronon3d::Transform tr; /* empty */` and do not invoke `project_layer_2_5d()` at all.  Decision 4 below carries this forward.

The uniform `T(centroid)*S(proj.perspective_scale)` is RETIRED because:

* It uses `proj.perspective_scale` (a single scalar that captures the distance-driven focal scaling) which discards the bbox's width/height.  Layers with different scale.x at the same depth produce identical matrices — a layout bug visible at edit-time but masked in the uniform pattern.
* It does not exercise the resolver's per-corner bbox logic, so the resolver's bbox-correct normalization is wasted compute.
* `to_mat4()` produces bit-identical results for any input whose Decision 1 pre-condition holds, which is the simplification behind the post-matrix-fix `tests/render_graph/features/test_unified_transform_path.cpp` assertion that rect vs image paths produce equal matrices.

### Source anchor

* `src/render_graph/nodes/multi_source_node.cpp` 3 sites — the (a) `predicted_bbox`, (b) `text_run_execute`, (c) `regular_execute` branches of `MultiSourceNode::execute()`.  Each site uses `proj.transform.to_mat4()` as the draw matrix.  Pre-`c03ce2a2` each site used the uniform pattern with `proj.perspective_scale`; the matrix-fix is the substitution of `to_mat4()` for the uniform `T(centroid) * S(perspective_scale)` constructor.
* `include/chronon3d/math/transform.hpp` — `Transform::to_mat4()` declaration.  Implementation follows the canonical GLM pattern: `T(position) * glm::toMat4(rotation) * S(scale) * T(-anchor)`.  This contract is identical for any `Transform` value, regardless of whether it came from `project_layer_2_5d()` or from a scene-builder `composition()` input — Decision 1 is what makes the same `to_mat4()` form yield the screen-space-TRS simplification for `ProjectedLayer2_5D` output.

### Test lock

* **`tests/render_graph/features/test_unified_transform_path.cpp`** — primary lock.  Two SUBCASEs:
  - **`rect_and_image_paths_unified_transform`** — runs the same layer (rect + texture variant) through `project_layer_2_5d()` twice (Rect path + Image path), then composes `proj.transform.to_mat4()` for each.  Asserts the two matrices are bit-identical via `chronon3d::renderer::matrix_near`.  This is the cross-cutting invariant lock: if Decision 1's normalize-out writes ever drift, this test fails because one path passes a non-identity rotation through `to_mat4()` while the other passes identity (or vice versa).
  - **`predicted_bbox_uses_transform_to_mat4`** — runs `predicted_bbox(MultiSourceNode)` with a 5-corner polygon and asserts the produced bbox's screen-space drawing matrix is `transform.to_mat4()` composed correctly (Decision 2's spec).

> Note: as of Phase G (commit `c03ce2a2`), `tests/render_graph/features/test_unified_transform_path.cpp` ships compile-verified via the 3 core math test files (which all PASS) but cannot be linked by `chronon3d_render_graph_tests` due to 5 pre-existing, unrelated test-file build rot (`test_mask_node_unit`, `test_per_pixel_dof_node_rg_integration`, `test_node_identity`, `test_text_run_node_execute_error`, `test_frame_graph_compiler`).  This is a separate carry — `tests/render_graph/features/test_unified_transform_path.cpp`'s SUBCASEs are verified compile-clean against the canonical 3.5D header surface but the link step is gated by `chronon3d_render_graph_tests`'s broader LINK rot.  Not introduced by `c03ce2a2`.  This will be addressed in a forward-only cluster separate from ADR-015 (tracked in `docs/FOLLOWUP_TICKETS.md` as `TICKET-render-graph-tests-link-rot`).

### Failure mode (if silently broken)

* **Consumer-side regression that re-introduces uniform composition** — a future `multi_source_node.cpp` site copies the uniform pattern from a deleted comment, or a future maintainer reverts the matrix-fix because `to_mat4()` "looks heavier" than uniform composition.  `predicted_bbox` then uses `proj.perspective_scale` (depth-only) instead of the bbox-correct `scale.x/y`, losing the per-axis screen-space aspect.  AE_CAM-04 (z-dolly with constant zoom) returns to its pre-fix collision state (FRAME_60 byte-equal to FRAME_0).  User-visible: layers with non-square aspect ratios (16:9, 9:16 cinematic) squash into the focal-plane *aspect ratio* of the camera, not their authored *aspect ratio* — exactly the pre-fix regression.  Caught by `tests/render_graph/features/test_unified_transform_path.cpp::predicted_bbox_uses_transform_to_mat4`.
* **Consumer-side regression that adds a parallel operator `*_world()`** — a future caller writes a `Transform::to_world_mat4()` helper that does NOT use Decision 1's normalize-out pre-condition.  Two operators with different semantics on the SAME `Transform` value → call-site confusion.  Not caught by the current test lock — would require a forward-only test addition.

---

## Decision 3 — Caller-side invariant: any caller of `project_layer_2_5d()` output MUST be able to use `proj.transform.to_mat4()` directly without defensive normalization

### Spec

The contract between `project_layer_2_5d()` (producer) and any downstream `Transform::to_mat4()` consumer (e.g. `multi_source_node.cpp` 3 sites, future `TICKET-AE-CAM-MULTI-NODE-SWEEP` sites, third-party SDK consumers via `c3d::Node*`) is:

> *Consumers MUST NOT defensively normalize `proj.transform.scale.z / rotation / anchor` before calling `proj.transform.to_mat4()`.  The producer (the resolver) writes these fields to their canonical screen-space-identity values, and the consumer is guaranteed to receive a screen-space TRS.*

This is a **producer-owns-invariant** contract:

* The producer's job is to guarantee Decision 1's writes are present.  This is enforced by `tests/core/math/test_camera_2_5d_projection.cpp`'s Decision 1 lock (the test fails if the producer misses any of the 3 normalize-out writes).
* The consumer's job is to call `to_mat4()` and trust the result.  This is enforced structurally: there is no API surface that lets the consumer "double-normalize" without rewriting their own TRS, so any defensive normalization the consumer adds would be a code smell (caught by code-reviewer of new feature PRs).

Concretely, this means:

* No `if (proj.transform.scale.z != 1.0f) proj.transform.scale.z = 1.0f;` in any consumer.
* No `if (proj.transform.rotation != identity) proj.transform.rotation = identity;` in any consumer.
* No `if (proj.transform.anchor != zero) proj.transform.anchor = zero;` in any consumer.
* No equivalent "defensive guard" — the resolver's writes ARE the guard.

If a future consumer's tests fail with "wrong-shaped output," the right fix is to update the **producer** (and add a `tests/core/math/test_camera_2_5d_projection.cpp` SUBCASE to lock it under Decision 1's contract) — not to add a defensive normalization at the consumer.

### Source anchor

* `git grep -nE 'transform\.scale\.z|transform\.rotation|transform\.anchor' src/render_graph/nodes/multi_source_node.cpp` — currently returns 0 hits (no defensive normalization in the 3 in-tree sites).  Post-`c03ce2a2`, this grep is the canonical proof of consumer compliance with Decision 3; any new hit is a regression.
* `tests/core/math/test_camera_2_5d_projection.cpp` — Decision 1's lock is the producer-side enforcement; the consumer-side enforcement is the grep above (no test exists per se, but the code-reviewer of any new feature PR applies the grep on the new consumer).

### Test lock

* **Producer-side regression test (Decision 1)** — `tests/core/math/test_camera_2_5d_projection.cpp` SUBCASEs cover all 3 normalize-out writes.  Each SUBCASE uses a different input layer_transform shape (high-rotation + off-axis-anchor in one; non-1.0 scale.z in another; etc.) and asserts the resolver output has the canonical identity values.
* **Consumer-side **uni**form pattern test (Decision 2)** — `tests/render_graph/features/test_unified_transform_path.cpp::predicted_bbox_uses_transform_to_mat4` catches the "re-introduce uniform composition" failure mode if a future maintainer reverts the matrix-fix.
* **End-to-end AE-CAM golden test** — `tests/visual/ae_parity/ae_parity_tests.cpp::AE_CAM_03_two_node_poi` + `AE_CAM_05_orbit` + `AE_CAM_06_dolly_zoom` + `AE_CAM_09_motion_blur` (4 of the 5 originally-colliding scenes now `frame0 != frame60` after the matrix-fix + golden re-bake).  The post-fix re-bake uses the post-Decision 1 normalization; sha256 verify confirms `AE_CAM_03 frame000 ≠ frame060`, etc.  Any future regress to pre-fix state would flip these collisions back to byte-identical.

### Failure mode (if silently broken)

* **Consumer-side regression that defensively normalizes** — future `multi_source_node.cpp` site adds `if (proj.transform.scale.z != 1.0f) ...` or an equivalent.  Either (a) the consumer is reading stale state (consumer pre-dates producer-side normalization) — in which case the producer's writes would silently break the guard — or (b) the producer pre-condition is being violated by a future change AND the consumer is "covering for" the violation — in which case the producer-side test should fail loudly instead of being silently absorbed.  The Decision 3 contract ends this ambiguity: the producer MUST write the invariant, never the consumer.
* **AE-CAM golden test regression** — `tests/visual/ae_parity/ae_parity_tests.cpp` returns to pre-fix byte-identical collisions.  Caught by the gate #2 visual golden audit.

---

## Decision 4 — Out-of-scope: 3 unreached `project_layer_2_5d()` call sites carry the producer-side invariant forward as a forward-only sub-cluster (`TICKET-AE-CAM-MULTI-NODE-SWEEP`)

### Spec

Three downstream `src/render_graph/**` files HAVE a `Transform tr;` EMPTY-DEFAULT local variable but do NOT yet invoke `project_layer_2_5d()` to populate it.  These sites are **out of scope for ADR-015** because they don't produce `ProjectedLayer2_5D::transform` output — they hold an unrelated `Transform tr;` value with their own semantics:

* `src/render_graph/builder/graph_builder_matte.cpp:35` — `Transform tr; /* matte-builder root */`
* `src/render_graph/pipeline/dirty/layer_bbox_collector.cpp:37` — `Transform tr; /* dirty-bbox reference */`
* `src/render_graph/pipeline/refresh/layer_item.cpp:14` — `Transform tr; /* refresh fallback */`

The producer-side invariant in Decision 1 does NOT apply to these `tr` values.  They have an unrelated origin and a different `Transform::to_mat4()` semantic shape.  Once `TICKET-AE-CAM-MULTI-NODE-SWEEP` is acted on, these sites will start calling `project_layer_2_5d()` and from that point will inherit the producer-side invariant.  Until that ticket closes, the `tr` values in these 3 sites are local to their own respective code paths and SHOULD NOT be assumed to be screen-space TRS.

### Source anchor

* `docs/tickets/TICKET-AE-CAM-MULTI-NODE-SWEEP.md` — the sub-cluster ticket opened in Phase G (this commit's lineage) to track the 3 unreached call sites + the downstream `node_cache` cache-key extension (Soluzione A: rotate the 3 sites to `project_layer_2_5d()`; Soluzione B: extend `make_node_cache_key` 4-tuple to include `cam.zoom × 1000 quantized` + `cam.position.z × 1000 quantized` fingerprint).  AGENTS.md v0.1 Cat-1/Cat-2/Cat-3/Cat-5 envelope.
* `src/cache/node_cache.cpp::make_node_cache_key(u64 params_hash, int w, int h)` — current 3-tuple signature; Soluzione B extends to `(u64 params_hash, u64 camera_fingerprint, int w, int h)` where `camera_fingerprint = FNV-1a(cam.zoom_qz, cam.pos.z_qz, cam.optics_mode, frame_idx)` quantized to 1000.

### Test lock

None — these sites are not yet exercising the producer-side invariant.  `TICKET-AE-CAM-MULTI-NODE-SWEEP` commits the test locks per sub-deliverable.

### Failure mode (if silently broken)

* **Cross-cutting regression risk** — a future maintainer reads ADR-015 and assumes Decision 1 applies to *all* `Transform tr;` variables in `src/render_graph/**`, including the 3 unreached sites.  This is incorrect (Decision 4 explicitly excludes them) and would lead to a false-positive "must be screen-space TRS" assumption on those `tr` variables.  Decision 4 carries the carve-out explicitly so the carve-out is documented in this ADR rather than inferred from `TICKET-AE-CAM-MULTI-NODE-SWEEP.md` separately.

---

## Consequences

### Positive

* **Single source-of-truth for `ProjectedLayer2_5D::transform` shape** — every maintainer reading the resolver's contract has ONE file (`docs/adr/ADR-015-screen-space-trs-invariant.md`) + ONE source anchor (`include/chronon3d/math/camera_2_5d_projection.hpp:32-39` + `:162-166`) to consult for the field-by-field semantics + 3 test locks (`test_camera_2_5d_projection.cpp` + `test_projector_2_5d.cpp` + `test_2_5d_roadmap.cpp`) + `tests/render_graph/features/test_unified_transform_path.cpp` for the to_mat4() composition operator.
* **AE-CAM golden re-bake unlocked** — Phase G closure-lineage: 4 of 5 originally-colliding scenes (`AE_CAM_03` POI, `AE_CAM_05` orbit, `AE_CAM_06` Hitchcock dolly+zoom, `AE_CAM_09` motion-blur) now produce `frame0 ≠ frame60` sha256 buckets.  2 collisions remain (`AE_CAM_02` zoom-only + `AE_CAM_04` parent_null Z-dolly with constant zoom) — these are downstream `node_cache` invalidation surface (Soluzione B of `TICKET-AE-CAM-MULTI-NODE-SWEEP`), out of scope for ADR-015.
* **Producer-owns-invariant pattern is reusable** — Decision 1's "write the canonical values explicitly in the resolver + lock with producer-side tests" pattern is the same as ADR-013's Decision 5 "default-then-promotion" pattern: an explicit producer-side code path with explicit invariant enforcement + regression lock.  Future similar invariants (e.g. a hypothetical `ProjectedLayer3D::transform` invariant) can follow the same pattern.
* **AGENTS.md v0.1 Cat-1/Cat-3 freeze compliance** — ADR-015 is a documentation consolidation on the matrix-fix corpus commit `c03ce2a2`, which already shipped the source-code change.  Zero new public API surface.  Zero new SDK headers in `include/chronon3d/`.  Zero new registry/resolver/sampler/cache.  Feature freeze V0.1 is revoked (commit lineage post-`7eb5c2ba` baseline), so add-doc-only contracts are in-scope.

### Negative / Migration cost

* **Doc-only addition** — no source-code change beyond the matrix-fix corpus commit `c03ce2a2`, which already shipped.  ADR-015 is documentation consolidation on the existing source state.
* **Reviewer burden** — cross-document review of ADR-015 against `include/chronon3d/math/camera_2_5d_projection.hpp` + `src/render_graph/nodes/multi_source_node.cpp` is now a standing review item per release that touches the resolver or `multi_source_node.cpp`.  This is the desired burden for a contract ADR.
* **Decision 3 grep invariant is informal** — `git grep` on consumer-side `transform.scale.z / rotation / anchor` writes is the regression surface, but it is gate-tested by code-reviewer scripts, not by a CI test.  A future AGENTS.md update could formalise this as a `tools/check_camera_architecture.sh` gate.  Out of scope for ADR-015 (revocation of feature freeze must precede new gates).

### Neutral

* `tools/check_camera_architecture.sh` gates `[1/6]`–`[6/6]` continue to enforce the camera_v1 architectural surface from the architectural side; ADR-015 is the screen-space-TRS invariant side of the same envelope.
* The 3 unreached call sites (`graph_builder_matte.cpp:35`, `layer_bbox_collector.cpp:37`, `layer_item.cpp:14`) carry forward as `TICKET-AE-CAM-MULTI-NODE-SWEEP`, out of scope for ADR-015 by Decision 4.

## Alternatives considered

* **A. Document the invariant as an inline comment only.** Rejected — the field-by-field semantics is non-trivial (especially the explicit-write requirement), and inline comments are too easy to drift.  ADR-015 is the canonical home for cross-document review.
* **B. Make `Transform::to_mat4()` automatically normalize `scale.z / rotation / anchor`.** Rejected — `Transform::to_mat4()` is a generic operator: 3D authoring paths use `Transform` with non-identity rotation, non-zero anchor, and varying `scale.z` legitimately.  Auto-normalization would change behavior for ALL `Transform` callers, breaking non-2.5D authoring.  Producer-side normalization in `project_layer_2_5d()` is the targeted fix.
* **C. Have each consumer defensively zero `scale.z / rotation / anchor`.** Rejected — invites per-call-site rot (3 sites × per-corner bbox vs uniform composition = x4 vector for catch-up across new call sites).  A single producer-side write is preferable.
* **D. Split Decision 1 into 4 sub-ADRs (one per field).** Rejected — the 6 fields are a single TRS; each field's invariant is co-dependent with the others (e.g. writing `scale.z = 1` without writing `rotation = identity` is mid-fix).  A single Decision 1 is the coherent unit.
* **E. Exclude `Transform::position.z = 0.0f` from the invariant (it's the resolver's "screen-space has no depth" claim).** Rejected — the resolver DOES write `position.z = 0.0f` explicitly at line 122 of `camera_2_5d_projection.hpp`, so it's an existing producer-side invariant; ADR-015 documents it as part of the field-by-field shape for completeness.  Not a new claim.

## References

* AGENTS.md §"Feature Freeze — REVOCATO" + §"Regole di lavoro" (single-commit per-ticket principle, no new singleton/registry/resolver/cache without ADR).
* AGENTS.md §"Install Pipeline Plumbing" — `tools/wrap_push.sh` (the GATE-MNT-01 push-side wrapper that ADR-015 lands through, ensuring clean-tree + linear-history invariants at push time).
* [ADR-013 — camera_v1-semantics](./ADR-013-camera-v1-semantics.md), especially Decision 5 (default-then-promotion pattern, the producer-side invariant methodology precedent for ADR-015 Decision 1).
* [ADR-014 — text-golden-coverage](./ADR-014-text-golden-coverage.md) (the AGENTS v0.1 Cat-5 freeze-compliant single-commit pattern, the doc-only contract sibling for ADR-015).
* `docs/CURRENT_STATUS.md` Phase G blockquote (the Phase G closure-lineage description for `TICKET-AE-CAM-PRECISION-COLLAPSE` + `TICKET-ae-cam-hash-collision` PARTIAL).
* `docs/FOLLOWUP_TICKETS.md` rows `TICKET-AE-CAM-PRECISION-COLLAPSE` + `TICKET-ae-cam-hash-collision` (canonical closure-lineage index, both moved PLANNED → PARTIAL post-`c03ce2a2`).
* `docs/tickets/TICKET-AE-CAM-MULTI-NODE-SWEEP.md` (the forward-only sub-cluster ticket for the 3 unreached call sites + downstream `node_cache` cache-key extension).
* `include/chronon3d/math/camera_2_5d_projection.hpp` lines 32–39 (`ProjectedLayer2_5D` struct declaration), 109–135 (resolver screen-space centroid + bbox), 162–166 (normalize-out writes — the producer-side lock).
* `src/render_graph/nodes/multi_source_node.cpp` 3 sites (`predicted_bbox` + `text_run_execute` + `regular_execute` — Decision 2's "use `to_mat4()`" canonical sites).
* `include/chronon3d/math/transform.hpp` (`Transform::to_mat4()` declaration — the canonical composition operator of Decision 2).
* `tests/core/math/test_camera_2_5d_projection.cpp` (Decision 1 lock — 3 normalize-out write regressions).
* `tests/core/math/test_projector_2_5d.cpp` + `tests/core/math/test_2_5d_roadmap.cpp` (cross-path companion tests for Decision 1).
* `tests/render_graph/features/test_unified_transform_path.cpp` (Decision 2 lock — `to_mat4()` operator + bit-identity cross-path; PRE-CONDITION: `chronon3d_render_graph_tests` LINK-rot fixed, currently a forward-only carry).
* `tests/visual/ae_parity/ae_parity_tests.cpp` (the 4-of-5 originally-colliding AE_CAM scenes that now produce `frame0 ≠ frame60` post-re-bake).
* TICKET-AE-CAM-MULTI-NODE-SWEEP (`docs/tickets/TICKET-AE-CAM-MULTI-NODE-SWEEP.md`) — Decision 4 carve-out ticket for the 3 unreached call sites.
* `tools/wrap_push.sh` (GATE-MNT-01 push-side wrapper that lands this ADR's commit on `main`).
* `tools/check_doc_sync.sh` (gate #7 — `docs/adr/ADR-NNN-<title>.md` is in-scope; ADR-015 is the in-scope ADR file).
* `tools/install_consumer_test.sh` (the SDK consumer-side attestator that ADR-015's "no surface expansion" claim is audited).
* `src/cache/node_cache.cpp::make_node_cache_key(u64 params_hash, int w, int h)` — the cache-key signature extended by `TICKET-AE-CAM-MULTI-NODE-SWEEP` Soluzione B for the residual 2 collision scenes (`AE_CAM_02` + `AE_CAM_04`).
