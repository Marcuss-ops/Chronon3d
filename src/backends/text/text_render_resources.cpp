#include <chronon3d/backends/text/text_render_resources.hpp>

#include <chronon3d/assets/asset_resolver.hpp>

#include <spdlog/spdlog.h>

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

FreeTypeFaceCache::FreeTypeFaceCache() = default;

FreeTypeFaceCache::~FreeTypeFaceCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ft_face_) {
        FT_Done_Face(ft_face_);
        ft_face_ = nullptr;
    }
}

FT_Face FreeTypeFaceCache::get_face(const std::string& resolved_path, f32 font_size) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Fast path: same path and size, return cached face
    if (ft_face_ && resolved_path == loaded_path_) {
        FT_Set_Pixel_Sizes(ft_face_, 0, static_cast<FT_UInt>(std::ceil(font_size)));
        return ft_face_;
    }

    // Release stale face
    if (ft_face_) {
        FT_Done_Face(ft_face_);
        ft_face_ = nullptr;
    }

    FT_Library lib = shared_library();
    if (!lib) return nullptr;

    if (FT_New_Face(lib, resolved_path.c_str(), 0, &ft_face_) != 0) {
        spdlog::error("[text-resources] FreeTypeFaceCache: FT_New_Face failed for {}", resolved_path);
        return nullptr;
    }

    FT_Set_Pixel_Sizes(ft_face_, 0, static_cast<FT_UInt>(std::ceil(font_size)));
    loaded_path_ = resolved_path;
    loaded_size_ = font_size;
    return ft_face_;
}

// ═══════════════════════════════════════════════════════════════════════════
// GlyphOutlineCache
// ═══════════════════════════════════════════════════════════════════════════

BLPath GlyphOutlineCache::build_outline(
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
    // Load FreeType face (for stroke outlines)
    handle.ft_face = ft_faces.get_face(resolved, font_size);
    handle.outlines = &outlines;
#endif

    return handle;
}

} // namespace chronon3d
