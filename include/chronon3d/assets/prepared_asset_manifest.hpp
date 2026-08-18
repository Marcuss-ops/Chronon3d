#pragma once

#include <chronon3d/core/types/result.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <span>
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

/// Hash a deterministic byte/string representation with the same SHA-256
/// primitive used by prepared assets.
[[nodiscard]] ContentDigest sha256_string(std::string_view value);

/// Stream-SHA-256 a file's bytes with the same primitive. Returns nullopt if
/// the file cannot be opened or fails while being read. This is the canonical
/// content digest for rendered artifacts (post-render verification, `copy
/// eligibility`), and is deliberately unbound by any asset-size policy.
[[nodiscard]] std::optional<ContentDigest> sha256_file(
    const std::filesystem::path& path);

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
    // Filesystem timestamp captured around the preflight hash.  It is an
    // execution-time fast path only; content_digest remains authoritative.
    std::int64_t file_timestamp{0};
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
    HashFailed,
    AssetChangedAfterPreflight
};

struct AssetPreflightError {
    AssetPreflightErrorCode code{AssetPreflightErrorCode::ReadFailed};
    std::string logical_path;
    std::string message;
};

struct PreparedAssetView {
    std::span<const std::byte> bytes{};
    PreparedAssetKind kind{PreparedAssetKind::Data};
    std::uint64_t byte_size{0};
    ContentDigest content_digest{};
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

/// Immutable resources retained after preflight. Small textual resources are
/// copied into owned byte storage; decoders for larger resources reopen their
/// source through the runtime boundary after the manifest integrity check.
class PreparedAssetStore {
public:
    [[nodiscard]] const PreparedAssetManifest& manifest() const noexcept {
        return m_manifest;
    }

    [[nodiscard]] Result<PreparedAssetView, AssetPreflightError> find(
        std::string_view logical_path,
        PreparedAssetKind kind) const;

private:
    struct Resource {
        PreparedAsset metadata;
        std::vector<std::byte> bytes;
    };

    PreparedAssetManifest m_manifest;
    std::vector<Resource> m_resources;

    friend Result<PreparedAssetStore, AssetPreflightError>
    prepare_asset_store(
        const render_plan::RenderPlan& plan,
        AssetResolver& resolver,
        const AssetPreflightPolicy& policy);
};

Result<PreparedAssetManifest, AssetPreflightError> prepare_asset_manifest(
    const render_plan::RenderPlan& plan,
    AssetResolver& resolver,
    const AssetPreflightPolicy& policy = {});

/// Verify that every asset in an immutable manifest still resolves inside the
/// same mounted root and has the certified bytes. Size/timestamp equality is
/// the normal O(1) path; a full SHA-256 is performed when the timestamp is
/// unavailable or changed. This is the render-job boundary check that closes
/// the preflight-to-decode TOCTOU window.
Result<bool, AssetPreflightError> verify_asset_manifest(
    const PreparedAssetManifest& manifest,
    AssetResolver& resolver,
    const AssetPreflightPolicy& policy = {});

Result<PreparedAssetStore, AssetPreflightError> prepare_asset_store(
    const render_plan::RenderPlan& plan,
    AssetResolver& resolver,
    const AssetPreflightPolicy& policy = {});

} // namespace chronon3d::assets
