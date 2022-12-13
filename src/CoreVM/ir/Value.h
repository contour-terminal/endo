// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/LiteralType.h>

#include <string>
#include <vector>

namespace CoreVM
{

class Instr;

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

} // namespace CoreVM
