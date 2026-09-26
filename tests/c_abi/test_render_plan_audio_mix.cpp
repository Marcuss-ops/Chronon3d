#include <chronon3d/render_plan/audio_mix_compiler.hpp>
#include <chronon3d/assets/asset_resolver.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

TEST_CASE("audio mix compiler uses exact frame sample trims and deterministic duck graph") {
    const auto root = std::filesystem::temp_directory_path() / "chronon-audio-mix-compiler-test";
    std::filesystem::create_directories(root);
    std::ofstream(root / "music.wav").put('\0');
    std::ofstream(root / "voice.wav").put('\0');
    chronon3d::assets::AssetResolver resolver;
    resolver.mount(std::filesystem::absolute(root));

    chronon3d::render_plan::CanvasSpec canvas;
    canvas.fps = chronon3d::FrameRate{30, 1};
    canvas.duration = chronon3d::Frame{30};
    chronon3d::render_plan::AudioMixPlan mix;
    chronon3d::render_plan::AudioClipPlan music;
    music.id = "bed";
    music.asset = "music.wav";
    music.clip_in = chronon3d::Frame{15};
    music.clip_out = chronon3d::Frame{30};
    music.start = chronon3d::Frame{15};
    music.role = "music";
    mix.clips.push_back(music);
    chronon3d::render_plan::AudioClipPlan voice;
    voice.id = "dialogue";
    voice.asset = "voice.wav";
    voice.role = "voice";
    mix.clips.push_back(voice);

    const auto compiled = chronon3d::render_plan::AudioMixCompiler::compile(
        mix, canvas, resolver, 48000);
    REQUIRE(compiled);
    REQUIRE(compiled->inputs.size() == 2);
    CHECK(compiled->duration_samples == 48000);
    CHECK(compiled->inputs[0].source_start_sample == 24000);
    CHECK(compiled->inputs[0].source_end_sample == 48000);
    CHECK(compiled->inputs[0].timeline_start_sample == 24000);
    CHECK(compiled->filter_complex.find("atrim=start_sample=24000:end_sample=48000") != std::string::npos);
    CHECK(compiled->filter_complex.find("sidechaincompress") != std::string::npos);
    std::filesystem::remove_all(root);
}
