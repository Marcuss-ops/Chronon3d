#include <chronon3d/motion/element_contract.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <iterator>

namespace chronon3d::motion {
namespace {
    bool finite(const chronon_element_state& state) noexcept {
        for (float value : state.position) if (!std::isfinite(value)) return false;
        for (float value : state.rotation) if (!std::isfinite(value)) return false;
        for (float value : state.scale) if (!std::isfinite(value)) return false;
        return std::isfinite(state.opacity) && std::isfinite(state.blur);
    }

    void consume(const chronon_element_state* states, std::size_t count,
                 compiler::FrameParameterBlock* block,
                 ElementApplyReport& report) noexcept {
        report = {};
        if (!states || count == 0) return;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& state = states[i];
            if (state.struct_size < sizeof(chronon_element_state) ||
                state.layer == 0 || state.element == 0 || !finite(state) ||
                state.opacity < 0.f || state.opacity > 1.f || state.blur < 0.f) {
                ++report.rejected;
                continue;
            }
            bool duplicate = false;
            for (std::size_t earlier = 0; earlier < i; ++earlier) {
                if (states[earlier].layer == state.layer &&
                    states[earlier].element == state.element) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                ++report.rejected;
                continue;
            }
            if (!block) continue;
            const auto layer = std::find_if(block->layers.begin(), block->layers.end(),
                [&](const auto& candidate) { return candidate.id == state.layer; });
            if (layer == block->layers.end()) {
                ++report.unmatched;
                continue;
            }
            auto element = std::find_if(layer->elements.begin(), layer->elements.end(),
                [&](const auto& candidate) { return candidate.id == state.element; });
            if (element == layer->elements.end()) {
                if (layer->kind != compiler::NodeKind::Text) {
                    ++report.unmatched;
                    continue;
                }
                layer->elements.push_back({});
                layer->elements.back().id = state.element;
                element = std::prev(layer->elements.end());
            }
            element->position = {state.position[0], state.position[1], state.position[2]};
            element->rotation = {state.rotation[0], state.rotation[1], state.rotation[2]};
            element->scale = {state.scale[0], state.scale[1], state.scale[2]};
            element->opacity = state.opacity;
            element->blur = state.blur;
            element->active = state.visible != 0;
            ++report.applied;
        }
    }
}

bool validate_element_states(const chronon_element_state* states, std::size_t count,
                             ElementApplyReport* report) noexcept {
    ElementApplyReport local{};
    auto& result = report ? *report : local;
    consume(states, count, nullptr, result);
    return result.accepted();
}

bool apply_element_states(const chronon_element_state* states, std::size_t count,
                          compiler::FrameParameterBlock& block,
                          ElementApplyReport* report) noexcept {
    ElementApplyReport local{};
    auto& result = report ? *report : local;
    consume(states, count, &block, result);
    return result.accepted();
}

}
