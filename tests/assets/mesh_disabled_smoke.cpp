#include <doctest/doctest.h>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/mesh_loader.hpp>

TEST_CASE("MeshLoader OFF build reports an explicit disabled-feature error") {
    chronon3d::assets::AssetResolver resolver;
    const chronon3d::assets::InternalAssetRef ref{
        chronon3d::assets::AssetKind::Mesh,
        "models/triangle.glb",
        "mesh/off-smoke",
        true};

    const auto result = chronon3d::assets::MeshLoader::load(ref, resolver);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == chronon3d::assets::MeshLoadErrorCode::UnsupportedGlb);
    CHECK(result.error().path == "models/triangle.glb");
    CHECK(result.error().message ==
          "Mesh support is disabled (CHRONON3D_ENABLE_MESH=OFF)");
}

TEST_CASE("MeshIdentity cache keys remain available in the OFF build") {
    chronon3d::assets::MeshIdentity identity;
    identity.resolved_path = "/assets/models/triangle.glb";
    identity.byte_size = 42;
    identity.write_time = 7;

    const auto key = identity.cache_key();
    CHECK(key.find("/assets/models/triangle.glb") != std::string::npos);
    CHECK(key.find("42") != std::string::npos);
    CHECK(key.find("7") != std::string::npos);
}
