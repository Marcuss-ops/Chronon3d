std::string MeshIdentity::cache_key() const {
    return resolved_path + "\n" + std::to_string(byte_size) + "\n"
        + std::to_string(write_time) + "\n" + content_digest.hex();
}

#ifdef CHRONON3D_ENABLE_MESH
Result<PreparedMeshSourceRef, MeshLoadError> MeshLoader::load(
    const InternalAssetRef& ref, const AssetResolver& resolver, MeshPreparationCache* cache) {
    if (ref.kind != AssetKind::Mesh) {
        return MeshLoadError{MeshLoadErrorCode::InvalidReference, ref.path,
                             "mesh preparation requires AssetKind::Mesh"};
    }
    if (ref.path.empty()) {
        return MeshLoadError{MeshLoadErrorCode::InvalidReference, ref.path,
                             "mesh reference path is empty"};
    }
    const auto extension = std::filesystem::path{ref.path}.extension().string();
    if (extension != ".glb" && extension != ".GLB") {
        return MeshLoadError{MeshLoadErrorCode::UnsupportedGlb, ref.path,
                             "unsupported mesh format '" + extension
                                 + "'; V1 accepts only self-contained .glb (not .gltf)"};
    }
    const auto resolved = resolver.resolve(ref.path);
    if (!resolved.has_value()) {
        return MeshLoadError{MeshLoadErrorCode::MissingAsset, ref.path,
                             "mesh asset not found: " + ref.path};
    }
    MeshIdentity identity;
    try {
        identity = identity_for(*resolved);
        if (cache) {
            if (const auto cached = cache->find(identity); cached.has_value()) return *cached;
        }
        auto loaded = decode(ref, *resolved, identity);
        if (!(identity == identity_for(*resolved))) {
            return MeshLoadError{MeshLoadErrorCode::ReadFailed, ref.path,
                                 "GLB changed while it was being prepared"};
        }
        if (cache) cache->store(identity, loaded);
        return loaded;
    } catch (const json::exception& e) {
        return MeshLoadError{MeshLoadErrorCode::InvalidGlb, ref.path, std::string{"invalid GLB JSON: "} + e.what()};
    } catch (const std::ios_base::failure& e) {
        return MeshLoadError{MeshLoadErrorCode::ReadFailed, ref.path, e.what()};
    } catch (const std::exception& e) {
        return MeshLoadError{MeshLoadErrorCode::InvalidGeometry, ref.path, e.what()};
    }
}
#else
Result<PreparedMeshSourceRef, MeshLoadError> MeshLoader::load(
    const InternalAssetRef& ref, const AssetResolver&, MeshPreparationCache*) {
    return MeshLoadError{
        MeshLoadErrorCode::UnsupportedGlb,
        ref.path,
        "Mesh support is disabled (CHRONON3D_ENABLE_MESH=OFF)"};
}
#endif
