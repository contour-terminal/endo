// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/LiteralType.h>
#include <CoreVM/util/RegExp.h>
#include <CoreVM/util/assert.h>
#include <CoreVM/vm/Handler.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iosfwd>
#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CoreVM
{

using Quota = int64_t;
constexpr Quota NoQuota = -1;

// ExecutionEngine
// VM
class Runner
{
  public:
    enum State
    {
        Inactive,  //!< No handler running nor suspended.
        Running,   //!< Active handler is currently running.
        Suspended, //!< Active handler is currently suspended.
    };

    class QuotaExceeded: public std::runtime_error
    {
      public:
        QuotaExceeded(): std::runtime_error { "CoreVM runtime quota exceeded." } {}
    };

    using Value = uint64_t;
    using Globals = std::vector<Value>;

    using TraceLogger = std::function<void(Instruction instr, size_t ip, size_t sp)>;

    class Stack
    { // {{{
      public:
        explicit Stack(size_t stackSize) { _stack.reserve(stackSize); }

        void push(Value value) { _stack.push_back(value); }

        Value pop()
        {
            COREVM_ASSERT(!_stack.empty(), "BUG: Cannot pop from empty stack.");
            Value v = _stack.back();
            _stack.pop_back();
            return v;
        }

        void discard(size_t n)
        {
            COREVM_ASSERT(n <= _stack.size(), "vm: Attempt to discard more items than available on stack.");
            n = std::min(n, _stack.size());
            _stack.resize(_stack.size() - n);
        }

        void rotate(size_t n);

        size_t size() const { return _stack.size(); }

        Value operator[](int relativeIndex) const
        {
            if (relativeIndex < 0)
            {
                COREVM_ASSERT(static_cast<size_t>(-relativeIndex - 1) < _stack.size(),
                              "vm: Attempt to load from stack beyond stack top");
                return _stack[_stack.size() + relativeIndex];
            }
            else
            {
                COREVM_ASSERT(static_cast<size_t>(relativeIndex) < _stack.size(),
                              "vm: Attempt to load from stack beyond stack top");
                return _stack[relativeIndex];
            }
        }

        Value& operator[](int relativeIndex)
        {
            if (relativeIndex < 0)
            {
                COREVM_ASSERT(static_cast<size_t>(-relativeIndex - 1) < _stack.size(),
                              "vm: Attempt to load from stack beyond stack top");
                return _stack[_stack.size() + relativeIndex];
            }
            else
            {
                COREVM_ASSERT(static_cast<size_t>(relativeIndex) < _stack.size(),
                              "vm: Attempt to load from stack beyond stack top");
                return _stack[relativeIndex];
            }
        }

        Value operator[](size_t absoluteIndex) const { return _stack[absoluteIndex]; }

        Value& operator[](size_t absoluteIndex) { return _stack[absoluteIndex]; }

      private:
        std::vector<Value> _stack;
    };
    // }}}

  public:
    Runner(const Handler* handler, void* userdata, Globals* globals, TraceLogger logger);
    Runner(const Handler* handler, void* userdata, Globals* globals, Quota quota, TraceLogger logger);
    ~Runner() = default;

    const Handler* handler() const noexcept { return _handler; }
    const Program* program() const noexcept { return _program; }
    void* userdata() const noexcept { return _userdata; }

    bool run();
    void suspend();
    bool resume();
    void rewind();

    /** Retrieves the last saved program execution offset. */
    size_t getInstructionPointer() const noexcept { return _ip; }

    /** Retrieves number of elements on stack. */
    size_t getStackPointer() const noexcept { return _stack.size(); }

    const util::RegExpContext* regexpContext() const noexcept { return &_regexpContext; }

    CoreString* newString(std::string value);

  private:
    //! consumes @p tokens from quota and raises QuotaExceeded if quota is being exceeded.
    void consume(Opcode op);

    //! retrieves a pointer to a an empty string constant.
    const CoreString* emptyString() const { return &*_stringGarbage.begin(); }

    CoreString* catString(const CoreString& a, const CoreString& b);

    const Stack& stack() const noexcept { return _stack; }
    Value stack(int si) const { return _stack[si]; }

    CoreNumber getNumber(int si) const { return static_cast<CoreNumber>(_stack[si]); }
    const CoreString& getString(int si) const { return *(CoreString*) _stack[si]; }
    const util::IPAddress& getIPAddress(int si) const { return *(util::IPAddress*) _stack[si]; }
    const util::Cidr& getCidr(int si) const { return *(util::Cidr*) _stack[si]; }
    const util::RegExp& getRegExp(int si) const { return *(util::RegExp*) _stack[si]; }

    const CoreString* getStringPtr(int si) const { return (CoreString*) _stack[si]; }
    const util::Cidr* getCidrPtr(int si) const { return (util::Cidr*) _stack[si]; }

    void push(Value value) { _stack.push(value); }
    Value pop() { return _stack.pop(); }
    void discard(size_t n) { _stack.discard(n); }
    void pushString(const CoreString* value) { push((Value) value); }

    bool loop();

    Runner(Runner&) = delete;
    Runner& operator=(Runner&) = delete;

  private:
    Quota _quota;
    const Handler* _handler;
    TraceLogger _traceLogger;

    /**
     * We especially keep this ref to prevent ensure handler has
     * access to the program until the end, which is not garranteed
     * as it is only having a weak reference to the program (to avoid cycling
     * references).
     */
    const Program* _program;

    //! pointer to the currently evaluated HttpRequest/HttpResponse our case
    void* _userdata;

    util::RegExpContext _regexpContext;

    State _state; //!< current VM state
    size_t _ip;   //!< last saved program execution offset

    Stack _stack; //!< runtime stack

    Globals& _globals; //!< runtime global scope

    std::list<std::string> _stringGarbage;
};

} // namespace CoreVM
