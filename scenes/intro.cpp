#include "chronon/Chronon.hpp"

#include <chrono>

namespace ch {
using namespace std::chrono_literals;

Scene makeIntroScene() {
    return Scene([](Frame frame) {
        const double introProgress = frame.progress(0s, 3s);
        const double titleRise = easeOutCubic(frame.progress(250ms, 1250ms));
        const double cameraPull = easeInOutCubic(frame.progress(0s, 3s));

        Camera camera;
        camera.position(0.0, 0.0, lerp(1000.0, 720.0, cameraPull));
        camera.rotation(0.0, lerp(-5.0, 5.0, introProgress), 0.0);
        camera.fieldOfView(30.0);
        camera.pushIn(48.0);

        auto background = Solid({0.08, 0.09, 0.12, 1.0})
            .position(0.0, 0.0, -300.0)
            .scale(20.0)
            .opacity(1.0);

        auto rearPlate = Video(ASSET("assets/drone.mp4"))
            .trim(0s, 5s)
            .position(0.0, 0.0, 0.0)
            .scale(1.1)
            .opacity(1.0);

        auto title = Text("CHRONON")
            .font(ASSET("assets/fonts/Inter-Bold.ttf"), 120.0)
            .position(0.0, lerp(120.0, 0.0, titleRise), 120.0)
            .rotation(0.0, lerp(-12.0, 0.0, titleRise), 0.0)
            .opacity(titleRise);

        auto subtitle = Text("2.5D timeline DSL")
            .font(ASSET("assets/fonts/Inter-Regular.ttf"), 42.0)
            .position(0.0, 70.0, 220.0)
            .opacity(introProgress);

        auto music = Audio(ASSET("assets/audio/track.wav"))
            .volume(-6.0)
            .at(0s);

        return Compose(background, rearPlate, title, subtitle, music).with(camera);
    }).duration(3s);
}

} // namespace ch
