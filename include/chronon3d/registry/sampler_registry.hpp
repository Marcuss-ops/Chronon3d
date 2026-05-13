#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <functional>

namespace chronon3d::registry {

enum class SamplerKind {
    Nearest,
    Bilinear,
    Bicubic,
    Lanczos,
};

[[nodiscard]] constexpr std::string_view to_string(SamplerKind kind) {
    switch (kind) {
    case SamplerKind::Nearest:  return "nearest";
    case SamplerKind::Bilinear: return "bilinear";
    case SamplerKind::Bicubic:  return "bicubic";
    case SamplerKind::Lanczos:  return "lanczos";
    }
    return "nearest";
}

struct SamplerDescriptor {
    std::string id;
    std::string display_name;
    SamplerKind kind{SamplerKind::Nearest};
    std::string description;
    bool        builtin{false};
};

class SamplerRegistry {
public:
    SamplerRegistry();

    void register_sampler(SamplerDescriptor descriptor);

    [[nodiscard]] bool contains(std::string_view id) const;
    [[nodiscard]] const SamplerDescriptor& get(std::string_view id) const;
    [[nodiscard]] std::vector<std::string> available() const;
    [[nodiscard]] std::vector<SamplerDescriptor> list() const;

private:
    std::map<std::string, SamplerDescriptor, std::less<>> m_samplers;
};

} // namespace chronon3d::registry
