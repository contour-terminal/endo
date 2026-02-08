// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/CoreVM.hpp>
#include <CoreVM/sysconfig.h>
#include <CoreVM/util.hpp>
#include <CoreVM/util/assert.hpp>
#include <CoreVM/util/strings.hpp>

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>
// XXX Visual Studio doesn't support computed goto statements
#if defined(_MSC_VER)
    #define COREVM_VM_LOOP_SWITCH 1
#endif

#define COREVM_DEBUG(msg, ...) \
    do                         \
    {                          \
    } while (0)

namespace CoreVM
{

// {{{ VM helper preprocessor definitions
#define OP opcode((Instruction) * pc)
#define A  operandA((Instruction) * pc)
#define B  operandB((Instruction) * pc)
#define C  operandC((Instruction) * pc)

#define SP(i)          _stack[(i)]
#define popStringPtr() ((CoreString*) _stack.pop())
#define incr_pc() \
    do            \
    {             \
        ++pc;     \
    } while (0)
#define jump_to(offset) \
    do                  \
    {                   \
        set_pc(offset); \
        jump;           \
    } while (0)
#define tracelog()                                                 \
    do                                                             \
    {                                                              \
        _traceLogger((Instruction) * pc, get_pc(), _stack.size()); \
    } while (0)

#if defined(COREVM_VM_LOOP_SWITCH)
    #define LOOP_BEGIN() \
        for (;;)         \
        {                \
            switch (OP)  \
            {
    #define LOOP_END()                                        \
        default: COREVM_ASSERT(false, "Unknown Opcode hit!"); \
            }                                                 \
            }
    #define instr(NAME) \
        case NAME: tracelog();
    #define get_pc() (pc - code.data())
    #define set_pc(offset)               \
        do                               \
        {                                \
            pc = code.data() + (offset); \
        } while (0)
    #define next  \
        if (true) \
        {         \
            ++pc; \
            jump; \
        }
    #define jump         \
        if (true)        \
        {                \
            consume(OP); \
            break;       \
        }
#elif defined(COREVM_DIRECT_THREADED_VM)
    #define LOOP_BEGIN() jump;
    #define LOOP_END()
    #define instr(name) \
        l_##name: ++pc; \
        tracelog();
    #define get_pc() ((pc - code.data()) / 2)
    #define set_pc(offset)                   \
        do                                   \
        {                                    \
            pc = code.data() + (offset) * 2; \
        } while (0)
    #define next  \
        do        \
        {         \
            ++pc; \
            jump; \
        } while (0)
    #define jump              \
        do                    \
        {                     \
            consume(OP);      \
            goto*(void*) *pc; \
        } while (0)
#else
    #define LOOP_BEGIN() jump;
    #define LOOP_END()
    #define instr(name) l_##name: tracelog();
    #define get_pc()    (pc - code.data())
    #define set_pc(offset)               \
        do                               \
        {                                \
            pc = code.data() + (offset); \
        } while (0)
    #define next  \
        do        \
        {         \
            ++pc; \
            jump; \
        } while (0)
    #define jump           \
        do                 \
        {                  \
            consume(OP);   \
            goto* ops[OP]; \
        } while (0)
#endif
// }}}

// {{{
void Runner::Stack::rotate(size_t n)
{
    // moves stack[n] to stack[top], and shifts stack[n+1..] to stack[n..]
    Value tmp = _stack[n];
    while (n + 1 < _stack.size())
    {
        _stack[n] = _stack[n + 1];
        n++;
    }
    _stack[_stack.size() - 1] = tmp;
}

// }}}
static CoreString* t = nullptr;

Runner::Runner(
    const Handler* handler, void* userdata, Globals* globals, RuntimeConfig config, TraceLogger traceLogger):
    Runner { handler, userdata, globals, NoQuota, std::move(config), std::move(traceLogger) }
{
}

Runner::Runner(const Handler* handler,
               void* userdata,
               Globals* globals,
               Quota quota,
               RuntimeConfig config,
               TraceLogger traceLogger):
    _quota { quota },
    _config { std::move(config) },
    _handler(handler),
    _traceLogger { traceLogger ? std::move(traceLogger) : [](Instruction, size_t, size_t) {} },
    _program(handler->program()),
    _userdata(userdata),
    _regexpContext(),
    _state(Inactive),
    _ip(0),
    _stack(_handler->stackSize()),
    _globals { *globals },
    _stringGarbage()
{
    // initialize emptyString()
    t = newString("");
}

void Runner::consume(Opcode opcode)
{
    if (_quota == NoQuota)
        return;

    unsigned price = getPrice(opcode);
    if (price >= _quota)
    {
        _quota = 0;
        throw QuotaExceeded {};
    }

    _quota -= price;
}

CoreString* Runner::newString(std::string value)
{
    _stringGarbage.emplace_back(std::move(value));
    return &_stringGarbage.back();
}

CoreString* Runner::catString(const CoreString& a, const CoreString& b)
{
    _stringGarbage.emplace_back(a + b);
    return &_stringGarbage.back();
}

const TypeRegistry& Runner::typeRegistry() const
{
    return _program->constants().typeRegistry();
}

TypedObject* Runner::allocObject(uint16_t typeId)
{
    const TypeDescriptor* type = typeRegistry().get(typeId);
    if (!type)
    {
        // Invalid type ID - this is a programming error
        COREVM_ASSERT(false, "Invalid type ID in OALLOC");
        return nullptr;
    }

    // Allocate memory for the object
    size_t allocSize = TypedObject::allocationSize(type);
    auto storage = std::make_unique<uint8_t[]>(allocSize);

    // Initialize the object
    auto* obj = reinterpret_cast<TypedObject*>(storage.get());
    obj->type = type;
    obj->refCount.store(1, std::memory_order_relaxed);
    obj->tag = 0;

    // Zero-initialize slots
    for (uint16_t i = 0; i < type->slotCount; ++i)
    {
        obj->setSlot(i, 0);
    }

    // Track the allocation
    _objectPool.push_back(std::move(storage));
    ++_objectAllocCount;

    return obj;
}

void Runner::freeObject(TypedObject* obj)
{
    if (!obj)
        return;

    // Find and remove from the object pool
    // Note: This is O(n) but we expect few objects in practice.
    // For better performance, we could use a free list or arena allocator.
    for (auto it = _objectPool.begin(); it != _objectPool.end(); ++it)
    {
        if (reinterpret_cast<TypedObject*>(it->get()) == obj)
        {
            _objectPool.erase(it);
            return;
        }
    }
}

bool Runner::run()
{
    assert(_state == Inactive);
    auto result = runWithResult();
    if (!result)
    {
        // Print error to stderr and return non-zero exit
        std::println(stderr, "{}", result.error().format());
        return true;
    }
    return result.value();
}

Runner::RunResult Runner::runWithResult()
{
    assert(_state == Inactive);
    return loopWithResult();
}

RuntimeError Runner::makeError(std::string message) const
{
    return RuntimeError { std::move(message), _handler->locationOf(_ip) };
}

void Runner::suspend()
{
    assert(_state == Running);
    _state = Suspended;
}

bool Runner::resume()
{
    assert(_state == Suspended);
    auto result = loopWithResult();
    if (!result)
    {
        std::println(stderr, "{}", result.error().format());
        return true;
    }
    return result.value();
}

void Runner::rewind()
{
    _ip = 0;
}

Runner::RunResult Runner::loopWithResult()
{
// {{{ jump table
#if !defined(COREVM_VM_LOOP_SWITCH)
    #define label(opcode) &&l_##opcode
    static const void* const ops[] = {
        // misc
        label(NOP),
        label(ALLOCA),
        label(DISCARD),
        label(STACKROT),
        label(GALLOCA),
        label(GLOAD),
        label(GSTORE),

        // control
        label(EXIT),
        label(EXITPOP),
        label(JMP),
        label(JN),
        label(JZ),

        // array
        label(ITLOAD),
        label(STLOAD),
        label(PTLOAD),
        label(CTLOAD),

        // load'n'store
        label(LOAD),
        label(STORE),

        // numerical
        label(ILOAD),
        label(NLOAD),
        label(NNEG),
        label(NNOT),
        label(NADD),
        label(NSUB),
        label(NMUL),
        label(NDIV),
        label(NREM),
        label(NSHL),
        label(NSHR),
        label(NPOW),
        label(NAND),
        label(NOR),
        label(NXOR),
        label(NCMPZ),
        label(NCMPEQ),
        label(NCMPNE),
        label(NCMPLE),
        label(NCMPGE),
        label(NCMPLT),
        label(NCMPGT),

        // boolean op
        label(BNOT),
        label(BAND),
        label(BOR),
        label(BXOR),

        // string op
        label(SLOAD),
        label(SADD),
        label(SSUBSTR),
        label(SCMPEQ),
        label(SCMPNE),
        label(SCMPLE),
        label(SCMPGE),
        label(SCMPLT),
        label(SCMPGT),
        label(SCMPBEG),
        label(SCMPEND),
        label(SCONTAINS),
        label(SLEN),
        label(SISEMPTY),
        label(SMATCHEQ),
        label(SMATCHBEG),
        label(SMATCHEND),
        label(SMATCHR),

        // ipaddr
        label(PLOAD),
        label(PCMPEQ),
        label(PCMPNE),
        label(PINCIDR),

        // cidr
        label(CLOAD),

        // regex
        label(SREGMATCH),
        label(SREGGROUP),

        // conversion
        label(N2S),
        label(P2S),
        label(C2S),
        label(R2S),
        label(S2N),

        // invokation
        label(CALL),
        label(HANDLER),

        // object operations
        label(OALLOC),
        label(ORETAIN),
        label(ORELEASE),
        label(OGETTAG),
        label(OSETTAG),
        label(OGETSLOT),
        label(OSETSLOT),
        label(OTYPEID),
        label(OISTYPE),

        // dynamic value comparison
        label(VCMPEQ),
        label(VCMPNE),
        label(VCMPLT),
        label(VCMPLE),
        label(VCMPGT),
        label(VCMPGE),
    };
#endif
// }}}
// {{{ direct threaded code initialization
#if defined(COREVM_DIRECT_THREADED_VM)
    std::vector<uint64_t>& code = const_cast<Handler*>(_handler)->directThreadedCode();
    if (code.empty())
    {
        const std::vector<Instruction>& source = _handler->code();
        code.resize(source.size() * 2);

        uint64_t* pc = code.data();
        for (size_t i = 0, e = source.size(); i != e; ++i)
        {
            Instruction instr = source[i];

            *pc++ = (uint64_t) ops[opcode(instr)];
            *pc++ = instr;
        }
    }
#else
    std::vector<Instruction> const& code = _handler->code();
#endif
    // }}}

    _state = Running;
    decltype(code.data()) pc {};
    set_pc(_ip);

    LOOP_BEGIN()

    // {{{ misc
    instr(NOP)
    {
        next;
    }

    instr(ALLOCA)
    {
        for (int i = 0; i < A; ++i)
            _stack.push(0);
        next;
    }

    instr(DISCARD)
    {
        _stack.discard(A);
        next;
    }

    instr(STACKROT)
    {
        _stack.rotate(A);
        next;
    }

    instr(GALLOCA)
    {
        _globals.push_back(0);
        next;
    }

    instr(GLOAD)
    {
        push(_globals[A]);
        next;
    }

    instr(GSTORE)
    {
        _globals[A] = pop();
        next;
    }
    // }}}
    // {{{ control
    instr(EXIT)
    {
        _state = Inactive;
        _ip = get_pc();
        return A != 0;
    }

    instr(EXITPOP)
    {
        _state = Inactive;
        _ip = get_pc();
        CoreNumber exitCode = static_cast<CoreNumber>(pop());
        return exitCode != 0;
    }

    instr(JMP)
    {
        jump_to(A);
    }

    instr(JN)
    {
        if (pop() != 0)
        {
            jump_to(A);
        }
        else
        {
            next;
        }
    }

    instr(JZ)
    {
        if (pop() == 0)
        {
            jump_to(A);
        }
        else
        {
            next;
        }
    }
    // }}}
    // {{{ array
    instr(ITLOAD)
    {
        push(reinterpret_cast<Value>(&program()->constants().getIntArray(A)));
        next;
    }
    instr(STLOAD)
    {
        push(reinterpret_cast<Value>(&program()->constants().getStringArray(A)));
        next;
    }
    instr(PTLOAD)
    {
        push(reinterpret_cast<Value>(&program()->constants().getIPAddressArray(A)));
        next;
    }
    instr(CTLOAD)
    {
        push(reinterpret_cast<Value>(&program()->constants().getCidrArray(A)));
        next;
    }
    // }}}
    // {{{ load & store
    instr(LOAD)
    {
        push(_stack[A]);
        next;
    }

    instr(STORE)
    { // STORE imm
        _stack[A] = pop();
        next;
    }
    // }}}
    // {{{ numerical
    instr(ILOAD)
    {
        push(A);
        next;
    }

    instr(NLOAD)
    {
        push(program()->constants().getInteger(A));
        next;
    }

    instr(NNEG)
    {
        SP(-1) = -getNumber(-1);
        next;
    }

    instr(NNOT)
    {
        SP(-1) = ~getNumber(-1);
        next;
    }

    instr(NADD)
    {
        SP(-2) = getNumber(-2) + getNumber(-1);
        pop();
        next;
    }

    instr(NSUB)
    {
        SP(-2) = getNumber(-2) - getNumber(-1);
        pop();
        next;
    }

    instr(NMUL)
    {
        SP(-2) = getNumber(-2) * getNumber(-1);
        pop();
        next;
    }

    instr(NDIV)
    {
        CoreNumber divisor = getNumber(-1);
        if (_config.typeChecksEnabled && divisor == 0)
        {
            _ip = get_pc();
            return std::unexpected(makeError("division by zero"));
        }
        SP(-2) = getNumber(-2) / divisor;
        pop();
        next;
    }

    instr(NREM)
    {
        CoreNumber divisor = getNumber(-1);
        if (_config.typeChecksEnabled && divisor == 0)
        {
            _ip = get_pc();
            return std::unexpected(makeError("division by zero"));
        }
        SP(-2) = getNumber(-2) % divisor;
        pop();
        next;
    }

    instr(NSHL)
    {
        SP(-2) = getNumber(-2) << getNumber(-1);
        pop();
        next;
    }

    instr(NSHR)
    {
        SP(-2) = getNumber(-2) >> getNumber(-1);
        pop();
        next;
    }

    instr(NPOW)
    {
        SP(-2) = powl(getNumber(-2), getNumber(-1));
        pop();
        next;
    }

    instr(NAND)
    {
        SP(-2) = getNumber(-2) & getNumber(-1);
        pop();
        next;
    }

    instr(NOR)
    {
        SP(-2) = getNumber(-2) | getNumber(-1);
        pop();
        next;
    }

    instr(NXOR)
    {
        SP(-2) = getNumber(-2) ^ getNumber(-1);
        pop();
        next;
    }

    instr(NCMPZ)
    {
        SP(-1) = getNumber(-1) == 0;
        next;
    }

    instr(NCMPEQ)
    {
        SP(-2) = getNumber(-2) == getNumber(-1);
        pop();
        next;
    }

    instr(NCMPNE)
    {
        SP(-2) = getNumber(-2) != getNumber(-1);
        pop();
        next;
    }

    instr(NCMPLE)
    {
        SP(-2) = getNumber(-2) <= getNumber(-1);
        pop();
        next;
    }

    instr(NCMPGE)
    {
        SP(-2) = getNumber(-2) >= getNumber(-1);
        pop();
        next;
    }

    instr(NCMPLT)
    {
        SP(-2) = getNumber(-2) < getNumber(-1);
        pop();
        next;
    }

    instr(NCMPGT)
    {
        SP(-2) = getNumber(-2) > getNumber(-1);
        pop();
        next;
    }
    // }}}
    // {{{ boolean
    instr(BNOT)
    {
        SP(-1) = !getNumber(-1);
        next;
    }

    instr(BAND)
    {
        SP(-2) = getNumber(-2) && getNumber(-1);
        pop();
        next;
    }

    instr(BOR)
    {
        SP(-2) = getNumber(-2) || getNumber(-1);
        pop();
        next;
    }

    instr(BXOR)
    {
        SP(-2) = getNumber(-2) ^ getNumber(-1);
        pop();
        next;
    }
    // }}}
    // {{{ string
    instr(SLOAD)
    {
        push(reinterpret_cast<Value>(&program()->constants().getString(A)));
        next;
    }

    instr(SADD)
    {
        SP(-2) = (Value) catString(getString(-2), getString(-1));
        pop();
        next;
    }

    instr(SSUBSTR)
    {
        SP(-2) = (Value) newString(getString(-3).substr(getNumber(-2), getNumber(-1)));
        _stack.discard(2);
        next;
    }

    instr(SCMPEQ)
    {
        SP(-2) = getString(-2) == getString(-1);
        pop();
        next;
    }

    instr(SCMPNE)
    {
        SP(-2) = getString(-2) != getString(-1);
        pop();
        next;
    }

    instr(SCMPLE)
    {
        SP(-2) = getString(-2) <= getString(-1);
        pop();
        next;
    }

    instr(SCMPGE)
    {
        SP(-2) = getString(-2) >= getString(-1);
        pop();
        next;
    }

    instr(SCMPLT)
    {
        SP(-2) = getString(-2) < getString(-1);
        pop();
        next;
    }

    instr(SCMPGT)
    {
        SP(-2) = getString(-2) > getString(-1);
        pop();
        next;
    }

    instr(SCMPBEG)
    {
        SP(-2) = beginsWith(getString(-2), getString(-1));
        pop();
        next;
    }

    instr(SCMPEND)
    {
        SP(-2) = endsWith(getString(-2), getString(-1));
        pop();
        next;
    }

    instr(SCONTAINS)
    {
        SP(-2) = getString(-2).find(getString(-1)) != std::string::npos;
        pop();
        next;
    }

    instr(SLEN)
    {
        SP(-1) = getString(-1).size();
        next;
    }

    instr(SISEMPTY)
    {
        SP(-1) = getString(-1).empty();
        next;
    }

    instr(SMATCHEQ)
    {
        auto target = program()->match(A)->evaluate(popStringPtr(), this);
        jump_to(target);
    }

    instr(SMATCHBEG)
    {
        auto target = program()->match(A)->evaluate(popStringPtr(), this);
        jump_to(target);
    }

    instr(SMATCHEND)
    {
        auto target = program()->match(A)->evaluate(popStringPtr(), this);
        jump_to(target);
    }

    instr(SMATCHR)
    {
        auto target = program()->match(A)->evaluate(popStringPtr(), this);
        jump_to(target);
    }
    // }}}
    // {{{ ipaddr
    instr(PLOAD)
    {
        push(reinterpret_cast<Value>(&program()->constants().getIPAddress(A)));
        next;
    }

    instr(PCMPEQ)
    {
        SP(-2) = getIPAddress(-2) == getIPAddress(-2);
        pop();
        next;
    }

    instr(PCMPNE)
    {
        SP(-2) = getIPAddress(-2) != getIPAddress(-1);
        pop();
        next;
    }

    instr(PINCIDR)
    {
        const util::IPAddress& ipaddr = getIPAddress(-2);
        const util::Cidr& cidr = getCidr(-1);
        SP(-2) = cidr.contains(ipaddr);
        pop();
        next;
    }
    // }}}
    // {{{ cidr
    instr(CLOAD)
    {
        push(reinterpret_cast<Value>(&program()->constants().getCidr(A)));
        next;
    }
    // }}}
    // {{{ regex
    instr(SREGMATCH)
    { // A =~ B
        const util::RegExp& regex = program()->constants().getRegExp(A);
        const CoreString& data = getString(-1);
        const bool result = regex.match(data, _regexpContext.regexMatch());
        SP(-1) = result;
        next;
    }

    instr(SREGGROUP)
    {
        {
            CoreNumber position = A;
            util::RegExp::Result& rr = *_regexpContext.regexMatch();
            std::string match = rr[position];

            push((Value) newString(std::move(match)));
        }
        next;
    }
    // }}}
    // {{{ conversion
    instr(S2N)
    { // A = atoi(B)
        SP(-1) = std::stoi(getString(-1));
        next;
    }

    instr(N2S)
    { // A = itoa(B)
        CoreNumber value = getNumber(-1);
        char buf[64];
        if (snprintf(buf, sizeof(buf), "%" PRIi64 "", (int64_t) value) > 0)
        {
            SP(-1) = (Value) newString(buf);
        }
        else
        {
            SP(-1) = (Value) emptyString();
        }
        next;
    }

    instr(P2S)
    {
        const util::IPAddress& ipaddr = getIPAddress(-1);
        SP(-1) = (Value) newString(ipaddr.str());
        next;
    }

    instr(C2S)
    {
        const util::Cidr& cidr = getCidr(-1);
        SP(-1) = (Value) newString(cidr.str());
        next;
    }

    instr(R2S)
    {
        const util::RegExp& re = getRegExp(-1);
        SP(-1) = (Value) newString(re.pattern());
        next;
    }
    // }}}
    // {{{ invokation
    instr(CALL)
    {
        {
            size_t id = A;
            int argc = B;

            incr_pc();
            _ip = get_pc();

            Params args(this, argc);
            for (int i = 1; i <= argc; i++)
                args.setArg(i, SP(-(argc + 1) + i));

            const Signature& signature = _handler->program()->nativeFunction(id)->signature();

            _handler->program()->nativeFunction(id)->invoke(args);

            discard(argc);
            if (signature.returnType() != LiteralType::Void)
                push(args[0]);

            if (_state == Suspended)
            {
                COREVM_DEBUG("CoreVM: vm suspended in function. returning (false)");
                return false;
            }
        }
        set_pc(_ip);
        jump;
    }

    instr(HANDLER)
    {
        {
            size_t id = A;
            int argc = B;

            incr_pc();
            _ip = get_pc();

            Params args(this, argc);
            for (int i = 1; i <= argc; i++)
                args.setArg(i, SP(-(argc + 1) + i));

            _handler->program()->nativeHandler(id)->invoke(args);
            const bool handled = (bool) args[0];
            discard(argc);

            if (_state == Suspended)
            {
                COREVM_DEBUG("CoreVM: vm suspended in handler. returning (false)");
                return false;
            }

            if (handled)
            {
                _state = Inactive;
                return true;
            }
        }
        set_pc(_ip);
        jump;
    }
    // }}}
    // {{{ object operations
    instr(OALLOC)
    {
        // A = typeId
        if (_config.typeChecksEnabled && !typeRegistry().get(A))
        {
            _ip = get_pc();
            return std::unexpected(makeError(std::format("invalid type ID: {}", A)));
        }
        TypedObject* obj = allocObject(A);
        pushObject(obj);
        next;
    }

    instr(ORETAIN)
    {
        // Increment refcount of object at top of stack
        TypedObject* obj = getObject(-1);
        if (_config.typeChecksEnabled && !obj)
        {
            _ip = get_pc();
            return std::unexpected(makeError("null object dereference in ORETAIN"));
        }
        retainObject(obj);
        next;
    }

    instr(ORELEASE)
    {
        // Decrement refcount, free if zero, pop
        TypedObject* obj = getObject(-1);
        pop();
        if (_config.typeChecksEnabled && !obj)
        {
            _ip = get_pc();
            return std::unexpected(makeError("null object dereference in ORELEASE"));
        }
        if (releaseObject(obj))
        {
            freeObject(obj);
        }
        next;
    }

    instr(OGETTAG)
    {
        // Pop object, push its tag
        TypedObject* obj = getObject(-1);
        if (_config.typeChecksEnabled && !obj)
        {
            _ip = get_pc();
            return std::unexpected(makeError("null object dereference in OGETTAG"));
        }
        SP(-1) = obj->tag;
        next;
    }

    instr(OSETTAG)
    {
        // Pop tag, pop object, set tag, push object
        CoreNumber tag = getNumber(-1);
        pop();
        TypedObject* obj = getObject(-1);
        if (_config.typeChecksEnabled && !obj)
        {
            _ip = get_pc();
            return std::unexpected(makeError("null object dereference in OSETTAG"));
        }
        obj->tag = static_cast<uint8_t>(tag);
        // Object stays on stack
        next;
    }

    instr(OGETSLOT)
    {
        // A = slot index
        // Pop object, push slot[A]
        TypedObject* obj = getObject(-1);
        if (_config.typeChecksEnabled && !obj)
        {
            _ip = get_pc();
            return std::unexpected(makeError("null object dereference in OGETSLOT"));
        }
        if (_config.typeChecksEnabled && A >= obj->type->slotCount)
        {
            _ip = get_pc();
            return std::unexpected(makeError(
                std::format("slot index {} out of bounds (object has {} slots)", A, obj->type->slotCount)));
        }
        SP(-1) = obj->getSlot(static_cast<uint8_t>(A));
        next;
    }

    instr(OSETSLOT)
    {
        // A = slot index
        // Pop value, pop object, set slot[A] = value, push object
        Value value = pop();
        TypedObject* obj = getObject(-1);
        if (_config.typeChecksEnabled && !obj)
        {
            _ip = get_pc();
            return std::unexpected(makeError("null object dereference in OSETSLOT"));
        }
        if (_config.typeChecksEnabled && A >= obj->type->slotCount)
        {
            _ip = get_pc();
            return std::unexpected(makeError(
                std::format("slot index {} out of bounds (object has {} slots)", A, obj->type->slotCount)));
        }
        obj->setSlot(static_cast<uint8_t>(A), value);
        // Object stays on stack
        next;
    }

    instr(OTYPEID)
    {
        // Pop object, push type ID
        TypedObject* obj = getObject(-1);
        if (_config.typeChecksEnabled && !obj)
        {
            _ip = get_pc();
            return std::unexpected(makeError("null object dereference in OTYPEID"));
        }
        SP(-1) = obj->type->id;
        next;
    }

    instr(OISTYPE)
    {
        // A = typeId to check
        // Pop object, push boolean (obj.typeId == A)
        TypedObject* obj = getObject(-1);
        if (_config.typeChecksEnabled && !obj)
        {
            _ip = get_pc();
            return std::unexpected(makeError("null object dereference in OISTYPE"));
        }
        SP(-1) = (obj->type->id == A) ? 1 : 0;
        next;
    }

    instr(VCMPEQ)
    {
        // Dynamic value comparison - compare two values as numbers
        // Pop B, pop A, push (A == B)
        CoreNumber b = getNumber(-1);
        CoreNumber a = getNumber(-2);
        pop();
        SP(-1) = (a == b) ? 1 : 0;
        next;
    }

    instr(VCMPNE)
    {
        CoreNumber b = getNumber(-1);
        CoreNumber a = getNumber(-2);
        pop();
        SP(-1) = (a != b) ? 1 : 0;
        next;
    }

    instr(VCMPLT)
    {
        CoreNumber b = getNumber(-1);
        CoreNumber a = getNumber(-2);
        pop();
        SP(-1) = (a < b) ? 1 : 0;
        next;
    }

    instr(VCMPLE)
    {
        CoreNumber b = getNumber(-1);
        CoreNumber a = getNumber(-2);
        pop();
        SP(-1) = (a <= b) ? 1 : 0;
        next;
    }

    instr(VCMPGT)
    {
        CoreNumber b = getNumber(-1);
        CoreNumber a = getNumber(-2);
        pop();
        SP(-1) = (a > b) ? 1 : 0;
        next;
    }

    instr(VCMPGE)
    {
        CoreNumber b = getNumber(-1);
        CoreNumber a = getNumber(-2);
        pop();
        SP(-1) = (a >= b) ? 1 : 0;
        next;
    }
    // }}}

    LOOP_END()
}

} // namespace CoreVM
