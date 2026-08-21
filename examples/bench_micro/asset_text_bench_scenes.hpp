#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <string>

namespace chronon3d::bench_micro {

// Image test scenes
Composition create_image_scene(const std::string& image_path);
Composition create_four_images_scene();
Composition create_100_images_scene(const std::string& image_path);

// Text test scenes
Composition create_static_text_scene(const std::string& text, float font_size = 64.0f);
Composition create_dynamic_text_scene(const std::string& base_text);
Composition create_100_unique_texts_scene();

} // namespace chronon3d::bench_micro
