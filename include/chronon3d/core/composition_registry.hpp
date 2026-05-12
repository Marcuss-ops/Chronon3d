#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <functional>
#include <string>
#include <unordered_map>
#include <memory>
#include <stdexcept>

namespace chronon3d {

class CompositionRegistry {
public:
    static CompositionRegistry& instance() {
        static CompositionRegistry registry;
        return registry;
    }

    using Factory = std::function<Composition()>;

    void add(std::string id, Factory factory) {
        factories_[std::move(id)] = std::move(factory);
    }

    [[nodiscard]] Composition create(const std::string& id) const {
        auto it = factories_.find(id);
        if (it == factories_.end()) {
            throw std::runtime_error("Unknown composition: " + id);
        }

        return it->second();
    }

    [[nodiscard]] bool contains(const std::string& id) const {
        return factories_.contains(id);
    }

private:
    std::unordered_map<std::string, Factory> factories_;
};

} // namespace chronon3d
