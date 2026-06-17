add_library(chronon3d_content_shapes STATIC
    proofs/shape_proofs.cpp
    motion/shape_motion_proofs.cpp
)
# shape_registrations.cpp was merged into shapes_module.cpp
# (legacy file retained as comment placeholder)
set_target_properties(chronon3d_content_shapes PROPERTIES UNITY_BUILD OFF)

# GCC 14.2 ICE workaround: magic_enum.hpp triggers internal compiler error
# in wide_int_to_tree_1 (tree.cc:1761) at -O3 with GCC 14.
# Lower optimization to -O1 for the affected files only.
# Skip PCH on these files to avoid __NO_INLINE__ mismatch.
set_source_files_properties(
    proofs/shape_proofs.cpp
    motion/shape_motion_proofs.cpp
    PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON
        COMPILE_FLAGS "-O1"
)

target_include_directories(chronon3d_content_shapes PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_shapes
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content_shapes PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)
add_library(chronon3d::content_shapes ALIAS chronon3d_content_shapes)
