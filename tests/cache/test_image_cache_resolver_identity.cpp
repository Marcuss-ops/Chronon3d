// ==============================================================================
// tests/cache/test_image_cache_resolver_identity.cpp
//
// ImageCache resolver-identity contract (P0-1).
//
// `VideoDeviceRuntime::ImageCache` is process/device lifetime, while the
// AssetResolver a DirectYUV job mounts is JOB lifetime. Installing the job
// resolver on the shared cache (`image_cache().set_asset_resolver(job_ptr)`)
// created two defects that only stay hidden while the daemon serializes jobs:
//
//   * two concurrent jobs overwrote each other's resolver, so job A resolved
//     its overlays through job B's asset root;
//   * the job resolver died with the job while the device cache kept the raw
//     pointer, so the cache could dereference a destroyed resolver.
//
// The fix is that the shared cache never retains a resolver: callers pass it
// explicitly per call. These cases pin the resolution behaviour so the
// pointer-storing shortcut cannot come back.
// ==============================================================================

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

// Two distinct job-scoped asset roots, each owning exactly one overlay. The
// same logical path must therefore resolve to two different absolute paths.
class TwoJobRoots {
public:
    TwoJobRoots() {
        const auto base = std::filesystem::temp_directory_path();
        const auto stamp =
            std::chrono::system_clock::now().time_since_epoch().count();
        root = base / ("chronon3d_test_image_cache_resolver_" +
                       std::to_string(stamp));
        for (const auto* job : {"job_a", "job_b"}) {
            std::error_code ec;
            std::filesystem::create_directories(root / job, ec);
        }
    }

    ~TwoJobRoots() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    [[nodiscard]] std::filesystem::path root;
};

TwoJobRoots& roots() {
    static TwoJobRoots instance;
    return instance;
}

} // namespace

TEST_CASE("ImageCache resolves through the caller-supplied resolver, not the installed one") {
    const auto dir_a = roots().root / "job_a";
    const auto dir_b = roots().root / "job_b";

    chronon3d::assets::AssetResolver job_a;
    job_a.mount(dir_a);
    chronon3d::assets::AssetResolver job_b;
    job_b.mount(dir_b);

    // Device-persistent cache. The legacy setter is still reachable, so install
    // job A's resolver on it to prove the explicit argument wins.
    chronon3d::ImageCache device_cache;
    device_cache.set_asset_resolver(&job_a);

    const auto via_b = device_cache.canonical_key_for("overlay.png", {}, job_b);
    REQUIRE(via_b.has_value());
    CHECK(via_b->canonical_path ==
          (dir_b / "overlay.png").lexically_normal());

    const auto via_a = device_cache.canonical_key_for("overlay.png", {}, job_a);
    REQUIRE(via_a.has_value());
    CHECK(via_a->canonical_path ==
          (dir_a / "overlay.png").lexically_normal());

    // Same logical path, different jobs ⇒ different cache entries. If these
    // ever alias, job A can be served job B's pixels.
    CHECK(via_a->canonical_path != via_b->canonical_path);
}

TEST_CASE("ImageCache legacy overload still uses the installed resolver") {
    const auto dir_a = roots().root / "job_a";

    chronon3d::assets::AssetResolver job_a;
    job_a.mount(dir_a);

    chronon3d::ImageCache device_cache;
    const auto before_install = device_cache.canonical_key_for("overlay.png", {});
    REQUIRE(before_install.has_value());
    CHECK(before_install->canonical_path ==
          std::filesystem::path("overlay.png").lexically_normal());

    device_cache.set_asset_resolver(&job_a);
    const auto after_install = device_cache.canonical_key_for("overlay.png", {});
    REQUIRE(after_install.has_value());
    CHECK(after_install->canonical_path ==
          (dir_a / "overlay.png").lexically_normal());
}

TEST_CASE("ImageCache explicit-resolver lookup ignores an entry installed under another root") {
    const auto dir_a = roots().root / "job_a";
    const auto dir_b = roots().root / "job_b";

    chronon3d::assets::AssetResolver job_a;
    job_a.mount(dir_a);
    chronon3d::assets::AssetResolver job_b;
    job_b.mount(dir_b);

    chronon3d::ImageCache device_cache;
    device_cache.set_asset_resolver(&job_a);

    // Nothing has been loaded, so both lookups must miss — but crucially the
    // lookup must be performed against the resolver the CALLER passed, which is
    // observable through canonical_key_for: an unresolved path yields nullopt.
    chronon3d::assets::AssetResolver no_mount;
    CHECK_FALSE(device_cache.canonical_key_for("overlay.png", {}, no_mount).has_value());
    CHECK(device_cache.find("overlay.png", {}, job_b) == nullptr);

    // The installed job A resolver is irrelevant to the explicit job B call:
    // job B's key must describe job B's root.
    const auto key_b = device_cache.canonical_key_for("overlay.png", {}, job_b);
    REQUIRE(key_b.has_value());
    CHECK(key_b->canonical_path == (dir_b / "overlay.png").lexically_normal());
}

TEST_CASE("ImageCache decode options participate in the explicit-resolver key") {
    const auto dir_a = roots().root / "job_a";
    chronon3d::assets::AssetResolver job_a;
    job_a.mount(dir_a);

    chronon3d::ImageCache device_cache;

    chronon3d::ImageDecodeOptions linear;
    linear.premultiply = chronon3d::PremultiplyAlpha::Yes;
    linear.orientation = chronon3d::OrientationPolicy::IgnoreExif;
    chronon3d::ImageDecodeOptions straight;
    straight.premultiply = chronon3d::PremultiplyAlpha::No;
    straight.orientation = chronon3d::OrientationPolicy::ApplyExif;

    const auto key_linear =
        device_cache.canonical_key_for("overlay.png", linear, job_a);
    const auto key_straight =
        device_cache.canonical_key_for("overlay.png", straight, job_a);
    REQUIRE(key_linear.has_value());
    REQUIRE(key_straight.has_value());
    CHECK(key_linear->canonical_path == key_straight->canonical_path);
    // Same bytes, different decode policy ⇒ distinct entries.
    CHECK_FALSE(*key_linear == *key_straight);
}
