// LayerBuilder media-domain methods.
// Contains video and precomp method implementations.
// Extracted from commands/layer_property_commands.cpp as part of the
// domain split (core, transform, layout, text, shapes, effects, media, masks).

#include <chronon3d/scene/builders/layer_builder.hpp>

#include <memory_resource>
#include <utility>

namespace chronon3d {

LayerBuilder& LayerBuilder::video(video::VideoSource source) {
    // Sequence V2: collect video asset reference.
    if (!source.path.empty()) {
        m_layer.asset_manifest.add_video(source.path, std::string(m_layer.name) + "/video");
    }
    m_layer.kind = LayerKind::Video;
    m_layer.video_source = std::make_unique<video::VideoSource>(std::move(source));
    return *this;
}

LayerBuilder& LayerBuilder::video(std::string path) {
    video::VideoSource source;
    source.path = std::move(path);
    return video(std::move(source));
}

LayerBuilder& LayerBuilder::video_size(Vec2 size) {
    if (m_layer.video_source) {
        m_layer.video_source->size = size;
    }
    return *this;
}

LayerBuilder& LayerBuilder::precomp(std::string comp_name) {
    m_layer.kind = LayerKind::Precomp;
    m_layer.precomp_composition_name = std::pmr::string{std::move(comp_name), resource()};
    return *this;
}

} // namespace chronon3d
