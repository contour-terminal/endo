// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/CoreTypes.hpp>
#include <CoreVM/RuntimeConfig.hpp>
#include <CoreVM/enums.hpp>
#include <CoreVM/types/TypedObject.hpp>
#include <CoreVM/util.hpp>
#include <CoreVM/util/assert.hpp>
#include <CoreVM/vm/ObjectPool.hpp>

#include <cstdint>
#include <expected>
#include <functional>
#include <list>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace CoreVM
{

class Function;
class Program;
struct RuntimeError;
class TypeRegistry;

/// ExecutionEngine / VM
class Runner
{
  public:
    enum class State : uint8_t
    {
        Inactive,
        Running,
        Suspended,
    };

    class QuotaExceeded: public std::runtime_error
    {
      public:
        QuotaExceeded(): std::runtime_error { "CoreVM runtime quota exceeded." } {}
    };

    /// Represents a single frame on the call stack.
    struct CallFrame
    {
        size_t ip = 0;
        const Function* function = nullptr;
        size_t fp = 0;
        size_t argsBase = 0;
        TypedObject* lazyObj = nullptr;
    };

    using Value = uint64_t;
    using Globals = std::vector<Value>;

    using TraceLogger = std::function<void(Instruction instr, size_t ip, size_t sp)>;

    /// Callback invoked on runtime errors. Returns true to suspend (debugger break), false to propagate.
    using ErrorCallback = std::function<bool(RuntimeError const&)>;

    class Stack
    {
      public:
        explicit Stack(size_t stackSize) { _stack.reserve(stackSize); }

        void push(Value value) { _stack.push_back(value); }

        Value pop()
        {
            Value v = _stack.back();
            _stack.pop_back();
            return v;
        }

        void discard(size_t n)
        {
            n = std::min(n, _stack.size());
            _stack.resize(_stack.size() - n);
        }

        void rotate(size_t n);
        void rotate(size_t fp, size_t n);

        [[nodiscard]] size_t size() const { return _stack.size(); }

        Value operator[](int relativeIndex) const
        {
            if (relativeIndex < 0)
            {
                auto const absIndex = static_cast<size_t>(-relativeIndex);
                COREVM_ASSERT(absIndex <= _stack.size(), "VM stack underflow");
                return _stack[_stack.size() - absIndex];
            }
            else
            {
                return _stack[relativeIndex];
            }
        }

        Value& operator[](int relativeIndex)
        {
            if (relativeIndex < 0)
            {
                auto const absIndex = static_cast<size_t>(-relativeIndex);
                COREVM_ASSERT(absIndex <= _stack.size(), "VM stack underflow");
                return _stack[_stack.size() - absIndex];
            }
            else
            {
                return _stack[relativeIndex];
            }
        }

        Value operator[](size_t absoluteIndex) const { return _stack[absoluteIndex]; }

        Value& operator[](size_t absoluteIndex) { return _stack[absoluteIndex]; }

      private:
        std::vector<Value> _stack;
    };

  public:
    Runner(Runner&) = delete;
    Runner& operator=(Runner&) = delete;

    using RunResult = std::expected<bool, RuntimeError>;

    Runner(
        const Function* function, void* userdata, Globals* globals, RuntimeConfig config, TraceLogger logger);
    Runner(const Function* function,
           void* userdata,
           Globals* globals,
           Quota quota,
           RuntimeConfig config,
           TraceLogger logger);
    ~Runner() = default;

    /// Returns the current execution state.
    [[nodiscard]] State state() const noexcept { return _state; }

    const RuntimeConfig& config() const noexcept { return _config; }

    const Function* function() const noexcept { return _function; }

    const Program* program() const noexcept { return _program; }

    void* userdata() const noexcept { return _userdata; }

    bool run();
    RunResult runWithResult();

    void suspend();
    bool resume();
    RunResult resumeWithResult();
    void rewind();

    size_t getInstructionPointer() const noexcept { return _ip; }

    size_t getStackPointer() const noexcept { return _stack.size(); }

    /// Returns the current call stack depth.
    [[nodiscard]] size_t callStackDepth() const noexcept { return _callStack.size(); }

    /// Returns a read-only view of the call stack.
    [[nodiscard]] std::span<CallFrame const> callStack() const noexcept { return _callStack; }

    /// Returns the current frame pointer.
    [[nodiscard]] size_t framePointer() const noexcept { return _fp; }

    const util::RegExpContext* regexpContext() const noexcept { return &_regexpContext; }

    CoreString* newString(std::string value);

    const Stack& stack() const noexcept { return _stack; }

    TypedObject* allocObject(uint16_t typeId);

    TypedObject* makeNilList(LiteralType elemType = LiteralType::Void);
    TypedObject* makeConsCell(uint64_t head, TypedObject* tail, LiteralType elemType = LiteralType::Void);

    TypedObject* makeSomeOption(uint64_t value, LiteralType innerType = LiteralType::Void);
    TypedObject* makeNoneOption();

    TypedObject* makeOkResult(uint64_t value, LiteralType innerType = LiteralType::Void);
    TypedObject* makeErrorResult(uint64_t value, LiteralType innerType = LiteralType::Void);

    [[nodiscard]] bool isKnownObject(uint64_t rawValue) const noexcept;
    [[nodiscard]] bool isKnownString(uint64_t rawValue) const noexcept;

    /// Sets the error callback for debugger exception breakpoints.
    void setErrorCallback(ErrorCallback callback) { _errorCallback = std::move(callback); }

    /// Provides mutable access to the stack for variable mutation.
    Stack& mutableStack() noexcept { return _stack; }

    /// Write barrier: tracks objects whose slots were mutated (potential cycle roots).
    /// No-op when GC is disabled. Called by mutation opcodes (future ref cells).
    void writeBarrier(TypedObject* obj) noexcept;

    /// Enumerate root objects from stack, globals, and call frames.
    [[nodiscard]] std::vector<TypedObject*> enumerateRoots() const;

    /// Perform a full GC cycle (mark-and-sweep + string sweep).
    void performGC();

    /// Returns the object pool (for testing/inspection).
    [[nodiscard]] ObjectPool const& objectPool() const noexcept { return _objectPool; }

    /// Returns the GC suspects set (for testing/inspection).
    [[nodiscard]] std::unordered_set<TypedObject*> const& gcSuspects() const noexcept { return _gcSuspects; }

  private:
    void consume(Opcode op);

    const CoreString* emptyString() const { return &*_stringGarbage.begin(); }

    CoreString* catString(const CoreString& a, const CoreString& b);

    Value stack(int si) const { return _stack[si]; }

    CoreNumber getNumber(int si) const { return static_cast<CoreNumber>(_stack[si]); }

    const CoreString& getString(int si) const { return *(CoreString*) _stack[si]; }

    const util::IPAddress& getIPAddress(int si) const { return *(util::IPAddress*) _stack[si]; }

    const util::Cidr& getCidr(int si) const { return *(util::Cidr*) _stack[si]; }

    const util::RegExp& getRegExp(int si) const { return *(util::RegExp*) _stack[si]; }

    const CoreString* getStringPtr(int si) const { return (CoreString*) _stack[si]; }

    const util::Cidr* getCidrPtr(int si) const { return (util::Cidr*) _stack[si]; }

    TypedObject* getObject(int si) const { return reinterpret_cast<TypedObject*>(_stack[si]); }

    /// Recursively release child objects and deallocate. Called when refcount hits 0.
    void releaseAndFree(TypedObject* obj);

    const TypeRegistry& typeRegistry() const;

    void push(Value value) { _stack.push(value); }

    Value pop() { return _stack.pop(); }

    void discard(size_t n) { _stack.discard(n); }

    void pushString(const CoreString* value) { push((Value) value); }

    void pushObject(TypedObject* obj) { push(reinterpret_cast<Value>(obj)); }

    RunResult loopWithResult();
    RunResult handleRuntimeError(RuntimeError error);

    TypedObject* createPartialCallable(TypedObject* callable, std::vector<Value> const& partialArgs);

    [[nodiscard]] RuntimeError makeError(std::string message) const;

  private:
    Quota _quota;
    RuntimeConfig _config;
    const Function* _function;
    TraceLogger _traceLogger;
    ErrorCallback _errorCallback;

    const Program* _program = nullptr;

    void* _userdata = nullptr;

    util::RegExpContext _regexpContext;

    State _state = State::Inactive;
    size_t _ip = 0;

    Stack _stack;

    Globals& _globals;

    std::list<std::string> _stringGarbage;
    std::unordered_set<CoreString const*> _knownStrings; ///< O(1) string pointer validation.

    ObjectPool _objectPool;                              ///< Slab-allocated object pool.
    std::unordered_set<TypedObject*> _gcSuspects;        ///< Objects with mutated slots (write barrier).

    size_t _fp = 0;

    std::vector<CallFrame> _callStack;

    static constexpr size_t maxCallDepth = 10000;
};

} // namespace CoreVM
