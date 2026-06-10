#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// LayerBuilder — Core Commands
//
// Timing, transform, depth, layout, node transform/polish, transitions,
// specialized helpers and font-engine access.
//
// This header is #included inside class LayerBuilder { … };
// ═════════════════════════════════════════════════════════════════════════════

    // ── Timing ──
    LayerBuilder& parent(std::string name);
    LayerBuilder& from(Frame frame);
    LayerBuilder& duration(Frame frames);
    LayerBuilder& until(Frame frame);
    LayerBuilder& offset(Frame frames);

    // ── Time Remap ──
    LayerBuilder& speed(f32 multiplier);
    LayerBuilder& reverse(bool value = true);
    LayerBuilder& freeze_frame(Frame source_frame);
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
    LayerBuilder& pin_to(Anchor anchor, f32 margin = 0.0f);
    LayerBuilder& pin_to(AnchorPlacement placement, f32 margin = 0.0f);
    LayerBuilder& keep_in_safe_area(SafeArea area = {});
    LayerBuilder& fit_text();

    // ── Node Transform ──
    LayerBuilder& at(Vec3 pos);
    LayerBuilder& rotate_node(Vec3 euler_deg);
    LayerBuilder& scale_node(Vec3 s);
    LayerBuilder& anchor_node(Vec3 a);
    LayerBuilder& node_opacity(f32 a);

    // ── Node Polish ──
    LayerBuilder& with_shadow(DropShadow shadow);
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
    LayerBuilder& screen_dimensions(f32 w, f32 h);
    LayerBuilder& fullscreen_rect(std::string name, Color color);
    LayerBuilder& fill(Color color);

    // ── FontEngine ──
    LayerBuilder& font_engine(FontEngine* engine);
    [[nodiscard]] FontEngine* font_engine() const;
