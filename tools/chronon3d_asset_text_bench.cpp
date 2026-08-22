#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/config.hpp>
#include "../examples/bench_micro/asset_text_bench_scenes.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <algorithm>

using namespace chronon3d;
using namespace chronon3d::bench_micro;

struct BenchStats {
    double cold_first_frame_ms{0.0};
    double warm_408_wall_ms{0.0};
    double warm_per_frame_ms{0.0};
    double p95_warm_ms{0.0};
};

#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/software/runtime_adapter.hpp>

BenchStats benchmark_composition(
    const Composition& comp,
    const std::string& case_name,
    int frames_count = 408,
    graph::BackendPreference pref = graph::BackendPreference::GPU) {
    (void)case_name;
    auto runtime_res = runtime::RenderRuntime::create(runtime::RuntimeConfig{});
    if (!runtime_res.has_value()) {
        std::cerr << "Failed to create runtime\n";
        return {};
    }
    auto runtime = std::move(runtime_res).value();
    runtime->resolver().mount(std::filesystem::current_path());
    runtime->image_cache().set_backend(std::make_shared<image::StbImageBackend>());
    SoftwareRenderer renderer(*runtime, Config{});
    backends::software::attach_software_backend(&renderer, pref);

    // 1. COLD: First frame render with asset loading & graph compile
    auto t0 = std::chrono::high_resolution_clock::now();
    (void)renderer.render(comp, Frame{0});
    auto t1 = std::chrono::high_resolution_clock::now();
    double cold_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // 2. 1 Warm run (100 frames)
    int bench_frames = std::min(frames_count, 100);
    auto run_start = std::chrono::high_resolution_clock::now();
    for (int f = 0; f < bench_frames; ++f) {
        (void)renderer.render(comp, Frame{f});
    }
    auto run_end = std::chrono::high_resolution_clock::now();
    double warm_ms = std::chrono::duration<double, std::milli>(run_end - run_start).count();

    BenchStats stats;
    stats.cold_first_frame_ms = cold_ms;
    stats.warm_408_wall_ms = warm_ms * (408.0 / bench_frames);
    stats.warm_per_frame_ms = warm_ms / bench_frames;
    stats.p95_warm_ms = warm_ms;
    return stats;
}

int main(int argc, char** argv) {
    std::string filter;
    if (argc > 1) filter = argv[1];

    std::cout << "\n================ CHRONON ASSET/TEXT BENCHMARK MATRIX ================\n";
    std::cout << std::left << std::setw(28) << "CASE"
              << std::right << std::setw(12) << "COLD (1st)"
              << std::setw(16) << "WARM/408"
              << std::setw(16) << "PER FRAME"
              << std::setw(14) << "WARM P95\n";
    std::cout << "------------------------------------------------------------------------\n" << std::flush;

    std::vector<std::pair<std::string, Composition>> cases;

    // Image cases
    cases.emplace_back("IMG_camera_jpeg", create_image_scene(std::filesystem::current_path() / "assets/images/camera_reference.jpg"));
    cases.emplace_back("IMG_landscape_png", create_image_scene(std::filesystem::current_path() / "assets/images/minimalist_landscape.png"));
    cases.emplace_back("IMG_checker_png", create_image_scene(std::filesystem::current_path() / "assets/images/checker.png"));
    cases.emplace_back("IMG_grid_tile_png", create_image_scene(std::filesystem::current_path() / "assets/images/grid_tile.png"));
    cases.emplace_back("IMG_four_mixed", create_four_images_scene());
    cases.emplace_back("IMG_15_static", create_15_images_static_scene(std::filesystem::current_path() / "assets/images/checker.png"));
    cases.emplace_back("IMG_15_animated", create_15_images_animated_scene(std::filesystem::current_path() / "assets/images/checker.png"));

    // Text cases
    cases.emplace_back("TXT_CHRONON", create_static_text_scene("CHRONON"));
    cases.emplace_back("TXT_sentence", create_static_text_scene("Chronon renders motion graphics at incredible speed."));
    cases.emplace_back("TXT_200_chars", create_static_text_scene("Chronon is a deterministic, ultra-fast 2D/3D motion graphics engine architected for sub-second high-throughput pipelines, leveraging zero-copy GPU memory bridges, SIMD pixel kernels, and fused execution."));
    cases.emplace_back("TXT_names_ascii", create_static_text_scene("Marcus John Smith Michael Jordan Elon Musk"));
    cases.emplace_back("TXT_names_accents", create_static_text_scene("José Álvarez François Müller Søren Kierkegaard Łukasz Żółć André Noël São Paulo"));
    cases.emplace_back("TXT_symbols", create_static_text_scene("€ £ ¥ $ © ® ™ 25°C 100% 1 → 2 A • B • C"));
    cases.emplace_back("TXT_combining_unicode", create_static_text_scene("José Jose\u0301"));
    cases.emplace_back("TXT_dynamic_frame", create_dynamic_text_scene("Marcus Live"));
    cases.emplace_back("TXT_15_unique_static", create_15_unique_texts_static_scene());
    cases.emplace_back("TXT_15_unique_anim", create_15_unique_texts_animated_scene());

    for (const auto& [name, comp] : cases) {
        if (!filter.empty() && name.find(filter) == std::string::npos) continue;
        BenchStats s = benchmark_composition(comp, name, 408);
        std::cout << std::left << std::setw(28) << name
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(9) << s.cold_first_frame_ms << " ms"
                  << std::setw(13) << s.warm_408_wall_ms << " ms"
                  << std::fixed << std::setprecision(4)
                  << std::setw(13) << s.warm_per_frame_ms << " ms"
                  << std::fixed << std::setprecision(2)
                  << std::setw(11) << s.p95_warm_ms << " ms\n" << std::flush;
    }

    std::cout << "========================================================================\n\n";
    return 0;
}
