# src/CMakeLists.txt

add_subdirectory(core)
add_subdirectory(cache)
add_subdirectory(effects)
add_subdirectory(registry)
add_subdirectory(assets)
add_subdirectory(backends/image)
add_subdirectory(media)
if(CHRONON3D_ENABLE_TEXT AND CHRONON3D_USE_BLEND2D)
    add_subdirectory(backends/text)
endif()
add_subdirectory(backends/assets)
if(CHRONON3D_ENABLE_VULKAN)
    add_subdirectory(backends/vulkan)
endif()
add_subdirectory(render_graph)
add_subdirectory(runtime)
if(CHRONON3D_ENABLE_VERIFICATION)
    add_subdirectory(verification)
endif()
if(CHRONON3D_ENABLE_VIDRUSH)
    add_subdirectory(vidrush)
endif()
add_subdirectory(render_plan)
add_subdirectory(extension)
add_subdirectory(backends/software)
add_subdirectory(scene)
add_subdirectory(timeline)
add_subdirectory(animations)
if(CHRONON3D_ENABLE_TEXT)
    add_subdirectory(text)
endif()
if(CHRONON3D_ENABLE_IPC)
    add_subdirectory(ipc)
endif()

if(MSVC AND CHRONON3D_PARALLEL_MSVC)
    file(GLOB_RECURSE CHRONON3D_SRC_CPP CONFIGURE_DEPENDS
         "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
    if(CHRONON3D_SRC_CPP)
        set_source_files_properties(${CHRONON3D_SRC_CPP} PROPERTIES COMPILE_OPTIONS "/MP")
    endif()
endif()

add_library(chronon3d_core
    scene3d/scene3d.cpp
    scene3d/manifest.cpp
    scene3d/import_gltf.cpp
    scene3d/material.cpp
    scene3d/mesh_cache.cpp
    scene3d/animation.cpp
    scene3d/loader/obj_loader.cpp
    scene3d/loader/mtl_loader.cpp
)

add_library(chronon3d::core ALIAS chronon3d_core)

target_include_directories(chronon3d_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(chronon3d_core
    PUBLIC chronon3d_compile_config
    PRIVATE nlohmann_json::nlohmann_json
)

if (CHRONON3D_ENABLE_MESH)
  target_link_libraries(chronon3d_core PUBLIC meshoptimizer::meshoptimizer)
  target_compile_definitions(chronon3d_core PUBLIC CHRONON3D_ENABLE_MESH=1)
endif()

if(MSVC)
    target_compile_options(chronon3d_core PRIVATE /W4 /permissive-)
    if(CHRONON3D_WARNINGS_AS_ERRORS)
        target_compile_options(chronon3d_core PRIVATE /WX)
    endif()
else()
    target_compile_options(chronon3d_core PRIVATE
        -Wall -Wextra -Wshadow -Wconversion -Wpedantic)
    if(CHRONON3D_WARNINGS_AS_ERRORS)
        target_compile_options(chronon3d_core PRIVATE -Werror)
    endif()
endif()

set_target_properties(chronon3d_core PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
