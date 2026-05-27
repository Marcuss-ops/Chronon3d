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
    effects/glow_01_neon_sign.cpp
    effects/glow_02_orb_galaxy.cpp
    effects/glow_03_cinematic_text.cpp
    effects/glow_04_ui_card.cpp
    effects/glow_05_pulse_wave.cpp
    effects/glow_06_video_layer.cpp
)
target_include_directories(chronon3d_content PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)
