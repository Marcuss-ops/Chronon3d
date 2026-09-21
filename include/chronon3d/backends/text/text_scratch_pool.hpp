#pragma once

// Focused per-draw scratch pool surface.  Kept separate from font caches and
// TextRenderResources so material/effect code does not include the full text
// resource aggregator.

#include <blend2d.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace chronon3d {

struct TextScratchState {
    std::vector<float> alpha;
    std::vector<float> h_max;
    std::vector<float> v_max;
    std::vector<float> blur_tmp;
    std::vector<float> inner_shadow_src;
    std::vector<float> inner_shadow_blurred;
    std::vector<uint32_t> blur_buffer;

    struct SurfaceEntry {
        BLImage image;
        int w{0};
        int h{0};
        uint32_t fmt{BL_FORMAT_PRGB32};

        SurfaceEntry(BLImage img, int w_, int h_, uint32_t fmt_)
            : image(std::move(img)), w(w_), h(h_), fmt(fmt_) {}
    };

    static constexpr size_t kMaxPooledSurfaces = 8;
    std::vector<SurfaceEntry> surface_pool;

    [[nodiscard]] BLImage acquire_surface(
        int w,
        int h,
        uint32_t fmt = BL_FORMAT_PRGB32
    );
    void release_surface(BLImage img);
};

class TextScratchManager {
public:
    class Handle {
    public:
        Handle() = default;
        Handle(TextScratchState* state, TextScratchManager* manager)
            : state_(state), manager_(manager) {}
        ~Handle() { if (manager_ && state_) manager_->release(state_); }
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
        Handle(Handle&& other) noexcept
            : state_(other.state_), manager_(other.manager_) {
            other.state_ = nullptr;
            other.manager_ = nullptr;
        }
        Handle& operator=(Handle&& other) noexcept {
            if (this != &other) {
                if (manager_ && state_) manager_->release(state_);
                state_ = other.state_;
                manager_ = other.manager_;
                other.state_ = nullptr;
                other.manager_ = nullptr;
            }
            return *this;
        }
        TextScratchState* operator->() const { return state_; }
        TextScratchState& operator*() const { return *state_; }
        explicit operator bool() const { return state_ != nullptr; }

    private:
        TextScratchState* state_{nullptr};
        TextScratchManager* manager_{nullptr};
    };

    static constexpr size_t kMaxPooledStates = 8;

    Handle acquire();

private:
    void release(TextScratchState* state);
    std::vector<std::unique_ptr<TextScratchState>> available_;
    std::mutex mutex_;
};

} // namespace chronon3d
