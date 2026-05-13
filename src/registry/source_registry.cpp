#include <chronon3d/registry/source_registry.hpp>

#include <stdexcept>
#include <utility>

namespace chronon3d::registry {

namespace {

void register_builtin_sources(SourceRegistry& registry) {
    registry.register_source(SourceDescriptor{
        .id = "source.shape",
        .display_name = "Shape Source",
        .kind = SourceKind::Shape,
        .description = "Rasterizes primitive and path-based shapes",
        .builtin = true,
    });
    registry.register_source(SourceDescriptor{
        .id = "source.text",
        .display_name = "Text Source",
        .kind = SourceKind::Text,
        .description = "Shapes text into a raster source",
        .builtin = true,
    });
    registry.register_source(SourceDescriptor{
        .id = "source.image",
        .display_name = "Image Source",
        .kind = SourceKind::Image,
        .description = "Loads and samples bitmap images",
        .builtin = true,
    });
    registry.register_source(SourceDescriptor{
        .id = "source.video",
        .display_name = "Video Source",
        .kind = SourceKind::Video,
        .description = "Decodes frames from a video clip",
        .builtin = true,
        .temporal = true,
    });
    registry.register_source(SourceDescriptor{
        .id = "source.layer",
        .display_name = "Layer Source",
        .kind = SourceKind::Layer,
        .description = "Evaluates a layer subtree",
        .builtin = true,
    });
    registry.register_source(SourceDescriptor{
        .id = "source.precomp",
        .display_name = "Precomp Source",
        .kind = SourceKind::Precomp,
        .description = "Renders a nested composition",
        .builtin = true,
    });
    registry.register_source(SourceDescriptor{
        .id = "source.adjustment",
        .display_name = "Adjustment Source",
        .kind = SourceKind::Adjustment,
        .description = "Applies effects over everything below",
        .builtin = true,
    });
}

} // namespace

SourceRegistry::SourceRegistry() {
    register_builtin_sources(*this);
}

void SourceRegistry::register_source(SourceDescriptor descriptor) {
    if (descriptor.id.empty()) {
        throw std::runtime_error("Source id cannot be empty");
    }
    if (m_sources.contains(descriptor.id)) {
        throw std::runtime_error("Duplicate source: " + descriptor.id);
    }
    if (descriptor.display_name.empty()) {
        descriptor.display_name = descriptor.id;
    }
    const std::string id = descriptor.id;
    m_sources.emplace(id, std::move(descriptor));
}

bool SourceRegistry::contains(std::string_view id) const {
    return m_sources.find(id) != m_sources.end();
}

const SourceDescriptor& SourceRegistry::get(std::string_view id) const {
    auto it = m_sources.find(id);
    if (it == m_sources.end()) {
        throw std::runtime_error("Unknown source: " + std::string{id});
    }
    return it->second;
}

std::vector<std::string> SourceRegistry::available() const {
    std::vector<std::string> ids;
    ids.reserve(m_sources.size());
    for (const auto& [id, _] : m_sources) {
        ids.push_back(id);
    }
    return ids;
}

std::vector<SourceDescriptor> SourceRegistry::list() const {
    std::vector<SourceDescriptor> descriptors;
    descriptors.reserve(m_sources.size());
    for (const auto& [_, descriptor] : m_sources) {
        descriptors.push_back(descriptor);
    }
    return descriptors;
}

} // namespace chronon3d::registry
