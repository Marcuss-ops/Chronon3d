#include <chronon3d/media/video/direct_yuv_program.hpp>

namespace chronon3d::media::video {

std::shared_ptr<DirectYuvProgram> DirectYuvProgram::prepare(
    const CompiledComposition&,
    ImageCache&,
    std::shared_ptr<::chronon3d::media::VideoDeviceRuntime>,
    const assets::AssetResolver&,
    std::string& reason) {
    reason = "DirectYUV requires native FFmpeg support";
    return nullptr;
}

std::shared_ptr<DirectYuvFrame> DirectYuvProgram::execute(
    ::chronon3d::media::NativeVideoFrameDecoder&, Frame) {
    return nullptr;
}

} // namespace chronon3d::media::video
