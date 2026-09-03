#include <doctest/doctest.h>

#include <chronon3d/audio/audio_sample_clock.hpp>

#include <stdexcept>

using chronon3d::Frame;
using chronon3d::FrameRate;
using chronon3d::audio::AudioSampleClock;

TEST_CASE("AudioSampleClock uses an exact integer cadence at 24 fps") {
    const AudioSampleClock clock{48000, FrameRate{24, 1}};

    CHECK(clock.sample_index_at(Frame{0}) == 0);
    CHECK(clock.sample_index_at(Frame{1}) == 2000);
    CHECK(clock.sample_index_at(Frame{24}) == 48000);
    CHECK(clock.samples_for_frame(Frame{17}) == 2000);

    const auto span = clock.span_for_frame(Frame{17});
    CHECK(span.first_sample == 34000);
    CHECK(span.sample_count == 2000);
}

TEST_CASE("AudioSampleClock distributes NTSC fractional samples without drift") {
    const AudioSampleClock clock{48000, FrameRate{30000, 1001}};

    CHECK(clock.sample_index_at(Frame{0}) == 0);
    CHECK(clock.sample_index_at(Frame{1}) == 1601);
    CHECK(clock.sample_index_at(Frame{2}) == 3203);
    CHECK(clock.sample_index_at(Frame{3}) == 4804);
    CHECK(clock.sample_index_at(Frame{4}) == 6406);
    CHECK(clock.sample_index_at(Frame{5}) == 8008);

    CHECK(clock.samples_for_frame(Frame{0}) == 1601);
    CHECK(clock.samples_for_frame(Frame{1}) == 1602);
    CHECK(clock.samples_for_frame(Frame{2}) == 1601);
    CHECK(clock.samples_for_frame(Frame{3}) == 1602);
    CHECK(clock.samples_for_frame(Frame{4}) == 1602);

    // 30,000 frames at 30,000/1001 fps are exactly 1001 seconds.
    CHECK(clock.sample_index_at(Frame{30000}) == 48048000);
}

TEST_CASE("AudioSampleClock keeps long-run rational boundaries exact") {
    const AudioSampleClock clock{44100, FrameRate{24000, 1001}};

    // 24,000 frames are exactly 1001 seconds at 24,000/1001 fps.
    CHECK(clock.sample_index_at(Frame{24000}) == 44144100);

    // No state is accumulated: random access and adjacent spans agree exactly.
    const auto span = clock.span_for_frame(Frame{23999});
    CHECK(span.first_sample + span.sample_count ==
          clock.sample_index_at(Frame{24000}));
}

TEST_CASE("AudioSampleClock uses floor boundaries for negative timeline frames") {
    const AudioSampleClock clock{48000, FrameRate{30000, 1001}};

    CHECK(clock.sample_index_at(Frame{-1}) == -1602);
    CHECK(clock.sample_index_at(Frame{0}) == 0);

    const auto span = clock.span_for_frame(Frame{-1});
    CHECK(span.first_sample == -1602);
    CHECK(span.sample_count == 1602);
}

TEST_CASE("AudioSampleClock exposes exact sample timestamps") {
    const AudioSampleClock clock{48000, FrameRate{24, 1}};
    const auto time = clock.sample_time(96000);

    CHECK(time.ticks() == 96000);
    CHECK(time.time_base.numerator == 1);
    CHECK(time.time_base.denominator == 48000);
}

TEST_CASE("AudioSampleClock rejects invalid rates") {
    CHECK_THROWS_AS((AudioSampleClock{0, FrameRate{24, 1}}), std::invalid_argument);
    CHECK_THROWS_AS((AudioSampleClock{48000, FrameRate{0, 1}}), std::invalid_argument);
    CHECK_THROWS_AS((AudioSampleClock{48000, FrameRate{24, 0}}), std::invalid_argument);
    CHECK_THROWS_AS((AudioSampleClock{48000, FrameRate{24, -1}}), std::invalid_argument);
}
