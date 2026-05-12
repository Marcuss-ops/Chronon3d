find_package(doctest CONFIG REQUIRED)

add_executable(chronon3d_tests
    test_main.cpp
    test_core.cpp
)

target_link_libraries(chronon3d_tests
    PRIVATE
        chronon3d
        doctest::doctest
)

add_test(NAME chronon3d_tests COMMAND chronon3d_tests)
