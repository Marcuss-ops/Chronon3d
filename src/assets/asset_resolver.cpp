// ===========================================================================
// assets/asset_resolver.cpp
//
// WP-8 PR 8.0 — typed engine-local asset resolver implementation.
// Lives next to AssetRegistry (its sibling for import-by-extension /
// metadata-id work); ownership is on RenderRuntime.
// ===========================================================================

#include <chronon3d/assets/asset_resolver.hpp>

#include <stdexcept>
#include <system_error>
#include <utility>

namespace chronon3d::assets {

void AssetResolver::mount(std::filesystem::path root_path) {
    std::lock_guard<std::mutex> lock(*m_mutex);
    if (root_path.empty()) {
        m_root_path.clear();
        return;
    }
    if (!root_path.is_absolute()) {
        // PR 8.0 contract — every Resolver is engine-owned; mount roots
        // MUST be absolute so two engines can never accidentally share a
        // process-relative directory.  Rejecting up-front preserves the
        // documented "deterministic and independent per engine" guarantee.
        throw std::invalid_argument(
            "AssetResolver::mount(): root must be an absolute path; received: "
            + root_path.string());
    }
    m_root_path = root_path.lexically_normal();
}

void AssetResolver::unmount() {
    std::lock_guard<std::mutex> lock(*m_mutex);
    m_root_path.clear();
}

bool AssetResolver::has_mount() const noexcept {
    std::lock_guard<std::mutex> lock(*m_mutex);
    return !m_root_path.empty();
}

std::filesystem::path AssetResolver::mount_root() const {
    std::lock_guard<std::mutex> lock(*m_mutex);
    return m_root_path;
}

std::optional<std::filesystem::path>
AssetResolver::resolve_locked_(const std::filesystem::path& path) const {
    if (path.empty()) {
        return std::nullopt;
    }
    if (path.is_absolute()) {
        // Absolute bypasses mount entirely.
        return path.lexically_normal();
    }
    if (m_root_path.empty()) {
        // PR 8.0 contract: relative without mount → missing-asset.
        return std::nullopt;
    }
    const auto combined = (m_root_path / path).lexically_normal();
    // Clamp: lexically_relative returns the path's traversal above the
    // mount as ".." segments.  A single ".." anywhere rejects.  Same
    // semantics as the previous sibling-prefix string compare but
    // expressed in std::filesystem terms and resilient to root-prefix
    // attacks automatically.
    const auto rel = combined.lexically_relative(m_root_path);
    for (const auto& part : rel) {
        if (part == std::filesystem::path("..")) {
            return std::nullopt;
        }
    }
    return combined;
}

std::optional<std::filesystem::path>
AssetResolver::resolve(const std::filesystem::path& path) const {
    std::lock_guard<std::mutex> lock(*m_mutex);
    auto candidate = resolve_locked_(path);
    if (!candidate.has_value()) {
        return std::nullopt;
    }
    std::error_code ec;
    if (!std::filesystem::exists(*candidate, ec)) {
        return std::nullopt;
    }
    return candidate;
}

std::optional<std::filesystem::path>
AssetResolver::resolve_lexical(const std::filesystem::path& path) const {
    std::lock_guard<std::mutex> lock(*m_mutex);
    return resolve_locked_(path);
}

} // namespace chronon3d::assets
