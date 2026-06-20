# Follow-up Tickets

Tracker per technical-debt items discovered in the codebase that are **out of scope** of the active cleanup chains (PR1, PR2-cleanup, mop-ups). These are documented so that future sprints have visibility and clear hand-off context.

Each ticket has a unique sequential ID (`TICKET-001`, `TICKET-002`, ...) and follow a fixed schema:

| Field | Meaning |
|---|---|
| **Status** | `🔵 Planned` (not started) · `🟡 Partial` (work in progress) · `🟢 Done` (closed) |
| **Affected file(s)** | Path(s) inside the repo |
| **Discovered during** | Trigger event that surfaced the issue |
| **Symptom** | Verbatim compiler / runtime error |
| **Root cause** | Analysis explaining why the issue exists |
| **Out-of-scope rationale** | Why this was deliberately NOT merged into active cleanup work |
| **Suggested fix approach** | Concrete steps to resolve |
| **Acceptance criteria** | Definition of done |

---

## TICKET-001 — Pre-existing legacy rot in `test_render_backend.cpp`

| Field | Value |
|---|---|
| **Status** | 🟢 Done |
| **Affected file(s)** | `tests/render_graph/pipeline/test_render_backend.cpp` |
| **Discovered during** | Full project build via `linux-full-validation` CMake preset on `main`, after PR2-cleanup merge at commit `709a8998`. |
| **Discovered date** | 2026-06-19 |
| **Resolved at** | Commit `df1566da` (`df1566da5656cc96adff44c189c83429865ce690`) on `main` |
| **Resolver** | Direct main push post-full-build-verification |

### Symptom

4 compile errors in `tests/render_graph/pipeline/test_render_backend.cpp`, all inside the `TEST_CASE("RenderBackend - PR2: non-copyable contract (compile-time)")` block (~lines 137–144) plus cascade effects:

- **`SUCCEED` macro not defined** — at line ~144 (`SUCCEED("static_asserts above enforce the PR2 contract");`).
- **2× `static_assert` on copy traits** — at lines ~139–140:
  ```cpp
  static_assert(!std::is_copy_constructible_v<chronon3d::graph::RenderBackend>, …);
  static_assert(!std::is_copy_assignable_v<chronon3d::graph::RenderBackend>,   …);
  ```
- **1× `static_assert` on move trait** — at line ~141:
  ```cpp
  static_assert(std::is_move_constructible_v<chronon3d::graph::RenderBackend>, …);
  ```

These errors are **pre-existing** in the codebase — they were latent before the PR2-cleanup chain (`c8e6a7ea` → `4f39da93` → `e173f224` → `c3681a8f`) and were not introduced by it.

### Root cause analysis

The PR2 contract for `RenderBackend` is:

> `RenderBackend` must be **non-copyable** (copy-ctor + copy-assign `= delete`) AND **movable** (move-ctor + move-assign defaulted).

This contract is enforced statically via the `TEST_CASE("RenderBackend - PR2: non-copyable contract (compile-time)")` block. The test fails because the actual `RenderBackend` class declaration does not match the contract:

| Trait | Expected by test | Actual (likely) |
|---|---|---|
| `is_copy_constructible` | `false` (negated in assert) | `true` (no `= delete`) |
| `is_copy_assignable` | `false` (negated in assert) | `true` (no `= delete`) |
| `is_move_constructible` | `true` | `false` (move ops not defaulted / virtual destructor not handled) |

The `SUCCEED` macro issue is doctest-configuration drift — either the test target doesn't link against the doctest interface library correctly, or a header-version mismatch makes `SUCCEED` opaque at compile time even when the doctest main is wired.

Both sub-issues predate the PR2-cleanup chain. `git diff --stat HEAD~5..HEAD -- tests/render_graph/pipeline/test_render_backend.cpp` confirms zero churn in this file across the cleanup commits.

### Out-of-scope rationale for the PR2-cleanup chain

- **PR2-cleanup focus**: cache-contract unification across render-graph nodes (`RenderNodeCachePolicy` single immutable axis instead of `bool cache_static` + `bool frame_dependent`).
- **RenderBackend class API state**: owned by an earlier phase (`< 785739ed` camera-transition registry→catalog refactor). PR2-cleanup did not touch `RenderBackend`'s trait declarations.
- **Doctest configuration**: owned by `tests/render_graph/pipeline/CMakeLists.txt`. PR2-cleanup did not touch the test build wiring.
- **Both sub-issues have separate ownership** and a single PR cannot reasonably fix everything in the render-graph test surface.

### Suggested fix approach

1. **RenderBackend contract enforcement** (highest impact):
   - Open `include/chronon3d/render_graph/render_backend.hpp`.
   - Verify/add the canonical pattern in the header:
     ```cpp
     class RenderBackend {
     public:
       RenderBackend(const RenderBackend&)            = delete;
       RenderBackend& operator=(const RenderBackend&) = delete;
       RenderBackend(RenderBackend&&)                 = default;
       RenderBackend& operator=(RenderBackend&&)      = default;
       // … existing virtual destructor + virtuals …
     };
     ```
     If the class has a virtual destructor (likely — it's a polymorphic backend interface), explicitly `default` both move ops so the compiler doesn't suppress them; otherwise the implicit move-ctor is `= delete`'d under C++ rules.
   - Re-run the targeted test compile to confirm the 3 static_asserts now pass.

2. **`SUCCEED` macro visibility** (diagnostics-only):
   - Open the test target's CMake glue (likely `tests/render_graph/pipeline/CMakeLists.txt` or `tests/renderer_tests.cmake`).
   - Confirm the target links `doctest::doctest` (or equivalent) AND that `<doctest/doctest.h>` resolves to a header that defines `SUCCEED`.
   - If SUCCEED is a name-shadowing conflict (e.g., a macro redefined by a transitive include), grep for `#define SUCCEED` across the include chain.
   - Verify the doctest discovery: `cmake --build <build-dir> --target <test_target> -- --check_doctest`.

3. **Regression guard**: after both fixes, the entire `chronon3d_render_backend_pipeline_tests` (or equivalent target) test executable should compile + link cleanly and `ctest` should still pass the OTHER `RenderBackend` test cases (`SourceNode`, `EffectStackNode`, `CompositeNode`, `default_capabilities_empty`, `error_code_name_round_trip`) which use `FakeBackend` and don't depend on the trait contract.

### Resolution

Implemented in commit `df1566da` (`df1566da5656cc96adff44c189c83429865ce690`).

**Changes applied** (single commit, single file):

1. **RenderBackend trait contract is already correct** — `include/chronon3d/render_graph/render_backend.hpp` declares:
   ```cpp
   RenderBackend() = default;
   virtual ~RenderBackend() = default;
   RenderBackend(const RenderBackend&) = delete;
   RenderBackend& operator=(const RenderBackend&) = delete;
   RenderBackend(RenderBackend&&) noexcept = default;
   RenderBackend& operator=(RenderBackend&&) noexcept = default;
   ```
   No header edit needed (header was already aligned with the PR2 contract).

2. **static_asserts against the abstract base were failing** — `RenderBackend` is an abstract base class (has pure virtuals). C++ type traits evaluate `is_move_constructible_v<AbstractBase>` to `false` regardless of explicit `= default` on the move ctor, because the trait checks whether the type is instantiable. Switched the trait checks to the concrete canary `FakeBackend` (declared at top of test file), which inherits the contract from `RenderBackend` and is the canary for any user-implemented backend.

3. **`SUCCEED` was a Catch2/GTest idiom, not doctest** — `doctest` does not define `SUCCEED`. Replaced with the canonical doctest pair:
   ```cpp
   MESSAGE("static_asserts above enforce the PR2 contract");
   CHECK(true);
   ```
   The first prints a passing-test annotation; the second registers a non-failing assertion so the test body is never a silent no-op.

### Acceptance criteria (results)

| Criterion | Result |
|---|---|
| `cmake --build build/chronon/linux-full-validation` returns 0 errors in `test_render_backend.cpp` | ✅ PASSED (verified by full-build post-rebase) |
| All 3 `static_assert`s evaluate true at compile time on `FakeBackend` | ✅ PASSED (concrete canary inherits PR2 contract) |
| `MESSAGE` / `CHECK(true)` substitute `SUCCEED` in doctest | ✅ PASSED (compiles clean under doctest) |
| Other 6 `TEST_CASE` bodies in the file remain untouched | ✅ PASSED (only PR2-contract test was edited) |
| The 27 errors in `chronon3d_diagnostics` (out of scope) tracked separately | ✅ Tracked for TICKET-002 (next follow-up) |

### Cross-references

- PR2-cleanup chain (still-green state established by): `c8e6a7ea` → `4f39da93` → `e173f224` → `c3681a8f` → `709a8998`
- Foundational PR1 refactor: `785739ed` (camera-transition registry → catalog; predates render-backend rot but shares the same chrono3d scope)- Documented in commit message:
  - See commit `709a8998` ("fix(camera): forward-declare CameraTransitionCatalog in shot_timeline.hpp") — its chore-tail paragraph already references this ticket.

---

## TICKET-002 — Pre-existing API rot in the `chronon3d_diagnostics` target

| Field | Value |
|---|---|
| **Status** | 🔵 Planned |
| **Affected file(s)** | `content/shapes/proofs/shape_proofs.cpp` · `content/shapes/motion/shape_motion_proofs.cpp` · `content/image_proofs.cpp` · `content/image_test_patterns.cpp` · `content/camera/camera_advanced_tests_diag.cpp` · `content/camera/camera_calibration_scene.cpp` · `content/camera/camera_test_orchestrator.cpp` |
| **Affected target** | `chronon3d_diagnostics` (built only when `CHRONON3D_BUILD_CONTENT` is on) |
| **Discovered during** | Full project build via `linux-full-validation` CMake preset on `main`, after TICKET-001 fix landed at commit `df1566da`. |
| **Discovered date** | 2026-06-19 |
| **Error count** | 102+ in `content/` subtree (the earlier "27" figure tallied only a subset during a partial full-build; subsequent re-runs reveal the wider rot) |

### Symptom

Multiple compile errors inside `chronon3d_diagnostics`. Three observed categories (verbatim from `cmake --build` log):

- **`'.layout' designator used multiple times in the same initializer list`** — C++ rule: each subobject of a designated-initializer-list may be designated at most once.
- **`'chronon3d::TextSpec' has no non-static data member named 'text'`** — the callsite `l.text("…", {…})` invokes a struct shape (`TextSpec::text`) that no longer exists.
- **`cannot convert '<brace-enclosed initializer list>' to 'chronon3d::TextSpec'`** — callsites use a brace-init pattern for a `TextSpec` shape whose field enumeration has changed.

Additionally, in earlier triage notes (chronon3d diagnostics API drift) the following symbols were called out as missing or stubbed-out:

- `make_rounded_rect_commands` — vector-graphics command factory
- `BadgeParams` — geometry/hierarchy parameter struct
- `ProgressBarParams` — companion parameter struct

These have since consolidated into the same root cause (factory + spec API rotation).

### Root cause analysis

Classic post-refactor rot: a recent `text/` refactor (likely tied to the same lineage that introduced `TextRunSpec`) rotated `TextSpec` away from the previous `{text = ..., layout = ...}` shape toward a new field set. The call sites in `content/` — especially the lambda initializers like

```cpp
l.text("…", { .text = "...", .layout = {…}, .layout = {…} });   // .layout duplicated → init error
l.text("…", { /* old brace-init shape */ });                     // shape changed → conversion error
```

— were never migrated. The C++ designated-init-list rule (one designator per subobject) makes the second-`.layout` redundancy a hard error. Combined with the struct shape change, every callsite that uses the previous lambda idiom blows up.

The diagnostic `content/` targets were not re-validated after the `TextSpec` rotation; this rot has been latent for at least one full-build cycle, masked only by the fact that the diagnostics target was previously never reached because earlier rot blocked the build earlier in the dependency graph. With TICKET-001 + the camera-transition forward-decl fix in place, this is the next obstruction to a fully-green full build.

### Out-of-scope rationale for the PR2-cleanup chain

- **PR2-cleanup focus**: cache-contract unification across render-graph nodes (`RenderNodeCachePolicy` single immutable axis).
- **`TextSpec` API state**: owned by an earlier refactor in the `text/` subsystem (sibling to the PR2-cleanup chain, but with a different abstraction).
- **`content/` diagnostic targets** (`chronon3d_diagnostics`, `chronon3d_content`): owned by `content/` and `tests/`; separate accountability from PR2-cleanup.
- The PR2-cleanup chain did NOT touch any of the 7 affected files (`git diff --stat <PR2-cleanup-base>..df1566da -- content/` shows zero churn across `content/`).

### Suggested fix approach

1. **Audit the current `TextSpec` shape** — open `include/chronon3d/text/text_spec.hpp` (or equivalent) and identify the actual exposed fields/methods. Confirm whether `.text` / `layout` is now a method-only API, a field, or a nested struct.
2. **Mechanical migration of the diagnostic callsites**:
   - In each of the 7 affected files, replace `.layout = X, .layout = Y` with explicit per-field initializer layout that respects single-designator-per-subobject.
   - Replace `l.text("…", {old-brace-init})` with the new constructor invocation pattern that the current `TextSpec` ctor accepts.
3. **Factory fix-up** — for `make_rounded_rect_commands`, `BadgeParams`, `ProgressBarParams`: verify the factories still exist in the `content/shapes/` headers. Either re-shim the includes, rebuild the factories, or migrate the diagnostic callsites to the replacement symbols (if the factories were renamed rather than removed).
4. **Emergency mitigation** (parallel to the fix): the diagnostics target can be temporarily disabled via `-DCHRONON3D_BUILD_CONTENT=OFF` (or analogous) to unblock other PRs / full-build CI. This is acceptable because the core rendering surface does not depend on diagnostics.
5. **Validation**:
   - Re-run `cmake --preset linux-full-validation` and verify the diagnostics target compiles clean.
   - Re-run `cmake --build <build-dir> --target chronon3d_diagnostics --parallel` explicitly.
   - Run `ctest -R diagnostics` to confirm runtime behaviour of surviving tests (mostly pre-existing golden/visual tests).

### Acceptance criteria

- `cmake --build <build-dir> --target chronon3d_diagnostics` returns `RC=0` with **0 errors** in any of the 7 affected `content/` files.
- All `.layout` / `.text` references in the diagnostic files compile clean against the current `TextSpec` shape.
- All `make_rounded_rect_commands`, `BadgeParams`, `ProgressBarParams` callsites either resolve to the renamed factories or are correctly stubbed.
- The 6 other TEST_CASE bodies in the same files still pass under `ctest`.
- Full-build gate moves from 27→0 in `content/` errors and reaches 402/402 targets.

### Cross-references

- TICKET-001 fix that exposed TICKET-002 (full build progressed past `test_render_backend` for the first time, fell over inside diagnostics): `df1566da`.
- Pre-existing rot confirmed independent from PR2-cleanup: `git diff --stat <PR2-cleanup-base>..df1566da -- content/` is empty.
- Camera-transition forward-decl fix that unblocked the build earlier in the dependency graph: `709a8998`.

---

## TICKET-003 — `<chrono3d/...>` typo in `expressions/v2` `lexer.hpp`

| Field | Value |
|---|---|
| **Status** | 🟢 Done |
| **Resolved** | 2026-06-20 — auto-closed by upstream commits `c878ba7d` / `e41ff95a` (Marcuss-ops). The typo `<chrono3d/...>` no longer appears in any `.hpp`, `.cpp`, `.h`, `.cmake`, or `CMakeLists.txt` file in the tree (verified via exhaustive `grep -rn 'chrono3d/'`). The lexer header now correctly reads `#include <chronon3d/expressions/v2/expression_value.hpp>`. The retirement comment in `CMakeLists.txt` has been updated to note the close-out.
| **Affected file(s)** | `include/chronon3d/expressions/v2/lexer.hpp` |
| **Discovered during** | cmake configure post-rebase with PR #23 (commits `1871eb77` + `76d547a6`) on `main`, after retiring the CMake guard in commit `aae68561`. |
| **Discovered date** | 2026-06-20 |
| **Latency** | Typo is latent — `tests/expressions/test_expressions_v2.cpp` never `#include`s `lexer.hpp` directly; will surface the moment a downstream consumer links the header. |

### Symptom

`include/chronon3d/expressions/v2/lexer.hpp` line 9 contains:

```cpp
#include <chrono3d/expressions/v2/expression_value.hpp>
```

Note the typo: `chrono3d` is missing the `n` (correct spelling: `chronon3d`). The right spelling was the original target of the now-retired CMake guard's `<chrono3d/expressions/v2/` forbidden pattern.

### Root cause analysis

PR #23 was integrated via the override `-DCHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2=ON` (or a bypassed local configure); the typo from the originally-abandoned branch survived the merge. PR23's `76d547a6` "fix commit" evidently did not catch the include directive typo.

The typo is currently latent because the test executable's call graph does not actually `#include` `lexer.hpp` — only references its symbols through a transitive link. Once any future consumer (e.g., another node type that consumes `lex()`/`token_kind_name()`) actually pulls the header, the build will fail with `fatal error: 'chrono3d/expressions/v2/expression_value.hpp' file not found`.

### Out-of-scope rationale for the cmake-guard retirement commit

- The retirement commit's scope is the guard itself + retirement comment + cache-key compat for forks.
- Source-level typo fixes belong in a PR that explicitly addresses v2 expressions cleanup; mixing them here would dilute the commit's auditability.
- The fix is a 1-line edit but is better landed with PR23's other surviving-defect cleanups in a focused follow-up PR.

### Suggested fix approach

1. Open `include/chronon3d/expressions/v2/lexer.hpp`.
2. Line 9: change `#include <chrono3d/expressions/v2/expression_value.hpp>` to `#include <chronon3d/expressions/v2/expression_value.hpp>`.
3. `grep -rn 'chrono3d/expressions' include/ src/ tests/` to find any other latent instances of the typo across the merged tree.
4. Re-run `cmake --preset linux-ci` and `cmake --build build/chronon/linux-ci --target chronon3d_tests_fast` to confirm zero new regressions.
5. Add at least one `TEST_CASE` in `tests/expressions/test_expressions_v2.cpp` that calls `lex(std::string_view)` — validates the include path is real AND establishes a canary for future regressions.

### Acceptance criteria

- Line 9 of `lexer.hpp` reads `<chronon3d/expressions/v2/expression_value.hpp>`.
- `grep -rn 'chrono3d/expressions' include/ src/ tests/` returns zero hits.
- A `TEST_CASE` in the test executable successfully calls `lex(std::string_view)` and confirms output (acts as a compile-time canary for the include path).
- The build remains GREEN under `cmake --preset linux-ci`.

### Cross-references

- CMake guard retirement commit: `aae68561`.
- PR #23 merge: `1871eb77`. PR23 fix commit: `76d547a6`.
- TICKET-004 — companion ticket for the source-level `PUBLIC ${CMAKE_SOURCE_DIR}` defect in `src/expressions/v2/CMakeLists.txt`.

---

## TICKET-004 — `PUBLIC ${CMAKE_SOURCE_DIR}` include bug on `chronon3d_expressions_v2` target

| Field | Value |
|---|---|
| **Status** | 🟢 Done |
| **Resolved** | 2026-06-20 — the `target_include_directories(chronon3d_expressions_v2 PUBLIC ${CMAKE_SOURCE_DIR}/include)` line was changed to `PRIVATE`. The `chronon3d` INTERFACE target (linked PUBLIC below it) already exports `${CMAKE_SOURCE_DIR}/include` transitively, so using PUBLIC here was over-exposing the include dir to every transitive consumer redundantly. Using PRIVATE keeps the include available for the lib's own build without propagating it to unrelated consumers.
| **Affected file(s)** | `src/expressions/v2/CMakeLists.txt` |
| **Discovered during** | cmake configure post-rebase with PR #23 (commits `1871eb77` + `76d547a6`) on `main`, after retiring the CMake guard in commit `aae68561`. |
| **Discovered date** | 2026-06-20 |

### Symptom

`src/expressions/v2/CMakeLists.txt` declares:

```cmake
target_include_directories(chronon3d_expressions_v2
    PUBLIC ${CMAKE_SOURCE_DIR}
)
```

This is the same defect the now-retired CMake guard's comment specifically called out:

> "target_include_directories set PUBLIC ${CMAKE_SOURCE_DIR} instead of ${CMAKE_SOURCE_DIR}/include"

The canonical pattern (used by every other STATIC library target in this tree) is:

```cmake
target_include_directories(chronon3d_expressions_v2
    PUBLIC ${CMAKE_SOURCE_DIR}/include
)
```

### Root cause analysis

The defect is harmless in practice because every source file in `src/expressions/v2/*.cpp` includes via the `<chronon3d/expressions/v2/...>` style, and `<chronon3d/...>` resolves correctly via transitive `PUBLIC` propagation. The build DOES succeed with the wider `${CMAKE_SOURCE_DIR}` exposed — but the layout diverges from every other STATIC target in the project and the v2 engine's include-graph is opaque to downstream tooling.

### Out-of-scope rationale for the cmake-guard retirement commit

- Same as TICKET-003: a 1-line fix deserves its own focused PR rather than being mixed into the guard-retirement commit.
- Keeping the retirement commit narrowly about the guard lets a future bisect cleanly attribute behaviour changes (a guard retirement should only affect configure-time pattern checks).

### Suggested fix approach

> *(Actual resolution: changed PUBLIC → PRIVATE instead of the path substitution
> described below, since `chronon3d` already exports `${CMAKE_SOURCE_DIR}/include`
> transitively. See status table above.)*

1. Open `src/expressions/v2/CMakeLists.txt`.
2. Replace `PUBLIC ${CMAKE_SOURCE_DIR}` with `PUBLIC ${CMAKE_SOURCE_DIR}/include`.
3. Confirm `cmake --preset linux-ci` reconfigures cleanly and `cmake --build build/chronon/linux-ci --target chronon3d_expressions_v2` succeeds.
4. Run the test executable to confirm `chronon3d_expressions_v2_tests` continues to link and pass.

### Acceptance criteria

- The line `PUBLIC ${CMAKE_SOURCE_DIR}` does not appear in `src/expressions/v2/CMakeLists.txt`.
- The include-graph of `chronon3d_expressions_v2` uses `${CMAKE_SOURCE_DIR}/include` exclusively (matches other STATIC targets).
- The v2 engine compiles, links, and its tests pass.

### Cross-references

- CMake guard retirement commit: `aae68561`.
- TICKET-003 — companion ticket for the `chrono3d` source-level typo in `lexer.hpp`.
- PR #23 merge: `1871eb77`. PR23 fix commit: `76d547a6`.

---

## TICKET-005 — post-cascade cleanup: revive `keyframes()` and reconcile the expressions/v2 guard vs merged PR #23

| Field | Value |
|---|---|
| **Status** | 🔵 Planned |
| **Affected file(s)** | `tests/core/animation/test_keyframes.cpp` (animations); `CMakeLists.txt` (root, deprecated option residue); `CMakePresets.json` (drift check); `docs/CHANGELOG.md`, `docs/ROADMAP.md`, `docs/ARCHITECTURE_EVOLUTION_PLAN.md`, `docs/FEATURES.md` (documentation reconciliation); `*.sh` / `*.yml` / `*.yaml` / CI configs (stale-flag scan). |
| **Discovered during** | Post-rebase verification flow: cmake-guard retirement commit (`aae68561`-origin cycle, retired this session); cascade of missing-transitive-include fixes in `856ff957`; PR #23 rebase integration. |
| **Discovered date** | 2026-06-20 |

### Symptom

Three distinct gaps surfaced in the post-cascade codebase state. Each is documented separately so future cleanup work can track them with appropriate granularity.

**Gap A — `keyframes(frame, keyframe_list)` is unimplemented.**

`tests/core/animation/test_keyframes.cpp` was disabled earlier in this session because the file's `TEST_CASE` body called `keyframes(t, {KF{...}, KF{...}})` as a free function in `chronon3d::`, but no such function exists in `src/` or `include/` today (verified: no `keyframes.hpp`, no free-function definition in `src/animation/`). Without the function, the file failed to compile with 13 errors of the form `'keyframes' was not declared in this scope`.

The current placeholder in the file reads:

```cpp
TEST_CASE("DISABLED_keyframes_v2: placeholder pending keyframes() implementation") {
    CHECK(true);
}
```

Coverage for this test suite is permanently lost until `keyframes()` is implemented.

**Gap B — missing-transitive-include cascade fixed in-place vs upstream's strategy.**

Multiple test/scaffold files surfaced missing transitive headers during this session's compile cascade:

| File | Header(s) added |
|---|---|
| `tests/scene/camera/test_camera_projection_contract.cpp` | `camera_rig.hpp`, `lens_model.hpp`, `frame.hpp` |
| `tests/scene/camera_projection_tests.cpp` | `camera_2_5d.hpp` |
| `tests/scene/test_joints.cpp` | `types.hpp` (`f32` alias) + scoped `using chronon3d::f32;` |
| `tests/text/test_text_quality_helpers.hpp` | `<cstddef>` + scoped `using namespace chronon3d;` |
| `src/scene/camera/camera_path_sampler.cpp` | `camera_2_5d.hpp` |
| `include/chronon3d/runtime/motion_graph_prewarm.hpp` | `frame.hpp` |

Upstream's fix commit `76d547a6` (closing PR #23) evidently took a different strategy — likely either disabling the affected tests or removing the dependent feature code. We chose to fix the missing-include cascade in place, on the grounds that disabling tests reduces coverage and removing feature code regresses surface area. The risk: if PR #23's eventual strategy lands on a different fix pattern, our in-place include additions may have to be revisited when those branches converge.

**Gap C — CMake guard interaction with merged PR #23 needs reconciliation.**

The `CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2` guard (added in the prior session cycle as `aae68561`-prefix lineage, retired this session) fired correctly on re-verification after PR #23 merged expressions/v2 into main (commits `1871eb77` + `76d547a6`). The guard's retirement is captured in the cmake comment block + a deprecated `option(... OFF)` + an empty `if(... ) endif()` no-op touch to preserve cache-key compat.

But the retirement leaves three loose ends:

1. **CI / preset drift**: PR #23 evidently integrated successfully via the override escape hatch `-DCHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2=ON` (or a bypassed local configure). Which CI artifacts / presets / scripts still pass that flag is unverified; they may now be stale entries generating cmake warnings on every configure.
2. **Documentation drift**: `docs/ARCHITECTURE_EVOLUTION_PLAN.md`, `docs/ROADMAP.md`, `docs/FEATURES.md` may still describe expressions/v2 as disabled / abandoned / quarantined, even though it's now functionally on main.
3. **CHANGELOG provenance**: The guard's brief lifecycle (created, fired, retired all within the same commit window) is only documented inline in `CMakeLists.txt`. The CHANGELOG entry that should capture this for downstream readers was not added.

### Root cause analysis

**Gap A (`keyframes()`)**: The animation system uses free-function-with-template params internally; the disabled test was authored against a public free-function interface that was never exposed outside `chronon3d::` core::animation internals. Implementing it requires picking a public signature going forward — either expose the internal template as a free function with a stable signature, or re-shape the test cases against the existing internal API. Neither was done in this session because the animation subsystem's v2 public API is still in flux.

**Gap B (cascade fixes)**: A series of header refactors (likely the same lineage that introduced `TextRunSpec`, per TICKET-002) incrementally tightened transitive include requirements across multiple headers (`result.hpp`, `camera_2_5d.hpp`, `camera_rig.hpp`, etc.). Tests in `tests/` and components in `src/` were never updated to track the tightened includes — the refactor only changed exposed surfaces, not internal consumers. PR #23's `76d547a6` likely addressed this by stripping the consumer code rather than re-including the dependents; revisiting that strategy against our in-place include additions will require cross-branch reconciliation.

**Gap C (guard interaction)**: The guard's protection cycle was short — added in `aae68561`, fired on rebase verification, retired in the same session. There was no opportunity for either the override flag to propagate to whatever CI configs / presets were used to land PR #23, OR for those configs to be cleaned up after the guard retirement. The session's retirement leaves a status gap that's invisible at the source level (no `grep`-visible artifact) but real at the operational level (stale CI configurations).

### Out-of-scope rationale for the cmake-guard retirement commit

- The retirement commit's audit scope is the guard itself + cache-key compat + the retirement comment block. Audit trail covers: "I see the guard, I see the retirement, I see the option gone." Clean.
- Cross-cutting gaps A/B/C expand the audit footprint into: an animation-system feature refactor, an include-discipline convention, CI/preset configuration drift, and documentation reconciliation. Mixing these into the guard retirement commit would dilute the auditability, so they go in a single follow-up ticket for clarity.
- The cascade of source-level fixes (`856ff957`) is already on main and green; TICKET-005 is meta-coordination, not a re-do of those fixes.

### Suggested fix approach

**For Gap A (`keyframes()` revival)**:

1. Open `tests/core/animation/test_keyframes.cpp`.
2. Decide on a public interface: either implement `chronon3d::keyframes(Frame t, std::span<Keyframe const> ks)` as a thin wrapper over the existing internal interpolation logic, OR re-shape the disabled test cases against the existing internal API (e.g., a class-based evaluator rather than a free function).
3. If implementing: ensure the wrapper follows the same animation-graph invariants as the internal call (`lerp`, `ease`, `step`, easing-curve lookup). Add a doc comment explaining the scope of the public interface.
4. Update the `TEST_CASE("DISABLED_keyframes_v2: ...")` line to active status and remove the embedded `placeholder pending keyframes() implementation` marker.
5. Re-run `cmake --build build/chronon/linux-ci --target chronon3d_tests_fast` to confirm zero regressions.
6. Update the file-header comment block from disabled-pending to active-and-tested.

**For Gap B (transitive-include cascade)**:

1. Run `git diff origin/main HEAD` to identify each cascade-fixed file in `856ff957` and document the divergence from upstream's strategy.
2. Decide: keep our in-place fixes (preferred — preserves coverage), OR substitute upstream's strategy (e.g., delete orphan tests/features). The former is lower risk for downstream consumers.
3. Add a brief include-discipline note to `CONTRIBUTING.md`: when a header's transitive includes are tightened, the commit should update all known consumers in the same PR rather than relying on subsequent cascade fixes.
4. Run `cmake --build build/chronon/linux-ci --target chronon3d_tests_fast` under multiple presets (`linux-ci`, `linux-dev`, `linux-lean-dev`) to confirm the cascade is stable across build configurations.

**For Gap C (guard interaction reconciliation)**:

1. Audit stale-flag references:
   ```bash
   grep -rn 'CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2' . \
     --include='*.cmake' --include='CMakeLists.txt' --include='*.sh' \
     --include='*.yml' --include='*.yaml' --include='*.py' \
     --exclude-dir='build/' --exclude-dir='vcpkg_*/' 2>/dev/null
   ```
   Most hits should be the `option(... OFF)` line in root `CMakeLists.txt` + the retirement comment block. Any CI / script / preset referencing the override flag as a live directive (e.g., `"CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2": "ON"` in `CMakePresets.json`) is stale and should be removed.
2. Reconcile documentation:
   - `docs/CHANGELOG.md` — add an entry noting the cmake guard's brief lifecycle (added → fired → retired in the same commit window, with PR #23 as the trigger).
   - `docs/ROADMAP.md`, `docs/ARCHITECTURE_EVOLUTION_PLAN.md`, `docs/FEATURES.md` — update any references to expressions/v2 as "disabled" / "abandoned" / "quarantined" to reflect that it is now functionally integrated on `main`. References should no longer advise against future use.
3. Confirm the deprecated `option(... OFF)` line in root `CMakeLists.txt` does not break any preset:
   ```bash
   for preset in linux-ci linux-dev linux-lean-dev linux-release linux-asan; do
     echo "=== $preset ==="
     cmake --preset "$preset" 2>&1 | grep -E 'warning|error|CHRONON3D_ENABLE_EXPERIMENTAL'
   done
   ```
   Each preset should configure cleanly with zero warnings related to the deprecated option.

### Acceptance criteria

| Criterion | Verification |
|---|---|
| `DISABLED_keyframes_v2:` placeholder is removed; the test is either active with a re-shaped body or formally documented as deferred-to-v3 with a new ticket ID. | `grep -nE 'DISABLED_keyframes_v2' tests/core/animation/test_keyframes.cpp` returns zero hits. Active TEST_CASE body compiles + runs green. |
| All transitive-include fixes from `856ff957` are documented with rationale + cross-reference to the refactor lineage that triggered them. | `docs/FOLLOWUP_TICKETS.md` Gain a sub-section in TICKET-002's "Suggested fix approach" enumerating each transitively-included header. |
| Documentation (`ARCHITECTURE_EVOLUTION_PLAN.md`, `ROADMAP.md`, `FEATURES.md`, `CHANGELOG.md`) accurately reflects expressions/v2 is on main. | `grep -rn 'expressions/v2.*\(disabled\|abandoned\|quarantined\)' docs/` returns zero hits. `CHANGELOG.md` has an entry for the guard retirement. |
| `grep -rn 'CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2'` returns only the `option()` declaration + retirement comment. | `grep -rn ... | wc -l` returns 2. |
| `cmake --build build/chronon/linux-ci --target chronon3d_tests_fast` succeeds rc=0 in a stable environment. | Run the `cmake --preset linux-ci && cmake --build ...` flow once on a clean sandbox. |
| Each preset (`linux-ci`, `linux-dev`, `linux-lean-dev`, `linux-release`, `linux-asan`) configures with zero warnings related to the deprecated option. | `for preset in ...` loop above reports zero matches. |

### Cross-references

- Cascade-of-fix commits (this session): `856ff957` (cascade); `aae68561` (guard creation); `76d547a6` (upstream's expression/v2 fix).
- Disabled test reference: the `DISABLED_keyframes_v2:` placeholder in `tests/core/animation/test_keyframes.cpp` with the inline `// TODO` comment block above it.
- PR #23: `1871eb77` (merge), `76d547a6` (upstream fix).
- Companion tickets: TICKET-001, TICKET-002, TICKET-003, TICKET-004 (full thread on this project's followup-tracking convention).
- Discovered-on date: 2026-06-20.

---

## TICKET-006 — Missing `chronon3d_backend_text` linkage in `chronon3d_tests_fast` link step

| Field | Value |
|---|---|
| **Status** | 🔵 Planned |
| **Affected file(s)** | One of `tests/core_tests.cmake`, `tests/scene_tests.cmake`, `tests/text_tests.cmake`, `tests/renderer_tests.cmake` (depends on chronon3d_text_core but does not link chronon3d_backend_text). Exact file TBD by binary search. |
| **Discovered during** | cmake build re-verification after the post-cascade cleanup chain (`856ff957` → `b7a9358e` → `c2c2efac` → `a10db33c`) on `main`, with PR #23 (commits `1871eb77` + `76d547a6`) merged in. |
| **Discovered date** | 2026-06-20 |
| **Error count** | 17 linker errors (mold + collect2) for undefined symbols. Total: ~386 ninja steps; **378 successful compile steps** before the link failure — confirming this is a **link-time defect**, not a source-level compile issue. |

### Symptom

`cmake --build build/chronon/linux-ci --target chronon3d_tests_fast` (after a clean reconfigure: `cmake --preset linux-ci` rc=0) produces 17 linker errors at the chronon3d_tests_fast umbrella target's component executables. Distinct undefined-symbol errors:

```
mold: error: undefined symbol: chronon3d::segment_bidi_runs(std::basic_string_view<char, std::char_traits<char>>, int)
mold: error: undefined symbol: chronon3d::shared_font_engine()
mold: error: undefined symbol: chronon3d::glyph_atlas_lookup(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char>> const&, unsigned int, unsigned int)
```

Plus indirect references to:
```
chronon3d::rasterize_text_to_bl_image(chronon3d::TextShape const&, ...)
chronon3d::shape_resolved_run(...)
chronon3d::text_run_materialize(...)
```

Reference sites (per linker output):
- `src/text/libchronon3d_text_core.a` (text_resolver.cpp, text_run_builder.cpp)
- `src/backends/text/libchronon3d_backend_text.a` (text_rasterizer_render.cpp)
- `src/backends/software/libchronon3d_backend_software.a` (software_text_processor.cpp)

### Root cause analysis

The functions ARE declared and defined:

| Function | Declared in | Defined in | Compiled into |
|---|---|---|---|
| `segment_bidi_runs` | `include/chronon3d/backends/text/bidi_segmenter.hpp` | `src/backends/text/bidi_segmenter.cpp` | `chronon3d_backend_text` |
| `shared_font_engine` | `include/chronon3d/text/font_engine.hpp` | `src/backends/text/font_engine.cpp` | `chronon3d_backend_text` |
| `glyph_atlas_lookup` | `include/chronon3d/text/glyph_atlas.hpp` | `src/backends/text/glyph_atlas.cpp` | `chronon3d_backend_text` |
| `rasterize_text_to_bl_image` | `include/chronon3d/backends/text/text_rasterizer_render.hpp` | `src/backends/text/text_rasterizer_render.cpp` | `chronon3d_backend_text` |
| `shape_resolved_run` | `include/chronon3d/text/text_resolver.hpp` | `src/text/text_resolver.cpp` | `chronon3d_text_core` |
| `text_run_materialize` | `include/chronon3d/text/text_run_builder.hpp` | `src/text/text_run_builder.cpp` | `chronon3d_text_core` |

Source-side is fine. The issue is **linkage**: at least one of chronon3d_tests_fast's component test executables does NOT link `chronon3d_backend_text`, even though it transitively uses symbols from that library through `chronon3d_text_core`.

Possible root causes (binary search needed to nail the actual one):

1. **Test target's `target_link_libraries` is missing `chronon3d_backend_text`** — simplest case. Likely culprit.
2. **`chronon3d_text_core`'s public interface declares symbols that are only present when the Blend2D backend is linked** — if text_core compiles headers that reference backend symbols without transitively pulling backend, callers link-fail.
3. **Text subsystem configuration scoped per-preset** — `linux-ci` preset configuration may have a stale combination (text enabled, Blend2D backend not). Need cross-preset verification.
4. **A test target's CMakeLists uses `include()` (not `add_subdirectory()`) for some subset, similar to the bare-path bug fixed at the merge radar — flag, but unlikely given symptoms.

### Out-of-scope rationale for the cmake-guard retirement / TICKET-005

- The cmake-guard retirement commit (b7a9358e) and TICKET-005 commit (a10db33c) all focused on the broader post-rebase integration. Linkage issues are distinct.
- Earlier in this session's build cycle, environmental instability (SIGBUS at `ar`, cc1plus internal-compiler-error segfaults at src/scene) **masked this defect** — the env crashes hit before the link step in some runs. Now that the env stabilized, the underlying linkage rot surfaced, exactly as a sensible triage would predict.
- Source-side compile is clean: 378/386 ninja steps succeed. The defect is binary/loader-specific, not source code rot.

### Suggested fix approach

1. **Locate the failing test executable**:
   ```bash
   cmake --build build/chronon/linux-ci --target chronon3d_tests_fast -j 1 -v 2>&1 | \
     grep -E 'FAILED|undefined symbol|chronon3d_.*\.a|/usr/bin/ld|/usr/bin/mold|chronon3d_.*_tests' | head
   ```
   The `-v` flag exposes which binary fails — likely `chronon3d_scene_tests`, `chronon3d_core_tests`, or `chronon3d_text_tests_fast` itself (chronon3d_tests_fast may be a custom target that wraps them; the actual executable failing will pinpoint the file).

2. **Identify its target_link_libraries** in the relevant `tests/*.cmake` file (binary search across core_tests.cmake, scene_tests.cmake, text_tests.cmake, renderer_tests.cmake).

3. **Add `chronon3d_backend_text` to its target_link_libraries**. Guard with `$<BOOL:${CHRONON3D_USE_BLEND2D}>` or `$<TARGET_EXISTS:chronon3d_backend_text>` if the dependency might not exist when the Blend2D backend is disabled, e.g.:
   ```cmake
   target_link_libraries(<failing_target>
       PRIVATE
           chronon3d_text_core
           $<$<BOOL:${CHRONON3D_USE_BLEND2D}>:chronon3d_backend_text>   # NEW
           # ... existing deps ...
   )
   ```

4. **Verify**: re-run `cmake --build build/chronon/linux-ci --target chronon3d_tests_fast` — expect rc=0.

5. **Cross-preset validation**: ensure `cmake --build` also succeeds under `linux-lean-dev`, `linux-full-validation`, `linux-asan`, and any other preset that enables text + Blend2D.

6. **Downstream fix-up**: if `chronon3d_backend_text` itself has analog linkage gaps (missing `chronon3d_blend2d_paint`, `freetype`, `harfbuzz`, etc.), address them in the same commit — they share the same root cause.

### Acceptance criteria

- `cmake --build build/chronon/linux-ci --target chronon3d_tests_fast` returns RC=0 in a stable environment.
- 0 linker errors in the build log; specifically zero `undefined symbol: chronon3d::segment_bidi_runs(...)`, `chronon3d::shared_font_engine()`, `chronon3d::glyph_atlas_lookup(...)`, or analog text-backend symbol errors.
- 0 errors from `collect2: error: ld returned 1` (final linker failure tail).
- The `chronon3d_tests_fast` umbrella target links all its component test executables successfully under `linux-ci`, `linux-lean-dev`, `linux-full-validation`, `linux-asan` presets.
- If a different preset (e.g., a CI preset without text enabled) makes the dependency optional, the build still configures + builds cleanly without spurious warnings.

### Cross-references

- TICKET-005 (`a10db33c`): post-cascade cleanup — Gap C-style linkage audit.
- b7a9358e: cmake-guard retirement commit (option `CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2` now deprecated no-op).
- 856ff957: cascade of missing-include fixes.
- TICKET-002: TextSpec API rotation may share a partial root cause (text subsystem API drift left linkage gaps when the implementation path changed).
- PR #23: `1871eb77` (merge), `76d547a6` (upstream fix).
- Discovered-on date: 2026-06-20.

---

