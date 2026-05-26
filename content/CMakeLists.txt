add_library(chronon3d_content STATIC
    text/text_helpers.cpp
    text/text_titles.cpp
    text/text_effects.cpp
    text/text_layout.cpp
    text/text_scrolling.cpp
    text/text_typewriter.cpp
    text/text_registrations.cpp
    text/lil_dirk.cpp
    anims/animation_compositions.cpp
    images/image_compositions.cpp
    2d5/two_point_five_d_compositions.cpp
    effects/glow_test_compositions.cpp
)
target_include_directories(chronon3d_content PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)
