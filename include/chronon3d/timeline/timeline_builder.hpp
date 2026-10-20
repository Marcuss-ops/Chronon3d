#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/animation/effects/stagger.hpp>
#include <string>
#include <vector>
#include <functional>

namespace chronon3d {

/**
 * TimelineBuilder — declarative, fluent API for choreographing layers within
 * a SceneBuilder.
 *
 * Usage:
 *   TimelineBuilder t(s);
 *   t.add("title").from(0).duration(45).with([](LayerBuilder& l) {
 *       l.text("t", {...});
 *       l.scale_anim().key(Frame{0}, Vec3{0.9f, 0.9f, 1.0f})
 *                      .key(Frame{20}, Vec3{1.0f, 1.0f, 1.0f});
 *   })
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
