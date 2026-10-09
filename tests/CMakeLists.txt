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

# Geo core (ADR-034): standalone main, no doctest. Registers through the
# canonical suite helper so the recursive test-registration gate stays green;
# it must run before the aggregates snapshot CHRONON3D_ALL_TEST_TARGETS. The
# suite still compiles the geo object library directly and links only the
# geo dependencies, so it does not depend on the aggregate pipeline.
if(CHRONON3D_ENABLE_GEO)
    chronon3d_add_test_suite(
        NAME chronon3d_geo_core_test
        TIER UNIT
        NO_PIPELINE
        NO_DOCTEST
        LINK_TARGETS
            Clipper2::Clipper2
            nlohmann_json::nlohmann_json
        SOURCES
            geo/geo_core_test.cpp
            $<TARGET_OBJECTS:chronon3d_geo>
    )
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

# Cartography tool self-test. The map plates (tools/cartography/
# map_cartography.py) and the georeferencing that places markers on them
# (ChrononMotion3D's MapWindow) are two implementations of one projection, and
# the tool owns the reference values. This locks the four Mercator rows that
# ChrononMotion3D's tests/motion_geospatial.cpp asserts against its own
# implementation, so a drift between the two sides fails a build here instead of
# silently moving every marker off its pixel.
#
# Offline by construction: the self-test never fetches a tile, only the plate
# commands do. It is stdlib-only for the geodesy, which is what this
# interpreter is: the raster assertions (bilinear, Horn hillshade, relief bake)
# need numpy and Pillow, so they disclose their skip on a host without them
# rather than turning a missing pip package into a red build.
set(CHRONON3D_CARTOGRAPHY_TOOL "${CMAKE_SOURCE_DIR}/tools/cartography/map_cartography.py")
if(EXISTS "${CHRONON3D_CARTOGRAPHY_TOOL}")
    add_test(
        NAME chronon3d_cartography_geodesy_tests
        COMMAND ${Python3_EXECUTABLE} "${CHRONON3D_CARTOGRAPHY_TOOL}" --self-test
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    set_tests_properties(chronon3d_cartography_geodesy_tests PROPERTIES
        LABELS "cartography;geodesy;tool;gate"
    )
endif()

# The dynamic tile pyramid suite runs in strict offline mode. It first checks
# every cached tile the assertions need, then either loads only those tiles or
# returns CTest's explicit skip code (77); a cache miss can never hit the net.
set(CHRONON3D_PYRAMID_SUITE "${CMAKE_SOURCE_DIR}/tools/cartography/test_dynamic_tile_pyramid_suite.py")
if(EXISTS "${CHRONON3D_PYRAMID_SUITE}")
    add_test(
        NAME chronon3d_dynamic_pyramid_suite
        COMMAND ${Python3_EXECUTABLE} "${CHRONON3D_PYRAMID_SUITE}" --offline
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    set_tests_properties(chronon3d_dynamic_pyramid_suite PROPERTIES
        LABELS "cartography;pyramid;gate;offline"
        SKIP_RETURN_CODE 77
        TIMEOUT 300
    )
endif()

# Runtime extraction, tour scheduling and phase-aware motion gate: stdlib-only,
# deterministic, and safe to run on hosts without map assets or network access.
# The runtime lives in the sibling ChrononTemplate tree (it imports the canary
# overlay stack), so the registration follows the file there.
set(CHRONON_GEO_RUNTIME_TOOL "${CMAKE_SOURCE_DIR}/../ChrononTemplate/tools/geo_runtime.py")
if(EXISTS "${CHRONON_GEO_RUNTIME_TOOL}")
    add_test(
        NAME chronon_geo_runtime_self_test
        COMMAND ${Python3_EXECUTABLE} "${CHRONON_GEO_RUNTIME_TOOL}" --self-test
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.."
    )
    set_tests_properties(chronon_geo_runtime_self_test PROPERTIES
        LABELS "geo_camera;runtime;gate;offline"
        TIMEOUT 120
    )
endif()
unset(CHRONON_GEO_RUNTIME_TOOL)
