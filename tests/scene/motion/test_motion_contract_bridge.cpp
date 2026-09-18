// ==============================================================================
// tests/scene/motion/test_motion_contract_bridge.cpp
//
// ADR-032 — contract-zone converter tests.
//
//   [C1] POD purity        — the wire records stay trivially copyable +
//                            standard layout, and struct_size is honest.
//   [C2] version handshake — chronon_contract_version() == kContractZoneVersion.
//   [C3] kind mapping      — ContentKind ↔ chronon_content_kind 1:1 (ABI-frozen
//                            values), NoContent → NONE with id 0.
//   [C4] metrics clamps    — anchors clamp into [0,1].
//   [C5] layer state       — visibility bit, opacity clamp, zeroed reserved.
//   [C6] matrix round-trip — GLM column-vector ↔ ABI row-major, bit-identical.
//   [C7] depth semantics   — explicit depth wins; negative recomputes from
//                            clip; w≈0 is safe.
//   [C8] ContentRef overload — opaque id extraction per ADR-031.
// ==============================================================================
#define DOCTEST_CONFIG_DISABLE_TEST_EXN_CATCH 0
#include <doctest/doctest.h>

#include <chronon/abi.h>

#include <chronon3d/motion/contract_bridge.hpp>
#include <chronon3d/scene/model/layer/content_ref.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>
#include <cstring>
#include <type_traits>

using namespace chronon3d;
using namespace chronon3d::motion;

// ── [C1] POD purity ─────────────────────────────────────────────────────────
TEST_CASE("ADR-032 [C1]: contract records are POD and self-describing") {
    static_assert(std::is_trivially_copyable_v<chronon_content_metrics>);
    static_assert(std::is_standard_layout_v<chronon_content_metrics>);
    static_assert(std::is_trivially_copyable_v<chronon_layer_state>);
    static_assert(std::is_standard_layout_v<chronon_layer_state>);

    const auto metrics = make_content_metrics(ContentKind::Text, 481u, 1030u, 190u, 0.5f, 0.5f, 7u);
    CHECK(metrics.struct_size == sizeof(chronon_content_metrics));

    LayerStateInput in{};
    in.layer = 12u;
    in.content = 481u;
    const auto state = make_layer_state(in);
    CHECK(state.struct_size == sizeof(chronon_layer_state));
    CHECK(state.reserved[0] == 0u);
    CHECK(state.reserved[1] == 0u);
    CHECK(state.reserved[2] == 0u);
}

// ── [C2] version handshake ──────────────────────────────────────────────────
TEST_CASE("ADR-032 [C2]: contract zone version handshake") {
    CHECK(chronon_contract_version() == kContractZoneVersion);
    CHECK(chronon_contract_version() == 1u);
    CHECK(contract_version_matches());
}

// ── [C3] kind mapping ───────────────────────────────────────────────────────
TEST_CASE("ADR-032 [C3]: ContentKind maps 1:1 onto chronon_content_kind") {
    CHECK(to_contract_kind(ContentKind::None)        == CHRONON_CONTENT_NONE);
    CHECK(to_contract_kind(ContentKind::Image)       == CHRONON_CONTENT_IMAGE);
    CHECK(to_contract_kind(ContentKind::Shape)       == CHRONON_CONTENT_SHAPE);
    CHECK(to_contract_kind(ContentKind::Text)        == CHRONON_CONTENT_TEXT);
    CHECK(to_contract_kind(ContentKind::Video)       == CHRONON_CONTENT_VIDEO);
    CHECK(to_contract_kind(ContentKind::Composition) == CHRONON_CONTENT_COMPOSITION);
    CHECK(to_contract_kind(ContentKind::Audio)       == CHRONON_CONTENT_AUDIO);

    // ABI-frozen values (append-only contract; re-ordering is breaking).
    static_assert(CHRONON_CONTENT_NONE == 0);
    static_assert(CHRONON_CONTENT_IMAGE == 1);
    static_assert(CHRONON_CONTENT_SHAPE == 2);
    static_assert(CHRONON_CONTENT_TEXT == 3);
    static_assert(CHRONON_CONTENT_VIDEO == 4);
    static_assert(CHRONON_CONTENT_COMPOSITION == 5);
    static_assert(CHRONON_CONTENT_AUDIO == 6);
}

TEST_CASE("ADR-032 [C3]: NoContent metrics carry kind NONE and id 0") {
    const auto m = make_content_metrics(ContentKind::None, 999u, 0u, 0u, 0.0f, 0.0f, 0u);
    CHECK(m.kind == CHRONON_CONTENT_NONE);
    CHECK(m.content == 0u);
    CHECK(m.fingerprint == 0u);
}

// ── [C4] metrics clamps ─────────────────────────────────────────────────────
TEST_CASE("ADR-032 [C4]: anchor coordinates clamp into [0,1]") {
    const auto m = make_content_metrics(ContentKind::Image, 7u, 640u, 360u,
                                        /*anchor_x=*/1.7f, /*anchor_y=*/-0.2f, 1u);
    CHECK(m.anchor_x == doctest::Approx(1.0f));
    CHECK(m.anchor_y == doctest::Approx(0.0f));
    CHECK(m.width == 640u);
    CHECK(m.height == 360u);
    CHECK(m.content == 7u);
}

// ── [C5] layer state basics ─────────────────────────────────────────────────
TEST_CASE("ADR-032 [C5]: visibility, opacity clamp, id passthrough") {
    LayerStateInput in{};
    in.layer = 12u;
    in.content = 481u;
    in.opacity = 1.7f;
    in.visible = true;
    auto state = make_layer_state(in);
    CHECK(state.visible == 1u);
    CHECK(state.layer == 12u);
    CHECK(state.content == 481u);
    CHECK(state.opacity == doctest::Approx(1.0f));

    in.opacity = -0.5f;
    in.visible = false;
    state = make_layer_state(in);
    CHECK(state.visible == 0u);
    CHECK(state.opacity == doctest::Approx(0.0f));
}

// ── [C6] matrix round-trip ──────────────────────────────────────────────────
TEST_CASE("ADR-032 [C6]: GLM column-vector matrix survives the row-major ABI packing") {
    // TRS product with rotation around Y and a perspective term — every
    // element exercised.
    const Mat4 t = glm::translate(Mat4(1.0f), Vec3{13.0f, -7.5f, 401.0f});
    const Mat4 r = glm::rotate(Mat4(1.0f), glm::radians(35.0f), Vec3{0.0f, 1.0f, 0.0f});
    const Mat4 s = glm::scale(Mat4(1.0f), Vec3{2.0f, 0.5f, 1.5f});
    const Mat4 m = t * r * s;

    LayerStateInput in{};
    in.layer = 1u;
    in.clip_from_local = m;
    const auto state = make_layer_state(in);

    // Unpack row-major back into GLM column-major storage: out[c][r] = arr[r*4+c].
    Mat4 round_trip(1.0f);
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            round_trip[col][row] = state.clip_from_local[static_cast<size_t>(row) * 4 + col];
        }
    }
    // Bit-exact: the packing is a pure relabeling, no arithmetic.
    CHECK(std::memcmp(&round_trip[0][0], &m[0][0], sizeof(float) * 16) == 0);

    // Known-element probes in ABI layout. Row-major storage of a column-vector
    // transform keeps the translation in the last *column* of the logical
    // matrix, which lands at indices 3, 7 and 11 — not 12..14, which is where
    // column-major storage would put it. The round-trip above is the proof: it
    // unpacks `arr[row*4 + col]` into `m[col][row]` and matches bit-exactly, so
    // reading the translation out of 12..14 would contradict the test itself.
    CHECK(state.clip_from_local[3] == doctest::Approx(13.0f));
    CHECK(state.clip_from_local[7] == doctest::Approx(-7.5f));
    CHECK(state.clip_from_local[11] == doctest::Approx(401.0f));
    CHECK(state.clip_from_local[15] == doctest::Approx(1.0f));
    CHECK(state.clip_from_local[12] == doctest::Approx(0.0f));
}

// ── [C7] depth semantics ────────────────────────────────────────────────────
TEST_CASE("ADR-032 [C7]: depth is taken from input when valid, recomputed otherwise") {
    LayerStateInput in{};
    in.clip_from_local = Mat4(1.0f);

    // Identity: no perspective term, NDC z of the origin is 0.
    auto state = make_layer_state(in);
    CHECK(state.depth == doctest::Approx(0.0f));

    // Explicit depth wins.
    in.depth = 0.73f;
    state = make_layer_state(in);
    CHECK(state.depth == doctest::Approx(0.73f));
    // doctest forbids a compound expression here; two checks, same meaning.
    CHECK(state.depth >= 0.0f);
    CHECK(state.depth <= 1.0f);

    // Out-of-range explicit depth clamps.
    in.depth = 2.5f;
    state = make_layer_state(in);
    CHECK(state.depth == doctest::Approx(1.0f));

    // w ≈ 0 matrix is safe (no NaN escapes onto the wire).
    Mat4 degenerate(1.0f);
    degenerate[3][3] = 0.0f;
    in.depth = -1.0f;
    in.clip_from_local = degenerate;
    state = make_layer_state(in);
    CHECK(state.depth == doctest::Approx(0.0f));
    CHECK(std::isfinite(state.depth));

    // A real camera product must reproduce the *analytical* NDC z of the layer
    // origin, not merely something finite that happens to sit in range.
    //
    // perspective(50°, 16:9, near 1, far 100) · translate(0, 0, -10) puts the
    // local origin 10 units in front of the camera on the -Z axis, so with the
    // engine's 0..1 depth range:
    //     z_clip = (f·(10 - n)) / (f - n) = 100 · 9 / 99
    //     w_clip = 10
    //     ndc_z  = 0.909090…
    // This is the case the identity above cannot see: there 0 is also the
    // correct answer, so a converter that reads the wrong component of the
    // matrix still passes it. Here it does not.
    {
        const Mat4 projection =
                glm::perspective(glm::radians(50.0f), 16.0f / 9.0f, 1.0f, 100.0f);
        const Mat4 clip_from_local =
                projection * glm::translate(Mat4(1.0f), glm::vec3(0.0f, 0.0f, -10.0f));

        LayerStateInput cam{};
        cam.clip_from_local = clip_from_local;
        const auto cam_state = make_layer_state(cam);

        CHECK(cam_state.depth == doctest::Approx(0.9090909f).epsilon(1.0e-4));

        // …and the same through the standalone converter.
        CHECK(with_clip_from_local(chronon_layer_state{}, clip_from_local).depth ==
              doctest::Approx(0.9090909f).epsilon(1.0e-4));
    }
}

// ── [C8] ContentRef overload ────────────────────────────────────────────────
TEST_CASE("ADR-032 [C8]: ContentRef overload extracts the opaque asset id") {
    LayerStateInput in{};
    in.layer = 12u;

    SUBCASE("Text asset id 481 flows through") {
        const auto state = make_layer_state(in, ContentRef{TextAssetId{481u}});
        CHECK(state.content == 481u);
    }
    SUBCASE("Image asset id flows through") {
        const auto state = make_layer_state(in, ContentRef{ImageAssetId{77u}});
        CHECK(state.content == 77u);
    }
    SUBCASE("NoContent zeroes the content id") {
        const auto state = make_layer_state(in, ContentRef{NoContent{}});
        CHECK(state.content == 0u);
    }
}

// ── with_clip_from_local preserves the rest of the record ───────────────────
TEST_CASE("ADR-032: with_clip_from_local updates the matrix and derived depth only") {
    LayerStateInput in{};
    in.layer = 9u;
    in.content = 3u;
    in.opacity = 0.4f;
    in.visible = true;
    in.clip_from_local = Mat4(1.0f);
    const auto base = make_layer_state(in);

    const Mat4 moved = glm::translate(Mat4(1.0f), Vec3{1.0f, 2.0f, 3.0f});
    const auto updated = with_clip_from_local(base, moved);

    CHECK(updated.layer == base.layer);
    CHECK(updated.content == base.content);
    CHECK(updated.opacity == doctest::Approx(base.opacity));
    CHECK(updated.visible == base.visible);
    // Translation again, at the row-major positions (see [C6]).
    CHECK(updated.clip_from_local[3] == doctest::Approx(1.0f));
    CHECK(updated.clip_from_local[7] == doctest::Approx(2.0f));
    CHECK(updated.clip_from_local[11] == doctest::Approx(3.0f));
}
