add_library(chronon3d_content_special_names STATIC
    special_names_animations.cpp
    special_names_module.cpp
)
set_target_properties(chronon3d_content_special_names PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_content_special_names PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_special_names
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content_special_names PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)
add_library(chronon3d::content_special_names ALIAS chronon3d_content_special_names)
