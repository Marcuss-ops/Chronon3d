cmake_minimum_required(VERSION 3.25)

project(Chronon3d
    VERSION 0.1.0
    DESCRIPTION "A modular C++20 3D engine/library"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND NOT DEFINED VCPKG_CHAINLOAD_TOOLCHAIN_FILE)
    message(WARNING "vcpkg toolchain not detected. Dependencies are strict and might fail to configure. Please use a CMake preset (e.g., --preset linux-release) or set CMAKE_TOOLCHAIN_FILE to vcpkg.cmake.")
endif()

# --- Opzioni di Compilazione Globali ---
if(MSVC)
    add_compile_options(/utf-8 /FS /EHsc /fp:strict)
else()
    add_compile_options(-ffp-contract=off)
endif()

# --- Configurazione Strumenti di Build (ccache / linkers) ---
if(NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER AND DEFINED ENV{CMAKE_CXX_COMPILER_LAUNCHER})
    set(CMAKE_CXX_COMPILER_LAUNCHER "$ENV{CMAKE_CXX_COMPILER_LAUNCHER}")
    set(CMAKE_C_COMPILER_LAUNCHER "$ENV{CMAKE_C_COMPILER_LAUNCHER}")
endif()

if(NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        message(STATUS "ccache found: ${CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE FILEPATH "C++ compiler launcher")
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE FILEPATH "C compiler launcher")
    endif()
endif()

if(NOT MSVC AND NOT CMAKE_CXX_LINKER_LAUNCHER)
    find_program(MOLD_PROGRAM mold)
    if(MOLD_PROGRAM)
        message(STATUS "mold found: ${MOLD_PROGRAM}")
        add_link_options("-fuse-ld=mold")
    else()
        find_program(LLD_PROGRAM ld.lld)
        if(LLD_PROGRAM)
            message(STATUS "lld found: ${LLD_PROGRAM}")
            add_link_options("-fuse-ld=lld")
        endif()
    endif()
endif()

# --- Opzioni del Progetto ---
option(CHRONON3D_UNITY_BUILD "Enable unity (jumbo) build" OFF)
option(CHRONON3D_BUILD_TESTS "Build Chronon3d tests" OFF)
option(CHRONON3D_BUILD_CLI "Build Chronon3d CLI" ON)
option(CHRONON3D_BUILD_BENCHMARKS "Build Chronon3d benchmarks" OFF)
option(CHRONON3D_BUILD_CONTENT "Build bundled content compositions (proofs, demos)" OFF)
option(CHRONON3D_BUILD_DIAGNOSTICS "Build diagnostic/debug compositions (tests, calibrations, A/B)" OFF)
option(CHRONON3D_ENABLE_NATIVE_FFMPEG "Enable native FFmpeg/libav encoder support" OFF)
option(CHRONON3D_ENABLE_TEXT "Enable text rendering support (freetype + harfbuzz)" ON)
option(CHRONON3D_ENABLE_EXR "Enable OpenEXR image format support" ON)
option(CHRONON3D_ENABLE_VIDEO "Enable video/media pipeline (frame conversion, video sinks)" ON)
option(CHRONON3D_ENABLE_PROFILING "Enable Tracy profiling" OFF)
option(CHRONON3D_ENABLE_TELEMETRY "Enable SQLite telemetry persistence" OFF)
option(CHRONON3D_ENABLE_DIAGNOSTICS "Enable engine-level diagnostics code (overlays, analysis, cache introspection)" OFF)
option(CHRONON3D_ENABLE_MESH "Enable mesh processing support" ON)

# ── Optional heavy dependency: Blend2D ──────────────────────────────────
# Blend2D is the only USE_ option because it is independent of its ENABLE_
# counterpart (ENABLE_TEXT gates freetype+harfbuzz but not Blend2D).
# All other optional deps use the ENABLE_* options exclusively.
option(CHRONON3D_USE_BLEND2D "Enable Blend2D software rendering backend" ON)

if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    add_compile_definitions(CHRONON3D_ENABLE_NATIVE_FFMPEG)
endif()

# ── Propagate optional-dependency defines so code can guard #includes ──
if(CHRONON3D_USE_BLEND2D)
    add_compile_definitions(CHRONON3D_USE_BLEND2D)
endif()
if(CHRONON3D_ENABLE_TEXT)
    add_compile_definitions(CHRONON3D_ENABLE_TEXT)
endif()
if(CHRONON3D_ENABLE_EXR)
    add_compile_definitions(CHRONON3D_ENABLE_EXR)
endif()
if(CHRONON3D_ENABLE_VIDEO)
    add_compile_definitions(CHRONON3D_ENABLE_VIDEO)
endif()
if(CHRONON3D_BUILD_TESTS)
    # Test suites assert against diagnostics snapshots and cache registration.
    set(CHRONON3D_ENABLE_DIAGNOSTICS ON CACHE BOOL "Enable engine-level diagnostics code" FORCE)
endif()
if(CHRONON3D_BUILD_DIAGNOSTICS)
    add_compile_definitions(CHRONON3D_BUILD_DIAGNOSTICS)
    # Content diagnostics require engine-level diagnostics code.
    set(CHRONON3D_ENABLE_DIAGNOSTICS ON CACHE BOOL "Enable engine-level diagnostics code" FORCE)
endif()

if(CHRONON3D_ENABLE_DIAGNOSTICS)
    add_compile_definitions(CHRONON3D_ENABLE_DIAGNOSTICS)
endif()

if(CHRONON3D_ENABLE_TELEMETRY)
    add_compile_definitions(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
endif()

if(CHRONON3D_UNITY_BUILD)
    set(CMAKE_UNITY_BUILD ON)
    if(NOT CMAKE_UNITY_BUILD_BATCH_SIZE)
        set(CMAKE_UNITY_BUILD_BATCH_SIZE 16)
    endif()
endif()

# --- Ricerca Dipendenze ---
find_package(spdlog CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)
find_package(Taskflow CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
if(CHRONON3D_ENABLE_MESH)
    find_package(meshoptimizer CONFIG REQUIRED)
endif()
find_package(xxHash CONFIG REQUIRED)
find_package(Stb REQUIRED)
if(CHRONON3D_BUILD_CLI)
    find_package(CLI11 CONFIG REQUIRED)
endif()
find_package(hwy CONFIG REQUIRED)
if(CHRONON3D_USE_BLEND2D)
    find_package(blend2d CONFIG REQUIRED)
endif()

if(CHRONON3D_ENABLE_TEXT)
    find_package(freetype CONFIG REQUIRED)
    find_package(harfbuzz CONFIG REQUIRED)
endif()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)

if(CHRONON3D_ENABLE_EXR)
    find_package(OpenEXR CONFIG REQUIRED)
endif()

if(CHRONON3D_ENABLE_TELEMETRY)
    find_package(unofficial-sqlite3 CONFIG REQUIRED)
endif()

if(CHRONON3D_BUILD_BENCHMARKS)
    find_package(benchmark CONFIG REQUIRED)
endif()

if(CHRONON3D_ENABLE_PROFILING)
    find_package(Tracy CONFIG QUIET)
endif()

# --- Git Commit Hash Extraction ---
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE CHRONON3D_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT CHRONON3D_GIT_COMMIT)
    set(CHRONON3D_GIT_COMMIT "unknown")
endif()

add_subdirectory(src)

# 10. Content Generation (optional — proofs/demos, not core engine)
if(CHRONON3D_BUILD_CONTENT OR CHRONON3D_BUILD_DIAGNOSTICS)
    add_subdirectory(content)
endif()

# --- Profiling & Condizionali di Build ---
if(CHRONON3D_ENABLE_PROFILING AND Tracy_FOUND)
    target_link_libraries(chronon3d_scene PRIVATE Tracy::TracyClient)
    target_compile_definitions(chronon3d_scene PRIVATE CHRONON_PROFILING TRACY_ON_DEMAND)
    target_link_libraries(chronon3d_pipeline INTERFACE Tracy::TracyClient)
    target_compile_definitions(chronon3d_pipeline INTERFACE CHRONON_PROFILING TRACY_ON_DEMAND)
endif()

if(CHRONON3D_BUILD_TESTS)
    set(BUILD_TESTING ON CACHE BOOL "Build the testing tree.")
    enable_testing()
    target_compile_definitions(chronon3d_core_impl PUBLIC CHRONON_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(CHRONON3D_BUILD_CLI)
    add_subdirectory(apps/chronon3d_cli)
endif()

# ── Former CMake guard: CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2 ───────────
# Path-B expression engine (feature/expressions-v2) was abandoned with multiple
# latent breakages. Two survived until TICKET-003/-004 close-out (2026-06-20):
# `<chrono3d` typo in lexer.hpp (TICKET-003 — auto-closed by upstream commits
# c878ba7d/e41ff95a) and target_include_directories PUBLIC ${CMAKE_SOURCE_DIR}/
# include instead of PRIVATE (TICKET-004 — fixed this session). The guard
# fired correctly, proving its value; all tracked defects are now resolved.
#
# A guard was added in commit aae68561 to refuse configure if those patterns
# re-appeared in any *.cmake / CMakeLists.txt — the guard fired correctly after
# rebase with PR #23 (commits 1871eb77 +
# 76d547a6) merged expressions/v2 into main. PR #23 was integrated ahead of
# guarded verification; the override -DCHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2=ON
# (or a bypassed local configure) was used to land it.
#
# Guardian verdict (recorded here for posterity): the protective value was
# real. As of 2026-06-20 both defects the guard was hunting for are closed:
# TICKET-003 (lexer.hpp typo — auto-closed upstream) and TICKET-004
# (CMake PUBLIC→PRIVATE — fixed). See docs/FOLLOWUP_TICKETS.md for
# the status transitions.
#
# Decision (this commit): retire the guard with this comment rather than
# maintain a permanent exclusion list. The feature is on main and functional;
# all surviving defects from the guard's fire are now tracked-and-closed.
# Re-introduce this guard (option + GLOB_RECURSE + string(FIND) + FATAL_ERROR)
# if/when another abandoned branch needs the same protective mechanism.
# ──────────────────────────────────────────────────────────────────────────────

# The former `option(CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2 OFF)`
# and its `if()-endif()` no-op block (retained for cache-key compat in the
# prior commit) are now removed. All tracked defects (TICKET-003/-004) are
# closed; zero presets reference the flag; no fork/CI needs the cache-key
# bridge. The retirement comment above remains as the authoritative
# historical record.

# ── SDK Install / Export ───────────────────────────────────────────────────
#
# Single EXPORT set (Chronon3DTargets.cmake) enumerates chronon3d_sdk AND the
# transitive STATIC / INTERFACE library targets so an external consumer can
# `target_link_libraries(app PRIVATE Chronon3D::SDK)` and resolve everything
# (including transitive third-party deps) without inspecting the source tree.
#
# Layout produced under `--prefix`:
#   <prefix>/include/chronon3d/*.hpp                 (public headers)
#   <prefix>/<libdir>/libchronon3d_*.a             (private static archives)
#   <prefix>/<libdir>/cmake/Chronon3D/
#     Chronon3DConfig.cmake        (entry: find_dependency + include targets)
#     Chronon3DConfigVersion.cmake (version compatibility gate)
#     Chronon3DTargets.cmake       (all imported Chronon3D::* targets)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Public headers — installed verbatim under <prefix>/include/.
install(DIRECTORY "${CMAKE_SOURCE_DIR}/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.hpp"
)

# Aggregate list of chronon3d_* targets to install + export.  Conditional
# targets (TEXT, VIDEO, NATIVE_FFMPEG) are appended under their respective
# CHRONON3D_ guards so an install with those features off does not try to
# install unbuilt targets.
set(_chronon3d_install_targets
    # consumer-facing aliases
    chronon3d_sdk
    # aggregate INTERFACE libraries
    chronon3d_pipeline
    chronon3d_core
    chronon3d_software
    chronon3d_graph
    chronon3d_media_interface
    # base INTERFACE
    chronon3d
    # STATIC / OBJECT libraries — these produce .a files in <libdir>
    chronon3d_core_impl
    chronon3d_cache
    chronon3d_effects
    chronon3d_registry
    chronon3d_assets
    chronon3d_backend_image
    chronon3d_backend_assets
    chronon3d_backend_software
    chronon3d_runtime
    chronon3d_extension
    chronon3d_scene
    chronon3d_animations
    chronon3d_text_core
    # render_graph split sub-targets
    chronon3d_graph_core
    chronon3d_graph_nodes
    chronon3d_graph_builder
    chronon3d_graph_compiler
    chronon3d_graph_executor
    chronon3d_graph_preflight
    chronon3d_graph_cache
    chronon3d_graph_pipeline
)

if(CHRONON3D_ENABLE_VIDEO)
    list(APPEND _chronon3d_install_targets
        chronon3d_backend_video
        chronon3d_media_video
    )
endif()
if(CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D)
    list(APPEND _chronon3d_install_targets chronon3d_backend_text)
endif()
if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    list(APPEND _chronon3d_install_targets
        chronon3d_ffmpeg_light
        chronon3d_ffmpeg_full
    )
endif()

# Blend2D paint utility is a transitive dep of chronon3d_backend_software
# and chronon3d_backend_text.  Built only when Blend2D is enabled.
if(CHRONON3D_USE_BLEND2D)
    list(APPEND _chronon3d_install_targets chronon3d_blend2d_paint)
endif()
# chronon3d_graph_pipeline pulls in chronon3d_content at runtime, so
# install content/diagnostics too when they were part of the build.
if(CHRONON3D_BUILD_CONTENT OR CHRONON3D_BUILD_DIAGNOSTICS)
    list(APPEND _chronon3d_install_targets chronon3d_content)
endif()
if(CHRONON3D_BUILD_DIAGNOSTICS)
    list(APPEND _chronon3d_install_targets chronon3d_diagnostics)
endif()

install(TARGETS ${_chronon3d_install_targets}
    EXPORT Chronon3DTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT Chronon3DTargets
    FILE Chronon3DTargets.cmake
    NAMESPACE Chronon3D::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Chronon3D
)

# Installable config: substitutes @PACKAGE_INIT@ / @OPTIONAL_…@ placeholders.
configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/Chronon3DConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Chronon3DConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Chronon3D
)

# Version file — consumers gate find_package() calls on COMPATIBILITY.
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/Chronon3DConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/Chronon3DConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/Chronon3DConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Chronon3D
)

# ── Aggregate developer target (CLI + fast tests) ──────────────────────────────
# Convenience target for daily development: builds the CLI and the fast test suite
# in a single invocation. Only created when both CLI and tests are enabled.
if(CHRONON3D_BUILD_CLI AND CHRONON3D_BUILD_TESTS)
    add_custom_target(chronon3d_dev_fast
        DEPENDS
            chronon3d_cli
            chronon3d_tests_fast
    )
endif()
