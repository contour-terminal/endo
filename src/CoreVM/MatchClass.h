// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

namespace CoreVM
{

enum class MatchClass
{
    Same,
    Head,
    Tail,
    RegExp,
};

std::string tos(MatchClass c);

} // namespace CoreVM
