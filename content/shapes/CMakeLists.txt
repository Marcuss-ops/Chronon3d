add_library(chronon3d_content_shapes STATIC
    proofs/shape_proofs.cpp
    motion/shape_motion_proofs.cpp
    registration/shape_registrations.cpp
)
set_target_properties(chronon3d_content_shapes PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_content_shapes PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_shapes
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content_shapes PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)
add_library(chronon3d::content_shapes ALIAS chronon3d_content_shapes)
