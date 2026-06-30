#include <chronon3d/backends/text/text_render_resources.hpp>

#include <chronon3d/assets/asset_resolver.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

namespace chronon3d {

// ═══════════════════════════════════════════════════════════════════════════
// BLFontFaceCache
// ═══════════════════════════════════════════════════════════════════════════

const BLFontFace* BLFontFaceCache::get_face(const std::string& resolved_path) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = faces_.find(resolved_path);
    if (it != faces_.end()) {
        return &it->second;
    }

    // Cat-2 font preflight — see header.  When the I/O fence is armed
    // (preflight_fonts() set + arm_atomic() succeeded) any unforeseen
    // cache miss produces a deterministic std::runtime_error here so the
    // regression test (tests/text/test_font_io_fence.cpp) catches
    // re-introduction of synchronous font I/O on the render thread.
    if (fence_ && fence_->load(std::memory_order_acquire)) {
        throw std::runtime_error(
            std::string{"BLFontFaceCache: synchronous font I/O on render thread (path='"}
            + resolved_path + "').  Pre-primed via SoftwareRenderer::preflight_fonts "
              "before arming the fence; this font was not pre-primed.");
    }

    BLFontFace face;
    if (face.createFromFile(resolved_path.c_str()) != BL_SUCCESS) {
        spdlog::error("[text-resources] BLFontFaceCache: failed to load {}", resolved_path);
        return nullptr;
    }

    auto [inserted, _] = faces_.emplace(resolved_path, std::move(face));
    return &inserted->second;
}

bool BLFontFaceCache::contains(const std::string& resolved_path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return faces_.find(resolved_path) != faces_.end();
}

// ═══════════════════════════════════════════════════════════════════════════
// FreeTypeFaceCache
// ═══════════════════════════════════════════════════════════════════════════

#ifdef CHRONON3D_ENABLE_TEXT

FT_Library FreeTypeFaceCache::shared_library() {
    static FT_Library lib = [] {
        FT_Library l = nullptr;
        if (FT_Init_FreeType(&l) != 0) {
            spdlog::error("[text-resources] FreeTypeFaceCache: FT_Init_FreeType failed");
        }
        return l;
    }();
    return lib;
}

// ── Bug #7 fix — Custom deleter for shared_ptr<FT_Face> ──────────────
// The deleter is a class type (not a lambda) so it has full type identity
// and can live in a static-local.  FT_Done_Face is called exactly once,
// exactly on the last shared_ptr drop; ref-count decrements are
// atomic, so the destroy call cannot fire while another thread still
// holds a shared_ptr to the same face.
namespace ft_face_cache_detail {
struct FTFaceDeleter {
    void operator()(FT_Face f) const noexcept {
        if (f != nullptr) {
            FT_Done_Face(f);
        }
    }
};
} // namespace ft_face_cache_detail

// `FreeTypeFaceCache` DESTRUCTOR CONTRACT (Bug #7 fix):
//
// Do NOT hold `mutex_` from inside the destructor.  Holding it would
// deadlock if any other thread still holds a `FontFaceHandle::ft_lifeline`
// pointing into this cache's dictionary (and the whole point of the lease
// anchor is to allow handles to outlive cache eviction/destruction, so
// such handles are explicitly expected).  Lifetime contract: the cache
// must NOT be destroyed while concurrent `get_face()` calls are in
// flight; once destruction begins, all outstanding handles are the
// callers' responsibility and their shared_ptr ref-count keeps any inline
// `FT_Done_Face` calls reachable.
//
// Implicit destruction of `faces_` (unordered_map of shared_ptr<FT_Face>)
// drops each entry's shared_ptr; the *last* ref drop is what fires
// `FT_Done_Face` via the custom deleter, AT THE LATEST when the last
// `FontFaceHandle::ft_lifeline` is destroyed.

FreeTypeFaceCache::FreeTypeFaceCache(std::atomic<bool>* fence) noexcept : fence_(fence) {}
FreeTypeFaceCache::~FreeTypeFaceCache() = default;

std::shared_ptr<FT_Face> FreeTypeFaceCache::get_face(
    const std::string& resolved_path,
    int face_index,
    f32 font_size
) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Reject invalid requests BEFORE touching FreeType so a caller bug
    // doesn't silently produce a size-1 face that renders garbled glyphs.
    if (resolved_path.empty()) {
        spdlog::warn("[text-resources] FreeTypeFaceCache: empty resolved path");
        return nullptr;
    }
    if (font_size <= 0.0f) {
        spdlog::warn(
            "[text-resources] FreeTypeFaceCache: non-positive font_size={} for {}",
            font_size, resolved_path);
        return nullptr;
    }

    // Quantize size: identify a per-pixel-size bucket so concurrent
    // requests at the same pixel size hit the same map entry (one
    // FT_New_Face call, not one per call) AND requests at different
    // sizes get distinct FT_Face objects (no FT_Set_Pixel_Sizes race).
    const int size_bucket = static_cast<int>(std::ceil(font_size));
    const FTFaceKey key{resolved_path, face_index, size_bucket};

    auto it = faces_.find(key);
    if (it != faces_.end()) {
        // Hit: copy shared_ptr (atomic ref-count inc).  Caller now owns
        // its own reference; the cached entry remains for the next caller.
        return it->second;
    }

    // Cat-2 font preflight — symmetric with BLFontFaceCache::get_face.
    // Throws std::runtime_error if the I/O fence is armed AND this
    // (resolved_path, face_index, size_bucket) tuple was not
    // pre-primed by SoftwareRenderer::preflight_fonts.
    if (fence_ && fence_->load(std::memory_order_acquire)) {
        throw std::runtime_error(
            std::string{"FreeTypeFaceCache: synchronous FT_New_Face on render thread "
                        "(path='"}
            + resolved_path + "' face_index=" + std::to_string(face_index)
            + " size_bucket=" + std::to_string(size_bucket)
            + ").  Pre-primed via SoftwareRenderer::preflight_fonts before arming "
              "the fence; this font was not pre-primed.");
    }

    FT_Library lib = shared_library();
    if (lib == nullptr) return nullptr;

    FT_Face raw = nullptr;
    if (FT_New_Face(lib, resolved_path.c_str(), face_index, &raw) != 0) {
        spdlog::error(
            "[text-resources] FreeTypeFaceCache: FT_New_Face failed for {} face {}",
            resolved_path, face_index);
        return nullptr;
    }

    if (FT_Set_Pixel_Sizes(raw, 0, static_cast<FT_UInt>(size_bucket)) != 0) {
        spdlog::error(
            "[text-resources] FreeTypeFaceCache: FT_Set_Pixel_Sizes failed for {} size {}",
            resolved_path, size_bucket);
        FT_Done_Face(raw);
        return nullptr;
    }

    auto shared = std::shared_ptr<FT_Face>(
        raw, ft_face_cache_detail::FTFaceDeleter{});

    // `find` returned end under the same lock, so `emplace` cannot fail;
    // discard the bool with `[[maybe_unused]]` so the diagnostic stays
    // self-checking without `_ok` boolean-noise.
    [[maybe_unused]] auto emplace_result = faces_.emplace(key, shared);
    return shared;
}

// ═══════════════════════════════════════════════════════════════════════════
// GlyphOutlineBuilder — builds BLPath from FT_Outline per glyph
// ═══════════════════════════════════════════════════════════════════════════
//
// NOT a cache: every call freshly runs FT_Load_Glyph + FT_Outline_Decompose.
// The `mutex_` guards FreeType face mutation safety across concurrent
// build_path() calls, not stored-result caching.  See header for rationale.

BLPath GlyphOutlineBuilder::build_path(
    FT_Face ft_face,
    u32 glyph_id,
    float origin_x,
    float origin_y
) {
    BLPath path;
    if (!ft_face) return path;

    std::lock_guard<std::mutex> lock(mutex_);

    if (FT_Load_Glyph(ft_face, glyph_id, FT_LOAD_NO_BITMAP) != 0) return path;
    if (ft_face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) return path;

    constexpr float kScale = 1.0f / 64.0f;
    constexpr double kConicWeight = 1.0;

    struct DecomposeCtx {
        BLPath* path;
        double  off_x;
        double  off_y;
        bool    first_contour{true};
    };

    FT_Outline_Funcs funcs;
    funcs.move_to = [](const FT_Vector* to, void* user) -> int {
        auto* ctx = static_cast<DecomposeCtx*>(user);
        if (!ctx->first_contour) ctx->path->close();
        ctx->first_contour = false;
        ctx->path->moveTo(
            ctx->off_x + static_cast<double>(to->x) * kScale,
            ctx->off_y - static_cast<double>(to->y) * kScale);
        return 0;
    };
    funcs.line_to = [](const FT_Vector* to, void* user) -> int {
        auto* ctx = static_cast<DecomposeCtx*>(user);
        ctx->path->lineTo(
            ctx->off_x + static_cast<double>(to->x) * kScale,
            ctx->off_y - static_cast<double>(to->y) * kScale);
        return 0;
    };
    funcs.conic_to = [](const FT_Vector* control, const FT_Vector* to, void* user) -> int {
        auto* ctx = static_cast<DecomposeCtx*>(user);
        ctx->path->conicTo(
            ctx->off_x + static_cast<double>(control->x) * kScale,
            ctx->off_y - static_cast<double>(control->y) * kScale,
            ctx->off_x + static_cast<double>(to->x) * kScale,
            ctx->off_y - static_cast<double>(to->y) * kScale,
            kConicWeight);
        return 0;
    };
    funcs.cubic_to = [](const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* user) -> int {
        auto* ctx = static_cast<DecomposeCtx*>(user);
        ctx->path->cubicTo(
            ctx->off_x + static_cast<double>(c1->x) * kScale,
            ctx->off_y - static_cast<double>(c1->y) * kScale,
            ctx->off_x + static_cast<double>(c2->x) * kScale,
            ctx->off_y - static_cast<double>(c2->y) * kScale,
            ctx->off_x + static_cast<double>(to->x) * kScale,
            ctx->off_y - static_cast<double>(to->y) * kScale);
        return 0;
    };
    funcs.delta = 0;
    funcs.shift = 0;

    DecomposeCtx ctx{&path, static_cast<double>(origin_x), static_cast<double>(origin_y), true};
    FT_Outline_Decompose(&ft_face->glyph->outline, &funcs, &ctx);
    if (!ctx.first_contour) path.close();

    return path;
}

#endif // CHRONON3D_ENABLE_TEXT

// ═══════════════════════════════════════════════════════════════════════════
// TextScratchState & TextScratchManager (FASE 3 thread-safety)
// ═══════════════════════════════════════════════════════════════════════════

BLImage TextScratchState::acquire_surface(int w, int h, uint32_t fmt) {
    // Drop dead (zeroed-out) entries from previous releases.
    surface_pool.erase(
        std::remove_if(surface_pool.begin(), surface_pool.end(),
            [](const SurfaceEntry& e) { return e.w == 0; }),
        surface_pool.end());

    for (auto& e : surface_pool) {
        if (e.w == w && e.h == h && e.fmt == fmt) {
            BLImage img = std::move(e.image);
            e.w = 0;
            e.h = 0;
            return img;
        }
    }
    return BLImage(w, h, fmt);
}

void TextScratchState::release_surface(BLImage img) {
    if (surface_pool.size() >= kMaxPooledSurfaces) return;
    BLImageData data;
    if (img.getData(&data) == BL_SUCCESS) {
        surface_pool.push_back({std::move(img), data.size.w, data.size.h, data.format});
    }
}

TextScratchManager::Handle TextScratchManager::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!available_.empty()) {
        TextScratchState* state = available_.back().release();
        available_.pop_back();
        return Handle(state, this);
    }
    return Handle(new TextScratchState(), this);
}

void TextScratchManager::release(TextScratchState* state) {
    if (!state) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (available_.size() >= kMaxPooledStates) {
        // Pool is at capacity — drop the state on the floor; the unique_ptr
        // destructor frees the BLImages + vectors it owns without leaking.
        // This bounds memory under burst workloads (e.g. 64-thread burst
        // amortizes down to kMaxPooledStates retained states).
        delete state;
        return;
    }
    available_.emplace_back(state);
}

// ═══════════════════════════════════════════════════════════════════════════
// TextRenderResources
// ═══════════════════════════════════════════════════════════════════════════

FontFaceHandle TextRenderResources::resolve_handle(
    const std::string& font_path,
    f32 font_size,
    const assets::AssetResolver& resolver
) {
    FontFaceHandle handle;
    handle.font_size = font_size;

    // Resolve path
    std::string resolved;
    if (auto opt = resolver.resolve_lexical(font_path)) {
        resolved = opt->string();
    } else {
        resolved = font_path.empty() ? std::string{} : std::string{font_path};
    }

    // Look up BLFontFace
    handle.bl_face = bl_faces.get_face(resolved);

#ifdef CHRONON3D_ENABLE_TEXT
    // Load FreeType face (for stroke outlines).  Bug #7 fix: take
    // ownership of the cache's shared_ptr via ft_lifeline BEFORE
    // aliasing the raw pointer; this keeps the FT_Face alive for
    // the lifetime of THIS FontFaceHandle even if the cache's
    // dictionary entry is dropped by another thread's swap or by an
    // external eviction policy.
    constexpr int kPrimaryFaceIndex = 0;  // multi-face support is future PR
    handle.ft_lifeline = ft_faces.get_face(resolved, kPrimaryFaceIndex, font_size);
    handle.ft_face     = handle.ft_lifeline ? handle.ft_lifeline.get() : nullptr;
    handle.outlines    = &outlines;
#endif

    return handle;
}

} // namespace chronon3d
