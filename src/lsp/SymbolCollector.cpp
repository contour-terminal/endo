// SPDX-License-Identifier: Apache-2.0
#include "SymbolCollector.hpp"

#include <unordered_map>

#include "StubRuntime.hpp"

#include <endo-language/AST.hpp>
#include <endo-language/Lexer.hpp>
#include <endo-language/Parser.hpp>
#include <endo-language/Pattern.hpp>

namespace endo::lsp
{

namespace
{

    /// Represents a symbol event emitted during AST traversal.
    struct SymbolEvent
    {
        enum class Kind
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
            for (auto it = _scopes.rbegin(); it != _scopes.rend(); ++it)
            {
                if (auto found = it->find(name); found != it->end())
                    return found->second;
            }
            return -1;
        }

        /// Records a reference and emits a reference event.
        void addReference(std::string const& name)
        {
            auto const defIndex = resolveSymbol(name);
            auto const refIndex = static_cast<int>(table.references.size());
            table.references.push_back(SymbolReference {
                .name = name,
                .location = {},
                .definitionIndex = defIndex,
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
            else if (auto const* exprStmt = dynamic_cast<ast::ExprStmt const*>(&node))
            {
                walkExpr(*exprStmt->expr);
            }
        }

        void walkLetBinding(ast::LetBindingStmt const& letStmt)
        {
            // Define the binding
            auto def = SymbolDefinition { .name = letStmt.name, .isFunction = letStmt.isFunction() };
            for (auto const& param: letStmt.parameters)
            {
                def.parameterNames.push_back(param.name);
                def.parameterTypes.push_back(param.typeAnnotation ? toString(*param.typeAnnotation)
                                                                  : std::string {});
            }
            if (letStmt.returnType)
                def.returnType = toString(*letStmt.returnType);
            defineSymbol(letStmt.name, std::move(def));

            // Walk value in a scope with parameters
            pushScope();
            for (auto const& param: letStmt.parameters)
            {
                defineSymbol(param.name,
                             SymbolDefinition {
                                 .name = param.name,
                                 .isParameter = true,
                                 .enclosingFunction = letStmt.name,
                             });
            }
            if (letStmt.value)
                walkExpr(*letStmt.value);
            popScope();

            // Handle and-bindings
            for (auto const& andBinding: letStmt.andBindings)
            {
                auto andDef = SymbolDefinition {
                    .name = andBinding.name,
                    .isFunction = !andBinding.parameters.empty(),
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
                                     .isParameter = true,
                                     .enclosingFunction = andBinding.name,
                                 });
                }
                if (andBinding.value)
                    walkExpr(*andBinding.value);
                popScope();
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
                auto def = SymbolDefinition { .name = e->name, .isFunction = e->isFunction() };
                for (auto const& param: e->parameters)
                {
                    def.parameterNames.push_back(param.name);
                    def.parameterTypes.push_back(param.typeAnnotation ? toString(*param.typeAnnotation)
                                                                     : std::string {});
                }
                if (e->returnType)
                    def.returnType = toString(*e->returnType);
                defineSymbol(e->name, std::move(def));

                pushScope();
                for (auto const& param: e->parameters)
                {
                    defineSymbol(param.name,
                                 SymbolDefinition {
                                     .name = param.name,
                                     .isParameter = true,
                                     .enclosingFunction = e->name,
                                 });
                }
                if (e->value)
                    walkExpr(*e->value);
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
                                     .isParameter = true,
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
                                             .isParameter = true,
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
                                 .isParameter = true,
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
                                             .isParameter = true,
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
        }

      private:
        std::vector<std::unordered_map<std::string, int>> _scopes;
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
        // The lexer's begin.column is 1-based (column after the last char before the token was consumed).
        // Convert to 0-based for LSP comparison.
        auto const beginCol = entry.range.begin.column > 0 ? entry.range.begin.column - 1 : 0;
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
    for (auto i = 0; i < static_cast<int>(table->definitions.size()); ++i)
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

} // namespace endo::lsp
