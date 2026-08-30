#include <chronon3d/media/video/hw_frame_ref.hpp>

namespace chronon3d::media {

HwFrameRef::HwFrameRef(AVFrame* frame) noexcept : frame_(frame) {}
HwFrameRef::HwFrameRef(const HwFrameRef&) = default;
HwFrameRef& HwFrameRef::operator=(const HwFrameRef&) = default;
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
HwFrameRef::~HwFrameRef() = default;
void HwFrameRef::release() noexcept { frame_ = nullptr; }
void HwFrameRef::reset() noexcept { frame_ = nullptr; }

} // namespace chronon3d::media
