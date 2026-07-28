# ==============================================================================
# src/render_plan/CMakeLists.txt — chronon3d_render_plan
#
# Standalone OBJECT library for the render-plan JSON Schema validator.
# No SDK state, no asset access — pure function module.  Linked into
# chronon3d_core (and therefore chronon3d_pipeline + chronon3d_sdk_impl)
# so the C API compile_plan entrypoint gets it for free.
# ==============================================================================

add_library(chronon3d_render_plan OBJECT
    render_plan_decoder.cpp
    render_plan_validator.cpp
)

add_library(chronon3d_render_plan_compiler OBJECT
    render_plan_compiler.cpp
)

target_include_directories(chronon3d_render_plan
    PUBLIC  ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(chronon3d_render_plan_compiler
    PUBLIC  ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(chronon3d_render_plan
    PUBLIC
        nlohmann_json::nlohmann_json
        chronon3d_core_impl
)

target_link_libraries(chronon3d_render_plan_compiler
    PUBLIC
        nlohmann_json::nlohmann_json
        chronon3d_core_impl
)

# Propagation: chronon3d_render_plan is intentionally NOT linked into
# chronon3d_core (would require it in the SDK export set — see
# Chronon3DTargets install/export wiring).  Instead, the validator's
# OBJECT files reach consumers via direct link:
#   - in-tree tests: tests/c_abi_tests.cmake LINK_TARGETS
#   - C API SHARED lib (chronon3d_c): see
#     cmake/Chronon3DSdkTargets.cmake — the C API SHARED is the only
#     SDK consumer that actually needs the validator (the C++ SDK
#     surface compiles plans in src/sdk_impl_marker.cpp + render_graph
#     internals which don't touch JSON parsing).
