// SPDX-License-Identifier: Apache-2.0
module;
#include <iostream>
#include <string>
#include <vector>

export module Prompt;

struct GridCell
{
    std::u32string graphemeCluster;
    int width = 0; // either 0, 1, or 2 (number of columns)

    [[nodiscard]] std::string toUtf8() const;
};

struct GridLine
{
    std::vector<GridCell> columns;

    [[nodiscard]] std::string toUtf8() const;

    [[nodiscard]] GridCell& cellAt(size_t index);
    [[nodiscard]] GridCell const& cellAt(size_t index) const;
};

struct Grid
{
    std::vector<GridLine> lines;

    [[nodiscard]] GridLine& lineAt(size_t index);
    [[nodiscard]] GridLine const& lineAt(size_t index) const;
};

export class Prompt
{
  public:
    Prompt(): _input(std::cin) {}

    [[nodiscard]] bool ready() const { return _input.good(); }

    std::string read()
    {
        while (ready() && !_complete)
        {
            std::cout << _prompt;
            std::getline(_input, _buffer);
            _complete = true; // keep it simple for now
        }

        auto line = std::move(_buffer);
        _buffer = {};
        _complete = false;
        return line;
    }

  private:
    std::istream& _input; // NOLINT
    std::string _prompt = "> ";
    std::string _buffer;
    bool _complete = false;

    Grid _grid;
};
