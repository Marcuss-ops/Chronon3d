#include <chronon3d/render_graph/render_graph_context.hpp>

namespace chronon3d::graph {

void DofSourceCoverage::reset() noexcept {
    source_bbox.reset();
    max_radius = 0.0f;
}

const std::string& FrameInput::assets_root() const noexcept {
    static const std::string kEmptyAssetsRoot;
    return static_context ? static_context->assets_root : kEmptyAssetsRoot;
}

bool RenderPolicy::is_gpu_native_required() const noexcept {
    return require_native_gpu ||
           gpu_hot_path_mode == GpuHotPathMode::RequireGpuNative ||
           gpu_hot_path_mode == GpuHotPathMode::RequireDirectYuv;
}

NodeExecutionContext::NativeDofDepthState& NodeExecutionContext::native_dof_state() noexcept {
    return shared_native_dof_depth ? *shared_native_dof_depth : native_dof_depth;
}

std::vector<float>& NodeExecutionContext::dof_depth_buffer() noexcept {
    return shared_dof_depth ? *shared_dof_depth : dof_depth;
}

const std::vector<float>& NodeExecutionContext::dof_depth_buffer() const noexcept {
    return shared_dof_depth ? *shared_dof_depth : dof_depth;
}

DofSourceCoverage& NodeExecutionContext::dof_sources() noexcept {
    return shared_dof_source_coverage ? *shared_dof_source_coverage : dof_source_coverage;
}

const DofSourceCoverage& NodeExecutionContext::dof_sources() const noexcept {
    return shared_dof_source_coverage ? *shared_dof_source_coverage : dof_source_coverage;
}

RenderBackend* RenderGraphContext::execution_backend() const noexcept {
    return backend;
}

std::shared_ptr<cache::FramebufferPool>
RenderGraphContext::execution_framebuffer_pool() const noexcept {
    return framebuffer_pool;
}

cache::NodeCache* RenderGraphContext::execution_node_cache() const noexcept {
    return node_cache;
}

} // namespace chronon3d::graph
