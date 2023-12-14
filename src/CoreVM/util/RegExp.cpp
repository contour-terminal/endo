// SPDX-License-Identifier: Apache-2.0
module;

#include <cstring>
#include <regex>

module CoreVM;
namespace CoreVM::util
{

RegExp::RegExp(const std::string& pattern): _pattern(pattern), _re(pattern)
{
}

bool RegExp::match(const std::string& target, Result* result) const
{
    std::regex_search(target, *result, _re);
    return !result->empty();
}

const char* RegExp::c_str() const
{
    return _pattern.c_str();
}

} // namespace CoreVM::util
