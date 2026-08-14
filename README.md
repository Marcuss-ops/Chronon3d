# Chronon3D

Code-first, headless, CPU-first motion graphics and compositing engine in C++20.

Lo stato corrente del progetto è [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md);
i requisiti di release sono [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md).

## Quick start

```bash
bash tools/chronon-linux.sh
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli list

# Still
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli \
  render BackgroundGrid --frame 0 -o output/test.png

# Sequence
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli \
  render BackgroundGrid --frames 0-90 -o output/frame_####.png

# Video: the output extension selects RenderMode::Video
./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli \
  render BackgroundGrid -o output/background_grid.mp4
```

`render` è l’unico comando canonico per still, sequence e video. I precedenti
comandi separati `still` e `video` sono stati rimossi.

Build incrementali: [`docs/FAST_BUILD.md`](docs/FAST_BUILD.md).

## Usare Chronon3D da un altro progetto C++

Chronon3D espone un solo target CMake pubblico: `Chronon3D::SDK`.
Dopo aver installato l'SDK in un prefix visibile a CMake:

```cmake
find_package(Chronon3D CONFIG REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE Chronon3D::SDK)
```

In alternativa, se Chronon3D è incluso come sorgente nel workspace, usare
`add_subdirectory(...)` e linkare lo stesso target `Chronon3D::SDK`. Le
dipendenze C++ di Chronon3D devono comunque essere risolte dal toolchain del
progetto host.

Per metadata semantici riutilizzabili tra pipeline e progetti è disponibile un
registry instance-owned e deterministico:

```cpp
#include <chronon3d/registry/content_registry.hpp>

chronon3d::registry::ContentRegistrySet content;

content.phrases.add({
    .id = "phrase.intro",
    .text = "Welcome to Chronon",
});

content.important_words.add({
    .id = "word.chronon",
    .word = "Chronon",
    .importance = 1.0F,
});

content.images.add({
    .id = "image.logo",
    .asset_path = "images/logo.png",
    .caption = "Chronon logo",
});

content.named_texts.add({
    .id = "person.host",
    .name = "Host",
    .text = "Main presenter",
});
```

`ImageEntry::asset_path` resta un path/id logico: la risoluzione del filesystem
rimane responsabilità dell'`AssetResolver` engine-local. Non viene introdotto
nessun registry globale o singleton.

## Asset authoring

Gli asset sono path logici. L’authoring non risolve il filesystem e non possiede
un asset root globale: la risoluzione avviene nel `AssetResolver` del singolo
runtime configurato tramite `sdk::RenderEngine::set_assets_root(...)`.

```cpp
#include <chronon3d/authoring/asset.hpp>
#include <chronon3d/authoring/layer.hpp>
#include <chronon3d/authoring/text.hpp>
#include <chronon3d/sdk/render_engine.hpp>

using chronon3d::authoring::asset;

layer.image("logo", asset("images/logo.png"));
layer.text("HELLO").font(asset("fonts/Inter.ttf"), 100);

chronon3d::sdk::RenderEngine engine;
engine.set_assets_root("/srv/project/assets");
```

Gli include restano espliciti e focalizzati: non esiste un nuovo mega-header
`chronon3d.hpp`, `authoring.hpp`, `render.hpp` o `advanced.hpp`.

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
