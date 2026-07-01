# Chronon3D

Code-first, headless, CPU-first motion graphics and compositing engine in C++20.

Lo stato corrente del progetto è [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md);
i requisiti di release sono [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md).

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

## Documenti principali

- [`AGENTS.md`](AGENTS.md) — istruzioni operative e regole architetturali.
- [`docs/DOCUMENTATION_GOVERNANCE.md`](docs/DOCUMENTATION_GOVERNANCE.md) — contratto documentale (single-source-of-truth).
- [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md) — stato presente.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — milestone prodotto.
- [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md) — requisiti permanenti di release.
- [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) — difetti e follow-up aperti.
- [`docs/FEATURES.md`](docs/FEATURES.md) — inventario delle feature.
- [`docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md`](docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md) — piano testo.
- [`docs/CAMERA_FEATURE_MATRIX.md`](docs/CAMERA_FEATURE_MATRIX.md) — matrice camera.
- [`docs/V3_BLUEPRINT.md`](docs/V3_BLUEPRINT.md) — futuro tile-first, non runtime corrente.

## License

MIT — vedere [`LICENSE`](LICENSE).
