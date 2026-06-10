#include "register_content_modules.hpp"

namespace chronon3d {

void register_content_modules() {
    // Each per-module function registers its module and calls initialize_all().
    register_minimalist_content();
    register_text_content();
    register_two_point_five_d_content();
    register_images_content();
}

} // namespace chronon3d
