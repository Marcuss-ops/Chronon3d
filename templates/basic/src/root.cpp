#include <chronon3d/sdk/render_engine.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

bool valid_composition_name(const std::string& name) {
    return !name.empty() && std::all_of(name.begin(), name.end(), [](char value) {
        const auto c = static_cast<unsigned char>(value);
        return std::isalnum(c) || c == '-' || c == '_';
    });
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5 || std::string_view(argv[1]) != "--render" ||
        std::string_view(argv[3]) != "-o") {
        std::fprintf(stderr,
            "Usage: %s --render <composition> -o <output.mp4>\n", argv[0]);
        return 2;
    }

    const std::string composition = argv[2];
    if (!valid_composition_name(composition)) {
        std::fprintf(stderr, "Invalid composition name: %s\n", composition.c_str());
        return 2;
    }

    const auto plan = std::filesystem::path("compositions") /
        (composition + ".json");
    chronon3d::sdk::RenderEngine engine;
    auto result = engine.render_plan_file(plan, argv[4], "assets");
    if (!result) {
        std::fprintf(stderr, "Render failed: %s\n", result.error().message.c_str());
        return 1;
    }

    std::printf("Rendered %s: %llu frames -> %s\n",
        composition.c_str(),
        static_cast<unsigned long long>(result->rendered_frames),
        result->output_path.string().c_str());
    return 0;
}
