# ADR-023 — Custom CMake Test Discovery Timeout Recovery

## Status

Proposed (will promote to Accepted after first green run on the working
build host per `docs/cert_sequence_wbh_protocol.md` Step 2 macchina-verifica
+ the ctest `--show-only` audit command in §Compliance & Verification).

## Date

2026-07-12

## Deciders

Agent3 (TICKET-DOCTEST-DISCOVER-TESTS-TIMEOUT-RECOVERY forward-point,
surfaced during the doctest_discover_tests rot-fix chore commit `88ad0d9`,
this session).

## Tags

cmake, test-infrastructure, timeout, doctest, ci-flake, gtest_discover_tests,
TICKET-DOCTEST-DISCOVER-TESTS-ROT-FIX, GATE-MNT-01.

## Context

### The rot-fix that created the timeout gap

The TICKET-DOCTEST-DISCOVER-TESTS-ROT-FIX chore commit (`88ad0d9`,
2026-07-12) REMOVED 2 redundant `doctest_discover_tests(...)` blocks at:

- `tests/pipeline_parity_tests.cmake:43-47`
- `tests/pipeline_parity_real_tests.cmake:27-31`

The rot-fix was necessary because the `doctest_discover_tests` blocks were
STALE remnants from before the ADR-018 canonical `chronon3d_add_test_suite()`
migration; the canonical helper already handles WORKING_DIRECTORY + ctest
registration per `cmake/Chronon3DTestSuite.cmake:146` (the helper emits
`add_test(NAME <test_name> COMMAND <test_binary> WORKING_DIRECTORY <dir>)`
at line 194).

### The timeout loss

The removed `doctest_discover_tests(...)` blocks had a per-TEST_CASE TIMEOUT
clause:

```cmake
doctest_discover_tests(chronon3d_pipeline_parity_tests
    PROPERTIES TIMEOUT 30)  # tests/pipeline_parity_tests.cmake:43
doctest_discover_tests(chronon3d_pipeline_parity_real_tests
    PROPERTIES TIMEOUT 120)  # tests/pipeline_parity_real_tests.cmake:27
```

The TIMEOUT 30/120 was per-TEST_CASE, NOT per-target. The canonical
`chronon3d_add_test_suite` helper at `cmake/Chronon3DTestSuite.cmake:194`
uses a single cumulative `add_test` wrapper:

```cmake
add_test(NAME ${TEST_SUITE_NAME} COMMAND ${TEST_SUITE_TARGET} WORKING_DIRECTORY ${WORKING_DIR})
```

Applying TIMEOUT 30/120 at the target level via `set_tests_properties(... PROPERTIES TIMEOUT)`
would apply to the ENTIRE executable cumulative runtime, not per-TEST_CASE.
The 7-pipeline × 18-CHECK matrix in `tests/text/test_pipeline_parity.cpp` and
the video-completeness matrix in `tests/text/test_pipeline_parity_real.cpp`
would risk CI flake under the cumulative timeout (a slow test case would
exhaust the 30s/120s budget for the entire test suite).

### Current state (post-rot-fix)

The TIMEOUT 30/120 was REMOVED along with the redundant blocks. The
canonical `add_test` wrapper does NOT emit a per-TEST_CASE TIMEOUT. The
current ctest invocation runs under the LOOSE 1500s ctest default (the
standard ctest TIMEOUT when no `set_tests_properties(... TIMEOUT)` is
emitted). The user-spec 30/120s per-TEST_CASE bound is SILENTLY LOST.

### CI flake risk under the 1500s default

A single hung test case (e.g., a deadlock in a `std::variant` move-ctor
cascade per TICKET-CONTENT-TEXT-CAMERA-V1-ROT-VARIANT-MOVE, or a 60-frame
CI scan per TICKET-VIDEO-COMPLETENESS-MATRIX §5) could exhaust the 1500s
cumulative budget, blocking CI for 25 minutes per hung test suite. The
30/120s per-TEST_CASE timeout was a fast-fail safety net; the loss
re-introduces the CI deadlock risk.

### Why this ADR is needed

AGENTS.md §"Regole permanenti" requires ADR for any new test-infrastructure
mechanism that affects CI flake surface:

> **No nuovi singleton/registry/resolver/cache senza ADR.**

The 3 recovery options are:

- **(a) `gtest_discover_tests`-equivalent per-TEST_CASE discovery**
  (canonical library import) — adds an external dependency (gtest
  discover mechanism adapted to doctest) + heavy configure cost.
- **(b) Custom CMake function that emits per-TEST_CASE TIMEOUT** (zero
  new dependencies) — a TU-local CMake helper that parses the test
  source for `TEST_CASE` declarations and emits explicit
  `set_tests_properties(... PROPERTIES TIMEOUT)` per test.
- **(c) Preserve loose 1500s ctest default** (relax the bound, no
  source change) — accepts the CI deadlock risk; no new test
  infrastructure.

The decision is non-trivial because:

- Option (a) adds a dependency + slow configure.
- Option (b) requires a custom CMake function with a regex parser
  (brittleness risk) + a new file in `cmake/`.
- Option (c) accepts the CI deadlock risk; the user-spec 30/120s bound
  is lost.

## Decision

Adopt **custom CMake function (option b)** as the canonical disposition
for TICKET-DOCTEST-DISCOVER-TESTS-TIMEOUT-RECOVERY:

1. **Custom CMake function `chronon3d_add_test_suite_with_timeout`** added
   to `cmake/Chronon3DTestSuite.cmake` (parallel to the existing
   `chronon3d_add_test_suite` at line 146). The new function accepts a
   `PER_TEST_TIMEOUT` parameter (default 30s, env-overrideable via
   `CHRONON3D_PER_TEST_TIMEOUT`); emits the existing
   `add_test(NAME ... COMMAND ... WORKING_DIRECTORY ...)` wrapper at
   line 194; THEN iterates over the test source file(s) and emits
   `set_tests_properties(<test_name> PROPERTIES TIMEOUT <per_test_timeout>)`
   for each `TEST_CASE` declaration parsed from the source.

2. **Per-TEST_CASE TIMEOUT recovery via source parse** (NOT a CMake
   registry): the new function uses CMake `file(STRINGS)` + `string(REGEX
   MATCHALL)` to parse the test source for `TEST_CASE("name")` patterns
   and emit one `set_tests_properties` per match. The source parse is
   BEST-EFFORT: the regex matches the canonical doctest `TEST_CASE`
   pattern; malformed test names are silently skipped (logged via
   `message(WARNING ...)` to the CMake configure output).

3. **Backward-compatible with the existing `chronon3d_add_test_suite`**:
   the existing helper at line 146 is UNCHANGED. The new function is
   an OPT-IN extension: test files that want per-TEST_CASE timeout
   recovery explicitly invoke `chronon3d_add_test_suite_with_timeout`
   instead of `chronon3d_add_test_suite`. The 2 affected files
   (`tests/pipeline_parity_tests.cmake` + `tests/pipeline_parity_real_
   tests.cmake`) are updated to use the new helper in a future chore
   commit (not in this ADR commit).

4. **Test source files UNCHANGED**: the test source files
   (`tests/text/test_pipeline_parity.cpp` + `tests/text/test_pipeline_
   parity_real.cpp`) are UNCHANGED. The new helper operates on the
   CMake configuration layer, not the test source layer. The test
   source `TEST_CASE` declarations are the source of truth; the
   helper parses them at configure time.

5. **CI flake surface RECOVERED**: with the new helper, the user-spec
   30s per-TEST_CASE timeout for `tests/text/test_pipeline_parity.cpp`
   (7-pipeline × 18-CHECK matrix) + 120s per-TEST_CASE timeout for
   `tests/text/test_pipeline_parity_real.cpp` (video-completeness
   matrix) are RECOVERED. Hung test cases fast-fail at 30s/120s,
   preventing the 25-minute CI deadlock.

6. **WBH macchina-verifica is the gating event** for the Proposed →
   Accepted transition: per `docs/cert_sequence_wbh_protocol.md`
   Step 4 macchina-verifica, the WBH session runs
   `ctest --test-dir build/chronon/linux-content-dev -R
   'TextVisibleInk|TextNoClip|TextClipBounds|TextCompleteness|TextAlign|
   TextWrapping|TextUnicode|TextGoldenGaps|TextStyleProps|TextTypewriter|
   TextDeterminism|TextLongText|TextEdgeCases|TextAntiFalseGreen|
   TextGlowSmoke|TextRotateZ|TextTransforms' --output-on-failure` +
   `ctest --show-only=json-v1 | jq '.tests[].properties[]' | grep TIMEOUT`
   audit command (verifies per-TEST_CASE TIMEOUT is emitted); first
   green run transitions this ADR from Proposed → Accepted.

### Why custom CMake function, not gtest_discover_tests or 1500s default

- **gtest_discover_tests (option a) rejected**: adds a heavy external
  dependency (gtest discover mechanism adapted to doctest) + slow
  configure cost (the gtest discover mechanism walks the test source
  at configure time, adding ~30-60s to the CMake configure step).
  The dependency is also NOT canonical: the project uses doctest
  (per AGENTS.md "no stime percentuali: usare stato osservabile"
  + the canonical test framework is doctest per ADR-018 migration).
  Adding a gtest discover mechanism is a Cat-3 surface violation
  (introduces a new external dependency in the test infrastructure).
- **Custom CMake function (option b, chosen)**: zero new dependencies
  (the helper is a TU-local CMake function in
  `cmake/Chronon3DTestSuite.cmake`) + zero new public SDK API
  surface (Cat-3 SATISFIED) + restores the 30/120s per-TEST_CASE
  fast-fail safety net + is backward-compatible with the existing
  `chronon3d_add_test_suite` helper.
- **Preserve loose 1500s default (option c) rejected**: accepts the
  CI deadlock risk; the user-spec 30/120s bound is lost; a single
  hung test case could block CI for 25 minutes. The CI flake surface
  is re-introduced.

## Consequences

### Positive

- **CI flake surface RECOVERED**: the 30/120s per-TEST_CASE fast-fail
  safety net is restored. Hung test cases fast-fail at 30s/120s,
  preventing the 25-minute CI deadlock.
- **Zero new dependencies**: the helper is a TU-local CMake function
  in `cmake/Chronon3DTestSuite.cmake`. No external libraries, no
  new CMake packages, no new test framework.
- **Zero new public SDK API surface** (Cat-3 SATISFIED): the helper
  is a build-system extension; zero new symbols in
  `include/chronon3d/`, zero public SDK API additions.
- **Backward-compatible**: the existing `chronon3d_add_test_suite`
  at line 146 is UNCHANGED. The new function is OPT-IN. Test files
  that don't need per-TEST_CASE timeout recovery continue to use
  the existing helper.
- **§Honest TIMEOUT loss documented + recovery path forward-pointed**:
  the rot-fix at commit `88ad0d9` SILENTLY LOST the 30/120s per-
  TEST_CASE bound; this ADR documents the loss + the recovery
  disposition. The CURRENT_STATUS row + the FOLLOWUP_TICKETS row
  reference this ADR for the recovery path.

### Negative

- **Custom CMake function with regex parser (brittleness risk)**: the
  helper uses `string(REGEX MATCHALL)` to parse `TEST_CASE("name")`
  patterns. The parser is BEST-EFFORT: malformed test names are
  silently skipped. The brittleness risk is mitigated by:
  (a) the parser is TU-local (changes are isolated to
  `cmake/Chronon3DTestSuite.cmake`);
  (b) the parser is backward-compatible (the existing helper is
  unchanged);
  (c) the parser emits `message(WARNING ...)` for malformed patterns
  (logged to the CMake configure output for forensic traceability).
- **Test source files UNCHANGED but the parser must match the
  canonical doctest `TEST_CASE` pattern**: the parser is calibrated
  for the canonical `TEST_CASE("name")` + `TEST_CASE_FIXTURE(...)`
  patterns. Future test source files that use non-canonical patterns
  (e.g., `TEST_SUITE` + `TEST_CASE` without parens) may not be
  parsed correctly. Mitigation: the canonical doctest pattern is
  documented in `cmake/Chronon3DTestSuite.cmake` header comments.
- **WBH macchina-verifica is the gating event**: the Proposed →
  Accepted transition is tied to the WBH first green run + the
  `ctest --show-only` audit command. Until the WBH session runs the
  ctest surface + the audit, this ADR remains Proposed.
- **Forward-point**: the new helper is documented in this ADR but
  the implementation (`chronon3d_add_test_suite_with_timeout` in
  `cmake/Chronon3DTestSuite.cmake`) is a separate atomic chore on
  the WBH. The ADR documents the decision; the implementation is
  forward-pointed.

### Neutral

- **Status is Proposed, not Accepted**: the WBH macchina-verifica is
  the gating event for the Proposed → Accepted transition.
- **The 2 affected `.cmake` files are forward-pointed** for the
  helper invocation update (`tests/pipeline_parity_tests.cmake` +
  `tests/pipeline_parity_real_tests.cmake` switch from
  `chronon3d_add_test_suite` to `chronon3d_add_test_suite_with_timeout`).
  This is a future source-extension chore, not in this ADR commit.

## Alternatives Considered

### A1. `gtest_discover_tests`-equivalent per-TEST_CASE discovery (option a)

Add `find_package(gtest)` + use the gtest discover mechanism (adapted to
doctest via a small wrapper) to walk the test source at configure time +
emit per-TEST_CASE `set_tests_properties` per match.

**Rejected because:**

- Adds a heavy external dependency (gtest discover mechanism adapted
  to doctest). The project uses doctest per the canonical test framework
  decision in ADR-018; adding a gtest discover mechanism is a Cat-3
  surface violation (introduces a new external dependency in the test
  infrastructure).
- Slow configure cost: the gtest discover mechanism walks the test
  source at configure time, adding ~30-60s to the CMake configure step.
  The 11-gate baseline suite is already configure-heavy; adding
  another 30-60s would degrade the developer experience.
- The dependency is NOT canonical: AGENTS.md "no stime percentuali:
  usare stato osservabile" + the canonical test framework is doctest
  per ADR-018 migration. Adding a gtest discover mechanism is a
  scope-creep risk (introduces a parallel test framework discovery
  surface).

### A2. Preserve loose 1500s ctest default (option c)

Accept the CI deadlock risk; do NOT add a per-TEST_CASE TIMEOUT. The
current ctest invocation runs under the 1500s default; the user-spec
30/120s bound is silently lost.

**Rejected because:**

- Accepts the CI deadlock risk: a single hung test case could block
  CI for 25 minutes per hung test suite. The 30/120s per-TEST_CASE
  fast-fail safety net is LOST.
- The user-spec 30/120s bound was a deliberate design decision (per
  the prior `doctest_discover_tests` PROPERTIES TIMEOUT clauses); the
  loss is a §honesty violation per AGENTS.md "non segnare verde una
  suite que restituisce failure" + "non cambiare un gate per
  nascondere un errore".
- The CI flake surface is re-introduced: the 7-pipeline × 18-CHECK
  matrix + the video-completeness matrix are at risk of deadlock
  under the cumulative 1500s budget.

### A3. Per-executable global TIMEOUT (option d)

Apply a single `set_tests_properties(<executable> PROPERTIES TIMEOUT 300)`
at the target level (5 minutes cumulative per executable).

**Rejected because:**

- Too COARSE for the 18-CHECK matrix: a single slow check (e.g., the
  60-frame alpha bbox CSV loop in TICKET-VIDEO-COMPLETENESS-MATRIX §5)
  could exhaust the 5-minute budget for the entire test suite.
- The per-TEST_CASE granularity is lost: a hung single TEST_CASE
  exhausts the budget for the entire executable; other TEST_CASEs in
  the same executable are NOT independently timeout-bounded.
- The 5-minute budget is not aligned with the user-spec 30/120s per-
  TEST_CASE bounds; the user spec is partially honored (5 minutes
  > 120s for the 120s case) but the per-TEST_CASE granularity is
  lost.

### A4. Source extension to TEST_CASE_DECORATOR + tag-based timeout (option e)

Add a `TEST_CASE_DECORATOR(*, "timeout_30")` macro to the test source
that emits a per-TEST_CASE TIMEOUT via a doctest listener at test
registration time.

**Rejected because:**

- Requires changes to the test source files (`tests/text/test_pipeline_
  parity.cpp` + `tests/text/test_pipeline_parity_real.cpp`) — the
  test source is the canonical surface and changes require careful
  review.
- Adds a new doctest listener to the test infrastructure — a new
  surface that requires its own ADR.
- The CMake-level solution (option b) achieves the same goal with
  zero test source changes; the test source remains the canonical
  surface.

## Compliance & Verification

### How to audit

```bash
# 1. Verify the existing chronon3d_add_test_suite helper is UNCHANGED
grep -n 'function(chronon3d_add_test_suite' cmake/Chronon3DTestSuite.cmake
# Expected: 1 match (the existing helper at line 146, unchanged)

# 2. Verify the new helper is added (forward-point, not in this ADR commit)
grep -n 'function(chronon3d_add_test_suite_with_timeout' cmake/Chronon3DTestSuite.cmake
# Expected: 1 match (added in a future WBH chore commit; this ADR documents the decision)

# 3. Verify the 2 affected .cmake files reference the new helper (forward-point)
grep -n 'chronon3d_add_test_suite_with_timeout' tests/pipeline_parity_tests.cmake tests/pipeline_parity_real_tests.cmake
# Expected: 1 match per file (added in a future WBH chore commit)

# 4. Verify the ctest --show-only audit emits per-TEST_CASE TIMEOUT
ctest --test-dir build/chronon/linux-content-dev --show-only=json-v1 | jq '.tests[].properties[]' | grep TIMEOUT
# Expected: per-TEST_CASE TIMEOUT entries for the 21 sub-cases in the ctest surface
# (17 patterns + 4 sub-patterns from TextTransforms)

# 5. WBH macchina-verifica (gating event for Proposed → Accepted transition)
ctest --test-dir build/chronon/linux-content-dev -R 'TextVisibleInk|TextNoClip|TextClipBounds|TextCompleteness|TextAlign|TextWrapping|TextUnicode|TextGoldenGaps|TextStyleProps|TextTypewriter|TextDeterminism|TextLongText|TextEdgeCases|TextAntiFalseGreen|TextGlowSmoke|TextRotateZ|TextTransforms' --output-on-failure
# Expected: 17 patterns / 21 sub-cases PASS with per-TEST_CASE TIMEOUT honored
```

### How to migrate away (future work, not this commit)

```bash
# When the WBH session runs the ctest surface + the ctest --show-only audit:
# 1. WBH macchina-verifica the ctest -R 'Text*' surface (21 sub-cases PASS)
# 2. WBH macchina-verifica the ctest --show-only audit (per-TEST_CASE TIMEOUT emitted)
# 3. First green run transitions this ADR from Proposed → Accepted
# 4. Update this ADR with a "Status: Accepted" + the WBH macchina-verifica SHA
# 5. Update TICKET-DOCTEST-DISCOVER-TESTS-TIMEOUT-RECOVERY row to DONE
```

## References

- `cmake/Chronon3DTestSuite.cmake:146` — the existing `chronon3d_add_test_suite`
  helper (UNCHANGED; new helper is OPT-IN extension)
- `cmake/Chronon3DTestSuite.cmake:194` — the `add_test` wrapper (cumulative
  per-target, not per-TEST_CASE)
- `tests/pipeline_parity_tests.cmake:43-47` — the prior `doctest_discover_tests`
  block with TIMEOUT 30 (REMOVED in TICKET-DOCTEST-DISCOVER-TESTS-ROT-FIX
  commit `88ad0d9`)
- `tests/pipeline_parity_real_tests.cmake:27-31` — the prior
  `doctest_discover_tests` block with TIMEOUT 120 (REMOVED in
  TICKET-DOCTEST-DISCOVER-TESTS-ROT-FIX commit `88ad0d9`)
- `tests/text/test_pipeline_parity.cpp` — the 7-pipeline × 18-CHECK matrix
  (forward-point for per-TEST_CASE TIMEOUT 30s recovery)
- `tests/text/test_pipeline_parity_real.cpp` — the video-completeness matrix
  (forward-point for per-TEST_CASE TIMEOUT 120s recovery)
- `docs/cert_sequence_wbh_protocol.md` Step 4 — the WBH runbook for the
  proposed → accepted transition
- `docs/FOLLOWUP_TICKETS.md` §Open Blockers TICKET-DOCTEST-DISCOVER-TESTS-
  TIMEOUT-RECOVERY row (NEW this ADR) + TICKET-DOCTEST-DISCOVER-TESTS-
  ROT-FIX row in §Recently Closed
- `docs/CURRENT_STATUS.md` §Text V1 Cert Step 10 row (mentions this ADR
  for the per-TEST_CASE TIMEOUT loss) + 11/11 WBH-DEFERRED row
- `docs/CHANGELOG.md` prepended `docs(adr): propose ADR-022 hybrid
  determinism + ADR-023 cmake timeouts` entry (NEW this ADR)
- AGENTS.md §"Regole permanenti" (singleton/registry ADR requirement
  applied by analogy to test-infrastructure decisions) + §Cat-3 (zero new
  public SDK API) + §honesty (TIMEOUT loss disclosed + recovery path
  forward-pointed)
- `docs/adr/ADR-022-determinism-hybrid-spec-variant.md` (sibling ADR in
  the same atomic chore commit)
- `docs/adr/ADR-021-commit-subject-length-policy.md` (MADR format
  reference) + `docs/adr/ADR-020-shared-static-fontengine-singleton.md`
  (MADR format reference with Compliance & Verification + Supersedes
  sections)
- `docs/DOCUMENTATION_GOVERNANCE.md` §docs/adr/ (ADR template + style
  guide: Status, Date, Deciders, Tags, Context, Decision, Consequences,
  Alternatives, Compliance & Verification, References)
