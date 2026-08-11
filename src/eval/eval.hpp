#pragma once

#include "board/board.hpp"
#include "nnue/nnue.hpp"

namespace ah {

Value classical_evaluate(const Position& pos);
Value evaluate(const Position& pos);
Value evaluate(const Position& pos, const NnueAccumulator* acc);

}
