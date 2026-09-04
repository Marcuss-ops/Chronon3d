AssetDigestCacheStats asset_digest_cache_stats() {
    return prepared_asset_digest_cache().stats();
}

Result<PreparedAssetView, AssetPreflightError> PreparedAssetStore::find(
    std::string_view logical_path,
    PreparedAssetKind kind) const {
    for (const auto& resource : m_resources) {
        if (resource.metadata.logical_path != logical_path ||
            resource.metadata.kind != kind) {
            continue;
        }
        return PreparedAssetView{
            std::span<const std::byte>{resource.bytes.data(), resource.bytes.size()},
            resource.metadata.kind,
            resource.metadata.byte_size,
            resource.metadata.content_digest};
    }
    return error(AssetPreflightErrorCode::MissingAsset, std::string(logical_path),
                 "prepared asset is not present in the immutable store");
}

Result<PreparedAssetManifest, AssetPreflightError> prepare_asset_manifest(
    const render_plan::RenderPlan& plan,
    AssetResolver& resolver,
    const AssetPreflightPolicy& policy) {
    const auto cache_before = asset_digest_cache_stats();
    const auto requests = requested_assets(plan);
    if (requests.empty()) {
        PreparedAssetManifest empty_manifest;
        empty_manifest.m_manifest_digest =
            sha256_string("chronon.prepared-asset-manifest.v1");
        return empty_manifest;
    }

    const auto root = resolver.mount_root();
    if (root.empty())
        return error(AssetPreflightErrorCode::OutsideAssetsRoot, {},
                     "asset preflight requires a mounted AssetResolver root");

    std::error_code ec;
    const auto canonical_root = std::filesystem::weakly_canonical(root, ec);
    if (ec || canonical_root.empty())
        return error(AssetPreflightErrorCode::OutsideAssetsRoot, {},
                     "asset resolver root could not be canonicalized");

    PreparedAssetManifest manifest;
    std::uint64_t total_bytes = 0;
    Sha256 manifest_hash;
    const std::string domain = "chronon.prepared-asset-manifest.v1";
    manifest_hash.update(domain.data(), domain.size());

    for (const auto& request : requests) {
        const auto asset_t0 = chronon3d::profiling::now();
        const std::filesystem::path raw_path{request.logical_path};
        if (raw_path.is_absolute() || drive_absolute(request.logical_path))
            return error(AssetPreflightErrorCode::AbsolutePathRejected,
                         request.logical_path, "Windows absolute asset paths are rejected");
        if (invalid_logical_path(request.logical_path)) {
            const auto code = request.logical_path.find("..") != std::string::npos
                                  ? AssetPreflightErrorCode::PathTraversalRejected
                                  : AssetPreflightErrorCode::InvalidLogicalPath;
            return error(code, request.logical_path, "asset reference is not a logical path");
        }

        const auto normalized = std::filesystem::path(request.logical_path)
                                    .lexically_normal().generic_string();
        const auto resolved = resolver.resolve_logical(normalized);
        if (!resolved)
            return error(AssetPreflightErrorCode::MissingAsset, normalized,
                         "logical asset does not exist under the mounted root");
        const auto canonical = std::filesystem::canonical(*resolved, ec);
        if (ec || !path_is_within(canonical_root, canonical))
            return error(AssetPreflightErrorCode::SymlinkOutsideRoot, normalized,
                         "asset resolves outside the mounted root");
        if (!policy.allow_symlinks_within_root && canonical != *resolved)
            return error(AssetPreflightErrorCode::SymlinkOutsideRoot, normalized,
                         "symlinked assets are disabled by the preflight policy");
        if (!std::filesystem::is_regular_file(canonical, ec) || ec)
            return error(AssetPreflightErrorCode::ReadFailed, normalized,
                         "asset is not a regular file");
        if (!known_extension_matches(request.kind, lower_extension(canonical)))
            return error(AssetPreflightErrorCode::WrongAssetKind, normalized,
                         "asset extension does not match the requested asset kind");

        const auto metadata_t0 = chronon3d::profiling::now();
        const auto byte_size = std::filesystem::file_size(canonical, ec);
        if (ec)
            return error(AssetPreflightErrorCode::ReadFailed, normalized,
                         "asset size could not be read");
        if (byte_size > policy.max_single_asset_bytes)
            return error(AssetPreflightErrorCode::AssetTooLarge, normalized,
                         "asset exceeds the configured single-asset limit");
        if (byte_size > policy.max_total_asset_bytes ||
            total_bytes > policy.max_total_asset_bytes - byte_size)
            return error(AssetPreflightErrorCode::TotalBudgetExceeded, normalized,
                         "assets exceed the configured total byte limit");

        std::error_code timestamp_ec;
        const auto timestamp_before = std::filesystem::last_write_time(
            canonical, timestamp_ec);
        if (timestamp_ec)
            return error(AssetPreflightErrorCode::ReadFailed, normalized,
                         "asset timestamp could not be read");

        const auto timestamp_value = static_cast<std::int64_t>(
            timestamp_before.time_since_epoch().count());
        const auto identity_before = file_identity(canonical);
        const double metadata_ms = chronon3d::profiling::duration_ms(
            metadata_t0, chronon3d::profiling::now());
        auto digest = prepared_asset_digest_cache().resolve(
            canonical, normalized, byte_size, timestamp_value,
            identity_before, policy.max_single_asset_bytes);
        if (!digest) return std::move(digest).error();

        timestamp_ec.clear();
        const auto timestamp_after = std::filesystem::last_write_time(
            canonical, timestamp_ec);
        if (timestamp_ec || timestamp_before != timestamp_after) {
            prepared_asset_digest_cache().discard(canonical);
            return error(AssetPreflightErrorCode::AssetChangedAfterPreflight,
                         normalized,
                         "asset changed while its prepared bytes were hashed");
        }
        std::error_code size_ec;
        const auto size_after = std::filesystem::file_size(canonical, size_ec);
        if (size_ec || size_after != byte_size) {
            prepared_asset_digest_cache().discard(canonical);
            return error(AssetPreflightErrorCode::AssetChangedAfterPreflight,
                         normalized,
                         "asset size changed while its prepared bytes were hashed");
        }
        if (file_identity(canonical) != identity_before) {
            prepared_asset_digest_cache().discard(canonical);
            return error(AssetPreflightErrorCode::AssetChangedAfterPreflight,
                         normalized,
                         "asset identity changed while it was being preflighted");
        }

        PreparedAsset prepared{
            normalized,
            request.kind,
            byte_size,
            digest.value().digest,
            static_cast<std::int64_t>(timestamp_after.time_since_epoch().count())};
        manifest_hash.update(prepared.logical_path.data(), prepared.logical_path.size());
        const auto kind = static_cast<std::uint8_t>(prepared.kind);
        manifest_hash.update(&kind, sizeof(kind));
        add_u64(manifest_hash, prepared.byte_size);
        manifest_hash.update(prepared.content_digest.bytes.data(),
                             prepared.content_digest.bytes.size());
        total_bytes += byte_size;
        manifest.m_assets.push_back(std::move(prepared));
        spdlog::info(
            "[asset-profile] kind={} path={} bytes={} cache={} invalidated={} "
            "stat={:.2f}ms hash={:.2f}ms hashed_bytes={} total={:.2f}ms",
            static_cast<int>(request.kind), normalized, byte_size,
            digest.value().hit ? "hit" : "miss",
            digest.value().invalidated ? 1 : 0,
            metadata_ms,
            digest.value().full_hash_ms, digest.value().bytes_hashed,
            chronon3d::profiling::duration_ms(asset_t0, chronon3d::profiling::now()));
    }

    manifest.m_manifest_digest = manifest_hash.finish();
    const auto cache_after = asset_digest_cache_stats();
    spdlog::info(
        "[asset-digest-cache] hits={} misses={} invalidations={} "
        "bytes_hashed={} lookup_ms={:.2f} write_ms={:.2f} full_hash_ms={:.2f}",
        cache_after.hits - cache_before.hits,
        cache_after.misses - cache_before.misses,
        cache_after.invalidations - cache_before.invalidations,
        cache_after.bytes_hashed - cache_before.bytes_hashed,
        cache_after.cache_lookup_ms - cache_before.cache_lookup_ms,
        cache_after.cache_write_ms - cache_before.cache_write_ms,
        cache_after.full_hash_ms - cache_before.full_hash_ms);
    return manifest;
}
