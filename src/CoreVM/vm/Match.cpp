// SPDX-License-Identifier: Apache-2.0
module;
#include <vector>
#include <cstdint>

module CoreVM;
namespace CoreVM
{

// {{{ Match
Match::Match(MatchDef def): _def(std::move(def))
{
}
// }}} Match
// {{{ MatchSame
MatchSame::MatchSame(const MatchDef& def, Program* program): Match(def)
{
    for (auto one: def.cases)
    {
        _map[program->constants().getString(one.label)] = one.pc;
    }
}

uint64_t MatchSame::evaluate(const CoreString* condition, Runner* /*env*/) const
{
    const auto i = _map.find(*condition);
    if (i != _map.end())
        return i->second;

    return _def.elsePC; // no match found
}
// }}}
// {{{ MatchHead
MatchHead::MatchHead(const MatchDef& def, Program* program): Match(def)
{
    for (auto one: def.cases)
    {
        _map.insert(program->constants().getString(one.label), one.pc);
    }
}

uint64_t MatchHead::evaluate(const CoreString* condition, Runner* /*env*/) const
{
    uint64_t result {};
    if (_map.lookup(*condition, &result))
        return result;

    return _def.elsePC; // no match found
}
// }}}
// {{{ MatchTail
MatchTail::MatchTail(const MatchDef& def, Program* program): Match(def)
{
    for (auto& one: def.cases)
    {
        _map.insert(program->constants().getString(one.label), one.pc);
    }
}

uint64_t MatchTail::evaluate(const CoreString* condition, Runner* /*env*/) const
{
    uint64_t result {};
    if (_map.lookup(*condition, &result))
        return result;

    return _def.elsePC; // no match found
}
// }}}
// {{{ MatchRegEx
MatchRegEx::MatchRegEx(const MatchDef& def, Program* program): Match(def)
{
    for (auto& one: def.cases)
    {
        _map.emplace_back(program->constants().getRegExp(one.label), one.pc);
    }
}

uint64_t MatchRegEx::evaluate(const CoreString* condition, Runner* env) const
{
    auto* cx = reinterpret_cast<util::RegExpContext*>(env->userdata());
    util::RegExp::Result* rs = cx ? cx->regexMatch() : nullptr;
    for (const std::pair<util::RegExp, uint64_t>& one: _map)
    {
        if (one.first.match(*condition, rs))
        {
            return one.second;
        }
    }

    return _def.elsePC; // no match found
}
// }}}

} // namespace CoreVM
