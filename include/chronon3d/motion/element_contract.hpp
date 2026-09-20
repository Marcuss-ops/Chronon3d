#pragma once

#include <chronon/abi.h>
#include <chronon3d/compiler/frame_parameters.hpp>

#include <cstddef>

namespace chronon3d::motion {

struct ElementApplyReport {
    unsigned applied{0};
    unsigned unmatched{0};
    unsigned rejected{0};
    [[nodiscard]] bool accepted() const noexcept { return rejected == 0; }
};

[[nodiscard]] bool validate_element_states(const chronon_element_state* states,
                                           std::size_t count,
                                           ElementApplyReport* report = nullptr) noexcept;

[[nodiscard]] bool apply_element_states(const chronon_element_state* states,
                                        std::size_t count,
                                        compiler::FrameParameterBlock& block,
                                        ElementApplyReport* report = nullptr) noexcept;

}
