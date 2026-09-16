#include <chronon3d/media/video/direct_yuv_program.hpp>

namespace chronon3d::media::video {

DirectYuvProgram::CapabilityReport DirectYuvProgram::resolve_support(
    const CompiledComposition&, DirectYuvPolicy) {
    CapabilityReport report;
    report.reason = "DirectYUV requires native FFmpeg support";
    return report;
}

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
