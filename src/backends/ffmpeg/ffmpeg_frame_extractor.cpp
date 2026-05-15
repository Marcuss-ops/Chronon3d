#include <chronon3d/backends/ffmpeg/ffmpeg_frame_extractor.hpp>
#include <chronon3d/backends/image/image_loader.hpp>
#include <filesystem>
#include <cstdlib>
#include <sstream>

namespace chronon3d::video {

FfmpegFrameExtractor::FfmpegFrameExtractor(std::filesystem::path temp_dir)
    : m_temp_dir(std::move(temp_dir)) {
    std::filesystem::create_directories(m_temp_dir);
}

std::filesystem::path FfmpegFrameExtractor::frame_path_for(
    const std::string& path,
    Frame frame,
    i32 width,
    i32 height
) const {
    const auto h = std::hash<std::string>{}(path);
    std::ostringstream name;
    name << "v_" << h << "_f" << frame << "_" << width << "x" << height << ".png";
    return m_temp_dir / name.str();
}

std::shared_ptr<Framebuffer> FfmpegFrameExtractor::decode_frame(
    const std::string& path,
    Frame source_frame,
    i32 width,
    i32 height,
    f32 fps
) {
    VideoFrameKey key{
        .path = path,
        .frame = source_frame,
        .width = width,
        .height = height
    };

    if (auto cached = m_cache.find(key)) {
        return cached;
    }

    auto out_path = frame_path_for(path, source_frame, width, height);

    if (!std::filesystem::exists(out_path)) {
        const double seconds = static_cast<double>(source_frame) / static_cast<double>(fps);

        std::ostringstream cmd;
        cmd
            << "ffmpeg -y "
            << "-ss " << seconds << " "
            << "-i \"" << path << "\" "
            << "-frames:v 1 "
            << "-vf \"scale=" << width << ":" << height << ":force_original_aspect_ratio=decrease,"
            << "pad=" << width << ":" << height << ":(ow-iw)/2:(oh-ih)/2:color=black@0\" "
            << "\"" << out_path.string() << "\" "
            << "> /dev/null 2>&1";

        int code = std::system(cmd.str().c_str());
        if (code != 0 || !std::filesystem::exists(out_path)) {
            auto empty = std::make_shared<Framebuffer>(width, height);
            empty->clear(Color::transparent());
            return empty;
        }
    }

    auto fb = io::load_image_as_framebuffer(out_path.string(), width, height);

    if (fb) {
        m_cache.store(std::move(key), fb);
    } else {
        fb = std::make_shared<Framebuffer>(width, height);
        fb->clear(Color::transparent());
    }
    
    return fb;
}

void FfmpegFrameExtractor::clear_memory_cache() {
    m_cache.clear();
}

} // namespace chronon3d::video
