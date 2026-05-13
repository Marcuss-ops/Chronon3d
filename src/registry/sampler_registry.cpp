#include <chronon3d/registry/sampler_registry.hpp>

#include <stdexcept>
#include <utility>

namespace chronon3d::registry {

namespace {

void register_builtin_samplers(SamplerRegistry& registry) {
    registry.register_sampler(SamplerDescriptor{
        .id = "sampler.nearest",
        .display_name = "Nearest",
        .kind = SamplerKind::Nearest,
        .description = "Point sampling",
        .builtin = true,
    });
    registry.register_sampler(SamplerDescriptor{
        .id = "sampler.bilinear",
        .display_name = "Bilinear",
        .kind = SamplerKind::Bilinear,
        .description = "Linear interpolation across texels",
        .builtin = true,
    });
    registry.register_sampler(SamplerDescriptor{
        .id = "sampler.bicubic",
        .display_name = "Bicubic",
        .kind = SamplerKind::Bicubic,
        .description = "Cubic reconstruction filter",
        .builtin = true,
    });
    registry.register_sampler(SamplerDescriptor{
        .id = "sampler.lanczos",
        .display_name = "Lanczos",
        .kind = SamplerKind::Lanczos,
        .description = "High-quality sinc-style reconstruction",
        .builtin = true,
    });
}

} // namespace

SamplerRegistry::SamplerRegistry() {
    register_builtin_samplers(*this);
}

void SamplerRegistry::register_sampler(SamplerDescriptor descriptor) {
    if (descriptor.id.empty()) {
        throw std::runtime_error("Sampler id cannot be empty");
    }
    if (m_samplers.contains(descriptor.id)) {
        throw std::runtime_error("Duplicate sampler: " + descriptor.id);
    }
    if (descriptor.display_name.empty()) {
        descriptor.display_name = descriptor.id;
    }
    const std::string id = descriptor.id;
    m_samplers.emplace(id, std::move(descriptor));
}

bool SamplerRegistry::contains(std::string_view id) const {
    return m_samplers.find(id) != m_samplers.end();
}

const SamplerDescriptor& SamplerRegistry::get(std::string_view id) const {
    auto it = m_samplers.find(id);
    if (it == m_samplers.end()) {
        throw std::runtime_error("Unknown sampler: " + std::string{id});
    }
    return it->second;
}

std::vector<std::string> SamplerRegistry::available() const {
    std::vector<std::string> ids;
    ids.reserve(m_samplers.size());
    for (const auto& [id, _] : m_samplers) {
        ids.push_back(id);
    }
    return ids;
}

std::vector<SamplerDescriptor> SamplerRegistry::list() const {
    std::vector<SamplerDescriptor> descriptors;
    descriptors.reserve(m_samplers.size());
    for (const auto& [_, descriptor] : m_samplers) {
        descriptors.push_back(descriptor);
    }
    return descriptors;
}

} // namespace chronon3d::registry
