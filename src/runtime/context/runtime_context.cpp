#include <chronon3d/runtime/context/runtime_context.hpp>

#include <utility>

namespace chronon3d::runtime {

RuntimeContext::RuntimeContext(chronon3d::Config config)
    : m_config(std::move(config)) {}

RuntimeContext::~RuntimeContext() = default;

}  // namespace chronon3d::runtime
