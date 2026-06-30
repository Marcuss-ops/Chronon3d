# src/text/CMakeLists.txt — Core text module (non-backend-specific)
# Exposes: chronon3d_text_core

add_library(chronon3d_text_core OBJECT
    animated_text_document.cpp
    animation/glyph_instance_state.cpp
    animation/text_animator_evaluator.cpp
    animation/text_animator_stack.cpp
    animation/text_property_applier.cpp
    animation/text_tracking_evaluator.cpp
    every_line_composer.cpp
    glyph_selector.cpp
    path_sampler.cpp
    single_line_composer.cpp
    text_document.cpp
    text_path_composer.cpp
    text_resolver.cpp
    text_run.cpp
    text_run_builder.cpp
    text_run_driver.cpp
    text_run_geometry.cpp
    unicode/utf8_decoder.cpp
    unicode/whitespace.cpp
    boundary_resolver/text_unit_map.cpp
)

# NOTE: chronon3d_text_core does NOT declare its own PUBLIC include directory
# here.  Public header access is already provided transitively through the
# PUBLIC link to `chronon3d` (which has both $<BUILD_INTERFACE:…> and
# $<INSTALL_INTERFACE:include>).  Adding a redundant PUBLIC include dir here
# would leak the source-tree path when consumers try to import this target
# from the installed Chronon3DTargets.cmake.
#
# PRIVATE link to chronon3d_cache so cross-module `<chronon3d/cache/*.hpp>`
# headers (under chronon3d_cache's `include_private/`) become visible at
# compile time.  The cache target exposes include_private via INTERFACE
# $<BUILD_INTERFACE:…>, so PRIVATE-link consumers automatically gain
# `<chronon3d/cache/lru_cache.hpp>` etc. without leaking internal headers
# to downstreams.  Required for `src/text/text_run.cpp` which uses the
# generic LRU utility without coupling to chronon3d_cache's whole interface.
target_link_libraries(chronon3d_text_core
    PUBLIC  chronon3d
    PRIVATE chronon3d_cache
)
