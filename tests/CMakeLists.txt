# Chronon3D test bootstrap and deterministic per-area registration.
find_package(doctest CONFIG REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
include(doctest)

# Effect recipes are owned by the sibling ChrononPlugin checkout. The product
# core never links this path; exposing it here only lets in-workspace test
# builds compile the plugin's RenderPlan recipe contract checks.
set(CHRONONPLUGIN_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../ChrononPlugin"
    CACHE PATH "ChrononPlugin source tree used by optional cross-repo tests")
if(EXISTS "${CHRONONPLUGIN_SOURCE_DIR}/plugins/effects/include")
    include_directories("${CHRONONPLUGIN_SOURCE_DIR}/plugins/effects/include")
endif()

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

# Cross-repo V3 bridge drift guard (ChrononMotion's
# tests/chronon3d_v3_bridge_regression.cpp).
#
# This is the one ChrononMotion test that must run inside a Chronon3D build
# tree: only here do the canonical RenderPlan consumer symbols
# (validate_render_plan / decode_render_plan) exist to link against. It carries
# its own main (the ChrononMotion suite has no doctest), hence NO_DOCTEST. It
# stays on the aggregate pipeline rather than NO_PIPELINE: linking the
# render-plan OBJECT library pulls in effects/scene/assets/text symbols that the
# pipeline resolves. This is the documented exception to the sibling project's
# CHRONONMOTION_BUILD_TESTS=OFF, so the producer/consumer contract between the
# two RenderPlan writers and Chronon3D's schema has an executable guard.
if(CHRONON3D_ENABLE_MOTION_ADAPTER
   AND TARGET chrononmotion_chronon3d_adapter
   AND EXISTS "${_chrononmotion_dir}/tests/chronon3d_v3_bridge_regression.cpp")
    chronon3d_add_test_suite(
        NAME chrononmotion_v3_bridge_regression
        TIER UNIT
        NO_DOCTEST
        EXTRA_LINK_TARGETS
            chrononmotion_chronon3d_adapter
            chronon3d_render_plan
            chronon3d_compiler
        SOURCES
            "${_chrononmotion_dir}/tests/chronon3d_v3_bridge_regression.cpp"
    )
    # Ask the bridge test for the canonical consumer round-trip; standalone
    # ChrononMotion builds compile it without this and report NOT RUN.
    target_compile_definitions(chrononmotion_v3_bridge_regression PRIVATE
        CHRONONMOTION_HAS_CHRONON3D_CONSUMER=1)
endif()

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
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/test_gpu_lane_profiles.py")
    add_test(
        NAME chronon3d_gpu_lane_profile_contract_tests
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/test_gpu_lane_profiles.py"
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

