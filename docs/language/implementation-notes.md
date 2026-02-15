# Implementation Notes

### 14.1 Lexer Modifications

New tokens required for F# syntax:

```cpp
enum class Token {
    // ... existing tokens ...

    // F# style keywords
    Let,              // 'let'
    Mut,              // 'mut'
    Fun,              // 'fun'
    Match,            // 'match'
    With,             // 'with'
    When,             // 'when'
    Type,             // 'type'
    Of,               // 'of'
    Rec,              // 'rec'
    And,              // 'and' (mutual recursion)
    As,               // 'as' (pattern alias)
    In,               // 'in' (let...in)

    // New operators
    Arrow,            // '->'
    FatArrow,         // '=>'
    LeftArrow,        // '<-'
    ForwardPipe,      // '|>'
    Composition,      // '>>'
    BackComposition,  // '<<'
    DoubleColon,      // '::'
    DoubleStar,       // '**'
    QuestionMark,     // '?'
    QuestionPipe,     // '?|'
    QuestionDot,      // '?.'

    // List delimiters
    // Note: Semicolon already exists but now also separates list elements
};
```

### 14.2 Grammar Ambiguities and Resolution

**Challenge 1: `let` binding vs bash assignment**
<!-- endo-no-check -->
```endo
let x = 42          # F# style (new)
x=42                # Bash style (existing)
```
**Resolution:** The `let` keyword unambiguously starts F# style. Bash style requires no spaces around `=`.

**Challenge 2: Pipe operators `|` vs `|>`**
<!-- endo-no-check -->
```endo
cmd1 | cmd2         # Shell pipe (process stdout -> stdin)
data |> func        # Function pipe (value -> function)
```
**Resolution:** Lexer emits different tokens. `|` followed by `>` produces `ForwardPipe`, otherwise `Pipe`.

**Challenge 3: Function call vs command**
<!-- endo-no-check -->
```endo
let x = foo bar     # Is foo a function or command?
```
**Resolution:** Unified semantics - resolved at runtime based on resolution order. Parser treats all calls uniformly as applications.

**Challenge 4: Semicolons in different contexts**
<!-- endo-no-check -->
```endo
[1; 2; 3]           # List element separator
cmd1; cmd2          # Statement separator
if cond then        # No semicolon needed (optional)
```
**Resolution:** Context-sensitive parsing. Inside `[]`, semicolon separates list elements. At statement level, separates statements.

**Challenge 5: Pattern vs Expression in match arms**
<!-- endo-no-check -->
```endo
match x with
| pattern -> expr   # pattern is NOT an expression
```
**Resolution:** After `|` in match context, parse as pattern (different grammar rules). After `->`, parse as expression.

### 14.3 Parser Structure

```cpp
class Parser {
    // ... existing methods ...

    // F# style additions
    std::unique_ptr<Stmt> parseLetBinding();
    std::unique_ptr<Stmt> parseFunctionDef();
    std::unique_ptr<Stmt> parseTypeDefinition();

    std::unique_ptr<Expr> parseLambda();
    std::unique_ptr<Expr> parseMatchExpr();
    std::unique_ptr<Expr> parseListLiteral();
    std::unique_ptr<Expr> parseRecordLiteral();
    std::unique_ptr<Expr> parsePipelineExpr();
    std::unique_ptr<Expr> parseApplicationExpr();

    // Pattern parsing
    std::unique_ptr<Pattern> parsePattern();
    std::unique_ptr<Pattern> parseOrPattern();
    std::unique_ptr<Pattern> parseConsPattern();
    std::unique_ptr<Pattern> parsePrimaryPattern();

    // Helper for curried parameters
    std::vector<Parameter> parseFunctionParams();

    // Type parsing
    std::unique_ptr<Type> parseType();
    std::unique_ptr<Type> parseTypeAtom();
    std::optional<Type> parseOptionalTypeAnnotation();
};
```

### 14.4 AST Extensions

```cpp
// New statement nodes
struct LetBindingStmt : Stmt {
    std::unique_ptr<Pattern> pattern;
    bool isMutable;
    std::optional<Type> typeAnnotation;
    std::unique_ptr<Expr> value;
};

struct FunctionDefStmt : Stmt {
    std::string name;
    bool isRecursive;
    std::vector<Parameter> params;
    std::optional<Type> returnType;
    std::unique_ptr<Expr> body;
};

struct TypeDefStmt : Stmt {
    std::string name;
    std::vector<std::string> typeParams;
    std::unique_ptr<TypeBody> body;  // RecordType or UnionType
};

// New expression nodes
struct LambdaExpr : Expr {
    std::vector<Parameter> params;
    std::unique_ptr<Expr> body;
};

struct MatchExpr : Expr {
    std::unique_ptr<Expr> scrutinee;
    std::vector<MatchArm> arms;
};

struct MatchArm {
    std::unique_ptr<Pattern> pattern;
    std::optional<std::unique_ptr<Expr>> guard;  // when clause
    std::unique_ptr<Expr> body;
};

struct ListExpr : Expr {
    std::vector<std::unique_ptr<Expr>> elements;
    // Or range: start, end, step
    std::optional<RangeSpec> range;
};

struct RecordExpr : Expr {
    std::optional<std::unique_ptr<Expr>> baseRecord;  // { x with ... }
    std::vector<FieldAssignment> fields;
};

struct PipelineExpr : Expr {
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    enum class Kind { Forward, Shell } kind;  // |> vs |
};

struct ApplicationExpr : Expr {
    std::unique_ptr<Expr> function;
    std::vector<std::unique_ptr<Expr>> args;
};

struct PropagateExpr : Expr {
    std::unique_ptr<Expr> inner;  // The ? operator
};

// Pattern AST
struct Pattern { virtual ~Pattern() = default; };

struct LiteralPattern : Pattern {
    Value value;
};

struct VariablePattern : Pattern {
    std::string name;
};

struct WildcardPattern : Pattern {};

struct TuplePattern : Pattern {
    std::vector<std::unique_ptr<Pattern>> elements;
};

struct ListPattern : Pattern {
    std::vector<std::unique_ptr<Pattern>> elements;
    std::optional<std::string> rest;  // for [head; tail...]
};

struct ConsPattern : Pattern {
    std::unique_ptr<Pattern> head;
    std::unique_ptr<Pattern> tail;
};

struct RecordPattern : Pattern {
    std::vector<FieldPattern> fields;
    bool hasWildcard;  // { name; _ }
};

struct ConstructorPattern : Pattern {
    std::string name;  // Some, None, Ok, Error, or user-defined
    std::optional<std::unique_ptr<Pattern>> payload;
};

struct AsPattern : Pattern {
    std::unique_ptr<Pattern> inner;
    std::string name;
};

struct OrPattern : Pattern {
    std::vector<std::unique_ptr<Pattern>> alternatives;
};
```

### 14.5 Type Inference Engine

```cpp
class TypeInference {
public:
    // Entry point: infer type of expression in environment
    Type infer(const Expr& expr, TypeEnv& env);

    // Check expression against expected type
    bool check(const Expr& expr, Type expected, TypeEnv& env);

    // Unification: find substitution making types equal
    std::optional<Substitution> unify(Type a, Type b);

    // Apply substitution to type
    Type apply(const Substitution& subst, Type t);

private:
    int freshVarCounter = 0;

    // Create fresh type variable
    Type freshTypeVar() {
        return TypeVar("t" + std::to_string(freshVarCounter++));
    }

    // Specific inference rules
    Type inferLet(const LetBindingStmt& let, TypeEnv& env);
    Type inferLambda(const LambdaExpr& lambda, TypeEnv& env);
    Type inferApplication(const ApplicationExpr& app, TypeEnv& env);
    Type inferMatch(const MatchExpr& match, TypeEnv& env);
    Type inferList(const ListExpr& list, TypeEnv& env);
    Type inferRecord(const RecordExpr& record, TypeEnv& env);
    Type inferPipeline(const PipelineExpr& pipeline, TypeEnv& env);

    // Pattern type checking (returns bindings introduced)
    TypeEnv checkPattern(const Pattern& pat, Type expectedType, TypeEnv& env);

    // Generalization for let-polymorphism
    TypeScheme generalize(Type t, const TypeEnv& env);
    Type instantiate(const TypeScheme& scheme);
};

// Type representation
struct Type {
    std::variant<
        PrimitiveType,      // int, str, bool, etc.
        TypeVar,            // 'a, 'b (inference variables)
        FunctionType,       // T -> U
        ListType,           // list<T>
        TupleType,          // (T, U, V)
        RecordType,         // { field: T; ... }
        UnionType,          // Case1 of T | Case2 of U
        OptionType,         // option<T>
        ResultType          // result<T, E>
    > inner;
};

struct TypeScheme {
    std::vector<std::string> quantified;  // Bound type variables
    Type body;
};
```

### 14.6 IR Generation Extensions

```cpp
class IRGenerator : public Visitor {
    // ... existing methods ...

    // New visit methods
    void visit(const LetBindingStmt& stmt) override;
    void visit(const FunctionDefStmt& stmt) override;
    void visit(const LambdaExpr& expr) override;
    void visit(const MatchExpr& expr) override;
    void visit(const ListExpr& expr) override;
    void visit(const RecordExpr& expr) override;
    void visit(const PipelineExpr& expr) override;

    // Pattern compilation (generates decision tree)
    void compilePattern(const Pattern& pat, Register scrutinee, Label onMatch, Label onFail);
    void compileMatchArms(const std::vector<MatchArm>& arms, Register scrutinee);

    // Closure handling
    std::vector<std::string> findFreeVariables(const LambdaExpr& lambda);
    void emitClosure(const LambdaExpr& lambda, const std::vector<std::string>& freeVars);
};
```

---
**See also:** [Grammar](grammar.md) | [Type System](type-system.md) | [Lexical Elements](lexical-elements.md)
