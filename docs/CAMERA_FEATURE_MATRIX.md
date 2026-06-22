# Camera Feature Matrix — Chronon3d

> **Status legend**
> - ✅ **Verified**     — implemented, tested, used in production-grade compositions.
> - ⚠️ **Partial**      — implemented but missing tests / edge cases / configuration knobs.
> - 📋 **Planned**      — listed in the V1 roadmap (P0–P14), not implemented yet.
> - ❌ **Deprecated**   — kept only as a thin compat wrapper; new code should target its replacement.

This matrix is the canonical inventory for the Camera System V1 effort. It is consulted every time a new camera feature is added: the entry must be updated in the same PR that introduces the feature, or the feature is considered un-owned.

## 1. Core model

| Feature | Status | Notes |
|---|---|---|
| `Camera2_5D` core model | ✅ | Single Vec3+rotation+dof struct used everywhere. |
| `CameraRig` 1-node / 2-node targeting | ✅ | `target`, `poi_node_target`, parenting tests in `tests/stabilization/test_stabilization.cpp`. |
| Focal length / sensor size / f-stop | ✅ | Animated, FOV derived. |
| Focus distance | ✅ | Animatable per key. |
| Motion blur (temporal) | ✅ | Velocity buffer + multi-sample. |
| DOF physics | ✅ | bokeh + CoC controllable. |
| FOV / zoom projection | ✅ | `Camera2_5D` projection unit tested. |

## 2. Motion presets

| Preset | Old API | Status | New API target |
|---|---|---|---|
| `hero_push_in` | `camera_rig::hero_push_in()` | ✅ Verified | `CameraMotionRegistry::build("camera.hero_push")` |
| `orbit`        | `camera_rig::orbit_yaw` keys | ✅ Verified | `CameraMotionRegistry::build("camera.orbit_small")` |
| `focus_pull`   | `camera_rig::focus_pull()` | ✅ Verified | `CameraMotionRegistry::build("camera.focus_pull")` |
| `parallax_pan` | `camera_rig::parallax_pan()` | ✅ Verified | `CameraMotionRegistry::build("camera.parallax_sweep")` |
| `dolly_zoom`  | `camera_rig::dolly_zoom()` | ⚠️ Partial | `CameraMotionRegistry::build("camera.dolly_zoom")` — regression test not yet implemented (CAM-04). |
| `low_angle_reveal` | `camera_rig::low_angle_reveal()` | ⚠️ Partial | Same. |
| `subtle_float` | `camera_rig::subtle_float()` | ⚠️ Partial | Same. |
| `product_orbit` | — | 📋 Planned | `CameraMotionRegistry::build("camera.product_orbit")` (P11). |
| `card_reveal`, `whip_pan`, `fly_through`, `dashboard_tour` | — | 📋 Planned | P11. |

> **P2 deliverable**: every Verified/Partial preset above has a thin wrapper in `camera_motion_registry.cpp` that delegates to the existing `camera_rig::` implementation, so legacy compositions continue to compile unchanged while new compositions use the registry directly.

## 3. Sampling / validation / framing

| Feature | Status | Notes |
|---|---|---|
| `sample_camera_path` (diagnostic) | ✅ | Renamed conceptually to **“diagnostic sampler”** — see P3 — to free the term `sampler` for `CameraTrajectorySampler`. |
| `CameraShotValidator` (centering / visibility / safe area / depth / black-frame) | ✅ | Already handles the 4 critical cases. |
| Hard-coded validation thresholds in `CameraPathSamplerReport` | ⚠️ Partial | Will be replaced by `CameraPathValidationOptions{ max_target_error_px, max_velocity_jump, max_acceleration_jump, max_jerk }` (P1). |
| Sub-frame / `SampleTime` evaluation | ⚠️ Partial | Many paths round to integer frames; `SampleTime` plumbing present in `core/types/sample_time.hpp` but not enforced everywhere. |
| `fit_camera_to_layers` (framing) | ⚠️ Partial | Iterative dolly steps; uses external `layer_sizes`. Will be replaced by `CameraFramingSolverV2` (P6). |
| Multi-target framing | 📋 Planned | P6. |
| Rule-of-thirds placement | 📋 Planned | P6. |
| Dead-zone / hysteresis / look-ahead | 📋 Planned | P6. |
| Bounds-aware framing (real bbox, no `layer_sizes` table) | 📋 Planned | P6. |

## 4. Diagnostics

| Feature | Status | Notes |
|---|---|---|
| `CameraDebugOverlay` (safe area, target, projected bounds, path, top-down, depth side view) | ✅ | Rich. Used in `content/2d5/compositions/camera_*_test.cpp`. |
| `chronon3d_cli camera path-report` | ⚠️ Partial | Not exposed yet (see P10). |
| `chronon3d_cli camera validate` | 📋 Planned | P10. |
| `chronon3d_cli camera debug-video` | 📋 Planned | P10. |
| Handle / tangent / banking overlay | 📋 Planned | P3, requires trajectories + orientation. |
| JSON-stable report format | 📋 Planned | P10. |

## 5. Trajectory / constraint / shot / lens (V1 NEW)

| Subsystem | Status | Notes |
|---|---|---|
| `CameraTrajectory` (Linear / Cubic-Bézier / Catmull-Rom / Hold) | 📋 Planned | P3. |
| `ArcLengthTable` LUT | 📋 Planned | P3 — O(1) sample with arc-length param. |
| `OrientationPolicy` (FixedRotation / LookAtPoint / LookAtLayer / OrientAlongPath / OrientAlongPathKeepHorizon / Custom) | 📋 Planned | P3. |
| `BankingSettings` | 📋 Planned | P3. |
| `CameraProgram` (single evaluator: source → trajectory → constraints → framing → lens → validation) | 📋 Planned | P4. |
| Constraint registry + resolver (`LookAt`, `Follow`, `KeepHorizon`, `DampedTarget`, `Rail`, `RotationLimit`, `Distance`) | 📋 Planned | P5. |
| `ShotTimeline` + `CameraTransitionRegistry` | 📋 Planned | P7. |
| Lens effects V1 (breathing, autofocus, rack focus, vignette, barrel/pincushion, CA, bokeh, bloom by CoC) | 📋 Planned | P8. |

## 6. Integration points

| Feature | Status | Notes |
|---|---|---|
| `ExtensionRegistry` module pattern for camera presets | ✅ | Same pattern as `MotionPresetRegistry` / `LayerCommandRegistry`. |
| `ExtensionModule` per-domain registration | ✅ | Demo composition registers via `ExtensionModule::register_composition`. |
| `SceneBuilder` is **unaware** of CameraProgram internals | ✅ | Camera is set via `scene.camera().set(cam)` after `program.evaluate(ctx)`. |
| CameraProgram render-graph pass | 📋 Planned | P4 — added only after the model is stable. |

## Definition-of-Done guardrails

- No entry in this matrix can move back from ✅ to ⚠️ without an explicit regression PR.
- No 📋 entry can be implemented in a PR that also touches an existing ✅ entry unless there is a justified cross-cutting refactor.
- Every new ✅ entry must come with a regression test in `tests/scene/camera/` and a debug-golden in `tests/golden/camera/`.
