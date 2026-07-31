#pragma once

#include "board/board.hpp"
#include "search/search.hpp"

namespace ah {
void uci_loop();
Move parse_move(const Position& pos, const std::string& token);
}
