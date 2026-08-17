// sdk_consumers/01_cpp_minimal/main.cpp
//
// Minimal C++ SDK consumer.  Links ONLY Chronon3D::SDK and drives the
// canonical RenderPlan JSON through the thin facade:
//
//     engine.compile_plan_json(json) -> CompiledComposition
//     engine.render_compiled(...)    -> RenderOutput
//
// No umbrella header, no internal target, no backend/runtime/graph types.

#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_error.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>

int main(int argc, char* argv[]) {
    const char* plan_path = (argc > 1) ? argv[1] : "plan.json";
    std::ifstream input(plan_path);
    if (!input) {
        std::fprintf(stderr, "cannot open %s\n", plan_path);
        return 1;
    }
    std::ostringstream json;
    json << input.rdbuf();

    chronon3d::sdk::RenderEngine engine;

    auto compiled = engine.compile_plan_json(json.str());
    if (!compiled.has_value()) {
        std::fprintf(stderr, "compile_plan_json failed: %s\n",
                     compiled.error().message.c_str());
        return 1;
    }

    auto out =
        engine.render_compiled(*compiled.value(), chronon3d::sdk::Frame{0});
    if (!out.has_value()) {
        std::fprintf(stderr, "render_compiled failed: %s\n",
                     out.error().message.c_str());
        return 1;
    }

    const chronon3d::sdk::RenderOutput& frame = out.value();
    const std::size_t pixel_count =
        static_cast<std::size_t>(frame.width) *
        static_cast<std::size_t>(frame.height) * 4u;
    bool nonzero = false;
    for (std::size_t i = 0; i < pixel_count; ++i) {
        if (frame.pixels[i] != 0) {
            nonzero = true;
            break;
        }
    }
    if (!nonzero) {
        std::fprintf(stderr, "rendered frame is empty\n");
        return 1;
    }

    std::printf("CPP_SDK_CONSUMER_PASS %dx%d\n", frame.width, frame.height);
    return 0;
}
