#include "../telemetry_report_sections.hpp"
#include "../command_telemetry_internal.hpp"

#if defined(CHRONON3D_ENABLE_SQLITE_TELEMETRY)
namespace chronon3d::cli::report_sections {

void write_layer_cost_breakdown(std::stringstream& out, const ReportModel& model) {
    out << "## Layer Cost Breakdown\n";
    out << "| Layer | Type | Calls | Time | Visible Pixels | Dirty Pixels | Glyphs | Images |\n";
    out << "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |\n";
    for (const auto& layer : model.layer_cost_breakdown) {
        out << "| " << layer.layer_name << " | "
            << layer.layer_type << " | "
            << layer.calls << " | "
            << format_ms(layer.duration_ms) << " | "
            << layer.visible_pixels << " | "
            << layer.dirty_pixels << " | "
            << layer.glyphs_rasterized << " | "
            << layer.images_sampled << " |\n";
    }
    out << "\n";
}

} // namespace chronon3d::cli::report_sections
#endif
