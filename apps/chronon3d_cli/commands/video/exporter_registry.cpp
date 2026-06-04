#include "exporter_registry.hpp"

namespace chronon3d::cli {

void ExporterRegistry::register_exporter(std::unique_ptr<VideoExporter> exporter) {
    if (!exporter) return;
    exporters_[std::string(exporter->id())] = std::move(exporter);
}

VideoExporter* ExporterRegistry::find(std::string_view id) const {
    auto it = exporters_.find(std::string(id));
    return (it != exporters_.end()) ? it->second.get() : nullptr;
}

std::vector<std::string_view> ExporterRegistry::list_ids() const {
    std::vector<std::string_view> ids;
    ids.reserve(exporters_.size());
    for (const auto& kv : exporters_) {
        ids.push_back(kv.first);
    }
    return ids;
}

// ── Register all built-in exporters ───────────────────────────────────────────

void register_builtin_exporters(ExporterRegistry& registry) {
    register_pipe_exporter(registry);
    register_chunked_exporter(registry);
}

} // namespace chronon3d::cli
