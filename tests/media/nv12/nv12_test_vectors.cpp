#include "nv12_test_vectors.hpp"

namespace chronon3d::nv12_equiv {

namespace {

// xorshift64* — deterministic, dependency-free, reproducible across runs.
struct XorShift64 {
    std::uint64_t state{0};
    explicit XorShift64(std::uint64_t seed) : state(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
    std::uint64_t next() {
        std::uint64_t x = state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        state = x;
        return x * 0x2545F4914F6CDD1DULL;
    }
    std::uint32_t next_u8() { return static_cast<std::uint32_t>(next() & 0xFFU); }
};

void fill_legal_noise(HostNv12Planes& out, int width, int height, std::uint64_t seed) {
    XorShift64 rng(seed);
    out.y.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (auto& v : out.y) {
        v = static_cast<std::uint8_t>(16 + (rng.next_u8() % 220));  // 16..235
    }
    const std::size_t uv_bytes = static_cast<std::size_t>(width) *
        static_cast<std::size_t>((height + 1) / 2);
    out.uv.assign(uv_bytes, 128);
    // Interleaved U/V per row; keep every pair inside 16..240.
    std::size_t i = 0;
    while (i + 1 < out.uv.size()) {
        out.uv[i]     = static_cast<std::uint8_t>(16 + (rng.next_u8() % 225));  // 16..240
        out.uv[i + 1] = static_cast<std::uint8_t>(16 + (rng.next_u8() % 225));
        i += 2;
    }
}

} // namespace

HostNv12Planes make_background_nv12(int width, int height, std::uint64_t seed) {
    HostNv12Planes out;
    fill_legal_noise(out, width, height, seed);
    return out;
}

HostRgba make_rgba_overlay(int width, int height, std::uint64_t seed, int alpha_mode) {
    HostRgba out;
    out.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    XorShift64 rng(seed);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // Strongly chromatic deterministic pattern: horizontal hue sweep
            // plus a spatial ripple so scale/crop/translation cases sample
            // different source texels.
            const float u = static_cast<float>(x) / static_cast<float>(width);
            const float v = static_cast<float>(y) / static_cast<float>(height);
            float r = 0.5f + 0.5f * ((x + y) % 16) / 15.0f;
            float g = 0.25f + 0.75f * u;
            float b = 0.25f + 0.75f * (1.0f - v);
            r = r * 0.6f + (u < 0.5f ? 0.5f : 0.0f) * 0.4f;  // keep chroma strong
            float a;
            switch (alpha_mode) {
                case 1:  a = 0.5f;                                    break;
                case 2:  a = 0.15f + 0.85f * u;                       break;
                default: a = (x == 0 || y == 0 || x == width - 1 || y == height - 1)
                                ? 0.0f                                // hard edge for boundary checks
                                : 1.0f;                               break;
            }
            out.push_back(r);
            out.push_back(g);
            out.push_back(b);
            out.push_back(a);
        }
    }
    (void)rng;
    return out;
}

HostNv12Planes make_nv12_overlay(int width, int height, std::uint64_t seed) {
    HostNv12Planes out;
    fill_legal_noise(out, width, height, seed ^ 0xA5A5A5A5ULL);
    return out;
}

std::vector<Nv12Case> nv12_equivalence_corpus() {
    using Kind = chronon3d::media::CudaLayerResourceKind;
    std::vector<Nv12Case> cases;

    auto full_overlay = [](std::uint32_t w, std::uint32_t h, int alpha_mode) {
        OverlaySpec o;
        o.resource_width = w;
        o.resource_height = h;
        o.kind = Kind::Rgba;
        o.src_x0 = 0.0f; o.src_y0 = 0.0f; o.src_x1 = 1.0f; o.src_y1 = 1.0f;
        o.dst_x0 = 0.0f; o.dst_y0 = 0.0f;
        return o;  // dst_x1/y1 patched by the caller with the canvas dims
    };

    const char* determinism_note =
        "Post-unification contract (Wave 1D): the direct NV12 batch path is "
        "the single authority (media::video::CudaNv12Compositor). Two "
        "independent instances of that authority must produce bit-identical "
        "planes for the same request (determinism is the retained "
        "equivalence property once the second authority was deleted).";

    // ── 1920x1080 / 1280x720 full-frame single overlays ────────────────
    {
        Nv12Case c;
        c.name = "full_frame_full_opacity_1920x1080";
        c.width = 1920; c.height = 1080;
        auto o = full_overlay(320, 240, 0);
        o.dst_x1 = 1920.0f; o.dst_y1 = 1080.0f;
        c.overlays.push_back(o);
        c.expect = Nv12Case::Expect::DeterministicComposite;
        c.rationale = determinism_note;
        cases.push_back(std::move(c));
    }
    {
        Nv12Case c;
        c.name = "full_frame_partial_opacity_1280x720";
        c.width = 1280; c.height = 720;
        auto o = full_overlay(320, 240, 1);
        o.opacity = 0.5f;
        o.dst_x1 = 1280.0f; o.dst_y1 = 720.0f;
        c.overlays.push_back(o);
        c.expect = Nv12Case::Expect::DeterministicComposite;
        c.rationale = determinism_note;
        cases.push_back(std::move(c));
    }

    // ── transform cases: scale, translation, crop ──────────────────────
    {
        Nv12Case c;
        c.name = "scaled_overlay_640x360_on_1280x720";
        c.width = 1280; c.height = 720;
        auto o = full_overlay(256, 256, 2);
        o.dst_x0 = 300.0f; o.dst_y0 = 180.0f;
        o.dst_x1 = 940.0f; o.dst_y1 = 540.0f;   // 640x360 target (scale)
        c.overlays.push_back(o);
        c.expect = Nv12Case::Expect::DeterministicComposite;
        c.rationale = determinism_note;
        cases.push_back(std::move(c));
    }
    {
        Nv12Case c;
        c.name = "cropped_translated_overlay_1280x720";
        c.width = 1280; c.height = 720;
        auto o = full_overlay(256, 256, 0);
        o.src_x0 = 0.25f; o.src_y0 = 0.25f;
        o.src_x1 = 0.75f; o.src_y1 = 0.75f;     // source crop
        o.dst_x0 = 900.0f; o.dst_y0 = 90.0f;    // translated into the canvas
        o.dst_x1 = 1180.0f; o.dst_y1 = 370.0f;
        c.overlays.push_back(o);
        c.expect = Nv12Case::Expect::DeterministicComposite;
        c.rationale = determinism_note;
        cases.push_back(std::move(c));
    }

    // ── odd dimensions ──────────────────────────────────────────────────
    {
        Nv12Case c;
        c.name = "odd_width_1919x1080";
        c.width = 1919; c.height = 1080;
        auto o = full_overlay(257, 240, 2);
        o.dst_x1 = 1919.0f; o.dst_y1 = 1080.0f;
        c.overlays.push_back(o);
        c.expect = Nv12Case::Expect::DeterministicComposite;
        c.rationale = determinism_note;
        cases.push_back(std::move(c));
    }
    {
        Nv12Case c;
        c.name = "odd_height_1920x1079";
        c.width = 1920; c.height = 1079;
        auto o = full_overlay(320, 241, 2);
        o.dst_x1 = 1920.0f; o.dst_y1 = 1079.0f;
        c.overlays.push_back(o);
        c.expect = Nv12Case::Expect::DeterministicComposite;
        c.rationale = determinism_note;
        cases.push_back(std::move(c));
    }

    // ── multiple overlays: Normal + Add stacking ────────────────────────
    {
        Nv12Case c;
        c.name = "multiple_overlays_normal_and_add_1920x1080";
        c.width = 1920; c.height = 1080;
        auto base = full_overlay(320, 240, 2);
        base.opacity = 0.6f;
        base.dst_x1 = 1920.0f; base.dst_y1 = 1080.0f;
        c.overlays.push_back(base);

        auto green = full_overlay(256, 256, 0);
        green.dst_x0 = 300.0f; green.dst_y0 = 200.0f;
        green.dst_x1 = 700.0f; green.dst_y1 = 600.0f;
        c.overlays.push_back(green);

        auto add = full_overlay(192, 192, 1);
        add.blend_mode = 1;  // Add
        add.opacity = 0.8f;
        add.dst_x0 = 600.0f; add.dst_y0 = 400.0f;
        add.dst_x1 = 1000.0f; add.dst_y1 = 800.0f;
        c.overlays.push_back(add);

        c.expect = Nv12Case::Expect::DeterministicComposite;
        c.rationale = determinism_note;
        cases.push_back(std::move(c));
    }

    // ── boundary pixels: overlay partially outside the canvas ───────────
    {
        Nv12Case c;
        c.name = "boundary_overlay_out_of_frame_1280x720";
        c.width = 1280; c.height = 720;
        auto o = full_overlay(320, 240, 0);
        o.dst_x0 = -120.0f; o.dst_y0 = -40.0f;   // negative origin (edge guard)
        o.dst_x1 = 500.0f;  o.dst_y1 = 400.0f;
        c.overlays.push_back(o);
        c.expect = Nv12Case::Expect::DeterministicComposite;
        c.rationale = determinism_note;
        cases.push_back(std::move(c));
    }

    // ── zero-contribution foreground: must be bit-exact bg passthrough ──
    {
        Nv12Case c;
        c.name = "zero_contribution_bg_passthrough_1280x720";
        c.width = 1280; c.height = 720;
        auto o = full_overlay(320, 240, 0);
        o.opacity = 0.0f;
        o.dst_x1 = 1280.0f; o.dst_y1 = 720.0f;
        c.overlays.push_back(o);
        auto o2 = full_overlay(160, 120, 1);
        o2.opacity = 0.0f;
        o2.dst_x0 = 100.0f; o2.dst_y0 = 100.0f;
        o2.dst_x1 = 400.0f; o2.dst_y1 = 300.0f;
        c.overlays.push_back(o2);
        c.expect = Nv12Case::Expect::BitExactPassthrough;
        c.rationale =
            "Zero foreground contribution (both overlays at opacity 0): the "
            "unified direct-batch path must copy the legal-range background "
            "unchanged, so output equals background bit-exactly and the two "
            "independent instances agree byte-for-byte.";
        cases.push_back(std::move(c));
    }

    // ── NV12-kind overlay: outside the unified direct-batch contract ────
    {
        Nv12Case c;
        c.name = "nv12_source_overlay_rejected_1280x720";
        c.width = 1280; c.height = 720;
        OverlaySpec o;
        o.resource_width = 320;
        o.resource_height = 240;
        o.kind = Kind::Nv12;
        o.src_x0 = 0.0f; o.src_y0 = 0.0f; o.src_x1 = 1.0f; o.src_y1 = 1.0f;
        o.dst_x0 = 0.0f; o.dst_y0 = 0.0f; o.dst_x1 = 1280.0f; o.dst_y1 = 720.0f;
        c.overlays.push_back(o);
        c.expect = Nv12Case::Expect::RejectedLayer;
        c.rationale =
            "Post-unification contract: the direct NV12 batch path accepts "
            "RGBA-kind linear overlay resources only (resource.rgba must be "
            "non-null). An NV12-kind overlay resource carries y/uv planes "
            "instead and is rejected (returns false) by both independent "
            "instances of the unified compositor.";
        cases.push_back(std::move(c));
    }

    return cases;
}

} // namespace chronon3d::nv12_equiv
