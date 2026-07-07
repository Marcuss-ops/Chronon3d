#pragma once

#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/text/font_engine.hpp>  // FontEngine (M1.5#9 step 2 — optional engine override on text())
#include <memory_resource>
#include <string>

namespace chronon3d {

/// Maps a TextAnchor enum value to the corresponding world_transform
/// anchor for a text box of the given size.  The anchor determines which
/// point of the text box corresponds to the node's position.
///
/// Used by RenderNodeFactory::text(), RenderNodeFactory::text_run(),
/// and LayerBuilder::build() to ensure consistent anchor resolution.
inline Vec3 resolve_text_anchor(TextAnchor anchor, Vec2 box) {
    switch (anchor) {
        case TextAnchor::TopLeft:        return {0.0f, 0.0f, 0.0f};
        case TextAnchor::TopCenter:      return {box.x * 0.5f, 0.0f, 0.0f};
        case TextAnchor::Center:         return {box.x * 0.5f, box.y * 0.5f, 0.0f};
        case TextAnchor::BottomCenter:   return {box.x * 0.5f, box.y, 0.0f};
        case TextAnchor::BaselineLeft:   return {0.0f, box.y * 0.5f, 0.0f};
        case TextAnchor::BaselineCenter: return {box.x * 0.5f, box.y * 0.5f, 0.0f};
    }
    return {box.x * 0.5f, box.y * 0.5f, 0.0f}; // fallback = Center
}

class RenderNodeFactory {
public:
    static RenderNode rect(std::pmr::memory_resource* res, std::string name, const RectParams& p);
    static RenderNode rounded_rect(std::pmr::memory_resource* res, std::string name, const RoundedRectParams& p);
    static RenderNode circle(std::pmr::memory_resource* res, std::string name, const CircleParams& p);
    static RenderNode line(std::pmr::memory_resource* res, std::string name, const LineParams& p);
    static RenderNode path(std::pmr::memory_resource* res, std::string name, PathParams p);
    static RenderNode image(std::pmr::memory_resource* res, std::string name, ImageParams p);
    static RenderNode tiled_image(std::pmr::memory_resource* res, std::string name, ImageParams p);
    static RenderNode grid_background(std::pmr::memory_resource* res, std::string name, const GridBackgroundParams& p);

    // ── text factory (M1.5#9 step 2 — delegate to materialize_text_run_shape) ──
    //
    // Public signature is back-compat: callers that pass only
    // `(res, name, TextSpec)` still compile (engine gets the default
    // `nullptr`).  Internally `text(...)` now wraps the supplied
    // `TextSpec` into a `TextRunSpec{.text = p}` and delegates to the
    // canonical materializer `materialize_text_run_shape(...)`, sharing
    // the same core used by `text_run()` and `LayerBuilder::build()`.
    //
    // When `engine == nullptr` the materializer logs an `spdlog::error`
    // and returns nullptr (PR 8.0 removed the legacy
    // `&shared_font_engine()` fallback — caller must supply a
    // FontEngine*).  In that case the factory still produces a
    // RenderNode but with `node.shape.type() == ShapeType::TextRun`
    // and `node.shape.text_run_shape_handle().value == nullptr` —
    // the renderer-side source-pass routes the entry to TextRunNode
    // which surfaces the missing-shape diagnostic at frame time
    // (graceful degradation; better than silently dropping the row).
    static RenderNode text(
        std::pmr::memory_resource* res,
        std::string name,
        TextSpec p,
        FontEngine* engine = nullptr);

    // ── text-run factory ──
    //
    // Materializes a TextRunSpec (canonical composable; TextRunParams was
    // the prior alias) into a `RenderNode` flagged with
    // `is_text_run_shape=true`.  Shares its core with
    // `LayerBuilder::text_run(...)` via the helper
    // `materialize_text_run_shape(...)`.
    //
    // Evaluates animators at `sample_time` (default: frame 0, 30fps).
    // For per-frame fidelity within a composition, callers should
    // prefer `LayerBuilder::text_run(...)` which evaluates at the
    // layer's local time at build().
    static RenderNode text_run(
        std::pmr::memory_resource* res,
        std::string name,
        TextRunSpec p,
        FontEngine* engine = nullptr,
        SampleTime sample_time = {});

    // Specialized 3D Shapes
    static RenderNode fake_box3d(std::pmr::memory_resource* res, std::string name, const FakeBox3DParams& p);
    static RenderNode grid_plane(std::pmr::memory_resource* res, std::string name, const GridPlaneParams& p);

private:
    static RenderNode base(std::pmr::memory_resource* res, std::string name);
};

} // namespace chronon3d
