// SPDX-License-Identifier: Apache-2.0
#include <endo-language/types/TypeEnv.hpp>

#include <algorithm>
#include <unordered_set>

namespace endo
{

TypeEnv::TypeEnv(): _parent(nullptr)
{
}

TypeEnv::TypeEnv(TypeEnvPtr parent): _parent(std::move(parent))
{
    // Inherit the type variable counter from parent to ensure uniqueness
    if (_parent)
        _nextTypeVarId = _parent->_nextTypeVarId;
}

std::optional<TypeScheme> TypeEnv::lookup(std::string const& name) const
{
    auto it = _bindings.find(name);
    if (it != _bindings.end())
        return it->second;
    if (_parent)
        return _parent->lookup(name);
    return std::nullopt;
}

void TypeEnv::bind(std::string const& name, TypeScheme scheme)
{
    _bindings[name] = std::move(scheme);
}

void TypeEnv::bindMono(std::string const& name, TypePtr type)
{
    bind(name, types::monomorphic(std::move(type)));
}

bool TypeEnv::isDefinedLocally(std::string const& name) const
{
    return _bindings.find(name) != _bindings.end();
}

bool TypeEnv::isDefined(std::string const& name) const
{
    if (isDefinedLocally(name))
        return true;
    if (_parent)
        return _parent->isDefined(name);
    return false;
}

TypeVarId TypeEnv::freshTypeVar()
{
    // Update all parent environments to keep the counter in sync
    TypeVarId id = _nextTypeVarId++;
    if (_parent)
    {
        // Propagate up the chain
        TypeEnv* p = _parent.get();
        while (p)
        {
            if (p->_nextTypeVarId <= id)
                p->_nextTypeVarId = id + 1;
            p = p->_parent.get();
        }
    }
    return id;
}

TypePtr TypeEnv::freshTypeVarType()
{
    return types::typeVar(freshTypeVar());
}

void TypeEnv::collectFreeVars(TypePtr const& type, std::vector<TypeVarId>& vars)
{
    foldType<bool>(type, false, [&vars](bool /*unused*/, TypePtr const& t) {
        if (auto const* tv = t->asTypeVar())
        {
            if (std::ranges::find(vars, tv->id) == vars.end())
                vars.push_back(tv->id);
        }
        return false;
    });
}

void TypeEnv::collectFreeVars(TypeScheme const& scheme, std::vector<TypeVarId>& vars)
{
    std::vector<TypeVarId> typeVars;
    collectFreeVars(scheme.type, typeVars);

    // Filter out quantified variables
    std::unordered_set<TypeVarId> quantified(scheme.quantifiedVars.begin(), scheme.quantifiedVars.end());
    for (auto id: typeVars)
    {
        if (quantified.find(id) == quantified.end())
        {
            if (std::ranges::find(vars, id) == vars.end())
                vars.push_back(id);
        }
    }
}

std::vector<TypeVarId> TypeEnv::freeTypeVars() const
{
    std::vector<TypeVarId> result;
    for (auto const& [name, scheme]: _bindings)
    {
        collectFreeVars(scheme, result);
    }
    if (_parent)
    {
        auto parentVars = _parent->freeTypeVars();
        for (auto id: parentVars)
        {
            if (std::ranges::find(result, id) == result.end())
                result.push_back(id);
        }
    }
    return result;
}

TypeScheme TypeEnv::generalize(TypePtr const& type) const
{
    // Get free type variables in the type
    std::vector<TypeVarId> typeVars;
    collectFreeVars(type, typeVars);

    // Get free type variables in the environment
    std::vector<TypeVarId> envVars = freeTypeVars();
    std::unordered_set<TypeVarId> envVarSet(envVars.begin(), envVars.end());

    // Quantify type variables that are free in the type but not in the environment
    std::vector<TypeVarId> quantified;
    for (auto id: typeVars)
    {
        if (envVarSet.find(id) == envVarSet.end())
            quantified.push_back(id);
    }

    return types::scheme(std::move(quantified), type);
}

TypePtr TypeEnv::instantiate(TypeScheme const& scheme)
{
    return scheme.instantiate([this]() { return freshTypeVar(); });
}

std::unordered_map<std::string, TypeScheme> const& TypeEnv::localBindings() const
{
    return _bindings;
}

TypeEnvPtr TypeEnv::childScope() const
{
    return std::make_shared<TypeEnv>(std::const_pointer_cast<TypeEnv>(shared_from_this()));
}

// TypeRegistry implementation

TypeRegistry::TypeRegistry() = default;

void TypeRegistry::registerRecord(std::string const& name, RecordType record)
{
    _records[name] = std::move(record);
}

void TypeRegistry::registerUnion(std::string const& name, UnionType unionDef)
{
    _unions[name] = std::move(unionDef);
}

std::optional<RecordType> TypeRegistry::lookupRecord(std::string const& name) const
{
    auto it = _records.find(name);
    if (it != _records.end())
        return it->second;
    return std::nullopt;
}

std::optional<UnionType> TypeRegistry::lookupUnion(std::string const& name) const
{
    auto it = _unions.find(name);
    if (it != _unions.end())
        return it->second;
    return std::nullopt;
}

std::optional<TypePtr> TypeRegistry::lookupType(std::string const& name) const
{
    if (auto rec = lookupRecord(name))
        return types::record(rec->name, rec->fields);
    if (auto un = lookupUnion(name))
        return types::unionType(un->name, un->cases);
    return std::nullopt;
}

bool TypeRegistry::isRegistered(std::string const& name) const
{
    return _records.find(name) != _records.end() || _unions.find(name) != _unions.end();
}

void TypeRegistry::registerBuiltins()
{
    // Register the built-in Error type
    registerRecord(
        "Error",
        RecordType { .name = "Error",
                     .fields = { { "code", types::intType() }, { "message", types::strType() } } });
}

// Standard environment creation

TypeEnvPtr createStandardTypeEnv()
{
    auto env = std::make_shared<TypeEnv>();

    // Arithmetic operators (int -> int -> int)
    auto intBinOp = types::function({ types::intType(), types::intType() }, types::intType());
    env->bindMono("+", intBinOp);
    env->bindMono("-", intBinOp);
    env->bindMono("*", intBinOp);
    env->bindMono("/", intBinOp);
    env->bindMono("%", intBinOp);

    // Float arithmetic (float -> float -> float)
    auto floatBinOp = types::function({ types::floatType(), types::floatType() }, types::floatType());
    env->bindMono("+.", floatBinOp);
    env->bindMono("-.", floatBinOp);
    env->bindMono("*.", floatBinOp);
    env->bindMono("/.", floatBinOp);

    // Comparison operators - polymorphic: forall a. a -> a -> bool
    auto a = env->freshTypeVar();
    auto cmpType = types::function({ types::typeVar(a), types::typeVar(a) }, types::boolType());
    env->bind("==", types::scheme({ a }, cmpType));
    env->bind("!=", types::scheme({ a }, cmpType));

    // Numeric comparisons (int -> int -> bool)
    auto intCmp = types::function({ types::intType(), types::intType() }, types::boolType());
    env->bindMono("<", intCmp);
    env->bindMono("<=", intCmp);
    env->bindMono(">", intCmp);
    env->bindMono(">=", intCmp);

    // Logical operators (bool -> bool -> bool)
    auto boolBinOp = types::function({ types::boolType(), types::boolType() }, types::boolType());
    env->bindMono("&&", boolBinOp);
    env->bindMono("||", boolBinOp);

    // Unary not (bool -> bool)
    env->bindMono("!", types::function(types::boolType(), types::boolType()));

    // String concatenation (str -> str -> str)
    env->bindMono("++", types::function({ types::strType(), types::strType() }, types::strType()));

    // List operations - all polymorphic

    // head: forall a. list<a> -> option<a>
    auto b = env->freshTypeVar();
    env->bind("head",
              types::scheme(
                  { b }, types::function(types::list(types::typeVar(b)), types::option(types::typeVar(b)))));

    // tail: forall a. list<a> -> list<a>
    auto c = env->freshTypeVar();
    env->bind("tail",
              types::scheme({ c },
                            types::function(types::list(types::typeVar(c)), types::list(types::typeVar(c)))));

    // length: forall a. list<a> -> int
    auto d = env->freshTypeVar();
    env->bind("length",
              types::scheme({ d }, types::function(types::list(types::typeVar(d)), types::intType())));

    // isEmpty: forall a. list<a> -> bool
    auto e = env->freshTypeVar();
    env->bind("isEmpty",
              types::scheme({ e }, types::function(types::list(types::typeVar(e)), types::boolType())));

    // map: forall a b. (a -> b) -> list<a> -> list<b>
    auto f = env->freshTypeVar();
    auto g = env->freshTypeVar();
    env->bind("map",
              types::scheme({ f, g },
                            types::function({ types::function(types::typeVar(f), types::typeVar(g)),
                                              types::list(types::typeVar(f)) },
                                            types::list(types::typeVar(g)))));

    // filter: forall a. (a -> bool) -> list<a> -> list<a>
    auto h = env->freshTypeVar();
    env->bind("filter",
              types::scheme({ h },
                            types::function({ types::function(types::typeVar(h), types::boolType()),
                                              types::list(types::typeVar(h)) },
                                            types::list(types::typeVar(h)))));

    // fold: forall a b. b -> (b -> a -> b) -> list<a> -> b
    auto i = env->freshTypeVar();
    auto j = env->freshTypeVar();
    env->bind("fold",
              types::scheme({ i, j },
                            types::function({ types::typeVar(j),
                                              types::function({ types::typeVar(j), types::typeVar(i) },
                                                              types::typeVar(j)),
                                              types::list(types::typeVar(i)) },
                                            types::typeVar(j))));

    // Option constructors
    // Some: forall a. a -> option<a>
    auto k = env->freshTypeVar();
    env->bind("Some",
              types::scheme({ k }, types::function(types::typeVar(k), types::option(types::typeVar(k)))));

    // None: forall a. option<a>
    auto l = env->freshTypeVar();
    env->bind("None", types::scheme({ l }, types::option(types::typeVar(l))));

    // isNone: forall a. option<a> -> bool
    auto m = env->freshTypeVar();
    env->bind("isNone",
              types::scheme({ m }, types::function(types::option(types::typeVar(m)), types::boolType())));

    // isSome: forall a. option<a> -> bool
    auto n = env->freshTypeVar();
    env->bind("isSome",
              types::scheme({ n }, types::function(types::option(types::typeVar(n)), types::boolType())));

    // Result constructors
    // Ok: forall a e. a -> result<a, e>
    auto o = env->freshTypeVar();
    auto p = env->freshTypeVar();
    env->bind("Ok",
              types::scheme(
                  { o, p },
                  types::function(types::typeVar(o), types::result(types::typeVar(o), types::typeVar(p)))));

    // Error: forall a e. e -> result<a, e>
    auto q = env->freshTypeVar();
    auto r = env->freshTypeVar();
    env->bind("Error",
              types::scheme(
                  { q, r },
                  types::function(types::typeVar(r), types::result(types::typeVar(q), types::typeVar(r)))));

    // isOk: forall a e. result<a, e> -> bool
    auto s = env->freshTypeVar();
    auto t = env->freshTypeVar();
    env->bind("isOk",
              types::scheme(
                  { s, t },
                  types::function(types::result(types::typeVar(s), types::typeVar(t)), types::boolType())));

    // isError: forall a e. result<a, e> -> bool
    auto u = env->freshTypeVar();
    auto v = env->freshTypeVar();
    env->bind("isError",
              types::scheme(
                  { u, v },
                  types::function(types::result(types::typeVar(u), types::typeVar(v)), types::boolType())));

    // print: forall a. a -> unit
    auto w = env->freshTypeVar();
    env->bind("print", types::scheme({ w }, types::function(types::typeVar(w), types::unitType())));

    // println: forall a. a -> unit
    auto x = env->freshTypeVar();
    env->bind("println", types::scheme({ x }, types::function(types::typeVar(x), types::unitType())));

    // string_length: str -> int
    env->bindMono("string_length", types::function(types::strType(), types::intType()));

    // int_of_string: str -> int
    env->bindMono("int_of_string", types::function(types::strType(), types::intType()));

    // string_of_int: int -> str
    env->bindMono("string_of_int", types::function(types::intType(), types::strType()));

    // not: bool -> bool
    env->bindMono("not", types::function(types::boolType(), types::boolType()));

    // rand: unit -> int (no-arg form; 2-arg form handled as overload in IRGenerator)
    env->bindMono("rand", types::function(types::unitType(), types::intType()));

    // Cons operator (::): forall a. a -> list<a> -> list<a>
    auto y = env->freshTypeVar();
    env->bind("::",
              types::scheme({ y },
                            types::function({ types::typeVar(y), types::list(types::typeVar(y)) },
                                            types::list(types::typeVar(y)))));

    // List concat (@): forall a. list<a> -> list<a> -> list<a>
    auto z = env->freshTypeVar();
    env->bind(
        "@",
        types::scheme({ z },
                      types::function({ types::list(types::typeVar(z)), types::list(types::typeVar(z)) },
                                      types::list(types::typeVar(z)))));

    // Negation (-): int -> int (unary, for consistency)
    env->bindMono("~-", types::function(types::intType(), types::intType()));

    return env;
}

std::shared_ptr<TypeRegistry> createStandardTypeRegistry()
{
    auto registry = std::make_shared<TypeRegistry>();
    registry->registerBuiltins();
    return registry;
}

} // namespace endo
