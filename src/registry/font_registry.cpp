#include <chronon3d/registry/font_registry.hpp>
#include <algorithm>
#include <cmath>

namespace chronon3d {

void FontRegistry::register_font(const FontDescriptor& desc) {
    auto& inst = instance();
    std::lock_guard<std::mutex> lock(inst.m_mutex);
    
    // Check if already registered
    auto it = std::find_if(inst.m_fonts.begin(), inst.m_fonts.end(), [&](const FontDescriptor& f) {
        return f.family == desc.family && f.weight == desc.weight && f.style == desc.style;
    });

    if (it != inst.m_fonts.end()) {
        it->path = desc.path;
    } else {
        inst.m_fonts.push_back(desc);
    }
}

std::string FontRegistry::resolve(const std::string& family, int weight, const std::string& style) {
    auto& inst = instance();
    std::lock_guard<std::mutex> lock(inst.m_mutex);

    if (inst.m_fonts.empty()) {
        return "";
    }

    // 1. Exact Match
    for (const auto& f : inst.m_fonts) {
        if (f.family == family && f.weight == weight && f.style == style) {
            return f.path;
        }
    }

    // 2. Family + Style Match (closest weight)
    const FontDescriptor* best_match = nullptr;
    int best_diff = 10000;
    for (const auto& f : inst.m_fonts) {
        if (f.family == family && f.style == style) {
            int diff = std::abs(f.weight - weight);
            if (diff < best_diff) {
                best_diff = diff;
                best_match = &f;
            }
        }
    }
    if (best_match) return best_match->path;

    // 3. Family Match (closest weight/style)
    best_match = nullptr;
    best_diff = 10000;
    for (const auto& f : inst.m_fonts) {
        if (f.family == family) {
            int diff = std::abs(f.weight - weight);
            if (f.style != style) diff += 100; // penalty for wrong style
            if (diff < best_diff) {
                best_diff = diff;
                best_match = &f;
            }
        }
    }
    if (best_match) return best_match->path;

    return "";
}

void FontRegistry::clear() {
    auto& inst = instance();
    std::lock_guard<std::mutex> lock(inst.m_mutex);
    inst.m_fonts.clear();
}

} // namespace chronon3d
