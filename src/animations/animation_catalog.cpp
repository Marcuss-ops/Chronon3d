#include <chronon3d/api/animations.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>

namespace chronon3d::api {

namespace {

using Clip = AnimationClipDescriptor;

const std::array<Clip, 5> kBuiltinClips = {{
    {
        .id = "studio_grid_loop",
        .name = "Studio Grid Loop",
        .family = "background",
        .description = "A smooth neon studio grid with drifting glow and particles.",
        .duration_seconds = 3.0f,
        .fps = 30,
        .loopable = true,
        .realtime_safe = true,
    },
    {
        .id = "gradient_orbs_loop",
        .name = "Gradient Orbs Loop",
        .family = "background",
        .description = "Soft color orbs blended into a fluid gradient field.",
        .duration_seconds = 3.0f,
        .fps = 30,
        .loopable = true,
        .realtime_safe = true,
    },
    {
        .id = "parallax_space_hero",
        .name = "Parallax Space Hero",
        .family = "hero",
        .description = "Layered parallax cards with space depth and soft foreground motion.",
        .duration_seconds = 3.0f,
        .fps = 30,
        .loopable = true,
        .realtime_safe = false,
    },
    {
        .id = "data_stream_loop",
        .name = "Data Stream Loop",
        .family = "background",
        .description = "Vertical data streams with a compact neon flow.",
        .duration_seconds = 3.0f,
        .fps = 30,
        .loopable = true,
        .realtime_safe = true,
    },
    {
        .id = "premium_studio_hero",
        .name = "Premium Studio Hero",
        .family = "hero",
        .description = "Premium layered studio treatment with glow, cards, and floating accents.",
        .duration_seconds = 3.0f,
        .fps = 30,
        .loopable = true,
        .realtime_safe = false,
    },
}};

const AnimationCatalog& builtin_catalog_storage() {
    static const AnimationCatalog catalog{std::vector<Clip>(kBuiltinClips.begin(), kBuiltinClips.end())};
    return catalog;
}

} // namespace

const AnimationCatalog& builtin_animation_catalog() {
    return builtin_catalog_storage();
}

const AnimationClipDescriptor* find_builtin_animation(std::string_view id) {
    const auto& clips = builtin_animation_catalog().clips;
    const auto it = std::find_if(clips.begin(), clips.end(), [id](const Clip& clip) {
        return clip.id == id;
    });
    return it == clips.end() ? nullptr : &*it;
}

std::string builtin_animation_catalog_json() {
    nlohmann::json root = nlohmann::json::array();
    for (const auto& clip : builtin_animation_catalog().clips) {
        root.push_back({
            {"id", clip.id},
            {"name", clip.name},
            {"family", clip.family},
            {"description", clip.description},
            {"duration_seconds", clip.duration_seconds},
            {"fps", clip.fps},
            {"loopable", clip.loopable},
            {"realtime_safe", clip.realtime_safe},
        });
    }
    return root.dump(2);
}

} // namespace chronon3d::api

extern "C" const char* chronon3d_animation_catalog_json() {
    static const std::string json = chronon3d::api::builtin_animation_catalog_json();
    return json.c_str();
}
