// SPDX-License-Identifier: Apache-2.0
#include "SymbolCollector.hpp"

#include <endo-language/ast/AST.hpp>
#include <endo-language/ast/Pattern.hpp>
#include <endo-language/lexer/Lexer.hpp>
#include <endo-language/parser/Parser.hpp>

#include <editor-protocol/StubRuntime.hpp>

#include <cstdint>
#include <ranges>
#include <unordered_map>
#include <utility>

namespace endo::lsp
{

namespace
{

    /// Represents a symbol event emitted during AST traversal.
    struct SymbolEvent
    {
        enum class Kind : std::uint8_t
        {
            Definition,
            Reference,
        };
        Kind kind;
        std::string name;
        int defIndex = -1; ///< For definitions: index in table. For references: resolved def index.
    };

    /// Scope-aware AST walker that collects symbol definitions and references.
    class SymbolWalker
    {
      public:
        SymbolTable& table;
        std::vector<SymbolEvent>& events;

        SymbolWalker(SymbolTable& t, std::vector<SymbolEvent>& e): table(t), events(e) {}

        void pushScope() { _scopes.emplace_back(); }

        void popScope() { _scopes.pop_back(); }

        /// Defines a symbol in the current scope and emits a definition event.
        int defineSymbol(std::string const& name, SymbolDefinition def)
        {
            auto const index = static_cast<int>(table.definitions.size());
            def.scopeId = _nextScopeId++;
            def.nestingDepth = static_cast<int>(_scopes.size()) - 1;
            table.definitions.push_back(std::move(def));
            if (!_scopes.empty())
                _scopes.back()[name] = index;
            events.push_back(SymbolEvent {
                .kind = SymbolEvent::Kind::Definition,
                .name = name,
                .defIndex = index,
            });
            return index;
        }

        /// Resolves a symbol name to its definition index, searching from innermost scope.
        [[nodiscard]] int resolveSymbol(std::string const& name) const
        {
            for (const auto& _scope: std::ranges::reverse_view(_scopes))
            {
                if (auto found = _scope.find(name); found != _scope.end())
                    return found->second;
            }
            return -1;
        }

        /// Records a reference and emits a reference event.
        /// @param isWrite true for mutation assignments (LHS of `<-`)
        void addReference(std::string const& name, bool isWrite = false)
        {
            auto const defIndex = resolveSymbol(name);
            auto const refIndex = static_cast<int>(table.references.size());
            table.references.push_back(SymbolReference {
                .name = name,
                .location = {},
                .definitionIndex = defIndex,
                .isWrite = isWrite,
            });
            events.push_back(SymbolEvent {
                .kind = SymbolEvent::Kind::Reference,
                .name = name,
                .defIndex = refIndex, // Index into references vector
            });
        }

        void walkStatement(ast::Node const& node)
        {
            if (auto const* compound = dynamic_cast<ast::CompoundStmt const*>(&node))
            {
                for (auto const& stmt: compound->statements)
                    walkStatement(*stmt);
            }
            else if (auto const* letStmt = dynamic_cast<ast::LetBindingStmt const*>(&node))
            {
                walkLetBinding(*letStmt);
            }
            else if (auto const* rec = dynamic_cast<ast::RecordTypeDefStmt const*>(&node))
            {
                walkRecordTypeDef(*rec);
            }
            else if (auto const* uni = dynamic_cast<ast::UnionTypeDefStmt const*>(&node))
            {
                walkUnionTypeDef(*uni);
            }
            else if (auto const* forIn = dynamic_cast<ast::ForInStmt const*>(&node))
            {
                walkExpr(*forIn->source);
                pushScope();
                for (auto const& name: pattern::collectBindings(*forIn->pattern))
                {
                    defineSymbol(name,
                                 SymbolDefinition {
                                     .name = name,
                                     .category = SymbolCategory::Parameter,
                                 });
                }
                walkStatement(*forIn->body);
                popScope();
            }
            else if (auto const* e = dynamic_cast<ast::MutAssignStmt const*>(&node))
            {
                addReference(e->name, /*isWrite=*/true);
                walkExpr(*e->value);
            }
            else if (auto const* exprStmt = dynamic_cast<ast::ExprStmt const*>(&node))
            {
                walkExpr(*exprStmt->expr);
            }
        }

        void walkLetBinding(ast::LetBindingStmt const& letStmt)
        {
            // Determine category
            auto category = SymbolCategory::Variable;
            if (letStmt.isProperty())
                category = SymbolCategory::Property;
            else if (letStmt.isFunction())
                category = SymbolCategory::Function;

            auto def = SymbolDefinition { .name = letStmt.name, .category = category };
            for (auto const& param: letStmt.parameters)
            {
                def.parameterNames.push_back(param.name);
                def.parameterTypes.push_back(param.typeAnnotation ? toString(*param.typeAnnotation)
                                                                  : std::string {});
            }
            if (letStmt.returnType)
                def.returnType = toString(*letStmt.returnType);
            auto const defIndex = defineSymbol(letStmt.name, std::move(def));

            // Walk value in a scope with parameters
            pushScope();
            if (category == SymbolCategory::Function)
                pushEnclosingFunction(defIndex);
            for (auto const& param: letStmt.parameters)
            {
                defineSymbol(param.name,
                             SymbolDefinition {
                                 .name = param.name,
                                 .category = SymbolCategory::Parameter,
                                 .enclosingSymbol = letStmt.name,
                             });
            }
            if (letStmt.value)
                walkExpr(*letStmt.value);
            if (category == SymbolCategory::Function)
                popEnclosingFunction();
            popScope();

            // Handle and-bindings
            for (auto const& andBinding: letStmt.andBindings)
            {
                auto andDef = SymbolDefinition {
                    .name = andBinding.name,
                    .category =
                        andBinding.parameters.empty() ? SymbolCategory::Variable : SymbolCategory::Function,
                };
                for (auto const& param: andBinding.parameters)
                {
                    andDef.parameterNames.push_back(param.name);
                    andDef.parameterTypes.push_back(param.typeAnnotation ? toString(*param.typeAnnotation)
                                                                         : std::string {});
                }
                if (andBinding.returnType)
                    andDef.returnType = toString(*andBinding.returnType);
                defineSymbol(andBinding.name, std::move(andDef));

                pushScope();
                for (auto const& param: andBinding.parameters)
                {
                    defineSymbol(param.name,
                                 SymbolDefinition {
                                     .name = param.name,
                                     .category = SymbolCategory::Parameter,
                                     .enclosingSymbol = andBinding.name,
                                 });
                }
                if (andBinding.value)
                    walkExpr(*andBinding.value);
                popScope();
            }
        }

        void walkRecordTypeDef(ast::RecordTypeDefStmt const& rec)
        {
            defineSymbol(rec.name,
                         SymbolDefinition { .name = rec.name, .category = SymbolCategory::RecordType });
            for (auto const& field: rec.fields)
            {
                defineSymbol(field.name,
                             SymbolDefinition {
                                 .name = field.name,
                                 .category = SymbolCategory::RecordField,
                                 .detail = toString(*field.type),
                                 .enclosingSymbol = rec.name,
                             });
            }
        }

        void walkUnionTypeDef(ast::UnionTypeDefStmt const& uni)
        {
            defineSymbol(uni.name,
                         SymbolDefinition { .name = uni.name, .category = SymbolCategory::UnionType });
            for (auto const& variant: uni.variants)
            {
                auto detail = std::optional<std::string> {};
                if (!variant.payloadTypes.empty())
                {
                    auto parts = std::string {};
                    for (auto const& pt: variant.payloadTypes)
                    {
                        if (!parts.empty())
                            parts += " * ";
                        parts += toString(*pt);
                    }
                    detail = std::move(parts);
                }
                defineSymbol(variant.name,
                             SymbolDefinition {
                                 .name = variant.name,
                                 .category = SymbolCategory::UnionVariant,
                                 .detail = std::move(detail),
                                 .enclosingSymbol = uni.name,
                             });
            }
        }

        void walkExpr(ast::Expr const& expr)
        {
            if (auto const* e = dynamic_cast<ast::IdentifierExpr const*>(&expr))
            {
                addReference(e->name);
            }
            else if (auto const* e = dynamic_cast<ast::ApplicationExpr const*>(&expr))
            {
                // Track call relations for call hierarchy
                if (auto const* ident = dynamic_cast<ast::IdentifierExpr const*>(e->function.get()))
                {
                    auto const calleeDefIndex = resolveSymbol(ident->name);
                    auto const callerDefIndex = currentEnclosingFunction();
                    if (calleeDefIndex >= 0)
                    {
                        table.callRelations.push_back(CallRelation {
                            .callerDefIndex = callerDefIndex,
                            .calleeDefIndex = calleeDefIndex,
                            .callSite = {}, // Will be assigned by assignLocations
                        });
                    }
                }
                walkExpr(*e->function);
                walkExpr(*e->argument);
            }
            else if (auto const* e = dynamic_cast<ast::BinaryExpr const*>(&expr))
            {
                walkExpr(*e->left);
                walkExpr(*e->right);
            }
            else if (auto const* e = dynamic_cast<ast::UnaryExpr const*>(&expr))
            {
                walkExpr(*e->operand);
            }
            else if (auto const* e = dynamic_cast<ast::IfExpr const*>(&expr))
            {
                walkExpr(*e->condition);
                walkExpr(*e->thenExpr);
                if (e->elseExpr)
                    walkExpr(*e->elseExpr);
            }
            else if (auto const* e = dynamic_cast<ast::LetInExpr const*>(&expr))
            {
                pushScope();
                auto const isFunc = e->isFunction();
                auto def = SymbolDefinition {
                    .name = e->name,
                    .category = isFunc ? SymbolCategory::Function : SymbolCategory::Variable,
                    .enclosingSymbol =
                        currentEnclosingFunction() >= 0
                            ? table.definitions[static_cast<size_t>(currentEnclosingFunction())].name
                            : std::optional<std::string> {},
                };
                for (auto const& param: e->parameters)
                {
                    def.parameterNames.push_back(param.name);
                    def.parameterTypes.push_back(param.typeAnnotation ? toString(*param.typeAnnotation)
                                                                      : std::string {});
                }
                if (e->returnType)
                    def.returnType = toString(*e->returnType);
                auto const defIndex = defineSymbol(e->name, std::move(def));

                pushScope();
                if (isFunc)
                    pushEnclosingFunction(defIndex);
                for (auto const& param: e->parameters)
                {
                    defineSymbol(param.name,
                                 SymbolDefinition {
                                     .name = param.name,
                                     .category = SymbolCategory::Parameter,
                                     .enclosingSymbol = e->name,
                                 });
                }
                if (e->value)
                    walkExpr(*e->value);
                if (isFunc)
                    popEnclosingFunction();
                popScope();

                if (e->body)
                    walkExpr(*e->body);
                popScope();
            }
            else if (auto const* e = dynamic_cast<ast::LambdaExpr const*>(&expr))
            {
                pushScope();
                for (auto const& param: e->parameters)
                {
                    defineSymbol(param.name,
                                 SymbolDefinition {
                                     .name = param.name,
                                     .category = SymbolCategory::Parameter,
                                 });
                }
                walkExpr(*e->body);
                popScope();
            }
            else if (auto const* e = dynamic_cast<ast::MatchExpr const*>(&expr))
            {
                walkExpr(*e->scrutinee);
                for (auto const& arm: e->arms)
                {
                    pushScope();
                    if (arm.pattern)
                    {
                        for (auto const& name: pattern::collectBindings(*arm.pattern))
                        {
                            defineSymbol(name,
                                         SymbolDefinition {
                                             .name = name,
                                             .category = SymbolCategory::Parameter,
                                         });
                        }
                    }
                    if (arm.guard)
                        walkExpr(*arm.guard);
                    if (arm.body)
                        walkExpr(*arm.body);
                    popScope();
                }
            }
            else if (auto const* e = dynamic_cast<ast::PipelineExpr const*>(&expr))
            {
                walkExpr(*e->value);
                walkExpr(*e->function);
            }
            else if (auto const* e = dynamic_cast<ast::ParenExpr const*>(&expr))
            {
                walkExpr(*e->inner);
            }
            else if (auto const* e = dynamic_cast<ast::TupleExpr const*>(&expr))
            {
                for (auto const& elem: e->elements)
                    walkExpr(*elem);
            }
            else if (auto const* e = dynamic_cast<ast::ListExpr const*>(&expr))
            {
                for (auto const& elem: e->elements)
                    walkExpr(*elem);
            }
            else if (auto const* e = dynamic_cast<ast::ListComprehensionExpr const*>(&expr))
            {
                walkExpr(*e->source);
                pushScope();
                defineSymbol(e->variable,
                             SymbolDefinition {
                                 .name = e->variable,
                                 .category = SymbolCategory::Parameter,
                             });
                if (e->filter)
                    walkExpr(*e->filter);
                walkExpr(*e->body);
                popScope();
            }
            else if (auto const* e = dynamic_cast<ast::OptionExpr const*>(&expr))
            {
                if (e->value)
                    walkExpr(*e->value);
            }
            else if (auto const* e = dynamic_cast<ast::ResultExpr const*>(&expr))
            {
                if (e->payload)
                    walkExpr(*e->payload);
            }
            else if (auto const* e = dynamic_cast<ast::TryExpr const*>(&expr))
            {
                walkExpr(*e->operand);
            }
            else if (auto const* e = dynamic_cast<ast::TryWithExpr const*>(&expr))
            {
                walkExpr(*e->body);
                for (auto const& handler: e->handlers)
                {
                    pushScope();
                    if (handler.pattern)
                    {
                        for (auto const& name: pattern::collectBindings(*handler.pattern))
                        {
                            defineSymbol(name,
                                         SymbolDefinition {
                                             .name = name,
                                             .category = SymbolCategory::Parameter,
                                         });
                        }
                    }
                    if (handler.guard)
                        walkExpr(*handler.guard);
                    if (handler.body)
                        walkExpr(*handler.body);
                    popScope();
                }
            }
            else if (auto const* e = dynamic_cast<ast::TryFinallyExpr const*>(&expr))
            {
                walkExpr(*e->body);
                walkExpr(*e->finallyExpr);
            }
            else if (auto const* e = dynamic_cast<ast::BlockExpr const*>(&expr))
            {
                pushScope();
                for (auto const& stmt: e->statements)
                    walkStatement(*stmt);
                if (e->result)
                    walkExpr(*e->result);
                popScope();
            }
            else if (auto const* e = dynamic_cast<ast::FieldAccessExpr const*>(&expr))
            {
                walkExpr(*e->object);
                addReference(e->fieldName);
            }
            else if (auto const* e = dynamic_cast<ast::FStringExpr const*>(&expr))
            {
                for (auto const& part: e->parts)
                    walkExpr(*part);
            }
            else if (auto const* e = dynamic_cast<ast::ConsExpr const*>(&expr))
            {
                walkExpr(*e->head);
                walkExpr(*e->tail);
            }
            else if (auto const* e = dynamic_cast<ast::UnionConstructorExpr const*>(&expr))
            {
                for (auto const& arg: e->arguments)
                    walkExpr(*arg);
            }
            else if (auto const* e = dynamic_cast<ast::MutAssignExpr const*>(&expr))
            {
                addReference(e->name, /*isWrite=*/true);
                walkExpr(*e->value);
            }
            else if (auto const* e = dynamic_cast<ast::OptionDefaultExpr const*>(&expr))
            {
                walkExpr(*e->option);
                walkExpr(*e->defaultValue);
            }
            else if (auto const* e = dynamic_cast<ast::CompositionExpr const*>(&expr))
            {
                walkExpr(*e->left);
                walkExpr(*e->right);
            }
            else if (auto const* e = dynamic_cast<ast::RecordExpr const*>(&expr))
            {
                for (auto const& field: e->fields)
                {
                    addReference(field.name);
                    walkExpr(*field.value);
                }
            }
            else if (auto const* e = dynamic_cast<ast::RecordUpdateExpr const*>(&expr))
            {
                walkExpr(*e->base);
                for (auto const& update: e->updates)
                {
                    addReference(update.name);
                    walkExpr(*update.value);
                }
            }
            else if (auto const* e = dynamic_cast<ast::ConcatListExpr const*>(&expr))
            {
                walkExpr(*e->left);
                walkExpr(*e->right);
            }
            else if (auto const* e = dynamic_cast<ast::ListRangeExpr const*>(&expr))
            {
                walkExpr(*e->start);
                if (e->step)
                    walkExpr(*e->step);
                walkExpr(*e->end);
            }
            else if (auto const* e = dynamic_cast<ast::LazyExpr const*>(&expr))
            {
                walkExpr(*e->body);
            }
            else if (auto const* e = dynamic_cast<ast::SeqExpr const*>(&expr))
            {
                for (auto const& yield: e->yields)
                    walkExpr(*yield.value);
            }
            else if (auto const* e = dynamic_cast<ast::OptionalChainExpr const*>(&expr))
            {
                walkExpr(*e->object);
                addReference(e->fieldName);
            }
            else if (auto const* e = dynamic_cast<ast::PlaceholderLambdaExpr const*>(&expr))
            {
                walkExpr(*e->body);
            }
            else if (auto const* e = dynamic_cast<ast::SplatExpr const*>(&expr))
            {
                addReference(e->name);
            }
            else if (auto const* e = dynamic_cast<ast::ExecPipelineExpr const*>(&expr))
            {
                for (auto const& cmd: e->commands)
                {
                    walkExpr(*cmd.program);
                    for (auto const& arg: cmd.arguments)
                        walkExpr(*arg);
                }
            }
        }

        /// Pushes the current function definition index for call tracking.
        void pushEnclosingFunction(int defIndex) { _enclosingFunctionStack.push_back(defIndex); }

        /// Pops the current enclosing function definition index.
        void popEnclosingFunction() { _enclosingFunctionStack.pop_back(); }

        /// Returns the current enclosing function index, or -1 if at top level.
        [[nodiscard]] int currentEnclosingFunction() const
        {
            return _enclosingFunctionStack.empty() ? -1 : _enclosingFunctionStack.back();
        }

      private:
        std::vector<std::unordered_map<std::string, int>> _scopes;
        std::vector<int> _enclosingFunctionStack;
        int _nextScopeId = 0;
    };

    /// Builds a token index from the source for fast position lookups.
    struct TokenEntry
    {
        Token token;
        std::string literal;
        SourceLocationRange range;
    };

    /// Tokenizes the source and returns all tokens.
    [[nodiscard]] std::vector<TokenEntry> tokenize(std::string const& source)
    {
        std::vector<TokenEntry> tokens;
        auto lexer = Lexer { std::make_unique<StringSource>(source) };
        lexer.enterFSharpExpr();
        while (lexer.currentToken() != Token::EndOfInput)
        {
            tokens.push_back(TokenEntry {
                .token = lexer.currentToken(),
                .literal = lexer.currentLiteral(),
                .range = lexer.currentRange(),
            });
            lexer.nextToken();
        }
        return tokens;
    }

    /// Checks if a 0-based LSP position falls within a token's range.
    /// Uses the token literal length to compute the end column, which handles
    /// the lexer's edge case where end==begin for the last token in the source.
    [[nodiscard]] bool tokenContainsPosition(TokenEntry const& entry, Position pos)
    {
        auto const beginCol = entry.range.begin.column;
        auto const endCol = beginCol + static_cast<int>(entry.literal.size());
        auto const line = entry.range.begin.line;

        if (pos.line != line)
            return false;
        if (pos.character < beginCol)
            return false;
        if (pos.character >= endCol)
            return false;
        return true;
    }

    /// Finds the identifier token at the given cursor position.
    [[nodiscard]] TokenEntry const* findIdentifierAt(std::vector<TokenEntry> const& tokens, Position pos)
    {
        for (auto const& entry: tokens)
        {
            if (entry.token == Token::Identifier && tokenContainsPosition(entry, pos))
                return &entry;
        }
        return nullptr;
    }

    /// Creates a corrected SourceLocationRange for a token, ensuring end > begin.
    /// The lexer may produce zero-width ranges for the last token in the source
    /// (because EOF doesn't advance the column). We use the literal length to fix this.
    [[nodiscard]] SourceLocationRange correctedRange(TokenEntry const& tok)
    {
        auto range = tok.range;
        // If begin == end (zero-width), compute end from literal length
        if (range.end.line == range.begin.line && range.end.column <= range.begin.column)
        {
            range.end.column = range.begin.column + static_cast<int>(tok.literal.size());
        }
        return range;
    }

    /// Assigns locations to definitions and references by matching events to tokens.
    /// Events are produced in source order by the AST walker; identifier tokens
    /// also appear in source order. We consume tokens in order, matching by name.
    void assignLocations(SymbolTable& table,
                         std::vector<SymbolEvent> const& events,
                         std::vector<TokenEntry> const& tokens)
    {
        // Collect all identifier tokens in order
        std::vector<TokenEntry const*> identTokens;
        for (auto const& tok: tokens)
        {
            if (tok.token == Token::Identifier)
                identTokens.push_back(&tok);
        }

        auto tokenIt = size_t { 0 };

        for (auto const& event: events)
        {
            // Find the next identifier token with matching name
            while (tokenIt < identTokens.size() && identTokens[tokenIt]->literal != event.name)
                ++tokenIt;

            if (tokenIt >= identTokens.size())
                break;

            auto const range = correctedRange(*identTokens[tokenIt]);
            if (event.kind == SymbolEvent::Kind::Definition)
            {
                table.definitions[static_cast<size_t>(event.defIndex)].location = range;
            }
            else
            {
                table.references[static_cast<size_t>(event.defIndex)].location = range;
            }
            ++tokenIt;
        }
    }

} // namespace

std::optional<SymbolTable> collectSymbols(std::string const& source)
{
    CoreVM::Runtime runtime;
    registerStubRuntime(runtime);

    CoreVM::diagnostics::BufferedReport report;
    Parser parser(runtime, report, std::make_unique<StringSource>(source));
    auto astRoot = parser.parse();
    if (!astRoot)
        return std::nullopt;

    SymbolTable table;
    std::vector<SymbolEvent> events;
    SymbolWalker walker(table, events);
    walker.pushScope();
    walker.walkStatement(*astRoot);
    walker.popScope();

    auto tokens = tokenize(source);
    assignLocations(table, events, tokens);

    // Assign call site locations: each call relation corresponds to a reference
    // to the callee function. We match by callee definition index, consuming refs in order.
    {
        auto refIdx = size_t { 0 };
        for (auto& rel: table.callRelations)
        {
            while (refIdx < table.references.size())
            {
                if (table.references[refIdx].definitionIndex == rel.calleeDefIndex)
                {
                    rel.callSite = table.references[refIdx].location;
                    ++refIdx;
                    break;
                }
                ++refIdx;
            }
        }
    }

    return table;
}

std::optional<SourceLocationRange> findDefinition(std::string const& source, Position position)
{
    auto tokens = tokenize(source);
    auto const* identToken = findIdentifierAt(tokens, position);
    if (!identToken)
        return std::nullopt;

    auto const& name = identToken->literal;
    auto const& tokenBegin = identToken->range.begin;

    auto table = collectSymbols(source);
    if (!table)
        return std::nullopt;

    // Check if cursor is on a definition itself — match by begin position
    for (auto const& def: table->definitions)
    {
        if (def.name == name && def.location.begin.line == tokenBegin.line
            && def.location.begin.column == tokenBegin.column)
            return def.location;
    }

    // Find the reference at cursor and follow to its definition
    for (auto const& ref: table->references)
    {
        if (ref.name == name && ref.location.begin.line == tokenBegin.line
            && ref.location.begin.column == tokenBegin.column && ref.definitionIndex >= 0)
        {
            auto const& def = table->definitions[static_cast<size_t>(ref.definitionIndex)];
            return def.location;
        }
    }

    return std::nullopt;
}

std::vector<SourceLocationRange> findReferences(std::string const& source,
                                                Position position,
                                                bool includeDeclaration)
{
    auto tokens = tokenize(source);
    auto const* identToken = findIdentifierAt(tokens, position);
    if (!identToken)
        return {};

    auto const& name = identToken->literal;

    auto table = collectSymbols(source);
    if (!table)
        return {};

    auto const& tokenBegin = identToken->range.begin;

    // Find the target definition index
    auto targetDefIndex = -1;

    // Check if cursor is on a definition — match by begin position
    for (auto i = 0; std::cmp_less(i, table->definitions.size()); ++i)
    {
        auto const& def = table->definitions[static_cast<size_t>(i)];
        if (def.name == name && def.location.begin.line == tokenBegin.line
            && def.location.begin.column == tokenBegin.column)
        {
            targetDefIndex = i;
            break;
        }
    }

    // If not on a definition, check references
    if (targetDefIndex < 0)
    {
        for (auto const& ref: table->references)
        {
            if (ref.name == name && ref.location.begin.line == tokenBegin.line
                && ref.location.begin.column == tokenBegin.column && ref.definitionIndex >= 0)
            {
                targetDefIndex = ref.definitionIndex;
                break;
            }
        }
    }

    if (targetDefIndex < 0)
        return {};

    std::vector<SourceLocationRange> result;

    if (includeDeclaration)
    {
        auto const& def = table->definitions[static_cast<size_t>(targetDefIndex)];
        result.push_back(def.location);
    }

    for (auto const& ref: table->references)
    {
        if (ref.definitionIndex == targetDefIndex)
            result.push_back(ref.location);
    }

    return result;
}

std::optional<SourceLocationRange> findSymbolRangeAt(std::string const& source, Position position)
{
    auto tokens = tokenize(source);
    auto const* identToken = findIdentifierAt(tokens, position);
    if (!identToken)
        return std::nullopt;

    return correctedRange(*identToken);
}

std::vector<HighlightEntry> findHighlights(std::string const& source, Position position)
{
    auto tokens = tokenize(source);
    auto const* identToken = findIdentifierAt(tokens, position);
    if (!identToken)
        return {};

    auto const& name = identToken->literal;
    auto const& tokenBegin = identToken->range.begin;

    auto table = collectSymbols(source);
    if (!table)
        return {};

    // Find the target definition index
    auto targetDefIndex = -1;

    // Check if cursor is on a definition
    for (auto i = 0; std::cmp_less(i, table->definitions.size()); ++i)
    {
        auto const& def = table->definitions[static_cast<size_t>(i)];
        if (def.name == name && def.location.begin.line == tokenBegin.line
            && def.location.begin.column == tokenBegin.column)
        {
            targetDefIndex = i;
            break;
        }
    }

    // If not on a definition, check references
    if (targetDefIndex < 0)
    {
        for (auto const& ref: table->references)
        {
            if (ref.name == name && ref.location.begin.line == tokenBegin.line
                && ref.location.begin.column == tokenBegin.column && ref.definitionIndex >= 0)
            {
                targetDefIndex = ref.definitionIndex;
                break;
            }
        }
    }

    if (targetDefIndex < 0)
        return {};

    std::vector<HighlightEntry> result;

    // Add the definition itself (Write kind)
    auto const& def = table->definitions[static_cast<size_t>(targetDefIndex)];
    result.push_back(HighlightEntry { .range = def.location, .isDefinition = true });

    // Add all references (Read or Write kind)
    for (auto const& ref: table->references)
    {
        if (ref.definitionIndex == targetDefIndex)
            result.push_back(
                HighlightEntry { .range = ref.location, .isDefinition = false, .isWrite = ref.isWrite });
    }

    return result;
}

} // namespace endo::lsp
