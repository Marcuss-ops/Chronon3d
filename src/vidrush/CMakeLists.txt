add_library(chronon3d_vidrush STATIC
    visual_unit_report.cpp
)

target_include_directories(chronon3d_vidrush
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(chronon3d_vidrush
    PUBLIC
        chronon3d_base
        nlohmann_json::nlohmann_json
)
