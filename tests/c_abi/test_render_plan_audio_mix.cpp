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
    chronon3d::render_plan::AnimationTrackPlan gain;
    gain.property = "gain_db";
    chronon3d::render_plan::AnimationKeyframePlan gain_start;
    gain_start.frame = chronon3d::Frame{0};
    gain_start.value = {0.0f};
    gain_start.interpolation = "hold";
    chronon3d::render_plan::AnimationKeyframePlan gain_end;
    gain_end.frame = chronon3d::Frame{10};
    gain_end.value = {-12.0f};
    gain.keyframes = {gain_start, gain_end};
    voice.gain = gain;
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
    REQUIRE(compiled->inputs[1].gain_db_per_frame.size() == 30);
    CHECK(compiled->inputs[1].gain_db_per_frame[5] == doctest::Approx(0.0f));
    CHECK(compiled->inputs[1].gain_db_per_frame[10] == doctest::Approx(-12.0f));
    std::filesystem::remove_all(root);
}
