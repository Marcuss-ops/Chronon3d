# Baseline verde

- [x] Build core verde.
- [x] Build lean verde.
- [x] Test veloci verdi (704/707 pass, 3 failure pre-esistenti).
- [ ] Consumer SDK esterno verde (fallisce per configurazione vcpkg).
- [x] Stato documentato con SHA e toolchain.

## Dettaglio Baseline

**Data:** 2026-06-21
**SHA:** `f7468355`
**Toolchain:** cmake 4.2.3, gcc 15.2.0, ninja 1.13.2, x86_64
**vcpkg:** 2026-05-27 (bootstrap presente)

### Build

| Preset | Status | Note |
|--------|--------|------|
| `linux-core-dev` | ✅ | Build minimale (no text, no blend2d, no CLI, no video) |
| `linux-lean` | ✅ | core-dev + CLI + MESH (168 target) |
| `linux-lean-dev` | ❌ | Build rot pre-esistente (text_audit_engine, typewriter tracking, SoftwareRenderer ctor, etc.) |

### Test (linux-core-dev)

| Suite | Status | Dettaglio |
|-------|--------|-----------|
| `chronon3d_core_tests` | ⚠️ | 1 FAIL — `test_render_session_reset_and_isolation` (concurrent calls) |
| `chronon3d_scene_tests` | ⚠️ | 2 FAIL — `RenderRuntime::backend()` chiamato prima di `attach_backend()` |
| `chronon3d_optimizer_tests` | ✅ | OK |
| `chronon3d_cache_tests` | ✅ | OK |
| `chronon3d_compositor_tests` | ✅ | OK |
| `chronon3d_architecture_includes_boundary` | ❌ | 2 violazioni header in `render_session.hpp` |
| Totale doctest | **704/707** | 3 failed, 3 skipped |

### Consumer SDK

| Test | Status | Note |
|------|--------|------|
| `install_consumer_ci` | ❌ | `find_package(spdlog)` fallisce nel configure standalone — manca `CMAKE_TOOLCHAIN_FILE`/`CMAKE_PREFIX_PATH` corretti |

### Fix applicati per raggiungere la baseline

1. **CMake: `add_subdirectory(text)` guardato con `CHRONON3D_ENABLE_TEXT`** — `chronon3d_text_core` non viene compilato quando TEXT=OFF, evitando linker error per `FontEngine`.
2. **CMake: tutti i riferimenti a `chronon3d_text_core` guardati con `if(TARGET ...)`** — in `src/CMakeLists.txt`, `tests/CMakeLists.txt`, `src/backends/software/CMakeLists.txt`, `src/render_graph/CMakeLists.txt`.
3. **Header: `m_font_engine` in `software_renderer.hpp` guardato con `CHRONON3D_ENABLE_TEXT`** — evita che `std::unique_ptr<FontEngine>` richieda il distruttore quando TEXT=OFF.
4. **Source: `software_renderer.cpp` allineato a `CHRONON3D_ENABLE_TEXT`** — le guardie `#ifdef CHRONON3D_HAS_BACKEND_TEXT` sostituite con `CHRONON3D_ENABLE_TEXT`.
5. **Authoring tests: `authoring_tests.cmake` guardato con `CHRONON3D_ENABLE_TEXT && CHRONON3D_USE_BLEND2D`** — evita compilazione quando il sottosistema text non è disponibile.
6. **Source: `TextRunNode.cpp` e `multi_source_node.cpp`** — chiamate a funzioni text (`compute_text_run_world_bbox`, `hash_text_run_shape`, `update_text_run_shape_per_frame`) guardate con `#ifdef CHRONON3D_ENABLE_TEXT`.
7. **Source: `software_text_processor.cpp`** — dereferenziazione puntatore `engine` → `*engine` per match con signature `FontEngine&`.
8. **Authoring test: `test_animator_dsl.cpp`** — split delle chain su `Text` temporaneo per evitare uso del copy constructor deleted.
9. **Test: `test_text_quality_tracking.cpp` e `test_text_quality_arabic.cpp`** — aggiunto parametro `resolver` alle chiamate `compute_typewriter_layout`.
10. **CLI: `text_audit_engine.cpp`** — aggiunto `#include <chronon3d/runtime/render_runtime.hpp>`, fix brace init `TextAuditBBox`.
11. **CLI: `CMakeLists.txt`** — file `text_audit_*.cpp` esclusi condizionalmente quando BLEND2D o TEXT sono OFF (via generator expression `$<$<AND:...>:>`)`.
12. **Test: `tests/CMakeLists.txt`** — `chronon3d_authoring_tests` nella fast-test list ora guardato con `if(TARGET ...)`.
13. **Source: `software_renderer.cpp`** — messaggi `throw` aggiornati da `CHRONON3D_HAS_BACKEND_TEXT` a `CHRONON3D_ENABLE_TEXT`.
