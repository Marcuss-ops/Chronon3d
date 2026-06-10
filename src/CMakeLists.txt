# ==============================================================================
# src/CMakeLists.txt — Orchestrator
#
# Ogni subsystem viene definito nel suo CMakeLists.txt sotto la rispettiva
# cartella.  Questo file gestisce:
#   - Target FFmpeg wrapper (light/full)
#   - Base interface chronon3d (INTERFACE)
#   - Opzione CHRONON3D_ENABLE_MESH
#   - add_subdirectory() per ogni subsystem (ordinati per grafo dipendenze)
#   - Target aggregati finali (chronon3d_core, chronon3d_software, chronon3d_pipeline)
#
# ORDINE DI CARICAMENTO (obbligatorio: un target deve esistere prima di essere
# referenziato da target_link_libraries in un altro add_subdirectory):
#   1. core          → chronon3d_core_impl
#      chronon3d     → INTERFACE base (include path, glm, xxhash, magic_enum)
#   2. cache         → chronon3d_cache            (dip: chronon3d, xxhash)
#   3. media         → chronon3d_media_interface   (serve a render_graph)
#                    → chronon3d_backend_video     (dip: media_interface, cache, ffmpeg)
#   4. render_graph  → chronon3d_graph             (dip: chronon3d, cache, media_interface)
#   5. effects       → chronon3d_effects           (dip: chronon3d, meshoptimizer)
#   6. backends/text → chronon3d_backend_text      (dip: chronon3d, cache)
#   7. backends/image→ chronon3d_backend_image     (dip: chronon3d)
#   8. backends/assets→ chronon3d_backend_assets   (dip: backend_image)
#   9. backends/software→ chronon3d_backend_software(dip: graph, cache, effects, image, text, assets)
#  10. runtime       → chronon3d_runtime           (dip: graph, effects, cache)
#  11. registry      → chronon3d_registry          (dip: chronon3d)
#  12. scene         → chronon3d_scene             (dip: runtime, graph, registry, cache, effects)
#  13. animations    → chronon3d_animations        (dip: scene, backend_software)
#  14. assets/libreria→ chronon3d_assets           (dip: registry)
#  15. extension     → chronon3d_extension         (dip: graph, registry, effects)
# ==============================================================================

# ==============================================================================
# CENTRALIZED EXTERNAL LINK FLAGS
# Target IMPORTED per FFmpeg — evitano raw "-l" flags nel grafo dipendenze.
# I nomi usano il prefisso CHRONON3D_ per non collidere con i target
# vcpkg/system (avcodec, avformat, ...).
# ==============================================================================
add_library(chronon3d_ffmpeg_light INTERFACE)
target_link_libraries(chronon3d_ffmpeg_light INTERFACE swscale avutil)

add_library(chronon3d_ffmpeg_full  INTERFACE)
target_link_libraries(chronon3d_ffmpeg_full  INTERFACE swscale avcodec avformat avutil)

# Variabili stringa legacy (per eventuali caller esterni che usano le vecchie liste raw)
set(CHRONON3D_FFMPEG_LIBS_LIGHT "swscale;avutil")
set(CHRONON3D_FFMPEG_LIBS_FULL  "swscale;avcodec;avformat;avutil")

# ==============================================================================
# 1. Core Implementation
# Profiling, metriche di sistema, cancellazione token, clear parallelo framebuffer
# ==============================================================================
add_subdirectory(core)

# ==============================================================================
# 2. Base Interface (chronon3d)
# Target INTERFACE condiviso da TUTTI i subsystem. Include path, define globali,
# dipendenze di base (glm, xxhash, magic_enum, meshoptimizer opzionale).
# ==============================================================================
add_library(chronon3d INTERFACE)
target_include_directories(chronon3d INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/deps/include>
)
target_compile_definitions(chronon3d INTERFACE 
    CHRONON3D_GIT_COMMIT="${CHRONON3D_GIT_COMMIT}"
    $<$<BOOL:${MSVC}>:_DISABLE_STRING_VIEW_FIND_FIRST_NOT_OF_OPTIMIZATION>
)
option(CHRONON3D_ENABLE_MESH "Abilita supporto mesh processing" ON)

# CHRONON3D_MESHOPTIMIZER_LINK è una variabile usata anche da effects/CMakeLists.txt
set(CHRONON3D_MESHOPTIMIZER_LINK "")
if(CHRONON3D_ENABLE_MESH)
    set(CHRONON3D_MESHOPTIMIZER_LINK meshoptimizer::meshoptimizer)
    target_compile_definitions(chronon3d INTERFACE CHRONON3D_ENABLE_MESH)
endif()

target_link_libraries(chronon3d INTERFACE
    glm::glm
    ${CHRONON3D_MESHOPTIMIZER_LINK}
    xxHash::xxhash
    magic_enum::magic_enum
    chronon3d_core_impl
)

# ==============================================================================
# SUBSYSTEM LIBRARIES (ordinati per grafo dipendenze)
# ==============================================================================

# 3. Cache — frame/node/disk cache, framebuffer pool
add_subdirectory(cache)

# 3b. Media Interface — DEVE stare prima di render_graph (chronon3d_graph lo referenzia)
add_subdirectory(media)

# 4. Render Graph — graph builder, executor, optimizer, compiler, nodes
add_subdirectory(render_graph)

# 5. Effects — effect registry
add_subdirectory(effects)

# 6. Text Backend — rasterizer, font engine
add_subdirectory(backends/text)

# 7. Backends Image + Assets
add_subdirectory(backends/image)
add_subdirectory(backends/assets)

# 8. Software Renderer Backend (dipende da graph, cache, effects, image, text, assets)
add_subdirectory(backends/software)

# 9. Runtime & Telemetry — pipeline, warmup, scene-to-graph, timeline
add_subdirectory(runtime)

# 10. Registry + Scene
add_subdirectory(registry)
add_subdirectory(scene)

# 11. Animations (dipende da scene + backend_software — entrambi già definiti sopra)
add_subdirectory(animations)

# 12. Assets libreria + Extension
add_subdirectory(assets)
add_subdirectory(extension)

# ==============================================================================
# TARGET AGGREGATI FINALI
# WHOLE_ARCHIVE è necessario per target con macro CHRONON_REGISTER_*:
# lo static initializer viene altrimenti eliminato dal linker se nessun symbol
# è referenziato visibilmente. WHOLE_ARCHIVE solo sui target di registrazione
# (scene, animations, backend_video).
# ==============================================================================

# chronon3d_media_interface è già linkato in render_graph/CMakeLists.txt

add_library(chronon3d_core INTERFACE)
target_link_libraries(chronon3d_core INTERFACE
    chronon3d_extension
    chronon3d_assets
    chronon3d_registry
    $<LINK_LIBRARY:WHOLE_ARCHIVE,chronon3d_scene>
    chronon3d_runtime
    chronon3d_graph
    chronon3d_cache
    $<LINK_LIBRARY:WHOLE_ARCHIVE,chronon3d_animations>
    chronon3d_core_impl
    $<LINK_LIBRARY:WHOLE_ARCHIVE,chronon3d_backend_video>
)

add_library(chronon3d_software INTERFACE)
target_link_libraries(chronon3d_software INTERFACE
    $<LINK_LIBRARY:WHOLE_ARCHIVE,chronon3d_backend_software>
    $<LINK_LIBRARY:WHOLE_ARCHIVE,chronon3d_backend_video>
    chronon3d_backend_assets
    chronon3d_backend_image
)

add_library(chronon3d_pipeline INTERFACE)
target_link_libraries(chronon3d_pipeline INTERFACE
    chronon3d_core
    chronon3d_software
    Taskflow::Taskflow
    concurrentqueue::concurrentqueue
    yuv
    ${CHRONON3D_FFMPEG_LIBS_LIGHT}
)
