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

// ── Bug #7 fix — Heap owner for the FT_Face resource ───────────────
// `FT_Face` is itself a pointer typedef (`FT_Face` = `FT_FaceRec_*`).
// `std::shared_ptr<T>` where T is a pointer-typedef cannot be
// constructed directly from a `T*` raw + custom deleter because the
// (Y*, D) ctor's template-y deduction forces Y = element type of the
// inner pointer (i.e. Y = `FT_FaceRec_`), producing a signature
// `shared_ptr(Y* = FT_FaceRec_**, D)` that mismatches our raw `FT_Face`
// (= `FT_FaceRec_*`).  The canonical workaround for "shared_ptr over a
// pointer-typedef" is to wrap the raw in a heap-owned storage and use
// the aliasing ctor: `shared_ptr<T>(other_shared_ptr, T*)`.  The
// storage type's destructor runs when the LAST shared_ptr drops, so
// FT_Done_Face fires once, exactly on the last reference release, with
// the same acquire/release semantics the original deleter had.
namespace ft_face_cache_detail {
struct FTFaceStorage {
    FT_Face raw{nullptr};
    FTFaceStorage() noexcept = default;
    explicit FTFaceStorage(FT_Face f) noexcept : raw(f) {}
    ~FTFaceStorage() {
        if (raw != nullptr) {
            FT_Done_Face(raw);
        }
    }

    // Non-copyable / non-movable — the resource is heap-owned and the
    // shared_ptr is the only valid handle.  Prevents accidental
    // double-destruction from a stray copy.
    FTFaceStorage(const FTFaceStorage&) = delete;
    FTFaceStorage& operator=(const FTFaceStorage&) = delete;
    FTFaceStorage(FTFaceStorage&&) = delete;
    FTFaceStorage& operator=(FTFaceStorage&&) = delete;
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

// ── TICKET-069: RAII guard for the debug-only in_flight_ counter ────────
//
// Definition deliberately placed here (not in the header) so the field can
// stay private on FreeTypeFaceCache without polluting the public surface
// with an extra friend declaration.  Stripped in release builds (NDEBUG).
#ifndef NDEBUG
namespace {
struct InFlightGuard {
    std::atomic<int>& counter;
    explicit InFlightGuard(std::atomic<int>& c) noexcept : counter(c) {
        counter.fetch_add(1, std::memory_order_relaxed);
    }
    ~InFlightGuard() {
        counter.fetch_sub(1, std::memory_order_relaxed);
    }
    InFlightGuard(const InFlightGuard&)            = delete;
    InFlightGuard& operator=(const InFlightGuard&) = delete;
    InFlightGuard(InFlightGuard&&)                 = delete;
    InFlightGuard& operator=(InFlightGuard&&)      = delete;
};
} // namespace
#endif // !defined(NDEBUG)

FreeTypeFaceCache::FreeTypeFaceCache(std::atomic<bool>* fence) noexcept : fence_(fence) {}

#ifndef NDEBUG
// TICKET-069: enforce the lease-anchor lifetime contract at runtime.  The
// unordered_map destructor that follows this assert drops shared_ptrs —
// potentially the LAST ref drop, firing `FT_Done_Face(raw)` while another
// thread might still be inside get_face() (use-after-free).  A concurrent
// get_face() call increments `in_flight_` (RAII guard in get_face()) so
// this load catches the bug before the destructor proceeds.  Acquire
// ordering here guarantees all relaxed `fetch_sub` writes from any
// in-flight get_face() return path complete before we sample the counter.
FreeTypeFaceCache::~FreeTypeFaceCache() {
    assert(in_flight_.load(std::memory_order_acquire) == 0 &&
           "FreeTypeFaceCache destroyed while get_face() in flight "
           "(TICKET-069: lease-anchor lifetime contract violated; "
           "ensure no concurrent get_face() calls before destruction)");
}
#else
FreeTypeFaceCache::~FreeTypeFaceCache() = default;
#endif

std::shared_ptr<FT_Face> FreeTypeFaceCache::get_face(
    const std::string& resolved_path,
    int face_index,
    f32 font_size
) {
#ifndef NDEBUG
    // TICKET-069: RAII increment.  Placement BEFORE the lock_guard means
    // even a thread BLOCKED on mutex_ acquisition counts as in-flight, so
    // concurrent destruction while threads are queued on the mutex fires
    // the destructor assert.  Decrement fires on every return path
    // (normal, early-return, throw via std::runtime_error) — the guard's
    // destructor runs during scope unwind.
    InFlightGuard guard(in_flight_);
#endif
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

    auto storage = std::make_shared<ft_face_cache_detail::FTFaceStorage>(raw);
    // Aliasing ctor: shared_ptr<FT_Face>(shared_ptr<FTFaceStorage>, T*)
    // — keeps `storage` alive while storing a pointer into its `raw`
    // member.  T* = FT_Face* = FT_FaceRec_**; `&storage->raw` is exactly
    // an FT_Face*.  When the last shared_ptr<FT_Face> (and any other
    // shared_ptr<FTFaceStorage> aliases) drops, FTFaceStorage's
    // destructor fires FT_Done_Face(raw) once and only once.
    auto shared = std::shared_ptr<FT_Face>(storage, &storage->raw);

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
    // Drop stale (zeroed-out) entries before searching for a hit so the
    // pool never grows unboundedly with dead slots.
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
    return BLImage(w, h, static_cast<BLFormat>(fmt));
}

void TextScratchState::release_surface(BLImage img) {
    if (surface_pool.size() >= kMaxPooledSurfaces) return;
    BLImageData data;
    if (img.getData(&data) == BL_SUCCESS) {
        // emplace_back routes through SurfaceEntry's explicit ctor so the
        // BLFormat argument is preserved as-is (no brace-init narrowing).
        surface_pool.emplace_back(std::move(img), data.size.w, data.size.h, data.format);
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
    // Dereference (operator*) returns FT_Face& (= FT_FaceRec_*&) — the
    // underlying FreeType handle itself.  `.get()` would yield
    // FT_Face* = FT_FaceRec_*_*, which is wrong because handle.ft_face
    // is `FT_Face` (one pointer, not two).
    handle.ft_face     = handle.ft_lifeline ? *handle.ft_lifeline : nullptr;
    handle.outlines    = &outlines;
#endif

    return handle;
}

} // namespace chronon3d
