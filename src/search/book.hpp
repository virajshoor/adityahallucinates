#pragma once

#include "types.hpp"
#include "board/board.hpp"
#include <vector>

namespace ah {

// Very small weighted opening book for the first few plies.
// Returns MOVE_NONE if no book move.
Move probe_book(const Position& pos);

} // namespace ah
