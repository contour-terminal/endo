// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/LiteralType.h>

#include <string>
#include <vector>

namespace CoreVM
{

class Signature
{
  private:
    std::string _name;
    LiteralType _returnType;
    std::vector<LiteralType> _args;

  public:
    Signature();
    explicit Signature(const std::string& signature);
    Signature(Signature&&) = default;
    Signature(const Signature&) = default;
    Signature& operator=(Signature&&) = default;
    Signature& operator=(const Signature&) = default;

    void setName(std::string name) { _name = std::move(name); }
    void setReturnType(LiteralType rt) { _returnType = rt; }
    void setArgs(std::vector<LiteralType> args) { _args = std::move(args); }

    [[nodiscard]] const std::string& name() const { return _name; }
    [[nodiscard]] LiteralType returnType() const { return _returnType; }
    [[nodiscard]] const std::vector<LiteralType>& args() const { return _args; }
    [[nodiscard]] std::vector<LiteralType>& args() { return _args; }

    [[nodiscard]] std::string to_s() const;

    [[nodiscard]] bool operator==(const Signature& v) const { return to_s() == v.to_s(); }
    [[nodiscard]] bool operator!=(const Signature& v) const { return to_s() != v.to_s(); }
    [[nodiscard]] bool operator<(const Signature& v) const { return to_s() < v.to_s(); }
    [[nodiscard]] bool operator>(const Signature& v) const { return to_s() > v.to_s(); }
    [[nodiscard]] bool operator<=(const Signature& v) const { return to_s() <= v.to_s(); }
    [[nodiscard]] bool operator>=(const Signature& v) const { return to_s() >= v.to_s(); }
};

LiteralType typeSignature(char ch);
char signatureType(LiteralType t);

} // namespace CoreVM

namespace fmt
{
template <>
struct formatter<CoreVM::Signature>
{
    template <typename ParseContext>
    auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const CoreVM::Signature& v, FormatContext& ctx)
    {
        return format_to(ctx.begin(), v.to_s());
    }
};
} // namespace fmt
