#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/time.hpp>
#include <chronon3d/core/gpu_hot_path_mode.hpp>
#include <chronon3d/media/video/native_frame_importer.hpp>

#include <memory>
#include <string>

namespace chronon3d {
struct RenderCounters;
}

namespace chronon3d::media {

class MediaFrameProvider {
public:
    virtual ~MediaFrameProvider() = default;

    virtual void set_counters(::chronon3d::RenderCounters*) {}
    virtual void set_native_frame_importer(
        std::shared_ptr<NativeFrameImporter> /*importer*/) {}
    virtual void set_gpu_hot_path_mode(GpuHotPathMode /*mode*/) {}

    /// Exact presentation-time entry point. PTS-native providers override this
    /// method; the default keeps legacy providers source-compatible.
    virtual std::shared_ptr<Framebuffer> decode_frame_at(
        const std::string& /*path*/,
        RationalTime /*presentation_time*/,
        int /*width*/,
        int /*height*/) {
        return nullptr;
    }

    /// Legacy frame/fps boundary retained for existing graph callers. Native
    /// media implementations must treat it as an adapter only; source sample
    /// selection belongs to decode_frame_at().
    virtual std::shared_ptr<Framebuffer> decode_frame(
        const std::string& path,
        Frame frame,
        int width,
        int height,
        float fps) = 0;
};

} // namespace chronon3d::media
