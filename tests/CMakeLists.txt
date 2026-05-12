find_package(doctest CONFIG REQUIRED)

add_executable(chronon3d_tests
    test_main.cpp
    test_core.cpp
    test_math.cpp
    test_geometry.cpp
    test_animation.cpp
    test_timeline.cpp
    test_renderer.cpp
    test_scene.cpp
    test_deterministic.cpp
)

target_link_libraries(chronon3d_tests
    PRIVATE
        chronon3d
        doctest::doctest
)

add_test(NAME chronon3d_tests COMMAND chronon3d_tests)
