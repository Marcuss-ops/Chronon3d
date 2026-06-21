# Chronon3d refactor roadmap

This directory is the operational source of truth for the runtime and render-graph cleanup.

`PR` labels are implementation package numbers. They do not require a GitHub pull request. Work may be committed directly according to the repository owner's workflow, but packages must still be completed in order and validated independently.

## Execution order

1. [Work Package 0 — Baseline and validation gates](00-baseline-and-gates.md)
2. [Work Package 1 — One scheduler authority](01-scheduler-single-authority.md)
3. [Work Package 2 — Compiled graph only](02-compiled-graph-only.md)
4. [Work Package 3 — Render session boundary](03-render-session-boundary.md)
5. [Work Package 4 — Stable node identity](04-stable-node-identity.md)
6. [Work Package 5 — Scene program store](05-scene-program-store.md)
7. [Work Package 6 — Execution scopes](06-execution-scopes.md)
8. [Work Package 7 — Complete the software backend](07-software-backend-completion.md)
9. [Work Package 8 — Remove global state and close the SDK](08-global-state-and-sdk.md)

## Status

| Package | Status | Blocking dependency |
|---|---|---|
| 0 — Baseline and gates | TODO | none |
| 1 — Scheduler authority | TODO | package 0 |
| 2 — Compiled graph only | TODO | package 1 |
| 3 — Session boundary | TODO | package 2 |
| 4 — Stable identity | TODO | package 3 |
| 5 — Scene program store | TODO | package 4 |
| 6 — Execution scopes | TODO | package 5 |
| 7 — Software backend | TODO | package 6 |
| 8 — Global state and SDK | TODO | package 7 |

Allowed status values:
- `TODO`
- `IN PROGRESS`
- `BLOCKED`
- `DONE`

Update this table whenever a package starts or finishes.

## Rules

- Complete packages in order unless the dependency is explicitly documented as independent.
- Keep one architectural problem per implementation package.
- Search for existing code before adding a new type or service.
- Reuse registries, resolvers, samplers, builders, compilers, and caches already present.
- Do not introduce a second owner for the same state.
- Do not mix feature work with these packages.
- Add targeted tests before marking a package done.
- Run the architecture, no-content, and install-consumer checks after every package.
- Update the package checklist and completion record in the same commit that finishes the package.

## Permanent target architecture

```text
RenderEngine::Impl
  RenderRuntime
    backend
    scheduler
    catalogs and registries
    engine-lifetime caches
    asset resolver
  RenderPipeline
  render-session factory

RenderSession
  job history
  dirty history
  telemetry
  session-owned scene program store

ExecutionScope
  active arena
  graph identity
  root, tile, or precomp lifetime

GraphExecutor
  stateless
  compiled graphs only
  explicit scheduler
  explicit execution scope
```

## Permanent boundary checks

The completed roadmap must enforce these conditions:

- no scheduler construction inside graph executor
- no production raw-graph executor path
- no software dependency from runtime session headers
- no graph dependency from runtime session headers
- no executor or session owned by `PrecompNode`
- no free arena override parameter
- no direct TBB orchestration in tile execution
- no backend-to-renderer cast in graph pipeline
- no process-wide active runtime or asset root
- one documented SDK consumer target: `Chronon3D::SDK`

## Completion protocol

For each package:

1. Mark the package `IN PROGRESS` in this file.
2. Complete tasks in numeric order.
3. Add or update targeted tests.
4. Run validation listed in Work Package 0.
5. Fill the package completion record.
6. Mark all exit criteria.
7. Mark the package `DONE` here.
8. Start the next package only after the previous package is stable.
