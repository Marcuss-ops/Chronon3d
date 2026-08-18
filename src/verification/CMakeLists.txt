# ==============================================================================
# src/verification/CMakeLists.txt — chronon3d_verification
#
# Canonical output verification + render receipt (chronon3d.render-receipt.v1).
# Pure function of a rendered output path + a caller-supplied identity: it
# reuses chronon3d::assets::sha256_file for the output hash and never
# recomputes content/request/asset-manifest identity.  Registered in
# cmake/Chronon3DRegistry.cmake so its OBJECT files reach both the SDK archive
# (libchronon3d_sdk_impl.a) and the in-tree build interface automatically.
# ==============================================================================

add_library(chronon3d_verification OBJECT
    render_receipt.cpp
)

target_include_directories(chronon3d_verification
    PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)

target_link_libraries(chronon3d_verification
    PUBLIC
        nlohmann_json::nlohmann_json
        chronon3d_assets
)
