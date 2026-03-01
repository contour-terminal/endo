// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/Diagnostics.hpp>
#include <CoreVM/SourceLocation.hpp>
#include <CoreVM/enums.hpp>
#include <CoreVM/util.hpp>
#include <CoreVM/vm/NativeCallback.hpp>

#include <format>
#include <functional>

namespace std
{

template <>
struct hash<CoreVM::util::Cidr>
{
    size_t operator()(const CoreVM::util::Cidr& v) const noexcept
    {
        auto const* bytes = static_cast<const uint8_t*>(v.address().data());
        auto const len = v.address().size();
        size_t h = 0;
        for (size_t i = 0; i < len; ++i)
            h = h * 131 + bytes[i];
        return h ^ static_cast<size_t>(v.prefix());
    }
};

template <>
struct hash<CoreVM::LiteralType>
{
    uint32_t operator()(CoreVM::LiteralType v) const noexcept { return static_cast<uint32_t>(v); }
};

} // namespace std

template <>
struct std::formatter<CoreVM::util::Cidr>: std::formatter<std::string>
{
    auto format(CoreVM::util::Cidr const& value, std::format_context& ctx) const
        -> std::format_context::iterator
    {
        return std::formatter<std::string>::format(value.str(), ctx);
    }
};

template <>
struct std::formatter<CoreVM::Signature>: std::formatter<std::string>
{
    auto format(const CoreVM::Signature& v, std::format_context& ctx) const -> std::format_context::iterator
    {
        return std::formatter<std::string>::format(v.to_s(), ctx);
    }
};

template <>
struct std::formatter<CoreVM::util::RegExp>: std::formatter<std::string>
{
    auto format(CoreVM::util::RegExp const& v, std::format_context& ctx) const
        -> std::format_context::iterator
    {
        return std::formatter<std::string>::format(v.pattern(), ctx);
    }
};

template <>
struct std::formatter<CoreVM::diagnostics::Type>: std::formatter<std::string_view>
{
    auto format(const CoreVM::diagnostics::Type& value, std::format_context& ctx) const
        -> std::format_context::iterator
    {
        std::string_view name;
        switch (value)
        {
            case CoreVM::diagnostics::Type::TokenError: name = "TokenError"; break;
            case CoreVM::diagnostics::Type::SyntaxError: name = "SyntaxError"; break;
            case CoreVM::diagnostics::Type::TypeError: name = "TypeError"; break;
            case CoreVM::diagnostics::Type::Warning: name = "Warning"; break;
            case CoreVM::diagnostics::Type::LinkError: name = "LinkError"; break;
        }
        return std::formatter<std::string_view>::format(name, ctx);
    }
};

template <>
struct std::formatter<CoreVM::diagnostics::Message>: std::formatter<std::string>
{
    auto format(const CoreVM::diagnostics::Message& value, std::format_context& ctx) const
        -> std::format_context::iterator
    {
        return std::formatter<std::string>::format(value.string(), ctx);
    }
};

template <>
struct std::formatter<CoreVM::LiteralType>: std::formatter<std::string_view>
{
    auto format(CoreVM::LiteralType id, std::format_context& ctx) const -> std::format_context::iterator
    {
        std::string_view name;
        switch (id)
        {
            case CoreVM::LiteralType::Void: name = "Void"; break;
            case CoreVM::LiteralType::Boolean: name = "Boolean"; break;
            case CoreVM::LiteralType::Number: name = "Number"; break;
            case CoreVM::LiteralType::String: name = "String"; break;
            case CoreVM::LiteralType::IPAddress: name = "IPAddress"; break;
            case CoreVM::LiteralType::Cidr: name = "Cidr"; break;
            case CoreVM::LiteralType::RegExp: name = "RegExp"; break;
            case CoreVM::LiteralType::Function: name = "Function"; break;
            case CoreVM::LiteralType::IntArray: name = "IntArray"; break;
            case CoreVM::LiteralType::StringArray: name = "StringArray"; break;
            case CoreVM::LiteralType::IPAddrArray: name = "IPAddrArray"; break;
            case CoreVM::LiteralType::CidrArray: name = "CidrArray"; break;
            case CoreVM::LiteralType::IntPair: name = "IntPair"; break;
            case CoreVM::LiteralType::Option: name = "Option"; break;
            case CoreVM::LiteralType::Result: name = "Result"; break;
            case CoreVM::LiteralType::Object: name = "Object"; break;
            case CoreVM::LiteralType::Float: name = "Float"; break;
        }
        return std::formatter<std::string_view>::format(name, ctx);
    }
};

template <>
struct std::formatter<CoreVM::FilePos>: std::formatter<std::string>
{
    auto format(const CoreVM::FilePos& value, std::format_context& ctx) const -> std::format_context::iterator
    {
        return std::formatter<std::string>::format(CoreVM::tos(value), ctx);
    }
};

template <>
struct std::formatter<CoreVM::SourceLocation>: std::formatter<std::string>
{
    auto format(const CoreVM::SourceLocation& value, std::format_context& ctx) const
        -> std::format_context::iterator
    {
        if (!value.filename.empty())
            return std::formatter<std::string>::format(value.filename + ":" + CoreVM::tos(value.begin), ctx);
        else
            return std::formatter<std::string>::format(CoreVM::tos(value.begin), ctx);
    }
};
