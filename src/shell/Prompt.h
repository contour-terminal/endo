// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

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

class Prompt
{
  public:
    Prompt();

    [[nodiscard]] bool ready() const;

    std::string read();

  private:
    std::istream& _input; // NOLINT
    std::string _prompt = "> ";
    std::string _buffer;
    bool _complete = false;

    Grid _grid;
};
