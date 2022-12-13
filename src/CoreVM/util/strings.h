// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstring>
#include <sstream>
#include <string>
#include <type_traits>

inline bool beginsWith(const std::string& str, const std::string& prefix)
{
    if (str.size() < prefix.size())
        return false;

    return strncmp(str.data(), prefix.data(), prefix.size()) == 0;
}

inline bool endsWith(const std::string& str, const std::string& prefix)
{
    if (str.size() < prefix.size())
        return false;

    return strncmp(str.data() + str.size() - prefix.size(), prefix.data(), prefix.size()) == 0;
}

/**
 * Joins all items from @p list into a string.
 *
 * @param list      list of items to join
 * @param separator item separator
 * @param mapfn     pointer-to-member function that maps the an item into a string
 *
 * @returns joined list of items or an empty string if @p list is empty.
 */
template <typename Container>
std::string join(const Container& list,
                 const std::string& separator,
                 std::string (std::remove_pointer<typename Container::value_type>::type::*mapfn)() const)
{
    std::stringstream sstr;
    int i = 0;
    for (const auto& item: list)
    {
        if (i)
        {
            sstr << separator;
        }
        if constexpr (std::is_pointer<typename Container::value_type>::value)
        {
            sstr << (item->*mapfn)();
        }
        else
        {
            sstr << (item.*mapfn)();
        }
        i++;
    }
    return sstr.str();
}

/**
 * Joins all items from @p list into a string.
 *
 * @param list      list of items to join
 * @param separator item separator
 * @param mapfn     pointer-to-member function that maps the an item into a const string&
 *
 * @returns joined list of items or an empty string if @p list is empty.
 */
template <typename Container>
std::string join(const Container& list,
                 const std::string& separator,
                 const std::string& (std::remove_pointer<typename Container::value_type>::type::*mapfn)()
                     const)
{
    std::stringstream sstr;
    int i = 0;
    for (const auto& item: list)
    {
        if (i)
        {
            sstr << separator;
        }
        if constexpr (std::is_pointer<typename Container::value_type>::value)
        {
            sstr << (item->*mapfn)();
        }
        else
        {
            sstr << (item.*mapfn)();
        }
        i++;
    }
    return sstr.str();
}
