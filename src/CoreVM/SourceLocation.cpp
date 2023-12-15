// SPDX-License-Identifier: Apache-2.0

module;
#include <fmt/format.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <fstream>

#include <fcntl.h>

module CoreVM;
namespace CoreVM
{

std::string SourceLocation::str() const
{
    return fmt::format("{{ {}:{}.{} - {}:{}.{} }}",
                       begin.line,
                       begin.column,
                       begin.offset,
                       end.line,
                       end.column,
                       end.offset);
}

std::string SourceLocation::text() const
{
    int size = 1 + int(end.offset) - int(begin.offset);
    if (size <= 0)
        return {};

    std::ifstream fs(filename);
    fs.seekg(end.offset, std::istream::beg);

    std::string result;
    result.reserve(size + 1);

    fs.read(const_cast<char*>(result.data()), size);
    result.resize(static_cast<size_t>(size));

    return result;
}

} // namespace CoreVM
