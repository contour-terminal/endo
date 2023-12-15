// SPDX-License-Identifier: Apache-2.0
module;
#include <CoreVM/util/assert.h>

module CoreVM;
namespace CoreVM
{

std::string tos(LiteralType type)
{
    switch (type)
    {
        case LiteralType::Void: return "void";
        case LiteralType::Boolean: return "bool";
        case LiteralType::Number: return "int";
        case LiteralType::String: return "string";
        case LiteralType::IPAddress: return "IPAddress";
        case LiteralType::Cidr: return "Cidr";
        case LiteralType::RegExp: return "RegExp";
        case LiteralType::Handler: return "HandlerRef";
        case LiteralType::IntArray: return "IntArray";
        case LiteralType::StringArray: return "StringArray";
        case LiteralType::IPAddrArray: return "IPAddrArray";
        case LiteralType::CidrArray: return "CidrArray";
        case LiteralType::IntPair: return "IntPair";
        default: COREVM_ASSERT(false, "InvalidArgumentError");
    }
}

bool isArrayType(LiteralType type)
{
    switch (type)
    {
        case LiteralType::IntArray:
        case LiteralType::StringArray:
        case LiteralType::IPAddrArray:
        case LiteralType::CidrArray: return true;
        default: return false;
    }
}

LiteralType elementTypeOf(LiteralType type)
{
    switch (type)
    {
        case LiteralType::Void:
        case LiteralType::Boolean:
        case LiteralType::Number:
        case LiteralType::String:
        case LiteralType::IPAddress:
        case LiteralType::Cidr:
        case LiteralType::RegExp:
        case LiteralType::Handler: return type;
        case LiteralType::IntArray: return LiteralType::Number;
        case LiteralType::StringArray: return LiteralType::String;
        case LiteralType::IPAddrArray: return LiteralType::IPAddress;
        case LiteralType::CidrArray: return LiteralType::Cidr;
        case LiteralType::IntPair: return LiteralType::Number;
        default: COREVM_ASSERT(false, "InvalidArgumentError");
    }
}

} // namespace CoreVM
