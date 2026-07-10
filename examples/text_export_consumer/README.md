# Chronon3D — Text Export Consumer Example

Minimal standalone C++ project that renders text using the Chronon3D public API.

## What it demonstrates

- `find_package(Chronon3D)` + `Chronon3D::SDK` link (no internal headers)
- `SceneBuilder` + `l.text()` to create styled text
- `sdk::RenderEngine` to render the composition
- `save_png` to export the result

## Prerequisites

- Chronon3D installed via `cmake --install`
- Font file `Inter-Bold.ttf` in `assets/fonts/` relative to the run directory
  (copy from the Chronon3D repo: `cp -r <chronon3d>/assets/fonts ./assets/fonts/`)

## Quick start

```bash
# 1. Build Chronon3D and install
cd /path/to/Chronon3d
cmake -S . -B build --preset linux-ci
cmake --build build --target chronon3d_sdk_impl
cmake --install build --prefix /tmp/chronon3d-install

# 2. Build this example
cd examples/text_export_consumer
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/tmp/chronon3d-install
cmake --build .

# 3. Copy fonts and run
cp -r ../../assets/fonts ./assets/fonts
./text_export_consumer
# → produces text_export_output.png
```

## Expected output

A 1280×720 PNG with white "Hello, Chronon3D!" text centered on a dark background.

## Drop-in for your project

Copy this directory into your project and adapt:
- `CMakeLists.txt` — change the project name and `CMAKE_PREFIX_PATH`
- `main.cpp` — change the text content, font, size, and colors
- `assets/fonts/` — include the fonts your text uses
