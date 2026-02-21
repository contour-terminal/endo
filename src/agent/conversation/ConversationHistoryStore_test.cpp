// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include <agent/conversation/ConversationHistoryStore.hpp>
#include <nlohmann/json.hpp>

using namespace endo::agent;

namespace
{
/// Creates a unique temporary file path in /tmp for testing.
auto tempHistoryPath() -> std::filesystem::path
{
    static int counter = 0;
    return std::filesystem::path("/tmp/endo-test-history-" + std::to_string(counter++) + ".json");
}

/// RAII guard that removes the file on destruction.
struct FileGuard
{
    std::filesystem::path path;

    ~FileGuard()
    {
        auto ec = std::error_code {};
        std::filesystem::remove(path, ec);
        std::filesystem::remove(std::filesystem::path(path.string() + ".tmp"), ec);
    }
};
} // namespace

TEST_CASE("ConversationHistoryStore.fresh_start_returns_empty")
{
    auto const path = tempHistoryPath();
    auto const guard = FileGuard { path };
    auto const store = ConversationHistoryStore(path);

    auto result = store.load();
    REQUIRE(result.has_value());
    CHECK(result->empty());
}

TEST_CASE("ConversationHistoryStore.round_trip_text_block")
{
    auto const path = tempHistoryPath();
    auto const guard = FileGuard { path };
    auto const store = ConversationHistoryStore(path);

    auto messages = std::vector<ChatMessage> {
        ChatMessage::text(Role::User, "Hello agent"),
        ChatMessage::text(Role::Assistant, "Hello user"),
    };

    auto saveResult = store.save(messages);
    REQUIRE(saveResult.has_value());

    auto loadResult = store.load();
    REQUIRE(loadResult.has_value());
    REQUIRE(loadResult->size() == 2);
    CHECK(loadResult->at(0).role == Role::User);
    CHECK(loadResult->at(0).textContent() == "Hello agent");
    CHECK(loadResult->at(1).role == Role::Assistant);
    CHECK(loadResult->at(1).textContent() == "Hello user");
}

TEST_CASE("ConversationHistoryStore.system_prompt_excluded")
{
    auto const path = tempHistoryPath();
    auto const guard = FileGuard { path };
    auto const store = ConversationHistoryStore(path);

    auto messages = std::vector<ChatMessage> {
        ChatMessage::text(Role::System, "You are a helpful assistant."),
        ChatMessage::text(Role::User, "Hi"),
        ChatMessage::text(Role::Assistant, "Hello!"),
    };

    REQUIRE(store.save(messages).has_value());

    auto loaded = store.load();
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->size() == 2);
    CHECK(loaded->at(0).role == Role::User);
    CHECK(loaded->at(1).role == Role::Assistant);
}

TEST_CASE("ConversationHistoryStore.corrupt_json_returns_error")
{
    auto const path = tempHistoryPath();
    auto const guard = FileGuard { path };

    // Write invalid JSON to the file.
    {
        auto ofs = std::ofstream(path);
        ofs << "{ this is not valid json }}}";
    }

    auto const store = ConversationHistoryStore(path);
    auto result = store.load();
    REQUIRE(!result.has_value());
    CHECK(result.error().code == HistoryStoreErrorCode::CorruptJson);
}

TEST_CASE("ConversationHistoryStore.missing_version_returns_error")
{
    auto const path = tempHistoryPath();
    auto const guard = FileGuard { path };

    // Write valid JSON but without version field.
    {
        auto ofs = std::ofstream(path);
        ofs << R"({"messages":[]})";
    }

    auto const store = ConversationHistoryStore(path);
    auto result = store.load();
    REQUIRE(!result.has_value());
    CHECK(result.error().code == HistoryStoreErrorCode::MissingVersion);
}

TEST_CASE("ConversationHistoryStore.atomic_write_no_tmp_left")
{
    auto const path = tempHistoryPath();
    auto const guard = FileGuard { path };
    auto const store = ConversationHistoryStore(path);

    auto messages = std::vector<ChatMessage> { ChatMessage::text(Role::User, "test") };
    REQUIRE(store.save(messages).has_value());

    // The .tmp file should not exist after a successful save.
    auto const tmpPath = std::filesystem::path(path.string() + ".tmp");
    CHECK(!std::filesystem::exists(tmpPath));
    CHECK(std::filesystem::exists(path));
}

TEST_CASE("ConversationHistoryStore.remove_and_reload")
{
    auto const path = tempHistoryPath();
    auto const guard = FileGuard { path };
    auto const store = ConversationHistoryStore(path);

    auto messages = std::vector<ChatMessage> { ChatMessage::text(Role::User, "test") };
    REQUIRE(store.save(messages).has_value());
    CHECK(std::filesystem::exists(path));

    REQUIRE(store.remove().has_value());
    CHECK(!std::filesystem::exists(path));

    // Load after remove should return empty vector.
    auto result = store.load();
    REQUIRE(result.has_value());
    CHECK(result->empty());
}

TEST_CASE("ConversationHistoryStore.remove_absent_file_succeeds")
{
    auto const path = tempHistoryPath();
    auto const store = ConversationHistoryStore(path);

    // File doesn't exist, remove should still succeed.
    auto result = store.remove();
    REQUIRE(result.has_value());
}

TEST_CASE("ConversationHistoryStore.round_trip_image_block_base64")
{
    auto const path = tempHistoryPath();
    auto const guard = FileGuard { path };
    auto const store = ConversationHistoryStore(path);

    auto imageData = std::vector<uint8_t> { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    auto msg = ChatMessage { .role = Role::User };
    msg.content.emplace_back(ImageBlock { .data = imageData, .mediaType = "image/png" });

    auto messages = std::vector<ChatMessage> { std::move(msg) };
    REQUIRE(store.save(messages).has_value());

    auto loaded = store.load();
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->size() == 1);
    auto const* imgBlock = std::get_if<ImageBlock>(&loaded->at(0).content.at(0));
    REQUIRE(imgBlock != nullptr);
    CHECK(imgBlock->mediaType == "image/png");
    CHECK(imgBlock->data == imageData);
}

TEST_CASE("ConversationHistoryStore.round_trip_empty_list")
{
    auto const path = tempHistoryPath();
    auto const guard = FileGuard { path };
    auto const store = ConversationHistoryStore(path);

    auto messages = std::vector<ChatMessage> {};
    REQUIRE(store.save(messages).has_value());

    auto loaded = store.load();
    REQUIRE(loaded.has_value());
    CHECK(loaded->empty());
}

TEST_CASE("ConversationHistoryStore.round_trip_tool_result_block")
{
    auto const path = tempHistoryPath();
    auto const guard = FileGuard { path };
    auto const store = ConversationHistoryStore(path);

    auto msg = ChatMessage { .role = Role::User };
    msg.content.emplace_back(ToolResultBlock {
        .toolUseId = "call_123",
        .content = "file contents here",
        .isError = true,
    });

    auto messages = std::vector<ChatMessage> { std::move(msg) };
    REQUIRE(store.save(messages).has_value());

    auto loaded = store.load();
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->size() == 1);
    auto const* trBlock = std::get_if<ToolResultBlock>(&loaded->at(0).content.at(0));
    REQUIRE(trBlock != nullptr);
    CHECK(trBlock->toolUseId == "call_123");
    CHECK(trBlock->content == "file contents here");
    CHECK(trBlock->isError == true);
}

TEST_CASE("ConversationHistoryStore.round_trip_tool_use_block")
{
    auto const path = tempHistoryPath();
    auto const guard = FileGuard { path };
    auto const store = ConversationHistoryStore(path);

    auto msg = ChatMessage { .role = Role::Assistant };
    msg.content.emplace_back(ToolUseBlock {
        .id = "call_456",
        .name = "read_file",
        .arguments = nlohmann::json { { "path", "/tmp/test.txt" } },
    });

    auto messages = std::vector<ChatMessage> { std::move(msg) };
    REQUIRE(store.save(messages).has_value());

    auto loaded = store.load();
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->size() == 1);
    auto const* tuBlock = std::get_if<ToolUseBlock>(&loaded->at(0).content.at(0));
    REQUIRE(tuBlock != nullptr);
    CHECK(tuBlock->id == "call_456");
    CHECK(tuBlock->name == "read_file");
    CHECK(tuBlock->arguments["path"] == "/tmp/test.txt");
}

// --- Version 2 (metadata) tests ---

TEST_CASE("ConversationHistoryStore.saveWithMetadata_and_loadWithMetadata_roundTrip")
{
    auto const path = tempHistoryPath();
    auto const guard = FileGuard { path };
    auto const store = ConversationHistoryStore(path);

    auto messages = std::vector<ChatMessage> {
        ChatMessage::text(Role::User, "Hello"),
        ChatMessage::text(Role::Assistant, "Hi there!"),
    };

    auto const now = std::chrono::system_clock::now();
    auto const metadata = SessionMetadata {
        .name = "test-session",
        .createdAt = now - std::chrono::hours(1),
        .updatedAt = now,
        .provider = "claude",
        .model = "claude-sonnet-4-6",
        .turnCount = 5,
        .tokenUsage = TokenUsage { .inputTokens = 1000, .outputTokens = 500, .cacheReadTokens = 200 },
    };

    auto saveResult = store.save(messages, metadata);
    REQUIRE(saveResult.has_value());

    auto loadResult = store.loadWithMetadata();
    REQUIRE(loadResult.has_value());
    auto const& [loadedMeta, loadedMessages] = *loadResult;
    CHECK(loadedMeta.name == "test-session");
    CHECK(loadedMeta.provider == "claude");
    CHECK(loadedMeta.model == "claude-sonnet-4-6");
    CHECK(loadedMeta.turnCount == 5);
    CHECK(loadedMeta.tokenUsage.inputTokens == 1000);
    CHECK(loadedMeta.tokenUsage.outputTokens == 500);
    CHECK(loadedMeta.tokenUsage.cacheReadTokens == 200);
    REQUIRE(loadedMessages.size() == 2);
    CHECK(loadedMessages.at(0).textContent() == "Hello");
    CHECK(loadedMessages.at(1).textContent() == "Hi there!");
}

TEST_CASE("ConversationHistoryStore.loadMetadataOnly_skipsMessages")
{
    auto const path = tempHistoryPath();
    auto const guard = FileGuard { path };
    auto const store = ConversationHistoryStore(path);

    auto messages = std::vector<ChatMessage> {
        ChatMessage::text(Role::User, "First message"),
        ChatMessage::text(Role::Assistant, "Response"),
    };

    auto const now = std::chrono::system_clock::now();
    auto const metadata = SessionMetadata {
        .name = "metadata-only",
        .createdAt = now,
        .updatedAt = now,
        .provider = "openai",
        .model = "gpt-4o",
        .turnCount = 2,
    };

    REQUIRE(store.save(messages, metadata).has_value());

    auto metaResult = store.loadMetadataOnly();
    REQUIRE(metaResult.has_value());
    CHECK(metaResult->name == "metadata-only");
    CHECK(metaResult->provider == "openai");
    CHECK(metaResult->model == "gpt-4o");
    CHECK(metaResult->turnCount == 2);
}

TEST_CASE("ConversationHistoryStore.version1File_loadsWithDefaultMetadata")
{
    auto const path = tempHistoryPath();
    auto const guard = FileGuard { path };

    // Write a version 1 file manually.
    {
        auto doc = nlohmann::json {
            { "version", 1 },
            { "messages",
              nlohmann::json::array(
                  { nlohmann::json { { "role", "user" },
                                     { "content",
                                       nlohmann::json::array({ nlohmann::json {
                                           { "type", "text" }, { "text", "v1 message" } } }) } } }) },
        };
        auto ofs = std::ofstream(path);
        ofs << doc.dump(2);
    }

    auto const store = ConversationHistoryStore(path);

    // loadWithMetadata should handle v1 gracefully.
    auto result = store.loadWithMetadata();
    REQUIRE(result.has_value());
    auto const& [meta, messages] = *result;
    CHECK(meta.name.empty());     // No name in v1.
    CHECK(meta.provider.empty()); // No provider in v1.
    CHECK(meta.turnCount == 0);   // Default.
    REQUIRE(messages.size() == 1);
    CHECK(messages.at(0).textContent() == "v1 message");

    // loadMetadataOnly should also work.
    auto metaResult = store.loadMetadataOnly();
    REQUIRE(metaResult.has_value());
    CHECK(metaResult->name.empty());
}
