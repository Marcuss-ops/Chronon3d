add_library(chronon3d_content_minimalist STATIC
    minimalist_animations.cpp
    minimalist_image_presets.cpp
    minimalist_module.cpp
)
set_target_properties(chronon3d_content_minimalist PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_content_minimalist PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_minimalist
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content_minimalist PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)
add_library(chronon3d::content_minimalist ALIAS chronon3d_content_minimalist)
