// ── include/chronon3d/scene/builders/sequence_builder.hpp
//
// Sequence V2 — SequenceBuilder facade.
//
// SequenceBuilder wraps a SceneBuilder and provides sequence-aware methods:
//   - layer(), screen_layer(), etc. (delegate to SceneBuilder)
//   - sequence() for nesting (creates nested SequenceBuilder)
//   - local_frame(), progress(), duration() context accessors
//
// Usage:
//   s.sequence("intro", {.from = Frame{0}, .duration = Frame{30}},
//     [](SequenceBuilder& seq) {
//       seq.layer("logo", [](LayerBuilder& l) {
//         l.rect("bg", {.size = {100, 100}, .color = Color::white()});
//       });
//       seq.sequence("subtitle", {.from = Frame{10}, .duration = Frame{20}},
//         [](SequenceBuilder& inner) {
//           inner.layer("text", [](LayerBuilder& l) { /* ... */ });
//         });
//     });
//
// The SequenceBuilder is created by SceneBuilder::sequence() when the lambda
// takes SequenceBuilder& (detected via if constexpr / std::is_invocable_v).
// Existing code using [](SceneBuilder& s) remains fully backward-compatible.

#pragma once

#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace chronon3d {

class SequenceBuilder {
public:
    SequenceBuilder(SceneBuilder& builder, FrameContext ctx,
                    Frame local_frame, Frame duration, f32 progress)
        : m_builder(builder), m_ctx(std::move(ctx)),
          m_local_frame(local_frame), m_duration(duration), m_progress(progress) {}

    // ── Layer methods (delegate to SceneBuilder) ─────────────────────

    template <typename Fn>
    SequenceBuilder& layer(std::string name, Fn&& fn) {
        m_builder.layer(std::move(name), std::forward<Fn>(fn));
        return *this;
    }

    template <typename Fn>
    SequenceBuilder& screen_layer(std::string name, Fn&& fn) {
        m_builder.screen_layer(std::move(name), std::forward<Fn>(fn));
        return *this;
    }

    template <typename Fn>
    SequenceBuilder& adjustment_layer(std::string name, Fn&& fn) {
        m_builder.adjustment_layer(std::move(name), std::forward<Fn>(fn));
        return *this;
    }

    template <typename Fn>
    SequenceBuilder& null_layer(std::string name, Fn&& fn) {
        m_builder.null_layer(std::move(name), std::forward<Fn>(fn));
        return *this;
    }

    template <typename Fn>
    SequenceBuilder& precomp_layer(std::string name, std::string comp_name, Fn&& fn) {
        m_builder.precomp_layer(std::move(name), std::move(comp_name), std::forward<Fn>(fn));
        return *this;
    }

    template <typename Fn>
    SequenceBuilder& video_layer(std::string name, video::VideoSource source, Fn&& fn) {
        m_builder.video_layer(std::move(name), std::move(source), std::forward<Fn>(fn));
        return *this;
    }

    // ── Shape methods (delegate to SceneBuilder) ─────────────────────

    SequenceBuilder& rect(std::string name, RectParams p) {
        m_builder.rect(std::move(name), std::move(p));
        return *this;
    }

    SequenceBuilder& rounded_rect(std::string name, RoundedRectParams p) {
        m_builder.rounded_rect(std::move(name), std::move(p));
        return *this;
    }

    SequenceBuilder& circle(std::string name, CircleParams p) {
        m_builder.circle(std::move(name), std::move(p));
        return *this;
    }

    SequenceBuilder& image(std::string name, ImageParams p) {
        m_builder.image(std::move(name), std::move(p));
        return *this;
    }

    // ── Nested sequence ─────────────────────────────────────────────
    //
    // C2 — delegates to SceneBuilder::compile_sequence(), the single
    // canonical implementation shared with SceneBuilder::sequence().
    // Asset manifest contract (A1) is enforced by compile_sequence().

    template <typename Fn>
    SequenceBuilder& sequence(const std::string& name,
                              SceneBuilder::SequenceSpec spec, Fn&& fn) {
        m_builder.compile_sequence(m_local_frame, m_ctx, spec,
                                   std::forward<Fn>(fn),
                                   m_builder.scene_,
                                   m_builder.m_shape_registry);
        return *this;
    }

    // ── Camera (delegate to SceneBuilder) ────────────────────────────

    [[nodiscard]] CameraApi camera() { return m_builder.camera(); }

    SequenceBuilder& animated_camera(const AnimatedCamera2_5D& cam) {
        m_builder.animated_camera(cam);
        return *this;
    }

    // ── Context accessors ───────────────────────────────────────────

    [[nodiscard]] Frame local_frame() const noexcept { return m_local_frame; }
    [[nodiscard]] Frame duration() const noexcept { return m_duration; }
    [[nodiscard]] f32 progress() const noexcept { return m_progress; }
    [[nodiscard]] const FrameContext& context() const noexcept { return m_ctx; }

    /// Series authoring sugar: add sequential sequences with cumulative `from`.
    [[nodiscard]] SeriesBuilder series(const std::string& name = {}) {
        return m_builder.series(name);
    }

    /// Escape hatch: access the underlying SceneBuilder for methods not
    /// yet wrapped by SequenceBuilder.
    [[nodiscard]] SceneBuilder& builder() noexcept { return m_builder; }
    [[nodiscard]] const SceneBuilder& builder() const noexcept { return m_builder; }

private:
    SceneBuilder& m_builder;
    FrameContext m_ctx{};
    Frame m_local_frame{0};
    Frame m_duration{0};
    f32 m_progress{0.0f};
};

} // namespace chronon3d

#include <chronon3d/scene/builders/detail/scene_builder_sequences.inl>
