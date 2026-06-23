# TXT‑00 — F‑C linkage rot: attempts blocked (Option C failed)

Branch: DIRECTLY on `main` (per owner directive 2026‑06‑23).
Parent SHA at start: `4ab8cbb8` (the post-AGENTS‑md‑rule‑removal baseline).

This file records the complete empirical history of the F‑C attempt to close
the linker rot of `chronon3d_text_preset_visual_tests`. After 7 iterations
across 4 strategies, **the build is NOT GREEN**. Per AGENTS.md "non segnare
verde una suite che restituisce failure", this is a transparent blocker.

## 1. Iteration history

| Iter | Strategy | Build rc | Unique undef refs |
|---|---|---|---|
| 1 | Baseline: `chronon3d_sdk + chronon3d_software + doctest` | 1 | 96 |
| 2 | + 8 granular OBJECTs (graph/pipeline/backend_software/scene/cache/runtime/text_core/effects) | 1 | sprouted Config, FontEngine, profiling::g_current_counters |
| 3 | + 2 (core_impl, backend_text), syntax `if()` inline error | — | — |
| 3b | + core_impl, backend_text (separate cmake statement) | 1 | sprouted ImageCache, StbImageBackend |
| 4 (D3) | Revert test cmake to canonical + structural fix in `src/CMakeLists.txt` (`target_sources(TARGET_OBJECTS)`→`target_link_libraries(INTERFACE obj)`) | 1 | 30 (96 error lines) |
| 5 (Option C) | Revert D3, add `chronon3d_sdk_impl` (STATIC archive) as FIRST PRIVATE dep | 1 | 30 (SoftwareRenderer, CameraProjectionSource, Config, Layer, LayerBuilder) |

## 2. Remaining undefined symbols (Option C failure, sampled)

```
chronon3d::SoftwareRenderer::update_dirty_telemetry(…)
chronon3d::CameraProjectionSource::CameraProjectionSource(…)
chronon3d::Config::Config()
chronon3d::Layer::~Layer()
chronon3d::LayerBuilder::build()
...and 25+ more (see /tmp/f_c_logs/build_optc.txt)
```

## 3. Root‑cause hypothesis

**H1 — Link‑order:** single‑pass linkers (mold, ld, lld) process static archives
left‑to‑right. `libchronon3d_sdk_impl.a` appears BEFORE `chronon3d_software`
(which pulls in transitive dep‑requirements for e.g. `LayerBuilder`, `Config`
etc. via its INTERFACE deps). When the linker reaches those references,
`libchronon3d_sdk_impl.a` has already been scanned and its objects discarded.

**H4 — whole‑archive needed:** even with correct ordering, the linker may
drop objects from a static archive whose only live references come from
static‑init registration (constructors, vtables) that the control flow
never touches.

**Decision per thinker‑with‑files‑gemini (Option‑C iteration 2026‑06‑23):**
> ITERATE‑ONE‑MORE with `-Wl,--whole-archive` wrapper around
> `chronon3d_sdk_impl` to force all objects in — circumvents both
> link‑order and static‑init drop.

That iteration was NOT executed here (user rule: "Se rc≠0: scrivi blocker
doc trasparente"). The next work package should test this.

## 4. What was attempted on this commit

- `tests/text_preset_visual_tests.cmake`: added `chronon3d_sdk_impl` as
  the FIRST PRIVATE dependency (before `chronon3d_sdk`). Build rc=1.
- `src/CMakeLists.txt`: reverted to HEAD `4ab8cbb8` (prior D3
  structural fix reverted — it was install‑unsafe and out‑of‑scope).
- Blocker doc: this file.

## 5. What this PR DOES NOT DO

- 🛑 Does NOT modify `src/CMakeLists.txt` (scope‑kept on `tests/*.cmake`).
- 🛑 Does NOT add third‑party dep spilling (fmt/spdlog/tbb etc).
- 🛑 Does NOT claim green (build=rc=1, ctest not runnable).
- 🛑 Does NOT delete the test target or remove sentinel gate assertions.

## 6. Next recommended work package: F‑D (single‑PR, 1‑file)

- Branch `codex/f-d-link-rot-fix` off `main` post‑this‑commit.
- `CHANGE`: wrap `chronon3d_sdk_impl` in `-Wl,--whole-archive`:
  ```cmake
  target_link_libraries(chronon3d_text_preset_visual_tests PRIVATE
      "-Wl,--whole-archive" chronon3d_sdk_impl "-Wl,--no-whole-archive"
      chronon3d_sdk
      chronon3d_software
      doctest::doctest
  )
  ```
- Rebuild → `ctest -R '^VRTextPresetVisual$' -V`. If rc=0, write
  `docs/baselines/<sha>-text-baseline.md` positive, commit, close TXT‑00.
- If still rc≠0, blocker doc again, escalate to CMake‑level linkage reset.

## 7. Status

- `cmake --preset linux-ci`: rc=0
- `cmake --build --preset linux-ci --target chronon3d_text_preset_visual_tests --parallel 8`: **rc=1**
- `ctest --test-dir build/chronon/linux-ci -R '^VRTextPresetVisual$' -V`: not executed (binary missing)
- `git status -sb` on this commit: clean (test cmake edit + this blocker doc only)
- TXT‑00: **NOT GREEN**. F‑C is blocked; F‑D is the recommended follow‑up.

## 8. Recorded commands (re‑runnable on F‑D)

```bash
cmake --preset linux-ci
cmake --build --preset linux-ci --target chronon3d_text_preset_visual_tests --parallel 8 \
  2>&1 | tee /tmp/build_fd.txt
ls build/chronon/linux-ci/CMakeFiles/chronon3d_text_preset_visual_tests.dir/link.txt
ctest --test-dir build/chronon/linux-ci -R '^VRTextPresetVisual$' -V \
  2>&1 | tee /tmp/ctest_fd.txt
grep -cE 'TEST CASE: .*PASSED' /tmp/ctest_fd.txt
grep -cE 'VR/Text/.* unset.* first hash to capture' /tmp/ctest_fd.txt
git status -sb
```
