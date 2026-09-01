Result<PreparedAssetStore, AssetPreflightError> prepare_asset_store(
    const render_plan::RenderPlan& plan,
    AssetResolver& resolver,
    const AssetPreflightPolicy& policy) {
    auto manifest_result = prepare_asset_manifest(plan, resolver, policy);
    if (!manifest_result) return std::move(manifest_result).error();

    PreparedAssetStore store;
    store.m_manifest = std::move(manifest_result).value();
    for (const auto& asset : store.m_manifest.assets()) {
        PreparedAssetStore::Resource resource;
        resource.metadata = asset;

        // Subtitle parsing is compiler-time work and therefore retains its
        // immutable bytes. Media decoders consume the certified metadata and
        // reopen their source through the runtime boundary only after the
        // render-job integrity check.
        if (asset.kind == PreparedAssetKind::Subtitle) {
            const auto resolved = resolver.resolve_logical(asset.logical_path);
            if (!resolved) {
                return error(AssetPreflightErrorCode::AssetChangedAfterPreflight,
                             asset.logical_path,
                             "subtitle disappeared after asset preflight");
            }
            std::error_code reopen_ec;
            const auto canonical_root = std::filesystem::weakly_canonical(
                resolver.mount_root(), reopen_ec);
            const auto canonical = std::filesystem::canonical(*resolved, reopen_ec);
            if (reopen_ec || canonical_root.empty() ||
                !path_is_within(canonical_root, canonical) ||
                (!policy.allow_symlinks_within_root && canonical != *resolved)) {
                return error(AssetPreflightErrorCode::AssetChangedAfterPreflight,
                             asset.logical_path,
                             "subtitle path changed outside the mounted root after asset preflight");
            }
            std::ifstream input(canonical, std::ios::binary);
            if (!input)
                return error(AssetPreflightErrorCode::AssetChangedAfterPreflight,
                             asset.logical_path,
                             "subtitle could not be reopened after asset preflight");
            if (asset.byte_size > std::numeric_limits<std::size_t>::max())
                return error(AssetPreflightErrorCode::ReadFailed, asset.logical_path,
                             "subtitle is too large for in-memory preparation");
            std::string contents(static_cast<std::size_t>(asset.byte_size), '\0');
            input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
            if (input.bad() || static_cast<std::uint64_t>(input.gcount()) != asset.byte_size ||
                sha256_string(contents) != asset.content_digest) {
                return error(AssetPreflightErrorCode::AssetChangedAfterPreflight,
                             asset.logical_path,
                             "subtitle bytes changed after asset preflight");
            }
            resource.bytes.resize(contents.size());
            std::transform(contents.begin(), contents.end(), resource.bytes.begin(),
                           [](char value) { return static_cast<std::byte>(value); });
        }
        store.m_resources.push_back(std::move(resource));
    }
    return store;
}
