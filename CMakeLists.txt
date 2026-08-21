cmake_minimum_required(VERSION 3.27)

project(Chronon3d
    VERSION 0.1.0
    DESCRIPTION "A modular C++20 3D engine/library"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND NOT DEFINED VCPKG_CHAINLOAD_TOOLCHAIN_FILE)
    message(WARNING "vcpkg toolchain not detected. Dependencies are strict and might fail to configure. Please use a CMake preset (e.g., --preset linux-release) or set CMAKE_TOOLCHAIN_FILE to vcpkg.cmake.")
endif()

# --- Opzioni di Compilazione Globali ---
add_compile_options(-ffp-contract=off)
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

if(NOT CMAKE_CXX_LINKER_LAUNCHER)
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

# --- Opzioni del Progetto --
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
option(CHRONON3D_ENABLE_TRACING "Enable timeline tracing (Tracy backend today; Perfetto migration in progress)" OFF)
option(CHRONON3D_ENABLE_TELEMETRY "Enable SQLite telemetry persistence" OFF)
option(CHRONON3D_ENABLE_DIAGNOSTICS "Enable engine-level diagnostics code (overlays, analysis, cache introspection)" OFF)
option(CHRONON3D_ENABLE_MESH "Enable mesh processing support" ON)
option(CHRONON3D_BUILD_C_API "Build the shared C ABI wrapper" ON)
option(CHRONON3D_ENABLE_VULKAN "Build the headless Vulkan backend" OFF)
option(CHRONON3D_ENABLE_CUDA_INTEROP
       "Build the CUDA/Vulkan external-memory interop probe and bridge prerequisites" OFF)
option(CHRONON3D_DEV_SPLIT_DWARF
       "Use split DWARF debug information in developer builds" OFF)
option(CHRONON3D_BUILD_TIME_TRACE
       "Emit Clang -ftime-trace JSON files for compile-time profiling" OFF)

if(CHRONON3D_DEV_SPLIT_DWARF AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        $<$<COMPILE_LANGUAGE:C,CXX>:$<$<CONFIG:Debug>:-gsplit-dwarf>>
        $<$<COMPILE_LANGUAGE:C,CXX>:$<$<CONFIG:RelWithDebInfo>:-gsplit-dwarf>>
    )
endif()

if(CHRONON3D_BUILD_TIME_TRACE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-ftime-trace>)
endif()

# ── Optional heavy dependency: Blend2D ──────────────────────────────────
# Blend2D is the only USE_ option because it is independent of its ENABLE_
# counterpart (ENABLE_TEXT gates freetype+harfbuzz but not Blend2D).
# All other optional deps use the ENABLE_* options exclusively.
option(CHRONON3D_USE_BLEND2D "Enable Blend2D software rendering backend" ON)

if(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    add_compile_definitions(CHRONON3D_ENABLE_NATIVE_FFMPEG)
endif()
if(CHRONON3D_ENABLE_CUDA_INTEROP)
    add_compile_definitions(CHRONON3D_ENABLE_CUDA_INTEROP)
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

if(CHRONON3D_ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
endif()

if(CHRONON3D_ENABLE_EXR)
    find_package(OpenEXR CONFIG REQUIRED)
endif()

if(CHRONON3D_ENABLE_TELEMETRY)
    find_package(unofficial-sqlite3 CONFIG REQUIRED)
endif()

if(CHRONON3D_BUILD_BENCHMARKS)
    find_package(benchmark CONFIG REQUIRED)
endif()

if(CHRONON3D_ENABLE_TRACING)
    find_package(Tracy CONFIG QUIET)
    find_package(unofficial-perfetto CONFIG REQUIRED)
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

# ── Central registry (FIX: must be included BEFORE add_subdirectory(src)) ─────
# `cmake/Chronon3DSdkInstall.cmake` derives `_chronon3d_install_targets`
# from `CHRONON3D_REGISTRY_OBJECT_LIBS` + `CHRONON3D_REGISTRY_INTERFACE_LIBS`.
# Without this include() the lists stay empty, the
# `install(TARGETS ... EXPORT Chronon3DTargets)` call has nothing to
# register, and the subsequent `install(EXPORT Chronon3DTargets ...)`
# fails with `INSTALL(EXPORT) given unknown export "Chronon3DTargets"`.
# Refactor regression introduced by the SDK install/export extraction
# commits (b1f24ff0 + 19abfe3b); this single-line include restores the
# canonical include-order documented at the top of
# `cmake/Chronon3DRegistry.cmake`.
include(${CMAKE_SOURCE_DIR}/cmake/Chronon3DRegistry.cmake)

add_subdirectory(src)

# 10. Content Generation (optional — proofs/demos, not core engine)
if(CHRONON3D_BUILD_CONTENT OR CHRONON3D_BUILD_DIAGNOSTICS)
    add_subdirectory(content)
endif()

# Late-binding: content is added AFTER src so its link deps exist,
# but Chronon3DSdkTargets.cmake (inside src) already ran.  Explicitly
# wire content objects into the SDK archive here.
if(TARGET chronon3d_content AND TARGET chronon3d_sdk_impl)
    target_sources(chronon3d_sdk_impl PRIVATE $<TARGET_OBJECTS:chronon3d_content>)
endif()

# Also wire content objects into chronon3d_pipeline so in-tree consumers
# (tests, CLI via sdk→pipeline) that link only the pipeline target get
# the register_content_modules symbol.  This mirrors the late-binding
# pattern used for chronon3d_sdk_impl above.
if(TARGET chronon3d_content AND TARGET chronon3d_pipeline)
    target_sources(chronon3d_pipeline INTERFACE $<TARGET_OBJECTS:chronon3d_content>)
endif()

# --- Profiling & Condizionali di Build ---
if(CHRONON3D_ENABLE_TRACING AND Tracy_FOUND)
    target_link_libraries(chronon3d_scene PRIVATE Tracy::TracyClient)
    target_compile_definitions(chronon3d_scene PRIVATE CHRONON3D_ENABLE_TRACING TRACY_ON_DEMAND)
    target_link_libraries(chronon3d_pipeline INTERFACE Tracy::TracyClient)
    target_compile_definitions(chronon3d_pipeline INTERFACE CHRONON3D_ENABLE_TRACING TRACY_ON_DEMAND)
endif()

# ── Perfetto tracing backend (Fase 1) ────────────────────────────────────
# The timeline-tracing surface (CHRONON_TRACE_*, TraceSession) lives in
# chronon3d_core_impl (src/core/tracing/*.cpp).  Perfetto is the canonical
# backend; Tracy remains as the transitional CHRONON_ZONE backend until the
# migration completes (Fase 5).  The compile definition is needed on
# core_impl so the tracing TUs see the enabled path.
if(CHRONON3D_ENABLE_TRACING)
    target_link_libraries(chronon3d_core_impl PRIVATE unofficial::perfetto::perfetto)
    target_compile_definitions(chronon3d_core_impl PRIVATE CHRONON3D_ENABLE_TRACING)
    # Final link closure: the static perfetto archive must reach every
    # consumer that pulls core_impl objects through the pipeline INTERFACE
    # (mirrors the Tracy INTERFACE pattern above).
    target_link_libraries(chronon3d_pipeline INTERFACE unofficial::perfetto::perfetto)
endif()

# Build CLI command-group libraries before the test definitions are loaded.
# Some focused SDK tests (notably render_job_contract_tests) intentionally link
# against chronon3d_cli_render and must see that target during configuration.
if(CHRONON3D_BUILD_CLI)
    add_subdirectory(apps/chronon3d_cli)
endif()

# Standalone bench binary (Phase 1 of TICKET-CLI-ISOLATE-RUNTIME-DEV cleanup).
# The bench subcommands (`bench`, `bench-convert`) used to live under
# apps/chronon3d_cli/commands/bench + commands/dev/command_bench_convert.cpp;
# they are extracted to a self-contained executable that owns its own CLI11
# entry point and only depends on chronon3d_sdk + chronon3d_pipeline.
if(CHRONON3D_BUILD_BENCHMARK_CLI)
    add_subdirectory(apps/chronon3d_bench)
endif()

# ── Test orchestration ───────────────────────────────────────────────────
# enable_testing() is invoked BEFORE the BUILD_TESTS gate so the
# standalone install-boundary test (add_test() registered further down)
# is registered with CTest even when CHRONON3D_BUILD_TESTS=OFF.  When
# BUILD_TESTS=ON, the per-area test executables still register as before.
enable_testing()

if(CHRONON3D_BUILD_TESTS)
    set(BUILD_TESTING ON CACHE BOOL "Build the testing tree.")
    target_compile_definitions(chronon3d_core_impl PUBLIC CHRONON_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ── Install-boundary CI gate (TICKET-011) ─────────────────────────────────
# Standalone consumer at `tests/install_consumer/` that uses ONLY the public
# Chronon3D::SDK target namespaced alias.  The orchestrator script:
#   1. Reuses the existing build artifact if `libchronon3d_sdk_impl.a` is
#      already produced (fast path; ~seconds).
#   2. Otherwise: cold reconfigure + build to an mktemp dir.
#   3. `cmake --install` to an mktemp prefix.
#   4. Validates that Chronon3DConfig.cmake + Chronon3DTargets.cmake +
#      libchronon3d_sdk_impl.a are present under the install prefix.
#   5. Configures + builds + runs the consumer and asserts the
#      [BOUNDARY-OK] marker.
#
# Wired into CI via the linux-ci preset cache var
# `CHRONON3D_BUILD_INSTALL_CONSUMER_TEST=ON`.  The consumer renders a
# real composition (text + camera → PNG) and verifies the output.
option(CHRONON3D_BUILD_INSTALL_CONSUMER_TEST
    "Run the end-to-end SDK install-boundary check via tools/install_consumer_test.sh. CTest registers install_consumer_ci; orchestrator exit code 2 maps to CTEST SKIP. Enable via the linux-ci preset or -DCHRONON3D_BUILD_INSTALL_CONSUMER_TEST=ON."
    OFF)
if(CHRONON3D_BUILD_INSTALL_CONSUMER_TEST AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(
        NAME install_consumer_ci
        COMMAND "${CMAKE_SOURCE_DIR}/tools/install_consumer_test.sh"
    )
    # TIMEOUT 900s: cold path reconfigures + builds the full SDK from
    # scratch (rare, but covered for legibility).  FAST path reuses the
    # existing build artifact and completes in seconds.
    set_tests_properties(install_consumer_ci PROPERTIES
        TIMEOUT 900
        LABELS "boundary;ci"
        ENVIRONMENT "CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE};CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH};SDK_BUILD=${CMAKE_BINARY_DIR};SDK_PREFIX=${CMAKE_BINARY_DIR}/install-consumer-prefix;CHRONON3D_INSTALL_TEST_PRESET=linux-ci-full-validation;CHRONON3D_INSTALL_TEST_FAST=1"
    )
endif()

# ── SDK Install / Export ─────────────────────────────────────────────────────
# Moved to `cmake/Chronon3DSdkInstall.cmake` so this root CMakeLists.txt
# stays a thin orchestrator.  The module derives the install + export
# target list from `cmake/Chronon3DRegistry.cmake` (replacing the
# previous hand-maintained ~40-entry list), runs GNUInstallDirs +
# CMakePackageConfigHelpers, and emits Chronon3DConfig.cmake,
# Chronon3DConfigVersion.cmake, Chronon3DTargets.cmake under
# `<prefix>/<libdir>/cmake/Chronon3D/`.  The (TICKET-011 cmake-boundary)
# public-surface contract is documented in that file's header — the
# EXACTLY-one public target is `Chronon3D::SDK`; inner targets remain
# importable under their raw names for transitive link closure.
include(${CMAKE_SOURCE_DIR}/cmake/Chronon3DSdkInstall.cmake)

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

# ── Hygiene gates ─────────────────────────────────────────────────────
# check_refs: invoke tools/check_filename_drift.sh to ensure every cited
# filename points to a path that exists on disk.  Complementary to
# check_doc_sync.sh (which enforces co-update, not existence).  Kept
# in a separate file to avoid mixing custom_targets with the
# OBJECT/INTERFACE library registry in Chronon3DRegistry.cmake.
include(cmake/Hygiene.cmake)

# ── WP0 PR 0.1 — chronon3d_architecture_check aggregate target ────────────
# The architecture-check target compiles (or verifies the previous build of)
# every architectural boundary layer the project uses:
#
#   • chronon3d_runtime          ─ engine-lifetime state owner (TICKET-011)
#   • chronon3d_graph_executor   ─ stateless graph executor (TICKET-009)
#   • chronon3d_graph_pipeline   ─ scene orchestration + tile dispatch
#   • chronon3d_backend_software ─ software backend core
#   • chronon3d_tests_fast       ─ guard tests that no boundary regresses
#   • chronon3d_sdk_impl         ─ single aggregated SDK archive (boundary proof)
#
# NO disjoint compile targets are introduced — every dependency listed above
# either already exists in src/CMakeLists.txt or src/<sub>/CMakeLists.txt.
# The custom_target is registered UNCONDITIONALLY so CI / `cmake --build
# --target chronon3d_architecture_check` works without the BUILD_TESTS gate.
# If a CONDITIONAL target is not present in the current configuration
# (e.g. CHRONON3D_BUILD_TESTS=OFF), `if(TARGET …)` guards drop it silently.
#
# The companion `tools/check_architecture_boundaries.sh` script (WP0 PR 0.2)
# is the RUNTIME boundary verifier — it is invoked by CI and the
# `chronon3d_dev_fast` aggregate does NOT wrap it (the script is fast and
# idempotent; running it directly is preferable to nesting it into a custom
# target).
add_custom_target(chronon3d_architecture_check)
if(TARGET chronon3d_runtime)
    add_dependencies(chronon3d_architecture_check chronon3d_runtime)
endif()
if(TARGET chronon3d_graph_executor)
    add_dependencies(chronon3d_architecture_check chronon3d_graph_executor)
endif()
if(TARGET chronon3d_graph_pipeline)
    add_dependencies(chronon3d_architecture_check chronon3d_graph_pipeline)
endif()
if(TARGET chronon3d_backend_software)
    add_dependencies(chronon3d_architecture_check chronon3d_backend_software)
endif()
if(TARGET chronon3d_sdk_impl)
    add_dependencies(chronon3d_architecture_check chronon3d_sdk_impl)
endif()
# Fast tests are gate here only when actually enabled — gated on BUILD_TESTS
# to avoid `add_dependencies` against a non-existent target in stripped
# configurations (e.g. the install_consumer sandbox).
add_executable(chronon3d_asset_text_bench
    tools/chronon3d_asset_text_bench.cpp
    examples/bench_micro/asset_text_bench_scenes.cpp
)
target_link_libraries(chronon3d_asset_text_bench
    PRIVATE
        chronon3d
        chronon3d_runtime
        chronon3d_pipeline
        chronon3d_backend_software
        chronon3d_backend_vulkan
        chronon3d_content
)


