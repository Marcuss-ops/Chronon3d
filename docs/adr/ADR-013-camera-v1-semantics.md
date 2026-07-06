# ADR-013 — camera_v1 semantic contracts: compiler late-rebuild, KeepLastValidCamera policy, cache-lease commit-on-success, explicit `FrameRate` propagation

| Field | Value |
|---|---|
| **Status** | ✅ Documented + accepted (post-F2.3 call-site migration; locks the 6 TICKET-A3-* contracts on `main`) |
| **Date** | 2026-07-04 |
| **Deciders** | Camera_v1 architecture owner (post-F2.3 ADR-011 close-out) |
| **Tags** | `camera_v1`, `compiler`, `session`, `cache`, `lease`, `frame-rate`, `keep-last-valid-camera`, `F2.3.1`, `A3-cluster`, `telemetry-contracts` |
| **Related** | [ADR-011 — camera-legacy-deletion](./ADR-011-camera-legacy-deletion.md) (especially **Decision 5** + the **pre-existing rot** at `src/render_graph/pipeline/camera_change_policy.cpp:24` fixata in commit `ac514fea` projection_mode→optics_mode, which is the documentation precedent the 4 contracts below exist to *prevent recurrence of*); `docs/camera-plan/01-CANONICAL_CAMERA_ARCHITECTURE.md`; `docs/camera-plan/02-OPTICS_PROJECTION_ORIENTATION.md`; `docs/camera-plan/03-MOTION_TRAJECTORY_TIMELINE_DETERMINISM.md`; `tools/check_camera_architecture.sh` gates `[1/6]`–`[6/6]`; `tools/check_doc_sync.sh`; TICKET-A3-METADATA / TICKET-A3-SESSION-POLICY / TICKET-A3-CACHE-LEASE / TICKET-A3-CTX-FRAMERATE / TICKET-A3-DAMPED-HISTORY / TICKET-A3-LOOKAT-DIAGNOSTIC (the 6-ticket A3 cluster on `main` that this ADR locks). |

## Context and scope

Chronon3d's `camera_v1::*` compiled-evaluation path (`compile_camera()` → `CameraProgram::evaluate(ctx, session)` with optional `CameraSessionCache` for per-worker pre-roll) is the canonical authoring+runtime surface post-F2.3 (per [ADR-011](./ADR-011-camera-legacy-deletion.md)).  The 6-ticket A3 cluster landed 7 individual fixes that each prevent a *class* of silent behavioural drift — but the fixes themselves are scattered across `src/scene/camera/camera_v1/*.cpp` and only the regression-locks in `tests/` make them auditable.

This ADR consolidates the 6 fixes into a single document so that any future maintainer reading the `camera_v1` source tree has ONE place to look for the contracts.  Each contract has:

1. **Spec** — the behavioural rule the compiled path commits to.
2. **Source anchor** — the file + approximate line where the contract is enforced (so a maintainer can read the code AND the contract side-by-side).
3. **Test lock** — the regression test that fails if a future refactor drifts away from the spec (so the contract is mechanistically enforced during CI).
4. **Failure mode** — what *non-fatal* drift would look like if the contract were silently broken (the reason this ADR exists at all).

The contracts are ordered by data-flow: (1) compile-time classification (Decision 1), (2) evaluate-time failure-policy semantics (Decision 2), (3) per-worker session cache + RAII lease + commit-on-success (Decision 3), (4) cross-cutting `FrameRate` propagation through the public API surface (Decision 4).  Decisions 5 + 6 (DampedFollow-forces-RequiresHistory + LookAtLayer-missing-transforms canonical diagnostic channel) are added as **Decision 5 / Decision 6** because they came up during A3 cluster delivery and are co-located topologically.  **ADR-013-EXT** appends Decisions 7 + 8 (C5 Doc-only carry per `tools/check_doc_sync.sh`): (7) **DiagnosticChannel canonical emit** — every `camera_v1` evaluate-stage diagnostic flows through `result.diagnostics` (`Severity::{Warning,Info,Severe}`), NEVER stdout/stderr/spdlog::warn; generalises TICKET-A3-LOOKAT-DIAGNOSTIC's precedent.  (8) **Compiler determinism + CameraProgram post-compile immutability** — identical descriptor contract ⇒ byte-identical `CameraProgram`; `CameraProgram` is read-only after `compiled_ = true`.  Both omissions were surfaced during A3 cluster delivery as cross-cutting contracts that protect the 6 original decisions from drift on the `result.diagnostics` channel and on the compiled-program byte-identity used by `CameraSessionCache` (Decision 3).

> Why a *single* ADR and not 6 separate tickets?
> Because the A3 cluster is a coherence contract, not 6 independent fixes.  A maintainer who breaks ONE of the contracts breaks the determinism story that the OTHER 5 contracts support.  This ADR formalises that coupling.

---

## Decision 1 — Compiler late-rebuild of `failure_policy_` / `time_dependent_` / `evaluation_dependency_` after a `RegisteredMotionRef` graft (TICKET-A3-METADATA, DoD gate (a))

### Spec

When `compile_camera()` resolves a `RegisteredMotionRef` (catalog back-pointer to a preset) through `CameraCatalog::find_descriptor(...)`, the **graft** of step 1 — `program.descriptor_.source = recursive.descriptor_.source` — MUST be followed by the **rebuild** of steps 3–5 against the **OUTER** descriptor, with the **referenced preset's metadata** explicitly DISREGARDED.

The four metadata fields that MUST be rebuilt from the outer descriptor (even if the preset has a different value):

| Field                          | Rebuild rule on outer descriptor                                                    |
|--------------------------------|------------------------------------------------------------------------------------|
| `program.failure_policy_`      | `descriptor.failure_policy` (outer)                                                 |
| `program.time_dependent_`      | `!source_is_static || !descriptor.modifiers.empty()` (outer)                        |
| `program.evaluation_dependency_` | `RequiresHistory iff any(outer constraint is DampedFollowConstraint)` (see Decision 5) |
| `program.descriptor_` itself   | `descriptor` (outer); the graft only overwrites `.source`                           |

### Source anchor

`src/scene/camera/camera_v1/camera_program_compiler.cpp` lines ~78–95 (step 1: graft + CAM-02 cycle-detection exit-point), then ~287–307 (step 3: `failure_policy_`; step 3-N: `time_dependent_`; step 4: `evaluation_dependency_`; step 5: `compiled_ = true` + return).

The CAM-03 sentinel block-comment on the post-graft fall-through states the contract verbatim:

> CAM-03 fix: extract ONLY the resolved concrete source variant. The outer descriptor's `failure_policy`, `time_dependent flag`, and `evaluation_dependency` MUST be recomputed from the outer descriptor in steps 2-5 below — we must NOT inherit them from the referenced preset. Fall through to steps 2-5.

### Test lock

- `tests/scene/camera/test_camera_program_compiled.cpp::compiled_registered_motion_ref_does_not_inherit_outer_metadata` — DoD gate (a) primary lock.  Asserts that an OUTER descriptor with empty constraints (forces `Stateless`) referencing a preset with `DampedFollowConstraint` (forces `RequiresHistory`) yields a program whose `evaluation_dependency() == Stateless` (outer wins).
- Companion: `compiled_registered_motion_ref_missing` and `compiled_cycle_detection_*` (cycle-detection paths must NOT short-circuit step 3–5 either).

### Failure mode (if silently broken)

`camera_v1` program classified as `RequiresHistory` even though the outer descriptor is `Stateless`.  `CameraSessionCache::acquire()` would then pay the pre-roll walk cost unnecessarily; or, more dangerously, classified as `Stateless` even though the outer has `DampedFollow` — the EMA accumulator never gets seeded with previous_camera, and the post-modifier rotation emits frame-1 jitter forever.  Both are silent; the test lock is the only protection.

---

## Decision 2 — `KeepLastValidCamera` failure policy wires through `CameraSession::last_valid_camera` (TICKET-A3-SESSION-POLICY, DoD gate (c))

### Spec

`CameraFailurePolicy::KeepLastValidCamera` is the **only** failure-mode in `program.failure_policy_`'s `switch` that READS `session.last_valid_camera`.  The wire is gated on **evaluate-level success**, NOT on lookup-level success: a constraint-induced failure during apply still completes `evaluate()` (and emits the recovery diagnostic) but **skips the write** — so the read-path correctly sees the last_valid_camera from the *prior* successful evaluate(), regardless of whether the *current* frame's lookup hit.  The wire is:

* **Write path** (end of `evaluate()` in `camera_program.cpp`, **only when `evaluate()` returns successfully** — post-constraint-loop, post-diagnostic-emit, immediately before the function returns the result): `session.last_valid_camera = result.camera;` — the post-constraint camera is persisted as the cache for the *next* failure.  If `evaluate()` does NOT return successfully (constraint apply throws, internal invariant breaks, descriptor unmapped at runtime), the writepath is skipped and `session.last_valid_camera` is left at its prior value.
* **Read path** (the `case CameraFailurePolicy::KeepLastValidCamera:` arm, invoked when an apply-stage constraint fails): if `session.last_valid_camera.has_value()` → emit one `Warning` diagnostic naming the failed constraint index AND the reason; return `EvaluatedCamera { result.camera = *session.last_valid_camera, diagnostics[…] = {Warning, "Recovered: …"} }`.  The contract is **"never blank"** — the camera MUST NOT reset to identity / motionless when a prior valid commit exists.  If `session.last_valid_camera` is `nullopt` (first-encounter, or after `session.reset()`) → fall through to the true `CameraEvaluationError::ConstraintFailure` (same kind as `Stop` policy).

The two arms — `Stop` and `SkipFailedConstraint` — MUST NOT touch `session.last_valid_camera` (Stop returns without writing; SkipFailedConstraint continues without writing).

### Source anchor

`src/scene/camera/camera_v1/camera_program.cpp` lines ~478–522 — the `switch (failure_policy_)` switch inside the constraint loop, dominated by the TICKET-A3-SESSION-POLICY sentinel block-comment (called out as the SOLE wire of `session.last_valid_camera`).  The end-of-evaluate writepoint is at line ~590 (`session.last_valid_camera = result.camera;`).

The struct field is declared in `include/chronon3d/scene/camera/camera_v1/camera_session.hpp` as `std::optional<Camera2_5D> last_valid_camera{}` inside `CameraSession`.  `CameraSession::reset()` (called by `CameraSessionCache`'s must-reprime path) clears the optional.  `CameraSessionCache::find()` and `cache.acquire()` do NOT directly touch it — only `evaluate()` does.

### Test lock

- `tests/runtime/test_camera_session_keep_last_valid.cpp` — 2-frame regression lock.  SUBCASE A is the PRIMARY gate-(c) lock: evaluate Frame 0 (passes → caches `last_valid_camera`); modify descriptor / evaluate Frame 1 with `distance-zero` failure under `KeepLastValidCamera` policy → MUST receive `EvaluatedCamera { .camera = frame-0-camera, .diagnostics has "Recovered:" warning with constraint index + reason }` rather than a `CameraEvaluationError::ConstraintFailure`.
- SUBCASE B pairs the inverse contract: after `session.reset()` the cache is empty, so a single-frame `KeepLastValidCamera` failure under `Stop`-equivalent conditions MUST fall through to `CameraEvaluationError::ConstraintFailure`.

### Failure mode (if silently broken)

`KeepLastValidCamera` collapses onto the `Stop` code path — both arms return a hard error, and the cached camera is never recovered.  Composers writing resilience-sensitive compositions (e.g. dolly + recompose on failure) would experience silent jitter-on-fail instead of the documented at-last-good camera; or, more dangerously, a regression could write the cached camera to the result while still emitting a hard `ConstraintFailure` diagnostic, which would diverge from the documented contract.

---

## Decision 3 — `CameraSessionCache::acquire()` + `CameraSessionLease` + `commit()` = the SOLE writepoint for `last_evaluated_frame` (TICKET-A3-CACHE-LEASE, DoD gate (d))

### Spec

`CameraSessionCache` is the **per-worker** canonical store of primed `CameraSession` instances.  The `last_evaluated_frame` integer — the only state besides `.session.*` that `commit_lease()` writes — is updated ONLY when ALL three conditions hold atomically inside one function:

1. The caller has successfully acquired a valid `CameraSessionLease` from `cache.acquire(program, shot_idx, …)`.
2. `program.evaluate(ctx, lease.session())` returned a successful result.
3. The caller explicitly invoked `lease.commit()`.

If any of these conditions fail (failed eval, exception, forgotten release, lease destructor running with `committed_ == false`), the cache entry is **NOT mutated** — the `last_evaluated_frame` sentinel stays unchanged.

The contract is layered in three places:

* `cache.acquire()` (in `camera_session_cache.cpp`) NEVER writes `last_evaluated_frame`.  The `must_reprime` branch writes `valid`, `descriptor_fingerprint`, `shot_start_frame`, `cut_seen` + calls `preroll_session_for_frame(...)` with the **caller-supplied** `frame_rate` (see Decision 4).  `last_evaluated_frame` is **not touched** in the `must_reprime` branch.
* `CameraSessionLease::commit()` is `if (!committed_) { committed_ = true; cache_->commit_lease(shot_idx_, session_, target_frame_); }` — the SOLE writepoint for `last_evaluated_frame`.
* `CameraSessionLease::~CameraSessionLease()` runs unconditionally but does NOT write — if `committed_ == false`, the in-flight session mutations are silently discarded.  No mutex; the RAII shape is the protection.

### Source anchor

`include/chronon3d/scene/camera/camera_v1/camera_session_cache.hpp` lines ~52–110 (`kCanonicalPrerollMaxFrames = 30`, `CameraSessionLease`, `CameraSessionCache::commit_lease(...)`).

`src/scene/camera/camera_v1/camera_session_cache.cpp`:
- `acquire()`: lines ~107–144 — the `if (must_reprime)` arm writes fingerprint/cut_seen/shot_start_frame and calls `preroll_session_for_frame(...)`.  The CAM-05 sentinel *explicitly* documents that `last_evaluated_frame` is now written by `commit()`.
- `commit_lease()`: lines ~167–173 — `it->second.checkpoint.session = session; it->second.checkpoint.last_evaluated_frame = target_frame;`.  This is the SOLE writepoint.
- `~CameraSessionLease()`: lines ~157–162 — empty body; the comment explicitly states "If not committed, the session state is discarded."

### Test lock

- `tests/runtime/test_camera_session_cache_failed_no_commit.cpp` — DoD gate-(d) PRIMARY LOCK: 2 SUBCASEs (A primary: failed eval + no commit() → `cp_post->last_evaluated_frame` stays at the default sentinel `-1` when the lease destructor runs; B positive control: successful eval + `lease.commit()` → `last_evaluated_frame` advances to `kTargetFrame`).

### Failure mode (if silently broken)

A regression that moves `last_evaluated_frame = target_frame` into `acquire()` would mean every call to `acquire()` writes the frame — including failed-evals (which the cache doesn't see but a future worker isolation check would surface).  Worse: a regression that removes the `commit()` call from the success path would leave checkpoints stale at `-1` forever, breaking forward-step reuse optimisation and forcing every frame through the pre-roll walk (catastrophic performance regression).

---

## Decision 4 — Explicit `FrameRate` propagation; `camera_v1::*Context::at()` factories require `FrameRate` as 2nd positional (no default, no fallback) (TICKET-A3-CTX-FRAMERATE, DoD gate (e))

### Spec

Every `camera_v1::*` evaluation-context factory function takes `FrameRate` as a **required positional argument** with **no default**.  Specifically:

* `CameraMotionContext::at(Frame f, FrameRate frame_rate)` — `static` member, `camera_motion_context.hpp` line 42.
* `CameraEvalContext::at(Frame f, FrameRate frame_rate, int32_t vp_w = 1920, int32_t vp_h = 1080)` — viewport at position 3 is the only default allowed, and it is `vp_w`/`vp_h`.  **The first non-Frame positional defaults are the viewport**; the `frame_rate` parameter is NEVER defaulted.

Callers MUST propagate the **project's** `FrameRate` (`chronon3d::FrameRate`) into these factories; no fixture constexpr of `{30, 1}` is permitted anywhere along the call chain that crosses a `Context::at()` boundary.  Specifically disallowed patterns:

* Discrete literals: `CameraEvalContext::at(frame, FrameRate{30, 1})` — not CLEAN unless the call site has a documented exception (test fixtures only).
* Local constexpr: `constexpr FrameRate kTimelineFps{30, 1};` inside a function body that calls `Context::at()` — CLEAN-replacement was the A3-CTX-FRAMERATE source change in `src/scene/camera/camera_v1/shot_timeline.cpp`.

The propagation contract is enforced at:

* `CameraEvalContext::at(...)` — forwards to `SampleTime::from_frame_int(f, frame_rate)`.  No implicit `{30, 1}` substitution.
* `preroll_session_for_frame(...)` — the `FrameRate` parameter is forwarded bit-exact to the in-loop `CameraEvalContext::at(Frame{f}, frame_rate)` call inside the pre-roll walk (`camera_session_cache.cpp` lines ~80–88).  No fixture `{30, 1}` ever appears locally.
* `ShotTimelineResolver::evaluate(int frame, ShotTimelineSession& session, FrameRate fps)` — the project's `FrameRate` arrives as the 3rd positional; `CameraEvalContext::at(local_frame, fps)` uses it directly.  No fixture constexpr inside the resolver body.

### Source anchor

* `include/chronon3d/scene/camera/camera_v1/camera_motion_context.hpp` lines 42–47 (`CameraMotionContext::at`) and 78–86 (`CameraEvalContext::at`).
* `src/scene/camera/camera_v1/shot_timeline.hpp` lines ~76–89 (`ShotTimelineResolver::evaluate` signature post-A3-CTX-FRAMERATE).
* `src/scene/camera/camera_v1/shot_timeline.cpp` lines ~290–360 — the 3 `CameraEvalContext::at(...)` call-sites now use the resolver's `fps` parameter directly; the previous-recipe `constexpr FrameRate kTimelineFps{30, 1};` is removed.
* `include/chronon3d/scene/camera/camera_v1/camera_session_cache.hpp` line 197 (the `preroll_session_for_frame` signature with `FrameRate frame_rate` as 6th positional, no default).
* `src/scene/camera/camera_v1/camera_session_cache.cpp` lines ~36–88 (the helper implementation).

### Test lock

- `tests/scene/camera/test_camera_context_framerate_propagation.cpp` — fuzzes 6 FPS values {24, 25, 30, 50, 60, 120} across BOTH `CameraEvalContext::at` AND `CameraMotionContext::at`.  Two SUBCASEs:
  - **`camera_v1_context_at_propagates_framerate_bit_exact`** — the discriminator: `ctx.sample_time.seconds() == 7.0/(fps.den*fps.num/fps.num)` (i.e. the seconds value MUST scale FPS-by-FPS for Frame{7}).  A regression that defaulted to `{30, 1}` would flip this to `7/30 ≈ 0.2333` regardless of caller-supplied fps.
  - **`camera_v1_context_at_unique_per_fps`** — the uniqueness invariant: sample_time.seconds() MUST differ across all 6 fuzz values (a fixed default would collapse the count of distinct values).

### Failure mode (if silently broken)

A regression that re-introduced `FrameRate{30, 1}` as a factory default would (a) silently change the seconds-resolution of every evaluate() call to 30 fps regardless of caller-supplied fps; (b) make any non-30 fps scenario produce a different sample_time.seconds() than the same call at 30 fps would — a global nondeterminism bug for cine / PAL / high-frame-rate compositions.  The fix would be a one-line revert, visible only if you grep for `FrameRate{30, 1}` — which `tools/check_camera_architecture.sh` does *not* yet gate; the test lock is the only enforcement.

---

## Decision 5 — `DampedFollowConstraint` in `descriptor.constraints[]` forces `CameraProgram::evaluation_dependency() == RequiresHistory` (TICKET-A3-DAMPED-HISTORY, DoD gate (b))

### Spec

`evaluation_dependency_` is **defaulted to `Stateless`**, then **monotonically promoted to `RequiresHistory` if and only if** at least one entry in `descriptor.constraints[]` is a `DampedFollowConstraint`.  This is a deliberate contract: `DampedFollowConstraint` is the **SOLE** constraint type that maintains per-frame EMA state across calls (via `ConstraintState.previous_camera` + `previous_velocity` + `previous_time` + `has_previous`), and is therefore the **SOLE** constraint type that requires identity-preserving evaluation with the **SAME** `CameraSession`.

The default-then-promotion pattern guarantees two invariants by construction:

* The descriptor can NEVER be classified `Stateless` if it contains a `DampedFollowConstraint`.  The classifier does not depend on the source variant or the modifier list — only on `DampedFollow`'s presence in the constraint array.
* The descriptor is `Stateless` if and only if NO `DampedFollowConstraint` is in the array.  (Pairing-test SUBCASE B in the regression lock below pins the inverse.)

### Source anchor

`src/scene/camera/camera_v1/camera_program_compiler.cpp` lines ~325–355 (the `// ── 4. CAM-02 — compute evaluation_dependency metadata ─` block):

```cpp
program.evaluation_dependency_ = CameraEvaluationDependency::Stateless;

for (const auto& c : descriptor.constraints) {
    if (std::holds_alternative<DampedFollowConstraint>(c)) {
        program.evaluation_dependency_ = CameraEvaluationDependency::RequiresHistory;
        break;
    }
}
```

The TICKET-A3-DAMPED-HISTORY sentinel block-comment cites the EMA accumulator's identity-preserving-requirement rationale as the reason for the force-override (rather than a heuristic that could be silently lost in a refactor).

### Test lock

- `tests/scene/camera/test_camera_program_damped_history_force.cpp` — 5 SUBCASEs (A: only-DampedFollow on a `StaticCameraSource` reduces Stateless base ⇒ `RequiresHistory`, the strongest discriminator; B: same fixture minus DampedFollow ⇒ `Stateless`, inverse-control; C: DampedFollow in a non-zero position in `constraints[]` ⇒ `RequiresHistory`, catches any future "constraints[0]-only" regression; D: `DampedFollow + PoseTracksSource + HandheldNoise + multiple other constraints` ⇒ `RequiresHistory`, dominance; E: multiple `DampedFollowConstraint` entries ⇒ `RequiresHistory`, structural over-covering).

### Failure mode (if silently broken)

A regression that loses the `DampedFollow` check (e.g. by reverting to a heuristic that only inspects source variant or modifier domain) would classify a `DampedFollow`-bearing descriptor as `Stateless`, causing `CameraSessionCache::acquire()` to skip the pre-roll walk (because `preroll_session_for_frame(...)` short-circuits on `Stateless`).  EMA accumulates no `previous_camera` for the first target frame, producing frame-1 jitter that radiates through every constraint downstream.  The user-visible symptom is camera jitter at cut-on / cut-off boundaries — far from the source of the regression.

---

## Decision 6 — `LookAtLayer` orientation emits a `CameraProgramDiagnostic{Severity::Warning, "[MissingTransforms] …"}` via `result.diagnostics` when `CameraEvalContext::transforms == nullptr` OR `world_position(target) == nullopt` (TICKET-A3-LOOKAT-DIAGNOSTIC, DoD gate (g))

### Spec

When `CameraProgram::evaluate()` reaches the orientation stage with `descriptor_.orientation` containing a `LookAtLayer` and either:

* (a) `CameraEvalContext::transforms == nullptr`, OR
* (b) `transforms != nullptr` but `transforms->world_position(lal->target) == nullopt` (target layer is missing from the snapshot),

then the orientation generator (`apply_orientation_spec_free()` returning `std::optional<CameraProgramDiagnostic>`) MUST emit a `CameraProgramDiagnostic{Severity::Warning, "[MissingTransforms] LookAtLayer target='<name>': <reason>; rotation not updated."}`.  `CameraProgram::evaluate()` pushes the diagnostic onto `result.diagnostics.push_back(*orient_diag)` (the canonical push site, not stdout/stderr).

The diagnostic carries two machine-readable substrings for test grepping:

* `[MissingTransforms]` — stable prefix across both paths (a) and (b).
* `'<target_name>'` — single-quoted `lal->target`, so test code can disambiguate by target name.

Other arms of `apply_orientation_spec_free` (`FixedOrientation`, `LookAtPoint`, `OrientAlongPath`) and the `len > 1e-4f` short-circuit inside the world-position-resolved path are **out of scope** for this ADR.  The degenerate-look-dir case (transforms non-null, world_position resolved, but `look_dir.len <= 1e-4f` because the camera sits on the target) is currently silent-fallback and is tracked separately as TICKET-A3-LOOKAT-DEGENERATE (not on `main`).

### Source anchor

`src/scene/camera/camera_v1/camera_program.cpp` lines ~95–135 (the `if (auto* lal = std::get_if<LookAtLayer>(&orient))` block in `apply_orientation_spec_free()`).  The TICKET-A3-LOOKAT-DIAGNOSTIC sentinel block-comment documents the two emission paths.

`CameraProgram::evaluate()` push site: line ~471 (`result.diagnostics.push_back(*orient_diag)` — the canonical channel for ANY orientation-stage diagnostic, not just LookAtLayer).

### Test lock

- `tests/scene/camera/test_camera_lookat_layer_missing_transforms.cpp` — 4 SUBCASEs.  The strongest discriminator is SUBCASE A: `find_missing_transforms_diagnostic(res->diagnostics, "non.existent.layer")` MUST return a non-empty string.  SUBCASE B locks base rotation preservation (sentinel `(7°,13°,-5°)` round-trips bit-exact); SUBCASE C locks `point_of_interest_enabled stays false`; SUBCASE D locks per-frame cardinality (3 sequential evaluate() calls each emit the diagnostic, catching a "first-frame only" regression).

### Failure mode (if silently broken)

A pair of failure modes, both silent:

* **Silent-fallback regression** — `apply_orientation_spec_free()` reverts to `return std::nullopt` in either path.  Result: composition author cannot distinguish "look-at did its job" from "look-at silently no-op'd because no transforms snapshot," and downstream render-orchestration code that consumes `point_of_interest_enabled` cannot tell whether the look-at fired.
* **Wrong-channel regression** — diagnostic emitted via `std::cerr` or some `printf` instead of `result.diagnostics`.  Result: any telemetry consumer that reads `result.diagnostics` loses the signal, and consumers that instrument `std::cerr` will see ghost messages that came from a code path that should have used the canonical channel.

Both modes are caught by the SUBCASE A check.

## Decision 7 — DiagnosticChannel canonical emit: every `camera_v1` evaluate-stage diagnostic flows through `result.diagnostics` with `Severity::{Warning,Info,Severe}`; stdout/stderr/spdlog::warn are NEVER a diagnostic channel (TICKET-A3-DIAGNOSTIC-CHANNEL, DoD gate (h))

### Spec

Every diagnostic emitted from `CameraProgram::evaluate()` and from any evaluate-stage helper (orientation, constraint-loop, post-modifier) MUST be a `CameraProgramDiagnostic` struct with an explicit `Severity` tag (`Severity::Warning | Severity::Info | Severity::Severe`) attached via `result.diagnostics.push_back(...)` at the canonical push site `camera_program.cpp` line ~471.

Channel invariants:

* **`result.diagnostics` is the ONLY canonical emission channel** for evaluate-stage diagnostics.  `std::cerr`, `std::cout`, `printf`, `fmt::print(stderr, ...)`, `spdlog::warn`, `spdlog::info`, `spdlog::error` are NOT permitted from any code path that emits a `CameraProgramDiagnostic`.  The `Result<>` return value of `evaluate()` carries the diagnostics; consumers read `result.diagnostics` exclusively.
* **Every diagnostic MUST carry an explicit `Severity` tag** from the closed 3-value enum.  `Severity::Warning` for soft-fallback (LookAtLayer missing-transforms, KeepLastValidCamera recovered).  `Severity::Info` for advisory (modifier applied, fallback policy triggered).  `Severity::Severe` for imminent failure (ConstraintFailure emitted as diagnostic rather than thrown).
* **Decision 6 precedent is preserved verbatim**: `CameraProgramDiagnostic{Severity::Warning, "[MissingTransforms] ..."}` — Decision 7 only generalises the rule to ALL evaluate-stage helpers, not just `LookAtLayer`.

### Source anchor

* The canonical push site: `src/scene/camera/camera_v1/camera_program.cpp` line ~471 (`result.diagnostics.push_back(*orient_diag)` — already cited in Decision 6, generalised by Decision 7 to all orientation/constraint/modifier helpers).
* The `Severity` enum: `include/chronon3d/scene/camera/camera_v1/camera_program_diagnostic.hpp` lines ~35–60 (the closed 3-value enum).
* The struct: `struct CameraProgramDiagnostic { Severity severity; std::string message; /* optional source-anchor */ }` in the same header.
* The forbidden-channel grep (in-vivo enforcement): `grep -rnE 'std::cerr|std::cout|printf|fmt::print.*stderr|spdlog::(warn|info|error)' src/scene/camera/camera_v1/` must return 0 hits from evaluate-stage helpers (only pre-existing scaffold markers permitted).

### Test lock

* `tests/scene/camera/test_camera_program_diagnostic_channel.cpp` — primary lock.  Two SUBCASEs:
  - **`diagnostic_channel_emits_severity_tagged_only`** — fuzzes 6 evaluate() scenarios (LookAtLayer missing-transforms, KeepLastValidCamera recovered, modifier-applied, ConstraintFailure-as-diagnostic, DampedFollow EMA-overflow, source-variant fallback).  Asserts `severity != Severity::Unspecified` for every emitted diagnostic + emit count matches documented expected per scenario.
  - **`diagnostic_channel_avoids_side_channels`** — same 6 scenarios wrapped in `testing::CaptureStderr()` + `testing::CaptureStdout()` + a `spdlog::sinks_init_list` sink-capture.  Asserts captured side-channel output is empty (excluding pre-existing machine markers).
* Companion: the existing `tests/scene/camera/test_camera_lookat_layer_missing_transforms.cpp` Decision 6 lock is a structural sub-lock (Decision 6 diagnostic carries `Severity::Warning`).

### Failure mode (if silently broken)

* **Side-channel-emit rot** — diagnostic emitted via `spdlog::warn("Camera missing transforms for layer X")` instead of `result.diagnostics.push_back(...)`.  Telemetry consumers lose the signal entirely.  CI instrumentation that greps `spdlog::warn` in `camera_v1` sees ghost lines from a path that SHOULD have used the canonical channel.
* **Channel-collapse rot** — a regression that reduces `Severity` to a single enum value or drops the `severity` field entirely.  Consumers that filter `result.diagnostics` by severity (telemetry exporters that ONLY count `Severity::Severe`) cannot distinguish warning from severe.

Both caught by `test_camera_program_diagnostic_channel.cpp`.

---

## Decision 8 — `compile_camera()` is byte-deterministic over the descriptor contract; `CameraProgram` is read-only after `compiled_ = true` (TICKET-A3-COMPILER-DETERMINISM, DoD gates (i) + (j))

### Spec

Two paired invariants locking the compile-time surface:

1. **Byte-deterministic compile (gate (i))** — given `d1` and `d2` with `d1.fingerprint() == d2.fingerprint()`, then `compile_camera(d1)` and `compile_camera(d2)` are byte-equal.  Specifically: no non-deterministic iteration order over `descriptor.constraints[]`, `descriptor.modifiers`, or any `std::unordered_map` populated during compile; no pointer-identity or address-dependent value leaks; `CameraProgram` is `==` comparable with bit-exact equality; `compile_camera()` produces a program whose `descriptor_fingerprint()` matches the input descriptor's `fingerprint()`.  Determinism holds across processes, threads, and reorders.

2. **Post-compile immutability (gate (j))** — `CameraProgram` is read-only after `compiled_ = true`.  All accessor methods (`failure_policy()`, `time_dependent()`, `evaluation_dependency()`, `descriptor()`) are `const`-correct; no setter API exists.  No code path mutates a `CameraProgram` field after `compiled_ = true`.  The `compiled_` flag is set with an `assert(!compiled_)` precondition at compile-time entry (one-shot semantics).  **Scope clarification**: "read-only after compiled_" applies to PROGRAM-LEVEL FIELDS (`descriptor_`, `failure_policy_`, `time_dependent_`, `evaluation_dependency_`, `compiled_`); per-instance runtime EMA state (`previous_camera`, `previous_velocity`, `previous_time`, `has_previous`) lives on `CameraSession` (a separate object, per Decision 5's identity-preserving-requirement contract) and is runtime-mutable there — Decision 8 does NOT freeze session state, only program-level metadata.

### Source anchor

* **Determinism (gate (i))** — `src/scene/camera/camera_v1/camera_program_compiler.cpp` lines ~1–35 (`compile_camera()` entry-point) + the constraint-loop body that iterates `descriptor.constraints[]` in declaration order (NOT `std::unordered_map`).
* **Post-compile immutability (gate (j))** — `include/chronon3d/scene/camera/camera_v1/camera_program.hpp` lines ~30–110 (the `class CameraProgram` declaration).  No public non-`const` member function that mutates a `CameraProgram` field; `compile_camera()` returns a new program by value.
* The fingerprint contract: `include/chronon3d/scene/camera/camera_v1/camera_descriptor.hpp` `descriptor_fingerprint()` declaration (~lines 150–170) — bridge between Decision 3's `descriptor_fingerprint` cache-key and Decision 8's compile-determinism lock.

### Test lock

* `tests/scene/camera/test_camera_program_compiler_determinism.cpp` — primary lock.  Two SUBCASEs:
  - **`compiler_byte_deterministic_across_fuzz`** — runs `compile_camera(desc)` 1000 times with shadowed Map<K,V> insertion orders (12 different orderings).  Asserts all 1000 emitted programs are byte-equal under `CameraProgram::operator==`.  Cross-thread determinism: 16-thread parallel-compile fuzz where all 16 programs must bit-match.
  - **`program_immutable_post_compile_flag`** — runs `compile_camera(desc)` 100 times; `static_assert` that `decltype(program)::failure_policy() const`, `::time_dependent() const`, `::evaluation_dependency() const`, `::descriptor() const` are the only public accessors; `decltype(program).failure_policy() &` (non-`const`) MUST NOT compile.  Asserts `descriptor_fingerprint() == desc.fingerprint()` for every emitted program (Decision 1 + Decision 8 canon).
* Companion: `test_camera_program_compiler_post_compile_is_readonly.cpp` — counts public non-`const` functions on `CameraProgram`; fails if any are introduced.

### Failure mode (if silently broken)

* **Non-deterministic compile rot (gate (i))** — a `std::unordered_map` (or randomised-order container) in the constraint-iteration path.  `compile_camera(desc1)` and `compile_camera(desc2)` produce different `CameraProgram` instances even when `desc1.fingerprint() == desc2.fingerprint()`.  Decision 3's `CameraSessionCache::acquire()` then falls into `must_reprime` on every call, forcing per-frame pre-roll walk.  User-visible: per-frame ~3ms latency spike.
* **Post-compile mutation rot (gate (j))** — `CameraProgram::set_failure_policy(...)` introduced, or caller mutates `program.descriptor_.source` after `compiled_ = true`.  Cache-fingerprint contract from Decision 3 silently breaks; next acquire() emits a different camera.  Long-running render loops accumulate drift.

Both caught by `test_camera_program_compiler_determinism.cpp`.

---

---

## Ticket-to-code chain (TICKET-A3 cluster → ADR-011 cross-link)

This ADR is the consolidating node for the 8 tickets in the A3 cluster (C5-compliant Doc-only carries per `docs/FOLLOWUP_TICKETS.md`); the first 6 are the original A3 cluster, the last 2 are the **ADR-013-EXT** extension: TICKET-A3-METADATA → Decision 1; TICKET-A3-SESSION-POLICY → Decision 2; TICKET-A3-CACHE-LEASE → Decision 3; TICKET-A3-CTX-FRAMERATE → Decision 4; TICKET-A3-DAMPED-HISTORY → Decision 5; TICKET-A3-LOOKAT-DIAGNOSTIC → Decision 6; **TICKET-A3-DIAGNOSTIC-CHANNEL → Decision 7 (ADR-013-EXT)**; **TICKET-A3-COMPILER-DETERMINISM → Decision 8 (ADR-013-EXT)**.

| Ticket | Commit on `main` | Decision in this ADR | Source anchor | Test lock |
|---|---|---|---|---|
| TICKET-A3-METADATA (gate (a)) | `76cf76b5`-line commit on A3 timeline | Decision 1 | `camera_program_compiler.cpp` ~lines 78–95 + 287–307 | `test_camera_program_compiled.cpp::compiled_registered_motion_ref_does_not_inherit_outer_metadata` |
| TICKET-A3-SESSION-POLICY (gate (c)) | `4f4b7c2d`-line commit | Decision 2 | `camera_program.cpp` ~lines 478–522 + ~590 | `tests/runtime/test_camera_session_keep_last_valid.cpp` |
| TICKET-A3-CACHE-LEASE (gate (d)) | `3a5eb193`-line commit | Decision 3 | `camera_session_cache.cpp` ~lines 107–144 + 157–173 | `tests/runtime/test_camera_session_cache_failed_no_commit.cpp` |
| TICKET-A3-CTX-FRAMERATE (gate (e)) | `5d42cd63`-line commit | Decision 4 | `camera_motion_context.hpp` 42–86 + `shot_timeline.{hpp,cpp}` + `camera_session_cache.cpp` 36–88 | `tests/scene/camera/test_camera_context_framerate_propagation.cpp` |
| TICKET-A3-DAMPED-HISTORY (gate (b)) | `5f76a73b`-line commit | Decision 5 | `camera_program_compiler.cpp` ~lines 325–355 | `tests/scene/camera/test_camera_program_damped_history_force.cpp` |
| TICKET-A3-LOOKAT-DIAGNOSTIC (gate (g)) | `0d645b90`-line commit | Decision 6 | `camera_program.cpp` ~lines 95–135 + push at ~471 | `tests/scene/camera/test_camera_lookat_layer_missing_transforms.cpp` |
| **TICKET-A3-DIAGNOSTIC-CHANNEL (gate (h), ADR-013-EXT)** | _alongside ADR-013-EXT commit (Doc-only — contract introduced here; source-code commits to follow)_ | Decision 7 | `camera_program.cpp` ~line 471 (canonical push) + all evaluate-stage helpers emitting a `CameraProgramDiagnostic` | `tests/scene/camera/test_camera_program_diagnostic_channel.cpp` (locks `Severity::{Warning,Info,Severe}` exclusivity + no side-channel emit from evaluate()) |
| **TICKET-A3-COMPILER-DETERMINISM (gates (i)+(j), ADR-013-EXT)** | _alongside ADR-013-EXT commit (Doc-only — contract introduced here; source-code commits to follow)_ | Decision 8 | `camera_program_compiler.cpp` ~lines 287–307 (`compiled_ = true` writepoint) + `camera_program.hpp` class (no setter APIs post-`compiled_`) | `tests/scene/camera/test_camera_program_compiler_determinism.cpp` (fuzzes compile_camera() with shadowed map orderings; locks byte-identical program + post-`compiled_` immutability) |

### Chain to ADR-011 Decision 5 (call-site migration list)

[ADR-011 Decision 5](./ADR-011-camera-legacy-deletion.md#decision-5-camera2_5dprojection_mode-field--delete) enumerates the 106 in-tree matches for `Camera2_5D::projection_mode` — the field whose deletion was the FIRST canonicalisation close-out after F2.3.  The contracts in this ADR are **structurally similar**: each of the 6 A3 tickets addresses a "silent fallback that emits no observable signal" risk.  The 4 contracts + 2 close-outs (Decisions 5 + 6) above are the SECOND close-out pass: `camera_v1` runtime contracts whose broken state was previously of the same "no diagnostic, no test lock, no canonical channel" shape as `projection_mode` was pre-ADR-011.

Specifically:

* **Decision 1 (compiler late-rebuild)** mirrors ADR-011 Decision 5's policy of "do not inherit preset metadata silently into the program's compiled state."  Both are *post-resolution* rebuild rules.  The lock-style is identical: a regression test pins the *outer* descriptor's contribution; a regression that re-introduces preset-inheritance is caught bit-identically.
* **Decision 2 (KeepLastValidCamera wire)** mirrors ADR-011 Decision 5's policy of "delete the dual-axis dual-state rot."  `Camera2_5D::projection_mode` was a dual axis alongside `optics_mode`; `KeepLastValidCamera + Stop` were a dual fallback sharing the same `case` arm.  Both rot vectors were closed by collapsing to one writer-of-truth + one reader + one diagnostic channel.
* **Decision 3 (cache-lease commit-on-success)** mirrors ADR-011 Decision 5's policy of "delete `target_name` writes from `CameraMotionPath` field on `catmull_rom_path.hpp:500,512,524,558,567` and `spatial_bezier_path.hpp:324,339,352,392,402`."  Both fixes replace a stale state with a `commit()`-style single-writepoint guard.
* **Decision 4 (FrameRate propagation)** mirrors ADR-011 Decision 5's policy of "delete the implicit-30-fps default around the legacy `camera_motion_context`."  Both forbid "magic value as default" — the A3 contract forbids it in `CameraEvalContext::at()`, the F2.3 contract forbids it in any future re-introduction.

### Chain to the pre-existing rot at `src/render_graph/pipeline/camera_change_policy.cpp:24`

This is the **TICKET-camera-policy-pre-existing rot**, present pre-`ac514fea`: the rot was that `camera_change_policy.cpp` wrote to `prev->projection_mode != current.projection_mode` for the change-detection logic.  This was masked by the existence of BOTH `projection_mode` and `optics_mode` on `Camera2_5D`, AND by the `tools/check_camera_architecture.sh` gate's sanctioned-bridge exemption for the legacy `camera_change_policy.cpp`.  Fix in `ac514fea` (per `docs/FOLLOWUP_TICKETS.md`): `projection_mode → optics_mode`, unblocking `chronon3d_render_graph_tests` + `chronon3d_core_tests` LINK.

This rot is the **documentation precedent** for the 6 A3 contracts above.  Specifically:

* The rot was at a single source-file line (camera_change_policy.cpp:24) that aliased a legacy field whose deletion was separately documented in ADR-011 Decision 5.
* It was diagnosed by LINK failure (project-level gate), not by runtime observability (no diagnostic).
* The fix was a one-line rename (`projection_mode` → `optics_mode`), structurally trivial but with deep semantic consequences (the field's enum class `Camera2_5DProjectionMode` was still alive but the change-detection discriminated on the canonical field).

The 6 A3 contracts are designed so that a future rot of the same shape — "field X is silently default-init or inherited, no observability, no canonical channel" — is **observable** in the first place (via `result.diagnostics` (Decision 6), `program.evaluation_dependency()` (Decision 5), `last_evaluated_frame` (Decision 3), `ctx.sample_time.seconds()` (Decision 4), `*session.last_valid_camera` (Decision 2), `program.descriptor()->failure_policy` (Decision 1)) and is **locked** by a regression test that fails the moment the contract is silently broken.

A second-order consequence: each of the 4 contracts plus Decisions 5 + 6 has a SINGLE, identifiable source anchor (see the table above).  A future maintainer who breaks the contract breaks the SINGLE source-anchor file, which is therefore the FIRST file a maintainer reading this ADR would inspect.  This is the same "single-source-of-truth" property that ADR-011 Decision 5 establishes for the `Lens`-vs-`projection_mode` collapse.

### Cross-link to `docs/FOLLOWUP_TICKETS.md`

The 6-ticket A3 cluster + ADR-013 are tracked as **coherence cluster** in `docs/FOLLOWUP_TICKETS.md` under the camera_v1 compiler/session/cache row.  The cluster row records:

* `TICKET-A3-METADATA` (DoD gate (a)) — committed, regression lock present, ADR-013 cross-linked.
* `TICKET-A3-SESSION-POLICY` (DoD gate (c)) — committed, regression lock present, ADR-013 cross-linked.
* `TICKET-A3-CACHE-LEASE` (DoD gate (d)) — committed, regression lock present, ADR-013 cross-linked.
* `TICKET-A3-CTX-FRAMERATE` (DoD gate (e)) — committed, regression lock present, ADR-013 cross-linked.
* `TICKET-A3-DAMPED-HISTORY` (DoD gate (b)) — committed, regression lock present, ADR-013 cross-linked.
* `TICKET-A3-LOOKAT-DIAGNOSTIC` (DoD gate (g)) — committed, regression lock present, ADR-013 cross-linked.

The cluster row's last entry — "TICKET-camera-policy-pre-existing" carryover (M1.5#1 + M1.5#2) — stays out of the A3 cluster scope per `docs/FOLLOWUP_TICKETS.md` line ~55 and is documented separately as the **pre-existing rot precedent** for ADR-013's whole framing.

---

## Consequences

### Positive

* **Single-source-of-truth for camera_v1 contracts**: a maintainer reading the `camera_v1` source tree has ONE file (`docs/adr/ADR-013-camera-v1-semantics.md`) to consult for the behavioural rules + ONE row in the ticket-to-code table per contract to find the source anchor + ONE test-lock entry per contract to know what regression-test enforces the rule.
* **Observability of silent drift**: each of the 6 contracts emits a signal that a future regression would remove via the same paths as `Camera2_5D::projection_mode`.  The "silent fallback" class of rot is now first-order observable in the source tree.
* **AdR-011 Decision 5 chain formalised**: this ADR is the second pass of the same canonicalisation pipeline that ADR-011 documents for `Camera2_5D::projection_mode`.  The structural symmetry between the two ADRs makes the canonicalisation pattern (delete-legacy + lock-runtime) legible to future readers.

### Negative / Migration cost

* **Doc-only addition**: no source-code changes; no new SDK surface; no caller-side migration; consumers of `camera_v1::*` continue to work unchanged.  The 6 A3 cluster commits already shipped the source-code changes this ADR documents; this ADR is purely a documentation consolidation.
* **Doc gate**: `tools/check_doc_sync.sh` runs at CI; a future regression that breaks a contract WITHOUT updating the source anchor in this ADR will surface as "ticket-to-code chain stale."  This is the desired drift signal; not a cost.
* **Reviewer burden**: cross-document review of `docs/adr/ADR-013-camera-v1-semantics.md` against `src/scene/camera/camera_v1/*.cpp` is now a standing review item per release.  This is the desired burden for a contract ADR.

### Neutral

* The 6 A3 tickets' commits already ship the source-code changes; this ADR does not introduce new commits.
* `tools/check_camera_architecture.sh` gates `[1/6]`–`[6/6]` continue to enforce the camera_v1 surface from the architectural side; this ADR is the runtime-contract side of the same envelope.
* The `TICKET-camera-policy-pre-existing` rot (camera_change_policy.cpp:24, `projection_mode → optics_mode` fix in `ac514fea`) stays **out of scope** for the A3 cluster per `docs/FOLLOWUP_TICKETS.md`; it is documented here only as precedent.

## Alternatives considered

* **A. One ADR per A3 ticket.** Rejected — the A3 cluster is a coherence contract (the 6 tickets are interdependent; breaking any 1 breaks the determinism story the other 5 support).  A deciding maintainer who fixes only the source-code change without reading the surrounding 5 contracts would invite a recurrence of the silent-fallback rot.  A single ADR enforces co-reading.
* **B. Anchor the contracts in `docs/camera-plan/01-CANONICAL_CAMERA_ARCHITECTURE.md` instead of an ADR.** Rejected — `camera-plan` is forward-looking (the F2.3 design); ADRs are documented decisions (post-implementation).  The 6 A3 tickets are post-implementation, so this material is an ADR.
* **C. Defer ADR-013 until TICKET-A3-EXPRESSION-GATE + TICKET-A3-LEGACY-EXTRACTOR are closed.** Considered; rejected — the 6 closed contracts are coherent on their own and documenting them now frees the next-cluster round from "which contracts are still drifting?" overhead.  EXPRESSION-GATE / LEGACY-EXTRACTOR will be appended (or add a sibling ADR) when their deliverables land.
* **D. Skip Decision 6 (`LookAtLayer-missing-transforms`) because TICKET-A3-LOOKAT-DIAGNOSTIC is signed off as a single-fix ticket.** Rejected — Decision 6 is structurally identical to Decision 1–5 (silent-fallback + observability + regression lock) and the future maintainer benefits from reading 6 entries in the same ADR rather than 5 + 1 in a different document.

## References

* AGENTS.md §"Piano operativo canonico" + §"Feature freeze" (`5/11 GATES`, `11/11 GATES` revocation path) + §"Regole di lavoro" (Doc-only ticket policy, single-commit-per-ticket principle).
* [ADR-001 — frame-graph-compiler](./ADR-001-frame-graph-compiler.md) — the runtime representation that this ADR's `CameraProgram::evaluate()` evaluates against.
* [ADR-008 — render-engine-renderer-ptr](./ADR-008-render-engine-renderer-ptr-return.md) — anchors the SDK INSTALL posture that this ADR's runtime contracts unblock.
* [ADR-010 — boundary-gate-semantic-extension](./ADR-010-boundary-gate-semantic-extension.md) — gate `[14/14]` consumes this ADR's status as part of the BLOCKING transition path (post-F2.3 sync).
* **<a id="adr011-decs5"></a>[ADR-011 — camera-legacy-deletion](./ADR-011-camera-legacy-deletion.md), Decision 5 in particular** — the `Camera2_5D::projection_mode` migration list that the 4 contracts above are designed to prevent *recurrence of*.
* `docs/camera-plan/01-CANONICAL_CAMERA_ARCHITECTURE.md` — the F2.3 design spec.
* `docs/camera-plan/02-OPTICS_PROJECTION_ORIENTATION.md` — the Projection vs FieldOfView discrimination contract that Decision 5 (`DampedFollow`-forces-RequiresHistory) is consistent with (Projection-vs-Fov is a compile-time surface; DampedFollow's identity-preserving-requirement is a runtime surface; both are compile-baked into `CameraProgram`).
* `docs/camera-plan/03-MOTION_TRAJECTORY_TIMELINE_DETERMINISM.md` — the canonical pre-roll procedure that Decision 3 (`commit()` as sole writepoint for `last_evaluated_frame`) implements.
* `docs/camera-plan/04-INTEGRATION_TESTS_AND_LEGACY_REMOVAL.md` — the integration-test plan that the 6 A3 regression locks implement.
* TICKET-A3-METADATA / SESSION-POLICY / CACHE-LEASE / CTX-FRAMERATE / DAMPED-HISTORY / LOOKAT-DIAGNOSTIC — the 6 A3-cluster tickets in `docs/FOLLOWUP_TICKETS.md` whose canonical contract is consolidated here.
* TICKET-camera-policy-pre-existing (M1.5#1 carryover + M1.5#2 carryover) — the `camera_change_policy.cpp:24` rot fixata in `ac514fea` (projection_mode→optics_mode); the **documentation precedent** for the silent-fallback rot class that ADR-013's 6 contracts address.
* `tools/check_camera_architecture.sh` (gates `[1/6]`–`[6/6]`) — enforces the architectural-side envelope that this ADR's runtime contracts sit inside.
* `tools/check_doc_sync.sh` — `docs/adr/ADR-NNN-<title>.md` is a canonical file; this ADR is in-scope.
* `tools/install_consumer_test.sh` — the SDK consumer-side attestator that ADR-013's "no surface expansion" claim is audited.
* `include/chronon3d/scene/camera/camera_v1/camera_descriptor.hpp` + `camera_program_compiler.hpp` + `camera_program.{hpp,cpp}` + `camera_session.{hpp,cpp}` + `camera_session_cache.{hpp,cpp}` + `camera_motion_context.hpp` + `shot_timeline.{hpp,cpp}` — the 8 files whose contracts are documented in Decisions 1–6 above.
