# src/text/CMakeLists.txt — Core text module (non-backend-specific)
# Exposes: chronon3d_text_core

add_library(chronon3d_text_core STATIC
    glyph_selector.cpp
    text_animator_property.cpp
    text_run.cpp
)

target_link_libraries(chronon3d_text_core PUBLIC chronon3d)
target_include_directories(chronon3d_text_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)
