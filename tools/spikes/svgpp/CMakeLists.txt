# Isolated feasibility spike: this target is never part of the canonical loader.
add_executable(chronon3d_svgpp_spike
    svgpp_spike.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/svg_importer.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/svgpp_parser_impl.cpp
)

target_compile_features(chronon3d_svgpp_spike PRIVATE cxx_std_20)
target_include_directories(chronon3d_svgpp_spike PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/assets
)
target_link_libraries(chronon3d_svgpp_spike PRIVATE
    chronon3d_base
)
target_link_libraries(chronon3d_svgpp_spike PRIVATE
    chronon3d_svgpp_headers
    Boost::spirit
    Boost::math
)

target_compile_definitions(chronon3d_svgpp_spike PRIVATE
    CHRONON3D_SVGPP_VERSION="v1.3.1"
)
