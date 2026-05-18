#pragma once

#include <chronon3d/scene/layer/layer.hpp>
#include <string_view>

namespace chronon3d {

/**
 * @brief Centralized resolver for layer types and metadata.
 */
class LayerKindResolver {
public:
    [[nodiscard]] static std::string_view to_string(LayerKind kind) {
        switch (kind) {
            case LayerKind::Normal:     return "Normal";
            case LayerKind::Adjustment: return "Adjustment";
            case LayerKind::Null:       return "Null";
            case LayerKind::Precomp:    return "Precomp";
            case LayerKind::Video:      return "Video";
            case LayerKind::Glass:      return "Glass";
            default:                    return "Unknown";
        }
    }

    [[nodiscard]] static LayerKind from_string(std::string_view name) {
        if (name == "Normal")     return LayerKind::Normal;
        if (name == "Adjustment") return LayerKind::Adjustment;
        if (name == "Null")       return LayerKind::Null;
        if (name == "Precomp")    return LayerKind::Precomp;
        if (name == "Video")      return LayerKind::Video;
        if (name == "Glass")      return LayerKind::Glass;
        return LayerKind::Normal;
    }

    [[nodiscard]] static bool is_visual(LayerKind kind) {
        return kind != LayerKind::Null;
    }

    [[nodiscard]] static bool supports_effects(LayerKind kind) {
        return kind != LayerKind::Null;
    }
};

} // namespace chronon3d
