#include "../compiler_stages.hpp"

namespace chronon3d::compiler::stages {
void build_passes(CompiledProgram& program) {
    program.passes.clear();
    program.passes.push_back({0, PassKind::Composite, 0,
                              static_cast<std::uint32_t>(program.nodes.size()), 0});
}
}
