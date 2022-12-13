// SPDX-License-Identifier: Apache-2.0

#include <CoreVM/MatchClass.h>

#include <cassert>

namespace CoreVM
{

std::string tos(MatchClass mc)
{
    switch (mc)
    {
        case MatchClass::Same: return "Same";
        case MatchClass::Head: return "Head";
        case MatchClass::Tail: return "Tail";
        case MatchClass::RegExp: return "RegExp";
        default: assert(!"FIXME: NOT IMPLEMENTED"); return "<FIXME>";
    }
}

} // namespace CoreVM
