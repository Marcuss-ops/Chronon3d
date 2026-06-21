# Architecture Evolution Plan — Chronon3D

Current status: [`STATUS.md`](STATUS.md). Active work:
[`refactor-roadmap/README.md`](refactor-roadmap/README.md).

## Current frontier

`Composition → Scene → RenderGraph → FrameGraphCompiler → CompiledFrameGraph → GraphExecutor → RenderBackend → output`

Landed foundations:

- compiled-graph-only execution;
- raw-graph overloads and `ExecutionPlanCache` retired;
- explicit scheduler;
- strong graph/node IDs and deterministic hashing;
- per-session scene/program-store work;
- typed runtime-owned asset resolution;
- explicit extension registration through host-owned registries.

Open work includes the validation gate, stale scheduler tests, `PrecompNode`
mismatch, identity race, nested execution scopes, backend separation, and SDK
install closure.

## Ownership

- Core: contracts and invariants.
- Feature: effects, nodes, exporters, media, presets.
- Integration: registries, catalogs, resolvers, samplers, extension points.
- Diagnostics: telemetry, profiling, debug and visual validation.
- Experimental: isolated opt-in work not exported by the stable SDK.

Every capability must extend a canonical registry, resolver, sampler, cache, or
execution contract. Parallel systems are forbidden by
[`ANTI_DUPLICATION_RULES.md`](ANTI_DUPLICATION_RULES.md).

## Registration

Canonical path:

`ExtensionModule → ExtensionContext → CompositionRegistry / GraphNodeCatalog / EffectCatalog / AssetRegistry`

Static global composition registration is retired. Consumer compositions belong
in external packs.

## Expressions V2

Expressions V2 lives under `experimental/expressions/`, is gated by
`CHRONON3D_BUILD_EXPERIMENTAL`, defaults to OFF, and is not installed or linked
by `Chronon3D::SDK`. TICKET-003 and TICKET-004 are closed. Promotion still
requires all eight gates in `EXPRESSIONS_V2_PROMOTION.md`.

## V3 tile-first

V3 is future replacement work, not the current runtime. P1–P10 remain planned.
Each component must name the V2 path it replaces, equivalence tests, objective
removal criteria, and a deletion milestone. Permanent dual paths are forbidden.

## Stable consumer boundary

Consumers should include public headers only, link only `Chronon3D::SDK`, avoid
`src/` and experimental headers, and use shared extension/resolver contracts.
