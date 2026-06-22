# Chronon3D — Current Status

> Code baseline audited on **2026-06-21** at
> `591f8e1ea0793902684389b97d1e509aae455533`.
>
> Piano operativo: [`NEXT_STEPS.md`](NEXT_STEPS.md).

Chronon3D è avanzato ma **pre-stabile**. `main` non va descritto come completamente verde, release-ready o SDK stabile finché i blocker P0 non sono chiusi e verificati.

## Stato reale

| Area | Stato | Gap principale |
|---|---|---|
| Render graph compilato | 🟡 Parziale | Fondazione completata; restano test e chiamanti sulle API eliminate. |
| Gate architetturali | 🔴 Bloccato | L’ultimo controllo può fallire senza exit code non-zero. |
| Determinismo scheduler | 🔴 Bloccato | Test ancora legati a `ExecutionPlanCache` e raw graph. |
| Precomp annidato | 🔴 Bloccato | Header e implementazione non condividono lo stesso contratto. |
| Identità/sessione | 🟡 Parziale | Race sul contesto condiviso e scope annidati incompleti. |
| Confine SDK | 🟢 Completato | Registry centrale CMake creato; consumer installato richiede fix transitive deps (ZLIB). |
| Diagnostics/content | 🔴 Da riparare | Target con API rotte da dividere in fix piccoli e verificabili. |
| V3 tile-first | 🔵 Pianificato | Non deve partire prima della chiusura P0. |
| Expressions V2 | 🧪 Sperimentale | In quarantena; non installato né esportato. |

## Fondazioni completate

- `RenderGraph → FrameGraphCompiler → CompiledFrameGraph → GraphExecutor`.
- Overload executor su `RenderGraph` grezzo rimossi.
- `ExecutionPlanCache` rimosso.
- Scheduler esplicito.
- ID forti e hashing deterministico.
- Lavoro su stato per-sessione e `SceneProgramStore`.
- `AssetResolver` tipizzato e runtime-owned.
- Registrazione esplicita tramite `ExtensionModule` e `ExtensionContext`.

## Priorità immediata

1. Riparare il gate architetturale.
2. Rifare i test scheduler con il contratto compilato corrente.
3. Sistemare `PrecompNode`, lease e ownership executor.
4. Eliminare la race su `current_identity`.
5. Introdurre `ExecutionScope` root/tile/precomp.
6. Chiudere install consumer e confine SDK.
7. Registrare una validazione completa e ripetibile.

## Expressions V2

Percorso reale: `experimental/expressions/`.

- Build gate: `CHRONON3D_BUILD_EXPERIMENTAL=ON`.
- Default: `OFF`.
- Non installato da `cmake --install`.
- Non collegato da `Chronon3D::SDK`.
- `TICKET-003` e `TICKET-004` sono fix storici chiusi.
- La promozione richiede tutti gli otto gate in
  [`EXPRESSIONS_V2_PROMOTION.md`](EXPRESSIONS_V2_PROMOTION.md).

## Ruolo dei documenti

- `STATUS.md`: stato corrente.
- `NEXT_STEPS.md`: ordine operativo e criteri di chiusura.
- `refactor-roadmap/`: dettagli dei work package architetturali.
- `ROADMAP.md`: backlog ordinato.
- `FOLLOWUP_TICKETS.md`: difetti e follow-up tracciati.
- `CHANGELOG.md`: lavoro completato e verificato.
- `V3_BLUEPRINT.md`: architettura futura, non runtime corrente.
