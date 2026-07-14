#pragma once

#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/text_run_builder.hpp>  // PR 4 — full type required for `std::unique_ptr<TextRunBuilder>` / `std::unique_ptr<TextRunSpec>` members (was forward-declared; caused `sizeof incomplete` + cascaded `private constructor` errors at `std::make_unique` sites and at every TU that destroys a LayerBuilder).
#include <chronon3d/scene/builders/node_handle.hpp>        // A4 — explicit node transform handle
#include <chronon3d/registry/shape_registry.hpp>
#include <chronon3d/vector/path_factories.hpp>
#include <chronon3d/scene/model/layer/mask.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/scene/model/shape/material_2_5d.hpp>
#include <chronon3d/scene/model/core/card3d_material.hpp>
#include <chronon3d/layout/layout_rules.hpp>
#include <chronon3d/backends/video/video_source.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/animation/core/animated_value.hpp>
#include <chronon3d/animation/effects/animated_transform.hpp>
#include <string>
#include <string_view>
#include <memory_resource>
#include <optional>
// NOTE: Phase-3.3 mechanical split avoided re-adding <utility>, <vector>, <memory> — text_run_builder.hpp transitively brings them in. See ADR-019 §A.2.

namespace chronon3d {

class FontEngine;  // forward declaration
struct ExtensionContext;  // PR 3.5 — forward-decl (host-side ambient registries).
                         // Full definition lives in <chronon3d/extension/extension_context.hpp>.
                         // Kept as a forward-decl here so layer_builder.hpp stays include-light;
                         // accessor below returns the pointer verbatim.
struct TextDefinition;  // F2.C — forward-decl for text(name, TextDefinition) overload.
                         // Full definition in <chronon3d/text/text_definition.hpp>.

// Forward-declare the test-only inspector for the `friend class LayerBuilderInspector` declaration below. See ADR-019 §A.2.
namespace builders { namespace testing { class LayerBuilderInspector; } }

// `TextRunBuilder` and `TextRunSpec` pulled in fully via `#include <chronon3d/scene/builders/text_run_builder.hpp>` above. See ADR-019 §A.2 for the sizeof-cascade incident.

namespace layer_builder_internal {

[[nodiscard]] RenderNode* last_node(Layer& layer);

void set_last_shadow(Layer& layer, DropShadow shadow);
void set_last_glow(Layer& layer, Glow glow);
void set_last_position(Layer& layer, Vec3 pos);
void set_last_rotation(Layer& layer, Vec3 euler_deg);
void set_last_scale(Layer& layer, Vec3 s);
void set_last_anchor(Layer& layer, Vec3 a);
void set_last_opacity(Layer& layer, f32 opacity);

} // namespace layer_builder_internal

class Layer3DDelegate {
public:
    static void add_fake_box3d(Layer& layer, std::string name, FakeBox3DParams p, FontEngine* font_engine);
    static void add_grid_plane(Layer& layer, std::string name, GridPlaneParams p, FontEngine* font_engine);
};

// ── Phase-3.3 mechanical split ──
// Inline accessor bodies (screen_dimensions / name / resource / extension_context) moved to detail/layer_builder_inline.inl. pending_text_runs() REMOVED — consumers use the test-only inspector (LayerBuilderInspector::pending_runs) via friend declaration below. See ADR-019 §A.2.

class LayerBuilder {
public:
    /// Primary constructor: accepts sub-frame SampleTime for continuous animation evaluation.
    explicit LayerBuilder(std::string name,
                          SampleTime current_time,
                          std::pmr::memory_resource* res = std::pmr::get_default_resource(),
                          registry::ShapeRegistry* shape_registry = nullptr);
    /// Backward-compatible constructor: accepts integer Frame, delegates to SampleTime.
    explicit LayerBuilder(std::string name,
                          Frame current_frame = 0,
                          std::pmr::memory_resource* res = std::pmr::get_default_resource(),
                          registry::ShapeRegistry* shape_registry = nullptr);
    explicit LayerBuilder(std::string name,
                          std::pmr::memory_resource* res,
                          registry::ShapeRegistry* shape_registry = nullptr);

    // ── Timing ──
    LayerBuilder& parent(std::string name);
    LayerBuilder& from(Frame frame);
    LayerBuilder& duration(Frame frames);
    LayerBuilder& until(Frame frame);
    LayerBuilder& offset(Frame frames);

    // ── Timing aliases (FIX 7: clearer naming) ──
    /// Alias for `from(frame)` — clearer when reading build code:
    ///   l.start_at(Frame{30}).length(Frame{60});
    LayerBuilder& start_at(Frame frame) { return from(frame); }
    /// Alias for `duration(frames)` — clearer when reading build code:
    ///   l.start_at(Frame{30}).length(Frame{60});
    LayerBuilder& length(Frame frames) { return duration(frames); }

    // ── Time Remap ──
    LayerBuilder& speed(f32 multiplier);
    LayerBuilder& reverse(bool value = true);
    LayerBuilder& freeze_frame(Frame frame);
    LayerBuilder& time_remap(AnimatedValue<f32> curve);
    AnimatedValue<f32>& time_remap_anim();
    LayerBuilder& visible(bool value);
    LayerBuilder& kind(LayerKind value);
    LayerBuilder& cache_static(bool value = true);

    // ── Transform ──
    LayerBuilder& position(Vec3 p);
    LayerBuilder& scale(Vec3 s);
    LayerBuilder& rotate(Vec3 euler_deg);
    LayerBuilder& anchor(Vec3 a);
    LayerBuilder& opacity(f32 value);
    LayerBuilder& enable_3d(bool value = true);

    AnimatedValue<Vec3>& position_anim();
    AnimatedValue<Vec3>& scale_anim();
    AnimatedValue<Vec3>& rotate_anim();
    AnimatedValue<Vec3>& anchor_anim();
    AnimatedValue<f32>&  opacity_anim();
    AnimatedValue<f32>&  blur_anim();

    // ── Depth ──
    LayerBuilder& depth_role(DepthRole role);
    LayerBuilder& depth_offset(f32 offset);

    // ── Layout ──
// ── Layout ──
// pin_to(Anchor) operates on LAYER coords (NOT canvas). Mixing with TextAnchor:: / .text(...) indicates ADR-019 §3 coordinate-level confusion. Prefer TextPlacement chain on TextDefinition. Gate #25 in tools/check_architecture_boundaries.sh enforces this. See ADR-019 §A.2.
    LayerBuilder& pin_to(Anchor anchor, f32 margin = 0.0f);
    LayerBuilder& pin_to(AnchorPlacement placement, f32 margin = 0.0f);
    LayerBuilder& keep_in_safe_area(SafeArea area = {});
    LayerBuilder& fit_text();

    // ── Convenience ──
    /// Position this layer at the center of the screen.
    /// Reads m_screen_width / m_screen_height (set via screen_dimensions()
    /// or the 1920×1080 default).
    LayerBuilder& center() {
        m_layer.transform.position = {
            m_screen_width * 0.5f,
            m_screen_height * 0.5f,
            0.0f};
        return *this;
    }

    /// Set the default font path for all subsequent animated_text() calls on
    /// this layer.  Overridden by an explicit font_path in TextRunSpec.
    LayerBuilder& font(std::string path) {
        m_default_font_path = std::move(path);
        return *this;
    }

    /// Set the default font size for all subsequent animated_text() calls on
    /// this layer.  Overridden by an explicit font_size in TextRunSpec.
    LayerBuilder& font_size(f32 size) {
        m_default_font_size = size;
        return *this;
    }

    // ── Node Transform (A4: [[deprecated]] — use last_node_handle() instead) ──
    [[deprecated("Use last_node_handle().position(pos) for explicit node access")]]
    LayerBuilder& at(Vec3 pos);
    [[deprecated("Use last_node_handle().rotate(euler_deg) for explicit node access")]]
    LayerBuilder& rotate_node(Vec3 euler_deg);
    [[deprecated("Use last_node_handle().scale(s) for explicit node access")]]
    LayerBuilder& scale_node(Vec3 s);
    [[deprecated("Use last_node_handle().anchor(a) for explicit node access")]]
    LayerBuilder& anchor_node(Vec3 a);
    [[deprecated("Use last_node_handle().opacity(v) for explicit node access")]]
    LayerBuilder& node_opacity(f32 a);

    // ── A4 — Explicit node handle for the last pushed node ────────────
    /// Returns a NodeHandle wrapping the most recently pushed node.
    /// Use this instead of the deprecated .at()/.scale_node()/etc.
    /// Returns a sentinel handle (no-op) when no nodes have been pushed yet.
    [[nodiscard]] NodeHandle last_node_handle();

    // ── Node Polish (A4: [[deprecated]] — use last_node_handle() instead) ──
    [[deprecated("Use last_node_handle().with_shadow(s) for explicit node access")]]
    LayerBuilder& with_shadow(DropShadow shadow);
    [[deprecated("Use last_node_handle().with_glow(g) for explicit node access")]]
    LayerBuilder& with_glow(Glow glow);
    LayerBuilder& accepts_lights(bool value = true);
    LayerBuilder& casts_shadows(bool value = true);
    LayerBuilder& accepts_shadows(bool value = true);
    LayerBuilder& material(Material2_5D value);
    LayerBuilder& card3d_material(Card3DMaterial value);

    // ── Transitions ──
    LayerBuilder& transition_in(LayerTransitionSpec spec);
    LayerBuilder& transition_out(LayerTransitionSpec spec);

    // ── Specialized ──
    // ── Phase-3.3.D inline bodies → detail/layer_builder_inline.inl ────
    LayerBuilder& screen_dimensions(f32 w, f32 h);
    LayerBuilder& fullscreen_rect(std::string name, Color color);
    LayerBuilder& fill(Color color);

    // ── PR 4 — screen_dimensions readback (no full type required) ────
    /// Returns true once `screen_dimensions(w, h)` has been called
    /// explicitly.  See the public header doc-comment block (kept above
    /// the inline `.inl` for grep-compatibility) and the body in
    /// detail/layer_builder_inline.inl.
    [[nodiscard]] bool screen_dimensions_were_set() const noexcept;
    /// Read-back accessor: returns (width, height) recorded by the last
    /// `screen_dimensions(w, h)` call (or the default 1920×1080 if the
    /// builder was constructed without one).
    [[nodiscard]] Vec2 screen_dimensions() const noexcept;
    /// Read-back accessor for the layer's name (used by error messages
    /// in `chronon3d::authoring::Layer::Layer(LayerBuilder&)` when the
    /// builder has no `screen_dimensions(...)` set).
    ///
    /// Returns a non-owning `std::string_view` over `m_layer.name`.
    /// See detail/layer_builder_inline.inl for the body.
    [[nodiscard]] std::string_view name() const noexcept;

    // (REMOVED in Phase-3.3) `pending_text_runs()` previously exposed
    // `std::vector<const PendingTextRun*>` (raw internal pointers).
    // Production callers that need pre-build enum of pending text-run
    // entries must now go through the test-only inspector at
    // <chronon3d/scene/builders/test/layer_builder_inspection.hpp> —
    // LayerBuilderInspector::pending_runs(lb) returns a value-typed
    // snapshot of (name, animators) per entry, friend-mediated.

    // ── FontEngine ──
    LayerBuilder& font_engine(FontEngine* engine);
    [[nodiscard]] FontEngine* font_engine() const;

    // ── PR 3.5 — ExtensionContext attachment (ambient authoring registries) ──
    // Pointer semantics: nullable. When no ExtensionContext is attached,
    // the authoring façade's ambient methods gracefully no-op. The
    // existing explicit-param variants on Text (`.style(id, registry)`,
    // `.motion(id, registry)`) keep working regardless of this state.
    //
    // Lifetime: the host owns the ExtensionContext — LayerBuilder does
    // NOT take ownership. The pointer must outlive this LayerBuilder.
    LayerBuilder&  extension_context(const ExtensionContext& ctx) noexcept;
    [[nodiscard]] const ExtensionContext* extension_context() const noexcept;

    // ── Masks ──
    LayerBuilder& mask_rect(RectMaskParams p);
    LayerBuilder& mask_rounded_rect(RoundedRectMaskParams p);
    LayerBuilder& mask_circle(CircleMaskParams p);

    // ── Track matte ──
    LayerBuilder& track_matte_alpha(std::string source_layer_name);
    LayerBuilder& track_matte_alpha_inverted(std::string source_layer_name);
    LayerBuilder& track_matte_luma(std::string source_layer_name);
    LayerBuilder& track_matte_luma_inverted(std::string source_layer_name);

    // ── Effects ──
    LayerBuilder& blur(f32 radius);
    LayerBuilder& tint(Color color, f32 amount = 1.0f);
    LayerBuilder& brightness(f32 v);
    LayerBuilder& contrast(f32 v);
    LayerBuilder& saturation(f32 v);
    LayerBuilder& hue_rotate(f32 deg);
    LayerBuilder& invert(f32 amount = 1.0f);
    LayerBuilder& vignette(f32 radius = 0.5f, f32 softness = 0.5f, f32 amount = 1.0f);
    LayerBuilder& drop_shadow(Vec2 offset, Color color = {0,0,0,0.35f}, f32 radius = 12.0f);
    LayerBuilder& glow(GlowParams params);
    LayerBuilder& bloom(f32 threshold = 0.80f, f32 radius = 24.0f, f32 intensity = 0.60f);
    LayerBuilder& fake_3d_wave(Fake3DWaveParams params);
    LayerBuilder& blend(BlendMode mode);

    // ── 2D Shapes ──
    LayerBuilder& rect(std::string name, RectParams p);
    LayerBuilder& rounded_rect(std::string name, RoundedRectParams p);
    LayerBuilder& circle(std::string name, CircleParams p);
    LayerBuilder& line(std::string name, LineParams p);
    LayerBuilder& path(std::string name, PathParams p);
    LayerBuilder& arrow(std::string name, ArrowParams p);
    LayerBuilder& star(std::string name, StarParams p);
    LayerBuilder& badge(std::string name, BadgeParams p);
    LayerBuilder& speech_bubble(std::string name, SpeechBubbleParams p);
    LayerBuilder& callout(std::string name, CalloutParams p);
    LayerBuilder& progress_bar(std::string name, ProgressBarParams p);
    LayerBuilder& timeline_bar(std::string name, TimelineBarParams p);
    LayerBuilder& image(std::string name, ImageParams p);
    LayerBuilder& tiled_image(std::string name, ImageParams p);
    LayerBuilder& grid_background(std::string name, GridBackgroundParams p);
    // ── Text V1 Fluent API Note (text V1 plan Phase A.5) ────────────────
    //
    // The PHASE-A5 spec asks for a fluent public builder on `LayerBuilder`:
    //   layer.text("id")
    //        .content("HELLO")
    //        .font(path, size)
    //        .frame({w, h})
    //        .place(TextPlacement::CanvasCenter)
    //        .align(H::Center, V::Middle)
    //        .commit();
    //
    // The thinker's Option B verdict (verbatim: "the fluent chain already
    // exists natively on the higher-level authoring facade via
    // `chronon3d::authoring::Layer::text("id")`") establishes that the
    // fluent surface is ALREADY canonical on the authoring facade.  The
    // AGENTS.md "Non duplicare..." + "No espansione API non necessaria"
    // rules forbid adding a parallel fluent chain here when one already
    // perfectly functions on the authoring layer.
    //
    // Architecture rationale:
    //   - `LayerBuilder` operates on low-level specs and pipelines
    //     (`TextRunBuilder`, `TextRunParams`).  Injecting a high-level
    //     fluent authoring interface (which needs context like
    //     `FrameContext` and registries) directly into the builder
    //     violates the facade separation.
    //   - The old `text(name, TextSpec)` API below is preserved (NOT
    //     removed) per the PHASE-A5 spec: "Does NOT remove old APIs yet
    //     (Phase B)".
    //
    // IMPORTANT: this is the OLD API.  The new fluent surface is on
    // `chronon3d::authoring::Layer::text(id)` — see the class-level
    // docstring at the top of `LayerBuilder` for the entry point.
    // This per-method note is here for callers who land on this method
    // via the OLD spec and need to be redirected.
    //
    // Spec-to-implementation mapping (PHASE-A5):
    //   Spec term                |  Canonical authoring-facade API
    //   -------------------------+---------------------------------------------
    //   layer.text("id")         |  chronon3d::authoring::Layer::text("id")
    //   .content("HELLO")        |  .content("HELLO")
    //   .font(path, size)        |  .font(path, size)
    //   .frame({w, h})           |  .box({w, h})
    //   .place(TextPlacement::…) |  .place(TextPlacement{TextPlacementKind::…})
    //   .align(H::Center, …)     |  TWO-CALL SPLIT (see note below)
    //   H::Center                |  TextAlign::Center  (from shape.hpp)
    //   V::Middle                |  VerticalAlign::Middle  (from shape.hpp)
    //   .commit()                |  NO METHOD — IMMEDIATE application
    //                                (see note below)
    //
    // Note on `.align(H::Center, V::Middle)` 2-call split:
    //   The user spec shows `.align(H::Center, V::Middle)` as a single
    //   call, but the `Text` authoring facade exposes alignment as TWO
    //   separate setters: `.align(TextAlign)` for horizontal + `.vertical_
    //   align(VerticalAlign)` for vertical.  This is a deliberate
    //   2-call split (not a 1-call sugar) because the two axes have
    //   independent semantics (per-character vs per-line).
    //
    // Note on `.commit()`:
    //   The `Text` handle is NOT a destructor-committer (see
    //   `include/chronon3d/authoring/text.hpp` class docstring at top
    //   of `Text`).  Each setter mutates `pending_->params.text`
    //   IMMEDIATELY when called — there is no `.commit()` method and
    //   no deferred-commit on destruction.  This is the inverse of
    //   `TextRunBuilder` (which DOES require `.commit()` to return
    //   control to the layer-level builder).
    //
    // Usage (PHASE-A5 spec, rewritten with canonical enums + 2-call split):
    //   // At top of file:
    //   using namespace chronon3d::authoring;  // optional but recommended
    //
    //   // On a layer (LayerBuilder):
    //   layer.text("hello-id")                  // OLD API — preserved, not removed
    //        .content("HELLO")                  // ← these setters go on the OLD TextSpec
    //        .font("fonts/Inter-Bold.ttf", 64.0f)
    //        .box({1700, 360})
    //        ...
    //
    //   // On a layer (NEW fluent surface, recommended):
    //   layer.text("hello-id")                  // Layer::text returns Text handle
    //        .content("HELLO")                  // immediate (no .commit() needed)
    //        .font("fonts/Inter-Bold.ttf", 64.0f)
    //        .box({1700, 360})
    //        .place(TextPlacement{TextPlacementKind::CanvasCenter})
    //        .align(TextAlign::Center)          // 2-call split: horizontal
    //        .vertical_align(VerticalAlign::Middle);  // 2-call split: vertical
    //
    // Lifecycle:
    //   Phase A.1 (text_definition.hpp)   — TextDefinition DTO (input)
    //   Phase A.2 (text_placement.hpp)    — TextPlacement authoring type
    //   Phase A.3 (text_placement_resolver) — ResolvedTextPlacement + resolver
    //   Phase A.4 (compile_text_layout)   — canonical TextCompiler
    //   Phase A.5 (THIS COMMENT)          — fluent authoring facade (existing)
    //   Phase B (migration)               — Old `text(name, spec)` API
    //                                       deprecation + new API promotion
    //                                       (the new Layer::text() fluent
    //                                       chain is the only authoring
    //                                       path forward after Phase B)
    //   Phase C (builder)                 — Simple builder sugar
    //   Phase D (gate)                    — Anti-legacy gate
    //
    // Anti-duplicazione honour:
    //   - No new fluent chain on LayerBuilder (the authoring facade
    //     already provides it).
    //   - No `H::` / `V::` namespace aliases (the canonical
    //     `TextAlign` / `VerticalAlign` enums are used directly).
    //   - No new `LayerTextHandle` wrapper class.
    //   - No `.commit()` method on Text (the existing immediate-apply
    //     semantic is preserved per text.hpp's non-destructor-committer
    //     contract).
    //
    // Usage example includes (typically transitively included):
    //   #include <chronon3d/scene/model/shape/shape.hpp>  // for TextAlign / VerticalAlign
    //   #include <chronon3d/text/text_placement.hpp>      // for TextPlacement / TextPlacementKind
    //   #include <chronon3d/authoring/text.hpp>          // for the fluent Text handle
    //
    // See also: the class-level docstring at the top of `LayerBuilder`
    // for the canonical entry point to the new fluent surface.
    //
/// F2.C — canonical authoring overload accepting TextDefinition
/// (RECOMMENDED entry point for new compositions; centered_text() / glow_text() / typewriter_text() all return TextDefinition + compose directly).
/// For SIMPLE text (no TextAnimator/GlyphSelector/script/language), prefer this over `animated_text(name, TextRunSpec)`. See ADR-019 §A.2.
    LayerBuilder& text(std::string name, const TextDefinition& def);

    /// Backward-compatible overload accepting the legacy TextSpec.
    /// Converts to TextDefinition via from_text_spec() and forwards to
    /// the canonical TextDefinition overload.
    ///
    /// TICKET-TEXT-SPEC-MIGRATION (2026-07-14, P1 OPEN):
    /// marked `[[deprecated]]` per audit Blocco 5.1 verbatim "Elimina
    /// `TextSpec` dai contenuti".  Callers MUST migrate to
    /// `text(name, const TextDefinition&)` above.  The 124+ call sites
    /// (production + tests) are tracked in this ticket (P1, OPEN);
    /// after the migration lands, this overload is removed wholesale.
    [[deprecated("Use text(name, const TextDefinition&) — see "
                 "TICKET-TEXT-SPEC-MIGRATION (Blocco 5.1). "
                 "This overload is removed after the migration lands.")]]
    LayerBuilder& text(std::string name, const TextSpec& spec);

    // ── TextRunBuilder (PR 4 — TextAnimator V2) ──────────────────────────
    /// Push a new animated text-run entry into the layer's pending specs
    /// and return a `TextRunBuilder&` for fluent chaining.  The text-run
    /// entry is committed to a `RenderNode` (with `is_text_run_shape
    /// = true` and a populated `text_run_shape`) inside
    /// `LayerBuilder::build()`.  Multiple `animated_text(...)` calls per
    /// layer are allowed — each spawns a separate RenderNode plus
    /// downstream TextRunNode at compose time.
    ///
    /// Note: this method does NOT participate in `LayerBuilder`'s
    /// `return *this` chain.  The returned `TextRunBuilder&` is the
    /// next layer in the chain; calling `.commit()` explicitly hands
    /// control back to the layer-level builder.
    ///
    /// For SIMPLE text cases (no TextAnimator, no GlyphSelector, no
    /// script/language override, no per-run cache_layout tweak), prefer
    /// `text(name, const TextDefinition&)` over `animated_text(name, TextRunSpec)`.
    /// The `text()` overload skips the `TextRunBuilder` indirection
    /// (which exists primarily to host animators/selectors) and goes
    /// directly through the canonical authoring DTO.  Reserve
    /// `animated_text()` for the case where you actually need a
    /// `TextAnimatorSpec` / `GlyphSelectorSpec` array.
    [[nodiscard]] TextRunBuilder& animated_text(std::string name, TextRunSpec params);

    /// Deprecated alias for `animated_text(name, TextRunSpec)`.  Migrate to
    /// `animated_text(name, spec)` and keep `.commit()` chain unchanged.
    [[deprecated("Use animated_text(name, TextRunSpec) instead")]]
    TextRunBuilder& text_run(std::string name, TextRunSpec params) {
        return animated_text(std::move(name), std::move(params));
    }

    LayerBuilder& shape(std::string_view id, std::string name, registry::ShapeParams params);

    // ── 3D Shapes (Delegated) ──
    LayerBuilder& fake_box3d(std::string name, FakeBox3DParams p);
    LayerBuilder& grid_plane(std::string name, GridPlaneParams p);

    // ── Motion Presets ──
    LayerBuilder& slide_in(Vec3 from, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& soft_pop(Frame duration = Frame{30});
    LayerBuilder& float_idle(f32 amplitude_y = 12.0f, Frame cycle = Frame{120});
    LayerBuilder& depth_reveal(f32 depth_z = 260.0f, Frame duration = Frame{45});
    LayerBuilder& card_flip_2_5d(Frame duration = Frame{60});
    LayerBuilder& settle(f32 overshoot = 0.08f, Frame duration = Frame{20});
    LayerBuilder& fade_in(Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& focus_in(f32 start_blur, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& scale_drop(f32 start_scale, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& fade_shift_vertical(Vec3 offset, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& fade_shift_horizontal(Vec3 offset, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& reveal_from_bottom(f32 distance, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& center_split(Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& underline_draw(Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& highlight_block(Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& framing_bracket(Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& word_stagger(Frame delay, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& tracking_breathing(f32 scale_factor, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& elegant_exit_vertical(Vec3 offset, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& elegant_exit_horizontal(Vec3 offset, Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});
    LayerBuilder& curtain_close(Frame duration, EasingCurve easing = EasingCurve{Easing::OutCubic});

    // ── Video ──
    LayerBuilder& video(video::VideoSource source);
    LayerBuilder& video(std::string path);
    LayerBuilder& video_size(Vec2 size);

    // ── Precomp ──
    LayerBuilder& precomp(std::string comp_name);

    [[nodiscard]] std::pmr::memory_resource* resource() const;
    [[nodiscard]] Layer build();

private:
    // ── PR 4 — PMR-string accessor policy ────────────────────────────
    // Any accessor that exposes a member stored in pmr memory (e.g.
    // m_layer.name, future m_layer.description / parent_name / tag,
    // etc.) must return std::string_view — not const std::string& and
    // not const std::pmr::string&.  std::string_view is allocator-
    // agnostic and lets callers construct either an owning std::string
    // (via std::string(string_view)) or a std::pmr::string without
    // tripping an implicit-conversion error across distinct allocator
    // types.

    // ── Phase-3.3.D friend declaration for the test-only inspector ──
    // Closes the access to `m_text_runs` for
    // <chronon3d/scene/builders/test/layer_builder_inspection.hpp>.
    // Friendship is 1:1 — production TUs that do not include the
    // test-only header cannot trigger this access.  See the inspector
    // header for design rationale + lifetime considerations.
    friend class chronon3d::builders::testing::LayerBuilderInspector;

    Layer m_layer;
    SampleTime m_current_time{SampleTime::from_frame_int(0, FrameRate{30, 1})};
    std::optional<Frame> m_until_frame{};
    bool m_duration_explicit{false};
    f32 m_screen_width{1920.0f};
    f32 m_screen_height{1080.0f};
    // PR 4 — flips to true only when an explicit `screen_dimensions(w, h)`
    // call has occurred.  Defaults to false on construction so LayerBuilder
    // spawned without screen info can be detected by the authoring facade.
    bool m_screen_dimensions_explicit{false};
    std::string m_default_font_path;          // layer-level default for animated_text()
    std::optional<f32> m_default_font_size;    // layer-level default for animated_text()
    FontEngine* m_font_engine{nullptr};
    registry::ShapeRegistry* m_shape_registry{nullptr};
    std::optional<registry::ShapeRegistry> m_own_shape_registry;
    // PR 3.5 — host-side ambient registries. Non-owning. Nullopt by default;
    // when set, propagates into every chronon3d::authoring::Text handle this
    // layer produces so `.style(id)` / `.motion(id)` resolve without an
    // explicit registry argument.
    const ExtensionContext* m_extension_context{nullptr};

    // ── PR 4 — pending text-run specs + builder pool ──────────────────
    //
    // `m_text_runs` holds the spec data the builder writes into; stored
    // via `unique_ptr` so push_back doesn't invalidate references.
    //
    // `m_text_run_builders` holds each TextRunBuilder keyed to the
    // matching spec.  Builders own nothing on the stack — the caller
    // keeps a `TextRunBuilder&` reference borrowed from this pool,
    // so destruction of this LayerBuilder (or any explicit reset())
    // is what ends the builder's lifetime.  This is the only way the
    // compiler accepts `layer.text_run(...).position(...).opacity(...)`
    // when chained on multiple statements.
    std::vector<std::unique_ptr<PendingTextRun>> m_text_runs;
    std::vector<std::unique_ptr<TextRunBuilder>> m_text_run_builders;
};

} // namespace chronon3d

// ── Phase-3.3.D bottom include of inline accessor bodies ──────────
// The bodies of the inline accessors (screen_dimensions family,
// name, resource, extension_context setter/getter) live in
// detail/layer_builder_inline.inl.  The .inl includes are reached
// here so that any TU pulling in <chronon3d/scene/builders/layer_builder.hpp>
// gets the inline bodies transitively via ODR-safe mechanism.
#include "chronon3d/scene/builders/detail/layer_builder_inline.inl"
