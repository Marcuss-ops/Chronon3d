# ==============================================================================
# src/cache/CMakeLists.txt — chronon3d_cache
# ==============================================================================

# ── FlatBuffers schema for compiled artifacts ────────────────────────────
find_package(flatbuffers CONFIG REQUIRED)

if(NOT DEFINED CHRONON3D_FLATC)
    find_program(CHRONON3D_FLATC flatc REQUIRED)
endif()

set(CHRONON3D_ARTIFACT_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(CHRONON3D_ARTIFACT_SCHEMA "${CMAKE_SOURCE_DIR}/schemas/flatbuffers/chronon_artifact.fbs")
set(CHRONON3D_ARTIFACT_GENERATED_HEADER
    "${CHRONON3D_ARTIFACT_GENERATED_DIR}/chronon_artifact_generated.h")

add_custom_command(
    OUTPUT "${CHRONON3D_ARTIFACT_GENERATED_HEADER}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CHRONON3D_ARTIFACT_GENERATED_DIR}"
    COMMAND "${CHRONON3D_FLATC}"
        --cpp
        -o "${CHRONON3D_ARTIFACT_GENERATED_DIR}"
        "${CHRONON3D_ARTIFACT_SCHEMA}"
    DEPENDS "${CHRONON3D_ARTIFACT_SCHEMA}"
    COMMENT "Compiling FlatBuffers schema: chronon_artifact.fbs → C++"
    VERBATIM
)

# Publish the generated dir so other targets (daemon) can find the header.
set(CHRONON3D_ARTIFACT_GENERATED_DIR "${CHRONON3D_ARTIFACT_GENERATED_DIR}"
    CACHE INTERNAL "Artifact FlatBuffers generated header directory")

add_library(chronon3d_cache OBJECT
    cache_policy.cpp
    frame_cache.cpp
    node_cache.cpp
    framebuffer_pool.cpp
    framebuffer_pool_evict.cpp
    video_frame_cache.cpp
    lru_log.cpp
    cache_diagnostics.cpp
    cache_diagnostics_format.cpp
    compression/zstd_compressor.cpp
    compiled_artifact_cache.cpp
    "${CHRONON3D_ARTIFACT_GENERATED_HEADER}"
)
set_target_properties(chronon3d_cache PROPERTIES UNITY_BUILD OFF)
target_link_libraries(chronon3d_cache PUBLIC chronon3d xxHash::xxhash zstd::libzstd flatbuffers::flatbuffers)
target_include_directories(chronon3d_cache
    PRIVATE ${CHRONON3D_ARTIFACT_GENERATED_DIR}
)
target_precompile_headers(chronon3d_cache PRIVATE <xxhash.h>)

# All include_private/ headers have been promoted to the public
# include/chronon3d/cache/ surface.  The stale mirrors were deleted
# (they were byte-identical to or outdated copies of the public headers).
