// RenderGraphContext framebuffer/context implementation is split by
// responsibility while remaining one translation unit. This preserves the
// existing linkage and CMake target boundary.
#include "framebuffer_acquire_owned_detail.hpp"
#include "framebuffer_acquire_shared_detail.hpp"
#include "render_graph_context_clone_detail.hpp"
