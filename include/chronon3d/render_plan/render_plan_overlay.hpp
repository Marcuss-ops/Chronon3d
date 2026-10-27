#pragma once

#include <string>
#include <vector>

namespace chronon3d::render_plan {

struct PreparedOverlayPackage {
    struct OverlayBinding {
        std::string id;
        std::string kind;
        std::string text_key;
        std::string asset_key;
    };
    struct TextBinding {
        std::string key;
        std::string text;
    };
    struct AssetBinding {
        std::string key;
        std::string logical_path;
        std::string content_hash;
    };

    std::string plan_id;
    std::string language;
    std::string content_hash;
    std::vector<OverlayBinding> overlays;
    std::vector<TextBinding> text_runs;
    std::vector<AssetBinding> assets;
};


} // namespace chronon3d::render_plan
