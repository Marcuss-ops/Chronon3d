#pragma once

#include <string>
#include <string_view>

namespace chronon3d::runtime {

enum class LaneDecision {
    Selected,
    FallbackUnsupported,
    Failed
};

struct LaneReason {
    LaneDecision decision{LaneDecision::Selected};
    // structured: "unsupported_feature" + "layer_2_5d" — not free text
    std::string_view category{"none"};   // unsupported_feature | error | none
    std::string_view detail{"none"};     // layer_2_5d | mask | transform_3d | upload_failed | ...

    [[nodiscard]] std::string to_string() const {
        if (category.empty() || category == "none") {
            return std::string(detail);
        }
        if (detail.empty() || detail == "none") {
            return std::string(category);
        }
        return std::string(category) + ":" + std::string(detail);
    }
};

} // namespace chronon3d::runtime
