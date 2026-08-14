#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d::registry {

/// Generic deterministic registry for project-owned semantic content.
///
/// Entry must expose a public `std::string id` member. The registry is
/// instance-owned: it performs no filesystem access, asset resolution,
/// rendering, or global registration.
template <typename Entry>
class ContentRegistry {
public:
    using entry_type = Entry;

    void add(Entry entry) {
        validate_id(entry);
        if (m_entries.contains(entry.id)) {
            throw std::runtime_error("Duplicate content registry id: " + entry.id);
        }
        const std::string id = entry.id;
        m_entries.emplace(id, std::move(entry));
    }

    /// Insert or replace an entry with the same id.
    void upsert(Entry entry) {
        validate_id(entry);
        const std::string id = entry.id;
        m_entries.insert_or_assign(id, std::move(entry));
    }

    [[nodiscard]] bool contains(std::string_view id) const {
        return m_entries.find(id) != m_entries.end();
    }

    [[nodiscard]] Entry* find(std::string_view id) noexcept {
        const auto it = m_entries.find(id);
        return it == m_entries.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const Entry* find(std::string_view id) const noexcept {
        const auto it = m_entries.find(id);
        return it == m_entries.end() ? nullptr : &it->second;
    }

    [[nodiscard]] Entry& get(std::string_view id) {
        auto* entry = find(id);
        if (entry == nullptr) {
            throw std::runtime_error("Unknown content registry id: " + std::string{id});
        }
        return *entry;
    }

    [[nodiscard]] const Entry& get(std::string_view id) const {
        const auto* entry = find(id);
        if (entry == nullptr) {
            throw std::runtime_error("Unknown content registry id: " + std::string{id});
        }
        return *entry;
    }

    [[nodiscard]] std::vector<std::string> available() const {
        std::vector<std::string> ids;
        ids.reserve(m_entries.size());
        for (const auto& [id, _] : m_entries) {
            ids.push_back(id);
        }
        return ids;
    }

    [[nodiscard]] std::vector<Entry> list() const {
        std::vector<Entry> entries;
        entries.reserve(m_entries.size());
        for (const auto& [_, entry] : m_entries) {
            entries.push_back(entry);
        }
        return entries;
    }

    bool erase(std::string_view id) {
        const auto it = m_entries.find(id);
        if (it == m_entries.end()) {
            return false;
        }
        m_entries.erase(it);
        return true;
    }

    void clear() noexcept {
        m_entries.clear();
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return m_entries.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return m_entries.empty();
    }

private:
    static void validate_id(const Entry& entry) {
        if (entry.id.empty()) {
            throw std::runtime_error("Content registry id cannot be empty");
        }
    }

    // std::map keeps snapshots deterministic and enables heterogeneous lookup.
    std::map<std::string, Entry, std::less<>> m_entries;
};

struct PhraseEntry {
    std::string id;
    std::string text;
    std::vector<std::string> tags;
};

struct ImportantWordEntry {
    std::string id;
    std::string word;
    float importance{1.0F};
    std::vector<std::string> tags;
};

struct ImageEntry {
    std::string id;
    /// Logical project asset path/id. Resolution remains AssetResolver-owned.
    std::string asset_path;
    std::string caption;
    std::vector<std::string> tags;
};

struct NamedTextEntry {
    std::string id;
    std::string name;
    std::string text;
    std::vector<std::string> tags;
};

using PhraseRegistry = ContentRegistry<PhraseEntry>;
using ImportantWordRegistry = ContentRegistry<ImportantWordEntry>;
using ImageRegistry = ContentRegistry<ImageEntry>;
using NamedTextRegistry = ContentRegistry<NamedTextEntry>;

/// Project-local set of semantic registries. No global state or singleton.
struct ContentRegistrySet {
    PhraseRegistry phrases;
    ImportantWordRegistry important_words;
    ImageRegistry images;
    NamedTextRegistry named_texts;
};

} // namespace chronon3d::registry
