#pragma once

#include <string>
#include <vector>

namespace chronon3d::render_plan::detail {

[[nodiscard]] bool native_audio_react_available() noexcept;
[[nodiscard]] bool decode_audio_mono_48k(const std::string& path,
                                         std::vector<float>& samples,
                                         std::string& error);

} // namespace chronon3d::render_plan::detail
