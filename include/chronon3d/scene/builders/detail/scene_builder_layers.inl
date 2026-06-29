// ── include/chronon3d/scene/builders/detail/scene_builder_layers.inl ───
//
// Phase-3.3 mechanical split.  These OUT-OF-LINE template member
// definitions are the verbatim contents of SceneBuilder's class-body
// inline definitions, simply re-homed under `SceneBuilder::method`
// scope so the public header can declare-then-include-this-file.
//
// Behaviour is preserved BIT-IDENTICALLY:
//   * Every LayerBuilder(...) construction parameter pack.
//   * Every `std::forward<Fn>(fn)(...)` call site.
//   * Every `l.kind = LayerKind::...` discriminator.
//   * Every `l.active_at(current_integer_frame())` gate.
//   * Every `scene_.add_layer(std::move(layer))` / `scene_.add_node(...)`.
//   * The Trim() probe on `screen_dimensions(...)` flag and FontEngine cascade.
//
// `template` definitions in C++ are implicitly inline, so no
// `inline` keyword is needed on each definition below.

#pragma once

#include <utility>   // std::forward

namespace chronon3d {

    // ── Standard Layer ───────────────────────────────────────────────────────
    template <typename Fn>
    SceneBuilder &SceneBuilder::layer(std::string name, Fn &&fn) {
        LayerBuilder builder(std::move(name), current_time_, scene_.resource(), m_shape_registry);
        builder.screen_dimensions(static_cast<f32>(m_width), static_cast<f32>(m_height));
        builder.font_engine(m_font_engine);  // cascade scene-level bind
        std::forward<Fn>(fn)(builder);

        Layer l = builder.build();
        if (l.active_at(current_integer_frame())) {
            scene_.add_layer(std::move(l));
        }
        return *this;
    }

    // ── Screen Layer ─────────────────────────────────────────────────────────
    template <typename Fn>
    SceneBuilder &SceneBuilder::screen_layer(std::string name, Fn &&fn) {
        LayerBuilder builder(std::move(name), current_time_, scene_.resource(), m_shape_registry);
        builder.screen_dimensions(static_cast<f32>(m_width), static_cast<f32>(m_height));
        builder.font_engine(m_font_engine);  // cascade scene-level bind
        std::forward<Fn>(fn)(builder);

        Layer l = builder.build();
        if (l.active_at(current_integer_frame())) {
            scene_.add_layer(std::move(l));
        }
        return *this;
    }

    // ── Adjustment Layer ───────────────────────────────────────────────────
    template <typename Fn>
    SceneBuilder &SceneBuilder::adjustment_layer(std::string name, Fn &&fn) {
        LayerBuilder builder(std::move(name), current_time_, scene_.resource(), m_shape_registry);
        builder.screen_dimensions(static_cast<f32>(m_width), static_cast<f32>(m_height));
        builder.font_engine(m_font_engine);  // cascade scene-level bind
        std::forward<Fn>(fn)(builder);

        Layer l = builder.build();
        l.kind = LayerKind::Adjustment;
        if (l.active_at(current_integer_frame())) {
            scene_.add_layer(std::move(l));
        }
        return *this;
    }

    // ── Precomp Layer ────────────────────────────────────────────────────────
    template <typename Fn>
    SceneBuilder &SceneBuilder::precomp_layer(std::string name, std::string comp_name, Fn &&fn) {
        LayerBuilder builder(std::move(name), current_time_, scene_.resource(), m_shape_registry);
        builder.font_engine(m_font_engine);  // cascade scene-level bind
        std::forward<Fn>(fn)(builder);

        Layer l = builder.build();
        l.kind = LayerKind::Precomp;
        l.precomp_composition_name = std::pmr::string{comp_name, scene_.resource()};
        if (l.active_at(current_integer_frame())) {
            scene_.add_layer(std::move(l));
        }
        return *this;
    }

    // ── Video Layer (source overload) ───────────────────────────────────────
    template <typename Fn>
    SceneBuilder &SceneBuilder::video_layer(std::string name, video::VideoSource source, Fn &&fn) {
        LayerBuilder builder(std::move(name), current_time_, scene_.resource(), m_shape_registry);
        builder.font_engine(m_font_engine);  // cascade scene-level bind
        std::forward<Fn>(fn)(builder);

        Layer l = builder.build();
        l.kind = LayerKind::Video;
        l.video_source = std::make_unique<video::VideoSource>(std::move(source));
        if (l.active_at(current_integer_frame())) {
            scene_.add_layer(std::move(l));
        }
        return *this;
    }

    // ── Video Layer (path overload) ──────────────────────────────────────────
    template <typename Fn>
    SceneBuilder &SceneBuilder::video_layer(std::string name, std::string path, Fn &&fn) {
        video::VideoSource source;
        source.path = std::move(path);
        return video_layer(std::move(name), std::move(source), std::forward<Fn>(fn));
    }

    // ── Null Layer ──────────────────────────────────────────────────────────
    template <typename Fn>
    SceneBuilder &SceneBuilder::null_layer(std::string name, Fn &&fn) {
        if constexpr (std::is_invocable_v<Fn, NullBuilder&>) {
            NullParams params;
            params.name = std::move(name);
            NullBuilder builder(params);
            std::forward<Fn>(fn)(builder);

            Layer l(scene_.resource());
            l.name = std::pmr::string(params.name, scene_.resource());
            l.kind = LayerKind::Null;
            l.transform.position = params.transform.position;
            l.transform.rotation = glm::quat(glm::radians(params.transform.rotation));
            l.transform.scale = params.transform.scale;
            l.transform.anchor = params.transform.anchor;
            l.parent_name = std::pmr::string(params.transform.parent_name, scene_.resource());
            l.visible = params.visible_in_diagnostics;

            if (l.active_at(current_integer_frame())) {
                scene_.add_layer(std::move(l));
            }
            return *this;
        } else {
            LayerBuilder builder(std::move(name), current_time_, scene_.resource(), m_shape_registry);
            builder.font_engine(m_font_engine);  // cascade scene-level bind
            std::forward<Fn>(fn)(builder);

            Layer l = builder.build();
            l.kind = LayerKind::Null;
            if (l.active_at(current_integer_frame())) {
                scene_.add_layer(std::move(l));
            }
            return *this;
        }
    }

} // namespace chronon3d
