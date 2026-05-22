#pragma once

#include <chronon3d/core/render_telemetry.hpp>
#include <nlohmann/json.hpp>
#include <string>

namespace chronon3d::telemetry {

nlohmann::json serialize_run(const RenderTelemetryRecord& run);
nlohmann::json serialize_frame(const std::string& run_id, const FrameTelemetryRecord& frame);
nlohmann::json serialize_phase(const std::string& run_id, const PhaseTelemetryRecord& phase);
nlohmann::json serialize_counter(const std::string& run_id, const CounterTelemetryRecord& cnt);
nlohmann::json serialize_node_event(const std::string& run_id, const NodeTelemetryRecord& ev);
nlohmann::json serialize_layer_event(const std::string& run_id, const LayerTelemetryRecord& ev);
nlohmann::json serialize_cache_event(const std::string& run_id, const CacheTelemetryRecord& ev);
nlohmann::json serialize_culling_event(const std::string& run_id, const CullingTelemetryRecord& ev);
nlohmann::json serialize_text_event(const std::string& run_id, const TextTelemetryRecord& ev);
nlohmann::json serialize_image_event(const std::string& run_id, const ImageTelemetryRecord& ev);
nlohmann::json serialize_tile_event(const std::string& run_id, const TileTelemetryRecord& ev);

} // namespace chronon3d::telemetry
