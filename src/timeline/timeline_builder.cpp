#include <chronon3d/timeline/timeline_builder.hpp>

namespace chronon3d {

TimelineBuilder::TimelineBuilder(SceneBuilder& scene)
    : m_scene(scene) {}

TimelineBuilder& TimelineBuilder::add(std::string name) {
    Track track;
    track.name = std::move(name);
    m_tracks.push_back(std::move(track));
    m_cursor = m_tracks.size() - 1;
    return *this;
}

TimelineBuilder& TimelineBuilder::from(Frame f) {
    if (!m_tracks.empty()) m_tracks[m_cursor].from = f;
    return *this;
}

TimelineBuilder& TimelineBuilder::duration(Frame f) {
    if (!m_tracks.empty()) m_tracks[m_cursor].duration = f;
    return *this;
}

TimelineBuilder& TimelineBuilder::to(Frame f) {
    if (!m_tracks.empty()) m_tracks[m_cursor].duration = f - m_tracks[m_cursor].from;
    return *this;
}

TimelineBuilder& TimelineBuilder::with(std::function<void(LayerBuilder&)> fn) {
    if (!m_tracks.empty()) m_tracks[m_cursor].layer_fn = std::move(fn);
    return *this;
}

TimelineBuilder& TimelineBuilder::with_camera(const AnimatedCamera2_5D& cam) {
    if (!m_tracks.empty()) {
        m_tracks[m_cursor].camera = cam;
        m_tracks[m_cursor].is_camera = true;
    }
    return *this;
}

TimelineBuilder& TimelineBuilder::stagger(const std::vector<std::string>& names,
                                          const StaggerConfig& config,
                                          StaggerOrder order) {
    m_staggers.push_back({names, config, order});
    return *this;
}

TimelineBuilder& TimelineBuilder::stagger(const StaggerConfig& config, StaggerOrder order) {
    m_staggers.push_back({{}, config, order});
    return *this;
}

void TimelineBuilder::apply() {
    // 1. Build every track into the scene
    for (const auto& track : m_tracks) {
        if (track.is_camera && track.camera) {
            m_scene.animated_camera(*track.camera);
        } else if (track.layer_fn) {
            m_scene.layer(track.name, [&](LayerBuilder& lb) {
                if (track.duration >= Frame{0}) {
                    lb.from(track.from).duration(track.duration);
                } else {
                    lb.from(track.from);
                }
                track.layer_fn(lb);
            });
        }
    }

    // 2. Apply stagger operations in declaration order
    for (const auto& op : m_staggers) {
        if (op.names.empty()) {
            m_scene.stagger(op.config, op.order);
        } else {
            m_scene.stagger(op.names, op.config, op.order);
        }
    }
}

} // namespace chronon3d
