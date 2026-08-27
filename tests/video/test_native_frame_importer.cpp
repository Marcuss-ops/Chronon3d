#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/media/video/native_frame_importer.hpp>

namespace {
class FakeSession final : public chronon3d::media::NativeFrameImportSession {
public:
    std::shared_ptr<chronon3d::Framebuffer> import(
        const chronon3d::media::NativeDecodedFrameView& view) override {
        ++imports;
        if (view.width == 0 || view.height == 0) return nullptr;
        return std::make_shared<chronon3d::Framebuffer>(
            static_cast<int>(view.width), static_cast<int>(view.height),
            static_cast<chronon3d::Color*>(nullptr));
    }
    int imports{0};
};

class FakeImporter final : public chronon3d::media::NativeFrameImporter {
public:
    std::unique_ptr<chronon3d::media::NativeFrameImportSession>
    create_session() override {
        ++sessions;
        return std::make_unique<FakeSession>();
    }
    int sessions{0};
};
} // namespace

TEST_CASE("Native frame importer owns one session per decoder source") {
    FakeImporter importer;
    auto first = importer.create_session();
    auto second = importer.create_session();
    REQUIRE(first);
    REQUIRE(second);
    CHECK(importer.sessions == 2);

    const chronon3d::media::NativeDecodedFrameView view{
        nullptr, 16, 8, chronon3d::runtime::PixelFormat::Nv12, {}};
    CHECK(first->import(view));
    CHECK(second->import(view));
}
