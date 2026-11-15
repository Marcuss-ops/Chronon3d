# Chronon3D

Code-first, headless, performance-oriented motion graphics and compositing engine in C++20.

Lo stato corrente del progetto è [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md);
i requisiti di release sono [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md).

## Quick start

```bash
cmake --preset linux-fast-dev
cmake --build --preset linux-fast-dev --target chronon3d_cli
mkdir -p output

# Render the included 64x64 SDK example plan to a PNG.
./build/fast/apps/chronon3d_cli/chronon3d_cli render \
  --plan sdk_consumers/01_cpp_minimal/plan.json --frame 0 \
  --backend software -o output/test.png
```

`render` è l’unico comando canonico per still, sequence e video. I precedenti
comandi separati `still` e `video` sono stati rimossi.



## C++ SDK example

Gli asset sono path logici. L’authoring non risolve il filesystem e non possiede
un asset root globale: la risoluzione avviene nel `AssetResolver` del singolo
runtime configurato tramite `sdk::RenderEngine::set_assets_root(...)`.

The working consumer example is [`sdk_consumers/01_cpp_minimal`](sdk_consumers/01_cpp_minimal/README.md).
It uses the installed SDK's real `chronon3d/sdk/render_engine.hpp` API and
compiles and renders the adjacent `plan.json`.

## Architettura corrente

```text
Composition
  → Scene
  → RenderGraph
  → GraphCompilePipeline
  → CompiledFrameGraph
  → GraphExecutor
  → RenderBackend
  → output
```

La direzione pubblica resta:

```text
Chronon3D owns content and pixels.
ChrononMotion owns space and time.
ChrononTemplate owns composition.
RenderingGen owns editorial intent.
```

Chronon3D is the sole authority for text/font loading, shaping, glyph/cluster
layout, glow, compositing, GPU rendering and encoding. ChrononTemplate may
request text, but it must delegate to the Chronon ContentHost; ChrononMotion3D
receives only opaque content identity, Chronon-produced metrics and final
transforms. Any cluster-level animation is evaluated here from Chronon-owned
stable element IDs, never reconstructed by a consumer.

ChrononMotion3D is an authoring-time producer: its offline physics/IK/PCM/flow/
SVG bakers emit sampled data, and its optional RenderPlanLowering adapter
lowers integer-frame poses to typed RenderPlan v2/v3. The adapter is not linked
into Chronon3d's renderer or frame path. Its bounded static glTF/GLB reader
produces geometry-only `Mesh` nodes on the Motion side; Chronon retains ownership
of materials and render assets. The sibling adapter test sends emitted V2/V3 data through both Chronon3d's
canonical schema validator and typed decoder, including camera animation and V3
Merge/Morph operators. Geometry output remains Motion-side authoring data; it is
not a promise that a GPU-backed mesh layer is part of the RenderPlan ABI. The
opt-in build switch is `CHRONON3D_ENABLE_MOTION_ADAPTER=ON`; CI's focused adapter
target is enabled only in the explicitly configured interoperability lane.

- una sola pipeline render compilata;
- una sola pipeline testuale canonica;
- CM3D è l'autorità unica per l'authoring e la valutazione cinematografica;
- C3D consuma la camera risolta e mantiene proiezione, depth, DOF pixel,
  motion blur, compositing e output;
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

- [`docs/INDEX.md`](docs/INDEX.md) — **indice unico**: è l'unico punto di ingresso alla documentazione.
- [`AGENTS.md`](AGENTS.md) — istruzioni operative e regole architetturali.
- [`docs/CHRONON_PLAN.md`](docs/CHRONON_PLAN.md) — piano operativo (10 fasi GPU-first).
- [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md) — stato presente.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — milestone prodotto.
- [`docs/RELEASE_GATE.md`](docs/RELEASE_GATE.md) — requisiti permanenti di release.
- [`docs/FOLLOWUP_TICKETS.md`](docs/FOLLOWUP_TICKETS.md) — difetti e follow-up aperti.
- [`docs/ZERO_COPY_GATES.md`](docs/ZERO_COPY_GATES.md) — certificazione zero-copy.

## License

MIT — vedere [`LICENSE`](LICENSE).
