#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace chronon3d {

struct FontDescriptor {
    std::string family;
    int weight{400};
    std::string style{"normal"};
    std::string path;
};

class FontRegistry {
public:
    static FontRegistry& instance() {
        static FontRegistry inst;
        return inst;
    }

    static void register_font(const FontDescriptor& desc);
    static std::string resolve(const std::string& family, int weight = 400, const std::string& style = "normal");
    static void clear();

private:
    FontRegistry() = default;
    ~FontRegistry() = default;
    FontRegistry(const FontRegistry&) = delete;
    FontRegistry& operator=(const FontRegistry&) = delete;

    std::vector<FontDescriptor> m_fonts;
    std::mutex m_mutex;
};

} // namespace chronon3d
