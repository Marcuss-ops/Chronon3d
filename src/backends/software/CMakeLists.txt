# ==============================================================================
# src/backends/software/CMakeLists.txt — chronon3d_backend_software
# ==============================================================================

# TICKET-011 cmake-boundary (OBJECT lift) — chronon3d_backend_software is now
# an OBJECT library; its compiled objects are aggregated into
# chronon3d_sdk_impl.a (see src/CMakeLists.txt).  Tests / CLI executables
# resolve the .o files transitively through chronon3d_pipeline → OBJECT deps.
add_library(chronon3d_backend_software OBJECT
    # Top-level files only — subsystem sources are in per-directory CMakeLists.txt
    software_compositor.cpp
    # M1.5#12 — SoftwareRenderer split: main = lifecycle + RenderRuntime forwarders;
    # 4 companion TUs hold construction / dispatch / text / effects per the
    # `software_backend_factory.cpp` M1.5#12 pattern (1/4 factory + dispatch +
    # text + effects).  No new public API.  ABI invariant.  Move ops confined
    # to software_renderer.cpp (ODR — single TU).
    software_renderer.cpp
    software_renderer_factory.cpp    # M1.5#12 -TICKET-M1.5#12 1/4 — 2 construction ctors
    software_renderer_dispatch.cpp   # M1.5#12 2/4 — render/draw_node/composite/apply_blur/apply_per_pixel_dof
    software_renderer_text.cpp       # M1.5#12 3/4 — preflight + text-aware render_scene + accessors + RAII
    software_renderer_effects.cpp    # M1.5#12 4/4 — apply_effect_stack single forwarder
    software_backend.cpp
    software_backend_factory.cpp   # M1.5#12 1/4 — make_software_backend() factory (UNIQUE validation source)
    software_processor_context.cpp
    # M1.5#7 — OOL ctor/dtor for `SoftwareSessionResources` (needed because
    # the default constructor now std::make_unique's a `TextRenderResources`
    # via raw new and must delete it on destruction — the prior inline `= default`
    # ctor was sufficient when `TextRenderResources*` defaulted to nullptr).
    software_session_resources.cpp
    # TICKET-118 — internal bridge that lets SoftwareBackend build a complete
    # `SoftwareProcessorContext` without holding a SoftwareRenderer* back-pointer.
    # Strictly under src/; never installed; not in include/chronon3d/.
    internal/software_processor_services.cpp
    # TICKET-121 — Phase 1 frame-bound allocation counter (thread_local, no
    # public API).  Lives alongside TICKET-118 internal bridge; the same
    # $<BUILD_INTERFACE:...> pattern keeps it strictly under src/.
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
        # text_run_processor.cpp calls compute_text_run_world_bbox() which
        # now lives in chronon3d_text_core (text_run_geometry.cpp).
        $<$<BOOL:${CHRONON3D_ENABLE_TEXT}>:chronon3d_text_core>
)
if(CHRONON3D_USE_BLEND2D)
    target_link_libraries(chronon3d_backend_software PRIVATE chronon3d_blend2d_paint)
endif()
if(CHRONON3D_USE_BLEND2D)
    target_link_libraries(chronon3d_backend_software PUBLIC blend2d::blend2d)
endif()
if(TARGET chronon3d_backend_text)
    target_link_libraries(chronon3d_backend_software PUBLIC chronon3d_backend_text)
    # Expose to the compiler so callers (e.g. software_renderer.cpp) can guard
    # text-backend-only initializers.  Prevents link errors when Blend2D /
    # text backend are disabled.
    # PUBLIC: required so all consumers (chronon3d_runtime, chronon3d_sdk_impl)
    # see the same SoftwareProcessorContext/ProcessorSourceExtras layout.
    # PRIVATE was an ODR violation — see TICKET-GATE-10 stack-smashing fix.
    target_compile_definitions(chronon3d_backend_software PUBLIC CHRONON3D_HAS_BACKEND_TEXT)
endif()

if(CHRONON3D_BUILD_DIAGNOSTICS)
    target_link_libraries(chronon3d_backend_software PRIVATE chronon3d_backend_software_diagnostics)
endif()
# TICKET-118 — BUILD_INTERFACE include dir exposes the new internal bridge
# (`internal/software_processor_services.hpp`) ONLY to in-build consumers
# (sibling TUs in the same library, plus test binaries during the build).
# The header is NEVER installed — `$<BUILD_INTERFACE:...>` is the canonical
# CMake idiom that satisfies the "no source-tree-prefixed paths in
# INTERFACE_INCLUDE_DIRECTORIES" check while still propagating the directory
# for compile-time ""-includes (`#include "internal/software_processor_services.hpp"`).
# If we ever provide an install rule, we will need to revisit — for now,
# the internal bridge stays strictly under src/.
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
