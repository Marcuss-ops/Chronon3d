# Chronon3D

Code-first, headless, CPU-only motion graphics engine in C++20.

> **Stato:** `main` è pre-stabile. Leggere [`AGENTS.md`](AGENTS.md),
> [`docs/STATUS.md`](docs/STATUS.md),
> [`docs/NEXT_STEPS.md`](docs/NEXT_STEPS.md) e
> [`docs/stabilization-plan/README.md`](docs/stabilization-plan/README.md)
> prima di modificare il repository o usarlo come SDK stabile.

## Quick start

```bash
bash tools/chronon-linux.sh
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli list
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli \
  render BackgroundGrid --frame 0 -o output/test.png
```

Build incrementali: [`docs/FAST_BUILD.md`](docs/FAST_BUILD.md).

## Architettura corrente

```text
Composition
  → Scene
  → RenderGraph
  → FrameGraphCompiler
  → CompiledFrameGraph
  → GraphExecutor
  → RenderBackend
  → output
```

Gli overload executor su `RenderGraph` grezzo e `ExecutionPlanCache` sono stati rimossi. Restano aperti gate di validazione, test scheduler, Precomp, execution scope e confine SDK.

## Registrare una composizione

La registrazione statica e `CHRONON_REGISTER_COMPOSITION(...)` sono ritirate.
Usare `ExtensionModule` e `ExtensionContext`:

```cpp
#include <chronon3d/extension/extension_context.hpp>
#include <chronon3d/extension/extension_module.hpp>

class MyModule final : public chronon3d::ExtensionModule {
public:
    std::string_view name() const override { return "my_module"; }

    void register_all(chronon3d::ExtensionContext& ctx) override {
        ctx.compositions.add("MyComp", [] {
            return make_my_comp();
        });
    }
};
```

Le composizioni di progetto devono vivere in pack esterni, non nel core engine.

## Stato delle aree principali

| Area | Stato |
|---|---|
| Render graph compilato | Fondazione completata; migrazioni/test da chiudere |
| Text, shape, image, effects | Implementati con limiti documentati |
| Software backend split | In corso |
| SDK installabile | In corso |
| V3 tile-first | Pianificato |
| Expressions V2 | Sperimentale in `experimental/expressions/` |

Expressions V2 è OFF di default, non viene installato e non è collegato da `Chronon3D::SDK`. L’opt-in usa `CHRONON3D_BUILD_EXPERIMENTAL=ON`.

## Documenti principali

- [`AGENTS.md`](AGENTS.md) — istruzioni e percorsi obbligatori per agenti.
- [`docs/stabilization-plan/README.md`](docs/stabilization-plan/README.md) — priorità operative e work package correnti.
- [`docs/STATUS.md`](docs/STATUS.md) — stato reale e blocker.
- [`docs/NEXT_STEPS.md`](docs/NEXT_STEPS.md) — cosa sistemare e criteri di chiusura.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — backlog ordinato.
- [`docs/refactor-roadmap/README.md`](docs/refactor-roadmap/README.md) — work package architetturali.
- [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) — ticket aperti e storici.
- [`docs/FEATURES.md`](docs/FEATURES.md) — feature e limiti.
- [`docs/ARCHITECTURE_EVOLUTION_PLAN.md`](docs/ARCHITECTURE_EVOLUTION_PLAN.md) — direzione architetturale.
- [`docs/V3_BLUEPRINT.md`](docs/V3_BLUEPRINT.md) — futuro tile-first.
- [`docs/CHANGELOG.md`](docs/CHANGELOG.md) — lavoro completato.

## License

MIT — vedere [`LICENSE`](LICENSE).
