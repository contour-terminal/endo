// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <tui/completer/Completer.hpp>
#include <tui/completer/CompletionItem.hpp>
#include <tui/completer/CompletionProvider.hpp>
#include <tui/completer/SmartCaseMatch.hpp>

using namespace tui;

namespace
{

/// @brief A simple test provider that returns fixed completions.
class TestProvider: public CompletionProvider
{
  public:
    explicit TestProvider(std::vector<CompletionItem> items, int priority = 0):
        _items(std::move(items)), _priority(priority)
    {
    }

    std::vector<CompletionItem> complete(std::string_view input, size_t cursorPosition) override
    {
        std::vector<CompletionItem> results;
        std::string_view prefix = input.substr(0, cursorPosition);

        for (auto const& item: _items)
        {
            if (SmartCaseMatch::matchesPrefix(item.text, prefix))
            {
                auto resultItem = item;
                resultItem.score = SmartCaseMatch::adjustScore(item.score, item.text, prefix);
                results.push_back(resultItem);
            }
        }
        return results;
    }

    int priority() const override { return _priority; }

  private:
    std::vector<CompletionItem> _items;
    int _priority;
};

/// @brief Helper to create items quickly.
CompletionItem item(std::string text, int score = 0, std::string desc = "")
{
    return CompletionItem {
        .text = std::move(text), .displayText = text, .description = std::move(desc), .score = score
    };
}

} // namespace

// ============================================================================
// CompletionItem tests
// ============================================================================

TEST_CASE("CompletionItem.suffix")
{
    CompletionItem ci { .text = "hello", .displayText = "hello", .description = "", .score = 0 };

    CHECK(ci.suffix(0) == "hello");
    CHECK(ci.suffix(2) == "llo");
    CHECK(ci.suffix(5) == "");
    CHECK(ci.suffix(10) == ""); // Beyond text length
}

TEST_CASE("CompletionItem.comparison")
{
    CompletionItem a { .text = "abc", .displayText = "abc", .description = "", .score = 10 };
    CompletionItem b { .text = "abc", .displayText = "abc", .description = "", .score = 10 };
    CompletionItem c { .text = "def", .displayText = "def", .description = "", .score = 10 };

    CHECK(a == b);
    CHECK(a != c);
    CHECK(a < c); // Lexicographic comparison
}

// ============================================================================
// Completer tests
// ============================================================================

TEST_CASE("Completer.empty_completer")
{
    Completer completer;

    CHECK(completer.providerCount() == 0);

    auto results = completer.complete("test", 4);
    CHECK(results.empty());

    auto suggestion = completer.suggest("test", 4);
    CHECK_FALSE(suggestion.has_value());
}

TEST_CASE("Completer.single_provider")
{
    Completer completer;

    std::vector<CompletionItem> items = { item("apple", 10), item("apricot", 5), item("banana", 8) };

    completer.addProvider(std::make_unique<TestProvider>(items));
    CHECK(completer.providerCount() == 1);

    auto results = completer.complete("ap", 2);
    CHECK(results.size() == 2);
    CHECK(results[0].text == "apple");   // Higher score
    CHECK(results[1].text == "apricot"); // Lower score
}

TEST_CASE("Completer.multiple_providers")
{
    Completer completer;

    std::vector<CompletionItem> items1 = { item("cat", 10), item("car", 5) };
    std::vector<CompletionItem> items2 = { item("cap", 15), item("can", 8) };

    completer.addProvider(std::make_unique<TestProvider>(items1, 1));
    completer.addProvider(std::make_unique<TestProvider>(items2, 2));
    CHECK(completer.providerCount() == 2);

    auto results = completer.complete("ca", 2);
    CHECK(results.size() == 4);
    // Should be sorted by score (descending)
    CHECK(results[0].text == "cap"); // 15
    CHECK(results[1].text == "cat"); // 10
    CHECK(results[2].text == "can"); // 8
    CHECK(results[3].text == "car"); // 5
}

TEST_CASE("Completer.deduplication")
{
    Completer completer;

    // Both providers return "cat"
    std::vector<CompletionItem> items1 = { item("cat", 10) };
    std::vector<CompletionItem> items2 = { item("cat", 20), item("car", 5) };

    completer.addProvider(std::make_unique<TestProvider>(items1, 1));
    completer.addProvider(std::make_unique<TestProvider>(items2, 2));

    auto results = completer.complete("ca", 2);
    CHECK(results.size() == 2);

    // First occurrence wins (from higher priority provider)
    int catCount = 0;
    for (auto const& r: results)
    {
        if (r.text == "cat")
            ++catCount;
    }
    CHECK(catCount == 1);
}

TEST_CASE("Completer.max_suggestions")
{
    Completer completer;

    std::vector<CompletionItem> items;
    for (int i = 0; i < 100; ++i)
        items.push_back(item("test" + std::to_string(i), 100 - i));

    completer.addProvider(std::make_unique<TestProvider>(items));

    CompletionConfig config { .maxSuggestions = 10 };
    completer.setConfig(config);

    auto results = completer.complete("test", 4);
    CHECK(results.size() == 10);
}

TEST_CASE("Completer.suggest_ghost_text")
{
    Completer completer;

    std::vector<CompletionItem> items = { item("hello_world", 10), item("hello_there", 5) };

    completer.addProvider(std::make_unique<TestProvider>(items));

    auto suggestion = completer.suggest("hello", 5);
    REQUIRE(suggestion.has_value());
    CHECK(*suggestion == "_world"); // Suffix of best match
}

TEST_CASE("Completer.suggest_no_match")
{
    Completer completer;

    std::vector<CompletionItem> items = { item("hello", 10) };

    completer.addProvider(std::make_unique<TestProvider>(items));

    auto suggestion = completer.suggest("xyz", 3);
    CHECK_FALSE(suggestion.has_value());
}

TEST_CASE("Completer.findCommonPrefix_empty")
{
    auto prefix = Completer::findCommonPrefix({});
    CHECK(prefix == "");
}

TEST_CASE("Completer.findCommonPrefix_single")
{
    auto prefix = Completer::findCommonPrefix({ item("hello") });
    CHECK(prefix == "hello");
}

TEST_CASE("Completer.findCommonPrefix_common")
{
    auto prefix = Completer::findCommonPrefix({ item("hello"), item("help"), item("helicopter") });
    CHECK(prefix == "hel");
}

TEST_CASE("Completer.findCommonPrefix_no_common")
{
    auto prefix = Completer::findCommonPrefix({ item("apple"), item("banana") });
    CHECK(prefix == "");
}

TEST_CASE("Completer.config_persistence")
{
    Completer completer;

    CompletionConfig config { .maxSuggestions = 25 };
    completer.setConfig(config);

    CHECK(completer.config().maxSuggestions == 25);
}
