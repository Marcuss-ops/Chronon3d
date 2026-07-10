# ADR-001 — Frame Graph Compiler come unica superficie di compilazione

- **Status:** Accepted (work-in-progress verso *Validato in CI* in WP-1)
- **Date:** 2026-06-17 (riformulato in ADR dai record storici)
- **Deciders:** Core renderer team
- **Tags:** architecture, render-graph, compiler

## Context

In passato il codice accettava l'esecuzione di un `RenderGraph`
grezzo (non compilato) da parte di `GraphExecutor` e manteneva una
cache di piani di esecuzione (`chronon3d::runtime::ExecutionPlanCache`)
per riutilizzare la topo-walk. Entrambi i meccanismi sono stati
identificati come fonti di non-determinismo e di stato nascosto:

- `RenderGraph` raw permetteva a un nodo per-cornice di modificare la
  topologia del grafo *dopo* la compilazione, rompendo la cache delle
  identità forti.
- `ExecutionPlanCache` manteneva un `shared_ptr` su dati
  potenzialmente vivi oltre il job, con race su `current_identity`.

## Decision

1. `FrameGraphCompiler` è l'unica superficie di compilazione. La
   sua firma canonica è
   `compile(graph, ctx) -> CompiledFrameGraph`. La variante
   `compile_with_reuse(graph, ctx, prior, options)` (TICKET-008) è
   un affordance compile-time sulla stessa `CompiledFrameGraph`
   payload, *non* un plan cache parallelo.
2. `GraphExecutor` accetta solo `CompiledFrameGraph`. Gli overload
   su `RenderGraph` raw sono rimossi.
3. `chronon3d::runtime::ExecutionPlanCache` non esiste più. La sua
   rimozione registrata.

## Consequences

* **Positive.** Topologia del grafo congelata una sola volta;
  identità per-nodo stabile; compilazione deterministica via
  `structure_hash`; nessun double-cache.
* **Negative.** Una cache LRU esterna esplicita deve essere gestita
  dal chiamante (oggi `RenderRuntime::graph_cache()` la fronte).
* **Neutral.** I test che confrontano più scheduler usano lo stesso
  `CompiledFrameGraph`; nessuna differenza di esecuzione osservabile.

## Alternatives considered

* **Cache piani per ogni scheduler.** Scartata: accoppia
  compilazione a scheduler e reintroduce ExecutionPlanCache di fatto.
* **AOT compile on construction.** Prematuro: richiederebbe
  determinismo di compilazione molto più stretto di quanto WP-1 può
  promettere prima del baseline.

## References

* Commit `9f9af90e`: rimozione `ExecutionPlanCache`.
* TICKET-008 (FOLLOWUP_TICKETS.md): closure di `graph_structure_unchanged`
  via `compile_with_reuse`.
* [`../ARCHIVE/refactor-roadmap/01-scheduler-single-authority.md`](../ARCHIVE/refactor-roadmap/01-scheduler-single-authority.md).
* [`../ARCHIVE/stabilization-plan/02-determinism.md`](../ARCHIVE/stabilization-plan/02-determinism.md) (§TODO).
