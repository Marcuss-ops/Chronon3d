#pragma once

#include "benchmark_args.hpp"
#include "diagnostic_args.hpp"
#include "render_args.hpp"

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/config.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace chronon3d::cli {

struct CliContext;
struct DoctorOptions;

int command_list(const CompositionRegistry& registry);
int command_benchmark_machine(const CliContext& ctx);
int command_benchmark_saturation(const CompositionRegistry& registry, const CliContext& ctx,
                                  const std::string& scene, int duration_sec,
                                  const std::string& report_json = {},
                                  MotionBlurMode motion_blur_mode = MotionBlurMode::Off,
                                  int motion_blur_samples = 8,
                                  graph::BackendPreference backend = graph::BackendPreference::Auto);
int command_daemon(const CompositionRegistry& registry,
                   const std::string& assets_root = "",
                   const std::string& build_command = "",
                   const std::string& socket_path = "",
                   graph::BackendPreference backend = graph::BackendPreference::Auto,
                   std::uint32_t gpu_device_id = Config::kAutoGpuDevice);
int command_info(const CompositionRegistry& registry, const std::string& id);
int command_doctor(const CompositionRegistry& registry, const DoctorOptions& options);
int command_verify(const CompositionRegistry& registry, const std::string& output_dir);
int command_render(const CompositionRegistry& registry, const RenderArgs& args,
                   const CompositionProps& props = {});
int command_bench_convert(const CompositionRegistry& registry, const BenchConvertArgs& args);
int command_bench(const CompositionRegistry& registry, const BenchArgs& args);
int command_graph(const CompositionRegistry& registry, const GraphArgs& args);
int command_batch(const CompositionRegistry& registry, const std::vector<std::string>& job_specs);
int command_telemetry(const TelemetryArgs& args);
int command_preflight(const CompositionRegistry& registry, const PreflightArgs& args, AssetRegistry& assets);
int command_watch(const CompositionRegistry& registry, const WatchArgs& args);
int command_preview(const CompositionRegistry& registry, const PreviewArgs& args);
int command_bake_layer(const CompositionRegistry& registry, const BakeLayerArgs& args);
int command_camera_path(const CompositionRegistry& registry, const CameraPathArgs& args);
int command_inspect_text(const CompositionRegistry& registry, const InspectTextArgs& args);
int command_text_def_inspect(const CompositionRegistry& registry, const TextDefInspectArgs& args);
int command_schema(const CompositionRegistry& registry, const SchemaArgs& args);
int command_example_props(const CompositionRegistry& registry, const ExamplePropsArgs& args);
int command_validate(const CompositionRegistry& registry, const ValidateArgs& args);
int command_resolve(const CompositionRegistry& registry, const ResolveArgs& args);

} // namespace chronon3d::cli
