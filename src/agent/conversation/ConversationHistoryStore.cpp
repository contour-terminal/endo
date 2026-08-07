// SPDX-License-Identifier: Apache-2.0
#include <crispy/Base64.hpp>

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <agent/conversation/ConversationHistoryStore.hpp>
#include <nlohmann/json.hpp>

namespace endo::agent
{

namespace
{
    // File format versions.
    constexpr auto FormatVersionV1 = 1;
    constexpr auto FormatVersionV2 = 2;

    // --- Time helpers ---

    auto timePointToIso8601(std::chrono::system_clock::time_point tp) -> std::string
    {
        auto const tt = std::chrono::system_clock::to_time_t(tp);
        auto tm = std::tm {};
#if defined(_WIN32)
        gmtime_s(&tm, &tt);
#else
        gmtime_r(&tt, &tm);
#endif
        auto buf = std::array<char, 32> {};
        std::strftime(buf.data(), buf.size(), "%Y-%m-%dT%H:%M:%SZ", &tm);
        return std::string(buf.data());
    }

    auto iso8601ToTimePoint(std::string_view str) -> std::chrono::system_clock::time_point
    {
        auto tm = std::tm {};
        auto ss = std::istringstream(std::string(str));
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        if (ss.fail())
            return {};
#if defined(_WIN32)
        return std::chrono::system_clock::from_time_t(_mkgmtime(&tm));
#else
        return std::chrono::system_clock::from_time_t(timegm(&tm));
#endif
    }

    auto tokenUsageToJson(TokenUsage const& usage) -> nlohmann::json
    {
        return nlohmann::json {
            { "input_tokens", usage.inputTokens },
            { "output_tokens", usage.outputTokens },
            { "cache_read_tokens", usage.cacheReadTokens },
            { "cache_creation_tokens", usage.cacheCreationTokens },
        };
    }

    auto tokenUsageFromJson(nlohmann::json const& j) -> TokenUsage
    {
        return TokenUsage {
            .inputTokens = j.value("input_tokens", int64_t { 0 }),
            .outputTokens = j.value("output_tokens", int64_t { 0 }),
            .cacheReadTokens = j.value("cache_read_tokens", int64_t { 0 }),
            .cacheCreationTokens = j.value("cache_creation_tokens", int64_t { 0 }),
        };
    }

    auto metadataFromJson(nlohmann::json const& doc) -> SessionMetadata
    {
        return SessionMetadata {
            .name = doc.value("name", std::string {}),
            .createdAt = iso8601ToTimePoint(doc.value("created_at", std::string {})),
            .updatedAt = iso8601ToTimePoint(doc.value("updated_at", std::string {})),
            .provider = doc.value("provider", std::string {}),
            .model = doc.value("model", std::string {}),
            .turnCount = doc.value("turn_count", 0),
            .tokenUsage =
                doc.contains("token_usage") ? tokenUsageFromJson(doc["token_usage"]) : TokenUsage {},
        };
    }

    // --- Serialization helpers (standalone, not ADL) ---

    auto contentBlockToJson(ContentBlock const& block) -> nlohmann::json
    {
        return std::visit(
            [](auto const& b) -> nlohmann::json {
                using T = std::decay_t<decltype(b)>;
                if constexpr (std::is_same_v<T, TextBlock>)
                {
                    return nlohmann::json { { "type", "text" }, { "text", b.text } };
                }
                else if constexpr (std::is_same_v<T, ImageBlock>)
                {
                    auto const encoded =
                        crispy::base64::encode(reinterpret_cast<char const*>(b.data.data()),
                                               reinterpret_cast<char const*>(b.data.data() + b.data.size()));
                    return nlohmann::json { { "type", "image" },
                                            { "media_type", b.mediaType },
                                            { "data", encoded } };
                }
                else if constexpr (std::is_same_v<T, ToolUseBlock>)
                {
                    return nlohmann::json {
                        { "type", "tool_use" },
                        { "id", b.id },
                        { "name", b.name },
                        { "arguments", b.arguments },
                    };
                }
                else if constexpr (std::is_same_v<T, ToolResultBlock>)
                {
                    return nlohmann::json {
                        { "type", "tool_result" },
                        { "tool_use_id", b.toolUseId },
                        { "content", b.content },
                        { "is_error", b.isError },
                    };
                }
            },
            block);
    }

    auto contentBlockFromJson(nlohmann::json const& j) -> ContentBlock
    {
        auto const type = j.at("type").get<std::string>();
        if (type == "text")
        {
            return TextBlock { .text = j.at("text").get<std::string>() };
        }
        if (type == "image")
        {
            auto const encoded = j.at("data").get<std::string>();
            auto decoded = crispy::base64::decode(encoded);
            auto bytes = std::vector<uint8_t>(decoded.begin(), decoded.end());
            return ImageBlock {
                .data = std::move(bytes),
                .mediaType = j.at("media_type").get<std::string>(),
            };
        }
        if (type == "tool_use")
        {
            return ToolUseBlock {
                .id = j.at("id").get<std::string>(),
                .name = j.at("name").get<std::string>(),
                .arguments = j.at("arguments"),
            };
        }
        if (type == "tool_result")
        {
            return ToolResultBlock {
                .toolUseId = j.at("tool_use_id").get<std::string>(),
                .content = j.at("content").get<std::string>(),
                .isError = j.value("is_error", false),
            };
        }
        // Unknown block type — preserve as empty text block.
        return TextBlock { .text = {} };
    }

    auto chatMessageToJson(ChatMessage const& msg) -> nlohmann::json
    {
        auto blocks = nlohmann::json::array();
        for (auto const& block: msg.content)
            blocks.push_back(contentBlockToJson(block));
        return nlohmann::json {
            { "role", std::string(roleToString(msg.role)) },
            { "content", std::move(blocks) },
        };
    }

    auto chatMessageFromJson(nlohmann::json const& j) -> ChatMessage
    {
        auto msg = ChatMessage {
            .role = roleFromString(j.at("role").get<std::string>()),
        };
        for (auto const& blockJson: j.at("content"))
            msg.content.push_back(contentBlockFromJson(blockJson));
        return msg;
    }

} // namespace

ConversationHistoryStore::ConversationHistoryStore(std::filesystem::path path): _path(std::move(path))
{
}

auto ConversationHistoryStore::load() const -> std::expected<std::vector<ChatMessage>, HistoryStoreError>
{
    auto ec = std::error_code {};
    if (!std::filesystem::exists(_path, ec))
        return std::vector<ChatMessage> {};

    auto ifs = std::ifstream(_path);
    if (!ifs.is_open())
    {
        return std::unexpected(HistoryStoreError {
            .code = HistoryStoreErrorCode::IoError,
            .message = "Failed to open history file: " + _path.string(),
        });
    }

    auto doc = nlohmann::json {};
    try
    {
        ifs >> doc;
    }
    catch (nlohmann::json::parse_error const& e)
    {
        return std::unexpected(HistoryStoreError {
            .code = HistoryStoreErrorCode::CorruptJson,
            .message = std::string("Corrupt JSON: ") + e.what(),
        });
    }

    if (!doc.contains("version"))
    {
        return std::unexpected(HistoryStoreError {
            .code = HistoryStoreErrorCode::MissingVersion,
            .message = "Missing 'version' field in history file.",
        });
    }

    auto messages = std::vector<ChatMessage> {};
    if (doc.contains("messages") && doc["messages"].is_array())
    {
        for (auto const& msgJson: doc["messages"])
            messages.push_back(chatMessageFromJson(msgJson));
    }
    return messages;
}

auto ConversationHistoryStore::save(std::span<ChatMessage const> messages) const
    -> std::expected<void, HistoryStoreError>
{
    // Create parent directory if needed.
    auto const parentDir = _path.parent_path();
    if (!parentDir.empty())
    {
        auto ec = std::error_code {};
        std::filesystem::create_directories(parentDir, ec);
        if (ec)
        {
            return std::unexpected(HistoryStoreError {
                .code = HistoryStoreErrorCode::IoError,
                .message = "Failed to create directory: " + parentDir.string() + " (" + ec.message() + ")",
            });
        }
    }

    // Filter out system messages.
    auto jsonMessages = nlohmann::json::array();
    for (auto const& msg: messages)
    {
        if (msg.role == Role::System)
            continue;
        jsonMessages.push_back(chatMessageToJson(msg));
    }

    auto doc = nlohmann::json {
        { "version", FormatVersionV1 },
        { "messages", std::move(jsonMessages) },
    };

    // Atomic write: write to .tmp file, then rename.
    auto const tmpPath = std::filesystem::path(_path.string() + ".tmp");
    {
        auto ofs = std::ofstream(tmpPath);
        if (!ofs.is_open())
        {
            return std::unexpected(HistoryStoreError {
                .code = HistoryStoreErrorCode::IoError,
                .message = "Failed to create temporary file: " + tmpPath.string(),
            });
        }
        ofs << doc.dump(2);
        if (!ofs.good())
        {
            return std::unexpected(HistoryStoreError {
                .code = HistoryStoreErrorCode::IoError,
                .message = "Failed to write temporary file: " + tmpPath.string(),
            });
        }
    }

    auto ec = std::error_code {};
    std::filesystem::rename(tmpPath, _path, ec);
    if (ec)
    {
        // Clean up temp file on rename failure.
        std::filesystem::remove(tmpPath, ec);
        return std::unexpected(HistoryStoreError {
            .code = HistoryStoreErrorCode::IoError,
            .message = "Failed to rename temporary file to: " + _path.string(),
        });
    }

    return {};
}

auto ConversationHistoryStore::save(std::span<ChatMessage const> messages,
                                    SessionMetadata const& metadata) const
    -> std::expected<void, HistoryStoreError>
{
    // Create parent directory if needed.
    auto const parentDir = _path.parent_path();
    if (!parentDir.empty())
    {
        auto ec = std::error_code {};
        std::filesystem::create_directories(parentDir, ec);
        if (ec)
        {
            return std::unexpected(HistoryStoreError {
                .code = HistoryStoreErrorCode::IoError,
                .message = "Failed to create directory: " + parentDir.string() + " (" + ec.message() + ")",
            });
        }
    }

    // Filter out system messages.
    auto jsonMessages = nlohmann::json::array();
    for (auto const& msg: messages)
    {
        if (msg.role == Role::System)
            continue;
        jsonMessages.push_back(chatMessageToJson(msg));
    }

    auto doc = nlohmann::json {
        { "version", FormatVersionV2 },
        { "name", metadata.name },
        { "created_at", timePointToIso8601(metadata.createdAt) },
        { "updated_at", timePointToIso8601(metadata.updatedAt) },
        { "provider", metadata.provider },
        { "model", metadata.model },
        { "turn_count", metadata.turnCount },
        { "token_usage", tokenUsageToJson(metadata.tokenUsage) },
        { "messages", std::move(jsonMessages) },
    };

    // Atomic write: write to .tmp file, then rename.
    auto const tmpPath = std::filesystem::path(_path.string() + ".tmp");
    {
        auto ofs = std::ofstream(tmpPath);
        if (!ofs.is_open())
        {
            return std::unexpected(HistoryStoreError {
                .code = HistoryStoreErrorCode::IoError,
                .message = "Failed to create temporary file: " + tmpPath.string(),
            });
        }
        ofs << doc.dump(2);
        if (!ofs.good())
        {
            return std::unexpected(HistoryStoreError {
                .code = HistoryStoreErrorCode::IoError,
                .message = "Failed to write temporary file: " + tmpPath.string(),
            });
        }
    }

    auto ec = std::error_code {};
    std::filesystem::rename(tmpPath, _path, ec);
    if (ec)
    {
        std::filesystem::remove(tmpPath, ec);
        return std::unexpected(HistoryStoreError {
            .code = HistoryStoreErrorCode::IoError,
            .message = "Failed to rename temporary file to: " + _path.string(),
        });
    }

    return {};
}

auto ConversationHistoryStore::loadWithMetadata() const
    -> std::expected<std::pair<SessionMetadata, std::vector<ChatMessage>>, HistoryStoreError>
{
    auto ec = std::error_code {};
    if (!std::filesystem::exists(_path, ec))
        return std::pair { SessionMetadata {}, std::vector<ChatMessage> {} };

    auto ifs = std::ifstream(_path);
    if (!ifs.is_open())
    {
        return std::unexpected(HistoryStoreError {
            .code = HistoryStoreErrorCode::IoError,
            .message = "Failed to open history file: " + _path.string(),
        });
    }

    auto doc = nlohmann::json {};
    try
    {
        ifs >> doc;
    }
    catch (nlohmann::json::parse_error const& e)
    {
        return std::unexpected(HistoryStoreError {
            .code = HistoryStoreErrorCode::CorruptJson,
            .message = std::string("Corrupt JSON: ") + e.what(),
        });
    }

    if (!doc.contains("version"))
    {
        return std::unexpected(HistoryStoreError {
            .code = HistoryStoreErrorCode::MissingVersion,
            .message = "Missing 'version' field in history file.",
        });
    }

    auto messages = std::vector<ChatMessage> {};
    if (doc.contains("messages") && doc["messages"].is_array())
    {
        for (auto const& msgJson: doc["messages"])
            messages.push_back(chatMessageFromJson(msgJson));
    }

    auto const version = doc["version"].get<int>();
    if (version >= FormatVersionV2)
    {
        auto metadata = metadataFromJson(doc);
        return std::pair { std::move(metadata), std::move(messages) };
    }

    // Version 1: construct default metadata from file modification time.
    auto metadata = SessionMetadata {};
    auto const lwt = std::filesystem::last_write_time(_path, ec);
    if (!ec)
    {
        // macOS libc++ lacks std::chrono::clock_cast; file_clock uses the POSIX epoch.
#if defined(__APPLE__)
        auto const sctp = std::chrono::system_clock::time_point(
            std::chrono::duration_cast<std::chrono::system_clock::duration>(lwt.time_since_epoch()));
#else
        auto const sctp = std::chrono::clock_cast<std::chrono::system_clock>(lwt);
#endif
        metadata.createdAt = sctp;
        metadata.updatedAt = sctp;
    }
    return std::pair { std::move(metadata), std::move(messages) };
}

auto ConversationHistoryStore::loadMetadataOnly() const -> std::expected<SessionMetadata, HistoryStoreError>
{
    auto ec = std::error_code {};
    if (!std::filesystem::exists(_path, ec))
        return SessionMetadata {};

    auto ifs = std::ifstream(_path);
    if (!ifs.is_open())
    {
        return std::unexpected(HistoryStoreError {
            .code = HistoryStoreErrorCode::IoError,
            .message = "Failed to open history file: " + _path.string(),
        });
    }

    auto doc = nlohmann::json {};
    try
    {
        ifs >> doc;
    }
    catch (nlohmann::json::parse_error const& e)
    {
        return std::unexpected(HistoryStoreError {
            .code = HistoryStoreErrorCode::CorruptJson,
            .message = std::string("Corrupt JSON: ") + e.what(),
        });
    }

    if (!doc.contains("version"))
    {
        return std::unexpected(HistoryStoreError {
            .code = HistoryStoreErrorCode::MissingVersion,
            .message = "Missing 'version' field in history file.",
        });
    }

    auto const version = doc["version"].get<int>();
    if (version >= FormatVersionV2)
        return metadataFromJson(doc);

    // Version 1: construct default metadata from file modification time.
    auto metadata = SessionMetadata {};
    auto const lwt = std::filesystem::last_write_time(_path, ec);
    if (!ec)
    {
        // macOS libc++ lacks std::chrono::clock_cast; file_clock uses the POSIX epoch.
#if defined(__APPLE__)
        auto const sctp = std::chrono::system_clock::time_point(
            std::chrono::duration_cast<std::chrono::system_clock::duration>(lwt.time_since_epoch()));
#else
        auto const sctp = std::chrono::clock_cast<std::chrono::system_clock>(lwt);
#endif
        metadata.createdAt = sctp;
        metadata.updatedAt = sctp;
    }
    return metadata;
}

auto ConversationHistoryStore::remove() const -> std::expected<void, HistoryStoreError>
{
    auto ec = std::error_code {};
    std::filesystem::remove(_path, ec);
    // Succeed even if file was absent (remove returns false but no error).
    if (ec)
    {
        return std::unexpected(HistoryStoreError {
            .code = HistoryStoreErrorCode::IoError,
            .message = "Failed to remove history file: " + _path.string() + " (" + ec.message() + ")",
        });
    }
    return {};
}

} // namespace endo::agent
