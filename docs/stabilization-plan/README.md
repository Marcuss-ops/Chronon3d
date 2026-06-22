# Chronon3D Stabilization Plan

Piano operativo per portare il repository da pre-stabile a baseline verificata.

## Documenti

I documenti nel `stabilization-plan/` sono **checklist operative brevi** che rimandano alla fonte canonica del dettaglio in `docs/`. Ogni work package ha una sola fonte autorevole.

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

## Priorità operative attuali

L'ordine seguente è obbligatorio. Non avviare il punto successivo finché il precedente non è chiuso o isolato con ticket verificabile.

1. [ ] Correggere le false dichiarazioni di baseline verde.
2. [ ] Risolvere i 3 test falliti della baseline core.
3. [ ] Risolvere le 2 violazioni architetturali rilevate dai test di boundary.
4. [ ] Riparare il preset `linux-lean-dev`.
5. [ ] Riparare il consumer SDK e il passaggio della toolchain/prefix vcpkg.
6. [ ] Catturare i golden hash reali ed eliminare i sentinel.
7. [ ] Sincronizzare il piano ExecutionScope con quanto già implementato.
8. [ ] Completare la migrazione scope di Precomp e dei call site legacy.
9. [ ] Rendere canonici i documenti ed eliminare i duplicati.
10. [ ] Avviare il registry centrale dei moduli CMake.

## Stato reale sintetico

- [x] Build `linux-core-dev` verde.
- [x] Build `linux-lean` verde.
- [ ] Build `linux-lean-dev` verde.
- [ ] Test veloci verdi: stato attuale 704/707, con 3 failure.
- [ ] Gate architetturali verdi: restano 2 violazioni.
- [ ] Consumer SDK esterno verde.
- [ ] Golden hash numerici acquisiti.
- [x] Fondazione ExecutionScope presente.
- [ ] Migrazione completa root/tile/precomp e call site.
- [ ] Registry CMake centrale.
- [ ] Documentazione canonica senza doppioni.

## Regola di chiusura

Un punto può essere segnato come completato solo quando codice, test e documentazione riportano lo stesso stato e la validazione è ripetibile da un checkout pulito. Un errore pre-esistente resta un errore: non rende verde una suite fallita.
