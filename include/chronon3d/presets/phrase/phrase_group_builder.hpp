#pragma once

#include <chronon3d/presets/phrase/phrase_presets.hpp>
#include <chronon3d/presets/motion_presets.hpp>
#include <vector>
#include <string>

namespace chronon3d::presets::phrase {

class PhraseGroupBuilder {
public:
    explicit PhraseGroupBuilder(const PhraseParams& params);

    PhraseGroupBuilder& panel(const std::string& id, MotionPreset preset);
    PhraseGroupBuilder& accent(const std::string& id, MotionPreset preset, Vec3 pos, Vec2 size);
    PhraseGroupBuilder& title(const std::string& id, MotionPreset preset, Vec3 pos, Vec2 size, TextAlign align = TextAlign::Center);
    PhraseGroupBuilder& subtitle(const std::string& id, MotionPreset preset, Vec3 pos, Vec2 size, TextAlign align = TextAlign::Center);
    PhraseGroupBuilder& quote(const std::string& id, MotionPreset preset, Vec3 pos);
    PhraseGroupBuilder& add(MotionObject obj);

    MotionObject build(MotionPreset group_preset);

    // Inspector method for testing
    const std::vector<MotionObject>& get_children() const { return children; }

private:
    const PhraseParams& p;
    std::vector<MotionObject> children;
};

} // namespace chronon3d::presets::phrase
