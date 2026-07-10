# ADR-011 — Delete legacy camera surface (AnimatedCamera2_5D, both CameraRig namespaces, CameraShotProfile, camera_descriptor_adapters); keep `camera_v1::CameraDescriptor` canonical; remove `projection_mode` from `Camera2_5D`

| Field | Value |
|---|---|
| **Status** | 📋 Documented (F2.3 design recorded; deletion pending dedicated F2.3.X workstream) |
| **Date** | 2026-06-23 |
| **Deciders** | F2.3.1 owner (chronon3d camera authorship maintainers) |
| **Tags** | `camera`, `canonicalisation`, `F2.3`, `deletion`, `SDK-surface`, `legacy-retire` |
| **Related** | [ADR-008 — render-engine-renderer-ptr](./ADR-008-render-engine-renderer-ptr-return.md), [ADR-010 — boundary-gate-semantic-extension](./ADR-010-boundary-gate-semantic-extension.md), `docs/camera-plan/01-CANONICAL_CAMERA_ARCHITECTURE.md`, TICKET-029/033/034 (camera_v1 rot history), TICKET-035 (rich Camera2_5D field + PIMPL re-light), AGENTS.md §"Piano operativo canonico" + §"Regole di lavoro", `tools/check_camera_architecture.sh` gates `[1/6]`+`[2/6]` |

## Context

Chronon3d's camera model has accumulated legacy surface during the camera_v1 canonicalisation (TICKET-033 layer-payload + TICKET-034 default-camera-on-composition + TICKET-029 rot-closure).  After those merges the canonical authoring + runtime surface is:

* `camera_v1::CameraDescriptor` — full `Camera2_5D`-shaped authoring payload (parent_name, target_name, point_of_interest, DoF, Lens, MotionBlur, ZoomProjection/FovProjection).  Reference: `include/chronon3d/scene/camera/camera_v1/camera_descriptor.hpp` + `Composition::default_camera_descriptor(...)` at `include/chronon3d/timeline/composition.hpp`.
* `camera_v1::CameraProgram` — canonical compiled-program runtime.
* `camera_v1::CameraDescriptor` as the `Composition` default surface (per TICKET-034: `Composition::default_camera_descriptor(...)` + `redecompose_camera_from_descriptor(time)`).

The following legacy surface is left over from the pre-canonicalisation era and creates real but avoidable maintenance, SDK-surface, and audit-burden issues:

1. **`AnimatedCamera2_5D`** (`include/chronon3d/scene/camera/animated_camera_2_5d.hpp:22`) — animated wrapping of `Camera2_5D`. **92 in-tree matches** across `tests/`, `content/2d5/`, `src/scene/builders/camera_api.cpp`, `include/chronon3d/scene/builders/{camera_api.hpp, scene_builder.hpp}`, `tools/check_camera_architecture.sh` gate `[1/6]`.
2. **`chronon3d::CameraRig`** (root-namespace, `include/chronon3d/scene/model/camera/camera_rig.hpp:97`) and **`camera_rig::CameraRig`** (legacy nested-namespace variant) — two parallel rig type declarations sharing the same intent.  **174 in-tree matches**; not reachable from the canonical `camera_v1::*` authoring surface.  The dual-namespace fork is itself a rot vector.
3. **`CameraShotProfile`** (`include/chronon3d/scene/model/camera/camera_shot_profile.hpp:9`) — shot-level timing/profiling struct superseded by `camera_v1::CameraDescriptor` (which carries motion-blur/DoF/Lens directly on the descriptor).  **51 in-tree matches** in `content/2d5/compositions/*` + `tests/scene/*`.
4. **`camera_descriptor_adapters.{hpp,cpp}`** (`include/chronon3d/scene/camera/camera_v1/camera_descriptor_adapters.{hpp,cpp}` + `src/scene/camera/camera_v1/camera_descriptor_adapters.cpp`) — adapter layer bridging pre-canonical descriptor formats; once items 1–3 delete, adapters have zero in-tree consumer.  **20 in-tree matches**.
5. **`Camera2_5D::projection_mode`** field (`include/chronon3d/scene/model/camera/camera_2_5d.hpp:105`) — separate enum (`Camera2_5DProjectionMode::Fov | Zoom`) parallel to the canonical `Lens` specifier.  **106 in-tree matches** across `tests/`, `content/2d5/`, `src/scene/camera/{camera_rig.cpp, camera_program.cpp, camera_null_rig.cpp}`, `tools/replace_script.py`, math contract, render-graph.  The `optics_mode` + `projection_mode` dual axis was the documented source of TICKET-007 rot + the stop-gap TICKET-007 fix `074eb1d1 "prioritize FOV over optics-mode in focal_from_camera"`.

This legacy surface:

* Inflates the SDK public surface visible to `install_consumer_test` (defeats ADR-008's intent and `[14/14]` boundary-gate's attestation per ADR-010).
* Creates a fork vector where legacy code-path bugs diverge from canonical code-path bugs.
* Prevents F3.x close-out — `[14/14]` cannot attest "all camera consumers use `camera_v1::CameraDescriptor` only" with these legacy types alive.
* `tools/check_camera_architecture.sh` already runs 6 gates against the legacy surface — gate `[1/6] AnimatedCamera2_5D` and gate `[2/6] CameraRig` are blocking, with bridge-path sanction lists that survive only because items 1 + 2 still exist.

## Decision

Execute F2.3 — the canonicalisation close-out — on a dedicated F2.3.X workstream.  Each of the 5 deletion targets is enumerated below with its migration contract.

### Decision 1. `AnimatedCamera2_5D` → DELETE

* **Delete** `include/chronon3d/scene/camera/animated_camera_2_5d.hpp` (declaration `struct AnimatedCamera2_5D` at line 22; ~92 in-tree matches).
* **Migrate call-sites**: every `AnimatedCamera2_5D cam` literal → `camera_v1::CameraDescriptor` + keyframes (via `keyframes()` builder); `AnimatedCamera2_5D::evaluate(time)` → `camera_v1::CameraProgram::evaluate(time)`.
* **Delete authoring entry points**: `CameraApi::set_animated(...)` (`include/chronon3d/scene/builders/camera_api.hpp:18` + `src/scene/builders/camera_api.cpp:11`), `SceneBuilder::animated_camera(cam)` (`include/chronon3d/scene/builders/scene_builder.hpp:82-83`), `TimelineBuilder::with_camera(const AnimatedCamera2_5D&)` (`include/chronon3d/timeline/timeline_builder.hpp:42,60` + `src/scene/timeline_builder.cpp:36`).
* **Delete content/ fixtures** that construct `AnimatedCamera2_5D cam;` directly; migrate to `camera_v1::CameraDescriptor` + keyframes.

### Decision 2. `chronon3d::CameraRig` + `camera_rig::CameraRig` → DELETE (both namespaces)

* **Delete** `include/chronon3d/scene/model/camera/camera_rig.hpp` (declarations: `struct CameraRig` line 97, `enum class CameraRigMode` line 28, `CameraRigDOF` line 53, `CameraRigMotionBlur` line 87), `include/chronon3d/scene/camera/camera_rig_builder.hpp` (`CameraRigBuilder` class).
* **Delete implementations**: `src/scene/camera/camera_rig.cpp` (`CameraRig::evaluate()` etc. at lines 9, 28, 228, 280, 297), `src/scene/camera/camera_path_sampler.cpp` (sample overloads taking `const CameraRig&`), `src/scene/camera/camera_rig_presets.{hpp,cpp}` (the 6 ergonomic presets).
* **Delete ergonomic API**: `include/chronon3d/scene/camera/camera_rig_animated_presets.hpp` (the inline-evaluated `AnimatedCamera2_5D`-returning helpers — transitively deleted via Decision 1).
* **Migrate SceneBuilder camera_rig()**: `s.camera_rig("name", [=](CameraRigBuilder& rig){...})` (`include/chronon3d/scene/builders/scene_builder.hpp:281-288`) → directly set `camera_v1::CameraDescriptor` on `Composition::default_camera_descriptor(...)`.

### Decision 3. `CameraShotProfile` → DELETE

* **Delete** `include/chronon3d/scene/model/camera/camera_shot_profile.hpp` (declaration `struct CameraShotProfile` line 9).
* **Migrate call-sites**: `content/2d5/compositions/*` (`CameraShotProfile shot;` literals in ~25 files), `tests/scene/camera/test_camera_descriptor_adapters.cpp`, `content/2d5/compositions/camera_test_orchestrator.{hpp,cpp}`, `content/2d5/compositions/camera_calibration_scene.hpp`, `content/anims/compositions/ae_camera_text_parity.cpp:200` (comment reference).
* **Migration target**: each `CameraShotProfile shot;` is process-internal in `content/2d5/`, so the orchestrator reshapes to composition-API-only call-sites that pass `camera_v1::CameraDescriptor` directly.

### Decision 4. `camera_descriptor_adapters.{hpp,cpp}` → DELETE (entire files)

* **Delete** `include/chronon3d/scene/camera/camera_v1/camera_descriptor_adapters.hpp` (header) + `src/scene/camera/camera_v1/camera_descriptor_adapters.cpp` (impl, lines 1–269+).
* **Delete tests**: `tests/scene/camera/test_camera_descriptor_adapters.cpp` and its `tests/scene_tests.cmake` registration line.
* **Migration**: `tests/scene/camera/test_camera_compiled_evaluate.cpp` keeps parity tests but reshapes to bake pure-descriptor round-trips (no adapters).
* **Remove CMake entry**: `src/scene/camera/camera_v1/CMakeLists.txt:29 src_camera_v1_camera_descriptor_adapters.cpp` line deletion.

### Decision 5. `Camera2_5D::projection_mode` field → DELETE

* **Delete the field + enum**: `Camera2_5DProjectionMode projection_mode{...}` declaration at `include/chronon3d/scene/model/camera/camera_2_5d.hpp:105` + the enum class `Camera2_5DProjectionMode`.  Replace with discrimination via `camera_v1::Lens` (which already canonicalises Projection vs FieldOfView via `LensSpec` discriminated union).
* **Delete helpers**:
  * `CameraProjectionSource::get_projection_mode()` (`include/chronon3d/scene/model/camera/camera_projection_source.hpp:52`).
  * `camera_rig.cpp:34,239` writes — file deleted via Decision 2.
  * `camera_null_rig.cpp:35` write — file follow-up deletion (transitively via Decision 2).
* **Delete motion-path projection_mode field**: `catmull_rom_path.hpp:500,512,524,558,567` + `spatial_bezier_path.hpp:324,339,352,392,402` — the `CameraMotionPath projection_mode` field on those structs.
* **Migrate change-policy + tools**:
  * `render_graph/pipeline/camera_change_policy.cpp:24` (diff logic).
  * `tools/replace_script.py:11,119` (diff script).
  * `src/backends/software/diagnostics/nulls_overlay.cpp:25` (overlay branch).
* **Migrate math contract**: `include/chronon3d/math/camera_2_5d_projection.hpp:49` (test helper), `include/chronon3d/math/camera_projection_contract.hpp:93-113` (rules mirroring legacy `focal_from_camera`).
* **Synthetic-test fixture mass-rewrite**: `tests/core/math/test_projector_2_5d.cpp`, `tests/core/math/test_camera_2_5d_projection.cpp`, `tests/core/math/test_camera_projection_resolver.cpp`, `tests/core/math/test_camera_projection_geometry_safety.cpp`, `tests/renderer/camera/test_lens_model.cpp`, `tests/scene/camera/test_catmull_rom_path.cpp`, `tests/scene/camera/test_camera_program_compiled.cpp`, `tests/scene/camera/test_camera_projection_contract.cpp`, `tests/scene/camera/camera_25d_tests.cpp`, `tests/scene/camera/test_camera_motion_path.cpp`, `tests/scene/camera/test_camera_descriptor_adapters.cpp`, `tests/renderer/perf/test_motion_blur_integration.cpp` — every `cam.projection_mode = ...` assignment + every `CHECK(cam.projection_mode == ...)` assertion rewrites to `camera_v1::Lens` discriminated assertions.

### Decision 6. Migration sequencing (separate workstream)

Per AGENTS.md's "no mixed refactors" rule, the code rotation is split into distinct phases on distinct branches:

* **Phase 1 (this ADR)**: `p2/f2.3.1-adr-camera-legacy-deletion` (this branch) writes this ADR doc + INDEX entry.  **NO CODE**.
* **Phase 2 (call-site dry-run)**: new branch `p2/f2.3.2-...-call-site-migration` — for each deletion target, replace usages with `camera_v1::*` equivalents via a content-preserving mechanical pass.  Build + tests should pass on this branch even before deletion commits.
* **Phase 3 (declaration deletion)**: branch `p2/f2.3.3-...-declaration-deletion` — remove the named files + struct declarations.  Build fails loudly for any leftover consumer; iterate until clean.
* **Phase 4 (boundary-gate promotion)**: branch `p2/f2.3.4-...-gate-promotion` — once items 1–5 land, gate `[14/14]` flips ADVISORY → BLOCKING per ADR-010; `tools/check_camera_architecture.sh` gates `[1/6]`+`[2/6]` drop sanctioned bridge exemptions; the gate becomes "zero usages" pure.

## Consequences

### Positive

* **Single canonical camera descriptor surface**: `camera_v1::CameraDescriptor` only.  Boundary gate `[14/14]` finally attestable.
* **No dual-namespace fork**: `chronon3d::CameraRig` + `camera_rig::CameraRig` rotate together; consumers migrate to `camera_v1` in one editorial pass, no "which-namespace" decision surface.
* **`Camera2_5D` projection locked via `Lens`**: removes the `optics_mode` + `projection_mode` parallel axis and the dangling-state bugs it historically produced (TICKET-007 was a stop-gap; this ADR closes the gap permanently).
* **Compile-time surface shrinks**: 6 fixture files (`camera_rig_animated_presets.hpp`, `camera_rig_presets.cpp`, `camera_api.{hpp,cpp}`, `camera_null_rig.cpp`, `camera_rig_builder.hpp`, `camera_descriptor_adapters.{hpp,cpp}`) delete; less include-graph weight.
* **Boundary gate `[1/6]`+`[2/6]`** simplify: sanctioned-path exception lists drop after F2.3 closes; the gates become pure "zero usages" without bridge exemption.

### Negative / Migration cost

* **Mass in-tree edit**: ~25 `content/2d5/compositions/*` files rebase on `camera_v1::CameraDescriptor`; ~6 fixture/test files delete; ~10 include/scenic headers delete.  Multi-PR but bounded.
* **`Camera2_5D`-breaking struct change**: removing `projection_mode` changes binary layout.  Bump `chronon3d::serialization::kCamera2_5D_LayoutVersion` magic + re-run golden-image diff suite.  External consumers that deserialise `Camera2_5D` directly (vs the `camera_v1::CameraDescriptor` typed wrapper) MUST migrate.
* **`CameraPathSampler` API change**: `sample(const CameraRig& rig, ...)` loses its rig overload.  New overloads take `camera_v1::CameraDescriptor` + keyframe list.
* **Synthetic test fixtures mass-rewrite**: ~30 test files' `cam.projection_mode = ...` lines + `CHECK(cam.projection_mode == ...)` assertions rewrite to `camera_v1::Lens` discriminated form.

### Neutral

* `animation::AnimatedXxx` remains the generalised animation API; only `AnimatedCamera2_5D` disappears.
* Golden-image test fixtures regenerate and need re-baselining.

## Migration path

This ADR documents the F2.3 design; the actual code rotation is a separate F2.3.X workstream per `Decision 6`.  The contract for each deletion branch:

* **Header rotation order** (Phase 3): `animated_camera_2_5d.hpp`, `camera_rig.hpp`, `camera_shot_profile.hpp`, `camera_descriptor_adapters.{hpp,cpp}` are DELETED first; their consumers then fail to compile, driving the call-site migration list (Phase 2).
* **Fixture/test rotation**: `tests/scene/camera_rig_tests.cpp`, `tests/renderer/camera/test_camera_rig.cpp`, `tests/scene/camera/test_camera_descriptor_adapters.cpp`, `tests/renderer/camera/test_animated_camera.cpp` delete; behaviour parity is now exercised only via `camera_v1` tests.  <!-- drift-allow: stale-ref -->
* **Content/ rotation**: `content/2d5/compositions/*` `CameraShotProfile` + `CameraRig` usages → `camera_v1::CameraDescriptor` direct; no adapter.
* **Boundary gate `[14/14]` rotation**: after items 1–5 land, gate `[14/14]` transitions ADVISORY → BLOCKING per ADR-010.
* **Boundary gate `tools/check_camera_architecture.sh` `[1/6]`+`[2/6]`**: sanctioned exemption lines drop after F2.3 closes; gates become pure "zero usages" without bridge exemption.

## Alternatives considered

* **A. Keep `camera_descriptor_adapters` as compat layer.** Rejected — bloats SDK public surface (defeats `[14/14]` intent) and provides migration smoothness without architectural benefit.
* **B. Alias `AnimatedCamera2_5D` to a thin wrapper over `camera_v1::*`.** Rejected — keeps the public symbol alive, defeating ADR-011's `[14/14]` attestation AND `[1/6]` zero-usage guarantee.
* **C. Defer deletion until TICKET-035 rich `Camera2_5D` field migration lands.** Considered and preferred in sequencing: TICKET-035 PIMPL re-light + rich field migration should complete BEFORE this ADR's deletion commit so `Camera2_5D::projection_mode` removal lands with PIMPL-stabilised `Camera2_5D`.
* **D. Keep `<camera_descriptor_adapters.hpp>` as a private inter-module convenience header (non-SDK-exported).** Rejected — adapters are unused once rig/types are deleted; no consumer benefit.
* **E. Convert `AnimatedCamera2_5D` to a thin keyframe-wrapper over `camera_v1::CameraDescriptor`'s keyframe list temporarily.** Considered for migration smoothness; rejected because it perpetuates the dual-API antipattern that this ADR retires.

## References

* AGENTS.md §"Piano operativo canonico" + §"Regole di lavoro" (single-identity + anti-duplication rules).
* [ADR-001](./ADR-001-frame-graph-compiler.md) — anchors the runtime representation that `camera_v1::CameraProgram` evaluates against.
* [ADR-008](./ADR-008-render-engine-renderer-ptr-return.md) — anchors the SDK INSTALL posture that this ADR's `[14/14]` attestation unblocks.
* [ADR-010](./ADR-010-boundary-gate-semantic-extension.md) — gates `[12/14]/[13/14]/[14/14]` consume this ADR's status as part of their BLOCKING transition path (post-TICKET-041/042/043 sync).
* `docs/camera-plan/01-CANONICAL_CAMERA_ARCHITECTURE.md` — the canonical specification for camera_v1.
* `docs/camera-plan/02-OPTICS_PROJECTION_ORIENTATION.md` — the Lens/Projection discrimination that this ADR relies on for the `projection_mode` migration.
* `docs/camera-plan/03-MOTION_TRAJECTORY_TIMELINE_DETERMINISM.md` — outlines the OrbitMotion parity path that obsoletes `CameraRig`.
* `docs/camera-plan/04-INTEGRATION_TESTS_AND_LEGACY_REMOVAL.md` — the F2.3 design that this ADR formalises.
* TICKET-029/033/034 (in `docs/FOLLOWUP_TICKETS.md`) — the camera_v1 rot history that necessitates this ADR's canonicalisation close-out.
* TICKET-035 (in `docs/FOLLOWUP_TICKETS.md`) — the rich `Camera2_5D`-+-PIMPL migration that pairs with this ADR's `projection_mode` removal.
* `tools/check_camera_architecture.sh` (gates `[1/6]`+`[2/6]`) — after F2.3.1 closes, sanctioned exemptions drop; gates become "zero usages" pure.
* `tools/check_architecture_boundaries.sh` (gates renumbered to `[12/14]/[13/14]/[14/14]` per ADR-010) — gate `[14/14]`'s BLOCKING transition is conditioned on this ADR's Phase 4.
* `include/chronon3d/scene/camera/camera_v1/camera_descriptor.hpp` — the canonical descriptor header.
* `include/chronon3d/timeline/composition.hpp` — `Composition::default_camera_descriptor(...)` API (TICKET-034).
