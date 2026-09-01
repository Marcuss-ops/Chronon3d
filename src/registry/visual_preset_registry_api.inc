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
