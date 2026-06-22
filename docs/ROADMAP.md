# Chronon3D — Active Roadmap

Stato: [`STATUS.md`](STATUS.md). Piano: [`NEXT_STEPS.md`](NEXT_STEPS.md).

## P0

1. Gate architetturale affidabile.
2. Test scheduler sul contratto compilato.
3. `PrecompNode` e `SceneProgramStore` coerenti.
4. Identità per-node senza race.
5. Scope root, tile e precomp.
6. SDK installabile con consumer esterno.
7. Build, test e CI registrati.

## Ticket successivi

- TICKET-002: diagnostics/content, da riparare in PR piccole.
- TICKET-006: fix di linkage atterrato; manca build `linux-ci` osservato di `chronon3d_renderer_tests` e uniformazione guard CMake.
- TICKET-005: decidere restore o rimozione di `keyframes()`.
- TICKET-008: implementazione del riuso compiler atterrata; verificare hash, fallback, test e benchmark.
- TICKET-EXP2-G3: migrare Path A verso Path B con un solo parser/VM produttivo.

## Vincoli

Non reintrodurre `ExecutionPlanCache`, executor raw graph, duplicati di registry/resolver/cache o executor locali nei nodi. Non iniziare V3 prima della chiusura P0.

## Roadmap testuale post-P0

Per la strategia 13-fasi sul testo/cinetica tipografica (kinetic typography, titoli cinematici, sottotitoli animati, text-on-path, variable fonts, ICU, MSDF, Text 3D) vedi [`docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`](TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md). Si apre dopo la chiusura di P0 e applica le stesse regole canon di [`docs/stabilization-plan/09-document-canonicalization.md`](stabilization-plan/09-document-canonicalization.md).
