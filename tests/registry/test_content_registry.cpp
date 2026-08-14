#include <doctest/doctest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include <chronon3d/registry/content_registry.hpp>

TEST_CASE("content registry set stores the four semantic content families") {
    using namespace chronon3d::registry;

    ContentRegistrySet content;
    content.phrases.add(PhraseEntry{
        .id = "phrase.intro",
        .text = "Welcome to Chronon",
        .tags = {"intro", "voiceover"},
    });
    content.important_words.add(ImportantWordEntry{
        .id = "word.chronon",
        .word = "Chronon",
        .importance = 0.95F,
        .tags = {"brand"},
    });
    content.images.add(ImageEntry{
        .id = "image.logo",
        .asset_path = "images/logo.png",
        .caption = "Chronon logo",
        .tags = {"brand"},
    });
    content.named_texts.add(NamedTextEntry{
        .id = "person.alex",
        .name = "Alex",
        .text = "Host",
        .tags = {"person"},
    });

    CHECK(content.phrases.get("phrase.intro").text == "Welcome to Chronon");
    CHECK(content.important_words.get("word.chronon").importance == doctest::Approx(0.95F));
    CHECK(content.images.get("image.logo").asset_path == "images/logo.png");
    CHECK(content.named_texts.get("person.alex").name == "Alex");
}

TEST_CASE("content registry rejects empty and duplicate ids") {
    using namespace chronon3d::registry;

    PhraseRegistry phrases;
    CHECK_THROWS_AS(phrases.add(PhraseEntry{}), std::runtime_error);

    phrases.add(PhraseEntry{.id = "phrase.a", .text = "A"});
    CHECK_THROWS_AS(
        phrases.add(PhraseEntry{.id = "phrase.a", .text = "duplicate"}),
        std::runtime_error);
}

TEST_CASE("content registry upsert erase and snapshots are deterministic") {
    using namespace chronon3d::registry;

    PhraseRegistry phrases;
    phrases.add(PhraseEntry{.id = "phrase.z", .text = "Z"});
    phrases.add(PhraseEntry{.id = "phrase.a", .text = "A"});
    phrases.upsert(PhraseEntry{.id = "phrase.z", .text = "updated"});

    const std::vector<std::string> expected_ids{"phrase.a", "phrase.z"};
    CHECK(phrases.size() == 2);
    CHECK(phrases.get("phrase.z").text == "updated");
    CHECK(phrases.available() == expected_ids);

    REQUIRE(phrases.find("phrase.a") != nullptr);
    CHECK(phrases.erase("phrase.a"));
    CHECK_FALSE(phrases.erase("phrase.missing"));
    CHECK_FALSE(phrases.contains("phrase.a"));
    CHECK(phrases.size() == 1);

    phrases.clear();
    CHECK(phrases.empty());
}
