// SPDX-License-Identifier: Apache-2.0
#include "GlobMatch.hpp"

#include <string_view>

namespace endo
{

bool globMatchFilename(std::string_view filename, std::string_view pattern)
{
    size_t fi = 0;
    size_t pi = 0;
    size_t starIdx = std::string_view::npos;
    size_t matchIdx = 0;

    while (fi < filename.size())
    {
        if (pi < pattern.size() && pattern[pi] == '*')
        {
            starIdx = pi;
            matchIdx = fi;
            ++pi;
        }
        else if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == filename[fi]))
        {
            ++fi;
            ++pi;
        }
        else if (pi < pattern.size() && pattern[pi] == '[')
        {
            bool negate = false;
            bool matched = false;
            ++pi;
            if (pi < pattern.size() && (pattern[pi] == '!' || pattern[pi] == '^'))
            {
                negate = true;
                ++pi;
            }
            while (pi < pattern.size() && pattern[pi] != ']')
            {
                if (pi + 2 < pattern.size() && pattern[pi + 1] == '-' && pattern[pi + 2] != ']')
                {
                    if (filename[fi] >= pattern[pi] && filename[fi] <= pattern[pi + 2])
                        matched = true;
                    pi += 3;
                }
                else
                {
                    if (filename[fi] == pattern[pi])
                        matched = true;
                    ++pi;
                }
            }
            if (pi < pattern.size())
                ++pi;

            if (negate)
                matched = !matched;
            if (!matched)
            {
                if (starIdx != std::string_view::npos)
                {
                    pi = starIdx + 1;
                    ++matchIdx;
                    fi = matchIdx;
                }
                else
                {
                    return false;
                }
            }
            else
            {
                ++fi;
            }
        }
        else if (starIdx != std::string_view::npos)
        {
            pi = starIdx + 1;
            ++matchIdx;
            fi = matchIdx;
        }
        else
        {
            return false;
        }
    }

    while (pi < pattern.size() && pattern[pi] == '*')
        ++pi;

    return pi == pattern.size();
}

} // namespace endo
