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
    software_renderer.cpp
    software_backend.cpp
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
