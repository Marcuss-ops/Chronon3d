# ORIENTATION — Chronon3D

Chronon3D è un motore motion-graphics code-first, headless e CPU-only in C++20.

- Stato: [`STATUS.md`](STATUS.md)
- Prossimi passi: [`NEXT_STEPS.md`](NEXT_STEPS.md)
- Roadmap: [`ROADMAP.md`](ROADMAP.md)

## Architettura corrente

`Composition → Scene → RenderGraph → FrameGraphCompiler → CompiledFrameGraph → GraphExecutor → RenderBackend → output`

`ExecutionPlanCache` e gli overload executor su `RenderGraph` grezzo sono ritirati. Il repository resta pre-stabile finché i blocker P0 non sono chiusi.

## Regole

- Registrazione tramite `ExtensionModule` e `ExtensionContext`.
- Nessun registry, resolver, sampler o cache parallelo.
- Nessun executor costruito dentro i nodi.
- Le composizioni cliente vivono in pack esterni.
- `experimental/` non appartiene allo SDK stabile.

## Build rapido

```bash
bash tools/chronon-linux.sh
./build-fast.sh
./build-fast.sh test '<pattern>'
```

## Validazione

Ogni lavoro deve chiudersi con build mirata, test, gate architetturale e, quando applicabile, no-content build, install consumer e CI registrata.

Expressions V2 resta sotto `experimental/expressions/`, OFF di default e fuori da `Chronon3D::SDK`. V3 resta pianificato e non deve partire prima della chiusura P0.
