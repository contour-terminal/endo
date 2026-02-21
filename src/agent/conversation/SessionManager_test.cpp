// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

#include <agent/conversation/SessionManager.hpp>

using namespace endo::agent;

namespace
{

/// Creates a unique temporary directory for testing.
auto tempSessionDir() -> std::filesystem::path
{
    static int counter = 0;
    auto const dir = std::filesystem::path("/tmp/endo-test-sessions-" + std::to_string(counter++));
    std::filesystem::create_directories(dir);
    return dir;
}

/// RAII guard that removes the directory on destruction.
struct DirGuard
{
    std::filesystem::path path;

    ~DirGuard()
    {
        auto ec = std::error_code {};
        std::filesystem::remove_all(path, ec);
    }
};

auto makeSampleMessages() -> std::vector<ChatMessage>
{
    return {
        ChatMessage::text(Role::User, "Hello agent"),
        ChatMessage::text(Role::Assistant, "Hello user"),
    };
}

auto makeSampleMetadata(std::string_view name) -> SessionMetadata
{
    auto const now = std::chrono::system_clock::now();
    return SessionMetadata {
        .name = std::string(name),
        .createdAt = now,
        .updatedAt = now,
        .provider = "claude",
        .model = "claude-sonnet-4-6",
        .turnCount = 3,
        .tokenUsage = TokenUsage { .inputTokens = 1500, .outputTokens = 500 },
    };
}

} // namespace

TEST_CASE("SessionManager.listSessions_emptyDirectory")
{
    auto const dir = tempSessionDir();
    auto const guard = DirGuard { dir };
    auto const manager = SessionManager(dir);

    auto result = manager.listSessions();
    REQUIRE(result.has_value());
    CHECK(result->empty());
}

TEST_CASE("SessionManager.saveAndLoad_roundTrip")
{
    auto const dir = tempSessionDir();
    auto const guard = DirGuard { dir };
    auto const manager = SessionManager(dir);

    auto const messages = makeSampleMessages();
    auto const metadata = makeSampleMetadata("test-session");

    auto saveResult = manager.saveSession("test-session", messages, metadata);
    REQUIRE(saveResult.has_value());

    auto loadResult = manager.loadSession("test-session");
    REQUIRE(loadResult.has_value());
    auto const& [loadedMeta, loadedMessages] = *loadResult;
    CHECK(loadedMeta.name == "test-session");
    CHECK(loadedMeta.provider == "claude");
    CHECK(loadedMeta.model == "claude-sonnet-4-6");
    CHECK(loadedMeta.turnCount == 3);
    CHECK(loadedMeta.tokenUsage.inputTokens == 1500);
    CHECK(loadedMeta.tokenUsage.outputTokens == 500);
    REQUIRE(loadedMessages.size() == 2);
    CHECK(loadedMessages.at(0).textContent() == "Hello agent");
    CHECK(loadedMessages.at(1).textContent() == "Hello user");
}

TEST_CASE("SessionManager.listSessions_sortedByUpdatedAt")
{
    auto const dir = tempSessionDir();
    auto const guard = DirGuard { dir };
    auto const manager = SessionManager(dir);

    auto const messages = makeSampleMessages();

    auto const now = std::chrono::system_clock::now();
    auto meta1 = makeSampleMetadata("older");
    meta1.updatedAt = now - std::chrono::hours(24);
    auto meta2 = makeSampleMetadata("newer");
    meta2.updatedAt = now;

    REQUIRE(manager.saveSession("older", messages, meta1).has_value());
    REQUIRE(manager.saveSession("newer", messages, meta2).has_value());

    auto result = manager.listSessions();
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);
    CHECK(result->at(0).name == "newer");
    CHECK(result->at(1).name == "older");
}

TEST_CASE("SessionManager.removeSession_deletesFile")
{
    auto const dir = tempSessionDir();
    auto const guard = DirGuard { dir };
    auto const manager = SessionManager(dir);

    auto const messages = makeSampleMessages();
    auto const metadata = makeSampleMetadata("to-delete");
    REQUIRE(manager.saveSession("to-delete", messages, metadata).has_value());
    CHECK(manager.sessionExists("to-delete"));

    auto removeResult = manager.removeSession("to-delete");
    REQUIRE(removeResult.has_value());
    CHECK(!manager.sessionExists("to-delete"));
}

TEST_CASE("SessionManager.lastActiveSession_readWriteClear")
{
    auto const dir = tempSessionDir();
    auto const guard = DirGuard { dir };
    auto const manager = SessionManager(dir);

    CHECK(manager.lastActiveSession().empty());

    manager.setLastActiveSession("my-session");
    CHECK(manager.lastActiveSession() == "my-session");

    manager.clearLastActiveSession();
    CHECK(manager.lastActiveSession().empty());
}

TEST_CASE("SessionManager.sessionExists_trueAndFalse")
{
    auto const dir = tempSessionDir();
    auto const guard = DirGuard { dir };
    auto const manager = SessionManager(dir);

    CHECK(!manager.sessionExists("nonexistent"));

    auto const messages = makeSampleMessages();
    auto const metadata = makeSampleMetadata("existing");
    REQUIRE(manager.saveSession("existing", messages, metadata).has_value());
    CHECK(manager.sessionExists("existing"));
}

TEST_CASE("SessionManager.generateSessionName_fromText")
{
    auto const dir = tempSessionDir();
    auto const guard = DirGuard { dir };
    auto const manager = SessionManager(dir);

    auto const name = manager.generateSessionName("Hello, how are you doing today?");
    CHECK(name == "hello-how-are-you-doing-today");
}

TEST_CASE("SessionManager.generateSessionName_deduplication")
{
    auto const dir = tempSessionDir();
    auto const guard = DirGuard { dir };
    auto const manager = SessionManager(dir);

    auto const messages = makeSampleMessages();
    auto const metadata = makeSampleMetadata("hello");

    // Create a session with the base name.
    REQUIRE(manager.saveSession("hello", messages, metadata).has_value());

    // Generate should now append -2.
    auto const name = manager.generateSessionName("hello");
    CHECK(name == "hello-2");
}

TEST_CASE("SessionManager.generateSessionName_specialCharacters")
{
    auto const dir = tempSessionDir();
    auto const guard = DirGuard { dir };
    auto const manager = SessionManager(dir);

    // Empty input should produce fallback.
    CHECK(manager.generateSessionName("") == "session");

    // All special characters should produce fallback.
    CHECK(manager.generateSessionName("!!!@@@###") == "session");

    // Mixed content should produce cleaned slug.
    auto const name = manager.generateSessionName("Fix: bug #42 in auth!");
    CHECK(name == "fix-bug-42-in-auth");
}

TEST_CASE("SessionManager.saveSession_createsDirectory")
{
    auto const dir = tempSessionDir();
    auto const guard = DirGuard { dir };

    // Remove the sessions subdirectory if it exists.
    auto ec = std::error_code {};
    std::filesystem::remove_all(dir / "sessions", ec);

    auto const manager = SessionManager(dir);
    auto const messages = makeSampleMessages();
    auto const metadata = makeSampleMetadata("auto-dir");

    auto result = manager.saveSession("auto-dir", messages, metadata);
    REQUIRE(result.has_value());
    CHECK(std::filesystem::exists(dir / "sessions" / "auto-dir.json"));
}

TEST_CASE("SessionManager.loadSession_nonexistent_returnsError")
{
    auto const dir = tempSessionDir();
    auto const guard = DirGuard { dir };
    auto const manager = SessionManager(dir);

    auto result = manager.loadSession("nonexistent");
    REQUIRE(!result.has_value());
    CHECK(result.error().code == HistoryStoreErrorCode::IoError);
}

TEST_CASE("SessionManager.sessionNames_returnsSortedNames")
{
    auto const dir = tempSessionDir();
    auto const guard = DirGuard { dir };
    auto const manager = SessionManager(dir);

    auto const messages = makeSampleMessages();
    REQUIRE(manager.saveSession("charlie", messages, makeSampleMetadata("charlie")).has_value());
    REQUIRE(manager.saveSession("alpha", messages, makeSampleMetadata("alpha")).has_value());
    REQUIRE(manager.saveSession("bravo", messages, makeSampleMetadata("bravo")).has_value());

    auto const names = manager.sessionNames();
    REQUIRE(names.size() == 3);
    CHECK(names[0] == "alpha");
    CHECK(names[1] == "bravo");
    CHECK(names[2] == "charlie");
}
