// SPDX-License-Identifier: Apache-2.0
#include <shell/Prompt.h>

#include <iostream>

Prompt::Prompt(): _input(std::cin)
{
}

bool Prompt::ready() const
{
    return _input.good();
}

std::string Prompt::read()
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
