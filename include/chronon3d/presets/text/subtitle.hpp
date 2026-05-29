#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/shape.hpp>
#include <string>
#include <vector>

namespace chronon3d::presets::text {

struct WordTiming {
    std::string word;
    Frame start{0};
    Frame end{0};
};

struct SubtitleCue {
    Frame start{0};
    Frame end{0};
    std::string text;
    std::vector<WordTiming> words;
};

struct SubtitleTrack {
    std::vector<SubtitleCue> cues;
};

enum class SubtitleMode {
    Normal,
    WordHighlight,
};

} // namespace chronon3d::presets::text
