#include <chronon3d/backends/software/software_renderer.hpp>

#include "primitive_renderer.hpp"
#include "utils/render_effects_processor.hpp"

#include <immintrin.h>
#include <chronon3d/backends/software/builtin_processors.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/backends/software/fake_extruded_text_renderer.hpp>

namespace chronon3d {

namespace {

inline bool composite_layer_normal_avx2(Framebuffer& dst, const Framebuffer& src) {
#if defined(__AVX2__) || defined(_MSC_VER)
    const i32 w = dst.width();
    const i32 h = dst.height();
    if (src.width() != w || src.height() != h) {
        return false;
    }

    const __m256 one = _mm256_set1_ps(1.0f);
    const __m256 rgb_mask = _mm256_castsi256_ps(_mm256_set_epi32(0, -1, -1, -1, 0, -1, -1, -1));

    for (i32 y = 0; y < h; ++y) {
        Color* dst_row = dst.pixels_row(y);
        const Color* src_row = src.pixels_row(y);
        i32 x = 0;
        for (; x + 1 < w; x += 2) {
            const __m256 srcv = _mm256_loadu_ps(reinterpret_cast<const float*>(src_row + x));
            const __m256 dstv = _mm256_loadu_ps(reinterpret_cast<const float*>(dst_row + x));
            const __m256 src_a = _mm256_permute_ps(srcv, _MM_SHUFFLE(3, 3, 3, 3));
            const __m256 one_minus_a = _mm256_sub_ps(one, src_a);
            const __m256 src_rgb = _mm256_and_ps(srcv, rgb_mask);
            const __m256 dst_term = _mm256_mul_ps(dstv, one_minus_a);
            const __m256 rgb_out = _mm256_add_ps(src_rgb, dst_term);
            const __m256 alpha_out = _mm256_add_ps(src_a, dst_term);
            const __m256 outv = _mm256_blend_ps(rgb_out, alpha_out, 0x88);
            _mm256_storeu_ps(reinterpret_cast<float*>(dst_row + x), outv);
        }
        for (; x < w; ++x) {
            dst_row[x] = compositor::blend(src_row[x].unpremultiplied(), dst_row[x], BlendMode::Normal);
        }
    }
    return true;
#else
    (void)dst;
    (void)src;
    return false;
#endif
}

} // namespace

SoftwareRenderer::SoftwareRenderer()
    : m_software_registry(std::make_unique<renderer::SoftwareRegistry>()) {
    renderer::register_builtin_processors(*m_software_registry);
}

std::unique_ptr<Framebuffer> SoftwareRenderer::render_frame(const Composition& comp,
                                                            Frame frame) {
    return software_internal::render_frame(*this, comp, frame);
}

std::shared_ptr<Framebuffer> SoftwareRenderer::render_scene(const Scene& scene,
                                                            const Camera& camera, i32 width,
                                                            i32 height) {
    return std::shared_ptr<Framebuffer>(
        render_scene_internal(scene, camera, width, height, 0, 0.0f).release());
}

std::shared_ptr<Framebuffer> SoftwareRenderer::render_scene(
    const Scene& scene, const std::optional<Camera2_5D>& camera, i32 width, i32 height) {
    Scene effective_scene = scene;
    if (camera.has_value()) {
        effective_scene.set_camera_2_5d(*camera);
    }

    Camera default_camera;
    return std::shared_ptr<Framebuffer>(
        render_scene_internal(effective_scene, default_camera, width, height, 0, 0.0f).release());
}

std::string SoftwareRenderer::debug_render_graph(const Scene& scene, const Camera& camera,
                                                  i32 width, i32 height, Frame frame,
                                                  f32 frame_time) const {
    return software_internal::debug_render_graph(*this, scene, camera, width, height, frame,
                                                  frame_time);
}

std::unique_ptr<Framebuffer>
SoftwareRenderer::render_scene_internal(const Scene& scene, const Camera& camera, i32 width,
                                        i32 height, Frame frame, f32 frame_time) {
    return software_internal::render_scene_internal(*this, scene, camera, width, height, frame,
                                                    frame_time);
}

void SoftwareRenderer::apply_blur(Framebuffer& fb, f32 radius) {
    renderer::apply_blur(fb, radius);
}

void SoftwareRenderer::draw_node(Framebuffer& fb, const RenderNode& node,
                                 const RenderState& state, const Camera& camera, i32 width,
                                 i32 height) {
    auto& registry = *m_software_registry;
    if (auto* processor = registry.get_shape(node.shape.type)) {
        processor->draw(fb, node, state, camera, width, height);
    } else {
        software_internal::draw_node(*this, fb, node, state, camera, width, height);
    }
}

void SoftwareRenderer::apply_effect_stack(Framebuffer& fb, const EffectStack& stack) {
    auto& registry = *m_software_registry;
    for (const auto& effect : stack) {
        if (!effect.enabled) continue;
        if (auto* processor = registry.get_effect(effect.params.index())) {
            processor->apply(fb, effect.params);
        } else {
            EffectStack single_effect{effect};
            renderer::apply_effect_stack(fb, single_effect);
        }
    }
}

void SoftwareRenderer::composite_layer(Framebuffer& dst, const Framebuffer& src, BlendMode mode) {
    if (mode == BlendMode::Normal && composite_layer_normal_avx2(dst, src)) {
        return;
    }

    const i32 w = dst.width(), h = dst.height();
    for (i32 y = 0; y < h; ++y) {
        for (i32 x = 0; x < w; ++x) {
            Color s = src.get_pixel(x, y);
            if (s.a <= 0.0f)
                continue;
            s = s.unpremultiplied();
            dst.set_pixel(x, y, compositor::blend(s, dst.get_pixel(x, y), mode));
        }
    }
}

} // namespace chronon3d
