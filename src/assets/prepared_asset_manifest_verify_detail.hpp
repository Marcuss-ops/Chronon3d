Result<bool, AssetPreflightError> verify_asset_manifest(
    const PreparedAssetManifest& manifest,
    AssetResolver& resolver,
    const AssetPreflightPolicy& policy) {
    if (manifest.assets().empty()) return true;

    const auto root = resolver.mount_root();
    if (root.empty())
        return error(AssetPreflightErrorCode::OutsideAssetsRoot, {},
                     "asset integrity verification requires a mounted root");

    std::error_code ec;
    const auto canonical_root = std::filesystem::weakly_canonical(root, ec);
    if (ec || canonical_root.empty())
        return error(AssetPreflightErrorCode::OutsideAssetsRoot, {},
                     "asset resolver root could not be canonicalized for verification");

    for (const auto& asset : manifest.assets()) {
        const auto resolved = resolver.resolve_logical(asset.logical_path);
        if (!resolved)
            return error(AssetPreflightErrorCode::AssetChangedAfterPreflight,
                         asset.logical_path,
                         "asset disappeared after asset preflight");

        ec.clear();
        const auto canonical = std::filesystem::canonical(*resolved, ec);
        if (ec || !path_is_within(canonical_root, canonical))
            return error(AssetPreflightErrorCode::AssetChangedAfterPreflight,
                         asset.logical_path,
                         "asset path moved outside the mounted root after asset preflight");
        if (!policy.allow_symlinks_within_root && canonical != *resolved)
            return error(AssetPreflightErrorCode::AssetChangedAfterPreflight,
                         asset.logical_path,
                         "asset symlink policy changed after asset preflight");

        ec.clear();
        if (!std::filesystem::is_regular_file(canonical, ec) || ec)
            return error(AssetPreflightErrorCode::AssetChangedAfterPreflight,
                         asset.logical_path,
                         "asset is no longer a regular file");
        if (!known_extension_matches(asset.kind, lower_extension(canonical)))
            return error(AssetPreflightErrorCode::AssetChangedAfterPreflight,
                         asset.logical_path,
                         "asset extension changed after asset preflight");

        ec.clear();
        const auto current_size = std::filesystem::file_size(canonical, ec);
        if (ec || current_size != asset.byte_size)
            return error(AssetPreflightErrorCode::AssetChangedAfterPreflight,
                         asset.logical_path,
                         "asset byte size changed after asset preflight");

        ec.clear();
        const auto current_timestamp = std::filesystem::last_write_time(canonical, ec);
        const auto current_stamp = ec
            ? std::int64_t{0}
            : static_cast<std::int64_t>(
                current_timestamp.time_since_epoch().count());
        if (!ec && asset.file_timestamp != 0 &&
            current_stamp == asset.file_timestamp) {
            continue;
        }

        auto digest = hash_file(canonical, asset.logical_path, current_size,
                                policy.max_single_asset_bytes);
        if (!digest)
            return error(AssetPreflightErrorCode::AssetChangedAfterPreflight,
                         asset.logical_path,
                         digest.error().message);
        if (digest.value() != asset.content_digest)
            return error(AssetPreflightErrorCode::AssetChangedAfterPreflight,
                         asset.logical_path,
                         "asset bytes changed after asset preflight");
    }
    return true;
}
