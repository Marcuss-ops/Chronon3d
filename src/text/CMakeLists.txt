# src/text/CMakeLists.txt — Core text module (non-backend-specific)
# Exposes: chronon3d_text_core

add_library(chronon3d_text_core OBJECT
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
    timed_text_parser.cpp
)

# NOTE: chronon3d_text_core does NOT declare its own PUBLIC include directory
# here.  Public header access is already provided transitively through the
# PUBLIC link to `chronon3d` (which has both $<BUILD_INTERFACE:…> and
# $<INSTALL_INTERFACE:include>).  Adding a redundant PUBLIC include dir here
# would leak the source-tree path when consumers try to import this target
# from the installed Chronon3DTargets.cmake.
target_link_libraries(chronon3d_text_core PUBLIC chronon3d)
