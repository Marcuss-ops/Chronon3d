# TICKET-PROJECTION-V1-EXT — Phase 2 follow-ups (type-signature hardening + cross-link + near/far wire-up + real layer bounds)

| Field | Value |
|---|---|
| **ID** | TICKET-PROJECTION-V1-EXT |
| **Pri** | P1 |
| **Area** | Camera V1 cert + projection contract convergence |
| **Parent** | TICKET-PROJECTION-V1 (commit `f60c8c54` on `main@HEAD`, doc-block regression locks landed 2026-07-11) |
| **Sibling** | TICKET-FRAMING-V1 (commit `7463808b` on `main@HEAD`, 5th-stage framing + 7th-stage validation finale) |
| **Code-reviewer verdict** | NEEDS-FIX on 3 items + 1 cross-ticket forward-point (4 sub-tickets total: A / B / C / D) |
| **Blocca** | Camera V1 cert — type-system enforcement of motion-blur-no-recompile invariant; grep-discoverable cross-link to TICKET-FRAMING-V1 §G; near/far configurable wire-up across all 4 legacy paths; real layer bounds for framing solver |
| **Stato** | OPEN |
| **Owner** | next work session |
| **Macchina-verifica** | deferred to working build host (vcpkg `doctest` install + tmpfs quota) per AGENTS.md §honesty |

## Scope

This ticket is the Phase 2 follow-up to TICKET-PROJECTION-V1. The TICKET-PROJECTION-V1 commit `f60c8c54` (subject `feat(camera): unified projection, mblur smp, dof v1`, 51 chars within the 72-char gate) added 2 doc-block regression locks:

1. **Motion-blur-no-recompile doc-block** in `src/scene/camera/camera_v1/internal/shutter_pose_sampler.cpp` — declares the architectural property that `ShutterPoseSampler` is constructed ONCE per `MotionBlurSettings`, the per-tick `evaluator` is a `CameraEvaluatorFn` pre-bound to a PRE-COMPILED `CameraProgram`, and no `compile_camera()` calls happen in the sub-frame loop.
2. **DOF V1 deterministic doc-block** in `src/render_graph/nodes/per_pixel_dof_node.cpp` — declares the 4 determinism invariants (no RNG, no temporal drift, no compilation, no threading-induced non-determinism).

The code-reviewer verdict on commit `f60c8c54` raised 3 NEEDS-FIX items + 1 cross-ticket forward-point. This ticket consolidates all 4 into a single Phase 2 workstream so each lands as 1 atomic commit on `main`.

## Sub-tickets (4 total, each 1 atomic commit on `main`)

### (A) Type-signature hardening for motion-blur-no-recompile invariant

**Current state (doc-block only, prose-level invariant)**: the doc-block at `src/scene/camera/camera_v1/internal/shutter_pose_sampler.cpp` declares that "no `compile_camera()` in the sub-frame loop". The current `CameraEvaluatorFn` is a typedef that may carry a `const CameraDescriptor&` (or a callable that internally references a `CameraDescriptor&`). A future contributor who adds a `compile_camera(...)` call inside the closure body would compile + pass the doc-block (the doc-block is a comment; the type system doesn't enforce it).

**Target state (type-level invariant)**: convert the `CameraEvaluatorFn` from a doc-block-protected contract to a type-system-enforced contract. Two candidate shapes (machine-verified during implementation):

1. **Option (a) — `std::function<Camera2_5D(const CameraEvalContext&)>`**: the closure signature takes ONLY a `CameraEvalContext&` (no `CameraDescriptor&`); a future contributor who tries to call `compile_camera(descriptor, ...)` cannot do so because the `descriptor` is not in scope. The closure must capture the pre-compiled `CameraProgram` (or its evaluation result) at construction time, NOT the `CameraDescriptor` itself.
2. **Option (b) — template parameter on `ShutterPoseSampler`**: `template <typename EvaluatorFn> class ShutterPoseSampler { ... };` with the closure type as a template parameter. The closure type can be a stateless function pointer, a `std::function<...>`, or a user-defined callable. The compile-time guarantee is the same as Option (a) — the closure signature has access to the pre-compiled state, not the descriptor.

**Recommended choice**: Option (a) is the minimum-blast-radius (changes one type alias + the call sites; no template metaprogramming in headers). The change is internal-only (the typedef lives in `shutter_pose_sampler.hpp`, an internal header per the per-pattern `internal/` sub-directory convention).

**Files changed**: `include/chronon3d/scene/camera/camera_v1/internal/shutter_pose_sampler.hpp` (typedef update) + 1-3 call sites in `src/scene/camera/camera_v1/internal/shutter_pose_sampler.cpp` (lambda capture changes) + the doc-block update (removes the prose-level invariant, replaces with the type-level invariant citation).

**Regression lock**: `tests/scene/camera/test_motion_blur_torture_pr1.cpp` (existing) + a new TEST_CASE that verifies a closure with the OLD signature (taking `CameraDescriptor&`) does NOT compile (negative-type test via `static_assert` in a `.cpp` file that is NOT included in the build target; this is the canonical C++ pattern for negative-type tests).

### (B) Cross-link to TICKET-FRAMING-V1 §G in the new doc-block

**Current state (cross-link missing)**: the TICKET-PROJECTION-V1 CHANGELOG entry references "the prior TICKET-FRAMING-V1 lineage" generically, but the 2 new doc-blocks at `shutter_pose_sampler.cpp` + `per_pixel_dof_node.cpp` do NOT cite TICKET-FRAMING-V1 §G (5th-stage framing + 7th-stage validation finale). The 5th-stage framing block is the upstream that the 5-constraint evaluation feeds into; the 7th-stage validation finale is the downstream that catches NaN/Inf in the framing solver output. The dependency is non-obvious from the doc-block alone.

**Target state (grep-discoverable cross-link)**: add a 1-line comment in EACH of the 2 doc-blocks:

```cpp
// TICKET-PROJECTION-V1 doc-block — see TICKET-FRAMING-V1 §G (5th-stage framing + 7th-stage validation finale) for the upstream/downstream contract.
```

The cross-link is in the SOURCE FILE (not just the CHANGELOG) so a future maintainer reading the doc-block via `git grep "TICKET-PROJECTION-V1 doc-block"` immediately finds the upstream/downstream reference without needing to read the CHANGELOG.

**Files changed**: 2 source files (`shutter_pose_sampler.cpp` + `per_pixel_dof_node.cpp`) each get 1 line added at the top of the existing doc-block.

**Regression lock**: `git grep -nE 'TICKET-FRAMING-V1.*§G'` returns ≥ 1 hit (the new cross-link) after the commit lands. A future `tools/check_doc_crosslinks.sh` (NOT in this ticket; deferred to a future doc-governance gate per AGENTS.md §honesty forward-points catalogue) could enforce this pattern.

### (C) Near/far plane + pixel-aspect parity across all 4 legacy paths

**Current state (hardcoded `near_epsilon` + partial pixel-aspect coverage)**: per the THINKER analysis on TICKET-PROJECTION-V1, `camera_projection_contract.hpp:207 + 245` hardcodes `near_epsilon = 1e-4f` instead of consuming configurable physical `near_plane` / `far_plane` properties consistently across all 4 legacy paths:

- `project_world_to_screen` (signature: `(world_point, camera, viewport, near_plane, far_plane) -> screen_point`)
- `project_layer_2_5d` (signature: `(layer, camera, viewport) -> projected_layer`; uses default `near_epsilon` for plane-skip tests)
- `ProjectionContext` (struct field: `near_epsilon` set at construction; not read from `LensModel::near_plane` / `far_plane`)
- `project_2_5d` (signature: `(cam_2_5d, viewport) -> screen_point`; uses default `near_epsilon` for Z-plane tests)

The 4 paths have different `near_epsilon` defaults (some 1e-4f, some 1e-3f, some 1e-5f) and don't consume the physical `near_plane` / `far_plane` from `LensModel`. This is the user-spec's "pixel aspect e near/far risultano ancora parziali" gap.

**Target state (single `ProjectionContractConfig`)**: introduce a `ProjectionContractConfig` struct (single source of truth) with `near_epsilon` (default 1e-4f) + `far_epsilon` (default 1e-4f) + `pixel_aspect_policy` (default `SquarePixels` enum) fields. All 4 legacy paths read from a single config instance (held in `EvaluatedProjection` per the existing pattern). The config is constructed at the camera-program compile time and threaded through the 4 paths via the `ProjectionContext` struct (which becomes `ProjectionContext{config, viewport, ...}`).

**Pixel-aspect parity across all 4 `GateFit` modes**: add explicit matrix-coverage test in `tests/renderer/camera/test_lens_model.cpp` — 4 GateFit modes × 3 pixel_aspect values (1.0 / 1.5 / 2.0) × 2 anamorphic_squeeze values (1.0 / 2.0) = 24 TEST_CASEs. Each TEST_CASE verifies the projected screen rect has the expected aspect ratio per the GateFit semantics (Fill: ignore aspect; Fit: letterbox/pillarbox; Overscan: crop; Stretch: distort).

**Files changed**: `include/chronon3d/math/camera_projection_contract.hpp` (NEW `ProjectionContractConfig` struct) + `include/chronon3d/math/projection_context.hpp` (extend `ProjectionContext` to carry config) + 4 source files (the 4 legacy paths read from config) + `tests/renderer/camera/test_lens_model.cpp` (NEW 24-TEST_CASE matrix).

**Regression lock**: `bash ctest -R lens_model` (existing target) + a new `chronon3d_projection_contract_config_tests` target with the 24-TEST_CASE matrix. Macchina-verifica deferred to working build host.

### (D) Real layer bounds for framing solver (cross-ticket with TICKET-FRAMING-V1)

**Current state (manual table approach)**: per the TICKET-FRAMING-V1 HONEST GAP block, the framing solver reads targets from `descriptor_.base.framing_targets` which the composition author fills in at descriptor-build time (the "manual table" approach). The user-spec asks for "Usa i bounds REALI dei layer (NON tabelle manuali)" — i.e., the framing solver should query the scene's transform tree for the world-bounds of named layers, not read a pre-populated vector.

**Target state (real-bounds resolver)**: add a `world_bounds(layer_name) -> FramingBBox` method to `ResolvedSceneTransforms` (the existing `ctx.transforms` type in `include/chronon3d/scene/model/resolved_scene_transforms.hpp`). The method walks the parent transform chain for the named layer + computes the world-space AABB of the layer's local AABB. In the framing solver, replace the `descriptor_.base.framing_targets` direct read with a query loop: `for (const auto& name : descriptor_.base.framing_target_layer_names) { targets.push_back(ctx.transforms.world_bounds(name)); }`.

**Schema change**: `CameraBaseSpec` drops the `framing_targets` field (the manual table) + adds a new `framing_target_layer_names` field (`std::vector<std::string>`). The 2 fields are mutually exclusive (a descriptor specifies EITHER a manual table OR a layer-name query, not both). The current `framing_targets` field is marked `[[deprecated]]` for 1 release before removal (per the TICKET-FRAMING-V1 §14 forward-only invariant).

**Files changed**: `include/chronon3d/scene/model/resolved_scene_transforms.hpp` (NEW method) + `include/chronon3d/scene/camera/camera_v1/camera_descriptor.hpp` (drop `framing_targets` + add `framing_target_layer_names`) + `src/scene/camera/camera_v1/camera_framing_solver.cpp` (replace direct read with query loop) + `tests/scene/camera/test_world_bounds_resolver.cpp` (NEW test file with 5+ TEST_CASEs covering parent-chain walk + identity transform + nested transforms + non-uniform scale + rotation).

**Regression lock**: NEW `chronon3d_world_bounds_tests` target (UNCONDITIONAL UNIT tier, NOT Blend2D-gated) with 5+ TEST_CASEs. Macchina-verifica deferred to working build host.

## Owner + next step

**Owner**: next work session (after the current doc-block + this followup ticket land).

**Next step** (4 atomic commits, 1 per sub-ticket):

1. Sub-ticket A first (type-signature hardening — the highest-priority NEEDS-FIX because it converts prose to type).
2. Sub-ticket B second (cross-link — smallest, can be lumped with A in 1 commit if user prefers).
3. Sub-ticket C third (near/far + pixel-aspect — the user-spec's "pixel aspect e near/far risultano ancora parziali" forward-point).
4. Sub-ticket D fourth (real layer bounds — the TICKET-FRAMING-V1 forward-point; the largest change).

**Macchina-verifica**: deferred to working build host (vcpkg `doctest` install + tmpfs quota for full cmake build). The 4 sub-tickets can each be code-reviewed + committed independently; the test execution is deferred.

## Cross-references

- [`docs/FOLLOWUP_TICKETS.md`](../FOLLOWUP_TICKETS.md) (this ticket's row in §Open Blockers)
- [`docs/CHANGELOG.md`](../CHANGELOG.md) `## Luglio 2026 — TICKET-PROJECTION-V1` entry (the parent commit `f60c8c54` doc-block changes)
- [`docs/CHANGELOG.md`](../CHANGELOG.md) `## Luglio 2026 — TICKET-FRAMING-V1` entry (the sibling commit `7463808b` 5th-stage framing + 7th-stage validation finale)
- [`src/scene/camera/camera_v1/internal/shutter_pose_sampler.cpp`](../../src/scene/camera/camera_v1/internal/shutter_pose_sampler.cpp) (the motion-blur-no-recompile doc-block; target of sub-ticket A)
- [`src/render_graph/nodes/per_pixel_dof_node.cpp`](../../src/render_graph/nodes/per_pixel_dof_node.cpp) (the DOF V1 deterministic doc-block; target of sub-ticket B cross-link)
- [`include/chronon3d/math/camera_projection_contract.hpp`](../../include/chronon3d/math/camera_projection_contract.hpp) (the `near_epsilon` hardcoding at line 207 + 245; target of sub-ticket C)
- [`include/chronon3d/scene/camera/camera_v1/camera_framing_solver.cpp`](../../include/chronon3d/scene/camera/camera_v1/camera_framing_solver.cpp) (the manual-table read; target of sub-ticket D)
- AGENTS.md §Cat-3 (no gratuitous new public API; sub-ticket A changes an internal typedef; sub-ticket C adds 1 new struct; sub-ticket D adds 1 new method on an existing class — all JUSTIFIED per user-spec verbatim)
- AGENTS.md §honesty (1 documented parent honest gap per the TICKET-PROJECTION-V1 + TICKET-FRAMING-V1 entries; the 4 sub-tickets each document their own honest gap in their commit CHANGELOG entry)

## Non-goal

- claim "AGENTS.md §honesty" *DONE* without macchina-verification;
- expand the new symbols beyond the 4 sub-tickets (no gratuitous `ProjectionContractConfig::operator==` + `hash` unless a future test needs them);
- sub-ticket D's `framing_targets` field removal in the SAME commit as the deprecation (deprecation is the cat-3 forward-only invariant; 1 release wait then remove in a separate commit);
- bridge ACCEPTANCE → CI without addressing first-green (0e) forward-point (out of scope for TICKET-PROJECTION-V1-EXT).
