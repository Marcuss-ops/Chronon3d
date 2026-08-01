#pragma once

#include <chronon3d/core/types/result.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chronon3d::render_plan {
struct RenderPlan;
}

namespace chronon3d::assets {

class AssetResolver;

/// SHA-256 content digest used by prepared render resources.
struct ContentDigest {
    std::array<std::byte, 32> bytes{};

    [[nodiscard]] std::string hex() const;
    friend bool operator==(const ContentDigest&, const ContentDigest&) = default;
};

enum class PreparedAssetKind : std::uint8_t {
    Image,
    Video,
    Audio,
    Font,
    Subtitle,
    Data
};

struct PreparedAsset {
    std::string logical_path;
    PreparedAssetKind kind{PreparedAssetKind::Data};
    std::uint64_t byte_size{0};
    ContentDigest content_digest{};
};

struct AssetPreflightPolicy {
    std::uint64_t max_single_asset_bytes{512ULL * 1024ULL * 1024ULL};
    std::uint64_t max_total_asset_bytes{4ULL * 1024ULL * 1024ULL * 1024ULL};
    bool allow_symlinks_within_root{true};
};

enum class AssetPreflightErrorCode {
    InvalidLogicalPath,
    AbsolutePathRejected,
    PathTraversalRejected,
    MissingAsset,
    OutsideAssetsRoot,
    SymlinkOutsideRoot,
    WrongAssetKind,
    AssetTooLarge,
    TotalBudgetExceeded,
    ReadFailed,
    HashFailed
};

struct AssetPreflightError {
    AssetPreflightErrorCode code{AssetPreflightErrorCode::ReadFailed};
    std::string logical_path;
    std::string message;
};

/// Immutable result of asset preflight.  The only accessors expose const
/// views; instances are populated only by prepare_asset_manifest().
class PreparedAssetManifest {
public:
    [[nodiscard]] const std::vector<PreparedAsset>& assets() const noexcept {
        return m_assets;
    }

    [[nodiscard]] const ContentDigest& manifest_digest() const noexcept {
        return m_manifest_digest;
    }

private:
    std::vector<PreparedAsset> m_assets;
    ContentDigest m_manifest_digest{};

    friend Result<PreparedAssetManifest, struct AssetPreflightError>
    prepare_asset_manifest(
        const render_plan::RenderPlan& plan,
        AssetResolver& resolver,
        const struct AssetPreflightPolicy& policy);
};

Result<PreparedAssetManifest, AssetPreflightError> prepare_asset_manifest(
    const render_plan::RenderPlan& plan,
    AssetResolver& resolver,
    const AssetPreflightPolicy& policy = {});

} // namespace chronon3d::assets
