#pragma once

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::minimalist {

// ── Text intro animations ───────────────────────────────────────────────────
Composition minimalist_text_fade_up();
Composition minimalist_text_tracking_reveal();
Composition minimalist_text_clip_reveal();
Composition minimalist_text_fade_down();
Composition minimalist_text_soft_scale();
Composition minimalist_text_blur_focus();
Composition minimalist_text_slide_left();
Composition minimalist_text_slide_right();
Composition minimalist_text_scale_pop();
Composition minimalist_text_float_in();
Composition minimalist_text_letter_rise();
Composition minimalist_text_drift_in();
Composition minimalist_text_tilt_in();
Composition minimalist_text_mask_reveal();
Composition minimalist_text_snap_pop();

// ── Text exit animations ────────────────────────────────────────────────────
Composition minimalist_text_fade_away();
Composition minimalist_text_scale_out();
Composition minimalist_text_slide_up_out();
Composition minimalist_text_blur_away();
Composition minimalist_text_tilt_out();

// ── Image presets ───────────────────────────────────────────────────────────
Composition minimalist_image_fade_in();
Composition minimalist_image_focus_in();
Composition minimalist_image_scale_drop();
Composition minimalist_image_fade_shift_vertical();
Composition minimalist_image_center_split();
Composition minimalist_image_reveal_from_bottom();
Composition minimalist_image_framing_bracket();
Composition minimalist_image_tracking_breathing();
Composition minimalist_image_elegant_exit();
Composition minimalist_image_elastic_slide();

} // namespace chronon3d::content::minimalist
