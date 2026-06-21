// ═══════════════════════════════════════════════════════════════════════════
// Text — thin authoring handle over a `PendingTextRun`.
//
// `Text` is NOT a destructor-committer — it's a non-owning hand to the
// `PendingTextRun` already stored inside the parent `LayerBuilder`'s
// `m_text_runs` vector.  All setters mutate `pending_->params` (the
// underlying `TextRunSpec`) directly; destruction is a no-op.
//
// ── Why a handle and not a builder owned by the layer? ──────────────────
//
// Owning the `TextRunSpec` locally would force a commit-on-destruction
// side effect, which is dangerous (move semantics, exceptions, stack
// unwinding).  Instead `Text` mutates the single source of truth — the
// pending entry — and the `LayerBuilder::build()` path reads the mutated
// spec verbatim during node materialization.  This keeps the authoring
// composition symmetric with the rest of the scene.
//
// ── Position semantics — explicit, not ambiguous ────────────────────────
//
// The legacy `TextRunBuilder::position(Vec3)` injects an *implicit*
// per-glyph `PositionProperty` animator (weight=1 for every glyph).
// The new façade does NOT replicate that — `Text::at(Vec3)` mutates
// `spec.text.position` (immediate base-position effect).  Per-glyph
// animation goes through `.animate(animator("...").position({...}))`.
//
// `Text::rotate(...)` / `.scale(...)` are likewise base transforms —
// they mutate `spec.text.position`'s companion fields when present,
// otherwise route via `configure_core()`.  Per-glyph rotation/scale
// always go through `Animator`.
//
// ── Registry hookup (PR 5) ──────────────────────────────────────────────
//
// `style(std::string_view id, const StyleRegistry&)` resolves to a
// `TextStyle` and field-maps it onto `spec.text`.  Fields that exist in
// `TextStyle` but not in `TextSpec` (e.g. `TextBox`, `TextStyle::min_size`
// field-name clash, `auto_scale`) deliberately route through `configure_core()`
// so the surface stays honest.  See `text.hpp` field-by-field map below.
//
// `motion(std::string_view id, const MotionRegistry&)` resolves to a
// `TextAnimatorSpec` and appends it to `spec.animators` — lossless path,
// all TextAnimatorSpec fields are preserved.
//
// ── C++20 ref-qualifier note ────────────────────────────────────────────
//
// Single `&` ref-qualifier per setter, identical user-facing syntax to
// `Animator` / `Material` / `Selector`.  See `selector.hpp` for the
// rationale on not duplicating `&`/`&&` pairs.
//
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>      // TextAnchor / TextAlign / VerticalAlign / TextCenteringMode / TextOverflow / TextWrap / TextShadow / TextPaint / TextBoxStyle / TextStyle
#include <chronon3d/scene/builders/text_run_builder.hpp>  // PendingTextRun, TextRunParams
#include <chronon3d/text/text_animator_property.hpp>      // TextAnimatorSpec
#include <chronon3d/text/text_direction.hpp>              // TextDirection

#include <chronon3d/authoring/animator.hpp>
#include <chronon3d/authoring/material.hpp>
#include <chronon3d/authoring/style_registry.hpp>
#include <chronon3d/authoring/motion_registry.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace chronon3d::authoring {

class Layer;  // forward — Layer::text(...) returns Text

// ── FrameContext — viewport information carried by Layer ───────────────
//
// Kept lean on purpose: only the dimensions needed by `.center()`.  Other
// per-frame context (current_frame, fps, etc.) is the renderer's domain
// and should NOT enter the authoring façade — a `.center()` call in a
// non-rendering context still needs an answer, so we use the recorded
// viewport at scene-authoring time.
struct FrameContext {
    f32 width  {1920.0f};
    f32 height {1080.0f};

    static FrameContext default_viewport() noexcept {
        return FrameContext{1920.0f, 1080.0f};
    }
    static FrameContext from_dimensions(f32 w, f32 h) noexcept {
        return FrameContext{w, h};
    }
};

// ── Text ─────────────────────────────────────────────────────────────────
class Text {
public:
    Text(PendingTextRun& pending, const FrameContext* ctx) noexcept
        : pending_(&pending), context_(ctx) {}

    Text(const Text&)            = delete;
    Text& operator=(const Text&) = delete;
    Text(Text&&)                 = default;  // pointer-only move
    Text& operator=(Text&&)      = default;

    // ── Identity ────────────────────────────────────────────────────────
    Text& id(std::string value) & {
        pending_->name = std::move(value);
        return *this;
    }

    /// Replace the text *literal*.  Does NOT change `pending_->params.text.content.pre_shaped`;
    /// pair with `pre_shaped(...)` if you need pre-shaped glyphs.
    Text& content(std::string value) & {
        pending_->params.text.content.value = std::move(value);
        return *this;
    }
    Text& pre_shaped(std::shared_ptr<PlacedGlyphRun> shaped) & {
        pending_->params.text.content.pre_shaped = std::move(shaped);
        return *this;
    }

    // ── Font (mutate spec.text.font directly) ────────────────────────────
    Text& font(std::string font_path, f32 size) & {
        pending_->params.text.font.font_path = std::move(font_path);
        pending_->params.text.font.font_size = size;
        return *this;
    }
    Text& font_family(std::string family) & {
        pending_->params.text.font.font_family = std::move(family);
        return *this;
    }
    Text& weight(int weight) & {
        pending_->params.text.font.font_weight = weight;
        return *this;
    }
    /// `true` → font_style = "italic"; `false` → "normal".
    Text& italic(bool value = true) & {
        pending_->params.text.font.font_style = value ? "italic" : "normal";
        return *this;
    }
    Text& font_size(f32 size) & {
        pending_->params.text.font.font_size = size;
        return *this;
    }

    // ── Position (mutate spec.text.position directly; NOT per-glyph) ─────
    Text& at(Vec3 pos) & {
        pending_->params.text.position = pos;
        return *this;
    }
    Text& at(Vec2 pos) & {
        pending_->params.text.position = {pos.x, pos.y, 0.0f};
        return *this;
    }
    /// f32 x, f32 y convenience — lifts to Vec3{ x, y, 0 }.
    Text& at(f32 x, f32 y) & {
        pending_->params.text.position = {x, y, 0.0f};
        return *this;
    }

    /// Place at the viewport center. Implicitly sets the layout's anchor +
    /// alignment + vertical-alignment so the visible glyphs are centred
    /// even when the box is wider than the ink.
    Text& center() & {
        const f32 w = context_ ? context_->width  : 1920.0f;
        const f32 h = context_ ? context_->height : 1080.0f;
        pending_->params.text.position = {w * 0.5f, h * 0.5f, 0.0f};
        auto& layout = pending_->params.text.layout;
        layout.anchor         = TextAnchor::Center;
        layout.align          = TextAlign::Center;
        layout.vertical_align = VerticalAlign::Middle;
        return *this;
    }

    // ── Layout (mutate spec.text.layout directly) ───────────────────────
    Text& box(Vec2 size) & {
        pending_->params.text.layout.box = size;
        return *this;
    }
    Text& anchor_point(TextAnchor value) & {
        pending_->params.text.layout.anchor = value;
        return *this;
    }
    Text& align(TextAlign value) & {
        pending_->params.text.layout.align = value;
        return *this;
    }
    Text& vertical_align(VerticalAlign value) & {
        pending_->params.text.layout.vertical_align = value;
        return *this;
    }
    Text& pixel_ink_centering() & {
        pending_->params.text.layout.centering_mode = TextCenteringMode::PixelInk;
        return *this;
    }
    Text& layout_box_centering() & {
        pending_->params.text.layout.centering_mode = TextCenteringMode::LayoutBox;
        return *this;
    }
    Text& line_height(f32 value) & {
        pending_->params.text.layout.line_height = value;
        return *this;
    }
    Text& tracking(f32 pixels) & {
        pending_->params.text.layout.tracking = pixels;
        return *this;
    }
    Text& wrap(TextWrap value) & {
        pending_->params.text.layout.wrap = value;
        return *this;
    }
    Text& overflow(TextOverflow value) & {
        pending_->params.text.layout.overflow = value;
        return *this;
    }
    Text& ellipsis(bool value = true) & {
        pending_->params.text.layout.ellipsis = value;
        return *this;
    }
    Text& max_lines(int n) & {
        pending_->params.text.layout.max_lines = n;
        return *this;
    }
    /// Shrink-to-fit.  Both arguments required so callers cannot confuse
    /// it with `.max_font_size(...)`.
    Text& auto_fit(f32 minimum_size, int maximum_lines) & {
        auto& layout = pending_->params.text.layout;
        layout.auto_fit       = true;
        layout.min_font_size  = minimum_size;
        layout.max_lines      = maximum_lines;
        return *this;
    }
    Text& max_font_size(f32 v) & {
        pending_->params.text.layout.max_font_size = v;
        return *this;
    }

    // ── Appearance (mutate spec.text.appearance directly) ────────────────
    Text& color(Color c) & {
        pending_->params.text.appearance.color = c;
        return *this;
    }
    Text& paint(TextPaint value) & {
        pending_->params.text.appearance.paint = std::move(value);
        return *this;
    }
    Text& shadows(std::vector<TextShadow> values) & {
        pending_->params.text.appearance.shadows = std::move(values);
        return *this;
    }
    Text& box_style(TextBoxStyle value) & {
        pending_->params.text.appearance.box_style = std::move(value);
        return *this;
    }

    // ── Script / language / direction (mutate top-level TextRunSpec) ─────
    Text& direction(TextDirection d) & {
        pending_->params.direction = d;
        return *this;
    }
    Text& language(std::string bcp47) & {
        pending_->params.language = std::move(bcp47);
        return *this;
    }

    // ── Material (PR 2 hookup): consumes Material::release() ─────────────
    Text& material(Material material_in) & {
        pending_->params.text.appearance.material = std::move(material_in).release();
        return *this;
    }

    // ── Animator (PR 1 hookup): consumes Animator::release() ─────────────
    Text& animate(Animator animator_in) & {
        pending_->params.animators.emplace_back(std::move(animator_in).release());
        return *this;
    }

    // ── Style registry (PR 5 hookup): lose-free field-mapping ────────────
    //
    // Maps a `TextStyle` from the registry onto `spec.text`:
    //   font_path/family/weight/style/size  → spec.text.font.*
    //   color                               → spec.text.appearance.color
    //   anchor/align/vertical_align         → spec.text.layout.*
    //   centering_mode                      → spec.text.layout.centering_mode
    //   line_height/tracking/max_lines      → spec.text.layout.*
    //   ellipsis/overflow/wrap              → spec.text.layout.*
    //   auto_fit/auto_scale/min_size/max_size → spec.text.layout.auto_fit + size bounds
    //   paint/shadows/box_style             → spec.text.appearance.*
    //   material                            → spec.text.appearance.material
    //
    // Fields exclusive to `TextStyle` (`TextBox box`, `shaping.script`,
    // explicit `pre_shaped` glyphs) require a follow-up `.configure_core(...)`
    // call.  We do NOT silently drop them — registry lookups that don't
    // resolve return without mutating the spec.
    Text& style(std::string_view id, const StyleRegistry& registry) & {
        const auto resolved = registry.resolve(id);
        if (!resolved.has_value()) return *this;
        const TextStyle& s = *resolved;
        auto& spec = pending_->params.text;
        spec.font.font_path   = s.font_path;
        spec.font.font_family = s.font_family;
        spec.font.font_weight = s.font_weight;
        spec.font.font_style  = s.font_style;
        spec.font.font_size   = s.size;
        spec.appearance.color = s.color;
        spec.layout.anchor         = s.anchor;
        spec.layout.align          = s.align;
        spec.layout.vertical_align = s.vertical_align;
        spec.layout.centering_mode = s.centering_mode;
        spec.layout.line_height    = s.line_height;
        spec.layout.tracking       = s.tracking;
        spec.layout.max_lines      = s.max_lines;
        spec.layout.ellipsis       = s.ellipsis;
        spec.layout.auto_fit       = s.auto_fit || s.auto_scale;
        spec.layout.min_font_size  = s.min_size;
        spec.layout.max_font_size  = s.max_size;
        spec.layout.overflow       = s.overflow;
        spec.layout.wrap           = s.wrap;
        spec.appearance.paint      = s.paint;
        spec.appearance.shadows    = s.shadows;
        spec.appearance.box_style  = s.box_style;
        spec.appearance.material   = s.material;
        pending_->params.direction = s.shaping.direction;
        pending_->params.language  = s.shaping.language;
        if (s.pre_shaped) {
            spec.content.pre_shaped = s.pre_shaped;
        }
        return *this;
    }

    // ── Motion registry (PR 5 hookup): append resolved animator ─────────
    Text& motion(std::string_view id, const MotionRegistry& registry) & {
        const auto resolved = registry.resolve(id);
        if (!resolved.has_value()) return *this;
        pending_->params.animators.push_back(*resolved);
        return *this;
    }

    // ── Level 3 escape hatch ─────────────────────────────────────────────
    /// Pass a lambda that mutates the underlying `TextRunSpec`.  Use for
    /// fields the fluent surface doesn't expose yet (rare — the surface
    /// above is intentionally comprehensive).  Inlined by the compiler,
    /// no `std::function` overhead.
    ///
    /// Single `&` ref-qualifier matches the convention used by Animator /
    /// Material / Layer in PR 1+2+3 — the codebase deliberately does NOT
    /// duplicate `&` / `&&` overload pairs.
    ///
    /// Example:
    /// ```cpp
    /// layer.text("T").configure_core([](TextRunSpec& spec){
    ///     spec.text.font.font_path = "fonts/Anton.ttf";
    /// });
    /// ```
    template <class Fn>
    Text& configure_core(Fn&& mutator) & {
        mutator(pending_->params);
        return *this;
    }

    // ── Read-only accessors (for tests and tooling) ──────────────────────
    [[nodiscard]] const PendingTextRun& pending() const noexcept { return *pending_; }
    [[nodiscard]] PendingTextRun&       mutable_pending() noexcept { return *pending_; }

private:
    friend class Layer;                                               // PR 3 — Layer::text() needs to expose `text(content)` factory returning a Text handle.

    PendingTextRun* pending_;
    const FrameContext* context_;
};

} // namespace chronon3d::authoring
