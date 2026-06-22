# Chronon3D — Next Steps Reali

Piano operativo per portare il repository da pre-stabile a baseline verificata senza nascondere failure o duplicare architetture.

> Punto di ingresso per agenti: [`../AGENTS.md`](../AGENTS.md)  
> Checklist operative: [`stabilization-plan/README.md`](stabilization-plan/README.md)  
> Incarichi correnti: [`agent-tasks/`](agent-tasks/)

## Strategia corrente

Il lavoro è diviso in due stream con ownership non sovrapposta.

| Stream | Branch | Ownership |
|---|---|---|
| Agente 1 | `codex/agent1-renderer-boundary` | Renderer/backend software, cast, processor context, capability e gate renderer |
| Agente 2 | `codex/agent2-cmake-sdk-baseline` | Registry CMake, vcpkg/preset, install consumer, full validation e documenti canonici |

Gli agenti possono iniziare in parallelo. L'Agente 2 deve però fare rebase sul `main` aggiornato dopo il merge dell'Agente 1 prima di registrare la baseline finale.

## Fase A — Agente 1: confine renderer/backend

Documento operativo: [`AGENT_1_RENDERER_BOUNDARY.md`](agent-tasks/AGENT_1_RENDERER_BOUNDARY.md)

Ordine obbligatorio:

1. Inventariare cast e dipendenze dal renderer concreto.
2. Rimuovere la doppia identità `SoftwareRenderer : Renderer + RenderBackend`.
3. Rendere `SoftwareBackend` l'unica implementazione software di `graph::RenderBackend`.
4. Instradare capability e servizi attraverso backend/runtime canonici.
5. Eliminare `dynamic_cast<SoftwareRenderer*>` e `SoftwareRenderer&` dalle superfici di processo controllate.
6. Ridurre `software_renderer.hpp` senza creare nuovi bridge paralleli.
7. Far passare tutte le invarianti del boundary gate.
8. Rendere il gate CI bloccante.
9. Validare core e lean.

## Fase B — Agente 2: build, SDK e baseline

Documento operativo: [`AGENT_2_CMAKE_SDK_BASELINE.md`](agent-tasks/AGENT_2_CMAKE_SDK_BASELINE.md)

Ordine obbligatorio:

1. Inventariare tutte le OBJECT library e le liste duplicate.
2. Introdurre un registry CMake centrale.
3. Derivare build, aggregazione, install ed export dalla stessa fonte.
4. Uniformare root vcpkg, toolchain e preset/workflow.
5. Chiudere il consumer esterno su profili core, text e no-content.
6. Fare rebase sul `main` contenente il lavoro dell'Agente 1.
7. Eseguire core, lean, no-content, install consumer e full-validation.
8. Aggiornare i documenti soltanto con risultati osservati.

## Ordine di merge

1. Review e merge Agente 1.
2. `git fetch origin` e `git rebase origin/main` sul branch Agente 2.
3. Riesecuzione completa della validazione da parte dell'Agente 2.
4. Review e merge Agente 2.
5. Controllo del commit finale e dei workflow associati.

## File ownership per evitare conflitti

### Agente 1

- `include/chronon3d/backends/software/`
- `src/backends/software/`
- runtime e render-graph soltanto per la migrazione del confine
- test renderer/backend mirati
- `tools/check_software_renderer_boundary.sh`
- step renderer in `.github/workflows/gates.yml`

### Agente 2

- root e moduli CMake
- `cmake/`
- `CMakePresets.json`
- `vcpkg.json`
- install consumer e relativi script/workflow
- `docs/STATUS.md`
- `docs/NEXT_STEPS.md`
- `docs/ROADMAP.md`
- `docs/stabilization-plan/`

## Cosa non rifare

- Non reintrodurre `ExecutionPlanCache`.
- Non reintrodurre executor su `RenderGraph` grezzo.
- Non creare registry, resolver, sampler, cache o service locator paralleli.
- Non costruire `GraphExecutor` dentro i nodi.
- Non rendere verde un gate modificandone le soglie per adattarlo al codice rotto.
- Non disabilitare o saltare test per ottenere una baseline verde.
- Non iniziare V3 prima della chiusura dei P0.
- Non promuovere Expressions V2 solo perché compila in modalità opt-in.

## Lavori successivi alla nuova baseline

Dopo il merge dei due stream, rivalutare sul commit verificato:

1. ExecutionScope root/tile/precomp e child arena.
2. Precomp lease, nested execution e concorrenza.
3. Riuso del compiled graph con identità per-node corretta.
4. Diagnostics/content, divisi in ticket piccoli in base agli errori reali residui.
5. Determinismo seriale/parallelo/tile e golden hash.
6. Canonicalizzazione dei documenti ancora duplicati.
7. Strategia text/kinetic typography post-P0: [`TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`](TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md).

Non mantenere automaticamente ticket storici se la nuova baseline dimostra che sono già chiusi; aggiornarli con prova verificabile.

## Criteri di chiusura

| Area | Prova richiesta |
|---|---|
| Renderer/backend | single identity, nessun cast vietato, capability corrette |
| Gate renderer | exit zero e step CI bloccante |
| CMake | un solo registry da cui derivano build, install ed export |
| Toolchain | preset locali e CI usano lo stesso contratto vcpkg |
| SDK | install, configure, build e run di consumer esterni |
| Baseline | core, lean, no-content e full validation registrati sullo stesso commit |
| Documenti | `STATUS`, `NEXT_STEPS`, `ROADMAP` e stabilization plan coerenti |
| Git | branch aggiornati, PR mirate, cronologia verificata dopo push |

Un'attività è completata soltanto quando codice, test, gate e documenti riportano lo stesso stato.