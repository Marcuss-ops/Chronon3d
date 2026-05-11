#include "chronon/Chronon.hpp"

#include <chrono>
#include <iostream>

namespace ch {
Scene makeIntroScene();
}

int main() {
    using namespace std::chrono_literals;

    const ch::Scene intro = ch::makeIntroScene();
    const ch::Scene outro = ch::Scene([](ch::Frame frame) {
        const double fade = ch::easeInOutCubic(frame.progress(0s, 2s));
        ch::Camera camera;
        camera.position(0.0, 0.0, ch::lerp(760.0, 680.0, fade));
        camera.rotation(0.0, 0.0, 0.0);

        auto endPlate = ch::Solid({0.04, 0.05, 0.07, 1.0})
            .position(0.0, 0.0, -220.0)
            .scale(20.0);

        auto endTitle = ch::Text("OUTRO")
            .font(ASSET("assets/fonts/Inter-Bold.ttf"), 96.0)
            .position(0.0, 0.0, 120.0)
            .opacity(fade);

        return ch::Compose(endPlate, endTitle).with(camera);
    }).duration(2s);

    const ch::RenderJob job{
        .size = {1920, 1080},
        .fps = 60,
        .scenes = {
            intro.duration(5s),
            outro.duration(3s).transition(ch::CrossFade(500ms))
        },
        .output = "video.mp4"
    };

    ch::Project project({1920, 1080, 60});

    std::cout << "Chronon template\n";
    std::cout << "RenderJob: " << job.size.width << "x" << job.size.height
              << " @ " << job.fps << "fps -> " << job.output << "\n";
    std::cout << "Scenes: " << job.scenes.size() << "\n\n";

    project.preview(job.scenes.front(), {0.0, 0.5, 1.0, 2.0, 3.0});
    return 0;
}
