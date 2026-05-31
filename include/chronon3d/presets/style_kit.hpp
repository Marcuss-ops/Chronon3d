#pragma once

#include <chronon3d/math/color.hpp>
#include <string>

namespace chronon3d::presets {

struct ColorPalette {
    Color primary{1.0f, 1.0f, 1.0f, 1.0f};
    Color secondary{0.76f, 0.79f, 0.84f, 1.0f};
    Color accent{0.98f, 0.54f, 0.16f, 1.0f};
    Color background{0.05f, 0.06f, 0.08f, 0.86f};
};

struct TypographyScale {
    std::string font_family{"Inter"};
    f32 title_size{72.0f};
    f32 subtitle_size{36.0f};
    f32 body_size{24.0f};
    f32 tracking_title{0.0f};
    f32 tracking_body{0.0f};
};

struct StyleKit {
    ColorPalette colors;
    TypographyScale typography;
    f32 corner_radius{12.0f};
    f32 line_thickness{2.0f};

    static StyleKit documentary() {
        return {
            .colors = {
                .primary = {0.95f, 0.95f, 0.95f, 1.0f},
                .secondary = {0.7f, 0.7f, 0.7f, 1.0f},
                .accent = {0.8f, 0.6f, 0.2f, 1.0f},
                .background = {0.02f, 0.02f, 0.03f, 0.9f}
            },
            .typography = {.font_family = "Inter"},
            .corner_radius = 4.0f,
            .line_thickness = 1.5f
        };
    }

    static StyleKit tech() {
        return {
            .colors = {
                .primary = {1.0f, 1.0f, 1.0f, 1.0f},
                .secondary = {0.6f, 0.8f, 0.9f, 1.0f},
                .accent = {0.0f, 0.8f, 1.0f, 1.0f},
                .background = {0.05f, 0.08f, 0.12f, 0.85f}
            },
            .typography = {.font_family = "Inter"},
            .corner_radius = 16.0f,
            .line_thickness = 2.0f
        };
    }

    static StyleKit luxury() {
        return {
            .colors = {
                .primary = {1.0f, 1.0f, 1.0f, 1.0f},
                .secondary = {0.7f, 0.7f, 0.7f, 1.0f},
                .accent = {0.83f, 0.68f, 0.21f, 1.0f},
                .background = {0.02f, 0.02f, 0.02f, 0.95f}
            },
            .typography = {.font_family = "Cinzel"},
            .corner_radius = 0.0f,
            .line_thickness = 1.0f
        };
    }

    static StyleKit warning() {
        return {
            .colors = {
                .primary = {1.0f, 0.9f, 0.9f, 1.0f},
                .secondary = {0.8f, 0.6f, 0.6f, 1.0f},
                .accent = {0.98f, 0.2f, 0.2f, 1.0f},
                .background = {0.15f, 0.02f, 0.02f, 0.92f}
            },
            .typography = {.font_family = "Inter"},
            .corner_radius = 12.0f,
            .line_thickness = 3.0f
        };
    }

    static StyleKit sports() {
        return {
            .colors = {
                .primary = {1.0f, 1.0f, 1.0f, 1.0f},
                .secondary = {0.7f, 0.9f, 0.75f, 1.0f},
                .accent = {0.0f, 0.95f, 0.4f, 1.0f},
                .background = {0.02f, 0.15f, 0.05f, 0.9f}
            },
            .typography = {.font_family = "Oswald"},
            .corner_radius = 8.0f,
            .line_thickness = 4.0f
        };
    }

    static StyleKit gossip() {
        return {
            .colors = {
                .primary = {1.0f, 1.0f, 1.0f, 1.0f},
                .secondary = {0.9f, 0.7f, 0.85f, 1.0f},
                .accent = {1.0f, 0.1f, 0.6f, 1.0f},
                .background = {0.16f, 0.03f, 0.12f, 0.88f}
            },
            .typography = {.font_family = "Inter"},
            .corner_radius = 24.0f,
            .line_thickness = 2.5f
        };
    }

    static StyleKit breaking_news() {
        return {
            .colors = {
                .primary = {1.0f, 1.0f, 1.0f, 1.0f},
                .secondary = {0.85f, 0.85f, 0.85f, 1.0f},
                .accent = {0.9f, 0.05f, 0.05f, 1.0f},
                .background = {0.08f, 0.01f, 0.02f, 0.95f}
            },
            .typography = {.font_family = "Montserrat"},
            .corner_radius = 0.0f,
            .line_thickness = 3.0f
        };
    }

};

} // namespace chronon3d::presets
