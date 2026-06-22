# Chronon3D — Active Roadmap

Stato: [`STATUS.md`](STATUS.md). Piano: [`NEXT_STEPS.md`](NEXT_STEPS.md). Incarichi: [`agent-tasks/`](agent-tasks/).

## P0 — Stabilizzazione immediata

1. **Single identity renderer/backend**
   - `SoftwareRenderer` torna facade/orchestratore.
   - `SoftwareBackend` resta l'implementazione di `graph::RenderBackend`.
   - nessun `dynamic_cast<SoftwareRenderer*>` nelle superfici controllate.
2. **Gate renderer affidabile e bloccante**
   - invarianti I1–I5 verdi;
   - rimozione di `continue-on-error` soltanto dopo exit zero reale.
3. **Registry CMake centrale**
   - una sola registrazione per ogni OBJECT library;
   - build, SDK, install ed export derivati dalla stessa fonte.
4. **Toolchain e preset coerenti**
   - una sola root vcpkg;
   - stesso contratto tra locale e GitHub Actions.
5. **SDK installabile con consumer esterno**
   - profili core/no-text, text+Blend2D e no-content;
   - configure, build e run fuori dalla source tree.
6. **Baseline verificata sullo stesso commit**
   - core;
   - lean;
   - no-content;
   - architecture gates;
   - install consumer;
   - full-validation.
7. **Documentazione canonica sincronizzata**
   - nessun claim verde senza log e commit;
   - `STATUS`, `NEXT_STEPS`, `ROADMAP` e stabilization plan coerenti.

## P0 successivo alla nuova baseline

Questi lavori restano prioritari, ma devono essere ricalibrati dopo la baseline prodotta dai due agenti:

1. Scheduler e determinismo sul contratto compilato corrente.
2. `PrecompNode`, lease e `SceneProgramStore` coerenti.
3. Identità per-node senza race.
4. Scope root, tile e precomp con child arena sicura.
5. Riuso del compiled graph senza metadata identity-sensitive obsoleti.
6. Diagnostics/content divisi in fix piccoli sulla base degli errori realmente residui.

## Assegnazione corrente

- [`Agente 1 — Renderer Boundary`](agent-tasks/AGENT_1_RENDERER_BOUNDARY.md)
- [`Agente 2 — CMake, SDK e Baseline`](agent-tasks/AGENT_2_CMAKE_SDK_BASELINE.md)

Ordine di integrazione: Agente 1, rebase Agente 2, validazione completa, merge Agente 2.

## Ticket successivi

- TICKET-005: decidere restore o rimozione di `keyframes()`.
- TICKET-008: completare la sicurezza del riuso compiler con hash, fallback, test e benchmark.
- TICKET-EXP2-G3: migrare Path A verso Path B con un solo parser/VM produttivo.
- Diagnostics/content: aggiornare o creare ticket soltanto dopo il nuovo full-validation, evitando conteggi storici non riconfermati.

## Vincoli

Non reintrodurre `ExecutionPlanCache`, executor raw graph, duplicati di registry/resolver/cache, service locator paralleli o executor locali nei nodi. Non indebolire gate per adattarli al codice. Non iniziare V3 prima della chiusura completa dei P0.

## Roadmap testuale post-P0

Per la strategia 13-fasi sul testo/cinetica tipografica, inclusi kinetic typography, titoli cinematici, sottotitoli animati, text-on-path, variable fonts, ICU, MSDF e Text 3D, vedere [`TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`](TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md).

Questa roadmap si apre soltanto dopo la chiusura di P0 e applica le regole canoniche di [`stabilization-plan/09-document-canonicalization.md`](stabilization-plan/09-document-canonicalization.md).