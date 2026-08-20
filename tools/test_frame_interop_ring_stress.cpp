#include "apps/chronon3d_cli/commands/video/common/pipe_export_queue.hpp"

#include <cassert>
#include <cstddef>

int main() {
    chronon3d::cli::FrameInteropRing ring;

    for (std::size_t cycle = 0; cycle < 10'000; ++cycle) {
        const auto slot = ring.acquire();
        assert(slot != chronon3d::cli::FrameInteropRing::kInvalidSlot);
        ring.release(slot);
    }

    assert(ring.wait_count() == 0);
    return 0;
}
