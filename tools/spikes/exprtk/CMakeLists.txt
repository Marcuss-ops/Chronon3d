# ── ExprTk expression compiler spike ──────────────────────────────────
# Isolated, compile once per request, evaluate per frame.
# Requires CHRONON3D_BUILD_EXPRTK_SPIKE=ON (OFF by default).

include(FetchContent)
FetchContent_Declare(
    exprtk
    URL https://github.com/ArashPartow/exprtk/archive/refs/tags/0.0.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(exprtk)

add_library(chronon3d_exprtk_headers INTERFACE)
target_include_directories(chronon3d_exprtk_headers INTERFACE "${exprtk_SOURCE_DIR}")
# ExprTk is header-only — no link deps

add_executable(chronon3d_exprtk_spike
    exprtk_spike.cpp
)
target_link_libraries(chronon3d_exprtk_spike PRIVATE
    chronon3d_exprtk_headers
    chronon3d_pipeline  # expression headers plus the runtime object closure
)
target_include_directories(chronon3d_exprtk_spike PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
set_target_properties(chronon3d_exprtk_spike PROPERTIES
    UNITY_BUILD OFF
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
