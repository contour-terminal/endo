// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreTypes.hpp>
#include <CoreVM/enums.hpp>
#include <CoreVM/types/TypedObject.hpp>
#include <CoreVM/util.hpp>

#include <cassert>
#include <functional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace CoreVM
{

class Instr;
class IRBuilder;
class Runner;
class Function;
class Program;
class Runtime;

// =============================================================================
// Params
// =============================================================================

class Params
{
  public:
    using Value = uint64_t;

    Params(Runner* caller, int argc): _caller(caller), _argc(argc), _argv(argc + 1) {}

    void setArg(int argi, Value value) { _argv[argi] = value; }

    [[nodiscard]] Runner* caller() const { return _caller; }

    void setResult(bool value) { _argv[0] = value; }

    void setResult(CoreNumber value) { _argv[0] = static_cast<Value>(value); }

    void setResult(const Function* fn);

    void setResult(const char* str);

    void setResult(std::string str);

    void setResult(const CoreString* str) { _argv[0] = reinterpret_cast<Value>(str); }

    void setResult(const util::IPAddress* ip) { _argv[0] = reinterpret_cast<Value>(ip); }

    void setResult(const util::Cidr* cidr) { _argv[0] = reinterpret_cast<Value>(cidr); }

    [[deprecated("Use count()")]] [[nodiscard]] int size() const { return _argc; }

    [[nodiscard]] int count() const { return _argc; }

    [[nodiscard]] Value at(size_t i) const { return _argv[i]; }

    [[nodiscard]] Value operator[](size_t i) const { return _argv[i]; }

    [[nodiscard]] Value& operator[](size_t i) { return _argv[i]; }

    [[nodiscard]] bool getBool(size_t offset) const { return at(offset); }

    [[nodiscard]] CoreNumber getInt(size_t offset) const { return static_cast<CoreNumber>(at(offset)); }

    [[nodiscard]] const CoreString& getString(size_t offset) const
    {
        return *reinterpret_cast<CoreString*>(at(offset));
    }

    [[nodiscard]] Function* getFunction(size_t offset) const;

    [[nodiscard]] const util::IPAddress& getIPAddress(size_t offset) const
    {
        return *reinterpret_cast<util::IPAddress*>(at(offset));
    }

    [[nodiscard]] const util::Cidr& getCidr(size_t offset) const
    {
        return *reinterpret_cast<util::Cidr*>(at(offset));
    }

    /// @brief Retrieves a TypedObject pointer from the argument at the given offset.
    [[nodiscard]] TypedObject* getObject(size_t offset) const
    {
        return reinterpret_cast<TypedObject*>(static_cast<uintptr_t>(at(offset)));
    }

    [[nodiscard]] const CoreIntArray& getIntArray(size_t offset) const
    {
        return *reinterpret_cast<CoreIntArray*>(at(offset));
    }

    [[nodiscard]] const CoreStringArray& getStringArray(size_t offset) const
    {
        return *reinterpret_cast<CoreStringArray*>(at(offset));
    }

    [[nodiscard]] const CoreIPAddrArray& getIPAddressArray(size_t offset) const
    {
        return *reinterpret_cast<CoreIPAddrArray*>(at(offset));
    }

    [[nodiscard]] const CoreCidrArray& getCidrArray(size_t offset) const
    {
        return *reinterpret_cast<CoreCidrArray*>(at(offset));
    }

    class iterator // NOLINT
    {
      private:
        Params* _params;
        size_t _current;

      public:
        iterator(Params* p, size_t init): _params(p), _current(init) {}

        iterator(const iterator& v) = default;

        [[nodiscard]] size_t offset() const { return _current; }

        [[nodiscard]] Value get() const { return _params->at(_current); }

        [[nodiscard]] Value& operator*() { return _params->_argv[_current]; }

        [[nodiscard]] const Value& operator*() const { return _params->_argv[_current]; }

        iterator& operator++()
        {
            if (std::cmp_not_equal(_current, _params->_argc))
            {
                ++_current;
            }
            return *this;
        }

        [[nodiscard]] bool operator==(const iterator& other) const { return _current == other._current; }

        [[nodiscard]] bool operator!=(const iterator& other) const { return _current != other._current; }
    };

    [[nodiscard]] iterator begin() { return iterator(this, std::min(1, _argc)); }

    [[nodiscard]] iterator end() { return iterator(this, _argc); }

  private:
    Runner* _caller;
    int _argc;
    std::vector<Value> _argv;
};

// =============================================================================
// Attribute
// =============================================================================

enum class Attribute : uint8_t
{
    Experimental = 0x01,
    NoReturn = 0x02,
    SideEffectFree = 0x04,
};

// =============================================================================
// Signature
// =============================================================================

class Signature
{
  private:
    std::string _name;
    LiteralType _returnType = LiteralType::Void;
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

// =============================================================================
// NativeCallback
// =============================================================================

class NativeCallback
{
  public:
    using Functor = std::function<void(Params& args)>;
    using Verifier = std::function<bool(Instr*, IRBuilder*)>;
    using DefaultValue =
        std::variant<std::monostate, bool, CoreString, CoreNumber, util::IPAddress, util::Cidr, util::RegExp>;

  private:
    Runtime* _runtime;
    Verifier _verifier;
    Functor _function;
    Signature _signature;

    unsigned _attributes = 0;

    std::vector<std::string> _names;
    std::vector<DefaultValue> _defaults;

  public:
    NativeCallback(Runtime* runtime, std::string name, LiteralType returnType = LiteralType::Void);
    ~NativeCallback() = default;

    [[nodiscard]] std::string const& name() const noexcept;
    [[nodiscard]] Signature const& signature() const noexcept;

    NativeCallback& returnType(LiteralType type);

    /** Declare a single named parameter by LiteralType. */
    NativeCallback& param(LiteralType type, const std::string& name);

    template <typename T>
    NativeCallback& param(const std::string& name);

    template <typename T>
    NativeCallback& param(const std::string& name, T defaultValue);

    template <typename... Args>
    NativeCallback& params(Args... args);

    // semantic verifier
    NativeCallback& verifier(const Verifier& vf);
    template <typename Class>
    NativeCallback& verifier(bool (Class::*method)(Instr*, IRBuilder*), Class* obj);
    template <typename Class>
    NativeCallback& verifier(bool (Class::*method)(Instr*, IRBuilder*));
    bool verify(Instr* call, IRBuilder* irBuilder);

    // bind callback
    NativeCallback& bind(const Functor& cb);
    template <typename Class>
    NativeCallback& bind(void (Class::*method)(Params&), Class* obj);
    template <typename Class>
    NativeCallback& bind(void (Class::*method)(Params&));

    // named parameter handling
    [[nodiscard]] bool parametersNamed() const;
    [[nodiscard]] const std::string& getParamNameAt(size_t i) const;
    [[nodiscard]] const DefaultValue& getDefaultParamAt(size_t i) const;
    [[nodiscard]] int findParamByName(const std::string& name) const;

    // attributes
    NativeCallback& setNoReturn() noexcept;
    NativeCallback& setReadOnly() noexcept;
    NativeCallback& setExperimental() noexcept;

    [[nodiscard]] bool getAttribute(Attribute t) const noexcept
    {
        return (_attributes & static_cast<unsigned>(t)) != 0;
    }

    [[nodiscard]] bool isNeverReturning() const noexcept { return getAttribute(Attribute::NoReturn); }

    [[nodiscard]] bool isReadOnly() const noexcept { return getAttribute(Attribute::SideEffectFree); }

    [[nodiscard]] bool isExperimental() const noexcept { return getAttribute(Attribute::Experimental); }

    // runtime
    void invoke(Params& args) const;
};

// =============================================================================
// NativeProperty
// =============================================================================

class NativeProperty
{
  public:
    using Getter = std::function<void(Params& args)>;
    using Setter = std::function<void(Params& args)>;

    NativeProperty(std::string name, LiteralType type);

    [[nodiscard]] std::string const& name() const noexcept { return _name; }

    /// Primary (declared) type of this property. Used for the getter and as the
    /// default type for setter overload registration.
    [[nodiscard]] LiteralType type() const noexcept { return _type; }

    /// All setter types accepted by this property, in registration order.
    [[nodiscard]] std::vector<LiteralType> const& acceptedSetterTypes() const noexcept
    {
        return _setterTypes;
    }

    [[nodiscard]] std::string const& description() const noexcept { return _description; }

    NativeProperty& description(std::string desc);

    [[nodiscard]] bool hasGetter() const noexcept { return _getter != nullptr; }

    /// Returns true if any setter (primary or overload) is registered.
    [[nodiscard]] bool hasSetter() const noexcept { return !_setters.empty(); }

    /// Returns true if a setter for the given argument type is registered.
    [[nodiscard]] bool hasSetter(LiteralType argType) const noexcept;

    NativeProperty& onGet(Getter cb);

    /// Registers the primary setter (accepts a value of the property's declared type()).
    NativeProperty& onSet(Setter cb);

    /// Registers an additional setter overload for the given argument type. This
    /// enables a single property to accept multiple RHS types (e.g. String or Function),
    /// dispatched by the argument's literal type at the assignment site.
    NativeProperty& onSet(LiteralType argType, Setter cb);

    template <typename Class>
    NativeProperty& onGet(void (Class::*method)(Params&), Class* obj)
    {
        return onGet([obj, method](Params& args) { (obj->*method)(args); });
    }

    template <typename Class>
    NativeProperty& onSet(void (Class::*method)(Params&), Class* obj)
    {
        return onSet([obj, method](Params& args) { (obj->*method)(args); });
    }

    void invokeGet(Params& args) const;

    /// Invokes the setter for the given argument type. No-op if no setter is
    /// registered for that type.
    void invokeSet(LiteralType argType, Params& args) const;

  private:
    std::string _name;
    LiteralType _type;
    std::string _description;
    Getter _getter;
    std::vector<std::pair<LiteralType, Setter>> _setters; ///< Ordered list of setter overloads.
    std::vector<LiteralType> _setterTypes;                ///< Accepted types (parallel to _setters).
};

// =============================================================================
// NativeCallback inline/template implementations
// =============================================================================

inline NativeCallback& NativeCallback::returnType(LiteralType type)
{
    _signature.setReturnType(type);
    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<bool>(const std::string& name)
{
    _signature.args().push_back(LiteralType::Boolean);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});
    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<bool>(const std::string& name, bool defaultValue)
{
    _signature.args().push_back(LiteralType::Boolean);
    _names.push_back(name);
    _defaults.emplace_back(defaultValue);
    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<CoreNumber>(const std::string& name)
{
    _signature.args().push_back(LiteralType::Number);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});
    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<CoreNumber>(const std::string& name, CoreNumber defaultValue)
{
    _signature.args().push_back(LiteralType::Number);
    _names.push_back(name);
    _defaults.emplace_back(defaultValue);
    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<int>(const std::string& name)
{
    _signature.args().push_back(LiteralType::Number);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});
    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<int>(const std::string& name, int defaultValue)
{
    _signature.args().push_back(LiteralType::Number);
    _names.push_back(name);
    _defaults.emplace_back(CoreNumber { defaultValue });
    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<CoreString>(const std::string& name)
{
    _signature.args().push_back(LiteralType::String);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});
    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<CoreString>(const std::string& name, CoreString defaultValue)
{
    _signature.args().push_back(LiteralType::String);
    _names.push_back(name);
    _defaults.emplace_back(defaultValue);
    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<util::IPAddress>(const std::string& name)
{
    _signature.args().push_back(LiteralType::IPAddress);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});
    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<util::IPAddress>(const std::string& name,
                                                              util::IPAddress defaultValue)
{
    _signature.args().push_back(LiteralType::IPAddress);
    _names.push_back(name);
    _defaults.emplace_back(defaultValue);
    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<util::Cidr>(const std::string& name)
{
    _signature.args().push_back(LiteralType::Cidr);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});
    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<util::Cidr>(const std::string& name, util::Cidr defaultValue)
{
    _signature.args().push_back(LiteralType::Cidr);
    _names.push_back(name);
    _defaults.emplace_back(defaultValue);
    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<util::RegExp>(const std::string& name)
{
    _signature.args().push_back(LiteralType::RegExp);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});
    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<util::RegExp>(const std::string& name, util::RegExp defaultValue)
{
    _signature.args().push_back(LiteralType::RegExp);
    _names.push_back(name);
    _defaults.emplace_back(defaultValue);
    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<CoreIntArray>(const std::string& name)
{
    assert(_defaults.size() == _names.size());
    _signature.args().push_back(LiteralType::IntArray);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});
    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<CoreStringArray>(const std::string& name)
{
    assert(_defaults.size() == _names.size());
    _signature.args().push_back(LiteralType::StringArray);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});
    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<CoreVM::TypedObject*>(const std::string& name)
{
    assert(_defaults.size() == _names.size());
    _signature.args().push_back(LiteralType::Object);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});
    return *this;
}

template <>
inline NativeCallback& NativeCallback::param<const CoreVM::Function*>(const std::string& name)
{
    assert(_defaults.size() == _names.size());
    _signature.args().push_back(LiteralType::Function);
    _names.push_back(name);
    _defaults.emplace_back(std::monostate {});
    return *this;
}

template <typename... Args>
inline NativeCallback& NativeCallback::params(Args... args)
{
    _signature.setArgs({ args... });
    return *this;
}

inline NativeCallback& NativeCallback::verifier(const Verifier& vf)
{
    _verifier = vf;
    return *this;
}

template <typename Class>
inline NativeCallback& NativeCallback::verifier(bool (Class::*method)(Instr*, IRBuilder*), Class* obj)
{
    _verifier = std::bind(method, obj, std::placeholders::_1, std::placeholders::_2);
    return *this;
}

template <typename Class>
inline NativeCallback& NativeCallback::verifier(bool (Class::*method)(Instr*, IRBuilder*))
{
    _verifier =
        std::bind(method, static_cast<Class*>(_runtime), std::placeholders::_1, std::placeholders::_2);
    return *this;
}

inline bool NativeCallback::verify(Instr* call, IRBuilder* irBuilder)
{
    if (!_verifier)
        return true;

    return _verifier(call, irBuilder);
}

inline NativeCallback& NativeCallback::bind(const Functor& cb)
{
    _function = cb;
    return *this;
}

template <typename Class>
inline NativeCallback& NativeCallback::bind(void (Class::*method)(Params&), Class* obj)
{
    _function = std::bind(method, obj, std::placeholders::_1);
    return *this;
}

template <typename Class>
inline NativeCallback& NativeCallback::bind(void (Class::*method)(Params&))
{
    _function =
        std::bind(method, static_cast<Class*>(_runtime), std::placeholders::_1, std::placeholders::_2);
    return *this;
}

inline bool NativeCallback::parametersNamed() const
{
    return !_names.empty();
}

inline const std::string& NativeCallback::getParamNameAt(size_t i) const
{
    assert(i < _names.size());
    return _names[i];
}

inline const NativeCallback::DefaultValue& NativeCallback::getDefaultParamAt(size_t i) const
{
    assert(i < _defaults.size());
    return _defaults[i];
}

} // namespace CoreVM
