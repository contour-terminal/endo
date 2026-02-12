// SPDX-License-Identifier: Apache-2.0
#include "OutputParser.hpp"

#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypedObject.hpp>

#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace endo
{

namespace
{
    /// Creates a Nil list node.
    CoreVM::TypedObject* createNilList(CoreVM::Runner& runner)
    {
        auto* nil = runner.allocObject(CoreVM::BuiltinTypeId::List);
        nil->tag = 0;
        return nil;
    }

    /// Creates a Cons cell prepending value to tail.
    CoreVM::TypedObject* createCons(CoreVM::Runner& runner,
                                    CoreVM::TypedObject* value,
                                    CoreVM::TypedObject* tail)
    {
        auto* cons = runner.allocObject(CoreVM::BuiltinTypeId::List);
        cons->tag = 1;
        cons->setSlot(0, reinterpret_cast<uintptr_t>(value));
        cons->setSlot(1, reinterpret_cast<uintptr_t>(tail));
        return cons;
    }

    /// Creates a record object from a JSON object according to the schema.
    CoreVM::TypedObject* createRecordFromJson(CoreVM::Runner& runner,
                                              nlohmann::json const& obj,
                                              OutputVariant const& variant)
    {
        auto* record = runner.allocObject(variant.assignedTypeId);
        for (size_t i = 0; i < variant.schema.size(); ++i)
        {
            auto const& field = variant.schema[i];
            auto const& key = field.sourceKey.empty() ? field.name : field.sourceKey;

            if (field.type == CoreVM::LiteralType::String)
            {
                std::string value;
                if (obj.contains(key))
                {
                    if (obj[key].is_string())
                        value = obj[key].get<std::string>();
                    else
                        value = obj[key].dump();
                }
                record->setSlot(static_cast<uint8_t>(i),
                                reinterpret_cast<uintptr_t>(runner.newString(value)));
            }
            else if (field.type == CoreVM::LiteralType::Number)
            {
                int64_t value = 0;
                if (obj.contains(key))
                {
                    if (obj[key].is_number())
                        value = obj[key].get<int64_t>();
                    else if (obj[key].is_string())
                    {
                        try
                        {
                            value = std::stoll(obj[key].get<std::string>());
                        }
                        catch (...)
                        {
                        }
                    }
                }
                record->setSlot(static_cast<uint8_t>(i), static_cast<uint64_t>(value));
            }
            else if (field.type == CoreVM::LiteralType::Boolean)
            {
                bool value = false;
                if (obj.contains(key))
                    value = obj[key].is_boolean() ? obj[key].get<bool>() : !obj[key].empty();
                record->setSlot(static_cast<uint8_t>(i), static_cast<uint64_t>(value ? 1 : 0));
            }
        }
        return record;
    }

    /// Splits a string by a delimiter, optionally limiting the number of splits.
    std::vector<std::string> splitFields(std::string_view line,
                                         std::string_view separator,
                                         std::optional<int> maxFields)
    {
        std::vector<std::string> fields;
        size_t pos = 0;
        int count = 0;

        while (pos < line.size())
        {
            if (maxFields && count + 1 >= *maxFields)
            {
                // Last field: take the rest of the line
                fields.emplace_back(line.substr(pos));
                return fields;
            }

            auto const found = line.find(separator, pos);
            if (found == std::string_view::npos)
            {
                fields.emplace_back(line.substr(pos));
                return fields;
            }

            fields.emplace_back(line.substr(pos, found - pos));
            pos = found + separator.size();
            ++count;
        }

        return fields;
    }

    /// Creates a record object from a vector of field strings according to the schema.
    CoreVM::TypedObject* createRecordFromFields(CoreVM::Runner& runner,
                                                std::vector<std::string> const& fields,
                                                OutputVariant const& variant)
    {
        auto* record = runner.allocObject(variant.assignedTypeId);
        for (size_t i = 0; i < variant.schema.size(); ++i)
        {
            auto const& fieldSchema = variant.schema[i];
            std::string value = i < fields.size() ? fields[i] : "";

            if (fieldSchema.type == CoreVM::LiteralType::String)
            {
                record->setSlot(static_cast<uint8_t>(i),
                                reinterpret_cast<uintptr_t>(runner.newString(value)));
            }
            else if (fieldSchema.type == CoreVM::LiteralType::Number)
            {
                int64_t numVal = 0;
                try
                {
                    if (!value.empty())
                        numVal = std::stoll(value);
                }
                catch (...)
                {
                }
                record->setSlot(static_cast<uint8_t>(i), static_cast<uint64_t>(numVal));
            }
            else if (fieldSchema.type == CoreVM::LiteralType::Boolean)
            {
                auto const boolVal = value == "true" || value == "1";
                record->setSlot(static_cast<uint8_t>(i), static_cast<uint64_t>(boolVal ? 1 : 0));
            }
        }
        return record;
    }
} // namespace

CoreVM::TypedObject* OutputParser::parseJson(CoreVM::Runner& runner,
                                             std::string_view text,
                                             OutputVariant const& variant)
{
    std::vector<CoreVM::TypedObject*> records;

    if (variant.parser.format == ParserConfig::Format::Array)
    {
        // Parse as JSON array
        try
        {
            auto const arr = nlohmann::json::parse(text);
            if (arr.is_array())
            {
                for (auto const& item: arr)
                {
                    if (item.is_object())
                        records.push_back(createRecordFromJson(runner, item, variant));
                }
            }
        }
        catch (nlohmann::json::parse_error const&)
        {
            // Return empty list on parse failure
        }
    }
    else
    {
        // Parse as NDJSON (one JSON object per line)
        auto textStr = std::string(text);
        std::istringstream stream(textStr);
        std::string line;
        while (std::getline(stream, line))
        {
            if (line.empty())
                continue;
            try
            {
                auto const obj = nlohmann::json::parse(line);
                if (obj.is_object())
                    records.push_back(createRecordFromJson(runner, obj, variant));
            }
            catch (nlohmann::json::parse_error const&)
            {
                // Skip malformed lines
            }
        }
    }

    // Build cons-cell list right-to-left
    auto* list = createNilList(runner);
    for (auto it = records.rbegin(); it != records.rend(); ++it)
        list = createCons(runner, *it, list);

    return list;
}

CoreVM::TypedObject* OutputParser::parseFields(CoreVM::Runner& runner,
                                               std::string_view text,
                                               OutputVariant const& variant)
{
    std::vector<CoreVM::TypedObject*> records;

    auto textStr = std::string(text);
    std::istringstream stream(textStr);
    std::string line;
    while (std::getline(stream, line))
    {
        if (line.empty())
            continue;

        // Trim leading whitespace for field-based formats
        auto const start = line.find_first_not_of(" \t");
        if (start != std::string::npos && start > 0)
            line = line.substr(start);

        auto const fields = splitFields(line, variant.parser.fieldSeparator, variant.parser.maxFields);
        records.push_back(createRecordFromFields(runner, fields, variant));
    }

    // Build cons-cell list right-to-left
    auto* list = createNilList(runner);
    for (auto it = records.rbegin(); it != records.rend(); ++it)
        list = createCons(runner, *it, list);

    return list;
}

} // namespace endo
