# Chronon3D test bootstrap and deterministic per-area registration.
find_package(doctest CONFIG REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
include(doctest)

set(TEST_MAIN test_main.cpp)

function(chronon3d_enable_test_pch target_name)
    # Disabled on resource-constrained build hosts.
endfunction()

include(${CMAKE_SOURCE_DIR}/cmake/Chronon3DTestSuite.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/manifests/test_definitions.cmake)

foreach(definition IN LISTS CHRONON3D_TEST_DEFINITIONS)
    set(path "${CMAKE_CURRENT_SOURCE_DIR}/${definition}")
    set_property(
        DIRECTORY APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${path}"
    )
    include("${path}")
endforeach()

include(${CMAKE_CURRENT_SOURCE_DIR}/manifests/test_aggregates.cmake)

# Report-driven performance contract test. It is intentionally separate from
# the C++ benchmark binaries: the gate is pure stdlib Python and must remain
# runnable even when Google Benchmark is disabled.
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_common_performance_gate.py")
    add_test(
        NAME chronon3d_common_performance_gate_tests
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/test_common_performance_gate.py"
    )
endif()

# Canonical source-registry artifact consumed by the registration gate.
get_property(CHRONON3D_ALL_REGISTERED_TEST_SOURCES
    GLOBAL
    PROPERTY CHRONON3D_REGISTERED_TEST_SOURCES
)
file(WRITE
    "${CMAKE_BINARY_DIR}/registered_test_sources.txt"
    "${CHRONON3D_ALL_REGISTERED_TEST_SOURCES}"
)
