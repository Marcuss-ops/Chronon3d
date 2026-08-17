// ─── visual_preset_registry.cpp — VisualPresetRegistry implementation ──────
//
// VISUAL-SSOT-01 — single canonical registry for overlay-level visual
// presets.  Seeds the 7 built-in presets (caption_card / active_word_pop /
// subtitle_card / lower_third_safe / organization_card / location_card /
// image_focus_in) with their default style, anchor, animation, fallback
// anchors and capabilities.
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

#include <stdexcept>
#include <utility>

namespace chronon3d::registry {

namespace {

// register_builtin_visual_presets seeds the canonical catalog.  Order is
// editorial: the caption/word treatments first, then the entity cards, then
// the image treatment.
void register_builtin_visual_presets(VisualPresetRegistry& r) {
    // ── caption_card — IMPORTANT_PHRASE ─────────────────────────────────
    r.register_preset(VisualPresetDescriptor{
        .id = "caption_card",
        .version = 1,
        .supported_layer = VisualLayerKind::Text,
        .style = VisualStyle{
            .font_family = "Poppins",
            .font_weight = 700,
            .font_size = 72.0f,
            .fill = "#FFFFFF",
            .background_color = "#050509",
            .background_opacity = 0.86f,
            .radius = 12.0f,
            .padding = std::array<float, 2>{24.0f, 14.0f},
        },
        .anchor = AnchorSpec{.type = "safe_area", .safe_margin = 0.06f, .alignment = "center"},
        .animation = AnimationSpec{.preset = "fade_in", .unit = "line",
                                   .enter_duration_frames = 8, .exit_duration_frames = 6},
        .fallback_anchors = {"center", "top"},
        .capabilities = {"card", "local_background", "collision_avoid"},
    });

    // ── active_word_pop — IMPORTANT_WORD ───────────────────────────────
    r.register_preset(VisualPresetDescriptor{
        .id = "active_word_pop",
        .version = 1,
        .supported_layer = VisualLayerKind::Text,
        .style = VisualStyle{
            .font_family = "Poppins",
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
        .animation = AnimationSpec{.preset = "active_word_pop", .unit = "word",
                                   .enter_duration_frames = 8, .exit_duration_frames = 6},
        .fallback_anchors = {"center", "top", "bottom"},
        .capabilities = {"card", "local_background", "word_selector"},
    });

    // ── subtitle_card ──────────────────────────────────────────────────
    r.register_preset(VisualPresetDescriptor{
        .id = "subtitle_card",
        .version = 1,
        .supported_layer = VisualLayerKind::Text,
        .style = VisualStyle{
            .font_family = "Poppins",
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
        .capabilities = {"card", "local_background"},
    });

    // ── lower_third_safe — PERSON ──────────────────────────────────────
    r.register_preset(VisualPresetDescriptor{
        .id = "lower_third_safe",
        .version = 1,
        .supported_layer = VisualLayerKind::Text,
        .style = VisualStyle{
            .font_family = "Poppins",
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
        .animation = AnimationSpec{.preset = "fade_in", .unit = "line",
                                   .enter_duration_frames = 8, .exit_duration_frames = 6},
        .fallback_anchors = {"lower_right", "top_left", "top_right"},
        .capabilities = {"card", "local_background", "collision_avoid"},
    });

    // ── organization_card — ORG ────────────────────────────────────────
    r.register_preset(VisualPresetDescriptor{
        .id = "organization_card",
        .version = 1,
        .supported_layer = VisualLayerKind::Text,
        .style = VisualStyle{
            .font_family = "Poppins",
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
        .capabilities = {"card", "local_background", "collision_avoid"},
    });

    // ── location_card — LOCATION ───────────────────────────────────────
    r.register_preset(VisualPresetDescriptor{
        .id = "location_card",
        .version = 1,
        .supported_layer = VisualLayerKind::Text,
        .style = VisualStyle{
            .font_family = "Poppins",
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
        .capabilities = {"card", "local_background", "collision_avoid"},
    });

    // ── image_focus_in — IMAGE ─────────────────────────────────────────
    r.register_preset(VisualPresetDescriptor{
        .id = "image_focus_in",
        .version = 1,
        .supported_layer = VisualLayerKind::Image,
        .style = VisualStyle{},
        .anchor = AnchorSpec{.type = "image_right", .safe_margin = 0.06f, .alignment = "left"},
        .animation = AnimationSpec{.preset = "fade_in", .unit = "line",
                                   .enter_duration_frames = 8, .exit_duration_frames = 6},
        .fallback_anchors = {"image_left", "top_right", "top_left"},
        .capabilities = {"collision_avoid"},
    });
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
