// SPDX-License-Identifier: Apache-2.0

#include <CoreVM/ir/ConstantArray.h>

#include <cstdlib>

namespace CoreVM
{

LiteralType ConstantArray::makeArrayType(LiteralType elementType)
{
    switch (elementType)
    {
        case LiteralType::Number: return LiteralType::IntArray;
        case LiteralType::String: return LiteralType::StringArray;
        case LiteralType::IPAddress: return LiteralType::IPAddrArray;
        case LiteralType::Cidr: return LiteralType::CidrArray;
        case LiteralType::Boolean:
        case LiteralType::RegExp:
        case LiteralType::Handler:
        case LiteralType::IntArray:
        case LiteralType::StringArray:
        case LiteralType::IPAddrArray:
        case LiteralType::CidrArray:
        case LiteralType::Void: abort();
        default: abort();
    }
}

} // namespace CoreVM
