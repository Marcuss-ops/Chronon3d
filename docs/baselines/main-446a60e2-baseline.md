# Post-merge Baseline — main @ 446a60e2

> Recorded on: 2026-06-23
> Branch: `main`
> HEAD: `446a60e2` (Merge codex/agent1-renderer-boundary: Agent-1 perimeter + TICKET-009 cmake fix)
> Trigger: post-merge stabilization gate validation, TICKET-009 partial closure (PR-A4 merged; PR-A in orphan-cleanup; PR-B outstanding).

## Validation steps

This baseline captures four checks against a clean checkout of `main` at HEAD `446a60e2` (working-tree dirty count = 0; all on main reference).

### (a) Boundary gate — `tools/check_software_renderer_boundary.sh`

| Aspect | Value |
|---|---|
| Command | `bash -n tools/check_software_renderer_boundary.sh && bash tools/check_software_renderer_boundary.sh` |
| Exit code | `0` |
| Verdict | **PASS** ✅ |

Boundary invariants enforced (informational):

- I1: no `SoftwareRenderer` inheritance from `graph::RenderBackend`.
- I2: `software_renderer.hpp` LOC budget ≤ 200.
- I3: no forwarders leaking `RenderRuntime` internals.
- I4: dual-ownership constructors explicit.
- I5: no `SoftwareRenderer const&&` EAST-CONST hack.

### (b) `cmake --preset linux-full-validation` (configure-only)

| Aspect | Value |
|---|---|
| Command | `rm -rf build/chronon/linux-full-validation && cmake --preset linux-full-validation` |
| Exit code | `0` |
| Verdict | **PASS** ✅ |

Configure completed cleanly; presets activated, dependency tree resolved, generated build files for the full-validation profile. No build was issued; only configure.

### (c.1) `cmake --preset linux-lean-dev` (configure-only)

| Aspect | Value |
|---|---|
| Command | `rm -rf build/chronon/linux-lean-dev && cmake --preset linux-lean-dev` |
| Exit code | `0` |
| Verdict | **PASS** ✅ |

Lean-dev preset configured cleanly. The baseline test target `chronon3d_text_preset_visual_tests` is registered.

### (c.2) `cmake --build --target chronon3d_text_preset_visual_tests --parallel 8`

| Aspect | Value |
|---|---|
| Command | `cmake --build build/chronon/linux-lean-dev --target chronon3d_text_preset_visual_tests --parallel 8` |
| Exit code | `1` |
| Verdict | **FAIL — TICKET-039 NEW rot (regression)** ❌ |

**Important discovery (TICKET-039 — NEW, not previously expected)**:

The targeted build of `chronon3d_text_preset_visual_tests` fails NOT due to the previously predicted TICKET-038 lambda-capture rot in `tests/text/test_text_preset_visual.cpp`, but due to a **new type-system rot** in `src/runtime/render_engine.cpp`:

> `SoftwareRenderer::settings()` is referenced from `RenderEngine::Impl` but is `private` (declared as `m_settings`).

This is a **regression** introduced during the Agent-1 perimeter refactor. The original branch's commit `b5c7df01` "canonicalize settings()" apparently renamed/shadowed the public `settings()` accessor (or changed visibility) without updating the only known consumer (`RenderEngine::Impl`).

The PR pre-merge baseline validation flow did NOT catch this because:

1. The targeted build was only attempted for `chronon3d_text_preset_visual_tests` (canonical-pattern cmake fix), which uses `settings()` indirectly via `runtime()->render_settings()` chain OR via `RenderSettings` glob — not via the removed `SoftwareRenderer::settings()` alias.
2. The full-validation profile configure (step b) does not compile instantiations of `RenderEngine::Impl::Impl()` until a target that pulls it in is built.
3. No full `cmake --build` was invoked before merging to main.

**Scope of the rot**:

- Single TU: `src/runtime/render_engine.cpp`.
- Surface impact: any target that links `chronon3d_runtime` and instantiates `RenderEngine::Impl` (e.g., the CLI, app binaries, possibly some integration tests).
- Surface relief: `chronon3d_text_preset_visual_tests` itself only fails because the build graph pulls in `RenderEngine::Impl` transitively (via `chronon3d_runtime` chain).

**Recommended fix (TICKET-039)**:

Either:
- (a) Restore `[[nodiscard]] RenderSettings& settings() const noexcept { return m_settings; }` on `SoftwareRenderer` as a public inline accessor (canonical-pattern restore).
- (b) Update `RenderEngine::Impl` to use `m_settings` directly (private-access friend pattern) OR `render_settings()` chain.
- (c) Replace the call site with the orchestrator pattern (`sw_renderer.runtime()->render_settings()`).

Out of scope for this baseline-record commit. Tracked separately.

## Summary table

| Step | rc | Verdict |
|---|---|---|
| (a) Boundary gate | 0 | **GREEN** ✅ |
| (b) linux-full-validation configure | 0 | **GREEN** ✅ |
| (c.1) linux-lean-dev configure | 0 | **GREEN** ✅ |
| (c.2) target build | 1 | **RED** ❌ — TICKET-039 settings() regression |

**Net verdict**: 3/4 GREEN. The single targeted target build fails due to TICKET-039 (new regression discovered during baseline) — orthogonal to the TICKET-038 prediction previously expected.

## Cross-references

- `docs/FOLLOWUP_TICKETS.md:1118` — TICKET-009 entry (PR-A4 partially closed at `64c40ed9`; PR-A subtree-fix in orphan-cleanup; PR-B outstanding).
- `docs/FOLLOWUP_TICKETS.md:1136` — TICKET-008 Done (precedent for hash reference archiving).
- `docs/baselines/` — this and future baselines (one per main-HEAD consent point).
- `docs/agent-tasks/AGENT_1_RENDERER_BOUNDARY.md` — Agent-1 perimeter scope; commit `b5c7df01` is the suspect source of TICKET-039 regression.  <!-- drift-allow: stale-ref -->
- `https://github.com/Marcuss-ops/Chronon3d/compare/446a60e2^...446a60e2` — graph view of the merge commit content (archeology lookup helper).
- AGENTS.md — gate + commit discipline + per-commit hygiene rules.

## Next stabilization wave (priority queue)

Per AGENTS.md *Priorità obbligatoria* after P0 stabilization:

1. **Open TICKET-039 (NEW, prioritized-high)** — `SoftwareRenderer::settings()` access regression from `b5c7df01`; restore canonical accessor OR update `RenderEngine::Impl` consumer. Required for c.2 of any future baseline to flip to GREEN.
2. **Open TICKET-038** — pre-existing rot in `tests/text/test_text_preset_visual.cpp` (lambda capture / auto deduction) — secondary blocker for full TU compilation chain.
3. **Land orphan-cleanup PR** — `codex/orphan-cleanup-2026-06-23` (TICKET-009 PR-A subtree-fix in `experimental/expressions/CMakeLists.txt` + 6 other orphan captures per AGENTS.md "PR piccole"). Required for main to absorb merged-from-feature work consistently.
4. **Open TICKET-009 PR-B** — `CompileResult` rot in `experimental/expressions/src/expressions/v2/vm.cpp:414` + cascading variant errors. Required for `chronon3d_expressions_v2_tests` target to compile end-to-end.  <!-- drift-allow: stale-ref -->

Each ticket lands via its own PR per AGENTS.md "PR piccole e mirate".

## Archeology note

The first-attempt baseline flow (in the same session) predicted TICKET-038 as the (c.2) failure cause. This baseline (recorded here) supersedes that prediction with the discovered TICKET-039. TICKET-038 remains an OPEN rot, but it did NOT manifest in the (c.2) build path because the target build aborted at TICKET-039 first. Once TICKET-039 is fixed, TICKET-038 may re-surface and should be the next item in the priority queue.
