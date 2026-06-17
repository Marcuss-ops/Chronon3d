add_library(chronon3d_content_images STATIC
    images_module.cpp
    compositions/image_test_patterns.cpp
    compositions/image_animated_reveals.cpp
    compositions/image_proofs.cpp
)
set_target_properties(chronon3d_content_images PROPERTIES UNITY_BUILD OFF)
target_include_directories(chronon3d_content_images PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(chronon3d_content_images
    PUBLIC chronon3d
    PRIVATE chronon3d_pipeline
)
target_precompile_headers(chronon3d_content_images PRIVATE
    <glm/glm.hpp> <spdlog/spdlog.h> <fmt/core.h>
)
add_library(chronon3d::content_images ALIAS chronon3d_content_images)
