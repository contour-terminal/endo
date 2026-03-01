// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/enums.hpp>
#include <CoreVM/util.hpp>

#include <memory>
#include <string>
#include <vector>

namespace CoreVM
{

class Instr;
class NativeCallback;
class Signature;

/**
 * Defines an immutable IR value.
 */
class Value
{
  protected:
    Value(const Value& v);

  public:
    Value(LiteralType ty, std::string name);
    Value(Value&&) noexcept = delete;
    Value& operator=(Value&&) noexcept = delete;
    Value& operator=(Value const&) = delete;
    virtual ~Value();

    [[nodiscard]] LiteralType type() const { return _type; }

    void setType(LiteralType ty) { _type = ty; }

    [[nodiscard]] const std::string& name() const { return _name; }

    void setName(const std::string& n) { _name = n; }

    /**
     * adds @p user to the list of instructions that are "using" this value.
     */
    void addUse(Instr* user);

    /**
     * removes @p user from the list of instructions that determines the list of
     * instructions that are using this value.
     */
    void removeUse(Instr* user);

    /**
     * Determines whether or not this value is being used by at least one other
     * instruction.
     */
    [[nodiscard]] bool isUsed() const { return !_uses.empty(); }

    /**
     * Retrieves a range instructions that are *using* this value.
     */
    [[nodiscard]] const std::vector<Instr*>& uses() const { return _uses; }

    [[nodiscard]] size_t useCount() const { return _uses.size(); }

    /**
     * Replaces all uses of \c this value as operand with value \p newUse instead.
     *
     * @param newUse the new value to be used.
     */
    void replaceAllUsesWith(Value* newUse);

    [[nodiscard]] virtual std::string to_string() const;

  private:
    LiteralType _type;
    std::string _name;

    std::vector<Instr*> _uses; //! list of instructions that <b>use</b> this value.
};

class Constant: public Value
{
  public:
    Constant(LiteralType ty, std::string name): Value(ty, std::move(name)) {}

    [[nodiscard]] std::string to_string() const override;
};

class ConstantArray: public Constant
{
  public:
    ConstantArray(LiteralType elementType, std::vector<Constant*> elements, std::string name = ""):
        Constant(makeArrayType(elementType), std::move(name)), _elements(std::move(elements))
    {
    }

    ConstantArray(const std::vector<Constant*>& elements, const std::string& name = ""):
        Constant(makeArrayType(elements.front()->type()), name), _elements(elements)
    {
    }

    ConstantArray(ConstantArray&&) = delete;
    ConstantArray& operator=(ConstantArray&&) = delete;

    ConstantArray(const ConstantArray&) = delete;
    ConstantArray& operator=(const ConstantArray&) = delete;

    [[nodiscard]] const std::vector<Constant*>& get() const { return _elements; }

    [[nodiscard]] LiteralType elementType() const { return _elements[0]->type(); }

  private:
    std::vector<Constant*> _elements;

    LiteralType makeArrayType(LiteralType elementType);
};

class IRBuiltinFunction: public Constant
{
  public:
    explicit IRBuiltinFunction(const NativeCallback& cb);

    [[nodiscard]] const Signature& signature() const;

    [[nodiscard]] const NativeCallback& getNative() const { return _native; }

  private:
    const NativeCallback& _native;
};

template <typename T, const LiteralType Ty>
class ConstantValue: public Constant
{
  public:
    ConstantValue(T value, const std::string& name = ""): Constant(Ty, name), _value(std::move(value)) {}

    [[nodiscard]] T get() const { return _value; }

    [[nodiscard]] std::string to_string() const override
    {
        auto valueStr = [&]() -> std::string {
            if constexpr (std::is_same_v<T, std::string>)
                return _value;
            else if constexpr (std::is_same_v<T, bool>)
                return _value ? "true" : "false";
            else if constexpr (std::is_arithmetic_v<T>)
                return std::to_string(_value);
            else if constexpr (requires { _value.str(); })
                return _value.str();
            else if constexpr (requires { _value.pattern(); })
                return _value.pattern();
            else
                return "?";
        }();
        return "Constant '" + std::string(name()) + "': " + tos(type()) + " = " + valueStr;
    }

  private:
    T _value;
};

using ConstantInt = ConstantValue<int64_t, LiteralType::Number>;
using ConstantBoolean = ConstantValue<bool, LiteralType::Boolean>;
using ConstantFloat = ConstantValue<double, LiteralType::Float>;
using ConstantString = ConstantValue<std::string, LiteralType::String>;
using ConstantIP = ConstantValue<util::IPAddress, LiteralType::IPAddress>;
using ConstantCidr = ConstantValue<util::Cidr, LiteralType::Cidr>;
using ConstantRegExp = ConstantValue<util::RegExp, LiteralType::RegExp>;

// Forward declaration for tos() used by ConstantValue::to_string()
std::string tos(LiteralType type);

} // namespace CoreVM
