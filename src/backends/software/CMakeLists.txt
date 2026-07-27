# ==============================================================================
# src/backends/software/CMakeLists.txt — chronon3d_backend_software
# ==============================================================================

# Software backend objects are aggregated into the SDK implementation target.
add_library(chronon3d_backend_software OBJECT
    # Subsystem sources are registered by their directory CMakeLists files.
    software_compositor.cpp
    software_renderer.cpp
    software_renderer_factory.cpp
    software_renderer_dispatch.cpp
    software_renderer_text.cpp
    software_renderer_effects.cpp
    software_backend.cpp
    software_backend_factory.cpp
    software_processor_context.cpp
    software_session_resources.cpp
    # Internal bridge, never installed as public API.
    internal/software_processor_services.cpp
    internal/frame_alloc_counter.cpp
    runtime_adapter.cpp
    buffer_ring.cpp
    scratch_buffer.cpp
)
set_target_properties(chronon3d_backend_software PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_backend_software
    PUBLIC
        chronon3d
        chronon3d_graph
        chronon3d_cache
        chronon3d_effects
        chronon3d_backend_assets
        chronon3d_backend_image
    PRIVATE
        spdlog::spdlog_header_only
        hwy::hwy
        fmt::fmt
        xxHash::xxhash
        TBB::tbb
        $<$<BOOL:${CHRONON3D_ENABLE_TEXT}>:chronon3d_text_core>
)
if(CHRONON3D_USE_BLEND2D)
    target_link_libraries(
        chronon3d_backend_software
        PUBLIC blend2d::blend2d
        PRIVATE chronon3d_blend2d_paint
    )
endif()
if(TARGET chronon3d_backend_text)
    target_link_libraries(chronon3d_backend_software PUBLIC chronon3d_backend_text)
    # Keep capability layout consistent for all consumers of the context.
    target_compile_definitions(chronon3d_backend_software PUBLIC CHRONON3D_HAS_BACKEND_TEXT)
endif()

if(CHRONON3D_BUILD_DIAGNOSTICS)
    target_link_libraries(chronon3d_backend_software PRIVATE chronon3d_backend_software_diagnostics)
endif()
# Expose internal bridge headers only to in-tree consumers.
target_include_directories(chronon3d_backend_software
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
)

target_precompile_headers(chronon3d_backend_software PRIVATE
    <chronon3d/detail/pch.hpp>
)

# ==============================================================================
# Subsystem source files (each directory adds its sources via target_sources)
# ==============================================================================
add_subdirectory(processors)
add_subdirectory(effects)
add_subdirectory(rasterizers)
add_subdirectory(sampling)
add_subdirectory(utils)
add_subdirectory(blend2d)
add_subdirectory(diagnostics)
add_subdirectory(simd)
add_subdirectory(kernels)
add_subdirectory(specialized)

# ── AVX2 backend for the SIMD registry (ADR-025 kAvx2Set) ────────────────
# Per-file compile gate via a CMake try-compile so the binary never
# contains AVX2 instructions on toolchains/hosts that cannot produce
# them (avoiding illegal-instruction faults on pre-Haswell deployment
# hosts). Compiler-aware: MSVC uses /arch:AVX2, GCC/Clang use -mavx2/-mfma.
include(CheckCXXSourceCompiles)
set(_CRON_ISA_AVX2_TMP "${CMAKE_REQUIRED_FLAGS}")
unset(_cron_avx2_flags)
if(MSVC)
    set(CMAKE_REQUIRED_FLAGS "/arch:AVX2")
    set(_cron_avx2_flags /arch:AVX2)
else()
    set(CMAKE_REQUIRED_FLAGS "-mavx2;-mfma")
    set(_cron_avx2_flags "-mavx2" "-mfma")
endif()
check_cxx_source_compiles(
    "
#include <immintrin.h>
int main() { __m256 v = _mm256_setzero_ps(); (void)v; return 0; }
"
    CHRONON3D_ISA_HAVE_AVX2
)
set(CMAKE_REQUIRED_FLAGS "${_CRON_ISA_AVX2_TMP}")
if(CHRONON3D_ISA_HAVE_AVX2)
    message(STATUS "Enabling AVX2 backend for chronon3d_backend_software (kAvx2Set wired)")
    target_compile_definitions(chronon3d_backend_software PUBLIC CHRONON3D_ISA_BACKEND_AVX2)
    # Per-source-file flags: ONLY the AVX2 TU emits -mavx2/-mfma code. The
    # rest of `chronon3d_backend_software` stays portable so the binary
    # can ship to pre-Haswell deployment hosts without an illegal-
    # instruction fault (dispatch never reaches AVX2 if
    # CpuCapabilities::has_avx2 is false, but the static instructions
    # would otherwise be present in any TU that consumed the same flag).
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/simd/avx2_pixel_kernels.cpp
        PROPERTIES COMPILE_OPTIONS "${_cron_avx2_flags}"
    )
    target_sources(chronon3d_backend_software PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/simd/avx2_pixel_kernels.cpp
    )
endif()
unset(_CRON_ISA_AVX2_TMP)
unset(_cron_avx2_flags)
