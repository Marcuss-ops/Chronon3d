# Chronon3D Stabilization Plan

Piano operativo per portare il repository da pre-stabile a baseline verificata.

## Documenti

I documenti nel `stabilization-plan/` sono checklist operative brevi che rimandano alla fonte canonica del dettaglio in `docs/`. Ogni work package deve avere una sola fonte autorevole.

1. [Baseline verde](01-baseline-green.md) → dettaglio: [`docs/01-baseline-green.md`](../01-baseline-green.md)
2. [Determinismo](02-determinism.md) → dettaglio: [`docs/02-determinism.md`](../02-determinism.md)
3. [Execution scope e Precomp](03-execution-scope-and-precomp.md) → dettaglio: [`docs/03-execution-scope-and-precomp.md`](../03-execution-scope-and-precomp.md)
4. [Registry moduli CMake](04-cmake-module-registry.md)
5. [Confine SDK](05-sdk-plan.md)
6. [SoftwareRenderer](06-renderer-plan.md)
7. [Documentazione e ADR](07-documentation-and-adrs.md)
8. [Profili dipendenze](08-dependency-profiles.md)
9. [Canonicalizzazione documenti](09-document-canonicalization.md)

I work package 04–09 non hanno duplicati in `docs/` e sono essi stessi fonte canonica.

## Esecuzione corrente

Il lavoro attivo è diviso in due incarichi eseguibili:

- [Agente 1 — Renderer/Backend Single Identity](../agent-tasks/AGENT_1_RENDERER_BOUNDARY.md)
- [Agente 2 — CMake Registry, SDK Boundary e Baseline](../agent-tasks/AGENT_2_CMAKE_SDK_BASELINE.md)

I due agenti possono iniziare in parallelo, ma il merge e la validazione devono seguire questo ordine:

1. chiusura e merge del confine renderer/backend;
2. rebase del branch CMake/SDK su `origin/main` aggiornato;
3. full validation e aggiornamento definitivo dei documenti;
4. merge del lavoro CMake/SDK.

## Priorità operative attuali

### Stream Agente 1

1. [ ] Rimuovere la doppia identità di `SoftwareRenderer`.
2. [ ] Rendere `SoftwareBackend` l'unico backend software canonico.
3. [ ] Eliminare `dynamic_cast<SoftwareRenderer*>` dalle superfici controllate.
4. [ ] Migrare i processori fuori da `SoftwareRenderer&`.
5. [ ] Portare `software_renderer.hpp` entro le soglie del boundary gate.
6. [ ] Rendere il boundary gate verde e bloccante in CI.
7. [ ] Verificare core e lean.

### Stream Agente 2

1. [ ] Inventariare tutte le OBJECT library.
2. [ ] Introdurre un registry CMake centrale.
3. [ ] Derivare aggregazione, SDK, install ed export dalla stessa fonte.
4. [ ] Uniformare root vcpkg, toolchain, preset e workflow.
5. [ ] Verificare il consumer SDK esterno su core, text e no-content.
6. [ ] Eseguire rebase dopo il merge dell'Agente 1.
7. [ ] Eseguire core, lean, no-content e full-validation sullo stesso commit.
8. [ ] Aggiornare i documenti canonici con risultati osservati.

## Stato sintetico non attestato

Fino alla nuova validazione, non usare conteggi storici o vecchi claim verdi come prova dello stato corrente.

- [ ] Renderer/backend single identity verificata.
- [ ] Boundary gate verde e bloccante.
- [ ] Build e test `linux-core-dev` riconfermati sul commit finale.
- [ ] Build e test `linux-lean-dev` riconfermati sul commit finale.
- [ ] Build e test no-content riconfermati sul commit finale.
- [ ] External install consumer verde.
- [ ] Full-validation verde.
- [ ] Registry CMake centrale.
- [ ] Toolchain vcpkg unificata.
- [x] Fondazione ExecutionScope presente nel codice.
- [ ] Integrazione root/tile/precomp riconfermata dalla nuova baseline.
- [ ] Documentazione canonica coerente con il commit verificato.

## Regole anti-falso-verde

- Non trasformare failure in skip.
- Non ridurre le soglie di un gate per adattarle a una regressione.
- Non dichiarare verde una build che non è stata eseguita sul commit corrente.
- Non usare il successo della sola compilazione come prova che i test passino.
- Non usare il successo dei test in-tree come prova che l'SDK installato funzioni.
- Non aggiornare `CHANGELOG.md` come completato prima delle prove richieste.

## Regola di chiusura

Un punto può essere segnato come completato solo quando codice, test, gate e documentazione riportano lo stesso stato e la validazione è ripetibile da un checkout pulito. Un errore pre-esistente resta un errore: non rende verde una suite fallita.