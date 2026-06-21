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

---

## Experimental Zone — Promozioni recenti (2026-06-20)

Componenti entrate nella **Experimental Zone** (vedi `CORE_OWNERSHIP.md` §1D)
con la corrispondente traccia di lifecycle, registrata qui il 2026-06-20
per evitare che i reviewer perdano tempo riaprendo TICKET già documentati.

### `expressions/v2` — attualmente su `main`

| Campo | Valore |
|---|---|
| **Stato** | 🧪 Sperimentale — merged su `main`, non ancora promosso a feature stabile |
| **Dove su main** | `include/chronon3d/expressions/v2/` (header pubblici) + `src/expressions/v2/` (sorgenti + target `chronon3d_expressions_v2`) + `tests/expressions/` (fixture di test, alcune disabilitate) |
| **Perché su main nonostante Experimental** | Merged via PR #23 come pull-request sperimentale; non ancora passato al vaglio della fase di promozione |
| **Provenance** | `CHANGELOG.md` → "Expression System v2 — Lifecycle" |
| **Promotion blockers** | TICKET-EXP2-G3 (Path A scalar parser delegation to Path B `compile()` — Gate 3 di `EXPRESSIONS_V2_PROMOTION.md`). TICKET-003 (typo `<chrono3d/...>`) e TICKET-004 (regression `PUBLIC ${CMAKE_SOURCE_DIR}`) sono **🟢 Done** dal 2026-06-20. |
| **Storia del flag CMake** | `CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2` ritirato in questa sessione; `option(... OFF)` mantenuto a `CMakeLists.txt:233-237` come no-op deprecato per compatibilità della cache-key |

La promozione alla **Feature Zone** richiede i quattro gate elencati in
[`FEATURES.md` → "Expression System v2 — Experimental" → Promotion gates](FEATURES.md#expression-system-v2--experimental).

> Audit di copertura (2026-06-20): nessun riferimento stale al flag in CI,
> preset, script, `.cfg`, `.ini`, `.env`, `Dockerfile*`, `.github/workflows`,
> `vcpkg.json`, `tools/*.sh`, o documenti MD. Uniche menzioni residue del
> flag sono: (a) il blocco di retirement comment in `CMakeLists.txt`,
> intenzionale, e (b) i riferimenti storici in `docs/FOLLOWUP_TICKETS.md`
> (TICKET-003/-004/-005).
