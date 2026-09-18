// ==============================================================================
// src/scene/motion_contract_bridge.cpp — ADR-032 contract-zone converters.
//
// Pure, allocation-free, deterministic. The wire types are the PODs from
// <chronon/abi.h>; the internal types are the scene model's ContentRef and
// GLM math types. Nothing else participates: no session, no registry, no
// allocation, no diagnostics.
// ==============================================================================

#include <chronon3d/motion/contract_bridge.hpp>

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>

namespace chronon3d::motion {

namespace {

/// GLM column-major storage → the ABI's row-major, column-vector layout.
/// m is GLM's internal column-major array; out[r*4 + c] = m[c][r].
void pack_row_major(const Mat4& m, float (&out)[16]) noexcept {
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out[static_cast<size_t>(r) * 4 + c] = m[c][r];
        }
    }
}

/// Recompute the forward NDC depth of the local origin from the clip-space
/// product. Depth 0..1 (GLM_FORCE_DEPTH_ZERO_TO_ONE), perspective divide
/// guarded against w ≈ 0.
///
/// GLM is column-major: `m[column][row]`. The local origin is the fourth
/// *column* of clip_from_local, so its clip-space z is `m[3][2]` and its w is
/// `m[3][3]`. Reading `m[2][3]` instead picks up the z coefficient of the w
/// row, which is 0 for any affine view·world — i.e. every real camera — and
/// silently collapsed every layer to depth 0.
float depth_from_clip(const Mat4& clip_from_local) noexcept {
    const float w = clip_from_local[3][3];
    if (!std::isfinite(w) || std::fabs(w) < 1.0e-8f) {
        return 0.0f;
    }
    const float ndc_z = clip_from_local[3][2] / w;
    return std::clamp(ndc_z, 0.0f, 1.0f);
}

} // namespace

// ── Contract zone version ───────────────────────────────────────────────────
// The contract zone is the engine's, so its version has exactly one source:
// kContractZoneVersion above. On ELF the engine is also where the symbol is
// defined, which keeps a static build (CHRONON3D_BUILD_C_API=OFF, the default)
// free of an unresolved reference into a shared library it never links. That
// reference is what stopped the ADR-032 suites from building at all.
//
// Windows is the exception: abi.h expands CHRONON_API to __declspec(dllimport)
// for every consumer of the header, and MSVC forbids defining a dllimport
// function (C2491). There the C ABI wrapper, compiled with CHRONON3D_BUILD_DLL,
// remains the definition site and this one is skipped.
#if !defined(_WIN32)
extern "C" uint32_t chronon_contract_version(void) {
    return kContractZoneVersion;
}
#endif

bool contract_version_matches() noexcept {
    return chronon_contract_version() == kContractZoneVersion;
}

chronon_content_kind to_contract_kind(ContentKind kind) noexcept {
    switch (kind) {
        case ContentKind::Image:       return CHRONON_CONTENT_IMAGE;
        case ContentKind::Shape:       return CHRONON_CONTENT_SHAPE;
        case ContentKind::Text:        return CHRONON_CONTENT_TEXT;
        case ContentKind::Video:       return CHRONON_CONTENT_VIDEO;
        case ContentKind::Composition: return CHRONON_CONTENT_COMPOSITION;
        case ContentKind::Audio:       return CHRONON_CONTENT_AUDIO;
        case ContentKind::None:        return CHRONON_CONTENT_NONE;
    }
    return CHRONON_CONTENT_NONE;
}

chronon_content_metrics make_content_metrics(ContentKind kind,
                                             ContentAssetId id,
                                             uint32_t width_px,
                                             uint32_t height_px,
                                             float anchor_x,
                                             float anchor_y,
                                             uint64_t fingerprint) noexcept {
    chronon_content_metrics out{};
    out.struct_size = static_cast<uint32_t>(sizeof(chronon_content_metrics));
    out.kind = to_contract_kind(kind);
    out.content = (kind == ContentKind::None) ? 0u : id;
    out.width = width_px;
    out.height = height_px;
    out.anchor_x = std::clamp(anchor_x, 0.0f, 1.0f);
    out.anchor_y = std::clamp(anchor_y, 0.0f, 1.0f);
    out.fingerprint = fingerprint;
    return out;
}

chronon_layer_state make_layer_state(const LayerStateInput& in) noexcept {
    chronon_layer_state out{};
    out.struct_size = static_cast<uint32_t>(sizeof(chronon_layer_state));
    out.visible = in.visible ? 1u : 0u;
    out.reserved[0] = 0u;
    out.reserved[1] = 0u;
    out.reserved[2] = 0u;
    out.layer = in.layer;
    out.content = in.content;
    pack_row_major(in.clip_from_local, out.clip_from_local);
    out.opacity = std::clamp(in.opacity, 0.0f, 1.0f);
    out.depth = (in.depth >= 0.0f) ? std::clamp(in.depth, 0.0f, 1.0f)
                                   : depth_from_clip(in.clip_from_local);
    return out;
}

chronon_layer_state make_layer_state(const LayerStateInput& in,
                                     const ContentRef& content) noexcept {
    chronon_layer_state out = make_layer_state(in);
    if (content.empty()) {
        out.content = 0u;
        return out;
    }
    // ContentRef keeps opaque u64 ids for every kind; the id is the value
    // member shared by all asset-id alternatives.
    out.content = content.asset() ? *content.asset() : 0u;
    return out;
}

chronon_layer_state with_clip_from_local(const chronon_layer_state& base,
                                         const Mat4& clip_from_local) noexcept {
    chronon_layer_state out = base;
    pack_row_major(clip_from_local, out.clip_from_local);
    out.depth = depth_from_clip(clip_from_local);
    return out;
}

} // namespace chronon3d::motion
