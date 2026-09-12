# Chronon3D test bootstrap and deterministic per-area registration.
find_package(doctest CONFIG REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
include(doctest)

set(TEST_MAIN test_main.cpp)

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

if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_gpu_corpus_manifest.py")
    add_test(
        NAME chronon3d_gpu_corpus_manifest_tests
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/test_gpu_corpus_manifest.py"
    )
endif()

# Shader/CPU layout contract for the text-run glyph instance (fixes.md B.5/4.4).
# Pure stdlib Python: the scalar-slot and sizeof checks need no toolchain, and the
# compiled-spirv stride check reports its skip explicitly when glslangValidator is
# absent, so this stays runnable on any host.
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_shader_layout_contract.py")
    add_test(
        NAME chronon3d_shader_layout_contract_tests
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/test_shader_layout_contract.py"
    )
endif()

# Behavioural tests for the steady-state allocation gate's revision binding
# (fixes.md B.4): a historical baseline PASS must be disclosed, not trusted.
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_steady_state_allocation_gate.py")
    add_test(
        NAME chronon3d_steady_state_allocation_gate_tests
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/test_steady_state_allocation_gate.py"
    )
endif()
