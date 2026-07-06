#pragma once
// ─── text_audit_typewriter.hpp ───────────────────────────────────────────
//
// FASE 2 Step 2 — extracted from text_audit_engine.cpp.
// Typewriter-detection helpers: layer text collection, typewriter pattern
// detection (per-character layers named "{prefix}_c{N}").
// ─────────────────────────────────────────────────────────────────────────

#include <chronon3d/scene/model/scene.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>

#include <string>
#include <vector>

namespace chronon3d::cli {

/// Single text node found in a scene layer.
struct LayerTextNode {
    TextShape text;
    std::string layer_name;
    float layer_opacity{1.0f};
    float offset_x{0.0f};
    float offset_y{0.0f};
};

/// Walk scene.layers() and each layer's nodes to find text shapes.
std::vector<LayerTextNode> collect_text_from_scene(const Scene& scene);

/// Parse layer name pattern "{prefix}_c{digits}" used by typewriter_build.
/// Returns true and sets prefix + index on success.
bool parse_typewriter_layer_name(const std::string& name,
                                  std::string& prefix, int& index);

/// Group of single-character layers detected as a typewriter animation.
struct TypewriterGroup {
    std::string prefix;
    std::vector<LayerTextNode> chars;  // sorted by index
    std::string full_text;             // concatenation of all char glyphs
};

/// Detect typewriter_build patterns: groups of single-char layers with
/// names like "tw_c0", "tw_c1", etc.
std::vector<TypewriterGroup> detect_typewriter_groups(
    const std::vector<LayerTextNode>& nodes);

} // namespace chronon3d::cli
