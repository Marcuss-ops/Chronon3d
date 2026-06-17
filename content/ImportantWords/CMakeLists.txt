add_library(chronon3d_content_important_words STATIC
    important_words_animations.cpp
)
set_target_properties(chronon3d_content_important_words PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_content_important_words PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_important_words
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content_important_words PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)
add_library(chronon3d::content_important_words ALIAS chronon3d_content_important_words)
