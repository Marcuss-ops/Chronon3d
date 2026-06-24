# TXT‑00 Baseline: Structural Build/Link Green

**Commit**: `main@ccabb574`
**Date**: 2026-06-24
**Trigger**: Agent 2 CMake/SDK merge — centralize registry + explicit preset features + SDK archive aggregation fix.

## Validation Results

| Step | rc | Verdict |
|---|---|---|
| `cmake --preset linux-ci` (configure) | 0 | ✅ GREEN |
| `cmake --build --preset linux-ci --target chronon3d_text_preset_visual_tests --parallel 8` | 0 | ✅ GREEN |
| `ctest --test-dir build/chronon/linux-ci -R '^VRTextPresetVisual$' --output-on-failure -V` | 8 | ❌ RED (environment) |

### Build/Link: GREEN

The structural linker rot (~30 unresolved symbols across 7 iterations) is **completely resolved**.
The CMake registry centralization from Agent 2 fixed the root cause: the SDK archive aggregation
now includes all registry OBJECTs, eliminating the symbol-drop and link-order problems that
plagued the F‑A through F‑D attempts.

The `--whole-archive` workaround proposed in F‑D is **no longer needed** — the fix was structural,
not a linker-flag band-aid.

### Test: RED (environment, not code)

The test executable compiles and links, but fails at runtime with:

```
materialize_text_run_shape: no FontEngine available — text_run ... will render blank.
CHECK( ink_pixels > 0 ) is NOT correct!
```

This is an **environment dependency** (FontEngine not initialized in bare ctest context),
not a code defect. The test correctly detects that rendered text produces zero pixels
when no FontEngine is available.

**Next step for TXT‑00 closure**: ensure FontEngine is available in the test environment,
or document as a runtime dependency for visual text tests.

### Git Status

- `git status -sb`: clean ✅
- Branch: `main`

## Cross-references

- `docs/baselines/tcb-txt-00-f-c-blocked-4ab8cbb8.md` — previous blocked state (build rc=1, ~30 unresolved)
- `docs/baselines/codex-txt-00-attempt1-blocked-375bd5b9.md` — first attempt (linker rot out of scope)
- `docs/baselines/main-446a60e2-baseline.md` — prior baseline (TICKET-039 settings() regression)
- `docs/agent-tasks/AGENT_2_CMAKE_SDK_BASELINE.md` — Agent 2 scope, owner of the fix
- `tests/text_preset_visual_tests.cmake` — updated status reflecting this baseline

## Recorded Commands

```bash
git fetch origin && git checkout main && git pull --ff-only origin main
cmake --preset linux-ci
cmake --build --preset linux-ci --target chronon3d_text_preset_visual_tests --parallel 8
ctest --test-dir build/chronon/linux-ci -R '^VRTextPresetVisual$' --output-on-failure -V
git status -sb
```

## Verdict

**TXT‑00 build/link**: GREEN — structural rot resolved by Agent 2 CMake registry fix.
**TXT‑00 test execution**: BLOCKED by FontEngine environment dependency.
**TXT‑00 overall**: NOT CLOSED — requires FontEngine runtime availability for full green.
