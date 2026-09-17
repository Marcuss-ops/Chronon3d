#pragma once

// ADR-031 — one content dimension on `Layer`.
//
// `LayerKind` answers three questions at once (what a layer shows, what it does,
// how it is treated), so "does this layer carry content?" ended up re-written by
// hand in five places: `render_graph/pipeline/scene_refresh.cpp`,
// `render_graph/pipeline/refresh/source.cpp`,
// `render_graph/pipeline/refresh/multi_source.cpp`,
// `render_graph/builder/passes/graph_builder_source_pass.cpp` and
// `compiler/graph_builder/graph_builder_layer_pipeline.cpp` — two of them with
// byte-identical predicate bodies. Those call sites converge here.
//
// BOUNDARY: `ContentRef` holds opaque IDS ONLY. No text style, font, glyph,
// layout or raster type belongs in this engine — content is authored and
// resolved outside it, and this side only ever anchors, transforms and lifetimes
// a reference to it.
//
// PHASE (TICKET-LAYER-CONTENT-REF): in P1 `Layer::kind` is still the storage and
// `Layer::content()`/`Layer::role()` are pure functions of it, so no reader and
// no behaviour changes in this phase. P2 migrates the readers onto the
// predicates below; P3 flips the storage to `content` + `role` and derives
// `kind`.

#include <chronon3d/core/types/types.hpp>

#include <array>
#include <cstdint>
#include <variant>

namespace chronon3d {

/// Same underlying type as the canonical `chronon3d::AssetId`
/// (`assets/asset_registry.hpp`), repeated as `u64` on purpose: the scene model
/// must not pull the asset registry (xxhash, `<filesystem>`, `<mutex>`) into
/// every translation unit that includes `layer.hpp`. `0` means "no asset bound
/// yet" — a layer can legitimately know it carries an image before the image is
/// resolved.
using ContentAssetId = u64;

/// The content axis: what a layer carries, and nothing else.
enum class ContentKind : std::uint8_t {
    None,
    Image,
    Shape,
    Text,
    Video,
    Composition,
    Audio,
};

/// `Null = Layer + NoContent`, spelled as content instead of as a kind.
struct NoContent {
    static constexpr ContentKind kind = ContentKind::None;
};

struct ImageAssetId {
    ContentAssetId value{};
    static constexpr ContentKind kind = ContentKind::Image;
};

struct ShapeAssetId {
    ContentAssetId value{};
    static constexpr ContentKind kind = ContentKind::Shape;
};

struct TextAssetId {
    ContentAssetId value{};
    static constexpr ContentKind kind = ContentKind::Text;
};

struct VideoAssetId {
    ContentAssetId value{};
    static constexpr ContentKind kind = ContentKind::Video;
};

/// A nested composition reference, not a file asset: it names a composition, it
/// does not point at bytes.
struct CompositionRef {
    ContentAssetId value{};
    static constexpr ContentKind kind = ContentKind::Composition;
};

struct AudioAssetId {
    ContentAssetId value{};
    static constexpr ContentKind kind = ContentKind::Audio;
};

namespace detail {

/// Index → `ContentKind` table built from the variant's own alternative list, so
/// it can never drift from `ContentRef::Storage` and needs no `std::visit` (which
/// is what keeps `ContentRef` usable in constant expressions). Adding an
/// alternative updates the table automatically; a new content type without a
/// `kind` member fails to compile here.
template <class Variant>
struct content_kinds;

template <class... Alternatives>
struct content_kinds<std::variant<Alternatives...>> {
    static constexpr std::array<ContentKind, sizeof...(Alternatives)> value{Alternatives::kind...};
};

} // namespace detail

/// What a layer carries. Opaque ids only — this type is the whole content
/// vocabulary of this engine.
class ContentRef {
public:
    using Storage = std::variant<NoContent,
                                 ImageAssetId,
                                 ShapeAssetId,
                                 TextAssetId,
                                 VideoAssetId,
                                 CompositionRef,
                                 AudioAssetId>;

    constexpr ContentRef() noexcept = default;
    constexpr ContentRef(NoContent) noexcept {}
    constexpr ContentRef(ImageAssetId id) noexcept : ref_(id) {}
    constexpr ContentRef(ShapeAssetId id) noexcept : ref_(id) {}
    constexpr ContentRef(TextAssetId id) noexcept : ref_(id) {}
    constexpr ContentRef(VideoAssetId id) noexcept : ref_(id) {}
    constexpr ContentRef(CompositionRef id) noexcept : ref_(id) {}
    constexpr ContentRef(AudioAssetId id) noexcept : ref_(id) {}

    [[nodiscard]] constexpr ContentKind kind() const noexcept {
        return detail::content_kinds<Storage>::value[ref_.index()];
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return std::holds_alternative<NoContent>(ref_);
    }

    [[nodiscard]] constexpr const Storage& storage() const noexcept { return ref_; }

    /// Bound asset id, or `nullptr` for `NoContent`.
    [[nodiscard]] constexpr const ContentAssetId* asset() const noexcept {
        if (const auto* id = std::get_if<ImageAssetId>(&ref_)) return &id->value;
        if (const auto* id = std::get_if<ShapeAssetId>(&ref_)) return &id->value;
        if (const auto* id = std::get_if<TextAssetId>(&ref_)) return &id->value;
        if (const auto* id = std::get_if<VideoAssetId>(&ref_)) return &id->value;
        if (const auto* id = std::get_if<CompositionRef>(&ref_)) return &id->value;
        if (const auto* id = std::get_if<AudioAssetId>(&ref_)) return &id->value;
        return nullptr;
    }

    /// Canonical "does this layer put pixels on screen?" predicate. Replaces the
    /// hand-written `kind == Normal || kind == Shape || kind == Text [|| kind ==
    /// Video] [|| kind == Precomp]` chains. Written out rather than phrased as
    /// "everything but None and Audio" so that adding a `ContentKind` forces an
    /// explicit decision here.
    [[nodiscard]] constexpr bool draws() const noexcept {
        switch (kind()) {
            case ContentKind::Image:
            case ContentKind::Shape:
            case ContentKind::Text:
            case ContentKind::Video:
            case ContentKind::Composition:
                return true;
            case ContentKind::None:
            case ContentKind::Audio:
                return false;
        }
        return false;
    }

private:
    Storage ref_{NoContent{}};
};

/// The role axis: what a layer does, independent of what it carries.
enum class LayerRole : std::uint8_t {
    /// Draws and composites its content.
    Rendered,
    /// Draws nothing; exists to parent transforms and to scope effect stacks.
    Null,
    /// Draws nothing of its own; applies effects to what is below it.
    Adjustment,
    /// Camera rig. See ADR-031 second exit: camera authoring moves to the motion
    /// side, this value survives only until `CameraState` compilation lands.
    Camera,
    /// Audio-only; contributes to the timeline, never to the frame.
    Audio,
};

} // namespace chronon3d
