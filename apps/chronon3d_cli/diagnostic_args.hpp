#pragma once

#include <chronon3d/core/types/frame.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace chronon3d::cli {

struct GraphArgs { std::string comp_id; std::string assets_root; Frame frame{0}; std::string output; bool summary{false}; bool plan{false}; };
struct TelemetryArgs { std::string run_id; std::string output_file; };
struct TelemetryExportArgs { std::string run_id; std::string output_file; };
struct PreflightArgs { std::string comp_id; Frame start{0}; Frame end{0}; int sample_step{1}; std::string output; std::string json_file; std::string assets_root; };
struct WatchArgs { std::string comp_id; int frame{0}; std::filesystem::path output{"/tmp/preview.png"}; std::vector<std::filesystem::path> watch_dirs; std::string build_command{"bash build-fast.sh"}; int poll_ms{500}; std::string chronon_binary; bool no_build{false}; std::string props_file; };
struct InspectTextArgs { std::string comp_id; Frame frame{0}; bool json{true}; };
struct TextDefInspectArgs { std::string comp_id; std::string assets_root; Frame frame{0}; std::string json_output; };
struct SchemaArgs { std::string comp_id; bool json{true}; };
struct ExamplePropsArgs { std::string comp_id; bool json{true}; };
struct ValidateArgs { std::string comp_id; std::string plan_file; std::string props_file; std::string props_json; bool json{true}; std::string assets_root; };
struct ResolveArgs { std::string comp_id; std::string props_file; std::string props_json; bool json{true}; };

} // namespace chronon3d::cli
