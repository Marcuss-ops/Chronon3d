#include "render_plan_compiler_detail.hpp"
#include "lottie_expansion/lottie_expansion.hpp"

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>

namespace chronon3d::render_plan::detail {

ResolvedLayerMediaAssets resolve_layer_media_assets(
    const RenderPlan& plan, chronon3d::assets::AssetResolver& resolver) {
    ResolvedLayerMediaAssets result{
        std::vector<std::string>(plan.layers.size()),
        std::vector<std::string>(plan.layers.size())};
    for (std::size_t index = 0; index < plan.layers.size(); ++index) {
        const auto& layer = plan.layers[index];
        if (layer.type != LayerType::Video) continue;
        const auto path = resolver.resolve_logical(std::filesystem::path(layer.source));
        if (!path)
            throw std::runtime_error("video source was not prepared: " + layer.source);
        result.video_paths[index] = path->string();
    }
    for (std::size_t index = 0; index < plan.layers.size(); ++index) {
        const auto& layer = plan.layers[index];
        if (!layer.particle_emitter || layer.particle_emitter->sprite_asset.empty()) continue;
        const auto& asset = layer.particle_emitter->sprite_asset;
        const auto path = resolver.resolve_logical(std::filesystem::path(asset));
        if (!path)
            throw std::runtime_error("particle sprite asset was not prepared: " + asset);
        result.particle_sprite_paths[index] = path->string();
    }
    return result;
}

std::optional<PlanDecodeError> prepare_data_feed_assets(
    RenderPlan& plan, chronon3d::assets::AssetResolver& resolver) {
    for (std::size_t layer_index = 0; layer_index < plan.layers.size(); ++layer_index) {
        auto& layer = plan.layers[layer_index];
        if (!layer.animation) continue;
        for (std::size_t track_index = 0; track_index < layer.animation->tracks.size(); ++track_index) {
            auto& track = layer.animation->tracks[track_index];
            if (!track.data_feed || track.data_feed->asset.empty()) continue;
            auto& feed = *track.data_feed;
            const auto path = "layers[" + std::to_string(layer_index) + "].animation.tracks[" +
                std::to_string(track_index) + "].data_feed.asset";
            const auto resolved = resolver.resolve_logical(feed.asset);
            if (!resolved)
                return PlanDecodeError{path, "data_feed asset could not be resolved within the mounted asset root",
                    "data_feed_asset_unresolved", "render-plan-data-feed"};
            std::error_code size_error;
            const auto byte_size = std::filesystem::file_size(*resolved, size_error);
            constexpr std::uintmax_t kMaxDataFeedAssetBytes = 32ULL * 1024ULL * 1024ULL;
            if (size_error || byte_size > kMaxDataFeedAssetBytes)
                return PlanDecodeError{path, "data_feed asset must be readable and no larger than 32 MiB",
                    "data_feed_asset_size", "render-plan-data-feed"};
            const auto digest = chronon3d::assets::sha256_file(*resolved);
            if (!digest || digest->hex() != feed.sha256)
                return PlanDecodeError{path, "data_feed asset SHA-256 does not match the declared digest",
                    "data_feed_digest_mismatch", "render-plan-data-feed"};
            try {
                std::ifstream input(*resolved, std::ios::binary);
                const auto data = nlohmann::json::parse(input);
                if (!data.is_array() || data.size() != feed.item_count)
                    throw std::runtime_error("data_feed asset must be a numeric JSON array matching item_count");
                feed.values.clear();
                feed.values.reserve(data.size());
                for (const auto& sample : data) {
                    if (!sample.is_number() || !std::isfinite(sample.get<double>()) ||
                        !std::isfinite(sample.get<float>()))
                        throw std::runtime_error("data_feed asset values must be finite float numbers");
                    feed.values.push_back(sample.get<float>());
                }
            } catch (const std::exception& error) {
                return PlanDecodeError{path, error.what(), "data_feed_asset_invalid", "render-plan-data-feed"};
            }
        }
    }
    return std::nullopt;
}

std::optional<PlanDecodeError> expand_lottie_assets(
    RenderPlan& plan, chronon3d::assets::AssetResolver& resolver) {
    for (std::size_t layer_index = 0; layer_index < plan.layers.size(); ++layer_index) {
        auto& layer = plan.layers[layer_index];
        if (!layer.shape || !layer.shape->lottie_asset) continue;
        const auto logical = *layer.shape->lottie_asset;
        const auto resolved = resolver.resolve_logical(logical);
        if (!resolved)
            return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.lottie",
                "Lottie asset could not be resolved within the mounted asset root",
                "lottie_asset_unresolved", "render-plan-lottie"};
        std::vector<LayerPlan> expanded;
        auto lowered = expand_static_lottie(layer, *resolved, expanded);
        if (lowered)
            return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.lottie",
                *lowered, "lottie_unsupported_or_invalid", "render-plan-lottie"};
        const auto count = expanded.size();
        // Retain a single source reference for asset preflight and the prepared
        // asset manifest. The imported path data is already materialized.
        if (count != 0 && expanded.front().shape)
            expanded.front().shape->lottie_asset = logical;
        plan.layers.erase(plan.layers.begin() + static_cast<std::ptrdiff_t>(layer_index));
        plan.layers.insert(plan.layers.begin() + static_cast<std::ptrdiff_t>(layer_index),
            std::make_move_iterator(expanded.begin()), std::make_move_iterator(expanded.end()));
        layer_index += count - 1;
    }
    return std::nullopt;
}

} // namespace chronon3d::render_plan::detail
