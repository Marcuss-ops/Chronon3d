#pragma once

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/text/text_rasterizer_utils.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/cache/lru_cache.hpp>
#include <blend2d.h>
#include <mutex>

namespace chronon3d::renderer {

using CacheKey = u64;
using ShadowCache = cache::LruCache<CacheKey, std::shared_ptr<BLImage>>;

bool is_affine_transform(const Mat4& m);
bool has_non_translation(const Mat4& m);

CacheKey hash_text_shape(const TextShape& text, float effective_size);
CacheKey hash_glow_params(const RenderNode& node, float effective_size);
CacheKey hash_shadow_params(const RenderNode& node, float effective_size, size_t index);

size_t resolve_cache_max_mb(const char* env_name, size_t default_mb);
extern std::mutex g_text_shadow_cache_mutex;
extern std::mutex g_text_glow_cache_mutex;

ShadowCache& get_shadow_cache();
ShadowCache& get_glow_cache();

void draw_text_shadow(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state, const TextRasterization& raster, const TextShadow& shadow, size_t index);
void draw_text_glow(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state, const TextRasterization& raster);

void clear_text_glow_cache();
void clear_text_shadow_cache();

std::unique_ptr<ShapeProcessor> create_text_processor();

} // namespace chronon3d::renderer
