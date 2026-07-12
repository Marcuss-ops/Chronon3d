# TICKET-129-text-clip-policy-cmake-rewrite

## Status

**DONE (2026-07-12, atomic chore commit `a00b9d2f fix(tests): wire chronon3d_text_clip_policy_tests target` on main)**.

## §2 Cleanup Mandate (per TICKET-SIMPLICITY-PARITY-EXTRACT)

Rewrite `tests/text/text_clip_policy_tests.cmake`'s target registration from the stale `chronon3d_pipeline_parity_tests` (referenced the deleted `tests/text/test_pipeline_parity.cpp`) to the new `chronon3d_text_clip_policy_tests` target using the canonical `tests/text/test_text_clip_policy.cpp` source.  Ensure the descriptor on the umbrella label `sanitizer-subsystems` resolves to the new target name (wired at `tests/CMakeLists.txt` via the conditional `CHRONON3D_SANITIZER_SUBSYSTEMS_DEPS` membership + `set_tests_properties(... LABELS "${_ss_existing_labels};sanitizer-subsystems")` per-target label append).

## Closure Lineage

- `TICKET-TEXT-HEADER-CMAKE-REWIRE-ROT` (`docs/FOLLOWUP_TICKETS.md` rot-fix chore batch row on main consolidating this closure)
- `TICKET-SIMPLICITY-PARITY-EXTRACT` (the mandate root: the test file extraction `tests/text/test_pipeline_parity.cpp → tests/text/test_text_clip_policy.cpp`)
- `TICKET-TEXT-INSPECTION-ALPHA-BBOX-VISIBILITY` (inserted the `text_visibility_audit` chain that the new test exercises via the `audit_text_visibility()` helper)

## Verification (post-fix state, machine-verifiable)

- `grep -c 'chronon3d_pipeline_parity_tests' tests/text/text_clip_policy_tests.cmake` returns **0** (stale reference fully removed)
- `grep -c 'chronon3d_text_clip_policy_tests' tests/text/text_clip_policy_tests.cmake` returns **≥1** (new target referenced; matches `chronon3d_pipeline_parity_tests → chronon3d_text_clip_policy_tests` rename)
- `tests/CMakeLists.txt` `if(TARGET chronon3d_text_clip_policy_tests) list(APPEND CHRONON3D_SANITIZER_SUBSYSTEMS_DEPS chronon3d_text_clip_policy_tests)` confirms `sanitizer-subsystems` umbrella correctly resolves to the new target name (no dangling reference to the deleted `chronon3d_pipeline_parity_tests`)
- `ctest --test-dir build/chronon/<preset> -L sanitizer-subsystems --show-only=json-v1 | jq '.[] | select(.name | contains("chronon3d_text_clip_policy"))'` (per ADR-023 audit gate — note: ADR-023 is currently Proposed 2026-07-12, will promote to Accepted after first green run on WBH per AGENTS.md §honesty) — return expected 1 TEST_CASE batch entry (the 7-pipeline × 5-clip × 18-CHECK matrix via `assert_pipeline_clip_18_checks` macro)

## Cross-link

`docs/FOLLOWUP_TICKETS.md` `TICKET-TEXT-HEADER-CMAKE-REWIRE-ROT` (DONE row) + `docs/CHANGELOG.md` `fix(tests): wire chronon3d_text_clip_policy_tests target` entry + the canonical `tests/text/test_text_clip_policy.cpp` (the 7-pipeline × 5-clip × 18-CHECK matrix binary that the new cmake target compiles).

