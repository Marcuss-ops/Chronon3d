#pragma once

#include <memory>
#include <mutex>

namespace chronon3d::graph { struct NodeExecutionError; }

namespace chronon3d::runtime {

/// Frame-scoped error channel owned by a slot payload. A job may promote a
/// completed error to its aggregate diagnostic channel, but the mutable
/// storage itself follows the slot lease.
class RenderErrorSlot final {
public:
    void clear();
    void publish_first(const graph::NodeExecutionError& error);
    [[nodiscard]] std::shared_ptr<const graph::NodeExecutionError> load() const;

private:
    mutable std::mutex mutex_;
    std::shared_ptr<const graph::NodeExecutionError> error_;
};

}  // namespace chronon3d::runtime
