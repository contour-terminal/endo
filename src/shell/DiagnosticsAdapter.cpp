// SPDX-License-Identifier: Apache-2.0
#include <shell/DiagnosticsAdapter.hpp>

#include <algorithm>
#include <string>
#include <string_view>

namespace endo
{

CoreVM::SourceLocation toCoreLoc(SourceLocationRange const& range)
{
    return CoreVM::SourceLocation(std::string(range.name),
                                  CoreVM::FilePos { static_cast<unsigned>(range.begin.line + 1),
                                                    static_cast<unsigned>(range.begin.column + 1) },
                                  CoreVM::FilePos { static_cast<unsigned>(range.end.line + 1),
                                                    static_cast<unsigned>(range.end.column + 1) });
}

CoreVM::SourceLocation toCoreLoc(SourceLocation const& loc)
{
    return CoreVM::SourceLocation(
        std::string(loc.name),
        CoreVM::FilePos { static_cast<unsigned>(loc.line + 1), static_cast<unsigned>(loc.column + 1) },
        CoreVM::FilePos { static_cast<unsigned>(loc.line + 1), static_cast<unsigned>(loc.column + 1) });
}

std::string extractSourceLine(std::string_view source, int line)
{
    if (line < 0)
        return {};

    int currentLine = 0;
    size_t lineStart = 0;

    for (size_t i = 0; i < source.size(); ++i)
    {
        if (currentLine == line)
        {
            // Find end of this line
            auto const lineEnd = source.find('\n', i);
            auto const length = (lineEnd == std::string_view::npos) ? source.size() - i : lineEnd - i;
            return std::string(source.substr(i, length));
        }

        if (source[i] == '\n')
        {
            ++currentLine;
            lineStart = i + 1;
        }
    }

    // Check if we're at the last line (no trailing newline)
    if (currentLine == line && lineStart < source.size())
        return std::string(source.substr(lineStart));

    return {};
}

std::string createCaretLine(int column, int length)
{
    if (column < 0)
        column = 0;
    if (length < 1)
        length = 1;

    std::string result(static_cast<size_t>(column), ' ');
    result += '^';
    if (length > 1)
        result += std::string(static_cast<size_t>(length - 1), '~');
    return result;
}

} // namespace endo
