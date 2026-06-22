# Chronon3D — Next Steps Reali

Piano operativo per chiudere lo stato pre-stabile del repository.

> Punto di ingresso per agenti: [`../AGENTS.md`](../AGENTS.md)  
> Checklist operative: [`stabilization-plan/README.md`](stabilization-plan/README.md)

## Ordine obbligatorio

1. Correggere le false dichiarazioni di baseline verde.
2. Risolvere i 3 test falliti della baseline core.
3. Risolvere le 2 violazioni architetturali rilevate dai test di boundary.
4. Riparare il preset `linux-lean-dev`.
5. Riparare il consumer SDK e il passaggio della toolchain/prefix vcpkg.
6. Catturare i golden hash reali ed eliminare i sentinel.
7. Sincronizzare il piano ExecutionScope con quanto già implementato.
8. Completare la migrazione scope di Precomp e dei call site legacy.
9. Rendere canonici i documenti ed eliminare i duplicati.
10. Avviare il registry centrale dei moduli CMake.

## Work package

- [Baseline](stabilization-plan/01-baseline-green.md)
- [Determinismo](stabilization-plan/02-determinism.md)
- [ExecutionScope e Precomp](stabilization-plan/03-execution-scope-and-precomp.md)
- [Registry CMake](stabilization-plan/04-cmake-module-registry.md)
- [SDK consumer](stabilization-plan/05-sdk-plan.md)
- [SoftwareRenderer](stabilization-plan/06-renderer-plan.md)
- [Documentazione e ADR](stabilization-plan/07-documentation-and-adrs.md)
- [Profili dipendenze](stabilization-plan/08-dependency-profiles.md)
- [Canonicalizzazione](stabilization-plan/09-document-canonicalization.md)

## Dopo il P0

- TICKET-002: riparare diagnostics/content a piccoli blocchi, registrando il numero di errori residui dopo ogni modifica.
- TICKET-006: verificare il build `linux-ci` di `chronon3d_renderer_tests`, quindi uniformare lo stile dei guard CMake.
- TICKET-005: decidere esplicitamente se ripristinare o rimuovere `keyframes()`, poi aggiornare test e documentazione.
- TICKET-008: verificare il riuso del compiler con hash, fallback, test e benchmark prima della chiusura definitiva.
- Expressions V2: completare i gate senza rimuovere anticipatamente la quarantena.

## Cosa non rifare

- Non reintrodurre `ExecutionPlanCache`.
- Non reintrodurre executor su `RenderGraph` grezzo.
- Non creare registry, resolver, sampler o cache paralleli.
- Non costruire `GraphExecutor` dentro i nodi.
- Non iniziare V3 prima della chiusura P0.
- Non promuovere Expressions V2 solo perché compila in modalità opt-in.

## Criteri di chiusura

| Area | Prova richiesta |
|---|---|
| Baseline | build obbligatorie e test senza failure |
| Gate architettura | repository pulito verde e test di regressione verde |
| Scheduler | golden numerici e test seriale/parallelo/tile ripetuti verdi |
| Precomp | test cache, lease, nested execution e concorrenza verdi |
| Scope | child execution non invalida arena o stato del parent |
| SDK | install, configure, build e run di un consumer esterno |
| CMake | un solo registry da cui derivano build, install ed export |
| CI | esito richiesto registrato sul commit di chiusura |
| Documenti | una sola fonte canonica per area |
