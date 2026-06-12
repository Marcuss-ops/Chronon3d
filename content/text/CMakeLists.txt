add_library(chronon3d_content_text STATIC
    text_module.cpp
)
set_target_properties(chronon3d_content_text PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_content_text PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_text
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content_text PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)
add_library(chronon3d::content_text ALIAS chronon3d_content_text)
