// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "Lexer.hpp"
#include "Type.hpp"

namespace endo::pattern
{

struct PatternVisitor;

// ============================================================================
// Pattern Base Class
// ============================================================================

/// Base class for all pattern AST nodes.
///
/// Patterns are used in:
/// - `let` bindings: `let (x, y) = point`
/// - `match` expressions: `match x with | Some n -> n | None -> 0`
/// - Function parameters: `let add (x, y) = x + y`
struct Pattern
{
    std::optional<SourceLocationRange> location; ///< Source location of this pattern

    virtual ~Pattern() = default;
    virtual void accept(PatternVisitor&) const = 0;

    /// Clone this pattern (deep copy)
    [[nodiscard]] virtual std::unique_ptr<Pattern> clone() const = 0;
};

using PatternPtr = std::unique_ptr<Pattern>;

// ============================================================================
// Literal Patterns
// ============================================================================

/// Literal value that a pattern can match against.
using LiteralValue = std::variant<int64_t, double, bool, std::string>;

/// Matches a specific literal value.
///
/// Examples:
/// - `0` matches the integer zero
/// - `"start"` matches the string "start"
/// - `true` matches the boolean true
struct LiteralPattern final: Pattern
{
    LiteralValue value;

    explicit LiteralPattern(LiteralValue v): value(std::move(v)) {}

    void accept(PatternVisitor& visitor) const override;
    [[nodiscard]] std::unique_ptr<Pattern> clone() const override;
};

// ============================================================================
// Variable and Wildcard Patterns
// ============================================================================

/// Binds the matched value to a variable name.
///
/// Examples:
/// - `x` binds the matched value to variable `x`
/// - `name` in `{ name; age }` binds the `name` field
struct VariablePattern final: Pattern
{
    std::string name;
    bool isMutable = false; ///< True for `mut x` pattern

    explicit VariablePattern(std::string n, bool mut = false): name(std::move(n)), isMutable(mut) {}

    void accept(PatternVisitor& visitor) const override;
    [[nodiscard]] std::unique_ptr<Pattern> clone() const override;
};

/// Matches anything but discards the value.
///
/// Examples:
/// - `_` matches any value
/// - `{ name; _ }` matches a record with `name`, ignoring other fields
struct WildcardPattern final: Pattern
{
    void accept(PatternVisitor& visitor) const override;
    [[nodiscard]] std::unique_ptr<Pattern> clone() const override;
};

// ============================================================================
// Compound Patterns
// ============================================================================

/// Matches a tuple by destructuring its elements.
///
/// Examples:
/// - `(x, y)` matches a 2-tuple
/// - `(0, 0)` matches the origin point
/// - `(x, _, z)` matches a 3-tuple, ignoring the middle element
struct TuplePattern final: Pattern
{
    std::vector<PatternPtr> elements;

    explicit TuplePattern(std::vector<PatternPtr> elems): elements(std::move(elems)) {}

    void accept(PatternVisitor& visitor) const override;
    [[nodiscard]] std::unique_ptr<Pattern> clone() const override;
};

/// Matches a list by its elements.
///
/// Examples:
/// - `[]` matches an empty list
/// - `[x]` matches a single-element list
/// - `[x; y; z]` matches a 3-element list
/// - `[first; rest...]` matches a list with at least one element
struct ListPattern final: Pattern
{
    std::vector<PatternPtr> elements;       ///< Fixed elements to match
    std::optional<std::string> restBinding; ///< Variable name for remaining elements (e.g., `rest...`)

    ListPattern(std::vector<PatternPtr> elems, std::optional<std::string> rest = std::nullopt):
        elements(std::move(elems)), restBinding(std::move(rest))
    {
    }

    void accept(PatternVisitor& visitor) const override;
    [[nodiscard]] std::unique_ptr<Pattern> clone() const override;
};

/// Matches a list using the cons operator (head :: tail).
///
/// Examples:
/// - `head :: tail` matches a non-empty list
/// - `x :: y :: rest` matches a list with at least 2 elements
struct ConsPattern final: Pattern
{
    PatternPtr head;
    PatternPtr tail;

    ConsPattern(PatternPtr h, PatternPtr t): head(std::move(h)), tail(std::move(t)) {}

    void accept(PatternVisitor& visitor) const override;
    [[nodiscard]] std::unique_ptr<Pattern> clone() const override;
};

// ============================================================================
// Record Patterns
// ============================================================================

/// A single field in a record pattern.
struct FieldPattern
{
    std::string name; ///< Field name
    PatternPtr
        pattern; ///< Pattern to match the field value (null for punning: `{ name }` = `{ name = name }`)

    FieldPattern(std::string n, PatternPtr p = nullptr): name(std::move(n)), pattern(std::move(p)) {}

    /// Clone this field pattern
    [[nodiscard]] FieldPattern clone() const;
};

/// Matches a record by its fields.
///
/// Examples:
/// - `{ name; age }` matches a record with name and age fields (punning)
/// - `{ name = n; age = a }` matches and renames
/// - `{ name; _ }` matches a record with name, ignoring other fields
struct RecordPattern final: Pattern
{
    std::vector<FieldPattern> fields;
    bool hasWildcard = false; ///< True if pattern contains `_` to ignore other fields

    RecordPattern(std::vector<FieldPattern> f, bool wildcard = false):
        fields(std::move(f)), hasWildcard(wildcard)
    {
    }

    void accept(PatternVisitor& visitor) const override;
    [[nodiscard]] std::unique_ptr<Pattern> clone() const override;
};

// ============================================================================
// Constructor/Variant Patterns
// ============================================================================

/// Matches a discriminated union constructor.
///
/// Examples:
/// - `Some x` matches Some variant and binds payload
/// - `None` matches None variant (no payload)
/// - `Ok value` matches Ok variant
/// - `Error { code; message }` matches Error with record payload
/// - `Circle r` matches a Circle union case
struct ConstructorPattern final: Pattern
{
    std::string name;                  ///< Constructor name (Some, None, Ok, Error, or user-defined)
    std::optional<PatternPtr> payload; ///< Optional payload pattern

    ConstructorPattern(std::string n, std::optional<PatternPtr> p = std::nullopt):
        name(std::move(n)), payload(std::move(p))
    {
    }

    void accept(PatternVisitor& visitor) const override;
    [[nodiscard]] std::unique_ptr<Pattern> clone() const override;
};

// ============================================================================
// Composite Patterns
// ============================================================================

/// Binds the entire matched value while also destructuring.
///
/// Examples:
/// - `{ name; price } as product` binds both fields and the whole record
/// - `Leaf _ as leaf` binds the whole leaf node
struct AsPattern final: Pattern
{
    PatternPtr inner; ///< Pattern to match against
    std::string name; ///< Name to bind the entire value

    AsPattern(PatternPtr p, std::string n): inner(std::move(p)), name(std::move(n)) {}

    void accept(PatternVisitor& visitor) const override;
    [[nodiscard]] std::unique_ptr<Pattern> clone() const override;
};

/// Matches if any of the alternatives match (logical OR).
///
/// Examples:
/// - `"quit" | "exit" | "q"` matches any of the three strings
/// - `200 | 201 | 204` matches success status codes
struct OrPattern final: Pattern
{
    std::vector<PatternPtr> alternatives;

    explicit OrPattern(std::vector<PatternPtr> alts): alternatives(std::move(alts)) {}

    void accept(PatternVisitor& visitor) const override;
    [[nodiscard]] std::unique_ptr<Pattern> clone() const override;
};

/// A guarded pattern with a when clause.
///
/// Examples:
/// - `x when x < 0` matches negative numbers
/// - `{ age } when age >= 18` matches adults
///
/// Note: The guard expression is represented as a string for now.
/// Once the F# expression AST is implemented, this should be replaced
/// with a proper expression node.
struct GuardedPattern final: Pattern
{
    PatternPtr pattern; ///< The pattern to match
    std::string guard;  ///< Guard expression (temporary: string representation)

    GuardedPattern(PatternPtr p, std::string g): pattern(std::move(p)), guard(std::move(g)) {}

    void accept(PatternVisitor& visitor) const override;
    [[nodiscard]] std::unique_ptr<Pattern> clone() const override;
};

// ============================================================================
// Pattern Visitor
// ============================================================================

/// Visitor interface for pattern AST traversal.
struct PatternVisitor
{
    virtual ~PatternVisitor() = default;

    virtual void visit(LiteralPattern const&) = 0;
    virtual void visit(VariablePattern const&) = 0;
    virtual void visit(WildcardPattern const&) = 0;
    virtual void visit(TuplePattern const&) = 0;
    virtual void visit(ListPattern const&) = 0;
    virtual void visit(ConsPattern const&) = 0;
    virtual void visit(RecordPattern const&) = 0;
    virtual void visit(ConstructorPattern const&) = 0;
    virtual void visit(AsPattern const&) = 0;
    virtual void visit(OrPattern const&) = 0;
    virtual void visit(GuardedPattern const&) = 0;
};

// ============================================================================
// Pattern Utilities
// ============================================================================

/// Collects all variable bindings from a pattern.
///
/// For example, `(x, { name; age })` produces ["x", "name", "age"].
/// Used for type checking and code generation.
[[nodiscard]] std::vector<std::string> collectBindings(Pattern const& pattern);

/// Checks if a pattern is irrefutable (always matches).
///
/// Irrefutable patterns:
/// - Variable patterns
/// - Wildcard patterns
/// - Tuple/record patterns with all irrefutable sub-patterns
///
/// Refutable patterns:
/// - Literal patterns
/// - Constructor patterns (except single-variant types)
/// - List patterns (except empty list matching empty list)
[[nodiscard]] bool isIrrefutable(Pattern const& pattern);

/// Pretty-prints a pattern for debugging/error messages.
[[nodiscard]] std::string toString(Pattern const& pattern);

// ============================================================================
// Factory Functions
// ============================================================================

namespace patterns
{

    /// Create a literal pattern
    inline PatternPtr literal(int64_t v)
    {
        return std::make_unique<LiteralPattern>(v);
    }

    inline PatternPtr literal(double v)
    {
        return std::make_unique<LiteralPattern>(v);
    }

    inline PatternPtr literal(bool v)
    {
        return std::make_unique<LiteralPattern>(v);
    }

    inline PatternPtr literal(std::string v)
    {
        return std::make_unique<LiteralPattern>(std::move(v));
    }

    /// Create a variable pattern
    inline PatternPtr variable(std::string name, bool mut = false)
    {
        return std::make_unique<VariablePattern>(std::move(name), mut);
    }

    /// Create a wildcard pattern
    inline PatternPtr wildcard()
    {
        return std::make_unique<WildcardPattern>();
    }

    /// Create a tuple pattern
    inline PatternPtr tuple(std::vector<PatternPtr> elements)
    {
        return std::make_unique<TuplePattern>(std::move(elements));
    }

    /// Create a list pattern
    inline PatternPtr list(std::vector<PatternPtr> elements, std::optional<std::string> rest = std::nullopt)
    {
        return std::make_unique<ListPattern>(std::move(elements), std::move(rest));
    }

    /// Create a cons pattern
    inline PatternPtr cons(PatternPtr head, PatternPtr tail)
    {
        return std::make_unique<ConsPattern>(std::move(head), std::move(tail));
    }

    /// Create a record pattern
    inline PatternPtr record(std::vector<FieldPattern> fields, bool wildcard = false)
    {
        return std::make_unique<RecordPattern>(std::move(fields), wildcard);
    }

    /// Create a constructor pattern
    inline PatternPtr constructor(std::string name, std::optional<PatternPtr> payload = std::nullopt)
    {
        return std::make_unique<ConstructorPattern>(std::move(name), std::move(payload));
    }

    /// Create an as pattern
    inline PatternPtr as(PatternPtr inner, std::string name)
    {
        return std::make_unique<AsPattern>(std::move(inner), std::move(name));
    }

    /// Create an or pattern
    inline PatternPtr or_(std::vector<PatternPtr> alternatives)
    {
        return std::make_unique<OrPattern>(std::move(alternatives));
    }

    /// Create a guarded pattern
    inline PatternPtr guarded(PatternPtr pattern, std::string guard)
    {
        return std::make_unique<GuardedPattern>(std::move(pattern), std::move(guard));
    }

} // namespace patterns

} // namespace endo::pattern
