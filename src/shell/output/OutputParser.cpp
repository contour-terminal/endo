// SPDX-License-Identifier: Apache-2.0
#include "OutputParser.hpp"

#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypedObject.hpp>

#include <algorithm>
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

OutputVariant OutputParser::buildVariantFromDesc(std::string_view schemaDesc,
                                                 uint16_t typeId,
                                                 ParserConfig::Type parserType)
{
    OutputVariant variant;
    variant.assignedTypeId = typeId;
    variant.parser.type = parserType;
    variant.parser.format = ParserConfig::Format::Lines; // default, caller may override

    // Parse "name:type,age:int,active:bool" format
    size_t pos = 0;
    while (pos < schemaDesc.size())
    {
        auto const colonPos = schemaDesc.find(':', pos);
        if (colonPos == std::string_view::npos)
            break;

        auto const name = std::string(schemaDesc.substr(pos, colonPos - pos));
        auto const nextComma = schemaDesc.find(',', colonPos + 1);
        auto const typeStr = schemaDesc.substr(
            colonPos + 1,
            nextComma == std::string_view::npos ? std::string_view::npos : nextComma - colonPos - 1);

        auto vmType = CoreVM::LiteralType::String;
        if (typeStr == "int")
            vmType = CoreVM::LiteralType::Number;
        else if (typeStr == "float")
            vmType = CoreVM::LiteralType::Float;
        else if (typeStr == "bool")
            vmType = CoreVM::LiteralType::Boolean;

        variant.schema.push_back(OutputFieldSchema { .name = name, .sourceKey = {}, .type = vmType });

        if (nextComma == std::string_view::npos)
            break;
        pos = nextComma + 1;
    }

    // For CSV, set comma as default field separator
    if (parserType == ParserConfig::Type::Fields)
        variant.parser.fieldSeparator = ",";

    return variant;
}

bool OutputParser::detectCsvHeader(std::string_view firstLine,
                                   std::string_view separator,
                                   std::vector<OutputFieldSchema> const& schema)
{
    // Split the first line by separator
    auto const fields = splitFields(firstLine, separator, std::nullopt);
    if (fields.size() != schema.size())
        return false;

    // Check if each field matches a schema field name (case-insensitive)
    size_t matches = 0;
    for (size_t i = 0; i < fields.size() && i < schema.size(); ++i)
    {
        auto fieldLower = fields[i];
        auto schemaLower = schema[i].name;
        // Simple case-insensitive compare
        std::transform(fieldLower.begin(), fieldLower.end(), fieldLower.begin(), ::tolower);
        std::transform(schemaLower.begin(), schemaLower.end(), schemaLower.begin(), ::tolower);
        // Trim whitespace from field
        while (!fieldLower.empty() && fieldLower.front() == ' ')
            fieldLower.erase(fieldLower.begin());
        while (!fieldLower.empty() && fieldLower.back() == ' ')
            fieldLower.pop_back();
        if (fieldLower == schemaLower)
            ++matches;
    }

    // Consider it a header if all fields match
    return matches == schema.size();
}

} // namespace endo
