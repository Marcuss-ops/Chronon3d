# Chronon3D — Current Status

> Stato documentale aggiornato il **2026-06-22**.
>
> Questo documento non certifica una baseline verde. Build, test, gate e consumer devono essere verificati da checkout pulito sul commit che verrà candidato alla chiusura.
>
> Piano operativo: [`NEXT_STEPS.md`](NEXT_STEPS.md).
> Incarichi correnti: [`agent-tasks/`](agent-tasks/).

Chronon3D è avanzato ma **pre-stabile**. `main` non deve essere descritto come completamente verde, release-ready o SDK stabile finché i blocker P0 non sono chiusi e verificati.

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

## Contraddizione P0 immediata

Il confine software corrente non è coerente:

- il gate richiede che `SoftwareRenderer` non erediti `graph::RenderBackend`;
- il gate vieta `dynamic_cast<SoftwareRenderer*>`;
- l'header corrente dichiara la doppia ereditarietà;
- lo step CI relativo usa ancora `continue-on-error: true`.

La soluzione richiesta è correggere il codice verso una singola identità, non rilassare il gate.

## Fondazioni valide da preservare

- `RenderGraph → FrameGraphCompiler → CompiledFrameGraph → GraphExecutor`.
- Executor su `RenderGraph` grezzo ritirati.
- Scheduler esplicito.
- ID forti e hashing deterministico.
- Stato per-sessione per `SceneHasher`, `SceneProgramStore`, frame history e dirty history.
- `AssetResolver` tipizzato e runtime-owned.
- Registrazione esplicita tramite `ExtensionModule` e `ExtensionContext`.
- `RenderRuntime` come proprietario dell'infrastruttura engine-lifetime.
- `experimental/` escluso dallo SDK stabile.

## Esecuzione corrente a due agenti

### Agente 1

[`AGENT_1_RENDERER_BOUNDARY.md`](agent-tasks/AGENT_1_RENDERER_BOUNDARY.md)

Chiude:

- single identity renderer/backend;
- eliminazione dei cast al renderer concreto;
- capability sul backend software;
- riduzione della superficie del renderer;
- gate renderer bloccante.

### Agente 2

[`AGENT_2_CMAKE_SDK_BASELINE.md`](agent-tasks/AGENT_2_CMAKE_SDK_BASELINE.md)

Chiude:

- registry CMake centrale;
- text-preset registry cleanup sub-PR: TextPresetRegistry confirmed single source of truth (Stage-5 canonical path: `wire_preset_text_run_params(preset_id, spec)` + `AnimatorResolver::compose_for(preset_id)`); freeze() mirrored at PR-A4 fixture (EffectCatalog parity); zero local preset-construction fallbacks outside the registry TU.
- toolchain/preset vcpkg coerenti;
- external consumer SDK;
- baseline osservata e documenti canonici sincronizzati.

## Ordine di integrazione

1. Merge del lavoro Agente 1 dopo review e gate mirati verdi.
2. Rebase del lavoro Agente 2 su `origin/main` aggiornato.
3. Full validation, install consumer e aggiornamento definitivo dei claim.
4. Solo dopo, rivalutazione dei gap residui su Precomp, determinismo, content e diagnostics.

## Criterio per dichiarare una baseline verde

Servono tutte le prove seguenti sullo stesso commit:

- build core verde;
- test core verdi;
- build lean verde;
- test lean verdi;
- architecture gate verde;
- renderer boundary gate verde e bloccante;
- no-content build/test verde;
- install consumer esterno verde;
- full-validation build/test verde;
- documenti aggiornati con commit, comandi e risultati.

L'assenza di workflow run o di log verificabili non equivale a successo.

## Ruolo dei documenti

- `STATUS.md`: stato corrente, senza claim non verificati.
- `NEXT_STEPS.md`: ordine operativo e coordinamento.
- `agent-tasks/`: incarichi eseguibili dai due agenti.
- `stabilization-plan/`: checklist dei work package.
- `ROADMAP.md`: backlog ordinato.
- `FOLLOWUP_TICKETS.md`: difetti e follow-up tracciati.
- `CHANGELOG.md`: lavoro completato e verificato.
- `V3_BLUEPRINT.md`: architettura futura, non runtime corrente.