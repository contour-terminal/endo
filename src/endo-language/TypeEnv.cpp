// SPDX-License-Identifier: Apache-2.0
#include "TypeEnv.hpp"

#include <algorithm>
#include <unordered_set>

namespace endo
{

TypeEnv::TypeEnv(): _parent(nullptr), _nextTypeVarId(0)
{
}

TypeEnv::TypeEnv(TypeEnvPtr parent): _parent(std::move(parent)), _nextTypeVarId(0)
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
    if (auto* tv = type->asTypeVar())
    {
        // Add if not already present
        if (std::find(vars.begin(), vars.end(), tv->id) == vars.end())
            vars.push_back(tv->id);
    }
    else if (auto* fn = type->asFunction())
    {
        collectFreeVars(fn->paramType, vars);
        collectFreeVars(fn->returnType, vars);
    }
    else if (auto* lst = type->asList())
    {
        collectFreeVars(lst->elementType, vars);
    }
    else if (auto* tup = type->asTuple())
    {
        for (auto const& elem: tup->elementTypes)
            collectFreeVars(elem, vars);
    }
    else if (auto* opt = type->asOption())
    {
        collectFreeVars(opt->innerType, vars);
    }
    else if (auto* res = type->asResult())
    {
        collectFreeVars(res->okType, vars);
        collectFreeVars(res->errorType, vars);
    }
    else if (auto* rec = type->asRecord())
    {
        for (auto const& field: rec->fields)
            collectFreeVars(field.type, vars);
    }
    else if (auto* un = type->asUnion())
    {
        for (auto const& c: un->cases)
        {
            if (c.payloadType)
                collectFreeVars(*c.payloadType, vars);
        }
    }
    // Primitives have no free type variables
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
            if (std::find(vars.begin(), vars.end(), id) == vars.end())
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
            if (std::find(result.begin(), result.end(), id) == result.end())
                result.push_back(id);
        }
    }
    return result;
}

TypeScheme TypeEnv::generalize(TypePtr type) const
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
    registerRecord("Error",
                   RecordType { "Error", { { "code", types::intType() }, { "message", types::strType() } } });
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

    return env;
}

std::shared_ptr<TypeRegistry> createStandardTypeRegistry()
{
    auto registry = std::make_shared<TypeRegistry>();
    registry->registerBuiltins();
    return registry;
}

} // namespace endo
