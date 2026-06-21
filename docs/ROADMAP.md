# Chronon3D — Active Roadmap

Stato corrente: [`STATUS.md`](STATUS.md). Piano operativo:
[`NEXT_STEPS.md`](NEXT_STEPS.md). Work package:
[`refactor-roadmap/README.md`](refactor-roadmap/README.md).

> ✅ Completato = presente in `CHANGELOG.md` con prova.
> 🟡 Parziale = implementazione esistente ma criteri non chiusi.
> 🔵 Pianificato = non implementato.
> 🔴 Bloccato = impedisce una validazione affidabile.

## P0 — da chiudere prima di nuova architettura

| Ordine | Lavoro | Stato | Chiusura richiesta |
|---|---|---|---|
| 1 | Gate architetturale finale | 🔴 Bloccato | Fixture positiva verde e negativa rossa |
| 2 | Test scheduler sul contratto compilato | 🔴 Bloccato | Nessun riferimento a plan cache/raw graph; test ripetuti verdi |
| 3 | `PrecompNode` e `SceneProgramStore` | 🔴 Bloccato | Header/impl coerenti, lease bloccata, executor di sessione |
| 4 | Identità per-node senza race | 🔴 Bloccato | Scrittura solo sul contesto clonato |
| 5 | `ExecutionScope` root/tile/precomp | 🔵 Pianificato | Child non resetta arena/stato parent |
| 6 | SDK/install consumer | 🟡 Parziale | Progetto esterno installato, compilato ed eseguito |
| 7 | Validazione completa | 🔴 Bloccato | Build/test/CI registrati sul commit di chiusura |

## Riparazioni subito dopo P0

| Ticket | Lavoro reale | Strategia |
|---|---|---|
| TICKET-002 | Diagnostics/content con API rotte | PR piccole per include, API, registry, link e test; conteggio errori residui |
| TICKET-006 | Linkage text backend nei test fast | Collegare il target canonico, non duplicare sorgenti |
| TICKET-005 | `keyframes()` e documentazione expression | Decidere restore o rimozione; aggiornare API/test senza promuovere V2 |
| TICKET-008 | Fast path `graph_structure_unchanged` | Hint verificato da hash/topologia, fallback e benchmark |
| TICKET-EXP2-G3 | Path A → Path B | Un solo parser/VM produttivo, senza convivenza permanente |

## Performance e feature non bloccanti

Questi lavori restano sospesi finché il P0 non è verde:

- motion blur SIMD e benchmark box blur;
- specializzazione blend dispatcher;
- ownership dei framebuffer temporanei;
- `LayoutFlow` e `LayoutGrid`;
- zero-copy encoder;
- metriche miss del framebuffer pool;
- valutazione SPSC, ISPC e NUMA;
- test dedicati per Shadow, Glow, Bloom, Gradient, DoF e Mask.

## V3 tile-first

I dieci pillar di [`V3_BLUEPRINT.md`](V3_BLUEPRINT.md) restano pianificati.
Non iniziare implementazione V3 finché P0, SDK boundary e test deterministici non sono chiusi.
Ogni pillar deve dichiarare il percorso V2 sostituito e il criterio di eliminazione.

## Expressions V2

- Quarantena e default OFF: completati.
- TICKET-003 e TICKET-004: chiusi.
- Gate 3 `AnimatedValue`/Path A → Path B: aperto.
- Benchmark, replacement map, deadline e single-engine enforcement: aperti.
- Install/export stabile e rimozione quarantena: non consentiti prima degli otto gate.

Contratto autorevole:
[`EXPRESSIONS_V2_PROMOTION.md`](EXPRESSIONS_V2_PROMOTION.md).

## Cosa non deve tornare

- `ExecutionPlanCache`.
- Overload executor su `RenderGraph` grezzo.
- Registry/resolver/sampler/cache duplicati.
- `GraphExecutor` costruiti dentro i nodi.
- Globali process-wide per stato di rendering.
