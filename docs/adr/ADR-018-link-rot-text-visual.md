# ADR-018 — Fix CMake per-target `link.txt` staleness via `set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS)` on `tests/CMakeLists.txt`

| Field | Value |
|---|---|
| **Status** | Accepted (F-B diagnostic + 1-line CMake fix; landed on branch `codex/link-rot-diagnostic` off `main` @ `375bd5b9…`) |
| **Date** | 2026-06-23 |
| **Deciders** | `codex/text-rot-installer` author of branch `codex/link-rot-diagnostic`, code-reviewer-minimax-m3 |
| **Tags** | `cmake`, `configure-depends`, `ninja-tracing`, `link-rot`, `txt-00-followup`, `diagnostic-only` |
| **Related** | [ADR-010 — boundary-gate-semantic-extension](./ADR-010-boundary-gate-semantic-extension.md), [ADR-011 — camera-legacy-deletion](./ADR-011-camera-legacy-deletion.md), `docs/baselines/codex-txt-00-attempt1-blocked-375bd5b9.md` (the empirical paper-trail), `tests/CMakeLists.txt` (the orchestrator now patched), AGENTS.md §"Regole di lavoro" (no drive-by refactor mixing), `Text Production V1 PR Plan` (TXT-00 follow-ups F-A → F-C). |

## Context

PR TXT-00 attempt 1 (branch `codex/txt-00-baseline-compilable`, pinned to `375bd5b…`) brought the text‑preset visual regression baseline back to a compile-only-green state — the 4 source-level ROT 1‑3 rotations closed on `cmake --target chronon3d_graph_pipeline` (rc=0) and on `cmake --target chronon3d_text_preset_visual_tests` at the **compile step** (0 errors, `.o` produced). The **link step** of the latter still fails with 96 undefined references (`chronon3d::Scene::~Scene`, `chronon3d::Layer::~Layer`, `chronon3d::SoftwareRenderer::update_dirty_telemetry`, `chronon3d::cache::{FramebufferPool::acquire_shared,NodeCache::store}`, `chronon3d::{ExecutionScheduler,TransformScratchBuffer,CameraProjectionSource}`, `chronon3d::simd::clear_framebuffer`, `chronon3d::hash_text_run_shape`, `chronon3d::build_text_unit_map`, `chronon3d::effects::EffectCatalog::freeze`, `chronon3d::ColorPipeline::apply` — full list in `docs/baselines/codex-txt-00-attempt1-blocked-375bd5b9.md` §2) — and that broader coverage is F‑C's job, not F‑B's.

The **diagnostic finding this ADR formalises** surfaced during the F‑A investigation: whenever the link step fails, `build/chronon/linux-ci/CMakeFiles/chronon3d_text_preset_visual_tests.dir/link.txt` is **missing**, even though the surrounding `CMakeFiles/<target>.dir/` is otherwise populated. Reverting the .cmake and re-building produces the same miss. Editing any of the 23 per-area `include()`-loaded `.cmake` files (e.g. `text_preset_visual_tests.cmake`) **does not always trigger a CMake reconfigure** in the linux‑ci preset → **Ninja continues to consume the cached `build.ninja`** + the previously-generated target graph → per-target `build.make` / `link.txt` records drift out of sync with the orchestrator's intent.

Root cause: the CMake source-tracking model stat-checks:

1. **Every `CMakeLists.txt` reachable through `add_subdirectory(...)`** at configure time, and re-runs configure on change (this is what gives the canonical "I touched `tests/CMakeLists.txt` → `cmake --build` reconfigures" UX).
2. Anything mentioned in `target_sources`/`add_custom_command(OUTPUT … DEPENDS …)` — also stat-checked and re-run on change.
3. **NOTHING mentioned in `<DIRECTORY>` `CMAKE_CONFIGURE_DEPENDS`** unless explicitly opted in.

The 23 per-area `.cmake` files in `tests/CMakeLists.txt` are reached via `include(${CMAKE_CURRENT_SOURCE_DIR}/<foo>.cmake)` — `include()` is a textual content-merge, not a directory descend, so Ninja/Make file-tracking **does not see them as configure-time inputs**. Touching one of them can therefore leave the build graph stale for hours/days, only showing up as a `link.txt` miss the next time a link step happens to fail.

This is what bit TXT-00 attempt 1's step-by-step link-fix iterations: each attempt touched `tests/text_preset_visual_tests.cmake` (link surface edits inside F-A's earlier ROT 4 round), but **Ninja retained the old `build.ninja`** because the file-level dependency graph wasn't tracking the include-loaded `.cmake`. The downstream symptom was a missing `CMakeFiles/<target>.dir/link.txt`, which the build system surfaces as "no rule to make `CMakeFiles/.../link.txt`" — i.e. a configure-time error masquerading as a build-time one.

## Decision

Apply the canonical CMake pattern for opt-in configure-time stat-tracking on the **DIRECTORY** of `tests/CMakeLists.txt` over the 23 per-area `.cmake` files now `include()`d.

```cmake
# ADR-018 — declare the per-area `.cmake` files that are `include()`d
# below as configure-time dependencies, so Ninja / Make re-runs
# `cmake --preset` whenever any of them is touched. Without this, an
# edit to e.g. `text_preset_visual_tests.cmake` silently leaves the
# generated `build.ninja` and per-target `link.txt` stale (the F-A
# diagnostic surfaced this as a missing `link.txt` when the link
# step fails). Adding a `.cmake` to the property list is the canonical
# fix; do NOT add new per-area `.cmake` files without extending this
# list too. ADR-018 keeps the rule explicit and the file list co-located
# with the orchestrator.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/backends_software_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/core_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/scene_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/cli_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/optimizer_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/cache_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/compositor_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/authoring_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/animation_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/io_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/breathing_golden_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/visual_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/graphics_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/gradient_visual_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/deterministic_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/text_preset_visual_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/precomp_focus_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/content_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/video_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/media_tests.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/architecture_tests.cmake
)
```

Three design constraints honoured by this Decision (each surveyed in turn):

1. **List the 23 files explicitly, not `file(GLOB)` them.** `file(GLOB)` is discouraged by the project's CMake hygiene rules (`cmake/Hygiene.cmake`) because it produces non-deterministic results across regenerations. The explicit list is canonical, grep-discoverable, and matches the existing per-area folder organisation. The cost is a 24-line edit when a new test area is added — accepted trade: when adding a new `.cmake` it MUST be added to the property list too.
2. **Directory scope (`DIRECTORY`), not global.** The property is bound to `tests/CMakeLists.txt`'s directory (`CMAKE_CURRENT_SOURCE_DIR`), so other CMakeLists.txt trees in the project (`src/*`, `examples/*`, `apps/*`) are unaffected. Localised blast radius.
3. **`APPEND`, not `SET`.** Concatenates to any existing `CMAKE_CONFIGURE_DEPENDS` set on the same directory by an upstream `CMakeLists.txt`. Defensive against future orchestrator additions.

## Consequences

### Positive

* **Single-line per-touch rerun closure**: from now on, `touch tests/text_preset_visual_tests.cmake && cmake --build …` correctly re-runs the configure step (Ninja logs `[<N>/<M>] Generating …` or `[<N>/<M>] Re-running CMake…` in the build output), regenerates `build.ninja` + per-target `link.txt`, and then proceeds with the (still-failing-but-now-properly-instrumented) link step. No more "missing `link.txt`" stale-graph artifact.
* **Future-proof against silent staleness**: any contributor who adds a new `include()`-loaded `.cmake` will see the build suddenly reconfigure — if the property list is updated correctly, this is a feature; if it isn't, the new file will produce a fresh rebuild-graph inconsistency and the developer will need to extend the list (the gate will be enforced by the file co-location + the comment block above the property list).
* **Zero test-suite-area coordination overhead**: the property is a per-directory declaration; no cross-tree changes required.
* **F-C inheritability**: F-C's full link-surface closure (the granular libs that close the remaining 96 undefined refs) is *enabled* by this ADR in the sense that future link-step debugging won't be confounded by build-graph staleness anymore.

### Negative

* **Cost: every `.cmake` touch triggers a full configure (~1‑3 s on linux‑ci preset, more on debug generators).** 23 stat-checks per reconfigure is a small fraction of generator / reconfigure cost; the dominant cost is the regeneration of `build.ninja` + the target graph. Not measurable on developer workflow; measurable in CI if a contributor hammers the per-area tests with `touch X.cmake && cmake --build` in a loop.
* **Sparse-coverage gap**: the property targets the *file* stat-change but not the *logical* configuration state. If a developer modifies `text_preset_visual_tests.cmake` content to add a conditional `if(...) include(...)` block that depends on a CMake variable, the property triggers reconfigure but Ninja/Make regen still walks the cmakedeps graph correctly — no functional gap, just slower.
* **Hash-stability check sister-attribute**: `cmake_policy(SET CMP0074 NEW)` (default in CMake 3.27+) is required for `target_sources(... FILE_SET ...)`-style robustness, not for `CMAKE_CONFIGURE_DEPENDS`, so this ADR does not require a separate policy bump. Documented for clarity.
* **Generator-specific behaviour**: `CMAKE_CONFIGURE_DEPENDS` is honoured by **Ninja**, **Makefile**, **Ninja Multi-Config** and **Visual Studio** generators. Not honoured by **NMake** (legacy Windows) — irrelevant on this project's linux‑ci/macos‑dev presets but documented for completeness.

### Neutral

* **No external consumer impact.** No public API / SDK surface change.
* **No link-surface change.** This ADR explicitly does NOT touch `target_link_libraries(...)` lists; that's F-C's deliverable.
* **No F-A regressions.** F-A lands independently on `codex/txt-00-baseline-compilable`; both F-A and F‑B are mergeable in either order.
* **`AGENTS.md` regole di lavoro compliance**: this ADR is a single-property fix + diagnostic doc; no drive-by refactors mixed in; "non mescolare refactor indipendenti" honoured.
* **CMake policy default**: `cmake_minimum_required(VERSION 3.20)` at root `CMakeLists.txt` — `set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS <list>)` has been available since CMake 3.7 (2016); no policy bump needed.

## Migration path

Not applicable: this ADR is a single-directory declaration, not a code surface change. The migration for downstream consumers is implicit: when adding a new per-area `.cmake` to `tests/`, also extend the property list (the comment block above the property makes this obligation loud).

## Alternatives considered

* **A. `file(GLOB "tests/*.cmake")` to auto-track all per-area files.** Rejected — `cmake/Hygiene.cmake` discourages `file(GLOB)` for canonical lists (determinism risk). Explicit list is the canonical pattern in this project (cf. `cmake/Chronon3DRegistry.cmake` aggregate lists).
* **B. Move each per-area `.cmake` into a `add_subdirectory(tests/foo_tests)` sub-tree.** Rejected — pure sub-directory restructure crosses project hygiene bounds ("PR piccole e mirate / non mescolare refactor indipendenti"); would inflate scope to days of plumbing work without closing any actual link-rot symptom.
* **C. CI-only check that re-runs `cmake --preset linux-ci` after every commit.** Rejected — adds CI overhead, doesn't fix developer-local workflow, doesn't address the developer's actual `link.txt` miss symptom.
* **D. Replace Ninja with `Unix Makefiles` or visual-studio generator.** Rejected — outside F-B scope; risk of breaking cross-tool workflows.

## References

* `tests/CMakeLists.txt` — orchestrator now patched with the `CMAKE_CONFIGURE_DEPENDS` property block described in §Decision.
* `docs/baselines/codex-txt-00-attempt1-blocked-375bd5b9.md` — empirical paper-trail for F-A and the link-rot diagnostic (the source for §Context.
* `cmake/Chronon3DRegistry.cmake::CHRONON3D_REGISTRY_INTERFACE_LIBS` — the canonical INTERFACE-vs-OBJECT distinction that determines whether static archives drop `.o` lazily (cross-reference for F-C's link-surface scoping).
* `CMakeLists.txt` (root) — `cmake_minimum_required(VERSION 3.20)` + `CHRONON3D_BUILD_TESTS` / `CHRONON3D_USE_BLEND2D` flag definitions referenced by `include()`-guarded `.cmake` files.
* [ADR-010 — boundary-gate-semantic-extension](./ADR-010-boundary-gate-semantic-extension.md) — the project's most recent boundary-gate precedent (renumbering `[N/14]`).
* [ADR-011 — camera-legacy-deletion](./ADR-011-camera-legacy-deletion.md) — the model for the "Decision + Migration path + Alternatives" section structure this ADR mirrors.
* `docs/agent-tasks/TEXT_PRODUCTION_V1_PR_PLAN.md` — plan-section §3 deterministic / execution-scope-and-precomp commitments that the link-surface recovery (F-C) inherits from this ADR.
* `cmake --help-property CMAKE_CONFIGURE_DEPENDS` — official CMake property doc; "*Files added to the CMAKE_CONFIGURE_DEPENDS directory property will be re-run during the build if they are modified.*"
* AGENTS.md §"Regole di lavoro" — anchor for "non mescolare refactor indipendenti" + "non cambiare un gate per nascondere un errore" (this ADR respects both: it does NOT relax any check, it adds a tracking property).
* CI gate: `.github/workflows/gates.yml` (Gate 5 `tools/check_architecture_boundaries.sh`) — unaffected by this ADR; link-rot CI re-runs `cmake --preset linux-ci` before reading the build dir, which is the inverse direction of the in-tree bug this ADR closes.
