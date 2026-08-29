# ==============================================================================
# src/assets/CMakeLists.txt — chronon3d_assets
# ==============================================================================

set(CHRONON3D_ASSET_SOURCES
    asset_resolver.cpp
    mesh_loader.cpp
    prepared_asset_manifest.cpp
    prepared_asset_digest.cpp
    render_preflight_req.cpp
    render_preflight_validate.cpp
    render_preflight_helpers.cpp
)
if(CHRONON3D_ENABLE_SVG)
    list(APPEND CHRONON3D_ASSET_SOURCES
        svg_path_loader.cpp
        svg_importer.cpp
        svgpp_parser_impl.cpp
    )
endif()
add_library(chronon3d_assets OBJECT ${CHRONON3D_ASSET_SOURCES})
set_target_properties(chronon3d_assets PROPERTIES UNITY_BUILD OFF)

# Use the platform's optimized SHA-256 implementation for artifact hashing
# when OpenSSL is available.  The source retains a small portable fallback so
# asset hashing remains available on builds without OpenSSL.
find_package(OpenSSL QUIET COMPONENTS Crypto)
if(OpenSSL_FOUND)
    target_compile_definitions(chronon3d_assets PRIVATE CHRONON3D_HAS_OPENSSL_SHA256=1)
    target_link_libraries(chronon3d_assets PRIVATE OpenSSL::Crypto)
else()
    # Some minimal GPU/worker images ship libcrypto without the development
    # headers.  EVP is a stable C ABI, so use the runtime library directly in
    # that environment instead of falling back to the debug scalar SHA code.
    find_library(CHRONON3D_CRYPTO_LIBRARY
        NAMES crypto libcrypto.so.3 libcrypto.so)
    if(CHRONON3D_CRYPTO_LIBRARY)
        target_compile_definitions(chronon3d_assets PRIVATE CHRONON3D_HAS_OPENSSL_SHA256=1)
        target_link_libraries(chronon3d_assets PRIVATE "${CHRONON3D_CRYPTO_LIBRARY}")
    endif()
endif()
set_source_files_properties(prepared_asset_manifest.cpp PROPERTIES
    SKIP_UNITY_BUILD_INCLUSION ON)
if(CHRONON3D_ENABLE_SVG)
    set_source_files_properties(
        svg_importer.cpp
        svgpp_parser_impl.cpp
        PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
    )
endif()
# TICKET-PREFLIGHT-PATH-EXISTENCE-MAP-EXISTS-BODY: link chronon3d_path_cache
# (the dedicated STATIC library defined in src/render_graph/CMakeLists.txt
# that owns preflight/path_existence_map.cpp).  The consumer
# render_preflight_validate.cpp calls cache->exists(...) which is the
# O(1) lookup with TTL-driven refresh; without this link dep, the
# apps/chronon3d_cli/chronon3d_cli link step fails with
# `undefined reference to 'chronon3d::preflight::PathExistenceMap::exists(
# std::filesystem::__cxx11::path const&) const'`.  PUBLIC so the
# transitive link closure reaches apps/chronon3d_cli/chronon3d_cli via
# chronon3d_assets → chronon3d_core → chronon3d_pipeline → chronon3d_sdk.
target_link_libraries(chronon3d_assets PUBLIC
    chronon3d
    chronon3d_registry
    chronon3d_path_cache
    nlohmann_json::nlohmann_json
)
if(CHRONON3D_ENABLE_SVG)
    target_link_libraries(chronon3d_assets PRIVATE
        Boost::spirit
        Boost::math
    )
    target_include_directories(chronon3d_assets PRIVATE
        "${svgpp_SOURCE_DIR}/include"
    )
endif()
