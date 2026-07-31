#pragma once

#include "board/board.hpp"
#include <string>

namespace ah {

struct NnueNet {
  bool loaded = false;
  bool load(const std::string& path);
  Value evaluate(const Position& pos) const;
};

NnueNet& nnue();
bool nnue_ready();
bool load_nnue(const std::string& path);

} // namespace ah
