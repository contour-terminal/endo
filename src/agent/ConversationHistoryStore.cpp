// SPDX-License-Identifier: Apache-2.0
#include <crispy/base64.h>

#include <fstream>

#include <agent/ConversationHistoryStore.hpp>
#include <nlohmann/json.hpp>

namespace endo::agent
{

namespace
{
    // Current file format version.
    constexpr auto FormatVersion = 1;

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
        { "version", FormatVersion },
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
