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
// ── Registry hookup (PR 5 + PR 3.5) ─────────────────────────────────
//
// Two resolution paths:
//   1. Explicit payload: `.style(id, registry)` / `.motion(id, registry)` —
//      caller supplies the registry per call. Was PR 3's shipped surface.
//   2. Ambient payload:   `.style(id)` / `.motion(id)` — resolves against
//      registries pinned from the parent `LayerBuilder`'s
//      `ExtensionContext` (when attached). When the ambient pointer is
//      null, these methods gracefully no-op.
//
// The implementation reuses a single private helper for the field-maps
// (PR 5 logic is the same regardless of where the TextStyle comes from).
// `style(std::string_view id, const StyleRegistry& registry)` resolves to a
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
// NO ref-qualifier on Text setters. `&` (lvalue-only) would force
// callers to bind the temporary returned by `Layer::text(content)` to a
// named lvalue before chaining; `&&` (rvalue-only) would block legitimate
// long-lived Text handles.  Dropping the qualifier means a single
// implementation serves BOTH `l.text("Ae").id(...).font(...)` (where
// `.text()` returns a temporary Text) and `auto t = l.text("Ae"); t.id(...);`
// (where `t` is a stable lvalue) — no `&`/`&&` pair duplication, matching
// the rationale documented in `selector.hpp` ("we do NOT duplicate
// `&`/`&&` overload pairs because they always do the same thing").
//
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>      // TextAnchor / TextAlign / VerticalAlign / TextCenteringMode / TextOverflow / TextWrap / TextShadow / TextPaint / TextBoxStyle / TextStyle
#include <chronon3d/scene/builders/text_run_builder.hpp>  // PendingTextRun, TextRunSpec
#include <chronon3d/text/font_engine.hpp>                 // FontEngine — required for `Text::font_engine(FontEngine&)` reference parameter (also pulls in transitively via text_run_builder.hpp, but explicit for hygiene)
#include <chronon3d/text/text_animator_property.hpp>      // TextAnimatorSpec
#include <chronon3d/text/text_direction.hpp>              // TextDirection
#include <chronon3d/text/text_placement_resolver.hpp>     // TextPlacement, CanvasInfo, resolve_text_placement

#include <chronon3d/authoring/animator.hpp>
#include <chronon3d/authoring/material.hpp>
#include <chronon3d/authoring/style_registry.hpp>
#include <chronon3d/authoring/motion_registry.hpp>
#include <chronon3d/authoring/resolution_outcome.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace chronon3d::authoring {

class Layer;  // forward — Layer::text(...) returns Text

// Forward-declare TextRunBuilderInspector for TUs that include this
// header before the test-only inspection header.  The actual definition
// lives in tests/support/text_run_builder_inspection.hpp under
// chronon3d::authoring::testing.
namespace testing { class TextRunBuilderInspector; }

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

    // ── A2 — deprecated: silently assumes 1920×1080, a footgun for
    // non-16:9 compositions.  Prefer `FrameContext::default_viewport()`
    // only in test/legacy code where 16:9 is the explicit intent.
    [[deprecated("Use FrameContext::from_dimensions(w, h) with explicit viewport dimensions")]]
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
    /// Primary ctor. `style_registry` and `motion_registry` are pinned
    /// references used by the ambient-resolution `.style(id)` /
    /// `.motion(id)` variations (PR 3.5). Default-nullptr preserves
    /// backward compatibility with callers that don't need ambient
    /// resolution.
    Text(PendingTextRun& pending, const FrameContext* ctx,
         const StyleRegistry*  style_registry  = nullptr,
         const MotionRegistry* motion_registry = nullptr) noexcept
        : pending_(&pending), context_(ctx),
          style_registry_(style_registry),
          motion_registry_(motion_registry) {}

    Text(const Text&)            = delete;
    Text& operator=(const Text&) = delete;
    Text(Text&&)                 = default;  // pointer-only move
    Text& operator=(Text&&)      = default;

    // ── Identity ────────────────────────────────────────────────────────
    Text& id(std::string value) {
        pending_->name = std::move(value);
        return *this;
    }

    /// Replace the text *literal*.  Does NOT change `pending_->params.text.content.pre_shaped`;
    /// pair with `pre_shaped(...)` if you need pre-shaped glyphs.
    Text& content(std::string value) {
        pending_->params.text.content.value = std::move(value);
        return *this;
    }
    Text& pre_shaped(std::shared_ptr<PlacedGlyphRun> shaped) {
        pending_->params.text.content.pre_shaped = std::move(shaped);
        return *this;
    }

    // ── Font (mutate spec.text.font directly) ────────────────────────────
    Text& font(std::string font_path, f32 size) {
        pending_->params.text.font.font_path = std::move(font_path);
        pending_->params.text.font.font_size = size;
        return *this;
    }
    Text& font_family(std::string family) {
        pending_->params.text.font.font_family = std::move(family);
        return *this;
    }
    Text& weight(int weight) {
        pending_->params.text.font.font_weight = weight;
        return *this;
    }
    /// `true` → font_style = "italic"; `false` → "normal".
    Text& italic(bool value = true) {
        pending_->params.text.font.font_style = value ? "italic" : "normal";
        return *this;
    }
    Text& font_size(f32 size) {
        pending_->params.text.font.font_size = size;
        return *this;
    }

    // ── WP-8 PR 8.2 — per-Text FontEngine isolation contract ─────────
    // Mutates `pending_->font_engine` (= `PendingTextRun::font_engine`,
    // ultimately propagated to the corresponding `RenderNode::font_engine`
    // at materialization; the runtime `software_text_processor::draw()`
    // conditional `node.font_engine ? node.font_engine : &renderer.font_engine()`
    // honours the override).  The pointer is non-owning; lifetime
    // must outlive the layer's materialised RenderNodes.  Setting
    // nullptr reverts to the parent layer's `LayerBuilder::font_engine`
    // default at materialization time (the per-spec-per-layer resolution
    // order documented in `src/scene/builders/text_run_builder.cpp` —
    // "1. TextRunSpec.font_engine (per-spec override) ... 2.
    // LayerBuilder.m_font_engine (per-layer default)").
    //
    // Pointer-only surface — callers passing a `FontEngine&` can write
    // `t.font_engine(&engine)` at the call site. Matches the single-
    // pointer-overload precedent of the underlying `LayerBuilder::
    // font_engine(FontEngine*)` setter that this delegates to.
    Text& font_engine(FontEngine* engine) {
        pending_->font_engine = engine;
        return *this;
    }

    // ── Position (mutate spec.text.placement directly; NOT per-glyph) ─────
    Text& at(Vec3 pos) {
        pending_->params.text.placement = TextPlacement{
            TextPlacementKind::Absolute, {pos.x, pos.y}};
        return *this;
    }
    Text& at(Vec2 pos) {
        pending_->params.text.placement = TextPlacement{
            TextPlacementKind::Absolute, {pos.x, pos.y}};
        return *this;
    }
    /// f32 x, f32 y convenience — lifts to Absolute placement.
    Text& at(f32 x, f32 y) {
        pending_->params.text.placement = TextPlacement{
            TextPlacementKind::Absolute, {x, y}};
        return *this;
    }

    Text& center() {
        // A2 — context_ is always set by the Layer ctor (fail-fast if
        // screen_dimensions were never called).  The previous 1920×1080
        // fallback was a silent footgun for non-16:9 compositions.
        assert(context_ && "Text::center(): FrameContext must be set (Layer ctor guarantees this)");
        const f32 w = context_->width;
        const f32 h = context_->height;
        pending_->params.text.placement = TextPlacement{
            TextPlacementKind::Absolute, {w * 0.5f, h * 0.5f}};
        auto& layout = pending_->params.text.layout;
        layout.anchor         = TextAnchor::Center;
        layout.align          = TextAlign::Center;
        layout.vertical_align = VerticalAlign::Middle;
        return *this;
    }

    // ── F2.B — Semantic placement via TextPlacement struct (Phase A.2) ─
    //
    /// Place the text box using high-level placement semantics.
    /// This is the ergonomic replacement for manual `.at(x,y)` + `.center()`
    /// combinations.
    ///
    /// The `TextPlacement` struct bundles the placement kind (which anchor
    /// point on the canvas the box should pin to) with the user-specified
    /// offset (additive from the resolved pin point).  After Phase A.2 the
    /// `TextPlacement` enum no longer exists as a separate loose parameter —
    /// this struct is the SINGLE source of truth (replaces the previously
    /// ambiguous `TextSpec.position` field).
    ///
    /// The anchor defaults to `TextAnchor::Center`, so the box is centered
    /// on the placement position.  Position is set to the pin point (where
    /// the anchor sits), matching the `.center()` semantics.
    ///
    /// Examples:
    ///   .place(TextPlacement{TextPlacementKind::CanvasCenter})
    ///                                          — box center = canvas center
    ///   .place(TextPlacement{TextPlacementKind::TopLeft})
    ///                                          — box center = safe area top-left
    ///   .place(TextPlacement{TextPlacementKind::SafeAreaBottom})
    ///                                          — box center = safe area bottom
    ///   .place(TextPlacement{TextPlacementKind::Absolute, {500, 300}})
    ///                                          — box center = (500, 300)
    ///   .place(TextPlacement{TextPlacementKind::CanvasCenter, {0, -100}})
    ///                                          — box center = canvas center + (0, -100)
    ///
    /// Safe margins are derived from the canvas context (5% of each dimension).
    Text& place(TextPlacement placement) {
        return place(std::move(placement), TextAnchor::Center);
    }

    /// Place the text box using high-level placement semantics with a
    /// specific anchor.  The anchor determines which point of the box
    /// aligns with the placement position.
    ///
    /// Position is set to the pin point (resolve_placement_origin), NOT
    /// the box top-left.  This matches the rendering pipeline's contract:
    /// `node.world_transform.position = spec.params.text.position` with
    /// `node.world_transform.anchor = resolve_text_anchor(anchor, box)`.
    Text& place(TextPlacement placement, TextAnchor anchor) {
        const CanvasInfo canvas = make_canvas_info_();
        const Vec2 box_size = pending_->params.text.layout.box;
        const Vec2 pin_point = resolve_placement_origin(
            canvas, box_size, placement);
        pending_->params.text.placement = TextPlacement{
            TextPlacementKind::Absolute, {pin_point.x, pin_point.y}};
        pending_->params.text.layout.anchor = anchor;
        return *this;
    }

    // ── Layout (mutate spec.text.layout directly) ───────────────────────
    Text& box(Vec2 size) {
        pending_->params.text.layout.box = size;
        return *this;
    }
    Text& anchor_point(TextAnchor value) {
        pending_->params.text.layout.anchor = value;
        return *this;
    }
    Text& align(TextAlign value) {
        pending_->params.text.layout.align = value;
        return *this;
    }
    Text& vertical_align(VerticalAlign value) {
        pending_->params.text.layout.vertical_align = value;
        return *this;
    }
    Text& pixel_ink_centering() {
        pending_->params.text.layout.centering_mode = TextCenteringMode::PixelInk;
        return *this;
    }
    Text& layout_box_centering() {
        pending_->params.text.layout.centering_mode = TextCenteringMode::LayoutBox;
        return *this;
    }
    Text& line_height(f32 value) {
        pending_->params.text.layout.line_height = value;
        return *this;
    }
    Text& tracking(f32 pixels) {
        pending_->params.text.layout.tracking = pixels;
        return *this;
    }
    Text& wrap(TextWrap value) {
        pending_->params.text.layout.wrap = value;
        return *this;
    }
    Text& overflow(TextOverflow value) {
        pending_->params.text.layout.overflow = value;
        return *this;
    }
    Text& ellipsis(bool value = true) {
        pending_->params.text.layout.ellipsis = value;
        return *this;
    }
    Text& max_lines(int n) {
        pending_->params.text.layout.max_lines = n;
        return *this;
    }
    /// Shrink-to-fit.  Both arguments required so callers cannot confuse
    /// it with `.max_font_size(...)`.
    Text& auto_fit(f32 minimum_size, int maximum_lines) {
        auto& layout = pending_->params.text.layout;
        layout.auto_fit       = true;
        layout.min_font_size  = minimum_size;
        layout.max_lines      = maximum_lines;
        return *this;
    }
    Text& max_font_size(f32 v) {
        pending_->params.text.layout.max_font_size = v;
        return *this;
    }

    // ── Appearance (mutate spec.text.appearance directly) ────────────────
    Text& color(Color c) {
        pending_->params.text.appearance.color = c;
        return *this;
    }
    Text& paint(TextPaint value) {
        pending_->params.text.appearance.paint = std::move(value);
        return *this;
    }
    Text& shadows(std::vector<TextShadow> values) {
        pending_->params.text.appearance.shadows = std::move(values);
        return *this;
    }
    Text& box_style(TextBoxStyle value) {
        pending_->params.text.appearance.box_style = std::move(value);
        return *this;
    }

    // ── Script / language / direction (mutate top-level TextRunSpec) ─────
    Text& direction(TextDirection d) {
        pending_->params.direction = d;
        return *this;
    }
    Text& language(std::string bcp47) {
        pending_->params.language = std::move(bcp47);
        return *this;
    }
    /// Explicit 4-byte OpenType script tag (HB_SCRIPT_*).  Reaches
    /// `pending_->params.script` — the field added in PR 4 alongside
    /// `direction` + `language`.  Examples: 0x4C61746E (Latin),
    /// 0x41726162 (Arabic), 0x48616E20 (Han), 0x44657661 (Devanagari).
    ///
    /// Default value `0` preserves the historical Latin-only path:
    /// HarfBuzz leaves the script as HB_SCRIPT_INVALID and lets
    /// `hb_buffer_guess_segment_properties()` auto-detect from the
    /// text content.  Pass a non-zero tag to opt-in to a specific
    /// script for shaping.
    ///
    /// Until PR 4, `Text::style(id, registry)` silently DROPPED the
    /// `shaping.script` field — see `apply_text_style` docstring below
    /// for the gap narrative and the new mapping behaviour.
    Text& script(std::uint32_t value) & {
        pending_->params.script = value;
        return *this;
    }

    // ── Material (PR 2 hookup): consumes Material::release() ─────────────
    Text& material(Material material_in) {
        pending_->params.text.appearance.material = std::move(material_in).release();
        return *this;
    }

    // ── Animator (PR 1 hookup): consumes Animator::release() ─────────────
    Text& animate(Animator animator_in) {
        pending_->params.animators.emplace_back(std::move(animator_in).release());
        return *this;
    }

    // ── Style registry (PR 5 + PR 3.5): field-map TextStyle → spec.text ──
    //
    // Both call paths funnel through the same private helper
    // `apply_text_style(const TextStyle&)` which performs the field-map.
    //
    // A3 — structured outcomes: each call updates `last_style_outcome_`
    // so callers can distinguish Found / Missing / RegistryUnavailable
    // instead of the previous silent no-op contract.
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

    // Explicit-path (PR 3 surface): caller supplies the registry.
    Text& style(std::string_view id, const StyleRegistry& registry) {
        const auto resolved = registry.resolve(id);
        if (!resolved.has_value()) {
            last_style_outcome_ = ResolutionOutcome::Missing;
            return *this;
        }
        apply_text_style(*resolved);
        last_style_outcome_ = ResolutionOutcome::Found;
        return *this;
    }

    // Ambient-path (PR 3.5): resolves against the registry pinned at handle
    // construction (LayerBuilder → ExtensionContext → StyleRegistry*).
    // A3 — when the ambient registry pointer is null or the id is missing,
    // records the structured outcome instead of silently no-opping.
    Text& style(std::string_view id) {
        if (style_registry_ == nullptr) {
            last_style_outcome_ = ResolutionOutcome::RegistryUnavailable;
            return *this;
        }
        const auto resolved = style_registry_->resolve(id);
        if (!resolved.has_value()) {
            last_style_outcome_ = ResolutionOutcome::Missing;
            return *this;
        }
        apply_text_style(*resolved);
        last_style_outcome_ = ResolutionOutcome::Found;
        return *this;
    }

    // ── Motion registry (PR 5 + PR 3.5) ────────────────────────────────

    // Explicit-path.
    Text& motion(std::string_view id, const MotionRegistry& registry) {
        const auto resolved = registry.resolve(id);
        if (!resolved.has_value()) {
            last_motion_outcome_ = ResolutionOutcome::Missing;
            return *this;
        }
        pending_->params.animators.push_back(*resolved);
        last_motion_outcome_ = ResolutionOutcome::Found;
        return *this;
    }

    // Ambient-path.
    Text& motion(std::string_view id) {
        if (motion_registry_ == nullptr) {
            last_motion_outcome_ = ResolutionOutcome::RegistryUnavailable;
            return *this;
        }
        const auto resolved = motion_registry_->resolve(id);
        if (!resolved.has_value()) {
            last_motion_outcome_ = ResolutionOutcome::Missing;
            return *this;
        }
        pending_->params.animators.push_back(*resolved);
        last_motion_outcome_ = ResolutionOutcome::Found;
        return *this;
    }

    // ── Level 3 escape hatch ─────────────────────────────────────────────
    /// Pass a lambda that mutates the underlying `TextRunSpec`.  Use for
    /// fields the fluent surface doesn't expose yet (rare — the surface
    /// above is intentionally comprehensive).  Inlined by the compiler,
    /// no `std::function` overhead.
    ///
    /// NO ref-qualifier — matches the rationale documented at the top of
    /// this file (single implementation serves both rvalue chain-temporary
    /// and lvalue named-handle call patterns; we deliberately do NOT
    /// duplicate `&` / `&&` overload pairs).
    ///
    /// Example:
    /// ```cpp
    /// layer.text("T").configure_core([](TextRunSpec& spec){
    ///     spec.text.font.font_path = "fonts/Anton.ttf";
    /// });
    /// ```
    template <class Fn>
    Text& configure_core(Fn&& mutator) {
        mutator(pending_->params);
        return *this;
    }

    // ── A3 — Structured resolution outcomes ──────────────────────────
    /// Returns the outcome of the most recent `.style()` call on this handle.
    /// `NotAttempted` means no `.style()` call has been made yet.
    [[nodiscard]] ResolutionOutcome last_style_outcome()  const noexcept { return last_style_outcome_; }

    /// Returns the outcome of the most recent `.motion()` call on this handle.
    /// `NotAttempted` means no `.motion()` call has been made yet.
    [[nodiscard]] ResolutionOutcome last_motion_outcome() const noexcept { return last_motion_outcome_; }

    // ── Existing accessors ──────────────────────────────────────────
    [[nodiscard]] const PendingTextRun& pending() const noexcept { return *pending_; }
    // TICKET-110 — `mutable_pending()` was demoted from public to private.
    // The accessor survives (Layer::text() construction path still needs a
    // non-const handle into the underlying PendingTextRun), but is now
    // reachable only via the explicit friend grants in the `private:`
    // section below (`Layer` for the construction path, the
    // test-only `testing::TextRunBuilderInspector` for read access from
    // tests).  Production code MUST go through the public `Text` setters
    // or the `configure_core(...)` escape hatch; raw read access is
    // intentionally locked down.
    // PR 3.5 — exposed for tests that exercise the ambient-resolution path.
    [[nodiscard]] const StyleRegistry*  ambient_style_registry()  const noexcept { return style_registry_; }
    [[nodiscard]] const MotionRegistry* ambient_motion_registry() const noexcept { return motion_registry_; }

private:
    friend class Layer;                                               // PR 3
    friend class testing::TextRunBuilderInspector;                    // TICKET-110 — test-only


    // TICKET-110 — demoted from public; reachable only via the friend grants above.
    [[nodiscard]] PendingTextRun&       mutable_pending() noexcept { return *pending_; }

    // Single field-map helper shared by explicit + ambient `.style(...)`
    // paths.  Owns the PR 3-equivalence contract.
    void apply_text_style(const chronon3d::TextStyle& s) {
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
        // ── PR 4 — close the `shaping.script` gap ───────────────────────
        // Pre-PR-4 this field was silently dropped during `.style(id)`
        // resolution — the HarfBuzz script tag set inside a `TextStyle`
        // was lost.  Now we forward `s.shaping.script` to the spec only
        // when non-zero, so the historical "default = 0 → auto-detect"
        // behaviour is preserved (hb_buffer_guess_segment_properties
        // still runs for un-tagged content).  Set `.script(0x...)`
        // explicitly when you want to override the auto-detect path.
        if (s.shaping.script != 0) {
            pending_->params.script = s.shaping.script;
        }
        if (s.pre_shaped) {
            spec.content.pre_shaped = s.pre_shaped;
        }
    }

    // ── Private helpers ────────────────────────────────────────────────

    CanvasInfo make_canvas_info_() const noexcept {
        CanvasInfo canvas;
        if (context_) {
            canvas.width  = context_->width;
            canvas.height = context_->height;
            canvas.safe_margin_top    = context_->height * 0.05f;
            canvas.safe_margin_bottom = context_->height * 0.05f;
            canvas.safe_margin_left   = context_->width  * 0.05f;
            canvas.safe_margin_right  = context_->width  * 0.05f;
        }
        return canvas;
    }

    PendingTextRun* pending_;
    const FrameContext* context_;
    // PR 3.5 — ambient registry pointers, pinned at handle construction
    // by Layer::text(...). Read-only for the handle's lifetime; mutable
    // via the public accessor only for test introspection.
    const StyleRegistry*  style_registry_;
    const MotionRegistry* motion_registry_;
    // A3 — structured resolution outcomes, updated by .style() / .motion().
    ResolutionOutcome last_style_outcome_{ResolutionOutcome::NotAttempted};
    ResolutionOutcome last_motion_outcome_{ResolutionOutcome::NotAttempted};
};

} // namespace chronon3d::authoring
