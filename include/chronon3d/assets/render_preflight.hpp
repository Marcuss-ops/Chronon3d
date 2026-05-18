#pragma once

#include <string>
#include <vector>
#include <stdexcept>

namespace chronon3d {

struct RequiredFont {
    std::string family;
    int weight{400};
    std::string style{"normal"};
};

class ChrononAssetError : public std::runtime_error {
public:
    explicit ChrononAssetError(const std::string& message) : std::runtime_error(message) {}
};

class RenderPreflight {
public:
    static RenderPreflight& instance() {
        static RenderPreflight inst;
        return inst;
    }

    void require_font(const std::string& family, int weight = 400, const std::string& style = "normal");
    void require_image(const std::string& path);
    void require_video(const std::string& path);
    
    void validate_or_throw();
    void clear();

private:
    RenderPreflight() = default;
    ~RenderPreflight() = default;
    RenderPreflight(const RenderPreflight&) = delete;
    RenderPreflight& operator=(const RenderPreflight&) = delete;

    std::vector<RequiredFont> m_required_fonts;
    std::vector<std::string> m_required_images;
    std::vector<std::string> m_required_videos;
};

} // namespace chronon3d
