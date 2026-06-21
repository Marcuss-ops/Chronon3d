# Chronon3D refactor roadmap

Stato: [`../STATUS.md`](../STATUS.md). Piano operativo:
[`../NEXT_STEPS.md`](../NEXT_STEPS.md).

## Ordine attivo

1. WP0 — gate di validazione.
2. WP1 — determinismo scheduler sul contratto compilato.
3. WP5 — Precomp e `SceneProgramStore`.
4. WP4 — identità stabile senza race.
5. WP6 — `ExecutionScope` root/tile/precomp.
6. WP3 — chiusura del confine sessione.
7. WP8 — globali e SDK.
8. WP7 — separazione finale software backend.

## Blocker correnti

| Package | Stato | Criterio di chiusura |
|---|---|---|
| WP0 | 🔴 | Ogni regola può far fallire lo script; fixture negativa presente |
| WP1 | 🔴 | Nessuna API plan-cache/raw graph; test seriale/parallelo/tile verdi |
| WP5 | 🔴 | Header/impl coerenti, lease bloccata, executor di sessione |
| WP4 | 🔴 | Identità assegnata solo sul contesto clonato |
| WP6 | 🔵 | Child scope non resetta arena o stato parent |
| WP3 | 🟡 | Nessuna dipendenza implementation-only nel contratto pubblico |
| WP8 | 🟡 | Install consumer verde; bridge globali rimossi |
| WP7 | 🔵 | Backend separato senza duplicare facade o ownership |

## Regole

- Un problema e un set di file per PR.
- Test mirati obbligatori.
- Nessun nuovo registry, resolver, sampler o cache parallelo.
- Nessun executor costruito dentro un nodo.
- Nessuna nuova architettura prima della chiusura P0.
- Aggiornare `STATUS.md`, `ROADMAP.md` e ticket nello stesso merge che cambia stato.

## Target

```text
RenderRuntime: servizi engine-lifetime
RenderSession: stato job-owned
ExecutionScope: root/tile/precomp, parent, arena, identità
SceneProgramStore: lease identity-keyed e bloccata
GraphExecutor: stateless, compiled graph, scheduler/scope espliciti
```
