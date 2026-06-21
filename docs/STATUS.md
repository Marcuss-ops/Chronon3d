# Chronon3D — Current Status

> Code baseline audited on **2026-06-21** at
> `591f8e1ea0793902684389b97d1e509aae455533`.

Chronon3D is advanced but **pre-stable**. Do not describe `main` as fully green,
release-ready, or a stable external SDK until the blockers below are closed and a
required build/test run is recorded.

## Current state

| Area | State | Main gap |
|---|---|---|
| Compiled render-graph architecture | 🟡 Partial | Foundation landed; some callers/tests still use retired APIs. |
| Validation gates | 🔴 Blocked | Boundary check 5 can fail while the script exits 0. |
| Scheduler determinism | 🔴 Blocked | Test still includes deleted `ExecutionPlanCache` and raw-graph execution. |
| Nested precomp | 🔴 Blocked | `PrecompNode` header/implementation disagree; implementation creates a local executor. |
| Session/identity isolation | 🟡 Partial | Shared-context identity and nested execution scopes remain open. |
| SDK boundary | 🟡 Partial | Install validation and global/runtime bridges remain. |
| V3 tile-first | 🔵 Planned | P1–P10 are future replacement work. |
| Expressions V2 | 🧪 Experimental | Quarantined, opt-in, not installed/exported. |

## Completed foundations

- `RenderGraph → FrameGraphCompiler → CompiledFrameGraph → GraphExecutor`.
- Raw `RenderGraph` executor overloads retired.
- `ExecutionPlanCache` retired.
- Explicit `ExecutionScheduler&`.
- Strong graph/node IDs and deterministic hashing.
- Per-session scene-hasher/program-store work.
- Typed runtime-owned `AssetResolver`.
- Explicit registration through `ExtensionModule` and `ExtensionContext`.

## P0 order

1. Fix `tools/check_architecture_boundaries.sh` final result handling.
2. Migrate scheduler tests to `CompiledFrameGraph`.
3. Synchronize `PrecompNode` and borrow the session executor.
4. Assign node identity only on cloned per-node contexts.
5. Add root/tile/child execution scopes.
6. Close installed-consumer and global-state SDK boundaries.
7. Record clean required CI/build/test evidence.

See [`refactor-roadmap/README.md`](refactor-roadmap/README.md).

## Expressions V2

Actual location: `experimental/expressions/`.

- Build gate: `CHRONON3D_BUILD_EXPERIMENTAL=ON`.
- Default: `OFF`.
- Not installed by `cmake --install`.
- Not linked by `Chronon3D::SDK`.
- No stable productive render-path dependency.
- `TICKET-003` and `TICKET-004` are closed historical fixes.
- Promotion requires all eight gates in
  [`EXPRESSIONS_V2_PROMOTION.md`](EXPRESSIONS_V2_PROMOTION.md).

## Document roles

- `STATUS.md`: current maturity and blockers.
- `refactor-roadmap/`: unfinished architecture.
- `ROADMAP.md`: feature/performance backlog.
- `CHANGELOG.md`: completed historical work.
- `V3_BLUEPRINT.md`: planned tile-first replacement.
