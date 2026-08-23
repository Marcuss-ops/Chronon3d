# ORIENTATION — Chronon3D

Chronon3D è un motore motion-graphics code-first, headless e CPU-only in C++20.

- Stato: [`CURRENT_STATUS.md`](CURRENT_STATUS.md)
- Prossimi passi: [`ROADMAP.md`](ROADMAP.md)
- Roadmap: [`ROADMAP.md`](ROADMAP.md)
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