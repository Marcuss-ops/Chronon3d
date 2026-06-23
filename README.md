# Chronon3D

Code-first, headless, CPU-first motion graphics and compositing engine in C++20.

> **Stato al 23 giugno 2026:** `main` è avanzato ma pre-stabile.
> La fonte canonica del punto prodotto è
> [`docs/CURRENT_READINESS.md`](docs/CURRENT_READINESS.md).
> Non descrivere il repository come release-ready, completamente verde o in
> parità con After Effects senza prove eseguite sullo stesso commit.

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

La direzione pubblica resta:

- una sola pipeline render compilata;
- una sola pipeline testuale canonica;
- una sola pipeline camera `CameraDescriptor → CameraProgram`;
- registrazione esplicita tramite estensioni;
- runtime headless e deterministico;
- nessuna GUI o dipendenza browser nel core.

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

## Stato prodotto sintetico

Le percentuali seguenti sono stime di copertura funzionale, non risultati CI.

| Area | Completezza stimata | Stato reale |
|---|---:|---|
| Text Production V1 | 60–65% | Fondazioni avanzate; word timing, rich text produttivo, preset e golden da chiudere. |
| Camera Production V1 | 70–75% | Percorso compilato avanzato; migrazione legacy e alcuni gate/funzioni restano aperti. |
| SDK C++ installabile | 80–85% | Package CMake e target pubblico presenti; consumer di rendering reale ancora da certificare. |
| SDK cross-language | 30–40% | C ABI e formato dichiarativo delle animazioni ancora da progettare e implementare. |

Dettagli, limiti e criteri di chiusura:
[`docs/CURRENT_READINESS.md`](docs/CURRENT_READINESS.md).

## Stato delle aree principali

| Area | Stato |
|---|---|
| Render graph compilato | Fondazione presente; baseline completa da verificare sul commit candidato |
| Text, shape, image, effects | Implementati con limiti documentati |
| Camera compilata | Presente e avanzata; non ancora unico percorso produttivo |
| Software backend | Confine rifattorizzato; gate e full validation da osservare insieme |
| SDK C++ installabile | Package presente; release gate end-to-end da completare |
| Pacchetti animazione / C ABI | Pianificati |
| V3 tile-first | Pianificato, non prioritario prima della stabilizzazione |
| Expressions V2 | Sperimentale in `experimental/expressions/` |

Expressions V2 è OFF di default, non viene installato e non è collegato da
`Chronon3D::SDK`. L’opt-in usa `CHRONON3D_BUILD_EXPERIMENTAL=ON`.

## Documenti principali

- [`AGENTS.md`](AGENTS.md) — istruzioni operative e regole architetturali.
- [`docs/CURRENT_READINESS.md`](docs/CURRENT_READINESS.md) — snapshot canonico del prodotto.
- [`docs/STATUS.md`](docs/STATUS.md) — blocker e stato operativo.
- [`docs/NEXT_STEPS.md`](docs/NEXT_STEPS.md) — ordine immediato.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — milestone prodotto.
- [`docs/FEATURES.md`](docs/FEATURES.md) — inventario delle feature.
- [`docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`](docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md) — piano testo.
- [`docs/CAMERA_FEATURE_MATRIX.md`](docs/CAMERA_FEATURE_MATRIX.md) — matrice camera.
- [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) — difetti e follow-up.
- [`docs/V3_BLUEPRINT.md`](docs/V3_BLUEPRINT.md) — futuro tile-first, non runtime corrente.

## License

MIT — vedere [`LICENSE`](LICENSE).
