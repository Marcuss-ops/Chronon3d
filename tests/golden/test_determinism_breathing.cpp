// test_determinism_breathing.cpp
//
// Renderizza MinimalistImageTrackingBreathing frame 50 per 5 volte di fila
// e stampa tutti gli hash per vedere se il renderer è deterministico.
//
// Uso: ./chronon3d_determinism_test

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <tests/helpers/test_utils.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

const std::string IMAGE_PATH = "assets/images/minimalist_landscape.png";
const Vec2 IMAGE_SIZE = {800.0f, 450.0f};

void add_common_background(SceneBuilder& s) {
    s.layer("background", [](auto& l) {
        l.cache_static();
        l.pin_to(Anchor::Center);
        l.grid_background("grid_bg", {
            .size = {1920.0f, 1080.0f},
            .offset = {0.0f, 0.0f},
            .bg_color = {0.025f, 0.027f, 0.031f, 1.0f},
            .grid_color = {0.58f, 0.61f, 0.66f, 0.045f},
            .spacing = 136.0f,
            .minor_thickness = 1.0f,
            .major_thickness = 2.0f,
            .major_every = 4,
            .centered = true
        });
    });
}

void add_image_border(LayerBuilder& l, Vec2 size) {
    l.rounded_rect("image_backdrop", {
        .size = size + Vec2{24.0f, 24.0f},
        .radius = 16.0f,
        .color = Color{0.0f, 0.0f, 0.0f, 0.35f},
        .pos = {0.0f, 0.0f, 0.0f}
    });
    l.rounded_rect("image_border", {
        .size = size + Vec2{2.0f, 2.0f},
        .radius = 10.0f,
        .color = Color{0.25f, 0.27f, 0.31f, 0.8f},
        .pos = {0.0f, 0.0f, 0.0f}
    });
}

Composition make_breathing_comp() {
    return composition(
        {.name = "MinimalistImageTrackingBreathing", .duration = 150},
        [](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            add_common_background(s);
            s.layer("image_layer", [](auto& l) {
                l.pin_to(Anchor::Center);
                l.tracking_breathing(1.04f, Frame{120});
                add_image_border(l, IMAGE_SIZE);
                l.image("img", {
                    .path = IMAGE_PATH,
                    .size = IMAGE_SIZE,
                    .radius = 8.0f
                });
            });
            return s.build();
        }
    );
}

} // anonymous namespace

int main() {
    if (!std::filesystem::exists(IMAGE_PATH)) {
        std::fprintf(stderr, "ERROR: Asset not found: %s\n", IMAGE_PATH.c_str());
        return 1;
    }

    std::printf("=== Determinism Test: MinimalistImageTrackingBreathing frame 50 ===\n");
    std::printf("Rendering frame 50 five times...\n\n");

    const auto comp = make_breathing_comp();
    std::vector<u64> hashes;
    hashes.reserve(5);

    for (int run = 0; run < 5; ++run) {
        auto renderer = make_renderer();
        auto fb = renderer.render_frame(comp, Frame{50});
        if (!fb) {
            std::fprintf(stderr, "ERROR: Run %d returned null framebuffer\n", run);
            return 1;
        }
        const u64 hash = XXH64(fb->pixels_row(0), fb->size_bytes(), 0);
        hashes.push_back(hash);
        std::printf("  Run %d: hash = 0x%016llx\n", run + 1,
                    static_cast<unsigned long long>(hash));
    }

    std::printf("\n--- Analysis ---\n");

    // Count unique hashes
    int unique_count = 1;
    for (size_t i = 1; i < hashes.size(); ++i) {
        bool is_new = true;
        for (size_t j = 0; j < i; ++j) {
            if (hashes[i] == hashes[j]) {
                is_new = false;
                break;
            }
        }
        if (is_new) unique_count++;
    }

    if (unique_count == 1) {
        std::printf("  ✓ PERFECTLY DETERMINISTIC: All 5 runs produced identical hash 0x%016llx\n",
                    static_cast<unsigned long long>(hashes[0]));
    } else {
        std::printf("  ✗ NON-DETERMINISTIC: %d unique hashes out of 5 runs\n", unique_count);
        std::printf("\n  Unique hashes:\n");
        std::vector<u64> shown;
        for (size_t i = 0; i < hashes.size(); ++i) {
            bool already_shown = false;
            for (size_t j = 0; j < i; ++j) {
                if (hashes[i] == hashes[j]) { already_shown = true; break; }
            }
            if (!already_shown) {
                shown.push_back(hashes[i]);
                int count = 0;
                for (size_t k = 0; k < hashes.size(); ++k) {
                    if (hashes[k] == hashes[i]) count++;
                }
                std::printf("    0x%016llx  (%d run%s)\n",
                    static_cast<unsigned long long>(hashes[i]), count, count > 1 ? "s" : "");
            }
        }

        // Check if it's always different or has stable patterns
        bool always_different = (unique_count == 5);
        bool bimodal = (unique_count == 2);
        if (always_different) {
            std::printf("\n  Pattern: ALL DIFFERENT — every run produces a unique hash.\n");
        } else if (bimodal) {
            std::printf("\n  Pattern: BIMODAL — exactly 2 distinct outputs.\n");
        } else {
            std::printf("\n  Pattern: %d distinct outputs.\n", unique_count);
        }

        // Compare first pixel of first and second run to see error magnitude
        auto renderer1 = make_renderer();
        auto fb1 = renderer1.render_frame(comp, Frame{50});
        auto renderer2 = make_renderer();
        auto fb2 = renderer2.render_frame(comp, Frame{50});

        if (fb1 && fb2 && fb1->width() == fb2->width() && fb1->height() == fb2->height()) {
            float max_diff = 0.0f;
            double total_diff = 0.0;
            int mismatched = 0;
            const int total = fb1->width() * fb1->height();
            for (int y = 0; y < fb1->height(); ++y) {
                for (int x = 0; x < fb1->width(); ++x) {
                    const Color a = fb1->get_pixel(x, y);
                    const Color b = fb2->get_pixel(x, y);
                    const float dr = std::abs(a.r - b.r);
                    const float dg = std::abs(a.g - b.g);
                    const float db = std::abs(a.b - b.b);
                    const float da = std::abs(a.a - b.a);
                    const float max_d = std::max({dr, dg, db, da});
                    max_diff = std::max(max_diff, max_d);
                    total_diff += dr + dg + db + da;
                    if (max_d > 3.0f / 255.0f) ++mismatched;
                }
            }
            const float mean_err = static_cast<float>(total_diff / (total * 4));
            std::printf("\n  Pixel diff between Run 1 and Run 2:\n");
            std::printf("    mismatched pixels: %d / %d (%.2f%%)\n",
                        mismatched, total, 100.0f * mismatched / total);
            std::printf("    mean channel error: %.6f\n", mean_err);
            std::printf("    max channel error: %.6f  (%.2f/255)\n", max_diff, max_diff * 255.0f);
        }
    }

    return (unique_count == 1) ? 0 : 1;
}
