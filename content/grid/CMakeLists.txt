add_library(chronon3d_content_grid STATIC
    compositions/grid_showcase.cpp
)
set_target_properties(chronon3d_content_grid PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_content_grid PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_grid
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content_grid PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)
add_library(chronon3d::content_grid ALIAS chronon3d_content_grid)
