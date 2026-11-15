#include "../../commands.hpp"
#include "../../utils/job/cli_render_utils.hpp"
#include "doctor_report.hpp"

#include <chronon3d/simd/cpu_isa.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <cstdlib>
#include <iostream>
#include <spdlog/spdlog.h>

namespace chronon3d {
namespace cli {

int command_doctor(const CompositionRegistry& /*registry*/,
                   const DoctorOptions& options) {
    const DoctorReport report = run_doctor(options);

    if (options.json) {
        nlohmann::json checks = nlohmann::json::array();
        std::string git_sha = "unknown";
        for (const auto& check : report.checks) {
            checks.push_back({
                {"id", check.id},
                {"status", doctor_status_name(check.status)},
                {"message", check.message},
            });
            if (check.id == "engine.git_sha") {
                git_sha = check.message;
            }
        }
        nlohmann::json caps = {
            {"vulkan",
#ifdef CHRONON3D_ENABLE_VULKAN
             true
#else
             false
#endif
            },
            {"cuda_interop",
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
             true
#else
             false
#endif
            },
            {"direct_yuv",
#if defined(CHRONON3D_ENABLE_CUDA_INTEROP) && defined(CHRONON3D_ENABLE_VULKAN)
             true
#else
             false
#endif
            },
            {"native_ffmpeg",
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
             true
#else
             false
#endif
            },
            {"nvdec",
#ifdef CHRONON3D_ENABLE_NATIVE_FFMPEG
             true
#else
             false
#endif
            },
            {"nvenc", true},
            {"ipc",
#ifdef CHRONON3D_ENABLE_IPC
             true
#else
             false
#endif
            },
            {"build_sha", git_sha}
        };
        const auto cpu = simd::detect_cpu_capabilities();
        const nlohmann::json features = {
            {"blend2d", CHRONON3D_DOCTOR_HAS_BLEND2D != 0},
            {"text", CHRONON3D_DOCTOR_HAS_TEXT != 0},
            {"svg", CHRONON3D_DOCTOR_HAS_SVG != 0},
            {"mesh", CHRONON3D_DOCTOR_HAS_MESH != 0},
            {"ocio", CHRONON3D_DOCTOR_HAS_OCIO != 0},
            {"exr", CHRONON3D_DOCTOR_HAS_EXR != 0},
            {"video", CHRONON3D_DOCTOR_HAS_VIDEO != 0},
            {"vulkan", CHRONON3D_DOCTOR_HAS_VULKAN != 0},
            {"native_ffmpeg", CHRONON3D_DOCTOR_HAS_NATIVE_FFMPEG != 0},
            {"fribidi", CHRONON3D_DOCTOR_HAS_FRIBIDI != 0},
        };
        const nlohmann::json cpu_isa = {
            {"highest", simd::cpu_isa_name(cpu.highest_isa)},
            {"sse42", cpu.has_sse42},
            {"avx2", cpu.has_avx2},
            {"avx512", cpu.has_avx512},
            {"neon", cpu.has_neon},
        };
        std::cout << nlohmann::json{
                         {"ready", report.ready},
                         {"capabilities", std::move(caps)},
                         {"features", features},
                         {"backend_default_preference", "auto"},
                         {"cpu_isa", cpu_isa},
                         {"checks", std::move(checks)}}
                         .dump(2)
                  << '\n';
    } else {
        std::cout << "Chronon Doctor\n\n";
        for (const auto& check : report.checks) {
            std::cout << "  " << check.id << "\t"
                      << doctor_status_label(check.status);
            if (!check.message.empty()) {
                std::cout << "\t" << check.message;
            }
            std::cout << '\n';
        }
        std::cout << "\nOverall\t" << (report.ready ? "READY" : "NOT READY")
                  << '\n';
    }

    return report.ready ? 0 : 1;
}

int command_verify(const CompositionRegistry& registry, const std::string& output_dir) {
    std::filesystem::create_directories(output_dir);
    int exit_code = 0;

#ifdef CHRONON3D_HAS_CLI_RENDER
    for (const auto& id : registry.available()) {
        RenderArgs args;
        args.comp_id = id;
        args.frames = "0";
        args.output = output_dir + "/" + id + ".png";
        if (command_render(registry, args) != 0) {
            exit_code = 1;
        }
    }
#else
    spdlog::warn("verify: CHRONON3D_HAS_CLI_RENDER off — per-composition render loop skipped");
    // Still mark verify incomplete so callers know the per-frame smoke wasn't run.
    exit_code = 1;
#endif

#if defined(CHRONON3D_HAS_CLI_RENDER) && defined(CHRONON3D_HAS_CLI_VIDEO_EXPORT)
    const auto available = registry.available();
    if (!available.empty()) {
        RenderArgs video_args;
        video_args.comp_id = available.front();
        video_args.frames = "0-1";
        video_args.output =
            (std::filesystem::path(output_dir) / "video_smoke_verify.mp4").string();
        if (command_render(registry, video_args) != 0) {
            exit_code = 1;
        }
    }
#endif

    return exit_code;
}

} // namespace cli
} // namespace chronon3d
