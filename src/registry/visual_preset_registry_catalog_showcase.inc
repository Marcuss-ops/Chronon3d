    // ── 2D showcase presets ────────────────────────────────────────────
    r.register_preset(make_simple_2d_preset(
        "image_fade_in", "image", VisualLayerKind::Image, "",
        "fade_in", "line", "image_right", "left", 0.0f, 0));
    r.register_preset(make_simple_2d_preset(
        "image_slide_left", "image", VisualLayerKind::Image, "",
        "slide_in", "line", "image_right", "left", 0.0f, 0));
    r.register_preset(make_simple_2d_preset(
        "image_slide_right", "image", VisualLayerKind::Image, "",
        "fade_shift_horizontal", "line", "image_left", "right", 0.0f, 0));
    r.register_preset(make_simple_2d_preset(
        "image_scale_in", "image", VisualLayerKind::Image, "",
        "scale_drop", "line", "center", "center", 0.0f, 0));

    r.register_preset(make_simple_2d_preset(
        "phrase_fade_in", "important_phrase", VisualLayerKind::Text,
        "caption_safe_area", "fade_in", "line", "safe_area", "center", 64.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "phrase_scale_in", "important_phrase", VisualLayerKind::Text,
        "caption_safe_area", "scale_drop", "line", "safe_area", "center", 64.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "phrase_slide_up", "important_phrase", VisualLayerKind::Text,
        "caption_safe_area", "reveal_from_bottom", "line", "safe_area", "center", 64.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "phrase_soft_pop", "important_phrase", VisualLayerKind::Text,
        "caption_safe_area", "soft_pop", "line", "safe_area", "center", 64.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "phrase_word_reveal", "important_phrase", VisualLayerKind::Text,
        "caption_safe_area", "fade_in", "word", "safe_area", "center", 64.0f, 700));

    // ── Modern fast overlay aliases ────────────────────────────────────
    r.register_preset(make_simple_2d_preset(
        "fast_fade_through", "important_phrase", VisualLayerKind::Text,
        "caption_safe_area", "fade_in", "line", "safe_area", "center", 64.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "snap_scale", "important_phrase", VisualLayerKind::Text,
        "caption_safe_area", "scale_drop", "line", "safe_area", "center", 64.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "clean_slide_up", "important_phrase", VisualLayerKind::Text,
        "caption_safe_area", "reveal_from_bottom", "line", "safe_area", "center", 64.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "slide_lateral", "important_phrase", VisualLayerKind::Text,
        "caption_safe_area", "slide_in", "line", "safe_area", "center", 64.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "undertext_pop", "subtitle", VisualLayerKind::Text,
        "subtitle_bottom", "fade_in", "line", "lower_third", "center", 48.0f, 700));

    r.register_preset(make_simple_2d_preset(
        "modern_rounded_pop", "image", VisualLayerKind::Image,
        "", "scale_drop", "line", "center", "center", 0.0f, 0));
    r.register_preset(make_simple_2d_preset(
        "bottom_card_rise", "image", VisualLayerKind::Image,
        "", "reveal_from_bottom", "line", "center", "center", 0.0f, 0));
    r.register_preset(make_simple_2d_preset(
        "image_fast_fade", "image", VisualLayerKind::Image,
        "", "fade_in", "line", "center", "center", 0.0f, 0));

    // Names / entity cards
    r.register_preset(make_simple_2d_preset(
        "name_fade_in", "name", VisualLayerKind::Text, "lower_third",
        "fade_in", "line", "lower_third", "left", 58.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "name_slide_up", "name", VisualLayerKind::Text, "lower_third",
        "reveal_from_bottom", "line", "lower_third", "left", 58.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "name_pop_in", "name", VisualLayerKind::Text, "lower_third",
        "soft_pop", "line", "lower_third", "left", 58.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "name_slide_left", "name", VisualLayerKind::Text, "lower_third",
        "slide_in", "line", "lower_third", "left", 58.0f, 700));
    r.register_preset(make_simple_2d_preset(
        "name_scale_in", "name", VisualLayerKind::Text, "lower_third",
        "scale_drop", "line", "lower_third", "left", 58.0f, 700));

    r.register_preset(make_name_glow_preset(
        "name_glow_typewriter", "fade_in", "glyph"));
    r.register_preset(make_name_glow_preset(
        "name_glow_slide", "reveal_from_bottom", "line"));
    r.register_preset(make_name_glow_preset(
        "name_glow_pop", "fade_in", "line"));
