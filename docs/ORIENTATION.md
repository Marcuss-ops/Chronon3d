# ORIENTATION — Chronon3D

Chronon3D è un motore motion-graphics code-first, headless e CPU-only in C++20.

- Stato: [`STATUS.md`](STATUS.md)
- Prossimi passi: [`NEXT_STEPS.md`](NEXT_STEPS.md)
- Roadmap: [`ROADMAP.md`](ROADMAP.md)
- Incarichi correnti: [`agent-tasks/`](agent-tasks/)

## Architettura corrente

`Composition → Scene → RenderGraph → FrameGraphCompiler → CompiledFrameGraph → GraphExecutor → RenderBackend → output`

`ExecutionPlanCache` e gli overload executor su `RenderGraph` grezzo sono ritirati. Il repository resta pre-stabile finché i blocker P0 non sono chiusi.

Il modello da preservare è:

- `SoftwareRenderer`: facade e orchestrazione;
- `RenderRuntime`: ownership engine-lifetime;
- `RenderSession`: stato per-job/per-sessione;
- `SoftwareBackend`: implementazione software di `graph::RenderBackend`;
- `GraphExecutor`: esecuzione del grafo compilato attraverso il backend astratto.

Lo stato corrente del confine renderer/backend è in riallineamento. Non usare la doppia identità o cast al renderer concreto come modello per nuovo codice.

## Lavoro attivo

1. [`Agente 1 — Renderer/Backend Single Identity`](agent-tasks/AGENT_1_RENDERER_BOUNDARY.md)
2. [`Agente 2 — CMake Registry, SDK Boundary e Baseline`](agent-tasks/AGENT_2_CMAKE_SDK_BASELINE.md)

Il secondo stream deve essere validato dopo il merge e il rebase del primo.

## Regole

- Registrazione tramite `ExtensionModule` e `ExtensionContext`.
- Nessun registry, resolver, sampler, cache o service locator parallelo.
- Nessun executor costruito dentro i nodi.
- Le composizioni cliente vivono in pack esterni.
- `experimental/` non appartiene allo SDK stabile.
- Nessun claim verde senza build, test e gate registrati sul commit corrente.
- Non cambiare un gate per nascondere un errore del codice.

## Build rapido

```bash
bash tools/chronon-linux.sh
./build-fast.sh
./build-fast.sh test '<pattern>'
```

Il build rapido è utile per iterazione, ma non certifica da solo la baseline.

## Validazione

Ogni lavoro deve chiudersi con build mirata, test e gate architetturale. Quando applicabile servono anche no-content build, install consumer esterno, full-validation e CI registrata.

Expressions V2 resta sotto `experimental/expressions/`, OFF di default e fuori da `Chronon3D::SDK`. V3 resta pianificato e non deve partire prima della chiusura P0.

## Closed-state baseline

TXT-00 chiuso su `main@345e5f2e` (audit-pinned al commit `b8114705` su `origin/main`). Per i log completi osservati (`cmake --preset linux-ci` / build / `ctest -R '^VRTextPresetVisual$'` rc=0; 18/18 doctest cases + 263/263 assertions; chiusura TICKET‑038 + TICKET‑039; matrice di trasparenza 128 sentinels su 16 preset × 8 frame) vedere:

[`baselines/main-345e5f2e-txt-00-closed.md`](baselines/main-345e5f2e-txt-00-closed.md).

Il file conserva ancora la baseline storica di pre‑chiusura `baselines/main-ccabb574-txt-00-build-green.md` come riferimento dell'attività precedente dell'Agent 2 (merge della rot linker strutturale) ma lo stato autorevole corrente è quello del file `345e5f2e` indicato sopra.