#pragma once

#include <chronon3d/backends/vulkan/vulkan_backend.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>

namespace chronon3d::backends::vulkan::native_shape_dispatch_detail {

bool try_native_path_fill(VulkanBackend&, Framebuffer&, const RenderNode&, const RenderState&);
bool try_native_path_stroke(VulkanBackend&, Framebuffer&, const RenderNode&, const RenderState&);
bool try_native_grid(VulkanBackend&, Framebuffer&, const RenderNode&, const RenderState&);
bool try_native_solid_rect(VulkanBackend&, Framebuffer&, const RenderNode&, const RenderState&);

} // namespace chronon3d::backends::vulkan::native_shape_dispatch_detail
