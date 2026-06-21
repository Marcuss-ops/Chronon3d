# Chronon3D — Current Repository Status

> **Source of truth for current maturity and release readiness.**
>
> Last audited: **2026-06-21**  
> Audited `main`: `591f8e1ea0793902684389b97d1e509aae455533`

Chronon3D is an advanced, actively developed C++20 motion-graphics engine, but the current `main` branch is **pre-stable**. Several core refactors are complete, while the validation gates, precomp execution path, scheduler determinism coverage, and SDK boundary still contain known blockers.

Use the other documents for their narrower purposes:

- [`refactor-roadmap/README.md`](refactor-roadmap/README.md): unfinished architectural work.
- [`ROADMAP.md`](ROADMAP.md): feature and performance backlog.
- [`CHANGELOG.md`](CHANGELOG.md): completed work and historical delivery record.
- [`V3_BLUEPRINT.md`](V3_BLUEPRINT.md): future tile-first architecture.
- [`EXPRESSIONS_V2_PROMOTION.md`](EXPRESSIONS_V2_PROMOTION.md): promotion gates for Expressions V2.

## Overall assessment

| Area | State | Meaning |
|---|---|---|
| Core rendering architecture | 🟡 Advanced / transitional | Compiled-graph execution and explicit scheduler ownership have landed, but some callers and tests still target retired APIs. |
| Full build trustworthiness | 🔴 Blocked | The architecture boundary script can report success even when its final check fails. |
| Nested precomp execution | 🔴 Blocked | `PrecompNode` header and implementation are out of sync; the implementation still constructs a local executor. |
| Scheduler determinism | 🔴 Blocked | The registered test still includes the deleted `ExecutionPlanCache` API and calls a retired raw-graph overload. |
| Session and identity isolation | 🟡 In progress | Per-session state exists, but execution identity and nested-scope ownership are not fully closed. |
| Public SDK boundary | 🟡 In progress | `Chronon3D::SDK` exists, but the install boundary and removal of process/global bridges still need completion. |
| V3 tile-first architecture | 🔵 Planned | P1–P10 remain future work. |
| Expressions V2 | 🧪 Quarantined | Opt-in experimental subsystem; not installed, exported, or linked by the default SDK. |

**Release-readiness conclusion:** do not describe the repository as fully green, release-ready, or a stable external SDK until the P0 blockers below are closed and a clean required CI/build run is recorded.

## Completed foundations

The following architectural foundations are present on `main`:

- compiled-graph-only executor frontier: `RenderGraph → FrameGraphCompiler → CompiledFrameGraph → GraphExecutor`;
- retirement of the raw `RenderGraph` executor overloads;
- retirement of `ExecutionPlanCache`;
- explicit `ExecutionScheduler&` on graph execution;
- strong graph/node identity wrapper types;
- deterministic project hashing for stable IDs;
- per-session `SceneHasher` and `SceneProgramStore` ownership work;
- canonical frame and dirty-history work;
- typed `AssetResolver` owned by `RenderRuntime`;
- migration away from the legacy string-only asset-resolution helper;
- modular registries and explicit extension registration through `ExtensionContext`.

Completed foundations do **not** imply that every target currently compiles. The active blockers below are the authoritative qualification.

## P0 blockers

1. **Repair the boundary gate**
   - `tools/check_architecture_boundaries.sh` evaluates the final PASS/FAIL branch before check 5.
   - A forbidden `plan_cache` hit can set `FAILED=1` and still end with exit code 0.

2. **Repair scheduler determinism tests**
   - `tests/render_graph/executor/test_scheduler_determinism.cpp` still includes `chronon3d/runtime/execution_plan_cache.hpp`.
   - It still passes `ExecutionPlanCache*` and invokes a retired raw-graph executor overload instead of compiling a `CompiledFrameGraph`.

3. **Synchronize `PrecompNode`**
   - `include/chronon3d/render_graph/nodes/precomp_node.hpp` derives the instance key from `RenderGraphContext`.
   - `src/render_graph/cache/precomp_node_execute.cpp` still references a removed cached `m_instance_key`, implements the old accessor shape, and constructs `GraphExecutor local_executor`.

4. **Remove the shared-context identity race**
   - node identity must be assigned on the per-node cloned context before parallel execution, not on a shared `RenderGraphContext`.

5. **Add explicit execution scopes**
   - root, tile, and nested-precomp execution need independent arena ownership;
   - child execution must never reset a parent arena.

6. **Close the SDK and global-state boundary**
   - finish removing active-runtime/process-global asset access;
   - complete the standalone installed-consumer validation;
   - keep consumers on `Chronon3D::SDK` and public headers only.

Detailed sequencing and acceptance criteria live in [`refactor-roadmap/README.md`](refactor-roadmap/README.md).

## Expressions V2: actual status

Expressions V2 is **not** a stable public feature.

| Property | Current state |
|---|---|
| Location | `experimental/expressions/` |
| Public experimental headers | `experimental/expressions/include/chronon3d_experimental/expressions/v2/` |
| Build gate | `CHRONON3D_BUILD_EXPERIMENTAL=ON` |
| Default | `OFF` |
| Installed by `cmake --install` | No |
| Exported through `Chronon3D::SDK` | No |
| Productive render-path dependency | No |
| Promotion contract | Eight gates in `EXPRESSIONS_V2_PROMOTION.md` |

`TICKET-003` and `TICKET-004` are closed. They are historical fixes, not current promotion blockers. The remaining promotion work includes real `AnimatedValue` integration, benchmark evidence, the V1→V2 replacement map, a removal deadline, and enforcement of a single productive parser/VM/dependency graph.

Use `CHRONON3D_BUILD_EXPERIMENTAL=ON` only for opt-in development and validation. Do not include `chronon3d_experimental/...` from stable SDK or production code.

## CI and validation evidence

The repository contains GitHub Actions configuration for Linux, Windows, ASAN, no-content, fast-PR, and full-push builds. For the audited commit, however, no remote combined status or workflow run was exposed through the repository API.

Therefore:

- the presence of workflow YAML is not proof that the audited commit is green;
- documentation must not claim a successful full build without a recorded run;
- a doc-only audit cannot substitute for configure, build, test, install-consumer, architecture-gate, and deterministic-test execution.

## Documentation status conventions

- ✅ **Verified** — implemented and backed by a recorded test/build/benchmark result.
- 🟡 **Partial** — meaningful implementation exists, but acceptance criteria remain.
- 🔵 **Planned** — design or backlog item; not implemented.
- 🧪 **Experimental** — implemented behind an opt-in quarantine and not part of the stable SDK.
- 🔴 **Blocked** — a known compile, test, correctness, or validation issue prevents completion.

When code and prose disagree, update this file and the owning roadmap in the same change. Historical documents may retain old names only when clearly marked as historical.
