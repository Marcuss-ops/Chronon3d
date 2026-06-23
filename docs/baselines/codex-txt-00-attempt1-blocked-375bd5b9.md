# PR TXT‑00 — attempt 1 — *blocked* (linker rot out of scope)

Branch: `codex/txt-00-baseline-compilable`
Main snapshot pinned at the start: `375bd5b962964aaeefbed20760bb441cd4d74ef2`
(via `docs/agent-tasks/TEXT_PRODUCTION_V1_PR_PLAN.md`, `commit 375bd5b…`)

This file is intentionally UNTRACKED. It records the first attempt to
restore the text‑preset visual‑test baseline per PR TXT‑00 of the
`Text Production V1` plan. The attempt is **declaratively NOT green**:
the linker step for `chronon3d_text_preset_visual_tests` fails with a
broad set of `chronon3d::*` undefined references that the plan's
minimal‑closure invariant forbids fixing inside TXT‑00.

## 1. Snapshot of what was closed (each `APPROVED` by code-reviewer-minimax-m3)

### ROT 1 — soft rot: RenderRuntime forward decl
- File: `src/render_graph/pipeline/scene_tile_execution.cpp`
- File: `src/render_graph/pipeline/tile_execution_coordinator.cpp`
- Edit: added `#include <chronon3d/runtime/render_runtime.hpp>` with a
  TICKET‑038 / TXT‑00 comment block on each TU that dereferences
  `sw_renderer->runtime().executor()`. The include placement respects
  the 6‑non‑local‑include budget documented in
  `include/chronon3d/backends/software/software_renderer.hpp` (which
  intentionally keeps `RenderRuntime` as a forward declaration).

### ROT 2 — soft rot: SDK symbols not in TU scope
- File: `tests/text/test_text_preset_visual.cpp`
- File: `tests/deterministic/test_visual_regression_scenarios.cpp`
- Edit (TU 1): explicit `using` declarations for
  `chronon3d::content::text::CenterTextOptions`,
  `chronon3d::registry::TextPreset`,
  `chronon3d::registry::TextPresetRegistry`, and
  `chronon3d::registry::make_default_text_preset_registry`.
- Edit (TU 1 + TU 2): TU‑local POD `struct RectF { float
  x{0.0f}, y{0.0f}, w{0.0f}, h{0.0f}; };`inside the anonymous
  namespace, forward of `struct ScenarioMetrics`. The SDK does NOT
  expose a public canonical `RectF`; the plan forbids cross‑package
  aliasing; per‑TU POD is the no‑alias‑API resolution the user
  approved (option 2: "Define struct interna"). Both TUs use the EXACT
  same layout so the metrics struct stays byte‑equivalent.

### ROT 3 — soft rot: macro self‑reference
- File: `tests/text/test_text_preset_visual.cpp`
- Edit: rename macro‑internal binding `m` → `gate_m` inside
  `VR_TEXT_PRESET_GATE` to dodge the `auto m = m` self‑reference rot
  that surfaces after `emit_preset_gate`'s outer `auto m` is passed
  back as `metrics_expr`. 4 sites inside the macro body updated;
  callers untouched; comment block above the macro explains the
  WHY (no canonical type touched, no API surface affect).

### ROT 4 — NOT landed: linker rot (deferred to F-B / F-C)
- File: `tests/text_preset_visual_tests.cmake` — **NOT in this commit**.
  The in‑working‑tree entry was REVERTED to the `origin/main` state at the
  F‑A packaging step. The reverted baseline surface is
  `chronon3d_sdk + chronon3d_software + doctest::doctest` (the same
  surface that the test target was registered with at `375bd5b…`).
- Diagnostic work (NOT committed): the WIP tried two fix shapes:
  (a) `+ chronon3d_sdk_impl` (alleged static aggregate archive) —
  empirically did NOT resolve the link step;
  (b) replace with the proven‑good in‑tree pattern from
  `tests/deterministic_tests.cmake` (`chronon3d_sdk +
  chronon3d_graph + chronon3d_graph_pipeline +
  chronon3d_backend_software + chronon3d_scene + chronon3d_cache +
  chronon3d_runtime + doctest::doctest`) — STILL did not resolve
  all misses; new visible undefined references surfaced for graphics
  /simd kernels (`chronon3d::simd::clear_framebuffer`), text‑core
  hash helpers (`chronon3d::hash_text_run_shape`,
  `chronon3d::build_text_unit_map`), the color pipeline
  (`chronon3d::ColorPipeline::apply`) and the effect catalog
  (`chronon3d::effects::EffectCatalog::freeze`).
- Decision: per AGENTS.md "non mescolare refactor indipendenti / fare PR
  piccole e mirate", closing this from inside TXT‑00 would expand
  scope. Stop Rule applied: ROT 4 here is **a diagnostic that surfaces
  scope**, not a TXT‑00 closure. Tracking: see §4 follow‑up PRs F‑B
  (diagnostic ADR + in‑tree link‑surface contract) and F‑C (real
  TXT‑00 follow‑up that lands the granular libs that close the
  remaining miss set, regenerates the positive baseline, and finally
  declares TXT‑00 green).

## 2. Empirical evidence that the link step is blocked despite ROT 4

Diagnostic run after the commit‑quality state above (linux‑ci preset,
`cmake --build --preset linux-ci --target
chronon3d_text_preset_visual_tests --parallel 8`):

- `build/chronon/linux-ci/CMakeFiles/chronon3d_text_preset_visual_tests.dir/link.txt`
  is **MISSING** when the link fails. This is a symmetric symptom of
  CMake `.cmake` `include()`‑dependency tracking staleness or a
  reconfiguration that did not regenerate `build.ninja`.
- `find build/chronon/linux-ci -name 'libchronon3d_*.a'` returns ALL
  of the granular archives EXCEPT `libchronon3d_graph.a`:
  - `libchronon3d_sdk_impl.a`: present
  - `libchronon3d_graph_pipeline.a`: present
  - `libchronon3d_backend_software.a`: present
  - `libchronon3d_scene.a`: present
  - `libchronon3d_cache.a`: present
  - `libchronon3d_runtime.a`: present
  - **`libchronon3d_graph.a`: MISSING**

  Because `chronon3d_graph` is listed as an INTERFACE aggregator in
  `cmake/Chronon3DRegistry.cmake::CHRONON3D_REGISTRY_INTERFACE_LIBS`
  rather than as an OBJECT sub‑target, the missing `.a` is consistent
  with the project design — but also means the in‑tree executable
  cannot resolve any symbol whose source lives behind an OBJECT sub‑
  target under `chronon3d_graph_*` and is not already PUBLIC‑linked by
  one of the granular libs explicitly listed.
- New visible undefined references appearing after ROT 4 — sampled:
  - `chronon3d::simd::clear_framebuffer(...)`
  - `chronon3d::hash_text_run_shape(...)`
  - `chronon3d::build_text_unit_map(...)`
  - `chronon3d::effects::EffectCatalog::freeze()`
  - `chronon3d::ColorPipeline::apply(...)`

  These point to OBJECT sub‑targets under `chronon3d_text_core`,
  `chronon3d_effects`, `chronon3d_simd`/graphics kernels and a
  `chronon3d_color_pipeline` slice that's not in the granular list
  above. Each additional lib added to the test target is exactly the
  kind of "mescolare refactor indipendenti / introdurre nuove
  dipendenze" the AGENTS.md rules and plan forbid.

## 3. Why this is *blocked* per Stop Rule (regola di arresto, §7 of the plan)

The plan's discipline invariants for TXT‑00 are:

> *non correggere errori non osservati. Registrare il primo errore
> reale e risolvere soltanto quello e gli eventuali errori a cascata
> direttamente collegati.*

The first observed error was the type‑recognition rot (RECTF,
`CenterTextOptions`, `TextPreset{Registry}`, `make_default_text_preset_registry`).
That was ROT 2 — closed.

Each subsequent rotation revealed a *new category* of rot that was
previously hidden behind the earlier ones:

- ROT 3 (macro self‑reference rot) — direct cascade of the type
  rotation closures.
- ROT 4 (linker rot) — direct cascade of the source‑level rotation
  closures, because nothing in the in‑tree cmake pattern primitively
  tests the text visual baseline.

ROT 4 itself expands further: each new granular lib revealed new
undefined references that point to additional OBJECT targets which
the in‑tree pattern of `tests/deterministic_tests.cmake` simply does
not cover (the deterministic target does not use the text system
deeply enough to surface those symbols).

Per Stop Rule item 1 this is no longer a "blocco diretto e
inseparabile" of TXT‑00. It is a broader link‑surface scoping
exercise that crosses SDK / test / granular‑lib axes and would
require its own work package.

## 4. Recommended follow‑up PR(s)

Three independent follow‑ups are required for the text visual baseline
to become genuinely green; each is small by construction:

### F‑A. Land ROT 1‑3 as an isolated PR (`codex/txt-00-rot-1-3-fixes`)
- Branch: `codex/txt-00-rot-1-3-fixes` (off `375bd5b…`)
- Files touched: only the 4 source‑level files rotated above
  (2 `src/.../pipeline/*.cpp` + 2 `tests/.../*.cpp`)
- Tests: `cmake --build --preset linux-ci --target
  chronon3d_sdk_tests` etc. that don't require the text visual
  binary to keep building. `chronon3d_text_preset_visual_tests` stays
  un‑built on this PR — explicit blocker in the PR description.
- This PR can land safely because none of the changes require the
  test target itself to compile.

### F‑B. Investigate the linker rot (`codex/investigate-link-rot`)
- Branch: `codex/investigate-link-rot` (off `375bd5b…`)
- Files touched: diagnostic only — no source change. Possible
  additions: a tiny `CMakeLists.txt` snippet that prints the
  transitive `$<TARGET_OBJECTS:...>` closures of
  `target_link_libraries(chronon3d_text_preset_visual_tests ...
  ...)` for inspection, plus fixing the apparent staleness of
  `tests/CMakeLists.txt` `include()`‑tracking if that's the missing
  piece (cmake‑side `set_property(DIRECTORY APPEND PROPERTY
  CMAKE_CONFIGURE_DEPENDS ...)` over the included `.cmake` files).
- A separate ADR candidate: `docs/adr/018-link-rot-text-visual.md`
  recording the canonical in‑tree pattern, the OBJECT vs static‑
  archive distinction, and the missing‑graph‑.a observation.

### F‑C. The "real" TXT‑00 follow‑up that finally declares green
- Branch: `codex/txt-00f-completes-link-surface`
- After F‑A and F‑B have landed, this PR rebases onto the new main
  and adds ONLY the granular lib entries that close the remaining
  undefined references (`chronon3d_text_core`, `chronon3d_effects`,
  the graphics/simd sub‑targets, the color‑pipeline module if it
  exists). It also regenerates `docs/baselines/<sha>-text-baseline.md`
  with a positive `Configure / Build / Test rc=0` and a clean
  `git status -sb` — and only THEN the DoD is satisfied.

## 5. Files in the F‑A commit that landed

```
M src/render_graph/pipeline/scene_tile_execution.cpp   (ROT 1)
M src/render_graph/pipeline/tile_execution_coordinator.cpp  (ROT 1)
M tests/text/test_text_preset_visual.cpp                (ROT 2 + ROT 3)
M tests/deterministic/test_visual_regression_scenarios.cpp  (ROT 2 mirror POD only)
A docs/baselines/codex-txt-00-attempt1-blocked-375bd5b9.md   ← THIS FILE
```

Intentionally NOT in the F‑A commit (reverted at packaging):

```
  tests/text_preset_visual_tests.cmake   (baseline kept from origin/main;
                                          ROT 4 deferred to F‑B / F‑C per §4)
```

Pre‑commit, on the branch, the same 5 file edits listed above plus the
`tests/text_preset_visual_tests.cmake` modification were in the working
tree. At F‑A packaging the `.cmake` change was reverted so the commit
stays minimal and avoids mixing the link‑rot investigation into the
source‑level fix closure.

Branch state at HEAD `d261dd44`:

- Rebased onto `375bd5b…` (the `docs(agent-tasks): add Text Production
  V1 PR plan` baseline).
- Commit `91debc36` (F‑A): closes ROT 1‑3 source‑level rotations +
  adds this blocker doc.
- Commit `d261dd44` (this one): aligns section 1 ROT 4 entry +
  section 5 file list with the actual F‑A commit scope.
- 2 commits ahead of `origin/main`; pushed to
  `origin/codex/txt-00-baseline-compilable`.
- Ready to be opened as a PR against `main` (current `main` = `375bd5b…`).

## 6. Recorded commands (re‑runnable on the next attempt)

```bash
git fetch origin && git checkout main && git pull --ff-only origin main
git checkout -b codex/txt-00-<next-attempt-tag>
cmake --preset linux-ci
cmake --build --preset linux-ci --target \
    chronon3d_text_preset_visual_tests --parallel 8 \
    2>&1 | tee /tmp/vr_text_logs/build_attempt.txt
ls build/chronon/linux-ci/CMakeFiles/chronon3d_text_preset_visual_tests.dir/link.txt
find build/chronon/linux-ci -name 'libchronon3d_*.a' | sort
ctest --test-dir build/chronon/linux-ci -R 'VRTextPresetVisual' -V \
    2>&1 | tee /tmp/vr_text_logs/ctest_attempt.txt
git log -n 5 --oneline
```

## 7. Status

- `cmake --preset linux-ci`: rc=0
- `cmake --build --preset linux-ci --target
  chronon3d_text_preset_visual_tests --parallel 8`: rc=1
- `ctest --test-dir build/chronon/linux-ci -R 'VRTextPresetVisual'
  -V`: not executed (binary missing)
- `git status -sb`: dirty (rotations in‑working‑tree, uncommitted)

TXT‑00 is **NOT** declared green per AGENTS.md "non segnare verde una
suite che restituisce failure".
