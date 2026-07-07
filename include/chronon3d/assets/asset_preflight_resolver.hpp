// ── include/chronon3d/assets/asset_preflight_resolver.hpp
//
// Sequence V2 — AssetPreflightResolver: unified preflight that checks
// asset readiness before rendering.
//
// Design:
//   PreflightMode          — FullComposition or FrameOnly
//   AssetPreflightResult   — structured result with issues
//   AssetPreflightResolver — static check() method
//
// The resolver reads the AssetManifest from a Scene (collected during
// build by LayerBuilder) and verifies each asset exists via the
// AssetResolver. It reuses the existing PreflightIssue types from
// render_preflight.hpp for structured error reporting.
//
// Two modes:
//   FullComposition — check ALL assets in the scene manifest
//   FrameOnly       — check only assets from layers active at a given frame
//
// Usage:
//   auto result = AssetPreflightResolver::check(scene, resolver,
//       PreflightMode::FrameOnly, Frame{50});
//   if (!result.ok()) {
//       spdlog::error("{}", format_preflight_issues_text(result.issues));
//   }

#pragma once

#include <chronon3d/assets/asset_manifest.hpp>
#include <chronon3d/assets/asset_metadata.hpp>
#include <chronon3d/assets/render_preflight.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <string>
#include <vector>

namespace chronon3d {

// Forward declaration
namespace assets { class AssetResolver; }

// ═══════════════════════════════════════════════════════════════════════════
// PreflightMode — controls which assets are checked
// ═══════════════════════════════════════════════════════════════════════════

enum class PreflightMode {
    FullComposition,  // check all assets in the scene manifest
    FrameOnly         // check only assets from layers active at a given frame
};

// ═══════════════════════════════════════════════════════════════════════════
// AssetPreflightResult — structured result
// ═══════════════════════════════════════════════════════════════════════════

struct AssetPreflightResult {
    std::vector<PreflightIssue> issues;

    [[nodiscard]] bool ok() const {
        return !has_preflight_errors(issues);
    }

    [[nodiscard]] bool empty() const noexcept { return issues.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return issues.size(); }
};

// ═══════════════════════════════════════════════════════════════════════════
// AssetPreflightResolver — unified preflight checker
// ═══════════════════════════════════════════════════════════════════════════
//
// Static-only class. Reads the AssetManifest from a Scene and verifies
// each asset exists via the AssetResolver.
//
// For FrameOnly mode, only layers active at the given frame are checked.
// This is useful for still rendering — you don't want to fail because
// a video asset used at frame 900 is missing when rendering frame 10.
//
class AssetPreflightResolver {
public:
    // ── Primary entry point ──────────────────────────────────────────

    /// Check all assets in the scene manifest against the resolver.
    [[nodiscard]] static AssetPreflightResult check(
        const Scene& scene,
        const assets::AssetResolver& resolver,
        PreflightMode mode = PreflightMode::FullComposition,
        Frame frame = Frame{0}
    ) {
        AssetPreflightResult result;

        if (mode == PreflightMode::FullComposition) {
            check_manifest(scene.asset_manifest(), resolver, result);
        } else {
            // FrameOnly: build a manifest from only active layers
            AssetManifest active_manifest;
            for (const auto& layer : scene.layers()) {
                if (layer.active_at(frame)) {
                    active_manifest.merge(layer.asset_manifest);
                }
            }
            check_manifest(active_manifest, resolver, result);
        }

        return result;
    }

    // ── Convenience: check a single AssetManifest ────────────────────

    /// Check a manifest directly (no scene needed).
    [[nodiscard]] static AssetPreflightResult check_manifest(
        const AssetManifest& manifest,
        const assets::AssetResolver& resolver
    ) {
        AssetPreflightResult result;
        check_manifest(manifest, resolver, result);
        return result;
    }

private:
    // ── Internal: check each asset in a manifest ─────────────────────

    static void check_manifest(
        const AssetManifest& manifest,
        const assets::AssetResolver& resolver,
        AssetPreflightResult& result
    ) {
        for (const auto& ref : manifest.assets()) {
            if (ref.path.empty()) continue;

            auto resolved = resolver.resolve_lexical(ref.path);
            if (!resolved.has_value()) {
                PreflightIssue issue;
                issue.severity = ref.required
                    ? PreflightSeverity::Error
                    : PreflightSeverity::Warning;
                issue.type = to_preflight_type(ref.kind);
                issue.code = "ASSET_NOT_FOUND";
                issue.message = "Asset not found: " + ref.path;
                issue.path = ref.path;
                issue.layer_id = ref.owner;
                issue.recommendation = "Ensure the asset exists at the expected path.";
                result.issues.push_back(std::move(issue));
            }
        }
    }

    // ── Map AssetType → PreflightAssetType ───────────────────────────

    [[nodiscard]] static constexpr PreflightAssetType to_preflight_type(AssetType type) noexcept {
        switch (type) {
            case AssetType::Font:   return PreflightAssetType::Font;
            case AssetType::Image:  return PreflightAssetType::Image;
            case AssetType::Video:  return PreflightAssetType::Video;
            case AssetType::Audio:  return PreflightAssetType::Audio;
            default:                return PreflightAssetType::RegisteredAsset;
        }
    }
};

} // namespace chronon3d
