// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/LiteralType.h>
#include <CoreVM/util/Cidr.h>
#include <CoreVM/util/IPAddress.h>
#include <CoreVM/vm/Handler.h>
#include <CoreVM/vm/Program.h>
#include <CoreVM/vm/Runner.h>

#include <memory>

namespace CoreVM
{

class Params
{
  public:
    using Value = Runner::Value;

    Params(Runner* caller, int argc): _caller(caller), _argc(argc), _argv(argc + 1) {}

    void setArg(int argi, Value value) { _argv[argi] = value; }

    [[nodiscard]] Runner* caller() const { return _caller; }

    void setResult(bool value) { _argv[0] = value; }
    void setResult(CoreNumber value) { _argv[0] = (Value) value; }
    void setResult(const Handler* handler) { _argv[0] = _caller->program()->indexOf(handler); }
    void setResult(const char* str) { _argv[0] = (Value) _caller->newString(str); }
    void setResult(std::string str) { _argv[0] = (Value) _caller->newString(std::move(str)); }
    void setResult(const CoreString* str) { _argv[0] = (Value) str; }
    void setResult(const util::IPAddress* ip) { _argv[0] = (Value) ip; }
    void setResult(const util::Cidr* cidr) { _argv[0] = (Value) cidr; }

    [[deprecated("Use count()")]] [[nodiscard]] int size() const { return _argc; }
    [[nodiscard]] int count() const { return _argc; }

    [[nodiscard]] Value at(size_t i) const { return _argv[i]; }
    [[nodiscard]] Value operator[](size_t i) const { return _argv[i]; }
    [[nodiscard]] Value& operator[](size_t i) { return _argv[i]; }

    [[nodiscard]] bool getBool(size_t offset) const { return at(offset); }
    [[nodiscard]] CoreNumber getInt(size_t offset) const { return at(offset); }
    [[nodiscard]] const CoreString& getString(size_t offset) const { return *(CoreString*) at(offset); }
    [[nodiscard]] Handler* getHandler(size_t offset) const
    {
        return _caller->program()->handler(static_cast<size_t>(at(offset)));
    }
    [[nodiscard]] const util::IPAddress& getIPAddress(size_t offset) const
    {
        return *(util::IPAddress*) at(offset);
    }
    [[nodiscard]] const util::Cidr& getCidr(size_t offset) const { return *(util::Cidr*) at(offset); }

    [[nodiscard]] const CoreIntArray& getIntArray(size_t offset) const { return *(CoreIntArray*) at(offset); }
    [[nodiscard]] const CoreStringArray& getStringArray(size_t offset) const
    {
        return *(CoreStringArray*) at(offset);
    }
    [[nodiscard]] const CoreIPAddrArray& getIPAddressArray(size_t offset) const
    {
        return *(CoreIPAddrArray*) at(offset);
    }
    [[nodiscard]] const CoreCidrArray& getCidrArray(size_t offset) const
    {
        return *(CoreCidrArray*) at(offset);
    }

    class iterator // NOLINT
    {              // {{{
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
            if (_current != static_cast<decltype(_current)>(_params->_argc))
            {
                ++_current;
            }
            return *this;
        }

        [[nodiscard]] bool operator==(const iterator& other) const { return _current == other._current; }

        [[nodiscard]] bool operator!=(const iterator& other) const { return _current != other._current; }
    }; // }}}

    [[nodiscard]] iterator begin() { return iterator(this, std::min(1, _argc)); }
    [[nodiscard]] iterator end() { return iterator(this, _argc); }

  private:
    Runner* _caller;
    int _argc;
    std::vector<Value> _argv;
};

} // namespace CoreVM
