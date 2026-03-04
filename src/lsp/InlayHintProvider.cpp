// SPDX-License-Identifier: Apache-2.0
#include "InlayHintProvider.hpp"

#include <endo-language/ast/AST.hpp>
#include <endo-language/lexer/Lexer.hpp>
#include <endo-language/parser/Parser.hpp>
#include <endo-language/types/Type.hpp>
#include <endo-language/types/TypeEnv.hpp>
#include <endo-language/types/TypeInferencer.hpp>

#include <editor-protocol/StubRuntime.hpp>

#include <string>
#include <vector>

namespace endo::lsp
{

namespace
{

    /// Describes a hint to be placed after a specific identifier token.
    struct HintEvent
    {
        enum class Kind
        {
            ParamType,      ///< `: <type>` after an untyped parameter name
            ReturnType,     ///< `: <type>` after the last parameter (before `=`)
            LetBindingType, ///< `: <type>` after a let-binding name (before `=`)
        };

        Kind kind;
        std::string name;  ///< Identifier name to match in the token stream
        std::string label; ///< The hint label text (e.g., ": int")
    };

    /// Checks if a type contains unresolved type variables.
    [[nodiscard]] bool containsTypeVar(TypePtr const& type)
    {
        if (!type)
            return true;
        if (type->isTypeVar())
            return true;
        if (auto const* fn = type->asFunction())
            return containsTypeVar(fn->paramType) || containsTypeVar(fn->returnType);
        if (auto const* lst = type->asList())
            return containsTypeVar(lst->elementType);
        if (auto const* tup = type->asTuple())
        {
            for (auto const& elem: tup->elementTypes)
                if (containsTypeVar(elem))
                    return true;
            return false;
        }
        if (auto const* opt = type->asOption())
            return containsTypeVar(opt->innerType);
        if (auto const* res = type->asResult())
            return containsTypeVar(res->okType) || containsTypeVar(res->errorType);
        return false;
    }

    /// Collects hint events from function parameters and return types.
    void collectFunctionHints(std::string const& funcName,
                              std::vector<ast::TypedParameter> const& parameters,
                              std::optional<TypePtr> const& returnTypeAnnotation,
                              InferenceResult const& inference,
                              std::vector<HintEvent>& events)
    {
        auto const it = inference.functions.find(funcName);
        if (it == inference.functions.end())
            return;

        auto const& inferred = it->second;

        // Parameter type hints for untyped parameters
        for (size_t i = 0; i < parameters.size() && i < inferred.paramTypes.size(); ++i)
        {
            auto const& param = parameters[i];
            if (param.typeAnnotation || param.isVariadic || param.isUnit)
                continue;
            auto const& paramType = inferred.paramTypes[i];
            if (containsTypeVar(paramType))
                continue;
            events.push_back(HintEvent {
                .kind = HintEvent::Kind::ParamType,
                .name = param.name,
                .label = ": " + toString(paramType),
            });
        }

        // Return type hint
        if (!returnTypeAnnotation && inferred.returnType)
        {
            auto const& retType = *inferred.returnType;
            if (!containsTypeVar(retType))
            {
                // Use the last parameter name as anchor — the hint is placed after it
                if (!parameters.empty())
                {
                    events.push_back(HintEvent {
                        .kind = HintEvent::Kind::ReturnType,
                        .name = parameters.back().name,
                        .label = ": " + toString(retType),
                    });
                }
            }
        }
    }

    /// AST walker that collects hint events in source order.
    class HintWalker
    {
      public:
        explicit HintWalker(InferenceResult const& inference): _inference(inference) {}

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
            else if (auto const* forIn = dynamic_cast<ast::ForInStmt const*>(&node))
            {
                walkExpr(*forIn->source);
                walkStatement(*forIn->body);
            }
        }

        [[nodiscard]] std::vector<HintEvent> const& events() const { return _events; }

      private:
        void walkLetBinding(ast::LetBindingStmt const& letStmt)
        {
            if (letStmt.isFunction())
            {
                collectFunctionHints(
                    letStmt.name, letStmt.parameters, letStmt.returnType, _inference, _events);

                // Walk and-bindings
                for (auto const& andBind: letStmt.andBindings)
                {
                    collectFunctionHints(
                        andBind.name, andBind.parameters, andBind.returnType, _inference, _events);
                    if (andBind.value)
                        walkExpr(*andBind.value);
                }
            }
            else if (!letStmt.isDestructuring())
            {
                // Simple value binding — check if it has a lambda value (recorded as function)
                if (dynamic_cast<ast::LambdaExpr const*>(letStmt.value.get()))
                {
                    // Lambda assigned to let binding: use function inference
                    auto const* lambda = dynamic_cast<ast::LambdaExpr const*>(letStmt.value.get());
                    collectFunctionHints(
                        letStmt.name, lambda->parameters, lambda->returnType, _inference, _events);
                }
                else
                {
                    // Simple value binding type hint
                    auto const it = _inference.bindings.find(letStmt.name);
                    if (it != _inference.bindings.end() && !containsTypeVar(it->second))
                    {
                        _events.push_back(HintEvent {
                            .kind = HintEvent::Kind::LetBindingType,
                            .name = letStmt.name,
                            .label = ": " + toString(it->second),
                        });
                    }
                }
            }

            if (letStmt.value)
                walkExpr(*letStmt.value);
        }

        void walkExpr(ast::Expr const& expr)
        {
            if (auto const* letIn = dynamic_cast<ast::LetInExpr const*>(&expr))
            {
                if (letIn->isFunction())
                {
                    collectFunctionHints(
                        letIn->name, letIn->parameters, letIn->returnType, _inference, _events);
                }
                else if (!letIn->isDestructuring())
                {
                    auto const it = _inference.bindings.find(letIn->name);
                    if (it != _inference.bindings.end() && !containsTypeVar(it->second))
                    {
                        _events.push_back(HintEvent {
                            .kind = HintEvent::Kind::LetBindingType,
                            .name = letIn->name,
                            .label = ": " + toString(it->second),
                        });
                    }
                }
                if (letIn->value)
                    walkExpr(*letIn->value);
                if (letIn->body)
                    walkExpr(*letIn->body);
            }
            else if (auto const* lambda = dynamic_cast<ast::LambdaExpr const*>(&expr))
            {
                // Lambdas not assigned to a named binding — skip (no name to look up in inference result)
                if (lambda->body)
                    walkExpr(*lambda->body);
            }
            else if (auto const* matchExpr = dynamic_cast<ast::MatchExpr const*>(&expr))
            {
                walkExpr(*matchExpr->scrutinee);
                for (auto const& arm: matchExpr->arms)
                {
                    if (arm.guard)
                        walkExpr(*arm.guard);
                    walkExpr(*arm.body);
                }
            }
            else if (auto const* ifExpr = dynamic_cast<ast::IfExpr const*>(&expr))
            {
                walkExpr(*ifExpr->condition);
                walkExpr(*ifExpr->thenExpr);
                if (ifExpr->elseExpr)
                    walkExpr(*ifExpr->elseExpr);
            }
            else if (auto const* block = dynamic_cast<ast::BlockExpr const*>(&expr))
            {
                for (auto const& stmt: block->statements)
                    walkStatement(*stmt);
                if (block->result)
                    walkExpr(*block->result);
            }
            else if (auto const* app = dynamic_cast<ast::ApplicationExpr const*>(&expr))
            {
                walkExpr(*app->function);
                walkExpr(*app->argument);
            }
            else if (auto const* pipe = dynamic_cast<ast::PipelineExpr const*>(&expr))
            {
                walkExpr(*pipe->value);
                walkExpr(*pipe->function);
            }
            else if (auto const* bin = dynamic_cast<ast::BinaryExpr const*>(&expr))
            {
                walkExpr(*bin->left);
                walkExpr(*bin->right);
            }
            else if (auto const* paren = dynamic_cast<ast::ParenExpr const*>(&expr))
            {
                walkExpr(*paren->inner);
            }
        }

        InferenceResult const& _inference;
        std::vector<HintEvent> _events;
    };

    /// Token entry for position lookup.
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

    /// Checks if a position is within the given range (inclusive start, exclusive end).
    [[nodiscard]] bool positionInRange(Position pos, Range range)
    {
        if (pos.line < range.start.line || pos.line > range.end.line)
            return false;
        if (pos.line == range.start.line && pos.character < range.start.character)
            return false;
        if (pos.line == range.end.line && pos.character > range.end.character)
            return false;
        return true;
    }

} // namespace

std::vector<InlayHint> computeInlayHints(std::string const& source, Range range)
{
    // Parse source into AST
    CoreVM::Runtime runtime;
    endo::registerStubRuntime(runtime);

    CoreVM::diagnostics::BufferedReport report;
    Parser parser(runtime, report, std::make_unique<StringSource>(source));
    auto astRoot = parser.parse();
    if (!astRoot)
        return {};

    // Run type inference
    InferenceResult inference;
    try
    {
        auto env = createStandardTypeEnv();
        TypeInferencer inferencer(std::move(env));
        inference = inferencer.inferProgram(*astRoot);
    }
    catch (...)
    {
        return {};
    }

    // Walk AST to collect hint events
    HintWalker walker(inference);
    walker.walkStatement(*astRoot);
    auto const& events = walker.events();
    if (events.empty())
        return {};

    // Tokenize source for position lookup
    auto const tokens = tokenize(source);

    // Collect identifier tokens in order
    std::vector<TokenEntry const*> identTokens;
    for (auto const& tok: tokens)
    {
        if (tok.token == Token::Identifier)
            identTokens.push_back(&tok);
    }

    // Match events to tokens and produce hints
    std::vector<InlayHint> hints;
    auto tokenIt = size_t { 0 };

    for (auto const& event: events)
    {
        // Find the next identifier token with matching name
        auto searchIt = tokenIt;
        while (searchIt < identTokens.size() && identTokens[searchIt]->literal != event.name)
            ++searchIt;

        if (searchIt >= identTokens.size())
            continue;

        auto const& tok = *identTokens[searchIt];
        auto const endCol = tok.range.begin.column + static_cast<int>(tok.literal.size());
        auto const hintPos = Position { .line = tok.range.begin.line, .character = endCol };

        // Advance token iterator past this match
        tokenIt = searchIt + 1;

        // Filter by range
        if (!positionInRange(hintPos, range))
            continue;

        auto hint = InlayHint {
            .position = hintPos,
            .label = event.label,
            .kind = InlayHintKind::Type,
        };

        if (event.kind == HintEvent::Kind::ReturnType)
            hint.paddingLeft = true;

        hints.push_back(std::move(hint));
    }

    return hints;
}

} // namespace endo::lsp
