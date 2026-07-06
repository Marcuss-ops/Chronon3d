#include "text_audit_typewriter.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <utility>

namespace chronon3d::cli {

// ── collect_text_from_scene ────────────────────────────────────────────────

std::vector<LayerTextNode> collect_text_from_scene(const Scene& scene) {
    std::vector<LayerTextNode> result;
    for (const auto& layer : scene.layers()) {
        if (!layer.visible) continue;
        for (const auto& node : layer.nodes) {
            if (node.shape.type() == ShapeType::Text && !node.shape.text().text.empty()) {
                LayerTextNode info;
                info.text = node.shape.text();
                info.layer_name = std::string(layer.name);
                info.layer_opacity = layer.transform.opacity;
                info.offset_x = node.world_transform.position.x;
                info.offset_y = node.world_transform.position.y;
                result.push_back(std::move(info));
            }
        }
    }
    return result;
}

// ── parse_typewriter_layer_name ─────────────────────────────────────────────

bool parse_typewriter_layer_name(const std::string& name,
                                  std::string& prefix, int& index) {
    auto cpos = name.rfind("_c");
    if (cpos == std::string::npos || cpos == 0) return false;
    std::string suffix = name.substr(cpos + 2);
    if (suffix.empty()) return false;
    for (char c : suffix) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    prefix = name.substr(0, cpos);
    index = std::stoi(suffix);
    return true;
}

// ── detect_typewriter_groups ───────────────────────────────────────────────

std::vector<TypewriterGroup> detect_typewriter_groups(
    const std::vector<LayerTextNode>& nodes)
{
    std::map<std::string, std::vector<std::pair<int, LayerTextNode>>> groups;

    for (const auto& node : nodes) {
        std::string prefix;
        int index;
        if (parse_typewriter_layer_name(node.layer_name, prefix, index)) {
            groups[prefix].push_back({index, node});
        }
    }

    std::vector<TypewriterGroup> result;
    for (auto& [prefix, entries] : groups) {
        if (entries.size() < 2) continue;
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        TypewriterGroup group;
        group.prefix = prefix;
        for (auto& [idx, node] : entries) {
            group.full_text += node.text.text;
            group.chars.push_back(std::move(node));
        }
        result.push_back(std::move(group));
    }
    return result;
}

} // namespace chronon3d::cli
