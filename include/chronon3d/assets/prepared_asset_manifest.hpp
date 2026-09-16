#pragma once

#include <chronon3d/core/types/result.hpp>

#include <array>
#include <cctype>
#include <cstddef>
#include <initializer_list>
#include <string_view>
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

// ── FIXING.md H-02/U-02 — deterministic u64→ContentDigest expansion ─────────
//
// Glyph atlas identities are integer hashes, not content digests. Expanding a
// 64-bit xxHash via splitmix64 avoids the SHA-256 string-hash round-trip for
// every glyph/page. The expansion is deterministic and collision-negligible
// for atlas keys; if ContentDigest is ever rehashed content-addressedly, use
// sha256_string instead.
[[nodiscard]] ContentDigest digest_from_u64(std::uint64_t value) noexcept;

/// Stream-SHA-256 a file's bytes with the same primitive. Returns nullopt if
/// the file cannot be opened or fails while being read. This is the canonical
/// content digest for rendered artifacts (post-render verification, `copy
/// eligibility`), and is deliberately unbound by any asset-size policy.
[[nodiscard]] std::optional<ContentDigest> sha256_file(
    const std::filesystem::path& path);

/// Process-persistent digest cache counters. The cache is shared by all
/// execution modes in a worker; counters are cumulative for the process and
/// are intended for startup/preflight telemetry and tests.
struct AssetDigestCacheStats {
    std::uint64_t hits{0};
    std::uint64_t misses{0};
    std::uint64_t invalidations{0};
    std::uint64_t bytes_hashed{0};
    double cache_lookup_ms{0.0};
    double cache_write_ms{0.0};
    double full_hash_ms{0.0};
};

[[nodiscard]] AssetDigestCacheStats asset_digest_cache_stats();

enum class PreparedAssetKind : std::uint8_t {
    Image,
    Video,
    Audio,
    Font,
    Subtitle,
    Data
};

/// True when `extension` (any case, leading dot optional) belongs to `kind`.
///
/// AUDIT P1-3 — this is the INGEST vocabulary: which sources Chronon can read
/// for a given asset kind. It used to exist as four different literal sets
/// (`asset_registry.hpp` knew `.mp4/.mov/.avi/.mkv` and no `.webm`/`.m4v`; the
/// manifest knew `.mp4/.mov/.mkv/.webm/.avi/.m4v`; the CLI job validator and
/// the video sink adapter each had a third and fourth answer), so whether a
/// file was a video depended on which layer asked. One table, one answer.
///
/// This table is deliberately WIDER than the muxer output vocabulary
/// (`media::video::classify_video_container`): a source may be decodable even
/// when it cannot be published. That containment is the only legal difference
/// between the two, and it is pinned by a test.
[[nodiscard]] inline bool asset_kind_accepts_extension(
    PreparedAssetKind kind, std::string_view extension) noexcept {
    if (!extension.empty() && extension.front() == '.') extension.remove_prefix(1);
    const auto equals_ci = [extension](std::string_view token) noexcept {
        if (extension.size() != token.size()) return false;
        for (std::size_t index = 0; index < token.size(); ++index) {
            const auto lhs = static_cast<unsigned char>(extension[index]);
            const auto rhs = static_cast<unsigned char>(token[index]);
            if (std::tolower(lhs) != std::tolower(rhs)) return false;
        }
        return true;
    };
    const auto accepts = [&equals_ci](std::initializer_list<std::string_view> tokens) {
        for (const auto token : tokens) {
            if (equals_ci(token)) return true;
        }
        return false;
    };
    switch (kind) {
        case PreparedAssetKind::Font:
            return accepts({"ttf", "otf", "woff", "woff2"});
        case PreparedAssetKind::Image:
            return accepts({"png", "jpg", "jpeg", "webp", "bmp", "gif",
                            "tif", "tiff", "exr", "svg"});
        case PreparedAssetKind::Video:
            return accepts({"mp4", "mov", "mkv", "webm", "avi", "m4v"});
        case PreparedAssetKind::Audio:
            return accepts({"wav", "mp3", "aac", "m4a", "flac", "ogg"});
        case PreparedAssetKind::Subtitle:
            return accepts({"ass", "srt", "vtt", "json"});
        case PreparedAssetKind::Data:
            return true;
    }
    return true;
}

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
