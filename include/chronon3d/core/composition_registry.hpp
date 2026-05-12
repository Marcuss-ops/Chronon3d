#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/core/hash.hpp>
#include <functional>
#include <string>
#include <unordered_map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace chronon3d {

class CompositionRegistry {
public:
    using Factory = std::function<Composition()>;

    void add(std::string name, Factory factory) {
        u64 id = Hash::fnv1a(name);
        factories_[id] = std::move(factory);
        names_[id] = std::move(name);
    }

    [[nodiscard]] Composition create(const std::string& name) const {
        u64 id = Hash::fnv1a(name);
        auto it = factories_.find(id);
        if (it == factories_.end()) {
            throw std::runtime_error("Unknown composition: " + name);
        }

        return it->second();
    }

    [[nodiscard]] bool contains(const std::string& name) const {
        return factories_.contains(Hash::fnv1a(name));
    }

    [[nodiscard]] std::vector<std::string> available() const {
        std::vector<std::string> ids;
        for (const auto& [_, name] : names_) {
            ids.push_back(name);
        }
        return ids;
    }

private:
    std::unordered_map<u64, Factory> factories_;
    std::unordered_map<u64, std::string> names_;
};

} // namespace chronon3d
