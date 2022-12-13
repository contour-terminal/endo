// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/LiteralType.h>
#include <CoreVM/Signature.h>
#include <CoreVM/util/Cidr.h>
#include <CoreVM/util/IPAddress.h>
#include <CoreVM/util/RegExp.h>
#include <CoreVM/vm/Runner.h> // Runner*, Runner::Value

#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace CoreVM
{

class Params;
class Instr;
class IRBuilder;
class Runtime;

enum class Attribute : unsigned
{
    Experimental = 0x0001,   // implementation is experimental, hence, parser can warn on use
    NoReturn = 0x0002,       // implementation never returns to program code
    SideEffectFree = 0x0004, // implementation is side effect free
};

class NativeCallback
{
  public:
    using Functor = std::function<void(Params& args)>;
    using Verifier = std::function<bool(Instr*, IRBuilder*)>;
    using DefaultValue =
        std::variant<std::monostate, bool, CoreString, CoreNumber, util::IPAddress, util::Cidr, util::RegExp>;

  private:
    Runtime* _runtime;
    bool _isHandler;
    Verifier _verifier;
    Functor _function;
    Signature _signature;

    // function attributes
    unsigned _attributes;

    // following attribs are irrelevant to the VM but useful for the frontend
    std::vector<std::string> _names;
    std::vector<DefaultValue> _defaults;

  public:
    NativeCallback(Runtime* runtime, std::string name);
    NativeCallback(Runtime* runtime, std::string name, LiteralType returnType);
    ~NativeCallback() = default;

    [[nodiscard]] bool isHandler() const noexcept;
    [[nodiscard]] bool isFunction() const noexcept;
    [[nodiscard]] std::string const& name() const noexcept;
    [[nodiscard]] Signature const& signature() const noexcept;

    // signature builder

    /** Declare the return type. */
    NativeCallback& returnType(LiteralType type);

    /** Declare a single named parameter without default value. */
    template <typename T>
    NativeCallback& param(const std::string& name);

    /** Declare a single named parameter with default value. */
    template <typename T>
    NativeCallback& param(const std::string& name, T defaultValue);

    /** Declare ordered parameter signature. */
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

    [[nodiscard]] bool getAttribute(Attribute t) const noexcept { return _attributes & unsigned(t); }
    [[nodiscard]] bool isNeverReturning() const noexcept { return getAttribute(Attribute::NoReturn); }
    [[nodiscard]] bool isReadOnly() const noexcept { return getAttribute(Attribute::SideEffectFree); }
    [[nodiscard]] bool isExperimental() const noexcept { return getAttribute(Attribute::Experimental); }

    // runtime
    void invoke(Params& args) const;
};

// {{{ inlines
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

// ------------------------------------------------------------------------------------------

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
// }}}

} // namespace CoreVM
