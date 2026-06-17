#pragma once

// ═════════════════════════════════════════════════════════════════════════════
//  SvgSceneImporter — full SVG document → Chronon3d layers.
//
//  Slice scope (what we *do* parse):
//    • <svg viewBox="minX minY w h">
//    • <g transform="translate(x,y) | scale(sx,sy) | rotate(a) | matrix(a,b,c,d,e,f)">
//    • <path d="…"> — via a thin wrapper around `parse_svg_path_data`
//    • <rect x y width height>, <circle cx cy r>
//    • id="…" on any element
//    • fill="…" / stroke="…" / stroke-width="…" colour attrs (basic)
//
//  Slice scope (what we *don't* yet handle):
//    • <defs> / <linearGradient> / <clipPath>   → represented as plain colour
//    • CSS-style class styling                  → inlined attrs only
//    • `<use href="#…">` references             → denied; resolve at load
//    • <text>                                   → text shapes via TextParams
//      (intentionally out-of-slice; the SVG importer focuses on graphics)
//
//  Document cache: parsed scenes are kept in a process-wide singleton,
//  keyed by absolute file path.
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/math/color.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d::motion_studio {

struct SvgElement {
    enum class Kind { Path, Rect, Circle };
    Kind        kind   = Kind::Path;
    std::string id;
    std::string path_d;                              // for Kind == Path
    chronon3d::Vec2 rect_size{0.0f, 0.0f};          // for Kind == Rect
    chronon3d::Vec2 circle_pos{0.0f, 0.0f};         // for Kind == Circle
    f32          circle_r = 0.0f;
    chronon3d::Vec2 bbox_min{0.0f, 0.0f};
    chronon3d::Vec2 bbox_max{0.0f, 0.0f};
    chronon3d::Color fill  {0.0f, 0.0f, 0.0f, 0.0f};
    chronon3d::Color stroke{0.0f, 0.0f, 0.0f, 0.0f};
    f32          stroke_width = 1.0f;
    bool         has_fill   = false;
    bool         has_stroke = false;
};

struct SvgScene {
    std::vector<SvgElement> elements;
    chronon3d::Vec2 viewbox{0.0f, 0.0f};
    chronon3d::Vec2 viewbox_size{0.0f, 0.0f};
    bool has_viewbox = false;

    /// Return the union bbox of all elements (local to viewbox).
    [[nodiscard]] chronon3d::Vec2 content_min() const;
    [[nodiscard]] chronon3d::Vec2 content_max() const;
};

struct SvgImportError {
    std::string message;
    std::size_t offset = 0;       // char offset in input when available
};

struct SvgImportResult {
    std::shared_ptr<SvgScene> scene;
    std::vector<SvgImportError> errors;
    bool ok() const { return scene != nullptr; }
};

class SvgSceneImporter {
public:
    static SvgSceneImporter& instance();

    /// Parse SVG text.  No caching.
    SvgImportResult parse_text(std::string_view svg) const;

    /// Load + parse SVG file.  Cached by absolute path on success.
    SvgImportResult load_file(const std::string& path);

    void clear_cache() { cache_.clear(); }
    std::size_t cache_size() const { return cache_.size(); }

    /// Emit layers for each element under `<svg>` with a given origin/scale.
    /// Layer names are prefixed with `id_prefix + "_" + element_id` so they
    /// never collide with the rest of the composition.
    void apply(const SvgScene& scene,
               chronon3d::SceneBuilder& s,
               chronon3d::Vec2 origin,
               f32 scale,
               const std::string& id_prefix = "svg");

private:
    std::unordered_map<std::string, std::shared_ptr<SvgScene>> cache_;
};

} // namespace chronon3d::motion_studio
