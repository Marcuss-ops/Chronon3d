# ${PROJECT_NAME}

Chronon3D project scaffolded with [`chronon create`](https://chronon3d.com/cli/create).
The template provides a working **Hello, World!** video project that you can build,
customize, and extend.

## Quick start

```bash
# 1. Configure
cmake -B build

# 2. Build
cmake --build build

# 3. Run — renders the "HelloWorld" composition to stdout
./build/${PROJECT_NAME}
```

## Prerequisites

- **Chronon3D SDK installed** on your system.  See
  [https://chronon3d.com/install](https://chronon3d.com/install) for instructions.
  Verify with `cmake --find-package -DNAME=Chronon3D -DCOMPILER_ID=GNU -DLANGUAGE=CXX`.

- **CMake ≥ 3.27** (`cmake --version`).
- **C++20-capable compiler** (GCC ≥ 10, Clang ≥ 12, MSVC ≥ 19.29).

## Layout

```
${PROJECT_NAME}/
├── CMakeLists.txt          # consumer project — links Chronon3D::SDK (no umbrella)
├── chronon.json            # project manifest (name, version, assets_root, main)
├── README.md               # this file
├── assets/                 # drop your fonts, images, audio here
│   └── .gitkeep
├── props/                  # drop your *.json props files here (V0.2+)
│   └── .gitkeep
└── src/
    ├── root.cpp            # int main() — registers "HelloWorld" + renders via sdk::RenderEngine
    └── hello_world.cpp     # defines the "HelloWorld" composition (SceneBuilder facade)
```

## Customizing

### Change the composition

Edit `src/hello_world.cpp` to:
- **Add layers** (text, image, shape, 3D) using the canonical `authoring::Layer` facade.
- **Configure animations** using `interpolate`, `spring`, `loop` from the top-level header.
- **Use sequences** with `Scene::sequence(...)` (forward-point of authoring facade completion).

### Add multiple compositions

Edit `src/root.cpp` to call `make_hello_world` plus additional `make_*` factories:

```cpp
make_hello_world(project);
make_intro(project);     // factory defined in src/intro.cpp
make_outro(project);     // factory defined in src/outro.cpp
```

Then build with multiple `add_executable` targets or one binary that dispatches
based on `argv[1]`.

### Add PNG output

`root.cpp` currently only prints the render result.  To write a PNG:

```cpp
#include <chronon3d/image_io/save_png.hpp>
// ...
auto save_result = chronon3d::save_png(result->framebuffer, "output/hello.png");
```

## `chronon render` integration (forward-point)

Currently the standalone binary is the rendering path: `./build/${PROJECT_NAME}`.
A future `chronon render HelloWorld -o hello.mp4` integration is planned
(see [TICKET-ADD-LOADER-FOR-CHRONON-JSON](docs/tickets/TICKET-ADD-LOADER-FOR-CHRONON-JSON.md))
where the CLI reads `chronon.json` and dispatches to the built binary.

## License

This template is part of the Chronon3D project.  See [LICENSE](../../LICENSE).
