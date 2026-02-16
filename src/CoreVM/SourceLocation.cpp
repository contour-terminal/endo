// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>

#include <format>
#include <fstream>

#include <sys/stat.h>
#include <sys/types.h>

#include <fcntl.h>

namespace CoreVM
{

std::string tos(FilePos const& pos)
{
    return std::to_string(pos.line) + ":" + std::to_string(pos.column);
}

std::string SourceLocation::str() const
{
    return std::format("{{ {}:{}.{} - {}:{}.{} }}",
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
