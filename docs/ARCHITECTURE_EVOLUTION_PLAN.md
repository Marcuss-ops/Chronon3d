# Architecture Evolution Plan — Chronon3D

Stato presente: [`CURRENT_STATUS.md`](CURRENT_STATUS.md). Piano operativo:
[`ROADMAP.md`](ROADMAP.md).

## Frontiera corrente

`Composition → Scene → RenderGraph → FrameGraphCompiler → CompiledFrameGraph → GraphExecutor → RenderBackend → output`

Fondazioni completate:

- esecuzione solo su grafo compilato;
- rimozione overload raw graph e `ExecutionPlanCache`;
- scheduler esplicito;
- ID forti e hashing deterministico;
- stato per-sessione;
- `AssetResolver` tipizzato;
- registrazione esplicita tramite registry host-owned.

Restano aperti gate non affidabili, test scheduler obsoleti, `PrecompNode`, race sull’identità, scope annidati e confine SDK.

## Ownership canonica

- Core: contratti e invarianti.
- Feature: effetti, nodi, exporter, media e preset.
- Integration: registry, catalog, resolver, sampler ed extension point.
- Diagnostics: telemetria, profiling, debug e visual validation.
- Experimental: lavoro opt-in non esportato dallo SDK stabile.

Non creare registry, resolver, sampler, cache o execution path paralleli. Vedi [`ANTI_DUPLICATION_RULES.md`](ANTI_DUPLICATION_RULES.md).

## Registrazione

`ExtensionModule → ExtensionContext → CompositionRegistry / GraphNodeCatalog / EffectCatalog / AssetRegistry`

La registrazione statica globale è ritirata. Le composizioni cliente devono vivere in pack esterni.

## Target

```text
RenderRuntime: servizi engine-lifetime
RenderSession: stato job-owned
ExecutionScope: root/tile/precomp, parent, arena, identità
GraphExecutor: stateless, compiled graph, scheduler/scope espliciti
```

## Sequenza

1. Riparare gate e test.
2. Sistemare Precomp e lease.
3. Eliminare race e introdurre `ExecutionScope`.
4. Chiudere SDK/install consumer.
5. Riparare diagnostics/content e test fast.
6. Solo dopo riaprire performance avanzata e V3.

## Expressions V2

Percorso reale: `experimental/expressions/`.
Header: `experimental/expressions/include/chronon3d_experimental/expressions/v2/`.
Build gate: `CHRONON3D_BUILD_EXPERIMENTAL`, default OFF.
Non è installato, esportato o usato dal percorso produttivo.

TICKET-003 e TICKET-004 sono chiusi. Il prossimo lavoro è TICKET-EXP2-G3: migrare Path A verso Path B senza due parser/VM produttivi. La promozione richiede tutti gli otto gate di [`EXPRESSIONS_V2_PROMOTION.md`](EXPRESSIONS_V2_PROMOTION.md).

## V3 tile-first

V3 è futuro lavoro di sostituzione. P1–P10 restano pianificati. Ogni componente deve dichiarare il percorso V2 sostituito, test di equivalenza, criterio di rimozione e milestone di eliminazione.

## Confine consumer

Un consumer esterno deve includere solo header pubblici, collegare solo `Chronon3D::SDK`, evitare `src/` e `chronon3d_experimental/`, e ricevere servizi tramite contratti espliciti.
