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
- Foundational PR1 refactor: `785739ed` (camera-transition registry → catalog; predates render-backend rot but shares the same chrono3d scope)
- Documented in commit message:
  - See commit `709a8998` ("fix(camera): forward-declare CameraTransitionCatalog in shot_timeline.hpp") — its chore-tail paragraph already references this ticket.

---
