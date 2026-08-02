#include <chronon3d/assets/prepared_asset_manifest.hpp>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <sstream>
#include <string>
#include <limits>
#include <utility>

namespace chronon3d::assets {
namespace {

class Sha256 {
public:
    Sha256() : m_state{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                       0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U} {}

    void update(const void* data, std::size_t size) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        while (size != 0) {
            const auto count = std::min(size, m_block.size() - m_block_size);
            std::copy_n(bytes, count, m_block.begin() + m_block_size);
            m_block_size += count;
            bytes += count;
            size -= count;
            m_total_bytes += count;
            if (m_block_size == m_block.size()) {
                transform(m_block.data());
                m_block_size = 0;
            }
        }
    }

    [[nodiscard]] ContentDigest finish() const {
        Sha256 copy = *this;
        const std::uint64_t bit_count = copy.m_total_bytes * 8U;
        const std::uint8_t one = 0x80U;
        copy.update(&one, 1);
        const std::uint8_t zero = 0;
        while (copy.m_block_size != 56U) copy.update(&zero, 1);
        std::array<std::uint8_t, 8> length{};
        for (std::size_t i = 0; i < length.size(); ++i)
            length[length.size() - 1U - i] =
                static_cast<std::uint8_t>(bit_count >> (i * 8U));
        copy.update(length.data(), length.size());

        ContentDigest digest;
        for (std::size_t i = 0; i < copy.m_state.size(); ++i) {
            digest.bytes[i * 4U] =
                static_cast<std::byte>(copy.m_state[i] >> 24U);
            digest.bytes[i * 4U + 1U] =
                static_cast<std::byte>(copy.m_state[i] >> 16U);
            digest.bytes[i * 4U + 2U] =
                static_cast<std::byte>(copy.m_state[i] >> 8U);
            digest.bytes[i * 4U + 3U] =
                static_cast<std::byte>(copy.m_state[i]);
        }
        return digest;
    }

private:
    static constexpr std::array<std::uint32_t, 64> k_round_constants{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

    static constexpr std::uint32_t rotate_right(std::uint32_t value,
                                                  unsigned count) {
        return (value >> count) | (value << (32U - count));
    }

    void transform(const std::uint8_t* block) {
        std::array<std::uint32_t, 64> schedule{};
        for (std::size_t i = 0; i < 16U; ++i) {
            schedule[i] = (static_cast<std::uint32_t>(block[i * 4U]) << 24U) |
                          (static_cast<std::uint32_t>(block[i * 4U + 1U]) << 16U) |
                          (static_cast<std::uint32_t>(block[i * 4U + 2U]) << 8U) |
                          static_cast<std::uint32_t>(block[i * 4U + 3U]);
        }
        for (std::size_t i = 16U; i < schedule.size(); ++i) {
            const auto s0 = rotate_right(schedule[i - 15U], 7U) ^
                            rotate_right(schedule[i - 15U], 18U) ^
                            (schedule[i - 15U] >> 3U);
            const auto s1 = rotate_right(schedule[i - 2U], 17U) ^
                            rotate_right(schedule[i - 2U], 19U) ^
                            (schedule[i - 2U] >> 10U);
            schedule[i] = schedule[i - 16U] + s0 + schedule[i - 7U] + s1;
        }

        auto working = m_state;
        for (std::size_t i = 0; i < schedule.size(); ++i) {
            const auto s1 = rotate_right(working[4], 6U) ^
                            rotate_right(working[4], 11U) ^
                            rotate_right(working[4], 25U);
            const auto choose = (working[4] & working[5]) ^
                                ((~working[4]) & working[6]);
            const auto temp1 = working[7] + s1 + choose +
                               k_round_constants[i] + schedule[i];
            const auto s0 = rotate_right(working[0], 2U) ^
                            rotate_right(working[0], 13U) ^
                            rotate_right(working[0], 22U);
            const auto majority = (working[0] & working[1]) ^
                                  (working[0] & working[2]) ^
                                  (working[1] & working[2]);
            const auto temp2 = s0 + majority;
            working[7] = working[6];
            working[6] = working[5];
            working[5] = working[4];
            working[4] = working[3] + temp1;
            working[3] = working[2];
            working[2] = working[1];
            working[1] = working[0];
            working[0] = temp1 + temp2;
        }
        for (std::size_t i = 0; i < m_state.size(); ++i) m_state[i] += working[i];
    }

    std::array<std::uint32_t, 8> m_state{};
    std::array<std::uint8_t, 64> m_block{};
    std::size_t m_block_size{0};
    std::uint64_t m_total_bytes{0};
};

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
            return matches({".srt", ".vtt", ".json"});
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
        if (layer.type == render_plan::LayerType::Image)
            add(layer.asset, PreparedAssetKind::Image);
        else if (layer.type == render_plan::LayerType::Video)
            add(layer.source, PreparedAssetKind::Video);
        else if (layer.type == render_plan::LayerType::SubtitleTrack) {
            add(layer.source, PreparedAssetKind::Subtitle);
            add(layer.font, PreparedAssetKind::Font);
        } else if (layer.type == render_plan::LayerType::Text) {
            add(layer.font, PreparedAssetKind::Font);
        }
    }
    for (const auto& track : plan.audio_tracks)
        add(track.source, PreparedAssetKind::Audio);

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

    std::ifstream input(path, std::ios::binary);
    if (!input)
        return error(AssetPreflightErrorCode::ReadFailed, std::move(logical_path),
                     "asset could not be opened for hashing");
    Sha256 sha;
    std::array<char, 64U * 1024U> buffer{};
    std::uint64_t read_size = 0;
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count <= 0) break;
        const auto bytes = static_cast<std::uint64_t>(count);
        if (read_size > max_size || bytes > max_size - read_size)
            return error(AssetPreflightErrorCode::AssetTooLarge, std::move(logical_path),
                         "asset exceeds the configured single-asset limit while reading");
        sha.update(buffer.data(), static_cast<std::size_t>(count));
        read_size += bytes;
    }
    if (input.bad() || read_size != expected_size)
        return error(AssetPreflightErrorCode::HashFailed, std::move(logical_path),
                     "asset changed or failed while hashing");
    return sha.finish();
}

void add_u64(Sha256& sha, std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>(value >> (i * 8U));
    sha.update(bytes.data(), bytes.size());
}

} // namespace

std::string ContentDigest::hex() const {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : bytes)
        output << std::setw(2) << static_cast<unsigned>(std::to_integer<unsigned>(byte));
    return output.str();
}

ContentDigest sha256_string(std::string_view value) {
    Sha256 sha;
    sha.update(value.data(), value.size());
    return sha.finish();
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

        auto digest = hash_file(canonical, normalized, byte_size,
                                policy.max_single_asset_bytes);
        if (!digest) return std::move(digest).error();

        timestamp_ec.clear();
        const auto timestamp_after = std::filesystem::last_write_time(
            canonical, timestamp_ec);
        if (timestamp_ec || timestamp_before != timestamp_after) {
            return error(AssetPreflightErrorCode::AssetChangedAfterPreflight,
                         normalized,
                         "asset changed while its prepared bytes were hashed");
        }
        std::error_code size_ec;
        const auto size_after = std::filesystem::file_size(canonical, size_ec);
        if (size_ec || size_after != byte_size) {
            return error(AssetPreflightErrorCode::AssetChangedAfterPreflight,
                         normalized,
                         "asset size changed while its prepared bytes were hashed");
        }

        PreparedAsset prepared{
            normalized,
            request.kind,
            byte_size,
            std::move(digest).value(),
            static_cast<std::int64_t>(timestamp_after.time_since_epoch().count())};
        manifest_hash.update(prepared.logical_path.data(), prepared.logical_path.size());
        const auto kind = static_cast<std::uint8_t>(prepared.kind);
        manifest_hash.update(&kind, sizeof(kind));
        add_u64(manifest_hash, prepared.byte_size);
        manifest_hash.update(prepared.content_digest.bytes.data(),
                             prepared.content_digest.bytes.size());
        total_bytes += byte_size;
        manifest.m_assets.push_back(std::move(prepared));
    }

    manifest.m_manifest_digest = manifest_hash.finish();
    return manifest;
}

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

} // namespace chronon3d::assets
