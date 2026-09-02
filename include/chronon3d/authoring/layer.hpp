// ═══════════════════════════════════════════════════════════════════════════
// Layer — thin authoring handle over a `LayerBuilder`.
//
// B1 — canonical authoring surface.  LayerBuilder is now an internal
// compilation target; the public API is `authoring::Layer`.  All methods
// delegate directly to the underlying LayerBuilder via `builder_->`.
//
// Mirrors `Text`'s design: `Layer` is a non-owning pointer to a
// `LayerBuilder` already owned by the caller.  `text(...)` returns a
// `Text` handle that mutates the new pending text-run in place.
//
// The lifetime invariant is identical to `Text`: as long as the
// `LayerBuilder` referenced by `Layer` stays alive, the `Text` handles
// returned by `text(...)` remain valid (the pending entries live in
// unique_ptr inside `LayerBuilder::m_text_runs` so push_back cannot
// invalidate them).
//
// ── Why a separate handle instead of just exposing text() on LayerBuilder?
//
// The design goal is "zero commit on destruction".  LayerBuilder's chain
// already does that implicitly (each setter returns `LayerBuilder&` for
// chaining) — but a Text handle on Layer creates a different shape:
//   `layer.text("H").font(...).animate(...)` — the `.font()` here mutates
//   directly, no commit step, and the handle can be discarded mid-chain
//   without losing state, because the underlying PendingTextRun is owned
//   by the parent LayerBuilder.
//
// ── CanvasInfo ────────────────────────────────────────────────────────────
//
// `Layer` carries the canonical `CanvasInfo` placement descriptor. Text
// placement never receives a second authoring FrameContext or a hidden
// 1920×1080 viewport.
//
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/node_handle.hpp>    // B1 — NodeHandle return from rect()
#include <chronon3d/text/font_engine.hpp>             // FontEngine — transitive via layer_builder.hpp -> text_run_builder.hpp; explicit here so the surface can document `FontEngine` in the doc-comment without an include-graph lookup
#include <chronon3d/authoring/text.hpp>
#include <chronon3d/authoring/subtitle_track_builder.hpp>
// Audit §10 — typed `assets::ImageRef` overload accepts the thin authoring
// helper `authoring::asset("images/logo.png")`.  Bridge delegates to the
// existing `image(name, ImageParams)` overload by extracting the canonical
// manifest-clean field `asset_path`.  No new resolver or factory is
// introduced; the per-runtime resolver still resolves the path at
// materialization time (see builder_params.hpp::detail::image_params_resolve_path).
#include <chronon3d/authoring/asset.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace chronon3d::authoring {

class Layer {
public:
    /// Primary constructor — caller-supplied `LayerBuilder` + canonical
    /// `CanvasInfo`. Custom safe-area margins remain intact through Text.
    Layer(LayerBuilder& builder, CanvasInfo canvas) noexcept
        : builder_(&builder), canvas_(std::move(canvas)) {}

    Layer(const Layer&)            = delete;
    Layer& operator=(const Layer&) = delete;
    Layer(Layer&&)                 = default;
    Layer& operator=(Layer&&)      = default;

    /// Push a new text-run entry into the parent layer and return a
    /// `Text` handle that mutates the new pending PreparedText in place.
    ///
    /// The new entry's `name` is auto-generated as `text_<N>` where N is
    /// the per-Layer instance counter.  Multiple `.text(...)` calls on the
    /// same Layer produce independent entries.
    Text text(std::string content) {
        const std::string generated_name =
            "text_" + std::to_string(next_text_index_++);

        // Push the empty pending entry first.  Materialized when
        // LayerBuilder::build() runs.
        PreparedText seed_spec{};
        seed_spec.document.utf8 = std::move(content);

        TextRunBuilder& builder = builder_->text_run(
            generated_name,
            std::move(seed_spec)
        );

        // Public PR 3 accessor on TextRunBuilder — single Core Surface
        // extension.  The returned reference is non-owning and stable
        // (the spec itself lives behind a `unique_ptr` in
        // `LayerBuilder::m_text_runs`).
        PendingTextRun& pending = builder.mutable_pending();

        return Text{pending, &canvas_};
    }

    // ── B1 — Layer-level transforms (delegate to LayerBuilder) ─────────

    Layer& position(Vec3 p)  { builder_->position(p);  return *this; }
    Layer& scale(Vec3 s)     { builder_->scale(s);     return *this; }
    Layer& rotate(Vec3 euler_deg) { builder_->rotate(euler_deg); return *this; }
    Layer& anchor(Vec3 a)    { builder_->anchor(a);    return *this; }
    Layer& opacity(f32 v)    { builder_->opacity(v);   return *this; }

    // ── B1 — Timing (delegate to LayerBuilder) ─────────────────────────

    Layer& from(Frame f)    { builder_->from(f);    return *this; }
    Layer& duration(Frame f){ builder_->duration(f);return *this; }
    Layer& until(Frame f)   { builder_->until(f);   return *this; }
    Layer& offset(Frame f)  { builder_->offset(f);  return *this; }

    // ── B1 — Time Remap (delegate to LayerBuilder) ─────────────────────

    Layer& speed(f32 m)         { builder_->speed(m);         return *this; }
    Layer& reverse(bool v=true) { builder_->reverse(v);       return *this; }
    Layer& freeze_frame(Frame f){ builder_->freeze_frame(f);  return *this; }

    // ── B1 — Layout (delegate to LayerBuilder) ─────────────────────────

    Layer& pin_to(Anchor a, f32 margin = 0.0f) {
        builder_->pin_to(a, margin);
        return *this;
    }
    Layer& pin_to(AnchorPlacement placement, f32 margin = 0.0f) {
        builder_->pin_to(placement, margin);
        return *this;
    }

    // ── B1 — Effects (delegate to LayerBuilder) ────────────────────────

    Layer& blur(f32 radius)   { builder_->blur(radius);   return *this; }
    Layer& tint(Color color, f32 amount = 1.0f) { builder_->tint(color, amount); return *this; }
    Layer& brightness(f32 v)  { builder_->brightness(v);  return *this; }
    Layer& contrast(f32 v)    { builder_->contrast(v);    return *this; }
    Layer& saturation(f32 v)  { builder_->saturation(v);  return *this; }
    Layer& vignette(f32 radius = 0.5f, f32 softness = 0.5f, f32 amount = 1.0f) {
        builder_->vignette(radius, softness, amount);
        return *this;
    }
    Layer& drop_shadow(Vec2 offset, Color color = {0,0,0,0.35f}, f32 radius = 12.0f) {
        builder_->drop_shadow(offset, color, radius);
        return *this;
    }

    // ── B1 — Flags (delegate to LayerBuilder) ──────────────────────────

    Layer& visible(bool v)        { builder_->visible(v);      return *this; }
    Layer& kind(LayerKind k)      { builder_->kind(k);         return *this; }
    Layer& cache_static(bool v = true) { builder_->cache_static(v); return *this; }
    Layer& enable_3d(bool v=true) { builder_->enable_3d(v);    return *this; }

    // ── B1 — Blend mode (delegate to LayerBuilder) ─────────────────────

    Layer& blend(BlendMode mode) { builder_->blend(mode); return *this; }

    // ── B1 — Masks (delegate to LayerBuilder) ──────────────────────────

    Layer& mask_rect(RectMaskParams p) { builder_->mask_rect(std::move(p)); return *this; }
    Layer& mask_circle(CircleMaskParams p) { builder_->mask_circle(std::move(p)); return *this; }

    // ── B1 — Shape creation — returns NodeHandle for explicit transform access ──

    /// Create a rectangle shape and return a NodeHandle for explicit
    /// per-node transform chaining (A4 pattern).
    [[nodiscard]] NodeHandle rect(std::string name, RectParams p) {
        builder_->rect(std::move(name), std::move(p));
        return builder_->last_node_handle();
    }

    /// Create a circle shape and return a NodeHandle.
    [[nodiscard]] NodeHandle circle(std::string name, CircleParams p) {
        builder_->circle(std::move(name), std::move(p));
        return builder_->last_node_handle();
    }

    /// Create an image shape and return a NodeHandle.
    [[nodiscard]] NodeHandle image(std::string name, ImageParams p) {
        builder_->image(std::move(name), std::move(p));
        return builder_->last_node_handle();
    }

    /// Create an unloaded mesh node and register its logical dependency.
    /// Mesh import and filesystem resolution remain runtime preparation work.
    [[nodiscard]] NodeHandle mesh(assets::MeshRef ref) {
        builder_->mesh("mesh_" + std::to_string(next_mesh_index_++), std::move(ref));
        return builder_->last_node_handle();
    }

    // Audit §10 — typed `assets::ImageRef` overload so that
    //   `layer.image("logo", authoring::asset("images/logo.png"))`
    // compiles.  Bridge: extract the canonical `asset_path` field on
    // `ImageParams` (manifest-clean alternative to the deprecated
    // `path` field — see builder_params.hpp forward-point 0e) and
    // forward to the existing `image(name, ImageParams)`.  No new
    // resolver/factory/singleton — the canonical per-runtime
    // AssetResolver (mounted via `engine.set_assets_root()`) still
    // resolves the path at materialization.
    [[nodiscard]] NodeHandle image(std::string name, assets::ImageRef ref) {
        ImageParams p;
        p.source = std::move(ref);
        return image(std::move(name), std::move(p));
    }

    /// Create a rounded rectangle and return a NodeHandle.
    [[nodiscard]] NodeHandle rounded_rect(std::string name, RoundedRectParams p) {
        builder_->rounded_rect(std::move(name), std::move(p));
        return builder_->last_node_handle();
    }

    Layer& fill(Color color) {
        builder_->fill(color);
        return *this;
    }

    Layer& fullscreen_rect(std::string name, Color color) {
        builder_->fullscreen_rect(std::move(name), color);
        return *this;
    }

    // ── WP-8 PR 8.2 — per-layer FontEngine default ────────────────────
    // Forwards to `LayerBuilder::font_engine(FontEngine*)`, mirroring the
    // authoring `Text::font_engine(*)` per-spec override (defined upstream
    // in `include/chronon3d/authoring/text.hpp`).  Resolution order at
    // materialization (see `src/scene/builders/layer_builder_core.cpp:397` —
    // `spec.font_engine ? spec.font_engine : m_font_engine`):
    //   1. PreparedText/PendingTextRun.font_engine bound via Text::font_engine(...)
    //   2. LayerBuilder.m_font_engine bound here.
    //   3. The owning SoftwareRenderer's `renderer.font_engine()` (built
    //      from `runtime().resolver()`) at the draw site.
    // Setting nullptr here clears the layer default and falls back to the
    // renderer-level engine for every text node in the layer.
    Layer& font_engine(FontEngine* engine) {
        builder_->font_engine(engine);
        return *this;
    }

    /// Create a scheduled subtitle track from a canonical SubtitleTrack.
    /// Returns a SubtitleTrackBuilder for fluent configuration; call
    /// `.build()` to commit the cues as timed text-runs.
    [[nodiscard]] SubtitleTrackBuilder subtitles(const presets::text::SubtitleTrack& track) {
        return SubtitleTrackBuilder{*builder_, canvas_, track};
    }

    [[nodiscard]] const CanvasInfo&   canvas()          const noexcept { return canvas_; }

private:
    LayerBuilder* builder_;
    CanvasInfo    canvas_;
    std::size_t   next_text_index_{0};
    std::size_t   next_mesh_index_{0};
};

} // namespace chronon3d::authoring
