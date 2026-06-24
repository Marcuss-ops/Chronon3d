# Blocco 1 — Render-Aggregator Dependency Wiring Repair

**Commit:** `main@25c6b5cd`
**Date:** 2026-06-24
**Scope:** `tests/CMakeLists.txt` — single file diff
**MR scope:** Blocco 1 step 1 + 2 + 3 of the audit recommended order
(TXT-00 closure is a separate PR — Blocco 2).

## Symptom (verbatim from audit verdict §1)

In `tests/CMakeLists.txt`, the target `chronon3d_text_preset_visual_tests`
was appended into `CHRONON3D_RENDER_TEST_DEPS` at line ~99, then the same
variable was reset with `set(CHRONON3D_RENDER_TEST_DEPS "")` at line ~164
and rebuilt by other `if(TARGET …) list(APPEND …)` blocks. The early append
was wiped out, and the subsequent RENDER_TEST_DEPS rebuild did not include
the visual executable — so it was **not** a transitive build dependency of
`chronon3d_tests_render` (and therefore not of `chronon3d_tests`) even though
the comment said so. CI could try to execute `VRTextPresetVisual` without
the executable ever having been built by `cmake --build … --target
chronon3d_tests`.

## Diff (single-file, minimal)

`tests/CMakeLists.txt`:

1. Removed the premature `if(TARGET …) list(APPEND …)` block immediately
   after `include(${CMAKE_CURRENT_SOURCE_DIR}/text_preset_visual_tests.cmake)`.
   The orchestrator should never own a copy of the aggregator append here:
   the visual executable is gated by the outer `if(CHRONON3D_USE_BLEND2D AND
   CHRONON3D_ENABLE_TEXT)` wrap, and the aggregator append lives **below** in
   the render-section, after the `set(... "")` reset.

2. Added a single canonical `if(TARGET …) list(APPEND …)` block right
   before `add_custom_target(chronon3d_tests_render …)`, grouped with the
   other ~10 render-test appends:

   ```cmake
   # PR-A4: visual executable created by the outer `if(CHRONON3D_USE_BLEND2D AND
   # CHRONON3D_ENABLE_TEXT)` wrap above. Single aggregator append — do not
   # reintroduce duplicate appends before the `set(... "")` reset below.
   if(TARGET chronon3d_text_preset_visual_tests)
       list(APPEND CHRONON3D_RENDER_TEST_DEPS chronon3d_text_preset_visual_tests)
   endif()
   ```

The two comment blocks are aligned to the relevant lookup points: the
include site tells you *why* the executable exists at all, the append site
tells the future maintainer *why this is the only correct place to touch*.

## Validation Results (this PR)

All verification was reproduced from a fresh configure-only pass on
`build/chronon/linux-ci-block1/` with `cmake --preset linux-ci -B …`
(Ninja generator, `${sourceDir}/vcpkg_installed/linux-ci` already populated
by prior builds).

| Step | rc | Verdict |
|---|---|---|
| `cmake --preset linux-ci -B build/chronon/linux-ci-block1` configure | 0 | ✅ GREEN |
| `grep 'VRTextPresetVisual' build/.../CTestTestfile.cmake` | match L53 | ✅ CTest registered |
| `grep 'chronon3d_text_preset_visual_tests' build.ninja` | match (build rule at L~11405) | ✅ Target exists |
| `build.ninja` phony `chronon3d_tests_render:` → lists `tests/chronon3d_text_preset_visual_tests` | match | ✅ Render aggregator depends on visual target |
| `build.ninja` phony `chronon3d_tests:` → lists `chronon3d_tests_render` and `tests/chronon3d_text_preset_visual_tests` | match | ✅ All-tests aggregator depends on render + visual |

The two `build.ninja` phony edges are the actual proof that the bug is fixed:

```
11787: build tests/chronon3d_tests_render: phony ...
       ... tests/chronon3d_text_preset_visual_tests ...
11799: build tests/chronon3d_tests: phony ...
       chronon3d_tests_fast chronon3d_tests_render tests/chronon3d_text_preset_visual_tests
       chronon3d_tests_video ${CHRONON3D_BENCHMARK_DEP}
```

After this change, `cmake --build --preset linux-ci --target chronon3d_tests`
and `--target chronon3d_tests_render` will deterministically build
`tests/chronon3d_text_preset_visual_tests` as part of the dependency graph.

## Validation Results (explicitly DEFERRED to Blocco 2)

| Step | rc | Verdict |
|---|---|---|
| `cmake --build --preset linux-ci --target chronon3d_tests --parallel 8` | — | DEFERRED |
| `ctest --preset linux-ci-test -R '^VRTextPresetVisual$' --output-on-failure -V` | — | DEFERRED |

The full-build + CTest pass inspection is the **next** item in the audit
recommended order (Blocco 2: "chiudere TXT-00"). It belongs to a separate
PR because:

- it necessarily shows the FontEngine runtime problem (audit §2): the test
  executable rc=0 but CTest rc=8 — a runtime environment dependency, not a
  CMake defect;
- it may also surface frame-classification issues in the
  `expected_visible` semantics (audit §4) that warrant their own PR.

Locking those down before declaring TXT-00 closed is what Blocco 2 is for.

## Architectural compliance

| Constraint | Where | Status |
|---|---|---|
| ADR-018 — orchestrator-only test deps wiring | `tests/CMakeLists.txt` (this diff) | ✅ |
| ADR-019 — single canonical linker surface, no per-test OBJECT props | unchanged | ✅ |
| Anti-duplication — orchestrator owns aggregator list | `tests/CMakeLists.txt:CHRONON3D_RENDER_TEST_DEPS` | ✅ |
| AGENTS.md — no gate weakening, no doc green without observed rc | this baseline doc itself | ✅ |

No gate was relaxed. No error was silenced. The diff is two recipe edits in
one file.

## Git Status

- `git status -sb`: ` M tests/CMakeLists.txt` (single file modified).
- Branch: `main` (up to date with `origin/main` @ `25c6b5cd`).
- Working tree clean apart from this one file.

## Recorded Commands

```bash
git fetch origin && git checkout main && git pull --ff-only origin main
rm -rf build/chronon/linux-ci-block1
cmake --preset linux-ci -B build/chronon/linux-ci-block1
grep 'VRTextPresetVisual' \
    build/chronon/linux-ci-block1/CTestTestfile.cmake
grep -n 'chronon3d_text_preset_visual_tests' \
    build/chronon/linux-ci-block1/build.ninja
git status -sb
```

## Cross-references

- Audit verdict (the user-shared context for this PR) — "Verdetto aggiornato" §1.
- `docs/baselines/main-ccabb574-txt-00-build-green.md` — prior baseline; the
  wiring bug surfaced here was NOT yet diagnosed at that baseline.
- `docs/adr/ADR-018-link-rot-text-visual.md` — orchestrator-source-of-truth rule.
- `docs/adr/ADR-019-test-bin-object-propagation.md` — single canonical SDK
  linker surface.
- `tests/text_preset_visual_tests.cmake` — the visual executable registration
  (out of scope for this PR; the stale "~510 LOC" comment is a doc-sync item
  flagged by the audit and deliberately left untouched here per AGENTS.md).

## Verdict

Blocco 1 step 1+2+3 — **WIRING VERIFIED** on `main@25c6b5cd`.

Closing step 4 (full-build + ctest) and step 5 (new baseline) is deferred to
the **next** PR (Blocco 2 / TXT-00 closure) where the runtime evidence is
captured together with FontEngine handling and frame-classification fixes.
