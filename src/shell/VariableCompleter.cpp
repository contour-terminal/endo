// SPDX-License-Identifier: Apache-2.0
#include "VariableCompleter.hpp"

#include <algorithm>

namespace endo
{

VariableCompleter::VariableCompleter(Environment const& env): _env(env)
{
}

std::vector<CompletionItem> VariableCompleter::complete(CompletionContext const& context)
{
    std::vector<CompletionItem> results;
    auto const& prefix = context.prefix;

    // Add special variables first
    for (auto const& special: specialVariables())
    {
        if (special.text.starts_with(prefix))
            results.push_back(special);
    }

    // Add environment variables
    for (auto const& varName: _env.keys())
    {
        if (varName.starts_with(prefix))
        {
            // Check if already added (avoid duplicates with specials)
            bool isDuplicate = false;
            for (auto const& existing: results)
            {
                if (existing.text == varName)
                {
                    isDuplicate = true;
                    break;
                }
            }

            if (!isDuplicate)
            {
                auto value = _env.get(varName);
                std::string description;
                if (value)
                {
                    // Truncate long values for display
                    if (value->size() > 40)
                        description = std::string(value->substr(0, 37)) + "...";
                    else
                        description = std::string(*value);
                }

                results.push_back(CompletionItem {
                    .text = varName, .displayText = varName, .description = description, .score = 50 });
            }
        }
    }

    // Sort by score (descending), then alphabetically
    std::sort(results.begin(), results.end(), [](auto const& a, auto const& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.text < b.text;
    });

    return results;
}

bool VariableCompleter::canHandle(CompletionContextType type) const
{
    return type == CompletionContextType::Variable || type == CompletionContextType::VariableBrace;
}

std::vector<CompletionItem> VariableCompleter::specialVariables()
{
    return {
        { .text = "?", .displayText = "$?", .description = "Exit status of last command", .score = 100 },
        { .text = "$", .displayText = "$$", .description = "Process ID of shell", .score = 100 },
        { .text = "!", .displayText = "$!", .description = "PID of last background job", .score = 100 },
        { .text = "0", .displayText = "$0", .description = "Name of shell or script", .score = 90 },
        { .text = "#", .displayText = "$#", .description = "Number of positional parameters", .score = 90 },
        { .text = "@",
          .displayText = "$@",
          .description = "All positional parameters (quoted)",
          .score = 90 },
        { .text = "*", .displayText = "$*", .description = "All positional parameters", .score = 90 },
        { .text = "-", .displayText = "$-", .description = "Current shell options", .score = 80 },
        { .text = "1", .displayText = "$1", .description = "First positional parameter", .score = 70 },
        { .text = "2", .displayText = "$2", .description = "Second positional parameter", .score = 70 },
        { .text = "3", .displayText = "$3", .description = "Third positional parameter", .score = 70 },
        { .text = "4", .displayText = "$4", .description = "Fourth positional parameter", .score = 70 },
        { .text = "5", .displayText = "$5", .description = "Fifth positional parameter", .score = 70 },
        { .text = "6", .displayText = "$6", .description = "Sixth positional parameter", .score = 70 },
        { .text = "7", .displayText = "$7", .description = "Seventh positional parameter", .score = 70 },
        { .text = "8", .displayText = "$8", .description = "Eighth positional parameter", .score = 70 },
        { .text = "9", .displayText = "$9", .description = "Ninth positional parameter", .score = 70 },
    };
}

} // namespace endo
