// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <CoreVM/LiteralType.h>
#include <CoreVM/MatchClass.h>
#include <CoreVM/util/PrefixTree.h>
#include <CoreVM/util/RegExp.h>
#include <CoreVM/util/SuffixTree.h>
#include <CoreVM/vm/Instruction.h>

#include <sys/types.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace CoreVM
{

struct MatchCaseDef
{
    //!< offset into the string pool (or regexp pool) of the associated program.
    uint64_t label;
    //!< program offset into the associated handler
    uint64_t pc;

    MatchCaseDef() = default;
    explicit MatchCaseDef(uint64_t l): label(l), pc(0) {}
    MatchCaseDef(uint64_t l, uint64_t p): label(l), pc(p) {}
};

class MatchDef
{
  public:
    size_t handlerId;
    MatchClass op; // == =^ =$ =~
    uint64_t elsePC;
    std::vector<MatchCaseDef> cases;
};

class Program;
class Handler;
class Runner;

class Match
{
  public:
    explicit Match(MatchDef def);
    virtual ~Match() = default;

    const MatchDef& def() const { return _def; }

    /**
     * Matches input condition.
     * \return a code pointer to continue processing
     */
    virtual uint64_t evaluate(const CoreString* condition, Runner* env) const = 0;

  protected:
    MatchDef _def;
};

/** Implements SMATCHEQ instruction. */
class MatchSame: public Match
{
  public:
    MatchSame(const MatchDef& def, Program* program);

    uint64_t evaluate(const CoreString* condition, Runner* env) const override;

  private:
    std::unordered_map<CoreString, uint64_t> _map;
};

/** Implements SMATCHBEG instruction. */
class MatchHead: public Match
{
  public:
    MatchHead(const MatchDef& def, Program* program);

    uint64_t evaluate(const CoreString* condition, Runner* env) const override;

  private:
    util::PrefixTree<CoreString, uint64_t> _map;
};

/** Implements SMATCHBEG instruction. */
class MatchTail: public Match
{
  public:
    MatchTail(const MatchDef& def, Program* program);

    uint64_t evaluate(const CoreString* condition, Runner* env) const override;

  private:
    util::SuffixTree<CoreString, uint64_t> _map;
};

/** Implements SMATCHR instruction. */
class MatchRegEx: public Match
{
  public:
    MatchRegEx(const MatchDef& def, Program* program);

    uint64_t evaluate(const CoreString* condition, Runner* env) const override;

  private:
    std::vector<std::pair<util::RegExp, uint64_t>> _map;
};

} // namespace CoreVM
