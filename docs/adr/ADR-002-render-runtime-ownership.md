# ADR-002 — `RenderRuntime` possiede i servizi engine-lifetime

- **Status:** Accepted (work-in-progress verso *Validato in CI* in R3 del piano 06)
- **Date:** 2026-06-21
- **Deciders:** Core runtime + refactor roadmap coordinatori
- **Tags:** architecture, ownership, runtime, render-runtime

## Context

Prima di TICKET-011, `SoftwareRenderer` teneva tre aggregati di stato
a lungo vita:

* `m_cache_state` — `NodeCache`, `FramebufferPool`, `CompiledGraphCache`.
* `m_runtime_resources` — `SoftwareRegistry`, `GraphExecutor`,
  `GraphNodeCatalog`, `EffectCatalog`.
* `m_backend` — istanza di `SoftwareBackend`.

Conseguenze: ciclo di vita accoppiato al renderer, lifetime
extension nei test, allocator multipli, e perdita della facciata
unica `RenderBackend` (perché il renderer implementava già
l'interfaccia).

## Decision

1. `RenderRuntime` possiede *tutti* i servizi a lungo vita:
   asset resolver, scheduler, executor, cataloghi (Shape,
   GraphNode, Effect, Sampler, Source, Composition), il backend
   `SoftwareBackend`, e le cache (`NodeCache`, `FramebufferPool`,
   `CompiledGraphCache`).
2. `SoftwareRenderer` prende un **puntatore** (`m_runtime`) al
   runtime, non un riferimento. Ciò permette il ctor transitorio
   `SoftwareRenderer(Config)` che sintetizza un runtime interno via
   `m_owned_runtime_storage` e vi lega il puntatore.
3. Tutti i consumer del renderer (`RenderGraph`,
   `RenderGraphContext`, `PrecompNode`, i processor, i nodi del
   pipeline) devono raggiungere cache/scheduler/executor attraverso
   il runtime, sia direttamente sia via forwarder sul renderer in
   via transitoria.
4. Il backend `SoftwareBackend` è attaccato esternamente dal
   `RenderEngine::Impl`. `SoftwareRenderer` non eredita più
   `graph::RenderBackend` (R3 del piano 06).

## Consequences

* **Positive.** Una sola fonte per cache/scheduler/executor;
  `SoftwareBackend` testabile senza `SoftwareRenderer`; ciclo di
  vita indipendente dal renderer; preparazione al SDK boundary
  (ADR-003) e a multi-engine isolation (PR 8.2 / R6 piano 06).
* **Negative.** Accesso indiretto per i consumer: richiede il
  rafforzamento dei forwarder (post-R3).
* **Neutral.** API pubblica invariata: il consumer vede solo
  `RenderEngine` (ADR-003) e non nota il cambiamento.

## Alternatives considered

* **Servizi globali.** Scartata: viola ADR-005 (no bridge globali).
* **Service-locator generico.** Scartata: viola ANTI_DUPLICATION_RULES
  #9 (nessuna cache/servizio ad-hoc) e introdurrebbe un punto di
  accesso unico a costo di un'altra singletons.
* **Renderer come composito runtime.** Prematura: confonderebbe
  orchestrazione e runtime.

## References

* TICKET-011 / `docs/FOLLOWUP_TICKETS.md`.
* WP-3 PR 3.4 (SoftwareRenderSession end-state).
* WP-7 PR 7.4 (`SoftwareRenderer` non più backend).
* [`../ARCHIVE/stabilization-plan/06-renderer-plan.md`](../ARCHIVE/stabilization-plan/06-renderer-plan.md) (R3, R6).
