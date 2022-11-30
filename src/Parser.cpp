#include "Parser.h"

using namespace std;

namespace contrair
{

Parser::Parser(std::unique_ptr<Source> source): _lexer{ std::move(source) }
{
}

ast::CompoundStmt Parser::parse()
{
    call();
    while (_lexer.currentToken() == Token::Semicolon)
        call();

    return {};
}

}
