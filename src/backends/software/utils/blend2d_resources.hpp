#pragma once

#include <blend2d.h>
#include <mutex>
#include <unordered_map>
#include <string>
#include <spdlog/spdlog.h>

namespace chronon3d::blend2d_utils {

struct Blend2DResources {
    std::unordered_map<std::string, BLFontFace> faces;
    std::mutex mutex;

    static Blend2DResources& instance() {
        static Blend2DResources resources;
        return resources;
    }

    BLFontFace get_face(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = faces.find(path);
        if (it == faces.end()) {
            BLFontFace face;
            if (face.createFromFile(path.c_str()) != BL_SUCCESS) {
                spdlog::error("Blend2D: failed to load font {}", path);
                return BLFontFace();
            }
            faces[path] = face;
            return face;
        }
        return it->second;
    }
};

} // namespace chronon3d::blend2d_utils
