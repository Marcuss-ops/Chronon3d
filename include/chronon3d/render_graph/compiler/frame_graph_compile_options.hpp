#pragma once

namespace chronon3d::graph {

struct FrameGraphCompileOptions {
    bool run_optimizer{true};
    bool compute_lifetimes{true};
    bool compute_bboxes{true};
    bool validate_dag{true};
    bool include_diagnostics{false};
};

} // namespace chronon3d::graph
