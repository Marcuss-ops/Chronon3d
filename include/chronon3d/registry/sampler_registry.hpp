#pragma once

#include <chronon3d/core/enum_utils.hpp>
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

[[nodiscard]] inline std::string to_string(SamplerKind kind) {
    return enum_utils::enum_name_lower_snake(kind);
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
