// ==============================================================================
// CLI Command: inspect text-def
// Export a JSON diagnostic of all TextDefinition/TextRunSpec fields for frame 0
// of a composition.  Walks the resolved scene, finds text-run layers, and
// serialises every TextRunShape field to structured JSON.
// ==============================================================================
#include "../../commands.hpp"
#include "../../utils/job/cli_render_utils.hpp"
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/layer/layer.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <chronon3d/text/text_run_layout.hpp>
#include <nlohmann/json.hpp>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <fstream>

namespace chronon3d {
namespace cli {

namespace {

// ── Helper: serialise a TextLayoutSpec to JSON ─────────────────────────────
nlohmann::json layout_spec_to_json(const TextLayoutSpec& ls) {
    nlohmann::json j;
    j["box"]             = {{"width", ls.box.x}, {"height", ls.box.y}};
    j["anchor"]          = static_cast<int>(ls.anchor);
    j["centering_mode"]  = static_cast<int>(ls.centering_mode);
    j["align"]           = static_cast<int>(ls.align);
    j["vertical_align"]  = static_cast<int>(ls.vertical_align);
    j["wrap"]            = static_cast<int>(ls.wrap);
    j["overflow"]        = static_cast<int>(ls.overflow);
    j["line_height"]     = ls.line_height;
    j["tracking"]        = ls.tracking;
    j["auto_fit"]        = ls.auto_fit;
    j["min_font_size"]   = ls.min_font_size;
    j["max_font_size"]   = ls.max_font_size;
    j["max_lines"]       = ls.max_lines;
    j["ellipsis"]        = ls.ellipsis;
    return j;
}

// ── Helper: serialise TextPaint to JSON ────────────────────────────────────
nlohmann::json paint_to_json(const TextPaint& p) {
    nlohmann::json j;
    j["fill"]            = {p.fill.r, p.fill.g, p.fill.b, p.fill.a};
    j["fill_style"]      = p.fill_style.has_value() ? "gradient" : "none";
    j["stroke_enabled"]  = p.stroke_enabled;
    j["stroke_color"]    = {p.stroke_color.r, p.stroke_color.g,
                             p.stroke_color.b, p.stroke_color.a};
    j["stroke_width"]    = p.stroke_width;
    j["stroke_style"]    = p.stroke_style.has_value() ? "gradient" : "none";
    return j;
}

// ── Helper: serialise a Transform to JSON ──────────────────────────────────
nlohmann::json transform_to_json(const Transform& t) {
    nlohmann::json j;
    j["position"] = {t.position.x, t.position.y, t.position.z};
    j["rotation"] = {t.rotation.x, t.rotation.y, t.rotation.z};
    j["scale"]    = {t.scale.x, t.scale.y, t.scale.z};
    return j;
}

// ── Helper: serialise one TextRunShape to JSON ─────────────────────────────
nlohmann::json text_run_shape_to_json(const TextRunShape& trs,
                                       const RenderNode& node) {
    nlohmann::json j;

    // ── TextRunLayout (authoring-level fields: content, font, direction) ─
    if (trs.layout) {
        const auto& l = *trs.layout;
        j["source_text"]  = l.source_text;
        {
            auto& f = j["font"];
            f["font_family"] = l.font.font_family;
            f["font_weight"] = l.font.font_weight;
            f["font_style"]  = l.font.font_style;
            f["font_size"]   = l.font.font_size;
            f["font_path"]   = l.font.font_path;
        }
        j["direction"]   = static_cast<int>(l.direction);
        j["language"]    = l.language;
        j["features"]    = l.features;
        j["variation_axes"] = l.variation_axes;
        j["glyph_count"] = l.placed.glyphs.size();
        j["bounds"]      = {{"width", l.bounds.x}, {"height", l.bounds.y}};
        j["line_height"] = l.line_height;
        j["tracking"]    = l.tracking;
        j["wrap"]        = static_cast<int>(l.wrap);
    } else {
        j["glyph_count"] = 0;
    }
    j["engine"] = trs.engine != nullptr;

    // ── Layout spec ───────────────────────────────────────────────────
    j["layout"] = layout_spec_to_json(trs.layout_spec);

    // ── Appearance ────────────────────────────────────────────────────
    j["paint"]   = paint_to_json(trs.paint);
    {
        auto& sh = j["shadows"] = nlohmann::json::array();
        for (const auto& s : trs.shadows) {
            nlohmann::json sj;
            sj["enabled"] = s.enabled;
            sj["offset"]  = {s.offset.x, s.offset.y};
            sj["blur"]    = s.blur;
            sj["opacity"] = s.opacity;
            sj["color"]   = {s.color.r, s.color.g, s.color.b, s.color.a};
            sh.push_back(std::move(sj));
        }
    }

    // ── Material (premium effects) ────────────────────────────────────
    {
        auto& m = j["material"];
        m["glow_color"]     = {trs.material.glow_color.r,
                               trs.material.glow_color.g,
                               trs.material.glow_color.b,
                               trs.material.glow_color.a};
        m["glow_radius"]    = trs.material.glow_radius;
        m["glow_intensity"] = trs.material.glow_intensity;
        m["bevel_px"]       = trs.material.bevel_px;
        m["bevel_highlight_opacity"] = trs.material.bevel_highlight_opacity;
        m["bevel_shadow_opacity"]    = trs.material.bevel_shadow_opacity;
        m["inner_shadow_enabled"]    = trs.material.inner_shadow.has_value();
    }

    // ── Animation ─────────────────────────────────────────────────────
    j["animator_count"] = trs.animators.size();
    j["crossfade_active"] = trs.crossfade_mix > 0.0f && trs.crossfade_mix < 1.0f;
    j["cache"] = trs.cache != nullptr;

    // ── World transform (from RenderNode) ────────────────────────────
    j["world_transform"] = transform_to_json(node.world_transform);

    // ── Node metadata ────────────────────────────────────────────────
    j["node_name"] = std::string(node.name);
    j["surface_policy"] = static_cast<int>(node.surface_policy);

    return j;
}

} // namespace

// ── command_text_def_inspect ────────────────────────────────────────────────
int command_text_def_inspect(const CompositionRegistry& registry,
                              const TextDefInspectArgs& args) {
    // Resolve composition
    auto resolved = resolve_composition(registry, args.comp_id);
    if (!resolved) {
        spdlog::error("text-def-inspect: unknown composition '{}'", args.comp_id);
        return 1;
    }

    const auto& comp = *resolved.comp;

    // Evaluate at frame 0
    Scene scene = comp.evaluate(Frame{0});

    // Build JSON
    nlohmann::json js;
    js["composition"] = args.comp_id;
    js["canvas"]      = {{"width", comp.width()}, {"height", comp.height()}};
    js["duration"]    = comp.duration();
    js["frame"]       = 0;

    auto& layers_json = js["text_layers"] = nlohmann::json::array();

    int total_text_runs = 0;

    for (const auto& layer : scene.layers()) {
        // Scan ALL layers — legacy Normal-kind layers may also carry text runs
        nlohmann::json lj;
        lj["name"]    = std::string(layer.name);
        lj["kind"]    = layer.is_text() ? "Text" : static_cast<int>(layer.kind);
        lj["visible"] = layer.visible;
        lj["active"]  = layer.active_at(Frame{0});
        lj["blend_mode"] = static_cast<int>(layer.blend_mode);

        // World transform of the layer itself
        lj["transform"] = transform_to_json(layer.transform);

        auto& runs_json = lj["text_runs"] = nlohmann::json::array();

        for (const auto& node : layer.nodes) {
            // Only process TextRun nodes
            if (node.shape.type() != ShapeType::TextRun) continue;

            const auto& handle = node.shape.text_run_shape_handle();
            if (!handle.value) continue;

            runs_json.push_back(text_run_shape_to_json(*handle.value, node));
            ++total_text_runs;
        }

        // Only include layers that have text runs
        if (!runs_json.empty()) {
            layers_json.push_back(std::move(lj));
        }
    }

    js["total_text_layers"] = layers_json.size();
    js["total_text_runs"]   = total_text_runs;

    // ── Output ──────────────────────────────────────────────────────────────
    if (!args.json_output.empty()) {
        std::ofstream out(args.json_output);
        if (!out) {
            spdlog::error("text-def-inspect: cannot write to '{}'", args.json_output);
            return 1;
        }
        out << js.dump(2) << "\n";
        spdlog::info("text-def-inspect: JSON written to '{}' ({} layers, {} text runs)",
                     args.json_output, layers_json.size(), total_text_runs);
    } else {
        // Print to stdout
        fmt::print("{}\n", js.dump(2));
    }

    spdlog::info("text-def-inspect: {} text layers, {} text runs",
                 layers_json.size(), total_text_runs);

    return 0;
}

} // namespace cli
} // namespace chronon3d
