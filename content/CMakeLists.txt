add_library(chronon3d_content STATIC
    text/text_compositions.cpp
    anims/animation_compositions.cpp
    images/image_compositions.cpp
    2d5/two_point_five_d_compositions.cpp
)
target_include_directories(chronon3d_content PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)
