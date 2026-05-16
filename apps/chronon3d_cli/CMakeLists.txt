add_executable(chronon3d_cli
    main.cpp
    utils/cli_utils.cpp
    utils/cli_render_utils.cpp
    utils/render_job.cpp
    utils/batch_job_spec.cpp

    commands/command_list.cpp
    commands/command_info.cpp
    commands/command_render.cpp
    commands/command_batch.cpp
    commands/command_watch.cpp
    commands/command_video.cpp
    commands/command_doctor_verify.cpp
    commands/command_bench.cpp
    commands/command_graph.cpp
)

get_target_property(CLI11_INCLUDE_DIRS CLI11::CLI11 INTERFACE_INCLUDE_DIRECTORIES)

target_link_libraries(chronon3d_cli
    PRIVATE
        chronon3d_pipeline
        chronon3d_backend_image
        spdlog::spdlog
        fmt::fmt
)

target_include_directories(chronon3d_cli PRIVATE ${CMAKE_SOURCE_DIR})

if(CLI11_INCLUDE_DIRS)
    target_include_directories(chronon3d_cli PRIVATE ${CLI11_INCLUDE_DIRS})
endif()
