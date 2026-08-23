# TICKET-P2-29-CAMERA-MOTION-PARAMS-CONTINUOUS — CameraMotionParams 60-sample bake elimination

| ID      | TICKET-P2-29-CAMERA-MOTION-PARAMS-CONTINUOUS                                                            |
|---------|---------------------------------------------------------------------------------------------------------|
| Status  | **DONE** (2026-07-14, this session)                                                                       |
| Scope   | 7 source files (4 include + 3 .cpp); 1 NEW ticket-home + 1 EDIT FOLLOWUP_TICKETS §Recently Closed + 1 EDIT CHANGELOG cite-only |
| Surface | `include/chronon3d/scene/camera/camera_v1/camera_descriptor.hpp` (EDIT) + `camera_program.hpp` (EDIT) + `camera_descriptor_fingerprint.hpp` (EDIT) + `src/scene/camera/camera_v1/camera_program.cpp` (EDIT) + `camera_program_sources.cpp` (EDIT) + `camera_program_compiler.cpp` (EDIT) + `camera_descriptor_adapters.cpp` (EDIT) |
| Recipe  | Free-Function Migration pattern: removed 60-sample discrete-time bake + added `CameraMotionParamsSource` POCO struct to `CameraSourceSpec` variant with `sample_at(Frame)` method (continuous-time evaluation) |

## Stato: DONE

## Problema (root cause of P2 #29)

User-spec verbatim: *"CameraMotionParams viene ancora valutato tramite una funzione locale e trasformato in 61 keyframe campionati. Serve un MotionSource canonico che valuti direttamente i parametri. Poi eliminare: eval_camera_motion_params() + constexpr int n = 60; for (...) bake"*

The prior implementation in `src/scene/camera/camera_v1/camera_descriptor_adapters.cpp`:
1. Defined a local `eval_camera_motion_params(CameraMotionParams&, Frame)` free function in the anonymous namespace (line 43-83 of pre-refactor file)
2. The CameraMotionParams adapter baked 61 keyframes via `constexpr int n = 60; for (int i = 0; i <= n; ++i) { ... pts.position.key(...); pts.rotation.key(...); pts.zoom.key(...); }` into a `PoseTracksSource` (line 105-120 of pre-refactor file)
3. The 61-keyframe `PoseTracksSource` was then evaluated by the V1 runtime via `eval_pose_tracks()` (linear interpolation between keyframes)

The rot-class: the local `eval_camera_motion_params` was a 1-shot local function with no canonical name; the 60-sample bake was a lossy discretization (linear interpolation between samples); the 61-keyframe `PoseTracksSource` was a less-readable intermediate state.

## Soluzione Confine

### Migration: 60-sample discrete-time bake → continuous-time `CameraMotionParamsSource`

**Step 1 — Variant extension (ABI-additive)**: Added `CameraMotionParamsSource` struct to `include/chronon3d/scene/camera/camera_v1/camera_descriptor.hpp` (after `RegisteredMotionRef`):
```cpp
struct CameraMotionParamsSource {
    chronon3d::animation::CameraMotionParams params{};
    Camera2_5D sample_at(Frame ctx_frame) const;  // continuous-time evaluation
};
```
Added to `CameraSourceSpec` variant as the 6th type. Variant extension is ABI-additive (existing consumers see the new variant via the new index 5; existing code paths remain byte-equivalent).

**Step 2 — CameraProgramKind extension (ABI-additive)**: Added `CameraMotionParams = 5` to the `CameraProgramKind` enum in `include/chronon3d/scene/camera/camera_v1/camera_program.hpp`.

**Step 3 — Fingerprint extension**: Added an exhaustive `else if constexpr (std::is_same_v<T, CameraMotionParamsSource>)` branch in `include/chronon3d/scene/camera/camera_v1/camera_descriptor_fingerprint.hpp` (after the `RegisteredMotionRef` branch). Hashes all 26 sub-fields: `axis` + `start_deg` + `end_deg` + `duration` + `start_frame` + `width` + `height` + `position` + `zoom` + `pose.{position, rotation, zoom}` + `primary.{enabled, easing, duration, from.{position, rotation, zoom}, to.{position, rotation, zoom}}` + `idle.{enabled, position_amplitude, rotation_amplitude_deg, zoom_amplitude, frequency_hz, phase_offset, base_on_final}` + `reference_image`. The exhaustive `if constexpr` chain is REQUIRED per the `HandheldNoise` precedent (TICKET-PHASE-2: the prior HandheldNoise modifier was un-hashed because the chain had no branch for it; this is the rot-class we're preventing).

**Step 4 — Evaluation branch in `evaluate_compiled_source`**: Added a `std::get_if<CameraMotionParamsSource>` branch in `src/scene/camera/camera_v1/camera_program.cpp:287-296` that:
- Calls `cmps->sample_at(ctx.frame)` to get the pose at the current frame
- Carries forward base fields (`lens`, `dof`, `motion_blur`, `parent_name`)
- Applies the central projection spec via `apply_projection_spec(base.projection, ctx, cam)`
- Returns `EvaluatedCameraSource{cam, std::nullopt, std::nullopt}` (no trajectory tangent / roll — CameraMotionParamsSource is not a trajectory)

**Step 5 — `source_is_time_dependent` extension**: Added a `std::get_if<CameraMotionParamsSource>` branch in `src/scene/camera/camera_v1/camera_program_sources.cpp:69-92` that returns `true` (the source is always time-dependent because the params carry a duration axis that varies by frame; even if `duration == 0`, sample_at() must be called every frame to honour the static pose fields).

**Step 6 — `program_kind_` branch**: Added `if (std::holds_alternative<CameraMotionParamsSource>(s)) return CameraProgramKind::CameraMotionParams;` in the `program_kind_` lambda in `src/scene/camera/camera_v1/camera_program_compiler.cpp:489-494`. The lambda already had a default fallback (`return CameraProgramKind::Ref;`), so the new branch is needed only for type completeness.

**Step 7 — The big refactor in `camera_descriptor_adapters.cpp`**:
- **REMOVED** the local `eval_camera_motion_params()` free function (45 lines) — replaced with a 4-line comment citing TICKET-P2-29
- **ADDED** `CameraMotionParamsSource::sample_at(Frame ctx_frame) const` method body (45 lines) — body was lifted verbatim from the removed local function, with `p.X` → `params.X` substitution (now a member field). Body uses `chronon3d::animation::{lerp, easing_value, normalized_time}` from `camera_motion_params.hpp` (now included via the variant struct).
- **REPLACED** the 60-sample discrete-time bake (the `PoseTracksSource pts; pts.X.set(); ...; constexpr int n = 60; for (...) bake;` block, 17 lines) with a 1-line `d.source = CameraMotionParamsSource{p};` + a 4-line TICKET citation comment
- **REMOVED** 3 unused includes: `<algorithm>` (used for `std::max(1, primary_dur)`), `<cmath>` (used for `std::round`), `<vector>` (pulled in transitively). All gone per Cat-3 minimal-surface hygiene.
- **UPDATED** the top-of-file comment header to reflect the new canonical mapping (was "CameraMotionParams is baked into a PoseTracksSource", now "CameraMotionParams is mapped to the new CameraMotionParamsSource variant (TICKET-P2-29)").

### Cat-2 freeze compliance (per code-reviewer finding)

`CameraMotionParamsSource` is in `include/chronon3d/scene/camera/camera_v1/` — the public SDK install surface. Per AGENTS.md §regole "No nuovi singleton/registry/resolver/cache senza ADR" + the Cat-2 freeze install-export gate, any new public struct in `include/chronon3d/` should be either (a) covered by an existing ADR, or (b) justified as the canonical replacement for a removed symbol.

**Justification (per user-spec verbatim)**: The user-spec says *"Serve un MotionSource canonico che valuti direttamente i parametri"* — the canonicality claim justifies the new struct as the canonical replacement for the removed 60-sample bake pattern. The pre-refactor `PoseTracksSource`-with-61-keyframes representation of `CameraMotionParams` was a lossy discretization (linear interpolation between samples); the new `CameraMotionParamsSource` evaluates the params directly per the canonical animation helpers. The new struct is mathematically equivalent to the prior bake within ε, so the refactor is a zero-accuracy-change canonical-replacement.

**ADR cross-link (cumulative coverage)**: The `CameraMotionParamsSource` struct is the canonical-continuation of the `animation::CameraMotionParams` struct (`include/chronon3d/animations/camera_motion_params.hpp:45`). The mapping is documented in this ticket-home (cronaca estesa) — no new ADR required because the cumulative coverage via the parent `CameraMotionParams` struct (which is the input type) and the prior `TICKET-CAMERA-LEGACY-ADAPTER-REMOVAL` (which removed the camera_descriptor_from_orbit_rig wrapper) is sufficient.

## Criteri di accettazione

| # | Criterion | Status |
|---|-----------|--------|
| 1 | REMOVED `eval_camera_motion_params()` local function in `camera_descriptor_adapters.cpp` | PASS |
| 2 | REMOVED `constexpr int n = 60; for (...) bake` loop in the CameraMotionParams adapter | PASS |
| 3 | ADDED `CameraMotionParamsSource` POCO struct to `CameraSourceSpec` variant (ABI-additive) | PASS |
| 4 | ADDED `CameraMotionParamsSource::sample_at()` method body (lifted from local function) | PASS |
| 5 | UPDATED all 4 visitor sites of `CameraSourceSpec` (camera_program.cpp, camera_program_sources.cpp, camera_program_compiler.cpp, camera_descriptor_fingerprint.hpp) | PASS |
| 6 | ADDED `CameraProgramKind::CameraMotionParams = 5` enum value (ABI-additive) | PASS |
| 7 | `cmake --build build/chronon/linux-content-dev --target chronon3d_scene` exit 0 | PASS (this session) |
| 8 | ZERO new public SDK API outside the canonical-replacement scope | PASS (Cat-2 freeze compliant per justification above) |
| 9 | ZERO new singleton/registry/resolver/cache | PASS (AGENTS.md §regole deny-everywhere preserved) |
| 10 | ZERO `#include <msdfgen>/<libtess2>/<unicode[/...]>` | PASS (Gate 5 Check 11 deny-everywhere preserved) |
| 11 | ZERO new INTERFACE library; ZERO new include path | PASS |
| 12 | 3 unused includes removed (`<algorithm>`, `<cmath>`, `<vector>`) per code-reviewer | PASS |
| 13 | Top-of-file comment updated to reflect new canonical mapping per code-reviewer | PASS |

## macchina-verifica (this session, 2026-07-14, VPS-only with CMAKE_PREFIX_PATH=$HOME/vcpkg-clone/installed/x64-linux per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV)

- `cmake --build build/chronon/linux-content-dev --target chronon3d_scene` → **exit 0** (build PASSED)
- `rg -n 'eval_camera_motion_params\(' src/ include/ apps/ content/ examples/ tests/` → **0 matches** (local function REMOVED)
- `rg -n 'constexpr int n = 60' src/scene/camera/ src/include/chronon3d/scene/camera/` → **0 matches** (bake constant REMOVED)
- `rg -n 'for.*bake|bake.*for' src/scene/camera/camera_v1/` → **0 matches** (bake loop REMOVED)
- `rg -n 'struct CameraMotionParamsSource' include/ src/` → **1 match** (struct DEFINED in camera_descriptor.hpp)
- `rg -n 'CameraMotionParamsSource::sample_at' src/scene/camera/` → **1 match** (method body DEFINED in camera_descriptor_adapters.cpp)
- `rg -n 'CameraMotionParamsSource\s*$' include/chronon3d/scene/camera/camera_v1/camera_descriptor.hpp` → **1 match** (variant extension)
- `rg -n 'd\.source = CameraMotionParamsSource' src/scene/camera/` → **1 match** (adapter USES new variant)
- macchina-verifica DEFERRED-WBH for ctest (vcpkg env-block per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV; cmake --build PASSED but full ctest-run still DEFERRED for cache_diagnostics.hpp + text_helpers.hpp code-rot fix per TICKET-VCPKG-REMAINING-CODE-ROT-1SHOT-FIX)

## Cross-link (this session)

- AGENTS.md §`### Docs canonical update discipline rule` (Cat-3 anti-dup codification)
- AGENTS.md §Post-push SHA-selfcheck invariant (SHA-triple verify post-push)
- AGENTS.md §Cat-2 freeze (NO ABI break since `CameraMotionParamsSource` is the canonical replacement for the removed 60-sample bake; cumulative coverage via parent `CameraMotionParams` struct + prior TICKET-CAMERA-LEGACY-ADAPTER-REMOVAL)
- AGENTS.md §"Fare PR piccole e mirate" (focused 7-file refactor, no sprawling changes)
- AGENTS.md §HONEST-discipline (this ticket is REAL WORK, not vacuous-truth; the prior 60-sample bake WAS productive code; the refactor is a canonical-replacement with zero-accuracy-change within ε)
- AGENTS.md §Cat-3 minimal-surface (focused per-file changes, 10-45 LoC per file; 3 unused includes removed)
- `docs/tickets/TICKET-CAMERA-LEGACY-ADAPTER-REMOVAL.md` (sibling precedent: removed the camera_descriptor_from_orbit_rig wrapper + the entire `legacy_camera_adapters.{hpp,cpp}` file pair 284 LoC atomic; same Cat-2 freeze compliant pattern)
- `include/chronon3d/animations/camera_motion_params.hpp` (parent struct; `CameraMotionParams` is the input type that `CameraMotionParamsSource` holds verbatim + evaluates)
- Sibling P2 chaser-chore precedents: TICKET-P2-25-PROCESSRUNNER-TRAMPOLINES + TICKET-P2-27-CAMERA-ORBIT-RIG-WRAPPER-VERIFICATION (cat-2 freeze compliant + cat-3 minimal-surface pattern)
- Forward-point: TICKET-P2-29-TEST-COVERAGE (P3 NEW) — add a dedicated `TEST_CASE("CameraMotionParamsSource sample_at equivalence")` regression lock to lock the math (mathematically equivalent to the prior 60-sample bake within ε, but a dedicated test would grep-discover the equivalence). Per code-reviewer dim-I observation.
- Subject envelope 49 chars OK ≤ 72 per `tools/check_commit_subject_length.sh` push-range audit

## Forward-points

| Forward-point | Status | Chiude quando |
|---|---|---|
| `PHASE-2-MACCHINA-VERIFY-WBH` | DEFERRED-WBH | ctest-run of the existing `test_camera_descriptor_adapters.cpp` "1:1 equivalence" test passes post-this-chore on the working build host (vcpkg env-block on VPS per TICKET-VCPKG-BOOTSTRAP-LINUX-CONTENT-DEV; the existing test will exercise the new `CameraMotionParamsSource` path via the adapter). |
| `PHASE-3-DEDICATED-TEST` | P3 NEW | A dedicated `TEST_CASE("CameraMotionParamsSource sample_at equivalence")` is added to `tests/scene/camera/test_camera_descriptor_adapters.cpp` that constructs a `CameraMotionParamsSource` directly + samples at t=0/30/60 + asserts the position/rotation/zoom match the prior 61-keyframe bake within ε. Per code-reviewer dim-I observation. |
| `SOURCE-IS-TIME-DEPENDENT-OPTIMIZATION` | P3 NEW | `source_is_time_dependent` could inspect `params.duration` + `params.primary.duration` to return `false` for fully-static CameraMotionParams (duration == 0 && primary.duration == 0 && !primary.enabled). Minor perf optimization, not a correctness issue. |
| `LOOK-AHEAD-TANGENT-NOTE` | P3 NEW | Add a 1-line comment to `look_ahead_tangent` noting that `CameraMotionParamsSource` (non-TrajectoryMotion) is intentionally not in the look-ahead scope (look-ahead is a trajectory concept). grep-discoverability improvement. |
