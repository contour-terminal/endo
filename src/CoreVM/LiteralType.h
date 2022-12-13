// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/util/Cidr.h>
#include <CoreVM/util/IPAddress.h>

#include <fmt/format.h>

#include <cstdint>
#include <string>
#include <vector>

namespace CoreVM
{

//! \addtogroup CoreVM
//@{

enum class LiteralType
{
    Void = 0,
    Boolean = 1,      // bool (int64)
    Number = 2,       // int64
    String = 3,       // std::string*
    IPAddress = 5,    // IPAddress*
    Cidr = 6,         // Cidr*
    RegExp = 7,       // RegExp*
    Handler = 8,      // bool (*native_handler)(CoreContext*);
    IntArray = 9,     // array<int>
    StringArray = 10, // array<string>
    IPAddrArray = 11, // array<IPAddress>
    CidrArray = 12,   // array<Cidr>
    IntPair = 13,     // array<int, 2>
};

using Register = uint64_t; // vm

using CoreNumber = int64_t;
using CoreString = std::string;

std::string tos(LiteralType type);

bool isArrayType(LiteralType type);
LiteralType elementTypeOf(LiteralType type);

// {{{ array types
class CoreArray
{
  public:
    [[nodiscard]] size_t size() const { return static_cast<size_t>(_base[0]); }

  protected:
    explicit CoreArray(const Register* base): _base(base) {}

    [[nodiscard]] Register getRawAt(size_t i) const { return _base[1 + i]; }
    [[nodiscard]] const Register* data() const { return _base + 1; }

  protected:
    const Register* _base;
};

using CoreIntArray = std::vector<CoreNumber>;
using CoreStringArray = std::vector<CoreString>;
using CoreIPAddrArray = std::vector<util::IPAddress>;
using CoreCidrArray = std::vector<util::Cidr>;

// }}}

//!@}

} // namespace CoreVM

namespace std
{
template <>
struct hash<CoreVM::LiteralType>
{
    uint32_t operator()(CoreVM::LiteralType v) const noexcept { return static_cast<uint32_t>(v); }
};
} // namespace std

template <>
struct fmt::formatter<CoreVM::LiteralType>: fmt::formatter<std::string_view>
{
    using LiteralType = CoreVM::LiteralType;

    auto format(LiteralType id, format_context& ctx) -> format_context::iterator
    {
        std::string_view name;
        switch (id)
        {
            case LiteralType::Void: name = "Void"; break;
            case LiteralType::Boolean: name = "Boolean"; break;
            case LiteralType::Number: name = "Number"; break;
            case LiteralType::String: name = "String"; break;
            case LiteralType::IPAddress: name = "IPAddress"; break;
            case LiteralType::Cidr: name = "Cidr"; break;
            case LiteralType::RegExp: name = "RegExp"; break;
            case LiteralType::Handler: name = "Handler"; break;
            case LiteralType::IntArray: name = "IntArray"; break;
            case LiteralType::StringArray: name = "StringArray"; break;
            case LiteralType::IPAddrArray: name = "IPAddrArray"; break;
            case LiteralType::CidrArray: name = "CidrArray"; break;
            case LiteralType::IntPair: name = "IntPair"; break;
        }
        return formatter<std::string_view>::format(name, ctx);
    }
};
