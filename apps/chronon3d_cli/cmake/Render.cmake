if(NOT CHRONON3D_BUILD_CLI_RENDER)
    return()
endif()

add_library(chronon3d_cli_render STATIC
    commands/group_render.cpp
    commands/render/register_render_commands.cpp
    commands/render/command_render.cpp
    commands/render/command_preflight.cpp
    commands/render/command_bake_layer.cpp
    commands/render/command_graph.cpp
    commands/render/command_render_plan.cpp
    commands/render/command_render_plan_internal.cpp
    commands/render/caption_plan_expansion.cpp
    commands/render/render_plan_preparation.cpp
    commands/render/render_plan_inspection.cpp
    commands/render/command_inspect_plan.cpp
    commands/render/command_script.cpp
    commands/render/command_scopes.cpp
    commands/render/command_mask_propagate.cpp
    commands/render/command_ocio_bake_lut.cpp
    utils/job/render_job.cpp
    utils/job/render_job_setup.cpp
    utils/job/render_job_finalize.cpp
    utils/job/render_job_execute.cpp
    utils/job/render_job_loop.cpp
    utils/job/render_job_write_frame.cpp
    utils/job/cli_render_utils.cpp
    utils/project/chronon_project_render.cpp
    utils/job/report/render_job_report.cpp
    # Copy-lane trust boundary: the render-plan executor is its only caller, so
    # it lives in this target (not in cli_video_export, which this target does
    # not link). See the header for why the refusal belongs here.
    utils/video/copy_lane_coverage.cpp
    utils/semantic/semantic_script.cpp
)

target_include_directories(chronon3d_cli_render PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/apps/chronon3d_cli
    ${CMAKE_SOURCE_DIR}/src/compiler
)
target_link_libraries(chronon3d_cli_render PRIVATE
    chronon3d_cli_core
    chronon3d_render_plan
    chronon3d_compiler
    chronon3d_pipeline
    chronon3d_backend_image
    chronon3d_media_interface
    CLI11::CLI11
    spdlog::spdlog_header_only
    fmt::fmt
)

if(CHRONON3D_ENABLE_VIDEO AND TARGET chronon3d_media_video)
    target_link_libraries(chronon3d_cli_render PRIVATE chronon3d_media_video)
endif()

target_compile_definitions(chronon3d_cli_core PRIVATE
    CHRONON3D_HAS_CLI_RENDER
)

target_compile_definitions(chronon3d_cli_render PRIVATE
    CHRONON3D_CLI_PROJECT_VERSION="${PROJECT_VERSION}"
)

# The receipt/report block in commands/render/command_render_plan_internal.cpp
# and the receipt include in command_render_plan.cpp are guarded by
# `#ifdef CHRONON3D_ENABLE_VERIFICATION` — a PREPROCESSOR macro, not the CMake
# variable of the same name. Nothing ever defined the macro, so a build with
# -DCHRONON3D_ENABLE_VERIFICATION=ON still compiled its only consumer out: the
# command asked for a render report, Chronon logged "verification/receipts are
# disabled in this build", no `<output>.receipt.json` was written, and
# RenderingGen's strict native gate (strict_native_backend + source video)
# rejected EVERY clip job with "missing Chronon media receipt".
#
# The definition belongs to the target that owns the guarded translation units,
# like the other CLI feature macros (CHRONON3D_HAS_CLI_RENDER, ...): the flag
# and the code it enables must be decided together, in one place. It is
# conditional on the verification library actually existing so a lean build
# (option OFF) keeps compiling the same code out, unchanged.
if(CHRONON3D_ENABLE_VERIFICATION AND TARGET chronon3d_verification)
    target_compile_definitions(chronon3d_cli_render PRIVATE
        CHRONON3D_ENABLE_VERIFICATION
    )
endif()
