# src/text/CMakeLists.txt — Core text module (non-backend-specific)
# Exposes: chronon3d_text_core

add_library(chronon3d_text_core STATIC
    animated_text_document.cpp
    every_line_composer.cpp
    glyph_selector.cpp
    path_sampler.cpp
    single_line_composer.cpp
    text_animator_property.cpp
    text_document.cpp
    text_path_composer.cpp
    text_resolver.cpp
    text_run.cpp
    text_run_builder.cpp
    text_run_driver.cpp
    text_run_geometry.cpp
)

target_link_libraries(chronon3d_text_core PUBLIC chronon3d)
target_include_directories(chronon3d_text_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)
