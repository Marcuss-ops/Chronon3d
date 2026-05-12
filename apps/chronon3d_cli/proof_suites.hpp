#pragma once

#include <chronon3d/core/frame.hpp>
#include <string>
#include <vector>
#include <optional>
#include <string_view>

namespace chronon3d {
namespace cli {

struct ProofFrame {
    std::string composition;
    Frame       frame{0};
    std::string filename;
};

struct ProofSuite {
    std::string             name;
    std::vector<ProofFrame> frames;
};

const std::vector<ProofSuite>& proof_suites();
std::optional<ProofSuite>      find_proof_suite(std::string_view name);

} // namespace cli
} // namespace chronon3d
