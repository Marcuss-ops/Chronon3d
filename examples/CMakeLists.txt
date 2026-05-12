add_executable(code_first_smoke
    code_first_smoke.cpp
)

target_link_libraries(code_first_smoke
    PRIVATE
        chronon3d
        spdlog::spdlog
        fmt::fmt
)

add_executable(remotion_style_guide
    remotion_style_guide.cpp
)

target_link_libraries(remotion_style_guide
    PRIVATE
        chronon3d
        spdlog::spdlog
        fmt::fmt
)
