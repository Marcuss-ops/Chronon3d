#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/camera/animated_camera_2_5d.hpp>
#include <chronon3d/animation/stagger.hpp>
#include <string>
#include <vector>
#include <functional>
#include <optional>

namespace chronon3d {

/**
 * TimelineBuilder — declarative, fluent API for choreographing layers and
 * camera over time within a SceneBuilder.
 *
 * Usage:
 *   TimelineBuilder t(s);
 *   t.add("title").from(0).duration(45).with([](LayerBuilder& l) {
 *       l.text("t", {...});
 *       l.soft_pop();
 *   })
 *   .add("camera").from(0).duration(120).with_camera(camera_rig::hero_push_in())
 *   .stagger({"title"}, StaggerConfig{.delay_per_unit = Frame{4}}, StaggerOrder::LeftToRight)
 *   .apply();
 */
class TimelineBuilder {
public:
    explicit TimelineBuilder(SceneBuilder& scene);

    // ── Track declaration ──────────────────────────────────────────────────
    TimelineBuilder& add(std::string name);

    // ── Timing ─────────────────────────────────────────────────────────────
    TimelineBuilder& from(Frame f);
    TimelineBuilder& duration(Frame f);
    TimelineBuilder& to(Frame f); // sets duration = to - from

    // ── Content ────────────────────────────────────────────────────────────
    TimelineBuilder& with(std::function<void(LayerBuilder&)> fn);
    TimelineBuilder& with_camera(const AnimatedCamera2_5D& cam);

    // ── Stagger (applied in apply() after all tracks are built) ──────────
    TimelineBuilder& stagger(const std::vector<std::string>& names,
                             const StaggerConfig& config,
                             StaggerOrder order = StaggerOrder::LeftToRight);
    TimelineBuilder& stagger(const StaggerConfig& config,
                             StaggerOrder order = StaggerOrder::LeftToRight);

    // ── Apply all tracks to the underlying SceneBuilder ────────────────────
    void apply();

private:
    struct Track {
        std::string name;
        Frame from{0};
        Frame duration{-1};
        std::function<void(LayerBuilder&)> layer_fn;
        std::optional<AnimatedCamera2_5D> camera;
        bool is_camera{false};
    };

    struct StaggerOp {
        std::vector<std::string> names; // empty = all layers
        StaggerConfig config;
        StaggerOrder order{StaggerOrder::LeftToRight};
    };

    SceneBuilder& m_scene;
    std::vector<Track> m_tracks;
    std::vector<StaggerOp> m_staggers;
    size_t m_cursor{0}; // index of track currently being configured
};

} // namespace chronon3d
