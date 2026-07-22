#pragma once

#include <cmath>      // std::floor (current_integer_frame body)
#include <utility>    // std::forward, std::move

// ═══════════════════════════════════════════════════════════════════════════
// detail/scene_builder_inline.inl — Refactor 6a
// ═══════════════════════════════════════════════════════════════════════════
//
// Non-template member function bodies for SceneBuilder.
// Lives in src/-style include location next to the existing Phase-3.3
// .inl files (layers / sequences / camera). Included from the bottom of
// the public header `include/chronon3d/scene/builders/scene_builder.hpp`
// AFTER the class is fully declared; bodies below therefore have full
// access to SceneBuilder's private members and friends (CameraApi).
//
// `inline` keyword on every non-template body is required: the header is
// included by many TUs (composition pipeline, content, tests, presets),
// and inline-functions-with-external-linkage resolve to a single image
// definition. Templates are implicitly inline by definition so no extra
// keyword is needed on `edit_camera`.
//
// IMPORTANT — DO NOT introduce new public symbols here. This file is a
// pure body relocation; the public API surface remains identical to the
// pre-Refactor-6 header.

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// Constructors
// ═══════════════════════════════════════════════════════════════════════════

inline SceneBuilder::SceneBuilder(std::pmr::memory_resource* res,
                                  registry::ShapeRegistry* shape_registry)
    : scene_(res), current_time_(SampleTime::from_frame_int(0, FrameRate{30, 1})) {
    m_ctx.resource = res;
    m_ctx.width = m_width;
    m_ctx.height = m_height;
    if (shape_registry) {
        m_shape_registry = shape_registry;
    } else {
        m_own_shape_registry.emplace(registry::make_default_shape_registry());
        m_shape_registry = &*m_own_shape_registry;
    }
}

inline SceneBuilder::SceneBuilder(i32 width, i32 height,
                                  std::pmr::memory_resource* res,
                                  registry::ShapeRegistry* shape_registry)
    : scene_(res), current_time_(SampleTime::from_frame_int(0, FrameRate{30, 1})),
      m_width(width), m_height(height) {
    m_ctx.resource = res;
    m_ctx.width = width;
    m_ctx.height = height;
    if (shape_registry) {
        m_shape_registry = shape_registry;
    } else {
        m_own_shape_registry.emplace(registry::make_default_shape_registry());
        m_shape_registry = &*m_own_shape_registry;
    }
}

inline SceneBuilder::SceneBuilder(const FrameContext& ctx,
                                  registry::ShapeRegistry* shape_registry)
    : scene_(ctx.resource),
      current_time_(ctx.sample_time),
      m_ctx(ctx), m_width(ctx.width), m_height(ctx.height) {
    if (shape_registry) {
        m_shape_registry = shape_registry;
    } else {
        m_own_shape_registry.emplace(registry::make_default_shape_registry());
        m_shape_registry = &*m_own_shape_registry;
    }
    // WP-9 PR 9.0 / P1-16 — auto-forward the per-frame FontEngine from
    // the FrameContext's runtime to the builder's m_font_engine slot.
    // The canonical access path is `ctx.runtime->font_engine()`.
    // Explicit override guarantee: a composition lambda that calls
    // `s.font_engine(X)` later REPLACES this auto-bind with its own
    // pointer, so per-composition overrides continue to work.
    if (ctx.runtime && ctx.runtime->font_engine()) {
        m_font_engine = ctx.runtime->font_engine();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Font engine binding
// ═══════════════════════════════════════════════════════════════════════════

inline SceneBuilder& SceneBuilder::font_engine(FontEngine* engine) {
    m_font_engine = engine;
    return *this;
}

inline FontEngine* SceneBuilder::font_engine() const {
    return m_font_engine;
}

// ═══════════════════════════════════════════════════════════════════════════
// Camera façade
// ═══════════════════════════════════════════════════════════════════════════

inline CameraApi SceneBuilder::camera() {
    return CameraApi(*this);
}

inline SceneBuilder& SceneBuilder::animated_camera(const AnimatedCamera2_5D& cam) {
    set_camera(cam.evaluate(current_time_));
    return *this;
}

// ═══════════════════════════════════════════════════════════════════════════
// Lighting + post
// ═══════════════════════════════════════════════════════════════════════════

inline SceneBuilder& SceneBuilder::apply_depth_grade(const rendering::DepthGrade& grade) {
    scene_.set_depth_grade(grade);
    return *this;
}

// ═══════════════════════════════════════════════════════════════════════════
// Inspection
// ═══════════════════════════════════════════════════════════════════════════

inline std::pmr::memory_resource* SceneBuilder::resource() const {
    return scene_.resource();
}

inline Frame SceneBuilder::frame() const {
    return current_integer_frame();
}

inline SampleTime SceneBuilder::sample_time() const {
    return current_time_;
}

// ═══════════════════════════════════════════════════════════════════════════
// Private helpers (SceneBuilder self-access)
// ═══════════════════════════════════════════════════════════════════════════

inline void SceneBuilder::set_camera(Camera2_5D camera) {
    scene_.set_camera_2_5d(std::move(camera));
}

template <typename Fn>
void SceneBuilder::edit_camera(Fn&& fn) {
    auto cam = scene_.camera_2_5d();
    std::forward<Fn>(fn)(cam);
    scene_.set_camera_2_5d(cam);
}

inline Frame SceneBuilder::current_integer_frame() const {
    return Frame{static_cast<i64>(std::floor(current_time_.frame))};
}

} // namespace chronon3d
