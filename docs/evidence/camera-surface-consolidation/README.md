# Camera surface consolidation — consumer census (2026-10-31)

Evidence for `TICKET-CAMERA-SURFACE-CONSOLIDATION` steps **S0**, **S1**, **S4**.
The ticket remains PARTIAL: **S2** is blocked by the open F3 Z-convention fix and
**S3** by the legacy `dof_aperture` migration.

## Method

Direct-include census over the working trees (`26f61715a` plus this pass):

```shell
# C3D
grep -rl --include='*.hpp' --include='*.cpp' --include='*.cc' \
  --include='*.c' --include='*.h' '<header path>' include src tests apps
# CM3D
grep -rl --include='*.hpp' --include='*.cpp' '<header path>' \
  include src tests tools
```

Counts are **include sites**, not transitive consumers; umbrella includes are not
expanded. A zero count would make a file a removal candidate; every file below
has at least one real consumer, so no camera/DoF file was deleted.

## 1. C3D — classification and consumers

| File | Bucket | Includes | Representative consumers |
|---|---|---:|---|
| `math/camera_projection_contract.hpp` | Admission contract | 11 | `detail/camera_projection_{frustum,matrix}`, resolver, `camera_2_5d_projection`, `evaluated_projection` |
| `math/camera_projection_resolver.hpp` | Projection authority | 6 | `camera_2_5d_projection`, four math tests |
| `math/detail/camera_projection_matrix.hpp` | Internal detail | 1 | resolver |
| `math/detail/camera_projection_clip.hpp` | Internal detail | 2 | resolver, `test_clip_with_uv` |
| `math/detail/camera_projection_frustum.hpp` | Internal detail | 2 | resolver, `test_frustum_culling` |
| `math/camera_2_5d_projection.hpp` | 2.5D projection | 24 | `projection_helpers`, `projector_2_5d`, `projected_card_rasterizer`, `nulls_overlay`, `preflight`, `source_node_native_image` |
| `math/camera_pose.hpp` | Pose type | 5 | camera model and projection headers |
| `math/projection_context.hpp` | Projection context | 4 | `render_graph_context`, `render_graph_node`, `projector_2_5d`, `transform` |
| `scene/model/camera/camera_2_5d.hpp` | Camera model | 49 | `catmull_rom_path`, `render_settings`, `post_processing_system`, `render_graph_context`, `per_pixel_dof_node` |
| `scene/model/camera/dof.hpp` | DoF model | 17 | `per_pixel_dof`, `post_processing_system`, `render_backend_frame_api`, `per_pixel_dof_node`, `render_plan_camera_adapter` |
| `scene/model/camera/camera.hpp` | Scene camera | 13 | scene builder and adapters |
| `scene/model/camera/lens_model.hpp` | Optics model | 3 | camera model and projection source |
| `scene/camera/camera_projection.hpp` | Diagnostic facade | 7 | `camera_shot_report`, `camera_debug_overlay`, `overlay_diagnostic_panels`, projection test |
| `scene/camera/evaluated_projection.hpp` | Projection evaluation | 2 | `golden_projection_test` |
| `scene/camera/camera_debug_overlay.hpp` | Diagnostic | 2 | overlay implementation and panels |
| `scene/camera/camera_shot_report.hpp` | Diagnostic | 1 | debug overlay |
| `scene/camera/camera_state_builder.hpp` | Diagnostic/builder | 1 | scene builder |
| `render_plan/camera_contract.hpp` | Admission contract | 4 | decoder, compiler validation, compiler, camera tests |
| `render_plan/camera_sample_provider.hpp` | Provider contract | 2 | render-plan compiler, compiler |
| `render_graph/nodes/per_pixel_dof_node.hpp` | DoF graph node | 3 | node implementation, output pass, unit test |

**S4 disposition.** `camera_projection_{matrix,clip,frustum}` are now
`math/detail/` and remain package-closure entries, not SDK API. The resolver and
`camera_contract.hpp` / `camera_projection_contract.hpp` stay the pinned
admission points. The diagnostic files all have consumers, so the
zero-consumer deletion rule does not apply to any of them.

## 2. CM3D — camera headers

| Header | Bucket | Includes | Representative consumers |
|---|---|---:|---|
| `CameraRig.hpp` | Authority | 22 | `DocumentaryCameraPath`, `CameraSequence`, `CameraPanelChoreography`, `AspectReframe`, `TitleCameraRecipe` |
| `CameraMotion.hpp` | Applier | 12 | `AspectReframe`, `Motion.hpp`, `EyeTrace`, `Presets`, `GeoCameraRig` |
| `CameraFocusSequence.hpp` | Applier | 3 | `DocumentaryCameraPath.cpp`, implementation, focus-sequence test |
| `CameraRecipe.hpp` | Applier | 4 | `DocumentaryCameraPath.cpp`, recipe registry, implementation, camera test |
| `CameraSequence.hpp` | Applier | 2 | implementation, sequence test |
| `CameraTrajectory.hpp` | Applier | 3 | `Motion.hpp`, implementation, trajectory test |
| `CameraPanelChoreography.hpp` | Applier | 3 | `Motion.hpp`, implementation, framing test |
| `DocumentaryCameraPath.hpp` | Applier | 2 | implementation, documentary test |
| `TitleCameraRecipe.hpp` | Applier | 2 | implementation, framing test |
| `CameraScreenSpace.hpp` | Geometric service | 11 | `AspectReframe`, `Interaction`, `Motion.hpp`, `EyeTrace`, `CameraValidation` |
| `CameraFraming.hpp` | Geometric service | 5 | framing, documentary, sequence, recipe, framing test |
| `CameraValidation.hpp` | Validation service | 3 | `Motion.hpp`, implementation, framing test |
| `CameraShake.hpp` | Procedural service | 2 | implementation, shake test |

**S2 status — blocked.** Nine of thirteen headers are appliers with the same
`apply*(CameraRig&, ...)` shape. The ticket target (13 → ≤6) is unchanged, but
the pack migrations must not start before F3
(`planPosition` vs `planAnimationPosition`), still **OPEN** in
`docs/FOLLOWUP_TICKETS.md`. No header was removed in this pass.

## 3. S3 — legacy `dof_aperture` is still a wire fact

The field has 16 production touchpoints: 10 in CM3D (lowering, emission,
validation, samples, tests) and 6 in C3D (plan type, contract, adapter,
decoder, fingerprint, DoF tests). It is **not** removable by deletion:

- `render/spike/assets/t19b_apollo/T19B_Apollo_SceneSource.v3.json` contains
  **301** `aperture` values and **0** `f_stop` values.
- C3D decoder fixtures (`tests/c_abi/test_render_plan_decoder*.cpp`) decode
  `aperture` and assert the value survives into `CameraPlan`.
- `fingerprint_values.cpp` hashes it; `render_plan_camera_adapter.hpp` maps it to
  `Camera2_5D::dof.aperture`; `dof.hpp` uses it as the non-physical blur gain.

Removal therefore requires a coordinated schema/producer/consumer migration and
a frame-equivalence gate. It is recorded as blocked, not half-removed.

## 4. Acceptance commands (run on the build host)

This pass verified the path moves with `git diff --check` and the include census;
the full C3D build and Gate C run were deferred by operator request.

```shell
# C3D — public surface and camera/math suites
python3 tools/check_public_headers.py \
  --compile-commands build/chronon/linux-video-release/compile_commands.json \
  --manifest cmake/Chronon3DPublicHeaders.cmake --out out
cmake --build build/chronon/linux-video-test --target chronon3d_camera_tests chronon3d_core_tests -j12
ctest --test-dir build/chronon/linux-video-test -R 'camera|clip|frustum' --output-on-failure

# CM3D — boundary guardrail
ctest --test-dir build/release-fast -R chrononmotion_boundary --output-on-failure
```
