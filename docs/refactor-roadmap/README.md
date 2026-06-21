# Chronon3D refactor roadmap

Repository status: [`../STATUS.md`](../STATUS.md).

Active order:

1. WP0 validation gates.
2. WP5 precomp and SceneProgramStore.
3. WP4 stable identity.
4. WP6 execution scopes.
5. WP1 scheduler determinism.
6. WP3 session boundary.
7. WP8 globals and SDK.
8. WP7 software backend.

Current blockers:

- Boundary check 5 can fail without a non-zero result.
- Scheduler tests use retired plan-cache/raw-graph APIs.
- `PrecompNode` header and implementation disagree.
- Shared-context identity can race before cloning.
- Root, tile, and child execution scopes are not explicit.
- Installed SDK and global/runtime boundaries remain incomplete.

Target:

```text
RenderRuntime: engine-lifetime services
RenderSession: job-owned state
ExecutionScope: root/tile/precomp lifetime and arena
SceneProgramStore: identity-keyed locked lease
GraphExecutor: stateless, compiled graphs, explicit scheduler/scope
```

Update this file together with `../STATUS.md`.
