#include <chronon3d/render_plan/audio_mix_compiler.hpp>
#include <chronon3d/assets/asset_resolver.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

TEST_CASE("audio mix compiler lowers deterministic image cue timing and gain on the SFX bus") {
    const auto root = std::filesystem::temp_directory_path() / "chronon-image-sfx-mix-test";
    std::filesystem::create_directories(root);
    std::ofstream(root / "cue.m4a").put('\0');
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(std::filesystem::absolute(root));

    chronon3d::render_plan::CanvasSpec canvas;
    canvas.fps = chronon3d::FrameRate{24, 1};
    canvas.duration = chronon3d::Frame{72};

    chronon3d::render_plan::AudioMixPlan mix;
    chronon3d::render_plan::AudioClipPlan cue;
    cue.id = "overlay-sfx-photo";
    cue.asset = "cue.m4a";
    cue.start = chronon3d::Frame{24};
    cue.clip_in = chronon3d::Frame{0};
    cue.clip_out = chronon3d::Frame{6};
    cue.gain_db = -18.0f;
    cue.role = "sfx";
    mix.clips.push_back(cue);

    const auto compiled = chronon3d::render_plan::AudioMixCompiler::compile(
        mix, canvas, resolver, 48000);
    REQUIRE(compiled);
    REQUIRE(compiled->inputs.size() == 1);
    CHECK(compiled->inputs[0].source_start_sample == 0);
    CHECK(compiled->inputs[0].source_end_sample == 12000);
    CHECK(compiled->inputs[0].timeline_start_sample == 48000);
    CHECK(compiled->inputs[0].gain_db == doctest::Approx(-18.0f));
    CHECK(compiled->filter_complex.find("atrim=start_sample=0:end_sample=12000") != std::string::npos);
    CHECK(compiled->filter_complex.find("adelay=48000S:all=1") != std::string::npos);
    CHECK(compiled->filter_complex.find("volume='pow(10") != std::string::npos);
    CHECK(compiled->filter_complex.find("-18.000000") != std::string::npos);
    CHECK(compiled->filter_complex.find("sidechaincompress") == std::string::npos);
    CHECK(compiled->filter_complex.find("[clip0]amix=inputs=1:duration=longest:normalize=0[sfx];") != std::string::npos);
    std::filesystem::remove_all(root);
}
