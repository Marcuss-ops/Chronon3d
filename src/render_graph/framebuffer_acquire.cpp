// RenderGraphContext framebuffer/context implementation is split by
// responsibility while remaining one translation unit. This preserves the
// existing linkage and CMake target boundary.
#include "framebuffer_acquire_owned.inc"
#include "framebuffer_acquire_shared.inc"
#include "render_graph_context_clone.inc"
