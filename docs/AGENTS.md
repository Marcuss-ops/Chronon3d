# Chronon3D — Build & Development Guide

## Prerequisiti

- **C++20** toolchain (GCC 12+, Clang 16+, MSVC 2022+)
- **CMake** ≥ 3.24
- **Ninja** (opzionale ma raccomandato — usato dai preset)
- **vcpkg** (automatico, impostato via `VCPKG_ROOT`)
- **ccache** (raccomandato — rilevato automaticamente)

## Build rapida

```bash
# Configura + build (release)
cmake --preset linux-release
cmake --build build/chronon/linux-release -j$(nproc)

# Solo CLI
cmake --build build/chronon/linux-release --target chronon3d_cli -j$(nproc)

# Solo tests
cmake --build build/chronon/linux-release --target chronon3d_tests -j$(nproc)

# Eseguire i test
ctest --test-dir build/chronon/linux-release -j$(nproc)
```

## Build incrementale (dopo aver modificato codice)

```bash
# Il backend software è il target principale
cmake --build build/chronon/linux-release --target chronon3d_backend_software -j$(nproc)

# Poi linka CLI/tests
cmake --build build/chronon/linux-release --target chronon3d_cli -j$(nproc)
```

Con **ccache** + **PCH**, un rebuild dopo aver cambiato un `.cpp` singolo impiega **~3-14s**.

## Preset disponibili

| Preset | Build dir | Uso |
|---|---|---|
| `linux-release` | `build/chronon/linux-release` | Build di produzione |
| `linux-debug` | `build/chronon/linux-debug` | Debug con simboli |
| `win-release` | `build/chronon/win-release` | Windows release |
| `win-debug` | `build/chronon/win-debug` | Windows debug |
| `macos-release` | `build/chronon/macos-release` | macOS release |
| `macos-debug` | `build/chronon/macos-debug` | macOS debug |

## Ambiente

```bash
# vcpkg (obbligatorio)
export VCPKG_ROOT="$HOME/vcpkg"

# ccache (opzionale, ma automatico)
sudo apt install ccache        # Debian/Ubuntu
brew install ccache            # macOS
```

## Variabili d'ambiente

| Variabile | Default | Descrizione |
|---|---|---|
| `CHRONON_FB_POOL_MAX_MB` | illimitato | Limite pool framebuffer |
| `CHRONON_NODE_CACHE_MAX_MB` | 64 | Dimensione cache nodi |
| `CHRONON_TEXT_CACHE_MAX_MB` | 128 | Dimensione cache raster testo |
| `CHRONON_SHADOW_CACHE_MAX_MB` | 64 | Dimensione cache ombre |
| `CHRONON_GLOW_CACHE_MAX_MB` | 64 | Dimensione cache glow |

## Architettura del build system

```
CMakeLists.txt  →  librerie statiche (25+)  →  link in chronon3d_cli / chronon3d_tests
```

Le librerie principali sono ordinate in DAG:
```
chronon3d (INTERFACE — headers/defines)
  ├─ chronon3d_core_impl (tracing, benchmark)
  ├─ chronon3d_registry (shape/source/sampler)
  ├─ chronon3d_cache (framebuffer pool, LRU caches)
  ├─ chronon3d_graph (render graph, executor)
  ├─ chronon3d_effects (effect registry)
  ├─ chronon3d_runtime (pipeline, telemetry)
  ├─ chronon3d_scene (scene graph, camera, layout)
  ├─ chronon3d_compositions (scene proof presets)
  ├─ chronon3d_backend_image (load/write PNG, EXR)
  ├─ chronon3d_backend_assets (image cache)
  └─ chronon3d_backend_software (rasterizer, text, compositing)
        └─ 30 file, PCH su glm/blend2d/spdlog/fmt
```

## Performance build

| Scenario | Prima | Dopo (ccache + PCH + mold) |
|---|---|---|
| Full clean build | ~8-10 min | ~6 min |
| Rebuild singolo .cpp | ~2 min | ~14s (cold) / ~3s (hot) |
| Rebuild header inline | ~5 min | ~30s-1min (dipende dalla portata) |
| Link CLI | ~30-60s | ~5s (mold) |

**ccache** taglia i rebuild ridondanti. **PCH** evita di ricompilare `glm.hpp` e `blend2d.h` 30+ volte.  
**mold** (linker) accelera il link finale di 10x — rilevato automaticamente se installato.
