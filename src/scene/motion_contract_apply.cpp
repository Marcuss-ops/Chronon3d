// ==============================================================================
// src/scene/motion_contract_apply.cpp — ADR-032 consume half.
//
// Pure, allocation-free, deterministic: the same records applied to the same
// block produce the same block, in any evaluation order.
// ==============================================================================

#include <chronon3d/motion/contract_apply.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace chronon3d::motion {

namespace {

/// Largest value a layer record can address: NodeId is 32 bits, the contract's
/// layer id is 64, and the binding is the widening.
constexpr std::uint64_t kMaxAddressableLayer =
        static_cast<std::uint64_t>(std::numeric_limits<compiler::NodeId>::max());

/// The ABI stores the product row-major with column vectors; GLM is
/// column-major. This is the exact inverse of the bridge's pack_row_major, so
/// the two halves round-trip bit-for-bit.
void unpack_row_major(const float (&in)[16], Mat4& out) noexcept {
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            out[col][row] = in[row * 4 + col];
        }
    }
}

/// A non-finite transform would poison every downstream bounds/sort decision
/// without raising anything, so it is refused at the boundary instead.
bool finite_matrix(const float (&in)[16]) noexcept {
    for (const float value : in) {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

/// The one place the record policy is written. `block` null means "validate
/// only"; the caller decides what a refusal means, this decides what a refusal
/// is.
void consume(const chronon_layer_state* states, std::size_t count,
             compiler::FrameParameterBlock* block,
             ContractApplyReport& counts) noexcept {
    counts = ContractApplyReport{};
    if (states == nullptr || count == 0u) {
        return;
    }

    constexpr std::size_t kKnownSize = sizeof(chronon_layer_state);

    for (std::size_t i = 0; i < count; ++i) {
        const chronon_layer_state& state = states[i];

        // A record compiled against an older (smaller) record cannot be read
        // safely; a larger one is a newer producer's record and its known prefix
        // still is, which is the tolerance the C ABI's out-structs extend in the
        // other direction.
        if (state.struct_size < kKnownSize || state.layer == 0u ||
            state.layer > kMaxAddressableLayer ||
            !finite_matrix(state.clip_from_local)) {
            ++counts.rejected;
            continue;
        }

        // The input is a set: a repeated layer id is refused rather than
        // resolved to a silent winner. Scanning backwards keeps this
        // allocation-free, which the frame path depends on; layer counts per
        // frame are small.
        bool repeated = false;
        for (std::size_t earlier = 0; earlier < i; ++earlier) {
            if (states[earlier].layer == state.layer) {
                repeated = true;
                break;
            }
        }
        if (repeated) {
            ++counts.rejected;
            continue;
        }

        if (block == nullptr) {
            continue;  // validated only; matching needs a frame
        }

        const auto id = static_cast<compiler::NodeId>(state.layer);
        const auto layer = std::find_if(
                block->layers.begin(), block->layers.end(),
                [id](const compiler::SampledLayerParameters& candidate) {
                    return candidate.id == id;
                });
        if (layer == block->layers.end()) {
            ++counts.unmatched;
            continue;
        }

        unpack_row_major(state.clip_from_local, layer->matrix);
        layer->opacity = std::clamp(state.opacity, 0.0f, 1.0f);
        layer->active = state.visible != 0u;
        ++counts.applied;
        if (state.visible == 0u) {
            ++counts.deactivated;
        }
    }
}

}  // namespace

bool validate_layer_states(const chronon_layer_state* states, std::size_t count,
                           ContractApplyReport* report) noexcept {
    ContractApplyReport local{};
    ContractApplyReport& counts = report ? *report : local;
    consume(states, count, nullptr, counts);
    return counts.accepted();
}

bool apply_layer_states(const chronon_layer_state* states, std::size_t count,
                        compiler::FrameParameterBlock& block,
                        ContractApplyReport* report) noexcept {
    ContractApplyReport local{};
    ContractApplyReport& counts = report ? *report : local;
    consume(states, count, &block, counts);
    return counts.accepted();
}

}  // namespace chronon3d::motion
