// Layer property method implementations for LayerBuilder.
// Mask, track matte, transitions, and video/precomp methods.
// Extracted from layer_builder.cpp to reduce file size.

#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/model/layer/track_matte.hpp>

#include <memory_resource>
#include <utility>

namespace chronon3d {

// ── Mask ──────────────────────────────────────────────────────────────

LayerBuilder& LayerBuilder::mask_rect(RectMaskParams p) {
    m_layer.mask.type = MaskType::Rect;
    m_layer.mask.size = p.size;
    m_layer.mask.pos = p.pos;
    m_layer.mask.inverted = p.inverted;
    return *this;
}

LayerBuilder& LayerBuilder::mask_rounded_rect(RoundedRectMaskParams p) {
    m_layer.mask.type = MaskType::RoundedRect;
    m_layer.mask.size = p.size;
    m_layer.mask.radius = p.radius;
    m_layer.mask.pos = p.pos;
    m_layer.mask.inverted = p.inverted;
    return *this;
}

LayerBuilder& LayerBuilder::mask_circle(CircleMaskParams p) {
    m_layer.mask.type = MaskType::Circle;
    m_layer.mask.size = {p.radius * 2.0f, p.radius * 2.0f};
    m_layer.mask.radius = p.radius;
    m_layer.mask.pos = p.pos;
    m_layer.mask.inverted = p.inverted;
    return *this;
}

// ── Track Matte ───────────────────────────────────────────────────────

LayerBuilder& LayerBuilder::track_matte_alpha(std::string src) {
    m_layer.track_matte.type = TrackMatteType::Alpha;
    m_layer.track_matte.source_layer = std::pmr::string{src, m_layer.track_matte.source_layer.get_allocator().resource()};
    return *this;
}

LayerBuilder& LayerBuilder::track_matte_alpha_inverted(std::string src) {
    m_layer.track_matte.type = TrackMatteType::AlphaInverted;
    m_layer.track_matte.source_layer = std::pmr::string{src, m_layer.track_matte.source_layer.get_allocator().resource()};
    return *this;
}

LayerBuilder& LayerBuilder::track_matte_luma(std::string src) {
    m_layer.track_matte.type = TrackMatteType::Luma;
    m_layer.track_matte.source_layer = std::pmr::string{src, m_layer.track_matte.source_layer.get_allocator().resource()};
    return *this;
}

LayerBuilder& LayerBuilder::track_matte_luma_inverted(std::string src) {
    m_layer.track_matte.type = TrackMatteType::LumaInverted;
    m_layer.track_matte.source_layer = std::pmr::string{src, m_layer.track_matte.source_layer.get_allocator().resource()};
    return *this;
}

// ── Transitions ───────────────────────────────────────────────────────

LayerBuilder& LayerBuilder::transition_in(LayerTransitionSpec spec) {
    m_layer.transition_in = std::move(spec);
    return *this;
}

LayerBuilder& LayerBuilder::transition_out(LayerTransitionSpec spec) {
    m_layer.transition_out = std::move(spec);
    return *this;
}

// ── Video / Precomp ───────────────────────────────────────────────────

LayerBuilder& LayerBuilder::video(video::VideoSource source) {
    m_layer.kind = LayerKind::Video;
    m_layer.video_source = std::make_unique<video::VideoSource>(std::move(source));
    return *this;
}

LayerBuilder& LayerBuilder::precomp(std::string comp_name) {
    m_layer.kind = LayerKind::Precomp;
    m_layer.precomp_composition_name = std::pmr::string{std::move(comp_name), resource()};
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

} // namespace chronon3d
