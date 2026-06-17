add_library(chronon3d_content_anims STATIC
    compositions/animation_compositions.cpp
    compositions/text_animations.cpp
    compositions/catmull_rom_showcase.cpp
    compositions/dolly_zoom_showcase.cpp
    compositions/camera_spline_comparison.cpp
    compositions/cinematic_title_reveal.cpp
    compositions/tilt_sweep_title_v2.cpp
)
set_target_properties(chronon3d_content_anims PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_content_anims PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_anims
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content_anims PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)
add_library(chronon3d::content_anims ALIAS chronon3d_content_anims)
