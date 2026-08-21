// ─── visual_preset_registry.cpp — VisualPresetRegistry implementation ──────
//
// VISUAL-SSOT-01 — single canonical registry for overlay-level visual
// presets. Seeds the canonical cards plus 15 simple 2D showcase variants
// (five image, five name and five important-phrase presets) with their
// default style, anchor, animation, fallback anchors and capabilities.
//
// Anti-circular-dependency: this .cpp includes ONLY the registry descriptor
// header.  No `content/text/text_*.hpp`, no SceneBuilder/LayerBuilder, no
// backend headers — the edge-direction canon (content → core/registry,
// never vice versa) is preserved.
//
// Singleton pattern mirrors `text_preset_registry.cpp`: a namespace-scope
// const object (constant-initialized, thread-safe) exposed through a
// const-ref accessor.

#include <chronon3d/registry/visual_preset_registry.hpp>

#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace chronon3d::registry {

namespace {

VisualPresetDescriptor make_simple_2d_preset(
    std::string id,
    std::string semantic_role,
    VisualLayerKind layer,
    std::string base_preset,
    std::string animation,
    std::string unit,
    std::string anchor,
    std::string alignment,
    float font_size,
    int font_weight) {
    VisualPresetDescriptor preset;
    preset.id = std::move(id);
    preset.semantic_role = std::move(semantic_role);
    preset.version = 1;
    preset.supported_layer = layer;
    preset.base_preset = std::move(base_preset);
    preset.anchor = AnchorSpec{
        .type = std::move(anchor), .safe_margin = 0.06f,
        .alignment = std::move(alignment)};
    preset.animation = AnimationSpec{
        .preset = std::move(animation), .unit = std::move(unit),
        .enter_duration_frames = 8, .exit_duration_frames = 6};
    preset.fallback_anchors = {"center", "top_left", "top_right", "bottom_left"};
    preset.capabilities = {"2d", "collision_avoid"};

    if (layer == VisualLayerKind::Text) {
        preset.style.font_family = "Poppins";
        preset.style.font_asset = "assets/fonts/Poppins-Bold.ttf";
        preset.style.font_weight = font_weight;
        preset.style.font_size = font_size;
        preset.style.fill = "#FFFFFF";
        preset.style.stroke_color = "#111827";
        preset.style.stroke_width = 1.5f;
        preset.style.shadow_color = "#000000";
        preset.style.shadow_opacity = 0.72f;
        preset.style.shadow_blur = 10.0f;
        preset.style.shadow_offset = std::array<float, 2>{0.0f, 4.0f};
        preset.style.background_color = "#050509";
        preset.style.background_opacity = 0.88f;
        preset.style.radius = 10.0f;
        preset.style.padding = std::array<float, 2>{20.0f, 12.0f};
        preset.capabilities.push_back("local_background");
        preset.capabilities.push_back("card");
    } else {
        preset.fallback_anchors = {"image_left", "image_right", "top_right", "top_left"};
        preset.capabilities.push_back("image_transform");
        // Image presets own their canonical box + fit (the former PipelineGen
        // IMAGE_OVERLAY transport shape): 260×260 contain, matched across the
        // 2D image showcase family (ADR-029 — geometry lives in Chronon).
        preset.box_width = 260.0f;
        preset.box_height = 260.0f;
        preset.fit = "contain";
    }
    return preset;
}

VisualPresetDescriptor make_name_glow_preset(
    std::string id,
    std::string motion,
    std::string unit) {
    const bool typewriter = unit == "glyph";
    auto preset = make_simple_2d_preset(
        std::move(id), "name", VisualLayerKind::Text, "lower_third",
        std::move(motion), std::move(unit), "lower_third", "left", 58.0f, 700);
    // The glow is local to the name card, keeping the treatment readable
    // without a global veil or a full-frame effect.
    preset.style.shadow_color = "#38BDF8";
    preset.style.shadow_opacity = 0.82f;
    preset.style.shadow_blur = 14.0f;
    preset.style.shadow_offset = std::array<float, 2>{0.0f, 2.0f};
    preset.capabilities.push_back("glow");
    if (typewriter) preset.capabilities.push_back("typewriter");
    return preset;
}

// register_builtin_visual_presets seeds the canonical catalog.  Order is
// editorial: the caption/word treatments first, then the entity cards, then
// the image treatment.
void register_builtin_visual_presets(VisualPresetRegistry& r) {
    // ── caption_card — IMPORTANT_PHRASE ─────────────────────────────────
    r.register_preset(VisualPresetDescriptor{
        .id = "caption_card",
        .semantic_role = "important_phrase",
        .version = 1,
        .supported_layer = VisualLayerKind::Text,
        .base_preset = "caption_safe_area",
        .style = VisualStyle{
            .font_family = "Poppins",
            .font_asset = "assets/fonts/Poppins-Bold.ttf",
            .font_weight = 700,
            .font_size = 64.0f,
            .fill = "#FFFFFF",
            .shadow_color = "#000000",
            .shadow_opacity = 0.42f,
            .shadow_blur = 10.0f,
            .shadow_offset = std::array<float, 2>{0.0f, 4.0f},
            .background_color = "#050509",
            .background_opacity = 0.88f,
            .radius = 10.0f,
            .padding = std::array<float, 2>{20.0f, 12.0f},
        },
        .anchor = AnchorSpec{.type = "center", .safe_margin = 0.06f, .alignment = "center"},
        .animation = AnimationSpec{.preset = "fade_in", .unit = "line",
                                   .enter_duration_frames = 10, .exit_duration_frames = 8},
        .fallback_anchors = {"center", "top"},
        .capabilities = {"2d", "card", "local_background", "collision_avoid"},
    });

    // ── active_word_pop — IMPORTANT_WORD ───────────────────────────────
    r.register_preset(VisualPresetDescriptor{
        .id = "active_word_pop",
        .semantic_role = "important_word",
        .version = 1,
        .supported_layer = VisualLayerKind::Text,
        .base_preset = "kinetic_word",
        .style = VisualStyle{
            .font_family = "Poppins",
            .font_asset = "assets/fonts/Poppins-Bold.ttf",
            .font_weight = 800,
            .font_size = 64.0f,
            .fill = "#FFFFFF",
            .stroke_color = "#000000",
            .stroke_width = 2.0f,
            .background_color = "#050509",
            .background_opacity = 0.90f,
            .radius = 12.0f,
            .padding = std::array<float, 2>{16.0f, 10.0f},
        },
        .anchor = AnchorSpec{.type = "safe_area", .safe_margin = 0.06f, .alignment = "center"},
        .animation = AnimationSpec{.preset = "fade_in", .unit = "word",
                                   .enter_duration_frames = 8, .exit_duration_frames = 6},
        .fallback_anchors = {"center", "top", "bottom"},
        .capabilities = {"2d", "card", "local_background", "word_selector"},
    });

    // ── subtitle_card ──────────────────────────────────────────────────
    r.register_preset(VisualPresetDescriptor{
        .id = "subtitle_card",
        .semantic_role = "subtitle",
        .version = 1,
        .supported_layer = VisualLayerKind::Text,
        .base_preset = "subtitle_bottom",
        .style = VisualStyle{
            .font_family = "Poppins",
            .font_asset = "assets/fonts/Poppins-Regular.ttf",
            .font_weight = 500,
            .font_size = 40.0f,
            .fill = "#FFFFFF",
            .background_color = "#050509",
            .background_opacity = 0.78f,
            .radius = 8.0f,
            .padding = std::array<float, 2>{16.0f, 8.0f},
        },
        .anchor = AnchorSpec{.type = "lower_third", .safe_margin = 0.06f, .alignment = "center"},
        .animation = AnimationSpec{.preset = "fade_in", .unit = "line",
                                   .enter_duration_frames = 6, .exit_duration_frames = 4},
        .fallback_anchors = {"bottom", "safe_area"},
        .capabilities = {"2d", "card", "local_background"},
    });

    // ── lower_third_safe — PERSON ──────────────────────────────────────
    r.register_preset(VisualPresetDescriptor{
        .id = "lower_third_safe",
        .semantic_role = "name",
        .version = 1,
        .supported_layer = VisualLayerKind::Text,
        .base_preset = "lower_third",
        .style = VisualStyle{
            .font_family = "Poppins",
            .font_asset = "assets/fonts/Poppins-Bold.ttf",
            .font_weight = 700,
            .font_size = 58.0f,
            .fill = "#FFFFFF",
            .stroke_color = "#000000",
            .stroke_width = 2.0f,
            .shadow_color = "#000000",
            .shadow_opacity = 0.65f,
            .shadow_blur = 16.0f,
            .shadow_offset = std::array<float, 2>{0.0f, 6.0f},
            .background_color = "#050509",
            .background_opacity = 0.86f,
            .radius = 12.0f,
            .padding = std::array<float, 2>{24.0f, 14.0f},
        },
        .anchor = AnchorSpec{.type = "lower_third", .safe_margin = 0.06f, .alignment = "left"},
        .animation = AnimationSpec{.preset = "focus_in", .unit = "line",
                                   .enter_duration_frames = 8, .exit_duration_frames = 6},
        .fallback_anchors = {"lower_right", "top_left", "top_right"},
        .capabilities = {"2d", "card", "local_background", "collision_avoid"},
    });

    // ── organization_card — ORG ────────────────────────────────────────
    r.register_preset(VisualPresetDescriptor{
        .id = "organization_card",
        .semantic_role = "organization",
        .version = 1,
        .supported_layer = VisualLayerKind::Text,
        .base_preset = "lower_third",
        .style = VisualStyle{
            .font_family = "Poppins",
            .font_asset = "assets/fonts/Poppins-Bold.ttf",
            .font_weight = 600,
            .font_size = 52.0f,
            .fill = "#FFFFFF",
            .background_color = "#050509",
            .background_opacity = 0.84f,
            .radius = 12.0f,
            .padding = std::array<float, 2>{22.0f, 12.0f},
        },
        .anchor = AnchorSpec{.type = "lower_third", .safe_margin = 0.06f, .alignment = "left"},
        .animation = AnimationSpec{.preset = "fade_in", .unit = "line",
                                   .enter_duration_frames = 8, .exit_duration_frames = 6},
        .fallback_anchors = {"lower_right", "top_left", "top_right"},
        .capabilities = {"2d", "card", "local_background", "collision_avoid"},
    });

    // ── location_card — LOCATION ───────────────────────────────────────
    r.register_preset(VisualPresetDescriptor{
        .id = "location_card",
        .semantic_role = "location",
        .version = 1,
        .supported_layer = VisualLayerKind::Text,
        .base_preset = "lower_third",
        .style = VisualStyle{
            .font_family = "Poppins",
            .font_asset = "assets/fonts/Poppins-Bold.ttf",
            .font_weight = 600,
            .font_size = 52.0f,
            .fill = "#FFFFFF",
            .background_color = "#050509",
            .background_opacity = 0.84f,
            .radius = 12.0f,
            .padding = std::array<float, 2>{22.0f, 12.0f},
        },
        .anchor = AnchorSpec{.type = "lower_third", .safe_margin = 0.06f, .alignment = "left"},
        .animation = AnimationSpec{.preset = "fade_in", .unit = "line",
                                   .enter_duration_frames = 8, .exit_duration_frames = 6},
        .fallback_anchors = {"lower_right", "top_left", "top_right"},
        .capabilities = {"2d", "card", "local_background", "collision_avoid"},
    });

    // ── image_focus_in — IMAGE ─────────────────────────────────────────
    r.register_preset(VisualPresetDescriptor{
        .id = "image_focus_in",
        .semantic_role = "image",
        .version = 1,
        .supported_layer = VisualLayerKind::Image,
        .base_preset = {},
        .style = VisualStyle{},
        .anchor = AnchorSpec{.type = "image_right", .safe_margin = 0.06f, .alignment = "left"},
        .animation = AnimationSpec{.preset = "focus_in", .unit = "line",
                                   .enter_duration_frames = 8, .exit_duration_frames = 6},
        .fallback_anchors = {"image_left", "top_right", "top_left"},
        .capabilities = {"2d", "collision_avoid"},
        .box_width = 260.0f,
        .box_height = 260.0f,
        .fit = "contain",
    });

    // ── 2D showcase presets ────────────────────────────────────────────
    r.register_preset(make_simple_2d_preset(
        "image_fade_in", "image", VisualLayerKind::Image, "",
        "fade_in", "line", "image_right", "left", 0.0f, 0));
    r.register_preset(make_simple_2d_preset(
        "image_slide_left", "image", VisualLayerKind::Image, "",
        "slide_in", "line", "image_right", "left", 0.0f, 0));
    r.register_preset(make_simple_2d_preset(
        "image_slide_right", "image", VisualLayerKind::Image, "",
        "fade_shift_horizontal", "line", "image_left", "right", 0.0f, 0));
    r.register_preset(make_simple_2d_preset(
        "image_scale_in", "image", VisualLayerKind::Image, "",
        "scale_drop", "line", "center", "center", 0.0f, 0));

    r.register_preset(make_simple_2d_preset(
        "phrase_fade_in", "important_phrase", VisualLayerKind::Text,
        "caption_safe_area", "fade_in", "line", "safe_area", "center", 64.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "phrase_scale_in", "important_phrase", VisualLayerKind::Text,
        "caption_safe_area", "scale_drop", "line", "safe_area", "center", 64.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "phrase_slide_up", "important_phrase", VisualLayerKind::Text,
        "caption_safe_area", "reveal_from_bottom", "line", "safe_area", "center", 64.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "phrase_soft_pop", "important_phrase", VisualLayerKind::Text,
        "caption_safe_area", "soft_pop", "line", "safe_area", "center", 64.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "phrase_word_reveal", "important_phrase", VisualLayerKind::Text,
        "caption_safe_area", "fade_in", "word", "safe_area", "center", 64.0f, 700));

    // ── Modern fast overlay aliases ────────────────────────────────────
    r.register_preset(make_simple_2d_preset(
        "fast_fade_through", "important_phrase", VisualLayerKind::Text,
        "caption_safe_area", "fade_in", "line", "safe_area", "center", 64.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "snap_scale", "important_phrase", VisualLayerKind::Text,
        "caption_safe_area", "scale_drop", "line", "safe_area", "center", 64.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "clean_slide_up", "important_phrase", VisualLayerKind::Text,
        "caption_safe_area", "reveal_from_bottom", "line", "safe_area", "center", 64.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "slide_lateral", "important_phrase", VisualLayerKind::Text,
        "caption_safe_area", "slide_in", "line", "safe_area", "center", 64.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "undertext_pop", "subtitle", VisualLayerKind::Text,
        "subtitle_bottom", "fade_in", "line", "lower_third", "center", 48.0f, 700));

    r.register_preset(make_simple_2d_preset(
        "modern_rounded_pop", "image", VisualLayerKind::Image,
        "", "scale_drop", "line", "center", "center", 0.0f, 0));
    r.register_preset(make_simple_2d_preset(
        "bottom_card_rise", "image", VisualLayerKind::Image,
        "", "reveal_from_bottom", "line", "center", "center", 0.0f, 0));
    r.register_preset(make_simple_2d_preset(
        "image_fast_fade", "image", VisualLayerKind::Image,
        "", "fade_in", "line", "center", "center", 0.0f, 0));

    // Names / entity cards
    r.register_preset(make_simple_2d_preset(
        "name_fade_in", "name", VisualLayerKind::Text, "lower_third",
        "fade_in", "line", "lower_third", "left", 58.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "name_slide_up", "name", VisualLayerKind::Text, "lower_third",
        "reveal_from_bottom", "line", "lower_third", "left", 58.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "name_pop_in", "name", VisualLayerKind::Text, "lower_third",
        "soft_pop", "line", "lower_third", "left", 58.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "name_slide_left", "name", VisualLayerKind::Text, "lower_third",
        "slide_in", "line", "lower_third", "left", 58.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "name_scale_in", "name", VisualLayerKind::Text, "lower_third",
        "scale_drop", "line", "lower_third", "left", 58.0f, 700));

    r.register_preset(make_name_glow_preset(
        "name_glow_typewriter", "fade_in", "glyph"));
    r.register_preset(make_name_glow_preset(
        "name_glow_slide", "reveal_from_bottom", "line"));
    r.register_preset(make_name_glow_preset(
        "name_glow_pop", "fade_in", "line"));
}

} // namespace

// ── ctor ────────────────────────────────────────────────────────────────────
VisualPresetRegistry::VisualPresetRegistry() = default;

// ── register_preset ─────────────────────────────────────────────────────────
void VisualPresetRegistry::register_preset(VisualPresetDescriptor preset) {
    if (m_frozen) {
        throw std::runtime_error(
            "VisualPresetRegistry::register_preset: registry is frozen ("
            + (preset.id.empty() ? std::string{"<empty-id>"} : preset.id) + ")");
    }
    if (preset.id.empty()) {
        throw std::runtime_error(
            "VisualPresetRegistry::register_preset: empty id rejected");
    }
    if (m_presets.contains(preset.id)) {
        throw std::runtime_error(
            "VisualPresetRegistry::register_preset: duplicate id '"
            + preset.id + "'");
    }
    m_presets.emplace(preset.id, std::move(preset));
}

// ── contains / get ──────────────────────────────────────────────────────────
bool VisualPresetRegistry::contains(std::string_view id) const {
    return m_presets.contains(id);
}

const VisualPresetDescriptor& VisualPresetRegistry::get(std::string_view id) const {
    auto it = m_presets.find(id);
    if (it == m_presets.end()) {
        throw std::runtime_error(
            "VisualPresetRegistry::get: unknown preset id '" + std::string{id} + "'");
    }
    return it->second;
}

VisualPresetDescriptor VisualPresetRegistry::get_for_profile(
    std::string_view id, std::string_view profile) const {
    if (profile != "discovery" && profile != "young" && profile != "crime") {
        throw std::runtime_error("VisualPresetRegistry: unknown style profile '" +
                                 std::string{profile} + "'");
    }
    auto resolved = get(id);
    if (profile == "discovery") return resolved;

    // Profile variants are intentionally resolved here, beside the canonical
    // preset registry.  RenderingGen only transports `style_profile`; it does
    // not maintain a second visual table.
    if (resolved.supported_layer == VisualLayerKind::Text) {
        if (profile == "young") {
            resolved.style.font_family = "Poppins";
            resolved.style.font_asset = "assets/fonts/Poppins-Bold.ttf";
            resolved.style.font_weight = 700;
            resolved.style.font_size = 64.0f;
            resolved.style.fill = "#FFFFFF";
            resolved.style.stroke_color = "#22D3EE";
            resolved.style.stroke_width = 2.0f;
            resolved.style.shadow_color = "#020617";
            resolved.style.shadow_opacity = 0.72f;
            resolved.style.shadow_blur = 14.0f;
            resolved.style.background_color = "#172554";
            resolved.style.background_opacity = 0.94f;
            resolved.animation.preset = "soft_pop";
        } else { // crime: high-contrast red/black broadcast treatment.
            resolved.style.font_family = "Poppins";
            resolved.style.font_asset = "assets/fonts/Poppins-Bold.ttf";
            resolved.style.font_weight = 800;
            resolved.style.font_size = 58.0f;
            resolved.style.fill = "#F8FAFC";
            resolved.style.stroke_color = "#EF4444";
            resolved.style.stroke_width = 2.0f;
            resolved.style.shadow_color = "#000000";
            resolved.style.shadow_opacity = 0.86f;
            resolved.style.shadow_blur = 18.0f;
            resolved.style.shadow_offset = std::array<float, 2>{0.0f, 5.0f};
            resolved.style.background_color = "#160B0B";
            resolved.style.background_opacity = 0.95f;
            // `slide_up` is a text reveal preset, not a LayerBuilder motion.
            // Keep the profile on the canonical registered layer motion.
            resolved.animation.preset = "soft_pop";
        }
    } else if (profile == "young") {
        resolved.animation.preset = "soft_pop";
    } else {
        resolved.animation.preset = "soft_pop";
    }
    return resolved;
}

// ── available / list ────────────────────────────────────────────────────────
std::vector<std::string> VisualPresetRegistry::available() const {
    std::vector<std::string> out;
    out.reserve(m_presets.size());
    for (const auto& [id, _] : m_presets) {
        out.push_back(id);
    }
    return out;  // std::map guarantees sorted-by-key determinism.
}

std::vector<VisualPresetDescriptor> VisualPresetRegistry::list() const {
    std::vector<VisualPresetDescriptor> out;
    out.reserve(m_presets.size());
    for (const auto& [_, preset] : m_presets) {
        out.push_back(preset);
    }
    return out;
}

// ── clear / reset ───────────────────────────────────────────────────────────
void VisualPresetRegistry::clear() {
    if (m_frozen) return;  // freeze trumps clear.
    m_presets.clear();
}

void VisualPresetRegistry::reset() {
    m_presets.clear();
    m_frozen = false;
}

// ── make_default_visual_preset_registry ─────────────────────────────────────
VisualPresetRegistry make_default_visual_preset_registry() {
    VisualPresetRegistry r;
    register_builtin_visual_presets(r);
    r.freeze();
    return r;
}

// ── builtin_visual_preset_registry ──────────────────────────────────────────
//
// Built-in catalog storage is a single immutable namespace-scope object.
// The accessor is intentionally only a view; production consumers cannot
// mutate the frozen catalog through this API.
const VisualPresetRegistry kBuiltinVisualPresetRegistry =
    make_default_visual_preset_registry();

const VisualPresetRegistry&
builtin_visual_preset_registry() noexcept {
    return kBuiltinVisualPresetRegistry;
}

} // namespace chronon3d::registry
