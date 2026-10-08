# ==============================================================================
# src/geo/CMakeLists.txt — headless geo / historical map core (ADR-034).
# Enabled by CHRONON3D_ENABLE_GEO. Polygon boolean ops use Clipper2 (vcpkg
# feature `geo`); nothing else in the core depends on it.
# ==============================================================================

add_library(chronon3d_geo OBJECT
    ${CMAKE_CURRENT_SOURCE_DIR}/geojson.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/historical_timeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/feature_query.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/geo_boolean.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/territory_change.cpp
)

target_include_directories(chronon3d_geo PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# PUBLIC so the link dependency reaches the archive/consumers that link the
# object library through the registry (CHRONON3D_REGISTRY_OBJECT_LIBS).
target_link_libraries(chronon3d_geo PUBLIC
    Clipper2::Clipper2
    nlohmann_json::nlohmann_json
)
