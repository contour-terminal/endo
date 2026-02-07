// SPDX-License-Identifier: Apache-2.0
#include "Pattern.hpp"

#include <format>
#include <sstream>

#include "AST.hpp"
#include "ASTPrinter.hpp"

namespace endo::pattern
{

// ============================================================================
// GuardedPattern implementation (constructor/destructor out-of-line for incomplete type)
// ============================================================================

GuardedPattern::GuardedPattern(PatternPtr p, std::unique_ptr<ast::Expr> g):
    pattern(std::move(p)), guard(std::move(g))
{
}

GuardedPattern::~GuardedPattern() = default;

// ============================================================================
// Pattern accept() implementations
// ============================================================================

void LiteralPattern::accept(PatternVisitor& visitor) const
{
    visitor.visit(*this);
}

void VariablePattern::accept(PatternVisitor& visitor) const
{
    visitor.visit(*this);
}

void WildcardPattern::accept(PatternVisitor& visitor) const
{
    visitor.visit(*this);
}

void TuplePattern::accept(PatternVisitor& visitor) const
{
    visitor.visit(*this);
}

void ListPattern::accept(PatternVisitor& visitor) const
{
    visitor.visit(*this);
}

void ConsPattern::accept(PatternVisitor& visitor) const
{
    visitor.visit(*this);
}

void RecordPattern::accept(PatternVisitor& visitor) const
{
    visitor.visit(*this);
}

void ConstructorPattern::accept(PatternVisitor& visitor) const
{
    visitor.visit(*this);
}

void AsPattern::accept(PatternVisitor& visitor) const
{
    visitor.visit(*this);
}

void OrPattern::accept(PatternVisitor& visitor) const
{
    visitor.visit(*this);
}

void GuardedPattern::accept(PatternVisitor& visitor) const
{
    visitor.visit(*this);
}

// ============================================================================
// Pattern clone() implementations
// ============================================================================

std::unique_ptr<Pattern> LiteralPattern::clone() const
{
    auto result = std::make_unique<LiteralPattern>(value);
    result->location = location;
    return result;
}

std::unique_ptr<Pattern> VariablePattern::clone() const
{
    auto result = std::make_unique<VariablePattern>(name, isMutable);
    result->location = location;
    return result;
}

std::unique_ptr<Pattern> WildcardPattern::clone() const
{
    auto result = std::make_unique<WildcardPattern>();
    result->location = location;
    return result;
}

std::unique_ptr<Pattern> TuplePattern::clone() const
{
    std::vector<PatternPtr> clonedElements;
    clonedElements.reserve(elements.size());
    for (auto const& elem: elements)
        clonedElements.push_back(elem->clone());
    auto result = std::make_unique<TuplePattern>(std::move(clonedElements));
    result->location = location;
    return result;
}

std::unique_ptr<Pattern> ListPattern::clone() const
{
    std::vector<PatternPtr> clonedElements;
    clonedElements.reserve(elements.size());
    for (auto const& elem: elements)
        clonedElements.push_back(elem->clone());
    auto result = std::make_unique<ListPattern>(std::move(clonedElements), restBinding);
    result->location = location;
    return result;
}

std::unique_ptr<Pattern> ConsPattern::clone() const
{
    auto result = std::make_unique<ConsPattern>(head->clone(), tail->clone());
    result->location = location;
    return result;
}

FieldPattern FieldPattern::clone() const
{
    return FieldPattern(name, pattern ? pattern->clone() : nullptr);
}

std::unique_ptr<Pattern> RecordPattern::clone() const
{
    std::vector<FieldPattern> clonedFields;
    clonedFields.reserve(fields.size());
    for (auto const& field: fields)
        clonedFields.push_back(field.clone());
    auto result = std::make_unique<RecordPattern>(std::move(clonedFields), hasWildcard);
    result->location = location;
    return result;
}

std::unique_ptr<Pattern> ConstructorPattern::clone() const
{
    std::optional<PatternPtr> clonedPayload;
    if (payload)
        clonedPayload = (*payload)->clone();
    auto result = std::make_unique<ConstructorPattern>(name, std::move(clonedPayload));
    result->location = location;
    return result;
}

std::unique_ptr<Pattern> AsPattern::clone() const
{
    auto result = std::make_unique<AsPattern>(inner->clone(), name);
    result->location = location;
    return result;
}

std::unique_ptr<Pattern> OrPattern::clone() const
{
    std::vector<PatternPtr> clonedAlts;
    clonedAlts.reserve(alternatives.size());
    for (auto const& alt: alternatives)
        clonedAlts.push_back(alt->clone());
    auto result = std::make_unique<OrPattern>(std::move(clonedAlts));
    result->location = location;
    return result;
}

std::unique_ptr<Pattern> GuardedPattern::clone() const
{
    // Note: Guard expression is not cloned (would need Expr::clone() method)
    // For pattern compilation, we don't need to clone guards as patterns are traversed once.
    auto result = std::make_unique<GuardedPattern>(pattern->clone(), nullptr);
    result->location = location;
    return result;
}

// ============================================================================
// Utility Functions
// ============================================================================

namespace
{

    class BindingCollector final: public PatternVisitor
    {
      public:
        std::vector<std::string> bindings;

        void visit(LiteralPattern const&) override
        {
            // No bindings from literals
        }

        void visit(VariablePattern const& p) override { bindings.push_back(p.name); }

        void visit(WildcardPattern const&) override
        {
            // No bindings from wildcards
        }

        void visit(TuplePattern const& p) override
        {
            for (auto const& elem: p.elements)
                elem->accept(*this);
        }

        void visit(ListPattern const& p) override
        {
            for (auto const& elem: p.elements)
                elem->accept(*this);
            if (p.restBinding)
                bindings.push_back(*p.restBinding);
        }

        void visit(ConsPattern const& p) override
        {
            p.head->accept(*this);
            p.tail->accept(*this);
        }

        void visit(RecordPattern const& p) override
        {
            for (auto const& field: p.fields)
            {
                if (field.pattern)
                    field.pattern->accept(*this);
                else
                    // Field punning: { name } binds `name`
                    bindings.push_back(field.name);
            }
        }

        void visit(ConstructorPattern const& p) override
        {
            if (p.payload)
                (*p.payload)->accept(*this);
        }

        void visit(AsPattern const& p) override
        {
            bindings.push_back(p.name);
            p.inner->accept(*this);
        }

        void visit(OrPattern const& p) override
        {
            // All alternatives must bind the same variables
            // Just collect from the first alternative
            if (!p.alternatives.empty())
                p.alternatives[0]->accept(*this);
        }

        void visit(GuardedPattern const& p) override { p.pattern->accept(*this); }
    };

    class IrrefutabilityChecker final: public PatternVisitor
    {
      public:
        bool irrefutable = true;

        void visit(LiteralPattern const&) override
        {
            irrefutable = false; // Literals are refutable
        }

        void visit(VariablePattern const&) override
        {
            // Variables always match
        }

        void visit(WildcardPattern const&) override
        {
            // Wildcards always match
        }

        void visit(TuplePattern const& p) override
        {
            for (auto const& elem: p.elements)
            {
                elem->accept(*this);
                if (!irrefutable)
                    return;
            }
        }

        void visit(ListPattern const& p) override
        {
            // List patterns are generally refutable (need exact length match)
            // Only [] matching empty list is irrefutable
            if (!p.elements.empty() || p.restBinding)
                irrefutable = false;
        }

        void visit(ConsPattern const&) override
        {
            // Cons pattern is refutable (list could be empty)
            irrefutable = false;
        }

        void visit(RecordPattern const& p) override
        {
            // Record patterns with wildcard are irrefutable if all specified fields are
            for (auto const& field: p.fields)
            {
                if (field.pattern)
                {
                    field.pattern->accept(*this);
                    if (!irrefutable)
                        return;
                }
            }
        }

        void visit(ConstructorPattern const&) override
        {
            // Constructor patterns are generally refutable (could be different variant)
            // Exception: single-variant types, but we don't have that info here
            irrefutable = false;
        }

        void visit(AsPattern const& p) override { p.inner->accept(*this); }

        void visit(OrPattern const&) override
        {
            // Or patterns are refutable unless they cover all cases
            irrefutable = false;
        }

        void visit(GuardedPattern const&) override
        {
            // Guarded patterns are always refutable (guard might fail)
            irrefutable = false;
        }
    };

    class PatternPrinter final: public PatternVisitor
    {
      public:
        std::ostringstream output;

        void visit(LiteralPattern const& p) override
        {
            std::visit(
                [this](auto const& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, std::string>)
                        output << '"' << v << '"';
                    else if constexpr (std::is_same_v<T, bool>)
                        output << (v ? "true" : "false");
                    else
                        output << v;
                },
                p.value);
        }

        void visit(VariablePattern const& p) override
        {
            if (p.isMutable)
                output << "mut ";
            output << p.name;
        }

        void visit(WildcardPattern const&) override { output << "_"; }

        void visit(TuplePattern const& p) override
        {
            output << "(";
            for (size_t i = 0; i < p.elements.size(); ++i)
            {
                if (i > 0)
                    output << ", ";
                p.elements[i]->accept(*this);
            }
            output << ")";
        }

        void visit(ListPattern const& p) override
        {
            output << "[";
            for (size_t i = 0; i < p.elements.size(); ++i)
            {
                if (i > 0)
                    output << "; ";
                p.elements[i]->accept(*this);
            }
            if (p.restBinding)
            {
                if (!p.elements.empty())
                    output << "; ";
                output << *p.restBinding << "...";
            }
            output << "]";
        }

        void visit(ConsPattern const& p) override
        {
            p.head->accept(*this);
            output << " :: ";
            p.tail->accept(*this);
        }

        void visit(RecordPattern const& p) override
        {
            output << "{ ";
            for (size_t i = 0; i < p.fields.size(); ++i)
            {
                if (i > 0)
                    output << "; ";
                output << p.fields[i].name;
                if (p.fields[i].pattern)
                {
                    output << " = ";
                    p.fields[i].pattern->accept(*this);
                }
            }
            if (p.hasWildcard)
            {
                if (!p.fields.empty())
                    output << "; ";
                output << "_";
            }
            output << " }";
        }

        void visit(ConstructorPattern const& p) override
        {
            output << p.name;
            if (p.payload)
            {
                output << " ";
                (*p.payload)->accept(*this);
            }
        }

        void visit(AsPattern const& p) override
        {
            p.inner->accept(*this);
            output << " as " << p.name;
        }

        void visit(OrPattern const& p) override
        {
            for (size_t i = 0; i < p.alternatives.size(); ++i)
            {
                if (i > 0)
                    output << " | ";
                p.alternatives[i]->accept(*this);
            }
        }

        void visit(GuardedPattern const& p) override
        {
            p.pattern->accept(*this);
            if (p.guard)
            {
                output << " when " << ast::ASTPrinter::print(*p.guard);
            }
        }
    };

} // namespace

std::vector<std::string> collectBindings(Pattern const& pattern)
{
    BindingCollector collector;
    pattern.accept(collector);
    return collector.bindings;
}

bool isIrrefutable(Pattern const& pattern)
{
    IrrefutabilityChecker checker;
    pattern.accept(checker);
    return checker.irrefutable;
}

std::string toString(Pattern const& pattern)
{
    PatternPrinter printer;
    pattern.accept(printer);
    return printer.output.str();
}

namespace patterns
{

    PatternPtr guarded(PatternPtr pattern, std::unique_ptr<ast::Expr> guard)
    {
        return std::make_unique<GuardedPattern>(std::move(pattern), std::move(guard));
    }

} // namespace patterns

} // namespace endo::pattern
