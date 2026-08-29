#include <chronon3d/media/video/hw_frame_ref.hpp>

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
extern "C" {
#include <libavutil/frame.h>
}
#endif

namespace chronon3d::media {

#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG

HwFrameRef::HwFrameRef(AVFrame* frame) noexcept : frame_(frame) {}

HwFrameRef::HwFrameRef(const HwFrameRef& other) {
    if (other.frame_) {
        // Shares the underlying frame buffers (refcounted by FFmpeg); never
        // deep-copies pixel data.
        frame_ = av_frame_clone(other.frame_);
    }
}

HwFrameRef& HwFrameRef::operator=(const HwFrameRef& other) {
    if (this != &other) {
        HwFrameRef copy(other);
        *this = std::move(copy);
    }
    return *this;
}

HwFrameRef::HwFrameRef(HwFrameRef&& other) noexcept : frame_(other.frame_) {
    other.frame_ = nullptr;
}

HwFrameRef& HwFrameRef::operator=(HwFrameRef&& other) noexcept {
    if (this != &other) {
        release();
        frame_ = other.frame_;
        other.frame_ = nullptr;
    }
    return *this;
}

HwFrameRef::~HwFrameRef() {
    release();
}

void HwFrameRef::release() noexcept {
    if (frame_) {
        av_frame_free(&frame_);
    }
}

void HwFrameRef::reset() noexcept {
    release();
}

#else
// This translation unit is only built when native FFmpeg is enabled
// (chronon3d_media_native); the class stays null-owning otherwise so the
// header remains includable on any configuration.
HwFrameRef::HwFrameRef(AVFrame* frame) noexcept : frame_(frame) {}
HwFrameRef::HwFrameRef(const HwFrameRef&) {}
HwFrameRef& HwFrameRef::operator=(const HwFrameRef&) { return *this; }
HwFrameRef::HwFrameRef(HwFrameRef&& other) noexcept : frame_(other.frame_) {
    other.frame_ = nullptr;
}
HwFrameRef& HwFrameRef::operator=(HwFrameRef&& other) noexcept {
    if (this != &other) {
        frame_ = other.frame_;
        other.frame_ = nullptr;
    }
    return *this;
}
HwFrameRef::~HwFrameRef() {}
void HwFrameRef::release() noexcept {}
void HwFrameRef::reset() noexcept { frame_ = nullptr; }
#endif

} // namespace chronon3d::media