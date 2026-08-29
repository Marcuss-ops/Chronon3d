#pragma once

#include <chronon3d/media/video/native_frame_importer.hpp>

#include <memory>

namespace chronon3d::graph {
class RenderBackend;
}

namespace chronon3d::runtime {
class RenderSurfaceRegistry;
}

namespace chronon3d::media {

/// Create the native-frame importer for the selected FullGraph backend.
/// Backend-specific implementations live with that backend; callers such as
/// Direct-YUV orchestration do not include or name Vulkan importer types.
[[nodiscard]] std::shared_ptr<NativeFrameImporter>
create_native_frame_importer_for_backend(
    graph::RenderBackend& backend,
    runtime::RenderSurfaceRegistry& registry,
    void* cuda_context);

} // namespace chronon3d::media
