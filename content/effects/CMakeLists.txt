add_library(chronon3d_content_effects STATIC
    glow/glow_01_neon_sign.cpp
    glow/glow_02_orb_galaxy.cpp
    glow/glow_03_cinematic_text.cpp
    glow/glow_04_ui_card.cpp
    glow/glow_05_pulse_wave.cpp
    glow/glow_06_video_layer.cpp
    glow/glow_07_premium_suite.cpp
    glow/glow_v2_ab_tests.cpp
    thumbnails/premium_thumbnail_showcase.cpp
    ref_2_5d/reference_2_5d_suite.cpp
)
set_target_properties(chronon3d_content_effects PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_content_effects PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_effects
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content_effects PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)
add_library(chronon3d::content_effects ALIAS chronon3d_content_effects)
