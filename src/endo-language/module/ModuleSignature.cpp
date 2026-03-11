// SPDX-License-Identifier: Apache-2.0
#include <endo-language/module/ModuleSignature.hpp>

#include <endo-language/module/ModuleDescriptor.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <ranges>
#include <sstream>
#include <string>

namespace endo
{

bool ModuleSignature::declares(std::string const& name) const
{
    return std::ranges::any_of(entries, [&](auto const& e) { return e.name == name; });
}

std::optional<ModuleSignature> parseModuleSignature(std::filesystem::path const& path)
{
    auto ifs = std::ifstream(path);
    if (!ifs)
        return std::nullopt;

    auto sig = ModuleSignature {};
    sig.moduleName = path.stem().string();

    auto line = std::string {};
    while (std::getline(ifs, line))
    {
        // Skip empty lines and comments
        auto const trimStart = line.find_first_not_of(" \t");
        if (trimStart == std::string::npos)
            continue;
        auto const trimmed = std::string_view(line).substr(trimStart);
        if (trimmed.starts_with("//") || trimmed.starts_with('#'))
            continue;

        // Parse "val name : signature"
        if (trimmed.starts_with("val "))
        {
            auto const rest = trimmed.substr(4);
            auto const colonPos = rest.find(':');
            if (colonPos == std::string::npos)
                continue; // Malformed, skip

            auto nameStr = std::string(rest.substr(0, colonPos));
            // Trim whitespace from name
            if (auto const end = nameStr.find_last_not_of(" \t"); end != std::string::npos)
                nameStr.erase(end + 1);
            if (auto const start = nameStr.find_first_not_of(" \t"); start != std::string::npos)
                nameStr.erase(0, start);

            auto sigStr = std::string(rest.substr(colonPos + 1));
            if (auto const start = sigStr.find_first_not_of(" \t"); start != std::string::npos)
                sigStr.erase(0, start);
            if (auto const end = sigStr.find_last_not_of(" \t\r\n"); end != std::string::npos)
                sigStr.erase(end + 1);

            sig.entries.push_back(SignatureEntry {
                .kind = SignatureEntry::Kind::Val,
                .name = std::move(nameStr),
                .signature = std::move(sigStr),
            });
        }
        // Parse "type Name = ..."
        else if (trimmed.starts_with("type "))
        {
            auto const rest = trimmed.substr(5);
            auto nameStr = std::string {};

            // Extract the type name (until '=' or end of line)
            for (auto const ch: rest)
            {
                if (ch == '=' || ch == ' ' || ch == '\t')
                    break;
                nameStr += ch;
            }

            auto sigStr = std::string {};
            if (auto const eqPos = rest.find('='); eqPos != std::string::npos)
            {
                sigStr = std::string(rest.substr(eqPos + 1));
                if (auto const start = sigStr.find_first_not_of(" \t"); start != std::string::npos)
                    sigStr.erase(0, start);
                if (auto const end = sigStr.find_last_not_of(" \t\r\n"); end != std::string::npos)
                    sigStr.erase(end + 1);
            }

            sig.entries.push_back(SignatureEntry {
                .kind = SignatureEntry::Kind::Type,
                .name = std::move(nameStr),
                .signature = std::move(sigStr),
            });
        }
    }

    return sig;
}

std::vector<std::string> validateSignature(ModuleDescriptor const& descriptor,
                                           ModuleSignature const& signature)
{
    auto errors = std::vector<std::string> {};

    for (auto const& entry: signature.entries)
    {
        switch (entry.kind)
        {
            case SignatureEntry::Kind::Val: {
                // Check if the module exports a function or value with this name
                auto const hasFunction = descriptor.functions.contains(entry.name);
                auto const hasValue =
                    std::ranges::any_of(descriptor.valueBindings,
                                        [&](auto const& b) { return b.name == entry.name; });
                if (!hasFunction && !hasValue)
                {
                    errors.push_back(std::format(
                        "Module '{}' does not match signature: missing '{} : {}'",
                        descriptor.name,
                        entry.name,
                        entry.signature));
                }
                break;
            }
            case SignatureEntry::Kind::Type: {
                // Check if the module exports a type with this name
                auto const hasProduct = std::ranges::any_of(
                    descriptor.productTypes, [&](auto const& t) { return t.name == entry.name; });
                auto const hasSum = std::ranges::any_of(
                    descriptor.sumTypes, [&](auto const& t) { return t.name == entry.name; });
                if (!hasProduct && !hasSum)
                {
                    errors.push_back(std::format(
                        "Module '{}' does not match signature: missing type '{}'",
                        descriptor.name,
                        entry.name));
                }
                break;
            }
        }
    }

    return errors;
}

} // namespace endo
