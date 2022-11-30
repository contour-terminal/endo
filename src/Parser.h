#pragma once

#include "AST.h"
#include "Lexer.h"

#include <memory>
#include <vector>
#include <string>

namespace contrair
{

class Parser
{
  public:
    explicit Parser(std::unique_ptr<Source> source);

    ast::CompoundStmt parse();

  private:
    void call();
    void callPpipeline();

    Lexer _lexer;
};

}
