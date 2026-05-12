add_executable(code_first_smoke
    code_first_smoke.cpp
)

target_link_libraries(code_first_smoke
    PRIVATE
        chronon3d
        spdlog::spdlog
        fmt::fmt
)
