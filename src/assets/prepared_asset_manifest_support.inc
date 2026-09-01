struct RequestedAsset {
    std::string logical_path;
    PreparedAssetKind kind{PreparedAssetKind::Data};
};

[[nodiscard]] std::string lower_extension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension;
}

[[nodiscard]] bool known_extension_matches(PreparedAssetKind kind,
                                            const std::string& extension) {
    const auto matches = [&](std::initializer_list<std::string_view> values) {
        return std::find(values.begin(), values.end(), extension) != values.end();
    };
    switch (kind) {
        case PreparedAssetKind::Font:
            return matches({".ttf", ".otf", ".woff", ".woff2"});
        case PreparedAssetKind::Image:
            return matches({".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif",
                            ".tif", ".tiff", ".exr", ".svg"});
        case PreparedAssetKind::Video:
            return matches({".mp4", ".mov", ".mkv", ".webm", ".avi", ".m4v"});
        case PreparedAssetKind::Audio:
            return matches({".wav", ".mp3", ".aac", ".m4a", ".flac", ".ogg"});
        case PreparedAssetKind::Subtitle:
            return matches({".ass", ".srt", ".vtt", ".json"});
        case PreparedAssetKind::Data:
            return true;
    }
    return true;
}

[[nodiscard]] bool invalid_logical_path(std::string_view raw) {
    if (raw.empty()) return true;
    if (raw.find('\\') != std::string_view::npos) return true;
    std::filesystem::path path{std::string(raw)};
    if (path.empty() || path.is_absolute()) return true;
    for (const auto& component : path) {
        if (component == std::filesystem::path("..")) return true;
    }
    return path.lexically_normal() == std::filesystem::path(".");
}

[[nodiscard]] bool drive_absolute(std::string_view raw) {
    return raw.size() >= 3U && std::isalpha(static_cast<unsigned char>(raw[0])) &&
           raw[1] == ':' && (raw[2] == '/' || raw[2] == '\\');
}

[[nodiscard]] bool path_is_within(const std::filesystem::path& root,
                                  const std::filesystem::path& candidate) {
    auto root_it = root.begin();
    auto candidate_it = candidate.begin();
    for (; root_it != root.end() && candidate_it != candidate.end(); ++root_it, ++candidate_it) {
        if (*root_it != *candidate_it) return false;
    }
    return root_it == root.end() && candidate_it != candidate.end();
}

[[nodiscard]] AssetPreflightError error(AssetPreflightErrorCode code,
                                        std::string path,
                                        std::string message) {
    return AssetPreflightError{code, std::move(path), std::move(message)};
}

[[nodiscard]] std::vector<RequestedAsset> requested_assets(
    const render_plan::RenderPlan& plan) {
    std::vector<RequestedAsset> result;
    const auto add = [&](std::string_view path, PreparedAssetKind kind) {
        if (!path.empty()) result.push_back({std::string(path), kind});
    };
    for (const auto& layer : plan.layers) {
        if (layer.type == render_plan::LayerType::Image) {
            add(layer.asset, PreparedAssetKind::Image);
        } else if (layer.type == render_plan::LayerType::Video) {
            add(layer.source, PreparedAssetKind::Video);
        } else if (layer.type == render_plan::LayerType::Text) {
            add(layer.font, PreparedAssetKind::Font);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.logical_path != right.logical_path)
            return left.logical_path < right.logical_path;
        return left.kind < right.kind;
    });
    result.erase(std::unique(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.logical_path == right.logical_path && left.kind == right.kind;
    }), result.end());
    return result;
}

[[nodiscard]] Result<ContentDigest, AssetPreflightError> hash_file(
    const std::filesystem::path& path,
    std::string logical_path,
    std::uint64_t expected_size,
    std::uint64_t max_size) {
    if (expected_size > max_size)
        return error(AssetPreflightErrorCode::AssetTooLarge, std::move(logical_path),
                     "asset exceeds the configured single-asset limit");

    const auto digest = sha256_file(path);
    if (!digest)
        return error(AssetPreflightErrorCode::HashFailed, std::move(logical_path),
                     "asset changed or failed while hashing");
    std::error_code size_ec;
    const auto actual_size = std::filesystem::file_size(path, size_ec);
    if (size_ec || actual_size != expected_size || actual_size > max_size)
        return error(AssetPreflightErrorCode::HashFailed, std::move(logical_path),
                     "asset changed or failed while hashing");
    return *digest;
}
