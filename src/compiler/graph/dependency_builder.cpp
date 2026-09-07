#include "../compiler_stages.hpp"

namespace chronon3d::compiler::stages {
void build_dependencies(CompiledProgram& program) {
    for (auto& node : program.nodes) {
        node.input_begin = 0;
        node.input_count = 0;
    }
}
}
