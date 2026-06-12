#pragma once

#include "exporter.hpp"
#include <unordered_map>
#include <memory>
#include <string_view>
#include <vector>

namespace chronon3d::cli {

/// Registry of available VideoExporters.
///
/// Usage:
///   ExporterRegistry reg;
///   register_builtin_exporters(reg);
///   auto* ex = reg.find("pipe");
///   if (ex) ex->export_video(job);
class ExporterRegistry {
public:
    /// Register an exporter.  Replaces any existing exporter with the same id.
    void register_exporter(std::unique_ptr<VideoExporter> exporter);

    /// Look up an exporter by id.  Returns nullptr if not found.
    VideoExporter* find(std::string_view id) const;

    /// List all registered exporter ids.
    std::vector<std::string_view> list_ids() const;

    /// Number of registered exporters.
    size_t size() const { return exporters_.size(); }

private:
    std::unordered_map<std::string, std::unique_ptr<VideoExporter>> exporters_;
};

// ── Null/benchmark sink registration ────────────────────────────────────────
void register_null_render_sink(ExporterRegistry& registry);
void register_null_convert_sink(ExporterRegistry& registry);

} // namespace chronon3d::cli
