# Chronon3D Stabilization Plan

Piano operativo per portare il repository da pre-stabile a baseline verificata.

## Documenti

1. [Baseline verde](01-baseline-green.md)
2. [Determinismo](02-determinism.md)
3. [Execution scope e Precomp](03-execution-scope-and-precomp.md)
4. [Registry moduli CMake](04-cmake-module-registry.md)
5. [Confine SDK](05-sdk-plan.md)
6. [SoftwareRenderer](06-renderer-plan.md)
7. [Documentazione e ADR](07-documentation-and-adrs.md)
8. [Profili dipendenze](08-dependency-profiles.md)

## TODO generale

- [ ] Build core, lean e no-content verdi.
- [ ] Test veloci verdi.
- [ ] Determinismo seriale, parallelo e tile verificato.
- [ ] Scope root, tile e precomp espliciti.
- [ ] Moduli CMake registrati una volta sola.
- [ ] Un solo consumer SDK esterno.
- [ ] SoftwareRenderer alleggerito.
- [ ] Stato e roadmap sincronizzati.

## Regola di chiusura

Un punto può essere segnato come completato solo quando codice, test e documentazione riportano lo stesso stato e la validazione è ripetibile da un checkout pulito.
