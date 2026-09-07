#include <chronon3d/compiler/compiled_program.hpp>
#include <chronon3d/compiler/compile_error.hpp>

#include <limits>
#include <vector>

namespace chronon3d::compiler {
std::vector<CompileError> validate_compiled_program(const CompiledProgram& program) {
    std::vector<CompileError> errors;
    for (const auto& node : program.nodes) {
        if (node.input_count > std::numeric_limits<std::uint32_t>::max() - node.input_begin) {
            errors.push_back({CompileErrorCode::InvalidProgram, "nodes", "node input range overflows", "validator"});
        }
    }
    for (const auto& pass : program.passes) {
        if (pass.node_count > std::numeric_limits<std::uint32_t>::max() - pass.node_begin ||
            pass.node_begin + pass.node_count > program.nodes.size()) {
            errors.push_back({CompileErrorCode::InvalidProgram, "passes", "pass node range is invalid", "validator"});
        }
    }
    return errors;
}
}
