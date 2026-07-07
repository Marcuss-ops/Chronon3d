// ── include/chronon3d/assets/asset_manifest.hpp
//
// Sequence V2 — Asset Manifest: typed asset references collected during
// composition build.
//
// Design:
//   AssetRef       — a reference to an asset (font, image, video, audio)
//   AssetManifest  — collects all AssetRef entries during build
//
// The manifest is populated by LayerBuilder when text_run(), image(),
// video() are called. It propagates through Layer → Scene so that
// AssetPreflightResolver can check all required assets before rendering.
//
// Usage:
//   AssetManifest manifest;
//   manifest.add_font("assets/fonts/Inter-Bold.ttf", "title/text");
//   manifest.add_image("assets/bg.png", "background/rect");
//   for (const auto& ref : manifest.assets()) { /* ... */ }

#pragma once

#include <chronon3d/assets/asset_metadata.hpp>  // AssetType
#include <iterator>
#include <string>
#include <vector>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// AssetRef — a typed reference to an external asset
// ═══════════════════════════════════════════════════════════════════════════
//
// Captures WHAT asset is needed, WHERE it's used, and WHETHER it's required.
// Collected during composition build; consumed by AssetPreflightResolver.
//
struct AssetRef {
    AssetType   kind{AssetType::Unknown};
    std::string path;
    std::string owner;      // e.g. "title/text" or "background/image"
    bool        required{true};
};

// ═══════════════════════════════════════════════════════════════════════════
// AssetManifest — collects asset references during build
// ═══════════════════════════════════════════════════════════════════════════
//
// Populated by LayerBuilder as assets are referenced. Aggregated by
// SceneBuilder across all layers. Consumed by AssetPreflightResolver.
//
class AssetManifest {
public:
    AssetManifest() = default;

    // ── Generic add ──────────────────────────────────────────────────

    void add(AssetRef ref) {
        m_assets.push_back(std::move(ref));
    }

    // ── Typed convenience methods ────────────────────────────────────

    void add_font(std::string path, std::string owner, bool required = true) {
        m_assets.push_back({AssetType::Font, std::move(path), std::move(owner), required});
    }

    void add_image(std::string path, std::string owner, bool required = true) {
        m_assets.push_back({AssetType::Image, std::move(path), std::move(owner), required});
    }

    void add_video(std::string path, std::string owner, bool required = true) {
        m_assets.push_back({AssetType::Video, std::move(path), std::move(owner), required});
    }

    void add_audio(std::string path, std::string owner, bool required = true) {
        m_assets.push_back({AssetType::Audio, std::move(path), std::move(owner), required});
    }

    // ── Accessors ────────────────────────────────────────────────────

    [[nodiscard]] const std::vector<AssetRef>& assets() const noexcept { return m_assets; }
    [[nodiscard]] std::vector<AssetRef>& assets() noexcept { return m_assets; }
    [[nodiscard]] bool empty() const noexcept { return m_assets.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_assets.size(); }

    // ── Merge another manifest into this one ─────────────────────────

    void merge(const AssetManifest& other) {
        m_assets.insert(m_assets.end(), other.m_assets.begin(), other.m_assets.end());
    }

    void merge(AssetManifest&& other) {
        m_assets.insert(m_assets.end(),
                        std::make_move_iterator(other.m_assets.begin()),
                        std::make_move_iterator(other.m_assets.end()));
    }

    // ── Filter by asset type ─────────────────────────────────────────

    [[nodiscard]] std::vector<AssetRef> filter(AssetType type) const {
        std::vector<AssetRef> result;
        for (const auto& ref : m_assets) {
            if (ref.kind == type) result.push_back(ref);
        }
        return result;
    }

    void clear() { m_assets.clear(); }

private:
    std::vector<AssetRef> m_assets;
};

} // namespace chronon3d
