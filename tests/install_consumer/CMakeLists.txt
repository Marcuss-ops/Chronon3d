cmake_minimum_required(VERSION 3.27)

project(install_consumer
    VERSION 0.0.1
    LANGUAGES CXX
)

# Standalone install-boundary project. Every executable links only the exported
# Chronon3D::SDK target and includes explicit installed headers.
find_package(Chronon3D CONFIG REQUIRED)

if(NOT TARGET Chronon3D::SDK)
    message(FATAL_ERROR
        "Install boundary broken: Chronon3D::SDK target absent")
endif()

function(add_chronon_consumer target source)
    add_executable(${target} ${source})
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_link_libraries(${target} PRIVATE Chronon3D::SDK)
endfunction()

add_chronon_consumer(check_install main.cpp)
add_chronon_consumer(check_text main_text.cpp)
add_chronon_consumer(check_camera main_camera.cpp)
add_chronon_consumer(check_full main_full.cpp)

# Focused authoring-assets boundary. This target proves that the installed SDK
# ships the explicit authoring headers and supports the documented asset(path)
# syntax for both images and fonts without any umbrella include.
add_chronon_consumer(check_assets main_assets.cpp)

# Synthetic image fixture for the existing full-surface consumer.
add_custom_command(TARGET check_full POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:check_full>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/test_image.png
        $<TARGET_FILE_DIR:check_full>/assets/test_image.png
    COMMENT "[install_consumer] Copying test_image.png to assets/ for check_full runtime"
    VERBATIM
)
