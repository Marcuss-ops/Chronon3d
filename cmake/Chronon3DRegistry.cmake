# ==============================================================================
# cmake/Chronon3DRegistry.cmake — Central module registry
#
# Single source of truth for build-graph targets consumed by SDK aggregation,
# install/export generation, and the public dependency contract. Conditional
# targets stay in the registry; downstream foreach-if-TARGET loops naturally
# skip modules disabled by CHRONON3D_* feature gates.
# ==============================================================================

set(CHRONON3D_REGISTRY_OBJECT_LIBS
    chronon3d_render_plan
    chronon3d_render_plan_compiler

    # Core
    chronon3d_core_impl

    # Animations
    chronon3d_animations

    # Cache
    chronon3d_cache

    # Effects
    chronon3d_effects

    # Registry
    chronon3d_registry

    # Assets
    chronon3d_assets

    # Scene
    chronon3d_scene

    # Runtime
    chronon3d_runtime

    # Verification — CHRONON3D_ENABLE_VERIFICATION
    chronon3d_verification

    # Extension — canonical composition registration boundary
    chronon3d_extension

    # Media execution
    chronon3d_media_execution

    # Backends
    chronon3d_backend_assets
    chronon3d_backend_image
    chronon3d_backend_software
    chronon3d_backend_vulkan

    # Render graph
    chronon3d_graph_builder
    chronon3d_graph_cache
    chronon3d_graph_compiler
    chronon3d_graph_core
    chronon3d_graph_executor
    chronon3d_graph_nodes
    chronon3d_graph_pipeline
    chronon3d_graph_preflight

    # Conditional OBJECT targets
    chronon3d_backend_software_diagnostics   # CHRONON3D_ENABLE_DIAGNOSTICS
    chronon3d_backend_text                   # CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D
    chronon3d_backend_video                  # CHRONON3D_ENABLE_VIDEO
    chronon3d_blend2d_paint                 # CHRONON3D_USE_BLEND2D
    chronon3d_ipc                           # CHRONON3D_ENABLE_IPC
    chronon3d_media_video                   # CHRONON3D_ENABLE_VIDEO
    chronon3d_text_core                     # CHRONON3D_ENABLE_TEXT
)

# INTERFACE / STATIC / aggregate libraries. These are not OBJECT targets and
# must not be consumed through $<TARGET_OBJECTS:...>.
set(CHRONON3D_REGISTRY_INTERFACE_LIBS
    chronon3d_base
    chronon3d_sdk
    chronon3d_sdk_impl
    chronon3d_pipeline
    chronon3d_core
    chronon3d_software
    chronon3d_graph
    chronon3d_media_interface
    chronon3d
    chronon3d_ffmpeg_light
    chronon3d_path_cache
    chronon3d_ffmpeg_full
    chronon3d_media_native
)

# Public SDK dependencies: one SSOT drives both the SDK install-interface link
# contract and generated find_dependency() calls. IPC/FlatBuffers is appended
# only when IPC is actually built so lean consumers do not inherit it.
set(CHRONON3D_SDK_PUBLIC_DEPS
    "glm::glm|glm"
    "fmt::fmt|fmt"
    "spdlog::spdlog_header_only|spdlog"
    "TBB::tbb|TBB"
    "magic_enum::magic_enum|magic_enum"
    "nlohmann_json::nlohmann_json|nlohmann_json"
    "xxHash::xxhash|xxHash"
    "zstd::libzstd|zstd"
)
if(CHRONON3D_ENABLE_IPC)
    list(APPEND CHRONON3D_SDK_PUBLIC_DEPS
        "flatbuffers::flatbuffers|flatbuffers"
    )
endif()

set(_chronon3d_find_dep_lines "")
foreach(_entry IN LISTS CHRONON3D_SDK_PUBLIC_DEPS)
    string(REPLACE "|" ";" _pair "${_entry}")
    list(GET _pair 1 _pkg_name)
    string(APPEND _chronon3d_find_dep_lines "find_dependency(${_pkg_name} CONFIG)\n")
endforeach()
set(CHRONON3D_FIND_DEPENDENCY_LINES "${_chronon3d_find_dep_lines}"
    CACHE INTERNAL "Auto-generated find_dependency lines (rebuilt every configure)" FORCE)

function(chronon3d_link_registered_objects_into_archive target)
    foreach(obj IN LISTS CHRONON3D_REGISTRY_OBJECT_LIBS)
        if(TARGET ${obj})
            target_link_libraries(${target} PRIVATE ${obj})
        endif()
    endforeach()
endfunction()

function(chronon3d_expose_registered_objects_to_build_interface target)
    foreach(obj IN LISTS CHRONON3D_REGISTRY_OBJECT_LIBS)
        if(TARGET ${obj})
            target_sources(${target} INTERFACE
                $<BUILD_INTERFACE:$<TARGET_OBJECTS:${obj}>>)
        endif()
    endforeach()
endfunction()
